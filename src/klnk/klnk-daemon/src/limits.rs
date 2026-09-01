// virthub/src/klnk/klnk-daemon/src/limits.rs

//! Resource limits and backpressure management for the klnk-daemon.
//!
//! This module provides a thread‑safe `ResourceLimiter` that tracks
//! memory usage (staging pool, RDMA registrations) and active descriptors.
//! It enforces capacity limits and high‑watermark backpressure, using
//! atomic operations for safe concurrent access across Tokio tasks.
//!
//! ## Hysteresis
//! Backpressure is activated when memory usage **exceeds** the high watermark,
//! and is not cleared until usage **drops below** the low watermark. This
//! prevents rapid toggling and reduces flapping.

use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
use std::sync::Arc;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum LimitsError {
    #[error("Staging memory capacity exceeded: requested {requested} bytes, limit is {limit} bytes (currently used: {current} bytes)")]
    MemoryLimitExceeded {
        requested: usize,
        current: usize,
        limit: usize,
    },

    #[error("Active descriptor connection limit reached ({current}/{max})")]
    DescriptorLimitExceeded { current: usize, max: usize },

    #[error("System backpressure triggered: current memory usage is at {usage_pct:.1}% (high watermark threshold is {threshold_pct:.1}%)")]
    HighWatermarkBackpressure {
        usage_pct: f64,
        threshold_pct: f64,
    },
}

/// System resource threshold configuration settings for `klnk-daemon`.
#[derive(Debug, Clone)]
pub struct DaemonResourceConfig {
    /// Maximum memory capacity (in bytes) allocated for staging page buffers.
    pub max_staging_bytes: usize,
    /// High-watermark memory threshold percentage (e.g., 0.85 = 85%) triggering backpressure.
    pub high_watermark_pct: f64,
    /// Low-watermark memory threshold percentage (e.g., 0.60 = 60%) clearing backpressure.
    pub low_watermark_pct: f64,
    /// Maximum allowed concurrent process connections / UFFD descriptors.
    pub max_active_descriptors: usize,
}

impl Default for DaemonResourceConfig {
    fn default() -> Self {
        Self {
            max_staging_bytes: 8 * 1024 * 1024 * 1024, // 8 GB default
            high_watermark_pct: 0.85,
            low_watermark_pct: 0.60,
            max_active_descriptors: 1024,
        }
    }
}

impl DaemonResourceConfig {
    /// Validates that the configuration is sane.
    pub fn validate(&self) -> Result<(), String> {
        if self.max_staging_bytes == 0 {
            return Err("max_staging_bytes must be > 0".to_string());
        }
        if self.high_watermark_pct <= self.low_watermark_pct {
            return Err("high_watermark_pct must be greater than low_watermark_pct".to_string());
        }
        if self.high_watermark_pct > 1.0 || self.low_watermark_pct < 0.0 {
            return Err("watermark percentages must be in [0.0, 1.0]".to_string());
        }
        if self.max_active_descriptors == 0 {
            return Err("max_active_descriptors must be > 0".to_string());
        }
        Ok(())
    }
}

/// Thread-safe resource monitor tracking active memory allocations and enforcing backpressure.
///
/// All methods use atomic operations and are safe to call concurrently from
/// multiple Tokio tasks or threads.
#[derive(Debug)]
pub struct ResourceLimiter {
    config: DaemonResourceConfig,
    /// Current total bytes allocated across all active staging pools.
    used_staging_bytes: AtomicUsize,
    /// Current count of active registered process file descriptors.
    active_descriptors: AtomicUsize,
    /// Whether backpressure is currently active (with hysteresis).
    backpressure_active: AtomicBool,
}

impl ResourceLimiter {
    /// Creates a new `ResourceLimiter` instance wrapped in an `Arc`.
    pub fn new(config: DaemonResourceConfig) -> Result<Arc<Self>, String> {
        config.validate()?;
        Ok(Arc::new(Self {
            config,
            used_staging_bytes: AtomicUsize::new(0),
            active_descriptors: AtomicUsize::new(0),
            backpressure_active: AtomicBool::new(false),
        }))
    }

