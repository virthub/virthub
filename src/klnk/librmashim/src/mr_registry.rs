// virthub/src/klnk/librmashim/src/mr_registry.rs

use parking_lot::RwLock;
use std::collections::HashMap;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum MemoryRegionError {
    #[error("Memory region key 0x{rkey:x} already registered")]
    AlreadyExists { rkey: u32 },

    #[error("Memory region key 0x{rkey:x} not found in registry")]
    NotFound { rkey: u32 },

    #[error("Invalid memory region descriptor: {0}")]
    InvalidDescriptor(String),
}

/// Metadata descriptor for a memory region registered for RDMA/RMA operations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MemoryRegion {
    pub rkey: u32,
    pub lkey: u32,
    pub vaddr: u64,
    pub size: usize,
    pub flags: u32,
    pub node_id: u32,
}

pub type MemoryRegionHandle = MemoryRegion;

impl MemoryRegion {
    pub fn new(rkey: u32, lkey: u32, vaddr: u64, size: usize, flags: u32, node_id: u32) -> Self {
        Self {
            rkey,
            lkey,
            vaddr,
            size,
            flags,
            node_id,
        }
    }

    /// Check if a given virtual address range resides entirely within this memory region.
    pub fn contains_range(&self, target_vaddr: u64, length: usize) -> bool {
        target_vaddr >= self.vaddr
            && (target_vaddr + length as u64) <= (self.vaddr + self.size as u64)
    }
}

/// Thread‑safe in‑memory registry tracking active RDMA memory regions.
///
/// The registry uses two indices for fast lookup:
/// - by `rkey`
/// - by `(vaddr, size)` (to avoid duplicate registration)
#[derive(Debug)]
pub struct MemoryRegionRegistry {
    regions: RwLock<HashMap<u32, MemoryRegion>>,
    vaddr_index: RwLock<HashMap<(u64, usize), u32>>,
}

impl MemoryRegionRegistry {
    pub fn new() -> Self {
        Self {
            regions: RwLock::new(HashMap::new()),
            vaddr_index: RwLock::new(HashMap::new()),
        }
    }

    /// Register a new pre‑constructed memory region descriptor.
    pub fn register(&self, region: MemoryRegion) -> Result<(), MemoryRegionError> {
        if region.vaddr == 0 || region.size == 0 {
            return Err(MemoryRegionError::InvalidDescriptor(
                "Base address and size must be non‑zero".to_string(),
            ));
        }

        let mut regions = self.regions.write();
        if regions.contains_key(&region.rkey) {
            return Err(MemoryRegionError::AlreadyExists { rkey: region.rkey });
        }

        // Insert into vaddr index.
        {
            let mut idx = self.vaddr_index.write();
            idx.insert((region.vaddr, region.size), region.rkey);
        }

        regions.insert(region.rkey, region);
        Ok(())
    }

    /// Construct and register a memory region from parameters.
    pub fn register_region(
        &self,
        vaddr: u64,
        size: usize,
        rkey: u32,
        lkey: u32,
        flags: u32,
        node_id: u32,
    ) -> Result<MemoryRegionHandle, MemoryRegionError> {
        let mr = MemoryRegion::new(rkey, lkey, vaddr, size, flags, node_id);
        self.register(mr.clone())?;
        Ok(mr)
    }

    /// Retrieve a cloned memory region descriptor by its remote key.
    pub fn get(&self, rkey: u32) -> Option<MemoryRegion> {
        self.regions.read().get(&rkey).cloned()
    }

    /// Retrieve a memory region by exact (vaddr, size) pair.
    pub fn get_by_vaddr_size(&self, vaddr: u64, size: usize) -> Option<MemoryRegion> {
        let idx = self.vaddr_index.read();
        if let Some(rkey) = idx.get(&(vaddr, size)) {
            self.regions.read().get(rkey).cloned()
        } else {
            None
        }
    }

    /// Deregister and return a memory region by remote key.
    pub fn deregister(&self, rkey: u32) -> Result<MemoryRegion, MemoryRegionError> {
        let mut regions = self.regions.write();
        if let Some(region) = regions.remove(&rkey) {
            // Remove from vaddr index.
            let mut idx = self.vaddr_index.write();
            idx.remove(&(region.vaddr, region.size));
            Ok(region)
        } else {
            Err(MemoryRegionError::NotFound { rkey })
        }
    }

    /// Clear all registered memory regions.
    pub fn clear(&self) {
        self.regions.write().clear();
        self.vaddr_index.write().clear();
    }

    /// Return count of registered memory regions.
    pub fn len(&self) -> usize {
        self.regions.read().len()
    }

    /// Check if registry is empty.
    pub fn is_empty(&self) -> bool {
        self.regions.read().is_empty()
    }
}

impl Default for MemoryRegionRegistry {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_memory_region_registry_lifecycle() {
        let registry = MemoryRegionRegistry::new();
        let mr = MemoryRegion::new(1001, 1001, 0x7fff_0000_0000, 4096, 0x3, 1);

        assert!(registry.register(mr.clone()).is_ok());
        assert_eq!(registry.len(), 1);

        // Duplicate registration should fail.
        assert!(registry.register(mr.clone()).is_err());

        // Range lookup check.
        let fetched = registry.get(1001).expect("Region should exist");
        assert!(fetched.contains_range(0x7fff_0000_0000, 2048));
        assert!(!fetched.contains_range(0x7fff_0000_1000, 2048));

        // Lookup by vaddr/size.
        let by_addr = registry.get_by_vaddr_size(0x7fff_0000_0000, 4096);
        assert!(by_addr.is_some());
        assert_eq!(by_addr.unwrap().rkey, 1001);

        // Deregistration.
        let removed = registry.deregister(1001).expect("Deregister should succeed");
        assert_eq!(removed.rkey, 1001);
        assert!(registry.is_empty());

        // vaddr index should also be empty.
        assert!(registry.get_by_vaddr_size(0x7fff_0000_0000, 4096).is_none());
    }
}
