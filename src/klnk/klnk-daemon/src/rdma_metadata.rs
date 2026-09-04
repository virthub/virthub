// virthub/src/klnk/klnk-daemon/src/rdma_metadata.rs

//! RDMA metadata exposure with push‑based updates and variable‑sized coherence entries.
//!
//! This module:
//! - Registers a serialised snapshot of the local control plane’s page states as an
//!   RDMA‑accessible buffer.
//! - Registers an invalidation ring buffer (retained for future eager‑push use).
//! - Provides a `push_metadata_to_peers` function that writes the latest snapshot
//!   directly into every remote node’s metadata region, avoiding polling overhead.
//! - Supports **variable‑sized coherence units** via the existing `page_size` field.

use crate::invalidation::InvalidationBuffer;
use klnk_core::control_plane::ControlPlaneManager;
use klnk_core::domain::{DistributedPageEntry, NodeId, PageCoherenceState};
use librmashim::{RmaTransportEngine, RegisteredRegion};
use serde::{Deserialize, Serialize};
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, RwLock};
use thiserror::Error;
use tracing::{debug, error, info, warn};
use tokio::task;

#[derive(Debug, Error)]
pub enum RdmaMetadataError {
    #[error("Failed to register metadata with RDMA: {0}")]
    RegistrationFailed(String),

    #[error("Failed to serialize metadata: {0}")]
    SerializationFailed(#[from] bincode::Error),

    #[error("Push to peer {0} failed: {1}")]
    #[allow(dead_code)]
    PushFailed(NodeId, String),
}

/// A compact, serializable snapshot of the entire page state table.
/// Each entry corresponds to a **coherence unit** which can be any size
/// (e.g., a 2 MB huge page, a 4 KB sub‑page, or a KV‑cache block of arbitrary size).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct MetadataSnapshot {
    pub version: u64,
    pub entries: Vec<SerializedPageEntry>,
}

/// Serialized version of a single coherence unit.
///
/// The `page_size` field specifies the exact size of this unit (not necessarily
/// a fixed 2 MB).  This allows variable‑sized coherence domains as described in
/// the KLNK paper.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct SerializedPageEntry {
    pub page_vaddr: u64,
    pub page_size: u32,
    pub coherence_state: u8,
    pub primary_owner: u64,
    pub replica_holders_count: u32,
    /// Optional version of the block (incremented on writes).
    pub version: u64,
}

impl From<&DistributedPageEntry> for SerializedPageEntry {
    fn from(entry: &DistributedPageEntry) -> Self {
        Self {
            page_vaddr: entry.page_vaddr,
            page_size: entry.page_size as u32,
            coherence_state: match entry.coherence_state {
                PageCoherenceState::Invalid => 0,
                PageCoherenceState::SharedRead => 1,
                PageCoherenceState::ExclusiveWrite => 2,
                PageCoherenceState::InFlight => 3,
            },
            primary_owner: entry.primary_owner.0,
            replica_holders_count: entry.replica_holders.len() as u32,
            version: entry.version,
        }
    }
}

/// Manages the RDMA‑exposed metadata and invalidation buffers, and provides
/// functions to push the latest metadata snapshot to remote peers.
pub struct RdmaMetadataPublisher {
    control_plane: Arc<ControlPlaneManager>,
    rma_engine: Arc<RmaTransportEngine>,
    /// The registered RDMA region for the metadata snapshot.
    metadata_region: Option<RegisteredRegion>,
    /// The registered RDMA region for the invalidation ring buffer.
    invalidation_region: Option<RegisteredRegion>,
    /// Current version of the metadata (incremented on each refresh).
    version: AtomicU64,
    /// Serialized metadata buffer (pinned memory for RDMA).
    buffer: Arc<RwLock<Vec<u8>>>,
    /// Invalidation buffer (ring buffer).
    invalidation_buffer: Arc<InvalidationBuffer>,
}

impl RdmaMetadataPublisher {
    /// Creates a new metadata publisher.
    pub fn new(
        control_plane: Arc<ControlPlaneManager>,
        rma_engine: Arc<RmaTransportEngine>,
    ) -> Self {
        Self {
            control_plane,
            rma_engine,
            metadata_region: None,
            invalidation_region: None,
            version: AtomicU64::new(0),
            buffer: Arc::new(RwLock::new(Vec::new())),
            invalidation_buffer: Arc::new(InvalidationBuffer::new()),
        }
    }

