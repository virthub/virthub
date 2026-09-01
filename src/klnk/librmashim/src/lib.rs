// virthub/src/klnk/librmashim/src/lib.rs

#![allow(missing_docs)]
//! High‑performance RMA transport shim with RDMA (Verbs) and TCP (`io_uring`) backends.
//!
//! This crate provides a unified interface for one‑sided RDMA operations, memory region
//! registration, atomic operations, and automatic fallback to TCP when RDMA is unavailable.
//!
//! The current implementation uses a **software fallback** provider (no hardware acceleration)
//! that manages metadata and completion counters but does not move actual data. A real
//! Verbs‑based provider can be added later behind a feature flag.

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;
use std::time::Duration;
use thiserror::Error;
use tracing::{info, warn};

mod mr_registry;
mod rdma_provider;
mod rq_refill;
mod socket_provider;

pub use mr_registry::{MemoryRegion, MemoryRegionError, MemoryRegionRegistry};
pub use rdma_provider::{CompletionQueue, ProtectionDomain, QpState, QueuePair, RdmaProvider};
pub use rq_refill::{ReceiveBufferSlot, RqConfig, RqRefillManager};
pub use socket_provider::{SocketProviderError, SocketTransportFallback};

#[derive(Debug, Error)]
pub enum RmaError {
    #[error("InfiniBand/RoCE device not found: {0}")]
    DeviceNotFound(String),

    #[error("Failed to create Verbs context or protection domain")]
    ContextCreationFailed,

    #[error("Memory region registration failed at vaddr 0x{vaddr:x}, size {size}")]
    MemoryRegistrationFailed { vaddr: u64, size: usize },

    #[error("Queue pair error on endpoint {0}")]
    QueuePairError(String),

    #[error("Completion queue error: {0}")]
    CompletionError(String),

    #[error("Network I/O error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("Operation not yet implemented: {0}")]
    Unimplemented(String),
}

pub use RmaError as RmaEngineError;

/// Advanced RDMA endpoint tuning parameters.
#[derive(Debug, Clone)]
pub struct RdmaEndpointConfig {
    pub device_name: Option<String>,
    pub enable_gdr: bool,
    pub rq_prepost_count: u32,
    pub control_immediate: bool,
    pub bind_addr: Option<SocketAddr>,
    pub max_recv_wr: u32,
    pub max_send_wr: u32,
    pub selective_signaling: bool,
    pub signal_batch_size: u32,
    pub inline_data_max: u32,
    pub enable_srq: bool,
}

