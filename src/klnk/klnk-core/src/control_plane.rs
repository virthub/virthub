// virthub/src/klnk/klnk-core/src/control_plane.rs

//! Central control plane coordinator for the KLNK DSM engine.
//!
//! This module manages memory regions, page coherence states, distributed locks,
//! and remote node information. It is transport-agnostic and does not depend on
//! RDMA or serialization libraries.
//!
//! ## Variable‑Sized Coherence Domains
//!
//! Unlike the previous version, this implementation **does not** force a fixed
//! 2 MB alignment when looking up page states.  The caller provides the exact
//! base virtual address that was used when the entry was created.  This allows
//! the system to manage coherence at any granularity (e.g., 4 KB sub‑pages,
//! 128 KB KV‑cache blocks, or 2 MB huge pages).
//!
//! ## Self‑Invalidation Support
//!
//! The new `get_page_version` method returns the current version of a coherence
//! entry.  Readers can compare their local version against this value and
//! invalidate their copy when they detect a mismatch – a **lazy, requester‑driven**
//! approach that eliminates writer‑side CPU overhead for invalidation messages.
//!
//! ## Performance Optimizations
//!
//! - Page state entries are stored as `Arc<DistributedPageEntry>` to avoid
//!   expensive clones when reading.
//! - Batch update methods are provided to reduce lock acquisitions.
//! - `DashMap` is used for concurrent access with minimal contention.

use crate::domain::{
    GlobalRegionId, MemoryRegionDescriptor, NodeId, PageCoherenceState,
    RemoteEndpointInfo,
};
use dashmap::DashMap;
use std::sync::Arc;
use thiserror::Error;
use tokio::sync::RwLock;

#[derive(Debug, Error)]
pub enum ControlPlaneError {
    #[error("Region already registered: {0}")]
    RegionAlreadyExists(GlobalRegionId),

    #[error("Region not found: {0}")]
    RegionNotFound(GlobalRegionId),

    #[error("Virtual address 0x{vaddr:x} not mapped in region {region_id}")]
    AddressNotMapped { region_id: GlobalRegionId, vaddr: u64 },

    #[error("Lock acquisition conflict for resource 0x{resource_id:x} by client PID {client_pid}")]
    LockConflict { resource_id: u64, client_pid: u32 },

    #[error("Lock release error: resource 0x{resource_id:x} not held by client PID {client_pid}")]
    LockNotHeld { resource_id: u64, client_pid: u32 },
}

/// Metadata tracking distributed page state across cluster nodes.
///
/// The `page_size` field defines the exact length of the coherence unit.
/// It can be any value (4 KB, 2 MB, or an arbitrary block size), enabling
/// variable‑sized coherence domains.
#[derive(Debug, Clone)]
pub struct DistributedPageEntry {
    pub page_vaddr: u64,
    pub page_size: usize,
    pub coherence_state: PageCoherenceState,
    pub primary_owner: NodeId,
    /// List of nodes that hold a SharedRead copy of this page.
    pub replica_holders: Vec<NodeId>,
    /// Version number incremented on every exclusive write.
    /// Used by readers for self‑invalidation.
    pub version: u64,
}

impl DistributedPageEntry {
    /// Creates a new entry with version 0 and an empty reader list.
    pub fn new(page_vaddr: u64, page_size: usize, owner: NodeId) -> Self {
        Self {
            page_vaddr,
            page_size,
            coherence_state: PageCoherenceState::Invalid,
            primary_owner: owner,
            replica_holders: Vec::new(),
            version: 0,
        }
    }
}

/// State of an active distributed spinlock / mutex.
#[derive(Debug, Clone)]
pub struct LockState {
    pub resource_id: u64,
    pub current_owner_pid: Option<u32>,
    pub waiting_pids: Vec<u32>,
}

/// Central control plane coordinator managing memory regions, coherence, and lock states.
#[derive(Debug)]
pub struct ControlPlaneManager {
    local_node_id: NodeId,
    regions: DashMap<GlobalRegionId, MemoryRegionDescriptor>,
    page_states: DashMap<u64, Arc<DistributedPageEntry>>,
    remote_nodes: DashMap<NodeId, RemoteEndpointInfo>,
    locks: DashMap<u64, Arc<RwLock<LockState>>>,
}

