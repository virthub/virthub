// virthub/src/klnk/klnk-core/src/lib.rs

pub mod control_plane;
pub mod diff;
pub mod domain;

// Re-export primary types from submodules for workspace-wide consumption
pub use control_plane::ControlPlaneError;
pub use diff::{DiffError, PageDiffList};
pub use domain::{
    GlobalRegionId, MemoryProtectionFlags, MemoryRegionDescriptor, NodeId,
    PageCoherenceState,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_node_id_equality() {
        let node_a = NodeId(1);
        let node_b = NodeId(1);
        let node_c = NodeId(2);

        assert_eq!(node_a, node_b);
        assert_ne!(node_a, node_c);
    }

    #[test]
    fn test_memory_protection_flags() {
        let prot = MemoryProtectionFlags::READ | MemoryProtectionFlags::WRITE;
        assert!((prot & MemoryProtectionFlags::READ) != 0);
        assert!((prot & MemoryProtectionFlags::EXEC) == 0);
    }
}
