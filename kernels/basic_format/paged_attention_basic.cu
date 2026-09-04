// virthub/kernels/basic_format/paged_attention_basic.cu
//
// Basic Format Paged Attention Kernel
//
// This is a reference implementation of paged attention for the Basic PSP‑KV
// format. In the Basic format, KV cache blocks are stored in a quantized
// (FP8) representation with per‑head affine parameters (scale and zero‑point).
// The dequantization is performed on‑the‑fly inside the kernel, directly
// reconstructing FP16 values from the quantized data.
//
// The kernel processes a single query token for a single query head. It
// iterates over all blocks assigned to the sequence, loads quantized keys and
// values for the corresponding KV head (grouped-query attention), dequantizes
// them, computes attention scores, and accumulates the output.
//
// Inputs:
//   out                : output tensor [num_tokens, num_heads, head_dim] (half)
//   q                  : query tensor [num_tokens, num_heads, head_dim] (half)
//   k_cache            : quantized key cache (uint8_t) with layout:
//                        [num_blocks, num_kv_heads, BLOCK_SIZE, head_dim]
//   v_cache            : quantized value cache with same layout
//   k_scales           : per‑head per‑block scale factors for keys
//                        [num_blocks, num_kv_heads] (float)
//   k_zero_points      : per‑head per‑block zero‑points for keys (float)
//   v_scales           : per‑head per‑block scale factors for values (float)
//   v_zero_points      : per‑head per‑block zero‑points for values (float)
//   block_tables       : [num_seqs, max_blocks_per_seq] block indices (int)
//   context_lens       : [num_seqs] actual number of tokens (int)
//   max_blocks_per_seq : maximum blocks per sequence (int)
//   num_heads          : number of query heads (int)
//   num_kv_heads       : number of KV heads (int)
//   head_dim           : head dimension (int)
//   scale              : softmax scale (float, usually 1/sqrt(head_dim))
//   BLOCK_SIZE         : number of tokens per block (compile‑time constant)

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <cuda_bf16.h>
#include <math.h>
#include <stdint.h>

#define BLOCK_SIZE 16
#define HEAD_DIM 128
#define MAX_KV_HEADS 8

__global__ void paged_attention_basic_kernel(
    half* __restrict__ out,
    const half* __restrict__ q,
    const uint8_t* __restrict__ k_cache,
    const uint8_t* __restrict__ v_cache,
    const float* __restrict__ k_scales,
    const float* __restrict__ k_zero_points,
    const float* __restrict__ v_scales,
    const float* __restrict__ v_zero_points,
    const int* __restrict__ block_tables,
    const int* __restrict__ context_lens,
    int max_blocks_per_seq,
    int num_heads,
    int num_kv_heads,
    int head_dim,
    float scale)
{
    int token_idx = blockIdx.x;          // one token per block
    int head_idx = blockIdx.y;           // one query head per block
    int seq_idx = blockIdx.z;            // one sequence per grid dimension

    if (token_idx >= context_lens[seq_idx]) return;

    // Determine which KV head this query head maps to (GQA)
    int kv_head = head_idx / (num_heads / num_kv_heads);

    // Load query vector for this token and head
    const half* q_ptr = q + (token_idx * num_heads + head_idx) * head_dim;
    float query[HEAD_DIM];
    #pragma unroll
    for (int i = 0; i < HEAD_DIM; ++i) {
        query[i] = __half2float(q_ptr[i]);
    }

    // Accumulators for output
    float acc[HEAD_DIM] = {0.0f};
    float max_score = -INFINITY;
    float sum_exp = 0.0f;

    // Iterate over blocks belonging to this sequence
    int num_blocks = (context_lens[seq_idx] + BLOCK_SIZE - 1) / BLOCK_SIZE;
    for (int block_idx = 0; block_idx < num_blocks; ++block_idx) {
        int physical_block = block_tables[seq_idx * max_blocks_per_seq + block_idx];

        // Compute start token index within this block
        int start_token = block_idx * BLOCK_SIZE;
        int tokens_in_block = min(BLOCK_SIZE, context_lens[seq_idx] - start_token);

        // Load scale and zero‑point for this KV head and block
        float k_scale = k_scales[physical_block * num_kv_heads + kv_head];
        float k_zp    = k_zero_points[physical_block * num_kv_heads + kv_head];
        float v_scale = v_scales[physical_block * num_kv_heads + kv_head];
        float v_zp    = v_zero_points[physical_block * num_kv_heads + kv_head];

        // Pointer to this block's KV cache for the relevant head
        const uint8_t* k_block_ptr = k_cache + ((size_t)physical_block * num_kv_heads + kv_head) * BLOCK_SIZE * HEAD_DIM;
        const uint8_t* v_block_ptr = v_cache + ((size_t)physical_block * num_kv_heads + kv_head) * BLOCK_SIZE * HEAD_DIM;

        // Iterate over tokens in this block
        for (int t = 0; t < tokens_in_block; ++t) {
            // Compute dot product between query and key (dequantize key on‑the‑fly)
            float score = 0.0f;
            const uint8_t* k_token = k_block_ptr + t * HEAD_DIM;
            #pragma unroll
            for (int d = 0; d < HEAD_DIM; ++d) {
                float k_val = k_scale * ((float)k_token[d] - k_zp);
                score += query[d] * k_val;
            }
            score *= scale;

            // Update running max and sum for online softmax
            float new_max = fmaxf(max_score, score);
            float exp_old = expf(max_score - new_max);
            float exp_new = expf(score - new_max);
            sum_exp = sum_exp * exp_old + exp_new;
            max_score = new_max;

            // Dequantize value vector and accumulate with the current score (will be normalized later)
            const uint8_t* v_token = v_block_ptr + t * HEAD_DIM;
            float weight = exp_new; // temporary unnormalized weight
            #pragma unroll
            for (int d = 0; d < HEAD_DIM; ++d) {
                float v_val = v_scale * ((float)v_token[d] - v_zp);
                acc[d] += weight * v_val;
            }
        }
    }

    // Normalize accumulators by sum_exp
    float inv_sum = 1.0f / sum_exp;
    half* out_ptr = out + (token_idx * num_heads + head_idx) * head_dim;
    #pragma unroll
    for (int d = 0; d < HEAD_DIM; ++d) {
        out_ptr[d] = __float2half(acc[d] * inv_sum);
    }
}

void launch_paged_attention_basic(
    half* out,
    const half* q,
    const uint8_t* k_cache,
    const uint8_t* v_cache,
    const float* k_scales,
    const float* k_zero_points,
    const float* v_scales,
    const float* v_zero_points,
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
    // Each block has 256 threads, but our kernel is completely serial per (token, head, seq).
    // We launch a single thread per (token, head, seq) by using a block of 1 thread.
    // Alternatively, we could parallelize over head_dim, but this is a simple reference.
    paged_attention_basic_kernel<<<grid, 1, 0, stream>>>(
        out, q, k_cache, v_cache,
        k_scales, k_zero_points, v_scales, v_zero_points,
        block_tables, context_lens,
        max_blocks_per_seq, num_heads, num_kv_heads, head_dim, scale
    );
}
