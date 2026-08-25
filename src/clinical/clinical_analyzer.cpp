#include "../include/clinical_analyzer.h"
#include "../include/inference_engine.h"
#include "../include/clinical_scoring.h"
#include <cmath>
#include <sstream>
#include <algorithm>

ClinicalAnalyzer::ClinicalAnalyzer() {
    // Initialize clinical thresholds based on standard nursing practice
    
    // Heart Rate (bpm)
    hr_thresholds.critical_low = 40;
    hr_thresholds.warning_low = 50;
    hr_thresholds.normal_low = 60;
    hr_thresholds.normal_high = 100;
    hr_thresholds.warning_high = 110;
    hr_thresholds.critical_high = 130;
    
    // SpO2 (%)
    spo2_thresholds.critical_low = 85;
    spo2_thresholds.warning_low = 90;
    spo2_thresholds.normal_low = 95;
    spo2_thresholds.normal_high = 100;
    spo2_thresholds.warning_high = 100; // No upper warning for SpO2
    spo2_thresholds.critical_high = 100;
    
    // Systolic BP (mmHg)
    bp_sys_thresholds.critical_low = 80;
    bp_sys_thresholds.warning_low = 90;
    bp_sys_thresholds.normal_low = 100;
    bp_sys_thresholds.normal_high = 140;
    bp_sys_thresholds.warning_high = 160;
    bp_sys_thresholds.critical_high = 180;
    
    // Respiratory Rate (breaths/min)
    rr_thresholds.critical_low = 8;
    rr_thresholds.warning_low = 10;
    rr_thresholds.normal_low = 12;
    rr_thresholds.normal_high = 20;
    rr_thresholds.warning_high = 24;
    rr_thresholds.critical_high = 30;

    // Temperature (°C, oral) -- standard adult reference ranges used for
    // this training simulation, not a substitute for institutional
    // protocol. Normal ~36.1-37.2; below/above that but short of fever or
    // hypothermia is a warning band; critical_low is clinically significant
    // hypothermia, critical_high is high fever requiring escalation.
    temp_thresholds.critical_low = 35.0f;
    temp_thresholds.warning_low = 36.0f;
    temp_thresholds.normal_low = 36.1f;
    temp_thresholds.normal_high = 37.2f;
    temp_thresholds.warning_high = 38.0f;
    temp_thresholds.critical_high = 38.5f;
}

ClinicalAnalyzer::~ClinicalAnalyzer() {}

