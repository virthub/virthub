// virthub/src/klnk/klnk-daemon/src/prefetch.rs

//! Prefetch engine for speculative RDMA reads.
//!
//! This module consumes `PrefetchRecommendation` events from the eBPF stride
//! detector and issues one‑sided RDMA reads to fetch predicted memory pages
//! into the staging pool before they are actually faulted. This hides latency
//! and reduces CPU interruptions.
//!
//! ## Performance Optimizations
//!
//! - Prefetched pages are tracked in a `DashMap` for lock‑free access.
//! - Each page is fetched in its own task, bounded by a semaphore, allowing
//!   multiple RDMA reads to be in flight concurrently.
//! - A `prefetch_hits` counter records how many prefetched pages are actually
//!   consumed by the UFFD handler.
//! - The engine exposes `take_prefetched_slot` to atomically remove a slot when
//!   it is consumed, avoiding a separate get/remove race.

use dashmap::DashMap;
use klnk_core::control_plane::ControlPlaneManager;
use klnk_core::domain::PageCoherenceState;
use klnk_ebpf::PrefetchRecommendation;
use librmashim::RmaTransportEngine;
use store::staging_pool::{StagingMemoryPool, StagingSlotHandle};
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use tokio::sync::{mpsc, Semaphore};
use tokio::task::JoinHandle;
use tracing::{debug, info, warn};

/// Configuration for the prefetch engine.
#[derive(Debug, Clone)]
pub struct PrefetchConfig {
    /// Maximum number of concurrent in‑flight prefetch requests.
    pub max_concurrent_requests: usize,
    /// Maximum number of pages to prefetch per recommendation.
    pub max_prefetch_count: usize,
    /// Whether to enable prefetching (can be disabled at runtime).
    pub enabled: bool,
}

impl Default for PrefetchConfig {
    fn default() -> Self {
        Self {
            max_concurrent_requests: 16,
            max_prefetch_count: 8,
            enabled: true,
        }
    }
}

/// Metrics for prefetch engine observability.
#[derive(Debug, Default)]
pub struct PrefetchMetrics {
    pub total_recommendations: AtomicU64,
    pub total_pages_prefetched: AtomicU64,
    pub total_bytes_prefetched: AtomicU64,
    pub total_prefetch_errors: AtomicU64,
    /// Number of prefetched pages that were actually consumed by the UFFD handler.
    /// This field is currently unused until UFFD integration is completed.
    #[allow(dead_code)]
    pub prefetch_hits: AtomicU64,
}

/// The prefetch engine that runs as a background task.
pub struct PrefetchEngine {
    config: PrefetchConfig,
    control_plane: Arc<ControlPlaneManager>,
    rma_engine: Arc<RmaTransportEngine>,
    staging_pool: Arc<StagingMemoryPool>,
    recommendation_rx: Option<mpsc::UnboundedReceiver<PrefetchRecommendation>>,
    semaphore: Arc<Semaphore>,
    join_handle: Option<JoinHandle<()>>,
    metrics: Arc<PrefetchMetrics>,
    /// Mapping from target virtual address to the staging slot holding the prefetched data.
    /// The UFFD handler can later remove entries atomically via `take_prefetched_slot`.
    prefetched_pages: Arc<DashMap<u64, StagingSlotHandle>>,
}

impl PrefetchEngine {
    /// Creates a new prefetch engine.
    pub fn new(
        config: PrefetchConfig,
        control_plane: Arc<ControlPlaneManager>,
        rma_engine: Arc<RmaTransportEngine>,
        staging_pool: Arc<StagingMemoryPool>,
        recommendation_rx: mpsc::UnboundedReceiver<PrefetchRecommendation>,
    ) -> Self {
        Self {
            config: config.clone(),
            control_plane,
            rma_engine,
            staging_pool,
            recommendation_rx: Some(recommendation_rx),
            semaphore: Arc::new(Semaphore::new(config.max_concurrent_requests)),
            join_handle: None,
            metrics: Arc::new(PrefetchMetrics::default()),
            prefetched_pages: Arc::new(DashMap::new()),
        }
    }

