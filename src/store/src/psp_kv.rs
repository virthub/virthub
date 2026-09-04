// virthub/src/store/src/psp_kv.rs

//! PSP‑KV format definitions and metadata structures.
//!
//! This module defines the three physical storage formats used by
//! precision‑scalable paged KV‑cache (PSP‑KV):
//!
//! 1. **Basic Format** – in‑band metadata header, symmetric 1D layout,
//!    separate staging buffer for dequantization.
//! 2. **Enhanced GPU‑Native Format** – out‑of‑band sidecar metadata,
//!    asymmetric 1D layout, fused in‑register dequantization.
//! 3. **Hardware‑Native 2D Block‑Tiled Format (BT‑KV)** – 2D hardware
//!    tiles, TMA descriptors, interleaved micro‑scale stripes.
//!
//! The format generation is fixed globally via configuration; this module
//! provides the corresponding data layouts and descriptor types. The
//! precision level and residual flag are stored separately in the packed
//! policy word defined in the `precision` crate and are not duplicated here.

use serde::{Deserialize, Serialize};

/// Physical storage format generation for PSP‑KV.
///
/// The format is chosen at deployment time and remains constant for the
/// entire serving run. It determines how KV blocks are laid out in GPU
/// memory and how dequantization is performed.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum PspKvFormat {
    /// Modular Basic Format with in‑band metadata and staging buffer.
    Basic = 0,
    /// Enhanced GPU‑Native Format with sidecar metadata and fused dequant.
    Enhanced = 1,
    /// Hardware‑Native 2D Block‑Tiled Format for Hopper/Blackwell TMA.
    BtKv = 2,
}

impl PspKvFormat {
    /// Converts a raw `u8` value to a `PspKvFormat`.
    ///
    /// Values 0..=2 map to the corresponding variant; any other value
    /// defaults to `Basic` for safety (though configuration should never
    /// produce such a value).
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Basic,
            1 => Self::Enhanced,
            2 => Self::BtKv,
            _ => Self::Basic,
        }
    }
}

/// Sidecar descriptor for the Enhanced and BT‑KV formats.
///
/// This descriptor is stored out‑of‑band in a contiguous array of 64‑byte
/// entries. It contains all metadata required to reconstruct a KV block:
/// data page pointers, residual page pointer, head presence mask, base
/// precision, compression level, and per‑head scale exponents (E8M0).
///
/// The structure is aligned to 64 bytes to match cache line size and enable
/// efficient TMA / vector loads. The layout is fixed and must match the C
/// struct in `kernels/common/psp_kv_types.h`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(C, align(64))]
pub struct PspKvSidecarDescriptor {
    /// Base pointer to the physical KV data page.
    pub data_page_base_ptr: u64,
    /// Pointer to the residual page (Level 2 only).
    pub residual_page_ptr: u64,
    /// 64‑bit presence bitmask (Level 3 head pruning).
    pub head_presence_mask: u64,
    /// Base precision: 0 = FP16/BF16, 1 = FP8_E4M3, 2 = FP8_E5M2, 3 = FP4.
    pub base_precision: u8,
    /// Compression level: 0 = lossless, 1 = FP8, 2 = FP8+residual, 3 = pruned.
    pub compression_level: u8,
    /// Number of heads packed per page (G).
    pub head_group_size: u8,
    /// Runtime flags / alignment padding.
    pub reserved_flags: u8,
    /// Per‑head scale exponents (up to 8 heads), stored as E8M0 bytes.
    pub per_head_scale_e8m0: [u8; 8],
    /// Padding to ensure exact 64‑byte stride.
    pub reserved_padding: [u8; 28],
}

impl Default for PspKvSidecarDescriptor {
    fn default() -> Self {
        Self {
            data_page_base_ptr: 0,
            residual_page_ptr: 0,
            head_presence_mask: 0xFFFF_FFFF_FFFF_FFFF,
            base_precision: 0,
            compression_level: 0,
            head_group_size: 1,
            reserved_flags: 0,
            per_head_scale_e8m0: [0; 8],
            reserved_padding: [0; 28],
        }
    }
}

