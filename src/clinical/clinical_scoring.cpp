#include "../include/clinical_scoring.h"
#include <algorithm>
#include <cmath>

// ============================================================
// NEWS2 (Royal College of Physicians, 2017) -- SpO2 Scale 1 only (Scale 2,
// for known/suspected hypercapnic respiratory failure, isn't modeled: this
// simulator has no COPD/hypercapnic-risk flag to select it correctly).
// ============================================================
NEWS2Result calculate_news2(const Vitals& v) {
    NEWS2Result r;

    // Respiration rate (breaths/min)
    if (v.rr <= 8) r.respiration_score = 3;
    else if (v.rr <= 11) r.respiration_score = 1;
    else if (v.rr <= 20) r.respiration_score = 0;
    else if (v.rr <= 24) r.respiration_score = 2;
    else r.respiration_score = 3;

    // SpO2 Scale 1 (%)
    if (v.spo2 <= 91) r.spo2_score = 3;
    else if (v.spo2 <= 93) r.spo2_score = 2;
    else if (v.spo2 <= 95) r.spo2_score = 1;
    else r.spo2_score = 0;

    // Supplemental oxygen
    r.oxygen_score = v.on_oxygen ? 2 : 0;

    // Systolic BP (mmHg)
    if (v.bp_sys <= 90) r.systolic_score = 3;
    else if (v.bp_sys <= 100) r.systolic_score = 2;
    else if (v.bp_sys <= 110) r.systolic_score = 1;
    else if (v.bp_sys <= 219) r.systolic_score = 0;
    else r.systolic_score = 3;

    // Heart rate (bpm)
    if (v.hr <= 40) r.heart_rate_score = 3;
    else if (v.hr <= 50) r.heart_rate_score = 1;
    else if (v.hr <= 90) r.heart_rate_score = 0;
    else if (v.hr <= 110) r.heart_rate_score = 1;
    else if (v.hr <= 130) r.heart_rate_score = 2;
    else r.heart_rate_score = 3;

    // Consciousness (AVPU) -- Alert scores 0; any of Voice/Pain/Unresponsive
    // ("CVPU" in the official chart, which also includes new-onset
    // Confusion) scores 3.
    r.consciousness_score = (v.consciousness == ConsciousnessLevel::Alert) ? 0 : 3;

    // Temperature (deg C)
    if (v.temp <= 35.0f) r.temperature_score = 3;
    else if (v.temp <= 36.0f) r.temperature_score = 1;
    else if (v.temp <= 38.0f) r.temperature_score = 0;
    else if (v.temp <= 39.0f) r.temperature_score = 1;
    else r.temperature_score = 2;

    r.total_score = r.respiration_score + r.spo2_score + r.oxygen_score +
                     r.systolic_score + r.heart_rate_score +
                     r.consciousness_score + r.temperature_score;

    r.red_score = (r.respiration_score == 3 || r.spo2_score == 3 ||
                    r.systolic_score == 3 || r.heart_rate_score == 3 ||
                    r.consciousness_score == 3 || r.temperature_score == 3);

    // RCP escalation protocol: total >=7 is always High; a "red score" (any
    // single parameter at 3) always warrants at least an urgent clinical
    // review even when the total is otherwise low, so it's treated as at
    // least Medium here rather than folded silently into Low-Medium.
    if (r.total_score >= 7 || (r.red_score && r.total_score >= 5)) {
        r.risk_level = "High";
        r.clinical_response = "Emergency assessment by a team with critical care competencies; continuous monitoring";
    } else if (r.total_score >= 5 || r.red_score) {
        r.risk_level = "Medium";
        r.clinical_response = "Urgent review by a clinician with core competencies for acutely ill patients; at least hourly monitoring";
    } else if (r.total_score >= 1) {
        r.risk_level = "Low-Medium";
        r.clinical_response = "Registered nurse assessment; minimum 4-6 hourly monitoring";
    } else {
        r.risk_level = "Low";
        r.clinical_response = "Routine ward-based monitoring; minimum 12-hourly";
    }

    return r;
}

// ============================================================
// qSOFA (Sepsis-3, Singer et al. JAMA 2016)
// ============================================================
qSOFAResult calculate_qsofa(const Vitals& v) {
    qSOFAResult r;

    if (v.rr >= 22) r.score += 1;
    if (v.bp_sys <= 100) r.score += 1;
    if (v.consciousness != ConsciousnessLevel::Alert) r.score += 1;

    r.high_risk = (r.score >= 2);
    if (r.high_risk) {
        r.interpretation = "qSOFA >=2 -- high risk of poor outcome; consider sepsis workup and escalation";
    } else if (r.score == 1) {
        r.interpretation = "qSOFA 1 -- increase monitoring frequency";
    } else {
        r.interpretation = "qSOFA 0 -- low risk by this screen";
    }
    return r;
}

