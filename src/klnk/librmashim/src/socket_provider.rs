// virthub/src/klnk/librmashim/src/socket_provider.rs

//! TCP socket fallback transport provider.
//!
//! This module provides a basic TCP-based fallback for RDMA operations when
//! hardware acceleration is unavailable or disabled. It uses `tokio::net`
//! for asynchronous I/O, making it compatible with the rest of the async
//! runtime in Virthub.
//!
//! The primary purpose is to send and receive control messages or small data
//! chunks over a reliable stream. All methods are `async` and return
//! `SocketProviderError` on failure.

use std::net::SocketAddr;
use thiserror::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

#[derive(Debug, Error)]
pub enum SocketProviderError {
    #[error("Socket transport failure during operation: {0}")]
    TransportFailure(String),

    #[error("Failed to bind socket fallback to address '{addr}': {reason}")]
    BindError { addr: String, reason: String },

    #[error("Failed to connect to peer '{peer}': {reason}")]
    ConnectError { peer: SocketAddr, reason: String },

    #[error("Socket fallback send buffer overflow")]
    BufferOverflow,

    #[error("I/O error: {0}")]
    IoError(#[from] std::io::Error),
}

/// TCP/Socket fallback transport provider when RDMA hardware channels are unavailable.
#[derive(Debug)]
pub struct SocketTransportFallback {
    bind_addr: String,
    /// Optional listener for incoming connections (if bound).
    listener: Option<TcpListener>,
}

impl SocketTransportFallback {
    /// Creates a new fallback transport with an ephemeral bind address.
    pub fn new() -> Self {
        Self {
            bind_addr: "127.0.0.1:0".to_string(),
            listener: None,
        }
    }

    /// Creates a fallback transport bound to a specific address.
    pub fn with_address(bind_addr: impl Into<String>) -> Self {
        Self {
            bind_addr: bind_addr.into(),
            listener: None,
        }
    }

    /// Asynchronously binds the fallback socket to its configured address and
    /// starts listening for incoming connections.
    ///
    /// This method is idempotent; subsequent calls return the existing listener.
    pub async fn bind(&mut self) -> Result<(), SocketProviderError> {
        if self.listener.is_some() {
            return Ok(());
        }

        let addr: SocketAddr = self
            .bind_addr
            .parse()
            .map_err(|e| SocketProviderError::BindError {
                addr: self.bind_addr.clone(),
                reason: format!("Failed to parse address: {e}"),
            })?;

        let listener = TcpListener::bind(addr).await.map_err(|e| {
            SocketProviderError::BindError {
                addr: self.bind_addr.clone(),
                reason: e.to_string(),
            }
        })?;

        // Update bind_addr to actual bound address (in case port was 0)
        self.bind_addr = listener.local_addr()?.to_string();
        self.listener = Some(listener);
        Ok(())
    }

    /// Returns the actual bound address (after `bind` has been called).
    pub fn bound_address(&self) -> Result<SocketAddr, SocketProviderError> {
        if let Some(listener) = &self.listener {
            listener
                .local_addr()
                .map_err(|e| SocketProviderError::IoError(e))
        } else {
            // Try to parse configured address as fallback
            self.bind_addr
                .parse()
                .map_err(|e| SocketProviderError::BindError {
                    addr: self.bind_addr.clone(),
                    reason: format!("Not bound and address invalid: {e}"),
                })
        }
    }

    /// Establishes a new TCP connection to the given peer.
    pub async fn connect(&self, peer: SocketAddr) -> Result<TcpStream, SocketProviderError> {
        TcpStream::connect(peer).await.map_err(|e| {
            SocketProviderError::ConnectError {
                peer,
                reason: e.to_string(),
            }
        })
    }

    /// Accepts an incoming connection (requires the listener to be bound).
    pub async fn accept(&self) -> Result<(TcpStream, SocketAddr), SocketProviderError> {
        let listener = self.listener.as_ref().ok_or_else(|| {
            SocketProviderError::TransportFailure(
                "Cannot accept: listener not bound".to_string(),
            )
        })?;

        listener.accept().await.map_err(|e| {
            SocketProviderError::TransportFailure(format!(
                "Accept failed: {}",
                e
            ))
        })
    }

    /// Sends a raw byte buffer over an established TCP stream.
    ///
    /// Returns the number of bytes written.
    pub async fn send(
        &self,
        stream: &mut TcpStream,
        buffer: &[u8],
    ) -> Result<usize, SocketProviderError> {
        if buffer.is_empty() {
            return Ok(0);
        }

        stream
            .write_all(buffer)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        stream
            .flush()
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        Ok(buffer.len())
    }

    /// Receives data into the provided buffer.
    ///
    /// Returns the number of bytes read. The buffer must have enough capacity.
    pub async fn recv(
        &self,
        stream: &mut TcpStream,
        buffer: &mut [u8],
    ) -> Result<usize, SocketProviderError> {
        stream
            .read(buffer)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))
    }

    /// Sends a length-prefixed message over the stream. This simplifies
    /// message framing for control messages.
    pub async fn send_message(
        &self,
        stream: &mut TcpStream,
        payload: &[u8],
    ) -> Result<(), SocketProviderError> {
        let len = payload.len() as u32;
        let header = len.to_be_bytes();
        stream
            .write_all(&header)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        stream
            .write_all(payload)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        stream
            .flush()
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        Ok(())
    }

    /// Receives a length-prefixed message, returning the payload as a Vec<u8>.
    pub async fn recv_message(
        &self,
        stream: &mut TcpStream,
    ) -> Result<Vec<u8>, SocketProviderError> {
        let mut header = [0u8; 4];
        stream
            .read_exact(&mut header)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        let len = u32::from_be_bytes(header) as usize;
        let mut payload = vec![0u8; len];
        stream
            .read_exact(&mut payload)
            .await
            .map_err(|e| SocketProviderError::TransportFailure(e.to_string()))?;
        Ok(payload)
    }
}

impl Default for SocketTransportFallback {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_bind_and_connect() {
        let mut server = SocketTransportFallback::with_address("127.0.0.1:0");
        server.bind().await.expect("Bind should succeed");
        let bound_addr = server.bound_address().expect("Get bound address");
        assert!(bound_addr.port() > 0);

        let client = SocketTransportFallback::new();
        let mut stream = client
            .connect(bound_addr)
            .await
            .expect("Connect should succeed");

        let server_accept_task = tokio::spawn(async move {
            let (mut srv_stream, _) = server.accept().await.expect("Accept");
            let mut buf = [0u8; 4];
            let n = srv_stream.read(&mut buf).await.unwrap();
            assert_eq!(n, 4);
            assert_eq!(&buf, b"ping");
        });

        let send_result = client
            .send(&mut stream, b"ping")
            .await
            .expect("Send should succeed");
        assert_eq!(send_result, 4);

        server_accept_task.await.unwrap();
    }

    #[tokio::test]
    async fn test_length_prefixed_message() {
        let mut server = SocketTransportFallback::with_address("127.0.0.1:0");
        server.bind().await.unwrap();
        let bound_addr = server.bound_address().unwrap();

        let client = SocketTransportFallback::new();
        let mut stream = client.connect(bound_addr).await.unwrap();

        let server_task = tokio::spawn(async move {
            let (mut srv_stream, _) = server.accept().await.unwrap();
            let payload = server.recv_message(&mut srv_stream).await.unwrap();
            assert_eq!(payload, b"hello world");
        });

        client
            .send_message(&mut stream, b"hello world")
            .await
            .unwrap();

        server_task.await.unwrap();
    }
}
