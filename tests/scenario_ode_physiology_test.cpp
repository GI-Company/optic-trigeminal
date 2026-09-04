// Golden tests for ScenarioRuntime's ODE-physiology path
// (ScenarioDefinition::uses_ode_physiology, currently HYPOTENSION_001,
// SEVERE_BLEEDING_001, and ANAPHYLAXIS_001 -- see training_scenario.h's own
// comment for why the other scenarios stay on the legacy curve engine for
// now).
//
// Verifies the specific things this migration needs to hold:
//   1. A fresh (and every randomized) session starts on the non-triggering
//      side of the scenario's own grading threshold (bp_sys >= 90) --
//      randomize_case's existing invariant, now protected against a
//      different engine underneath it.
//   2. The scripted crisis actually develops and crosses the grading
//      threshold within a similar tick count to the original curve-based
//      scenario's own authored timeline (severe_hypotension at t=15min).
//   3. The scenario's "correct" actions (apply_iv_fluids/start_vasopressor)
//      measurably raise bp_sys via the real drug-effects pipeline, not just
//      get graded correct in isolation from any physiological consequence.
//
// SEVERE_BLEEDING_001 is structurally different from HYPOTENSION_001 in a
// way worth calling out: its authored baseline already starts mid-crisis
// (bp_sys=88, already below the massive_transfusion<90 gate) rather than
// healthy-then-declining, and hemorrhage_control/emergency_surgery stop the
// crisis's ongoing depletion (source control) rather than mapping to a drug
// -- see ode_action_stops_crisis and the crisis_ever_activated_ latch in
// training_scenario.cpp/.h. A real bug was found and fixed via the timing
// probe that led to these tests: the original onset-scan re-armed the
// crisis on this scenario's later timeline events (increased_bleeding at
// t=7, hemorrhagic_shock at t=14) even after hemorrhage_control had
// correctly stopped it, silently undoing source control a few ticks later.
//
// ANAPHYLAXIS_001 is the first migrated scenario needing genuinely new
// engine surface, not just reuse: a new "Anaphylaxis" crisis type
// (apply_crisis_physiology) combining vasodilation (Sepsis's own
// mechanism, without the infection/volume side effects) and
// bronchoconstriction (Respiratory Failure's own oxygenation_efficiency/
// shunt_fraction rates, reused directly), plus a new DrugType::Epinephrine
// modeling real alpha+beta pharmacology as three simultaneous physiology
// terms. Also the first scenario needing the constructor's baseline
// seeding extended to oxygenation_efficiency (not just arterial_pressure/
// circulating_volume), since its authored baseline is already hypoxic
// (spo2=88) as well as hypotensive (bp_sys=78). Unlike the other two
// migrated scenarios, none of its three actions gate on a vitals
// threshold at all (epinephrine_im's correctness gates purely on elapsed
// time), so this migration carries essentially no grading-timing risk --
// the timing probe here was about physiological plausibility, not
// grading correctness.
//
// RESPIRATORY_001 reuses the existing "Respiratory Failure" crisis type
// and DrugType::Oxygen unmodified -- zero new engine surface. It was
// previously investigated (see LIMITATIONS.md's history) and flagged as
// NOT migrating cleanly, based on a compound failure condition
// (SpO2<85 AND RR>30) never firing under the ODE engine at any time-scale
// tried. Re-checked here: (a) evaluate_action_correctness's own
// RESPIRATORY_001 branch grades apply_oxygen/call_respiratory on ONE
// vital each, independently -- neither depends on the compound condition
// at all; (b) a probe against the *unmodified legacy engine's own*
// scripted timeline (RR's total authored delta is only +6, 20->26) found
// RR barely reaches the low 30s even there, confirming the compound
// condition was already marginal/near-unreachable under the original
// authored numbers, not a regression this migration introduces. RR itself
// isn't an independent physiology variable in this engine -- it's a
// formula output of oxygenation_efficiency/infection_burden (see
// step_physiology) -- so both graded vitals move together off the same
// underlying state, unlike Sepsis's genuinely independent BP/temp
// mechanisms (which is why Sepsis remains unmigrated).
//
// A real gap was found and fixed across all four migrated scenarios while
// investigating Stroke Alert's own complication mechanism: the legacy
// engine's recompute_vitals_with_overlay applies a triggered failure's
// FailureCondition::complication_effect curve (real, non-empty data for
// HYPOTENSION_001's no_intervention_20min and both of
// SEVERE_BLEEDING_001's failure conditions), but
// update_vitals_via_ode_physiology had no equivalent step at all -- that
// effect was silently dropped for any scenario migrated to the ODE engine.
// Fixed via apply_complication_effects_via_ode, a one-time physiology-state
// nudge (not a repeating curve -- see its own comment for why) applied the
// tick after a complication first arms.
//
// DKA_CRISIS_001 needed one genuinely new mechanism, smaller than the
// "whole new glucose/ketone/pH/potassium subsystem" an earlier pass through
// this project's own plan assumed it would: its grading is entirely
// unconditional or ordering-based (obtain_labs unconditional, iv_fluids
// near-unconditional since baseline RR is already >24, insulin_infusion
// gated only on "was iv_fluids given first" -- the same shape as Stroke
// Alert's tPA/CT ordering check), so the actual new work was purely about
// physiological plausibility, not grading risk. The one real gap: Kussmaul
// breathing (compensatory tachypnea for metabolic acidosis) needed a new
// InternalPhysiology::metabolic_acidosis state variable and its own term in
// step_physiology's RR formula, since folding it into oxygenation_efficiency
// (the existing RR-driving mechanism) would have wrongly dropped SpO2 too --
// DKA's SpO2 stays clinically normal, unlike Respiratory Failure/Sepsis's
// hypoxia-driven RR. A first version of the new crisis's accumulation rate
// (0.012f/s) was found via probe to sit *below* metabolic_acidosis's own
// standing relax rate (0.02f/s) -- since this scenario starts already
// acidotic (seeded from its authored baseline RR), that meant the seeded
// value was already above its own equilibrium and actually declined
// instead of rising untreated. Fixed by raising the rate to 0.03f/s. A
// similar probe-driven fix was needed for DrugType::Insulin's own reversal
// rate, initially too weak to produce any visible effect against the
// crisis's accumulation.
#include "../include/training_scenario.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

