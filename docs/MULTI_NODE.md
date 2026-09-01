# Multi‑Node Deployment Guide (Virthub)

This guide walks you through setting up a multi‑node Virthub cluster for distributed KV‑cache sharing across LLM inference engines. By the end, you will have:

- 2 or 3 machines (or containers) running the `klnk-daemon` service.
- Each node able to register GPU memory regions and fetch KV blocks from remote peers via RDMA (or TCP fallback).

---

## Prerequisites

### Hardware / OS

* **Linux kernel ≥ 6.8** – recommended for zero‑copy `UFFDIO_MOVE`. Older kernels work with `UFFDIO_COPY` fallback but with higher latency.
* **RDMA NICs** – InfiniBand or RoCE (optional; TCP fallback is built in).
* **GPUs** – for GPU‑direct RDMA (`enable_gdr = true`). CPU‑only clusters are also supported but currently blocked by a vLLM CPU engine bug (the Virthub daemon itself works on CPU).

### Software

* **Rust** – stable toolchain (installed by `install.sh`).
* **Python ≥ 3.10** – for the Python bindings and test suite.
* **vLLM / SGLang / LMCache** (optional) – needed only if you intend to run the full integration tests with an inference framework.

*All system dependencies can be installed with:*
```bash
./install.sh
```

---

## 1. Build the Native Rust Extension (Optional)

The Python bindings can use a native `_virthub` module for RDMA operations. If you want real RDMA or TCP data movement (instead of the in‑memory stub), build it:

```bash
# From the repository root
pip install maturin
maturin develop --release -m bindings/rust/Cargo.toml
```

Verify with:

```bash
python -c "import _virthub; print('OK')"
```

> **Note:** If you don’t need the native extension yet (e.g., for testing with in‑memory stubs), skip this step. The Python layer will automatically fall back to an in‑memory stub. The stub validates the API flow but does not move data between nodes.

---

## 2. Configuration

A ready‑to‑use example for a 3‑node cluster is provided in **`conf/cluster.toml`**. Copy it to each node and edit the node‑specific values:

| Field | Description |
|-------|-------------|
| `node_id` | Unique string, e.g. `"node-1"`, `"node-2"`, `"node-3"` |
| `data_bind_addr` | IP and port for the data plane (RDMA or TCP). Use the node’s actual IP for inter‑node communication. |
| `control_socket` | Path to the Unix domain socket. Keep it unique per node if running multiple daemons on the same host. |
| `initial_peers` | Must contain **all** node IDs. Same list on every node. |

> **Important:** All nodes must be able to reach each other on the `data_bind_addr` ports. Configure your firewall / security groups accordingly.

> **Note:** The Python connector (`VirthubKVConnector`) expects a **dictionary** matching the TOML structure, not the file path. If you are configuring via TOML, use `VirthubConfig::load_from_file` on the Rust side or pass the dictionary extracted from that file.

---

## 3. Start the Daemons

On each node, start the daemon with its configuration:

```bash
export VIRTHUB_CONFIG=/path/to/cluster.toml
./target/release/klnk-daemon
```

The daemon will:
- Create the Unix control socket.
- Bind the data plane port.
- Join the embedded Raft cluster (waiting for peers to become available).

Check the logs (`RUST_LOG=info` by default) to confirm the daemon has discovered its peers and is ready.

---

## 4. Verify Cross‑Node Operation

Use the Python connectors to test that a KV block registered on one node can be fetched from another. Below is a minimal script – run it on a machine that can reach both daemons.

> **Important:** In the current V1 adapter, `save_kv_layer` and `start_load_kv` are **synchronous** (not `async`). Therefore, do **not** use `await` when calling them. The script below reflects this.

```python
import asyncio
from virthub.vllm import VirthubKVConnector

async def main():
    # Connect to node‑1
    cfg1 = {
        "general": {
            "control_socket": "/tmp/virthub_node1.sock",
            "data_bind_addr": "192.168.1.10:19001",
            "node_id": "node-1",
        },
        "transport": {"default_protocol": "tcp"},
    }
    conn1 = VirthubKVConnector(cfg1)

    # Connect to node‑2
    cfg2 = {
        "general": {
            "control_socket": "/tmp/virthub_node2.sock",
            "data_bind_addr": "192.168.1.11:19001",
            "node_id": "node-2",
        },
        "transport": {"default_protocol": "tcp"},
    }
    conn2 = VirthubKVConnector(cfg2)

    # Register a block on node‑1
    block_id = 42
    vaddr = 0x7FFF_1000_0000
    size = 2 * 1024 * 1024
    # V1 save_kv_layer is synchronous
    conn1.save_kv_layer(0, [MockBlock(block_id, vaddr, size)], MockWorker(0))
    print("Block registered on node‑1")

    # Fetch it on node‑2
    conn2.start_load_kv([MockBlock(block_id, 0x7FFF_2000_0000, size)], MockWorker(0))
    print("Block loaded on node‑2:", conn2.block_map.get(block_id))

    conn1.close()
    conn2.close()

# Minimal mocks – replace with real vLLM blocks when integrated.
class MockWorker:
    def __init__(self, device_id): self.device_id = device_id
class MockBlock:
    def __init__(self, block_id, gpu_ptr, size):
        self.block_id = block_id
        self.gpu_ptr = gpu_ptr
        self.size = size

asyncio.run(main())
```

If the native extension is built, the block data is actually transferred via RDMA (or TCP). With the in‑memory stub, the script still runs and validates the API flow.

---

## 5. Integration with Inference Frameworks

### vLLM

1. Ensure the vLLM entry points are registered (they are automatically when you run `pip install -e .` from `bindings/python`).
2. Start vLLM with the Virthub KV connector:

   ```bash
   vllm serve facebook/opt-125m \
       --kv-connector virthub \
       --kv-role kv_both
   ```

3. On a second node (or the same node with a different port), start another instance. They will share KV‑cache blocks via the daemon.

### SGLang / LMCache

Full runtime integration is still in progress. The connector stubs can already be instantiated and tested (see `tests/python/integration/`).

---

## 6. Monitoring

> **Note:** A Prometheus endpoint is planned but **not yet implemented** in the current codebase. The section below describes the intended configuration once it becomes available.

Future configuration (example only, not yet functional):

```toml
[observability]
metrics_bind_addr = "0.0.0.0:9090"
```

When implemented, you would scrape `http://<node-ip>:9090/metrics` to monitor:

- Active memory regions
- RDMA completion queue depth
- Raft cluster status
- Page fault resolution latency
