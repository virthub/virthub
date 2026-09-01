// virthub/src/connectors/lmcache/src/lib.rs

use librmashim::{MemoryRegionHandle, RdmaEndpointConfig, RegisteredRegion, RmaEngineError, RmaTransportEngine};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::{Arc, RwLock};
use thiserror::Error;
use virthub_config::VirthubConfig;
use store::kv_block::KvBlockKey;
use store::tier_manager::StorageTier;
use bincode;


#[derive(Debug, Error)]
pub enum LmCacheConnectorError {
    #[error("Configuration error: {0}")]
    ConfigError(String),

    #[error("RMA engine failure: {0}")]
    RmaEngineFailed(#[from] RmaEngineError),

    #[error("Transport communication failed: {0}")]
    TransportFailed(String),

    #[error("Chunk {0} not found in local registry")]
    ChunkNotFound(KvBlockKey),

    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("Serialization error: {0}")]
    SerializationError(#[from] bincode::Error),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LmCacheChunkMeta {
    pub key: KvBlockKey,
    pub tier: StorageTier,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
}

pub struct VirthubLmCacheConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    registered_chunks: Arc<RwLock<HashMap<KvBlockKey, RegisteredRegion>>>,
    local_store: Arc<RwLock<HashMap<KvBlockKey, Vec<u8>>>>,
}

