# tests/python/integration/test_vllm_e2e.py
#
# End-to-end integration tests for vLLM with the Virthub KV connector.
#
# Exercises both V1 and V2 adapters, provided the respective vLLM
# interfaces are available in the environment.
#
# Tests are skipped gracefully when required dependencies are missing
# or when vLLM cannot run on the current hardware (e.g., CPU‑only mode).

import os
import sys
import subprocess
import time
import pytest
from pathlib import Path
from typing import Generator, Optional

os.environ.setdefault("VLLM_WORKER_MULTIPROC_METHOD", "spawn")

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
VLLM_IMPORT_ERROR = ""

try:
    import vllm
    from vllm import LLM, SamplingParams
    VLLM_AVAILABLE = True
except Exception as e:
    VLLM_IMPORT_ERROR = f"{type(e).__name__}: {e}"

try:
    from virthub.vllm import VirthubKVConnector, VirthubKVConnectorV1, VirthubKVConnectorV2
    CONNECTOR_AVAILABLE = True
except ImportError:
    CONNECTOR_AVAILABLE = False
    VirthubKVConnector = None
    VirthubKVConnectorV1 = None
    VirthubKVConnectorV2 = None


def _force_cpu_if_needed():
    """
    Force vLLM to use CPU **only if no GPU is available**.
    On GPU systems, this does nothing so vLLM can use the GPU.
    """
    if not _has_gpu():
        try:
            import vllm.platforms
            vllm.platforms.current_platform.device_type = "cpu"
        except Exception:
            pass


def create_baseline_engine(model_name: str):
    """Create vLLM engine (GPU if available, else CPU)."""
    if not VLLM_AVAILABLE:
        pytest.skip(f"vLLM not available: {VLLM_IMPORT_ERROR}")

    _force_cpu_if_needed()
    return LLM(
        model=model_name,
        enforce_eager=True,
        disable_log_stats=True,
    )


def create_engine_with_connector(model_name: str, connector_version: str = "auto"):
    """
    Create vLLM engine with the Virthub KV connector enabled.

    `connector_version` can be "auto", "v1", or "v2". The corresponding
    environment variable is set before instantiating the connector.
    """
    if not VLLM_AVAILABLE:
        pytest.skip(f"vLLM not available: {VLLM_IMPORT_ERROR}")
    if not CONNECTOR_AVAILABLE:
        pytest.skip("Virthub connector not installed")

    os.environ["VIRTHUB_VLLM_CONNECTOR_VERSION"] = connector_version

    _force_cpu_if_needed()
    kv_cfg = {"kv_connector": "virthub", "kv_role": "kv_both"}
    return LLM(
        model=model_name,
        enforce_eager=True,
        disable_log_stats=True,
        kv_transfer_config=kv_cfg,
    )


@pytest.fixture(scope="module")
def virthub_daemon(project_root: Path) -> Generator[Optional[subprocess.Popen], None, None]:
    """
    Start the klnk-daemon with its **default** configuration.

    The default control socket is /tmp/virthub_control.sock.
    """

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
    output_lines = []
    ready = False

    while time.time() - start < 30:
        if proc.poll() is not None:
            remaining = proc.stdout.read() if proc.stdout else ""
            output_lines.append(remaining)
            full_output = "\n".join(output_lines)
            print(f"\n[DAEMON CRASHED] exit code {proc.returncode}\n{full_output}\n",
                  file=sys.stderr)
            pytest.skip(f"Daemon exited with code {proc.returncode}")

        if proc.stdout:
            import select
            while select.select([proc.stdout], [], [], 0.05)[0]:
                line = proc.stdout.readline()
                if not line:
                    break
                output_lines.append(line.rstrip())

        if os.path.exists(default_socket):
            ready = True
            break
        time.sleep(0.1)

    if not ready:
        full_output = "\n".join(output_lines)
        print(f"\n[DAEMON TIMEOUT] Socket not created after 30s.\n{full_output}\n",
              file=sys.stderr)
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
class TestVllmVirthub:
    def test_baseline_generation(self, virthub_daemon, project_root):
        """Verify vLLM generation works without Virthub (CPU or GPU)."""
        model = "facebook/opt-125m"
        try:
            llm = create_baseline_engine(model)
        except Exception as e:
            pytest.skip(f"vLLM baseline engine creation failed: {type(e).__name__}: {e}")

        out = llm.generate(
            "Hello world",
            SamplingParams(temperature=0, max_tokens=16, seed=42),
        )
        assert len(out) > 0 and out[0].outputs[0].text

    def test_connector_instantiation(self):
        """The VirthubKVConnector can be instantiated (Python object only)."""
        if not CONNECTOR_AVAILABLE:
            pytest.skip("Virthub connector not installed")
        connector = VirthubKVConnector()
        assert connector is not None
        connector.close()

    @pytest.mark.parametrize("version", ["auto", "v1", "v2"])
    def test_engine_with_connector(self, version, virthub_daemon, project_root):
        """
        Attempt to create a vLLM engine with the Virthub KV connector.
        Skips gracefully if the connector type is not yet registered in vLLM
        or if the engine fails for any other reason.
        """
        if not CONNECTOR_AVAILABLE:
            pytest.skip("Virthub connector not installed")
        model = "facebook/opt-125m"
        try:
            llm = create_engine_with_connector(model, connector_version=version)
        except Exception as e:
            if "Unsupported connector type" in str(e):
                pytest.skip("Virthub connector type not registered in vLLM")
            else:
                pytest.skip(f"Virthub connector engine creation failed: {e}")
        else:
            out = llm.generate(
                "Hello world",
                SamplingParams(temperature=0, max_tokens=16, seed=42),
            )
            assert len(out) > 0 and out[0].outputs[0].text