int g_checked = 0;
int g_failed = 0;

void check(bool cond, const std::string& what) {
    g_checked++;
    if (!cond) {
        g_failed++;
        std::cerr << "FAIL: " << what << "\n";
    }
}

void test_baseline_is_non_triggering() {
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();
    check(def.uses_ode_physiology, "HYPOTENSION_001 should have uses_ode_physiology=true");

    ScenarioRuntime rt(def);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.bp_sys >= 90, "Fresh HYPOTENSION_001 session should start with bp_sys >= 90 (non-triggering), got " + std::to_string(v.bp_sys));

    ActionEvaluation eval = rt.evaluate_action_correctness("apply_iv_fluids");
    check(eval.grade == ActionGrade::PREMATURE,
          "Giving fluids at t=0, before the scripted decline, should grade PREMATURE");
}

void test_randomize_case_invariant() {
    // The whole point of randomize_case's jitter design (see its own header
    // comment) is that it must never accidentally start a session already
    // past its own grading threshold. Sweep enough seeds to catch a rare
    // off-by-a-little jitter bug, not just the default case.
    ScenarioDefinition base = ScenarioLibrary::create_hypotension_scenario();
    int violations = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        ScenarioDefinition randomized = ScenarioLibrary::randomize_case(base, seed);
        ScenarioRuntime rt(randomized);
        ScenarioVitals v = rt.get_current_vitals();
        if (v.bp_sys < 90) violations++;
    }
    check(violations == 0, "randomize_case should never produce a starting bp_sys < 90 across 200 seeds, got " + std::to_string(violations) + " violations");
}

void test_crisis_develops_and_crosses_threshold() {
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();
    ScenarioRuntime rt(def);

    int crossing_tick = -1;
    for (int tick = 0; tick <= 25 && crossing_tick < 0; ++tick) {
        ScenarioVitals v = rt.get_current_vitals();
        if (v.bp_sys < 90) crossing_tick = tick;
        rt.tick(60); // matches the real training tick cadence (delta_seconds=60)
    }
    check(crossing_tick >= 0, "bp_sys should cross below 90 within 25 training ticks");
    // Original curve-authored timeline's severe_hypotension marker is at
    // t=15min (tick 15 at this cadence) -- a wide but meaningful band
    // around that, not an exact match (different engine, real physiology
    // instead of a scripted delta).
    check(crossing_tick >= 8 && crossing_tick <= 22,
          "bp_sys crossing below 90 should land in a similar window to the original t=15min marker (got tick " +
          std::to_string(crossing_tick) + "), not within the first couple of ticks or never");

    // HR should be causally rising as pressure falls (baroreceptor
    // response) -- not just BP moving in isolation.
    ScenarioVitals final_v = rt.get_current_vitals();
    check(final_v.hr > def.synthetic_patient.baseline_vitals.hr,
          "HR should rise above baseline as BP falls (compensatory tachycardia), got " + std::to_string(final_v.hr));
}

