# PSP-KV Format Specifications

This document describes the three physical storage formats used by Precision‑Scalable Paged KV‑Cache (PSP‑KV). The format generation is fixed at engine startup via configuration and remains constant for the entire serving run.

---

## 1. Overview

PSP‑KV partitions the KV cache into fixed‑size logical blocks. Each block may be stored at one of four precision levels (`p`):

| Precision Level | Data Format          |
|-----------------|----------------------|
| 0               | FP16 / BF16 (lossless) |
| 1               | FP8 (symmetric or affine) |
| 2               | FP8 + residual correction |
| 3               | Partial (head pruning) |

The physical layout of a block depends on the configured format generation `g`:

- **`g = 0` – Basic Format**  
  In‑band metadata header, symmetric 1D layout, separate staging dequantization.

- **`g = 1` – Enhanced GPU‑Native Format**  
  Out‑of‑band sidecar metadata, asymmetric 1D layout, fused in‑register dequantization.

- **`g = 2` – Hardware‑Native 2D Block‑Tiled Format (BT‑KV)**  
  2D hardware‑aligned tiles, TMA descriptors, interleaved micro‑scale stripes.

---

## 2. Format Details

### 2.1 Basic Format (First Generation)

The Basic Format is designed for simplicity and broad compatibility, at the cost of extra memory traffic.

**Layout**
- Both K and V are stored in token‑major order `[B_t, D_head]`.
- A block header is embedded directly before the payload (in‑band). It contains per‑head FP32 scale and zero‑point for affine quantization.
- The header causes an unaligned offset: `Δ_hdr = 8 * N_kv` bytes.

**Quantization**
- Level 1: `q = round(x / s) + z`
- Level 2: residual stored in FP16: `r = x - s * (q - z)`
- Level 3: dynamic branching during scalar token load.

**Dequantization Path**
- A separate prologue kernel reads compressed pages, applies affine dequantization, and writes reconstructed FP16 into a global memory staging buffer.
- The attention kernel then reads from the staging buffer.

**Pros**
- Minimal kernel changes.
- Works on any GPU (pre‑Ampere as well).

**Cons**
- Global memory round‑trip increases latency and bandwidth.
- Unaligned header offsets may cause bank conflicts.

---

### 2.2 Enhanced GPU‑Native Format (Second Generation)

The Enhanced Format eliminates the staging buffer and uses asymmetric layouts for coalesced memory access.

**Layout**
- **K**: token‑major `[B_t, D_head]`
- **V**: transposed 1D `[D_head, B_t]` (head‑major, enabling coalesced loads in both GEMM phases)

**Metadata**
- Stored out‑of‑band in a contiguous sidecar descriptor array, aligned to 64 bytes.
- No in‑band header; payload pages begin exactly at 256‑byte boundaries.

**Quantization**
- Symmetric power‑of‑two scaling: `x = 2^s * q`
- The scale exponent `s` is stored as E8M0 (8‑bit unsigned integer).
- Level 2: adds residual FP8 with its own scale: `x = 2^s_q * q + 2^s_r * q_r`
- Level 3: dense compaction of non‑pruned heads using `__popc` mapping.

**Dequantization Path**
- Fused in‑register dequantization during the shared memory load phase.
- No global memory staging buffer; values go directly from shared memory to Tensor Core registers.

**Pros**
- Avoids memory round‑trip; better bandwidth.
- Asymmetric V layout improves coalescing.
- 256‑byte alignment enables TMA compatibility.

**Cons**
- Requires custom attention kernel with fused dequantization.

---

### 2.3 Hardware‑Native 2D Block‑Tiled Format (BT‑KV, Third Generation)

BT‑KV exploits 2D hardware execution primitives on Hopper/Blackwell.

**Layout**
- Blocks are partitioned into 2D sub‑tiles:
  - K tile: `[16, 64]` (tokens × head_dim)
  - V tile: `[64, 16]` (head_dim × tokens)
- These tile shapes match TMA copy granularities and `wgmma.mma_async` instructions.

**Metadata**
- For sub‑byte precision tiers (FP4), the scale exponents are **interleaved** directly inside the 2D tile container:
  ```
  C_tile = [ S_E8M0 | Q_FP4 ]
  ```
- A single `cp.async.bulk.tensor` instruction transfers both scales and payload into shared memory.

**Dequantization Path**
- TMA bulk copy to shared memory.
- Fused dequantization feeds directly into `wgmma`.

**Level 3 Pruning**
- TMA descriptor sub‑tile coordinate skipping; no software indexing overhead.

**Pros**
- Near‑optimal bandwidth utilization.
- Hardware handles boundary clamping and swizzling.

**Cons**
- Only available on Hopper (SM90) and Blackwell (SM100+).
- Requires `CUtensorMap` descriptors and advanced kernels.

