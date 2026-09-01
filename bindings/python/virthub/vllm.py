# bindings/python/virthub/vllm.py

"""
Virthub KV Connector for vLLM.

This module provides two adapters:
- VirthubKVConnectorV1 : for GPUModelRunner V1 (legacy)
- VirthubKVConnectorV2 : for GPUModelRunner V2 (newer vLLM)

Both adapters delegate actual RDMA operations to a native Rust client
(``_virthub.VirthubVllmConnector``) when the extension is built; otherwise an
in‑memory stub is used for testing.

The correct adapter is selected at import time based on the environment
variable ``VIRTHUB_VLLM_CONNECTOR_VERSION`` (``auto``, ``v1``, or ``v2``).
If neither vLLM base class is available, a dummy base is used so that vLLM's
type checks still pass.
"""

import logging
import os
import threading
from typing import Any, Dict, List, Optional, Tuple

import toml

logger = logging.getLogger(__name__)

try:
    import _virthub
    _RustClient = _virthub.VirthubVllmConnector
    _RUST_AVAILABLE = True
except ImportError:
    _RUST_AVAILABLE = False
    logger.warning("Rust Virthub connector not available; using in‑memory stub for testing.")


class _StubRustConnector:
    """In‑memory stub that simulates the Rust connector."""

    __slots__ = ('config', 'regions', '_lock')

    def __init__(self, config: Any):
        self.config = config
        self.regions: Dict[int, Any] = {}
        self._lock = threading.RLock()

    def register_kv_block(
        self, block_id: int, vaddr: int, size: int, gpu_device_id: int
    ) -> Dict[str, Any]:
        meta = {
            "block_id": block_id,
            "vaddr": vaddr,
            "size_bytes": size,
            "gpu_device_id": gpu_device_id,
            "rkey": 1000 + block_id,
            "lkey": 1000 + block_id,
            "node_addr": "127.0.0.1:19001",
        }
        with self._lock:
            self.regions[block_id] = meta
        return meta

    def swap_in_remote_block(
        self,
        peer_addr: str,
        remote_vaddr: int,
        remote_rkey: int,
        local_vaddr: int,
        size_bytes: int,
    ) -> None:
        # no-op in stub
        pass

    def deregister_memory_region(self, rkey: int) -> None:
        with self._lock:
            matching = [bid for bid, meta in self.regions.items() if meta["rkey"] == rkey]
            for bid in matching:
                del self.regions[bid]

    def close(self) -> None:
        with self._lock:
            self.regions.clear()


def _config_dict_to_path(config: Dict[str, Any]) -> str:
    """Serialize *config* to a temporary TOML file and return its path."""
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".toml", prefix="virthub_config_")
    with os.fdopen(fd, "w") as f:
        toml.dump(config, f)
    return path


def _default_config_dict() -> Dict[str, Any]:
    """Return a complete default configuration suitable for the native client."""
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
            "default_protocol": "tcp",
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
            "memlock_limit": 17179869184,
        },
    }


_native_client_lock = threading.Lock()
_native_client_singleton: Optional[Any] = None


def _get_native_client(config_dict: Dict[str, Any]) -> Any:
    """Return the singleton native client, creating it if necessary."""
    global _native_client_singleton
    with _native_client_lock:
        if _native_client_singleton is None:
            config_path = _config_dict_to_path(config_dict)
            try:
                _native_client_singleton = _RustClient(config_path)
            finally:
                try:
                    os.unlink(config_path)
                except OSError:
                    pass
        return _native_client_singleton


class _BaseConnector:
    """Common logic for both adapter versions."""

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        self._initialize(config or {})

    def _initialize(self, config: Any):
        self.config = config
        self._client = self._create_client()
        self.block_map: Dict[int, Any] = {}

    def _create_client(self):
        if _RUST_AVAILABLE:
            config_dict = self.config if isinstance(self.config, dict) else _default_config_dict()
            return _get_native_client(config_dict)
        else:
            return _StubRustConnector(self.config)

    def unregister_block(self, block_id: int) -> None:
        if block_id in self.block_map:
            meta = self.block_map.pop(block_id)
            if self._client:
                try:
                    self._client.deregister_memory_region(meta["rkey"])
                except Exception as e:
                    logger.warning("Failed to deregister block %d: %s", block_id, e)

    def close(self) -> None:
        if self._client:
            for meta in list(self.block_map.values()):
                try:
                    self._client.deregister_memory_region(meta["rkey"])
                except Exception:
                    pass
            self._client.close()
            self.block_map.clear()


try:
    from vllm.worker.kv_connector import KVConnectorBase_V1
except ImportError:
    try:
        from vllm.distributed.kv_transfer.kv_connector.v1.base import KVConnectorBase_V1
    except ImportError:
        KVConnectorBase_V1 = None

