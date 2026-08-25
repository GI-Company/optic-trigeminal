#pragma once

#include "clinical_sim.h"

// Simplified cardiovascular/oxygenation physiology, integrated with a
// classic 4th-order Runge-Kutta step. Replaces the old independent random
// walk in ClinicalSimulator::update_patient_vitals: vitals are now a
// consequence of a small hidden state (InternalPhysiology, see
// clinical_sim.h) rather than randomized per-vital, so a crisis or drug
// effect that changes one underlying thing (e.g. circulating volume) shows
// up as a causally-consistent pattern across HR/BP/SpO2/etc. instead of
// unrelated coincidental jiggling.
//
// This is a teaching-tool-grade simplification (a 3-state Windkessel-style
// model), not a validated physiological simulator -- see LIMITATIONS.md.

// Advances phys by dt_seconds (internally sub-stepped for numerical
// stability) and writes the resulting observable vitals into v, with a
// small amount of measurement noise so it still feels "alive." Every
// output is guaranteed finite and clamped to physiologically-survivable
// bounds (see apply_hard_limits below) before this returns.
void step_physiology(InternalPhysiology& phys, Vitals& v, float dt_seconds);

// Applies crisis-driven parameter modifiers for one tick. crisis_type is
// one of "", "Respiratory Failure", "Sepsis", "Septic Shock", "Hypovolemic
// Shock" -- "" (no crisis) applies no pathological pull, letting the
// standing relaxation-toward-baseline in step_physiology recover the
// patient over time.
void apply_crisis_physiology(InternalPhysiology& phys, const std::string& crisis_type, float dt_seconds);

// Applies and decays every entry in active_drugs against phys, erasing
// any whose remaining_seconds has run out. Called once per tick.
void apply_drug_effects(InternalPhysiology& phys, std::vector<ActiveDrug>& active_drugs, float dt_seconds);

// Resets phys to a healthy baseline -- an explicit, instant reset (used by
// ClinicalSimulator::reset_patient), not the gradual relaxation
// step_physiology applies on its own.
void reset_physiology_to_baseline(InternalPhysiology& phys);

// NaN/Inf guards + hard clamps to physiologically-survivable bounds, run
// on both the ODE state and the derived vitals every tick. A genuinely
// rebuilt, right-sized version of the "edge diagnostics" idea -- keeps the
// NaN/Inf/clamp safety net, skips the elaborate streak-to-Fatal escalation
// machinery a training simulator doesn't need.
void apply_hard_limits(InternalPhysiology& phys, Vitals& v);
