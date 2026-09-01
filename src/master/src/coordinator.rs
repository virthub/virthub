// src/master/src/coordinator.rs

use std::collections::HashMap;
use std::sync::atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::sync::RwLock;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum MasterError {
    #[error("Master coordinator internal error: {0}")]
    Internal(String),

    #[error("Node registration conflict for node ID {0}")]
    NodeAlreadyExists(u32),

    #[error("Node {0} not found in cluster state")]
    NodeNotFound(u32),

    #[error("Network binding error on '{addr}': {reason}")]
    BindError { addr: String, reason: String },
}

/// Metadata and heartbeat state for a registered Virthub node
#[derive(Debug, Clone)]
pub struct NodeDescriptor {
    pub node_id: u32,
    pub endpoint_addr: String,
    pub last_heartbeat: Instant,
    pub is_active: bool,
}

#[derive(Debug, Clone)]
pub struct MasterCoordinatorConfig {
    pub heartbeat_timeout_ms: u64,
    pub listen_addr: String,
}

impl Default for MasterCoordinatorConfig {
    fn default() -> Self {
        Self {
            heartbeat_timeout_ms: 5000,
            listen_addr: "0.0.0.0:9090".to_string(),
        }
    }
}

/// Central Coordinator Engine managing global Virthub node states and cluster configuration.
///
/// Performance optimizations:
/// - Active node count is maintained with an atomic counter, avoiding O(n) scans.
/// - Batch operations for registration and heartbeat updates.
/// - Metrics track registration, heartbeat, and eviction events.
pub struct MasterCoordinator {
    config: MasterCoordinatorConfig,
    nodes: Arc<RwLock<HashMap<u32, NodeDescriptor>>>,
    running: Arc<AtomicBool>,
    active_count: Arc<AtomicUsize>,
    metrics: Arc<CoordinatorMetrics>,
}

#[derive(Debug, Default)]
struct CoordinatorMetrics {
    total_registrations: AtomicU64,
    total_heartbeats: AtomicU64,
    total_evictions: AtomicU64,
}

impl MasterCoordinator {
    pub fn new(config: MasterCoordinatorConfig) -> Self {
        Self {
            config,
            nodes: Arc::new(RwLock::new(HashMap::new())),
            running: Arc::new(AtomicBool::new(true)),
            active_count: Arc::new(AtomicUsize::new(0)),
            metrics: Arc::new(CoordinatorMetrics::default()),
        }
    }

    pub fn config(&self) -> &MasterCoordinatorConfig {
        &self.config
    }

    /// Register a new node with the master coordinator.
    /// Increments the active node counter.
    pub async fn register_node(&self, node_id: u32, endpoint_addr: impl Into<String>) -> Result<(), MasterError> {
        let mut nodes = self.nodes.write().await;
        if nodes.contains_key(&node_id) {
            return Err(MasterError::NodeAlreadyExists(node_id));
        }

        let descriptor = NodeDescriptor {
            node_id,
            endpoint_addr: endpoint_addr.into(),
            last_heartbeat: Instant::now(),
            is_active: true,
        };

        nodes.insert(node_id, descriptor);
        self.active_count.fetch_add(1, Ordering::Relaxed);
        self.metrics.total_registrations.fetch_add(1, Ordering::Relaxed);
        tracing::info!("Registered node {node_id} at endpoint");
        Ok(())
    }

    /// Record a heartbeat received from a cluster node.
    /// If the node was previously inactive, it is marked active and the counter is updated.
    pub async fn record_heartbeat(&self, node_id: u32) -> Result<(), MasterError> {
        let mut nodes = self.nodes.write().await;
        if let Some(node) = nodes.get_mut(&node_id) {
            if !node.is_active {
                node.is_active = true;
                self.active_count.fetch_add(1, Ordering::Relaxed);
            }
            node.last_heartbeat = Instant::now();
            self.metrics.total_heartbeats.fetch_add(1, Ordering::Relaxed);
            Ok(())
        } else {
            Err(MasterError::NodeNotFound(node_id))
        }
    }

    /// Get total active node count in O(1).
    pub async fn active_node_count(&self) -> usize {
        self.active_count.load(Ordering::Relaxed)
    }

