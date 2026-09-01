// tests/connector_flow_test.rs

use std::net::SocketAddr;
use std::path::Path;
use lmcache::VirthubLmCacheConnector;
use sglang::VirthubSglangConnector;
use store::kv_block::KvBlockKey;
use store::tier_manager::StorageTier;
use virthub_config::VirthubConfig;
use vllm::VirthubVllmConnector;

const CONFIG_FILE_PATH: &str = "conf/virthub.toml";

/// Helper function to clone config and set node ID and bind address for simulated peer nodes.
fn create_peer_config(base_config: &VirthubConfig, node_id_str: &str, bind_addr: &str) -> VirthubConfig {
    let mut peer_config = base_config.clone();
    peer_config.general.node_id = node_id_str.to_string();
    peer_config.general.data_bind_addr = bind_addr.to_string();
    peer_config
}

#[tokio::test]
async fn test_vllm_multi_node_kv_swap_flow() {
    assert!(Path::new(CONFIG_FILE_PATH).exists());
    let base_config = VirthubConfig::load_from_file(CONFIG_FILE_PATH).expect("Failed to load config");

    // Node 1: Primary Inference Worker (0.0.0.0:61001)
    let node1_config = create_peer_config(&base_config, "node-1", "127.0.0.1:61001");
    let primary_vllm = VirthubVllmConnector::new(node1_config)
        .expect("Primary vLLM connector initialization failed");

    // Node 2: Secondary Replica Worker (0.0.0.0:61002)
    let node2_config = create_peer_config(&base_config, "node-2", "127.0.0.1:61002");
    let replica_vllm = VirthubVllmConnector::new(node2_config)
        .expect("Replica vLLM connector initialization failed");

    // 1. Primary node registers a local PagedAttention KV-cache block (2MB)
    let block_id = 42;
    let primary_vaddr = 0x7fff_1000_0000u64;
    let block_size = 2 * 1024 * 1024; // 2MB

    let primary_meta = primary_vllm
        .register_kv_block(block_id, primary_vaddr, block_size, 0)
        .await
        .expect("Primary block registration should succeed");

    assert_eq!(primary_meta.block_id, block_id);
    assert_eq!(primary_vllm.registered_block_count().await, 1);

    // 2. Replica node initiates a zero-copy remote block fetch (swap-in) from Primary
    let replica_peer_addr: SocketAddr = "127.0.0.1:61001".parse().unwrap();
    let replica_vaddr = 0x7fff_2000_0000u64;

    let swap_result = replica_vllm
        .swap_in_remote_block(
            replica_peer_addr,
            primary_meta.vaddr,
            primary_meta.rkey,
            replica_vaddr,
            block_size,
        )
        .await;

    assert!(swap_result.is_ok(), "Remote KV block swap-in should succeed");

    // 3. Primary node evicts block when sequence finishes
    primary_vllm
        .unregister_kv_block(block_id)
        .await
        .expect("Unregistration should succeed");
    assert_eq!(primary_vllm.registered_block_count().await, 0);
}

