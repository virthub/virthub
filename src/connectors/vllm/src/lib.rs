// virthub/src/connectors/vllm/src/lib.rs

//! vLLM Connector for PagedAttention KV-cache swapping over RDMA.
//!
//! This connector registers KV cache blocks with the RDMA transport and
//! provides methods to fetch remote blocks via one‑sided RDMA reads.  It is
//! used by both `VirthubKVConnectorV1` and `VirthubKVConnectorV2` Python
//! bindings without modification.
//!
//! ## Precision‑Scalable PSP‑KV Integration
//!
//! The connector now supports attaching a packed precision policy (from the
//! `precision` crate) and an optional PSP‑KV sidecar descriptor to each
//! registered block. This enables the upper‑level scheduler to store
//! precision decisions and sidecar metadata alongside the block’s RDMA
//! registration. The actual dequantization is performed by the GPU kernels;
//! this connector only stores and forwards the metadata.

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
pub enum VllmConnectorError {
    #[error("Configuration error: {0}")]
    ConfigError(String),

    #[error("RMA engine failure: {0}")]
    RmaEngineFailed(#[from] RmaEngineError),

    #[error("Transport communication failed: {0}")]
    TransportFailed(String),

    #[error("Block {0} not found in local registry")]
    BlockNotFound(u64),

    #[error("No block found with rkey {0}")]
    RkeyNotFound(u32),

    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),
}

/// Metadata returned when registering a KV block.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VllmBlockMeta {
    pub block_id: u64,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
    /// Optional precision policy assigned at allocation time.
    pub precision_policy: Option<PackedBlockPolicy>,
    /// Optional PSP‑KV sidecar descriptor for quantized blocks.
    pub sidecar: Option<PspKvSidecarDescriptor>,
}

/// vLLM Connector for PagedAttention KV-cache swapping over RDMA.
pub struct VirthubVllmConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    /// Map from block_id to registered region metadata.
    registered_blocks: Arc<RwLock<HashMap<u64, RegisteredRegion>>>,
    /// Map from block_id to packed precision policy.
    block_policies: Arc<DashMap<u64, PackedBlockPolicy>>,
    /// Map from block_id to sidecar descriptor.
    block_sidecars: Arc<DashMap<u64, PspKvSidecarDescriptor>>,
}