    /// Start the prefetch engine as a background task.
    pub fn start(&mut self) {
        if !self.config.enabled {
            info!("Prefetch engine is disabled.");
            return;
        }

        let config = self.config.clone();
        let control_plane = self.control_plane.clone();
        let rma_engine = self.rma_engine.clone();
        let staging_pool = self.staging_pool.clone();
        let mut rx = self.recommendation_rx.take().expect("prefetch engine already started");
        let semaphore = self.semaphore.clone();
        let metrics = self.metrics.clone();
        let prefetched_pages = self.prefetched_pages.clone();

        self.join_handle = Some(tokio::spawn(async move {
            info!(
                "Prefetch engine started (concurrency limit: {})",
                config.max_concurrent_requests
            );

            loop {
                tokio::select! {
                    rec = rx.recv() => {
                        match rec {
                            Some(recommendation) => {
                                metrics.total_recommendations.fetch_add(1, Ordering::Relaxed);
                                let cp = control_plane.clone();
                                let rma = rma_engine.clone();
                                let pool = staging_pool.clone();
                                let cfg = config.clone();
                                let metrics_clone = metrics.clone();
                                let prefetched = prefetched_pages.clone();
                                let sem = semaphore.clone();

                                tokio::spawn(async move {
                                    // Create a separate clone for error reporting after moving the first.
                                    let metrics_for_handle = metrics_clone.clone();
                                    if let Err(e) = Self::handle_recommendation(
                                        recommendation,
                                        cp,
                                        rma,
                                        pool,
                                        &cfg,
                                        metrics_for_handle,
                                        prefetched,
                                        sem,
                                    ).await {
                                        warn!("Prefetch failed: {}", e);
                                        metrics_clone.total_prefetch_errors.fetch_add(1, Ordering::Relaxed);
                                    }
                                });
                            }
                            None => {
                                debug!("Prefetch recommendation channel closed; shutting down.");
                                break;
                            }
                        }
                    }
                }
            }
            info!("Prefetch engine stopped.");
        }));
    }

    /// Handle a single prefetch recommendation by spawning one task per page.
    ///
    /// Concurrency is limited by the shared semaphore; each spawned task acquires
    /// a permit, ensuring no more than `max_concurrent_requests` RDMA reads happen
    /// simultaneously.
    async fn handle_recommendation(
        recommendation: PrefetchRecommendation,
        control_plane: Arc<ControlPlaneManager>,
        rma_engine: Arc<RmaTransportEngine>,
        staging_pool: Arc<StagingMemoryPool>,
        config: &PrefetchConfig,
        metrics: Arc<PrefetchMetrics>,
        prefetched_pages: Arc<DashMap<u64, StagingSlotHandle>>,
        semaphore: Arc<Semaphore>,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let local_node = control_plane.local_node_id();
        let count = recommendation.count.min(config.max_prefetch_count);

        let mut tasks = Vec::with_capacity(count);

        for i in 0..count {
            let vaddr = (recommendation.base_vaddr as i64
                + (i as i64 * recommendation.stride_bytes)) as u64;

            // Avoid prefetching the same page multiple times.
            if prefetched_pages.contains_key(&vaddr) {
                continue;
            }

            // Look up page state.
            let page_entry = match control_plane.lookup_page_state(vaddr) {
                Some(entry) => entry,
                None => continue,
            };

            // Skip pages that are already local or in flight.
            if page_entry.primary_owner == local_node {
                continue;
            }
            if page_entry.coherence_state == PageCoherenceState::InFlight {
                continue;
            }

            // Get remote node info.
            let remote_info = match control_plane.get_remote_node(page_entry.primary_owner) {
                Some(info) => info,
                None => continue,
            };

            // Acquire owned permit from Arc<Semaphore>.
            let permit = semaphore.clone().acquire_owned().await?;

            let rma = rma_engine.clone();
            let pool = staging_pool.clone();
            let metrics_clone = metrics.clone();
            let prefetched = prefetched_pages.clone();

            tasks.push(tokio::spawn(async move {
                let _permit = permit; // keep permit alive until task completes
                Self::prefetch_one_page(
                    vaddr,
                    page_entry.page_size,
                    remote_info.socket_addr,
                    remote_info.metadata_rkey,
                    &rma,
                    &pool,
                    &metrics_clone,
                    &prefetched,
                ).await;
            }));
        }

        // Wait for all page tasks to finish (or at least be spawned).
        for task in tasks {
            let _ = task.await;
        }

        Ok(())
    }

