// virthub/src/precision/tests/predictor_tests.rs

//! Integration tests for the allocation‑time precision predictor.
//!
//! These tests exercise the predictor's hard invariants (attention sinks,
//! local windows, critical layers), sensitivity scoring, memory‑pressure
//! hysteresis, and batch consistency. They are independent of any DSM or
//! GPU runtime and can run in plain `cargo test`.

use precision::hysteresis::MemoryPressureState as H;
use precision::policy::{PackedBlockPolicy, PrecisionLevel};
use precision::Predictor;

/// Helper to create a predictor for a 32‑layer model.
fn test_predictor() -> Predictor {
    Predictor::new(32)
}

/// Helper to create a predictor with custom sink/local window sizes.
fn test_predictor_with_windows(sink: usize, local: usize) -> Predictor {
    Predictor::with_windows(32, sink, local)
}

#[test]
fn test_attention_sink_always_fp16() {
    let pred = test_predictor();
    // Sink tokens are positions 0..15, regardless of memory pressure or
    // retrieval flag.
    for h in [H::Nominal, H::Elevated, H::Critical] {
        for start in 0..16 {
            let policy = pred.predict(10, start, 1000, h, 0xFFFFFFFF, true);
            assert_eq!(policy.precision(), PrecisionLevel::Fp16);
            assert!(!policy.use_residual());
            assert_eq!(policy.head_mask(), 0xFF_FFFF);
        }
    }
}

#[test]
fn test_local_window_always_fp16() {
    let pred = test_predictor();
    let seq_len = 1000;
    // Local window: last 64 tokens. Because the condition is
    // `(seq_len - token_start) < 64`, the smallest start is `seq_len - 63`.
    for h in [H::Nominal, H::Elevated, H::Critical] {
        for start in (seq_len - 63)..seq_len {
            let policy = pred.predict(10, start, seq_len, h, 0xFFFFFFFF, true);
            assert_eq!(policy.precision(), PrecisionLevel::Fp16);
            assert!(!policy.use_residual());
            assert_eq!(policy.head_mask(), 0xFF_FFFF);
        }
    }
}

#[test]
fn test_critical_layers_fp16_unless_critical_pressure() {
    let pred = test_predictor();
    // Critical layers: 0, 1, 30, 31.
    let critical_layers = [0, 1, 30, 31];
    for &layer in &critical_layers {
        // Under Nominal and Elevated pressure, critical layers remain FP16.
        let policy = pred.predict(layer, 100, 1000, H::Nominal, 0xFFFFFFFF, true);
        assert_eq!(policy.precision(), PrecisionLevel::Fp16);

        let policy = pred.predict(layer, 100, 1000, H::Elevated, 0xFFFFFFFF, true);
        assert_eq!(policy.precision(), PrecisionLevel::Fp16);

        // Under Critical pressure, they are downgraded to FP8 (never pruned).
        let policy = pred.predict(layer, 100, 1000, H::Critical, 0xFFFFFFFF, true);
        assert_eq!(policy.precision(), PrecisionLevel::Fp8);
        assert!(!policy.use_residual());
        assert_eq!(policy.head_mask(), 0xFF_FFFF);
    }
}

#[test]
fn test_retrieval_head_and_memory_pressure() {
    let pred = test_predictor();
    // Use a middle layer with base_bonus=1 to achieve score=3 when retrieval
    // flag is true. Layer 3 has base_bonus=1 (l<4) and is not critical.
    let layer = 3;
    // base_bonus=1, retrieval_flag=true => score=3.
    // Nominal: score>=3 and H=Nominal => FP16.
    let policy = pred.predict(layer, 100, 1000, H::Nominal, 0x1, true);
    assert_eq!(policy.precision(), PrecisionLevel::Fp16);

    // Elevated/Critical: score>=3 and H!=Nominal => FP8Residual.
    let policy = pred.predict(layer, 100, 1000, H::Elevated, 0x1, true);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8Residual);
    assert!(policy.use_residual());

    let policy = pred.predict(layer, 100, 1000, H::Critical, 0x1, true);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8Residual);
    assert!(policy.use_residual());

    // Retrieval flag false => score=1 (base_bonus only), so FP8 in all states.
    let policy = pred.predict(layer, 100, 1000, H::Nominal, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);
    assert!(!policy.use_residual());

    let policy = pred.predict(layer, 100, 1000, H::Elevated, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);

    let policy = pred.predict(layer, 100, 1000, H::Critical, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);
}

