// virthub/src/klnk/klnk-dsm-api/src/lib.rs

//! Adapter crate that implements the generic `DsmBackend` trait from
//! `virthub-api` using the concrete KLNK DSM infrastructure.
//!
//! This crate bridges the upper‑level KV‑cache management (which depends only
//! on `virthub-api`) and the internal `klnk-core` control plane / `librmashim`
//! transport. It converts generic `RegionId` values into the actual
//! `GlobalRegionId` used by `klnk-core` and exposes block registration,
//! metadata updates, and remote block fetch operations.
//!
//! **Note on remote fetch:** The current `DsmBackend::fetch_block` signature
//! does not include the remote virtual address or rkey. The adapter therefore
//! maintains an internal registry of remote blocks (populated by the
//! metadata publishing subsystem in a full implementation). For now, remote
//! fetch returns an error if the block is not found in that registry.

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::Arc;

use async_trait::async_trait;
use dashmap::DashMap;
use thiserror::Error;

use klnk_core::control_plane::ControlPlaneManager;
use klnk_core::domain::{GlobalRegionId, MemoryRegionDescriptor, NodeId};
use librmashim::RmaTransportEngine;
use virthub_api::{BlockData, BlockMetadata, DsmBackend, DsmError, RegionId};

/// Converts a generic `RegionId` (u64) to a concrete `GlobalRegionId`.
///
/// The encoding packs `owner_pid` into the high 32 bits and `shmid` into the
/// low 32 bits. This convention is shared between the upper layer and this
/// adapter; the upper layer must use the same packing when constructing
/// `RegionId`s.
fn region_id_to_global(region_id: RegionId) -> GlobalRegionId {
    let owner_pid = (region_id >> 32) as u32;
    let shmid = (region_id & 0xFFFF_FFFF) as i32;
    GlobalRegionId { owner_pid, shmid }
}

/// Internal information about a remote block needed for RDMA fetch.
struct RemoteBlockInfo {
    peer_addr: SocketAddr,
    remote_vaddr: u64,
    rkey: u32,
    size: usize,
    metadata: BlockMetadata,
}

/// Adapter struct that implements `DsmBackend` for KLNK.
pub struct KlnkDsmBackend {
    control_plane: Arc<ControlPlaneManager>,
    rma_engine: Arc<RmaTransportEngine>,
    /// Registry of locally registered blocks: maps RegionId -> (vaddr, size).
    local_blocks: DashMap<RegionId, (u64, usize)>,
    /// Registry of remote block information (populated externally).
    remote_blocks: DashMap<RegionId, RemoteBlockInfo>,
}

impl KlnkDsmBackend {
    /// Creates a new adapter instance.
    ///
    /// * `control_plane` – the KLNK control plane manager (shared).
    /// * `rma_engine`    – the RMA transport engine (shared).
    pub fn new(
        control_plane: Arc<ControlPlaneManager>,
        rma_engine: Arc<RmaTransportEngine>,
    ) -> Self {
        Self {
            control_plane,
            rma_engine,
            local_blocks: DashMap::new(),
            remote_blocks: DashMap::new(),
        }
    }

    /// (Test/Debug only) Inserts remote block information into the adapter.
    ///
    /// In a production system, this method is replaced by logic that consumes
    /// metadata pushed from remote peers and populates the registry
    /// automatically. It is provided here to enable testing of `fetch_block`.
    pub fn register_remote_block_info(
        &self,
        region_id: RegionId,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        rkey: u32,
        size: usize,
        metadata: BlockMetadata,
    ) {
        self.remote_blocks.insert(
            region_id,
            RemoteBlockInfo {
                peer_addr,
                remote_vaddr,
                rkey,
                size,
                metadata,
            },
        );
    }
}

#[async_trait]
impl DsmBackend for KlnkDsmBackend {
    async fn register_block(
        &self,
        region_id: RegionId,
        vaddr: u64,
        size: usize,
        metadata: BlockMetadata,
    ) -> Result<(), DsmError> {
        let global_id = region_id_to_global(region_id);

        // Construct a minimal memory region descriptor.
        // The staging parameters are irrelevant for upper‑layer block registration;
        // they are only used by the control plane to create page entries.
        let descriptor = MemoryRegionDescriptor {
            region_id: global_id,
            main_vaddr: vaddr,
            region_size: size,
            staging_vaddr: 0,
            staging_num_pages: 1,
            staging_page_size: size.max(4096),
            prot_flags: klnk_core::domain::MemoryProtectionFlags(
                klnk_core::domain::MemoryProtectionFlags::READ
                    | klnk_core::domain::MemoryProtectionFlags::WRITE,
            ),
            mem_flags: 0,
            version: 0,
        };

        self.control_plane
            .register_region_with_metadata(descriptor, metadata.clone())
            .map_err(|e| DsmError::Internal(e.to_string()))?;

        // Remember the local vaddr for potential local fetch.
        self.local_blocks.insert(region_id, (vaddr, size));
        Ok(())
    }

