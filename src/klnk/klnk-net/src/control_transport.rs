// virthub/src/klnk/klnk-net/src/control_transport.rs

//! Control plane transport using length‑delimited framing and bincode serialization.
//!
//! ## Enhancements
//! - **Maximum frame size** validation to prevent memory exhaustion attacks.
//! - **Protocol versioning** in the header.
//! - **Optional compression** for large payloads (feature‑gated; currently a stub).
//! - **Batch message support** to reduce per‑message overhead.
//!
//! All existing APIs remain compatible; new functionality is additive.

use bytes::{Buf, BufMut, BytesMut};
use serde::{Deserialize, Serialize};
use std::io;
use thiserror::Error;
use tokio_util::codec::{Decoder, Encoder};

/// Maximum allowed frame size (16 MB) to prevent DoS.
pub const MAX_FRAME_SIZE: usize = 16 * 1024 * 1024;

/// Protocol version (1 byte in header).
pub const PROTOCOL_VERSION: u8 = 1;

/// Minimum header size: version (1) + length (4) + flags (1) = 6 bytes.
pub const HEADER_SIZE: usize = 6;

// Flags bit definitions
const FLAG_COMPRESSED: u8 = 0x01;
const FLAG_BATCH: u8 = 0x02;

#[derive(Debug, Error)]
pub enum TransportError {
    #[error("Network I/O failure: {0}")]
    IoError(#[from] io::Error),

    #[error("Serialization failure: {0}")]
    SerializationError(#[from] bincode::Error),

    #[error("Invalid control frame size or payload")]
    InvalidFrame,

    #[error("Frame size {0} exceeds maximum allowed size {MAX_FRAME_SIZE}")]
    FrameTooLarge(usize),

    #[error("Unsupported protocol version {0}")]
    UnsupportedVersion(u8),

    #[error("io_uring operation failed: {0}")]
    UringError(String),

    #[error("Compression or decompression failed: {0}")]
    CompressionError(String),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum ControlMessagePayload {
    RegionRegister {
        pid: u32,
        vaddr: u64,
        size: usize,
        flags: u32,
    },
    RegionDeregister {
        pid: u32,
        vaddr: u64,
    },
    LockAcquire {
        region_id: u64,
        offset: u64,
        len: u64,
    },
    LockRelease {
        region_id: u64,
        offset: u64,
        len: u64,
    },
    Heartbeat {
        node_id: u32,
        timestamp: u64,
    },
    ResponseAck {
        success: bool,
        message: String,
    },
}

/// A single control frame with version and optional compression flag.
#[derive(Debug, Clone)]
pub struct ControlFrame {
    pub version: u8,
    pub flags: u8,
    pub length: u32,
    pub payload: ControlMessagePayload,
}

/// Codec that supports single and batch frames, compression, and versioning.
pub struct ControlCodec {
    // Optional compression level; 0 means no compression. Could be extended.
    compression_level: u32,
}

impl Default for ControlCodec {
    fn default() -> Self {
        Self {
            compression_level: 0,
        }
    }
}

impl ControlCodec {
    /// Create a codec with compression enabled (level 1-9; 0 disables).
    pub fn with_compression(level: u32) -> Self {
        Self {
            compression_level: level.min(9),
        }
    }

    /// Internal helper to serialize payload and apply compression if requested.
    fn serialize_payload(&self, item: &ControlMessagePayload) -> Result<Vec<u8>, TransportError> {
        let serialized = bincode::serialize(item)?;

        if self.compression_level > 0 && serialized.len() > 1024 {
            // Placeholder compression: in a real implementation, use flate2/lz4.
            // Here we simply mark as not compressed to avoid dependency.
            // For now, return uncompressed data.
            Ok(serialized)
        } else {
            Ok(serialized)
        }
    }

    /// Internal helper to deserialize payload, decompressing if flag set.
    fn deserialize_payload(
        &self,
        data: &[u8],
        flags: u8,
    ) -> Result<ControlMessagePayload, TransportError> {
        if flags & FLAG_COMPRESSED != 0 {
            // Placeholder decompression
            // In real code, decompress here.
            // Since we never set the flag, this should not be reached.
            return Err(TransportError::CompressionError(
                "Compressed frames not yet supported".to_string(),
            ));
        }
        let msg = bincode::deserialize(data)?;
        Ok(msg)
    }
}

impl Decoder for ControlCodec {
    type Item = ControlFrame;
    type Error = TransportError;

    fn decode(&mut self, src: &mut BytesMut) -> Result<Option<Self::Item>, Self::Error> {
        if src.len() < HEADER_SIZE {
            return Ok(None);
        }

        // Read header
        let version = src[0];
        if version != PROTOCOL_VERSION {
            return Err(TransportError::UnsupportedVersion(version));
        }

        let mut length_bytes = [0u8; 4];
        length_bytes.copy_from_slice(&src[1..5]);
        let frame_len = u32::from_be_bytes(length_bytes) as usize;

        let flags = src[5];

        if frame_len > MAX_FRAME_SIZE {
            return Err(TransportError::FrameTooLarge(frame_len));
        }

        if src.len() < HEADER_SIZE + frame_len {
            return Ok(None);
        }

        // Extract payload (skip version + length + flags)
        src.advance(HEADER_SIZE);
        let payload_buf = src.split_to(frame_len);

        // Deserialize payload
        let msg = self.deserialize_payload(&payload_buf, flags)?;

        Ok(Some(ControlFrame {
            version,
            flags,
            length: frame_len as u32,
            payload: msg,
        }))
    }
}

impl Encoder<ControlMessagePayload> for ControlCodec {
    type Error = TransportError;

    fn encode(
        &mut self,
        item: ControlMessagePayload,
        dst: &mut BytesMut,
    ) -> Result<(), Self::Error> {
        let serialized = self.serialize_payload(&item)?;

        // Write header
        dst.put_u8(PROTOCOL_VERSION);
        dst.put_u32(serialized.len() as u32);
        // Flags: compression not set in this stub.
        let flags = 0u8;
        dst.put_u8(flags);
        dst.put_slice(&serialized);
        Ok(())
    }
}

/// A batch of control messages.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ControlMessageBatch {
    pub messages: Vec<ControlMessagePayload>,
}

/// Codec for batch messages.
pub struct ControlBatchCodec;

impl Default for ControlBatchCodec {
    fn default() -> Self {
        Self
    }
}

impl Decoder for ControlBatchCodec {
    type Item = Vec<ControlMessagePayload>;
    type Error = TransportError;

    fn decode(&mut self, src: &mut BytesMut) -> Result<Option<Self::Item>, Self::Error> {
        if src.len() < HEADER_SIZE {
            return Ok(None);
        }

        let version = src[0];
        if version != PROTOCOL_VERSION {
            return Err(TransportError::UnsupportedVersion(version));
        }

        let mut length_bytes = [0u8; 4];
        length_bytes.copy_from_slice(&src[1..5]);
        let frame_len = u32::from_be_bytes(length_bytes) as usize;
        let flags = src[5];

        if frame_len > MAX_FRAME_SIZE {
            return Err(TransportError::FrameTooLarge(frame_len));
        }

        if src.len() < HEADER_SIZE + frame_len {
            return Ok(None);
        }

        src.advance(HEADER_SIZE);
        let payload_buf = src.split_to(frame_len);

        // Decode as batch
        if flags & FLAG_BATCH == 0 {
            return Err(TransportError::InvalidFrame);
        }

        // Placeholder: assume payload is bincode-serialized ControlMessageBatch
        let batch: ControlMessageBatch = bincode::deserialize(&payload_buf)?;
        Ok(Some(batch.messages))
    }
}

impl Encoder<Vec<ControlMessagePayload>> for ControlBatchCodec {
    type Error = TransportError;

    fn encode(
        &mut self,
        items: Vec<ControlMessagePayload>,
        dst: &mut BytesMut,
    ) -> Result<(), Self::Error> {
        let batch = ControlMessageBatch { messages: items };
        let serialized = bincode::serialize(&batch)?;

        dst.put_u8(PROTOCOL_VERSION);
        dst.put_u32(serialized.len() as u32);
        dst.put_u8(FLAG_BATCH);
        dst.put_slice(&serialized);
        Ok(())
    }
}

pub async fn send_control_message_uring(
    stream: &tokio_uring::net::UnixStream,
    msg: &ControlMessagePayload,
) -> Result<(), TransportError> {
    let serialized = bincode::serialize(msg)?;
    let len = serialized.len() as u32;

    let mut buf = Vec::with_capacity(HEADER_SIZE + serialized.len());
    buf.push(PROTOCOL_VERSION);
    buf.extend_from_slice(&len.to_be_bytes());
    buf.push(0); // flags
    buf.extend_from_slice(&serialized);

    let (res, _) = stream.write(buf).await;
    res.map_err(|e| TransportError::UringError(e.to_string()))?;
    Ok(())
}

pub async fn recv_control_message_uring(
    stream: &tokio_uring::net::UnixStream,
) -> Result<ControlMessagePayload, TransportError> {
    let (res, header_vec) = stream.read(vec![0u8; HEADER_SIZE]).await;
    let n = res.map_err(|e| TransportError::UringError(e.to_string()))?;
    if n != HEADER_SIZE {
        return Err(TransportError::InvalidFrame);
    }

    let version = header_vec[0];
    if version != PROTOCOL_VERSION {
        return Err(TransportError::UnsupportedVersion(version));
    }

    let header_buf: [u8; 4] = header_vec[1..5].try_into().unwrap();
    let len = u32::from_be_bytes(header_buf) as usize;
    let flags = header_vec[5];

    if len > MAX_FRAME_SIZE {
        return Err(TransportError::FrameTooLarge(len));
    }

    let (res, payload_vec) = stream.read(vec![0u8; len]).await;
    let n = res.map_err(|e| TransportError::UringError(e.to_string()))?;
    if n != len {
        return Err(TransportError::InvalidFrame);
    }

    // Decompress if necessary (stub)
    if flags & FLAG_COMPRESSED != 0 {
        return Err(TransportError::CompressionError(
            "Compressed frames not yet supported".to_string(),
        ));
    }

    let msg: ControlMessagePayload = bincode::deserialize(&payload_vec)?;
    Ok(msg)
}

pub async fn send_control_message_tokio(
    stream: &mut tokio::net::UnixStream,
    msg: &ControlMessagePayload,
) -> Result<(), TransportError> {
    use tokio::io::AsyncWriteExt;
    let serialized = bincode::serialize(msg)?;
    let len = serialized.len() as u32;

    let mut buf = Vec::with_capacity(HEADER_SIZE + serialized.len());
    buf.push(PROTOCOL_VERSION);
    buf.extend_from_slice(&len.to_be_bytes());
    buf.push(0); // flags
    buf.extend_from_slice(&serialized);

    stream.write_all(&buf).await?;
    Ok(())
}

pub async fn recv_control_message_tokio(
    stream: &mut tokio::net::UnixStream,
) -> Result<ControlMessagePayload, TransportError> {
    use tokio::io::AsyncReadExt;
    let mut header = [0u8; HEADER_SIZE];
    stream.read_exact(&mut header).await?;

    let version = header[0];
    if version != PROTOCOL_VERSION {
        return Err(TransportError::UnsupportedVersion(version));
    }

    let len = u32::from_be_bytes(header[1..5].try_into().unwrap()) as usize;
    let flags = header[5];

    if len > MAX_FRAME_SIZE {
        return Err(TransportError::FrameTooLarge(len));
    }

    let mut payload = vec![0u8; len];
    stream.read_exact(&mut payload).await?;

    if flags & FLAG_COMPRESSED != 0 {
        return Err(TransportError::CompressionError(
            "Compressed frames not yet supported".to_string(),
        ));
    }

    let msg: ControlMessagePayload = bincode::deserialize(&payload)?;
    Ok(msg)
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::tempdir;

    #[test]
    fn test_serialization_roundtrip() {
        let msg = ControlMessagePayload::Heartbeat {
            node_id: 1,
            timestamp: 12345,
        };
        let serialized = bincode::serialize(&msg).unwrap();
        let deserialized: ControlMessagePayload = bincode::deserialize(&serialized).unwrap();
        match deserialized {
            ControlMessagePayload::Heartbeat { node_id, timestamp } => {
                assert_eq!(node_id, 1);
                assert_eq!(timestamp, 12345);
            }
            _ => panic!("Wrong variant"),
        }
    }

    #[test]
    fn test_codec_max_frame_size() {
        let mut codec = ControlCodec::default();
        let mut src = BytesMut::new();
        // Craft a header with excessive length
        src.put_u8(PROTOCOL_VERSION);
        src.put_u32((MAX_FRAME_SIZE as u32) + 1);
        src.put_u8(0);
        let err = codec.decode(&mut src).unwrap_err();
        assert!(matches!(err, TransportError::FrameTooLarge(_)));
    }

    #[tokio::test(flavor = "current_thread")]
    async fn test_tokio_fallback_send_recv() {
        use tokio::net::{UnixListener, UnixStream};

        let dir = tempdir().unwrap();
        let socket_path = dir.path().join("test.sock");
        let listener = UnixListener::bind(&socket_path).unwrap();

        let addr = socket_path.to_str().unwrap().to_string();

        let server = async {
            let (mut stream, _) = listener.accept().await.unwrap();
            let msg = recv_control_message_tokio(&mut stream).await.unwrap();
            match msg {
                ControlMessagePayload::Heartbeat { node_id, timestamp } => {
                    assert_eq!(node_id, 42);
                    assert_eq!(timestamp, 999);
                }
                _ => panic!("Wrong message"),
            }
        };

        let client = async {
            let mut stream = UnixStream::connect(&addr).await.unwrap();
            let msg = ControlMessagePayload::Heartbeat {
                node_id: 42,
                timestamp: 999,
            };
            send_control_message_tokio(&mut stream, &msg).await.unwrap();
        };

        tokio::join!(server, client);
    }

    #[tokio::test(flavor = "current_thread")]
    async fn test_batch_codec_roundtrip() {
        let mut codec = ControlBatchCodec::default();
        let mut dst = BytesMut::new();
        let msgs = vec![
            ControlMessagePayload::Heartbeat { node_id: 1, timestamp: 111 },
            ControlMessagePayload::Heartbeat { node_id: 2, timestamp: 222 },
        ];
        codec.encode(msgs, &mut dst).unwrap();

        let mut src = dst.clone();
        let decoded = codec.decode(&mut src).unwrap().unwrap();
        assert_eq!(decoded.len(), 2);
        assert!(matches!(decoded[0], ControlMessagePayload::Heartbeat { node_id: 1, .. }));
    }
}
