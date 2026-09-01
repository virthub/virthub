// virthub/src/klnk/klnk-shim/src/ipc_client.rs

//! Low‑latency IPC client connecting the interposition shim to the local node daemon.
//!
//! This module provides a `DaemonIpcClient` that communicates with `klnk-daemon`
//! over a Unix domain socket using a simple length‑delimited framing protocol.
//! All messages are serialized with `bincode`. The client supports automatic
//! reconnection and configurable timeouts.

use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::Duration;
use thiserror::Error;
use serde::{Deserialize, Serialize};
use klnk_core::domain::{GlobalRegionId, MemoryRegionDescriptor};

#[derive(Debug, Error)]
pub enum IpcError {
    #[error("Socket connection failed for path '{path}': {reason}")]
    ConnectError { path: String, reason: String },

    #[error("IPC I/O error: {0}")]
    IoError(#[from] std::io::Error),

    #[error("Serialization error: {0}")]
    SerializationError(#[from] bincode::Error),

    #[error("Client is not connected to daemon socket")]
    NotConnected,

    #[error("Operation timed out after {0:?}")]
    Timeout(Duration),

    #[error("Daemon returned an error: {0}")]
    DaemonError(String),
}

/// Request types sent from the shim to the daemon.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IpcRequest {
    RegisterRegion {
        region_id: GlobalRegionId,
        vaddr: u64,
        size: usize,
        flags: u32,
    },
    DeregisterRegion {
        region_id: GlobalRegionId,
    },
    Heartbeat,
}

/// Response types returned by the daemon.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum IpcResponse {
    Ack { success: bool, message: String },
    RegionRegistered(MemoryRegionDescriptor),
    RegionDeregistered,
    Pong,
}

/// Low‑latency IPC client connecting the interposition shim to the local node daemon.
pub struct DaemonIpcClient {
    socket_path: String,
    stream: Option<UnixStream>,
    timeout: Duration,
}

impl DaemonIpcClient {
    /// Create a new daemon IPC client pointing to a Unix socket path.
    pub fn new(socket_path: impl Into<String>) -> Self {
        Self {
            socket_path: socket_path.into(),
            stream: None,
            timeout: Duration::from_secs(5),
        }
    }

    /// Set the timeout for request/response operations.
    pub fn with_timeout(mut self, timeout: Duration) -> Self {
        self.timeout = timeout;
        self
    }

    /// Establish connection to the local klnk daemon Unix socket.
    pub fn connect(&mut self) -> Result<(), IpcError> {
        let stream = UnixStream::connect(&self.socket_path).map_err(|e| {
            IpcError::ConnectError {
                path: self.socket_path.clone(),
                reason: e.to_string(),
            }
        })?;
        // Set read/write timeouts to avoid indefinite blocking.
        stream.set_read_timeout(Some(self.timeout))?;
        stream.set_write_timeout(Some(self.timeout))?;
        self.stream = Some(stream);
        Ok(())
    }

    /// Establish connection with retry.
    pub fn connect_with_retry(
        &mut self,
        max_retries: usize,
        retry_delay: Duration,
    ) -> Result<(), IpcError> {
        for attempt in 0..=max_retries {
            match self.connect() {
                Ok(()) => return Ok(()),
                Err(e) if attempt < max_retries => {
                    std::thread::sleep(retry_delay);
                    continue;
                }
                Err(e) => return Err(e),
            }
        }
        unreachable!()
    }

    /// Check if client is currently connected.
    pub fn is_connected(&self) -> bool {
        self.stream.is_some()
    }

    /// Close the connection.
    pub fn disconnect(&mut self) {
        self.stream = None;
    }

    /// Send a request and wait for a response.
    fn send_request(&mut self, request: &IpcRequest) -> Result<IpcResponse, IpcError> {
        let stream = self.stream.as_mut().ok_or(IpcError::NotConnected)?;

        // Serialize request.
        let payload = bincode::serialize(request)?;

        // Write length prefix and payload.
        let len = payload.len() as u32;
        stream.write_all(&len.to_be_bytes())?;
        stream.write_all(&payload)?;
        stream.flush()?;

        // Read response length prefix.
        let mut header = [0u8; 4];
        stream.read_exact(&mut header)?;
        let resp_len = u32::from_be_bytes(header) as usize;

        // Read response payload.
        let mut resp_payload = vec![0u8; resp_len];
        stream.read_exact(&mut resp_payload)?;

        // Deserialize response.
        let response: IpcResponse = bincode::deserialize(&resp_payload)?;
        Ok(response)
    }

