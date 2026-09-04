// virthub/src/connectors/lmcache/src/lib.rs

//! LMCache Connector for distributed multi‑tier KV chunk storage over RDMA.
//!
//! This connector registers KV chunks with the RDMA transport and provides
//! methods to store, retrieve, and remove chunks. It is used by the
//! LMCache integration to offload chunks to remote nodes.
//!
//! ## Precision‑Scalable PSP‑KV Integration
//!
//! The connector now supports attaching a packed precision policy (from the
//! `precision` crate) and an optional PSP‑KV sidecar descriptor to each
//! stored chunk. This enables the scheduler to store precision decisions
//! and sidecar metadata alongside the chunk’s RDMA registration.

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
use store::kv_block::KvBlockKey;
use store::psp_kv::PspKvSidecarDescriptor;
use store::tier_manager::StorageTier;

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

/// Metadata returned when storing a chunk.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LmCacheChunkMeta {
    pub key: KvBlockKey,
    pub tier: StorageTier,
    pub vaddr: u64,
    pub size_bytes: usize,
    pub gpu_device_id: u32,
    pub rkey: u32,
    pub lkey: u32,
    /// Optional precision policy assigned at allocation time.
    pub precision_policy: Option<PackedBlockPolicy>,
    /// Optional PSP‑KV sidecar descriptor for quantized chunks.
    pub sidecar: Option<PspKvSidecarDescriptor>,
}

pub struct VirthubLmCacheConnector {
    config: VirthubConfig,
    rma_engine: Arc<RmaTransportEngine>,
    /// Map from chunk key to registered region metadata.
    registered_chunks: Arc<RwLock<HashMap<KvBlockKey, RegisteredRegion>>>,
    /// Map from chunk key to packed precision policy.
    chunk_policies: Arc<DashMap<KvBlockKey, PackedBlockPolicy>>,
    /// Map from chunk key to sidecar descriptor.
    chunk_sidecars: Arc<DashMap<KvBlockKey, PspKvSidecarDescriptor>>,
    /// Local store of chunk payloads (used for testing and local retrieval).
    local_store: Arc<RwLock<HashMap<KvBlockKey, Vec<u8>>>>,
}

impl VirthubLmCacheConnector {
    pub fn new(config: VirthubConfig) -> Result<Self, LmCacheConnectorError> {
        let addr_str = format!("0.0.0.0:{}", config.transport.tcp.tcp_port);
        let listen_addr: SocketAddr = addr_str
            .parse()
            .map_err(|e| LmCacheConnectorError::ConfigError(format!(
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
            registered_chunks: Arc::new(RwLock::new(HashMap::new())),
            chunk_policies: Arc::new(DashMap::new()),
            chunk_sidecars: Arc::new(DashMap::new()),
            local_store: Arc::new(RwLock::new(HashMap::new())),
        })
    }

    pub fn from_config(config: &VirthubConfig) -> Result<Self, LmCacheConnectorError> {
        Self::new(config.clone())
    }

    /// Put a chunk into the local multi-tier storage and register its memory
    /// region for RDMA. Does not attach precision metadata.
    pub async fn put_chunk(
        &self,
        key: KvBlockKey,
        tier: StorageTier,
        gpu_device_id: u32,
        payload: Vec<u8>,
        vaddr: u64,
        len: usize,
    ) -> Result<LmCacheChunkMeta, LmCacheConnectorError> {
        self.put_chunk_with_policy(
            key,
            tier,
            gpu_device_id,
            payload,
            vaddr,
            len,
            None,
            None,
        ).await
    }

    /// Put a chunk with optional precision policy and sidecar descriptor.
    pub async fn put_chunk_with_policy(
        &self,
        key: KvBlockKey,
        tier: StorageTier,
        gpu_device_id: u32,
        payload: Vec<u8>,
        vaddr: u64,
        len: usize,
        precision_policy: Option<PackedBlockPolicy>,
        sidecar: Option<PspKvSidecarDescriptor>,
    ) -> Result<LmCacheChunkMeta, LmCacheConnectorError> {
        {
            let mut store = self.local_store.write().unwrap();
            store.insert(key, payload.clone());
        }

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

        if let Some(policy) = precision_policy {
            self.chunk_policies.insert(key, policy);
        }
        if let Some(desc) = sidecar {
            self.chunk_sidecars.insert(key, desc);
        }

        Ok(LmCacheChunkMeta {
            key,
            tier,
            vaddr: region.vaddr,
            size_bytes: region.length,
            gpu_device_id,
            rkey: region.rkey,
            lkey: region.lkey,
            precision_policy,
            sidecar,
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
        {
            let mut store = self.local_store.write().unwrap();
            store.remove(key);
        }

        let mut map = self.registered_chunks.write().unwrap();
        if let Some(region) = map.remove(key) {
            self.rma_engine.deregister_memory_region(region.rkey)?;
        }
        self.chunk_policies.remove(key);
        self.chunk_sidecars.remove(key);
        Ok(())
    }

    /// Fetch a remote chunk from a peer node via one‑sided RDMA.
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
        let _ = key;
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

    /// Return the packed precision policy for a chunk, if present.
    pub fn get_chunk_policy(&self, key: &KvBlockKey) -> Option<PackedBlockPolicy> {
        self.chunk_policies.get(key).map(|p| *p)
    }

    /// Return the sidecar descriptor for a chunk, if present.
    pub fn get_chunk_sidecar(&self, key: &KvBlockKey) -> Option<PspKvSidecarDescriptor> {
        self.chunk_sidecars.get(key).map(|d| *d)
    }

    /// Register a KV cache buffer region without chunk key tracking.
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

    /// Asynchronously fetch a remote KV block via RDMA (legacy method).
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
    use precision::policy::{PackedBlockPolicy, PrecisionLevel};
    use virthub_config::VirthubConfig;

    #[tokio::test]
    async fn test_lmcache_connector_put_get_remove_with_policy() {
        let config = VirthubConfig::default();
        let connector = VirthubLmCacheConnector::new(config).unwrap();

        let key = KvBlockKey::new(1, 100);
        let payload = vec![0xABu8; 1024];
        let vaddr = 0x7fff_5000_0000;
        let size = 1024;
        let tier = StorageTier::Dram;
        let policy = PackedBlockPolicy::new(PrecisionLevel::Fp8, false, 0xFF_FFFF);
        let sidecar = PspKvSidecarDescriptor::default();

        let meta = connector
            .put_chunk_with_policy(
                key,
                tier,
                0,
                payload.clone(),
                vaddr,
                size,
                Some(policy),
                Some(sidecar),
            )
            .await
            .unwrap();

        assert_eq!(meta.key, key);
        assert_eq!(meta.size_bytes, size);
        assert_eq!(meta.precision_policy, Some(policy));
        assert_eq!(meta.sidecar, Some(sidecar));

        // Verify stored policy and sidecar
        assert_eq!(connector.get_chunk_policy(&key), Some(policy));
        assert_eq!(connector.get_chunk_sidecar(&key), Some(sidecar));

        let retrieved = connector.get_chunk(&key).await.unwrap();
        assert_eq!(retrieved, payload);

        connector.remove_chunk(&key).await.unwrap();
        assert_eq!(connector.registered_chunk_count().await, 0);
        assert_eq!(connector.get_chunk_policy(&key), None);
        assert_eq!(connector.get_chunk_sidecar(&key), None);
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