void test_fluids_slow_the_decline() {
    // Under the real physiology, Crystalloid's volume-replacement rate
    // (0.004/s) is genuinely weaker than Hypovolemic Shock's ongoing
    // depletion rate (0.012/s) -- fluids alone measurably *slow* the
    // decline rather than fully reversing it. That's not a bug to paper
    // over: it matches this scenario's own escalating-severity design
    // (REC-001 suggests fluids, REC-002 escalates to a vasopressor at a
    // lower threshold), and real clinical teaching that fluids alone often
    // aren't sufficient for ongoing volume loss. Verified via an
    // untreated-control comparison, not an absolute "went up" assertion,
    // since asserting full reversal from fluids alone would have been
    // asserting something clinically wrong.
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();

    ScenarioRuntime untreated(def);
    for (int i = 0; i < 10; ++i) untreated.tick(60);
    int u_start = untreated.get_current_vitals().bp_sys;
    for (int i = 0; i < 5; ++i) untreated.tick(60);
    int u_end = untreated.get_current_vitals().bp_sys;

    ScenarioRuntime treated(def);
    for (int i = 0; i < 10; ++i) treated.tick(60);
    int t_start = treated.get_current_vitals().bp_sys;
    treated.accept_action("apply_iv_fluids", "RN_TEST");
    for (int i = 0; i < 5; ++i) treated.tick(60);
    int t_end = treated.get_current_vitals().bp_sys;

    int untreated_decline = u_start - u_end;
    int treated_decline = t_start - t_end;
    check(treated_decline < untreated_decline,
          "apply_iv_fluids should measurably slow bp_sys's decline vs. an untreated control (untreated declined " +
          std::to_string(untreated_decline) + ", treated declined " + std::to_string(treated_decline) + ")");
}

void test_vasopressor_reverses_shock() {
    // Unlike fluids, Norepinephrine's SVR/contractility boost is strong
    // enough to fully reverse this crisis, matching REC-002's escalation
    // intent -- confirms the scenario's overall "fluids slow it, a
    // vasopressor is the definitive fix" narrative actually holds under
    // the real engine, not just each drug in isolation.
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();
    ScenarioRuntime rt(def);
    for (int i = 0; i < 22; ++i) rt.tick(60); // well past crisis onset, past the noisy tick~16 borderline
    int before = rt.get_current_vitals().bp_sys;
    check(before < 90, "Sanity check: should be in shock (bp_sys<90) by tick 22, got " + std::to_string(before));

    rt.accept_action("start_vasopressor", "RN_TEST");
    for (int i = 0; i < 10; ++i) rt.tick(60);
    int after = rt.get_current_vitals().bp_sys;

    check(after > before + 15,
          "start_vasopressor should substantially raise bp_sys (before=" + std::to_string(before) +
          ", after=" + std::to_string(after) + ")");
}

void test_action_correctness_still_reads_ode_vitals() {
    // evaluate_action_correctness's own scenario-id branch (unmodified by
    // this migration) reads current_vitals_.bp_sys directly -- confirm it's
    // actually seeing the ODE-computed value, not some stale/legacy field.
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();
    ScenarioRuntime rt(def);
    for (int i = 0; i < 20; ++i) rt.tick(60);
    ScenarioVitals v = rt.get_current_vitals();
    ActionEvaluation eval = rt.evaluate_action_correctness("start_vasopressor");
    if (v.bp_sys < 90) {
        check(eval.grade == ActionGrade::CORRECT, "start_vasopressor with bp_sys<90 (ODE-computed) should grade CORRECT");
    } else {
        check(eval.grade == ActionGrade::PREMATURE, "start_vasopressor with bp_sys>=90 (ODE-computed) should grade PREMATURE");
    }
}

void test_complication_effect_applies_via_ode() {
    // HYPOTENSION_001's own complication (no_intervention_20min /
    // no_action_within_20_min -- {BP_sys:-10, HR:+8}) should produce a
    // real, attributable extra decline in bp_sys once it fires (~20
    // simulated minutes with zero actions taken), on top of the ongoing
    // crisis's own smooth decline -- not silently do nothing, which is
    // what happened before apply_complication_effects_via_ode existed.
    //
    // A true paired A/B, not a within-run comparison: two runs given the
    // exact same std::srand() seed before each (so noise()'s per-tick
    // jitter is byte-for-byte identical in both), differing only in
    // whether "notify_provider" -- an action absent from
    // ode_drug_for_action's HYPOTENSION_001 mapping, so it has zero
    // physiological effect of its own -- is taken early enough to keep
    // action_history_ non-empty, which is all no_action_within_20_min's
    // "zero actions ever taken" condition checks. This eliminates noise as
    // a confound entirely (two earlier designs -- a single tick's delta,
    // then a windowed average within one run -- were both still sensitive
    // to whatever global rand() state this test happens to inherit from
    // whichever tests ran before it in the same process, since this file
    // doesn't seed std::srand anywhere else). Verified robust across 30
    // explicit seeds via a standalone probe before landing on this design.
    ScenarioDefinition def = ScenarioLibrary::create_hypotension_scenario();
    constexpr unsigned kPairedSeed = 42;

    std::srand(kPairedSeed);
    ScenarioRuntime with_complication(def);
    for (int i = 0; i < 25; ++i) with_complication.tick(60);
    int bp_with = with_complication.get_current_vitals().bp_sys;

    std::srand(kPairedSeed);
    ScenarioRuntime without_complication(def);
    without_complication.accept_action("notify_provider", "RN_TEST");
    for (int i = 0; i < 25; ++i) without_complication.tick(60);
    int bp_without = without_complication.get_current_vitals().bp_sys;

    check(bp_with < bp_without,
          "bp_sys should end up lower when the no-intervention complication can fire than in an "
          "identical (same noise seed) run where a no-op action kept it from ever firing "
          "(with_complication=" + std::to_string(bp_with) + ", without=" + std::to_string(bp_without) + ")");
}