    /// Request region registration over the Unix domain socket connection.
    pub fn register_region(
        &mut self,
        region_id: GlobalRegionId,
        vaddr: u64,
        size: usize,
        flags: u32,
    ) -> Result<MemoryRegionDescriptor, IpcError> {
        if !self.is_connected() {
            return Err(IpcError::NotConnected);
        }

        let request = IpcRequest::RegisterRegion {
            region_id,
            vaddr,
            size,
            flags,
        };

        match self.send_request(&request)? {
            IpcResponse::RegionRegistered(desc) => Ok(desc),
            IpcResponse::Ack { success: false, message } => Err(IpcError::DaemonError(message)),
            _ => Err(IpcError::DaemonError("Unexpected response".to_string())),
        }
    }

    /// Request region deregistration.
    pub fn deregister_region(&mut self, region_id: GlobalRegionId) -> Result<(), IpcError> {
        if !self.is_connected() {
            return Err(IpcError::NotConnected);
        }

        let request = IpcRequest::DeregisterRegion { region_id };
        match self.send_request(&request)? {
            IpcResponse::RegionDeregistered => Ok(()),
            IpcResponse::Ack { success: false, message } => Err(IpcError::DaemonError(message)),
            _ => Err(IpcError::DaemonError("Unexpected response".to_string())),
        }
    }

    /// Send a heartbeat and check daemon liveness.
    pub fn heartbeat(&mut self) -> Result<(), IpcError> {
        if !self.is_connected() {
            return Err(IpcError::NotConnected);
        }

        let request = IpcRequest::Heartbeat;
        match self.send_request(&request)? {
            IpcResponse::Pong => Ok(()),
            _ => Err(IpcError::DaemonError("Unexpected heartbeat response".to_string())),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::os::unix::net::UnixListener;
    use tempfile::tempdir;
    use klnk_core::domain::MemoryProtectionFlags;

    #[test]
    fn test_daemon_ipc_client_initialization() {
        let client = DaemonIpcClient::new("/var/run/klnk.sock");
        assert!(!client.is_connected());
    }

    #[test]
    fn test_register_region_not_connected() {
        let mut client = DaemonIpcClient::new("/var/run/klnk.sock");
        let region_id = GlobalRegionId {
            owner_pid: 1,
            shmid: 1,
        };
        let err = client.register_region(region_id, 0x1000, 4096, 0x3);
        assert!(matches!(err, Err(IpcError::NotConnected)));
    }

    #[test]
    fn test_full_roundtrip() {
        let dir = tempdir().unwrap();
        let socket_path = dir.path().join("test.sock");
        let listener = UnixListener::bind(&socket_path).unwrap();

        // Spawn a simple server thread.
        let server_thread = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            // Read request
            let mut header = [0u8; 4];
            stream.read_exact(&mut header).unwrap();
            let len = u32::from_be_bytes(header) as usize;
            let mut payload = vec![0u8; len];
            stream.read_exact(&mut payload).unwrap();
            let request: IpcRequest = bincode::deserialize(&payload).unwrap();

            // Process request
            let response = match request {
                IpcRequest::RegisterRegion { region_id, vaddr, size, flags } => {
                    IpcResponse::RegionRegistered(MemoryRegionDescriptor {
                        region_id,
                        main_vaddr: vaddr,
                        region_size: size,
                        staging_vaddr: vaddr,
                        staging_num_pages: size / 4096,
                        staging_page_size: 4096,
                        prot_flags: MemoryProtectionFlags(flags),
                        mem_flags: 0,
                        version: 0,
                    })
                }
                _ => IpcResponse::Ack {
                    success: false,
                    message: "unexpected".to_string(),
                },
            };

            let resp_payload = bincode::serialize(&response).unwrap();
            let resp_len = resp_payload.len() as u32;
            stream.write_all(&resp_len.to_be_bytes()).unwrap();
            stream.write_all(&resp_payload).unwrap();
        });

        let mut client = DaemonIpcClient::new(socket_path.to_str().unwrap());
        client.connect().unwrap();

        let region_id = GlobalRegionId {
            owner_pid: 123,
            shmid: 456,
        };
        let desc = client
            .register_region(region_id, 0x7fff_0000_0000, 4096, 0x3)
            .unwrap();

        assert_eq!(desc.region_id, region_id);
        assert_eq!(desc.main_vaddr, 0x7fff_0000_0000);
        assert_eq!(desc.region_size, 4096);

        server_thread.join().unwrap();
    }
}
