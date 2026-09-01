// src/master/src/scheduler.rs

use dashmap::DashMap;
use klnk_core::domain::NodeId;
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use thiserror::Error;
use virthub_config::VirthubConfig;

/// Scheduling policy strategy for placement of new shared memory regions.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum SchedulingPolicy {
    /// Least-loaded node first based on allocated memory ratio
    LeastLoaded,
    /// Round-robin distribution across available nodes
    RoundRobin,
    /// Strict NUMA affinity matching the caller's target NUMA socket
    NumaAffinity,
}

/// Errors originating from cluster placement and scheduling operations.
#[derive(Debug, Error)]
pub enum SchedulerError {
    #[error("No suitable cluster node available for memory allocation (requested size: {requested_bytes} bytes)")]
    NoNodeAvailable { requested_bytes: usize },

    #[error("Node {0} not found in scheduler cluster registry")]
    NodeNotFound(NodeId),

    #[error("Node {node_id} capacity exceeded: available {available_bytes} bytes, requested {requested_bytes} bytes")]
    CapacityExceeded {
        node_id: NodeId,
        available_bytes: u64,
        requested_bytes: usize,
    },
}

/// Scheduler parameters loaded from [master.scheduler] and [master.sharding]
#[derive(Debug, Clone)]
pub struct SchedulerConfig {
    pub prefetch_window: usize,
    pub l0_promote_threshold: u64,
    pub l1_demote_idle_secs: u64,
    pub lru_decay: f64,
    pub shard_count: usize,
}

impl Default for SchedulerConfig {
    fn default() -> Self {
        Self {
            prefetch_window: 8,
            l0_promote_threshold: 100,
            l1_demote_idle_secs: 60,
            lru_decay: 0.8,
            shard_count: 64,
        }
    }
}

impl SchedulerConfig {
    pub fn from_app_config(config: &VirthubConfig) -> Self {
        Self {
            prefetch_window: config.master.scheduler.prefetch_window,
            l0_promote_threshold: config.master.scheduler.l0_promote_threshold,
            l1_demote_idle_secs: config.master.scheduler.l1_demote_idle_secs,
            lru_decay: config.master.scheduler.lru_decay,
            shard_count: config.master.sharding.shard_count,
        }
    }
}

/// Node resource metrics tracked by the scheduler.
#[derive(Debug, Clone)]
pub struct NodeResourceCapacity {
    pub node_id: NodeId,
    pub total_memory_bytes: u64,
    pub allocated_memory_bytes: Arc<AtomicU64>,
    pub numa_nodes: u32,
    pub active_rdma_qps: u32,
    pub is_online: bool,
}

impl NodeResourceCapacity {
    pub fn new(node_id: NodeId, total_memory_bytes: u64, numa_nodes: u32) -> Self {
        Self {
            node_id,
            total_memory_bytes,
            allocated_memory_bytes: Arc::new(AtomicU64::new(0)),
            numa_nodes,
            active_rdma_qps: 0,
            is_online: true,
        }
    }

    pub fn available_memory_bytes(&self) -> u64 {
        let allocated = self.allocated_memory_bytes.load(Ordering::Relaxed);
        if self.total_memory_bytes > allocated {
            self.total_memory_bytes - allocated
        } else {
            0
        }
    }

    pub fn load_ratio(&self) -> f64 {
        if self.total_memory_bytes == 0 {
            1.0
        } else {
            self.allocated_memory_bytes.load(Ordering::Relaxed) as f64 / self.total_memory_bytes as f64
        }
    }
}

/// Placement decision result returned by the scheduler.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PlacementDecision {
    pub selected_node_id: NodeId,
    pub target_numa_node: u32,
    pub allocated_vaddr: u64,
    pub size_bytes: usize,
    pub shard_id: usize,
}

/// Master Cluster Memory Scheduler.
///
/// Performance optimizations:
/// - Nodes stored in a `DashMap` for concurrent access.
/// - `schedule_allocation` avoids cloning `NodeResourceCapacity` by extracting
///   only the necessary fields (node id, available memory, load ratio, numa count)
///   into a lightweight tuple. This reduces allocation overhead.
/// - Round‑robin index is atomic, ensuring thread safety without locks.
#[derive(Debug)]
pub struct ClusterScheduler {
    policy: SchedulingPolicy,
    config: SchedulerConfig,
    nodes: DashMap<NodeId, NodeResourceCapacity>,
    next_rr_index: AtomicU64,
}

