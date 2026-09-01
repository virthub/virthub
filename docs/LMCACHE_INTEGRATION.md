# LMCache Integration Guide (Virthub)

This document describes how to integrate Virthub’s **distributed multi‑tier
storage** into the LMCache inference cache.  Once integrated, LMCache can
store and retrieve hierarchical KV chunks across multiple nodes, leveraging
Virthub’s RDMA‑backed transport for low‑latency remote access.

> **Status:** The Virthub LMCache connector API is complete and testable.
> The runtime integration outlined here is a guide for LMCache developers
> or advanced users; a fully plug‑and‑play solution is under development.

---

## 1. Overview

LMCache is a hierarchical KV‑cache storage system that moves data between
GPU VRAM, host DRAM, and NVMe SSDs.  By default, it works within a single
node.

Virthub extends LMCache across nodes by implementing its `BackendInterface`:

- **`contains(key)`** – check if a chunk exists in the Virthub mesh.
- **`submit(key, payload)`** – store a chunk in the distributed memory
  mesh (registered for RDMA).
- **`get(key)`** – retrieve a chunk from a local or remote node.
- **`remove(key)`** – evict a chunk from all tiers.

The following diagram illustrates the flow:

```
LMCache eviction / promotion decision
         |
    Does the chunk exist locally?
         |        \
        Yes       No
         |          \
    Serve from     Call Virthub.get(key)
    local DRAM/SSD   |
                   RDMA fetch from remote node
                     |
                   Store in local DRAM (optional)
```

---

## 2. Prerequisites

- **Virthub daemon** running on each node (see `docs/MULTI_NODE.md`).
- **Virthub Python bindings** installed (`pip install -e bindings/python`).
  The in‑memory stub works for development; for production, build the
  native Rust extension.
- **LMCache** installed (version ≥ 0.5.0 recommended).

### Building the native extension (optional, for production)

If you want to use the native Rust client instead of the Python stubs:

```bash
# From the repository root
maturin develop --release -m bindings/rust/Cargo.toml
# Or, if already inside bindings/rust:
cd bindings/rust && maturin develop --release
```

This installs a `_virthub` module that the Python bindings will automatically
detect and use.

---

## 3. Integration Steps

### 3.1. Instantiate the Virthub Connector

In the LMCache server or worker process, create a global instance of
`VirthubLmCacheConnector`:

```python
from virthub.lmcache import VirthubLmCacheConnector

virthub_backend = VirthubLmCacheConnector({
    "control_socket": "/tmp/virthub_control.sock",
    "data_bind_addr": "0.0.0.0:19001",
    "transport": {"default_protocol": "tcp"},
})
```

If you are using the native Rust extension, this constructor will create a
native client that communicates with the local Virthub daemon. Otherwise it
falls back to an in‑memory stub for development.

### 3.2. Adapt the Virthub Backend to LMCache

LMCache does **not** accept a `remote_backend` argument directly in its public
configuration. Instead, you have two options:

1. **Wrap the Virthub connector** to match LMCache’s expected `RemoteBackend`
   interface, if you are using a custom LMCache build.
2. **Modify LMCache’s source** to use Virthub as the storage backend (a PR to
   LMCache is planned, see “Next Steps”).

A typical adapter might look like this:

```python
from lmcache import RemoteBackend

class VirthubRemoteBackend(RemoteBackend):
    def __init__(self, virthub_connector):
        self._connector = virthub_connector

    def contains(self, key):
        # LMCache may use different key types; adapt as needed.
        return asyncio.run(self._connector.contains(key))

    def submit(self, key, payload):
        return asyncio.run(self._connector.submit(key, payload))

    def get(self, key):
        return asyncio.run(self._connector.get(key))

    def remove(self, key):
        return asyncio.run(self._connector.remove(key))
```

Then instantiate LMCache with this adapter in whatever way your LMCache
version expects (e.g., via configuration file or direct object injection).

### 3.3. Storing and Retrieving Chunks

Once LMCache is configured to use Virthub as its remote tier:

- **Cache write (submit):** When LMCache decides to offload a chunk,
  it calls `backend.submit(key, payload)`. Virthub registers the chunk’s
  memory for RDMA and publishes its metadata to the cluster.

- **Cache read (get):** When LMCache needs a chunk that is not available
  locally, it calls `backend.get(key)`. Virthub first checks the local
  daemon; if the chunk is owned by another node, it issues an RDMA read
  and returns the data.

- **Cache eviction (remove):** When a chunk is evicted, LMCache calls
  `backend.remove(key)`. Virthub deregisters the memory region and removes
  the chunk from its index.

### 3.4. Direct Connector Usage (Optional)

If you prefer to bypass LMCache’s automatic tiering and directly interact
with Virthub, use the lower‑level chunk API:

```python
import asyncio
from virthub.lmcache import VirthubLmCacheConnector, KvBlockKey, StorageTier

async def main():
    backend = VirthubLmCacheConnector({ ... })
    key = KvBlockKey(namespace_id=1, block_id=42)
    payload = b"some KV data"

    # Store
    meta = await backend.put_chunk(
        key=key,
        tier=StorageTier.Dram,
        gpu_device_id=0,
        payload=payload,
        vaddr=0x7FFF_5000_0000,
        length=len(payload),
    )
    print("Stored chunk with rkey:", meta["rkey"])

    # Retrieve
    data = await backend.get_chunk(key)
    assert data == payload

    # Remove
    await backend.remove_chunk(key)

asyncio.run(main())
```

> **Note:** Even when the native Rust extension is used, the Python methods
> are still exposed as `async def` for API compatibility; internally they
> call synchronous Rust functions and return immediately.

> **Important:** When using GPUs, the memory must be registered with the
> RDMA NIC (GPUDirect). Set `enable_gdr = true` in your Virthub config.

---

## 4. Obtaining Remote Metadata

The Virthub control plane automatically publishes metadata for every
registered chunk. When a remote node needs to fetch a chunk, it can:

- Query the local daemon’s metadata (via Unix socket) to find the chunk’s
  owner and remote keys.
- Alternatively, use the push‑based metadata updates (see `main.rs` and
  `rdma_metadata.rs`) to have an up‑to‑date view of the cluster.

For LMCache, the Virthub connector handles this transparently.

---

## 5. Testing

Use the existing tests as a reference:

```bash
# Unit tests (in‑memory stub)
.venv/bin/python -m pytest tests/python/test_lmcache_connector.py -v

# Integration test (starts daemon, tests chunk lifecycle)
.venv/bin/python -m pytest tests/python/integration/test_cross_framework_e2e.py -k lmcache -v
```

If you have a dedicated LMCache integration test file, adjust the path
accordingly.
