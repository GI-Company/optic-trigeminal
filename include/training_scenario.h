#pragma once

#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <memory>
#include <cstdint>

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
    // Empty vital_effects (default) = escalation-only, no physiology
    // change. When present, reuses the exact same onset/duration ramp-
    // plateau-decay curve engine as a normal ActionEffect (see
    // physio_curve in training_scenario.cpp) -- a complication is
    // physiologically just another timed effect, not a parallel system.
    // action_name is unused for these (kept only so the struct can share
    // ActionEffect's shape); onset_delay_sec is measured from the moment
    // the failure first triggers, not from session start.
    ActionEffect complication_effect;
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

// Widened from a plain bool so a wrong action can be "not ideal but not
// dangerous" (PARTIALLY_CORRECT/PREMATURE) versus "actively harmful"
// (CONTRAINDICATED) rather than a single flat penalty for everything that
// isn't the one exact-right answer.
enum class ActionGrade { CORRECT, PARTIALLY_CORRECT, PREMATURE, INCORRECT, CONTRAINDICATED };

struct ActionEvaluation {
    ActionGrade grade;
    float score_delta;
    std::string feedback;
    // When true, complication_name must match a FailureCondition::condition_name
    // in this scenario's failure_conditions -- the action itself (not a
    // tick-detected vitals/time condition) arms that complication's curve,
    // via ScenarioRuntime::arm_complication().
    bool triggers_complication = false;
    std::string complication_name;
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

    ActionEvaluation evaluate_action_correctness(const std::string& action);
    // Arms a named complication's physiology curve starting now (see
    // FailureCondition::complication_effect), from an action graded
    // CONTRAINDICATED rather than a tick-detected condition. No-op if the
    // name doesn't match a failure_conditions entry, or is already armed.
    void arm_complication(const std::string& complication_name);
    std::string get_escalation_reason();

    // Permanent, never-pruned action timing history (distinct from the
    // transient active_actions_ map below, which the physiology overlay
    // prunes once an effect's curve fully decays) -- failure conditions
    // like "was tPA given more than 4.5h in, or without CT first" need to
    // know an action ever happened and exactly when, long after its
    // physiological effect has ended.
    std::vector<std::pair<std::string, int>> get_action_history() const { return action_history_; }

    const ScenarioDefinition::SyntheticPatient& get_synthetic_patient() const { return definition_.synthetic_patient; }

private:
    ScenarioDefinition definition_;
    ScenarioVitals baseline_vitals_;   // timeline deltas + random jitter only, never clamped mid-flight
    ScenarioVitals current_vitals_;    // = clamp(baseline_vitals_ + action overlay + complication overlay), recomputed fresh every tick
    ScenarioVitals last_vitals_;

    int elapsed_time_sec_;
    int last_tick_sec_;
    bool is_active_;
    std::string current_state_;

    std::vector<AIRecommendation> issued_recommendations_;
    std::map<std::string, int> active_actions_;             // transient: action_name -> start elapsed_sec, pruned once its curve fully decays
    std::vector<std::pair<std::string, int>> action_history_; // permanent: every action ever taken, in order
    std::map<std::string, int> triggered_failures_;          // condition_name -> elapsed_sec it first triggered (one-shot arming)
    int first_shock_onset_sec_ = -1;                          // first tick bp_sys < 90 (SEVERE_BLEEDING_001's delayed_massive_transfusion)

    TrainingSession session_record_;

    void apply_timeline_events(int minutes_before, int minutes_after);
    void recompute_vitals_with_overlay();
    void trigger_ai_recommendations();
    void mutate_vitals_baseline(int minutes_crossed);

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

    // Returns a copy of `base` with age and a conservative baseline-vitals
    // jitter randomized from `seed`, so the same scenario doesn't play out
    // identically every run. expected_actions/failure_conditions/timeline
    // are NOT randomized -- they're the grading rubric itself, and several
    // evaluate_action_correctness/evaluate_condition branches assume the
    // baseline starts clearly on the non-triggering side of their
    // hardcoded thresholds.
    static ScenarioDefinition randomize_case(const ScenarioDefinition& base, uint32_t seed);
};