impl Default for RdmaEndpointConfig {
    fn default() -> Self {
        Self {
            device_name: None,
            enable_gdr: true,
            rq_prepost_count: 512,
            control_immediate: true,
            bind_addr: None,
            max_recv_wr: 1024,
            max_send_wr: 1024,
            selective_signaling: true,
            signal_batch_size: 8,
            inline_data_max: 64,
            enable_srq: false,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct RegisteredRegion {
    pub lkey: u32,
    pub rkey: u32,
    pub vaddr: u64,
    pub length: usize,
    pub gpu_id: Option<u32>,
}

pub type MemoryRegionHandle = RegisteredRegion;

#[derive(Debug, Clone)]
pub struct Completion {
    pub opcode: u32,
    pub status: u32,
    pub wr_id: u64,
    pub immediate_data: Option<u32>,
}

#[derive(Debug)]
pub struct RmaTransportEngine {
    #[allow(dead_code)]
    config: RdmaEndpointConfig,
    #[allow(dead_code)]
    listen_addr: SocketAddr,
    #[allow(dead_code)]
    active_device: Option<String>,
    hardware_accelerated: bool,
    next_rkey: AtomicU32,
    registered_regions: parking_lot::RwLock<HashMap<u32, RegisteredRegion>>,
    completed_ops: AtomicU64,
    rdma_provider: Option<Arc<RdmaProvider>>,
}

impl RmaTransportEngine {
    pub fn auto_detect(
        config: RdmaEndpointConfig,
        listen_addr: SocketAddr,
    ) -> Result<Arc<Self>, RmaError> {
        info!("Initializing RMA Transport Engine at {}", listen_addr);

        let active_device = if let Some(ref dev_name) = config.device_name {
            info!("User requested explicit RDMA device: {}", dev_name);
            Some(dev_name.clone())
        } else {
            let detected = Self::detect_verbs_hardware();
            if let Some(ref dev) = detected {
                info!("Auto-detected hardware RDMA device: {}", dev);
            } else {
                warn!("No hardware RDMA devices detected. Falling back to TCP/software transport.");
            }
            detected
        };

        let is_hw = active_device.is_some();

        let rdma_provider = RdmaProvider::new(config.clone())
            .map_err(|_e| RmaError::ContextCreationFailed)?;
        if let Err(e) = rdma_provider.enable_optimizations() {
            warn!("Could not enable RDMA optimizations: {}", e);
        }

        Ok(Arc::new(Self {
            config,
            listen_addr,
            active_device,
            hardware_accelerated: is_hw,
            next_rkey: AtomicU32::new(1001),
            registered_regions: parking_lot::RwLock::new(HashMap::new()),
            completed_ops: AtomicU64::new(0),
            rdma_provider: Some(Arc::new(rdma_provider)),
        }))
    }

    fn detect_verbs_hardware() -> Option<String> {
        if std::path::Path::new("/sys/class/infiniband").exists() {
            if let Ok(entries) = std::fs::read_dir("/sys/class/infiniband") {
                for entry in entries.flatten() {
                    if let Ok(name) = entry.file_name().into_string() {
                        return Some(name);
                    }
                }
            }
        }
        None
    }

    pub fn is_hardware_accelerated(&self) -> bool {
        self.hardware_accelerated
    }

    pub fn register_memory_region(
        &self,
        vaddr: u64,
        length: usize,
        gpu_id: Option<u32>,
    ) -> Result<RegisteredRegion, RmaError> {
        if vaddr == 0 || length == 0 {
            return Err(RmaError::MemoryRegistrationFailed { vaddr, size: length });
        }

        {
            let map = self.registered_regions.read();
            for region in map.values() {
                if region.vaddr == vaddr && region.length == length && region.gpu_id == gpu_id {
                    return Ok(*region);
                }
            }
        }

        if let Some(ref provider) = self.rdma_provider {
            let flags = if gpu_id.is_some() { 0x3 } else { 0x3 };
            let mr = provider
                .register_region(vaddr, length, flags)
                .map_err(|_e| RmaError::MemoryRegistrationFailed { vaddr, size: length })?;
            let reg = RegisteredRegion {
                lkey: mr.lkey,
                rkey: mr.rkey,
                vaddr: mr.vaddr,
                length: mr.size,
                gpu_id,
            };
            self.registered_regions.write().insert(reg.rkey, reg);
            return Ok(reg);
        }

        let key = self.next_rkey.fetch_add(1, Ordering::SeqCst);
        let region = RegisteredRegion {
            lkey: key,
            rkey: key,
            vaddr,
            length,
            gpu_id,
        };
        self.registered_regions.write().insert(key, region);
        Ok(region)
    }

    pub fn deregister_memory_region(&self, rkey: u32) -> Result<(), RmaError> {
        if let Some(ref provider) = self.rdma_provider {
            provider
                .deregister_region(rkey)
                .map_err(|_| RmaError::MemoryRegistrationFailed { vaddr: 0, size: 0 })?;
        }
        self.registered_regions.write().remove(&rkey);
        Ok(())
    }

    pub async fn rdma_read(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        rkey: u32,
        local_vaddr: u64,
        len: usize,
    ) -> Result<(), RmaError> {
        if let Some(ref provider) = self.rdma_provider {
            provider
                .post_read(peer_addr, remote_vaddr, rkey, local_vaddr, len)
                .map_err(|e| RmaError::QueuePairError(e.to_string()))?;
        }
        self.completed_ops.fetch_add(1, Ordering::Relaxed);
        tokio::task::yield_now().await;
        Ok(())
    }

    pub async fn rdma_write(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        rkey: u32,
        local_vaddr: u64,
        len: usize,
    ) -> Result<(), RmaError> {
        if let Some(ref provider) = self.rdma_provider {
            provider
                .post_write(peer_addr, remote_vaddr, rkey, local_vaddr, len)
                .map_err(|e| RmaError::QueuePairError(e.to_string()))?;
        }
        self.completed_ops.fetch_add(1, Ordering::Relaxed);
        tokio::task::yield_now().await;
        Ok(())
    }

    pub async fn rdma_write_immediate(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        rkey: u32,
        local_vaddr: u64,
        len: usize,
        _immediate_data: u32,
    ) -> Result<(), RmaError> {
        // Software provider doesn't support immediate; delegate to normal write.
        self.rdma_write(peer_addr, remote_vaddr, rkey, local_vaddr, len).await
    }

    pub async fn rdma_atomic_cas(
        &self,
        peer_addr: SocketAddr,
        remote_vaddr: u64,
        rkey: u32,
        compare: u64,
        swap: u64,
    ) -> Result<u64, RmaError> {
        if let Some(ref provider) = self.rdma_provider {
            return provider
                .post_atomic_cas(peer_addr, remote_vaddr, rkey, compare, swap)
                .map_err(|e| RmaError::CompletionError(e.to_string()));
        }
        self.completed_ops.fetch_add(1, Ordering::Relaxed);
        tokio::task::yield_now().await;
        Ok(compare)
    }

    pub async fn poll_completions(&self, _timeout: Duration) -> Result<Vec<Completion>, RmaError> {
        if let Some(ref provider) = self.rdma_provider {
            let count = provider
                .poll_cq(16)
                .map_err(|e| RmaError::CompletionError(e.to_string()))?;
            if count > 0 {
                return Ok(vec![Completion {
                    opcode: 0,
                    status: 0,
                    wr_id: 0,
                    immediate_data: None,
                }; count]);
            }
            return Ok(Vec::new());
        }
        let count = self.completed_ops.load(Ordering::Relaxed);
        if count > 0 {
            self.completed_ops.store(0, Ordering::Relaxed);
            Ok(vec![Completion {
                opcode: 0,
                status: 0,
                wr_id: 0,
                immediate_data: None,
            }])
        } else {
            Ok(Vec::new())
        }
    }

    pub fn register_staging_pool(
        &self,
        vaddr: u64,
        length: usize,
    ) -> Result<RegisteredRegion, RmaError> {
        self.register_memory_region(vaddr, length, None)
    }

    pub fn config(&self) -> &RdmaEndpointConfig {
        &self.config
    }

    pub fn completed_operations_count(&self) -> u64 {
        self.completed_ops.load(Ordering::Relaxed)
    }

    pub fn enable_srq(&self) -> Result<(), RmaError> {
        if !self.config.enable_srq {
            return Err(RmaError::Unimplemented("SRQ support not yet implemented".to_string()));
        }
        Ok(())
    }

    pub fn set_inline_threshold(&self, _max_bytes: u32) -> Result<(), RmaError> {
        Ok(())
    }
}
