# tests/python/conftest.py
#
# Shared pytest fixtures for Virthub connector tests.
#
# Provides:
#   - Mock Virthub configuration
#   - Mock Rust client (VirthubVllmConnector, etc.)
#   - Temporary directories for daemon sockets and test data
#   - Optional mock daemon subprocess
#   - Integration test markers
#
# Fixtures are designed to work with the Python bindings located in
# `bindings/python/virthub/` (vllm.py, sglang.py, lmcache.py).
#
# All fixtures are automatically discovered by pytest.

import warnings
import pytest
import tempfile
import shutil
from pathlib import Path
from unittest.mock import MagicMock, AsyncMock
from typing import Dict, Any, Generator

# Suppress SWIG deprecation warnings (from lmdb, torch, etc.)
warnings.filterwarnings(
    "ignore",
    message="builtin type SwigPyPacked has no __module__ attribute",
    category=DeprecationWarning,
    module="importlib"
)
warnings.filterwarnings(
    "ignore",
    message="builtin type SwigPyObject has no __module__ attribute",
    category=DeprecationWarning,
    module="importlib"
)

# Suppress Pydantic deprecation warnings (from eval_protocol)
warnings.filterwarnings(
    "ignore",
    category=DeprecationWarning,
    module="pydantic"
)

# Suppress pytest-asyncio fixture loop scope warning
warnings.filterwarnings(
    "ignore",
    category=pytest.PytestDeprecationWarning,
    message='The configuration option "asyncio_default_fixture_loop_scope" is unset.'
)


@pytest.fixture(scope="session")
def project_root() -> Path:
    """
    Return the absolute path to the project root directory.

    This fixture looks for Cargo.toml as a marker for the project root.
    """
    # Start from the current file's location
    current = Path(__file__).resolve().parent.parent.parent
    # Walk up until we find Cargo.toml
    root = current
    while root != root.parent:
        if (root / "Cargo.toml").exists():
            return root
        root = root.parent
    # Fallback: assume we are in tests/python/
    return Path(__file__).resolve().parent.parent.parent


@pytest.fixture(scope="function")
def temp_dir() -> Generator[Path, None, None]:
    """
    Create a temporary directory for test files and clean up afterwards.

    Yields:
        Path: Path to the temporary directory.
    """
    dir_path = Path(tempfile.mkdtemp(prefix="virthub_test_"))
    yield dir_path
    shutil.rmtree(dir_path, ignore_errors=True)


@pytest.fixture(scope="function")
def mock_config() -> Dict[str, Any]:
    """
    Return a mock Virthub configuration dictionary.

    This mirrors the structure of conf/virthub.toml and can be used to
    initialise the Python connectors.

    Returns:
        Dict[str, Any]: Configuration dictionary.
    """
    return {
        "general": {
            "log_level": "info",
            "control_socket": "/tmp/virthub_control.sock",
            "data_bind_addr": "0.0.0.0:19001",
            "node_id": "node-1",
        },
        "klnk": {
            "enable_uffd_move": True,
            "fallback_copy": True,
            "staging_num_pages": 16,
            "huge_page_size": 2097152,
        },
        "store": {
            "block_size": 2097152,
            "tier": {
                "l0_enabled": False,
                "l0_device_ids": [0, 1],
                "l1_enabled": True,
                "l2_enabled": False,
                "l2_path": "/mnt/nvme/virthub_cache",
            },
        },
        "master": {
            "raft": {
                "embedded": True,
                "initial_peers": ["node-1", "node-2", "node-3"],
                "etcd_endpoints": ["http://127.0.0.1:2379"],
            },
            "scheduler": {
                "prefetch_window": 8,
                "l0_promote_threshold": 100,
                "l1_demote_idle_secs": 60,
                "lru_decay": 0.8,
            },
            "sharding": {"shard_count": 64},
        },
        "transport": {
            "default_protocol": "rdma",
            "rdma": {
                "device_name": "",
                "enable_gdr": False,
                "rq_prepost_count": 1024,
                "control_immediate": True,
            },
            "tcp": {
                "io_uring_enabled": True,
                "tcp_port": 19002,
            },
        },
        "ebpf": {
            "enabled": True,
            "program_path": "/usr/lib/virthub/stride_tracer.bpf.o",
            "report_interval_ms": 100,
        },
        "tuning": {
            "numa_node": -1,
            "operation_timeout_ms": 500,
            "memlock_limit": 0,
        },
    }