impl ControlPlaneManager {
    /// Instantiates a new ControlPlaneManager for the local node.
    pub fn new(local_node_id: NodeId) -> Arc<Self> {
        Arc::new(Self {
            local_node_id,
            regions: DashMap::new(),
            page_states: DashMap::new(),
            remote_nodes: DashMap::new(),
            locks: DashMap::new(),
        })
    }

    /// Registers a new virtual memory region with the control plane.
    ///
    /// Page state entries are created for each `staging_page_size` chunk within
    /// the region.  The exact base address of each chunk is stored, enabling
    /// lookups at any granularity.
    pub fn register_region(
        &self,
        descriptor: MemoryRegionDescriptor,
    ) -> Result<(), ControlPlaneError> {
        let region_id = descriptor.region_id;

        if self.regions.contains_key(&region_id) {
            return Err(ControlPlaneError::RegionAlreadyExists(region_id));
        }

        let page_size = descriptor.staging_page_size.max(4096);
        let mut curr_addr = descriptor.main_vaddr;
        let end_addr = descriptor.main_vaddr + descriptor.region_size as u64;

        while curr_addr < end_addr {
            let entry = Arc::new(DistributedPageEntry::new(curr_addr, page_size, self.local_node_id));
            self.page_states.insert(curr_addr, entry);
            curr_addr += page_size as u64;
        }

        self.regions.insert(region_id, descriptor);
        Ok(())
    }

    /// Deregisters an active virtual memory region and purges associated page entries.
    pub fn deregister_region(
        &self,
        region_id: GlobalRegionId,
    ) -> Result<(), ControlPlaneError> {
        if let Some((_, descriptor)) = self.regions.remove(&region_id) {
            let page_size = descriptor.staging_page_size.max(4096);
            let mut curr_addr = descriptor.main_vaddr;
            let end_addr = descriptor.main_vaddr + descriptor.region_size as u64;

            while curr_addr < end_addr {
                self.page_states.remove(&curr_addr);
                curr_addr += page_size as u64;
            }

            Ok(())
        } else {
            Err(ControlPlaneError::RegionNotFound(region_id))
        }
    }

    /// Retrieves a copy of a registered region descriptor.
    pub fn get_region(
        &self,
        region_id: GlobalRegionId,
    ) -> Result<MemoryRegionDescriptor, ControlPlaneError> {
        self.regions
            .get(&region_id)
            .map(|r| r.clone())
            .ok_or(ControlPlaneError::RegionNotFound(region_id))
    }

    /// Updates page coherence state upon fault resolution or invalidation.
    ///
    /// If the new state is `ExclusiveWrite`, the version is incremented and the
    /// readers list is cleared.
    pub fn update_page_state(
        &self,
        page_vaddr: u64,
        new_state: PageCoherenceState,
        owner: NodeId,
    ) -> bool {
        if let Some(mut entry) = self.page_states.get_mut(&page_vaddr) {
            let entry = Arc::make_mut(&mut entry);
            entry.coherence_state = new_state;
            entry.primary_owner = owner;
            if new_state == PageCoherenceState::ExclusiveWrite {
                entry.version += 1;
                entry.replica_holders.clear();
            }
            true
        } else {
            false
        }
    }

    /// Batch version of `update_page_state` for multiple pages.
    pub fn update_page_states_batch(
        &self,
        updates: &[(u64, PageCoherenceState, NodeId)],
    ) -> Vec<bool> {
        updates.iter()
            .map(|&(addr, state, owner)| self.update_page_state(addr, state, owner))
            .collect()
    }

    /// Adds a node to the readers list of a page (used when a node obtains a SharedRead copy).
    pub fn add_reader(&self, page_vaddr: u64, reader_node: NodeId) -> bool {
        if let Some(mut entry) = self.page_states.get_mut(&page_vaddr) {
            let entry = Arc::make_mut(&mut entry);
            if !entry.replica_holders.contains(&reader_node) {
                entry.replica_holders.push(reader_node);
            }
            true
        } else {
            false
        }
    }

