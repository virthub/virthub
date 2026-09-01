# VLLM Integration Guide (Virthub)

This document describes how to integrate Virthub’s **distributed KV‑cache
sharing** into the vLLM inference engine. Once integrated, multiple vLLM
instances can automatically exchange KV‑cache blocks across nodes, reducing
memory pressure and enabling faster cold‑start times.

> **Status:** The Virthub vLLM connector (V1/V2) is implemented and tested
> with in‑memory stubs. Full RDMA acceleration is not yet available; the
> native Rust extension compiles but currently provides no‑op stubs for data
> movement. Real RDMA support is under development. This guide also covers
> version‑specific setup for vLLM 0.20.2 (Python 3.10, PyTorch 2.5.0, CUDA
> 12.1) and the necessary patch to register the `virthub` connector.

---

## 1. Overview

vLLM’s PagedAttention allocates KV‑cache blocks in GPU memory. With the
Virthub KV connector, these blocks can be **registered** for remote access
and **fetched** on demand from another node. The integration is transparent
to the model – vLLM’s scheduler simply calls the connector’s `save_kv_layer`
/ `start_load_kv` (V1) or `pre_forward` / `post_forward` (V2) methods.

Key benefits (once RDMA is enabled):

- **Cross‑node KV‑cache sharing** – avoid recomputing prompts that another
  node has already processed.
- **Reduced GPU memory usage** – offload infrequently used blocks to remote
  DRAM or NVMe via the Virthub multi‑tier store.
- **Zero‑copy RDMA** – when the native extension and hardware are available.

---

## 2. Prerequisites

- **Virthub daemon** running on each node (see `docs/MULTI_NODE.md`).
- **Virthub Python bindings** installed (`pip install -e bindings/python`).
  - To build the native Rust extension (currently provides API parity but
    no real RDMA):
    ```bash
    pip install maturin
    maturin develop --release -m bindings/rust/Cargo.toml
    ```
  - Verify the native module is importable:
    ```bash
    python -c "import _virthub; print('Native extension OK')"
    ```
- **vLLM** installed. Two supported paths:
  - **Latest vLLM (≥0.26.0)** – supports Python 3.10–3.13, CUDA 12.x/13.x.
  - **vLLM 0.20.2** – **requires Python 3.10 and PyTorch 2.5.0 with CUDA 12.1**.  
    Use the provided `install.sh` to set up this environment automatically:
    ```bash
    ./install.sh --with-integration --engine vllm --vllm-version "0.20.2" --python-version "3.10"
    ```
    This will install compatible dependencies and patch vLLM to register the
    Virthub connector (unless `--no-patch-vllm` is used).

---

## 3. Connector Registration

vLLM discovers external KV connectors via Python entry points. The Virthub
bindings already register the necessary entry points in `pyproject.toml`:

```toml
[project.entry-points."vllm.kv_connector"]
virthub = "virthub.vllm:VirthubKVConnector"
virthub_v1 = "virthub.vllm:VirthubKVConnectorV1"
virthub_v2 = "virthub.vllm:VirthubKVConnectorV2"
```

However, **vLLM 0.20.2 does not use entry points** for KV connectors; it
has a hard‑coded list in its factory. To enable `virthub`, you must **patch**
the factory. We provide a script for this:

```bash
source .venv/bin/activate   # or your vLLM 0.20.2 environment
python scripts/patch_vllm_connector.py
```

The script is idempotent and inserts a branch that imports
`VirthubKVConnectorV1` when `connector_name == "virthub"`.

For newer vLLM versions, entry points may work directly; verify with:

```bash
pip show virthub-bindings | grep -A5 "vllm.kv_connector"
```

---

## 4. Enabling the Connector in vLLM

Start the vLLM server with the `--kv-connector` flag:

```bash
vllm serve facebook/opt-125m \
    --kv-connector virthub \
    --kv-role kv_both
```

The `--kv-role` can be:

| Value | Behavior |
|-------|----------|
| `kv_producer` | Only **save** KV blocks (register for remote access). |
| `kv_consumer` | Only **load** remote KV blocks. |
| `kv_both` | Save and load (full sharing). |

For a multi‑node setup, use `kv_both` on every node.

> **Note:** Because real RDMA is not yet implemented, the connector
> currently uses a software stub and does not move actual data. Use this
> setup for API testing and integration development.

---

## 5. Multi‑Node Example

Assume two nodes:

- `node-1` (192.168.1.10) – runs the first vLLM instance.
- `node-2` (192.168.1.11) – runs the second vLLM instance.

Each node already has a Virthub daemon running (see `docs/MULTI_NODE.md`).
Use the configuration file `conf/cluster.toml` on each node, editing the
node‑specific fields.

### Node‑1

```bash
export VIRTHUB_CONFIG=/path/to/cluster.toml
vllm serve facebook/opt-125m \
    --host 0.0.0.0 --port 8000 \
    --kv-connector virthub \
    --kv-role kv_both
```

### Node‑2

```bash
export VIRTHUB_CONFIG=/path/to/cluster.toml
vllm serve facebook/opt-125m \
    --host 0.0.0.0 --port 8001 \
    --kv-connector virthub \
    --kv-role kv_both
```

When real RDMA is available, Node‑2 will fetch KV blocks from Node‑1
instead of recomputing. In the current state, both instances run but no
data transfer occurs.

---

## 6. Configuration Reference

The Virthub connector can be configured via the standard Virthub TOML file.
Key parameters for vLLM integration:

```toml
[vllm]
connector_version = "auto"   # "auto", "v1", or "v2"
```

- `auto` selects V2 if the vLLM version supports it; otherwise falls back to V1.
- `v1` forces the legacy `GPUModelRunner` adapter.
- `v2` forces the new `GPUModelRunnerV2` adapter.

All other settings (transport, storage tiers, prefetch) are shared with
the main Virthub configuration. See `conf/virthub.toml` and
`conf/cluster.toml` for complete examples.

---

## 7. Testing

Run the Virthub integration tests to verify the connector:

```bash
# Unit tests (in‑memory stub)
.venv/bin/python -m pytest tests/python/test_vllm_connector.py -v

# Integration test (starts daemon, tests engine creation with connector)
.venv/bin/python -m pytest tests/python/integration/test_vllm_e2e.py -v

# Cross‑framework e2e test (includes vLLM baseline and connector)
.venv/bin/python -m pytest tests/python/integration/test_cross_framework_e2e.py -v
```

> **Note:** The baseline generation test currently skips on CPU‑only
> machines due to a known vLLM issue. On GPU systems, it will run and should
> pass once the environment is correctly set up.

### Testing with vLLM 0.20.2

Use the dedicated environment created by `install.sh`:

```bash
source .venv/bin/activate
CUDA_VISIBLE_DEVICES=0 .venv/bin/python -m pytest tests/python/integration/test_vllm_e2e.py -v
```

If you see `RuntimeError: Cannot re-initialize CUDA in forked subprocess`,
ensure the environment variable `VLLM_WORKER_MULTIPROC_METHOD=spawn` is set.
Our `conftest.py` and `run.sh` already do this automatically.

After patching vLLM (Section 3), the connector tests (`test_engine_with_connector[*]`)
will run instead of skipping.
