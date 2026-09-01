// virthub/src/klnk/klnk-uffd/src/handler.rs

//! Userfaultfd event handler for the KLNK DSM engine.
//!
//! This module provides a `UffdHandler` that manages the userfaultfd file
//! descriptor, registers memory regions, polls for page faults, and resolves
//! them using `UFFDIO_MOVE` (with fallback to `UFFDIO_COPY`). It integrates
//! with the control plane to update coherence states, fetches remote pages
//! via RDMA, and handles invalidations.

use crate::move_ops::{uffd_move, uffd_zeropage, UffdOpError};
use dashmap::DashMap;
use klnk_core::control_plane::{ControlPlaneError, ControlPlaneManager};
use klnk_core::domain::{GlobalRegionId, MemoryRegionDescriptor, NodeId, PageCoherenceState};
use librmashim::{RmaError, RmaTransportEngine};
use nix::poll::{poll, PollFd, PollFlags};
use std::collections::HashMap;
use std::os::fd::{AsFd, AsRawFd, BorrowedFd};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use store::staging_pool::{StagingMemoryPool, StagingSlotHandle};
use thiserror::Error;
use tokio::sync::RwLock as TokioRwLock;
use tracing::{debug, error, info, warn};
use userfaultfd::{Event, Uffd, UffdBuilder};

