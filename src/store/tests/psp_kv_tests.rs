// virthub/src/store/tests/psp_kv_tests.rs

//! Integration tests for PSP‑KV format structures.
//!
//! These tests validate the size, alignment, default values, and basic
//! behavior of `PspKvFormat`, `PspKvSidecarDescriptor`, and
//! `BtKvTileContainer`. They are independent of any GPU hardware and run
//! in the standard `cargo test` environment.

use store::psp_kv::{
    BtKvTileContainer, PspKvFormat, PspKvSidecarDescriptor, btkv_tile,
};
use std::mem;

#[test]
fn test_psp_kv_format_from_u8() {
    assert_eq!(PspKvFormat::from_u8(0), PspKvFormat::Basic);
    assert_eq!(PspKvFormat::from_u8(1), PspKvFormat::Enhanced);
    assert_eq!(PspKvFormat::from_u8(2), PspKvFormat::BtKv);
    // Any other value falls back to Basic for safety.
    assert_eq!(PspKvFormat::from_u8(3), PspKvFormat::Basic);
    assert_eq!(PspKvFormat::from_u8(255), PspKvFormat::Basic);
}

#[test]
fn test_psp_kv_format_serialization_roundtrip() {
    // PspKvFormat derives Serialize/Deserialize, so bincode roundtrip should work.
    let formats = [
        PspKvFormat::Basic,
        PspKvFormat::Enhanced,
        PspKvFormat::BtKv,
    ];
    for fmt in formats {
        let serialized = bincode::serialize(&fmt).expect("serialization should succeed");
        let deserialized: PspKvFormat =
            bincode::deserialize(&serialized).expect("deserialization should succeed");
        assert_eq!(fmt, deserialized);
    }
}

#[test]
fn test_sidecar_descriptor_size() {
    // Must be exactly 64 bytes.
    assert_eq!(mem::size_of::<PspKvSidecarDescriptor>(), 64);
}

#[test]
fn test_sidecar_descriptor_alignment() {
    // Alignment must be at least 64 (cache line size).
    assert!(mem::align_of::<PspKvSidecarDescriptor>() >= 64);
}

#[test]
fn test_sidecar_descriptor_default_values() {
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
fn test_sidecar_descriptor_field_assignment() {
    let mut desc = PspKvSidecarDescriptor::default();
    desc.data_page_base_ptr = 0x1234_5678_9ABC_DEF0;
    desc.residual_page_ptr = 0x0FED_CBA9_8765_4321;
    desc.head_presence_mask = 0x0000_0000_0000_00FF;
    desc.base_precision = 1;
    desc.compression_level = 2;
    desc.head_group_size = 4;
    desc.reserved_flags = 0x55;
    desc.per_head_scale_e8m0 = [1, 2, 3, 4, 5, 6, 7, 8];
    desc.reserved_padding = [0xAA; 28];

    assert_eq!(desc.data_page_base_ptr, 0x1234_5678_9ABC_DEF0);
    assert_eq!(desc.residual_page_ptr, 0x0FED_CBA9_8765_4321);
    assert_eq!(desc.head_presence_mask, 0x0000_0000_0000_00FF);
    assert_eq!(desc.base_precision, 1);
    assert_eq!(desc.compression_level, 2);
    assert_eq!(desc.head_group_size, 4);
    assert_eq!(desc.reserved_flags, 0x55);
    assert_eq!(desc.per_head_scale_e8m0, [1, 2, 3, 4, 5, 6, 7, 8]);
    assert_eq!(desc.reserved_padding, [0xAA; 28]);
}

#[test]
fn test_btkv_tile_constants() {
    // Canonical tile shapes from the PSP‑KV specification.
    assert_eq!(btkv_tile::K_TILE_TOKENS, 16);
    assert_eq!(btkv_tile::K_TILE_DIMS, 64);
    assert_eq!(btkv_tile::V_TILE_DIMS, 64);
    assert_eq!(btkv_tile::V_TILE_TOKENS, 16);
}

#[test]
fn test_btkv_tile_container_basic() {
    let payload = vec![0xAB; 32];
    let scales = vec![0x01; 1]; // one scale for group_size=32
    let group_size = 32;
    let container = BtKvTileContainer::new(payload.clone(), scales.clone(), group_size);

    assert_eq!(container.quantized_data, payload);
    assert_eq!(container.scale_exponents, scales);
    assert_eq!(container.group_size, group_size);
    assert_eq!(container.size_bytes(), payload.len() + scales.len());
}

#[test]
fn test_btkv_tile_container_empty() {
    let container = BtKvTileContainer::new(Vec::new(), Vec::new(), 32);
    assert_eq!(container.quantized_data.len(), 0);
    assert_eq!(container.scale_exponents.len(), 0);
    assert_eq!(container.size_bytes(), 0);
}

#[test]
fn test_btkv_tile_container_with_multiple_groups() {
    // 64 elements with group_size=32 => 2 scales.
    let payload = vec![0xCD; 64];
    let scales = vec![0x02, 0x03];
    let container = BtKvTileContainer::new(payload.clone(), scales.clone(), 32);

    assert_eq!(container.group_size, 32);
    assert_eq!(container.scale_exponents.len(), 2);
    assert_eq!(container.size_bytes(), 66);
}
