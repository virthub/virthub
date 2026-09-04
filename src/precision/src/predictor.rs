// virthub/src/precision/src/predictor.rs

//! Allocation‑time precision predictor.
//!
//! This module implements the core decision logic described in the
//! PSP‑KV precision prediction specification. The predictor is fully
//! deterministic and requires no runtime telemetry or background threads.
//! It uses only:
//!
//! - the layer index (`l`),
//! - the starting token position of the block (`t_start`),
//! - the current sequence length (`s_len`),
//! - the 3‑state memory‑pressure hysteresis (`H_mem`),
//! - a retrieval‑head flag derived from the head mask (`M_head`).
//!
//! The physical format generation (`g`) is not part of the prediction;
//! it is fixed globally via configuration and passed separately to the
//! packing function by the upper layer. This predictor only produces the
//! per‑block precision level and residual flag.

use super::hysteresis::MemoryPressureState;
use super::lut::build_layer_profiles;
use super::policy::{LayerSensitivityProfile, PackedBlockPolicy, PrecisionLevel};

/// Default attention sink window (tokens from the beginning of a sequence).
const SINK_WINDOW: usize = 16;

/// Default local context window (tokens from the end of the sequence).
const LOCAL_WINDOW: usize = 64;

/// Mask used to indicate all 24 GQA heads are active.
const FULL_HEAD_MASK: u32 = 0xFF_FFFF;

/// The allocation‑time precision predictor.
pub struct Predictor {
    /// Precomputed per‑layer sensitivity profiles.
    profiles: Vec<LayerSensitivityProfile>,
    /// Sink window (number of initial tokens that are always lossless).
    sink_window: usize,
    /// Local window (number of trailing tokens that are always lossless).
    local_window: usize,
}

impl Predictor {
    /// Creates a new predictor for a model with `total_layers` layers.
    ///
    /// The layer sensitivity profiles are precomputed automatically using
    /// the standard boundaries defined in the PSP‑KV specification.
    pub fn new(total_layers: usize) -> Self {
        Self {
            profiles: build_layer_profiles(total_layers),
            sink_window: SINK_WINDOW,
            local_window: LOCAL_WINDOW,
        }
    }

    /// Creates a predictor with custom sink/local window sizes.
    ///
    /// This can be useful for experimentation or unusual model shapes.
    pub fn with_windows(total_layers: usize, sink_window: usize, local_window: usize) -> Self {
        Self {
            profiles: build_layer_profiles(total_layers),
            sink_window,
            local_window,
        }
    }

    /// Predicts the packed policy for a single KV‑cache block.
    ///
    /// # Arguments
    /// * `layer_idx`   – zero‑based layer index.
    /// * `token_start` – starting token position of the block within the sequence.
    /// * `seq_len`     – total sequence length (at allocation time).
    /// * `h_mem`       – current memory‑pressure hysteresis state.
    /// * `head_mask`   – bitmask of retrieval‑critical heads (only bit 0 is examined).
    /// * `retrieval_flag` – explicit flag indicating a retrieval‑critical head is present
    ///                 (equivalent to `(head_mask & 1) != 0`).
    ///
    /// # Returns
    /// A `PackedBlockPolicy` with the predicted precision, residual flag, and
    /// active head mask.
    pub fn predict(
        &self,
        layer_idx: usize,
        token_start: usize,
        seq_len: usize,
        h_mem: MemoryPressureState,
        head_mask: u32,
        retrieval_flag: bool,
    ) -> PackedBlockPolicy {
        // 1. Hard invariants: attention sink and local window are ALWAYS lossless.
        if token_start < self.sink_window || (seq_len - token_start) < self.local_window {
            return PackedBlockPolicy::new(
                PrecisionLevel::Fp16,
                false,
                FULL_HEAD_MASK,
            );
        }

        let prof = &self.profiles[layer_idx];

        // 2. Critical boundary layers: never pruned; only downgraded to FP8
        //    under critical memory pressure.
        if prof.is_critical {
            let p = if h_mem == MemoryPressureState::Critical {
                PrecisionLevel::Fp8
            } else {
                PrecisionLevel::Fp16
            };
            return PackedBlockPolicy::new(p, false, FULL_HEAD_MASK);
        }

        // 3. Sensitivity score.
        let base_bonus = prof.base_bonus as i32;
        let retrieval_score = if retrieval_flag { 2 } else { 0 };
        let score = base_bonus + retrieval_score;

        // 4. Scored precision mapping.
        let precision = if score >= 3 {
            if h_mem == MemoryPressureState::Nominal {
                PrecisionLevel::Fp16
            } else {
                PrecisionLevel::Fp8Residual
            }
        } else if score == 0
            && h_mem == MemoryPressureState::Critical
            && prof.can_prune
        {
            PrecisionLevel::Pruned
        } else {
            PrecisionLevel::Fp8
        };

        let use_residual = precision == PrecisionLevel::Fp8Residual;
        let active_mask = if precision == PrecisionLevel::Pruned {
            head_mask & FULL_HEAD_MASK
        } else {
            FULL_HEAD_MASK
        };

        PackedBlockPolicy::new(precision, use_residual, active_mask)
    }

