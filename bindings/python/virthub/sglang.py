# bindings/python/virthub/sglang.py
#
# SGLang KV Connector for Virthub.
# Provides prefix caching and RDMA-based KV cache sharing for SGLang serving.
# Native Rust client used when available; otherwise in-memory stub for testing.
# Connector instances are cached per configuration to avoid repeated initialization.
# All operations are synchronous and block until completion.
#
# This module provides:
# - VirthubSglangConnector: main connector class for SGLang integration
# - In-memory stub for testing when Rust extension is not built
# - Configuration-based instance caching for multi-worker scenarios
#
# The connector registers RadixAttention prefix nodes, fetches remote prefixes
# via RDMA, and manages memory region lifecycle. GPU is used when available;
# otherwise falls back to CPU with known limitations.

import logging
import os
import threading
from typing import Any, Dict, List, Optional, Tuple

import toml

logger = logging.getLogger(__name__)

try:
    import _virthub
    _RUST_AVAILABLE = True
    _RustClient = _virthub.VirthubSglangConnector
    # The native module also exposes SglangPrefixMeta, but we may not need it if
    # we convert to dict. We'll keep reference just in case.
    _RustSglangPrefixMeta = _virthub.SglangPrefixMeta if hasattr(_virthub, "SglangPrefixMeta") else None
except ImportError:
    _RUST_AVAILABLE = False
    logger.warning("Rust SGLang client not available; using in‑memory stub for testing.")


class _InMemoryClient:
    """Simulates the Rust SGLang connector for prefix sharing."""

    def __init__(self, config=None):
        self.regions = {}         # block_id -> metadata
        self.prefix_store = {}    # prefix_hash -> metadata dict
        self._lock = threading.RLock()

    def register_kv_block(self, block_id, vaddr, size, gpu_device_id):
        meta = {
            "block_id": block_id,
            "vaddr": vaddr,
            "size_bytes": size,
            "gpu_device_id": gpu_device_id,
            "rkey": 3000 + block_id,
            "lkey": 3000 + block_id,
            "node_addr": "127.0.0.1:19001",
        }
        with self._lock:
            self.regions[block_id] = meta
        return meta

    def deregister_memory_region(self, rkey):
        with self._lock:
            matching = [bid for bid, meta in self.regions.items() if meta["rkey"] == rkey]
            for bid in matching:
                del self.regions[bid]

    def register_prefix_node(self, prefix_hash, token_count, vaddr, size_bytes, gpu_device_id):
        meta = self.register_kv_block(prefix_hash, vaddr, size_bytes, gpu_device_id)
        info = {
            "prefix_hash": prefix_hash,
            "token_count": token_count,
            "vaddr": vaddr,
            "size_bytes": size_bytes,
            "gpu_device_id": gpu_device_id,
            "rkey": meta["rkey"],
            "lkey": meta["lkey"],
        }
        with self._lock:
            self.prefix_store[prefix_hash] = info
        return info

    def fetch_remote_prefix(self, peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes):
        # No real RDMA – just succeed
        pass

    def close(self):
        with self._lock:
            self.regions.clear()
            self.prefix_store.clear()


def _config_dict_to_path(config: Dict[str, Any]) -> str:
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".toml", prefix="virthub_config_")
    with os.fdopen(fd, "w") as f:
        toml.dump(config, f)
    return path


class _ConnectorCache:
    """Cache for VirthubSglangConnector instances keyed by config."""
    def __init__(self):
        self._lock = threading.Lock()
        self._cache: Dict[Tuple, Any] = {}

    def get_or_create(self, config: Dict[str, Any]) -> Any:
        key = frozenset(config.items())
        with self._lock:
            if key not in self._cache:
                # Create instance without calling __init__ (to avoid client recreation)
                instance = VirthubSglangConnector.__new__(VirthubSglangConnector)
                instance._initialize(config)
                self._cache[key] = instance
            return self._cache[key]

_connector_cache = _ConnectorCache()