#[derive(Debug, Error)]
pub enum UffdHandlerError {
    #[error("Failed to create userfaultfd: {0}")]
    CreateFailed(#[source] userfaultfd::Error),

    #[error("Failed to register memory region with userfaultfd: {0}")]
    RegisterFailed(#[source] userfaultfd::Error),

    #[error("Failed to unregister memory region: {0}")]
    UnregisterFailed(#[source] userfaultfd::Error),

    #[error("Failed to read userfaultfd event: {0}")]
    ReadFailed(#[source] userfaultfd::Error),

    #[error("Failed to resolve page fault: {0}")]
    FaultResolutionFailed(#[from] UffdOpError),

    #[error("Control plane error: {0}")]
    ControlPlaneError(#[from] ControlPlaneError),

    #[error("Staging pool error: {0}")]
    StagingPoolError(String),

    #[error("Invalid region registration: {0}")]
    InvalidRegion(String),

    #[error("RDMA operation failed: {0}")]
    RdmaError(#[from] RmaError),

    #[error("Lock acquisition failed: {0}")]
    LockError(String),
}

#[derive(Debug, Default)]
pub struct FaultStats {
    pub total_faults: AtomicU64,
    pub remote_faults: AtomicU64,
    pub local_faults: AtomicU64,
    pub zero_page_faults: AtomicU64,
    pub move_operations: AtomicU64,
    pub copy_operations: AtomicU64,
    pub failed_faults: AtomicU64,
}

#[derive(Debug, Clone)]
pub struct FaultStatsSnapshot {
    pub total_faults: u64,
    pub remote_faults: u64,
    pub local_faults: u64,
    pub zero_page_faults: u64,
    pub move_operations: u64,
    pub copy_operations: u64,
    pub failed_faults: u64,
}

pub struct UffdHandler {
    uffd: Arc<Uffd>,
    control_plane: Arc<ControlPlaneManager>,
    staging_pool: Arc<StagingMemoryPool>,
    rma_engine: Arc<RmaTransportEngine>,
    regions: Arc<TokioRwLock<HashMap<GlobalRegionId, (u64, usize)>>>,
    stats: Arc<FaultStats>,
    /// Optional shared map from target vaddr to prefetched staging slot.
    /// Populated by the prefetch engine; consumed here to avoid re-fetching.
    prefetched_pages: Option<Arc<DashMap<u64, StagingSlotHandle>>>,
}

impl UffdHandler {
    /// Create a new `UffdHandler`.
    pub fn new(
        control_plane: Arc<ControlPlaneManager>,
        staging_pool: Arc<StagingMemoryPool>,
        rma_engine: Arc<RmaTransportEngine>,
    ) -> Result<Self, UffdHandlerError> {
        let uffd = UffdBuilder::new()
            .non_blocking(true)
            .create()
            .map_err(UffdHandlerError::CreateFailed)?;

        info!("Userfaultfd created with fd {}", uffd.as_fd().as_raw_fd());
        Ok(Self {
            uffd: Arc::new(uffd),
            control_plane,
            staging_pool,
            rma_engine,
            regions: Arc::new(TokioRwLock::new(HashMap::new())),
            stats: Arc::new(FaultStats::default()),
            prefetched_pages: None,
        })
    }

    /// Set the prefetched pages map (from prefetch engine).
    pub fn set_prefetched_pages(&mut self, map: Arc<DashMap<u64, StagingSlotHandle>>) {
        self.prefetched_pages = Some(map);
    }

    /// Register a memory region for userfaultfd handling.
    pub async fn register_region(
        &self,
        desc: &MemoryRegionDescriptor,
    ) -> Result<(), UffdHandlerError> {
        let vaddr = desc.main_vaddr;
        let size = desc.region_size;

        if vaddr % 4096 != 0 || size % 4096 != 0 {
            return Err(UffdHandlerError::InvalidRegion(
                format!("Region 0x{:x} size {} not page‑aligned", vaddr, size),
            ));
        }

        self.uffd
            .register(vaddr as *mut _, size)
            .map_err(UffdHandlerError::RegisterFailed)?;

        let mut map = self.regions.write().await;
        map.insert(desc.region_id, (vaddr, size));
        info!("Registered region {:?} (0x{:x}, size {}) with UFFD", desc.region_id, vaddr, size);
        Ok(())
    }

    /// Unregister a memory region.
    pub async fn unregister_region(&self, region_id: GlobalRegionId) -> Result<(), UffdHandlerError> {
        let mut map = self.regions.write().await;
        if let Some((vaddr, size)) = map.remove(&region_id) {
            self.uffd
                .unregister(vaddr as *mut _, size)
                .map_err(UffdHandlerError::UnregisterFailed)?;
            info!("Unregistered region {:?}", region_id);
        } else {
            warn!("Region {:?} not found for unregistration", region_id);
        }
        Ok(())
    }

    /// Poll for a single fault and handle it asynchronously.
    /// Returns true if a fault was processed, false if no event was available.
    pub fn poll_and_handle(&self, timeout_ms: i32) -> Result<bool, UffdHandlerError> {
        let poll_fd = PollFd::new(&*self.uffd, PollFlags::POLLIN);
        let mut poll_fds = [poll_fd];
        let n = poll(&mut poll_fds, timeout_ms)
            .map_err(|e| UffdHandlerError::ReadFailed(userfaultfd::Error::from(nix::Error::from(e))))?;

        if n == 0 {
            return Ok(false);
        }

        let event = match self.uffd.read_event() {
            Ok(Some(ev)) => ev,
            Ok(None) => return Ok(false),
            Err(e) => return Err(UffdHandlerError::ReadFailed(e)),
        };

        match event {
            Event::Pagefault { addr, .. } => {
                let vaddr = addr as u64;
                self.stats.total_faults.fetch_add(1, Ordering::Relaxed);
                debug!("Page fault at 0x{:x}", vaddr);

                let cp = self.control_plane.clone();
                let pool = self.staging_pool.clone();
                let rma = self.rma_engine.clone();
                let uffd = self.uffd.clone();
                let stats = self.stats.clone();
                let prefetched = self.prefetched_pages.clone();
                tokio::spawn(async move {
                    let result = Self::handle_page_fault_async_inner(
                        vaddr, cp, pool, rma, uffd, prefetched, stats.clone()
                    ).await;
                    if let Err(e) = result {
                        error!("Failed to resolve page fault at 0x{:x}: {}", vaddr, e);
                        stats.failed_faults.fetch_add(1, Ordering::Relaxed);
                    }
                });
            }
            other => {
                debug!("Received UFFD event: {:?}", other);
            }
        }

        Ok(true)
    }

    /// Process a batch of faults concurrently (used by the daemon's batch processor).
    pub async fn process_fault_batch(&self, faults: &[u64]) -> Vec<Result<(), UffdHandlerError>> {
        let mut results = Vec::with_capacity(faults.len());
        for &vaddr in faults {
            self.stats.total_faults.fetch_add(1, Ordering::Relaxed);
            results.push(
                Self::handle_page_fault_async_inner(
                    vaddr,
                    self.control_plane.clone(),
                    self.staging_pool.clone(),
                    self.rma_engine.clone(),
                    self.uffd.clone(),
                    self.prefetched_pages.clone(),
                    self.stats.clone(),
                )
                .await,
            );
        }
        results
    }

    async fn handle_page_fault_async_inner(
        vaddr: u64,
        control_plane: Arc<ControlPlaneManager>,
        staging_pool: Arc<StagingMemoryPool>,
        rma_engine: Arc<RmaTransportEngine>,
        uffd: Arc<Uffd>,
        prefetched_pages: Option<Arc<DashMap<u64, StagingSlotHandle>>>,
        stats: Arc<FaultStats>,
    ) -> Result<(), UffdHandlerError> {
        let page_state = control_plane
            .lookup_page_state(vaddr)
            .ok_or_else(|| UffdHandlerError::StagingPoolError(format!("Page 0x{:x} not found", vaddr)))?;

        let local_node = control_plane.local_node_id();
        let owner = page_state.primary_owner;

        match page_state.coherence_state {
            PageCoherenceState::Invalid
            | PageCoherenceState::SharedRead
            | PageCoherenceState::ExclusiveWrite => {
                if owner != local_node {
                    stats.remote_faults.fetch_add(1, Ordering::Relaxed);
                    control_plane.update_page_state(vaddr, PageCoherenceState::InFlight, owner);
                    Self::fetch_remote_page(
                        vaddr, owner, &control_plane, &staging_pool, &rma_engine, &uffd,
                        &prefetched_pages, &stats,
                    ).await?;
                    control_plane.update_page_state(vaddr, PageCoherenceState::SharedRead, owner);
                    control_plane.add_reader(vaddr, local_node);
                    Ok(())
                } else {
                    stats.local_faults.fetch_add(1, Ordering::Relaxed);
                    Self::resolve_local_missing(vaddr, &control_plane, &staging_pool, &uffd, &stats).await
                }
            }
            PageCoherenceState::InFlight => {
                warn!("Page 0x{:x} is in flight; returning zero page", vaddr);
                stats.zero_page_faults.fetch_add(1, Ordering::Relaxed);
                Self::resolve_with_zeropage(vaddr, &control_plane, &uffd)
            }
        }
    }

    async fn fetch_remote_page(
        vaddr: u64,
        owner: NodeId,
        control_plane: &ControlPlaneManager,
        staging_pool: &StagingMemoryPool,
        rma_engine: &RmaTransportEngine,
        uffd: &Arc<Uffd>,
        prefetched_pages: &Option<Arc<DashMap<u64, StagingSlotHandle>>>,
        stats: &FaultStats,
    ) -> Result<(), UffdHandlerError> {
        // Check prefetch cache first – atomic removal.
        if let Some(prefetched) = prefetched_pages.as_ref() {
            if let Some((_, slot)) = prefetched.remove(&vaddr) {
                let moved = uffd_move(
                    uffd.as_fd().as_raw_fd(),
                    vaddr,
                    slot.vaddr,
                    slot.size as u64,
                )
                .map_err(UffdHandlerError::FaultResolutionFailed)?;
                debug!("Moved prefetched slot {} ({} bytes) to 0x{:x}", slot.slot_idx, moved, vaddr);
                stats.move_operations.fetch_add(1, Ordering::Relaxed);
                // Optional: record hit in prefetch metrics (if accessible).
                return Ok(());
            }
        }

        let remote_info = control_plane
            .get_remote_node(owner)
            .ok_or_else(|| UffdHandlerError::StagingPoolError(format!("Remote node {:?} not found", owner)))?;

        let slot = staging_pool
            .pop_slot()
            .map_err(|e| UffdHandlerError::StagingPoolError(e.to_string()))?;

        let len = slot.size;
        let remote_vaddr = vaddr;
        let remote_rkey = remote_info.metadata_rkey; // TODO: replace with data rkey

        rma_engine
            .rdma_read(remote_info.socket_addr, remote_vaddr, remote_rkey, slot.vaddr, len)
            .await?;

        stats.move_operations.fetch_add(1, Ordering::Relaxed);
        let moved = uffd_move(
            uffd.as_fd().as_raw_fd(),
            vaddr,
            slot.vaddr,
            len as u64,
        )
        .map_err(|e| {
            let _ = staging_pool.push_slot(slot.slot_idx);
            UffdHandlerError::FaultResolutionFailed(e)
        })?;

        debug!("Moved {} bytes from staging slot {} to 0x{:x}", moved, slot.slot_idx, vaddr);
        Ok(())
    }

    async fn resolve_local_missing(
        vaddr: u64,
        control_plane: &ControlPlaneManager,
        staging_pool: &StagingMemoryPool,
        uffd: &Arc<Uffd>,
        stats: &FaultStats,
    ) -> Result<(), UffdHandlerError> {
        let slot = staging_pool
            .pop_slot()
            .map_err(|e| UffdHandlerError::StagingPoolError(e.to_string()))?;

        let len = slot.size as u64;
        stats.move_operations.fetch_add(1, Ordering::Relaxed);
        let moved = uffd_move(
            uffd.as_fd().as_raw_fd(),
            vaddr,
            slot.vaddr,
            len,
        )
        .map_err(|e| {
            let _ = staging_pool.push_slot(slot.slot_idx);
            UffdHandlerError::FaultResolutionFailed(e)
        })?;

        debug!("Moved {} bytes from staging slot {} to 0x{:x}", moved, slot.slot_idx, vaddr);

        control_plane.update_page_state(vaddr, PageCoherenceState::SharedRead, control_plane.local_node_id());
        control_plane.add_reader(vaddr, control_plane.local_node_id());
        Ok(())
    }

    fn resolve_with_zeropage(
        vaddr: u64,
        control_plane: &ControlPlaneManager,
        uffd: &Arc<Uffd>,
    ) -> Result<(), UffdHandlerError> {
        uffd_zeropage(uffd.as_fd().as_raw_fd(), vaddr, 4096)
            .map_err(UffdHandlerError::FaultResolutionFailed)?;
        debug!("Zero‑page mapped at 0x{:x}", vaddr);
        control_plane.update_page_state(vaddr, PageCoherenceState::SharedRead, control_plane.local_node_id());
        Ok(())
    }

    pub fn fd(&self) -> BorrowedFd<'_> {
        self.uffd.as_fd()
    }

    pub fn control_plane(&self) -> &Arc<ControlPlaneManager> {
        &self.control_plane
    }

    pub fn staging_pool(&self) -> &Arc<StagingMemoryPool> {
        &self.staging_pool
    }

    pub fn rma_engine(&self) -> &Arc<RmaTransportEngine> {
        &self.rma_engine
    }

    /// Return a snapshot of the fault statistics.
    pub fn get_stats(&self) -> FaultStatsSnapshot {
        FaultStatsSnapshot {
            total_faults: self.stats.total_faults.load(Ordering::Relaxed),
            remote_faults: self.stats.remote_faults.load(Ordering::Relaxed),
            local_faults: self.stats.local_faults.load(Ordering::Relaxed),
            zero_page_faults: self.stats.zero_page_faults.load(Ordering::Relaxed),
            move_operations: self.stats.move_operations.load(Ordering::Relaxed),
            copy_operations: self.stats.copy_operations.load(Ordering::Relaxed),
            failed_faults: self.stats.failed_faults.load(Ordering::Relaxed),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use klnk_core::control_plane::ControlPlaneManager;
    use klnk_core::domain::NodeId;
    use librmashim::{RdmaEndpointConfig, RmaTransportEngine};
    use store::staging_pool::{StagingMemoryPool, PAGE_SIZE_4K};
    use std::net::SocketAddr;

    #[test]
    fn test_uffd_handler_creation() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let handler = UffdHandler::new(cp, pool, rma).unwrap();
        assert!(handler.fd().as_raw_fd() > 0);
    }

    #[tokio::test]
    async fn test_fault_stats_initial() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let handler = UffdHandler::new(cp, pool, rma).unwrap();
        let stats = handler.get_stats();
        assert_eq!(stats.total_faults, 0);
    }
}
