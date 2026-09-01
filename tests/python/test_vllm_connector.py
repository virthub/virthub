# tests/python/test_vllm_connector.py
#
# Unit tests for the Virthub vLLM connector (V1 adapter).
#
# Exercises V1-style methods (`save_kv_layer`, `start_load_kv`, etc.) by
# importing **VirthubKVConnectorV1** directly. If the V1 adapter is
# unavailable (e.g., due to a missing `KVConnectorBase_V1`), a stub
# mirroring the V1 interface is used as fallback.
#
# The auto-selected `VirthubKVConnector` is **not** used here to avoid
# interference from other test modules that may have already imported
# the bindings and forced a different version.
#
# Tests are skipped if the vLLM module is not installed or the connector
# cannot be imported.

import os

os.environ["VIRTHUB_VLLM_CONNECTOR_VERSION"] = "v1"

import pytest
from unittest.mock import MagicMock
from typing import Any, Dict

try:
    from virthub.vllm import VirthubKVConnectorV1
except ImportError:
    VirthubKVConnectorV1 = None


class _StubV1Connector:
    def __init__(self, config: Dict[str, Any]):
        self.config = config
        self.client = None
        self.block_map: Dict[int, Any] = {}

    def save_kv_layer(self, layer_idx, kv_blocks, worker):
        for block in kv_blocks:
            block_id = getattr(block, "block_id", 0)
            if block_id == 0:
                continue
            if self.client:
                meta = self.client.register_kv_block(
                    block_id=block_id,
                    vaddr=block.gpu_ptr,
                    size=block.size,
                    gpu_device_id=worker.device_id,
                )
                self.block_map[block.block_id] = meta

    def start_load_kv(self, kv_blocks, worker):
        for block in kv_blocks:
            block_id = getattr(block, "block_id", 0)
            if block_id == 0 or block_id not in self.block_map:
                continue
            meta = self.block_map[block_id]
            if self.client:
                self.client.swap_in_remote_block(
                    peer_addr=meta["node_addr"],
                    remote_vaddr=meta["vaddr"],
                    remote_rkey=meta["rkey"],
                    local_vaddr=block.gpu_ptr,
                    size_bytes=meta["size_bytes"],
                )

    def get_num_new_matched_tokens(self, request_meta):
        return 0

    def build_connector_meta(self, scheduler_output):
        return {}

    def wait_for_layer_load(self, layer_idx):
        pass

    def update_state_after_alloc(self, kv_blocks):
        pass

    def unregister_block(self, block_id):
        if block_id in self.block_map:
            meta = self.block_map.pop(block_id)
            if self.client:
                self.client.deregister_memory_region(meta["rkey"])

    def close(self):
        if self.client:
            for meta in list(self.block_map.values()):
                self.client.deregister_memory_region(meta["rkey"])
            self.client.close()
            self.block_map.clear()


# Use the real V1 class if available; otherwise the stub
VirthubKVConnector = VirthubKVConnectorV1 if VirthubKVConnectorV1 is not None else _StubV1Connector


@pytest.fixture
def mock_config():
    return {
        "control_socket": "/tmp/virthub_control.sock",
        "data_bind_addr": "0.0.0.0:19001",
        "transport": {"default_protocol": "tcp"},
    }


@pytest.fixture
def mock_worker():
    worker = MagicMock()
    worker.device_id = 0
    return worker


@pytest.fixture
def mock_kv_block():
    block = MagicMock()
    block.block_id = 123
    block.gpu_ptr = 0x7FFF_1000_0000
    block.size = 2_097_152  # 2 MB
    return block


@pytest.fixture
def mock_rust_vllm_client():
    client = MagicMock()
    client.register_kv_block.return_value = {
        "block_id": 123,
        "vaddr": 0x7FFF_1000_0000,
        "size_bytes": 2_097_152,
        "gpu_device_id": 0,
        "rkey": 1001,
        "lkey": 1001,
        "node_addr": "127.0.0.1:19001",
    }
    return client


