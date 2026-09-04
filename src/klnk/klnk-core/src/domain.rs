// virthub/src/klnk/klnk-core/src/domain.rs

//! Core domain types for the KLNK distributed shared memory system.
//!
//! This module defines the fundamental data structures used across the system:
//! node identification, memory region descriptors, coherence states,
//! distributed page entries (including opaque metadata), and remote endpoint
//! information. All types are generic and do not depend on KV‑cache specifics,
//! preserving the decoupling boundary.

use serde::{Deserialize, Serialize};
use std::fmt;
use std::net::SocketAddr;
use thiserror::Error;

/// Unique identifier for a node within the Virthub / KLNK cluster mesh.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct NodeId(pub u64);

impl fmt::Display for NodeId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Node({})", self.0)
    }
}

/// Global identifier for a registered shared memory region across the cluster.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize, Deserialize)]
pub struct GlobalRegionId {
    /// PID or process identifier on origin node.
    pub owner_pid: u32,
    /// System V shmid (-1 if created via POSIX mmap).
    pub shmid: i32,
}

impl fmt::Display for GlobalRegionId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Region(PID:{}, SHMID:{})", self.owner_pid, self.shmid)
    }
}

/// Memory protection flags mirroring Linux `sys/mman.h` values.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct MemoryProtectionFlags(pub u32);

impl MemoryProtectionFlags {
    pub const READ: u32 = libc::PROT_READ as u32;
    pub const WRITE: u32 = libc::PROT_WRITE as u32;
    pub const EXEC: u32 = libc::PROT_EXEC as u32;
    pub const NONE: u32 = libc::PROT_NONE as u32;

    pub fn can_read(&self) -> bool {
        (self.0 & Self::READ) != 0
    }

    pub fn can_write(&self) -> bool {
        (self.0 & Self::WRITE) != 0
    }
}

impl std::ops::BitOr for MemoryProtectionFlags {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self {
        Self(self.0 | rhs.0)
    }
}

/// Distributed coherence state for an individual coherence unit.
/// The unit size is defined by `page_size` in `DistributedPageEntry`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum PageCoherenceState {
    /// Unit is unmapped and absent locally (triggers UFFD missing page fault).
    Invalid,
    /// Shared Read-Only copy present locally.
    SharedRead,
    /// Exclusive Read-Write ownership held locally.
    ExclusiveWrite,
    /// Unit is currently being fetched over RDMA / Socket transport.
    InFlight,
}

/// Metadata representing a registered virtual memory region within a process space.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryRegionDescriptor {
    pub region_id: GlobalRegionId,
    pub main_vaddr: u64,
    pub region_size: usize,
    pub staging_vaddr: u64,
    pub staging_num_pages: usize,
    /// The size of each coherence unit within the region.
    pub staging_page_size: usize,
    pub prot_flags: MemoryProtectionFlags,
    pub mem_flags: u32,
    /// Optional version number to track changes to this region.
    #[serde(default)]
    pub version: u64,
}

impl MemoryRegionDescriptor {
    /// Validates if a virtual address range falls completely within this region's boundary.
    pub fn contains_range(&self, vaddr: u64, len: usize) -> bool {
        vaddr >= self.main_vaddr
            && (vaddr + len as u64) <= (self.main_vaddr + self.region_size as u64)
    }

    /// Computes offset relative to the base virtual address.
    pub fn offset_of(&self, vaddr: u64) -> Option<usize> {
        if vaddr >= self.main_vaddr && vaddr < self.main_vaddr + self.region_size as u64 {
            Some((vaddr - self.main_vaddr) as usize)
        } else {
            None
        }
    }
}

/// Metadata tracking distributed page state across cluster nodes.
///
/// This struct is generic with respect to the upper‑layer payload policy.
/// The `metadata` field is an opaque byte vector that the DSM layer stores
/// and replicates without interpretation. KV‑cache managers can serialize
/// their precision policy or sidecar descriptor into this field.
#[derive(Debug, Clone)]
pub struct DistributedPageEntry {
    pub page_vaddr: u64,
    pub page_size: usize,
    pub coherence_state: PageCoherenceState,
    pub primary_owner: NodeId,
    /// List of nodes that hold a SharedRead copy of this page.
    pub replica_holders: Vec<NodeId>,
    /// Version number incremented on every exclusive write.
    pub version: u64,
    /// Opaque metadata owned by the upper layer; DSM does not inspect.
    pub metadata: Vec<u8>,
}

impl DistributedPageEntry {
    /// Creates a new entry with version 0, empty reader list, and empty metadata.
    pub fn new(page_vaddr: u64, page_size: usize, owner: NodeId) -> Self {
        Self {
            page_vaddr,
            page_size,
            coherence_state: PageCoherenceState::Invalid,
            primary_owner: owner,
            replica_holders: Vec::new(),
            version: 0,
            metadata: Vec::new(),
        }
    }

    /// Creates a new entry with the given metadata.
    pub fn new_with_metadata(
        page_vaddr: u64,
        page_size: usize,
        owner: NodeId,
        metadata: Vec<u8>,
    ) -> Self {
        Self {
            page_vaddr,
            page_size,
            coherence_state: PageCoherenceState::Invalid,
            primary_owner: owner,
            replica_holders: Vec::new(),
            version: 0,
            metadata,
        }
    }
}

