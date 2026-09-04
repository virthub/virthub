// virthub/src/klnk/klnk-daemon/src/main.rs

//! KLNK Daemon – the core user‑space service for the Virthub DSM engine.
//!
//! This daemon manages memory regions, handles userfaultfd events, coordinates
//! distributed coherence, exposes metadata over RDMA, and runs a prefetch engine
//! that issues speculative RDMA reads based on eBPF telemetry.
//!
//! **Precision‑Scalable PSP‑KV Integration:**
//! The daemon now reads the `[precision]` configuration section and logs the
//! chosen physical format generation and threshold parameters. Although the
//! actual precision predictor is used by the master scheduler and connectors,
//! the daemon is responsible for validating the configuration and may later
//! pass it to the control plane metadata subsystem.

mod prefetch;
mod rdma_metadata;
mod uffd_batch;
mod invalidation;

use klnk_core::control_plane::ControlPlaneManager;
use klnk_core::domain::{
    GlobalRegionId, MemoryProtectionFlags, MemoryRegionDescriptor, NodeId,
};
use klnk_ebpf::EbpfTraceManager;
use klnk_net::control_transport::{
    recv_control_message_tokio, send_control_message_tokio, ControlMessagePayload,
};
use klnk_uffd::handler::UffdHandler;
use librmashim::{RdmaEndpointConfig, RmaTransportEngine};
use store::staging_pool::{StagingMemoryPool, PAGE_SIZE_2M};
use std::net::SocketAddr;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;
use tokio::net::{UnixListener, UnixStream};
use tokio::task;
use tracing::{error, info, warn};
use virthub_config::VirthubConfig;
use prefetch::{create_prefetch_engine, PrefetchConfig};
use rdma_metadata::{init_metadata_publisher, start_periodic_push};
use uffd_batch::UffdBatchProcessor;
use invalidation::start_invalidation_poller;

mod limits {
    use std::sync::atomic::{AtomicUsize, Ordering};
    use std::sync::Arc;
    use thiserror::Error;

    #[derive(Debug, Error)]
    pub enum LimitsError {
        #[error("Memory limit exceeded")]
        #[allow(dead_code)]
        MemoryLimitExceeded,
        #[error("Descriptor limit exceeded")]
        #[allow(dead_code)]
        DescriptorLimitExceeded,
        #[error("Backpressure")]
        HighWatermarkBackpressure,
    }

    #[derive(Debug, Clone)]
    pub struct DaemonResourceConfig {
        pub max_staging_bytes: usize,
        pub high_watermark_pct: f64,
        #[allow(dead_code)]
        pub low_watermark_pct: f64,
        #[allow(dead_code)]
        pub max_active_descriptors: usize,
    }

    impl Default for DaemonResourceConfig {
        fn default() -> Self {
            Self {
                max_staging_bytes: 8 * 1024 * 1024 * 1024,
                high_watermark_pct: 0.85,
                low_watermark_pct: 0.60,
                max_active_descriptors: 1024,
            }
        }
    }

    pub struct ResourceLimiter {
        used_staging_bytes: AtomicUsize,
        #[allow(dead_code)]
        active_descriptors: AtomicUsize,
        config: DaemonResourceConfig,
    }

    impl ResourceLimiter {
        pub fn new(config: DaemonResourceConfig) -> Arc<Self> {
            Arc::new(Self {
                used_staging_bytes: AtomicUsize::new(0),
                active_descriptors: AtomicUsize::new(0),
                config,
            })
        }
        pub fn check_backpressure(&self) -> Result<(), LimitsError> {
            let used = self.used_staging_bytes.load(Ordering::Relaxed) as f64;
            let limit = self.config.max_staging_bytes as f64;
            if used / limit >= self.config.high_watermark_pct {
                Err(LimitsError::HighWatermarkBackpressure)
            } else {
                Ok(())
            }
        }
        #[allow(dead_code)]
        pub fn reserve_memory(&self, _bytes: usize) -> Result<(), LimitsError> {
            Ok(())
        }
        #[allow(dead_code)]
        pub fn release_memory(&self, _bytes: usize) {}
    }
}
use limits::{DaemonResourceConfig, ResourceLimiter};

