// src/store/src/tier_manager.rs

use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::OnceLock;

/// Storage tier enumeration (DRAM, SSD, VRAM).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum StorageTier {
    Dram,
    Ssd,
    Vram,
}

/// Capacity and watermark configuration for a storage tier.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TierCapacityConfig {
    pub tier: StorageTier,
    pub enabled: bool,
    pub max_capacity_bytes: u64,
    pub high_watermark_pct: f32,
    pub low_watermark_pct: f32,
    pub current_usage_bytes: u64,
}

impl TierCapacityConfig {
    /// Default DRAM tier configuration (16 GB, 85%/60% watermarks).
    pub fn default_dram() -> Self {
        Self {
            tier: StorageTier::Dram,
            enabled: true,
            max_capacity_bytes: 16 * 1024 * 1024 * 1024, // 16 GB
            high_watermark_pct: 0.85,
            low_watermark_pct: 0.60,
            current_usage_bytes: 0,
        }
    }

    /// Default SSD tier configuration (256 GB, 90%/70% watermarks).
    pub fn default_ssd() -> Self {
        Self {
            tier: StorageTier::Ssd,
            enabled: true,
            max_capacity_bytes: 256 * 1024 * 1024 * 1024, // 256 GB
            high_watermark_pct: 0.90,
            low_watermark_pct: 0.70,
            current_usage_bytes: 0,
        }
    }

    /// Default VRAM tier configuration (8 GB, 90%/70% watermarks).
    pub fn default_vram() -> Self {
        Self {
            tier: StorageTier::Vram,
            enabled: true,
            max_capacity_bytes: 8 * 1024 * 1024 * 1024, // 8 GB
            high_watermark_pct: 0.90,
            low_watermark_pct: 0.70,
            current_usage_bytes: 0,
        }
    }
}

/// Manager for tracking usage across multiple storage tiers.
///
/// Uses atomic counters for lock‑free updates and provides capacity checking
/// and utilization metrics. Default tier configurations are cached with
/// `OnceLock` to avoid repeated allocation.
#[derive(Debug)]
pub struct StorageTierManager {
    dram_used_bytes: AtomicU64,
    ssd_used_bytes: AtomicU64,
    vram_used_bytes: AtomicU64,
    dram_config: OnceLock<TierCapacityConfig>,
    ssd_config: OnceLock<TierCapacityConfig>,
    vram_config: OnceLock<TierCapacityConfig>,
}

impl StorageTierManager {
    /// Creates a new tier manager with zero usage.
    pub fn new() -> Self {
        Self {
            dram_used_bytes: AtomicU64::new(0),
            ssd_used_bytes: AtomicU64::new(0),
            vram_used_bytes: AtomicU64::new(0),
            dram_config: OnceLock::new(),
            ssd_config: OnceLock::new(),
            vram_config: OnceLock::new(),
        }
    }

    /// Allocates `size_bytes` in the specified tier (increments usage counter).
    pub fn allocate(&self, tier: StorageTier, size_bytes: u64) {
        let counter = self.get_counter(tier);
        counter.fetch_add(size_bytes, Ordering::Relaxed);
    }

    /// Deallocates `size_bytes` from the specified tier (decrements usage counter).
    /// Prevents underflow by clamping to zero.
    pub fn deallocate(&self, tier: StorageTier, size_bytes: u64) {
        let counter = self.get_counter(tier);
        let mut current = counter.load(Ordering::Relaxed);
        loop {
            let new_val = current.saturating_sub(size_bytes);
            match counter.compare_exchange_weak(
                current,
                new_val,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                Ok(_) => break,
                Err(actual) => current = actual,
            }
        }
    }

    /// Attempts to allocate `size_bytes` in the tier, enforcing capacity limit.
    /// Returns `Ok(())` if the allocation succeeds, or `Err` if it would exceed
    /// the tier's maximum capacity.
    pub fn try_allocate(&self, tier: StorageTier, size_bytes: u64) -> Result<(), String> {
        if !self.can_allocate(tier, size_bytes) {
            return Err(format!(
                "Cannot allocate {} bytes in tier {:?}: capacity exceeded",
                size_bytes, tier
            ));
        }
        self.allocate(tier, size_bytes);
        Ok(())
    }

    /// Checks whether `size_bytes` can be allocated in the given tier without
    /// exceeding the maximum capacity.
    pub fn can_allocate(&self, tier: StorageTier, size_bytes: u64) -> bool {
        let config = self.get_config(tier);
        let current = self.get_usage(tier);
        current.saturating_add(size_bytes) <= config.max_capacity_bytes
    }