/// Canonical tile shapes for BT‑KV.
///
/// In the 2D block‑tiled format, K and V pages are partitioned into
/// hardware‑aligned sub‑tiles. These constants define the standard tile
/// dimensions used by the TMA descriptors and `wgmma` instructions.
pub mod btkv_tile {
    /// Tile height for K (tokens dimension).
    pub const K_TILE_TOKENS: usize = 16;
    /// Tile width for K (head dimension).
    pub const K_TILE_DIMS: usize = 64;
    /// Tile height for V (head dimension).
    pub const V_TILE_DIMS: usize = 64;
    /// Tile width for V (tokens dimension).
    pub const V_TILE_TOKENS: usize = 16;
}

/// A container for a BT‑KV 2D tile with interleaved micro‑scale stripes.
///
/// For sub‑byte precision tiers (e.g., FP4), the physical page is formatted
/// as a self‑contained unit grouping the quantized matrix payload with its
/// E8M0 scale exponents. A single `cp.async.bulk.tensor` instruction moves
/// both scales and payload into shared memory.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BtKvTileContainer {
    /// Quantized payload nibbles (packed 4‑bit values).
    pub quantized_data: Vec<u8>,
    /// E8M0 scale exponents for groups of 32 elements.
    pub scale_exponents: Vec<u8>,
    /// Number of elements per scale group (default 32 for MXFP4).
    pub group_size: usize,
}

impl BtKvTileContainer {
    /// Creates a new tile container with the given payload and scales.
    ///
    /// The `group_size` must match the quantization scheme (e.g., 32 for
    /// OCP MX formats). The scale vector length must equal
    /// `ceil(num_elements / group_size)`.
    pub fn new(
        quantized_data: Vec<u8>,
        scale_exponents: Vec<u8>,
        group_size: usize,
    ) -> Self {
        Self {
            quantized_data,
            scale_exponents,
            group_size,
        }
    }

    /// Returns the total size in bytes of this container.
    pub fn size_bytes(&self) -> usize {
        self.quantized_data.len() + self.scale_exponents.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::mem;

    #[test]
    fn test_psp_kv_format_from_u8() {
        assert_eq!(PspKvFormat::from_u8(0), PspKvFormat::Basic);
        assert_eq!(PspKvFormat::from_u8(1), PspKvFormat::Enhanced);
        assert_eq!(PspKvFormat::from_u8(2), PspKvFormat::BtKv);
        // Invalid values fall back to Basic for safety.
        assert_eq!(PspKvFormat::from_u8(9), PspKvFormat::Basic);
    }

    #[test]
    fn test_sidecar_descriptor_alignment_and_size() {
        // Must be exactly 64 bytes.
        assert_eq!(mem::size_of::<PspKvSidecarDescriptor>(), 64);
        // Alignment must be at least 64.
        assert!(mem::align_of::<PspKvSidecarDescriptor>() >= 64);
    }

    #[test]
    fn test_default_sidecar_values() {
        let desc = PspKvSidecarDescriptor::default();
        assert_eq!(desc.data_page_base_ptr, 0);
        assert_eq!(desc.residual_page_ptr, 0);
        assert_eq!(desc.head_presence_mask, 0xFFFF_FFFF_FFFF_FFFF);
        assert_eq!(desc.base_precision, 0);
        assert_eq!(desc.compression_level, 0);
        assert_eq!(desc.head_group_size, 1);
        assert_eq!(desc.reserved_flags, 0);
        assert_eq!(desc.per_head_scale_e8m0, [0; 8]);
        assert_eq!(desc.reserved_padding, [0; 28]);
    }

    #[test]
    fn test_btkv_tile_container_size() {
        let container = BtKvTileContainer::new(
            vec![0xAB; 32], // payload
            vec![0x1; 1],   // scales for 32 elements with group_size=32
            32,
        );
        assert_eq!(container.size_bytes(), 33);
        assert_eq!(container.group_size, 32);
    }
}
