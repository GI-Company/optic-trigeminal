// Golden/boundary tests for include/clinical_scoring.h -- these functions
// now drive four independent things (the dashboard's per-patient acuity
// context, the score-contribution ribbon, the multi-patient gestalt trend,
// and the raw score displayed on patients pages), so a silent regression in
// a threshold would be wrong in four places at once with nothing to catch
// it. Test values are the thresholds cited in the header/implementation
// (RCP NEWS2 2017 Scale 1, Sepsis-3 qSOFA, a standard 5-parameter MEWS) --
// this checks the implementation against its own documented boundaries,
// not invented clinical numbers.
#include "../include/clinical_scoring.h"
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

Vitals baseline() {
    Vitals v{};
    v.hr = 75;
    // 14, not NEWS2's more permissive midpoint of e.g. 16 -- MEWS's own
    // "normal" RR band is narrower (9-14 vs NEWS2's 12-20), so this is the
    // one value that reads as normal under every score below, keeping a
    // single shared "genuinely normal by every definition" baseline.
    v.rr = 14;
    v.spo2 = 98;
    v.bp_sys = 120;
    v.bp_dia = 78;
    v.temp = 37.0f;
    v.is_crisis = false;
    v.drift_variance = 0.0;
    v.lactate = 1.0f;
    v.urine_output_ml_hr = 60;
    v.on_oxygen = false;
    v.consciousness = ConsciousnessLevel::Alert;
    return v;
}

void test_news2_baseline() {
    NEWS2Result r = calculate_news2(baseline());
    check(r.total_score == 0, "NEWS2 baseline (all-normal vitals) should total 0, got " + std::to_string(r.total_score));
    check(r.risk_level == "Low", "NEWS2 baseline risk_level should be Low, got " + r.risk_level);
    check(!r.red_score, "NEWS2 baseline should not be a red score");
}

void test_news2_respiration_bands() {
    struct { int rr; int expected; } cases[] = {
        {8, 3}, {9, 1}, {11, 1}, {12, 0}, {20, 0}, {21, 2}, {24, 2}, {25, 3}, {40, 3}
    };
    for (auto& c : cases) {
        Vitals v = baseline();
        v.rr = c.rr;
        NEWS2Result r = calculate_news2(v);
        check(r.respiration_score == c.expected,
              "NEWS2 RR=" + std::to_string(c.rr) + " should score " + std::to_string(c.expected) +
              ", got " + std::to_string(r.respiration_score));
    }
}

void test_news2_spo2_bands() {
    struct { int spo2; int expected; } cases[] = {
        {91, 3}, {92, 2}, {93, 2}, {94, 1}, {95, 1}, {96, 0}, {100, 0}
    };
    for (auto& c : cases) {
        Vitals v = baseline();
        v.spo2 = c.spo2;
        NEWS2Result r = calculate_news2(v);
        check(r.spo2_score == c.expected,
              "NEWS2 SpO2=" + std::to_string(c.spo2) + " should score " + std::to_string(c.expected) +
              ", got " + std::to_string(r.spo2_score));
    }
}

void test_news2_oxygen() {
    Vitals v = baseline();
    v.on_oxygen = true;
    check(calculate_news2(v).oxygen_score == 2, "NEWS2 on_oxygen=true should score 2");
    v.on_oxygen = false;
    check(calculate_news2(v).oxygen_score == 0, "NEWS2 on_oxygen=false should score 0");
}

void test_news2_systolic_bands() {
    struct { int sbp; int expected; } cases[] = {
        {90, 3}, {91, 2}, {100, 2}, {101, 1}, {110, 1}, {111, 0}, {219, 0}, {220, 3}, {250, 3}
    };
    for (auto& c : cases) {
        Vitals v = baseline();
        v.bp_sys = c.sbp;
        NEWS2Result r = calculate_news2(v);
        check(r.systolic_score == c.expected,
              "NEWS2 SBP=" + std::to_string(c.sbp) + " should score " + std::to_string(c.expected) +
              ", got " + std::to_string(r.systolic_score));
    }
}

void test_news2_heart_rate_bands() {
    struct { int hr; int expected; } cases[] = {
        {40, 3}, {41, 1}, {50, 1}, {51, 0}, {90, 0}, {91, 1}, {110, 1}, {111, 2}, {130, 2}, {131, 3}
    };
    for (auto& c : cases) {
        Vitals v = baseline();
        v.hr = c.hr;
        NEWS2Result r = calculate_news2(v);
        check(r.heart_rate_score == c.expected,
              "NEWS2 HR=" + std::to_string(c.hr) + " should score " + std::to_string(c.expected) +
              ", got " + std::to_string(r.heart_rate_score));
    }
}

void test_news2_consciousness_and_temperature() {
    Vitals v = baseline();
    v.consciousness = ConsciousnessLevel::Voice;
    check(calculate_news2(v).consciousness_score == 3, "NEWS2 non-Alert consciousness should score 3");
    v.consciousness = ConsciousnessLevel::Alert;
    check(calculate_news2(v).consciousness_score == 0, "NEWS2 Alert consciousness should score 0");

    struct { float temp; int expected; } cases[] = {
        {35.0f, 3}, {35.1f, 1}, {36.0f, 1}, {36.1f, 0}, {38.0f, 0}, {38.1f, 1}, {39.0f, 1}, {39.1f, 2}, {41.0f, 2}
    };
    for (auto& c : cases) {
        Vitals tv = baseline();
        tv.temp = c.temp;
        NEWS2Result r = calculate_news2(tv);
        check(r.temperature_score == c.expected,
              "NEWS2 temp=" + std::to_string(c.temp) + " should score " + std::to_string(c.expected) +
              ", got " + std::to_string(r.temperature_score));
    }
}