    /// Batch version of `add_reader`.
    pub fn add_readers_batch(&self, additions: &[(u64, NodeId)]) -> Vec<bool> {
        additions.iter()
            .map(|&(addr, node)| self.add_reader(addr, node))
            .collect()
    }

    /// Removes a node from the readers list (e.g., on eviction or invalidation).
    pub fn remove_reader(&self, page_vaddr: u64, reader_node: NodeId) -> bool {
        if let Some(mut entry) = self.page_states.get_mut(&page_vaddr) {
            let entry = Arc::make_mut(&mut entry);
            entry.replica_holders.retain(|&n| n != reader_node);
            true
        } else {
            false
        }
    }

    /// Batch version of `remove_reader`.
    pub fn remove_readers_batch(&self, removals: &[(u64, NodeId)]) -> Vec<bool> {
        removals.iter()
            .map(|&(addr, node)| self.remove_reader(addr, node))
            .collect()
    }

    /// Gets the readers list for a page.
    pub fn get_readers(&self, page_vaddr: u64) -> Option<Vec<NodeId>> {
        self.page_states
            .get(&page_vaddr)
            .map(|entry| entry.replica_holders.clone())
    }

    /// Looks up page coherence info for a **specific** base virtual address.
    ///
    /// **Important:** The caller must provide the exact base address that was
    /// used when the entry was created.  No automatic alignment is performed.
    /// This allows coherence entries of any size (variable‑sized domains).
    ///
    /// Returns an `Arc` to avoid cloning the entry.
    pub fn lookup_page_state(&self, vaddr: u64) -> Option<Arc<DistributedPageEntry>> {
        self.page_states.get(&vaddr).map(|entry| entry.clone())
    }

    /// Returns the current version number of a coherence entry.
    ///
    /// This enables **lazy self‑invalidation**: a reader can compare its local
    /// version against this value and, if they differ, invalidate its copy and
    /// re‑fetch the data.  The writer does not need to send explicit invalidation
    /// messages.
    pub fn get_page_version(&self, page_vaddr: u64) -> Option<u64> {
        self.page_states.get(&page_vaddr).map(|e| e.version)
    }

    /// Returns a snapshot of all current page states.
    /// This is used by the daemon to generate RDMA-accessible metadata.
    /// Returns `Arc` handles to avoid deep copies.
    pub fn get_all_page_states(&self) -> Vec<Arc<DistributedPageEntry>> {
        self.page_states.iter().map(|entry| entry.clone()).collect()
    }

    /// Registers a remote cluster node's endpoint info.
    pub fn register_remote_node(&self, info: RemoteEndpointInfo) {
        self.remote_nodes.insert(info.node_id, info);
    }

    /// Removes a remote node endpoint on disconnect.
    pub fn remove_remote_node(&self, node_id: NodeId) {
        self.remote_nodes.remove(&node_id);
    }

    /// Retrieves endpoint info for a remote node by ID.
    pub fn get_remote_node(&self, node_id: NodeId) -> Option<RemoteEndpointInfo> {
        self.remote_nodes.get(&node_id).map(|entry| entry.clone())
    }

    /// Returns a list of all currently registered remote nodes.
    ///
    /// This is used by the daemon to push metadata snapshots to every peer.
    pub fn get_all_remote_nodes(&self) -> Vec<RemoteEndpointInfo> {
        self.remote_nodes.iter().map(|entry| entry.clone()).collect()
    }

    /// Acquires a distributed lock for a given resource ID.
    pub async fn acquire_lock(
        &self,
        resource_id: u64,
        client_pid: u32,
    ) -> Result<(), ControlPlaneError> {
        let lock_arc = self
            .locks
            .entry(resource_id)
            .or_insert_with(|| {
                Arc::new(RwLock::new(LockState {
                    resource_id,
                    current_owner_pid: None,
                    waiting_pids: Vec::new(),
                }))
            })
            .clone();

        let mut lock_state = lock_arc.write().await;

        match lock_state.current_owner_pid {
            None => {
                lock_state.current_owner_pid = Some(client_pid);
                Ok(())
            }
            Some(owner) if owner == client_pid => Ok(()), // Reentrant acquisition
            Some(_) => {
                if !lock_state.waiting_pids.contains(&client_pid) {
                    lock_state.waiting_pids.push(client_pid);
                }
                Err(ControlPlaneError::LockConflict {
                    resource_id,
                    client_pid,
                })
            }
        }
    }