@pytest.fixture(scope="function")
def mock_rust_vllm_client(mock_config) -> MagicMock:
    """
    Return a mock of the Rust VirthubVllmConnector client.

    This mock implements the methods expected by the Python wrapper:
    - register_kv_block
    - swap_in_remote_block
    - deregister_memory_region
    - fetch_remote_kv_block (if needed)

    All methods return synchronous MagicMocks by default; for async methods,
    use AsyncMock.

    Returns:
        MagicMock: Mocked Rust client.
    """
    client = MagicMock()

    def register_kv_block(block_id, vaddr, size, gpu_device_id):
        return {
            "block_id": block_id,
            "vaddr": vaddr,
            "size_bytes": size,
            "gpu_device_id": gpu_device_id,
            "rkey": 1234 + block_id,
            "lkey": 5678 + block_id,
            "node_addr": "127.0.0.1:19001",
        }

    client.register_kv_block.side_effect = register_kv_block

    # Synchronous methods
    client.swap_in_remote_block = MagicMock(return_value=None)
    client.deregister_memory_region = MagicMock(return_value=None)

    # Async methods
    client.fetch_remote_kv_block = AsyncMock(return_value=None)

    return client


@pytest.fixture(scope="function")
def mock_rust_sglang_client(mock_config) -> MagicMock:
    """
    Return a mock of the Rust SGLang client (VirthubSglangConnector).

    Similar to vLLM but with prefix_node methods.

    Returns:
        MagicMock: Mocked Rust SGLang client.
    """
    client = MagicMock()

    def register_prefix_node(prefix_hash, token_count, vaddr, size, gpu_device_id):
        return {
            "prefix_hash": prefix_hash,
            "token_count": token_count,
            "vaddr": vaddr,
            "size_bytes": size,
            "gpu_device_id": gpu_device_id,
            "rkey": 4321 + (prefix_hash % 1000),
            "lkey": 8765 + (prefix_hash % 1000),
            "node_addr": "127.0.0.1:19001",
        }

    client.register_prefix_node.side_effect = register_prefix_node

    # Synchronous methods
    client.deregister_memory_region = MagicMock(return_value=None)

    # Async methods
    client.fetch_remote_prefix = AsyncMock(return_value=None)

    return client


@pytest.fixture(scope="function")
def mock_rust_lmcache_client(mock_config) -> MagicMock:
    """
    Return a mock of the Rust LMCache client (VirthubLmCacheConnector).

    Returns:
        MagicMock: Mocked Rust LMCache client.
    """
    client = MagicMock()

    def put_chunk(key, tier, gpu_device_id, payload, vaddr, length):
        return {
            "key": key,
            "tier": tier,
            "vaddr": vaddr,
            "size_bytes": length,
            "gpu_device_id": gpu_device_id,
            "rkey": 9999 + key.block_id,
            "lkey": 8888 + key.block_id,
            "node_addr": "127.0.0.1:19001",
        }

    client.put_chunk.side_effect = put_chunk

    # Synchronous methods
    client.deregister_memory_region = MagicMock(return_value=None)

    # Async methods
    client.get_chunk = AsyncMock(return_value=b"mock_payload")
    client.remove_chunk = AsyncMock(return_value=None)
    client.fetch_remote_chunk = AsyncMock(return_value=None)

    return client


def pytest_configure(config):
    """
    Register custom markers for pytest.

    This function is called by pytest during test collection.
    """
    config.addinivalue_line(
        "markers",
        "integration: mark test as an integration test (may run a real model or daemon)",
    )
    config.addinivalue_line(
        "markers",
        "slow: mark test as slow (e.g., end-to-end with a model)",
    )
    config.addinivalue_line(
        "markers",
        "rdma: mark test as requiring RDMA hardware",
    )
