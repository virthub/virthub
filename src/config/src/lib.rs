// virthub/src/config/src/lib.rs

//! Configuration loading, TOML schema parsing, and defaults for Virthub DSM.
//!
//! This crate defines the `VirthubConfig` struct and all sub‑configs that map
//! to the `virthub.toml` / `cluster.toml` files. It also includes the
//! [`PrecisionConfig`] section used by the allocation‑time precision predictor.

use serde::{Deserialize, Serialize};
use std::path::Path;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("Failed to read configuration file at '{path}': {source}")]
    IoError {
        path: String,
        source: std::io::Error,
    },

    #[error("Failed to parse TOML configuration from '{path}': {source}")]
    ParseError {
        path: String,
        source: toml::de::Error,
    },
}

/// Root configuration for Virthub.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct VirthubConfig {
    pub general: GeneralConfig,
    pub klnk: KlnkConfig,
    pub store: StoreConfig,
    pub master: MasterConfig,
    pub transport: TransportConfig,
    pub ebpf: EbpfConfig,
    pub tuning: TuningConfig,
    /// Precision prediction and PSP‑KV format configuration.
    #[serde(default)]
    pub precision: PrecisionConfig,
}

/// General settings.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GeneralConfig {
    pub log_level: String,
    pub control_socket: String,
    pub data_bind_addr: String,
    pub node_id: String,
}

/// KLNK engine configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct KlnkConfig {
    pub enable_uffd_move: bool,
    pub fallback_copy: bool,
    pub staging_num_pages: usize,
    pub huge_page_size: usize,
}

/// Storage tier configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct StoreConfig {
    pub tier: TierConfig,
    pub block_size: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TierConfig {
    pub l0_enabled: bool,
    pub l0_device_ids: Vec<u32>,
    pub l1_enabled: bool,
    pub l2_enabled: bool,
    pub l2_path: String,
}

/// Master & indexer configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct MasterConfig {
    pub raft: RaftConfig,
    pub scheduler: SchedulerConfig,
    pub sharding: ShardingConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct RaftConfig {
    pub embedded: bool,
    pub initial_peers: Vec<String>,
    pub etcd_endpoints: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct SchedulerConfig {
    pub prefetch_window: usize,
    pub l0_promote_threshold: u64,
    pub l1_demote_idle_secs: u64,
    pub lru_decay: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ShardingConfig {
    pub shard_count: usize,
}

/// Transport layer configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TransportConfig {
    pub default_protocol: String,
    pub rdma: RdmaConfig,
    pub tcp: TcpConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct RdmaConfig {
    pub device_name: String,
    pub enable_gdr: bool,
    pub rq_prepost_count: usize,
    pub control_immediate: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TcpConfig {
    pub io_uring_enabled: bool,
    pub tcp_port: u16,
}

/// eBPF telemetry configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct EbpfConfig {
    pub enabled: bool,
    pub program_path: String,
    pub report_interval_ms: u64,
}

/// Performance tuning configuration.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TuningConfig {
    pub numa_node: i32,
    pub operation_timeout_ms: u64,
    pub memlock_limit: usize,
}

/// Precision prediction and PSP‑KV format configuration.
///
/// These settings control the allocation‑time predictor and the static
/// physical format generation. The format is fixed for the entire serving
/// run; it is **not** selected dynamically per block.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct PrecisionConfig {
    /// Physical storage format generation:
    /// 0 = Basic, 1 = Enhanced GPU‑Native, 2 = BT‑KV.
    pub format_generation: u8,
    /// Number of initial tokens that are always lossless (attention sink).
    pub sink_window: usize,
    /// Number of trailing tokens that are always lossless (local context).
    pub local_window: usize,
    /// Number of first/last layers considered critical.
    pub critical_layer_count: usize,
    /// Elevated memory pressure threshold (0.0‑1.0).
    pub elevated_pressure_threshold: f64,
    /// Nominal memory pressure threshold (0.0‑1.0).
    pub nominal_pressure_threshold: f64,
    /// Critical memory pressure threshold (0.0‑1.0).
    pub critical_pressure_threshold: f64,
    /// Critical pressure relaxation threshold (0.0‑1.0).
    pub critical_relax_threshold: f64,
}

impl Default for PrecisionConfig {
    fn default() -> Self {
        Self {
            format_generation: 1, // Enhanced GPU‑Native by default
            sink_window: 16,
            local_window: 64,
            critical_layer_count: 2,
            elevated_pressure_threshold: 0.78,
            nominal_pressure_threshold: 0.70,
            critical_pressure_threshold: 0.88,
            critical_relax_threshold: 0.82,
        }
    }
}

