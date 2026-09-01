# Virthub: A Distributed Shared Memory System for LLM Inference

**Virthub** (backed by the **KLNK** zero‑copy shared memory kernel transport) is a unified, hardware‑accelerated memory mesh designed for multi‑tier LLM KV‑cache sharing, ultra‑low latency remote memory access (RMA), and distributed virtual memory allocation.

> **⚡ Current Status**  
> Hardware‑accelerated RDMA and real eBPF telemetry are not yet fully integrated. The transport layer currently uses a software fallback (TCP/io_uring), and eBPF probes are simulated. The prefetch engine is fully wired but does not move data until real RDMA is enabled. All connectors and APIs are functional and ready for testing; multi‑node performance benefits will appear after hardware acceleration is implemented.

---

## 🏗️ Architecture Overview

```
  +-------------------------------------------------+
  |              LLM Inference Runtime              |
  |        (vLLM / SGLang / LMCache Engines)        |
  +-------------------------------------------------+
                            |
                 [ Integration Connectors ]
                            |
  +-------------------------------------------------+
  |                     VIRTHUB                     |
  |                                                 |
  |  * Master & Scheduler (Raft, NUMA Placement)    |
  |  * Indexing Subsystem (RobinHood, Radix)        |
  |  * Multi-Tier Store   (VRAM / DRAM / NVMe)      |
  |                                                 |
  |  +-------------------------------------------+  |
  |  |         KLNK Engine (klnk-daemon)         |  |
  |  |   - Zero-copy UFFDIO_MOVE                 |  |
  |  |   - eBPF Telemetry (stride detection)     |  |
  |  |   - Prefetch Engine (speculative RDMA)    |  |
  |  |   - Batch fault processing                |  |
  |  |   - RDMA-based invalidation & versioned   |  |
  |  |     coherence for distributed DSM         |  |
  |  +-------------------------------------------+  |
  +-------------------------+-----------------------+
                            |
               [ librmashim Transport Layer ]
                            |
  +-------------------------------------------------+
  |      Hardware: RDMA (Verbs/GDR) / TCP io_uring  |
  +-------------------------------------------------+
```

---

## 🧩 Key Components & Crate Layout

| Component Directory | Module Crate | Functionality & Role |
| --- | --- | --- |
| **`virthub/src/klnk/klnk-core/`** | `klnk-core` | Core domain types, control plane, and diff engine for DSM. |
| **`virthub/src/klnk/klnk-daemon/`** | `klnk-daemon` | Main daemon with UFFD handler, RDMA metadata publisher, prefetch engine, batch fault processor, and RDMA‑based invalidation with versioned coherence for distributed DSM. |
| **`virthub/src/klnk/klnk-net/`** | `klnk-net` | Transport layer for control messages (bincode serialization) with both Tokio and io_uring backends. |
| **`virthub/src/klnk/klnk-ebpf/`** | `klnk-ebpf` | eBPF telemetry for stride detection (currently simulated; real probes planned). |
| **`virthub/src/klnk/klnk-uffd/`** | `klnk-uffd` | Userfaultfd handler for zero‑copy page fault resolution with `UFFDIO_MOVE`. |
| **`virthub/src/klnk/klnk-shim/`** | `klnk-shim` | LD_PRELOAD shim for staging memory allocation. |
| **`virthub/src/klnk/librmashim/`** | `librmashim` | Hardware transport shim providing zero‑copy RDMA Read/Write over Verbs (software fallback currently). |
| **`virthub/src/store/`** | `store` | Multi‑tier storage manager governing L0 (GPU VRAM), L1 (Host DRAM), and L2 (NVMe SSD) page promotion/demotion, plus staging pool. |
| **`virthub/src/index/`** | `index` | Lock‑free Robin Hood hash index (128‑bit key), 64‑bit virtual address radix tree, and 2Q cache eviction policy. |
| **`virthub/src/master/`** | `master` | Embedded Raft consensus state engine and NUMA‑aware, load‑balanced cluster placement scheduler. |
| **`virthub/src/connectors/`** | `vllm`, `sglang`, `lmcache` | Specialized zero‑copy KV‑cache swapping connectors for LLM inference frameworks (Rust client). |
| **`bindings/python/`** | `virthub` (Python package) | Python bindings for vLLM (V1 & V2), SGLang, and LMCache integration. Includes in‑memory stubs for testing when the Rust client is not built. |

