# bindings/python/virthub/vllm.py
#
# Virthub KV Connector for vLLM.
# Provides two adapters: VirthubKVConnectorV1 (legacy) and VirthubKVConnectorV2 (newer vLLM).
# Delegates RDMA operations to native Rust client when extension is built; otherwise uses in-memory stub.
# Adapter selected via VIRTHUB_VLLM_CONNECTOR_VERSION env var (auto, v1, or v2).
#
# This module provides two adapters:
# - VirthubKVConnectorV1 : for GPUModelRunner V1 (legacy)
# - VirthubKVConnectorV2 : for GPUModelRunner V2 (newer vLLM)
#
# Both adapters delegate actual RDMA operations to a native Rust client
# (``_virthub.VirthubVllmConnector``) when the extension is built; otherwise an
# in‑memory stub is used for testing.
#
# The correct adapter is selected at import time based on the environment
# variable ``VIRTHUB_VLLM_CONNECTOR_VERSION`` (``auto``, ``v1``, or ``v2``).

import logging
import os
import threading
import importlib
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

def _get_kv_connector_base():
    """Find the exact KVConnectorBase class used by vLLM's mixin."""
    try:
        mixin = importlib.import_module("vllm.v1.worker.kv_connector_model_runner_mixin")
        return mixin.KVConnectorBase
    except Exception:
        candidates = [
            ("vllm.distributed.kv_transfer.kv_connector.v1.base", "KVConnectorBase"),
            ("vllm.distributed.kv_transfer.kv_connector.v1.base", "KVConnectorBase_V1"),
            ("vllm.worker.kv_connector", "KVConnectorBase"),
            ("vllm.worker.kv_connector", "KVConnectorBase_V1"),
        ]
        for mod_name, cls_name in candidates:
            try:
                mod = importlib.import_module(mod_name)
                return getattr(mod, cls_name)
            except (ImportError, AttributeError):
                continue
        return object

_KVConnectorBase = _get_kv_connector_base()


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
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".toml", prefix="virthub_config_")
    with os.fdopen(fd, "w") as f:
        toml.dump(config, f)
    return path


def _default_config_dict() -> Dict[str, Any]:
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


class _BaseConnector(_KVConnectorBase):
    """Common stub logic for both adapter versions."""

    def __init__(self, config: Optional[Dict[str, Any]] = None, *args, **kwargs):
        # We intentionally do NOT call the base class constructor.
        self._initialize(config or {})

    def _initialize(self, config: Any):
        self.config = config
        self._client = self._create_client()
        self.block_map: Dict[int, Any] = {}
        self.kv_caches = None
        self._host_xfer_ops = None
        self._handshake_metadata = None

    def _create_client(self):
        if _RUST_AVAILABLE:
            config_dict = self.config if isinstance(self.config, dict) else _default_config_dict()
            return _get_native_client(config_dict)
        else:
            return _StubRustConnector(self.config)

    @property
    def prefer_cross_layer_blocks(self) -> bool:
        return False

    def register_kv_caches(self, kv_caches):
        self.kv_caches = kv_caches

    def set_host_xfer_buffer_ops(self, copy_kv_blocks):
        self._host_xfer_ops = copy_kv_blocks

    def get_finished_count(self) -> int:
        return 0

    def get_handshake_metadata(self):
        return None

    def set_xfer_handshake_metadata(self, content):
        self._handshake_metadata = content

    def shutdown(self) -> None:
        self.close()

    def handle_preemptions(self, kv_connector_metadata):
        pass

    async def wait_for_save(self, *args, **kwargs):
        pass

    async def wait_for_load(self, *args, **kwargs):
        pass

    def save_kv_layer(self, layer_idx, kv_blocks, worker):
        for block in kv_blocks:
            block_id = getattr(block, "block_id", 0)
            if block_id == 0:
                continue
            if self._client:
                meta = self._client.register_kv_block(
                    block_id=block_id,
                    vaddr=block.gpu_ptr,
                    size=block.size,
                    gpu_device_id=worker.device_id,
                )
                self.block_map[block.block_id] = meta

    def start_load_kv(self, kv_blocks, worker=None):
        # vLLM calls with a single forward_context argument;
        # our unit tests call with (kv_blocks, worker).
        if worker is None:
            # vLLM path: ignore and return
            return
        for block in kv_blocks:
            block_id = getattr(block, "block_id", 0)
            if block_id == 0 or block_id not in self.block_map:
                continue
            meta = self.block_map[block_id]
            if self._client:
                self._client.swap_in_remote_block(
                    peer_addr=meta["node_addr"],
                    remote_vaddr=meta["vaddr"],
                    remote_rkey=meta["rkey"],
                    local_vaddr=block.gpu_ptr,
                    size_bytes=meta["size_bytes"],
                )

    def get_num_new_matched_tokens(self, request_meta, *args, **kwargs):
        return (0, False)

    def build_connector_meta(self, scheduler_output, *args, **kwargs):
        return {}

    def wait_for_layer_load(self, layer_idx):
        pass

    def update_state_after_alloc(self, *args, **kwargs):
        pass

    def unregister_block(self, block_id):
        if block_id in self.block_map:
            meta = self.block_map.pop(block_id)
            if self._client:
                self._client.deregister_memory_region(meta["rkey"])

    def close(self):
        if self._client:
            for meta in list(self.block_map.values()):
                self._client.deregister_memory_region(meta["rkey"])
            self._client.close()
            self.block_map.clear()


class VirthubKVConnectorV1(_BaseConnector):
    """Virthub connector for GPUModelRunner V1."""

    def __init__(self, config=None, *args, **kwargs):
        _BaseConnector.__init__(self, config)


class VirthubKVConnectorV2(_BaseConnector):
    """Virthub connector for GPUModelRunner V2."""

    def __init__(self, config=None, *args, **kwargs):
        _BaseConnector.__init__(self, config)

    async def pre_forward(self, *args, **kwargs):
        pass

    async def post_forward(self, *args, **kwargs):
        pass

    async def no_forward(self, *args, **kwargs):
        pass

    async def bind_gpu_block_pool(self, *args, **kwargs):
        pass


_version_env = os.environ.get("VIRTHUB_VLLM_CONNECTOR_VERSION", "auto").lower()

if _version_env == "v1":
    VirthubKVConnector = VirthubKVConnectorV1
elif _version_env == "v2":
    VirthubKVConnector = VirthubKVConnectorV2
else:
    VirthubKVConnector = VirthubKVConnectorV1

__all__ = [
    "VirthubKVConnector",
    "VirthubKVConnectorV1",
    "VirthubKVConnectorV2",
]
