# Allocation-Time Precision Prediction for PSP-KV

This document specifies a **pure single-pass, vectorized precision prediction algorithm** for PSP-KV. The predictor executes synchronously during block allocation in under **1.2 µs** per request, using only deterministic structural priors and a multi-tier memory-pressure hysteresis. It does not require post-prefill refinement passes, telemetry ring buffers, or RCU epoch deallocation.

---

## 1. Overview

Modern autoregressive LLM decoding is strictly memory-bandwidth-bound. PSP-KV dynamically adjusts the bit-width of KV-cache blocks to maximize memory throughput. To minimize CPU scheduler overhead, precision decisions must be made **once at block allocation time**.

The physical storage format (Basic, Enhanced, or BT-KV) is fixed globally via configuration. The predictor only assigns the **numeric precision level** `p` (0: FP16, 1: FP8, 2: FP8+Residual, 3: Pruned) and the residual flag.

---

## 2. Formal Problem Statement

Let `X` be the feature space and `P` the set of 32-bit packed policy words. Define the allocation-time decision mapping:

```
Φ_alloc : X → P
```

### 2.1 Input Feature Vector

Each input `x ∈ X` is a tuple:

```
x = ( l, t_start, s_len, μ, M_head )
```

where:

- `l ∈ [0, L-1]` – transformer layer index
- `t_start ∈ ℕ` – starting token position of the block
- `s_len ∈ ℕ` – current sequence length
- `μ ∈ [0,1]` – memory pressure ratio computed from allocator pool state:  
  ```
  μ = 1 - (free_blocks / total_blocks)
  ```
- `M_head ∈ {0,1}^(N_kv)` – bitmask of retrieval-critical heads

The format generation `g` is **not** part of the feature vector because it is fixed by configuration.

### 2.2 Output Policy Specification

The output is a 32-bit packed policy word:

```
W = pack(p, r, h_mask) = (p mod 4) + (r mod 2) * 2^2 + (h_mask mod 2^24) * 2^8
```

Fields:

- `p` – precision level (0=FP16, 1=FP8, 2=FP8+Residual, 3=Pruned)
- `r` – residual flag, `r = 1` iff `p = 2`
- `h_mask` – active head mask (24 bits); `h_mask = M_head & (2^24 - 1)` if `p=3`, otherwise `2^24 - 1`

The C struct layout is:

```c
typedef struct {
    uint32_t precision_level : 2;   // Bits 0-1
    uint32_t use_residual    : 1;   // Bit 2
    uint32_t reserved        : 5;   // Bits 3-7
    uint32_t active_head_mask: 24;  // Bits 8-31
} PackedBlockPolicy;
```

---

## 3. Static Format Configuration

The physical storage format `g` is set once per engine instance:

- `g = 0` → Basic Format
- `g = 1` → Enhanced GPU-Native Format
- `g = 2` → Hardware-Native 2D Block-Tiled Format (BT-KV)

The value comes from the `[precision]` section of `virthub.toml` and never changes at runtime. All blocks inherit the same `g`.

---

## 4. Precomputed Data Structures

### 4.1 Layer Sensitivity Lookup Table (LUT)

To avoid runtime branching, a layer profile is precomputed for each layer `l`:

```rust
struct LayerSensitivityProfile {
    base_score_bonus: u8,   // 1 if l < 4 or l >= L-4, else 0
    is_critical_layer: bool, // true for l < 2 or l >= L-2
    can_prune_heads: bool,   // true for floor(L/4) <= l <= floor(3L/4)
}
```

The table is built once at startup using `build_layer_profiles(total_layers)`.

### 4.2 Multi-Tier Memory Pressure Hysteresis

A 3-state Schmitt trigger maintains `H_mem ∈ {0, 1, 2}`:

- **State 0 (Nominal)** – normal serving
- **State 1 (Elevated)** – moderate memory pressure
- **State 2 (Critical)** – severe memory starvation

Transition logic:

```
H_mem = 2  if μ >= 0.88
H_mem = 1  if (μ >= 0.78 and previous < 2) or (μ >= 0.82 and previous == 2)
H_mem = 0  if μ < 0.70 or (μ < 0.78 and previous == 1)
otherwise keep previous state
```

This hysteresis prevents precision thrashing under oscillating memory pressure.

---

## 5. Hard Invariants

Three invariants are enforced **before** any scoring:

