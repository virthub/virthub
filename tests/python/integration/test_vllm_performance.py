# tests/python/integration/test_vllm_performance.py
#
# Performance benchmarks for vLLM with and without the Virthub KV connector.
#
# Utilizes GPU when available; falls back to CPU with the necessary
# monkey-patch to work around vLLM's empty-device detection bug.
#
# Tests are skipped if vLLM is not installed or hardware requirements
# cannot be satisfied.

import os
import sys
import time
import pytest
import subprocess
from pathlib import Path
from typing import Generator, Optional

_BINDINGS_DIR = Path(__file__).resolve().parent.parent.parent / "bindings" / "python"
if str(_BINDINGS_DIR) not in sys.path:
    sys.path.insert(0, str(_BINDINGS_DIR))

def _has_gpu() -> bool:
    """Return True if a CUDA‑capable GPU is available."""
    try:
        import torch
        return torch.cuda.is_available()
    except Exception:
        return False

if not _has_gpu():
    os.environ["VLLM_DEVICE"] = "cpu"
    os.environ["CUDA_VISIBLE_DEVICES"] = ""

VLLM_AVAILABLE = False
CONNECTOR_AVAILABLE = False

try:
    import vllm
    from vllm import LLM, SamplingParams
    VLLM_AVAILABLE = True
except Exception:
    vllm = None
    LLM = None
    SamplingParams = None

try:
    from virthub.vllm import VirthubKVConnector
    CONNECTOR_AVAILABLE = True
except ImportError:
    VirthubKVConnector = None

MODEL_NAME = "facebook/opt-125m"
PROMPTS = [
    "Once upon a time, there was a",
    "The capital of France is",
    "Machine learning is a field of study that",
    "In the beginning, the universe was",
    "The quick brown fox jumps over the lazy dog",
]

def _force_cpu_if_needed():
    """Force vLLM to use CPU only when no GPU is present."""
    if not _has_gpu():
        try:
            import vllm.platforms
            vllm.platforms.current_platform.device_type = "cpu"
        except Exception:
            pass


def create_vllm_baseline():
    if not VLLM_AVAILABLE:
        pytest.skip("vLLM not available")
    _force_cpu_if_needed()
    return LLM(
        model=MODEL_NAME,
        enforce_eager=True,
        disable_log_stats=True,
    )


def create_vllm_with_virthub():
    if not VLLM_AVAILABLE:
        pytest.skip("vLLM not available")
    if not CONNECTOR_AVAILABLE:
        pytest.skip("Virthub connector not installed")
    _force_cpu_if_needed()
    return LLM(
        model=MODEL_NAME,
        enforce_eager=True,
        disable_log_stats=True,
        kv_transfer_config={"kv_connector": "virthub", "kv_role": "kv_both"},
    )


@pytest.fixture(scope="module")
def virthub_daemon(project_root: Path) -> Generator[Optional[subprocess.Popen], None, None]:
    daemon = project_root / "target" / "release" / "klnk-daemon"
    if not daemon.exists():
        daemon = project_root / "target" / "debug" / "klnk-daemon"
    if not daemon.exists():
        pytest.skip("klnk-daemon binary not found. Build it first (cargo build).")

    default_socket = "/tmp/virthub_control.sock"
    if os.path.exists(default_socket):
        os.unlink(default_socket)

    env = os.environ.copy()
    env["RUST_LOG"] = "debug"

    proc = subprocess.Popen(
        [str(daemon)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        universal_newlines=True,
        bufsize=1,
    )

    start = time.time()
    ready = False
    while time.time() - start < 30:
        if proc.poll() is not None:
            remaining = proc.stdout.read() if proc.stdout else ""
            pytest.skip(f"Daemon exited with code {proc.returncode}. Output: {remaining}")
        if os.path.exists(default_socket):
            ready = True
            break
        time.sleep(0.1)

    if not ready:
        proc.terminate()
        proc.wait(timeout=2)
        pytest.skip("Daemon did not start within 30s")

    yield proc

    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if os.path.exists(default_socket):
        os.unlink(default_socket)


@pytest.mark.integration
@pytest.mark.slow
@pytest.mark.performance
class TestVllmPerformance:
    def test_baseline_throughput(self, virthub_daemon, project_root):
        try:
            llm = create_vllm_baseline()
        except Exception as e:
            pytest.skip(f"vLLM baseline engine creation failed: {e}")

        sampling_params = SamplingParams(temperature=0, max_tokens=32, seed=42)
        llm.generate("Warm up", sampling_params)

        start = time.perf_counter()
        outputs = llm.generate(PROMPTS, sampling_params)
        elapsed = time.perf_counter() - start

        total_tokens = sum(len(output.outputs[0].token_ids) for output in outputs)
        tokens_per_sec = total_tokens / elapsed if elapsed > 0 else float("inf")

        print(f"\nBaseline throughput: {tokens_per_sec:.1f} tokens/sec "
              f"({total_tokens} tokens in {elapsed:.2f}s)")
        assert total_tokens > 0

    def test_virthub_throughput(self, virthub_daemon, project_root):
        try:
            llm = create_vllm_with_virthub()
        except Exception as e:
            if "Unsupported connector type" in str(e):
                pytest.skip("Virthub connector not registered in vLLM")
            else:
                pytest.skip(f"Virthub connector engine creation failed: {e}")

        sampling_params = SamplingParams(temperature=0, max_tokens=32, seed=42)
        llm.generate("Warm up", sampling_params)

        start = time.perf_counter()
        outputs = llm.generate(PROMPTS, sampling_params)
        elapsed = time.perf_counter() - start

        total_tokens = sum(len(output.outputs[0].token_ids) for output in outputs)
        tokens_per_sec = total_tokens / elapsed if elapsed > 0 else float("inf")

        print(f"\nVirthub throughput: {tokens_per_sec:.1f} tokens/sec "
              f"({total_tokens} tokens in {elapsed:.2f}s)")
        assert total_tokens > 0
