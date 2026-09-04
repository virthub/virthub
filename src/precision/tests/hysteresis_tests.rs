// virthub/src/precision/tests/hysteresis_tests.rs

//! Integration tests for the multi‑tier memory‑pressure hysteresis state machine.
//!
//! These tests verify the transition logic of `MemoryPressureState` under
//! various memory pressure ratios (`mu`). The thresholds are:
//! - Nominal -> Elevated at mu >= 0.78
//! - Nominal -> Critical at mu >= 0.88 (direct jump)
//! - Elevated -> Nominal at mu < 0.70
//! - Elevated -> Critical at mu >= 0.88
//! - Critical -> Elevated at mu < 0.82
//! - Critical never falls directly to Nominal (must pass through Elevated)

use precision::hysteresis::MemoryPressureState as H;

#[test]
fn test_nominal_stays_nominal_below_thresholds() {
    let state = H::Nominal;
    assert_eq!(state.update(0.0), H::Nominal);
    assert_eq!(state.update(0.5), H::Nominal);
    assert_eq!(state.update(0.77), H::Nominal);
}

#[test]
fn test_nominal_to_elevated_at_threshold() {
    let state = H::Nominal;
    assert_eq!(state.update(0.78), H::Elevated);
}

#[test]
fn test_nominal_to_critical_direct() {
    let state = H::Nominal;
    assert_eq!(state.update(0.88), H::Critical);
    assert_eq!(state.update(0.95), H::Critical);
}

#[test]
fn test_elevated_stays_elevated_between_thresholds() {
    let state = H::Elevated;
    assert_eq!(state.update(0.70), H::Elevated); // not below 0.70 yet
    assert_eq!(state.update(0.75), H::Elevated);
    assert_eq!(state.update(0.87), H::Elevated); // not >= 0.88
}

#[test]
fn test_elevated_to_nominal_below_low_threshold() {
    let state = H::Elevated;
    assert_eq!(state.update(0.69), H::Nominal);
}

#[test]
fn test_elevated_to_critical_at_high_threshold() {
    let state = H::Elevated;
    assert_eq!(state.update(0.88), H::Critical);
}

#[test]
fn test_critical_stays_critical_above_relax_threshold() {
    let state = H::Critical;
    assert_eq!(state.update(0.82), H::Critical);
    assert_eq!(state.update(0.88), H::Critical);
    assert_eq!(state.update(1.0), H::Critical);
}

#[test]
fn test_critical_to_elevated_below_relax_threshold() {
    let state = H::Critical;
    assert_eq!(state.update(0.81), H::Elevated);
}

#[test]
fn test_critical_does_not_go_directly_to_nominal() {
    let state = H::Critical;
    // Even a very low pressure should go to Elevated, not Nominal.
    assert_eq!(state.update(0.0), H::Elevated);
}

#[test]
fn test_full_hysteresis_cycle() {
    let mut state = H::Nominal;
    // Rise to critical
    state = state.update(0.88);
    assert_eq!(state, H::Critical);
    // Drop to elevated
    state = state.update(0.81);
    assert_eq!(state, H::Elevated);
    // Drop to nominal
    state = state.update(0.69);
    assert_eq!(state, H::Nominal);
    // Rise to elevated again
    state = state.update(0.78);
    assert_eq!(state, H::Elevated);
    // Rise to critical
    state = state.update(0.88);
    assert_eq!(state, H::Critical);
}