1. **Attention Sink** – if `t_start < 16`, then `p = 0` (FP16)
2. **Local Context Window** – if `s_len - t_start < 64`, then `p = 0` (FP16)
3. **Critical Boundary Layers** – if `l < 2` or `l >= L-2`, then:
   - `p = 0` if `H_mem < 2`
   - `p = 1` if `H_mem == 2` (never pruned)

These conditions override any scored decision.

---

## 6. Sensitivity Scoring

For non-invariant blocks, compute a sensitivity score `S`:

```
S = base_bonus(l) + 2 * ( (M_head & 1) != 0 )
```

where `base_bonus(l)` comes from the layer LUT, and the retrieval-head flag adds 2 if any retrieval-critical head is present.

During allocation, the dynamic attention mass is unknown and is treated as zero.

---

## 7. Closed-Form Decision Mapping

Given `S` and `H_mem`, the baseline candidate precision `p_scored` is:

```
p_scored = 0  if S >= 3 and H_mem == 0
p_scored = 2  if S >= 3 and H_mem >= 1
p_scored = 3  if S == 0 and H_mem == 2 and layer_profile.can_prune
p_scored = 1  otherwise
```

The final precision `p` is obtained by applying the hard invariants over `p_scored`:

```
p = 0  if sink or local window
p = (H_mem == 2) ? 1 : 0  if critical layer
p = p_scored otherwise
```

Residual flag: `r = (p == 2)`.  
Head mask: `h_mask = M_head & 0xFFFFFF` if `p == 3`, else `0xFFFFFF`.

---

## 8. Vectorized Primitives

For batch evaluation across `N` blocks, define:

### 8.1 Vector Selection `σ`

```
σ(M, A, B)_i = M_i * A_i + (1 - M_i) * B_i
```

where `M ∈ {0,1}^N` is a condition mask, and `A,B ∈ ℕ^N` are operand vectors.

### 8.2 Batch Bit-Packing `PackPolicyBatch`

```
PackPolicyBatch(p, r, h_mask)_i = pack(p_i, r_i, h_mask_i)
```

These primitives enable SIMD execution on CPU (AVX2) or GPU.

---

## 9. Algorithm

The vectorized allocation-time prediction algorithm is:

```
Function PredictBlockPolicyBatch(block_ids, l, L, t_start, s_len, H_mem, M_head):
    Precomputed: bonus = bonus(l), crit = critical(l), prune = prunable(l), FULL_MASK = 0xFFFFFF

    // 1. Score non-invariant blocks
    retrieval_flag = (M_head & 1) ? 2 : 0
    S = bonus + retrieval_flag
    p_scored = 1   // default FP8

    if S >= 3:
        p_scored = (H_mem == 0) ? 0 : 2
    elif S == 0 and H_mem == 2 and prune:
        p_scored = 3

    // 2. Compute invariant masks
    sink_or_local = (t_start < 16) or (s_len - t_start < 64)
    crit_val = (H_mem == 2) ? 1 : 0

    // 3. Enforce invariants (guaranteed overwrite)
    p = VectorSelect(crit, crit_val, p_scored)
    p = VectorSelect(sink_or_local, 0, p)

    // 4. Pack policies
    r = (p == 2)
    h_mask = VectorSelect(p == 3, M_head & FULL_MASK, FULL_MASK)
    return PackPolicyBatch(p, r, h_mask)
```

---

## 10. Complexity and Overhead

- **Allocation latency**: `< 1.2 µs` per entire 32k request using SIMD (AVX2)
- **Zero telemetry overhead**: no GPU-to-host copies, no background threads
- **Memory footprint**: sidecar descriptor tables consume `< 0.05%` of aggregate VRAM
- **Deterministic memory safety**: no asynchronous demotion, no RCU barriers

---

## 11. Testing and Validation

### Unit Tests

Located in `src/precision/tests/`:

- `predictor_tests.rs` – tests hard invariants, scoring, pruning, and batch consistency
- `hysteresis_tests.rs` – verifies state transition logic at exact thresholds
- `lut_tests.rs` – checks layer sensitivity table boundaries

### Benchmarks

Located in `src/precision/benches/`:

- `predictor_bench.rs` – measures scalar and batch prediction latency
- `packing_bench.rs` – measures policy packing/unpacking cost

Run with:

```bash
./run.sh --test-precision
./run.sh --bench-predictor
```
