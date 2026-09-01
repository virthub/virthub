// virthub/src/index/src/lib.rs

pub mod eviction;
pub mod hash_index;
pub mod radix_tree;

// Re-export concrete index & eviction types matching submodule definitions
pub use eviction::{EvictionError, EvictionPolicy, TwoQEvictionPolicy};
pub use hash_index::{ConcurrentHashIndex, HashIndexError, IndexSlotValue};
pub use radix_tree::{ConcurrentRadixTree, RadixLeafValue, RadixTreeError};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_two_q_eviction_policy_instantiation() {
        let policy = TwoQEvictionPolicy::new();
        // Ensure standard initialization succeeds
        drop(policy);
    }

    #[test]
    fn test_hash_index_creation() {
        let index = ConcurrentHashIndex::new(64).expect("index creation should succeed");
        assert_eq!(index.capacity(), 64);
        assert!(index.is_empty());
    }

    #[test]
    fn test_radix_tree_creation() {
        let tree = ConcurrentRadixTree::new();
        assert!(tree.is_empty());
    }
}
