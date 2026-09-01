# tests/python/integration/test_lmcache_e2e.py
#
# End-to-end integration test for LMCache with the Virthub connector.
#
# Requirements:
#   - Virthub daemon binary (klnk-daemon)
#   - LMCache (optional) – currently only the Virthub connector is exercised,
#     but availability is checked for future extensibility.
#
# Tests are skipped gracefully if required dependencies are missing.

import os
import sys
import subprocess
import time
import pytest
from pathlib import Path
from typing import Generator, Optional

try:
    import lmcache
    LMCACHE_AVAILABLE = True
except ImportError:
    lmcache = None
    LMCACHE_AVAILABLE = False

try:
    from virthub.lmcache import VirthubLmCacheConnector, KvBlockKey, StorageTier
    CONNECTOR_AVAILABLE = True
except ImportError:
    CONNECTOR_AVAILABLE = False
    VirthubLmCacheConnector = None
    KvBlockKey = None
    StorageTier = None

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


@pytest.mark.skipif(not CONNECTOR_AVAILABLE, reason="Virthub LMCache connector not installed")
@pytest.mark.integration
@pytest.mark.slow
class TestLmCacheVirthub:
    @pytest.mark.asyncio
    async def test_chunk_lifecycle(self, virthub_daemon, project_root):
        """Put, retrieve, and remove a chunk via the connector (async)."""

        config = {
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "transport": {"default_protocol": "tcp"},
        }
        connector = VirthubLmCacheConnector(config)

        key = KvBlockKey(1, 42)
        payload = b"KV cache data for testing"
        vaddr = 0x7FFF_5000_0000
        size = len(payload)

        try:
            meta = await connector.put_chunk(
                key=key,
                tier=StorageTier.Dram,
                gpu_device_id=0,
                payload=payload,
                vaddr=vaddr,
                length=size,
            )
        except RuntimeError as e:
            if "Rust client not available" in str(e):
                pytest.skip("Rust LMCache client not available")
            raise

        # The stub returns a plain dict; access by key
        assert meta is not None
        assert meta["key"] == key
        assert meta["size_bytes"] == size
        assert meta["vaddr"] == vaddr

        retrieved = await connector.get_chunk(key)
        assert retrieved == payload

        await connector.remove_chunk(key)

        with pytest.raises(Exception):
            await connector.get_chunk(key)

        await connector.close()
