// virthub/src/klnk/librmashim/src/rq_refill.rs

//! Receive Queue refill manager.
//!
//! This module provides a thread‑safe manager that tracks the number of
//! outstanding Receive Work Requests (WRs) on a Queue Pair (QP) and posts
//! new WRs when the depth falls below a low‑watermark.
//!
//! Currently, only a software (mock) mode is implemented: no actual `ibv_post_recv`
//! is performed. When the `rdma-hardware` feature is enabled, a real implementation
//! can be added behind the same interface.
//!
//! The manager is safe to use from multiple threads.

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum RqRefillError {
    #[error("Invalid Ring Buffer configuration: capacity ({capacity}), watermark ({watermark})")]
    InvalidConfig { capacity: usize, watermark: usize },
}

/// A receive buffer descriptor pre-allocated for incoming RDMA messaging.
#[derive(Debug, Clone, Copy)]
pub struct ReceiveBufferSlot {
    /// Slot identifier index
    pub slot_id: usize,
    /// Base virtual address of the receive buffer
    pub buf_vaddr: u64,
    /// Length of the receive buffer in bytes
    pub buf_len: usize,
    /// Local Memory Key (`lkey`) for DMA permissions
    pub lkey: u32,
}

/// Configuration options for the Receive Queue auto-refiller.
#[derive(Debug, Clone)]
pub struct RqConfig {
    /// Maximum depth of the RQ ring buffer (e.g., 512 or 1024 slots)
    pub capacity: usize,
    /// Low-watermark threshold triggering automatic batch refill
    pub low_watermark: usize,
    /// Maximum number of WRs to post in a single `ibv_post_recv` batch
    pub batch_size: usize,
}

impl Default for RqConfig {
    fn default() -> Self {
        Self {
            capacity: 512,
            low_watermark: 128,
            batch_size: 64,
        }
    }
}

/// High-performance Receive Queue Manager tracking active work request depth and batch posting.
///
/// In the current software implementation, the manager only maintains an atomic counter
/// of outstanding receive work requests. It does **not** interact with real hardware.
/// This is sufficient for testing and for systems without RDMA.
#[derive(Debug)]
pub struct RqRefillManager {
    config: RqConfig,
    /// Atomic count of currently outstanding Receive Work Requests posted to kernel/hardware.
    outstanding_depth: AtomicUsize,
}

impl RqRefillManager {
    /// Creates a new `RqRefillManager` with specified queue depth parameters.
    ///
    /// The `raw_qp_ptr` parameter is ignored in software mode; it is kept for API compatibility
    /// with future hardware implementations.
    pub fn new(
        config: RqConfig,
        _raw_qp_ptr: Option<*mut std::ffi::c_void>,
    ) -> Result<Arc<Self>, RqRefillError> {
        if config.low_watermark >= config.capacity || config.capacity == 0 {
            return Err(RqRefillError::InvalidConfig {
                capacity: config.capacity,
                watermark: config.low_watermark,
            });
        }

        Ok(Arc::new(Self {
            config,
            outstanding_depth: AtomicUsize::new(0),
        }))
    }

    /// Evaluates whether the Receive Queue depth has dropped below the low-watermark threshold.
    pub fn needs_refill(&self) -> bool {
        let current = self.outstanding_depth.load(Ordering::Relaxed);
        current < self.config.low_watermark
    }

    /// Returns the number of new Receive Work Requests needed to bring RQ to full capacity.
    pub fn refill_deficit(&self) -> usize {
        let current = self.outstanding_depth.load(Ordering::Relaxed);
        if current < self.config.capacity {
            (self.config.capacity - current).min(self.config.batch_size)
        } else {
            0
        }
    }

    /// Posts a batch of Receive Work Requests.
    ///
    /// In software mode, this simply increments the outstanding depth counter
    /// by the number of buffers (limited to `batch_size`). No actual posting occurs.
    pub fn post_refill_batch(
        &self,
        buffers: &[ReceiveBufferSlot],
    ) -> Result<usize, RqRefillError> {
        if buffers.is_empty() {
            return Ok(0);
        }

        let batch_len = buffers.len().min(self.config.batch_size);
        self.outstanding_depth.fetch_add(batch_len, Ordering::AcqRel);
        Ok(batch_len)
    }

    /// Decrements the outstanding depth counter when a Work Completion (WC) is popped from CQ.
    pub fn on_work_completion(&self, count: usize) {
        let current = self.outstanding_depth.load(Ordering::Relaxed);
        let new_val = current.saturating_sub(count);
        self.outstanding_depth.store(new_val, Ordering::Release);
    }

    /// Returns the current count of active Receive Work Requests in hardware.
    pub fn current_depth(&self) -> usize {
        self.outstanding_depth.load(Ordering::Relaxed)
    }

    /// Returns configured capacity.
    pub fn capacity(&self) -> usize {
        self.config.capacity
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_rq_refill_thresholds() {
        let config = RqConfig {
            capacity: 100,
            low_watermark: 30,
            batch_size: 32,
        };

        let mgr = RqRefillManager::new(config, None).unwrap();

        // Initial state: depth is 0, needs refill
        assert!(mgr.needs_refill());
        assert_eq!(mgr.refill_deficit(), 32); // Capped by batch_size

        // Create mock buffers
        let buffers: Vec<ReceiveBufferSlot> = (0..32)
            .map(|i| ReceiveBufferSlot {
                slot_id: i,
                buf_vaddr: 0x1000 + (i * 4096) as u64,
                buf_len: 4096,
                lkey: 0x1234,
            })
            .collect();

        // Post first batch (software mode)
        let posted = mgr.post_refill_batch(&buffers).unwrap();
        assert_eq!(posted, 32);
        assert_eq!(mgr.current_depth(), 32);

        // Depth 32 > low_watermark (30) => does not need refill yet
        assert!(!mgr.needs_refill());

        // Process 5 work completions
        mgr.on_work_completion(5);
        assert_eq!(mgr.current_depth(), 27);

        // Depth 27 < low_watermark (30) => needs refill again
        assert!(mgr.needs_refill());
    }

    #[test]
    fn test_invalid_config() {
        let config = RqConfig {
            capacity: 50,
            low_watermark: 50, // Invalid: watermark == capacity
            batch_size: 10,
        };

        let res = RqRefillManager::new(config, None);
        assert!(matches!(res, Err(RqRefillError::InvalidConfig { .. })));
    }
}
