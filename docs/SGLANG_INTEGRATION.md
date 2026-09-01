# SGLang Integration Guide (Virthub)

This document describes how to integrate Virthub’s **RadixAttention prefix
sharing** into the SGLang inference runtime. Once integrated, multiple
SGLang instances can automatically share KV‑cache prefixes across nodes,
reducing redundant computation and latency.

> **Status:** The Virthub SGLang connector API is complete and testable.
> The runtime integration outlined here is a guide for SGLang developers
> or advanced users; a fully plug‑and‑play solution is under development.

---

## 1. Overview

SGLang uses a **RadixAttention** scheduler that caches key‑value tensors
for prompt prefixes. When a new request arrives with a known prefix,
SGLang reuses the cached KV tensors.

Virthub extends this across nodes:

- **Producer node** – after computing a prefix, calls
  `register_prefix_node()` to make it available to the cluster.
- **Consumer node** – before computing the same prefix, calls
  `fetch_remote_prefix()` to pull the KV tensors from the producer.

The following diagram illustrates the flow:

```
Request arrives at Node B
         |
    [Radix tree lookup]
         |
    Is prefix cached locally?
         |        \
        Yes       No
         |          \
    Use local     Call fetch_remote_prefix()
    KV cache           |
                  RDMA from Node A
                       |
                  Use remote KV cache
                       |
                  Call register_prefix_node()
                  to cache locally
```

---

## 2. Prerequisites

- **Virthub daemon** running on each node (see `docs/MULTI_NODE.md`).
- **Virthub Python bindings** installed (`pip install -e bindings/python`).
  The in‑memory stub works for development; for production, build the
  native Rust extension.
- **SGLang** installed (version ≥ 0.5.13 recommended).

---

## 3. Integration Steps

### 3.1. Instantiate the Virthub Connector

In the SGLang worker or scheduler process, create a global instance of
`VirthubSglangConnector`. The constructor expects a **dictionary matching
the Virthub TOML structure**, not a file path.

```python
from virthub.sglang import VirthubSglangConnector

virthub_connector = VirthubSglangConnector({
    "general": {
        "control_socket": "/tmp/virthub_control.sock",
        "data_bind_addr": "0.0.0.0:19001",
        "node_id": "node-1",
    },
    "transport": {"default_protocol": "tcp"},
    # ... other Virthub config sections as needed ...
})
```

> **Note:** The above dictionary is simplified. For full configuration,
> refer to `conf/virthub.toml` or `conf/cluster.toml` and convert the
> required sections into a Python dict.

### 3.2. Save a Prefix After Computation (Producer)

After the scheduler finishes computing a prefix, register it with
Virthub. All methods are **synchronous** (blocking), so no `await` is
needed.

```python
# Inside the RadixAttention scheduler after a forward pass:
prefix_hash = hash_prefix(prompt_tokens)          # your hash function
token_count = len(prompt_tokens)
kv_tensor_addr = kv_cache_block.gpu_ptr           # GPU pointer to KV data
kv_size_bytes = kv_cache_block.size_bytes

meta = virthub_connector.register_prefix_node(
    prefix_hash=prefix_hash,
    token_count=token_count,
    vaddr=kv_tensor_addr,
    size_bytes=kv_size_bytes,
    gpu_device_id=worker.device_id,
)
# meta contains rkey, lkey – used by remote nodes to issue RDMA reads
```

> **Important:** The GPU memory must be registered with the RDMA NIC
> (GPUDirect). Set `enable_gdr = true` in your Virthub config if you
> are using GPUs.

### 3.3. Load a Remote Prefix (Consumer)

When the scheduler finds a prefix that is **not** cached locally but
**is** known to exist on a remote node, fetch it before execution.
The following snippet is a **pseudo‑code**; replace the metadata
lookup and buffer allocation with your actual implementation.

```python
# Before running the forward pass for the prefix:
remote_node_addr = "192.168.1.10:19001"  # obtained from master/peer list
remote_meta = get_remote_prefix_metadata(prefix_hash)  # your lookup function

# Allocate a local GPU buffer for the incoming KV data
local_buffer = allocate_kv_cache(size_bytes=remote_meta.size_bytes)

virthub_connector.fetch_remote_prefix(
    peer_addr=remote_node_addr,
    remote_vaddr=remote_meta.vaddr,
    remote_rkey=remote_meta.rkey,
    local_vaddr=local_buffer.gpu_ptr,
    size_bytes=remote_meta.size_bytes,
)
# After fetch completes, local_buffer contains the KV tensors
# Now register this prefix locally so future requests hit local cache
virthub_connector.register_prefix_node(
    prefix_hash=prefix_hash,
    token_count=remote_meta.token_count,
    vaddr=local_buffer.gpu_ptr,
    size_bytes=remote_meta.size_bytes,
    gpu_device_id=worker.device_id,
)
```

### 3.4. Cleanup on Eviction

When the scheduler evicts a prefix, deregister it:

```python
virthub_connector.unregister_prefix(prefix_hash)
```

---

## 4. Obtaining Remote Metadata

The Virthub master/scheduler tracks which node owns each prefix.
You can query the local daemon for metadata via the Unix control socket
or use a simple distributed map. A practical approach is to let the
**first node** that registers a prefix broadcast its metadata to all
peers (using the embedded Raft state machine). The Virthub Python
bindings will soon expose a `get_prefix_location()` API; for now, you
can implement a simple lookup table shared via the daemon.

---

## 5. Testing

The SGLang connector is covered by the cross‑framework integration
tests (since it does not yet have a dedicated e2e file).

```bash
# Unit / integration test via cross‑framework file
.venv/bin/python -m pytest tests/python/integration/test_cross_framework_e2e.py -k sglang -v
```

If you have added a dedicated `test_sglang_connector.py`, adjust the path
accordingly. The in‑memory stub allows these tests to run without a
daemon or real RDMA hardware.