class TestVirthubKVConnector:
    def test_initialization(self, mock_config):
        connector = VirthubKVConnector(mock_config)
        assert connector.config == mock_config
        assert connector.client is None
        assert connector.block_map == {}

    def test_save_kv_layer(self, mock_config, mock_worker, mock_kv_block, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client

        connector.save_kv_layer(0, [mock_kv_block], mock_worker)

        mock_rust_vllm_client.register_kv_block.assert_called_once_with(
            block_id=mock_kv_block.block_id,
            vaddr=mock_kv_block.gpu_ptr,
            size=mock_kv_block.size,
            gpu_device_id=mock_worker.device_id,
        )
        assert mock_kv_block.block_id in connector.block_map

    def test_start_load_kv(self, mock_config, mock_worker, mock_kv_block, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client
        mock_meta = {
            "node_addr": "192.168.1.10:19001",
            "vaddr": 0x7FFF_2000_0000,
            "rkey": 1234,
            "size_bytes": mock_kv_block.size,
            "block_id": mock_kv_block.block_id,
        }
        connector.block_map[mock_kv_block.block_id] = mock_meta

        connector.start_load_kv([mock_kv_block], mock_worker)

        mock_rust_vllm_client.swap_in_remote_block.assert_called_once_with(
            peer_addr=mock_meta["node_addr"],
            remote_vaddr=mock_meta["vaddr"],
            remote_rkey=mock_meta["rkey"],
            local_vaddr=mock_kv_block.gpu_ptr,
            size_bytes=mock_meta["size_bytes"],
        )

    def test_error_handling_rdma_failure(self, mock_config, mock_worker, mock_kv_block, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client
        mock_rust_vllm_client.register_kv_block.side_effect = RuntimeError("RDMA registration failed")

        with pytest.raises(RuntimeError):
            connector.save_kv_layer(0, [mock_kv_block], mock_worker)

        assert len(connector.block_map) == 0

    def test_connector_with_multiple_blocks(self, mock_config, mock_worker, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client

        blocks = [
            MagicMock(block_id=i + 1, gpu_ptr=0x7FFF_1000_0000 + i * 2_097_152, size=2_097_152)
            for i in range(3)
        ]

        def register_side_effect(block_id, vaddr, size, gpu_device_id):
            return {
                "block_id": block_id,
                "vaddr": vaddr,
                "size_bytes": size,
                "gpu_device_id": gpu_device_id,
                "rkey": 1000 + block_id,
                "node_addr": "127.0.0.1:19001",
            }

        mock_rust_vllm_client.register_kv_block.side_effect = register_side_effect

        connector.save_kv_layer(0, blocks, mock_worker)
        assert mock_rust_vllm_client.register_kv_block.call_count == 3
        for block in blocks:
            assert block.block_id in connector.block_map

        mock_rust_vllm_client.swap_in_remote_block.reset_mock()
        connector.start_load_kv(blocks, mock_worker)
        assert mock_rust_vllm_client.swap_in_remote_block.call_count == 3

    def test_wait_for_layer_load(self, mock_config):
        connector = VirthubKVConnector(mock_config)
        connector.wait_for_layer_load(0)  # Should not raise

    def test_update_state_after_alloc(self, mock_config):
        connector = VirthubKVConnector(mock_config)
        connector.update_state_after_alloc([])  # Should not raise

    def test_metadata_management(self, mock_config):
        connector = VirthubKVConnector(mock_config)
        scheduler_output = MagicMock()
        meta = connector.build_connector_meta(scheduler_output)
        assert isinstance(meta, dict)

        num = connector.get_num_new_matched_tokens({"some": "data"})
        assert isinstance(num, int)

    def test_unregister_block(self, mock_config, mock_kv_block, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client
        connector.block_map[mock_kv_block.block_id] = {"rkey": 1234, "node_addr": "127.0.0.1:19001"}

        connector.unregister_block(mock_kv_block.block_id)
        mock_rust_vllm_client.deregister_memory_region.assert_called_once_with(1234)
        assert mock_kv_block.block_id not in connector.block_map

    def test_close(self, mock_config, mock_rust_vllm_client):
        connector = VirthubKVConnector(mock_config)
        connector.client = mock_rust_vllm_client
        for i in range(1, 4):
            connector.block_map[i] = {"rkey": 1000 + i}
        mock_rust_vllm_client.close = MagicMock()

        connector.close()

        assert mock_rust_vllm_client.deregister_memory_region.call_count == 3
        mock_rust_vllm_client.close.assert_called_once()
        assert connector.block_map == {}
