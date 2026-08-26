#pragma once

#include "clinical_sim.h"
#include <string>

// Standard clinical early-warning / sepsis-screening scores, computed from
// the existing Vitals struct. Thresholds are rebuilt from real published
// references (cited per function below), not fabricated -- where a score's
// real definition needs data this simulator doesn't model (SOFA's platelet/
// bilirubin/creatinine sub-scores need a lab-value model that doesn't
// exist here), that sub-score is honestly omitted rather than faked. See
// LIMITATIONS.md.

// ============================================================
// NEWS2 -- Royal College of Physicians, 2017 edition.
// https://www.rcplondon.ac.uk/projects/outputs/national-early-warning-score-news-2
// ============================================================
struct NEWS2Result {
    int total_score = 0;

    int respiration_score = 0;
    int spo2_score = 0;
    int oxygen_score = 0;
    int systolic_score = 0;
    int heart_rate_score = 0;
    int consciousness_score = 0;
    int temperature_score = 0;

    bool red_score = false;         // any single parameter scored 3
    std::string risk_level;         // "Low", "Low-Medium", "Medium", "High"
    std::string clinical_response;  // recommended monitoring/response per RCP
};

// SpO2 Scale 2 (for patients with hypercapnic respiratory failure risk, e.g.
// COPD) is a real, separate part of NEWS2 -- not modeled here since this
// simulator has no COPD/hypercapnic-risk flag; every patient uses Scale 1.
NEWS2Result calculate_news2(const Vitals& v);

// ============================================================
// qSOFA -- Sepsis-3 (2016) bedside sepsis screen. Real replacement for
// ClinicalAnalyzer::detect_sepsis_indicators's ad-hoc 3-criteria check.
// ============================================================
struct qSOFAResult {
    int score = 0;               // 0-3
    bool high_risk = false;      // score >= 2
    std::string interpretation;
};

qSOFAResult calculate_qsofa(const Vitals& v);

// ============================================================
// Partial SOFA -- respiratory, cardiovascular, and CNS sub-scores only.
// Real SOFA also scores coagulation (platelets), liver (bilirubin), and
// renal (creatinine) -- deliberately not computed here since this
// simulator has no lab-value model, and fabricating those three numbers
// would be exactly the kind of invented clinical data this project's
// standing rule prohibits. total/severity reflect only the three
// vitals-computable sub-scores, and are documented as partial, not full
// SOFA, everywhere they're surfaced.
// ============================================================
struct PartialSOFAResult {
    int respiration = 0;      // 0-4, via SpO2/FiO2 proxy
    int cardiovascular = 0;   // 0-4, via MAP / vasopressor use
    int cns = 0;              // 0-4, via consciousness level (GCS proxy)
    int partial_total = 0;    // sum of the three above -- NOT full SOFA
    std::string severity;     // qualitative band over partial_total
};

// fio2: fraction of inspired oxygen (0.21 room air; higher if on_oxygen).
// on_vasopressor: whether a vasopressor (e.g. norepinephrine) is currently
// active for this patient -- cardiovascular sub-score depends on this per
// the real SOFA definition, not just raw BP.
PartialSOFAResult calculate_partial_sofa(const Vitals& v, float fio2, bool on_vasopressor);

// ============================================================
// MEWS -- Modified Early Warning Score. Several variants exist in real
// practice (unlike NEWS2, MEWS was never singularly standardized); this
// implements the commonly-used five-parameter adult version
// (HR/SBP/RR/Temp/AVPU), documented as *a* variant, not *the* standard.
// ============================================================
struct MEWSResult {
    int total_score = 0;
    int respiration_score = 0;
    int heart_rate_score = 0;
    int systolic_score = 0;
    int temperature_score = 0;
    int consciousness_score = 0;
    std::string risk_level;
};

MEWSResult calculate_mews(const Vitals& v);

// Which single NEWS2 parameter is contributing the most points right now,
// for the CCPC "score-contribution ribbon" chart layer -- same idea as
// dominant_physiology_driver in ode_physiology.cpp (that one explains the
// hidden physiology *causing* the vitals; this one explains which
// *observable* parameter is actually moving the early-warning score, which
// isn't always the same thing -- e.g. a patient can be physiologically
// septic before any single NEWS2 parameter has crossed into scoring
// territory). parameter is one of: "respiration", "spo2", "oxygen",
// "systolic", "heart_rate", "consciousness", "temperature", or "none" (every
// sub-score is 0). Ties broken by a fixed priority order (consciousness,
// spo2, systolic, respiration, heart_rate, temperature, oxygen) reflecting
// roughly which single deranged parameter clinicians tend to act on first --
// not itself a scored part of NEWS2.
struct NEWS2Contribution {
    std::string parameter;
    int points = 0;
};
NEWS2Contribution dominant_news2_contributor(const NEWS2Result& r);
