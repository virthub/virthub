// virthub/src/index/src/radix_tree.rs

//! A high‑performance 64‑bit Virtual Address Radix Tree.
//!
//! This tree maps an exact virtual address to a `RadixLeafValue`. It does
//! **not** enforce any alignment or size constraints – the caller may insert
//! entries at any address (e.g., 4 KB, 128 KB, or 2 MB boundaries). This
//! directly supports **variable‑sized coherence domains**: each coherence unit
//! is simply inserted with its exact base address, and lookups return the
//! metadata for that exact address.
//!
//! The implementation uses a 4‑level trie with 16 bits per level (64‑bit
//! addresses). Each branch node contains a `HashMap` of child nodes for
//! memory efficiency. The tree is protected by a single `parking_lot::RwLock`,
//! allowing concurrent reads while writes are serialised.

use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use store::kv_block::KvBlockKey;
use thiserror::Error;

/// Number of bits processed per radix tree level (4 levels × 16 bits = 64 bits).
pub const RADIX_BITS_PER_LEVEL: usize = 16;
/// Number of possible child indices per level.
pub const RADIX_FANOUT: usize = 1 << RADIX_BITS_PER_LEVEL;

/// Errors originating from Radix Tree index operations.
#[derive(Debug, Error)]
pub enum RadixTreeError {
    #[error("Virtual address 0x{0:x} not mapped in radix tree")]
    AddressUnmapped(u64),

    #[error("Memory allocation failed for radix node")]
    AllocationFailed,

    #[error("Virtual address range [0x{start:x}, 0x{end:x}] extends beyond mapped bounds")]
    InvalidRange { start: u64, end: u64 },
}

/// Metadata mapped at the leaf level of the Radix Tree.
///
/// This value is associated with an **exact** base virtual address – no
/// alignment is assumed. It can represent any size coherence unit (the size
/// is stored elsewhere, e.g., in the control plane's `DistributedPageEntry`).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RadixLeafValue {
    pub block_key: KvBlockKey,
    pub frame_offset: u64,
    pub flags: u32,
}

/// Internal node representation.
#[derive(Debug)]
enum Node {
    /// Internal branch node holding child nodes.
    Branch(HashMap<u16, Arc<Node>>),
    /// Leaf node containing the value.
    Leaf(RadixLeafValue),
}

impl Node {
    fn new_branch() -> Self {
        Node::Branch(HashMap::new())
    }
}

/// A high‑performance 64‑bit Virtual Address Radix Tree.
///
/// # Variable‑Sized Coherence Support
///
/// The tree does not enforce page alignment. Entries can be inserted at any
/// virtual address, making it suitable for managing coherence units of
/// arbitrary size (e.g., 4 KB, 2 MB, or any custom block size).
pub struct ConcurrentRadixTree {
    root: RwLock<Node>,
    entry_count: AtomicUsize,
}

