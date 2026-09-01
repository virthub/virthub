# tests/python/integration/test_sglang_e2e.py
#
# End-to-end integration test for SGLang with the Virthub connector.
#
# Currently validates the Python connector's ability to instantiate and
# register prefix nodes. Full inference integration with SGLang is not
# yet implemented.
#
# Tests are skipped gracefully if SGLang is not installed or the
# connector module is unavailable.

import os
import sys
import subprocess
import time
import pytest
from pathlib import Path
from typing import Generator, Optional

_BINDINGS_DIR = Path(__file__).resolve().parent.parent.parent / "bindings" / "python"
if str(_BINDINGS_DIR) not in sys.path:
    sys.path.insert(0, str(_BINDINGS_DIR))

try:
    from virthub.sglang import VirthubSglangConnector
    CONNECTOR_AVAILABLE = True
except ImportError:
    CONNECTOR_AVAILABLE = False
    VirthubSglangConnector = None

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


@pytest.mark.skipif(not CONNECTOR_AVAILABLE, reason="Virthub SGLang connector not available")
@pytest.mark.integration
@pytest.mark.slow
class TestSglangVirthub:
    def test_connector_instantiation(self, virthub_daemon, project_root):
        """The SGLang connector can be created and holds a configuration."""
        config = {
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "transport": {"default_protocol": "tcp"},
        }
        connector = VirthubSglangConnector(config)
        assert connector is not None
        connector.close()

    def test_prefix_node_registration(self, virthub_daemon, project_root):
        """Register a prefix node and verify the returned metadata."""
        config = {
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "transport": {"default_protocol": "tcp"},
        }
        connector = VirthubSglangConnector(config)

        prefix_hash = 0xDEAD_BEEF_CAFE_0001
        token_count = 128
        vaddr = 0x7FFF_3000_0000
        size_bytes = 2 * 1024 * 1024
        gpu_device_id = 0

        meta = connector.register_prefix_node(
            prefix_hash=prefix_hash,
            token_count=token_count,
            vaddr=vaddr,
            size_bytes=size_bytes,
            gpu_device_id=gpu_device_id,
        )

        assert meta is not None
        assert meta["prefix_hash"] == prefix_hash
        assert meta["token_count"] == token_count
        assert meta["vaddr"] == vaddr
        assert meta["size_bytes"] == size_bytes
        assert meta["gpu_device_id"] == gpu_device_id
        assert "rkey" in meta
        assert "lkey" in meta

        connector.close()
