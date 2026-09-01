// bindings/rust/src/lib.rs

//! PyO3 bindings for the Virthub distributed memory mesh.
//!
//! This module provides Python classes that wrap the native Rust connectors
//! for vLLM, LMCache, and SGLang. All methods are **synchronous** from the
//! Python perspective; asynchronous Rust methods are executed via an internal
//! Tokio runtime.

#![allow(unused_imports)]

use pyo3::prelude::*;
use std::net::{AddrParseError, SocketAddr};
use std::sync::OnceLock;
use store::kv_block::KvBlockKey as RustKvBlockKey;
use store::tier_manager::StorageTier as RustStorageTier;
use virthub_connector_vllm::VirthubVllmConnector as RustVllmConnector;
use virthub_connector_lmcache::VirthubLmCacheConnector as RustLmCacheConnector;
use virthub_connector_sglang::VirthubSglangConnector as RustSglangConnector;

fn runtime() -> &'static tokio::runtime::Runtime {
    static RUNTIME: OnceLock<tokio::runtime::Runtime> = OnceLock::new();
    RUNTIME.get_or_init(|| {
        tokio::runtime::Runtime::new()
            .expect("Failed to create Tokio runtime")
    })
}

fn load_config(path: &str) -> PyResult<virthub_config::VirthubConfig> {
    virthub_config::VirthubConfig::load_from_file(path)
        .map_err(|e| PyErr::new::<pyo3::exceptions::PyValueError, _>(e.to_string()))
}

#[pyclass]
#[derive(Clone)]
struct KvBlockKey {
    #[pyo3(get)]
    namespace_id: u64,
    #[pyo3(get)]
    block_id: u64,
}

#[pymethods]
impl KvBlockKey {
    #[new]
    fn new(namespace_id: u64, block_id: u64) -> Self {
        Self { namespace_id, block_id }
    }

    fn __repr__(&self) -> String {
        format!("KvBlockKey(ns={}, id={})", self.namespace_id, self.block_id)
    }

    fn __eq__(&self, other: &KvBlockKey) -> bool {
        self.namespace_id == other.namespace_id && self.block_id == other.block_id
    }
}

#[pyclass]
#[derive(Clone, PartialEq)]
enum StorageTier {
    Dram,
    Ssd,
    Vram,
}

impl StorageTier {
    fn to_rust(&self) -> RustStorageTier {
        match self {
            StorageTier::Dram => RustStorageTier::Dram,
            StorageTier::Ssd => RustStorageTier::Ssd,
            StorageTier::Vram => RustStorageTier::Vram,
        }
    }
}

#[pyclass]
#[derive(Clone)]
struct VllmBlockMeta {
    #[pyo3(get)]
    block_id: u64,
    #[pyo3(get)]
    vaddr: u64,
    #[pyo3(get)]
    size_bytes: usize,
    #[pyo3(get)]
    gpu_device_id: u32,
    #[pyo3(get)]
    rkey: u32,
    #[pyo3(get)]
    lkey: u32,
}

#[pyclass]
#[derive(Clone)]
struct LmCacheChunkMeta {
    #[pyo3(get)]
    namespace_id: u64,
    #[pyo3(get)]
    block_id: u64,
    #[pyo3(get)]
    tier: StorageTier,
    #[pyo3(get)]
    vaddr: u64,
    #[pyo3(get)]
    size_bytes: usize,
    #[pyo3(get)]
    gpu_device_id: u32,
    #[pyo3(get)]
    rkey: u32,
    #[pyo3(get)]
    lkey: u32,
}

#[pyclass]
#[derive(Clone)]
struct SglangPrefixMeta {
    #[pyo3(get)]
    prefix_hash: u64,
    #[pyo3(get)]
    token_count: u64,
    #[pyo3(get)]
    vaddr: u64,
    #[pyo3(get)]
    size_bytes: usize,
    #[pyo3(get)]
    gpu_device_id: u32,
    #[pyo3(get)]
    rkey: u32,
    #[pyo3(get)]
    lkey: u32,
}

#[pyclass]
struct VirthubVllmConnector {
    inner: RustVllmConnector,
}

#[pymethods]
impl VirthubVllmConnector {
    #[new]
    fn new(config_path: String) -> PyResult<Self> {
        let config = load_config(&config_path)?;
        let inner = RustVllmConnector::new(config)
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(Self { inner })
    }