    /// Returns the current usage in bytes for the given tier.
    pub fn get_usage(&self, tier: StorageTier) -> u64 {
        self.get_counter(tier).load(Ordering::Relaxed)
    }

    /// Returns the utilization percentage (0.0 to 1.0) for the given tier.
    pub fn utilization(&self, tier: StorageTier) -> f64 {
        let config = self.get_config(tier);
        if config.max_capacity_bytes == 0 {
            return 0.0;
        }
        self.get_usage(tier) as f64 / config.max_capacity_bytes as f64
    }

    /// Batch allocate across multiple tiers (for performance).
    pub fn allocate_batch(&self, allocations: &[(StorageTier, u64)]) {
        for &(tier, size) in allocations {
            self.allocate(tier, size);
        }
    }

    /// Batch deallocate across multiple tiers.
    pub fn deallocate_batch(&self, deallocations: &[(StorageTier, u64)]) {
        for &(tier, size) in deallocations {
            self.deallocate(tier, size);
        }
    }

    // Private helper to obtain the appropriate atomic counter for a tier.
    #[inline]
    fn get_counter(&self, tier: StorageTier) -> &AtomicU64 {
        match tier {
            StorageTier::Dram => &self.dram_used_bytes,
            StorageTier::Ssd => &self.ssd_used_bytes,
            StorageTier::Vram => &self.vram_used_bytes,
        }
    }

    // Private helper to obtain the capacity configuration for a tier.
    // Uses OnceLock to avoid repeated construction of default configs.
    #[inline]
    fn get_config(&self, tier: StorageTier) -> &TierCapacityConfig {
        match tier {
            StorageTier::Dram => self.dram_config.get_or_init(TierCapacityConfig::default_dram),
            StorageTier::Ssd => self.ssd_config.get_or_init(TierCapacityConfig::default_ssd),
            StorageTier::Vram => self.vram_config.get_or_init(TierCapacityConfig::default_vram),
        }
    }
}

impl Default for StorageTierManager {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_storage_tier_usage() {
        let manager = StorageTierManager::new();
        assert_eq!(manager.get_usage(StorageTier::Dram), 0);

        manager.allocate(StorageTier::Dram, 1024);
        assert_eq!(manager.get_usage(StorageTier::Dram), 1024);

        manager.deallocate(StorageTier::Dram, 512);
        assert_eq!(manager.get_usage(StorageTier::Dram), 512);
    }

    #[test]
    fn test_try_allocate_capacity() {
        let manager = StorageTierManager::new();
        // Dram max is 16 GB
        let large_alloc = 17 * 1024 * 1024 * 1024; // 17 GB
        assert!(manager.try_allocate(StorageTier::Dram, large_alloc).is_err());

        let small_alloc = 1024;
        assert!(manager.try_allocate(StorageTier::Dram, small_alloc).is_ok());
        assert_eq!(manager.get_usage(StorageTier::Dram), small_alloc);
    }

    #[test]
    fn test_deallocate_underflow_prevention() {
        let manager = StorageTierManager::new();
        manager.deallocate(StorageTier::Dram, 1024);
        assert_eq!(manager.get_usage(StorageTier::Dram), 0);
    }

    #[test]
    fn test_utilization() {
        let manager = StorageTierManager::new();
        assert_eq!(manager.utilization(StorageTier::Dram), 0.0);
        manager.allocate(StorageTier::Dram, 8 * 1024 * 1024 * 1024); // 8 GB
        let util = manager.utilization(StorageTier::Dram);
        assert!((util - 0.5).abs() < 1e-6); // 8/16 = 0.5
    }

    #[test]
    fn test_batch_operations() {
        let manager = StorageTierManager::new();
        manager.allocate_batch(&[
            (StorageTier::Dram, 1000),
            (StorageTier::Ssd, 2000),
            (StorageTier::Vram, 3000),
        ]);
        assert_eq!(manager.get_usage(StorageTier::Dram), 1000);
        assert_eq!(manager.get_usage(StorageTier::Ssd), 2000);
        assert_eq!(manager.get_usage(StorageTier::Vram), 3000);

        manager.deallocate_batch(&[
            (StorageTier::Dram, 500),
            (StorageTier::Ssd, 1500),
        ]);
        assert_eq!(manager.get_usage(StorageTier::Dram), 500);
        assert_eq!(manager.get_usage(StorageTier::Ssd), 500);
        assert_eq!(manager.get_usage(StorageTier::Vram), 3000);
    }
}