fn pin_thread_to_core(core_id: usize) -> Result<(), String> {
    let mut cpuset: libc::cpu_set_t = unsafe { std::mem::zeroed() };
    unsafe { libc::CPU_SET(core_id, &mut cpuset); }
    let tid = unsafe { libc::syscall(libc::SYS_gettid) as libc::pid_t };
    if tid < 0 {
        return Err("Failed to get tid".to_string());
    }
    let ret = unsafe {
        libc::sched_setaffinity(tid, std::mem::size_of::<libc::cpu_set_t>(), &cpuset)
    };
    if ret == 0 {
        Ok(())
    } else {
        Err(format!("Failed: {}", std::io::Error::last_os_error()))
    }
}

fn get_core_id_for_numa_node(numa_node: i32) -> Option<usize> {
    if numa_node < 0 {
        None
    } else {
        Some((numa_node as usize) * 2)
    }
}

pub fn spawn_pinned_task<F>(numa_node: i32, future: F) -> task::JoinHandle<F::Output>
where
    F: std::future::Future + Send + 'static,
    F::Output: Send + 'static,
{
    let core_id = get_core_id_for_numa_node(numa_node);
    task::spawn(async move {
        if let Some(cid) = core_id {
            if let Err(e) = pin_thread_to_core(cid) {
                warn!("Failed to pin task: {}", e);
            }
        }
        future.await
    })
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    tracing_subscriber::fmt::init();
    info!("Starting klnk-daemon...");

    let app_config = VirthubConfig::load_default()
        .unwrap_or_else(|_| {
            warn!("Using default config.");
            VirthubConfig::default()
        });

    // Log precision configuration
    info!(
        "Precision configuration loaded: format_generation={}, sink_window={}, local_window={}, critical_layers={}, thresholds=({:.2}/{:.2}/{:.2}/{:.2})",
        app_config.precision.format_generation,
        app_config.precision.sink_window,
        app_config.precision.local_window,
        app_config.precision.critical_layer_count,
        app_config.precision.elevated_pressure_threshold,
        app_config.precision.nominal_pressure_threshold,
        app_config.precision.critical_pressure_threshold,
        app_config.precision.critical_relax_threshold,
    );

    let numa_node = app_config.tuning.numa_node;

    // 1. RMA engine
    let addr_str = format!("0.0.0.0:{}", app_config.transport.tcp.tcp_port);
    let listen_addr: SocketAddr = addr_str.parse()?;
    let verbs_config = RdmaEndpointConfig {
        device_name: Some(app_config.transport.rdma.device_name.clone()),
        enable_gdr: app_config.transport.rdma.enable_gdr,
        rq_prepost_count: app_config.transport.rdma.rq_prepost_count as u32,
        control_immediate: app_config.transport.rdma.control_immediate,
        ..Default::default()
    };
    let rma_engine = RmaTransportEngine::auto_detect(verbs_config, listen_addr)?;
    info!(
        "RMA engine initialized (hardware: {})",
        rma_engine.is_hardware_accelerated()
    );

    // 2. Control plane
    let node_id_val = app_config.general.node_id.parse().unwrap_or(1);
    let control_plane = ControlPlaneManager::new(NodeId(node_id_val));

    // 3. RDMA metadata publisher
    let metadata_publisher = init_metadata_publisher(control_plane.clone(), rma_engine.clone())?;
    info!(
        "RDMA metadata published: vaddr=0x{:x}, rkey={}, version={}",
        metadata_publisher.get_metadata_region().map(|r| r.vaddr).unwrap_or(0),
        metadata_publisher.get_metadata_region().map(|r| r.rkey).unwrap_or(0),
        metadata_publisher.metadata_version()
    );

    let metadata_publisher = Arc::new(metadata_publisher);
    let _metadata_push_handle = start_periodic_push(metadata_publisher.clone(), 500);

    // 4. Staging pool
    let num_pages = app_config.klnk.staging_num_pages;
    let page_size = PAGE_SIZE_2M;
    let staging_pool_numa = numa_node.max(0) as u32;
    let staging_pool = StagingMemoryPool::new(num_pages, page_size, staging_pool_numa)?;

    if let Ok(region) = rma_engine.register_staging_pool(
        staging_pool.base_address(),
        staging_pool.total_size(),
    ) {
        info!(
            "Staging pool registered for RDMA: vaddr=0x{:x}, rkey={}",
            region.vaddr, region.rkey
        );
    } else {
        warn!("Failed to register staging pool with RDMA.");
    }

    // 5. Prefetch engine
    let prefetch_config = PrefetchConfig {
        max_concurrent_requests: 16,
        max_prefetch_count: app_config.master.scheduler.prefetch_window,
        enabled: true,
    };
    let (mut prefetch_engine, prefetch_tx) = create_prefetch_engine(
        control_plane.clone(),
        rma_engine.clone(),
        staging_pool.clone(),
        prefetch_config,
    );
    let prefetched_pages = prefetch_engine.get_prefetched_pages();
    prefetch_engine.start();

    // 6. eBPF telemetry
    if app_config.ebpf.enabled {
        info!("eBPF telemetry enabled.");
        let (ebpf_manager, mut event_rx) = EbpfTraceManager::new(app_config.clone(), 1024);
        if let Err(e) = ebpf_manager.attach_probes() {
            error!("Failed to attach eBPF probes: {}", e);
        }
        let prefetch_tx_clone = prefetch_tx.clone();
        spawn_pinned_task(numa_node, async move {
            while let Some(event) = event_rx.recv().await {
                if let Some(rec) = ebpf_manager.analyze_event(event) {
                    if let Err(e) = prefetch_tx_clone.send(rec) {
                        warn!("Failed to send prefetch rec: {}", e);
                        break;
                    }
                }
            }
        });
    }

    // 7. UFFD handler
    let mut uffd_handler = UffdHandler::new(
        control_plane.clone(),
        staging_pool.clone(),
        rma_engine.clone(),
    )?;
    uffd_handler.set_prefetched_pages(prefetched_pages);
    let uffd_handler = Arc::new(uffd_handler);

    let uffd_handler_clone = uffd_handler.clone();
    spawn_pinned_task(numa_node, async move {
        let config = uffd_batch::UffdBatchConfig::default();
        let processor = UffdBatchProcessor::new(uffd_handler_clone, config);
        processor.run_loop().await;
    });

    // 8. Invalidation buffer poller
    let inval_buffer = metadata_publisher.get_invalidation_buffer();
    let cp_inval = control_plane.clone();
    let rma_inval = rma_engine.clone();
    spawn_pinned_task(numa_node, async move {
        start_invalidation_poller(cp_inval, rma_inval, inval_buffer).await;
    });

    // 9. Resource limiter
    let limiter_config = DaemonResourceConfig {
        max_staging_bytes: app_config.tuning.memlock_limit,
        ..Default::default()
    };
    let limiter = ResourceLimiter::new(limiter_config);
    let limiter_clone = limiter.clone();
    tokio::spawn(async move {
        loop {
            tokio::time::sleep(Duration::from_secs(1)).await;
            if let Err(e) = limiter_clone.check_backpressure() {
                warn!("Backpressure: {}", e);
            }
        }
    });

    // 10. Control socket
    let socket_path = app_config.general.control_socket.clone();
    if let Some(parent) = std::path::Path::new(&socket_path).parent() {
        if !parent.exists() {
            std::fs::create_dir_all(parent)?;
        }
    }
    let _ = tokio::fs::remove_file(&socket_path).await;
    let listener = UnixListener::bind(&socket_path)?;
    info!("Listening on Unix socket: {}", socket_path);

    let shutdown_flag = Arc::new(AtomicBool::new(false));
    let shutdown_flag_clone = shutdown_flag.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.expect("Failed to listen for ctrl_c");
        info!("Received shutdown signal");
        shutdown_flag_clone.store(true, Ordering::SeqCst);
    });

    loop {
        if shutdown_flag.load(Ordering::SeqCst) {
            info!("Shutting down control socket loop");
            break;
        }

        tokio::select! {
            _ = tokio::signal::ctrl_c() => {
                shutdown_flag.store(true, Ordering::SeqCst);
                break;
            }
            accept_result = listener.accept() => {
                match accept_result {
                    Ok((stream, _addr)) => {
                        let cp = control_plane.clone();
                        let rma = rma_engine.clone();
                        tokio::spawn(async move {
                            if let Err(e) = handle_client(stream, cp, rma).await {
                                error!("Client error: {}", e);
                            }
                        });
                    }
                    Err(e) => {
                        if shutdown_flag.load(Ordering::SeqCst) {
                            break;
                        }
                        warn!("Accept error: {}", e);
                    }
                }
            }
        }
    }

    info!("Daemon shutdown complete");
    Ok(())
}