    fn register_kv_block(
        &self,
        block_id: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> PyResult<VllmBlockMeta> {
        let meta = runtime()
            .block_on(self.inner.register_kv_block(block_id, vaddr, size_bytes, gpu_device_id))
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;

        Ok(VllmBlockMeta {
            block_id: meta.block_id,
            vaddr: meta.vaddr,
            size_bytes: meta.size_bytes,
            gpu_device_id: meta.gpu_device_id,
            rkey: meta.rkey,
            lkey: meta.lkey,
        })
    }

    fn swap_in_remote_block(
        &self,
        peer_addr: String,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> PyResult<()> {
        let addr: SocketAddr = peer_addr
            .parse()
            .map_err(|e: AddrParseError| PyErr::new::<pyo3::exceptions::PyValueError, _>(e.to_string()))?;
        runtime()
            .block_on(
                self.inner
                    .swap_in_remote_block(addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes),
            )
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(())
    }

    fn deregister_memory_region(&self, rkey: u32) -> PyResult<()> {
        self.inner
            .deregister_memory_region(rkey)
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(())
    }

    fn close(&self) -> PyResult<()> {
        self.inner
            .close()
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(())
    }
}

#[pyclass]
struct VirthubLmCacheConnector {
    inner: RustLmCacheConnector,
}

#[pymethods]
impl VirthubLmCacheConnector {
    #[new]
    fn new(config_path: String) -> PyResult<Self> {
        let config = load_config(&config_path)?;
        let inner = RustLmCacheConnector::new(config)
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(Self { inner })
    }

    fn put_chunk(
        &self,
        key: KvBlockKey,
        tier: StorageTier,
        gpu_device_id: u32,
        payload: Vec<u8>,
        vaddr: u64,
        length: usize,
    ) -> PyResult<LmCacheChunkMeta> {
        let native_key = RustKvBlockKey::new(key.namespace_id, key.block_id);
        let native_tier = tier.to_rust();

        let meta = runtime()
            .block_on(self.inner.put_chunk(
                native_key,
                native_tier,
                gpu_device_id,
                payload,
                vaddr,
                length,
            ))
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;

        Ok(LmCacheChunkMeta {
            namespace_id: meta.key.namespace_id,
            block_id: meta.key.block_id,
            tier: match meta.tier {
                RustStorageTier::Dram => StorageTier::Dram,
                RustStorageTier::Ssd => StorageTier::Ssd,
                RustStorageTier::Vram => StorageTier::Vram,
            },
            vaddr: meta.vaddr,
            size_bytes: meta.size_bytes,
            gpu_device_id: meta.gpu_device_id,
            rkey: meta.rkey,
            lkey: meta.lkey,
        })
    }

    fn get_chunk(&self, key: KvBlockKey) -> PyResult<Vec<u8>> {
        let native_key = RustKvBlockKey::new(key.namespace_id, key.block_id);
        runtime()
            .block_on(self.inner.get_chunk(&native_key))
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyKeyError, _>(e.to_string()))
    }

    fn remove_chunk(&self, key: KvBlockKey) -> PyResult<()> {
        let native_key = RustKvBlockKey::new(key.namespace_id, key.block_id);
        runtime()
            .block_on(self.inner.remove_chunk(&native_key))
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(())
    }

    fn deregister_memory_region(&self, _rkey: u32) -> PyResult<()> {
        Ok(())
    }

    fn close(&self) -> PyResult<()> {
        Ok(())
    }
}

#[pyclass]
struct VirthubSglangConnector {
    inner: RustSglangConnector,
}

#[pymethods]
impl VirthubSglangConnector {
    #[new]
    fn new(config_path: String) -> PyResult<Self> {
        let config = load_config(&config_path)?;
        let inner = RustSglangConnector::new(config)
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(Self { inner })
    }

    fn register_prefix_node(
        &self,
        prefix_hash: u64,
        token_count: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> PyResult<SglangPrefixMeta> {
        let meta = runtime()
            .block_on(self.inner.register_prefix_node(
                prefix_hash,
                token_count,
                vaddr,
                size_bytes,
                gpu_device_id,
            ))
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;

        Ok(SglangPrefixMeta {
            prefix_hash: meta.prefix_hash,
            token_count: meta.token_count,
            vaddr: meta.vaddr,
            size_bytes: meta.size_bytes,
            gpu_device_id: meta.gpu_device_id,
            rkey: meta.rkey,
            lkey: meta.lkey,
        })
    }

    fn fetch_remote_prefix(
        &self,
        peer_addr: String,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> PyResult<()> {
        let addr: SocketAddr = peer_addr
            .parse()
            .map_err(|e: AddrParseError| PyErr::new::<pyo3::exceptions::PyValueError, _>(e.to_string()))?;
        runtime()
            .block_on(
                self.inner
                    .fetch_remote_prefix(addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes),
            )
            .map_err(|e| PyErr::new::<pyo3::exceptions::PyRuntimeError, _>(e.to_string()))?;
        Ok(())
    }

    fn deregister_memory_region(&self, _rkey: u32) -> PyResult<()> {
        Ok(())
    }

    fn close(&self) -> PyResult<()> {
        Ok(())
    }
}

#[pymodule]
fn _virthub(m: &Bound<'_, PyModule>) -> PyResult<()> {
    m.add_class::<KvBlockKey>()?;
    m.add_class::<StorageTier>()?;
    m.add_class::<VirthubVllmConnector>()?;
    m.add_class::<VllmBlockMeta>()?;
    m.add_class::<VirthubLmCacheConnector>()?;
    m.add_class::<LmCacheChunkMeta>()?;
    m.add_class::<VirthubSglangConnector>()?;
    m.add_class::<SglangPrefixMeta>()?;
    Ok(())
}