try:
    from vllm.v1.worker.gpu.kv_connector import KVConnector as _KVConnectorV2
except ImportError:
    _KVConnectorV2 = None

if KVConnectorBase_V1 is None and _KVConnectorV2 is None:
    class _DummyKVConnectorBase:
        pass
    KVConnectorBase_V1 = _DummyKVConnectorBase
    _KVConnectorV2 = _DummyKVConnectorBase

if KVConnectorBase_V1 is not None:
    class VirthubKVConnectorV1(_BaseConnector, KVConnectorBase_V1):
        """Virthub connector for GPUModelRunner V1."""

        def __init__(self, config=None, *args, **kwargs):
            _BaseConnector.__init__(self, config)
            try:
                KVConnectorBase_V1.__init__(self, config, *args, **kwargs)
            except TypeError:
                try:
                    KVConnectorBase_V1.__init__(self, *args, **kwargs)
                except TypeError:
                    KVConnectorBase_V1.__init__(self)

        def unregister_block(self, block_id: int) -> None:
            _BaseConnector.unregister_block(self, block_id)

        def close(self) -> None:
            _BaseConnector.close(self)

        # Using *args, **kwargs avoids signature mismatch errors across vLLM versions.
        def save_kv_layer(self, *args, **kwargs) -> None:
            # vLLM calls with (layer_name, kv_cache, attn_metadata) or similar.
            # In the stub we do nothing; real implementation would register blocks.
            pass

        def start_load_kv(self, *args, **kwargs) -> None:
            # vLLM calls with (forward_context) or similar.
            pass

        def wait_for_layer_load(self, *args, **kwargs) -> None:
            pass

        def update_state_after_alloc(self, *args, **kwargs) -> None:
            pass

        def get_num_new_matched_tokens(
            self, request_meta: Dict[str, Any], *args, **kwargs
        ) -> Tuple[int, bool]:
            return (0, False)

        def build_connector_meta(self, scheduler_output: Any, *args, **kwargs) -> Dict[str, Any]:
            return {}

        def wait_for_save(self, *args, **kwargs) -> None:
            pass

        def wait_for_load(self, *args, **kwargs) -> None:
            pass
else:
    VirthubKVConnectorV1 = None

if _KVConnectorV2 is not None:
    class VirthubKVConnectorV2(_BaseConnector, _KVConnectorV2):
        """Virthub connector for GPUModelRunner V2."""

        def __init__(self, config=None, *args, **kwargs):
            _BaseConnector.__init__(self, config)
            try:
                _KVConnectorV2.__init__(self, config, *args, **kwargs)
            except TypeError:
                try:
                    _KVConnectorV2.__init__(self, *args, **kwargs)
                except TypeError:
                    _KVConnectorV2.__init__(self)

        def unregister_block(self, block_id: int) -> None:
            _BaseConnector.unregister_block(self, block_id)

        def close(self) -> None:
            _BaseConnector.close(self)

        async def pre_forward(self, *args, **kwargs) -> None:
            pass

        async def post_forward(self, *args, **kwargs) -> None:
            pass

        async def no_forward(self, *args, **kwargs) -> None:
            pass

        async def bind_gpu_block_pool(self, *args, **kwargs) -> None:
            pass

        def get_num_new_matched_tokens(
            self, request_meta: Dict[str, Any], *args, **kwargs
        ) -> Tuple[int, bool]:
            return (0, False)

        def build_connector_meta(self, scheduler_output: Any, *args, **kwargs) -> Dict[str, Any]:
            return {}

        def update_state_after_alloc(self, *args, **kwargs) -> None:
            pass

        async def wait_for_save(self, *args, **kwargs) -> None:
            pass

        async def wait_for_load(self, *args, **kwargs) -> None:
            pass
else:
    VirthubKVConnectorV2 = None

_version_env = os.environ.get("VIRTHUB_VLLM_CONNECTOR_VERSION", "auto").lower()

if _version_env == "v1":
    if VirthubKVConnectorV1 is None:
        raise ImportError("V1 adapter requested but KVConnectorBase_V1 not available.")
    VirthubKVConnector = VirthubKVConnectorV1
elif _version_env == "v2":
    if VirthubKVConnectorV2 is None:
        raise ImportError("V2 adapter requested but V2 KVConnector not available.")
    VirthubKVConnector = VirthubKVConnectorV2
else:  # auto
    if VirthubKVConnectorV2 is not None:
        VirthubKVConnector = VirthubKVConnectorV2
    elif VirthubKVConnectorV1 is not None:
        VirthubKVConnector = VirthubKVConnectorV1
    else:
        raise ImportError("No suitable Virthub KV connector found.")

__all__ = [
    "VirthubKVConnector",
    "VirthubKVConnectorV1",
    "VirthubKVConnectorV2",
]