---

## 3. Sidecar Descriptor Layout

For Enhanced and BT‑KV formats, metadata is stored in a 64‑byte descriptor:

```c
typedef struct __attribute__((aligned(64))) PspKvSidecarDescriptor {
    uint64_t data_page_base_ptr;     // Base pointer to physical KV data page
    uint64_t residual_page_ptr;      // Pointer to residual page (Level 2)
    uint64_t head_presence_mask;     // 64-bit presence mask (Level 3 pruning)
    uint8_t  base_precision;         // 0: FP16/BF16, 1: FP8_E4M3, 2: FP8_E5M2, 3: FP4
    uint8_t  compression_level;      // 0=lossless, 1=FP8, 2=FP8+residual, 3=pruned
    uint8_t  head_group_size;        // Heads packed per page (G)
    uint8_t  reserved_flags;         // Runtime flags / padding
    uint8_t  per_head_scale_e8m0[8]; // Per-head scale exponents (E8M0)
    uint8_t  reserved_padding[28];   // Pad to 64 bytes
} PspKvSidecarDescriptor;
```

This struct is mirrored in Rust as `store::psp_kv::PspKvSidecarDescriptor`.

---

## 4. Comparison Matrix

| Axis                | Basic Format (Gen 1)              | Enhanced GPU‑Native (Gen 2)       | BT‑KV (Gen 3)                          |
|---------------------|-----------------------------------|-----------------------------------|----------------------------------------|
| **Layout**          | Symmetric 1D token‑major          | Asymmetric 1D; V transposed      | 2D tiles [16,64] / [64,16]             |
| **Quantization**    | Affine FP8                        | Symmetric E8M0 FP8               | Symmetric E8M0 FP8 / group FP4        |
| **Metadata**        | In‑band header                    | Out‑of‑band sidecar              | Interleaved scale stripes inside tile  |
| **Alignment**       | Unaligned offset                  | 128/256‑byte aligned             | 128/256‑byte aligned 2D containers    |
| **Copy engine**     | Scalar loads (`LDG`)              | `cp.async` vector loads          | TMA (`cp.async.bulk.tensor`)           |
| **Dequantization**  | Global staging buffer             | Fused in‑register                | Fused in‑register feeding `wgmma`      |
| **Bank conflicts**  | Unmanaged                         | XOR swizzling                    | Native 2D hardware swizzle             |
| **Level 3**         | Branch per token                  | Dense `__popc` mapping           | TMA sub‑tile skip                      |
| **Target hardware** | Generic GPUs (Volta+)             | Ampere/Ada/Hopper               | Hopper/Blackwell                       |

---

## 5. Quantization Helpers

Common quantization functions are defined in `store::quantization` and mirrored in `kernels/common/quantization_utils.cuh`:

- `quantize_fp8_e4m3` / `dequantize_fp8_e4m3`
- `quantize_fp8_e5m2` / `dequantize_fp8_e5m2`
- `quantize_fp4_e2m1` / `dequantize_fp4_e2m1`
- `pack_fp4_pair` / `unpack_fp4_pair`
- `f32_to_e8m0` / `e8m0_to_f32`

These functions are validated by CPU reference tests in `kernels/tests/cpu_reference_dequant.cu` and Rust tests in `src/store/tests/quantization_tests.rs`.

---

## 6. Configuration

The format generation is set in `conf/virthub.toml` (or `conf/cluster.toml`):

```toml
[precision]
format_generation = 1   # 0=Basic, 1=Enhanced, 2=BT-KV
sink_window = 16
local_window = 64
critical_layer_count = 2
elevated_pressure_threshold = 0.78
nominal_pressure_threshold = 0.70
critical_pressure_threshold = 0.88
critical_relax_threshold = 0.82
```

The value of `format_generation` selects the GPU kernel variant to use. It is read once at startup and never changes during execution.

---

## 7. Testing

### Rust Tests
- `src/store/tests/psp_kv_tests.rs` – validates sidecar descriptor size/alignment, tile constants, default values.
- `src/store/tests/quantization_tests.rs` – roundtrip tests for FP8/FP4/E8M0.

Run:
```bash
./run.sh --test-pspkv
```

### GPU Kernel Tests
- `kernels/tests/test_header_consistency.py` – checks C header against Rust struct offsets.
- `kernels/tests/cpu_reference_dequant.cu` – validates quantization math on CPU.

Run:
```bash
./run.sh --build-kernels
./run.sh --test-kernels
```

---

## 8. Summary

PSP‑KV provides three evolutionary format generations, each targeting a different hardware capability level. The Basic Format ensures compatibility, the Enhanced Format delivers high performance on modern GPUs, and BT‑KV exploits advanced hardware primitives for maximum throughput. The choice is static per deployment and is driven entirely by the `[precision]` configuration section.