#[tokio::test]
async fn test_sglang_radix_attention_prefix_sharing_flow() {
    assert!(Path::new(CONFIG_FILE_PATH).exists());
    let base_config = VirthubConfig::load_from_file(CONFIG_FILE_PATH).expect("Failed to load config");

    let node1_config = create_peer_config(&base_config, "node-1", "127.0.0.1:62001");
    let node1_sglang = VirthubSglangConnector::new(node1_config)
        .expect("Node 1 SGLang connector creation failed");

    let node2_config = create_peer_config(&base_config, "node-2", "127.0.0.1:62002");
    let node2_sglang = VirthubSglangConnector::new(node2_config)
        .expect("Node 2 SGLang connector creation failed");

    // 1. Node 1 registers a common system prompt prefix in RadixAttention tree
    let prefix_hash = 0xDEAD_BEEF_CAFE_0001u64;
    let token_count = 128;
    let node1_vaddr = 0x7fff_3000_0000u64;
    let size_bytes = 1024 * 1024; // 1MB

    let prefix_meta = node1_sglang
        .register_prefix_node(prefix_hash, token_count, node1_vaddr, size_bytes, 0)
        .await
        .expect("Prefix registration should succeed");

    assert_eq!(prefix_meta.prefix_hash, prefix_hash);
    assert_eq!(node1_sglang.registered_prefix_count().await, 1);

    // 2. Node 2 encounters the same prompt prefix hash -> fetches cached KV tensor via RDMA
    let peer_addr: SocketAddr = "127.0.0.1:62001".parse().unwrap();
    let node2_local_vaddr = 0x7fff_4000_0000u64;

    let fetch_res = node2_sglang
        .fetch_remote_prefix(
            peer_addr,
            prefix_meta.vaddr,
            prefix_meta.rkey,
            node2_local_vaddr,
            size_bytes,
        )
        .await;

    assert!(fetch_res.is_ok(), "Remote RadixAttention prefix fetch should succeed");

    // 3. Node 2 caches the prefix metadata locally
    node2_sglang
        .register_prefix_node(prefix_hash, token_count, node2_local_vaddr, size_bytes, 0)
        .await
        .expect("Node 2 local prefix caching should succeed");

    assert_eq!(node2_sglang.registered_prefix_count().await, 1);
}

#[tokio::test]
async fn test_lmcache_tiered_chunk_sharing_flow() {
    assert!(Path::new(CONFIG_FILE_PATH).exists());
    let base_config = VirthubConfig::load_from_file(CONFIG_FILE_PATH).expect("Failed to load config");

    let node1_config = create_peer_config(&base_config, "node-1", "127.0.0.1:63001");
    let node1_lmcache = VirthubLmCacheConnector::new(node1_config)
        .expect("Node 1 LMCache connector creation failed");

    let node2_config = create_peer_config(&base_config, "node-2", "127.0.0.1:63002");
    let node2_lmcache = VirthubLmCacheConnector::new(node2_config)
        .expect("Node 2 LMCache connector creation failed");

    // 1. Node 1 stores a chunk in multi-tier storage
    let chunk_key = KvBlockKey::new(100, 500);
    let payload = vec![0x77u8; 4096]; // 4KB chunk
    let node1_vaddr = 0x7fff_5000_0000u64;

    let chunk_meta = node1_lmcache
        .put_chunk(chunk_key, StorageTier::Dram, 0, payload.clone(), node1_vaddr, 4096)
        .await
        .expect("Chunk put on Node 1 should succeed");

    assert_eq!(chunk_meta.key, chunk_key);
    assert_eq!(node1_lmcache.registered_chunk_count().await, 1);

    // 2. Local retrieval on Node 1 from tier manager
    let retrieved_payload = node1_lmcache
        .get_chunk(&chunk_key)
        .await
        .expect("Local chunk get should succeed");
    assert_eq!(retrieved_payload, payload);

    // 3. Node 2 pulls remote chunk directly over RDMA from Node 1's registered memory
    let peer_addr: SocketAddr = "127.0.0.1:63001".parse().unwrap();
    let node2_vaddr = 0x7fff_6000_0000u64;

    let remote_fetch_res = node2_lmcache
        .fetch_remote_chunk(
            chunk_key,
            peer_addr,
            chunk_meta.vaddr,
            chunk_meta.rkey,
            node2_vaddr,
            chunk_meta.size_bytes,
        )
        .await;

    assert!(remote_fetch_res.is_ok(), "Remote LMCache chunk fetch should succeed");

    // 4. Clean up Node 1 chunk
    node1_lmcache
        .remove_chunk(&chunk_key)
        .await
        .expect("Chunk removal should succeed");
    assert_eq!(node1_lmcache.registered_chunk_count().await, 0);
}
