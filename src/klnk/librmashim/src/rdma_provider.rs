// virthub/src/klnk/librmashim/src/rdma_provider.rs

//! Software (fallback) RDMA provider.
//!
//! This module implements a pure software RDMA provider that does **not** use
//! any hardware acceleration or unsafe FFI. It is used when no RDMA‑capable
//! device is available, or when the `rdma-hardware` feature is disabled. The
//! provider manages memory region metadata and completion counters so that
//! higher layers can function without modification.
//!
//! All operations are no‑ops for actual data movement; they only update
//! bookkeeping structures. A real Verbs‑based provider can be added later
//! behind a feature flag without changing the public interface.

use crate::mr_registry::{MemoryRegion, MemoryRegionError, MemoryRegionRegistry};
use crate::RdmaEndpointConfig;
use dashmap::DashMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use thiserror::Error;
use tracing::{debug, info};

#[derive(Debug, Error)]
pub enum RdmaProviderError {
    #[error("RDMA device initialization failed: {0}")]
    InitFailed(String),

    #[error("Memory region error: {0}")]
    MrError(#[from] MemoryRegionError),

    #[error("Queue pair creation failed: {0}")]
    QpCreationFailed(String),

    #[error("Completion queue error: {0}")]
    CqError(String),

    #[error("Work request posting failed: {0}")]
    PostFailed(String),

    #[error("Peer endpoint {0} not connected")]
    PeerNotConnected(SocketAddr),

    #[error("Operation not yet implemented: {0}")]
    Unimplemented(String),
}

/// Placeholder for a Protection Domain (PD) handle.
#[derive(Debug, Clone)]
pub struct ProtectionDomain {
    pub handle: u64,
    pub device_name: String,
}

/// Placeholder for a Completion Queue (CQ) handle.
#[derive(Debug, Clone)]
pub struct CompletionQueue {
    pub handle: u64,
    pub cq_size: u32,
}

/// Placeholder for a Queue Pair (QP) handle.
#[derive(Debug, Clone)]
pub struct QueuePair {
    pub qp_num: u32,
    pub state: QpState,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QpState {
    Reset,
    Init,
    ReadyToReceive,
    ReadyToSend,
    Error,
}

/// Software (fallback) RDMA provider that manages memory regions and queue pairs
/// without hardware acceleration.
#[derive(Debug)]
pub struct RdmaProvider {
    #[allow(dead_code)]
    config: RdmaEndpointConfig,
    qps: DashMap<SocketAddr, Arc<QueuePair>>,
    mr_registry: Arc<MemoryRegionRegistry>,
    next_qp_num: AtomicU32,
    completed_wrs: AtomicU32,
}

impl RdmaProvider {
    /// Create a new software RDMA provider.
    pub fn new(config: RdmaEndpointConfig) -> Result<Self, RdmaProviderError> {
        info!("Creating software RDMA provider (no hardware acceleration)");
        Ok(Self {
            config,
            qps: DashMap::new(),
            mr_registry: Arc::new(MemoryRegionRegistry::new()),
            next_qp_num: AtomicU32::new(1),
            completed_wrs: AtomicU32::new(0),
        })
    }

    /// Register a memory region (metadata only).
    pub fn register_region(
        &self,
        vaddr: u64,
        size: usize,
        flags: u32,
    ) -> Result<MemoryRegion, RdmaProviderError> {
        // Check cache first.
        if let Some(existing) = self.mr_registry.get_by_vaddr_size(vaddr, size) {
            debug!(
                "Reusing existing memory region for vaddr=0x{:x}, size={}",
                vaddr, size
            );
            return Ok(existing);
        }

        let rkey = self.next_qp_num.fetch_add(1, Ordering::Relaxed);
        let region = MemoryRegion::new(rkey, rkey, vaddr, size, flags, 0);
        self.mr_registry.register(region.clone())?;
        debug!(
            "Registered software memory region: vaddr=0x{:x}, size={}, rkey={}",
            vaddr, size, rkey
        );
        Ok(region)
    }

    /// Deregister a memory region (metadata only).
    pub fn deregister_region(&self, rkey: u32) -> Result<(), RdmaProviderError> {
        self.mr_registry.deregister(rkey)?;
        debug!("Deregistered software memory region rkey={}", rkey);
        Ok(())
    }

