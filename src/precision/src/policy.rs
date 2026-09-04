// virthub/src/precision/src/policy.rs

//! Core policy types for the allocation‑time precision predictor.
//!
//! This module defines the numerical precision levels, the packed 32‑bit
//! policy word, and the per‑layer sensitivity profile used by the predictor.
//! The format generation (`g`) is intentionally omitted from the packed
//! policy because it is fixed globally via configuration; only the per‑block
//! precision, residual flag, and active head mask are encoded here.

use serde::{Deserialize, Serialize};

/// Numerical precision tier assigned to a KV‑cache block.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[repr(u8)]
pub enum PrecisionLevel {
    /// Lossless FP16/BF16 storage.
    Fp16 = 0,
    /// Symmetric FP8 storage (E4M3 or E5M2) with per‑head or per‑group scale.
    Fp8 = 1,
    /// FP8 base plus a residual correction (FP8 or FP16 depending on format).
    Fp8Residual = 2,
    /// Partial storage: non‑critical heads are pruned according to `head_mask`.
    Pruned = 3,
}

impl PrecisionLevel {
    /// Converts a raw `u8` value into a `PrecisionLevel`.
    ///
    /// Values 0..=3 map to the corresponding variant; any other value
    /// defaults to `Pruned` for safety (though the predictor never produces
    /// such a value).
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Fp16,
            1 => Self::Fp8,
            2 => Self::Fp8Residual,
            _ => Self::Pruned,
        }
    }
}

/// A 32‑bit packed policy word attached to each KV‑cache block.
///
/// Bit layout:
/// - Bits 0‑1: precision level (`PrecisionLevel`)
/// - Bit 2: residual flag (1 if `p == Fp8Residual`)
/// - Bits 3‑7: reserved (must be zero)
/// - Bits 8‑31: active head mask (24 bits, up to 24 GQA heads)
///
/// This compact representation enables atomic updates and minimal sidecar
/// memory overhead.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct PackedBlockPolicy(u32);

impl PackedBlockPolicy {
    /// Creates a new packed policy from its components.
    ///
    /// * `precision` – the numeric precision tier.
    /// * `residual` – whether a residual page is allocated (`true` only for
    ///   `PrecisionLevel::Fp8Residual`).
    /// * `head_mask` – 24‑bit active head mask; used only when `precision`
    ///   is `PrecisionLevel::Pruned`. For all other levels it should be
    ///   `0xFF_FFFF` (all heads active).
    pub fn new(precision: PrecisionLevel, residual: bool, head_mask: u32) -> Self {
        let p_bits = precision as u32;
        let r_bit = if residual { 1u32 } else { 0u32 };
        let mask = head_mask & 0xFF_FFFF;
        Self(p_bits | (r_bit << 2) | (mask << 8))
    }

    /// Creates a packed policy from a raw 32‑bit word.
    pub fn from_raw(raw: u32) -> Self {
        Self(raw)
    }

    /// Returns the precision level encoded in the policy.
    pub fn precision(&self) -> PrecisionLevel {
        PrecisionLevel::from_u8((self.0 & 0x3) as u8)
    }

    /// Returns `true` if a residual page is allocated.
    pub fn use_residual(&self) -> bool {
        (self.0 >> 2) & 1 == 1
    }

    /// Returns the active head mask (24‑bit).
    pub fn head_mask(&self) -> u32 {
        (self.0 >> 8) & 0xFF_FFFF
    }

    /// Returns the raw 32‑bit policy word.
    pub fn raw(&self) -> u32 {
        self.0
    }
}

/// Static sensitivity profile for a single transformer layer.
///
/// These values are precomputed once at engine startup to avoid runtime
/// branching on the layer index.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct LayerSensitivityProfile {
    /// Score bonus (0 or 1) for layers near the critical boundaries.
    pub base_bonus: u8,
    /// `true` if this layer is considered critical (first 2 or last 2 layers).
    pub is_critical: bool,
    /// `true` if head pruning is allowed in this layer under critical memory
    /// pressure (middle half of the network).
    pub can_prune: bool,
}
