// virthub/src/connectors/vllm/src/lib.rs

//! vLLM Connector for PagedAttention KV-cache swapping over RDMA.
//!
//! This connector registers KV cache blocks with the RDMA transport and
//! provides methods to fetch remote blocks via one‑sided RDMA reads.  It is
//! used by both `VirthubKVConnectorV1` and `VirthubKVConnectorV2` Python
//! bindings without modification.
//!
//! ## Variable‑Sized Coherence & Lazy Self‑Invalidation
//!
//! The daemon now manages coherence at arbitrary block sizes.  The connector
//! simply passes the exact block size and virtual address to the control
//! plane; no alignment is enforced.  Lazy self‑invalidation (version checking)
//! is handled by the Python adapter using `control_plane.get_page_version()`
//! before calling `swap_in_remote_block` – this logic lives in the Python
//! bindings and does not require changes here.

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

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VllmBlockMeta {
    pub block_id: u64,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
}

/// vLLM Connector for PagedAttention KV-cache swapping over RDMA.
pub struct VirthubVllmConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    /// Map from block_id to registered region metadata.
    registered_blocks: Arc<RwLock<HashMap<u64, RegisteredRegion>>>,
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
        })
    }

    /// Factory method to match the test expectation of `from_config`.
    pub fn from_config(config: &VirthubConfig) -> Result<Self, VllmConnectorError> {
        Self::new(config.clone())
    }

    /// Register a new KV block with the RDMA transport.
    ///
    /// # Arguments
    /// * `block_id` – user‑supplied block identifier (must be unique).
    /// * `vaddr` – virtual address of the block's memory buffer.
    /// * `size_bytes` – size of the block in bytes (any value; no alignment enforced).
    /// * `gpu_device_id` – GPU device ID if using GPUDirect RDMA, otherwise 0.
    pub async fn register_kv_block(
        &self,
        block_id: u64,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
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

        Ok(VllmBlockMeta {
            block_id,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
        })
    }

    /// Unregister a previously registered block by its block_id.
    pub async fn unregister_kv_block(&self, block_id: u64) -> Result<(), VllmConnectorError> {
        let mut map = self.registered_blocks.write().unwrap();
        if let Some(region) = map.remove(&block_id) {
            self.rma_engine.deregister_memory_region(region.rkey)?;
            Ok(())
        } else {
            Err(VllmConnectorError::BlockNotFound(block_id))
        }
    }

    /// Deregister a memory region by its remote key (rkey).
    /// This is useful when the block_id is not known to the Python side.
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
                Ok(())
            }
            None => Err(VllmConnectorError::RkeyNotFound(rkey)),
        }
    }

    /// Close the connector, deregistering all remaining blocks.
    /// After this call, the connector should not be used again.
    pub fn close(&self) -> Result<(), VllmConnectorError> {
        let mut map = self.registered_blocks.write().unwrap();
        for (_, region) in map.drain() {
            self.rma_engine.deregister_memory_region(region.rkey)?;
        }
        Ok(())
    }

    /// Swap in (fetch) a remote block from a peer node via one‑sided RDMA.
    ///
    /// # Arguments
    /// * `peer_addr` – socket address of the remote node.
    /// * `remote_vaddr` – remote virtual address of the block.
    /// * `remote_rkey` – remote key of the block.
    /// * `local_vaddr` – local virtual address to place the data.
    /// * `size_bytes` – size of the block.
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

    /// Register a KV cache buffer region. This is a lower‑level variant that
    /// does not track block IDs.
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

    /// Asynchronously fetch a remote KV block via RDMA. This matches the
    /// original signature but uses the updated async method.
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
    use virthub_config::VirthubConfig;

    #[tokio::test]
    async fn test_vllm_connector_registration() {
        let config = VirthubConfig::default();
        let connector = VirthubVllmConnector::new(config).unwrap();

        let block_id = 42;
        let vaddr = 0x7fff_1000_0000;
        let size = 2 * 1024 * 1024;
        let gpu_id = 0;

        let meta = connector
            .register_kv_block(block_id, vaddr, size, gpu_id)
            .await
            .unwrap();

        assert_eq!(meta.block_id, block_id);
        assert_eq!(meta.vaddr, vaddr);
        assert_eq!(meta.size_bytes, size);
        assert_eq!(connector.registered_block_count().await, 1);

        connector.unregister_kv_block(block_id).await.unwrap();
        assert_eq!(connector.registered_block_count().await, 0);
    }

    #[tokio::test]
    async fn test_vllm_connector_swap_in() {
        let config = VirthubConfig::default();
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
        let config = VirthubConfig::default();
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
        let config = VirthubConfig::default();
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
    }
}
