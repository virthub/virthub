// virthub/kernels/btkv_format/dequant_kernel.cu
//
// BT‑KV Format Dequantization Kernel
//
// Reconstructs FP16 values from quantized FP8 data using symmetric
// power‑of‑two scaling (E8M0). The BT‑KV layout stores data as 2D tiles
// with interleaved micro‑scale stripes. For this reference implementation,
// we assume the scale exponents are provided as a separate contiguous
// array, but a production kernel can fetch them interleaved with the tile
// payload. The kernel supports both key (K) and value (V) tile shapes.
//
// Inputs:
//   quantized_data : pointer to the quantized FP8 payload (uint8_t)
//   output         : pointer to the output FP16 buffer, size tile_x * tile_y
//   scales_e8m0    : array of scale exponents (uint8_t), length ceil(num_elements / group_size)
//   tile_x         : first dimension of the 2D tile (e.g., 16 for K, 64 for V)
//   tile_y         : second dimension of the tile (e.g., 64 for K, 16 for V)
//   group_size     : number of elements sharing one scale (default 32)
//   is_transposed  : if true, the output is written in transposed order
//                    (used for V tiles to match [D_head, B_t] memory layout)

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

__global__ void dequant_btkv_kernel(
    const uint8_t* __restrict__ quantized_data,
    half* __restrict__ output,
    const uint8_t* __restrict__ scales_e8m0,
    int tile_x,
    int tile_y,
    int group_size,
    bool is_transposed)
{
    int total_elements = tile_x * tile_y;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total_elements) return;

    // Determine group index for this element
    int group = idx / group_size;
    float scale = powf(2.0f, (float)scales_e8m0[group]);

    float x = scale * (float)quantized_data[idx];

    int output_idx;
    if (is_transposed) {
        // For V tiles, the logical layout is [head_dim, tokens],
        // but the data is stored as [tokens, head_dim]. We need to transpose.
        int token = idx % tile_y;     // original token index (fast dimension)
        int dim   = idx / tile_y;     // original head dim index (slow dimension)
        output_idx = dim * tile_x + token; // transposed: [head_dim, tokens]
    } else {
        output_idx = idx;
    }

    output[output_idx] = __float2half(x);
}

void launch_dequant_btkv(
    const uint8_t* quantized_data,
    half* output,
    const uint8_t* scales_e8m0,
    int tile_x,
    int tile_y,
    int group_size,
    bool is_transposed,
    cudaStream_t stream = 0)
{
    int total_elements = tile_x * tile_y;
    int threads = 256;
    int blocks = (total_elements + threads - 1) / threads;

    dequant_btkv_kernel<<<blocks, threads, 0, stream>>>(
        quantized_data,
        output,
        scales_e8m0,
        tile_x,
        tile_y,
        group_size,
        is_transposed
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "BT-KV dequant kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}
