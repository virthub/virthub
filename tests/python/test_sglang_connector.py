# tests/python/test_sglang_connector.py
#
# Unit tests for the Virthub SGLang connector (VirthubSglangConnector).
#
# Tests target the in-memory stub implementation, which is used when the
# Rust client is unavailable. All connector operations are synchronous
# in the stub implementation.
#
# Tests are skipped if SGLang is not installed or the connector module
# cannot be imported.

import pytest
from unittest.mock import MagicMock
from virthub.sglang import VirthubSglangConnector


@pytest.fixture
def mock_config():
    return {
        "control_socket": "/tmp/virthub_control.sock",
        "data_bind_addr": "0.0.0.0:19001",
        "transport": {"default_protocol": "tcp"},
    }


class TestVirthubSglangConnector:
    def test_initialization(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        assert connector.config == mock_config
        assert connector.client is not None
        assert connector.block_map == {}

    def test_register_prefix_node(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        prefix_hash = 0xABCD_1234_5678
        meta = connector.register_prefix_node(
            prefix_hash=prefix_hash,
            token_count=64,
            vaddr=0x7FFF_4000_0000,
            size_bytes=2 * 1024 * 1024,
            gpu_device_id=0,
        )

        assert meta is not None
        assert meta["prefix_hash"] == prefix_hash
        assert meta["token_count"] == 64
        assert meta["vaddr"] == 0x7FFF_4000_0000
        assert "rkey" in meta
        assert prefix_hash in connector.block_map

    def test_fetch_remote_prefix(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        connector.fetch_remote_prefix(
            peer_addr="192.168.1.20:19001",
            remote_vaddr=0x7FFF_5000_0000,
            remote_rkey=9876,
            local_vaddr=0x7FFF_6000_0000,
            size_bytes=1024,
        )

    def test_error_handling(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        mock_client = MagicMock()
        mock_client.register_prefix_node.side_effect = RuntimeError("fail")
        connector._client = mock_client

        with pytest.raises(RuntimeError):
            connector.register_prefix_node(
                prefix_hash=1,
                token_count=1,
                vaddr=0x1000,
                size_bytes=4096,
                gpu_device_id=0,
            )

    def test_multiple_prefixes(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        prefixes = [
            {"prefix_hash": 0x1111, "token_count": 10, "vaddr": 0x1000, "size_bytes": 1024},
            {"prefix_hash": 0x2222, "token_count": 20, "vaddr": 0x2000, "size_bytes": 2048},
            {"prefix_hash": 0x3333, "token_count": 30, "vaddr": 0x3000, "size_bytes": 4096},
        ]

        for p in prefixes:
            connector.register_prefix_node(
                prefix_hash=p["prefix_hash"],
                token_count=p["token_count"],
                vaddr=p["vaddr"],
                size_bytes=p["size_bytes"],
                gpu_device_id=0,
            )

        for p in prefixes:
            assert p["prefix_hash"] in connector.block_map

    def test_unregister_prefix(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        prefix_hash = 0xDEAD
        connector.register_prefix_node(
            prefix_hash=prefix_hash,
            token_count=1,
            vaddr=0x7000,
            size_bytes=4096,
            gpu_device_id=0,
        )

        connector.unregister_prefix(prefix_hash)
        assert prefix_hash not in connector.block_map

    def test_close(self, mock_config):
        connector = VirthubSglangConnector(mock_config)
        connector.register_prefix_node(
            prefix_hash=1,
            token_count=1,
            vaddr=0x1000,
            size_bytes=4096,
            gpu_device_id=0,
        )
        connector.register_prefix_node(
            prefix_hash=2,
            token_count=1,
            vaddr=0x2000,
            size_bytes=4096,
            gpu_device_id=0,
        )

        connector.close()
        assert connector.block_map == {}