    /// Registers the metadata snapshot with the RDMA transport.
    /// The buffer is placed in a memory region that remote peers can read.
    pub fn register_metadata(&mut self) -> Result<(), RdmaMetadataError> {
        let snapshot = self.build_snapshot();
        let serialized = bincode::serialize(&snapshot)?;
        let mut buf = self.buffer.write().unwrap();
        *buf = serialized;
        let vaddr = buf.as_ptr() as u64;
        let len = buf.len();

        let region = self
            .rma_engine
            .register_memory_region(vaddr, len, None)
            .map_err(|e| RdmaMetadataError::RegistrationFailed(e.to_string()))?;

        self.metadata_region = Some(region);
        info!(
            "Metadata region registered: vaddr=0x{:x}, rkey={}, len={}",
            region.vaddr, region.rkey, region.length
        );
        Ok(())
    }

    /// Registers the invalidation buffer with the RDMA transport.
    pub fn register_invalidation_buffer(&mut self) -> Result<(), RdmaMetadataError> {
        let vaddr = self.invalidation_buffer.vaddr();
        let len = self.invalidation_buffer.size();

        let region = self
            .rma_engine
            .register_memory_region(vaddr, len, None)
            .map_err(|e| RdmaMetadataError::RegistrationFailed(e.to_string()))?;

        self.invalidation_region = Some(region);
        info!(
            "Invalidation buffer registered: vaddr=0x{:x}, rkey={}, len={}",
            region.vaddr, region.rkey, region.length
        );
        Ok(())
    }

    /// Builds a metadata snapshot from the current control plane state.
    fn build_snapshot(&self) -> MetadataSnapshot {
        let entries: Vec<SerializedPageEntry> = self
            .control_plane
            .get_all_page_states()
            .iter()
            .map(|entry| SerializedPageEntry::from(entry.as_ref()))
            .collect();
        MetadataSnapshot {
            version: self.version.load(Ordering::Relaxed),
            entries,
        }
    }

    /// Returns the registered metadata region info, if available.
    pub fn get_metadata_region(&self) -> Option<RegisteredRegion> {
        self.metadata_region
    }

    /// Returns the registered invalidation buffer region info, if available.
    #[allow(dead_code)]
    pub fn get_invalidation_region(&self) -> Option<RegisteredRegion> {
        self.invalidation_region
    }

    /// Returns the current metadata version.
    pub fn metadata_version(&self) -> u64 {
        self.version.load(Ordering::Relaxed)
    }

    /// Returns a reference to the invalidation buffer.
    pub fn get_invalidation_buffer(&self) -> Arc<InvalidationBuffer> {
        self.invalidation_buffer.clone()
    }

    /// Refreshes the local metadata buffer (serialises the latest snapshot)
    /// and updates the version.
    pub fn refresh_metadata(&self) -> Result<(), RdmaMetadataError> {
        let snapshot = self.build_snapshot();
        let serialized = bincode::serialize(&snapshot)?;
        let mut buf = self.buffer.write().unwrap();
        *buf = serialized;
        self.version.fetch_add(1, Ordering::Relaxed);
        Ok(())
    }