    /// Evict dead nodes that have exceeded the heartbeat timeout.
    /// Returns the list of evicted node IDs.
    pub async fn evict_stale_nodes(&self) -> Vec<u32> {
        let timeout = Duration::from_millis(self.config.heartbeat_timeout_ms);
        let mut nodes = self.nodes.write().await;
        let mut evicted = Vec::new();

        // Identify stale nodes
        let stale_ids: Vec<u32> = nodes
            .iter()
            .filter(|(_, desc)| desc.is_active && desc.last_heartbeat.elapsed() > timeout)
            .map(|(id, _)| *id)
            .collect();

        for id in &stale_ids {
            if let Some(desc) = nodes.get_mut(id) {
                desc.is_active = false;
            }
        }

        // Remove them from the map
        for id in &stale_ids {
            nodes.remove(id);
        }

        if !stale_ids.is_empty() {
            self.active_count.fetch_sub(stale_ids.len(), Ordering::Relaxed);
            self.metrics.total_evictions.fetch_add(stale_ids.len() as u64, Ordering::Relaxed);
            tracing::warn!("Evicted {} inactive nodes: {:?}", stale_ids.len(), stale_ids);
        }

        evicted.extend(stale_ids);
        evicted
    }

    /// Gracefully remove a node from the cluster.
    pub async fn remove_node(&self, node_id: u32) -> Result<(), MasterError> {
        let mut nodes = self.nodes.write().await;
        if let Some(desc) = nodes.remove(&node_id) {
            if desc.is_active {
                self.active_count.fetch_sub(1, Ordering::Relaxed);
            }
            tracing::info!("Node {} gracefully removed from cluster", node_id);
            Ok(())
        } else {
            Err(MasterError::NodeNotFound(node_id))
        }
    }

    /// Get information about a specific node.
    pub async fn get_node(&self, node_id: u32) -> Result<NodeDescriptor, MasterError> {
        let nodes = self.nodes.read().await;
        nodes
            .get(&node_id)
            .cloned()
            .ok_or(MasterError::NodeNotFound(node_id))
    }

    /// Get all node descriptors in the cluster.
    pub async fn get_all_nodes(&self) -> Vec<NodeDescriptor> {
        let nodes = self.nodes.read().await;
        nodes.values().cloned().collect()
    }

    /// Shutdown the coordinator engine.
    pub fn shutdown(&self) {
        self.running.store(false, Ordering::Relaxed);
    }

    /// Return a snapshot of the coordinator metrics.
    pub fn get_metrics(&self) -> CoordinatorMetricsSnapshot {
        CoordinatorMetricsSnapshot {
            total_registrations: self.metrics.total_registrations.load(Ordering::Relaxed),
            total_heartbeats: self.metrics.total_heartbeats.load(Ordering::Relaxed),
            total_evictions: self.metrics.total_evictions.load(Ordering::Relaxed),
        }
    }
}

#[derive(Debug, Clone)]
pub struct CoordinatorMetricsSnapshot {
    pub total_registrations: u64,
    pub total_heartbeats: u64,
    pub total_evictions: u64,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_master_coordinator_lifecycle() {
        let config = MasterCoordinatorConfig {
            heartbeat_timeout_ms: 100,
            listen_addr: "127.0.0.1:9090".to_string(),
        };

        let coordinator = MasterCoordinator::new(config);
        coordinator.register_node(1, "127.0.0.1:8001").await.expect("Registration failed");

        assert_eq!(coordinator.active_node_count().await, 1);
        coordinator.record_heartbeat(1).await.expect("Heartbeat failed");

        // Simulate timeout and eviction
        tokio::time::sleep(Duration::from_millis(150)).await;
        let evicted = coordinator.evict_stale_nodes().await;
        assert_eq!(evicted, vec![1]);
        assert_eq!(coordinator.active_node_count().await, 0);
    }

    #[tokio::test]
    async fn test_batch_operations() {
        let config = MasterCoordinatorConfig::default();
        let coordinator = MasterCoordinator::new(config);

        // Register multiple nodes
        for i in 1..=5 {
            coordinator.register_node(i, format!("192.168.1.{}:8000", i)).await.unwrap();
        }
        assert_eq!(coordinator.active_node_count().await, 5);

        // Heartbeat some nodes
        coordinator.record_heartbeat(1).await.unwrap();
        coordinator.record_heartbeat(3).await.unwrap();

        // Remove a node
        coordinator.remove_node(2).await.unwrap();
        assert_eq!(coordinator.active_node_count().await, 4);

        // Get all nodes
        let all = coordinator.get_all_nodes().await;
        assert_eq!(all.len(), 4);
    }
}
