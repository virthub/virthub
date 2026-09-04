// virthub/src/master/src/scheduler.rs

use dashmap::DashMap;
use klnk_core::domain::NodeId;
use serde::{Deserialize, Serialize};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use thiserror::Error;
use virthub_config::VirthubConfig;

use precision::hysteresis::MemoryPressureState;
use precision::policy::PackedBlockPolicy;
use precision::Predictor;

/// Scheduling policy strategy for placement of new shared memory regions.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum SchedulingPolicy {
    LeastLoaded,
    RoundRobin,
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
            self.allocated_memory_bytes.load(Ordering::Relaxed) as f64
                / self.total_memory_bytes as f64
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
///   only the necessary fields into lightweight tuples.
/// - Round‑robin index is atomic, ensuring thread safety without locks.
///
/// Precision prediction integration:
/// - The scheduler holds an optional `Predictor` for allocation‑time
///   precision decisions.
/// - It maintains a memory‑pressure hysteresis state that can be updated
///   from the free‑block ratio and used to influence predictor output.
#[derive(Debug)]
pub struct ClusterScheduler {
    policy: SchedulingPolicy,
    config: SchedulerConfig,
    nodes: DashMap<NodeId, NodeResourceCapacity>,
    next_rr_index: AtomicU64,
    /// Optional precision predictor (initialised when model layer count is known).
    predictor: parking_lot::RwLock<Option<Predictor>>,
    /// Current memory‑pressure hysteresis state.
    pressure_state: parking_lot::RwLock<MemoryPressureState>,
}

impl ClusterScheduler {
    /// Instantiates a new `ClusterScheduler` with explicit policy and config.
    pub fn new(policy: SchedulingPolicy, config: SchedulerConfig) -> Arc<Self> {
        Arc::new(Self {
            policy,
            config,
            nodes: DashMap::new(),
            next_rr_index: AtomicU64::new(0),
            predictor: parking_lot::RwLock::new(None),
            pressure_state: parking_lot::RwLock::new(MemoryPressureState::Nominal),
        })
    }

    /// Instantiates a new `ClusterScheduler` directly from `VirthubConfig`.
    pub fn from_config(config: &VirthubConfig) -> Arc<Self> {
        let sched_config = SchedulerConfig::from_app_config(config);
        Self::new(SchedulingPolicy::LeastLoaded, sched_config)
    }

    /// Initializes the precision predictor with the given total number of
    /// transformer layers. This is called once the model shape is known.
    pub fn init_predictor(&self, total_layers: usize) {
        let mut guard = self.predictor.write();
        *guard = Some(Predictor::new(total_layers));
    }

    /// Returns true if a predictor has been initialised.
    pub fn has_predictor(&self) -> bool {
        self.predictor.read().is_some()
    }

    /// Updates the memory‑pressure hysteresis state using the current
    /// free‑block / total‑block counts. The pressure ratio `mu` is computed
    /// as `1.0 - free_blocks / total_blocks`, clamped to [0.0, 1.0].
    pub fn update_memory_pressure(&self, free_blocks: usize, total_blocks: usize) {
        if total_blocks == 0 {
            return;
        }
        let mu = 1.0 - (free_blocks as f64 / total_blocks as f64);
        let mut state = self.pressure_state.write();
        *state = state.update(mu);
    }

    /// Returns the current memory‑pressure state.
    pub fn current_pressure_state(&self) -> MemoryPressureState {
        *self.pressure_state.read()
    }

    /// Predicts the packed block policy for a KV block using the allocation‑time
    /// precision predictor, if one is available.
    ///
    /// Returns `None` if the predictor has not been initialised.
    pub fn predict_block_policy(
        &self,
        layer_idx: usize,
        token_start: usize,
        seq_len: usize,
        head_mask: u32,
        retrieval_flag: bool,
    ) -> Option<PackedBlockPolicy> {
        let guard = self.predictor.read();
        if let Some(predictor) = guard.as_ref() {
            let h_mem = self.current_pressure_state();
            Some(predictor.predict(
                layer_idx,
                token_start,
                seq_len,
                h_mem,
                head_mask,
                retrieval_flag,
            ))
        } else {
            None
        }
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
                let idx = (self.next_rr_index.fetch_add(1, Ordering::Relaxed) as usize)
                    % candidates.len();
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
    use klnk_core::domain::NodeId;

    fn make_test_scheduler() -> Arc<ClusterScheduler> {
        let config = SchedulerConfig::default();
        ClusterScheduler::new(SchedulingPolicy::LeastLoaded, config)
    }

    #[test]
    fn test_least_loaded_scheduling_with_config() {
        let scheduler = make_test_scheduler();

        let node1 = NodeResourceCapacity::new(NodeId(1), 10 * 1024 * 1024 * 1024, 2);
        let node2 = NodeResourceCapacity::new(NodeId(2), 10 * 1024 * 1024 * 1024, 2);

        node1.allocated_memory_bytes.store(4 * 1024 * 1024 * 1024, Ordering::Relaxed);

        scheduler.register_node(node1);
        scheduler.register_node(node2);

        let decision = scheduler
            .schedule_allocation(1 * 1024 * 1024 * 1024, None)
            .expect("Scheduling should succeed");

        assert_eq!(decision.selected_node_id, NodeId(2));
        assert_eq!(decision.shard_id, 2 % 64);
    }

    #[test]
    fn test_round_robin_scheduling() {
        let scheduler = ClusterScheduler::new(SchedulingPolicy::RoundRobin, SchedulerConfig::default());

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
        let scheduler = make_test_scheduler();
        let node1 = NodeResourceCapacity::new(NodeId(1), 1024, 1);
        scheduler.register_node(node1);

        let res = scheduler.schedule_allocation(1024 * 1024, None);
        assert!(matches!(res, Err(SchedulerError::NoNodeAvailable { .. })));
    }

    #[test]
    fn test_numa_affinity_scheduling() {
        let scheduler = ClusterScheduler::new(SchedulingPolicy::NumaAffinity, SchedulerConfig::default());

        let node1 = NodeResourceCapacity::new(NodeId(1), 10 * 1024 * 1024 * 1024, 4);
        let node2 = NodeResourceCapacity::new(NodeId(2), 10 * 1024 * 1024 * 1024, 2);

        scheduler.register_node(node1);
        scheduler.register_node(node2);

        let decision = scheduler
            .schedule_allocation(1024, Some(3))
            .expect("Scheduling should succeed");
        assert_eq!(decision.selected_node_id, NodeId(1));
    }

    #[test]
    fn test_memory_pressure_update() {
        let scheduler = make_test_scheduler();

        // Initially Nominal
        assert_eq!(scheduler.current_pressure_state(), MemoryPressureState::Nominal);

        // Simulate high pressure (critical)
        scheduler.update_memory_pressure(10, 100); // mu = 0.90
        assert_eq!(scheduler.current_pressure_state(), MemoryPressureState::Critical);

        // Simulate low pressure
        scheduler.update_memory_pressure(90, 100); // mu = 0.10
        assert_eq!(scheduler.current_pressure_state(), MemoryPressureState::Nominal);
    }

    #[test]
    fn test_predictor_initialisation_and_prediction() {
        let scheduler = make_test_scheduler();
        scheduler.init_predictor(32);
        assert!(scheduler.has_predictor());

        let policy = scheduler
            .predict_block_policy(10, 100, 1000, 0xFFFFFFFF, false)
            .expect("policy should be produced");
        assert_eq!(policy.precision(), precision::policy::PrecisionLevel::Fp8);
    }
}
