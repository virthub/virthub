// virthub/src/precision/src/lib.rs

//! Allocation‑time precision prediction engine for PSP‑KV.
//!
//! This crate provides a deterministic, low‑overhead predictor that assigns a
//! numerical precision level (0: FP16, 1: FP8, 2: FP8+Residual, 3: Pruned) to
//! each KV‑cache block at block allocation time. The predictor uses only
//! static structural features (layer index, token position, head mask) and a
//! multi‑tier memory‑pressure hysteresis state. It does not rely on runtime
//! attention telemetry, background threads, or GPU‑to‑host synchronization.
//!
//! The physical storage format (Basic, Enhanced, or BT‑KV) is **not** selected
//! by this crate; it is fixed globally via configuration and passed to the
//! packing function by the upper layer. Only the precision level and residual
//! flag are encoded into the 32‑bit packed policy word.

pub mod hysteresis;
pub mod lut;
pub mod policy;
pub mod predictor;

pub use hysteresis::MemoryPressureState;
pub use policy::{LayerSensitivityProfile, PackedBlockPolicy, PrecisionLevel};
pub use predictor::Predictor;