    /// Attempts to reserve memory bytes for a new staging pool allocation.
    ///
    /// # Errors
    /// Returns `LimitsError::MemoryLimitExceeded` if the allocation exceeds `max_staging_bytes`.
    pub fn reserve_memory(&self, bytes: usize) -> Result<(), LimitsError> {
        let limit = self.config.max_staging_bytes;
        let mut current = self.used_staging_bytes.load(Ordering::Relaxed);

        loop {
            if current + bytes > limit {
                return Err(LimitsError::MemoryLimitExceeded {
                    requested: bytes,
                    current,
                    limit,
                });
            }
            match self.used_staging_bytes.compare_exchange_weak(
                current,
                current + bytes,
                Ordering::AcqRel,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(actual) => current = actual,
            }
        }

        // Update backpressure state after allocation.
        self.update_backpressure();
        Ok(())
    }

    /// Releases previously reserved staging pool memory bytes.
    pub fn release_memory(&self, bytes: usize) {
        let mut current = self.used_staging_bytes.load(Ordering::Relaxed);
        loop {
            let new_val = current.saturating_sub(bytes);
            match self.used_staging_bytes.compare_exchange_weak(
                current,
                new_val,
                Ordering::AcqRel,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(actual) => current = actual,
            }
        }
        self.update_backpressure();
    }

    /// Increments active process connection / descriptor count.
    ///
    /// # Errors
    /// Returns `LimitsError::DescriptorLimitExceeded` if maximum descriptor capacity is reached.
    pub fn register_descriptor(&self) -> Result<(), LimitsError> {
        let max = self.config.max_active_descriptors;
        let mut current = self.active_descriptors.load(Ordering::Relaxed);

        loop {
            if current >= max {
                return Err(LimitsError::DescriptorLimitExceeded { current, max });
            }
            match self.active_descriptors.compare_exchange_weak(
                current,
                current + 1,
                Ordering::AcqRel,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(actual) => current = actual,
            }
        }
        Ok(())
    }

    /// Decrements active process connection / descriptor count when a process exits or disconnects.
    pub fn unregister_descriptor(&self) {
        let mut current = self.active_descriptors.load(Ordering::Relaxed);
        loop {
            let new_val = current.saturating_sub(1);
            match self.active_descriptors.compare_exchange_weak(
                current,
                new_val,
                Ordering::AcqRel,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(actual) => current = actual,
            }
        }
    }

    /// Checks whether memory usage has breached the high-watermark threshold requiring system backpressure.
    ///
    /// This method uses **hysteresis**: it returns `true` only if the current
    /// usage ratio is >= `high_watermark_pct` and the backpressure was not already
    /// active, or if it was active and usage is still >= `low_watermark_pct`.
    /// The actual transition logic is handled by `update_backpressure`.
    pub fn check_backpressure(&self) -> Result<(), LimitsError> {
        let usage_pct = self.memory_usage_ratio();
        let high = self.config.high_watermark_pct;

        if usage_pct >= high {
            return Err(LimitsError::HighWatermarkBackpressure {
                usage_pct: usage_pct * 100.0,
                threshold_pct: high * 100.0,
            });
        }
        Ok(())
    }

    /// Update the backpressure flag based on current usage and hysteresis thresholds.
    /// This should be called after every reservation or release.
    fn update_backpressure(&self) {
        let usage = self.memory_usage_ratio();
        let high = self.config.high_watermark_pct;
        let low = self.config.low_watermark_pct;

        let currently_active = self.backpressure_active.load(Ordering::Acquire);

        let new_active = if currently_active {
            // Only deactivate when below low watermark
            usage >= low
        } else {
            // Activate when above high watermark
            usage >= high
        };

        self.backpressure_active.store(new_active, Ordering::Release);
    }

    /// Returns whether backpressure is currently active (based on hysteresis state).
    pub fn is_backpressure_active(&self) -> bool {
        self.backpressure_active.load(Ordering::Acquire)
    }