// ============================================================
// Partial SOFA -- respiratory (SpO2/FiO2 proxy for PaO2/FiO2), cardiovascular
// (MAP + vasopressor use), CNS (AVPU proxy for GCS). Coagulation/liver/renal
// deliberately omitted -- see clinical_scoring.h and LIMITATIONS.md.
// ============================================================
PartialSOFAResult calculate_partial_sofa(const Vitals& v, float fio2, bool on_vasopressor) {
    PartialSOFAResult r;

    // Respiratory: SpO2/FiO2 (S/F) ratio is a well-known non-invasive proxy
    // for PaO2/FiO2 (P/F), but the exact P/F-to-S/F conversion varies
    // between published studies -- these are round, clearly-approximate
    // bands for a training simulator, not a precise clinical substitute.
    float fio2_safe = std::max(fio2, 0.21f);
    float sf_ratio = static_cast<float>(v.spo2) / fio2_safe;
    if (sf_ratio >= 400.0f) r.respiration = 0;
    else if (sf_ratio >= 315.0f) r.respiration = 1;
    else if (sf_ratio >= 235.0f) r.respiration = 2;
    else if (sf_ratio >= 150.0f) r.respiration = 3;
    else r.respiration = 4;

    // Cardiovascular: real SOFA distinguishes vasopressor dose bands
    // (dopamine/dobutamine/epi/norepi thresholds); this simulator only
    // models one vasopressor (norepinephrine) with no dose tiers, so
    // on_vasopressor + MAP is used as a simplified stand-in.
    float map = v.bp_dia + (v.bp_sys - v.bp_dia) / 3.0f;
    if (!on_vasopressor) {
        r.cardiovascular = (map >= 70.0f) ? 0 : 1;
    } else if (map >= 70.0f) {
        r.cardiovascular = 2;
    } else if (map >= 50.0f) {
        r.cardiovascular = 3;
    } else {
        r.cardiovascular = 4;
    }

    // CNS: AVPU proxy for GCS (this simulator has no full GCS model).
    switch (v.consciousness) {
        case ConsciousnessLevel::Alert: r.cns = 0; break;
        case ConsciousnessLevel::Voice: r.cns = 2; break;
        case ConsciousnessLevel::Pain: r.cns = 3; break;
        case ConsciousnessLevel::Unresponsive: r.cns = 4; break;
    }

    r.partial_total = r.respiration + r.cardiovascular + r.cns;
    // Max possible is 12 (3 sub-scores x 4), not full SOFA's 24 (6 x 4).
    if (r.partial_total >= 9) r.severity = "Critical (partial)";
    else if (r.partial_total >= 6) r.severity = "Severe (partial)";
    else if (r.partial_total >= 3) r.severity = "Moderate (partial)";
    else r.severity = "Mild/None (partial)";

    return r;
}

// ============================================================
// MEWS -- a commonly used five-parameter adult variant (HR/SBP/RR/Temp/
// AVPU). Several MEWS variants exist in real practice; this is not "the"
// single standard the way NEWS2 is.
// ============================================================
MEWSResult calculate_mews(const Vitals& v) {
    MEWSResult r;

    if (v.hr <= 40) r.heart_rate_score = 2;
    else if (v.hr <= 50) r.heart_rate_score = 1;
    else if (v.hr <= 100) r.heart_rate_score = 0;
    else if (v.hr <= 110) r.heart_rate_score = 1;
    else if (v.hr <= 129) r.heart_rate_score = 2;
    else r.heart_rate_score = 3;

    if (v.bp_sys <= 70) r.systolic_score = 3;
    else if (v.bp_sys <= 80) r.systolic_score = 2;
    else if (v.bp_sys <= 100) r.systolic_score = 1;
    else if (v.bp_sys <= 199) r.systolic_score = 0;
    else r.systolic_score = 2;

    if (v.rr < 9) r.respiration_score = 2;
    else if (v.rr <= 14) r.respiration_score = 0;
    else if (v.rr <= 20) r.respiration_score = 1;
    else if (v.rr <= 29) r.respiration_score = 2;
    else r.respiration_score = 3;

    if (v.temp < 35.0f) r.temperature_score = 2;
    else if (v.temp < 38.5f) r.temperature_score = 0;
    else r.temperature_score = 2;

    switch (v.consciousness) {
        case ConsciousnessLevel::Alert: r.consciousness_score = 0; break;
        case ConsciousnessLevel::Voice: r.consciousness_score = 1; break;
        case ConsciousnessLevel::Pain: r.consciousness_score = 2; break;
        case ConsciousnessLevel::Unresponsive: r.consciousness_score = 3; break;
    }

    r.total_score = r.heart_rate_score + r.systolic_score + r.respiration_score +
                     r.temperature_score + r.consciousness_score;

    if (r.total_score >= 5) r.risk_level = "High -- urgent medical review";
    else if (r.total_score >= 3) r.risk_level = "Medium -- increase monitoring";
    else r.risk_level = "Low";

    return r;
}
