# scripts/benchmark_vllm_virthub.py
#
# Benchmark vLLM with and without the Virthub KV connector.
#
# Measures:
#   - Throughput (tokens/sec)
#   - Time to first token (TTFT)
#   - GPU memory usage (total used via NVML, because vLLM V1 runs in a child process)
#   - KV cache usage (if accessible)
#
# Uses facebook/opt-125m (small model) for quick runs.
#
# Usage:
#   python scripts/benchmark_vllm_virthub.py [--prompts N] [--max-tokens M] [--model MODEL]
#                                             [--iterations I] [--warmup W] [--output FILE]

import argparse
import gc
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

os.environ.setdefault("VLLM_WORKER_MULTIPROC_METHOD", "spawn")

try:
    import torch
    if torch.cuda.is_available():
        venv = Path(sys.prefix)
        py_ver = f"{sys.version_info.major}.{sys.version_info.minor}"
        lib_dirs = [
            venv / "lib" / f"python{py_ver}" / "site-packages" / "nvidia" / "cu13" / "lib",
            venv / "lib" / f"python{py_ver}" / "site-packages" / "torch" / "lib",
            venv / "lib" / f"python{py_ver}" / "site-packages" / "nvidia" / "nccl" / "lib",
        ]
        existing = [str(d) for d in lib_dirs if d.exists()]
        if existing:
            os.environ["LD_LIBRARY_PATH"] = ":".join(existing) + ":" + os.environ.get("LD_LIBRARY_PATH", "")
except Exception:
    pass  # ignore import errors during environment setup

try:
    from vllm import LLM, SamplingParams
except ImportError as e:
    print("vLLM is not available in this environment:", e)
    sys.exit(1)

DEFAULT_MODEL = "facebook/opt-125m"
DEFAULT_PROMPTS = [
    "Hello, world! The answer is",
    "The capital of France is",
    "Machine learning is a field",
    "In the beginning, the universe was",
    "Once upon a time, there was a",
    "The quick brown fox jumps over",
    "To be or not to be, that is",
    "The future of AI is",
]