impl ConcurrentRadixTree {
    /// Instantiates a new empty `ConcurrentRadixTree`.
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            root: RwLock::new(Node::new_branch()),
            entry_count: AtomicUsize::new(0),
        })
    }

    /// Extracts the 16‑bit chunk key for a given level (level 0..3 for 64‑bit addresses).
    #[inline]
    fn extract_chunk(vaddr: u64, level: usize) -> u16 {
        let shift = (3 - level) * RADIX_BITS_PER_LEVEL;
        ((vaddr >> shift) & 0xFFFF) as u16
    }

    /// Maps an **exact** 64‑bit virtual address (`vaddr`) to a `RadixLeafValue`.
    ///
    /// The address does not need to be aligned; the tree simply inserts at the
    /// given key. To manage a coherence unit of size `N`, insert an entry with
    /// the unit's base address.
    pub fn insert(&self, vaddr: u64, value: RadixLeafValue) -> Result<(), RadixTreeError> {
        let mut write_guard = self.root.write();
        let mut current = &mut *write_guard;

        for level in 0..4 {
            match current {
                Node::Branch(children) => {
                    let chunk = Self::extract_chunk(vaddr, level);
                    if level == 3 {
                        // Final level: insert or replace leaf.
                        if children.contains_key(&chunk) {
                            children.insert(chunk, Arc::new(Node::Leaf(value)));
                            // entry_count unchanged
                        } else {
                            children.insert(chunk, Arc::new(Node::Leaf(value)));
                            self.entry_count.fetch_add(1, Ordering::Relaxed);
                        }
                        return Ok(());
                    } else {
                        // Intermediate level: ensure child branch exists.
                        if !children.contains_key(&chunk) {
                            children.insert(chunk, Arc::new(Node::new_branch()));
                        }
                        // We hold exclusive write lock, so we can get mutable access.
                        let child_arc = children.get_mut(&chunk).unwrap();
                        current = Arc::get_mut(child_arc).unwrap();
                    }
                }
                Node::Leaf(_) => {
                    // Should never encounter a leaf before level 3.
                    return Err(RadixTreeError::AddressUnmapped(vaddr));
                }
            }
        }
        Ok(())
    }

    /// Looks up a mapped virtual address and returns the associated `RadixLeafValue`.
    ///
    /// The lookup is exact; the caller must provide the same base address that
    /// was used during insertion.
    pub fn lookup(&self, vaddr: u64) -> Result<RadixLeafValue, RadixTreeError> {
        let read_guard = self.root.read();
        let mut current = &*read_guard;

        for level in 0..4 {
            match current {
                Node::Branch(children) => {
                    let chunk = Self::extract_chunk(vaddr, level);
                    match children.get(&chunk) {
                        Some(child) => current = child,
                        None => return Err(RadixTreeError::AddressUnmapped(vaddr)),
                    }
                }
                Node::Leaf(_) => {
                    // If we reach a leaf before the last level, address is unmapped.
                    return Err(RadixTreeError::AddressUnmapped(vaddr));
                }
            }
        }

        // At level 4, current must be a Leaf.
        match current {
            Node::Leaf(val) => Ok(*val),
            _ => Err(RadixTreeError::AddressUnmapped(vaddr)),
        }
    }

    /// Performs range query returning mapped leaf values for contiguous range `[start_vaddr, end_vaddr)`.
    ///
    /// The `step` parameter should match the coherence unit size used during insertion
    /// (e.g., 4096 for 4 KB pages, 2 MB for huge pages, etc.).
    pub fn lookup_range(
        &self,
        start_vaddr: u64,
        end_vaddr: u64,
        step: usize,
    ) -> Vec<(u64, RadixLeafValue)> {
        let mut results = Vec::new();
        let mut curr = start_vaddr;

        while curr < end_vaddr {
            if let Ok(val) = self.lookup(curr) {
                results.push((curr, val));
            }
            curr += step as u64;
        }

        results
    }

    /// Returns the total count of mapped leaf addresses in the tree.
    pub fn len(&self) -> usize {
        self.entry_count.load(Ordering::Relaxed)
    }

    /// Returns whether the tree is empty.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_radix_tree_insert_and_lookup() {
        let tree = ConcurrentRadixTree::new();
        let vaddr = 0x7fff_1000_0000u64;

        let leaf_val = RadixLeafValue {
            block_key: KvBlockKey::new(1, 42),
            frame_offset: 2048,
            flags: 0x01,
        };

        tree.insert(vaddr, leaf_val).expect("Insert must succeed");
        assert_eq!(tree.len(), 1);

        let retrieved = tree.lookup(vaddr).expect("Lookup must succeed");
        assert_eq!(retrieved, leaf_val);
    }

    #[test]
    fn test_radix_tree_unmapped_lookup() {
        let tree = ConcurrentRadixTree::new();
        let unmapped_addr = 0x7fff_9999_0000u64;

        let res = tree.lookup(unmapped_addr);
        assert!(matches!(res, Err(RadixTreeError::AddressUnmapped(_))));
    }

    #[test]
    fn test_radix_tree_range_scan() {
        let tree = ConcurrentRadixTree::new();
        let base_vaddr = 0x7fff_0000_0000u64;
        let page_size = 2 * 1024 * 1024; // 2MB

        for i in 0..4 {
            let addr = base_vaddr + (i * page_size);
            let val = RadixLeafValue {
                block_key: KvBlockKey::new(100, i),
                frame_offset: i * 4096,
                flags: 0,
            };
            tree.insert(addr, val).unwrap();
        }

        assert_eq!(tree.len(), 4);

        let end_vaddr = base_vaddr + (4 * page_size);
        let range_results = tree.lookup_range(base_vaddr, end_vaddr, page_size as usize);

        assert_eq!(range_results.len(), 4);
        assert_eq!(range_results[0].0, base_vaddr);
        assert_eq!(range_results[3].0, base_vaddr + (3 * page_size));
    }

    #[test]
    fn test_variable_sized_inserts() {
        // Insert entries at different granularities – the tree should handle them.
        let tree = ConcurrentRadixTree::new();

        let vaddr_4k = 0x1000u64;
        let vaddr_2m = 0x200000u64;
        let vaddr_custom = 0x7fff_1234_5678u64;

        tree.insert(vaddr_4k, RadixLeafValue { block_key: KvBlockKey::new(1,1), frame_offset: 0, flags: 0 }).unwrap();
        tree.insert(vaddr_2m, RadixLeafValue { block_key: KvBlockKey::new(1,2), frame_offset: 0, flags: 0 }).unwrap();
        tree.insert(vaddr_custom, RadixLeafValue { block_key: KvBlockKey::new(1,3), frame_offset: 0, flags: 0 }).unwrap();

        assert_eq!(tree.len(), 3);
        assert!(tree.lookup(vaddr_4k).is_ok());
        assert!(tree.lookup(vaddr_2m).is_ok());
        assert!(tree.lookup(vaddr_custom).is_ok());
    }
}