    /// Create a Queue Pair for a given peer address (metadata only).
    pub fn create_qp(&self, peer_addr: SocketAddr) -> Result<Arc<QueuePair>, RdmaProviderError> {
        if let Some(qp) = self.qps.get(&peer_addr) {
            return Ok(qp.clone());
        }

        let qp_num = self.next_qp_num.fetch_add(1, Ordering::Relaxed);
        let qp = Arc::new(QueuePair {
            qp_num,
            state: QpState::Init,
        });
        self.qps.insert(peer_addr, qp.clone());
        debug!("Created software QP {} for peer {}", qp_num, peer_addr);
        Ok(qp)
    }

    /// Get or create a QP for a peer (metadata only).
    fn get_or_create_qp(&self, peer_addr: SocketAddr) -> Result<Arc<QueuePair>, RdmaProviderError> {
        if let Some(qp) = self.qps.get(&peer_addr) {
            return Ok(qp.clone());
        }
        self.create_qp(peer_addr)
    }

    /// Post an RDMA READ work request (does not move data).
    pub fn post_read(
        &self,
        peer_addr: SocketAddr,
        _remote_vaddr: u64,
        _rkey: u32,
        _local_vaddr: u64,
        _len: usize,
    ) -> Result<(), RdmaProviderError> {
        let _ = self.get_or_create_qp(peer_addr)?;
        debug!("Software RDMA READ (no-op)");
        self.completed_wrs.fetch_add(1, Ordering::Relaxed);
        Ok(())
    }

    /// Post an RDMA WRITE work request (does not move data).
    pub fn post_write(
        &self,
        peer_addr: SocketAddr,
        _remote_vaddr: u64,
        _rkey: u32,
        _local_vaddr: u64,
        _len: usize,
    ) -> Result<(), RdmaProviderError> {
        let _ = self.get_or_create_qp(peer_addr)?;
        debug!("Software RDMA WRITE (no-op)");
        self.completed_wrs.fetch_add(1, Ordering::Relaxed);
        Ok(())
    }

    /// Post an atomic Compare‑And‑Swap operation (not actually performed).
    pub fn post_atomic_cas(
        &self,
        peer_addr: SocketAddr,
        _remote_vaddr: u64,
        _rkey: u32,
        compare: u64,
        _swap: u64,
    ) -> Result<u64, RdmaProviderError> {
        let _ = self.get_or_create_qp(peer_addr)?;
        self.completed_wrs.fetch_add(1, Ordering::Relaxed);
        Ok(compare)
    }

    /// Post an atomic Fetch‑And‑Add operation (not actually performed).
    pub fn post_atomic_faa(
        &self,
        peer_addr: SocketAddr,
        _remote_vaddr: u64,
        _rkey: u32,
        add: u64,
    ) -> Result<u64, RdmaProviderError> {
        let _ = self.get_or_create_qp(peer_addr)?;
        self.completed_wrs.fetch_add(1, Ordering::Relaxed);
        Ok(add)
    }

    /// Poll the completion queue (returns number of completed operations).
    pub fn poll_cq(&self, _max_entries: usize) -> Result<usize, RdmaProviderError> {
        let count = self.completed_wrs.load(Ordering::Relaxed);
        if count > 0 {
            self.completed_wrs.store(0, Ordering::Relaxed);
            Ok(count as usize)
        } else {
            Ok(0)
        }
    }

    /// Refill the Receive Queue (no‑op in software mode).
    pub fn refill_rq(
        &self,
        buffers: &[crate::rq_refill::ReceiveBufferSlot],
    ) -> Result<usize, RdmaProviderError> {
        debug!("Refilling RQ (software) with {} buffers", buffers.len());
        Ok(buffers.len())
    }

    /// Get the memory region registry.
    pub fn registry(&self) -> &Arc<MemoryRegionRegistry> {
        &self.mr_registry
    }

    /// Enable optimizations (no‑op in software mode).
    pub fn enable_optimizations(&self) -> Result<(), RdmaProviderError> {
        debug!("Software optimizations enabled");
        Ok(())
    }
}