def get_gpu_memory_used_nvml() -> int:
    """
    Return total GPU memory currently used (in bytes) using nvidia-smi.
    If nvidia-smi is unavailable or fails, return 0.
    """
    try:
        out = subprocess.check_output(
            ["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
            universal_newlines=True,
        )
        # Assume first line contains the value for GPU 0.
        mem_mb = int(out.strip().split("\n")[0])
        return mem_mb * 1024 * 1024
    except Exception:
        return 0


def measure_gpu_memory() -> Tuple[int, int]:
    """
    Return (total_used_bytes, 0). The second value is kept for API compatibility.
    vLLM V1 runs in a separate process, so we must query NVML to capture the
    memory used by that child process.
    """
    used = get_gpu_memory_used_nvml()
    return used, 0


def get_kv_cache_stats(engine: LLM) -> Tuple[float, int]:
    """Get KV cache usage statistics from vLLM engine."""
    try:
        if hasattr(engine, 'llm_engine'):
            scheduler = engine.llm_engine.scheduler
            if hasattr(scheduler, 'block_manager'):
                bm = scheduler.block_manager
                if hasattr(bm, 'num_free_blocks') and hasattr(bm, 'num_total_blocks'):
                    free = bm.num_free_blocks
                    total = bm.num_total_blocks
                    usage = (1 - free / total) * 100 if total > 0 else 0
                    return usage, total
    except Exception:
        pass
    return 0.0, 0


def create_engine(use_virthub: bool, model: str, **kwargs) -> LLM:
    """Create vLLM engine with optional Virthub connector."""
    engine_kwargs = {
        "model": model,
        "enforce_eager": True,
        "disable_log_stats": True,
        **kwargs,
    }

    if use_virthub:
        engine_kwargs["kv_transfer_config"] = {
            "kv_connector": "virthub",
            "kv_role": "kv_both",
        }
        print("  Creating engine with Virthub connector...")
    else:
        print("  Creating baseline engine...")

    return LLM(**engine_kwargs)


def run_benchmark_iteration(
    engine: LLM,
    sampling_params: SamplingParams,
    prompts: List[str],
) -> Tuple[List[float], int, float]:
    """Run a single benchmark iteration, returning (ttft_list, total_tokens, throughput)."""
    ttft_list = []
    total_tokens = 0

    start_total = time.perf_counter()

    for prompt in prompts:
        start_prompt = time.perf_counter()
        outputs = engine.generate(prompt, sampling_params)
        end_prompt = time.perf_counter()

        ttft_list.append(end_prompt - start_prompt)
        total_tokens += len(outputs[0].outputs[0].token_ids)

    elapsed_total = time.perf_counter() - start_total
    throughput = total_tokens / elapsed_total if elapsed_total > 0 else 0.0

    return ttft_list, total_tokens, throughput


class BenchmarkResult:
    """Container for benchmark results with statistical analysis."""

    def __init__(self, use_virthub: bool):
        self.use_virthub = use_virthub
        self.ttft_list: List[float] = []
        self.throughput_list: List[float] = []
        self.total_tokens = 0
        self.elapsed_sec = 0.0
        self.gpu_mem_allocated = 0
        self.gpu_mem_reserved = 0
        self.kv_cache_usage_pct = 0.0
        self.kv_cache_blocks = 0

    @property
    def avg_ttft(self) -> float:
        return statistics.mean(self.ttft_list) if self.ttft_list else 0.0

    @property
    def median_ttft(self) -> float:
        return statistics.median(self.ttft_list) if self.ttft_list else 0.0

    @property
    def p95_ttft(self) -> float:
        if not self.ttft_list:
            return 0.0
        sorted_ttft = sorted(self.ttft_list)
        idx = min(int(len(sorted_ttft) * 0.95), len(sorted_ttft) - 1)
        return sorted_ttft[idx]

    @property
    def avg_throughput(self) -> float:
        return statistics.mean(self.throughput_list) if self.throughput_list else 0.0

    def to_dict(self) -> Dict:
        return {
            "use_virthub": self.use_virthub,
            "total_tokens": self.total_tokens,
            "elapsed_sec": self.elapsed_sec,
            "avg_throughput_tokens_per_sec": self.avg_throughput,
            "avg_ttft_ms": self.avg_ttft * 1000,
            "median_ttft_ms": self.median_ttft * 1000,
            "p95_ttft_ms": self.p95_ttft * 1000,
            "gpu_mem_allocated_mb": self.gpu_mem_allocated / 1e6,
            "gpu_mem_reserved_mb": self.gpu_mem_reserved / 1e6,
            "kv_cache_usage_pct": self.kv_cache_usage_pct,
            "kv_cache_blocks": self.kv_cache_blocks,
        }


def run_benchmark(
    use_virthub: bool,
    model: str,
    prompts: List[str],
    max_tokens: int,
    num_iterations: int = 3,
    warmup_iterations: int = 1,
) -> BenchmarkResult:
    """Run complete benchmark with multiple iterations."""
    result = BenchmarkResult(use_virthub)
    engine = create_engine(use_virthub, model)
    sampling_params = SamplingParams(
        temperature=0,
        max_tokens=max_tokens,
        seed=42,
    )

    # Warmup
    print("  Warming up...")
    for i in range(warmup_iterations):
        engine.generate("Warm up prompt", sampling_params)

    # Run benchmark iterations
    print(f"  Running {num_iterations} iterations...")
    for i in range(num_iterations):
        ttft_list, total_tokens, throughput = run_benchmark_iteration(
            engine, sampling_params, prompts
        )

        result.ttft_list.extend(ttft_list)
        result.total_tokens = total_tokens
        result.throughput_list.append(throughput)

        # GC between iterations
        gc.collect()
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

    result.elapsed_sec = sum(result.ttft_list)

    # Measure total GPU memory used by all processes (engine runs in child process)
    result.gpu_mem_allocated, result.gpu_mem_reserved = measure_gpu_memory()

    # Get KV cache stats
    result.kv_cache_usage_pct, result.kv_cache_blocks = get_kv_cache_stats(engine)

    # Cleanup
    del engine
    gc.collect()
    if torch.cuda.is_available():
        torch.cuda.empty_cache()

    return result


def print_comparison(baseline: BenchmarkResult, virthub: BenchmarkResult):
    """Print detailed comparison between baseline and Virthub."""
    print("\n" + "=" * 80)
    print("Benchmark Comparison")
    print("=" * 80)

    metrics = [
        ("Total tokens", f"{baseline.total_tokens}", f"{virthub.total_tokens}"),
        ("Avg TTFT (ms)", f"{baseline.avg_ttft*1000:.2f}", f"{virthub.avg_ttft*1000:.2f}"),
        ("Median TTFT (ms)", f"{baseline.median_ttft*1000:.2f}", f"{virthub.median_ttft*1000:.2f}"),
        ("P95 TTFT (ms)", f"{baseline.p95_ttft*1000:.2f}", f"{virthub.p95_ttft*1000:.2f}"),
        ("Throughput (tok/s)", f"{baseline.avg_throughput:.1f}", f"{virthub.avg_throughput:.1f}"),
        ("GPU Mem Alloc (MB)", f"{baseline.gpu_mem_allocated/1e6:.1f}", f"{virthub.gpu_mem_allocated/1e6:.1f}"),
        ("GPU Mem Reserved (MB)", f"{baseline.gpu_mem_reserved/1e6:.1f}", f"{virthub.gpu_mem_reserved/1e6:.1f}"),
        ("KV Cache Usage (%)", f"{baseline.kv_cache_usage_pct:.1f}", f"{virthub.kv_cache_usage_pct:.1f}"),
    ]

    print(f"{'Metric':<25}{'Baseline':>20}{'Virthub':>20}")
    print("-" * 80)

    for name, b_val, v_val in metrics:
        print(f"{name:<25}{b_val:>20}{v_val:>20}")

    print("\nRelative Changes:")
    changes = {
        "TTFT (avg)": ((virthub.avg_ttft - baseline.avg_ttft) / baseline.avg_ttft * 100) if baseline.avg_ttft > 0 else 0,
        "TTFT (median)": ((virthub.median_ttft - baseline.median_ttft) / baseline.median_ttft * 100) if baseline.median_ttft > 0 else 0,
        "Throughput": ((virthub.avg_throughput - baseline.avg_throughput) / baseline.avg_throughput * 100) if baseline.avg_throughput > 0 else 0,
        "GPU Memory": ((virthub.gpu_mem_allocated - baseline.gpu_mem_allocated) / baseline.gpu_mem_allocated * 100) if baseline.gpu_mem_allocated > 0 else 0,
    }

    for name, change in changes.items():
        sign = "+" if change >= 0 else ""
        print(f"  {name}: {sign}{change:.2f}%")


def main():
    parser = argparse.ArgumentParser(description="Enhanced benchmark for vLLM with Virthub")
    parser.add_argument("--prompts", type=int, default=8, help="Number of prompts")
    parser.add_argument("--max-tokens", type=int, default=32, help="Max tokens per generation")
    parser.add_argument("--model", type=str, default=DEFAULT_MODEL, help="Model name")
    parser.add_argument("--iterations", type=int, default=3, help="Number of benchmark iterations")
    parser.add_argument("--warmup", type=int, default=1, help="Warmup iterations")
    parser.add_argument("--output", type=str, help="Output JSON file for results")
    args = parser.parse_args()

    prompts = DEFAULT_PROMPTS[: args.prompts]

    print(f"Benchmark Configuration:")
    print(f"  Model: {args.model}")
    print(f"  Prompts: {len(prompts)}")
    print(f"  Max tokens: {args.max_tokens}")
    print(f"  Iterations: {args.iterations}")
    print(f"  Warmup: {args.warmup}")

    results = {}

    # Baseline
    print("\nBaseline run:")
    results["baseline"] = run_benchmark(
        False, args.model, prompts, args.max_tokens,
        args.iterations, args.warmup
    )

    # Virthub
    print("\nVirthub run:")
    results["virthub"] = run_benchmark(
        True, args.model, prompts, args.max_tokens,
        args.iterations, args.warmup
    )

    # Print comparison
    print_comparison(results["baseline"], results["virthub"])

    # Save results
    if args.output:
        output_data = {
            "baseline": results["baseline"].to_dict(),
            "virthub": results["virthub"].to_dict(),
        }
        with open(args.output, 'w') as f:
            json.dump(output_data, f, indent=2)
        print(f"\nResults saved to {args.output}")


if __name__ == "__main__":
    main()
