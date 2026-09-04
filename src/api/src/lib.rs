// virthub/src/api/src/lib.rs

//! Shared API for decoupled DSM and KV-cache management.
//!
//! This crate defines the narrow interface between the generic
//! distributed shared memory (DSM) infrastructure and the upper-level
//! KV-cache management layers. It contains **only** interface types and
//! error definitions, with no dependencies on concrete DSM or KV-cache
//! implementations. This separation allows either side to be tested
//! independently using mocks or in-memory stubs.

use std::net::SocketAddr;

/// Opaque metadata attached to a DSM block.
///
/// The upper layer (KV-cache management) serializes its own policy
/// (e.g., packed precision policy, sidecar descriptor) into a byte
/// vector and stores it here. The DSM layer treats this data as
/// completely opaque and never inspects its contents.
pub type BlockMetadata = Vec<u8>;

/// A generic region identifier used by the DSM.
///
/// This type must be defined in a common types crate that both the
/// DSM and KV-cache layer can depend on. For now, we use a simple
/// u64 placeholder to avoid introducing an internal dependency.
pub type RegionId = u64;

/// Error type returned by the DSM backend.
#[derive(Debug, thiserror::Error)]
pub enum DsmError {
    #[error("region {0} already registered")]
    RegionAlreadyExists(RegionId),

    #[error("region {0} not found")]
    RegionNotFound(RegionId),

    #[error("block fetch failed for region {region_id}: {reason}")]
    FetchFailed { region_id: RegionId, reason: String },

    #[error("metadata update failed for region {region_id}: {reason}")]
    UpdateFailed { region_id: RegionId, reason: String },

    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("internal DSM error: {0}")]
    Internal(String),
}

/// Data returned from a remote block fetch.
#[derive(Debug, Clone)]
pub struct BlockData {
    /// Raw payload bytes of the block.
    pub payload: Vec<u8>,
    /// Opaque metadata associated with the block.
    pub metadata: BlockMetadata,
}

/// Core DSM operations required by KV-cache management.
///
/// This trait defines the contract between the generic DSM layer and
/// the upper-level KV-cache logic. The DSM implementation (e.g.,
/// [`KlnkDsmBackend`](super::super::klnk_dsm_api::KlnkDsmBackend))
/// stores and moves blocks without interpreting the metadata.
#[async_trait::async_trait]
pub trait DsmBackend: Send + Sync {
    /// Register a new block with the DSM, attaching opaque metadata.
    ///
    /// * `region_id` - Unique identifier for the block.
    /// * `vaddr`     - Virtual address of the block in local memory.
    /// * `size`      - Size of the block in bytes.
    /// * `metadata`  - Serialized block policy (precision, format, etc.).
    async fn register_block(
        &self,
        region_id: RegionId,
        vaddr: u64,
        size: usize,
        metadata: BlockMetadata,
    ) -> Result<(), DsmError>;

    /// Fetch a block's data and metadata from a remote peer.
    ///
    /// * `region_id` - Unique identifier for the block.
    /// * `peer_addr` - Network address of the remote node.
    /// * `local_vaddr` - Local destination virtual address for the data.
    /// * `size`      - Expected size of the block.
    async fn fetch_block(
        &self,
        region_id: RegionId,
        peer_addr: SocketAddr,
        local_vaddr: u64,
        size: usize,
    ) -> Result<BlockData, DsmError>;

    /// Update the opaque metadata of an already registered block.
    ///
    /// * `region_id` - Unique identifier for the block.
    /// * `new_metadata` - Replacement metadata bytes.
    async fn update_metadata(
        &self,
        region_id: RegionId,
        new_metadata: BlockMetadata,
    ) -> Result<(), DsmError>;
}