    async fn fetch_block(
        &self,
        region_id: RegionId,
        peer_addr: SocketAddr,
        local_vaddr: u64,
        size: usize,
    ) -> Result<BlockData, DsmError> {
        // First check if the block is locally registered – then we can simply
        // copy from its local virtual address (assuming the caller provided
        // a valid `local_vaddr` where data should be placed? Actually the
        // trait says `local_vaddr` is the destination for incoming data.
        // For local fetch, we can copy from the registered vaddr to `local_vaddr`.
        if let Some((src_vaddr, src_size)) = self.local_blocks.get(&region_id) {
            if *src_size != size {
                return Err(DsmError::Internal(format!(
                    "size mismatch: expected {}, got {}",
                    size, *src_size
                )));
            }

            // Unsafe copy from src_vaddr to local_vaddr.
            // In a production implementation, this would be a typed copy or
            // handled by the DSM fault mechanism; here we provide a placeholder.
            unsafe {
                std::ptr::copy_nonoverlapping(
                    *src_vaddr as *const u8,
                    local_vaddr as *mut u8,
                    size,
                );
            }

            // Retrieve the metadata stored for this block from the control plane.
            let global_id = region_id_to_global(region_id);
            let metadata = self
                .control_plane
                .get_page_metadata(*src_vaddr)
                .unwrap_or_default();

            // Read back the payload from `local_vaddr` to return it.
            let mut payload = vec![0u8; size];
            unsafe {
                std::ptr::copy_nonoverlapping(
                    local_vaddr as *const u8,
                    payload.as_mut_ptr(),
                    size,
                );
            }

            return Ok(BlockData { payload, metadata });
        }

        // If not local, check the remote registry.
        let remote_info = self
            .remote_blocks
            .get(&region_id)
            .ok_or_else(|| DsmError::FetchFailed {
                region_id,
                reason: "remote block info not registered".to_string(),
            })?;

        if remote_info.size != size {
            return Err(DsmError::FetchFailed {
                region_id,
                reason: format!(
                    "size mismatch: expected {}, got {}",
                    size, remote_info.size
                ),
            });
        }

        // Perform RDMA read into a buffer, then copy to local_vaddr if needed.
        // For simplicity, we read into the provided `local_vaddr` and then
        // copy into Vec. This double‑copies but keeps the API clean.
        self.rma_engine
            .rdma_read(
                remote_info.peer_addr,
                remote_info.remote_vaddr,
                remote_info.rkey,
                local_vaddr,
                size,
            )
            .await
            .map_err(|e| DsmError::FetchFailed {
                region_id,
                reason: e.to_string(),
            })?;

        // Copy from local_vaddr into Vec to return.
        let mut payload = vec![0u8; size];
        unsafe {
            std::ptr::copy_nonoverlapping(local_vaddr as *const u8, payload.as_mut_ptr(), size);
        }

        Ok(BlockData {
            payload,
            metadata: remote_info.metadata.clone(),
        })
    }

    async fn update_metadata(
        &self,
        region_id: RegionId,
        new_metadata: BlockMetadata,
    ) -> Result<(), DsmError> {
        // Look up the local block's vaddr from the registry.
        let (vaddr, _) = self
            .local_blocks
            .get(&region_id)
            .ok_or_else(|| DsmError::RegionNotFound(region_id))?;

        self.control_plane
            .set_page_metadata(*vaddr, new_metadata);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use klnk_core::domain::NodeId;
    use librmashim::{RdmaEndpointConfig, RmaTransportEngine};
    use std::net::SocketAddr;

    fn create_test_backend() -> (Arc<KlnkDsmBackend>, Arc<ControlPlaneManager>) {
        let node_id = NodeId(1);
        let cp = ControlPlaneManager::new(node_id);
        // Create a dummy RMA engine using auto‑detect (will fall back to TCP).
        let addr: SocketAddr = "127.0.0.1:0".parse().unwrap();
        let config = RdmaEndpointConfig::default();
        let rma = RmaTransportEngine::auto_detect(config, addr).unwrap();
        let backend = KlnkDsmBackend::new(cp.clone(), rma);
        (Arc::new(backend), cp)
    }

    #[tokio::test]
    async fn test_register_and_update_metadata() {
        let (backend, _cp) = create_test_backend();

        // Use a region ID that encodes owner_pid=123, shmid=456.
        let region_id: RegionId = (123u64 << 32) | 456u64;
        let vaddr = 0x7fff_0000_0000u64;
        let size = 4096usize;
        let metadata = vec![1, 2, 3, 4];

        backend
            .register_block(region_id, vaddr, size, metadata.clone())
            .await
            .expect("registration should succeed");

        // Verify metadata via control plane (or via adapter's internal map).
        let global_id = region_id_to_global(region_id);
        let stored_meta = backend.control_plane.get_page_metadata(vaddr);
        assert_eq!(stored_meta, Some(metadata));

        // Update metadata.
        let new_meta = vec![9, 8, 7];
        backend
            .update_metadata(region_id, new_meta.clone())
            .await
            .expect("update should succeed");
        let updated_meta = backend.control_plane.get_page_metadata(vaddr);
        assert_eq!(updated_meta, Some(new_meta));
    }

    #[tokio::test]
    async fn test_local_fetch_block() {
        let (backend, _cp) = create_test_backend();

        let region_id: RegionId = (100u64 << 32) | 200u64;
        let src_vaddr = 0x7fff_1000_0000u64;
        let size = 1024usize;
        let payload = vec![0xAB; size];
        let metadata = vec![5, 6, 7, 8];

        // Allocate a local buffer to act as the registered block memory.
        // In a real test we'd allocate properly; here we use a Vec and leak it.
        let mut local_storage = Box::new([0u8; 1024]);
        local_storage.copy_from_slice(&payload);
        let storage_ptr = Box::into_raw(local_storage) as u64;

        backend
            .register_block(region_id, storage_ptr, size, metadata.clone())
            .await
            .unwrap();

        // Destination buffer for fetch.
        let mut dest = vec![0u8; size];
        let dest_ptr = dest.as_mut_ptr() as u64;

        let fetched = backend
            .fetch_block(region_id, "127.0.0.1:0".parse().unwrap(), dest_ptr, size)
            .await
            .expect("fetch should succeed");

        assert_eq!(fetched.payload, payload);
        assert_eq!(fetched.metadata, metadata);

        // Clean up the leaked storage.
        unsafe {
            drop(Box::from_raw(storage_ptr as *mut u8));
        }
    }
}