---

## 🚀 Getting Started

### 1. Install Dependencies

The [`install.sh`](./install.sh) script installs all system packages, Rust, and Python dependencies.  
It also builds the native Rust extension (`_virthub`) automatically via `maturin`.

```bash
./install.sh                     # basic (Rust + dev tools + native extension)
./install.sh --with-integration --engine vllm   # also installs vLLM + LMCache
./install.sh --with-integration --engine sglang # installs SGLang + LMCache
```

The Python virtual environment is created at `.venv/` and activated automatically by `run.sh` for Python tests.

### 2. Build the Workspace

```bash
cargo build --release --workspace
```

### 3. Configuration (`conf/virthub.toml`)

Virthub uses a central TOML configuration file. Below is an example with all major sections, including the new `[vllm]` block for connector version selection:

```toml
[general]
log_level = "info"
control_socket = "/tmp/virthub_control.sock"
data_bind_addr = "0.0.0.0:19001"
node_id = "node-1"

[klnk]
enable_uffd_move = true
fallback_copy = true
staging_num_pages = 16
huge_page_size = 2097152

[store]
block_size = 2097152

[store.tier]
l0_enabled = false
l0_device_ids = [0, 1]
l1_enabled = true
l2_enabled = false
l2_path = "/mnt/nvme/virthub_cache"

[master.raft]
embedded = true
initial_peers = ["node-1", "node-2", "node-3"]
etcd_endpoints = ["http://127.0.0.1:2379"]

[master.scheduler]
prefetch_window = 8
l0_promote_threshold = 100
l1_demote_idle_secs = 60
lru_decay = 0.8

[master.sharding]
shard_count = 64

[transport]
default_protocol = "rdma"

[transport.rdma]
device_name = ""
enable_gdr = false
rq_prepost_count = 1024
control_immediate = true

[transport.tcp]
io_uring_enabled = true
tcp_port = 19002

[ebpf]
enabled = true
program_path = "/usr/lib/virthub/stride_tracer.bpf.o"
report_interval_ms = 100

[tuning]
numa_node = -1
operation_timeout_ms = 500
memlock_limit = 17179869184  # 16 GB

# vLLM integration settings (new)
[vllm]
connector_version = "auto"   # "auto", "v1", or "v2"
```

For a **multi‑node cluster**, use the ready‑to‑use example in `conf/cluster.toml`.  
See [docs/MULTI_NODE.md](docs/MULTI_NODE.md) for step‑by‑step instructions.

### 4. Running the Service Daemon

```bash
export VIRTHUB_CONFIG="conf/virthub.toml"
./target/release/klnk-daemon
```

The daemon will listen on the default Unix socket `/tmp/virthub_control.sock` unless changed in the configuration.

You can also start a local 3‑node cluster for testing:

```bash
./run.sh --cluster --cluster-config conf/cluster.toml
```

---

## 📦 Python Bindings

The Python bindings are located in [`bindings/python/`](./bindings/python/). They provide connectors for vLLM, SGLang, and LMCache. When the native Rust client is not built, **in‑memory stubs** are automatically used, allowing tests and development without RDMA hardware.

### Installation

The bindings are installed automatically by `install.sh`. To install manually:

```bash
cd bindings/python
pip install -e .[dev]   # install with test dependencies
```

### Usage

```python
from virthub.vllm import VirthubKVConnector      # auto-selects V1 or V2 adapter
from virthub.sglang import VirthubSglangConnector
from virthub.lmcache import VirthubLmCacheConnector

# Example: vLLM connector (works with GPUModelRunner V1 or V2)
config = { ... }  # your Virthub config
connector = VirthubKVConnector(config)
meta = connector.save_kv_layer(...)   # or pre_forward/post_forward for V2
```

---

## 🦀 Framework Integration Examples (Rust)

> **Note:** The crate names in Rust use underscores, e.g., `virthub_connector_vllm`, `virthub_connector_sglang`, `virthub_connector_lmcache`.

