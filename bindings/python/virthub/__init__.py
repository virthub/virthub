# bindings/python/virthub/__init__.py

"""
Virthub Python Bindings.

This package provides connectors for vLLM, SGLang, and LMCache.
When the native Rust extension is available, these connectors use RDMA or
TCP fallback; otherwise they fall back to in‑memory stubs for testing.

Main classes:
- VirthubKVConnector (vLLM)
- VirthubSglangConnector
- VirthubLmCacheConnector
"""

from .vllm import (
    VirthubKVConnector,
    VirthubKVConnectorV1,
    VirthubKVConnectorV2,
)
from .sglang import VirthubSglangConnector
from .lmcache import (
    VirthubLmCacheConnector,
    KvBlockKey,
    StorageTier,
)

__all__ = [
    "VirthubKVConnector",
    "VirthubKVConnectorV1",
    "VirthubKVConnectorV2",
    "VirthubSglangConnector",
    "VirthubLmCacheConnector",
    "KvBlockKey",
    "StorageTier",
]