/// Metadata describing a remote node's RDMA endpoint and its exported metadata region
/// plus a dedicated invalidation buffer.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RemoteEndpointInfo {
    pub node_id: NodeId,
    pub socket_addr: SocketAddr,

    /// RDMA information for the node's exported metadata region.
    pub metadata_vaddr: u64,
    pub metadata_rkey: u32,
    pub metadata_len: usize,
    /// Current version of the metadata snapshot.
    pub metadata_version: u64,

    /// RDMA information for the node's invalidation ring buffer.
    pub invalidation_vaddr: u64,
    pub invalidation_rkey: u32,
    pub invalidation_len: usize,

    /// Optional NUMA node ID for affinity.
    pub numa_node: u32,
    /// Optional flag indicating if the node supports GPUDirect RDMA.
    pub supports_gdr: bool,
}

impl RemoteEndpointInfo {
    /// Create a new endpoint info with the given node and address.
    pub fn new(node_id: NodeId, socket_addr: SocketAddr) -> Self {
        Self {
            node_id,
            socket_addr,
            metadata_vaddr: 0,
            metadata_rkey: 0,
            metadata_len: 0,
            metadata_version: 0,
            invalidation_vaddr: 0,
            invalidation_rkey: 0,
            invalidation_len: 0,
            numa_node: 0,
            supports_gdr: false,
        }
    }

    /// Update the metadata region information.
    pub fn set_metadata_region(&mut self, vaddr: u64, rkey: u32, len: usize, version: u64) {
        self.metadata_vaddr = vaddr;
        self.metadata_rkey = rkey;
        self.metadata_len = len;
        self.metadata_version = version;
    }

    /// Update the invalidation buffer region information.
    pub fn set_invalidation_region(&mut self, vaddr: u64, rkey: u32, len: usize) {
        self.invalidation_vaddr = vaddr;
        self.invalidation_rkey = rkey;
        self.invalidation_len = len;
    }
}

/// Error types for domain model parsing and validation.
#[derive(Debug, Error)]
pub enum DomainError {
    #[error("Invalid virtual address offset: 0x{vaddr:x} outside base 0x{base:x} (size {size})")]
    InvalidOffset { vaddr: u64, base: u64, size: usize },

    #[error("Null pointer or zero-sized allocation invalid")]
    ZeroSizeAllocation,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_memory_region_range_check() {
        let descriptor = MemoryRegionDescriptor {
            region_id: GlobalRegionId { owner_pid: 100, shmid: 1 },
            main_vaddr: 0x7fff_0000_0000,
            region_size: 4 * 1024 * 1024, // 4MB
            staging_vaddr: 0x7fff_1000_0000,
            staging_num_pages: 2,
            staging_page_size: 2 * 1024 * 1024,
            prot_flags: MemoryProtectionFlags(MemoryProtectionFlags::READ | MemoryProtectionFlags::WRITE),
            mem_flags: 0,
            version: 0,
        };

        assert!(descriptor.contains_range(0x7fff_0000_0000, 2 * 1024 * 1024));
        assert_eq!(descriptor.offset_of(0x7fff_0010_0000), Some(0x10_0000));
        assert!(!descriptor.contains_range(0x7fff_0000_0000, 5 * 1024 * 1024));
        assert_eq!(descriptor.offset_of(0x7fff_0500_0000), None);
    }

    #[test]
    fn test_remote_endpoint_info_construction() {
        let node_id = NodeId(42);
        let addr: SocketAddr = "192.168.1.10:19001".parse().unwrap();
        let mut info = RemoteEndpointInfo::new(node_id, addr);
        assert_eq!(info.node_id, node_id);
        assert_eq!(info.socket_addr, addr);
        assert_eq!(info.metadata_vaddr, 0);
        assert_eq!(info.invalidation_vaddr, 0);

        info.set_metadata_region(0x7fff_2000_0000, 1234, 4096, 5);
        info.set_invalidation_region(0x7fff_3000_0000, 5678, 1024);
        assert_eq!(info.metadata_vaddr, 0x7fff_2000_0000);
        assert_eq!(info.metadata_rkey, 1234);
        assert_eq!(info.metadata_len, 4096);
        assert_eq!(info.metadata_version, 5);
        assert_eq!(info.invalidation_vaddr, 0x7fff_3000_0000);
        assert_eq!(info.invalidation_rkey, 5678);
        assert_eq!(info.invalidation_len, 1024);
    }

    #[test]
    fn test_distributed_page_entry_with_metadata() {
        let owner = NodeId(1);
        let mut entry = DistributedPageEntry::new(0x1000, 4096, owner);
        assert_eq!(entry.metadata.len(), 0);
        entry.metadata = vec![1, 2, 3, 4];
        assert_eq!(entry.metadata, vec![1, 2, 3, 4]);

        let entry2 = DistributedPageEntry::new_with_metadata(0x2000, 4096, owner, vec![9, 9]);
        assert_eq!(entry2.metadata, vec![9, 9]);
    }
}