### 1. vLLM (PagedAttention KV‑Cache Swapping)

```rust
use virthub_config::VirthubConfig;
use virthub_connector_vllm::VirthubVllmConnector;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = VirthubConfig::load_from_file("conf/virthub.toml")?;
    let connector = VirthubVllmConnector::new(config)?;

    let block_id = 1001;
    let vaddr = 0x7fff_3000_0000u64;
    let size = 2 * 1024 * 1024;

    let meta = connector.register_kv_block(block_id, vaddr, size, 0).await?;
    println!("Registered vLLM Block {} with rkey: {}", meta.block_id, meta.rkey);

    Ok(())
}
```

### 2. SGLang (RadixAttention Prefix Sharing)

```rust
use virthub_config::VirthubConfig;
use virthub_connector_sglang::VirthubSglangConnector;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = VirthubConfig::load_from_file("conf/virthub.toml")?;
    let connector = VirthubSglangConnector::new(config)?;

    let prefix_hash = 0xABCD_1234_5678u64;
    let token_count = 64;
    let vaddr = 0x7fff_4000_0000u64;
    let size = 2 * 1024 * 1024;

    connector.register_prefix_node(prefix_hash, token_count, vaddr, size, 0).await?;
    println!("Registered SGLang RadixAttention prefix node 0x{:x}", prefix_hash);

    Ok(())
}
```

### 3. LMCache (Hierarchical KV Chunk Storage)

```rust
use virthub_config::VirthubConfig;
use virthub_connector_lmcache::VirthubLmCacheConnector;
use store::kv_block::KvBlockKey;
use store::tier_manager::StorageTier;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let config = VirthubConfig::load_from_file("conf/virthub.toml")?;
    let connector = VirthubLmCacheConnector::new(config)?;

    let key = KvBlockKey::new(1, 500);
    let payload = vec![0xEEu8; 1024];
    let vaddr = 0x7fff_5000_0000u64;

    connector.put_chunk(key, StorageTier::Dram, 0, payload, vaddr, 1024).await?;
    println!("Stored LMCache chunk {} across local tier manager", key);

    Ok(())
}
```

---

## 🧪 Testing

Virthub uses a unified `run.sh` script to run both Rust and Python tests. The Python tests use **in‑memory stubs** when the Rust client is not available, so they can run immediately after installation.

| Command | Description |
|---------|-------------|
| `./run.sh --test` | Run Rust unit & integration tests (`cargo test`). |
| `./run.sh --test <crate>` | Run tests for a specific Rust crate. |
| `./run.sh --test-python` | Run Python unit tests (fast, mock‑based, no external dependencies). |
| `./run.sh --test-integration` | Run Python integration tests (requires `klnk-daemon` binary; tests skip if engines are not installed or unsupported). |
| `./run.sh --test-all` | Run both Rust and Python tests (unit + integration). |
| `./run.sh --cluster` | Start a local 3‑node cluster for multi‑node testing. |

> **Note:** Python integration tests are skipped automatically if the required dependencies are not installed. To install them, run `./install.sh --with-integration --engine <name>`. The vLLM baseline test currently skips on CPU‑only machines because of a known vLLM issue; this does not affect Virthub's own functionality.

---

## 📖 Further Documentation

- [Multi‑Node Deployment Guide](docs/MULTI_NODE.md) – set up a cluster with multiple daemons.
- [vLLM Integration Guide](docs/VLLM_INTEGRATION.md) – connect vLLM to Virthub.
- [SGLang Integration Guide](docs/SGLANG_INTEGRATION.md) – share RadixAttention prefixes.
- [LMCache Integration Guide](docs/LMCACHE_INTEGRATION.md) – use Virthub as a remote tier for LMCache.

---

## 📄 Academic Artifact Notice

This `main` branch contains the **active Rust rewrite** of Virthub.

The original **C implementation of KLNK** evaluated in the associated paper [[10.1109/TPDS.2024.3412833](https://ieeexplore.ieee.org/document/10549837)] is preserved in the [`original`](https://github.com/virthub/virthub/tree/original) branch.

---

## 📜 License

This project is licensed under the [MIT License](LICENSE).