impl VirthubConfig {
    /// Loads and parses the configuration file from disk.
    pub fn load_from_file<P: AsRef<Path>>(path: P) -> Result<Self, ConfigError> {
        let path_ref = path.as_ref();
        let path_str = path_ref.to_string_lossy().to_string();

        let content = std::fs::read_to_string(path_ref).map_err(|source| ConfigError::IoError {
            path: path_str.clone(),
            source,
        })?;

        let config: VirthubConfig =
            toml::from_str(&content).map_err(|source| ConfigError::ParseError {
                path: path_str,
                source,
            })?;

        Ok(config)
    }

    /// Loads configuration using the path in `VIRTHUB_CONFIG`, or a default path.
    pub fn load_default() -> Result<Self, ConfigError> {
        let path = std::env::var("VIRTHUB_CONFIG")
            .unwrap_or_else(|_| "conf/virthub.toml".to_string());
        Self::load_from_file(path)
    }

    /// Parses the numeric node ID from the `node_id` string.
    pub fn parsed_node_id(&self) -> u64 {
        self.general
            .node_id
            .trim_start_matches("node-")
            .parse::<u64>()
            .unwrap_or(1)
    }
}

impl Default for VirthubConfig {
    fn default() -> Self {
        Self {
            general: GeneralConfig {
                log_level: "info".to_string(),
                control_socket: "/tmp/virthub_control.sock".to_string(),
                data_bind_addr: "0.0.0.0:19001".to_string(),
                node_id: "node-1".to_string(),
            },
            klnk: KlnkConfig {
                enable_uffd_move: true,
                fallback_copy: true,
                staging_num_pages: 16,
                huge_page_size: 2_097_152,
            },
            store: StoreConfig {
                tier: TierConfig {
                    l0_enabled: false,
                    l0_device_ids: vec![0, 1],
                    l1_enabled: true,
                    l2_enabled: false,
                    l2_path: "/mnt/nvme/virthub_cache".to_string(),
                },
                block_size: 2_097_152,
            },
            master: MasterConfig {
                raft: RaftConfig {
                    embedded: true,
                    initial_peers: vec![
                        "node-1".to_string(),
                        "node-2".to_string(),
                        "node-3".to_string(),
                    ],
                    etcd_endpoints: vec!["http://127.0.0.1:2379".to_string()],
                },
                scheduler: SchedulerConfig {
                    prefetch_window: 8,
                    l0_promote_threshold: 100,
                    l1_demote_idle_secs: 60,
                    lru_decay: 0.8,
                },
                sharding: ShardingConfig { shard_count: 64 },
            },
            transport: TransportConfig {
                default_protocol: "rdma".to_string(),
                rdma: RdmaConfig {
                    device_name: "".to_string(),
                    enable_gdr: false,
                    rq_prepost_count: 1024,
                    control_immediate: true,
                },
                tcp: TcpConfig {
                    io_uring_enabled: true,
                    tcp_port: 19002,
                },
            },
            ebpf: EbpfConfig {
                enabled: true,
                program_path: "/usr/lib/virthub/stride_tracer.bpf.o".to_string(),
                report_interval_ms: 100,
            },
            tuning: TuningConfig {
                numa_node: -1,
                operation_timeout_ms: 500,
                memlock_limit: 0,
            },
            precision: PrecisionConfig::default(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_node_id_parsing() {
        let mut config = VirthubConfig::default();

        config.general.node_id = "node-5".to_string();
        assert_eq!(config.parsed_node_id(), 5);

        config.general.node_id = "42".to_string();
        assert_eq!(config.parsed_node_id(), 42);

        config.general.node_id = "invalid".to_string();
        assert_eq!(config.parsed_node_id(), 1);
    }

    #[test]
    fn test_precision_defaults() {
        let config = VirthubConfig::default();
        assert_eq!(config.precision.format_generation, 1);
        assert_eq!(config.precision.sink_window, 16);
        assert_eq!(config.precision.local_window, 64);
        assert_eq!(config.precision.critical_layer_count, 2);
    }

    #[test]
    fn test_precision_deserialization() {
        let toml_str = r#"
            format_generation = 2
            sink_window = 16
            local_window = 64
            critical_layer_count = 2
            elevated_pressure_threshold = 0.78
            nominal_pressure_threshold = 0.70
            critical_pressure_threshold = 0.88
            critical_relax_threshold = 0.82
        "#;
        let precision: PrecisionConfig = toml::from_str(toml_str).unwrap();
        assert_eq!(precision.format_generation, 2);
        assert_eq!(precision.sink_window, 16);
        assert_eq!(precision.local_window, 64);
        assert_eq!(precision.critical_layer_count, 2);
        assert_eq!(precision.elevated_pressure_threshold, 0.78);
        assert_eq!(precision.nominal_pressure_threshold, 0.70);
        assert_eq!(precision.critical_pressure_threshold, 0.88);
        assert_eq!(precision.critical_relax_threshold, 0.82);
    }
}
