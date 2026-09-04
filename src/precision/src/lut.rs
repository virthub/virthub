// virthub/src/precision/src/lut.rs

//! Precomputed layer sensitivity lookup table (LUT).
//!
//! To eliminate runtime branching on the layer index, the predictor builds
//! a static table at engine startup. Each entry contains three boolean
//! properties:
//!
//! - `base_bonus`  – `true` for layers near the critical boundaries
//!                   (first 4 or last 4 layers).
//! - `is_critical` – `true` for the first 2 and last 2 layers.
//! - `can_prune`   – `true` for the middle half of the network, where head
//!                   pruning is allowed under critical memory pressure.

use super::policy::LayerSensitivityProfile;

/// Builds the layer sensitivity lookup table for a model with `total_layers`
/// transformer layers.
///
/// The table size equals `total_layers`. The properties are computed using
/// the exact boundary definitions from the design document.
pub fn build_layer_profiles(total_layers: usize) -> Vec<LayerSensitivityProfile> {
    (0..total_layers)
        .map(|l| {
            let is_critical = l < 2 || l >= total_layers - 2;
            let base_bonus = if l < 4 || l >= total_layers - 4 { 1 } else { 0 };
            let can_prune = l >= total_layers / 4 && l <= (3 * total_layers) / 4;
            LayerSensitivityProfile {
                base_bonus,
                is_critical,
                can_prune,
            }
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_small_layer_count() {
        // 8 layers: indices 0..7
        let profiles = build_layer_profiles(8);
        assert_eq!(profiles.len(), 8);

        // Critical: 0,1,6,7
        assert!(profiles[0].is_critical);
        assert!(profiles[1].is_critical);
        assert!(!profiles[2].is_critical);
        assert!(!profiles[5].is_critical);
        assert!(profiles[6].is_critical);
        assert!(profiles[7].is_critical);

        // Bonus: all 1 for L=8 (l<4 or l>=4 covers all)
        for p in &profiles {
            assert_eq!(p.base_bonus, 1);
        }

        // Prunable: L/4=2, 3L/4=6 => 2,3,4,5,6 prunable; 0,1,7 not.
        assert!(!profiles[0].can_prune);
        assert!(!profiles[1].can_prune);
        assert!(profiles[2].can_prune);
        assert!(profiles[3].can_prune);
        assert!(profiles[4].can_prune);
        assert!(profiles[5].can_prune);
        assert!(profiles[6].can_prune);
        assert!(!profiles[7].can_prune);
    }

    #[test]
    fn test_large_layer_count() {
        // 32 layers
        let profiles = build_layer_profiles(32);
        assert_eq!(profiles.len(), 32);

        // Critical
        assert!(profiles[0].is_critical);
        assert!(profiles[1].is_critical);
        assert!(!profiles[2].is_critical);
        assert!(!profiles[29].is_critical);
        assert!(profiles[30].is_critical);
        assert!(profiles[31].is_critical);

        // Bonus
        assert_eq!(profiles[0].base_bonus, 1);
        assert_eq!(profiles[3].base_bonus, 1);
        assert_eq!(profiles[4].base_bonus, 0);
        assert_eq!(profiles[27].base_bonus, 0);
        assert_eq!(profiles[28].base_bonus, 1);

        // Prunable: L/4=8, 3L/4=24, so l=8..=24 inclusive
        assert!(!profiles[7].can_prune);
        assert!(profiles[8].can_prune);
        assert!(profiles[24].can_prune);
        assert!(!profiles[25].can_prune);
    }

    #[test]
    fn test_edge_one_layer() {
        // Degenerate case: 1 layer
        let profiles = build_layer_profiles(1);
        assert_eq!(profiles.len(), 1);
        assert!(profiles[0].is_critical);       // l<2 true
        assert_eq!(profiles[0].base_bonus, 1);  // l<4 true
        // According to formula: prunable = floor(L/4) <= l <= floor(3L/4)
        // L=1 => 0 <= 0 <= 0 => true
        assert!(profiles[0].can_prune);
    }
}