#[test]
fn test_pruning_conditions() {
    let pred = test_predictor();
    // Need: score=0, H=Critical, can_prune=true.
    // Use layer 10: base_bonus=0 (not <4 or >=28), can_prune=true (8..=24).
    let layer = 10;

    // score=0 (retrieval false), H=Critical => Pruned.
    let policy = pred.predict(layer, 100, 1000, H::Critical, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Pruned);
    // Head mask should be the provided head mask masked to 24 bits.
    // We passed head_mask=0, so head_mask should be 0.
    assert_eq!(policy.head_mask(), 0x0 & 0xFF_FFFF);
    assert!(!policy.use_residual());

    // With retrieval flag true, score=2, so not pruned; FP8.
    let policy = pred.predict(layer, 100, 1000, H::Critical, 0x1, true);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);
    assert_eq!(policy.head_mask(), 0xFF_FFFF); // all heads active

    // Under non-critical pressure, score=0 should still be FP8 (default).
    let policy = pred.predict(layer, 100, 1000, H::Nominal, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);

    let policy = pred.predict(layer, 100, 1000, H::Elevated, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp8);
}

#[test]
fn test_batch_prediction_consistency() {
    let pred = test_predictor();
    let layer = 10; // middle layer, not critical
    let starts = [100, 200, 300];
    let masks = [0x1, 0x0, 0x1];
    let flags = [true, false, true];
    let h = H::Nominal;

    let batch = pred.predict_batch(layer, &starts, 1000, h, &masks, &flags);

    // Compare with scalar predictions
    let scalar: Vec<_> = starts
        .iter()
        .zip(masks.iter())
        .zip(flags.iter())
        .map(|((&s, &m), &f)| pred.predict(layer, s, 1000, h, m, f))
        .collect();

    assert_eq!(batch, scalar);
}

#[test]
fn test_custom_windows() {
    // Use custom windows: sink=4, local=8.
    let pred = test_predictor_with_windows(4, 8);
    let seq_len = 100;

    // Position 2 (<4) should be FP16.
    let policy = pred.predict(10, 2, seq_len, H::Critical, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp16);

    // Position 4 should not be sink (not <4). It is also not local because
    // seq_len - start = 96, which is >8. So it follows normal scoring.
    // For layer 10, base_bonus=0, retrieval false => score=0, H=Critical and
    // can_prune true => Pruned.
    let policy = pred.predict(10, 4, seq_len, H::Critical, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Pruned);

    // Position 95 (seq_len - start = 5) is inside local window -> FP16.
    let policy = pred.predict(10, 95, seq_len, H::Critical, 0x0, false);
    assert_eq!(policy.precision(), PrecisionLevel::Fp16);
}

#[test]
fn test_packed_policy_roundtrip() {
    // Verify that PackedBlockPolicy can be created and queried correctly.
    let p = PrecisionLevel::Fp8Residual;
    let residual = true;
    let mask = 0x123456;
    let policy = PackedBlockPolicy::new(p, residual, mask);

    assert_eq!(policy.precision(), p);
    assert_eq!(policy.use_residual(), residual);
    assert_eq!(policy.head_mask(), mask & 0xFF_FFFF);

    // Ensure raw bit layout matches expected:
    // bits 0-1: precision (2)
    // bit 2: residual (1)
    // bits 8-31: mask
    let expected_raw = (p as u32) | (1 << 2) | ((mask & 0xFF_FFFF) << 8);
    assert_eq!(policy.raw(), expected_raw);
}
