// virthub/kernels/btkv_format/tma_descriptors.cu
//
// BT-KV TMA Descriptor Setup
//
// This file provides helper functions to create and manage CUtensorMap
// descriptors for 2D block-tiled KV cache (BT-KV). The descriptors are
// used by Hopper/Blackwell Tensor Memory Accelerator (TMA) to perform
// efficient asynchronous bulk copies between global memory and shared
// memory without SM thread intervention.
//
// The two tile shapes are:
//   K tile: [TILE_TOKENS = 16, TILE_DIMS = 64]
//   V tile: [TILE_DIMS = 64, TILE_TOKENS = 16]
//
// Each physical block may contain multiple heads (GQA), and each head may
// consist of one or more tiles. The descriptors created here are for a
// single tile within a block; the host code can iterate over heads and
// tiles as needed.
//
// Reference:
//   NVIDIA Hopper TMA documentation (CUDA 12.0+)

// Workaround for glibc `_Float32` errors when compiling with C++17.
// CUDA headers include system headers that may define `__STDC_WANT_IEC_60559_TYPES_EXT__`,
// causing declarations using `_Float32` which are not available in C++17.
// Undefining the macro before including CUDA headers prevents these declarations.
#undef __STDC_WANT_IEC_60559_TYPES_EXT__

#include <cuda_runtime.h>
#include <cuda.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BTKV_K_TILE_TOKENS 16
#define BTKV_K_TILE_DIMS   64
#define BTKV_V_TILE_DIMS   64
#define BTKV_V_TILE_TOKENS 16

// Helper macro for error checking
#define CUDA_CHECK(call)                                                      \
    do {                                                                      \
        cudaError_t err = call;                                               \
        if (err != cudaSuccess) {                                             \
            fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__,  \
                    cudaGetErrorString(err));                                 \
            return err;                                                       \
        }                                                                     \
    } while (0)

// Initialize a CUtensorMap for a 2D tile.
//
// This function encodes a 5D tensor map descriptor that describes a
// single tile within a larger KV cache block. The actual global memory
// layout is:
//   K: [num_tokens, head_dim]   (token-major)
//   V: [head_dim, num_tokens]   (head-major, transposed)
//
// For TMA, the innermost dimension is the contiguous one. For K, the
// contiguous dimension is head_dim; for V, it is num_tokens.
//
// Parameters:
//   tensor_map    : pointer to CUtensorMap to be filled
//   gmem_base_ptr : device pointer to the start of the tile
//   is_key        : true if K tile, false if V tile
//   tile_tokens   : number of tokens in the tile (usually 16)
//   tile_dims     : head dimension in the tile (usually 64)
//   num_heads     : total number of KV heads in the block (not used for
//                   single tile encoding, but retained for future multi-head)
//   head_idx      : which head this tile belongs to (affects pointer offset)
//   block_idx     : block index (affects pointer offset)
//   stride_bytes  : global stride between successive rows (in bytes)
//                   For K: stride = head_dim * element_size
//                   For V: stride = num_tokens * element_size
//   element_size  : bytes per element (1 for FP8, 2 for FP16, etc.)
//   swizzle_mode  : TMA swizzle mode (e.g., CU_TENSOR_MAP_SWIZZLE_128B)
//
// Returns:
//   cudaSuccess on success, otherwise CUDA error.
cudaError_t init_btkv_tile_tensor_map(
    CUtensorMap* tensor_map,
    void* gmem_base_ptr,
    bool is_key,
    int tile_tokens,
    int tile_dims,
    int num_heads,
    int head_idx,
    int block_idx,
    size_t stride_bytes,
    int element_size,
    CUtensorMapSwizzle swizzle_mode)
{
    // For a single tile, the global dimensions are:
    //   dimension 0 (innermost): contiguous dimension size
    //   dimension 1: other dimension size
    //   dimensions 2-4: unused (size 1)
    uint64_t gmem_dims[5];
    uint64_t gmem_strides[4]; // strides for dims 1..4 (in bytes)

    if (is_key) {
        // K tile: [tokens, dims] with dims contiguous
        gmem_dims[0] = tile_dims;         // innermost: head_dim
        gmem_dims[1] = tile_tokens;       // tokens
    } else {
        // V tile: [dims, tokens] with tokens contiguous
        gmem_dims[0] = tile_tokens;       // innermost: tokens
        gmem_dims[1] = tile_dims;         // head_dim
    }
    gmem_dims[2] = 1;
    gmem_dims[3] = 1;
    gmem_dims[4] = 1;

    // Stride for dimension 1 (between rows)
    gmem_strides[0] = stride_bytes;
    gmem_strides[1] = 0; // unused
    gmem_strides[2] = 0;
    gmem_strides[3] = 0;

    // Shared memory box dimensions: same as global dimensions
    uint32_t smem_dims[5] = {
        (uint32_t)gmem_dims[0],
        (uint32_t)gmem_dims[1],
        1, 1, 1
    };

    // Shared memory strides (in elements, not bytes). For TMA, strides are
    // in units of elements, not bytes. We set them to 1 for contiguous.
    uint32_t smem_strides[5] = {1, 1, 1, 1, 1};

    // Determine element type for TMA
    CUtensorMapDataType data_type;
    if (element_size == 1) {
        data_type = CU_TENSOR_MAP_DATA_TYPE_UINT8;
    } else if (element_size == 2) {
        data_type = CU_TENSOR_MAP_DATA_TYPE_FLOAT16;
    } else if (element_size == 4) {
        data_type = CU_TENSOR_MAP_DATA_TYPE_FLOAT32;
    } else {
        return cudaErrorInvalidValue;
    }

    // Encode the tensor map
    CUresult res = cuTensorMapEncodeTiled(
        tensor_map,
        data_type,
        5,                   // tensor rank
        gmem_base_ptr,
        gmem_dims,
        gmem_strides,
        smem_dims,
        smem_strides,
        CU_TENSOR_MAP_INTERLEAVE_NONE,
        swizzle_mode,
        CU_TENSOR_MAP_L2_PROMOTION_L2_128B,
        CU_TENSOR_MAP_FLOAT_OOB_FILL_NONE
    );

    if (res != CUDA_SUCCESS) {
        return cudaErrorUnknown;
    }
    return cudaSuccess;
}