impl VirthubVllmConnector {
    /// Instantiate a new vLLM connector from configuration.
    pub fn new(config: VirthubConfig) -> Result<Self, VllmConnectorError> {
        let addr_str = format!("0.0.0.0:{}", config.transport.tcp.tcp_port);
        let listen_addr: SocketAddr = addr_str
            .parse()
            .map_err(|e| VllmConnectorError::ConfigError(format!(
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
            registered_blocks: Arc::new(RwLock::new(HashMap::new())),
            block_policies: Arc::new(DashMap::new()),
            block_sidecars: Arc::new(DashMap::new()),
        })
    }

    /// Factory method to match test expectation of `from_config`.
    pub fn from_config(config: &VirthubConfig) -> Result<Self, VllmConnectorError> {
        Self::new(config.clone())
    }

    /// Register a new KV block with the RDMA transport, without precision metadata.
    ///
    /// This is a convenience wrapper around `register_kv_block_with_policy` that
    /// passes `None` for both policy and sidecar.
    pub async fn register_kv_block(
        &self,
        block_id: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> Result<VllmBlockMeta, VllmConnectorError> {
        self.register_kv_block_with_policy(
            block_id,
            vaddr,
            size_bytes,
            gpu_device_id,
            None,
            None,
        ).await
    }

    /// Register a new KV block with optional precision policy and sidecar descriptor.
    ///
    /// # Arguments
    /// * `block_id` – user‑supplied block identifier (must be unique).
    /// * `vaddr` – virtual address of the block's memory buffer.
    /// * `size_bytes` – size of the block in bytes.
    /// * `gpu_device_id` – GPU device ID if using GPUDirect RDMA, otherwise 0.
    /// * `precision_policy` – optional packed precision policy (from the `precision` crate).
    /// * `sidecar` – optional PSP‑KV sidecar descriptor (for quantized formats).
    pub async fn register_kv_block_with_policy(
        &self,
        block_id: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
        precision_policy: Option<PackedBlockPolicy>,
        sidecar: Option<PspKvSidecarDescriptor>,
    ) -> Result<VllmBlockMeta, VllmConnectorError> {
        let gpu_opt = if self.config.transport.rdma.enable_gdr {
            Some(gpu_device_id)
        } else {
            None
        };

        let region = self
            .rma_engine
            .register_memory_region(vaddr, size_bytes, gpu_opt)?;

        let mut map = self.registered_blocks.write().unwrap();
        map.insert(block_id, region);

        // Store precision metadata if provided.
        if let Some(policy) = precision_policy {
            self.block_policies.insert(block_id, policy);
        }
        if let Some(desc) = sidecar {
            self.block_sidecars.insert(block_id, desc);
        }

        Ok(VllmBlockMeta {
            block_id,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
            precision_policy,
            sidecar,
        })
    }

    /// Unregister a previously registered block by its block_id.
    pub async fn unregister_kv_block(&self, block_id: u64) -> Result<(), VllmConnectorError> {
        let mut map = self.registered_blocks.write().unwrap();
        if let Some(region) = map.remove(&block_id) {
            self.rma_engine.deregister_memory_region(region.rkey)?;
            // Clean up precision metadata.
            self.block_policies.remove(&block_id);
            self.block_sidecars.remove(&block_id);
            Ok(())
        } else {
            Err(VllmConnectorError::BlockNotFound(block_id))
        }
    }

    /// Deregister a memory region by its remote key (rkey).
    pub fn deregister_memory_region(&self, rkey: u32) -> Result<(), VllmConnectorError> {
        let mut map = self.registered_blocks.write().unwrap();
        let mut block_id_to_remove = None;
        for (&bid, region) in map.iter() {
            if region.rkey == rkey {
                block_id_to_remove = Some(bid);
                break;
            }
        }
        match block_id_to_remove {
            Some(bid) => {
                let region = map.remove(&bid).unwrap();
                self.rma_engine.deregister_memory_region(region.rkey)?;
                self.block_policies.remove(&bid);
                self.block_sidecars.remove(&bid);
                Ok(())
            }
            None => Err(VllmConnectorError::RkeyNotFound(rkey)),
        }
    }

    /// Close the connector, deregistering all remaining blocks.
    pub fn close(&self) -> Result<(), VllmConnectorError> {
        let mut map = self.registered_blocks.write().unwrap();
        for (_, region) in map.drain() {
            self.rma_engine.deregister_memory_region(region.rkey)?;
        }
        self.block_policies.clear();
        self.block_sidecars.clear();
        Ok(())
    }

    /// Swap in (fetch) a remote block from a peer node via one‑sided RDMA.
    pub async fn swap_in_remote_block(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> Result<(), VllmConnectorError> {
        self.rma_engine
            .rdma_read(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
            .await?;
        Ok(())
    }

    /// Get the number of locally registered blocks.
    pub async fn registered_block_count(&self) -> usize {
        self.registered_blocks.read().unwrap().len()
    }

    /// Return the local node ID (from configuration).
    pub fn local_node_id(&self) -> u64 {
        self.config.parsed_node_id()
    }

    /// Return the packed precision policy for a given block, if present.
    pub fn get_block_policy(&self, block_id: u64) -> Option<PackedBlockPolicy> {
        self.block_policies.get(&block_id).map(|p| *p)
    }

    /// Return the sidecar descriptor for a given block, if present.
    pub fn get_block_sidecar(&self, block_id: u64) -> Option<PspKvSidecarDescriptor> {
        self.block_sidecars.get(&block_id).map(|d| *d)
    }

    /// Register a KV cache buffer region without block ID tracking.
    pub fn register_kv_cache(
        &self,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> Result<MemoryRegionHandle, VllmConnectorError> {
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
    ) -> Result<(), VllmConnectorError> {
        self.swap_in_remote_block(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
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
    async fn test_vllm_connector_registration_with_policy() {
        let config = create_test_config();
        let connector = VirthubVllmConnector::new(config).unwrap();

        let block_id = 42;
        let vaddr = 0x7fff_1000_0000;
        let size = 2 * 1024 * 1024;
        let gpu_id = 0;
        let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8, false, 0xFF_FFFF);
        let sidecar = PspKvSidecarDescriptor::default();

        let meta = connector
            .register_kv_block_with_policy(block_id, vaddr, size, gpu_id, Some(policy), Some(sidecar))
            .await
            .unwrap();

        assert_eq!(meta.block_id, block_id);
        assert_eq!(meta.vaddr, vaddr);
        assert_eq!(meta.size_bytes, size);
        assert_eq!(meta.precision_policy, Some(policy));
        assert_eq!(meta.sidecar, Some(sidecar));

        // Verify stored policy and sidecar
        assert_eq!(connector.get_block_policy(block_id), Some(policy));
        assert_eq!(connector.get_block_sidecar(block_id), Some(sidecar));

        connector.unregister_kv_block(block_id).await.unwrap();
        assert_eq!(connector.registered_block_count().await, 0);
        assert_eq!(connector.get_block_policy(block_id), None);
        assert_eq!(connector.get_block_sidecar(block_id), None);
    }

    #[tokio::test]
    async fn test_vllm_connector_swap_in() {
        let config = create_test_config();
        let connector = VirthubVllmConnector::new(config).unwrap();

        let peer: SocketAddr = "192.168.1.10:10000".parse().unwrap();
        let remote_vaddr = 0x7fff_2000_0000;
        let remote_rkey = 1234;
        let local_vaddr = 0x7fff_3000_0000;
        let size = 2 * 1024 * 1024;

        let result = connector
            .swap_in_remote_block(peer, remote_vaddr, remote_rkey, local_vaddr, size)
            .await;
        assert!(result.is_ok());
    }

    #[tokio::test]
    async fn test_deregister_by_rkey() {
        let config = create_test_config();
        let connector = VirthubVllmConnector::new(config).unwrap();

        let meta = connector
            .register_kv_block(1, 0x1000, 4096, 0)
            .await
            .unwrap();
        let rkey = meta.rkey;

        connector.deregister_memory_region(rkey).unwrap();
        assert_eq!(connector.registered_block_count().await, 0);

        assert!(connector.deregister_memory_region(rkey).is_err());
    }

    #[tokio::test]
    async fn test_close_cleans_up() {
        let config = create_test_config();
        let connector = VirthubVllmConnector::new(config).unwrap();

        connector
            .register_kv_block(1, 0x1000, 4096, 0)
            .await
            .unwrap();
        connector
            .register_kv_block(2, 0x2000, 4096, 0)
            .await
            .unwrap();
        assert_eq!(connector.registered_block_count().await, 2);

        connector.close().unwrap();
        // After close, maps are empty
        assert_eq!(connector.registered_block_count().await, 0);
        assert_eq!(connector.get_block_policy(1), None);
        assert_eq!(connector.get_block_sidecar(2), None);
    }
}