    /// Fetch a single page into the staging pool and record it.
    async fn prefetch_one_page(
        vaddr: u64,
        page_size: usize,
        peer_addr: SocketAddr,
        remote_rkey: u32,
        rma_engine: &RmaTransportEngine,
        staging_pool: &StagingMemoryPool,
        metrics: &PrefetchMetrics,
        prefetched_pages: &DashMap<u64, StagingSlotHandle>,
    ) {
        // Pop a staging slot.
        let slot = match staging_pool.pop_slot() {
            Ok(slot) => slot,
            Err(_) => return,
        };

        let len = slot.size.min(page_size);
        let local_vaddr = slot.vaddr;

        match rma_engine
            .rdma_read(peer_addr, vaddr, remote_rkey, local_vaddr, len)
            .await
        {
            Ok(()) => {
                prefetched_pages.insert(vaddr, slot);
                metrics.total_pages_prefetched.fetch_add(1, Ordering::Relaxed);
                metrics.total_bytes_prefetched.fetch_add(len as u64, Ordering::Relaxed);
                debug!("Prefetched page 0x{:x} into slot {}", vaddr, slot.slot_idx);
            }
            Err(e) => {
                warn!("RDMA read failed for 0x{:x}: {}", vaddr, e);
                let _ = staging_pool.push_slot(slot.slot_idx);
                metrics.total_prefetch_errors.fetch_add(1, Ordering::Relaxed);
            }
        }
    }

    /// Retrieve and remove a prefetched slot for a given virtual address.
    /// This is intended to be called by the UFFD handler when it consumes a
    /// prefetched page.
    #[allow(dead_code)]
    pub fn take_prefetched_slot(&self, vaddr: u64) -> Option<StagingSlotHandle> {
        self.prefetched_pages.remove(&vaddr).map(|(_, slot)| slot)
    }

    /// Increment the prefetch hit counter (called by UFFD handler when a
    /// prefetched slot is used).
    #[allow(dead_code)]
    pub fn record_prefetch_hit(&self) {
        self.metrics.prefetch_hits.fetch_add(1, Ordering::Relaxed);
    }

    /// Return a reference to the prefetch metrics.
    #[allow(dead_code)]
    pub fn metrics(&self) -> &Arc<PrefetchMetrics> {
        &self.metrics
    }

    /// Return a clone of the shared `DashMap` used for UFFD integration.
    /// This can be passed to the UFFD handler for lock‑free consumption.
    pub fn get_prefetched_pages(&self) -> Arc<DashMap<u64, StagingSlotHandle>> {
        self.prefetched_pages.clone()
    }
}

/// Create a prefetch engine with a default configuration and a channel pair.
pub fn create_prefetch_engine(
    control_plane: Arc<ControlPlaneManager>,
    rma_engine: Arc<RmaTransportEngine>,
    staging_pool: Arc<StagingMemoryPool>,
    config: PrefetchConfig,
) -> (PrefetchEngine, mpsc::UnboundedSender<PrefetchRecommendation>) {
    let (tx, rx) = mpsc::unbounded_channel();
    let engine = PrefetchEngine::new(config, control_plane, rma_engine, staging_pool, rx);
    (engine, tx)
}
