// virthub/src/master/src/raft_state.rs

use klnk_core::domain::NodeId;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use thiserror::Error;
use tokio::sync::RwLock;
use virthub_config::VirthubConfig;

/// Role state of a Raft consensus node in the metadata cluster.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum RaftRole {
    Follower,
    Candidate,
    Leader,
}

/// Errors originating from Raft state transitions or log operations.
#[derive(Debug, Error)]
pub enum RaftStateError {
    #[error("Node {0} is not the cluster leader (Current Leader: {1:?})")]
    NotLeader(NodeId, Option<NodeId>),

    #[error("Term mismatch: request term {request_term} is lower than current term {current_term}")]
    StaleTerm { request_term: u64, current_term: u64 },

    #[error("Log entry index {index} out of bounds (last log index: {last_index})")]
    LogIndexOutOfBounds { index: u64, last_index: u64 },

    #[error("Log entry uncommitted at index {0}")]
    UncommittedLogIndex(u64),

    #[error("Failed to parse peer string '{0}' to numeric NodeId")]
    InvalidPeerFormat(String),
}

/// Raft consensus configuration settings loaded from [master.raft]
#[derive(Debug, Clone)]
pub struct RaftConfig {
    pub embedded: bool,
    pub initial_peers: Vec<NodeId>,
    pub etcd_endpoints: Vec<String>,
}

impl RaftConfig {
    /// Constructs default Raft settings
    pub fn default_config(local_node_id: NodeId) -> Self {
        Self {
            embedded: true,
            initial_peers: vec![local_node_id],
            etcd_endpoints: vec!["http://127.0.0.1:2379".to_string()],
        }
    }

    /// Parses Raft settings directly from virthub.toml
    pub fn from_app_config(config: &VirthubConfig) -> Result<Self, RaftStateError> {
        let mut initial_peers = Vec::new();
        for peer_str in &config.master.raft.initial_peers {
            let id_num = peer_str
                .trim_start_matches("node-")
                .parse::<u64>()
                .map_err(|_| RaftStateError::InvalidPeerFormat(peer_str.clone()))?;
            initial_peers.push(NodeId(id_num));
        }

        Ok(Self {
            embedded: config.master.raft.embedded,
            initial_peers,
            etcd_endpoints: config.master.raft.etcd_endpoints.clone(),
        })
    }
}

/// A single log entry within the Raft consensus log.
///
/// The enum now includes variants for distributed lock operations, which are
/// proposed by the leader and replicated to all peers. When committed, the
/// daemon applies them to the local control plane's lock table.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum RaftLogEntry {
    /// Generic state machine command (e.g., page state change).
    Command {
        index: u64,
        term: u64,
        payload: Vec<u8>,
    },
    /// Acquire a distributed lock on a resource.
    LockAcquire {
        resource_id: u64,
        client_pid: u32,
    },
    /// Release a distributed lock on a resource.
    LockRelease {
        resource_id: u64,
        client_pid: u32,
    },
}

impl RaftLogEntry {
    /// Returns the term of this entry, regardless of variant.
    pub fn term(&self) -> u64 {
        match self {
            RaftLogEntry::Command { term, .. } => *term,
            // Lock entries are proposed in the current term; the actual term is
            // stored in the Raft log index. We return 0 here because the term
            // is not stored in the variant itself – it is set by the leader
            // when appending. For simplicity, we omit term from these variants.
            // If needed, we can add a term field, but the leader always sets
            // the log entry's term in the log array.
            _ => 0,
        }
    }

    /// Set the term of this entry (used when appending).
    pub fn set_term(&mut self, term: u64) {
        match self {
            RaftLogEntry::Command { term: t, .. } => *t = term,
            // For lock entries, we cannot store term inside; but the leader's
            // log array index implies the term. This method is a no-op for them.
            _ => {}
        }
    }
}

/// State snapshot metadata for fast node catch-up.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RaftSnapshot {
    pub last_included_index: u64,
    pub last_included_term: u64,
    pub data: Vec<u8>,
}

/// In-memory state structure for a Raft consensus node.
#[derive(Debug)]
pub struct RaftNodeState {
    /// Local node ID
    pub node_id: NodeId,
    /// Current role in cluster
    pub role: RaftRole,
    /// Current term number (monotonically increasing)
    pub current_term: u64,
    /// Candidate node ID that received vote in current term
    pub voted_for: Option<NodeId>,
    /// Active leader node ID
    pub leader_id: Option<NodeId>,
    /// Consensus log array
    pub log: Vec<RaftLogEntry>,
    /// Highest log index known to be committed
    pub commit_index: u64,
    /// Highest log index applied to state machine
    pub last_applied: u64,
    /// Known cluster peer node IDs
    pub peers: Vec<NodeId>,
    /// For leaders: for each peer node, index of next log entry to send (NodeId -> u64)
    pub next_index: HashMap<NodeId, u64>,
    /// For leaders: for each peer node, highest index known to be replicated (NodeId -> u64)
    pub match_index: HashMap<NodeId, u64>,
    /// Last snapshot applied (if any)
    pub last_snapshot: Option<RaftSnapshot>,
}

/// Type alias for a callback that is invoked when entries are committed.
/// The callback receives the list of newly committed entries.
pub type CommitCallback = Arc<dyn Fn(&[RaftLogEntry]) + Send + Sync>;

/// Thread-safe Raft Consensus Engine managing state transitions and log replication.
#[derive(Debug)]
pub struct RaftEngine {
    config: RaftConfig,
    state: Arc<RwLock<RaftNodeState>>,
    term_counter: AtomicU64,
    /// Optional callback invoked after advancing the commit index.
    commit_callback: RwLock<Option<CommitCallback>>,
}

impl RaftEngine {
    /// Instantiates a new Raft state engine initialized with explicit parameters in `Follower` role.
    pub fn new(node_id: NodeId, config: RaftConfig) -> Arc<Self> {
        let initial_peers = config.initial_peers.clone();

        let initial_state = RaftNodeState {
            node_id,
            role: RaftRole::Follower,
            current_term: 0,
            voted_for: None,
            leader_id: None,
            log: Vec::new(),
            commit_index: 0,
            last_applied: 0,
            peers: initial_peers,
            next_index: HashMap::new(),
            match_index: HashMap::new(),
            last_snapshot: None,
        };

        Arc::new(Self {
            config,
            state: Arc::new(RwLock::new(initial_state)),
            term_counter: AtomicU64::new(0),
            commit_callback: RwLock::new(None),
        })
    }

    /// Instantiates a new Raft state engine directly from `VirthubConfig`.
    pub fn from_config(
        node_id: NodeId,
        config: &VirthubConfig,
    ) -> Result<Arc<Self>, RaftStateError> {
        let raft_cfg = RaftConfig::from_app_config(config)?;
        Ok(Self::new(node_id, raft_cfg))
    }

    /// Register a callback to be invoked whenever the commit index advances.
    /// The callback receives the newly committed entries that have not yet been applied.
    pub fn set_commit_callback(&self, cb: CommitCallback) {
        let mut guard = self.commit_callback.write().unwrap();
        *guard = Some(cb);
    }

    /// Propose a generic command payload (Leader only).
    pub async fn propose_command(&self, payload: Vec<u8>) -> Result<u64, RaftStateError> {
        let mut state = self.state.write().await;

        if state.role != RaftRole::Leader {
            return Err(RaftStateError::NotLeader(state.node_id, state.leader_id));
        }

        let new_index = state.log.len() as u64 + 1;
        let term = state.current_term;
        let mut entry = RaftLogEntry::Command {
            index: new_index,
            term,
            payload,
        };
        state.log.push(entry);
        Ok(new_index)
    }

    /// Propose a distributed lock acquisition (Leader only).
    pub async fn propose_lock_acquire(
        &self,
        resource_id: u64,
        client_pid: u32,
    ) -> Result<u64, RaftStateError> {
        let mut state = self.state.write().await;

        if state.role != RaftRole::Leader {
            return Err(RaftStateError::NotLeader(state.node_id, state.leader_id));
        }

        let new_index = state.log.len() as u64 + 1;
        let entry = RaftLogEntry::LockAcquire {
            resource_id,
            client_pid,
        };

        state.log.push(entry);
        Ok(new_index)
    }

    /// Propose a distributed lock release (Leader only).
    pub async fn propose_lock_release(
        &self,
        resource_id: u64,
        client_pid: u32,
    ) -> Result<u64, RaftStateError> {
        let mut state = self.state.write().await;

        if state.role != RaftRole::Leader {
            return Err(RaftStateError::NotLeader(state.node_id, state.leader_id));
        }

        let new_index = state.log.len() as u64 + 1;
        let entry = RaftLogEntry::LockRelease {
            resource_id,
            client_pid,
        };

        state.log.push(entry);
        Ok(new_index)
    }

    /// Processes an election RequestVote RPC from a candidate node.
    pub async fn handle_request_vote(
        &self,
        candidate_id: NodeId,
        term: u64,
        last_log_index: u64,
        last_log_term: u64,
    ) -> Result<bool, RaftStateError> {
        let mut state = self.state.write().await;

        if term < state.current_term {
            return Err(RaftStateError::StaleTerm {
                request_term: term,
                current_term: state.current_term,
            });
        }

        if term > state.current_term {
            state.current_term = term;
            state.role = RaftRole::Follower;
            state.voted_for = None;
            state.leader_id = None;
            self.term_counter.store(term, Ordering::Relaxed);
        }

        let last_index = state.log.len() as u64;
        let last_term = state.log.last().map(|e| e.term()).unwrap_or(0);

        let log_is_up_to_date = last_log_term > last_term
            || (last_log_term == last_term && last_log_index >= last_index);

        let can_vote = (state.voted_for.is_none() || state.voted_for == Some(candidate_id))
            && log_is_up_to_date;

        if can_vote {
            state.voted_for = Some(candidate_id);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Handles AppendEntries RPC from leader (or heartbeat).
    /// Returns true if successful, false if log inconsistency.
    pub async fn handle_append_entries(
        &self,
        leader_id: NodeId,
        term: u64,
        prev_log_index: u64,
        prev_log_term: u64,
        entries: Vec<RaftLogEntry>,
        leader_commit: u64,
    ) -> Result<bool, RaftStateError> {
        let mut state = self.state.write().await;

        // Reject if term is stale.
        if term < state.current_term {
            return Ok(false);
        }

        // Update term and recognize leader if term is newer.
        if term > state.current_term {
            state.current_term = term;
            state.role = RaftRole::Follower;
            self.term_counter.store(term, Ordering::Relaxed);
        }

        state.leader_id = Some(leader_id);
        state.voted_for = None; // Reset vote after hearing from a leader

        // Check log consistency.
        if prev_log_index > 0 {
            if prev_log_index > state.log.len() as u64 {
                return Ok(false);
            }
            let prev_entry = &state.log[(prev_log_index - 1) as usize];
            if prev_entry.term() != prev_log_term {
                // Conflict: delete conflicting entries and everything after.
                state.log.truncate((prev_log_index - 1) as usize);
                return Ok(false);
            }
        } else {
            // prev_log_index == 0 means leader's log is empty before these entries.
            // No consistency check needed.
        }

        // Append new entries (if any).
        for entry in entries {
            let index = state.log.len() as u64 + 1;
            let mut entry = entry;
            entry.set_term(term);
            state.log.push(entry);
        }

        // Update commit index.
        if leader_commit > state.commit_index {
            state.commit_index = leader_commit.min(state.log.len() as u64);
        }

        Ok(true)
    }

    /// Transitions node state to Leader upon winning an election.
    pub async fn promote_to_leader(&self) {
        let mut state = self.state.write().await;
        state.role = RaftRole::Leader;
        state.leader_id = Some(state.node_id);

        let last_log_idx = state.log.len() as u64;
        let peer_nodes = state.peers.clone();

        for node in peer_nodes {
            if node != state.node_id {
                state.next_index.insert(node, last_log_idx + 1);
                state.match_index.insert(node, 0);
            }
        }
    }

    /// Starts an election (increments term, votes for self, transitions to Candidate).
    pub async fn start_election(&self) -> Result<(), RaftStateError> {
        let mut state = self.state.write().await;
        state.current_term += 1;
        state.role = RaftRole::Candidate;
        state.voted_for = Some(state.node_id);
        state.leader_id = None;
        self.term_counter.store(state.current_term, Ordering::Relaxed);
        Ok(())
    }

    /// Advances the commit index when a majority of nodes replicate a log entry.
    /// If a commit callback is registered, it is invoked with the newly committed entries.
    pub async fn commit_to_index(&self, target_index: u64) -> Result<(), RaftStateError> {
        let mut state = self.state.write().await;

        if target_index > state.log.len() as u64 {
            return Err(RaftStateError::LogIndexOutOfBounds {
                index: target_index,
                last_index: state.log.len() as u64,
            });
        }

        if target_index > state.commit_index {
            let old_commit = state.commit_index;
            state.commit_index = target_index;

            // Invoke commit callback if present and if there are new entries.
            if let Some(ref cb) = *self.commit_callback.read().unwrap() {
                let new_entries: Vec<RaftLogEntry> = state.log
                    [(old_commit as usize)..(target_index as usize)]
                    .to_vec();
                if !new_entries.is_empty() {
                    cb(&new_entries);
                }
            }
        }

        Ok(())
    }

    /// Returns current role of this node.
    pub async fn role(&self) -> RaftRole {
        self.state.read().await.role
    }

    /// Returns current term number.
    pub fn current_term(&self) -> u64 {
        self.term_counter.load(Ordering::Relaxed)
    }

    /// Returns active leader ID if known.
    pub async fn leader_id(&self) -> Option<NodeId> {
        self.state.read().await.leader_id
    }

    /// Returns Raft configuration parameters.
    pub fn config(&self) -> &RaftConfig {
        &self.config
    }

    /// Returns a clone of the current log (for debugging or snapshotting).
    pub async fn get_log(&self) -> Vec<RaftLogEntry> {
        self.state.read().await.log.clone()
    }

    /// Returns the last log index.
    pub async fn last_log_index(&self) -> u64 {
        self.state.read().await.log.len() as u64
    }

    /// Returns the last log term.
    pub async fn last_log_term(&self) -> u64 {
        self.state.read().await.log.last().map(|e| e.term()).unwrap_or(0)
    }

    /// Persist current state to disk (simple JSON for now).
    /// In a production system, this would use a more efficient binary format.
    pub async fn save_state(&self, path: &Path) -> Result<(), RaftStateError> {
        let state = self.state.read().await;
        let snapshot = RaftSnapshot {
            last_included_index: state.log.len() as u64,
            last_included_term: state.current_term,
            data: serde_json::to_vec(&*state)?,
        };
        let serialized = serde_json::to_string_pretty(&snapshot)
            .map_err(|e| RaftStateError::Internal(e.to_string()))?;
        std::fs::write(path, serialized)
            .map_err(|e| RaftStateError::Internal(e.to_string()))?;
        Ok(())
    }

    /// Load state from disk.
    pub async fn load_state(&self, path: &Path) -> Result<(), RaftStateError> {
        let data = std::fs::read_to_string(path)
            .map_err(|e| RaftStateError::Internal(e.to_string()))?;
        let snapshot: RaftSnapshot = serde_json::from_str(&data)
            .map_err(|e| RaftStateError::Internal(e.to_string()))?;
        let state: RaftNodeState = serde_json::from_slice(&snapshot.data)
            .map_err(|e| RaftStateError::Internal(e.to_string()))?;
        *self.state.write().await = state;
        self.term_counter.store(state.current_term, Ordering::Relaxed);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_raft_initialization_and_vote() {
        let node1 = NodeId(1);
        let node2 = NodeId(2);
        let config = RaftConfig::default_config(node1);
        let engine = RaftEngine::new(node1, config);

        assert_eq!(engine.role().await, RaftRole::Follower);
        assert_eq!(engine.current_term(), 0);

        let vote_granted = engine
            .handle_request_vote(node2, 1, 0, 0)
            .await
            .expect("RequestVote should succeed");

        assert!(vote_granted);
        assert_eq!(engine.current_term(), 1);
    }

    #[tokio::test]
    async fn test_propose_and_commit_command() {
        let node1 = NodeId(1);
        let node2 = NodeId(2);
        let mut config = RaftConfig::default_config(node1);
        config.initial_peers = vec![node1, node2];

        let engine = RaftEngine::new(node1, config);

        // Attempting to propose as follower should fail
        let err = engine.propose_command(vec![1, 2, 3]).await;
        assert!(matches!(err, Err(RaftStateError::NotLeader(..))));

        // Promote to leader
        engine.promote_to_leader().await;
        assert_eq!(engine.role().await, RaftRole::Leader);

        // Propose command as leader
        let index = engine
            .propose_command(vec![1, 2, 3])
            .await
            .expect("Propose should succeed as leader");
        assert_eq!(index, 1);

        engine.commit_to_index(1).await.expect("Commit should succeed");
    }

    #[tokio::test]
    async fn test_propose_lock_operations() {
        let node1 = NodeId(1);
        let config = RaftConfig::default_config(node1);
        let engine = RaftEngine::new(node1, config);

        // Promote to leader so we can propose
        engine.promote_to_leader().await;

        let res_id = 0xABCD;
        let pid = 100;

        // Propose lock acquire
        let idx = engine.propose_lock_acquire(res_id, pid).await.unwrap();
        assert_eq!(idx, 1);

        // Propose lock release
        let idx2 = engine.propose_lock_release(res_id, pid).await.unwrap();
        assert_eq!(idx2, 2);

        // Commit and verify the callback receives the entries
        let committed_entries = Arc::new(tokio::sync::Mutex::new(Vec::new()));
        let entries_clone = committed_entries.clone();
        engine.set_commit_callback(Arc::new(move |entries| {
            let mut guard = entries_clone.blocking_lock();
            guard.extend_from_slice(entries);
        }));

        engine.commit_to_index(2).await.unwrap();

        let guard = committed_entries.lock().await;
        assert_eq!(guard.len(), 2);
        assert!(matches!(guard[0], RaftLogEntry::LockAcquire { resource_id, client_pid } if resource_id == res_id && client_pid == pid));
        assert!(matches!(guard[1], RaftLogEntry::LockRelease { resource_id, client_pid } if resource_id == res_id && client_pid == pid));
    }

    #[tokio::test]
    async fn test_handle_append_entries() {
        let node1 = NodeId(1);
        let node2 = NodeId(2);
        let config = RaftConfig::default_config(node1);
        let engine = RaftEngine::new(node1, config);

        // Simulate leader sending entries.
        let entries = vec![
            RaftLogEntry::Command { index: 1, term: 1, payload: vec![1,2,3] },
            RaftLogEntry::Command { index: 2, term: 1, payload: vec![4,5,6] },
        ];

        let success = engine
            .handle_append_entries(node2, 1, 0, 0, entries, 2)
            .await
            .expect("AppendEntries should succeed");
        assert!(success);
        assert_eq!(engine.last_log_index().await, 2);
        assert_eq!(engine.current_term(), 1);
    }

    #[tokio::test]
    async fn test_start_election() {
        let node1 = NodeId(1);
        let config = RaftConfig::default_config(node1);
        let engine = RaftEngine::new(node1, config);

        engine.start_election().await.unwrap();
        assert_eq!(engine.role().await, RaftRole::Candidate);
        assert_eq!(engine.current_term(), 1);
    }
}
