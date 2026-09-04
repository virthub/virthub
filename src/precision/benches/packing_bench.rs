// virthub/src/precision/benches/packing_bench.rs

//! Criterion benchmarks for the packed block policy bit packing and unpacking.
//!
//! These benchmarks measure the cost of creating and reading `PackedBlockPolicy`
//! values. The packing operation is a few bitwise operations and should be
//! extremely fast (< 1 ns). The benchmarks are useful for detecting any
//! accidental overhead in the implementation.

use criterion::{black_box, criterion_group, criterion_main, Criterion};
use precision::policy::{PackedBlockPolicy, PrecisionLevel};

/// Benchmarks the `PackedBlockPolicy::new` constructor.
fn bench_pack_policy(c: &mut Criterion) {
    let precision = PrecisionLevel::Fp8;
    let residual = false;
    let head_mask = 0xFF_FFFF;

    c.bench_function("pack_policy_new", |b| {
        b.iter(|| {
            black_box(PackedBlockPolicy::new(
                black_box(precision),
                black_box(residual),
                black_box(head_mask),
            ))
        })
    });
}

/// Benchmarks the `from_raw` constructor.
fn bench_pack_policy_from_raw(c: &mut Criterion) {
    let raw = 0x1234_5678u32;
    c.bench_function("pack_policy_from_raw", |b| {
        b.iter(|| black_box(PackedBlockPolicy::from_raw(black_box(raw))))
    });
}

/// Benchmarks extraction of precision level.
fn bench_unpack_precision(c: &mut Criterion) {
    let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8Residual, true, 0xABCDEF);
    c.bench_function("unpack_precision", |b| {
        b.iter(|| black_box(policy.precision()))
    });
}

/// Benchmarks extraction of residual flag.
fn bench_unpack_residual(c: &mut Criterion) {
    let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8Residual, true, 0xABCDEF);
    c.bench_function("unpack_residual", |b| {
        b.iter(|| black_box(policy.use_residual()))
    });
}

/// Benchmarks extraction of head mask.
fn bench_unpack_head_mask(c: &mut Criterion) {
    let policy = PackedBlockPolicy::new(PrecisionLevel::Pruned, false, 0xABCDEF);
    c.bench_function("unpack_head_mask", |b| {
        b.iter(|| black_box(policy.head_mask()))
    });
}

/// Benchmarks the raw accessor.
fn bench_unpack_raw(c: &mut Criterion) {
    let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8, false, 0xFF_FFFF);
    c.bench_function("unpack_raw", |b| {
        b.iter(|| black_box(policy.raw()))
    });
}

criterion_group!(
    benches,
    bench_pack_policy,
    bench_pack_policy_from_raw,
    bench_unpack_precision,
    bench_unpack_residual,
    bench_unpack_head_mask,
    bench_unpack_raw
);
criterion_main!(benches);
