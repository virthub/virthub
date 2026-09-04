// virthub/src/store/tests/quantization_tests.rs

//! Integration tests for PSP‑KV quantization and dequantization helpers.
//!
//! These tests validate FP8 (E4M3, E5M2), FP4 (E2M1), FP4 pair packing,
//! and E8M0 scale conversions. They are pure CPU tests and do not require
//! GPU hardware.

use store::quantization::{
    dequantize_fp4_e2m1, dequantize_fp8_e4m3, dequantize_fp8_e5m2,
    e8m0_to_f32, f32_to_e8m0, pack_fp4_pair, quantize_fp4_e2m1,
    quantize_fp8_e4m3, quantize_fp8_e5m2, unpack_fp4_pair,
};

// Helper for approximate float comparison
fn assert_close(a: f32, b: f32, eps: f32) {
    assert!(
        (a - b).abs() < eps,
        "expected {} and {} to be within {}",
        a,
        b,
        eps
    );
}

#[test]
fn test_fp8_e4m3_roundtrip_simple_values() {
    let values = [0.0, 1.0, -1.0, 0.5, 2.0, 0.125, 6.0, -0.25];
    for &v in &values {
        let q = quantize_fp8_e4m3(v);
        let dq = dequantize_fp8_e4m3(q);
        assert_close(v, dq, 0.1);
    }
}

#[test]
fn test_fp8_e4m3_nan_and_inf() {
    let q_nan = quantize_fp8_e4m3(f32::NAN);
    assert_eq!(q_nan, 0x7F);
    let q_inf = quantize_fp8_e4m3(f32::INFINITY);
    assert_eq!(q_inf, 0x78);
    let q_neg_inf = quantize_fp8_e4m3(f32::NEG_INFINITY);
    assert_eq!(q_neg_inf, 0xF8);
}

#[test]
fn test_fp8_e5m2_roundtrip_simple_values() {
    let values = [0.0, 1.0, -1.0, 0.5, 2.0, 0.25, 8.0, -0.125];
    for &v in &values {
        let q = quantize_fp8_e5m2(v);
        let dq = dequantize_fp8_e5m2(q);
        assert_close(v, dq, 0.2);
    }
}

#[test]
fn test_fp8_e5m2_nan_and_inf() {
    let q_nan = quantize_fp8_e5m2(f32::NAN);
    assert_eq!(q_nan, 0x7F);
    let q_inf = quantize_fp8_e5m2(f32::INFINITY);
    assert_eq!(q_inf, 0x7C);
    let q_neg_inf = quantize_fp8_e5m2(f32::NEG_INFINITY);
    assert_eq!(q_neg_inf, 0xFC);
}

#[test]
fn test_fp4_pair_packing_unpacking() {
    let a = 0x5;
    let b = 0xA;
    let packed = pack_fp4_pair(a, b);
    assert_eq!(unpack_fp4_pair(packed), (a, b));

    // Test edge values
    let (h, l) = unpack_fp4_pair(pack_fp4_pair(0x0, 0xF));
    assert_eq!(h, 0x0);
    assert_eq!(l, 0xF);
}

#[test]
fn test_fp4_e2m1_roundtrip_simple_values() {
    let values = [0.0, 0.5, 1.0, 1.5, 2.0, 3.0, -0.5, -2.0];
    for &v in &values {
        let q = quantize_fp4_e2m1(v);
        let dq = dequantize_fp4_e2m1(q);
        assert_close(v, dq, 0.4);
    }
}

#[test]
fn test_fp4_e2m1_nan_and_inf() {
    let q_nan = quantize_fp4_e2m1(f32::NAN);
    assert_eq!(q_nan, 0x7);
    let q_inf = quantize_fp4_e2m1(f32::INFINITY);
    assert_eq!(q_inf, 0x6);
    let q_neg_inf = quantize_fp4_e2m1(f32::NEG_INFINITY);
    assert_eq!(q_neg_inf, 0xE);
}

#[test]
fn test_e8m0_to_f32_powers_of_two() {
    assert_eq!(e8m0_to_f32(0), 1.0);
    assert_eq!(e8m0_to_f32(1), 2.0);
    assert_eq!(e8m0_to_f32(3), 8.0);
    assert_close(e8m0_to_f32(8), 256.0, 0.001);
}

#[test]
fn test_f32_to_e8m0_exact_powers() {
    assert_eq!(f32_to_e8m0(1.0), 0);
    assert_eq!(f32_to_e8m0(2.0), 1);
    assert_eq!(f32_to_e8m0(4.0), 2);
    assert_eq!(f32_to_e8m0(8.0), 3);
}

#[test]
fn test_f32_to_e8m0_rounding() {
    // 6.0 is between 4 (exp=2) and 8 (exp=3), log2 ~ 2.585 -> rounds to 3
    assert_eq!(f32_to_e8m0(6.0), 3);
    // 0.75 is between 0.5 (exp=-1) and 1.0 (exp=0), log2 ~ -0.415 -> rounds to 0
    assert_eq!(f32_to_e8m0(0.75), 0);
}

#[test]
fn test_f32_to_e8m0_invalid_inputs() {
    assert_eq!(f32_to_e8m0(0.0), 0);
    assert_eq!(f32_to_e8m0(-1.0), 0);
    assert_eq!(f32_to_e8m0(f32::NAN), 0);
    assert_eq!(f32_to_e8m0(f32::INFINITY), 255);
}
