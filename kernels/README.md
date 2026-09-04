# PSP‑KV GPU Kernels

This directory contains CUDA and Triton kernel implementations for the three
PSP‑KV physical storage formats:

| Format                 | Directory          | Description                                                                           |
| ---------------------- | ------------------ | ------------------------------------------------------------------------------------- |
| Basic Format           | `basic_format/`    | In‑band metadata, symmetric 1D layout, separate staging dequantization.               |
| Enhanced GPU‑Native    | `enhanced_format/` | Out‑of‑band sidecar metadata, asymmetric 1D layout, fused in‑register dequantization. |
| BT‑KV (2D Block‑Tiled) | `btkv_format/`     | Hardware‑native 2D tiles, TMA descriptors, interleaved micro‑scales.                  |

## Build Status and Environment Requirements

- **By default, CUDA kernel compilation is disabled** because CUDA 12.0 with
  modern glibc (2.35+) fails to compile C++17 host code. This is due to
  `_Float32` / `_Float64` / `_Float128` declarations in glibc that are not
  available in C++17.  
- **Recommended CUDA version:** 12.3 or newer, which properly supports C++17
  with current glibc.  
- **Host compiler:** GCC 11 or 12 works best with CUDA 12.x.  
- If your environment meets these requirements, you can enable kernel builds
  by passing `-DENABLE_GPU_KERNELS=ON` to CMake (see below).

---

## Building

### CUDA Kernels

`./run.sh --build-kernels` will complete without compiling any CUDA kernels
because kernel compilation is disabled by default due to the CUDA/glibc
compatibility issue.

### Triton Kernels

Triton kernels are imported directly from Python; no separate build step is
required. Ensure `triton` is installed in your Python environment.

---

## Integration Notes

- The C structs in `common/psp_kv_types.h` must match the Rust definitions in
  `virthub/src/precision/src/policy.rs` and
  `virthub/src/store/src/psp_kv.rs`. Keep them in sync when either side
  changes.
- The packed policy word layout is:
  - bits 0‑1: precision level (0=FP16, 1=FP8, 2=FP8+Residual, 3=Pruned)
  - bit 2: residual flag
  - bits 3‑7: reserved
  - bits 8‑31: active head mask (24 bits)
- The sidecar descriptor must be 64‑byte aligned and exactly 64 bytes in size.
- Attention kernels should branch only on the 2‑bit precision level; the
  physical format generation (`g`) is fixed per engine and not read from the
  policy word.

---

## Testing

Header consistency can be checked with:

```bash
python3 tests/test_header_consistency.py
```

CPU reference dequantization can be compiled and run (on a CPU-only system)
with:

```bash
nvcc -o cpu_reference tests/cpu_reference_dequant.cu
./cpu_reference
```

This validates the quantization math without requiring a real GPU.

---

## Status

These kernels are **reference implementations** intended to validate the
PSP‑KV design and guide integration with vLLM/FlashInfer. They have not yet
been tuned for production performance. GPU kernel compilation is currently
disabled by default until the CUDA/glibc compatibility issue is resolved.
