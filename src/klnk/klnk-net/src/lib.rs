// virthub/src/klnk/klnk-net/src/lib.rs

//! Network transport and control plane communication for klnk.
//!
//! This crate provides a framed protocol for control messages exchanged
//! over Unix domain sockets, with both `io_uring`‑based zero‑copy messaging
//! and a Tokio fallback.
//!
//! ## Features
//! - Length‑delimited framing with protocol versioning.
//! - Maximum frame size enforcement to prevent DoS.
//! - Optional compression (stub; actual compression can be added later).
//! - Batch message support for reducing per‑message overhead.

pub mod control_transport;

// Re‑export commonly used items
pub use control_transport::{
    recv_control_message_tokio,
    recv_control_message_uring,
    send_control_message_tokio,
    send_control_message_uring,
    ControlBatchCodec,
    ControlCodec,
    ControlFrame,
    ControlMessageBatch,
    ControlMessagePayload,
    TransportError,
    MAX_FRAME_SIZE,
    PROTOCOL_VERSION,
    HEADER_SIZE,
};