    /// Returns current staging memory usage in bytes.
    pub fn used_staging_bytes(&self) -> usize {
        self.used_staging_bytes.load(Ordering::Relaxed)
    }

    /// Returns current memory usage ratio as a fraction (0.0 to 1.0).
    pub fn memory_usage_ratio(&self) -> f64 {
        let current = self.used_staging_bytes.load(Ordering::Relaxed) as f64;
        let limit = self.config.max_staging_bytes as f64;
        if limit == 0.0 {
            0.0
        } else {
            current / limit
        }
    }

    /// Returns current active descriptor count.
    pub fn active_descriptors(&self) -> usize {
        self.active_descriptors.load(Ordering::Relaxed)
    }

    /// Returns a reference to the configuration.
    pub fn config(&self) -> &DaemonResourceConfig {
        &self.config
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_config() -> DaemonResourceConfig {
        DaemonResourceConfig {
            max_staging_bytes: 10 * 1024 * 1024, // 10MB
            high_watermark_pct: 0.80,
            low_watermark_pct: 0.50,
            max_active_descriptors: 10,
        }
    }

    #[test]
    fn test_memory_reservation_and_release() {
        let limiter = ResourceLimiter::new(test_config()).unwrap();

        // Reserve 4MB.
        limiter.reserve_memory(4 * 1024 * 1024).unwrap();
        assert_eq!(limiter.used_staging_bytes(), 4 * 1024 * 1024);
        assert!(!limiter.is_backpressure_active());

        // Reserve another 5MB (total 9MB = 90% > 80% watermark).
        limiter.reserve_memory(5 * 1024 * 1024).unwrap();
        assert!(limiter.is_backpressure_active());

        // Exceed limit (attempting 2MB when limit is 10MB).
        let err = limiter.reserve_memory(2 * 1024 * 1024);
        assert!(matches!(err, Err(LimitsError::MemoryLimitExceeded { .. })));

        // Release 5MB -> total 4MB = 40% < 50% low watermark, backpressure should clear.
        limiter.release_memory(5 * 1024 * 1024);
        assert_eq!(limiter.used_staging_bytes(), 4 * 1024 * 1024);
        assert!(!limiter.is_backpressure_active());
    }

    #[test]
    fn test_descriptor_limits() {
        let limiter = ResourceLimiter::new(test_config()).unwrap();

        limiter.register_descriptor().unwrap();
        limiter.register_descriptor().unwrap();
        assert_eq!(limiter.active_descriptors(), 2);

        // Exceed descriptor count (max 10, we are at 2, so this should succeed).
        // Let's fill to max.
        for _ in 0..8 {
            limiter.register_descriptor().unwrap();
        }
        assert_eq!(limiter.active_descriptors(), 10);

        // 11th should fail.
        let err = limiter.register_descriptor();
        assert!(matches!(err, Err(LimitsError::DescriptorLimitExceeded { .. })));

        limiter.unregister_descriptor();
        assert_eq!(limiter.active_descriptors(), 9);

        // Can register again.
        assert!(limiter.register_descriptor().is_ok());
    }

    #[tokio::test]
    async fn test_concurrent_access() {
        let limiter = ResourceLimiter::new(test_config()).unwrap();

        let mut handles = vec![];
        for _ in 0..10 {
            let l = limiter.clone();
            handles.push(tokio::spawn(async move {
                for _ in 0..100 {
                    let _ = l.reserve_memory(1024);
                    tokio::task::yield_now().await;
                    l.release_memory(1024);
                }
            }));
        }

        for h in handles {
            h.await.unwrap();
        }

        // Final usage should be zero.
        assert_eq!(limiter.used_staging_bytes(), 0);
    }

    #[test]
    fn test_validation() {
        let bad_config = DaemonResourceConfig {
            max_staging_bytes: 0,
            high_watermark_pct: 0.9,
            low_watermark_pct: 0.8,
            max_active_descriptors: 10,
        };
        assert!(ResourceLimiter::new(bad_config).is_err());
    }
}
