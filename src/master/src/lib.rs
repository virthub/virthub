// virthub/src/master/src/lib.rs

pub mod coordinator;

// Re-export primary coordinator types for workspace consumption
pub use coordinator::{
    MasterCoordinator, MasterCoordinatorConfig, MasterError, NodeDescriptor,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_master_coordinator_config_defaults() {
        let config = MasterCoordinatorConfig::default();
        assert!(config.heartbeat_timeout_ms > 0);
        assert!(!config.listen_addr.is_empty());
    }
}
