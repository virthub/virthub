// tests/config_integration.rs

use std::net::SocketAddr;
use std::path::Path;
use lmcache::LmCacheKlnkConnector;
use master::raft_state::{RaftEngine, RaftRole};
use master::scheduler::{ClusterScheduler, NodeResourceCapacity};
use sglang::SglangKlnkConnector;
use store::kv_block::KvBlockKey;
use store::tier_manager::{StorageTier, StorageTierManager};
use virthub_config::VirthubConfig;
use vllm::VllmKlnkConnector;

const CONFIG_FILE_PATH: &str = "conf/virthub.toml";

#[tokio::test]
async fn test_configuration_file_loading_and_parsing() {
    assert!(
        Path::new(CONFIG_FILE_PATH).exists(),
        "Configuration file {} does not exist!",
        CONFIG_FILE_PATH
    );

    let config = VirthubConfig::load_from_file(CONFIG_FILE_PATH)
        .expect("conf/virthub.toml should be successfully loaded and parsed");

    // 1. General Config Assertions
    assert_eq!(config.general.log_level, "info");
    assert_eq!(config.general.control_socket, "/tmp/virthub_control.sock");
    assert_eq!(config.general.data_bind_addr, "0.0.0.0:19001");
    assert_eq!(config.parsed_node_id(), 1);

    // 2. KLNK Config Assertions
    assert!(config.klnk.enable_uffd_move);
    assert!(config.klnk.fallback_copy);
    assert_eq!(config.klnk.staging_num_pages, 16);
    assert_eq!(config.klnk.huge_page_size, 2_097_152);

    // 3. Storage Tier Config Assertions
    assert_eq!(config.store.block_size, 2_097_152);
    assert!(!config.store.tier.l0_enabled);
    assert!(config.store.tier.l1_enabled);
    assert!(!config.store.tier.l2_enabled);

    // 4. Master Scheduler and Raft Assertions
    assert!(config.master.raft.embedded);
    assert_eq!(config.master.raft.initial_peers.len(), 3);
    assert_eq!(config.master.scheduler.prefetch_window, 8);
    assert_eq!(config.master.scheduler.l0_promote_threshold, 100);
    assert_eq!(config.master.sharding.shard_count, 64);

    // 5. Transport Layer Config Assertions
    assert_eq!(config.transport.default_protocol, "rdma");
    assert_eq!(config.transport.rdma.rq_prepost_count, 1024);
    assert!(config.transport.rdma.control_immediate);
    assert!(config.transport.tcp.io_uring_enabled);
}

#[tokio::test]
async fn test_storage_tier_manager_initialization() {
    // Use default manager; from_config is not needed for this test.
    let tier_manager = StorageTierManager::new();
    assert_eq!(tier_manager.get_usage(StorageTier::Dram), 0);
    assert_eq!(tier_manager.get_usage(StorageTier::Ssd), 0);
    assert_eq!(tier_manager.get_usage(StorageTier::Vram), 0);
}

#[tokio::test]
async fn test_master_scheduler_and_raft_engine_initialization_from_config() {
    let config = VirthubConfig::load_from_file(CONFIG_FILE_PATH)
        .expect("Config loading should succeed");

    // Initialize Scheduler from config
    let scheduler = ClusterScheduler::from_config(&config);
    assert_eq!(scheduler.node_count(), 0);

    let node_id = klnk_core::domain::NodeId(config.parsed_node_id());
    let capacity = NodeResourceCapacity::new(node_id, 16 * 1024 * 1024 * 1024, 2);
    scheduler.register_node(capacity);
    assert_eq!(scheduler.node_count(), 1);

    // Schedule test allocation
    let decision = scheduler
        .schedule_allocation(2 * 1024 * 1024, None)
        .expect("Allocation scheduling should succeed");

    assert_eq!(decision.selected_node_id, node_id);
    assert_eq!(decision.shard_id, (node_id.0 as usize) % config.master.sharding.shard_count);

    // Initialize Raft consensus engine from config
    let raft_engine = RaftEngine::from_config(node_id, &config)
        .expect("Raft engine creation from config should succeed");

    assert_eq!(raft_engine.role().await, RaftRole::Follower);
    assert_eq!(raft_engine.current_term(), 0);
}

#[tokio::test]
async fn test_all_connectors_initialization_from_config() {
    let mut config = VirthubConfig::load_from_file(CONFIG_FILE_PATH)
        .expect("Config loading should succeed");

    // Override bind address to avoid local socket port conflicts during integration testing
    config.general.data_bind_addr = "127.0.0.1:59001".to_string();

    // 1. Initialize vLLM Connector from VirthubConfig
    let vllm_connector = VllmKlnkConnector::new(config.clone())
        .expect("vLLM Connector initialization from VirthubConfig should succeed");
    assert_eq!(vllm_connector.local_node_id(), config.parsed_node_id());
    assert_eq!(vllm_connector.registered_block_count().await, 0);

    // Register a mock vLLM block
    let block_id = 99;
    let vaddr = 0x7fff_1000_0000u64;
    let meta = vllm_connector
        .register_kv_block(block_id, vaddr, 2_097_152, 0)
        .await
        .expect("vLLM block registration should succeed");
    assert_eq!(meta.block_id, block_id);
    assert_eq!(vllm_connector.registered_block_count().await, 1);

    // 2. Initialize SGLang Connector from VirthubConfig
    config.general.data_bind_addr = "127.0.0.1:59002".to_string();
    let sglang_connector = SglangKlnkConnector::new(config.clone())
        .expect("SGLang Connector initialization from VirthubConfig should succeed");
    assert_eq!(sglang_connector.local_node_id(), config.parsed_node_id());

    let prefix_hash = 0x1234_5678_ABCD_EF00u64;
    let radix_meta = sglang_connector
        .register_prefix_node(prefix_hash, 64, vaddr, 2_097_152, 0)
        .await
        .expect("SGLang prefix registration should succeed");
    assert_eq!(radix_meta.prefix_hash, prefix_hash);
    assert_eq!(sglang_connector.registered_prefix_count().await, 1);

    // 3. Initialize LMCache Connector from VirthubConfig
    config.general.data_bind_addr = "127.0.0.1:59003".to_string();
    let lmcache_connector = LmCacheKlnkConnector::new(config.clone())
        .expect("LMCache Connector initialization from VirthubConfig should succeed");
    assert_eq!(lmcache_connector.local_node_id(), config.parsed_node_id());

    let chunk_key = KvBlockKey::new(1, 100);
    let payload = vec![0xABu8; 1024];
    let chunk_meta = lmcache_connector
        .put_chunk(chunk_key, StorageTier::Dram, 0, payload, vaddr, 1024)
        .await
        .expect("LMCache chunk put should succeed");

    assert_eq!(chunk_meta.key, chunk_key);
    assert_eq!(chunk_meta.tier, StorageTier::Dram);
    assert_eq!(lmcache_connector.registered_chunk_count().await, 1);
}