    /// Predicts policies for a batch of blocks from the same layer.
    ///
    /// This is a convenience wrapper around `predict` that processes slices
    /// of equal length. It is not yet SIMD‑vectorized; a production system
    /// can replace the loop with AVX2 intrinsics if profiling shows a need.
    ///
    /// # Arguments
    /// * `layer_idx` – layer index shared by all blocks in the batch.
    /// * `token_starts` – slice of starting token positions.
    /// * `seq_len` – current sequence length (same for all blocks).
    /// * `h_mem` – current memory‑pressure state.
    /// * `head_masks` – slice of head masks (one per block).
    /// * `retrieval_flags` – slice of retrieval flags (one per block).
    ///
    /// # Panics
    /// If the input slices have different lengths.
    pub fn predict_batch(
        &self,
        layer_idx: usize,
        token_starts: &[usize],
        seq_len: usize,
        h_mem: MemoryPressureState,
        head_masks: &[u32],
        retrieval_flags: &[bool],
    ) -> Vec<PackedBlockPolicy> {
        assert_eq!(token_starts.len(), head_masks.len());
        assert_eq!(token_starts.len(), retrieval_flags.len());

        token_starts
            .iter()
            .zip(head_masks.iter())
            .zip(retrieval_flags.iter())
            .map(|((&ts, &hm), &rf)| {
                self.predict(layer_idx, ts, seq_len, h_mem, hm, rf)
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::hysteresis::MemoryPressureState as H;

    fn test_predictor(total_layers: usize) -> Predictor {
        Predictor::new(total_layers)
    }

    #[test]
    fn test_attention_sink_always_fp16() {
        let pred = test_predictor(32);
        for h in [H::Nominal, H::Elevated, H::Critical] {
            for start in 0..16 {
                let policy = pred.predict(10, start, 1000, h, 0xFFFFFFFF, true);
                assert_eq!(policy.precision(), PrecisionLevel::Fp16);
                assert!(!policy.use_residual());
                assert_eq!(policy.head_mask(), FULL_HEAD_MASK);
            }
        }
    }

    #[test]
    fn test_local_window_always_fp16() {
        let pred = test_predictor(32);
        let seq_len = 1000;
        // Local window is the last 64 tokens, but with strict `< 64`:
        // tokens with (seq_len - start) < 64.
        // The smallest start is seq_len - 63 (distance 63), largest is seq_len - 1.
        for h in [H::Nominal, H::Elevated, H::Critical] {
            for start in (seq_len - 63)..seq_len {
                let policy = pred.predict(10, start, seq_len, h, 0xFFFFFFFF, true);
                assert_eq!(policy.precision(), PrecisionLevel::Fp16);
                assert!(!policy.use_residual());
                assert_eq!(policy.head_mask(), FULL_HEAD_MASK);
            }
        }
    }

    #[test]
    fn test_critical_layers_fp16_unless_critical_pressure() {
        let pred = test_predictor(32);
        // Critical layers: 0, 1, 30, 31.
        for layer in [0, 1, 30, 31] {
            // Nominal -> FP16
            let policy = pred.predict(layer, 100, 1000, H::Nominal, 0xFFFFFFFF, true);
            assert_eq!(policy.precision(), PrecisionLevel::Fp16);

            // Elevated -> FP16 (critical layers are not downgraded under elevated)
            let policy = pred.predict(layer, 100, 1000, H::Elevated, 0xFFFFFFFF, true);
            assert_eq!(policy.precision(), PrecisionLevel::Fp16);

            // Critical -> FP8
            let policy = pred.predict(layer, 100, 1000, H::Critical, 0xFFFFFFFF, true);
            assert_eq!(policy.precision(), PrecisionLevel::Fp8);
            assert!(!policy.use_residual());
            assert_eq!(policy.head_mask(), FULL_HEAD_MASK);
        }
    }

    #[test]
    fn test_retrieval_head_and_memory_pressure() {
        let pred = test_predictor(32);
        // Use a middle layer with base_bonus=1 to achieve score=3 when retrieval flag true.
        let layer = 3; // base_bonus=1 (l<4) and not critical
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

        // Retrieval flag false => score=1 (base_bonus only) => FP8 in all states.
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
        let pred = test_predictor(32);
        // Need: score=0, H=Critical, can_prune=true.
        // Use layer 10: base_bonus=0, can_prune=true (8..=24).
        let layer = 10;

        // score=0 (retrieval false), H=Critical => Pruned.
        let policy = pred.predict(layer, 100, 1000, H::Critical, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Pruned);
        assert_eq!(policy.head_mask(), 0x0 & FULL_HEAD_MASK); // 0
        assert!(!policy.use_residual());

        // With retrieval flag true, score=2, so not pruned; FP8.
        let policy = pred.predict(layer, 100, 1000, H::Critical, 0x1, true);
        assert_eq!(policy.precision(), PrecisionLevel::Fp8);
        assert_eq!(policy.head_mask(), FULL_HEAD_MASK);

        // Under non-critical pressure, score=0 should still be FP8.
        let policy = pred.predict(layer, 100, 1000, H::Nominal, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Fp8);

        let policy = pred.predict(layer, 100, 1000, H::Elevated, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Fp8);
    }

    #[test]
    fn test_batch_prediction_consistency() {
        let pred = test_predictor(32);
        let layer = 10;
        let starts = [100, 200, 300];
        let masks = [0x1, 0x0, 0x1];
        let flags = [true, false, true];
        let h = H::Nominal;
        let batch = pred.predict_batch(layer, &starts, 1000, h, &masks, &flags);

        let scalar: Vec<_> = starts.iter().zip(masks.iter()).zip(flags.iter())
            .map(|((&s, &m), &f)| pred.predict(layer, s, 1000, h, m, f))
            .collect();
        assert_eq!(batch, scalar);
    }

    #[test]
    fn test_custom_windows() {
        let pred = Predictor::with_windows(32, 4, 8);
        let seq_len = 100;

        // Position 2 (<4) sink -> FP16.
        let policy = pred.predict(10, 2, seq_len, H::Critical, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Fp16);

        // Position 4 is not sink, not local, normal scoring -> pruned under critical.
        let policy = pred.predict(10, 4, seq_len, H::Critical, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Pruned);

        // Position 95 (seq_len - start = 5) is local -> FP16.
        let policy = pred.predict(10, 95, seq_len, H::Critical, 0x0, false);
        assert_eq!(policy.precision(), PrecisionLevel::Fp16);
    }

    #[test]
    fn test_packed_policy_roundtrip() {
        let p = PrecisionLevel::Fp8Residual;
        let residual = true;
        let mask = 0x123456;
        let policy = PackedBlockPolicy::new(p, residual, mask);

        assert_eq!(policy.precision(), p);
        assert_eq!(policy.use_residual(), residual);
        assert_eq!(policy.head_mask(), mask & 0xFF_FFFF);

        let expected_raw = (p as u32) | (1 << 2) | ((mask & 0xFF_FFFF) << 8);
        assert_eq!(policy.raw(), expected_raw);
    }
}