impl ClusterScheduler {
    /// Instantiates a new `ClusterScheduler` with explicit policy and config parameters.
    pub fn new(policy: SchedulingPolicy, config: SchedulerConfig) -> Arc<Self> {
        Arc::new(Self {
            policy,
            config,
            nodes: DashMap::new(),
            next_rr_index: AtomicU64::new(0),
        })
    }

    /// Instantiates a new `ClusterScheduler` directly from `VirthubConfig`.
    pub fn from_config(config: &VirthubConfig) -> Arc<Self> {
        let sched_config = SchedulerConfig::from_app_config(config);
        Self::new(SchedulingPolicy::LeastLoaded, sched_config)
    }

    /// Registers or updates a node's resource availability in the cluster registry.
    pub fn register_node(&self, capacity: NodeResourceCapacity) {
        self.nodes.insert(capacity.node_id, capacity);
    }

    /// Deregisters a node on disconnect or fault detection.
    pub fn unregister_node(&self, node_id: &NodeId) {
        self.nodes.remove(node_id);
    }

    /// Evaluates cluster state and selects the optimal node for allocating `size_bytes`.
    ///
    /// The function collects only essential metrics from each node, avoiding
    /// cloning the entire `NodeResourceCapacity`. After selecting a node, it
    /// atomically increments that node's allocated memory counter.
    pub fn schedule_allocation(
        &self,
        size_bytes: usize,
        target_numa_hint: Option<u32>,
    ) -> Result<PlacementDecision, SchedulerError> {
        // Collect lightweight candidate info: (node_id, available_mem, load_ratio, numa_nodes)
        let mut candidates: Vec<(NodeId, u64, f64, u32)> = Vec::new();

        for entry in self.nodes.iter() {
            let capacity = entry.value();
            if !capacity.is_online {
                continue;
            }
            let available = capacity.available_memory_bytes();
            if available < size_bytes as u64 {
                continue;
            }

            // For NUMA affinity, filter based on hint if provided.
            if let Some(hint) = target_numa_hint {
                if capacity.numa_nodes <= hint {
                    continue;
                }
            }

            candidates.push((
                capacity.node_id,
                available,
                capacity.load_ratio(),
                capacity.numa_nodes,
            ));
        }

        if candidates.is_empty() {
            return Err(SchedulerError::NoNodeAvailable {
                requested_bytes: size_bytes,
            });
        }

        // Select node based on policy.
        let selected_id = match self.policy {
            SchedulingPolicy::LeastLoaded => {
                candidates
                    .iter()
                    .min_by(|a, b| a.2.partial_cmp(&b.2).unwrap_or(std::cmp::Ordering::Equal))
                    .map(|c| c.0)
                    .unwrap()
            }
            SchedulingPolicy::RoundRobin => {
                let idx = (self.next_rr_index.fetch_add(1, Ordering::Relaxed) as usize) % candidates.len();
                candidates[idx].0
            }
            SchedulingPolicy::NumaAffinity => {
                // If hint is Some, we already filtered; pick first (lowest index).
                candidates[0].0
            }
        };

        // Atomically reserve memory on the selected node.
        if let Some(node_ref) = self.nodes.get(&selected_id) {
            node_ref
                .allocated_memory_bytes
                .fetch_add(size_bytes as u64, Ordering::AcqRel);
        } else {
            // Node disappeared between selection and update (rare race)
            return Err(SchedulerError::NodeNotFound(selected_id));
        }

        // Calculate target shard partition ID based on configured shard_count.
        let shard_id = (selected_id.0 as usize) % self.config.shard_count;

        // Base allocation virtual address generation (64-bit aligned).
        let allocated_vaddr = 0x7fff_0000_0000u64 + (selected_id.0 * 0x10_0000_0000);
        let target_numa_node = target_numa_hint.unwrap_or(0);

        Ok(PlacementDecision {
            selected_node_id: selected_id,
            target_numa_node,
            allocated_vaddr,
            size_bytes,
            shard_id,
        })
    }

