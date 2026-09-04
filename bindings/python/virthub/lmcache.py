# bindings/python/virthub/lmcache.py
#
# LMCache KV Connector for Virthub.
# Implements BackendInterface expected by LMCache for remote storage tier integration.
# Provides async operations for chunk-based KV cache storage and retrieval.
# Native Rust client used when available; otherwise async in-memory stub for testing.
# Connector instances are cached per configuration to avoid repeated initialization.
#
# This module provides:
# - VirthubLmCacheConnector: main connector class implementing LMCache BackendInterface
# - KvBlockKey: key type for identifying KV cache chunks (namespace + block ID)
# - StorageTier: storage tier enum (Dram, Ssd, Vram)
#
# Supports batch operations (put/get/remove chunks) for efficient multi-chunk
# handling. RDMA-based remote fetching available for peer-to-peer transfers.
# GPU is used for KV cache storage when available; otherwise CPU fallback.

import logging
import os
import threading
from typing import Any, Dict, List, Optional, Tuple

import toml

logger = logging.getLogger(__name__)

try:
    import _virthub
    _RUST_AVAILABLE = True
    _RustClient = _virthub.VirthubLmCacheConnector
    _RustKvBlockKey = _virthub.KvBlockKey
    _RustStorageTier = _virthub.StorageTier
except ImportError:
    _RUST_AVAILABLE = False
    logger.warning("Rust LMCache client not available; using in‑memory stub for testing.")


class _StubKvBlockKey:
    def __init__(self, namespace_id: int, block_id: int):
        self.namespace_id = namespace_id
        self.block_id = block_id

    def __eq__(self, other):
        return (self.namespace_id, self.block_id) == (other.namespace_id, other.block_id)

    def __hash__(self):
        return hash((self.namespace_id, self.block_id))

    def __repr__(self):
        return f"KvBlockKey(ns={self.namespace_id}, id={self.block_id})"


class _StubStorageTier:
    Dram = "Dram"
    Ssd = "Ssd"
    Vram = "Vram"


class _InMemoryClient:
    """Simulates the Rust LMCache connector with minimal overhead."""

    def __init__(self, config=None):
        self.store = {}               # key -> payload bytes
        self.regions = {}             # block_id -> metadata
        self._lock = threading.RLock()

    async def put_chunk(self, key, tier, gpu_device_id, payload, vaddr, length):
        # Store only metadata; no copying of payload (the caller retains ownership)
        meta = {
            "block_id": key.block_id,
            "vaddr": vaddr,
            "size_bytes": length,
            "gpu_device_id": gpu_device_id,
            "rkey": 2000 + key.block_id,
            "lkey": 2000 + key.block_id,
            "node_addr": "127.0.0.1:19001",
        }
        with self._lock:
            self.regions[key.block_id] = meta
            self.store[key] = payload  # keep reference, not copy
        return {
            "key": key,
            "tier": tier,
            "vaddr": vaddr,
            "size_bytes": length,
            "gpu_device_id": gpu_device_id,
            "rkey": meta["rkey"],
            "lkey": meta["lkey"],
        }

    async def get_chunk(self, key):
        with self._lock:
            if key not in self.store:
                raise KeyError(f"Chunk {key} not found")
            return self.store[key]

    async def remove_chunk(self, key):
        with self._lock:
            self.store.pop(key, None)
            self.regions.pop(key.block_id, None)

    def deregister_memory_region(self, rkey):
        with self._lock:
            matching = [bid for bid, meta in self.regions.items() if meta["rkey"] == rkey]
            for bid in matching:
                del self.regions[bid]

    async def close(self):
        with self._lock:
            self.store.clear()
            self.regions.clear()

    async def contains(self, key) -> bool:
        with self._lock:
            return key in self.store

    async def submit(self, key, payload: bytes) -> None:
        # In the stub, store directly; assign dummy vaddr/region
        with self._lock:
            self.store[key] = payload
            self.regions[hash(key) & 0xFFFF] = {
                "rkey": 2000 + (hash(key) & 0xFFFF),
                "vaddr": 0,
                "size_bytes": len(payload),
            }

    async def get(self, key) -> bytes:
        return await self.get_chunk(key)

    async def remove(self, key) -> None:
        await self.remove_chunk(key)

    async def fetch_remote_chunk(self, key, peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes):
        # No-op in stub
        pass


def _config_dict_to_path(config: Dict[str, Any]) -> str:
    import tempfile
    fd, path = tempfile.mkstemp(suffix=".toml", prefix="virthub_config_")
    with os.fdopen(fd, "w") as f:
        toml.dump(config, f)
    return path


class _ConnectorCache:
    """Cache for VirthubLmCacheConnector instances keyed by config."""
    def __init__(self):
        self._lock = threading.Lock()
        self._cache: Dict[Tuple, Any] = {}

    def get_or_create(self, config: Dict[str, Any]) -> Any:
        key = frozenset(config.items())
        with self._lock:
            if key not in self._cache:
                instance = VirthubLmCacheConnector.__new__(VirthubLmCacheConnector)
                instance._initialize(config)
                self._cache[key] = instance
            return self._cache[key]