void test_bleeding_baseline_already_in_shock() {
    // Unlike Hypotension, this scenario's own authored design starts
    // already mid-crisis -- confirm that intent survives the engine swap
    // (bp_sys<90 at t=0) and that massive_transfusion is therefore graded
    // CORRECT immediately, not PREMATURE (there's no "too early" here --
    // the patient arrives already in shock).
    ScenarioDefinition def = ScenarioLibrary::create_severe_bleeding_scenario();
    check(def.uses_ode_physiology, "SEVERE_BLEEDING_001 should have uses_ode_physiology=true");

    ScenarioRuntime rt(def);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.bp_sys < 90, "Fresh SEVERE_BLEEDING_001 session should start already in shock (bp_sys<90), got " +
          std::to_string(v.bp_sys));

    ActionEvaluation transfusion = rt.evaluate_action_correctness("massive_transfusion");
    check(transfusion.grade == ActionGrade::CORRECT,
          "massive_transfusion at t=0 should grade CORRECT given the authored already-in-shock baseline");

    ActionEvaluation control = rt.evaluate_action_correctness("hemorrhage_control");
    check(control.grade == ActionGrade::CORRECT, "hemorrhage_control should always grade CORRECT (no vitals gate)");

    ActionEvaluation surgery = rt.evaluate_action_correctness("emergency_surgery");
    check(surgery.grade == ActionGrade::CORRECT, "emergency_surgery should always grade CORRECT (no vitals gate)");
}

void test_bleeding_randomize_case_mostly_starts_in_shock() {
    // randomize_case's jitter is a flat, generic percentage shared by every
    // scenario -- for Hypotension's healthy-start baseline that never
    // crosses its own (much lower) grading threshold, but this scenario's
    // baseline (88) sits only 2 points below its own threshold (90), so
    // jitter measurably can (and, verified via timing probe, does for
    // ~30% of seeds) push a session to start on the non-shock side. That's
    // a pre-existing characteristic of the shared jitter function applied
    // to this scenario's own authored baseline, identical under the legacy
    // curve engine too -- not something this migration introduces, and not
    // a grading-correctness bug (massive_transfusion still grades
    // correctly either way, just against a different starting state). This
    // is a regression guard against that ratio moving further, not a
    // strict invariant like Hypotension's.
    ScenarioDefinition base = ScenarioLibrary::create_severe_bleeding_scenario();
    int starts_in_shock = 0;
    for (uint32_t seed = 0; seed < 200; ++seed) {
        ScenarioDefinition randomized = ScenarioLibrary::randomize_case(base, seed);
        ScenarioRuntime rt(randomized);
        if (rt.get_current_vitals().bp_sys < 90) starts_in_shock++;
    }
    check(starts_in_shock > 100,
          "the majority of randomized SEVERE_BLEEDING_001 sessions should still start in shock, got " +
          std::to_string(starts_in_shock) + "/200");
}

void test_bleeding_untreated_worsens() {
    ScenarioDefinition def = ScenarioLibrary::create_severe_bleeding_scenario();
    ScenarioRuntime rt(def);
    int start = rt.get_current_vitals().bp_sys;
    for (int i = 0; i < 20; ++i) rt.tick(60);
    int end = rt.get_current_vitals().bp_sys;
    check(end < start, "Untreated SEVERE_BLEEDING_001 should keep worsening (bp_sys declining), start=" +
          std::to_string(start) + " end=" + std::to_string(end));
}

void test_bleeding_source_control_reverses_decline() {
    // hemorrhage_control has no drug mapping -- it works entirely by
    // stopping the crisis (ode_action_stops_crisis) and letting
    // step_physiology's own standing relaxation recover circulating_volume
    // and SVR back toward baseline. A/B against untreated, mirroring
    // test_fluids_slow_the_decline's comparison style, since this is a
    // fundamentally different mechanism (crisis-stop, not a drug dose)
    // than Hypotension's actions and deserves its own direct verification
    // rather than assuming the same shape of test transfers.
    ScenarioDefinition def = ScenarioLibrary::create_severe_bleeding_scenario();

    ScenarioRuntime untreated(def);
    for (int i = 0; i < 20; ++i) untreated.tick(60);
    int u_end = untreated.get_current_vitals().bp_sys;

    ScenarioRuntime treated(def);
    treated.accept_action("hemorrhage_control", "RN_TEST");
    for (int i = 0; i < 20; ++i) treated.tick(60);
    int t_end = treated.get_current_vitals().bp_sys;

    check(t_end > u_end,
          "hemorrhage_control should leave bp_sys measurably higher than an untreated control after 20 ticks "
          "(untreated=" + std::to_string(u_end) + ", treated=" + std::to_string(t_end) + ")");
}

