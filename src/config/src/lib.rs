// virthub/src/config/src/lib.rs

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

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct VirthubConfig {
    pub general: GeneralConfig,
    pub klnk: KlnkConfig,
    pub store: StoreConfig,
    pub master: MasterConfig,
    pub transport: TransportConfig,
    pub ebpf: EbpfConfig,
    pub tuning: TuningConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GeneralConfig {
    pub log_level: String,
    pub control_socket: String,
    pub data_bind_addr: String,
    pub node_id: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct KlnkConfig {
    pub enable_uffd_move: bool,
    pub fallback_copy: bool,
    pub staging_num_pages: usize,
    pub huge_page_size: usize,
}

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

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct EbpfConfig {
    pub enabled: bool,
    pub program_path: String,
    pub report_interval_ms: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct TuningConfig {
    pub numa_node: i32,
    pub operation_timeout_ms: u64,
    pub memlock_limit: usize,
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

    pub fn load_default() -> Result<Self, ConfigError> {
        let path = std::env::var("VIRTHUB_CONFIG").unwrap_or_else(|_| "conf/virthub.toml".to_string());
        Self::load_from_file(path)
    }

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
                    initial_peers: vec!["node-1".to_string(), "node-2".to_string(), "node-3".to_string()],
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
}