    /// Releases memory allocation reservation on a node when a region is destroyed.
    pub fn release_allocation(&self, node_id: NodeId, size_bytes: usize) {
        if let Some(node_ref) = self.nodes.get(&node_id) {
            let current = node_ref.allocated_memory_bytes.load(Ordering::Relaxed);
            if current >= size_bytes as u64 {
                node_ref
                    .allocated_memory_bytes
                    .fetch_sub(size_bytes as u64, Ordering::AcqRel);
            } else {
                node_ref.allocated_memory_bytes.store(0, Ordering::Release);
            }
        }
    }

    /// Returns scheduler configuration settings.
    pub fn config(&self) -> &SchedulerConfig {
        &self.config
    }

    /// Returns the current total active node count in the cluster.
    pub fn node_count(&self) -> usize {
        self.nodes.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_least_loaded_scheduling_with_config() {
        let config = SchedulerConfig::default();
        let scheduler = ClusterScheduler::new(SchedulingPolicy::LeastLoaded, config);

        let node1 = NodeResourceCapacity::new(NodeId(1), 10 * 1024 * 1024 * 1024, 2); // 10 GB
        let node2 = NodeResourceCapacity::new(NodeId(2), 10 * 1024 * 1024 * 1024, 2); // 10 GB

        // Pre-allocate 4GB on node1
        node1.allocated_memory_bytes.store(4 * 1024 * 1024 * 1024, Ordering::Relaxed);

        scheduler.register_node(node1);
        scheduler.register_node(node2);

        // Schedule 1GB allocation: node2 has 0% load vs node1's 40% load -> node2 should be chosen
        let decision = scheduler
            .schedule_allocation(1 * 1024 * 1024 * 1024, None)
            .expect("Scheduling should succeed");

        assert_eq!(decision.selected_node_id, NodeId(2));
        assert_eq!(decision.shard_id, 2 % 64);
    }

    #[test]
    fn test_round_robin_scheduling() {
        let config = SchedulerConfig::default();
        let scheduler = ClusterScheduler::new(SchedulingPolicy::RoundRobin, config);

        let node1 = NodeResourceCapacity::new(NodeId(1), 10 * 1024 * 1024 * 1024, 1);
        let node2 = NodeResourceCapacity::new(NodeId(2), 10 * 1024 * 1024 * 1024, 1);

        scheduler.register_node(node1);
        scheduler.register_node(node2);

        let d1 = scheduler.schedule_allocation(1024, None).unwrap();
        let d2 = scheduler.schedule_allocation(1024, None).unwrap();

        assert_ne!(d1.selected_node_id, d2.selected_node_id);
    }

    #[test]
    fn test_no_capacity_error() {
        let config = SchedulerConfig::default();
        let scheduler = ClusterScheduler::new(SchedulingPolicy::LeastLoaded, config);
        let node1 = NodeResourceCapacity::new(NodeId(1), 1024, 1); // 1 KB total
        scheduler.register_node(node1);

        // Request 1 MB -> should fail with NoNodeAvailable
        let res = scheduler.schedule_allocation(1024 * 1024, None);
        assert!(matches!(res, Err(SchedulerError::NoNodeAvailable { .. })));
    }

    #[test]
    fn test_numa_affinity_scheduling() {
        let config = SchedulerConfig::default();
        let scheduler = ClusterScheduler::new(SchedulingPolicy::NumaAffinity, config);

        let node1 = NodeResourceCapacity::new(NodeId(1), 10 * 1024 * 1024 * 1024, 4); // 4 NUMA nodes
        let node2 = NodeResourceCapacity::new(NodeId(2), 10 * 1024 * 1024 * 1024, 2); // 2 NUMA nodes

        scheduler.register_node(node1);
        scheduler.register_node(node2);

        // Request with NUMA hint 3: only node1 qualifies (4 > 3)
        let decision = scheduler
            .schedule_allocation(1024, Some(3))
            .expect("Scheduling should succeed");
        assert_eq!(decision.selected_node_id, NodeId(1));
    }
}
