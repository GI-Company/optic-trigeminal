#pragma once

#include "clinical_sim.h"
#include <string>
#include <vector>
#include <ctime>

// Forward declarations to avoid circular dependency
class NativeInferenceEngine;


// Clinical observation with rationale and suggested actions
struct ClinicalObservation {
    int patient_id;
    std::string observation_type; // "trend", "threshold", "pattern", "correlation"
    std::string severity; // "info", "warning", "critical"
    std::string description; // What was observed
    std::string rationale; // Why it matters (clinical reasoning)
    float confidence; // 0.0 - 1.0
    std::vector<std::string> suggested_actions;
    bool requires_nurse_attention;
    std::time_t timestamp;
};

// Trend analysis result
struct TrendAnalysis {
    std::string trend_type; // "rising", "falling", "stable", "fluctuating"
    float slope; // Rate of change
    int duration_samples; // How many samples in this trend
    bool is_significant; // Exceeds clinical threshold
};

class ClinicalAnalyzer {
public:
    ClinicalAnalyzer();
    ~ClinicalAnalyzer();
    
    // Main analysis function - returns all current observations for a patient
    std::vector<ClinicalObservation> analyze_patient(
        const Patient& patient,
        NativeInferenceEngine* engine
    );
    
    // Trend detection for vital signs
    TrendAnalysis analyze_trend(const std::vector<int>& history, std::string vital_type);

    // Threshold checking. float (not int) so temperature (patient.vitals.temp
    // is a float) can reuse these directly -- existing int-valued vitals
    // (hr/rr/spo2/bp_sys) convert to float exactly at the call site, no
    // behavior change.
    bool is_critical_threshold(float value, std::string vital_type);
    bool is_warning_threshold(float value, std::string vital_type);
    
    // Pattern recognition
    bool detect_respiratory_compromise(const Patient& p);
    bool detect_shock_pattern(const Patient& p);
    bool detect_sepsis_indicators(const Patient& p);
    
    // Clinical correlation
    std::vector<std::string> correlate_vitals(const Patient& p);
    
private:
    // Threshold values. float (not int) so temp_thresholds can express
    // proper decimal precision (e.g. 38.0f) -- the existing integer-valued
    // vitals' thresholds (e.g. 40) still initialize the same way, just as
    // float literals now.
    struct VitalThresholds {
        float critical_low;
        float warning_low;
        float normal_low;
        float normal_high;
        float warning_high;
        float critical_high;
    };

    VitalThresholds hr_thresholds;
    VitalThresholds spo2_thresholds;
    VitalThresholds bp_sys_thresholds;
    VitalThresholds rr_thresholds;
    VitalThresholds temp_thresholds;
    
    // Helper functions
    // engine is used to reach both OpticTrigeminal (the retrieval-augmentation
    // lookup) and OpticEmbedder (embedding the canned rationale text as a
    // query) -- see generate_rationale's definition.
    std::string generate_rationale(const Patient& p, std::string finding, NativeInferenceEngine* engine);
    std::vector<std::string> suggest_actions(std::string observation_type, std::string severity);
    float calculate_confidence(const Patient& p, std::string observation);
    
    // Trend detection helpers
    float calculate_slope(const std::vector<int>& data);
    bool is_monotonic_trend(const std::vector<int>& data, bool increasing);
    int count_consecutive_changes(const std::vector<int>& data, bool increasing);
};

// Generates SBAR (Situation, Background, Assessment, Recommendation) notes
class DocumentationScaffold {
public:
    DocumentationScaffold();
    
    // Generate full SBAR note
    std::string generate_sbar(
        const Patient& patient,
        const std::vector<ClinicalObservation>& observations
    );
    
    // Individual SBAR component generation
    std::string generate_situation(const Patient& p, const std::vector<ClinicalObservation>& obs);
    std::string generate_background(const Patient& p);
    std::string generate_assessment(const std::vector<ClinicalObservation>& obs);
    std::string generate_recommendation(const std::vector<ClinicalObservation>& obs);
    
private:
    std::string get_observation_summary(const std::vector<ClinicalObservation>& obs);
};
