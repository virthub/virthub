# tests/python/test_lmcache_connector.py
#
# Unit tests for the Virthub LMCache connector (VirthubLmCacheConnector).
#
# Tests target the in-memory stub implementation, which is used when the
# Rust client is unavailable. All connector operations are asynchronous.
#
# Tests are skipped if the required LMCache module is not installed or
# the connector cannot be imported.

import pytest
from virthub.lmcache import VirthubLmCacheConnector, KvBlockKey, StorageTier


@pytest.fixture
def mock_config():
    return {
        "control_socket": "/tmp/virthub_control.sock",
        "data_bind_addr": "0.0.0.0:19001",
        "transport": {"default_protocol": "tcp"},
    }


@pytest.fixture
def mock_chunk_key():
    return KvBlockKey(1, 500)


@pytest.fixture
def mock_chunk_payload():
    return b"\xEE" * 1024  # 1KB of test data


class TestVirthubLmCacheConnector:
    def test_initialization(self, mock_config):
        connector = VirthubLmCacheConnector(mock_config)
        assert connector.config == mock_config
        # With the stub, client is an _InMemoryClient, not None
        assert connector.client is not None
        # block_map is empty initially
        assert connector.block_map == {}
        # The stub's internal store is a dict, accessible via client.store
        assert isinstance(connector.client.store, dict)

    @pytest.mark.asyncio
    async def test_put_chunk(self, mock_config, mock_chunk_key, mock_chunk_payload):
        connector = VirthubLmCacheConnector(mock_config)
        vaddr = 0x7FFF_5000_0000
        length = len(mock_chunk_payload)

        meta = await connector.put_chunk(
            key=mock_chunk_key,
            tier=StorageTier.Dram,
            gpu_device_id=0,
            payload=mock_chunk_payload,
            vaddr=vaddr,
            length=length,
        )

        # The stub returns a plain dict; access by key
        assert meta is not None
        assert meta["key"] == mock_chunk_key
        assert meta["size_bytes"] == length
        assert meta["vaddr"] == vaddr
        # block_map should be updated
        assert mock_chunk_key.block_id in connector.block_map

    @pytest.mark.asyncio
    async def test_get_chunk(self, mock_config, mock_chunk_key, mock_chunk_payload):
        connector = VirthubLmCacheConnector(mock_config)
        # Put a chunk first
        await connector.put_chunk(
            key=mock_chunk_key,
            tier=StorageTier.Dram,
            gpu_device_id=0,
            payload=mock_chunk_payload,
            vaddr=0x7FFF_5000_0000,
            length=len(mock_chunk_payload),
        )

        retrieved = await connector.get_chunk(mock_chunk_key)
        assert retrieved == mock_chunk_payload

    @pytest.mark.asyncio
    async def test_remove_chunk(self, mock_config, mock_chunk_key, mock_chunk_payload):
        connector = VirthubLmCacheConnector(mock_config)
        await connector.put_chunk(
            key=mock_chunk_key,
            tier=StorageTier.Dram,
            gpu_device_id=0,
            payload=mock_chunk_payload,
            vaddr=0x7FFF_5000_0000,
            length=len(mock_chunk_payload),
        )

        await connector.remove_chunk(mock_chunk_key)

        # After removal, retrieval should fail
        with pytest.raises(KeyError):
            await connector.get_chunk(mock_chunk_key)

    @pytest.mark.asyncio
    async def test_fetch_remote_chunk(self, mock_config, mock_chunk_key):
        connector = VirthubLmCacheConnector(mock_config)
        # fetch_remote_chunk is a method of the connector, not the internal client
        await connector.fetch_remote_chunk(
            key=mock_chunk_key,
            peer_addr="192.168.1.30:19001",
            remote_vaddr=0x7FFF_6000_0000,
            remote_rkey=54321,
            local_vaddr=0x7FFF_7000_0000,
            size_bytes=2048,
        )

    def test_error_handling(self, mock_config, mock_chunk_key, mock_chunk_payload):
        connector = VirthubLmCacheConnector(mock_config)
        # The stub never raises, but we can verify that a missing key raises
        with pytest.raises(KeyError):
            import asyncio
            asyncio.run(connector.get_chunk(KvBlockKey(99, 99)))

    @pytest.mark.asyncio
    async def test_multiple_chunks(self, mock_config):
        connector = VirthubLmCacheConnector(mock_config)
        keys = [
            (KvBlockKey(1, 101), b"\x01" * 512),
            (KvBlockKey(1, 102), b"\x02" * 1024),
            (KvBlockKey(2, 201), b"\x03" * 2048),
        ]

        for key, payload in keys:
            await connector.put_chunk(
                key=key,
                tier=StorageTier.Dram,
                gpu_device_id=0,
                payload=payload,
                vaddr=0x7FFF_5000_0000 + key.block_id * 4096,
                length=len(payload),
            )

        for key, payload in keys:
            assert await connector.get_chunk(key) == payload

    @pytest.mark.asyncio
    async def test_close(self, mock_config, mock_chunk_key, mock_chunk_payload):
        connector = VirthubLmCacheConnector(mock_config)
        await connector.put_chunk(
            key=mock_chunk_key,
            tier=StorageTier.Dram,
            gpu_device_id=0,
            payload=mock_chunk_payload,
            vaddr=0x7FFF_5000_0000,
            length=len(mock_chunk_payload),
        )

        await connector.close()
        # After close, the internal store is cleared
        with pytest.raises(KeyError):
            await connector.get_chunk(mock_chunk_key)