class VirthubSglangConnector:
    """
    SGLang connector for Virthub.

    If the native Rust extension is available, all operations use the native
    (synchronous) methods. Otherwise, an in‑memory stub is used.
    All methods are synchronous – they block until the operation completes.

    The connector instance is cached per configuration to avoid repeated
    client initialisation. This is safe because the underlying client is
    stateless with respect to configuration.
    """

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        # Delegate to cache for actual initialization
        self._initialize(config or {})

    def _initialize(self, config: Dict[str, Any]):
        self.config = config
        self._client = self._create_client()
        # block_map is used only for the stub; the Rust client manages its own state.
        self.block_map: Dict[int, Any] = {}

    @property
    def client(self):
        return self._client

    def _create_client(self):
        if _RUST_AVAILABLE:
            config_path = _config_dict_to_path(self.config)
            try:
                return _RustClient(config_path)
            finally:
                try:
                    os.unlink(config_path)
                except OSError:
                    pass
        else:
            return _InMemoryClient(self.config)

    def register_prefix_nodes_batch(
        self,
        items: List[Tuple[int, int, int, int, int]],
    ) -> List[Dict[str, Any]]:
        """
        Register multiple prefix nodes in one call.

        Args:
            items: list of (prefix_hash, token_count, vaddr, size_bytes, gpu_device_id)
        Returns list of metadata dicts.
        """
        metas = []
        for prefix_hash, token_count, vaddr, size_bytes, gpu_id in items:
            meta = self.register_prefix_node(prefix_hash, token_count, vaddr, size_bytes, gpu_id)
            metas.append(meta)
        return metas

    def register_prefix_node(
        self,
        prefix_hash: int,
        token_count: int,
        vaddr: int,
        size_bytes: int,
        gpu_device_id: int = 0,
    ) -> Dict[str, Any]:
        """Register a RadixAttention prefix node and return its metadata."""
        if _RUST_AVAILABLE:
            # Native method is synchronous; call directly.
            meta_obj = self._client.register_prefix_node(
                prefix_hash, token_count, vaddr, size_bytes, gpu_device_id
            )
            meta = {
                "prefix_hash": meta_obj.prefix_hash,
                "token_count": meta_obj.token_count,
                "vaddr": meta_obj.vaddr,
                "size_bytes": meta_obj.size_bytes,
                "gpu_device_id": meta_obj.gpu_device_id,
                "rkey": meta_obj.rkey,
                "lkey": meta_obj.lkey,
            }
            self.block_map[prefix_hash] = meta
            return meta
        else:
            meta = self._client.register_prefix_node(
                prefix_hash, token_count, vaddr, size_bytes, gpu_device_id
            )
            self.block_map[prefix_hash] = meta
            return meta

    def fetch_remote_prefix(
        self,
        peer_addr: str,
        remote_vaddr: int,
        remote_rkey: int,
        local_vaddr: int,
        size_bytes: int,
    ) -> None:
        """Fetch a remote prefix via RDMA (synchronous)."""
        if _RUST_AVAILABLE:
            # Native method is synchronous; call directly.
            self._client.fetch_remote_prefix(
                peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes
            )
        else:
            self._client.fetch_remote_prefix(
                peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes
            )

    def deregister_memory_region(self, rkey: int) -> None:
        """Deregister a memory region by its rkey."""
        if _RUST_AVAILABLE:
            self._client.deregister_memory_region(rkey)
        else:
            self._client.deregister_memory_region(rkey)

    def unregister_prefix(self, prefix_hash: int) -> None:
        """Remove a prefix from the local registry."""
        if prefix_hash in self.block_map:
            meta = self.block_map.pop(prefix_hash)
            self.deregister_memory_region(meta["rkey"])

    def close(self) -> None:
        """Synchronous close – deregisters all regions and clears state."""
        if _RUST_AVAILABLE:
            self._client.close()
        else:
            self._client.close()
        self.block_map.clear()


__all__ = ["VirthubSglangConnector"]
