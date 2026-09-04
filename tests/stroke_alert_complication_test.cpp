// Golden tests for STROKE_ALERT_001's hemorrhagic_transform complication
// -- the legacy curve engine's complication mechanism
// (ScenarioRuntime::arm_complication -> recompute_vitals_with_overlay's
// triggered_failures_ loop, applying FailureCondition::complication_effect
// via the same physio_curve engine a normal action uses) had zero existing
// test coverage anywhere in this codebase before this file, despite being
// real, live production code reachable from tpa_administration given
// without a CT first (see evaluate_action_correctness's STROKE_ALERT_001
// branch, and http_server.cpp's handle_training_action, which calls
// arm_complication whenever an ActionEvaluation sets
// triggers_complication).
//
// Also verifies a data fix made alongside this coverage: hemorrhagic_transform's
// complication_effect previously modeled the complication as a coarse
// {"HR": +8} proxy -- its own comment already called this "low confidence."
// Corrected to the same Cushing's-triad pattern (bradycardia + rising BP)
// already used for DKA's cerebral_edema complication, which models the
// same underlying mechanism (rising ICP) more accurately.
#include "../include/training_scenario.h"
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

void test_tpa_without_ct_is_contraindicated_and_triggers_complication() {
    ScenarioDefinition def = ScenarioLibrary::create_stroke_alert_scenario();
    ScenarioRuntime rt(def);
    rt.accept_action("activate_stroke_alert", "RN_TEST");

    ActionEvaluation eval = rt.evaluate_action_correctness("tpa_administration");
    check(eval.grade == ActionGrade::CONTRAINDICATED,
          "tpa_administration without ct_head first should grade CONTRAINDICATED");
    check(eval.triggers_complication, "should set triggers_complication=true");
    check(eval.complication_name == "hemorrhagic_transform",
          "should name the hemorrhagic_transform complication, got '" + eval.complication_name + "'");
}

void test_ct_before_tpa_is_correct_and_no_complication() {
    ScenarioDefinition def = ScenarioLibrary::create_stroke_alert_scenario();
    ScenarioRuntime rt(def);
    rt.accept_action("activate_stroke_alert", "RN_TEST");
    rt.accept_action("ct_head", "RN_TEST");

    ActionEvaluation eval = rt.evaluate_action_correctness("tpa_administration");
    check(eval.grade == ActionGrade::CORRECT,
          "tpa_administration after ct_head should grade CORRECT");
    check(!eval.triggers_complication, "a correctly-sequenced tPA administration shouldn't trigger a complication");
}

void test_hemorrhagic_transform_applies_cushings_triad() {
    // A/B: one run where hemorrhagic_transform arms (tPA given without a
    // CT first), one otherwise-identical run where it doesn't (CT done
    // first) -- isolates the complication's own contribution to bradycardia
    // (HR down) and rising BP (BP_sys up), rather than tPA's own authored
    // {"HR": +2.0f} action effect alone.
    ScenarioDefinition def = ScenarioLibrary::create_stroke_alert_scenario();

    ScenarioRuntime with_complication(def);
    with_complication.accept_action("activate_stroke_alert", "RN_TEST");
    ActionEvaluation bad_eval = with_complication.evaluate_action_correctness("tpa_administration");
    with_complication.accept_action("tpa_administration", "RN_TEST");
    if (bad_eval.triggers_complication) with_complication.arm_complication(bad_eval.complication_name);
    for (int i = 0; i < 10; ++i) with_complication.tick(60);
    ScenarioVitals v_with = with_complication.get_current_vitals();

    ScenarioRuntime without_complication(def);
    without_complication.accept_action("activate_stroke_alert", "RN_TEST");
    without_complication.accept_action("ct_head", "RN_TEST");
    without_complication.accept_action("tpa_administration", "RN_TEST");
    for (int i = 0; i < 10; ++i) without_complication.tick(60);
    ScenarioVitals v_without = without_complication.get_current_vitals();

    check(v_with.hr < v_without.hr,
          "hemorrhagic_transform's Cushing's-triad effect should leave HR lower (bradycardia) than the "
          "correctly-sequenced run (with=" + std::to_string(v_with.hr) + ", without=" + std::to_string(v_without.hr) + ")");
    check(v_with.bp_sys > v_without.bp_sys,
          "hemorrhagic_transform's Cushing's-triad effect should leave bp_sys higher (rising ICP) than the "
          "correctly-sequenced run (with=" + std::to_string(v_with.bp_sys) + ", without=" + std::to_string(v_without.bp_sys) + ")");
}

} // namespace

int main() {
    test_tpa_without_ct_is_contraindicated_and_triggers_complication();
    test_ct_before_tpa_is_correct_and_no_complication();
    test_hemorrhagic_transform_applies_cushings_triad();

    std::cout << "Stroke Alert complication golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
