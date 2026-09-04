// virthub/src/precision/tests/lut_tests.rs

//! Integration tests for the precomputed layer sensitivity lookup table.
//!
//! These tests verify that `build_layer_profiles` correctly marks layers as
//! critical, assigns base score bonuses, and determines prunability according
//! to the PSP‑KV specification.

use precision::lut::build_layer_profiles;

#[test]
fn test_empty_layer_count() {
    let profiles = build_layer_profiles(0);
    assert!(profiles.is_empty());
}

#[test]
fn test_one_layer_profile() {
    let profiles = build_layer_profiles(1);
    assert_eq!(profiles.len(), 1);
    let p = profiles[0];
    assert!(p.is_critical);
    assert_eq!(p.base_bonus, 1);
    assert!(p.can_prune);
}

#[test]
fn test_two_layers_profile() {
    let profiles = build_layer_profiles(2);
    assert_eq!(profiles.len(), 2);

    let p0 = profiles[0];
    assert!(p0.is_critical);
    assert_eq!(p0.base_bonus, 1);
    assert!(p0.can_prune);

    let p1 = profiles[1];
    assert!(p1.is_critical);
    assert_eq!(p1.base_bonus, 1);
    assert!(p1.can_prune);
}

#[test]
fn test_four_layers_profile() {
    let profiles = build_layer_profiles(4);
    assert_eq!(profiles.len(), 4);

    // All layers critical for L=4? Critical = l < 2 or l >= L-2 = l >= 2, so yes all.
    for p in &profiles {
        assert!(p.is_critical);
        assert_eq!(p.base_bonus, 1);
    }

    // Prunable: L/4=1, 3L/4=3 => indices 1,2,3 prunable; 0 not.
    assert!(!profiles[0].can_prune);
    assert!(profiles[1].can_prune);
    assert!(profiles[2].can_prune);
    assert!(profiles[3].can_prune);
}

#[test]
fn test_eight_layers_profile() {
    let profiles = build_layer_profiles(8);
    assert_eq!(profiles.len(), 8);

    // Critical: 0,1,6,7
    assert!(profiles[0].is_critical);
    assert!(profiles[1].is_critical);
    assert!(!profiles[2].is_critical);
    assert!(!profiles[3].is_critical);
    assert!(!profiles[4].is_critical);
    assert!(!profiles[5].is_critical);
    assert!(profiles[6].is_critical);
    assert!(profiles[7].is_critical);

    // Bonus: all 1 for L=8 because l<4 or l>=4 covers all.
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
fn test_thirty_two_layers_profile() {
    let profiles = build_layer_profiles(32);
    assert_eq!(profiles.len(), 32);

    // Critical: 0,1,30,31
    assert!(profiles[0].is_critical);
    assert!(profiles[1].is_critical);
    assert!(!profiles[2].is_critical);
    assert!(!profiles[29].is_critical);
    assert!(profiles[30].is_critical);
    assert!(profiles[31].is_critical);

    // Bonus: l<4 or l>=28 -> indices 0,1,2,3 and 28,29,30,31
    assert_eq!(profiles[0].base_bonus, 1);
    assert_eq!(profiles[3].base_bonus, 1);
    assert_eq!(profiles[4].base_bonus, 0);
    assert_eq!(profiles[27].base_bonus, 0);
    assert_eq!(profiles[28].base_bonus, 1);

    // Prunable: L/4=8, 3L/4=24 -> 8..=24 inclusive
    assert!(!profiles[7].can_prune);
    assert!(profiles[8].can_prune);
    assert!(profiles[24].can_prune);
    assert!(!profiles[25].can_prune);
}

#[test]
fn test_profile_field_values_are_correct_types() {
    let profiles = build_layer_profiles(10);
    for p in profiles {
        assert!(p.base_bonus == 0 || p.base_bonus == 1);
        assert!(p.is_critical == false || p.is_critical == true);
        assert!(p.can_prune == false || p.can_prune == true);
    }
}
