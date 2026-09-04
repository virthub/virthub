// virthub/src/precision/src/hysteresis.rs

//! Multi‑tier memory‑pressure hysteresis state machine.
//!
//! This module implements a 3‑state Schmitt trigger that prevents the
//! precision predictor from thrashing between precision tiers when GPU
//! memory pressure oscillates around threshold boundaries.
//!
//! The states are:
//! - `Nominal`   (0) – normal serving, no aggressive compression.
//! - `Elevated`  (1) – moderate memory pressure, allow FP8/residual.
//! - `Critical`  (2) – severe memory pressure, enable head pruning in
//!                     non‑critical middle layers.
//!
//! Transitions incorporate hysteresis: moving to a higher state requires
//! crossing a higher threshold, while falling back requires dropping below
//! a lower threshold.

/// Memory‑pressure state used by the precision predictor.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum MemoryPressureState {
    /// Normal memory pressure.
    Nominal = 0,
    /// Elevated memory pressure.
    Elevated = 1,
    /// Critical memory starvation.
    Critical = 2,
}

impl MemoryPressureState {
    /// Updates the hysteresis state based on the current memory pressure
    /// ratio `mu` (0.0 to 1.0). The thresholds are:
    ///
    /// - `Nominal -> Elevated` when `mu >= 0.78`
    /// - `Nominal -> Critical` when `mu >= 0.88` (jump directly to critical)
    /// - `Elevated -> Nominal` when `mu < 0.70`
    /// - `Elevated -> Critical` when `mu >= 0.88`
    /// - `Critical -> Elevated` when `mu < 0.82`
    /// - `Critical -> Nominal` not allowed directly; must go through Elevated.
    pub fn update(self, mu: f64) -> Self {
        match self {
            Self::Nominal => {
                if mu >= 0.88 {
                    Self::Critical
                } else if mu >= 0.78 {
                    Self::Elevated
                } else {
                    Self::Nominal
                }
            }
            Self::Elevated => {
                if mu >= 0.88 {
                    Self::Critical
                } else if mu < 0.70 {
                    Self::Nominal
                } else {
                    Self::Elevated
                }
            }
            Self::Critical => {
                if mu < 0.82 {
                    Self::Elevated
                } else {
                    Self::Critical
                }
            }
        }
    }

    /// Returns the integer state code (0, 1, or 2).
    pub fn as_u8(self) -> u8 {
        self as u8
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_nominal_to_elevated() {
        let state = MemoryPressureState::Nominal;
        assert_eq!(state.update(0.77), MemoryPressureState::Nominal);
        assert_eq!(state.update(0.78), MemoryPressureState::Elevated);
        assert_eq!(state.update(0.87), MemoryPressureState::Elevated);
    }

    #[test]
    fn test_nominal_to_critical_direct() {
        let state = MemoryPressureState::Nominal;
        assert_eq!(state.update(0.88), MemoryPressureState::Critical);
        assert_eq!(state.update(0.95), MemoryPressureState::Critical);
    }

    #[test]
    fn test_elevated_to_nominal() {
        let state = MemoryPressureState::Elevated;
        assert_eq!(state.update(0.70), MemoryPressureState::Elevated);
        assert_eq!(state.update(0.69), MemoryPressureState::Nominal);
    }

    #[test]
    fn test_elevated_to_critical() {
        let state = MemoryPressureState::Elevated;
        assert_eq!(state.update(0.87), MemoryPressureState::Elevated);
        assert_eq!(state.update(0.88), MemoryPressureState::Critical);
    }

    #[test]
    fn test_critical_to_elevated() {
        let state = MemoryPressureState::Critical;
        assert_eq!(state.update(0.82), MemoryPressureState::Critical);
        assert_eq!(state.update(0.81), MemoryPressureState::Elevated);
    }

    #[test]
    fn test_no_direct_critical_to_nominal() {
        // From Critical, mu < 0.82 should go to Elevated, not Nominal.
        let state = MemoryPressureState::Critical;
        assert_eq!(state.update(0.50), MemoryPressureState::Elevated);
    }
}