void test_bleeding_source_control_not_re_armed_by_later_timeline_events() {
    // Regression test for the exact bug the timing probe caught: without
    // the crisis_ever_activated_ latch, this scenario's later timeline
    // events (increased_bleeding at t=7, hemorrhagic_shock at t=14) would
    // silently re-activate the crisis a few ticks after hemorrhage_control
    // had already stopped it, undoing source control. 30 ticks runs well
    // past both of those markers.
    ScenarioDefinition def = ScenarioLibrary::create_severe_bleeding_scenario();
    ScenarioRuntime rt(def);
    rt.accept_action("hemorrhage_control", "RN_TEST");
    int after_control = rt.get_current_vitals().bp_sys;
    int min_seen = after_control;
    for (int i = 0; i < 30; ++i) {
        rt.tick(60);
        min_seen = std::min(min_seen, rt.get_current_vitals().bp_sys);
    }
    int final_v = rt.get_current_vitals().bp_sys;
    check(final_v >= after_control,
          "bp_sys should not end up lower than it was right after hemorrhage_control, even 30 ticks later "
          "(right after control=" + std::to_string(after_control) + ", 30 ticks later=" + std::to_string(final_v) + ")");
    check(min_seen >= after_control - 5,
          "bp_sys should not dip meaningfully below its post-control level at any point in the next 30 ticks "
          "(would indicate the crisis got silently re-armed), min seen=" + std::to_string(min_seen) +
          " vs post-control=" + std::to_string(after_control));
}

void test_bleeding_massive_transfusion_helps_despite_active_crisis() {
    // massive_transfusion deliberately does NOT stop the crisis (see
    // ode_drug_for_action's comment -- transfusion without source control
    // is a real, intentional teaching point from this scenario's own
    // REC-001-before-REC-002 ordering). Confirm it still measurably helps
    // relative to doing nothing, even while the bleed is ongoing.
    ScenarioDefinition def = ScenarioLibrary::create_severe_bleeding_scenario();

    ScenarioRuntime untreated(def);
    for (int i = 0; i < 20; ++i) untreated.tick(60);
    int u_end = untreated.get_current_vitals().bp_sys;

    ScenarioRuntime treated(def);
    treated.accept_action("massive_transfusion", "RN_TEST");
    for (int i = 0; i < 20; ++i) treated.tick(60);
    int t_end = treated.get_current_vitals().bp_sys;

    check(t_end > u_end,
          "massive_transfusion should leave bp_sys measurably higher than an untreated control after 20 ticks "
          "even without source control (untreated=" + std::to_string(u_end) + ", treated=" + std::to_string(t_end) + ")");
}

void test_anaphylaxis_baseline_matches_authored() {
    // Unlike Hypotension, this scenario's authored baseline is ALREADY
    // critical on two independent vitals (bp_sys=78, spo2=88) -- confirm
    // the constructor's oxygenation_efficiency seeding (added alongside
    // this migration) keeps SpO2 at its authored value too, not just BP,
    // so there's no first-tick jump toward a healthy ~98% before any
    // crisis physiology has run.
    ScenarioDefinition def = ScenarioLibrary::create_anaphylaxis_scenario();
    check(def.uses_ode_physiology, "ANAPHYLAXIS_001 should have uses_ode_physiology=true");

    ScenarioRuntime rt(def);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.bp_sys == def.synthetic_patient.baseline_vitals.bp_sys,
          "Fresh ANAPHYLAXIS_001 session should start at the authored bp_sys, got " + std::to_string(v.bp_sys));
    check(v.spo2 == def.synthetic_patient.baseline_vitals.spo2,
          "Fresh ANAPHYLAXIS_001 session should start at the authored spo2, got " + std::to_string(v.spo2));
}

void test_anaphylaxis_driver_attribution_is_vasodilation_not_hypovolemia() {
    // Regression test for a real bug caught live (via the new per-tick
    // driver badge in TrainingMode.ts): the constructor's generic
    // circulating_volume seeding (backing out volume from bp_sys so
    // target_pressure matches the authored starting point, avoiding a
    // first-tick jump toward a healthy ~120 -- see the constructor's own
    // comment) was being applied unconditionally, including to
    // ANAPHYLAXIS_001, whose own crisis mechanism is vasodilation
    // (systemic_vascular_resistance) and never touches circulating_volume
    // at all. That made dominant_physiology_driver's "hypovolemia"
    // candidate spuriously dominant from the seed alone -- a real
    // causal-attribution bug, not just a display quirk, since it named the
    // wrong physiological mechanism. Fixed by seeding
    // systemic_vascular_resistance instead for this scenario specifically.
    ScenarioDefinition def = ScenarioLibrary::create_anaphylaxis_scenario();
    ScenarioRuntime rt(def);
    PhysiologyDriver driver = rt.get_dominant_driver();
    check(driver.id == "vasodilation",
          "ANAPHYLAXIS_001's dominant driver at t=0 should be vasodilation (its own crisis mechanism), got '" +
          driver.id + "'");
    check(driver.id != "hypovolemia",
          "ANAPHYLAXIS_001 should never attribute to hypovolemia -- its crisis never touches circulating_volume");
}

