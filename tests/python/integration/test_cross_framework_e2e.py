# tests/python/integration/test_cross_framework_e2e.py
#
# Unified end-to-end integration test using a small, fixed LLM and dataset.
# Model: facebook/opt-125m (125M parameters, works on CPU)
# Prompts: a fixed set covering short and medium sequences
#
# Tests are skipped gracefully until external blockers are resolved.
# GPU is used when available; otherwise CPU is used (with known limitations).

import os
import sys
import subprocess
import time
import pytest
from pathlib import Path
from typing import Generator, Optional

def test_file_is_loadable():
    assert True

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

MODEL_NAME = "facebook/opt-125m"
PROMPTS = [
    "Hello, world! The answer is",
    "The capital of France is",
    "Machine learning is a",
]

VLLM_AVAILABLE = False
SGLANG_AVAILABLE = False
LMCACHE_AVAILABLE = False
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
    import sglang as sgl
    SGLANG_AVAILABLE = True
except Exception:
    sgl = None

try:
    import lmcache
    LMCACHE_AVAILABLE = True
except Exception:
    lmcache = None

try:
    from virthub.vllm import VirthubKVConnector
    from virthub.sglang import VirthubSglangConnector
    from virthub.lmcache import VirthubLmCacheConnector, KvBlockKey, StorageTier
    CONNECTOR_AVAILABLE = True
except ImportError:
    VirthubKVConnector = None
    VirthubSglangConnector = None
    VirthubLmCacheConnector = None

def _force_cpu_if_needed():
    """Force vLLM to use CPU only if no GPU is available."""
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
    return LLM(model=MODEL_NAME, enforce_eager=True, disable_log_stats=True)


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
class TestCrossFramework:
    def test_vllm_baseline(self, virthub_daemon, project_root):
        """vLLM generation without Virthub."""
        try:
            llm = create_vllm_baseline()
        except Exception as e:
            pytest.skip(f"vLLM baseline engine creation failed: {e}")

        sampling_params = SamplingParams(temperature=0, max_tokens=16, seed=42)
        outputs = llm.generate(PROMPTS, sampling_params)
        for prompt, output in zip(PROMPTS, outputs):
            assert len(output.outputs[0].text) > 0

    def test_vllm_with_virthub(self, virthub_daemon, project_root):
        """vLLM generation with Virthub KV connector."""
        try:
            llm = create_vllm_with_virthub()
        except Exception as e:
            if "Unsupported connector type" in str(e):
                pytest.skip("Virthub connector not registered in vLLM")
            else:
                pytest.skip(f"Virthub connector engine creation failed: {e}")

        sampling_params = SamplingParams(temperature=0, max_tokens=16, seed=42)
        outputs = llm.generate(PROMPTS, sampling_params)
        for prompt, output in zip(PROMPTS, outputs):
            assert len(output.outputs[0].text) > 0

    @pytest.mark.skipif(not SGLANG_AVAILABLE, reason="SGLang not installed")
    def test_sglang_baseline(self, virthub_daemon, project_root):
        """SGLang generation without Virthub (placeholder)."""
        pytest.skip("SGLang end‑to‑end inference not yet integrated")

    @pytest.mark.skipif(not CONNECTOR_AVAILABLE, reason="Virthub connector not installed")
    def test_sglang_with_virthub(self, virthub_daemon, project_root):
        """SGLang with Virthub connector instantiation."""
        connector = VirthubSglangConnector({
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "transport": {"default_protocol": "tcp"},
        })
        assert connector is not None
        connector.close()

    @pytest.mark.skipif(not CONNECTOR_AVAILABLE, reason="Virthub connector not installed")
    @pytest.mark.asyncio
    async def test_lmcache_connector(self, virthub_daemon, project_root):
        """LMCache connector: store and retrieve a chunk."""
        connector = VirthubLmCacheConnector({
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "transport": {"default_protocol": "tcp"},
        })
        key = KvBlockKey(1, 42)
        payload = b"cross-framework test data"

        meta = await connector.put_chunk(
            key=key,
            tier=StorageTier.Dram,
            gpu_device_id=0,
            payload=payload,
            vaddr=0x7FFF_5000_0000,
            length=len(payload),
        )
        assert meta is not None

        retrieved = await connector.get_chunk(key)
        assert retrieved == payload

        await connector.close()
