// virthub/kernels/enhanced_format/dequant_kernel.cu
//
// Enhanced Format Dequantization Kernel
//
// Dequantizes FP8 data to FP16 using symmetric power‑of‑two scaling.
// The format uses per‑head E8M0 scale exponents stored in an out‑of‑band
// sidecar descriptor. The kernel supports both key (K) and value (V)
// layouts:
//   - K: token‑major layout [B_t, D_head]
//   - V: transposed layout [D_head, B_t]
//
// Inputs:
//   quantized_data : pointer to the quantized FP8 payload (uint8_t)
//   output         : pointer to the output FP16 buffer, size num_tokens * head_dim * num_heads
//   scales_e8m0    : array of per‑head scale exponents (uint8_t), length num_heads
//   num_tokens     : number of tokens in the block (B_t)
//   head_dim       : head dimension (D_head)
//   num_heads      : number of KV heads (N_kv)
//   is_transposed  : true for V layout ([D_head, B_t]), false for K layout ([B_t, D_head])

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

__global__ void dequant_enhanced_kernel(
    const uint8_t* __restrict__ quantized_data,
    half* __restrict__ output,
    const uint8_t* __restrict__ scales_e8m0,
    int num_tokens,
    int head_dim,
    int num_heads,
    bool is_transposed)
{
    int total_elements = num_tokens * head_dim * num_heads;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total_elements) return;

    // Determine which head this element belongs to based on layout
    int head;
    if (is_transposed) {
        // V layout: [D_head, B_t] per head, stored as head * (D_head * B_t) + d * B_t + t
        int per_head = head_dim * num_tokens;
        head = idx / per_head;
    } else {
        // K layout: [B_t, D_head] per head, stored as head * (B_t * D_head) + t * D_head + d
        int per_head = num_tokens * head_dim;
        head = idx / per_head;
    }

    // E8M0 scale: factor = 2^exponent
    float scale = powf(2.0f, (float)scales_e8m0[head]);
    float x = scale * (float)quantized_data[idx];
    output[idx] = __float2half(x);
}

void launch_dequant_enhanced(
    const uint8_t* quantized_data,
    half* output,
    const uint8_t* scales_e8m0,
    int num_tokens,
    int head_dim,
    int num_heads,
    bool is_transposed,
    cudaStream_t stream = 0)
{
    int total_elements = num_tokens * head_dim * num_heads;
    int threads = 256;
    int blocks = (total_elements + threads - 1) / threads;

    dequant_enhanced_kernel<<<blocks, threads, 0, stream>>>(
        quantized_data,
        output,
        scales_e8m0,
        num_tokens,
        head_dim,
        num_heads,
        is_transposed
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "Enhanced dequant kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}
