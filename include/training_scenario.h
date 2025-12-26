#pragma once

#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <memory>

struct ScenarioVitals {
    int hr;
    int rr;
    int spo2;
    int bp_sys;
    int bp_dia;
    float temp;
    std::time_t timestamp;
};

struct TimelineEvent {
    int t_min;
    std::string event_name;
    std::map<std::string, float> vitals_delta;
};

struct ActionEffect {
    std::string action_name;
    int onset_delay_sec;
    int duration_sec;
    std::map<std::string, float> vital_effects;
    float efficacy_multiplier;
};

struct AIRecommendation {
    std::string id;
    std::string trigger_condition;
    std::string text;
    std::string priority;
    std::time_t issued_at;
    bool was_accepted;
    std::string accepted_by;
};

struct FailureCondition {
    std::string condition_name;
    std::string condition_expr;
    std::string result_escalation;
};

struct TrainingSession {
    std::string session_id;
    std::string scenario_id;
    std::string nurse_id;
    std::string nurse_role;
    std::time_t start_time;
    std::time_t end_time;
    bool completed;
    std::string outcome;
};

struct ScenarioDefinition {
    std::string scenario_id;
    std::string version;
    std::string tier;
    std::string title;
    std::string description;
    std::string context_mode;
    std::string context_unit;
    
    struct SyntheticPatient {
        std::string patient_id;
        int age;
        std::string sex;
        std::string diagnosis;
        ScenarioVitals baseline_vitals;
    } synthetic_patient;
    
    std::vector<TimelineEvent> timeline;
    std::vector<AIRecommendation> ai_recommendations;
    std::vector<ActionEffect> expected_actions;
    std::vector<FailureCondition> failure_conditions;
};

class ScenarioRuntime {
public:
    ScenarioRuntime(const ScenarioDefinition& def);
    
    void tick(int delta_seconds);
    void accept_action(const std::string& action, const std::string& nurse_id);
    void override_action(const std::string& action, const std::string& reason, const std::string& nurse_id);
    
    ScenarioVitals get_current_vitals() const;
    std::vector<AIRecommendation> get_pending_recommendations() const;
    std::vector<std::string> check_failure_conditions();
    
    int elapsed_seconds() const { return elapsed_time_sec_; }
    bool is_active() const { return is_active_; }
    std::string get_state() const { return current_state_; }
    
    TrainingSession get_session_record() const { return session_record_; }
    void finalize_session(const std::string& outcome);
    
    bool evaluate_action_correctness(const std::string& action, std::string& feedback);
    std::string get_escalation_reason();
    
private:
    ScenarioDefinition definition_;
    ScenarioVitals current_vitals_;
    ScenarioVitals last_vitals_;
    
    int elapsed_time_sec_;
    int last_tick_sec_;
    bool is_active_;
    std::string current_state_;
    
    std::vector<AIRecommendation> issued_recommendations_;
    std::map<std::string, std::time_t> active_actions_;
    
    TrainingSession session_record_;
    
    void apply_timeline_events();
    void apply_action_effects();
    void trigger_ai_recommendations();
    void mutate_vitals_baseline();
    
    float evaluate_condition(const std::string& expr);
};

class ScenarioLibrary {
public:
    static ScenarioDefinition load_scenario_json(const std::string& json_path);
    static std::vector<std::string> list_available_scenarios();
    static ScenarioDefinition create_hypotension_scenario();
    static ScenarioDefinition create_respiratory_distress_scenario();
    static ScenarioDefinition create_early_sepsis_scenario();
    static ScenarioDefinition create_cardiac_arrest_scenario();
    static ScenarioDefinition create_stroke_alert_scenario();
    static ScenarioDefinition create_dka_crisis_scenario();
    static ScenarioDefinition create_anaphylaxis_scenario();
    static ScenarioDefinition create_severe_bleeding_scenario();
};
