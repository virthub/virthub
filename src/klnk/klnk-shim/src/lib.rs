// virthub/src/klnk/klnk-shim/src/lib.rs

//! KLNK Interposition Shim Library
//!
//! This crate provides a shared library (`libklnk_shim.so`) that can be
//! preloaded (via `LD_PRELOAD`) into target applications to intercept memory
//! allocation and shared memory operations, automatically registering regions
//! with the local Virthub daemon.
//!
//! The library includes:
//! - [`ipc_client`] for communication with the daemon.
//! - [`staging`] for pre‑allocated memory used for zero‑copy page moves.
//!
//! In addition to the modules, the library exposes a global [`DAEMON_CLIENT`]
//! lazy static that holds a `Mutex<DaemonIpcClient>`. This client is initialised
//! using the socket path from the `VIRTHUB_CONTROL_SOCKET` environment variable
//! (or a default of `/tmp/virthub_control.sock`). Interposition hooks can use
//! this client to register regions on demand.
//!
//! **Note:** Actual interposition hooks for `mmap`, `shmget`, etc. are not yet
//! implemented; they will be added in a future revision.

pub mod ipc_client;
pub mod staging;

use once_cell::sync::Lazy;
use std::sync::Mutex;

/// Global IPC client for interposition hooks.
///
/// The socket path can be overridden by setting `VIRTHUB_CONTROL_SOCKET`.
/// The client is lazy‑initialised on first use and protected by a mutex.
pub static DAEMON_CLIENT: Lazy<Mutex<ipc_client::DaemonIpcClient>> = Lazy::new(|| {
    let socket_path = std::env::var("VIRTHUB_CONTROL_SOCKET")
        .unwrap_or_else(|_| "/tmp/virthub_control.sock".to_string());
    Mutex::new(ipc_client::DaemonIpcClient::new(socket_path))
});
