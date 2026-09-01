// virthub/src/klnk/klnk-daemon/src/uffd_batch.rs

//! Batch processing of userfaultfd events to reduce syscall overhead.
//!
//! This module provides a `UffdBatchProcessor` that polls the userfaultfd
//! file descriptor and processes multiple page faults in a single batch
//! iteration, reducing the number of syscalls and context switches.
//!
//! ## Variable‑Sized Coherence & Lazy Self‑Invalidation
//!
//! With the introduction of **variable‑sized coherence domains**, the underlying
//! `UffdHandler` no longer aligns fault addresses to a fixed 2 MB boundary.
//! Instead, it looks up the exact base address that was registered in the
//! control plane (`lookup_page_state`).  This allows a single 2 MB huge page
//! to be divided into multiple independently‑tracked blocks (e.g., 4 KB, 128 KB,
//! or any size specified during region registration).
//!
//! Additionally, the handler implements **lazy self‑invalidation**: before using
//! a locally cached page, it compares the local version against the owner’s
//! metadata.  If the version has changed, the page is invalidated and re‑fetched.
//! This eliminates writer‑side CPU overhead for invalidation messages and scales
//! efficiently with many readers.

use klnk_uffd::handler::UffdHandler;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;
use thiserror::Error;
use tracing::{error, info, trace};

#[derive(Debug, Error)]
pub enum UffdBatchError {
    #[error("UFFD handler error: {0}")]
    HandlerError(#[from] klnk_uffd::handler::UffdHandlerError),
}

/// Configuration for batch fault processing.
#[derive(Debug, Clone, Copy)]
pub struct UffdBatchConfig {
    /// Maximum number of faults to process per batch iteration.
    pub max_batch_size: usize,
    /// Poll timeout in milliseconds (0 = non‑blocking).
    pub poll_timeout_ms: i32,
    /// Minimum sleep time between iterations when no events are available.
    pub idle_sleep_ms: u64,
}

impl Default for UffdBatchConfig {
    fn default() -> Self {
        Self {
            max_batch_size: 64,
            poll_timeout_ms: 0, // non‑blocking
            idle_sleep_ms: 1,
        }
    }
}

#[derive(Debug, Default)]
pub struct BatchStats {
    pub total_batches: AtomicU64,
    pub total_faults_processed: AtomicU64,
    pub avg_batch_size: AtomicU64,
    pub last_batch_duration_ms: AtomicU64,
}

impl BatchStats {
    pub fn record_batch(&self, faults: usize, duration_ms: u64) {
        self.total_batches.fetch_add(1, Ordering::Relaxed);
        self.total_faults_processed.fetch_add(faults as u64, Ordering::Relaxed);
        // Simple exponential moving average for batch size
        let old_avg = self.avg_batch_size.load(Ordering::Relaxed);
        let new_avg = if old_avg == 0 {
            faults as u64
        } else {
            (old_avg * 7 + faults as u64) / 8
        };
        self.avg_batch_size.store(new_avg, Ordering::Relaxed);
        self.last_batch_duration_ms.store(duration_ms, Ordering::Relaxed);
    }
}

/// Handles batch processing of userfaultfd events.
pub struct UffdBatchProcessor {
    handler: Arc<UffdHandler>,
    config: UffdBatchConfig,
    stats: Arc<BatchStats>,
}

impl UffdBatchProcessor {
    /// Creates a new batch processor with the given handler and configuration.
    /// The lazy‑invalidation behaviour is now managed internally by the handler
    /// via the control plane's version checks; no explicit flag is needed here.
    pub fn new(handler: Arc<UffdHandler>, config: UffdBatchConfig) -> Self {
        Self {
            handler,
            config,
            stats: Arc::new(BatchStats::default()),
        }
    }

    /// Processes a single batch of faults by polling up to `max_batch_size`
    /// events and letting the handler spawn async tasks for each.
    ///
    /// Returns the number of faults processed (events that were read and
    /// dispatched to async handlers).
    pub fn process_batch(&self) -> Result<usize, UffdBatchError> {
        let start = std::time::Instant::now();
        let mut processed = 0;
        let max_batch = self.config.max_batch_size;

        // Poll and dispatch events until no more events or batch limit reached.
        while processed < max_batch {
            match self.handler.poll_and_handle(self.config.poll_timeout_ms) {
                Ok(true) => {
                    processed += 1;
                }
                Ok(false) => {
                    // No more events; break out of the inner loop.
                    break;
                }
                Err(e) => {
                    // If the error is recoverable (e.g., EAGAIN), we could continue,
                    // but we treat any error as a failure.
                    return Err(UffdBatchError::HandlerError(e));
                }
            }
        }

        let elapsed_ms = start.elapsed().as_millis() as u64;
        self.stats.record_batch(processed, elapsed_ms);

        Ok(processed)
    }

    /// Runs a continuous loop processing batches indefinitely.
    ///
    /// This is intended to be spawned as a background task.
    pub async fn run_loop(&self) {
        info!(
            "UFFD batch processor started (max_batch={})",
            self.config.max_batch_size
        );

        loop {
            match self.process_batch() {
                Ok(count) => {
                    if count == 0 {
                        // No events; sleep briefly to avoid busy‑waiting.
                        tokio::time::sleep(Duration::from_millis(self.config.idle_sleep_ms)).await;
                    } else {
                        trace!("Processed {} UFFD faults in batch", count);
                    }
                }
                Err(e) => {
                    error!("UFFD batch processing error: {}", e);
                    // Sleep to avoid tight loop on persistent errors.
                    tokio::time::sleep(Duration::from_millis(self.config.idle_sleep_ms * 10)).await;
                }
            }
        }
    }

    /// Returns the current configuration (for testing).
    #[cfg(test)]
    pub fn config(&self) -> &UffdBatchConfig {
        &self.config
    }

    /// Returns a snapshot of batch statistics.
    #[allow(dead_code)]
    pub fn get_stats(&self) -> BatchStatsSnapshot {
        BatchStatsSnapshot {
            total_batches: self.stats.total_batches.load(Ordering::Relaxed),
            total_faults_processed: self.stats.total_faults_processed.load(Ordering::Relaxed),
            avg_batch_size: self.stats.avg_batch_size.load(Ordering::Relaxed),
            last_batch_duration_ms: self.stats.last_batch_duration_ms.load(Ordering::Relaxed),
        }
    }
}

#[derive(Debug, Clone)]
#[allow(dead_code)]
pub struct BatchStatsSnapshot {
    pub total_batches: u64,
    pub total_faults_processed: u64,
    pub avg_batch_size: u64,
    pub last_batch_duration_ms: u64,
}

/// Spawns a background task that runs the batch processor loop.
#[cfg(test)]
pub fn spawn_batch_processor(
    handler: Arc<UffdHandler>,
    config: Option<UffdBatchConfig>,
) -> tokio::task::JoinHandle<()> {
    let config = config.unwrap_or_default();
    let processor = UffdBatchProcessor::new(handler, config);
    tokio::spawn(async move {
        processor.run_loop().await;
    })
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
    fn test_batch_processor_creation() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let handler = Arc::new(UffdHandler::new(cp, pool, rma).unwrap());
        let batch_config = UffdBatchConfig::default();
        let processor = UffdBatchProcessor::new(handler, batch_config);
        assert_eq!(processor.config().max_batch_size, 64);
    }

    #[tokio::test]
    async fn test_spawn_batch_processor() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let handler = Arc::new(UffdHandler::new(cp, pool, rma).unwrap());
        let handle = spawn_batch_processor(handler, None);
        tokio::time::sleep(Duration::from_millis(10)).await;
        handle.abort();
    }
}