void test_anaphylaxis_untreated_worsens() {
    // Both vitals should decline -- this crisis moves SVR and oxygenation/
    // shunt simultaneously (see apply_crisis_physiology's "Anaphylaxis"
    // branch), unlike Hypotension/Severe Bleeding's single-mechanism
    // Hypovolemic Shock.
    ScenarioDefinition def = ScenarioLibrary::create_anaphylaxis_scenario();
    ScenarioRuntime rt(def);
    int bp0 = rt.get_current_vitals().bp_sys, spo0 = rt.get_current_vitals().spo2;
    for (int i = 0; i < 15; ++i) rt.tick(60);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.bp_sys < bp0, "Untreated bp_sys should decline, start=" + std::to_string(bp0) + " end=" + std::to_string(v.bp_sys));
    check(v.spo2 < spo0, "Untreated spo2 should decline, start=" + std::to_string(spo0) + " end=" + std::to_string(v.spo2));
}

void test_anaphylaxis_epinephrine_helps_both_vitals() {
    // epinephrine_im's real pharmacology (alpha+beta agonist) is modeled
    // as a single drug moving three physiology terms at once
    // (DrugType::Epinephrine in apply_drug_effects) -- confirm it actually
    // helps BOTH the vasodilation-driven BP decline and the
    // bronchoconstriction-driven SpO2 decline, not just one of the two
    // mechanisms it's supposed to cover.
    ScenarioDefinition def = ScenarioLibrary::create_anaphylaxis_scenario();

    ScenarioRuntime untreated(def);
    for (int i = 0; i < 15; ++i) untreated.tick(60);
    ScenarioVitals u = untreated.get_current_vitals();

    ScenarioRuntime treated(def);
    treated.accept_action("epinephrine_im", "RN_TEST");
    for (int i = 0; i < 15; ++i) treated.tick(60);
    ScenarioVitals t = treated.get_current_vitals();

    check(t.bp_sys > u.bp_sys, "epinephrine_im should leave bp_sys higher than untreated (untreated=" +
          std::to_string(u.bp_sys) + ", treated=" + std::to_string(t.bp_sys) + ")");
    check(t.spo2 > u.spo2, "epinephrine_im should leave spo2 higher than untreated (untreated=" +
          std::to_string(u.spo2) + ", treated=" + std::to_string(t.spo2) + ")");
}

void test_anaphylaxis_action_grading() {
    ScenarioDefinition def = ScenarioLibrary::create_anaphylaxis_scenario();

    ScenarioRuntime immediate(def);
    ActionEvaluation early = immediate.evaluate_action_correctness("epinephrine_im");
    check(early.grade == ActionGrade::CORRECT, "epinephrine_im before 60s should grade CORRECT");

    ScenarioRuntime delayed(def);
    for (int i = 0; i < 2; ++i) delayed.tick(60); // past the 60s window
    ActionEvaluation late = delayed.evaluate_action_correctness("epinephrine_im");
    check(late.grade == ActionGrade::PARTIALLY_CORRECT,
          "epinephrine_im after 60s should still grade PARTIALLY_CORRECT (delayed but appropriate), not penalized "
          "like a wrong action -- epinephrine should never be withheld");

    ActionEvaluation airway = immediate.evaluate_action_correctness("airway_management");
    check(airway.grade == ActionGrade::CORRECT, "airway_management should always grade CORRECT (no vitals gate)");
    ActionEvaluation fluids = immediate.evaluate_action_correctness("fluid_bolus");
    check(fluids.grade == ActionGrade::CORRECT, "fluid_bolus should always grade CORRECT (no vitals gate)");
}

void test_respiratory_baseline_matches_authored() {
    ScenarioDefinition def = ScenarioLibrary::create_respiratory_distress_scenario();
    check(def.uses_ode_physiology, "RESPIRATORY_001 should have uses_ode_physiology=true");

    ScenarioRuntime rt(def);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.spo2 == def.synthetic_patient.baseline_vitals.spo2,
          "Fresh RESPIRATORY_001 session should start at the authored spo2, got " + std::to_string(v.spo2));
    check(v.rr == def.synthetic_patient.baseline_vitals.rr,
          "Fresh RESPIRATORY_001 session should start at the authored rr, got " + std::to_string(v.rr));
}

void test_respiratory_untreated_worsens() {
    ScenarioDefinition def = ScenarioLibrary::create_respiratory_distress_scenario();
    ScenarioRuntime rt(def);
    int spo0 = rt.get_current_vitals().spo2;
    for (int i = 0; i < 15; ++i) rt.tick(60);
    ScenarioVitals v = rt.get_current_vitals();
    check(v.spo2 < spo0, "Untreated spo2 should decline, start=" + std::to_string(spo0) + " end=" + std::to_string(v.spo2));
}

