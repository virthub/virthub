# virthub/kernels/triton/psp_kv_btkv.py
#
# Hardware-Native 2D Block-Tiled Format (BT-KV) Paged Attention
# Implements paged attention for BT-KV format with 2D hardware-aligned tiles.
# Tile sizes: K tile [16, 64] (tokens × head_dim), V tile [64, 16] (head_dim × tokens)
# Uses symmetric power-of-two scaling (E8M0) for quantized data.
# Scale exponents stored out-of-band (reference implementation).
#
# Layout assumptions:
#   - BLOCK_SIZE = 16 tokens (one tile row)
#   - head_dim multiple of 64; kernel splits into 64-dim tiles for offsets
#   - V stored transposed: [head_dim, BLOCK_SIZE] per block/head
#
# This kernel processes one query token, one query head, and one sequence,
# looping over blocks with on-the-fly dequantization and attention accumulation.
#
# GPU required; optimized for Triton compiler with hardware-aligned access patterns.
#
# Hardware‑Native 2D Block‑Tiled Format (BT‑KV) Paged Attention (Triton Reference)
# This kernel implements paged attention for the BT‑KV format.
# BT‑KV stores KV cache as 2D hardware‑aligned tiles:
#   K tile: [16, 64]   (tokens × head_dim)
#   V tile: [64, 16]   (head_dim × tokens)
# The quantized data uses symmetric power‑of‑two scaling (E8M0), with
# scale exponents stored either interleaved in tile containers or
# out‑of‑band (this reference uses out‑of‑band arrays for simplicity).
#
# This Triton kernel treats each block as containing multiple heads and
# possibly multiple tiles per head. For clarity, we assume:
#   - BLOCK_SIZE = 16 tokens (one tile row)
#   - head_dim can be any multiple of 64; the kernel internally splits
#     the head_dim dimension into tiles of 64 when computing offsets.
#   - V is stored transposed: [head_dim, BLOCK_SIZE] per block/head.
#
# The kernel processes one query token, one query head, and one sequence.
# It loops over blocks, dequantizes on the fly, and accumulates attention.

import torch
import triton
import triton.language as tl

@triton.jit
def _paged_attention_btkv_kernel(
    Q,                       # [num_tokens, num_heads, head_dim] fp16
    K_cache,                 # [num_blocks, num_kv_heads, BLOCK_SIZE, head_dim] uint8
    V_cache,                 # [num_blocks, num_kv_heads, head_dim, BLOCK_SIZE] uint8
    K_scales_e8m0,           # [num_blocks, num_kv_heads] uint8
    V_scales_e8m0,           # [num_blocks, num_kv_heads] uint8
    Block_tables,            # [num_seqs, max_blocks_per_seq] int32
    Context_lens,            # [num_seqs] int32
    Out,                     # [num_tokens, num_heads, head_dim] fp16
    max_blocks_per_seq,
    num_heads,
    num_kv_heads,
    head_dim: tl.constexpr,
    BLOCK_SIZE: tl.constexpr,   # 16
    TILE_DIM: tl.constexpr,     # 64 (dimension of one tile side)
    scale: tl.constexpr,
):
    token_idx = tl.program_id(0)
    head_idx = tl.program_id(1)
    seq_idx = tl.program_id(2)

    if token_idx >= tl.load(Context_lens + seq_idx):
        return

    kv_head = head_idx // (num_heads // num_kv_heads)

    q_offset = (token_idx * num_heads + head_idx) * head_dim
    q = tl.load(Q + q_offset + tl.arange(0, head_dim))
    q = q.to(tl.float32)

    acc = tl.zeros([head_dim], dtype=tl.float32)
    max_score = tl.full([1], float("-inf"), dtype=tl.float32)
    sum_exp = tl.zeros([1], dtype=tl.float32)

    num_blocks = (tl.load(Context_lens + seq_idx) + BLOCK_SIZE - 1) // BLOCK_SIZE

    for block_idx in range(num_blocks):
        physical_block = tl.load(
            Block_tables + seq_idx * max_blocks_per_seq + block_idx
        )

        k_exp = tl.load(K_scales_e8m0 + physical_block * num_kv_heads + kv_head)
        v_exp = tl.load(V_scales_e8m0 + physical_block * num_kv_heads + kv_head)
        k_scale = tl.exp2(k_exp.to(tl.float32))
        v_scale = tl.exp2(v_exp.to(tl.float32))

        k_base = (
            K_cache
            + (physical_block * num_kv_heads + kv_head) * BLOCK_SIZE * head_dim
        )
        v_base = (
            V_cache
            + (physical_block * num_kv_heads + kv_head) * head_dim * BLOCK_SIZE
        )

        start_token = block_idx * BLOCK_SIZE
        tokens_in_block = min(
            BLOCK_SIZE, tl.load(Context_lens + seq_idx) - start_token
        )

        for t in range(tokens_in_block):
            k_offsets = k_base + t * head_dim + tl.arange(0, head_dim)
            k_q = tl.load(k_offsets).to(tl.float32)
            k_vals = k_scale * k_q

            score = tl.sum(q * k_vals) * scale

            new_max = tl.maximum(max_score, score)
            exp_old = tl.exp(max_score - new_max)
            exp_new = tl.exp(score - new_max)
            sum_exp = sum_exp * exp_old + exp_new
            max_score = new_max

            v_offsets = v_base + tl.arange(0, head_dim) * BLOCK_SIZE + t
            v_q = tl.load(v_offsets).to(tl.float32)
            v_vals = v_scale * v_q

            acc += exp_new * v_vals

    acc = acc / sum_exp
    out_offsets = (token_idx * num_heads + head_idx) * head_dim + tl.arange(0, head_dim)
    tl.store(Out + out_offsets, acc.to(tl.float16))


def paged_attention_btkv(
    q: torch.Tensor,                # [num_tokens, num_heads, head_dim] fp16
    k_cache: torch.Tensor,          # [num_blocks, num_kv_heads, BLOCK_SIZE, head_dim] uint8
    v_cache: torch.Tensor,          # [num_blocks, num_kv_heads, head_dim, BLOCK_SIZE] uint8
    k_scales_e8m0: torch.Tensor,    # [num_blocks, num_kv_heads] uint8
    v_scales_e8m0: torch.Tensor,    # [num_blocks, num_kv_heads] uint8
    block_tables: torch.Tensor,     # [num_seqs, max_blocks_per_seq] int32
    context_lens: torch.Tensor,     # [num_seqs] int32
    max_blocks_per_seq: int,
    num_heads: int,
    num_kv_heads: int,
    head_dim: int,
    scale: float,
    tile_dim: int = 64,             # dimension of one tile (64 for BT‑KV)
):
    num_tokens, num_seqs = q.shape[0], block_tables.shape[0]
    out = torch.empty_like(q)

    grid = (num_tokens, num_heads, num_seqs)
    _paged_attention_btkv_kernel[grid](
        q,
        k_cache,
        v_cache,
        k_scales_e8m0,
        v_scales_e8m0,
        block_tables,
        context_lens,
        out,
        max_blocks_per_seq,
        num_heads,
        num_kv_heads,
        head_dim=head_dim,
        BLOCK_SIZE=16,
        TILE_DIM=tile_dim,
        scale=scale,
    )
    return out