void test_news2_risk_escalation() {
    // One parameter pegged at 3 (red score) with everything else normal:
    // total is low but a red score alone should still be at least Medium.
    Vitals v = baseline();
    v.spo2 = 85; // scores 3
    NEWS2Result r = calculate_news2(v);
    check(r.red_score, "A single param at 3 should set red_score");
    check(r.risk_level == "Medium", "A red score with low total should be at least Medium, got " + r.risk_level);

    // Total >= 7 is always High regardless of red_score.
    Vitals high = baseline();
    high.rr = 22;   // 2
    high.hr = 115;  // 2
    high.temp = 38.5f; // 1
    high.bp_sys = 95;  // 2
    NEWS2Result rh = calculate_news2(high);
    check(rh.total_score == 7, "Composite case should total 7, got " + std::to_string(rh.total_score));
    check(rh.risk_level == "High", "Total >= 7 should be High, got " + rh.risk_level);

    Vitals none = baseline();
    check(calculate_news2(none).risk_level == "Low", "All-normal vitals should be Low risk");
}

void test_qsofa() {
    Vitals v = baseline();
    qSOFAResult r0 = calculate_qsofa(v);
    check(r0.score == 0 && !r0.high_risk, "qSOFA baseline should be 0, not high risk");

    Vitals rr_only = baseline();
    rr_only.rr = 22;
    check(calculate_qsofa(rr_only).score == 1, "qSOFA RR>=22 alone should score 1");

    Vitals sepsis = baseline();
    sepsis.rr = 24;
    sepsis.bp_sys = 95;
    sepsis.consciousness = ConsciousnessLevel::Voice;
    qSOFAResult rs = calculate_qsofa(sepsis);
    check(rs.score == 3, "qSOFA with all 3 criteria met should score 3, got " + std::to_string(rs.score));
    check(rs.high_risk, "qSOFA >=2 should be high_risk");

    Vitals two = baseline();
    two.rr = 24;
    two.bp_sys = 95;
    check(calculate_qsofa(two).high_risk, "qSOFA with exactly 2 criteria should already be high_risk");
}

void test_partial_sofa() {
    Vitals v = baseline();
    PartialSOFAResult r = calculate_partial_sofa(v, 0.21f, false);
    check(r.respiration == 0, "Partial SOFA baseline respiration should be 0 on room air");
    check(r.cardiovascular == 0, "Partial SOFA baseline cardiovascular (no vasopressor, MAP>=70) should be 0");
    check(r.cns == 0, "Partial SOFA baseline CNS (Alert) should be 0");
    check(r.partial_total == 0, "Partial SOFA baseline partial_total should be 0");

    // On a vasopressor, cardiovascular sub-score jumps even at a decent MAP --
    // real SOFA scores vasopressor *use* as inherently worse than none.
    PartialSOFAResult on_pressor = calculate_partial_sofa(v, 0.21f, true);
    check(on_pressor.cardiovascular >= 2, "Partial SOFA on vasopressor with MAP>=70 should score >=2, got " + std::to_string(on_pressor.cardiovascular));

    Vitals unresponsive = baseline();
    unresponsive.consciousness = ConsciousnessLevel::Unresponsive;
    check(calculate_partial_sofa(unresponsive, 0.21f, false).cns == 4, "Partial SOFA Unresponsive should score CNS 4");
}

void test_mews() {
    Vitals v = baseline();
    MEWSResult r = calculate_mews(v);
    check(r.total_score == 0, "MEWS baseline should total 0, got " + std::to_string(r.total_score));
    check(r.risk_level == "Low", "MEWS baseline should be Low risk");

    Vitals bad = baseline();
    bad.hr = 135;    // 3
    bad.bp_sys = 65; // 3
    bad.rr = 32;     // 3
    bad.consciousness = ConsciousnessLevel::Unresponsive; // 3
    MEWSResult rb = calculate_mews(bad);
    check(rb.total_score >= 5, "MEWS severely deranged vitals should total >=5, got " + std::to_string(rb.total_score));
    check(rb.risk_level == "High -- urgent medical review", "MEWS high total should be High risk, got " + rb.risk_level);
}

void test_dominant_news2_contributor() {
    NEWS2Contribution none = dominant_news2_contributor(calculate_news2(baseline()));
    check(none.parameter == "none" && none.points == 0, "Dominant contributor for a normal patient should be 'none'/0");

    Vitals hypoxic = baseline();
    hypoxic.spo2 = 85; // scores 3, the only deranged parameter
    NEWS2Contribution c = dominant_news2_contributor(calculate_news2(hypoxic));
    check(c.parameter == "spo2" && c.points == 3, "Dominant contributor for isolated hypoxia should be spo2/3, got " + c.parameter + "/" + std::to_string(c.points));

    // Tie-break: HR and RR both at 2, consciousness/spo2/systolic untouched --
    // priority order (consciousness, spo2, systolic, respiration, heart_rate, ...)
    // should pick respiration before heart_rate.
    Vitals tie = baseline();
    tie.rr = 22;  // 2
    tie.hr = 115; // 2
    NEWS2Contribution ct = dominant_news2_contributor(calculate_news2(tie));
    check(ct.parameter == "respiration", "Tied HR/RR at equal points should prefer respiration by priority order, got " + ct.parameter);
}

} // namespace

int main() {
    test_news2_baseline();
    test_news2_respiration_bands();
    test_news2_spo2_bands();
    test_news2_oxygen();
    test_news2_systolic_bands();
    test_news2_heart_rate_bands();
    test_news2_consciousness_and_temperature();
    test_news2_risk_escalation();
    test_qsofa();
    test_partial_sofa();
    test_mews();
    test_dominant_news2_contributor();

    std::cout << "Clinical scoring golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