void test_respiratory_apply_oxygen_grades_and_helps() {
    // apply_oxygen grades purely on spo2<94 -- baseline spo2 is already
    // exactly 94 (not <94), so t=0 should grade PREMATURE; once the
    // crisis has visibly progressed it should grade CORRECT.
    ScenarioDefinition def = ScenarioLibrary::create_respiratory_distress_scenario();
    ScenarioRuntime immediate(def);
    ActionEvaluation early = immediate.evaluate_action_correctness("apply_oxygen");
    check(early.grade == ActionGrade::PREMATURE, "apply_oxygen at t=0 (spo2==94, not <94) should grade PREMATURE");

    ScenarioRuntime progressed(def);
    for (int i = 0; i < 15; ++i) progressed.tick(60); // well past the crisis's tick~8 onset, past noise() borderline
    ActionEvaluation later = progressed.evaluate_action_correctness("apply_oxygen");
    check(later.grade == ActionGrade::CORRECT, "apply_oxygen after the crisis has progressed should grade CORRECT");

    // Causal check: A/B against untreated.
    ScenarioRuntime untreated(def);
    for (int i = 0; i < 15; ++i) untreated.tick(60);
    int u_spo2 = untreated.get_current_vitals().spo2;

    ScenarioRuntime treated(def);
    treated.accept_action("apply_oxygen", "RN_TEST");
    for (int i = 0; i < 15; ++i) treated.tick(60);
    int t_spo2 = treated.get_current_vitals().spo2;

    check(t_spo2 > u_spo2, "apply_oxygen should leave spo2 measurably higher than untreated (untreated=" +
          std::to_string(u_spo2) + ", treated=" + std::to_string(t_spo2) + ")");
}

void test_respiratory_call_respiratory_never_grades_negatively() {
    // call_respiratory's own grading has no negative branch at all
    // (CORRECT if rr>25, else PARTIALLY_CORRECT/"reasonable") -- confirm
    // that holds at t=0 too, where rr is nowhere near 25 yet.
    ScenarioDefinition def = ScenarioLibrary::create_respiratory_distress_scenario();
    ScenarioRuntime rt(def);
    ActionEvaluation eval = rt.evaluate_action_correctness("call_respiratory");
    check(eval.grade == ActionGrade::PARTIALLY_CORRECT || eval.grade == ActionGrade::CORRECT,
          "call_respiratory should never grade negatively regardless of timing");
    check(eval.score_delta >= 0.0f, "call_respiratory's score_delta should never be negative, got " +
          std::to_string(eval.score_delta));
}

void test_dka_baseline_matches_authored() {
    ScenarioDefinition def = ScenarioLibrary::create_dka_crisis_scenario();
    check(def.uses_ode_physiology, "DKA_CRISIS_001 should have uses_ode_physiology=true");

    ScenarioRuntime rt(def);
    ScenarioVitals v = rt.get_current_vitals();
    const auto& baseline = def.synthetic_patient.baseline_vitals;
    check(v.hr == baseline.hr, "Fresh DKA_CRISIS_001 session should start at the authored hr, got " + std::to_string(v.hr));
    check(v.rr == baseline.rr, "Fresh DKA_CRISIS_001 session should start at the authored rr, got " + std::to_string(v.rr));
    check(v.spo2 == baseline.spo2, "Fresh DKA_CRISIS_001 session should start at the authored spo2, got " + std::to_string(v.spo2));
    check(v.bp_sys == baseline.bp_sys, "Fresh DKA_CRISIS_001 session should start at the authored bp_sys, got " + std::to_string(v.bp_sys));
}

void test_dka_untreated_worsens_without_hypoxia() {
    // RR should rise (Kussmaul breathing) and bp_sys should decline
    // (osmotic diuresis), but SpO2 should NOT drop the way it would under
    // a hypoxia-driven crisis (Respiratory Failure/Sepsis) -- DKA's
    // tachypnea is a compensatory response to acidosis, not a sign of
    // failing oxygenation.
    ScenarioDefinition def = ScenarioLibrary::create_dka_crisis_scenario();
    ScenarioRuntime rt(def);
    ScenarioVitals v0 = rt.get_current_vitals();
    for (int i = 0; i < 20; ++i) rt.tick(60);
    ScenarioVitals v20 = rt.get_current_vitals();

    check(v20.rr > v0.rr, "Untreated rr should rise (Kussmaul breathing), start=" +
          std::to_string(v0.rr) + " end=" + std::to_string(v20.rr));
    check(v20.bp_sys < v0.bp_sys, "Untreated bp_sys should decline (osmotic diuresis), start=" +
          std::to_string(v0.bp_sys) + " end=" + std::to_string(v20.bp_sys));
    check(v20.spo2 >= v0.spo2 - 2,
          "Untreated spo2 shouldn't meaningfully drop -- DKA's tachypnea isn't hypoxia-driven, start=" +
          std::to_string(v0.spo2) + " end=" + std::to_string(v20.spo2));
}