    /// Pushes the current metadata snapshot to all known remote nodes.
    pub async fn push_metadata_to_peers(&self) {
        if self.metadata_region.is_none() {
            warn!("No local metadata region registered; cannot push to peers");
            return;
        }

        if let Err(e) = self.refresh_metadata() {
            error!("Failed to refresh metadata before push: {}", e);
            return;
        }

        let (local_vaddr, len) = {
            let buf = self.buffer.read().unwrap();
            (buf.as_ptr() as u64, buf.len())
        };

        let remote_nodes: Vec<(NodeId, SocketAddr)> = self
            .control_plane
            .get_all_remote_nodes()
            .into_iter()
            .map(|info| (info.node_id, info.socket_addr))
            .collect();

        for (node_id, peer_addr) in remote_nodes {
            let remote_info = match self.control_plane.get_remote_node(node_id) {
                Some(info) => info,
                None => {
                    debug!("No remote info for node {:?}, skipping push", node_id);
                    continue;
                }
            };

            let remote_meta_vaddr = remote_info.metadata_vaddr;
            let remote_meta_rkey = remote_info.metadata_rkey;
            let remote_meta_len = remote_info.metadata_len;

            if remote_meta_vaddr == 0 || remote_meta_len == 0 {
                debug!("Remote node {:?} has no metadata buffer registered", node_id);
                continue;
            }

            if len > remote_meta_len {
                warn!(
                    "Local metadata size {} exceeds remote buffer size {} for node {:?}; skipping push",
                    len, remote_meta_len, node_id
                );
                continue;
            }

            if let Err(e) = self
                .rma_engine
                .rdma_write(peer_addr, remote_meta_vaddr, remote_meta_rkey, local_vaddr, len)
                .await
            {
                error!("Failed to push metadata to node {:?}: {}", node_id, e);
                continue;
            }

            if let Some(inval_info) = self.control_plane.get_remote_node(node_id) {
                if inval_info.invalidation_vaddr != 0 {
                    let immediate_data = self.metadata_version() as u32;
                    if let Err(e) = self
                        .rma_engine
                        .rdma_write_immediate(
                            peer_addr,
                            inval_info.invalidation_vaddr,
                            inval_info.invalidation_rkey,
                            0,
                            0,
                            immediate_data,
                        )
                        .await
                    {
                        debug!(
                            "Failed to send metadata notification to node {:?}: {}",
                            node_id, e
                        );
                    }
                }
            }
        }
    }
}

/// Initialises the metadata publisher, registers buffers, and starts a background
/// task that periodically (or on demand) pushes metadata to peers.
pub fn init_metadata_publisher(
    control_plane: Arc<ControlPlaneManager>,
    rma_engine: Arc<RmaTransportEngine>,
) -> Result<RdmaMetadataPublisher, RdmaMetadataError> {
    let mut publisher = RdmaMetadataPublisher::new(control_plane, rma_engine);
    publisher.register_metadata()?;
    publisher.register_invalidation_buffer()?;
    Ok(publisher)
}

/// Spawns a background task that pushes metadata to peers every `interval_ms` milliseconds.
pub fn start_periodic_push(
    publisher: Arc<RdmaMetadataPublisher>,
    interval_ms: u64,
) -> task::JoinHandle<()> {
    task::spawn(async move {
        let mut interval = tokio::time::interval(std::time::Duration::from_millis(interval_ms));
        loop {
            interval.tick().await;
            publisher.push_metadata_to_peers().await;
        }
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use klnk_core::control_plane::ControlPlaneManager;
    use klnk_core::domain::NodeId;
    use librmashim::{RdmaEndpointConfig, RmaTransportEngine};
    use std::net::SocketAddr;

    #[test]
    fn test_metadata_publisher_creation() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let publisher = init_metadata_publisher(cp, rma);
        assert!(publisher.is_ok());
        let pub_inst = publisher.unwrap();
        assert!(pub_inst.get_metadata_region().is_some());
        assert!(pub_inst.get_invalidation_region().is_some());
        assert_eq!(pub_inst.metadata_version(), 0);
    }

    #[test]
    fn test_variable_sized_entries() {
        let entries = vec![
            SerializedPageEntry {
                page_vaddr: 0x1000,
                page_size: 4096,
                coherence_state: 1,
                primary_owner: 1,
                replica_holders_count: 0,
                version: 0,
            },
            SerializedPageEntry {
                page_vaddr: 0x200000,
                page_size: 2 * 1024 * 1024,
                coherence_state: 2,
                primary_owner: 2,
                replica_holders_count: 1,
                version: 1,
            },
        ];
        let snapshot = MetadataSnapshot {
            version: 1,
            entries,
        };
        let serialized = bincode::serialize(&snapshot).unwrap();
        let deserialized: MetadataSnapshot = bincode::deserialize(&serialized).unwrap();
        assert_eq!(deserialized.entries.len(), 2);
        assert_eq!(deserialized.entries[0].page_size, 4096);
        assert_eq!(deserialized.entries[1].page_size, 2 * 1024 * 1024);
    }
}
