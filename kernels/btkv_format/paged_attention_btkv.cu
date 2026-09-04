// virthub/kernels/btkv_format/paged_attention_btkv.cu
//
// BT‑KV Format Paged Attention Kernel (Reference Implementation)
//
// This kernel implements paged attention for the Hardware‑Native 2D
// Block‑Tiled Format (BT‑KV). In BT‑KV, KV cache blocks are partitioned
// into 2D tiles:
//   - K tiles: [16, 64]  (tokens × head_dim)
//   - V tiles: [64, 16]  (head_dim × tokens)
// and scale exponents (E8M0) are interleaved with the quantized payload
// inside each tile container.
//
// For this reference, we simplify by assuming each block is stored as a
// single 2D tile of shape [BLOCK_SIZE, HEAD_DIM] for K and [HEAD_DIM,
// BLOCK_SIZE] for V. The per‑head scale exponents are provided as separate
// arrays (in production they would be fetched via TMA together with the
// data). The kernel still demonstrates the transposed V layout and
// symmetric power‑of‑two dequantization.
//
// Inputs:
//   out                : output tensor [num_tokens, num_heads, head_dim] (half)
//   q                  : query tensor [num_tokens, num_heads, head_dim] (half)
//   k_cache            : quantized key cache (uint8_t)
//                        layout: [num_blocks, num_kv_heads, BLOCK_SIZE, head_dim]
//   v_cache            : quantized value cache (uint8_t)
//                        layout: [num_blocks, num_kv_heads, head_dim, BLOCK_SIZE]
//   k_scales_e8m0      : key scale exponents (uint8_t)
//                        layout: [num_blocks, num_kv_heads]
//   v_scales_e8m0      : value scale exponents (uint8_t)
//                        layout: [num_blocks, num_kv_heads]
//   block_tables       : [num_seqs, max_blocks_per_seq] physical block indices
//   context_lens       : [num_seqs] actual sequence lengths
//   max_blocks_per_seq : maximum number of blocks per sequence
//   num_heads          : number of query heads
//   num_kv_heads       : number of KV heads
//   head_dim           : head dimension
//   scale              : softmax scale (usually 1/sqrt(head_dim))

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define BLOCK_SIZE 16
#define HEAD_DIM 128
#define MAX_KV_HEADS 8

__global__ void paged_attention_btkv_kernel(
    half* __restrict__ out,
    const half* __restrict__ q,
    const uint8_t* __restrict__ k_cache,
    const uint8_t* __restrict__ v_cache,
    const uint8_t* __restrict__ k_scales_e8m0,
    const uint8_t* __restrict__ v_scales_e8m0,
    const int* __restrict__ block_tables,
    const int* __restrict__ context_lens,
    int max_blocks_per_seq,
    int num_heads,
    int num_kv_heads,
    int head_dim,
    float scale)
{
    int token_idx = blockIdx.x;
    int head_idx = blockIdx.y;
    int seq_idx = blockIdx.z;

    if (token_idx >= context_lens[seq_idx]) return;

    int kv_head = head_idx / (num_heads / num_kv_heads);

    const half* q_ptr = q + (token_idx * num_heads + head_idx) * head_dim;
    float query[HEAD_DIM];
    #pragma unroll
    for (int i = 0; i < HEAD_DIM; ++i) {
        query[i] = __half2float(q_ptr[i]);
    }

    float acc[HEAD_DIM] = {0.0f};
    float max_score = -INFINITY;
    float sum_exp = 0.0f;

    int num_blocks = (context_lens[seq_idx] + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (int block_idx = 0; block_idx < num_blocks; ++block_idx) {
        int physical_block = block_tables[seq_idx * max_blocks_per_seq + block_idx];

        uint8_t k_scale_exp = k_scales_e8m0[physical_block * num_kv_heads + kv_head];
        uint8_t v_scale_exp = v_scales_e8m0[physical_block * num_kv_heads + kv_head];
        float k_scale = powf(2.0f, (float)k_scale_exp);
        float v_scale = powf(2.0f, (float)v_scale_exp);

        int start_token = block_idx * BLOCK_SIZE;
        int tokens_in_block = min(BLOCK_SIZE, context_lens[seq_idx] - start_token);

        // BT‑KV tile pointers
        const uint8_t* k_tile = k_cache + ((size_t)physical_block * num_kv_heads + kv_head) * BLOCK_SIZE * HEAD_DIM;
        const uint8_t* v_tile = v_cache + ((size_t)physical_block * num_kv_heads + kv_head) * HEAD_DIM * BLOCK_SIZE;

        for (int t = 0; t < tokens_in_block; ++t) {
            float score = 0.0f;
            const uint8_t* k_row = k_tile + t * HEAD_DIM;
            #pragma unroll
            for (int d = 0; d < HEAD_DIM; ++d) {
                float k_val = k_scale * (float)k_row[d];
                score += query[d] * k_val;
            }
            score *= scale;

            float new_max = fmaxf(max_score, score);
            float exp_old = expf(max_score - new_max);
            float exp_new = expf(score - new_max);
            sum_exp = sum_exp * exp_old + exp_new;
            max_score = new_max;

            float weight = exp_new;
            // Value is stored transposed: V[d, t] -> column t has stride BLOCK_SIZE
            const uint8_t* v_col = v_tile + t;
            #pragma unroll
            for (int d = 0; d < HEAD_DIM; ++d) {
                float v_val = v_scale * (float)v_col[d * BLOCK_SIZE];
                acc[d] += weight * v_val;
            }
        }
    }

    float inv_sum = 1.0f / sum_exp;
    half* out_ptr = out + (token_idx * num_heads + head_idx) * head_dim;
    #pragma unroll
    for (int d = 0; d < HEAD_DIM; ++d) {
        out_ptr[d] = __float2half(acc[d] * inv_sum);
    }
}

void launch_paged_attention_btkv(
    half* out,
    const half* q,
    const uint8_t* k_cache,
    const uint8_t* v_cache,
    const uint8_t* k_scales_e8m0,
    const uint8_t* v_scales_e8m0,
    const int* block_tables,
    const int* context_lens,
    int max_blocks_per_seq,
    int num_tokens,
    int num_seqs,
    int num_heads,
    int num_kv_heads,
    int head_dim,
    float scale,
    cudaStream_t stream = 0)
{
    dim3 grid(num_tokens, num_heads, num_seqs);
    paged_attention_btkv_kernel<<<grid, 1, 0, stream>>>(
        out, q, k_cache, v_cache,
        k_scales_e8m0, v_scales_e8m0,
        block_tables, context_lens,
        max_blocks_per_seq, num_heads, num_kv_heads, head_dim, scale
    );

    cudaError_t err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "BT-KV paged attention kernel launch failed: %s\n", cudaGetErrorString(err));
    }
}