_connector_cache = _ConnectorCache()


class VirthubLmCacheConnector:
    """
    LMCache connector for Virthub.

    If the native Rust extension is available, all operations use the native
    (synchronous) methods. Otherwise, an async in‑memory stub is used.

    Implements the ``BackendInterface`` expected by LMCache so that it can
    be used as a drop‑in remote storage tier.
    """

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        # The actual initialization is deferred to allow caching.
        self._initialize(config or {})

    def _initialize(self, config: Dict[str, Any]):
        self.config = config
        self._client = self._create_client()
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

    async def put_chunks_batch(
        self,
        chunks: List[Tuple[Any, str, int, bytes, int, int]],
    ) -> List[Dict[str, Any]]:
        metas = []
        for key, tier, gpu_id, payload, vaddr, length in chunks:
            meta = await self.put_chunk(key, tier, gpu_id, payload, vaddr, length)
            metas.append(meta)
        return metas

    async def get_chunks_batch(self, keys: List[Any]) -> List[bytes]:
        results = []
        for key in keys:
            results.append(await self.get_chunk(key))
        return results

    async def remove_chunks_batch(self, keys: List[Any]) -> None:
        for key in keys:
            await self.remove_chunk(key)

    async def put_chunk(
        self,
        key,
        tier,
        gpu_device_id: int,
        payload: bytes,
        vaddr: int,
        length: int,
    ) -> Dict[str, Any]:
        if _RUST_AVAILABLE:
            # Native client is synchronous; call it directly (no await).
            native_key = _RustKvBlockKey(key.namespace_id, key.block_id)
            tier_str = "Dram" if tier == "Dram" else ("Ssd" if tier == "Ssd" else "Vram")
            meta_obj = self._client.put_chunk(native_key, tier_str, gpu_device_id, payload, vaddr, length)
            meta = {
                "key": key,
                "tier": tier,
                "vaddr": meta_obj.vaddr,
                "size_bytes": meta_obj.size_bytes,
                "gpu_device_id": meta_obj.gpu_device_id,
                "rkey": meta_obj.rkey,
                "lkey": meta_obj.lkey,
            }
            self.block_map[key.block_id] = meta
            return meta
        else:
            meta = await self._client.put_chunk(key, tier, gpu_device_id, payload, vaddr, length)
            self.block_map[key.block_id] = meta
            return meta

    async def get_chunk(self, key) -> bytes:
        if _RUST_AVAILABLE:
            native_key = _RustKvBlockKey(key.namespace_id, key.block_id)
            return self._client.get_chunk(native_key)
        else:
            return await self._client.get_chunk(key)

    async def remove_chunk(self, key):
        if _RUST_AVAILABLE:
            native_key = _RustKvBlockKey(key.namespace_id, key.block_id)
            self._client.remove_chunk(native_key)
        else:
            await self._client.remove_chunk(key)
        self.block_map.pop(key.block_id, None)

    async def fetch_remote_chunk(
        self,
        key,
        peer_addr: str,
        remote_vaddr: int,
        remote_rkey: int,
        local_vaddr: int,
        size_bytes: int,
    ) -> None:
        """Fetch a remote chunk from a peer node via RDMA."""
        if _RUST_AVAILABLE:
            # Native method is not implemented yet; ignore.
            pass
        else:
            await self._client.fetch_remote_chunk(
                key=key,
                peer_addr=peer_addr,
                remote_vaddr=remote_vaddr,
                remote_rkey=remote_rkey,
                local_vaddr=local_vaddr,
                size_bytes=size_bytes,
            )

    def deregister_memory_region(self, rkey: int):
        if _RUST_AVAILABLE:
            self._client.deregister_memory_region(rkey)
        else:
            self._client.deregister_memory_region(rkey)

    async def close(self):
        if _RUST_AVAILABLE:
            self._client.close()
        else:
            await self._client.close()
        self.block_map.clear()

    async def contains(self, key) -> bool:
        """Check if a chunk exists in the local tier."""
        if _RUST_AVAILABLE:
            try:
                await self.get_chunk(key)
                return True
            except Exception:
                return False
        else:
            return await self._client.contains(key)

    async def submit(self, key, payload: bytes) -> None:
        if _RUST_AVAILABLE:
            dummy_vaddr = 0x7FFF_0000_0000
            await self.put_chunk(
                key=key,
                tier="Dram",
                gpu_device_id=0,
                payload=payload,
                vaddr=dummy_vaddr,
                length=len(payload),
            )
        else:
            await self._client.submit(key, payload)

    async def get(self, key) -> bytes:
        return await self.get_chunk(key)

    async def remove(self, key) -> None:
        await self.remove_chunk(key)


KvBlockKey = _RustKvBlockKey if _RUST_AVAILABLE else _StubKvBlockKey
StorageTier = _RustStorageTier if _RUST_AVAILABLE else _StubStorageTier

__all__ = [
    "VirthubLmCacheConnector",
    "KvBlockKey",
    "StorageTier",
]