impl VirthubLmCacheConnector {
    pub fn new(config: VirthubConfig) -> Result<Self, LmCacheConnectorError> {
        let addr_str = format!("0.0.0.0:{}", config.transport.tcp.tcp_port);
        let listen_addr: SocketAddr = addr_str
            .parse()
            .map_err(|e| LmCacheConnectorError::ConfigError(format!("Invalid socket address '{addr_str}': {e}")))?;

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
            registered_chunks: Arc::new(RwLock::new(HashMap::new())),
            local_store: Arc::new(RwLock::new(HashMap::new())),
        })
    }

    pub fn from_config(config: &VirthubConfig) -> Result<Self, LmCacheConnectorError> {
        Self::new(config.clone())
    }

    /// Put a chunk into the local multi-tier storage and register its memory region for RDMA.
    ///
    /// # Arguments
    /// * `key` - Unique KvBlockKey.
    /// * `tier` - Storage tier (DRAM, SSD, VRAM) – currently only DRAM is supported for RDMA.
    /// * `gpu_device_id` - GPU device ID if using GPUDirect RDMA, otherwise 0.
    /// * `payload` - The chunk data.
    /// * `vaddr` - Virtual address where the data is placed (caller must allocate memory).
    /// * `len` - Length of payload (must match size of vaddr region).
    pub async fn put_chunk(
        &self,
        key: KvBlockKey,
        tier: StorageTier,
        gpu_device_id: u32,
        payload: Vec<u8>,
        vaddr: u64,
        len: usize,
    ) -> Result<LmCacheChunkMeta, LmCacheConnectorError> {
        // For simplicity, we assume the caller has already placed payload at vaddr.
        // In a real implementation, we would copy payload to vaddr.
        // We'll store a copy in local_store.
        {
            let mut store = self.local_store.write().unwrap();
            store.insert(key, payload.clone());
        }

        // Register the memory region for RDMA access.
        let gpu_opt = if self.config.transport.rdma.enable_gdr {
            Some(gpu_device_id)
        } else {
            None
        };

        let region = self
            .rma_engine
            .register_memory_region(vaddr, len, gpu_opt)?;

        let mut map = self.registered_chunks.write().unwrap();
        map.insert(key, region);

        Ok(LmCacheChunkMeta {
            key,
            tier,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
        })
    }

    /// Get a chunk from local storage (tier manager).
    pub async fn get_chunk(&self, key: &KvBlockKey) -> Result<Vec<u8>, LmCacheConnectorError> {
        let store = self.local_store.read().unwrap();
        store
            .get(key)
            .cloned()
            .ok_or(LmCacheConnectorError::ChunkNotFound(*key))
    }

    /// Remove a chunk from local storage and deregister its RDMA region.
    pub async fn remove_chunk(&self, key: &KvBlockKey) -> Result<(), LmCacheConnectorError> {
        // Remove from local store
        {
            let mut store = self.local_store.write().unwrap();
            store.remove(key);
        }

        // Deregister RDMA region
        let mut map = self.registered_chunks.write().unwrap();
        if let Some(region) = map.remove(key) {
            self.rma_engine.deregister_memory_region(region.rkey)?;
        }
        Ok(())
    }

    /// Fetch a remote chunk from a peer node via one‑sided RDMA.
    ///
    /// # Arguments
    /// * `key` - The chunk key (for tracking purposes).
    /// * `peer_addr` - Socket address of the remote node.
    /// * `remote_vaddr` - Remote virtual address of the chunk data.
    /// * `remote_rkey` - Remote key of the chunk.
    /// * `local_vaddr` - Local virtual address to place the data.
    /// * `size_bytes` - Size of the chunk.
    pub async fn fetch_remote_chunk(
        &self,
        key: KvBlockKey,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        remote_rkey: u32,
        local_vaddr: u64,
        size_bytes: usize,
    ) -> Result<(), LmCacheConnectorError> {
        self.rma_engine
            .rdma_read(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
            .await?;

        // Optionally, we could store the fetched data in local_store if needed.
        // For now, we assume the caller handles the data at local_vaddr.
        // To silence the unused variable warning, we use `key` intentionally.
        let _ = key; // mark as used
        Ok(())
    }

    /// Get the number of locally registered chunks.
    pub async fn registered_chunk_count(&self) -> usize {
        self.registered_chunks.read().unwrap().len()
    }

    /// Return the local node ID (from configuration).
    pub fn local_node_id(&self) -> u64 {
        self.config.parsed_node_id()
    }

    /// Register a KV cache buffer region. This is a lower‑level variant that does not track chunk keys.
    pub fn register_kv_cache(
        &self,
        vaddr: u64,
        size_bytes: usize,
        gpu_device_id: u32,
    ) -> Result<MemoryRegionHandle, LmCacheConnectorError> {
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
    ) -> Result<(), LmCacheConnectorError> {
        self.rma_engine
            .rdma_read(peer_addr, remote_vaddr, remote_rkey, local_vaddr, size_bytes)
            .await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use virthub_config::VirthubConfig;

    #[tokio::test]
    async fn test_lmcache_connector_put_get_remove() {
        let config = VirthubConfig::default();
        let connector = VirthubLmCacheConnector::new(config).unwrap();

        let key = KvBlockKey::new(1, 100);
        let payload = vec![0xABu8; 1024];
        let vaddr = 0x7fff_5000_0000;
        let size = 1024;
        let tier = StorageTier::Dram;

        let meta = connector
            .put_chunk(key, tier, 0, payload.clone(), vaddr, size)
            .await
            .unwrap();

        assert_eq!(meta.key, key);
        assert_eq!(meta.size_bytes, size);
        assert_eq!(connector.registered_chunk_count().await, 1);

        let retrieved = connector.get_chunk(&key).await.unwrap();
        assert_eq!(retrieved, payload);

        connector.remove_chunk(&key).await.unwrap();
        assert_eq!(connector.registered_chunk_count().await, 0);
        assert!(connector.get_chunk(&key).await.is_err());
    }

    #[tokio::test]
    async fn test_lmcache_connector_fetch_remote() {
        let config = VirthubConfig::default();
        let connector = VirthubLmCacheConnector::new(config).unwrap();

        let key = KvBlockKey::new(2, 200);
        let peer: SocketAddr = "192.168.1.10:10000".parse().unwrap();
        let remote_vaddr = 0x7fff_6000_0000;
        let remote_rkey = 5678;
        let local_vaddr = 0x7fff_7000_0000;
        let size = 2048;

        let result = connector
            .fetch_remote_chunk(key, peer, remote_vaddr, remote_rkey, local_vaddr, size)
            .await;
        assert!(result.is_ok());
    }
}
