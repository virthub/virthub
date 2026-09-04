// virthub/kernels/basic_format/dequant_kernel.cu
//
// Basic Format Dequantization Kernel
//
// Reconstructs FP16 values from quantized FP8 data using per-head affine
// dequantization: x = scale * (q - zero_point).
//
// The quantized data is assumed to be stored in token-major order
// [B_t, D_head] for each head, following an in-band header that contains
// the per-head scale and zero-point arrays. The caller passes pointers to
// the start of the quantized payload and to the output FP16 buffer.
//
// Inputs:
//   quantized_data : pointer to the quantized FP8 payload (uint8_t)
//   output         : pointer to the output FP16 buffer, size B_t * D_head * N_kv
//   scales         : per-head FP32 scale factors, size N_kv
//   zero_points    : per-head FP32 zero-points, size N_kv
//   B_t            : number of tokens in the block
//   D_head         : head dimension
//   N_kv           : number of KV heads

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <stdint.h>
#include <cstdio>

__global__ void dequant_basic_kernel(
    const uint8_t* __restrict__ quantized_data,
    half* __restrict__ output,
    const float* __restrict__ scales,
    const float* __restrict__ zero_points,
    int B_t,
    int D_head,
    int N_kv)
{
    int total_elements = B_t * D_head * N_kv;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;

    if (idx >= total_elements) return;

    // Determine head index. The intra-head token and dimension indices
    // are not needed because dequantization is element-wise.
    int head = idx / (B_t * D_head);

    uint8_t q = quantized_data[idx];
    float scale = scales[head];
    float zero_point = zero_points[head];

    float x = scale * ((float)q - zero_point);
    output[idx] = __float2half(x);
}

void launch_dequant_basic(
    const uint8_t* quantized_data,
    half* output,
    const float* scales,
    const float* zero_points,
    int B_t,
    int D_head,
    int N_kv,
    cudaStream_t stream = 0)
{
    int total_elements = B_t * D_head * N_kv;
    int threads = 256;
    int blocks = (total_elements + threads - 1) / threads;

    dequant_basic_kernel<<<blocks, threads, 0, stream>>>(
        quantized_data,
        output,
        scales,
        zero_points,
        B_t,
        D_head,
        N_kv
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "Failed to launch dequant_basic_kernel: %s\n", cudaGetErrorString(err));
    }
}