async fn handle_client(
    mut stream: UnixStream,
    control_plane: Arc<ControlPlaneManager>,
    _rma_engine: Arc<RmaTransportEngine>,
) -> Result<(), Box<dyn std::error::Error>> {
    loop {
        let payload = match recv_control_message_tokio(&mut stream).await {
            Ok(msg) => msg,
            Err(e) => {
                if let klnk_net::control_transport::TransportError::IoError(io_err) = &e {
                    if io_err.kind() == std::io::ErrorKind::UnexpectedEof {
                        info!("Client disconnected.");
                        return Ok(());
                    }
                }
                error!("Recv error: {}", e);
                return Err(e.into());
            }
        };
        let response = process_control_payload(payload, &control_plane).await;
        if let Err(e) = send_control_message_tokio(&mut stream, &response).await {
            error!("Send error: {}", e);
            return Err(e.into());
        }
    }
}

async fn process_control_payload(
    payload: ControlMessagePayload,
    control_plane: &ControlPlaneManager,
) -> ControlMessagePayload {
    match payload {
        ControlMessagePayload::RegionRegister { pid, vaddr, size, flags } => {
            let region_id = GlobalRegionId {
                owner_pid: pid,
                shmid: vaddr as i32,
            };
            let desc = MemoryRegionDescriptor {
                region_id,
                main_vaddr: vaddr,
                region_size: size,
                staging_vaddr: 0,
                staging_num_pages: 0,
                staging_page_size: 4096,
                prot_flags: MemoryProtectionFlags(flags),
                mem_flags: 0,
                version: 0,
            };
            match control_plane.register_region(desc) {
                Ok(_) => ControlMessagePayload::ResponseAck {
                    success: true,
                    message: "OK".to_string(),
                },
                Err(e) => ControlMessagePayload::ResponseAck {
                    success: false,
                    message: e.to_string(),
                },
            }
        }
        ControlMessagePayload::RegionDeregister { pid, vaddr } => {
            let region_id = GlobalRegionId {
                owner_pid: pid,
                shmid: vaddr as i32,
            };
            match control_plane.deregister_region(region_id) {
                Ok(_) => ControlMessagePayload::ResponseAck {
                    success: true,
                    message: "OK".to_string(),
                },
                Err(e) => ControlMessagePayload::ResponseAck {
                    success: false,
                    message: e.to_string(),
                },
            }
        }
        ControlMessagePayload::LockAcquire { region_id, offset, len: _ } => {
            let offset_u32 = offset as u32;
            match control_plane.acquire_lock(region_id, offset_u32).await {
                Ok(_) => ControlMessagePayload::ResponseAck {
                    success: true,
                    message: "OK".to_string(),
                },
                Err(e) => ControlMessagePayload::ResponseAck {
                    success: false,
                    message: e.to_string(),
                },
            }
        }
        ControlMessagePayload::LockRelease { region_id, offset, len: _ } => {
            let offset_u32 = offset as u32;
            match control_plane.release_lock(region_id, offset_u32).await {
                Ok(_) => ControlMessagePayload::ResponseAck {
                    success: true,
                    message: "OK".to_string(),
                },
                Err(e) => ControlMessagePayload::ResponseAck {
                    success: false,
                    message: e.to_string(),
                },
            }
        }
        ControlMessagePayload::Heartbeat { .. } => ControlMessagePayload::ResponseAck {
            success: true,
            message: "Heartbeat".to_string(),
        },
        _ => ControlMessagePayload::ResponseAck {
            success: false,
            message: "Unsupported".to_string(),
        },
    }
}
