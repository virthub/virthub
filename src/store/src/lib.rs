// virthub/src/store/src/lib.rs

//! Multi‑tier storage manager for Virthub DSM.
//!
//! This crate provides KV block storage, tiered capacity management, and
//! a pre‑allocated staging pool for zero‑copy page moves.
//!
//! ## Modules
//! - [`kv_block`] – Key‑value block representation with checksum and serialization.
//! - [`staging_pool`] – Pre‑touched memory slots for `UFFDIO_MOVE`.
//! - [`tier_manager`] – Usage tracking for DRAM, SSD, and VRAM tiers.
//!
//! ## Re‑exports
//! Commonly used types are re‑exported at the crate root for convenience.

pub mod kv_block;
pub mod staging_pool;
pub mod tier_manager;

// Re‑export primary types from submodules for workspace‑wide consumption.
pub use kv_block::{
    KvBlock, KvBlockError, KvBlockHeader, KvBlockKey, RawBlockBuffer,
    CACHE_LINE_ALIGN, DEFAULT_BLOCK_SIZE,
};
pub use staging_pool::{
    StagingMemoryPool, StagingPoolError, StagingSlotHandle,
    PAGE_SIZE_2M, PAGE_SIZE_4K,
};
pub use tier_manager::{
    StorageTier, StorageTierManager, TierCapacityConfig,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_kv_block_key() {
        let key = KvBlockKey::new(101, 202);
        assert_eq!(key.namespace_id, 101);
        assert_eq!(key.block_id, 202);
    }

    #[test]
    fn test_tier_capacity_config() {
        let config = TierCapacityConfig::default_dram();
        assert!(config.max_capacity_bytes > 0);
    }

    #[test]
    fn test_staging_pool_creation() {
        let pool = StagingMemoryPool::new(4, PAGE_SIZE_4K, 0).unwrap();
        assert_eq!(pool.available_slots(), 4);
    }
}
