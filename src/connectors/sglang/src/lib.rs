// virthub/src/connectors/sglang/src/lib.rs

//! SGLang Connector for RadixAttention prefix sharing over RDMA.
//!
//! This connector registers RadixAttention prefix nodes with the RDMA
//! transport and provides methods to fetch remote prefixes via one‑sided
//! RDMA reads. It is used by the SGLang integration to share KV-cache
//! prefixes across nodes.
//!
//! ## Precision‑Scalable PSP‑KV Integration
//!
//! Like the vLLM connector, this connector now supports attaching a packed
//! precision policy (from the `precision` crate) and an optional PSP‑KV
//! sidecar descriptor to each registered prefix node. This enables the
//! scheduler to store precision decisions and sidecar metadata alongside
//! the prefix’s RDMA registration.

use dashmap::DashMap;
use librmashim::{
    MemoryRegionHandle, RdmaEndpointConfig, RegisteredRegion, RmaEngineError,
    RmaTransportEngine,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::{Arc, RwLock};
use thiserror::Error;
use virthub_config::VirthubConfig;

use precision::policy::PackedBlockPolicy;
use store::psp_kv::PspKvSidecarDescriptor;

#[derive(Debug, Error)]
pub enum SglangConnectorError {
    #[error("Configuration error: {0}")]
    ConfigError(String),

    #[error("RMA engine failure: {0}")]
    RmaEngineFailed(#[from] RmaEngineError),

    #[error("Transport communication failed: {0}")]
    TransportFailed(String),

    #[error("Prefix node {0:#x} not found in local registry")]
    PrefixNotFound(u64),

    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Metadata returned when registering a RadixAttention prefix node.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SglangPrefixMeta {
    pub prefix_hash: u64,
    pub token_count: u64,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
    /// Optional precision policy assigned at allocation time.
    pub precision_policy: Option<PackedBlockPolicy>,
    /// Optional PSP‑KV sidecar descriptor for quantized prefixes.
    pub sidecar: Option<PspKvSidecarDescriptor>,
}

/// SGLang Connector for RadixAttention prefix sharing over RDMA.
pub struct VirthubSglangConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    /// Map from prefix_hash to registered region metadata.
    registered_prefixes: Arc<RwLock<HashMap<u64, RegisteredRegion>>>,
    /// Map from prefix_hash to packed precision policy.
    prefix_policies: Arc<DashMap<u64, PackedBlockPolicy>>,
    /// Map from prefix_hash to sidecar descriptor.
    prefix_sidecars: Arc<DashMap<u64, PspKvSidecarDescriptor>>,
}

impl VirthubSglangConnector {
    /// Instantiate a new SGLang connector from configuration.
    pub fn new(config: VirthubConfig) -> Result<Self, SglangConnectorError> {
        let addr_str = format!("0.0.0.0:{}", config.transport.tcp.tcp_port);
        let listen_addr: SocketAddr = addr_str
            .parse()
            .map_err(|e| SglangConnectorError::ConfigError(format!(
                "Invalid socket address '{}': {}",
                addr_str, e
            )))?;

        let verbs_config = RdmaEndpointConfig {
            device_name: Some(config.transport.rdma.device_name.clone()),
            enable_gdr: config.transport.rdma.enable_gdr,
            rq_prepost_count: config.transport.rdma.rq_prepost_count as u32,
            control_immediate: config.transport.rdma.control_immediate,
            ..Default::default()
        };

        let rma_engine = RmaTransportEngine::auto_detect(verbs_config, listen_addr)?;

        Ok(Self {
            config,
            rma_engine,
            registered_prefixes: Arc::new(RwLock::new(HashMap::new())),
            prefix_policies: Arc::new(DashMap::new()),
            prefix_sidecars: Arc::new(DashMap::new()),
        })
    }

    /// Factory method to match test expectation of `from_config`.
    pub fn from_config(config: &VirthubConfig) -> Result<Self, SglangConnectorError> {
        Self::new(config.clone())
    }

    /// Register a RadixAttention prefix node with the RDMA transport,
    /// without precision metadata.
    ///
    /// This is a convenience wrapper around `register_prefix_node_with_policy`
    /// that passes `None` for both policy and sidecar.
    pub async fn register_prefix_node(
        &self,
        prefix_hash: u64,
        token_count: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> Result<SglangPrefixMeta, SglangConnectorError> {
        self.register_prefix_node_with_policy(
            prefix_hash,
            token_count,
            vaddr,
            size_bytes,
            gpu_device_id,
            None,
            None,
        ).await
    }

    /// Register a RadixAttention prefix node with optional precision policy
    /// and sidecar descriptor.
    ///
    /// # Arguments
    /// * `prefix_hash` – hash of the prefix.
    /// * `token_count` – number of tokens in the prefix.
    /// * `vaddr` – virtual address of the prefix KV tensor.
    /// * `size_bytes` – size in bytes.
    /// * `gpu_device_id` – GPU device ID if using GPUDirect RDMA, otherwise 0.
    /// * `precision_policy` – optional packed precision policy.
    /// * `sidecar` – optional PSP‑KV sidecar descriptor.
    pub async fn register_prefix_node_with_policy(
        &self,
        prefix_hash: u64,
        token_count: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
        precision_policy: Option<PackedBlockPolicy>,
        sidecar: Option<PspKvSidecarDescriptor>,
    ) -> Result<SglangPrefixMeta, SglangConnectorError> {
        let gpu_opt = if self.config.transport.rdma.enable_gdr {
            Some(gpu_device_id)
        } else {
            None
        };

        let region = self
            .rma_engine
            .register_memory_region(vaddr, size_bytes, gpu_opt)?;

        let mut map = self.registered_prefixes.write().unwrap();
        map.insert(prefix_hash, region);

        if let Some(policy) = precision_policy {
            self.prefix_policies.insert(prefix_hash, policy);
        }
        if let Some(desc) = sidecar {
            self.prefix_sidecars.insert(prefix_hash, desc);
        }

        Ok(SglangPrefixMeta {
            prefix_hash,
            token_count,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
            precision_policy,
            sidecar,
        })
    }

    /// Fetch a remote prefix from a peer node via one‑sided RDMA.
    pub async fn fetch_remote_prefix(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> Result<(), SglangConnectorError> {
        self.rma_engine
            .rdma_read(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
            .await?;
        Ok(())
    }

    /// Unregister a prefix node by its hash.
    pub async fn unregister_prefix(&self, prefix_hash: u64) -> Result<(), SglangConnectorError> {
        let mut map = self.registered_prefixes.write().unwrap();
        if let Some(region) = map.remove(&prefix_hash) {
            self.rma_engine.deregister_memory_region(region.rkey)?;
            self.prefix_policies.remove(&prefix_hash);
            self.prefix_sidecars.remove(&prefix_hash);
            Ok(())
        } else {
            Err(SglangConnectorError::PrefixNotFound(prefix_hash))
        }
    }

    /// Get the number of locally registered prefixes.
    pub async fn registered_prefix_count(&self) -> usize {
        self.registered_prefixes.read().unwrap().len()
    }

    /// Return the local node ID (from configuration).
    pub fn local_node_id(&self) -> u64 {
        self.config.parsed_node_id()
    }

    /// Return the packed precision policy for a given prefix, if present.
    pub fn get_prefix_policy(&self, prefix_hash: u64) -> Option<PackedBlockPolicy> {
        self.prefix_policies.get(&prefix_hash).map(|p| *p)
    }

    /// Return the sidecar descriptor for a given prefix, if present.
    pub fn get_prefix_sidecar(&self, prefix_hash: u64) -> Option<PspKvSidecarDescriptor> {
        self.prefix_sidecars.get(&prefix_hash).map(|d| *d)
    }

    /// Register a KV cache buffer region without prefix tracking.
    pub fn register_kv_cache(
        &self,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> Result<MemoryRegionHandle, SglangConnectorError> {
        let gpu_opt = if self.config.transport.rdma.enable_gdr {
            Some(gpu_device_id)
        } else {
            None
        };
        let region = self
            .rma_engine
            .register_memory_region(vaddr, size_bytes, gpu_opt)?;
        Ok(region)
    }

    /// Asynchronously fetch a remote KV block via RDMA (legacy method).
    pub async fn fetch_remote_kv_block(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> Result<(), SglangConnectorError> {
        self.fetch_remote_prefix(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
            .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use precision::policy::{PackedBlockPolicy, PrecisionLevel};
    use virthub_config::VirthubConfig;

    fn create_test_config() -> VirthubConfig {
        VirthubConfig::default()
    }

    #[tokio::test]
    async fn test_sglang_connector_registration_with_policy() {
        let config = create_test_config();
        let connector = VirthubSglangConnector::new(config).unwrap();

        let prefix_hash = 0xDEAD_BEEF_CAFE_0001;
        let token_count = 128;
        let vaddr = 0x7fff_3000_0000;
        let size = 1024 * 1024;
        let gpu_id = 0;
        let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8, false, 0xFF_FFFF);
        let sidecar = PspKvSidecarDescriptor::default();

        let meta = connector
            .register_prefix_node_with_policy(
                prefix_hash,
                token_count,
                vaddr,
                size,
                gpu_id,
                Some(policy),
                Some(sidecar),
            )
            .await
            .unwrap();

        assert_eq!(meta.prefix_hash, prefix_hash);
        assert_eq!(meta.token_count, token_count);
        assert_eq!(meta.precision_policy, Some(policy));
        assert_eq!(meta.sidecar, Some(sidecar));

        assert_eq!(connector.get_prefix_policy(prefix_hash), Some(policy));
        assert_eq!(connector.get_prefix_sidecar(prefix_hash), Some(sidecar));

        connector.unregister_prefix(prefix_hash).await.unwrap();
        assert_eq!(connector.registered_prefix_count().await, 0);
        assert_eq!(connector.get_prefix_policy(prefix_hash), None);
        assert_eq!(connector.get_prefix_sidecar(prefix_hash), None);
    }

    #[tokio::test]
    async fn test_sglang_connector_fetch() {
        let config = create_test_config();
        let connector = VirthubSglangConnector::new(config).unwrap();

        let peer: SocketAddr = "192.168.1.10:10000".parse().unwrap();
        let remote_vaddr = 0x7fff_4000_0000;
        let remote_rkey = 4321;
        let local_vaddr = 0x7fff_5000_0000;
        let size = 1024 * 1024;

        let result = connector
            .fetch_remote_prefix(peer, remote_vaddr, remote_rkey, local_vaddr, size)
            .await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_unregister_nonexistent_prefix() {
        let config = create_test_config();
        let connector = VirthubSglangConnector::new(config).unwrap();

        let result = connector.unregister_prefix(0x1234).await;
        assert!(matches!(result, Err(SglangConnectorError::PrefixNotFound(_))));
    }
}