void test_dka_iv_fluids_and_insulin_are_independently_effective() {
    // iv_fluids addresses volume (bp_sys) and shouldn't meaningfully touch
    // rr; insulin_infusion addresses acidosis (rr) and shouldn't
    // meaningfully touch bp_sys -- confirms the two new mechanisms
    // (circulating_volume depletion, metabolic_acidosis accumulation) are
    // genuinely independent, not accidentally coupled.
    ScenarioDefinition def = ScenarioLibrary::create_dka_crisis_scenario();

    ScenarioRuntime untreated(def);
    for (int i = 0; i < 15; ++i) untreated.tick(60);
    ScenarioVitals u = untreated.get_current_vitals();

    ScenarioRuntime fluids(def);
    fluids.accept_action("iv_fluids", "RN_TEST");
    for (int i = 0; i < 15; ++i) fluids.tick(60);
    ScenarioVitals f = fluids.get_current_vitals();

    ScenarioRuntime insulin(def);
    insulin.accept_action("iv_fluids", "RN_TEST"); // required first, or insulin grades CONTRAINDICATED
    insulin.accept_action("insulin_infusion", "RN_TEST");
    for (int i = 0; i < 15; ++i) insulin.tick(60);
    ScenarioVitals ins = insulin.get_current_vitals();

    check(f.bp_sys > u.bp_sys, "iv_fluids should raise bp_sys vs. untreated (untreated=" +
          std::to_string(u.bp_sys) + ", treated=" + std::to_string(f.bp_sys) + ")");
    check(ins.rr < u.rr, "insulin_infusion should lower rr vs. untreated (untreated=" +
          std::to_string(u.rr) + ", treated=" + std::to_string(ins.rr) + ")");
}

void test_dka_action_grading_and_complication() {
    ScenarioDefinition def = ScenarioLibrary::create_dka_crisis_scenario();

    ScenarioRuntime rt(def);
    ActionEvaluation labs = rt.evaluate_action_correctness("obtain_labs");
    check(labs.grade == ActionGrade::CORRECT, "obtain_labs should always grade CORRECT (no vitals gate)");

    ActionEvaluation fluids_first = rt.evaluate_action_correctness("iv_fluids");
    check(fluids_first.grade == ActionGrade::CORRECT,
          "iv_fluids at t=0 should grade CORRECT (baseline rr=28 already >24)");

    ActionEvaluation insulin_too_early = rt.evaluate_action_correctness("insulin_infusion");
    check(insulin_too_early.grade == ActionGrade::CONTRAINDICATED,
          "insulin_infusion before iv_fluids should grade CONTRAINDICATED");
    check(insulin_too_early.triggers_complication && insulin_too_early.complication_name == "hypoglycemia",
          "insulin_infusion before iv_fluids should trigger the hypoglycemia complication, got triggers=" +
          std::to_string(insulin_too_early.triggers_complication) + " name='" + insulin_too_early.complication_name + "'");

    rt.accept_action("iv_fluids", "RN_TEST");
    ActionEvaluation insulin_after = rt.evaluate_action_correctness("insulin_infusion");
    check(insulin_after.grade == ActionGrade::CORRECT, "insulin_infusion after iv_fluids should grade CORRECT");
}

} // namespace

int main() {
    test_baseline_is_non_triggering();
    test_randomize_case_invariant();
    test_crisis_develops_and_crosses_threshold();
    test_fluids_slow_the_decline();
    test_vasopressor_reverses_shock();
    test_action_correctness_still_reads_ode_vitals();
    test_complication_effect_applies_via_ode();

    test_bleeding_baseline_already_in_shock();
    test_bleeding_randomize_case_mostly_starts_in_shock();
    test_bleeding_untreated_worsens();
    test_bleeding_source_control_reverses_decline();
    test_bleeding_source_control_not_re_armed_by_later_timeline_events();
    test_bleeding_massive_transfusion_helps_despite_active_crisis();

    test_anaphylaxis_baseline_matches_authored();
    test_anaphylaxis_driver_attribution_is_vasodilation_not_hypovolemia();
    test_anaphylaxis_untreated_worsens();
    test_anaphylaxis_epinephrine_helps_both_vitals();
    test_anaphylaxis_action_grading();

    test_respiratory_baseline_matches_authored();
    test_respiratory_untreated_worsens();
    test_respiratory_apply_oxygen_grades_and_helps();
    test_respiratory_call_respiratory_never_grades_negatively();

    test_dka_baseline_matches_authored();
    test_dka_untreated_worsens_without_hypoxia();
    test_dka_iv_fluids_and_insulin_are_independently_effective();
    test_dka_action_grading_and_complication();

    std::cout << "Scenario ODE physiology golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