// Higher-level helper: initialize tensor maps for all tiles of a KV cache
// block. This function is provided as a reference; actual implementation
// may vary.
//
// Parameters:
//   k_maps      : array of CUtensorMap for K tiles (size num_heads * tiles_per_head)
//   v_maps      : array of CUtensorMap for V tiles (size num_heads * tiles_per_head)
//   k_base_ptr  : device pointer to start of K cache for this block
//   v_base_ptr  : device pointer to start of V cache for this block
//   num_heads   : number of KV heads
//   head_dim    : head dimension (must be divisible by TILE_DIMS)
//   block_tokens: tokens per block (must be divisible by TILE_TOKENS)
//   element_size: bytes per element
//   swizzle_mode: TMA swizzle mode
cudaError_t init_btkv_block_tensor_maps(
    CUtensorMap* k_maps,
    CUtensorMap* v_maps,
    void* k_base_ptr,
    void* v_base_ptr,
    int num_heads,
    int head_dim,
    int block_tokens,
    int element_size,
    CUtensorMapSwizzle swizzle_mode)
{
    if (head_dim % BTKV_K_TILE_DIMS != 0) {
        return cudaErrorInvalidValue;
    }
    if (block_tokens % BTKV_K_TILE_TOKENS != 0) {
        return cudaErrorInvalidValue;
    }

    int tiles_per_head = (head_dim / BTKV_K_TILE_DIMS) *
                         (block_tokens / BTKV_K_TILE_TOKENS);
    // For simplicity, assume one tile per head. Real code loops over sub-tiles.
    if (tiles_per_head != 1) {
        // This reference implementation only supports single tile per head.
        // Extend for multi-tile if needed.
        return cudaErrorNotSupported;
    }

    size_t k_stride = (size_t)head_dim * element_size;       // for K: row stride = head_dim * elem
    size_t v_stride = (size_t)block_tokens * element_size;   // for V: row stride = tokens * elem

    for (int h = 0; h < num_heads; ++h) {
        // Offset for this head: each head occupies block_tokens * head_dim elements
        size_t head_offset = (size_t)h * block_tokens * head_dim * element_size;
        void* k_head_ptr = (char*)k_base_ptr + head_offset;
        void* v_head_ptr = (char*)v_base_ptr + head_offset;

        CUDA_CHECK(init_btkv_tile_tensor_map(
            &k_maps[h],
            k_head_ptr,
            true,               // key tile
            BTKV_K_TILE_TOKENS,
            BTKV_K_TILE_DIMS,
            num_heads,
            h,
            0,                  // block_idx (not used in pointer offset here)
            k_stride,
            element_size,
            swizzle_mode
        ));

        CUDA_CHECK(init_btkv_tile_tensor_map(
            &v_maps[h],
            v_head_ptr,
            false,              // value tile
            BTKV_V_TILE_TOKENS,
            BTKV_V_TILE_DIMS,
            num_heads,
            h,
            0,
            v_stride,
            element_size,
            swizzle_mode
        ));
    }

    return cudaSuccess;
}