    /// Releases a distributed lock for a given resource ID.
    pub async fn release_lock(
        &self,
        resource_id: u64,
        client_pid: u32,
    ) -> Result<(), ControlPlaneError> {
        if let Some(lock_arc) = self.locks.get(&resource_id).map(|l| l.clone()) {
            let mut lock_state = lock_arc.write().await;

            if lock_state.current_owner_pid == Some(client_pid) {
                if !lock_state.waiting_pids.is_empty() {
                    let next_owner = lock_state.waiting_pids.remove(0);
                    lock_state.current_owner_pid = Some(next_owner);
                } else {
                    lock_state.current_owner_pid = None;
                }
                Ok(())
            } else {
                Err(ControlPlaneError::LockNotHeld {
                    resource_id,
                    client_pid,
                })
            }
        } else {
            Err(ControlPlaneError::LockNotHeld {
                resource_id,
                client_pid,
            })
        }
    }

    /// Returns the local node ID.
    pub fn local_node_id(&self) -> NodeId {
        self.local_node_id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::MemoryProtectionFlags;

    #[tokio::test]
    async fn test_region_registration_lifecycle() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let region_id = GlobalRegionId {
            owner_pid: 100,
            shmid: 1,
        };

        let descriptor = MemoryRegionDescriptor {
            region_id,
            main_vaddr: 0x7fff_0000_0000,
            region_size: 4 * 1024 * 1024,
            staging_vaddr: 0x7fff_1000_0000,
            staging_num_pages: 2,
            staging_page_size: 2 * 1024 * 1024,
            prot_flags: MemoryProtectionFlags(
                MemoryProtectionFlags::READ | MemoryProtectionFlags::WRITE,
            ),
            mem_flags: 0,
            version: 0,
        };

        cp.register_region(descriptor.clone())
            .expect("Registration should succeed");

        let fetched = cp.get_region(region_id).expect("Fetch should succeed");
        assert_eq!(fetched.main_vaddr, 0x7fff_0000_0000);

        // Page state lookup test – use the exact base address, not an offset.
        let page_entry = cp
            .lookup_page_state(0x7fff_0000_0000)
            .expect("Page state must exist");
        assert_eq!(page_entry.page_vaddr, 0x7fff_0000_0000);
        assert_eq!(page_entry.coherence_state, PageCoherenceState::Invalid);
        assert_eq!(page_entry.version, 0);

        // Looking up an address that is not an exact base should return None.
        assert!(cp.lookup_page_state(0x7fff_0000_1000).is_none());

        cp.deregister_region(region_id)
            .expect("Deregistration should succeed");
        assert!(cp.get_region(region_id).is_err());
    }

    #[tokio::test]
    async fn test_lock_acquire_and_release() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let resource_id = 0xDEADBEEF;

        cp.acquire_lock(resource_id, 100)
            .await
            .expect("First acquire should succeed");

        let res = cp.acquire_lock(resource_id, 200).await;
        assert!(matches!(res, Err(ControlPlaneError::LockConflict { .. })));

        cp.release_lock(resource_id, 100)
            .await
            .expect("Release should succeed");

