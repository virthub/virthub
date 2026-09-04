// virthub/src/precision/benches/predictor_bench.rs

//! Criterion benchmarks for the allocation‑time precision predictor.
//!
//! These benchmarks measure the latency of the scalar predictor, batch
//! prediction, and policy packing operations. They are intended to verify
//! that the predictor meets its performance target (< 1.2 µs per request
//! for thousands of blocks using SIMD; scalar baseline is slower but still
//! should be in the nanosecond range per block).

use criterion::{black_box, criterion_group, criterion_main, Criterion};

use precision::hysteresis::MemoryPressureState;
use precision::policy::{PackedBlockPolicy, PrecisionLevel};
use precision::Predictor;

/// Creates a predictor for a 32‑layer model.
fn make_predictor() -> Predictor {
    Predictor::new(32)
}

/// Generates a random boolean slice of `n` elements using a simple LCG.
fn random_bools(n: usize, seed: u64) -> Vec<bool> {
    let mut state = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    (0..n)
        .map(|_| {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            (state >> 63) & 1 == 1
        })
        .collect()
}

/// Generates a vector of random `u32` head masks (only lower 24 bits used).
fn random_head_masks(n: usize, seed: u64) -> Vec<u32> {
    let mut state = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    (0..n)
        .map(|_| {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            (state & 0xFF_FFFF) as u32
        })
        .collect()
}

/// Generates a slice of starting token positions that avoid the sink/local windows.
fn random_token_starts(n: usize, seq_len: usize, seed: u64) -> Vec<usize> {
    let mut state = seed.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
    (0..n)
        .map(|_| {
            state = state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            // Range between 16 and seq_len - 64 (inclusive) to avoid invariants.
            let range = seq_len - 64 - 16;
            if range == 0 {
                16
            } else {
                16 + (state as usize % range)
            }
        })
        .collect()
}

fn bench_scalar_predict(c: &mut Criterion) {
    let predictor = make_predictor();
    let h_mem = MemoryPressureState::Elevated;
    // Use a non-critical, non-prunable layer that still exercises scoring.
    let layer_idx = 10;
    // Avoid sink/local windows.
    let token_start = 100;
    let seq_len = 1000;
    let head_mask = 0x1; // retrieval head present
    let retrieval_flag = true;

    c.bench_function("predictor_scalar", |b| {
        b.iter(|| {
            black_box(
                predictor.predict(
                    black_box(layer_idx),
                    black_box(token_start),
                    black_box(seq_len),
                    black_box(h_mem),
                    black_box(head_mask),
                    black_box(retrieval_flag),
                ),
            )
        })
    });
}

fn bench_batch_predict(c: &mut Criterion) {
    let predictor = make_predictor();
    let h_mem = MemoryPressureState::Elevated;
    let layer_idx = 10;
    let seq_len = 1000;

    // Bench batch of 1000 blocks (simulating a 32k token request with 16-token blocks)
    let n = 1000;
    let token_starts = random_token_starts(n, seq_len, 42);
    let head_masks = random_head_masks(n, 43);
    let retrieval_flags = random_bools(n, 44);

    c.bench_function("predictor_batch_1000", |b| {
        b.iter(|| {
            black_box(
                predictor.predict_batch(
                    black_box(layer_idx),
                    black_box(&token_starts),
                    black_box(seq_len),
                    black_box(h_mem),
                    black_box(&head_masks),
                    black_box(&retrieval_flags),
                ),
            )
        })
    });
}

fn bench_policy_packing(c: &mut Criterion) {
    c.bench_function("policy_pack", |b| {
        b.iter(|| {
            black_box(
                PackedBlockPolicy::new(
                    black_box(PrecisionLevel::Fp8Residual),
                    black_box(true),
                    black_box(0x123456),
                ),
            )
        })
    });

    let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8Residual, true, 0x123456);
    c.bench_function("policy_unpack", |b| {
        b.iter(|| {
            black_box(policy.precision());
            black_box(policy.use_residual());
            black_box(policy.head_mask());
        })
    });
}

criterion_group!(
    benches,
    bench_scalar_predict,
    bench_batch_predict,
    bench_policy_packing
);
criterion_main!(benches);