std::vector<ClinicalObservation> ClinicalAnalyzer::analyze_patient(
    const Patient& patient,
    NativeInferenceEngine* engine
) {
    std::vector<ClinicalObservation> observations;
    
    // 1. Trend Analysis for each vital
    TrendAnalysis hr_trend = analyze_trend(patient.hr_history, "hr");
    if (hr_trend.is_significant) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "trend";
        obs.severity = (std::abs(hr_trend.slope) > 5.0) ? "warning" : "info";
        obs.description = "Heart rate " + hr_trend.trend_type + " (" + 
                         std::to_string(static_cast<int>(hr_trend.slope)) + " bpm/" + 
                         std::to_string(hr_trend.duration_samples) + " samples)";
        obs.rationale = generate_rationale(patient, "hr_trend_" + hr_trend.trend_type, engine);
        obs.confidence = calculate_confidence(patient, "hr_trend");
        obs.suggested_actions = suggest_actions("hr_trend", obs.severity);
        obs.requires_nurse_attention = (obs.severity != "info");
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }
    
    TrendAnalysis spo2_trend = analyze_trend(patient.spo2_history, "spo2");
    if (spo2_trend.is_significant) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "trend";
        obs.severity = (spo2_trend.trend_type == "falling" && std::abs(spo2_trend.slope) > 2.0) ? "warning" : "info";
        obs.description = "SpO2 " + spo2_trend.trend_type + " (" + 
                         std::to_string(static_cast<int>(spo2_trend.slope)) + "%/" + 
                         std::to_string(spo2_trend.duration_samples) + " samples)";
        obs.rationale = generate_rationale(patient, "spo2_trend_" + spo2_trend.trend_type, engine);
        obs.confidence = calculate_confidence(patient, "spo2_trend");
        obs.suggested_actions = suggest_actions("spo2_trend", obs.severity);
        obs.requires_nurse_attention = (obs.severity != "info");
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }
    
    TrendAnalysis rr_trend = analyze_trend(patient.rr_history, "rr");
    if (rr_trend.is_significant) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "trend";
        obs.severity = (std::abs(rr_trend.slope) > 3.0) ? "warning" : "info";
        obs.description = "Respiratory rate " + rr_trend.trend_type + " (" +
                         std::to_string(static_cast<int>(rr_trend.slope)) + " breaths/min/" +
                         std::to_string(rr_trend.duration_samples) + " samples)";
        obs.rationale = generate_rationale(patient, "rr_trend_" + rr_trend.trend_type, engine);
        obs.confidence = calculate_confidence(patient, "rr_trend");
        obs.suggested_actions = suggest_actions("rr_trend", obs.severity);
        obs.requires_nurse_attention = (obs.severity != "info");
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    // analyze_trend/calculate_slope only accept vector<int> (shared by
    // hr/rr/spo2, which are all naturally integer vitals) -- temp_history is
    // float, so it's scaled to tenths-of-a-degree ints (373 for 37.3C) just
    // for this call, then scaled back down for the description text. Doesn't
    // touch analyze_trend itself, so hr/rr/spo2 are unaffected.
    std::vector<int> temp_history_tenths;
    temp_history_tenths.reserve(patient.temp_history.size());
    for (float t : patient.temp_history) {
        temp_history_tenths.push_back(static_cast<int>(std::round(t * 10.0f)));
    }
    TrendAnalysis temp_trend = analyze_trend(temp_history_tenths, "temp");
    if (temp_trend.is_significant) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "trend";
        obs.severity = (std::abs(temp_trend.slope) > 3.0) ? "warning" : "info";
        std::ostringstream slope_str;
        slope_str.precision(1);
        slope_str << std::fixed << (temp_trend.slope / 10.0f);
        obs.description = "Temperature " + temp_trend.trend_type + " (" +
                         slope_str.str() + "°C/" +
                         std::to_string(temp_trend.duration_samples) + " samples)";
        obs.rationale = generate_rationale(patient, "temp_trend_" + temp_trend.trend_type, engine);
        obs.confidence = calculate_confidence(patient, "temp_trend");
        obs.suggested_actions = suggest_actions("temp_trend", obs.severity);
        obs.requires_nurse_attention = (obs.severity != "info");
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    // 2. Threshold Checking
    if (is_critical_threshold(patient.vitals.hr, "hr")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "critical";
        obs.description = "Heart rate " + std::to_string(patient.vitals.hr) + " bpm is critically " +
                         (patient.vitals.hr < hr_thresholds.critical_low ? "low" : "high");
        obs.rationale = generate_rationale(patient, "critical_hr", engine);
        obs.confidence = 0.95f;
        obs.suggested_actions = suggest_actions("critical_hr", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    } else if (is_warning_threshold(patient.vitals.hr, "hr")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "warning";
        obs.description = "Heart rate " + std::to_string(patient.vitals.hr) + " bpm is " +
                         (patient.vitals.hr < hr_thresholds.normal_low ? "below" : "above") + " normal range";
        obs.rationale = generate_rationale(patient, "abnormal_hr", engine);
        obs.confidence = 0.85f;
        obs.suggested_actions = suggest_actions("abnormal_hr", "warning");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }
    
    if (is_critical_threshold(patient.vitals.spo2, "spo2")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "critical";
        obs.description = "SpO2 " + std::to_string(patient.vitals.spo2) + "% indicates hypoxia";
        obs.rationale = generate_rationale(patient, "hypoxia", engine);
        obs.confidence = 0.95f;
        obs.suggested_actions = suggest_actions("hypoxia", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    } else if (is_warning_threshold(patient.vitals.spo2, "spo2")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "warning";
        obs.description = "SpO2 " + std::to_string(patient.vitals.spo2) + "% is below normal range";
        obs.rationale = generate_rationale(patient, "low_spo2", engine);
        obs.confidence = 0.85f;
        obs.suggested_actions = suggest_actions("low_spo2", "warning");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    if (is_critical_threshold(patient.vitals.rr, "rr")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "critical";
        obs.description = "Respiratory rate " + std::to_string(patient.vitals.rr) + " breaths/min is critically " +
                         (patient.vitals.rr < rr_thresholds.critical_low ? "low" : "high");
        obs.rationale = generate_rationale(patient, "critical_rr", engine);
        obs.confidence = 0.95f;
        obs.suggested_actions = suggest_actions("critical_rr", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    } else if (is_warning_threshold(patient.vitals.rr, "rr")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        obs.severity = "warning";
        obs.description = "Respiratory rate " + std::to_string(patient.vitals.rr) + " breaths/min is " +
                         (patient.vitals.rr < rr_thresholds.normal_low ? "below" : "above") + " normal range";
        obs.rationale = generate_rationale(patient, "abnormal_rr", engine);
        obs.confidence = 0.85f;
        obs.suggested_actions = suggest_actions("abnormal_rr", "warning");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    if (is_critical_threshold(patient.vitals.temp, "temp")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        std::ostringstream temp_str;
        temp_str.precision(1);
        temp_str << std::fixed << patient.vitals.temp;
        obs.severity = "critical";
        obs.description = "Temperature " + temp_str.str() + "°C is critically " +
                         (patient.vitals.temp < temp_thresholds.critical_low ? "low (hypothermia)" : "high (hyperthermia)");
        obs.rationale = generate_rationale(patient, "critical_temp", engine);
        obs.confidence = 0.95f;
        obs.suggested_actions = suggest_actions("critical_temp", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    } else if (is_warning_threshold(patient.vitals.temp, "temp")) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "threshold";
        std::ostringstream temp_str;
        temp_str.precision(1);
        temp_str << std::fixed << patient.vitals.temp;
        obs.severity = "warning";
        obs.description = "Temperature " + temp_str.str() + "°C is " +
                         (patient.vitals.temp < temp_thresholds.normal_low ? "below" : "above") + " normal range";
        obs.rationale = generate_rationale(patient, "abnormal_temp", engine);
        obs.confidence = 0.85f;
        obs.suggested_actions = suggest_actions("abnormal_temp", "warning");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    // 3. Pattern Recognition
    if (detect_respiratory_compromise(patient)) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "pattern";
        obs.severity = "warning";
        obs.description = "Pattern suggests respiratory compromise";
        obs.rationale = generate_rationale(patient, "respiratory_compromise", engine);
        obs.confidence = 0.80f;
        obs.suggested_actions = suggest_actions("respiratory_compromise", "warning");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }
    
    if (detect_shock_pattern(patient)) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "pattern";
        obs.severity = "critical";
        obs.description = "Pattern suggests possible shock state";
        obs.rationale = generate_rationale(patient, "shock_pattern", engine);
        obs.confidence = 0.75f;
        obs.suggested_actions = suggest_actions("shock", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    // Real qSOFA (Sepsis-3) bedside sepsis screen -- see
    // include/clinical_scoring.h. This is the standard-scoring replacement
    // for the ad-hoc 3-criteria detect_sepsis_indicators() check above
    // (kept in place, unused by this function, for now).
    qSOFAResult qsofa = calculate_qsofa(patient.vitals);
    if (qsofa.high_risk) {
        ClinicalObservation obs;
        obs.patient_id = patient.id;
        obs.observation_type = "pattern";
        obs.severity = "critical";
        obs.description = "qSOFA " + std::to_string(qsofa.score) + "/3 -- high risk of sepsis-related poor outcome";
        obs.rationale = generate_rationale(patient, "qsofa_high", engine);
        obs.confidence = 0.80f;
        obs.suggested_actions = suggest_actions("qsofa_high", "critical");
        obs.requires_nurse_attention = true;
        obs.timestamp = std::time(nullptr);
        observations.push_back(obs);
    }

    return observations;
}

TrendAnalysis ClinicalAnalyzer::analyze_trend(const std::vector<int>& history, std::string vital_type) {
    TrendAnalysis result;
    
    if (history.size() < 3) {
        result.trend_type = "insufficient_data";
        result.slope = 0.0f;
        result.duration_samples = 0;
        result.is_significant = false;
        return result;
    }
    
    // Calculate slope (simple linear regression)
    result.slope = calculate_slope(history);
    
    // Determine trend type
    if (std::abs(result.slope) < 0.5) {
        result.trend_type = "stable";
    } else if (result.slope > 0) {
        result.trend_type = "rising";
    } else {
        result.trend_type = "falling";
    }
    
    // Count consecutive changes in same direction
    result.duration_samples = count_consecutive_changes(history, result.slope > 0);
    
    // Determine significance based on vital type and slope magnitude
    if (vital_type == "hr") {
        result.is_significant = (std::abs(result.slope) > 3.0 && result.duration_samples >= 5);
    } else if (vital_type == "spo2") {
        result.is_significant = (std::abs(result.slope) > 1.5 && result.duration_samples >= 5);
    } else if (vital_type == "bp") {
        result.is_significant = (std::abs(result.slope) > 5.0 && result.duration_samples >= 5);
    } else {
        result.is_significant = (std::abs(result.slope) > 2.0 && result.duration_samples >= 5);
    }
    
    return result;
}

bool ClinicalAnalyzer::is_critical_threshold(float value, std::string vital_type) {
    if (vital_type == "hr") {
        return (value <= hr_thresholds.critical_low || value >= hr_thresholds.critical_high);
    } else if (vital_type == "spo2") {
        return (value <= spo2_thresholds.critical_low);
    } else if (vital_type == "bp_sys") {
        return (value <= bp_sys_thresholds.critical_low || value >= bp_sys_thresholds.critical_high);
    } else if (vital_type == "rr") {
        return (value <= rr_thresholds.critical_low || value >= rr_thresholds.critical_high);
    } else if (vital_type == "temp") {
        return (value <= temp_thresholds.critical_low || value >= temp_thresholds.critical_high);
    }
    return false;
}

bool ClinicalAnalyzer::is_warning_threshold(float value, std::string vital_type) {
    // Bounded by critical_low/critical_high, not warning_low/warning_high --
    // using the warning_* fields here left every value strictly between the
    // critical threshold and the warning threshold (e.g. SpO2 86-90%, HR
    // 41-50 or 111-129 bpm) classified as neither critical nor warning, so
    // analyze_patient() silently produced zero observations for vitals that
    // were clearly abnormal. Critical is checked first by every caller (see
    // analyze_patient() above), so this only needs to cover "abnormal but
    // not critical" -- the full span from just past critical out to normal.
    if (vital_type == "hr") {
        return ((value > hr_thresholds.critical_low && value < hr_thresholds.normal_low) ||
                (value > hr_thresholds.normal_high && value < hr_thresholds.critical_high));
    } else if (vital_type == "spo2") {
        return (value > spo2_thresholds.critical_low && value < spo2_thresholds.normal_low);
    } else if (vital_type == "bp_sys") {
        return ((value > bp_sys_thresholds.critical_low && value < bp_sys_thresholds.normal_low) ||
                (value > bp_sys_thresholds.normal_high && value < bp_sys_thresholds.critical_high));
    } else if (vital_type == "rr") {
        return ((value > rr_thresholds.critical_low && value < rr_thresholds.normal_low) ||
                (value > rr_thresholds.normal_high && value < rr_thresholds.critical_high));
    } else if (vital_type == "temp") {
        return ((value > temp_thresholds.critical_low && value < temp_thresholds.normal_low) ||
                (value > temp_thresholds.normal_high && value < temp_thresholds.critical_high));
    }
    return false;
}

bool ClinicalAnalyzer::detect_respiratory_compromise(const Patient& p) {
    // Respiratory compromise pattern: HR↑ + SpO2↓ + RR↑
    bool tachycardia = p.vitals.hr > hr_thresholds.normal_high;
    bool low_spo2 = p.vitals.spo2 < spo2_thresholds.normal_low;
    bool tachypnea = p.vitals.rr > rr_thresholds.normal_high;
    
    // At least 2 of 3 criteria
    int criteria_met = (tachycardia ? 1 : 0) + (low_spo2 ? 1 : 0) + (tachypnea ? 1 : 0);
    return (criteria_met >= 2);
}

bool ClinicalAnalyzer::detect_shock_pattern(const Patient& p) {
    // Shock pattern: HR↑ + BP↓ (compensatory tachycardia with hypotension)
    bool tachycardia = p.vitals.hr > hr_thresholds.warning_high;
    bool hypotension = p.vitals.bp_sys < bp_sys_thresholds.normal_low;
    
    return (tachycardia && hypotension);
}

bool ClinicalAnalyzer::detect_sepsis_indicators(const Patient& p) {
    // Simplified sepsis screening: Tachycardia + Tachypnea + Hypotension
    bool tachycardia = p.vitals.hr > hr_thresholds.warning_high;
    bool tachypnea = p.vitals.rr > rr_thresholds.warning_high;
    bool hypotension = p.vitals.bp_sys < bp_sys_thresholds.warning_low;
    
    // All 3 criteria
    return (tachycardia && tachypnea && hypotension);
}

std::string ClinicalAnalyzer::generate_rationale(const Patient& p, std::string finding, NativeInferenceEngine* engine) {
    // Base clinical rationale, keyed by finding type. This is accurate,
    // reviewed, deterministic clinical content and stays exactly as-is --
    // what follows is additive enrichment, never a replacement, given the
    // clinical-safety context.
    std::string base;
    if (finding == "hr_trend_rising") {
        base = "Increasing heart rate may indicate pain, anxiety, fever, hypovolemia, or cardiac stress. Consider assessing patient comfort and fluid status.";
    } else if (finding == "hr_trend_falling") {
        base = "Decreasing heart rate trend should be monitored. If patient is on beta-blockers or has cardiac conduction issues, close monitoring is warranted.";
    } else if (finding == "spo2_trend_falling") {
        base = "Declining oxygen saturation over time suggests worsening respiratory function or airway compromise. Early intervention may prevent acute decompensation.";
    } else if (finding == "spo2_trend_rising") {
        base = "Improving oxygen saturation indicates positive response to interventions or improving respiratory status.";
    } else if (finding == "critical_hr") {
        base = "Critically abnormal heart rate requires immediate assessment. Bradycardia <40 or tachycardia >130 can compromise cardiac output.";
    } else if (finding == "abnormal_hr") {
        base = "Heart rate outside normal range warrants assessment for potential causes and patient symptoms.";
    } else if (finding == "hypoxia") {
        base = "SpO2 <90% indicates hypoxemia requiring immediate intervention. Assess airway, breathing, consider supplemental oxygen.";
    } else if (finding == "low_spo2") {
        base = "SpO2 below 95% suggests suboptimal oxygen saturation. Monitor closely and assess for respiratory distress.";
    } else if (finding == "respiratory_compromise") {
        base = "Combined pattern of tachycardia, low SpO2, and/or tachypnea suggests respiratory compromise. Consider respiratory assessment and potential need for escalation.";
    } else if (finding == "shock_pattern") {
        base = "Tachycardia with hypotension may indicate compensatory shock. Assess for bleeding, sepsis, or cardiac causes. Consider rapid response activation.";
    } else if (finding == "rr_trend_rising") {
        base = "Increasing respiratory rate may indicate pain, anxiety, early respiratory compromise, or metabolic acidosis. Assess work of breathing and oxygenation.";
    } else if (finding == "rr_trend_falling") {
        base = "Decreasing respiratory rate should be monitored, especially with sedating medications on board -- can precede respiratory depression.";
    } else if (finding == "critical_rr") {
        base = "Critically abnormal respiratory rate requires immediate assessment. Bradypnea <8 or tachypnea >30 can precede respiratory failure.";
    } else if (finding == "abnormal_rr") {
        base = "Respiratory rate outside normal range warrants assessment for potential causes and patient symptoms.";
    } else if (finding == "temp_trend_rising") {
        base = "Rising temperature may indicate developing infection, inflammatory response, or environmental factors. Monitor for other signs of infection.";
    } else if (finding == "temp_trend_falling") {
        base = "Falling temperature trend should be monitored, particularly if approaching hypothermic range or if patient was previously febrile and improving.";
    } else if (finding == "critical_temp") {
        base = "Critically abnormal temperature requires immediate assessment. Hypothermia <35.0C or high fever >=38.5C can indicate serious underlying pathology.";
    } else if (finding == "abnormal_temp") {
        base = "Temperature outside normal range warrants assessment for potential causes -- infection, environmental exposure, or medication effects.";
    } else if (finding == "qsofa_high") {
        base = "qSOFA score of 2 or more (respiratory rate >=22, systolic BP <=100, or altered mentation) is associated with higher risk of sepsis-related mortality. Consider further sepsis workup (lactate, cultures, source control) and escalation.";
    } else {
        base = "Clinical observation requires assessment and documentation.";
    }

    if (!engine) return base;
    OpticTrigeminal* kg = engine->get_knowledge_graph();
    if (!kg) return base;

    // Query the training-corpus knowledge graph using the canned rationale
    // itself as the query. Only append when the top hit is meaningfully
    // separated from the runner-up (not just "top-1, whatever it is") --
    // a weak, ambiguous match isn't worth surfacing next to clinically
    // reviewed text. This is a design threshold, not a validated clinical
    // one: with only one candidate, the RRF fusion in find_related_concepts
    // has nothing to separate it from, so it's treated as confident too.
    Embedding query_emb = engine->embed_text(base);
    auto related = kg->find_related_concepts(query_emb, base, 3);
    if (!related.empty() && (related.size() == 1 || related[0].second > related[1].second * 1.5f)) {
        std::string snippet = related[0].first;
        if (snippet.size() > 200) snippet = snippet.substr(0, 200) + "...";
        base += " Related: " + snippet;
    }

    return base;
}

std::vector<std::string> ClinicalAnalyzer::suggest_actions(std::string observation_type, std::string severity) {
    std::vector<std::string> actions;
    
    if (observation_type == "hr_trend" || observation_type == "abnormal_hr") {
        actions.push_back("Assess patient comfort/pain");
        actions.push_back("Review medications");
        if (severity == "warning" || severity == "critical") {
            actions.push_back("Consider EKG");
        }
    } else if (observation_type == "critical_hr") {
        actions.push_back("Immediate bedside assessment");
        actions.push_back("Obtain EKG");
        actions.push_back("Notify physician");
    } else if (observation_type == "spo2_trend" || observation_type == "low_spo2") {
        actions.push_back("Assess respiratory effort");
        actions.push_back("Consider supplemental O2");
        actions.push_back("Increase monitoring frequency");
    } else if (observation_type == "hypoxia") {
        actions.push_back("Apply supplemental oxygen");
        actions.push_back("Assess airway/breathing");
        actions.push_back("Notify physician immediately");
    } else if (observation_type == "respiratory_compromise") {
        actions.push_back("Perform respiratory assessment");
        actions.push_back("Consider oxygen therapy");
        actions.push_back("Notify physician");
        actions.push_back("Prepare for potential escalation");
    } else if (observation_type == "shock") {
        actions.push_back("Activate rapid response");
        actions.push_back("Obtain vital signs set");
        actions.push_back("Assess perfusion");
        actions.push_back("Notify physician urgently");
    } else if (observation_type == "rr_trend" || observation_type == "abnormal_rr") {
        actions.push_back("Assess work of breathing");
        actions.push_back("Check oxygen saturation");
        if (severity == "warning" || severity == "critical") {
            actions.push_back("Increase monitoring frequency");
        }
    } else if (observation_type == "critical_rr") {
        actions.push_back("Immediate bedside assessment");
        actions.push_back("Assess airway/breathing");
        actions.push_back("Notify physician immediately");
    } else if (observation_type == "temp_trend" || observation_type == "abnormal_temp") {
        actions.push_back("Recheck temperature to confirm");
        actions.push_back("Assess for signs of infection");
        if (severity == "warning" || severity == "critical") {
            actions.push_back("Review recent medications/interventions");
        }
    } else if (observation_type == "critical_temp") {
        actions.push_back("Immediate bedside assessment");
        actions.push_back("Initiate warming/cooling measures as indicated");
        actions.push_back("Notify physician");
    } else if (observation_type == "qsofa_high") {
        actions.push_back("Check/repeat lactate");
        actions.push_back("Obtain blood cultures before antibiotics if not already done");
        actions.push_back("Notify physician -- consider sepsis pathway");
    }
    
    // Default actions if no specific match
    if (actions.empty()) {
        actions.push_back("Assess patient");
        actions.push_back("Document findings");
        if (severity == "warning" || severity == "critical") {
            actions.push_back("Notify physician");
        }
    }
    
    return actions;
}

float ClinicalAnalyzer::calculate_confidence(const Patient& p, std::string observation) {
    // Higher confidence if multiple data points support the observation
    // Lower confidence if data is noisy or borderline
    
    // Base confidence
    float confidence = 0.70f;
    
    // Increase confidence if crisis state (system is more certain)
    if (p.vitals.is_crisis) {
        confidence += 0.15f;
    }
    
    // Increase confidence if acuity score is high (patient is known to be unstable)
    if (p.acuity_score >= 7) {
        confidence += 0.10f;
    }
    
    // Cap at 0.95 (never 100% certain)
    return std::min(confidence, 0.95f);
}

float ClinicalAnalyzer::calculate_slope(const std::vector<int>& data) {
    if (data.size() < 2) return 0.0f;
    
    // Simple linear regression slope
    int n = data.size();
    float sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    
    for (int i = 0; i < n; i++) {
        float x = static_cast<float>(i);
        float y = static_cast<float>(data[i]);
        sum_x += x;
        sum_y += y;
        sum_xy += x * y;
        sum_x2 += x * x;
    }
    
    float slope = (n * sum_xy - sum_x * sum_y) / (n * sum_x2 - sum_x * sum_x);
    return slope;
}

bool ClinicalAnalyzer::is_monotonic_trend(const std::vector<int>& data, bool increasing) {
    if (data.size() < 2) return false;
    
    for (size_t i = 1; i < data.size(); i++) {
        if (increasing && data[i] < data[i-1]) return false;
        if (!increasing && data[i] > data[i-1]) return false;
    }
    return true;
}

int ClinicalAnalyzer::count_consecutive_changes(const std::vector<int>& data, bool increasing) {
    if (data.size() < 2) return 0;
    
    int count = 1; // Start with 1 for the most recent change
    for (int i = data.size() - 1; i > 0; i--) {
        bool change_matches = increasing ? (data[i] > data[i-1]) : (data[i] < data[i-1]);
        if (change_matches) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

// --- DocumentationScaffold Implementation ---

DocumentationScaffold::DocumentationScaffold() {}

std::string DocumentationScaffold::generate_sbar(
    const Patient& patient,
    const std::vector<ClinicalObservation>& observations
) {
    std::stringstream sbar;
    sbar << "SBAR CLINICAL NOTE\n";
    sbar << "==================\n\n";
    
    sbar << "S (SITUATION): " << generate_situation(patient, observations) << "\n\n";
    sbar << "B (BACKGROUND): " << generate_background(patient) << "\n\n";
    sbar << "A (ASSESSMENT): " << generate_assessment(observations) << "\n\n";
    sbar << "R (RECOMMENDATION): " << generate_recommendation(observations) << "\n";
    
    return sbar.str();
}

std::string DocumentationScaffold::generate_situation(const Patient& p, const std::vector<ClinicalObservation>& obs) {
    std::string situation = "Patient " + p.name + " (MRN: " + p.mrn + ") in Room " + p.room + ". ";
    
    bool critical = false;
    for (const auto& o : obs) {
        if (o.severity == "critical") {
            critical = true;
            situation += "CAUTION: " + o.description + ". ";
            break;
        }
    }
    
    if (!critical) {
        situation += "Currently monitoring clinical status. ";
    }
    
    return situation;
}

std::string DocumentationScaffold::generate_background(const Patient& p) {
    std::string bg = "Admission Diagnosis: " + p.admission_diagnosis + ". ";
    bg += "Current Acuity Score: " + std::to_string(p.acuity_score) + ". ";
    bg += "Stability: ";
    bg += p.vitals.is_crisis ? "CRITICAL / UNSTABLE" : "STABLE";
    bg += ". ";
    return bg;
}

std::string DocumentationScaffold::generate_assessment(const std::vector<ClinicalObservation>& obs) {
    if (obs.empty()) {
        return "No acute physiological changes detected by ACmK-OT.";
    }
    
    std::string assessment = "ACmK-OT analyzed current telemetry: ";
    for (size_t i = 0; i < obs.size(); ++i) {
        assessment += obs[i].description;
        if (i < obs.size() - 1) assessment += "; ";
    }
    assessment += ". Rationale: " + (obs[0].rationale.empty() ? "Pending further data." : obs[0].rationale);
    return assessment;
}

std::string DocumentationScaffold::generate_recommendation(const std::vector<ClinicalObservation>& obs) {
    if (obs.empty()) {
        return "Continue current plan of care and monitor vital signs per protocol.";
    }
    
    std::string rec = "Suggested Actions: ";
    std::vector<std::string> all_actions;
    for (const auto& o : obs) {
        for (const auto& a : o.suggested_actions) {
            if (std::find(all_actions.begin(), all_actions.end(), a) == all_actions.end()) {
                all_actions.push_back(a);
            }
        }
    }
    
    for (size_t i = 0; i < all_actions.size(); ++i) {
        rec += all_actions[i];
        if (i < all_actions.size() - 1) rec += ", ";
    }
    rec += ". ";
    
    for (const auto& o : obs) {
        if (o.requires_nurse_attention && o.severity == "critical") {
            rec += "IMMEDIATE PHYSICIAN NOTIFICATION REQUIRED.";
            break;
        }
    }
    
    return rec;
}