        cp.release_lock(resource_id, 200)
            .await
            .expect("Second owner release should succeed");
    }

    #[test]
    fn test_version_increment_on_write() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let region_id = GlobalRegionId {
            owner_pid: 100,
            shmid: 1,
        };
        let descriptor = MemoryRegionDescriptor {
            region_id,
            main_vaddr: 0x7fff_0000_0000,
            region_size: 4096,
            staging_vaddr: 0,
            staging_num_pages: 1,
            staging_page_size: 4096,
            prot_flags: MemoryProtectionFlags(0x3),
            mem_flags: 0,
            version: 0,
        };
        cp.register_region(descriptor).unwrap();
        let vaddr = 0x7fff_0000_0000;

        let entry = cp.lookup_page_state(vaddr).unwrap();
        assert_eq!(entry.version, 0);

        cp.update_page_state(vaddr, PageCoherenceState::ExclusiveWrite, NodeId(1));
        let entry = cp.lookup_page_state(vaddr).unwrap();
        assert_eq!(entry.version, 1);

        // Version unchanged for SharedRead.
        cp.update_page_state(vaddr, PageCoherenceState::SharedRead, NodeId(1));
        let entry = cp.lookup_page_state(vaddr).unwrap();
        assert_eq!(entry.version, 1);
    }

    #[test]
    fn test_add_remove_readers() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let region_id = GlobalRegionId {
            owner_pid: 100,
            shmid: 1,
        };
        let descriptor = MemoryRegionDescriptor {
            region_id,
            main_vaddr: 0x7fff_0000_0000,
            region_size: 4096,
            staging_vaddr: 0,
            staging_num_pages: 1,
            staging_page_size: 4096,
            prot_flags: MemoryProtectionFlags(0x3),
            mem_flags: 0,
            version: 0,
        };
        cp.register_region(descriptor).unwrap();
        let vaddr = 0x7fff_0000_0000;
        let node2 = NodeId(2);

        assert!(cp.add_reader(vaddr, node2));
        let readers = cp.get_readers(vaddr).unwrap();
        assert_eq!(readers, vec![node2]);

        assert!(cp.remove_reader(vaddr, node2));
        let readers = cp.get_readers(vaddr).unwrap();
        assert!(readers.is_empty());
    }

    #[test]
    fn test_variable_sized_entries() {
        // Create entries with different sizes and verify they can be looked up
        // by their exact base addresses.
        let cp = ControlPlaneManager::new(NodeId(1));

        // Register a region with 4 KB pages
        let region_id_4k = GlobalRegionId {
            owner_pid: 100,
            shmid: 1,
        };
        let desc_4k = MemoryRegionDescriptor {
            region_id: region_id_4k,
            main_vaddr: 0x1000,
            region_size: 12288,
            staging_vaddr: 0,
            staging_num_pages: 3,
            staging_page_size: 4096,
            prot_flags: MemoryProtectionFlags(0x3),
            mem_flags: 0,
            version: 0,
        };
        cp.register_region(desc_4k).unwrap();

        // Register another region with 2 MB pages
        let region_id_2m = GlobalRegionId {
            owner_pid: 101,
            shmid: 2,
        };
        let desc_2m = MemoryRegionDescriptor {
            region_id: region_id_2m,
            main_vaddr: 0x200000,
            region_size: 2 * 1024 * 1024,
            staging_vaddr: 0,
            staging_num_pages: 1,
            staging_page_size: 2 * 1024 * 1024,
            prot_flags: MemoryProtectionFlags(0x3),
            mem_flags: 0,
            version: 0,
        };
        cp.register_region(desc_2m).unwrap();

        // Lookup should succeed for exact base addresses
        assert!(cp.lookup_page_state(0x1000).is_some());
        assert!(cp.lookup_page_state(0x2000).is_some());
        assert!(cp.lookup_page_state(0x200000).is_some());

        // Get version for self‑invalidation
        assert_eq!(cp.get_page_version(0x1000), Some(0));

        // Update version for 4 KB page
        cp.update_page_state(0x1000, PageCoherenceState::ExclusiveWrite, NodeId(1));
        assert_eq!(cp.get_page_version(0x1000), Some(1));

        // Other entries unaffected
        assert_eq!(cp.get_page_version(0x2000), Some(0));
        assert_eq!(cp.get_page_version(0x200000), Some(0));
    }

    #[test]
    fn test_get_all_remote_nodes() {
        let cp = ControlPlaneManager::new(NodeId(1));
        let node2 = NodeId(2);
        let info = RemoteEndpointInfo::new(node2, "192.168.1.2:19001".parse().unwrap());
        cp.register_remote_node(info);
        let all = cp.get_all_remote_nodes();
        assert_eq!(all.len(), 1);
        assert_eq!(all[0].node_id, node2);
    }
}
