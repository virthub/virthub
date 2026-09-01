// virthub/src/connectors/sglang/src/lib.rs

use librmashim::{MemoryRegionHandle, RdmaEndpointConfig, RegisteredRegion, RmaEngineError, RmaTransportEngine};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::{Arc, RwLock};
use thiserror::Error;
use virthub_config::VirthubConfig;

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

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SglangPrefixMeta {
    pub prefix_hash: u64,
    pub token_count: u64,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
}

/// SGLang Connector for RadixAttention prefix sharing over RDMA.
pub struct VirthubSglangConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    /// Map from prefix_hash to registered region metadata.
    registered_prefixes: Arc<RwLock<HashMap<u64, RegisteredRegion>>>,
}

impl VirthubSglangConnector {
    /// Instantiate a new SGLang connector from configuration.
    pub fn new(config: VirthubConfig) -> Result<Self, SglangConnectorError> {
        let addr_str = format!("0.0.0.0:{}", config.transport.tcp.tcp_port);
        let listen_addr: SocketAddr = addr_str
            .parse()
            .map_err(|e| SglangConnectorError::ConfigError(format!("Invalid socket address '{addr_str}': {e}")))?;

        let verbs_config = RdmaEndpointConfig {
            device_name: Some(config.transport.rdma.device_name.clone()),
            enable_gdr: config.transport.rdma.enable_gdr,
            rq_prepost_count: config.transport.rdma.rq_prepost_count as u32, // cast to u32
            control_immediate: config.transport.rdma.control_immediate,
            ..Default::default()
        };

        let rma_engine = RmaTransportEngine::auto_detect(verbs_config, listen_addr)?;

        Ok(Self {
            config,
            rma_engine,
            registered_prefixes: Arc::new(RwLock::new(HashMap::new())),
        })
    }

    /// Factory method to match test expectation of `from_config`.
    pub fn from_config(config: &VirthubConfig) -> Result<Self, SglangConnectorError> {
        Self::new(config.clone())
    }

    /// Register a RadixAttention prefix node in the local memory region.
    ///
    /// # Arguments
    /// * `prefix_hash` - Hash of the prefix string.
    /// * `token_count` - Number of tokens in the prefix.
    /// * `vaddr` - Virtual address of the prefix KV tensor.
    /// * `size_bytes` - Size in bytes.
    /// * `gpu_device_id` - GPU device ID if using GPUDirect RDMA, otherwise 0.
    pub async fn register_prefix_node(
        &self,
        prefix_hash: u64,
        token_count: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
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

        Ok(SglangPrefixMeta {
            prefix_hash,
            token_count,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
        })
    }

    /// Fetch a remote prefix from a peer node via one‑sided RDMA.
    ///
    /// # Arguments
    /// * `peer_addr` - Socket address of the remote node.
    /// * `remote_vaddr` - Remote virtual address of the prefix data.
    /// * `remote_rkey` - Remote key of the prefix.
    /// * `local_vaddr` - Local virtual address to place the data.
    /// * `size_bytes` - Size of the prefix data.
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

    /// Get the number of locally registered prefixes.
    pub async fn registered_prefix_count(&self) -> usize {
        self.registered_prefixes.read().unwrap().len()
    }

    /// Return the local node ID (from configuration).
    pub fn local_node_id(&self) -> u64 {
        self.config.parsed_node_id()
    }

    /// Register a KV cache buffer region. This is a lower‑level variant that does not track prefix hashes.
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

    /// Asynchronously fetch a remote KV block via RDMA. This matches the original signature but uses the updated async method.
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
    use virthub_config::VirthubConfig;

    #[tokio::test]
    async fn test_sglang_connector_registration() {
        let config = VirthubConfig::default();
        let connector = VirthubSglangConnector::new(config).unwrap();

        let prefix_hash = 0xDEAD_BEEF_CAFE_0001;
        let token_count = 128;
        let vaddr = 0x7fff_3000_0000;
        let size = 1024 * 1024;
        let gpu_id = 0;

        let meta = connector
            .register_prefix_node(prefix_hash, token_count, vaddr, size, gpu_id)
            .await
            .unwrap();

        assert_eq!(meta.prefix_hash, prefix_hash);
        assert_eq!(meta.token_count, token_count);
        assert_eq!(connector.registered_prefix_count().await, 1);
    }

    #[tokio::test]
    async fn test_sglang_connector_fetch() {
        let config = VirthubConfig::default();
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
}
