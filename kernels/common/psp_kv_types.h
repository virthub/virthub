// virthub/kernels/common/psp_kv_types.h

#ifndef PSP_KV_TYPES_H
#define PSP_KV_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Physical storage format generation (configured once per engine)
typedef enum {
    PSP_KV_FORMAT_BASIC    = 0,   // Basic Format (legacy fallback)
    PSP_KV_FORMAT_ENHANCED = 1,   // Enhanced GPU-Native Format
    PSP_KV_FORMAT_BTKV     = 2    // Hardware-Native 2D Block-Tiled Format
} PspKvFormat;

// Packed precision policy (32-bit word)
// Bit layout:
//   bits 0-1 : precision_level (0=FP16, 1=FP8, 2=FP8+Residual, 3=Pruned)
//   bit 2    : use_residual (1 if Level 2)
//   bits 3-7 : reserved (must be zero)
//   bits 8-31: active_head_mask (24 bits)
typedef uint32_t PackedBlockPolicy;

#define PSP_POLICY_PRECISION_MASK      0x3
#define PSP_POLICY_PRECISION_SHIFT     0
#define PSP_POLICY_RESIDUAL_MASK       0x1
#define PSP_POLICY_RESIDUAL_SHIFT      2
#define PSP_POLICY_HEAD_MASK_MASK      0xFFFFFF
#define PSP_POLICY_HEAD_MASK_SHIFT     8

static inline uint32_t psp_policy_pack(
    uint8_t precision_level,
    bool use_residual,
    uint32_t active_head_mask
) {
    return ((uint32_t)precision_level & 0x3) |
           ((uint32_t)use_residual << 2) |
           ((active_head_mask & 0xFFFFFF) << 8);
}

static inline uint8_t psp_policy_get_precision(uint32_t policy) {
    return (uint8_t)(policy & PSP_POLICY_PRECISION_MASK);
}

static inline bool psp_policy_get_residual(uint32_t policy) {
    return (policy >> PSP_POLICY_RESIDUAL_SHIFT) & 1;
}

static inline uint32_t psp_policy_get_head_mask(uint32_t policy) {
    return (policy >> PSP_POLICY_HEAD_MASK_SHIFT) & PSP_POLICY_HEAD_MASK_MASK;
}

// PSP-KV Sidecar Descriptor (64 bytes, aligned to 64)
// Matches Rust `PspKvSidecarDescriptor` exactly.
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

// Compile-time check for size and alignment (C11 static assert)
_Static_assert(sizeof(PspKvSidecarDescriptor) == 64, "Sidecar descriptor must be 64 bytes");
#ifdef __cplusplus
static_assert(sizeof(PspKvSidecarDescriptor) == 64, "Sidecar descriptor must be 64 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif
