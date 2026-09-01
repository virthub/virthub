// virthub/src/klnk/klnk-ebpf/src/main.rs

//! Standalone eBPF manager binary for Virthub.
//!
//! This binary provides two functions:
//! 1. A region/lock manager (`EbpfManager`) that maintains eBPF‑accelerated
//!    metadata for memory regions and distributed locks.
//! 2. A telemetry manager (`EbpfTraceManager`) that attaches `stride_tracer`
//!    probes, processes memory access events, and generates prefetch recommendations.
//!
//! The telemetry manager now uses the improved `StrideDetector` with a single
//! lock and adaptive confidence decay. The binary spawns a background task to
//! consume `AccessEvent`s from the channel and print prefetch recommendations.
//!
//! Future work: replace the stub `attach_probes` with real eBPF loading using
//! `libbpf-rs`.

use klnk_core::domain::{GlobalRegionId, NodeId};
use klnk_ebpf::EbpfTraceManager;
use std::collections::HashMap;
use std::sync::Mutex;
use thiserror::Error;
use tracing::{error, info, warn};
use virthub_config::VirthubConfig;

#[derive(Debug, Error)]
pub enum EbpfManagerError {
    #[error("Region already registered: {0:?}")]
    RegionAlreadyExists(GlobalRegionId),

    #[error("Region not found: {0:?}")]
    RegionNotFound(GlobalRegionId),

    #[error("Virtual address 0x{vaddr:x} not mapped in region {region_id:?}")]
    AddressNotMapped { vaddr: u64, region_id: GlobalRegionId },

    #[error("Lock acquisition conflict for resource 0x{resource_id:x} by client PID {client_pid}")]
    LockConflict { resource_id: u64, client_pid: u32 },

    #[error("Lock release error: resource 0x{resource_id:x} not held by client PID {client_pid}")]
    LockNotHeld { resource_id: u64, client_pid: u32 },
}

#[derive(Debug, Clone)]
pub struct RegionState {
    pub region_id: GlobalRegionId,
    pub base_vaddr: u64,
    pub size: usize,
    pub owner_node: NodeId,
}

#[derive(Debug, Clone)]
pub struct PageState {
    pub vaddr: u64,
    pub coherence_state: u8,
}

#[derive(Debug, Clone)]
pub struct LockState {
    pub resource_id: u64,
    pub holder_pid: Option<u32>,
}

/// User‑space manager for eBPF‑accelerated memory region and lock states.
/// This is a synchronous controller that maintains maps for eBPF programs.
pub struct EbpfManager {
    regions: Mutex<HashMap<GlobalRegionId, RegionState>>,
    page_states: Mutex<HashMap<u64, PageState>>,
    locks: Mutex<HashMap<u64, LockState>>,
}

impl EbpfManager {
    pub fn new() -> Self {
        Self {
            regions: Mutex::new(HashMap::new()),
            page_states: Mutex::new(HashMap::new()),
            locks: Mutex::new(HashMap::new()),
        }
    }

    /// Register a virtual address region in the eBPF map controller.
    pub fn register_region(&self, region: RegionState) -> Result<(), EbpfManagerError> {
        let mut map = self.regions.lock().unwrap();
        let key = region.region_id;
        if map.contains_key(&key) {
            return Err(EbpfManagerError::RegionAlreadyExists(key));
        }
        map.insert(key, region);
        Ok(())
    }

    /// Unregister a virtual address region.
    pub fn unregister_region(&self, region_id: GlobalRegionId) -> Result<(), EbpfManagerError> {
        let mut map = self.regions.lock().unwrap();
        if map.remove(&region_id).is_none() {
            return Err(EbpfManagerError::RegionNotFound(region_id));
        }
        Ok(())
    }

    /// Fetch region state.
    pub fn get_region(&self, region_id: GlobalRegionId) -> Option<RegionState> {
        self.regions.lock().unwrap().get(&region_id).cloned()
    }

    /// Fetch page state at a specific aligned virtual address.
    pub fn get_page_state(&self, aligned_vaddr: u64) -> Option<PageState> {
        self.page_states.lock().unwrap().get(&aligned_vaddr).cloned()
    }

    /// Acquire a resource lock (synchronous).
    pub fn acquire_lock(&self, resource_id: u64, client_pid: u32) -> Result<(), EbpfManagerError> {
        let mut map = self.locks.lock().unwrap();
        let lock_state = map.entry(resource_id).or_insert_with(|| LockState {
            resource_id,
            holder_pid: None,
        });

        if let Some(current_holder) = lock_state.holder_pid {
            if current_holder != client_pid {
                return Err(EbpfManagerError::LockConflict {
                    resource_id,
                    client_pid,
                });
            }
        }
        lock_state.holder_pid = Some(client_pid);
        Ok(())
    }

    /// Release a resource lock (synchronous).
    pub fn release_lock(&self, resource_id: u64, client_pid: u32) -> Result<(), EbpfManagerError> {
        let mut map = self.locks.lock().unwrap();
        if let Some(lock_state) = map.get_mut(&resource_id) {
            if lock_state.holder_pid == Some(client_pid) {
                lock_state.holder_pid = None;
                Ok(())
            } else {
                Err(EbpfManagerError::LockNotHeld {
                    resource_id,
                    client_pid,
                })
            }
        } else {
            Err(EbpfManagerError::LockNotHeld {
                resource_id,
                client_pid,
            })
        }
    }
}

impl Default for EbpfManager {
    fn default() -> Self {
        Self::new()
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt::init();

    info!("Starting klnk-ebpf manager binary...");

    // Load configuration.
    let config = VirthubConfig::load_default()
        .unwrap_or_else(|_| {
            warn!("Failed to load configuration, using defaults.");
            VirthubConfig::default()
        });

    let manager = EbpfManager::new();
    let region = RegionState {
        region_id: GlobalRegionId {
            owner_pid: 100,
            shmid: 1,
        },
        base_vaddr: 0x7fff_0000_0000,
        size: 2 * 1024 * 1024,
        owner_node: NodeId(1),
    };
    manager.register_region(region)?;
    info!("EbpfManager initialized with a sample region.");

    if config.ebpf.enabled {
        info!("eBPF telemetry is enabled. Initializing trace manager...");
        let (trace_manager, mut event_rx) = EbpfTraceManager::new(config, 1024);

        // Attach probes (falls back to simulation if unavailable).
        if let Err(e) = trace_manager.attach_probes() {
            error!("Failed to attach eBPF probes: {}", e);
            warn!("Continuing in software simulation mode.");
        }

        // Spawn a task to consume events and generate prefetch recommendations.
        let detector = trace_manager.detector().clone();
        tokio::spawn(async move {
            while let Some(event) = event_rx.recv().await {
                if let Some(rec) = detector.process_event(event) {
                    info!(
                        "Prefetch recommendation: PID={}, base=0x{:x}, stride={}, count={}",
                        rec.pid, rec.base_vaddr, rec.stride_bytes, rec.count
                    );
                }
            }
        });

        info!("eBPF telemetry manager is running.");
    } else {
        info!("eBPF telemetry is disabled in configuration.");
    }

    info!("klnk-ebpf manager running. Press Ctrl+C to stop.");
    tokio::signal::ctrl_c()
        .await
        .expect("Failed to listen for shutdown signal");
    info!("Shutting down klnk-ebpf manager.");

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Instant;

    #[test]
    fn test_ebpf_manager_lock_lifecycle() {
        eprintln!(">>> test_ebpf_manager_lock_lifecycle STARTED");
        let start = Instant::now();

        let manager = EbpfManager::new();
        let resource_id = 0xdead_beef_u64;

        // Acquire lock for PID 1001
        manager
            .acquire_lock(resource_id, 1001)
            .expect("Lock acquire should succeed");

        // PID 1002 should fail to acquire
        assert!(manager.acquire_lock(resource_id, 1002).is_err());

        // Release lock by PID 1001
        manager
            .release_lock(resource_id, 1001)
            .expect("Lock release should succeed");

        // Now PID 1002 can acquire
        manager
            .acquire_lock(resource_id, 1002)
            .expect("Second acquire should now succeed");

        let elapsed = start.elapsed();
        eprintln!(">>> Test completed in {:?}", elapsed);
        assert!(elapsed.as_secs() < 2, "Test took too long, possible hang");
        eprintln!(">>> test_ebpf_manager_lock_lifecycle PASSED");
    }

    #[tokio::test]
    async fn test_ebpf_manager_with_telemetry() {
        // Test that the telemetry manager can be created without errors.
        let config = VirthubConfig::default();
        let (manager, _rx) = EbpfTraceManager::new(config, 10);
        assert!(manager.config().ebpf.enabled);
        // Attach probes should not panic.
        let _ = manager.attach_probes();
    }
}
