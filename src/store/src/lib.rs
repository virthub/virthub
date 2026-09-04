// virthub/src/store/src/lib.rs

//! Multi‑tier storage manager for Virthub DSM.
//!
//! This crate provides KV block storage, tiered capacity management,
//! a pre‑allocated staging pool for zero‑copy page moves, and the
//! precision‑scalable paged KV‑cache (PSP‑KV) format definitions and
//! quantization helpers.
//!
//! ## Modules
//! - [`kv_block`] – Key‑value block representation with checksum and serialization.
//! - [`psp_kv`] – PSP‑KV format structures: sidecar descriptor, tile container, format enum.
//! - [`quantization`] – FP8/FP4 quantization and dequantization utilities.
//! - [`staging_pool`] – Pre‑touched memory slots for `UFFDIO_MOVE`.
//! - [`tier_manager`] – Usage tracking for DRAM, SSD, and VRAM tiers.
//!
//! ## Re‑exports
//! Commonly used types are re‑exported at the crate root for convenience.

pub mod kv_block;
pub mod psp_kv;
pub mod quantization;
pub mod staging_pool;
pub mod tier_manager;

// Re‑export primary types from submodules for workspace‑wide consumption.
pub use kv_block::{
    KvBlock, KvBlockError, KvBlockHeader, KvBlockKey, RawBlockBuffer,
    CACHE_LINE_ALIGN, DEFAULT_BLOCK_SIZE,
};
pub use psp_kv::{
    BtKvTileContainer, PspKvFormat, PspKvSidecarDescriptor, btkv_tile,
};
pub use quantization::{
    dequantize_fp4_e2m1, dequantize_fp8_e4m3, dequantize_fp8_e5m2,
    e8m0_to_f32, f32_to_e8m0, pack_fp4_pair, quantize_fp4_e2m1,
    quantize_fp8_e4m3, quantize_fp8_e5m2, unpack_fp4_pair,
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

    #[test]
    fn test_psp_kv_format_default() {
        let format = PspKvFormat::from_u8(1);
        assert_eq!(format, PspKvFormat::Enhanced);
    }

    #[test]
    fn test_quantization_helpers_available() {
        let q = quantize_fp8_e4m3(1.0);
        assert!(q > 0);
        let dq = dequantize_fp8_e4m3(q);
        assert!((dq - 1.0).abs() < 0.1);
    }
}
