#include "training_scenario.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>
#include <random>

namespace physio_curve {

// Curve *shape* constants -- design choices for how an effect ramps in and
// decays out, not clinical/pharmacological fact. Every clinical magnitude
// (onset_delay_sec, duration_sec, vital_effects) stays exactly what each
// scenario already defines; this only wraps a bounded shape around them
// instead of the old unbounded per-tick reapplication.
constexpr float kRampFraction = 0.10f;
constexpr float kRampMinSec = 30.0f;
constexpr float kRampMaxSec = 300.0f;
constexpr float kDecayFraction = 0.20f;
constexpr float kDecayMinSec = 60.0f;
constexpr float kDecayMaxSec = 600.0f;

inline float smoothstep(float x) {
    x = std::max(0.0f, std::min(1.0f, x));
    return x * x * (3.0f - 2.0f * x);
}

inline float ramp_dur(float duration_sec) {
    float r = std::max(kRampMinSec, std::min(kRampMaxSec, kRampFraction * duration_sec));
    return std::min(r, duration_sec / 2.0f);
}

inline float decay_dur(float duration_sec) {
    float d = std::max(kDecayMinSec, std::min(kDecayMaxSec, kDecayFraction * duration_sec));
    return std::min(d, duration_sec / 2.0f);
}

// The single source of truth for one action/complication's current
// contribution to one vital -- purely a function of how long ago it was
// triggered, its onset/duration, and its peak effect. Recomputed from
// scratch every tick, never accumulated. This replaces the old
// `current_vitals_.hr += effect * efficacy_multiplier`, which ran again
// on *every* tick an action was active -- for a duration_sec=3600 action
// ticked once a minute, that's the delta re-applied ~60 times, causing
// runaway drift to the vitals clamp ceiling instead of a bounded,
// decaying response.
inline float contribution(float action_age_sec, float onset_delay_sec,
                           float duration_sec, float peak_effect) {
    if (duration_sec <= 0.0f || action_age_sec < onset_delay_sec) return 0.0f;

    float rd = ramp_dur(duration_sec);
    float dd = decay_dur(duration_sec);
    float t1 = onset_delay_sec + rd;
    float t2 = onset_delay_sec + duration_sec;
    float t3 = onset_delay_sec + duration_sec + dd;

    if (action_age_sec < t1) return peak_effect * smoothstep((action_age_sec - onset_delay_sec) / rd);
    if (action_age_sec < t2) return peak_effect;
    if (action_age_sec < t3) return peak_effect * (1.0f - smoothstep((action_age_sec - t2) / dd));
    return 0.0f;
}

inline float full_expiry_sec(float onset_delay_sec, float duration_sec) {
    return onset_delay_sec + duration_sec + decay_dur(duration_sec);
}

} // namespace physio_curve

ScenarioRuntime::ScenarioRuntime(const ScenarioDefinition& def)
    : definition_(def),
      elapsed_time_sec_(0),
      last_tick_sec_(0),
      is_active_(true),
      current_state_("INITIALIZING") {

    baseline_vitals_ = def.synthetic_patient.baseline_vitals;
    current_vitals_ = baseline_vitals_;
    last_vitals_ = current_vitals_;

    session_record_.session_id = def.scenario_id + "_" + std::to_string(std::time(nullptr));
    session_record_.scenario_id = def.scenario_id;
    session_record_.start_time = std::time(nullptr);
    session_record_.completed = false;
    session_record_.outcome = "";

    current_state_ = "ACTIVE";
}

void ScenarioRuntime::tick(int delta_seconds) {
    if (!is_active_) return;

    int minutes_before = elapsed_time_sec_ / 60;
    elapsed_time_sec_ += delta_seconds;
    int minutes_after = elapsed_time_sec_ / 60;
    last_vitals_ = current_vitals_;

    apply_timeline_events(minutes_before, minutes_after);
    mutate_vitals_baseline(minutes_after - minutes_before);
    recompute_vitals_with_overlay();
    trigger_ai_recommendations();

    auto failures = check_failure_conditions();
    for (const auto& name : failures) {
        if (triggered_failures_.find(name) == triggered_failures_.end()) {
            triggered_failures_[name] = elapsed_time_sec_;
        }
    }
    if (!failures.empty()) {
        current_state_ = "ESCALATED";
    }
}

void ScenarioRuntime::apply_timeline_events(int minutes_before, int minutes_after) {
    // Was an exact-minute equality check (event.t_min == current_minute),
    // so a single tick spanning more than one simulated minute (this
    // file's own test harness calls tick(300) in one shot) silently
    // skipped every timeline event strictly between the before/after
    // elapsed time. Range-matching (minutes_before, minutes_after] fires
    // every event actually crossed, regardless of tick granularity.
    // Mutates baseline_vitals_, not current_vitals_ -- see
    // recompute_vitals_with_overlay for why the split exists.
    for (const auto& event : definition_.timeline) {
        if (event.t_min > minutes_before && event.t_min <= minutes_after) {
            for (const auto& [vital, delta] : event.vitals_delta) {
                if (vital == "HR") baseline_vitals_.hr += static_cast<int>(delta);
                else if (vital == "BP") baseline_vitals_.bp_sys += static_cast<int>(delta);
                else if (vital == "RR") baseline_vitals_.rr += static_cast<int>(delta);
                else if (vital == "SpO2") baseline_vitals_.spo2 += static_cast<int>(delta);
                else if (vital == "Temp") baseline_vitals_.temp += delta;
            }
        }
    }
}

void ScenarioRuntime::mutate_vitals_baseline(int minutes_crossed) {
    // Same range-crossing fix as apply_timeline_events: jitter applies
    // once per simulated minute actually crossed, not once per tick call,
    // so its variance doesn't shrink as tick granularity coarsens.
    for (int i = 0; i < minutes_crossed; ++i) {
        baseline_vitals_.hr += (std::rand() % 5 - 2);
        baseline_vitals_.bp_sys += (std::rand() % 4 - 2);
        baseline_vitals_.rr += (std::rand() % 3 - 1);
    }

    // Clamped here too, not just on current_vitals_ in
    // recompute_vitals_with_overlay -- baseline never receives an action's
    // temporary peak (that lives entirely in the overlay computation), so
    // there's no risk of this clipping a drug effect the way clamping a
    // single shared current_vitals_ used to. Without this, minutes_crossed
    // random-walking over a long session could drift baseline_vitals_.hr
    // negative indefinitely (unlike current_vitals_, which was always
    // floored at 0) -- harmless-looking until a strong intervention's
    // overlay isn't enough to pull an already very-negative baseline back
    // into a physiologically real range.
    baseline_vitals_.hr = std::max(0, std::min(160, baseline_vitals_.hr));
    baseline_vitals_.bp_sys = std::max(60, std::min(180, baseline_vitals_.bp_sys));
    baseline_vitals_.spo2 = std::max(70, std::min(100, baseline_vitals_.spo2));
    baseline_vitals_.rr = std::max(8, std::min(40, baseline_vitals_.rr));

    baseline_vitals_.timestamp = std::time(nullptr);
}

void ScenarioRuntime::recompute_vitals_with_overlay() {
    // current_vitals_ is a pure function of baseline_vitals_ (timeline +
    // jitter) plus every active action's and triggered complication's
    // current curve contribution -- recomputed fresh every tick, never
    // incrementally mutated. Splitting baseline out of current_vitals_ is
    // what actually fixes the runaway-effect bug: recomputing "from
    // scratch" onto a field that still had *previous ticks'* overlay
    // baked into it would just move the accumulation bug down a level.
    float hr_overlay = 0, rr_overlay = 0, spo2_overlay = 0, bp_overlay = 0, temp_overlay = 0;

    std::vector<std::string> expired_actions;
    for (auto& [action_name, start_time] : active_actions_) {
        int action_age_sec = elapsed_time_sec_ - start_time;
        bool found = false;
        for (const auto& action_def : definition_.expected_actions) {
            if (action_def.action_name != action_name) continue;
            found = true;
            for (const auto& [vital, effect] : action_def.vital_effects) {
                float c = physio_curve::contribution(
                    static_cast<float>(action_age_sec), static_cast<float>(action_def.onset_delay_sec),
                    static_cast<float>(action_def.duration_sec), effect * action_def.efficacy_multiplier);
                if (vital == "HR") hr_overlay += c;
                else if (vital == "RR") rr_overlay += c;
                else if (vital == "SpO2") spo2_overlay += c;
                else if (vital == "BP_sys") bp_overlay += c;
                else if (vital == "Temp") temp_overlay += c;
            }
            if (action_age_sec >= physio_curve::full_expiry_sec(
                    static_cast<float>(action_def.onset_delay_sec), static_cast<float>(action_def.duration_sec))) {
                expired_actions.push_back(action_name);
            }
        }
        if (!found) expired_actions.push_back(action_name);
    }
    for (const auto& name : expired_actions) active_actions_.erase(name);

    for (auto& [name, trigger_time] : triggered_failures_) {
        int age_sec = elapsed_time_sec_ - trigger_time;
        for (const auto& fc : definition_.failure_conditions) {
            if (fc.condition_name != name) continue;
            const auto& eff = fc.complication_effect;
            if (eff.vital_effects.empty()) continue;  // escalation-only, no physiology change
            for (const auto& [vital, effect] : eff.vital_effects) {
                float c = physio_curve::contribution(
                    static_cast<float>(age_sec), static_cast<float>(eff.onset_delay_sec),
                    static_cast<float>(eff.duration_sec), effect * eff.efficacy_multiplier);
                if (vital == "HR") hr_overlay += c;
                else if (vital == "RR") rr_overlay += c;
                else if (vital == "SpO2") spo2_overlay += c;
                else if (vital == "BP_sys") bp_overlay += c;
                else if (vital == "Temp") temp_overlay += c;
            }
        }
    }

    current_vitals_.hr = std::max(0, std::min(160, baseline_vitals_.hr + static_cast<int>(hr_overlay)));
    current_vitals_.rr = std::max(8, std::min(40, baseline_vitals_.rr + static_cast<int>(rr_overlay)));
    current_vitals_.spo2 = std::max(70, std::min(100, baseline_vitals_.spo2 + static_cast<int>(spo2_overlay)));
    current_vitals_.bp_sys = std::max(60, std::min(180, baseline_vitals_.bp_sys + static_cast<int>(bp_overlay)));
    current_vitals_.temp = baseline_vitals_.temp + temp_overlay;
    current_vitals_.bp_dia = baseline_vitals_.bp_dia;
    current_vitals_.timestamp = std::time(nullptr);

    if (first_shock_onset_sec_ < 0 && current_vitals_.bp_sys < 90) {
        first_shock_onset_sec_ = elapsed_time_sec_;
    }
}

void ScenarioRuntime::trigger_ai_recommendations() {
    for (auto& rec : definition_.ai_recommendations) {
        if (!rec.was_accepted) {
            float condition_val = evaluate_condition(rec.trigger_condition);
            if (condition_val > 0.5f) {
                rec.issued_at = std::time(nullptr);
            }
        }
    }
}

void ScenarioRuntime::accept_action(const std::string& action, const std::string& nurse_id) {
    active_actions_[action] = elapsed_time_sec_;
    action_history_.push_back({action, elapsed_time_sec_});

    for (auto& rec : definition_.ai_recommendations) {
        if (rec.text.find(action) != std::string::npos) {
            rec.was_accepted = true;
            rec.accepted_by = nurse_id;
        }
    }
}

void ScenarioRuntime::arm_complication(const std::string& complication_name) {
    if (triggered_failures_.find(complication_name) != triggered_failures_.end()) return;
    for (const auto& fc : definition_.failure_conditions) {
        if (fc.condition_name == complication_name) {
            triggered_failures_[complication_name] = elapsed_time_sec_;
            current_state_ = "ESCALATED";
            return;
        }
    }
}

void ScenarioRuntime::override_action(const std::string& action, const std::string& reason, const std::string& nurse_id) {
    for (auto& rec : definition_.ai_recommendations) {
        if (rec.text.find(action) != std::string::npos) {
            rec.was_accepted = false;
            rec.accepted_by = nurse_id;
        }
    }
}

ScenarioVitals ScenarioRuntime::get_current_vitals() const {
    return current_vitals_;
}

std::vector<AIRecommendation> ScenarioRuntime::get_pending_recommendations() const {
    std::vector<AIRecommendation> pending;
    for (const auto& rec : definition_.ai_recommendations) {
        if (!rec.was_accepted && rec.issued_at > 0) {
            pending.push_back(rec);
        }
    }
    return pending;
}

std::vector<std::string> ScenarioRuntime::check_failure_conditions() {
    std::vector<std::string> triggered;
    
    for (const auto& failure : definition_.failure_conditions) {
        float condition_val = evaluate_condition(failure.condition_expr);
        if (condition_val > 0.5f) {
            triggered.push_back(failure.condition_name);
        }
    }
    
    return triggered;
}

void ScenarioRuntime::finalize_session(const std::string& outcome) {
    is_active_ = false;
    session_record_.end_time = std::time(nullptr);
    session_record_.completed = true;
    session_record_.outcome = outcome;
}

float ScenarioRuntime::evaluate_condition(const std::string& expr) {
    // Every failure_conditions entry across all 8 scenarios (training_scenario.cpp's
    // ScenarioLibrary::create_*) was checked against this function -- only
    // "no_action_within_20_min" (used by 2 scenarios) actually matched
    // anything here; the other 11 condition_expr strings (e.g.
    // "no_intervention_5min", "SpO2 < 85 AND RR > 30",
    // "HR_remains_zero_after_10min") had no matching branch at all, so
    // check_failure_conditions() could never trigger them regardless of
    // what a nurse did or didn't do -- FAILURE_TRIGGERED simply never fired
    // for those, silently. Generalized the patterns below to cover every
    // condition that's expressible from vitals + elapsed time + "was any
    // intervention taken" alone.

    if (expr.find("SpO2 < 85") != std::string::npos &&
        expr.find("RR > 30") != std::string::npos) {
        return (current_vitals_.spo2 < 85 && current_vitals_.rr > 30) ? 1.0f : 0.0f;
    }

    if (expr.find("Temp > 38.5") != std::string::npos &&
        expr.find("BP_sys < 100") != std::string::npos) {
        return (current_vitals_.temp > 38.5f && current_vitals_.bp_sys < 100) ? 1.0f : 0.0f;
    }

    if (expr.find("BP_sys < 90") != std::string::npos) {
        return (current_vitals_.bp_sys < 90) ? 1.0f : 0.0f;
    }

    if (expr.find("SpO2 < 90") != std::string::npos) {
        return (current_vitals_.spo2 < 90) ? 1.0f : 0.0f;
    }

    if (expr.find("RR > 24") != std::string::npos) {
        return (current_vitals_.rr > 24) ? 1.0f : 0.0f;
    }

    // "HR_remains_zero_after_10min" (cardiac arrest's no_rosc condition) --
    // a vital stuck at a critical value past a time threshold. Distinct
    // from "no intervention taken" below: a nurse can be actively
    // intervening and this still fires if it isn't working.
    {
        static const std::regex vital_stuck_after_min(R"((\w+)_remains_zero_after_(\d+)min)");
        std::smatch m;
        if (std::regex_search(expr, m, vital_stuck_after_min)) {
            int threshold_sec = std::stoi(m[2].str()) * 60;
            if (elapsed_time_sec_ > threshold_sec && m[1].str() == "HR") {
                return current_vitals_.hr == 0 ? 1.0f : 0.0f;
            }
        }
    }

    // "no_action_within_20_min" / "no_intervention_5min" /
    // "no_intervention_3min" / "no_hemorrhage_control_10min" -- all the
    // same underlying condition (N minutes elapse with zero interventions
    // ever taken), just phrased differently per scenario. Reads
    // action_history_ (permanent) rather than active_actions_ (transient,
    // pruned once an action's physiology curve decays) -- an action taken
    // early and long since worn off still counts as "an intervention was
    // taken," which active_actions_ alone couldn't represent once
    // recompute_vitals_with_overlay started pruning it on curve decay
    // instead of only on raw window expiry.
    {
        static const std::regex no_action_within_min(R"(no_.*?(\d+)_?min)");
        std::smatch m;
        if (std::regex_search(expr, m, no_action_within_min)) {
            int threshold_sec = std::stoi(m[1].str()) * 60;
            return (elapsed_time_sec_ > threshold_sec && action_history_.empty()) ? 1.0f : 0.0f;
        }
    }

    if (expr == "action_after_270min") {
        // Reuses the exact same 16200s (4.5hr tPA window) already present
        // in this file's own evaluate_action_correctness -- not a new
        // number.
        for (const auto& [name, at] : action_history_) {
            if (name == "tpa_administration" && at > 16200) return 1.0f;
        }
        return 0.0f;
    }

    if (expr == "delayed_tpa_dosing") {
        // Reframed as ordering, not timing: this codebase has no "delayed
        // dosing" timestamp to check against, but tPA given without CT
        // ruling out hemorrhage first is a real, groundable
        // contraindication (matches this scenario's own AI-recommendation
        // ordering, CT before tPA). Mirrors the same check
        // evaluate_action_correctness's STROKE_ALERT_001/tpa_administration
        // branch makes directly, for the tick-driven escalation path.
        int ct_at = -1, tpa_at = -1;
        for (const auto& [name, at] : action_history_) {
            if (name == "ct_head" && ct_at < 0) ct_at = at;
            if (name == "tpa_administration" && tpa_at < 0) tpa_at = at;
        }
        return (tpa_at >= 0 && (ct_at < 0 || tpa_at < ct_at)) ? 1.0f : 0.0f;
    }

    if (expr == "rapid_fluid_admin_early") {
        // "Rapid" implies an infusion rate this data model has no field
        // for -- reframed as ordering (fluids before labs) rather than
        // inventing a rate. See evaluate_action_correctness's DKA
        // insulin_infusion branch for the sibling ordering check.
        int fluids_at = -1, labs_at = -1;
        for (const auto& [name, at] : action_history_) {
            if (name == "iv_fluids" && fluids_at < 0) fluids_at = at;
            if (name == "obtain_labs" && labs_at < 0) labs_at = at;
        }
        return (fluids_at >= 0 && (labs_at < 0 || fluids_at < labs_at)) ? 1.0f : 0.0f;
    }

    if (expr == "insulin_too_early") {
        int insulin_at = -1, fluids_at = -1;
        for (const auto& [name, at] : action_history_) {
            if (name == "insulin_infusion" && insulin_at < 0) insulin_at = at;
            if (name == "iv_fluids" && fluids_at < 0) fluids_at = at;
        }
        return (insulin_at >= 0 && (fluids_at < 0 || insulin_at < fluids_at)) ? 1.0f : 0.0f;
    }

    if (expr == "delayed_massive_transfusion") {
        // Reuses this same scenario's own sibling condition's 10-minute
        // figure (no_hemorrhage_control_10min) by analogy -- a judgment
        // call, not a literal shared constant.
        if (first_shock_onset_sec_ < 0) return 0.0f;
        for (const auto& [name, at] : action_history_) {
            if (name == "massive_transfusion") return (at - first_shock_onset_sec_ > 600) ? 1.0f : 0.0f;
        }
        return (elapsed_time_sec_ - first_shock_onset_sec_ > 600) ? 1.0f : 0.0f;
    }

    // "no_observation_period" left unimplemented: real anaphylaxis
    // observation windows are hours, this scenario runs ~15 simulated
    // minutes, and there's no existing number anywhere in this scenario's
    // own data to ground a threshold in -- inventing one would be a
    // guess, not a grounded value.
    return 0.0f;
}

ScenarioDefinition ScenarioLibrary::create_hypotension_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "HYPOTENSION_001";
    scenario.version = "1.0.0";
    scenario.tier = "FOUNDATIONAL";
    scenario.title = "Acute Hypotension Management";
    scenario.description = "BP trending downward, AI suggests fluids then vasopressors";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "6 West";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00001";
    scenario.synthetic_patient.age = 62;
    scenario.synthetic_patient.sex = "M";
    scenario.synthetic_patient.diagnosis = "Post-op abdominal";
    scenario.synthetic_patient.baseline_vitals = {85, 18, 96, 115, 70, 37.2f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "baseline_stable", {}},
        {5, "bp_decline_onset", {{"BP", -15}}},
        {10, "tachycardia", {{"HR", +12}}},
        {15, "severe_hypotension", {{"BP", -20}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "BP_sys < 90",
            "Initiate IV fluid bolus",
            "HIGH",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "BP_sys < 85",
            "Consider vasopressor support (norepinephrine)",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "apply_iv_fluids",
            180,
            600,
            {{"BP_sys", +8.0f}},
            1.0f
        },
        {
            "start_vasopressor",
            60,
            3600,
            {{"BP_sys", +15.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "no_intervention_20min",
            "no_action_within_20_min",
            "progress_to_shock",
            {"", 0, 600, {{"BP_sys", -10.0f}, {"HR", 8.0f}}, 1.0f}
        }
    };

    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_respiratory_distress_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "RESPIRATORY_001";
    scenario.version = "1.0.0";
    scenario.tier = "FOUNDATIONAL";
    scenario.title = "Respiratory Distress Management";
    scenario.description = "Rising RR, falling SpO2 — oxygen escalation protocol";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "6 West";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00002";
    scenario.synthetic_patient.age = 58;
    scenario.synthetic_patient.sex = "F";
    scenario.synthetic_patient.diagnosis = "Pneumonia";
    scenario.synthetic_patient.baseline_vitals = {78, 20, 94, 118, 72, 38.1f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "baseline", {}},
        {8, "rr_increase", {{"RR", +6}}},
        {12, "spo2_drop", {{"SpO2", -5}}},
        {18, "critical_hypoxia", {{"SpO2", -8}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "RR > 24",
            "Increase oxygen delivery to maintain SpO2 > 94%",
            "HIGH",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "SpO2 < 90",
            "Consider non-invasive ventilation (CPAP/BiPAP)",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "apply_oxygen",
            30,
            300,
            {{"SpO2", +4.0f}},
            1.0f
        },
        {
            "call_respiratory",
            60,
            1800,
            {{"SpO2", +6.0f}, {"RR", -4.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "respiratory_failure",
            "SpO2 < 85 AND RR > 30",
            "require_intubation",
            {"", 0, 1800, {{"SpO2", -6.0f}, {"RR", 4.0f}}, 1.0f}
        }
    };

    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_early_sepsis_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "SEPSIS_EARLY_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Early Sepsis Recognition";
    scenario.description = "Subtle vitals drift requiring early intervention";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "6 West";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00003";
    scenario.synthetic_patient.age = 67;
    scenario.synthetic_patient.sex = "M";
    scenario.synthetic_patient.diagnosis = "Suspected infection";
    scenario.synthetic_patient.baseline_vitals = {92, 18, 96, 118, 72, 37.6f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "baseline_stable", {}},
        {10, "fever_spike", {{"Temp", +1.2f}, {"HR", +8}}},
        {20, "hypotension_onset", {{"BP", -20}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "Temp > 38.5 AND BP_sys < 100",
            "Initiate sepsis bundle: blood cultures, lactate, IV fluids, antibiotics",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "initiate_sepsis_bundle",
            180,
            600,
            {{"BP_sys", +10.0f}},
            1.0f
        },
        {
            "get_blood_cultures",
            300,
            3600,
            {{"Temp", -0.5f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "septic_shock",
            "no_action_within_20_min",
            "progress_to_septic_shock",
            {"", 0, 1800, {{"BP_sys", -10.0f}, {"HR", 10.0f}}, 1.0f}
        }
    };
    
    return scenario;
}

ActionEvaluation ScenarioRuntime::evaluate_action_correctness(const std::string& action) {
    // Score deltas below are illustrative design choices for the curve
    // *shape* of grading (how much better CORRECT should feel than
    // PARTIALLY_CORRECT), not a validated clinical rubric -- flagged for
    // review, same as physio_curve's constants above.
    ActionEvaluation eval;
    eval.grade = ActionGrade::PARTIALLY_CORRECT;
    eval.score_delta = 0.02f;
    eval.feedback = "Action recorded - monitoring for clinical effect";

    auto correct = [&](const std::string& fb) {
        eval.grade = ActionGrade::CORRECT; eval.score_delta = 0.10f; eval.feedback = fb;
    };
    auto partial = [&](const std::string& fb) {
        eval.grade = ActionGrade::PARTIALLY_CORRECT; eval.score_delta = 0.02f; eval.feedback = fb;
    };
    auto premature = [&](const std::string& fb) {
        eval.grade = ActionGrade::PREMATURE; eval.score_delta = -0.02f; eval.feedback = fb;
    };
    auto incorrect = [&](const std::string& fb) {
        eval.grade = ActionGrade::INCORRECT; eval.score_delta = -0.05f; eval.feedback = fb;
    };
    auto contraindicated = [&](const std::string& fb, const std::string& complication) {
        eval.grade = ActionGrade::CONTRAINDICATED; eval.score_delta = -0.15f; eval.feedback = fb;
        eval.triggers_complication = true; eval.complication_name = complication;
    };

    if (definition_.scenario_id == "HYPOTENSION_001") {
        if (action == "apply_iv_fluids" || action == "start_vasopressor") {
            if (current_vitals_.bp_sys < 90) correct("Correct action - appropriate for hypotensive patient");
            else premature("Premature action - BP not critically low yet; should wait for AI recommendation threshold");
        }
    } else if (definition_.scenario_id == "RESPIRATORY_001") {
        if (action == "apply_oxygen") {
            if (current_vitals_.spo2 < 94) correct("Correct action - oxygen indicated for low SpO2");
            else premature("Premature action - SpO2 not yet critical; monitor first");
        } else if (action == "call_respiratory") {
            if (current_vitals_.rr > 25) correct("Appropriate escalation - high respiratory rate warrants specialist");
            else partial("Reasonable to involve respiratory therapy, though RR isn't critically elevated yet");
        }
    } else if (definition_.scenario_id == "SEPSIS_EARLY_001") {
        if (action == "initiate_sepsis_bundle") {
            // Matches this scenario's own REC-001 trigger_condition text
            // ("Temp > 38.5 AND BP_sys < 100") shown to the nurse as the
            // AI recommendation (fixed earlier this session -- was
            // checking rr > 20, a vital this scenario's own timeline
            // never actually moves).
            if (current_vitals_.temp > 38.5f && current_vitals_.bp_sys < 100) correct("Correct action - sepsis bundle appropriate for fever + tachypnea");
            else premature("Early action - wait for more vital sign deterioration before full bundle");
        } else if (action == "get_blood_cultures") {
            if (current_vitals_.temp > 37.5f) correct("Correct action - blood cultures essential for suspected infection");
            else partial("Reasonable diagnostic step even before fever is fully established");
        }
    } else if (definition_.scenario_id == "CARDIAC_ARREST_001") {
        if (action == "initiate_cpr") {
            if (current_vitals_.hr == 0) correct("Correct action - CPR initiated for cardiac arrest");
            else partial("Pulse check ambiguous - continuing CPR is the safe default in suspected arrest");
        } else if (action == "defibrillate") {
            correct("Correct action - defibrillation appropriate for VF/pulseless VT");
        } else if (action == "epinephrine") {
            correct("Correct action - epinephrine per ACLS guidelines");
        }
    } else if (definition_.scenario_id == "STROKE_ALERT_001") {
        if (action == "activate_stroke_alert") {
            correct("Correct action - stroke alert activation is time-critical");
        } else if (action == "ct_head") {
            correct("Correct action - imaging needed to rule out hemorrhage");
        } else if (action == "tpa_administration") {
            bool ct_done = false;
            for (const auto& [name, at] : action_history_) if (name == "ct_head") ct_done = true;
            if (elapsed_time_sec_ >= 16200) {
                incorrect("Too late - tPA window has closed");
            } else if (!ct_done) {
                // Real contraindication: thrombolytics without imaging to
                // rule out hemorrhage first risks converting an ischemic
                // stroke into a fatal bleed -- matches this scenario's own
                // failure_conditions/delayed_tpa_dosing (evaluate_condition).
                contraindicated("Contraindicated - tPA given without CT ruling out hemorrhage first", "hemorrhagic_transform");
            } else {
                correct("Correct action - tPA within 4.5-hour window, hemorrhage ruled out first");
            }
        }
    } else if (definition_.scenario_id == "DKA_CRISIS_001") {
        if (action == "obtain_labs") {
            correct("Correct action - labs essential for DKA diagnosis");
        } else if (action == "iv_fluids") {
            if (current_vitals_.rr > 24) correct("Correct action - aggressive hydration needed");
            else partial("Reasonable to begin hydration even before RR criteria are fully met");
        } else if (action == "insulin_infusion") {
            bool fluids_first = false;
            for (const auto& [name, at] : action_history_) if (name == "iv_fluids") fluids_first = true;
            if (!fluids_first) {
                // Real teaching: insulin before adequate fluid/K+
                // repletion in DKA risks hypoglycemia and cerebral edema
                // -- matches failure_conditions/insulin_too_early.
                contraindicated("Contraindicated - insulin before fluid resuscitation risks hypoglycemia/cerebral edema", "hypoglycemia");
            } else if (current_vitals_.temp > 37.8f && current_vitals_.rr > 20) {
                correct("Correct action - insulin infusion appropriate after fluid resuscitation");
            } else {
                partial("Fluids given first as expected, though vitals criteria for insulin aren't fully met yet");
            }
        }
    } else if (definition_.scenario_id == "ANAPHYLAXIS_001") {
        if (action == "epinephrine_im") {
            if (elapsed_time_sec_ < 60) correct("Correct action - immediate IM epinephrine is lifesaving");
            else partial("Delayed but still appropriate - epinephrine should never be withheld in anaphylaxis");
        } else if (action == "airway_management") {
            correct("Correct action - prepare airway for potential intubation");
        } else if (action == "fluid_bolus") {
            correct("Correct action - fluid resuscitation for anaphylaxis");
        }
    } else if (definition_.scenario_id == "SEVERE_BLEEDING_001") {
        if (action == "hemorrhage_control") {
            // Direct pressure/tourniquet is appropriate for visible active
            // bleeding regardless of current BP -- unlike the other two
            // actions in this scenario, there's no clinical reason to gate
            // this on a vitals threshold.
            correct("Correct action - direct pressure/tourniquet is always appropriate for active bleeding");
        } else if (action == "massive_transfusion") {
            if (current_vitals_.bp_sys < 90) correct("Correct action - massive transfusion protocol for hemorrhagic shock");
            else premature("Premature action - blood products are a limited resource; wait for shock criteria");
        } else if (action == "emergency_surgery") {
            correct("Correct action - operating room needed for definitive bleeding control");
        }
    }

    return eval;
}

std::string ScenarioRuntime::get_escalation_reason() {
    auto failures = check_failure_conditions();
    
    if (failures.empty()) {
        return "";
    }
    
    std::string reason = "Escalation triggered: ";
    
    if (definition_.scenario_id == "HYPOTENSION_001") {
        if (current_vitals_.bp_sys < 70) {
            reason += "Critical hypotension (BP " + std::to_string(current_vitals_.bp_sys) + 
                     " mmHg) - IV fluids + vasopressors required immediately";
        } else if (current_vitals_.bp_sys < 85) {
            reason += "Severe hypotension (BP " + std::to_string(current_vitals_.bp_sys) + 
                     " mmHg) - rapid fluid resuscitation needed";
        }
    } else if (definition_.scenario_id == "RESPIRATORY_001") {
        if (current_vitals_.spo2 < 88) {
            reason += "Critical hypoxia (SpO2 " + std::to_string(current_vitals_.spo2) + 
                     "%) - oxygen + possible intubation required";
        } else if (current_vitals_.rr > 30) {
            reason += "Severe tachypnea (RR " + std::to_string(current_vitals_.rr) + 
                     ") - airway assessment urgent";
        }
    } else if (definition_.scenario_id == "SEPSIS_EARLY_001") {
        if (current_vitals_.temp > 39.0f && current_vitals_.hr > 110) {
            reason += "Sepsis progression - high fever + tachycardia + lactate likely elevated. "
                     "Broad-spectrum antibiotics + fluid bolus + source control needed";
        } else if (current_vitals_.bp_sys < 90 && current_vitals_.temp > 38.0f) {
            reason += "Septic shock criteria met (hypotension + fever). "
                     "ICU-level care with pressors + aggressive management required";
        }
    } else if (definition_.scenario_id == "CARDIAC_ARREST_001") {
        reason += "Cardiac arrest - immediate ROSC attempt required. "
                 "Continue CPR, consider ECMO if available, prepare for post-resuscitation care";
    } else if (definition_.scenario_id == "STROKE_ALERT_001") {
        reason += "Acute stroke with neurological deterioration - time critical. "
                 "Thrombolytic or thrombectomy window closing. Activate stroke team.";
    } else if (definition_.scenario_id == "DKA_CRISIS_001") {
        reason += "DKA with severe metabolic derangement - insulin/fluid balance critical. "
                 "Risk of cerebral edema, hypoglycemia, or hyperchloremic acidosis with aggressive therapy";
    } else if (definition_.scenario_id == "ANAPHYLAXIS_001") {
        reason += "Anaphylaxis with airway compromise - immediate threat to life. "
                 "Second dose of epinephrine, airway management, ICU monitoring required";
    } else if (definition_.scenario_id == "SEVERE_BLEEDING_001") {
        reason += "Hemorrhagic shock - patient exsanguinating. "
                 "Massive transfusion protocol, activate trauma surgery, prepare for OR";
    }
    
    return reason;
}

ScenarioDefinition ScenarioLibrary::create_cardiac_arrest_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "CARDIAC_ARREST_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Cardiac Arrest Management";
    scenario.description = "Unresponsive patient with no pulse - CPR and ACLS protocol required";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "6 West";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00004";
    scenario.synthetic_patient.age = 72;
    scenario.synthetic_patient.sex = "M";
    scenario.synthetic_patient.diagnosis = "Acute MI";
    scenario.synthetic_patient.baseline_vitals = {0, 0, 88, 60, 45, 36.8f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "cardiac_arrest", {}},
        {5, "prolonged_arrest", {{"SpO2", -8}, {"HR", -5}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "No pulse",
            "Initiate CPR: 30 chest compressions at 100-120/min",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "HR = 0",
            "Call code blue, attach AED/defibrillator, prepare for ACLS",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-003",
            "Arrest > 2 min",
            "Prepare epinephrine 1mg IV/IO every 3-5 minutes",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "initiate_cpr",
            0,
            180,
            {{"HR", +15.0f}},
            1.0f
        },
        {
            "defibrillate",
            30,
            600,
            {{"HR", +45.0f}, {"SpO2", +8.0f}},
            1.0f
        },
        {
            "epinephrine",
            120,
            3600,
            {{"HR", +20.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "prolonged_arrest",
            "no_intervention_5min",
            "irreversible_brain_damage"
        },
        {
            "no_rosc",
            "HR_remains_zero_after_10min",
            "death"
        }
    };
    
    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_stroke_alert_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "STROKE_ALERT_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Acute Stroke Alert";
    scenario.description = "Patient with sudden neurological deficits - time critical thrombolytic window";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "ED";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00005";
    scenario.synthetic_patient.age = 68;
    scenario.synthetic_patient.sex = "F";
    scenario.synthetic_patient.diagnosis = "Acute ischemic stroke";
    scenario.synthetic_patient.baseline_vitals = {88, 18, 96, 145, 92, 37.3f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "symptom_onset", {{"HR", +5}}},
        {10, "deteriorating_neuro", {}},
        {15, "increased_bp", {{"BP", +15}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "Sudden neuro deficit",
            "Activate stroke alert - last known well < 4.5 hours",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "Within thrombolytic window",
            "Stat CT head to rule out hemorrhage, prepare tPA",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-003",
            "CT negative for bleed",
            "Administer IV tPA 0.9 mg/kg within 4.5 hours of onset",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "activate_stroke_alert",
            0,
            120,
            {{"HR", -3.0f}},
            1.0f
        },
        {
            "ct_head",
            60,
            180,
            {},
            1.0f
        },
        {
            "tpa_administration",
            180,
            3600,
            {{"HR", +2.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "missed_window",
            "action_after_270min",
            "ineligible_for_thrombolytics"
        },
        {
            "hemorrhagic_transform",
            "delayed_tpa_dosing",
            "cerebral_hemorrhage",
            // Low confidence -- HR uptick is a coarse proxy for a
            // hemorrhagic complication this vitals model has no ICP/
            // neuro-status field to represent directly.
            {"", 0, 1800, {{"HR", 8.0f}}, 1.0f}
        }
    };
    
    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_dka_crisis_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "DKA_CRISIS_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Diabetic Ketoacidosis Management";
    scenario.description = "Diabetic patient with severe metabolic acidosis and altered mental status";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "ICU";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00006";
    scenario.synthetic_patient.age = 35;
    scenario.synthetic_patient.sex = "F";
    scenario.synthetic_patient.diagnosis = "Type 1 DM with DKA";
    scenario.synthetic_patient.baseline_vitals = {112, 28, 92, 95, 58, 38.9f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "altered_mental", {{"RR", +8}}},
        {8, "severe_acidosis", {{"HR", +15}, {"RR", +5}}},
        {15, "hypotension", {{"BP", -18}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "Kussmaul breathing + altered status",
            "Obtain VBG/ABG, check glucose, electrolytes, beta-hydroxybutyrate",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "Confirmed DKA",
            "Initiate aggressive IV hydration: 1-1.5L 0.9% NS first hour",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-003",
            "pH < 7.1",
            "Insulin drip 0.1 units/kg/hr, replace K+, monitor glucose hourly",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "iv_fluids",
            120,
            1200,
            {{"BP_sys", +12.0f}, {"RR", -3.0f}},
            1.0f
        },
        {
            "insulin_infusion",
            180,
            3600,
            {{"HR", -5.0f}},
            1.0f
        },
        {
            "obtain_labs",
            300,
            1800,
            {},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "cerebral_edema",
            "rapid_fluid_admin_early",
            "fatal_cerebral_edema",
            // Rising ICP presenting as early Cushing's triad (bradycardia
            // + rising BP) -- a real, recognizable pattern, not arbitrary.
            {"", 0, 1800, {{"HR", -10.0f}, {"BP_sys", 10.0f}}, 1.0f}
        },
        {
            "hypoglycemia",
            "insulin_too_early",
            "severe_hypoglycemia",
            // Adrenergic/sympathetic response to hypoglycemia -- tachycardia.
            {"", 0, 1800, {{"HR", 15.0f}}, 1.0f}
        }
    };
    
    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_anaphylaxis_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "ANAPHYLAXIS_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Anaphylaxis Management";
    scenario.description = "Acute allergic reaction with airway compromise and hypotension";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "ED";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00007";
    scenario.synthetic_patient.age = 42;
    scenario.synthetic_patient.sex = "M";
    scenario.synthetic_patient.diagnosis = "Drug-induced anaphylaxis";
    scenario.synthetic_patient.baseline_vitals = {128, 26, 88, 78, 40, 37.5f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "reaction_onset", {{"HR", +18}, {"RR", +10}}},
        {5, "airway_edema", {{"SpO2", -12}}},
        {10, "cardiovascular_collapse", {{"BP", -35}, {"HR", +25}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "Urticaria + wheeze + hypotension",
            "IMMEDIATE: IM epinephrine 0.3-0.5mg (1:1000) to lateral thigh",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "Airway compromise",
            "Prepare for emergency intubation, consider cricothyrotomy",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-003",
            "Persistent hypotension",
            "Large bore IV access, aggressive fluid bolus, second epi dose at 5-15 min",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "epinephrine_im",
            0,
            180,
            {{"HR", -15.0f}, {"BP_sys", +18.0f}, {"SpO2", +8.0f}},
            1.0f
        },
        {
            "airway_management",
            30,
            120,
            {},
            1.0f
        },
        {
            "fluid_bolus",
            60,
            600,
            {{"BP_sys", +15.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "airway_loss",
            "no_intervention_3min",
            "failed_intubation_arrest"
        },
        {
            "biphasic_reaction",
            "no_observation_period",
            "late_cardiovascular_collapse"
        }
    };
    
    return scenario;
}

ScenarioDefinition ScenarioLibrary::create_severe_bleeding_scenario() {
    ScenarioDefinition scenario;
    scenario.scenario_id = "SEVERE_BLEEDING_001";
    scenario.version = "1.0.0";
    scenario.tier = "CRISIS";
    scenario.title = "Hemorrhagic Shock Management";
    scenario.description = "Major trauma with active bleeding and rapid hemodynamic deterioration";
    scenario.context_mode = "TRAINING";
    scenario.context_unit = "Trauma Bay";
    
    scenario.synthetic_patient.patient_id = "TRAIN-PT-00008";
    scenario.synthetic_patient.age = 45;
    scenario.synthetic_patient.sex = "M";
    scenario.synthetic_patient.diagnosis = "Multi-system trauma";
    scenario.synthetic_patient.baseline_vitals = {115, 24, 94, 88, 35, 37.1f, std::time(nullptr)};
    
    scenario.timeline = {
        {0, "trauma_arrival", {{"HR", +8}, {"RR", +6}}},
        {7, "increased_bleeding", {{"BP", -20}, {"HR", +20}}},
        {14, "hemorrhagic_shock", {{"BP", -30}, {"SpO2", -6}}}
    };
    
    scenario.ai_recommendations = {
        {
            "REC-001",
            "Uncontrolled bleeding",
            "Apply direct pressure + tourniquet to bleeding extremity",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-002",
            "BP < 90",
            "Massive transfusion protocol: O neg blood, activate blood bank",
            "CRITICAL",
            0,
            false,
            ""
        },
        {
            "REC-003",
            "Class IV shock",
            "Position supine, elevate legs, prepare for emergency surgery",
            "CRITICAL",
            0,
            false,
            ""
        }
    };
    
    scenario.expected_actions = {
        {
            "hemorrhage_control",
            0,
            600,
            {{"HR", -8.0f}},
            1.0f
        },
        {
            "massive_transfusion",
            120,
            1800,
            {{"BP_sys", +25.0f}},
            1.0f
        },
        {
            "emergency_surgery",
            300,
            3600,
            {{"BP_sys", +20.0f}, {"HR", -12.0f}},
            1.0f
        }
    };
    
    scenario.failure_conditions = {
        {
            "exsanguination",
            "no_hemorrhage_control_10min",
            "death_from_blood_loss",
            {"", 0, 1800, {{"BP_sys", -15.0f}, {"HR", 15.0f}}, 1.0f}
        },
        {
            "disseminated_coagulopathy",
            "delayed_massive_transfusion",
            "irreversible_shock",
            {"", 0, 1800, {{"BP_sys", -12.0f}, {"HR", 12.0f}}, 1.0f}
        }
    };
    
    return scenario;
}

ScenarioDefinition ScenarioLibrary::load_scenario_json(const std::string& json_path) {
    return create_hypotension_scenario();
}

std::vector<std::string> ScenarioLibrary::list_available_scenarios() {
    return {
        "HYPOTENSION_001",
        "RESPIRATORY_001",
        "SEPSIS_EARLY_001",
        "CARDIAC_ARREST_001",
        "STROKE_ALERT_001",
        "DKA_CRISIS_001",
        "ANAPHYLAXIS_001",
        "SEVERE_BLEEDING_001"
    };
}

namespace {
// Jitters `value` by up to +-fraction (default 3%), then clamps to the
// side of `boundary` given by `keep_below`/`keep_above` -- so a
// randomized baseline can never accidentally start already past a
// scenario's own t=0 grading threshold. Uses a per-call seeded RNG
// (decoupled from mutate_vitals_baseline's per-tick std::rand()) so case
// randomization doesn't consume/perturb the tick-jitter random stream.
int jitter_int(std::mt19937& rng, int value, float fraction = 0.03f) {
    int span = std::max(1, static_cast<int>(std::round(std::abs(value) * fraction)));
    std::uniform_int_distribution<int> d(-span, span);
    return value + d(rng);
}
float jitter_float(std::mt19937& rng, float value, float fraction = 0.03f) {
    float span = std::max(0.05f, std::abs(value) * fraction);
    std::uniform_real_distribution<float> d(-span, span);
    return value + d(rng);
}
} // namespace

ScenarioDefinition ScenarioLibrary::randomize_case(const ScenarioDefinition& base, uint32_t seed) {
    // Randomizes age and a conservative baseline-vitals jitter so the same
    // scenario doesn't play out identically every run. Deliberately does
    // NOT touch expected_actions/failure_conditions/timeline -- those are
    // the grading rubric, not case presentation. Uses its own seeded RNG,
    // not the global std::rand() mutate_vitals_baseline already uses for
    // per-tick jitter, so this is reproducible from `seed` independent of
    // however many ticks a session runs.
    ScenarioDefinition def = base;
    std::mt19937 rng(seed);

    std::uniform_int_distribution<int> age_jitter(-5, 5);
    def.synthetic_patient.age = std::max(18, std::min(95, def.synthetic_patient.age + age_jitter(rng)));

    auto& v = def.synthetic_patient.baseline_vitals;
    v.hr = std::max(0, jitter_int(rng, v.hr));
    v.bp_sys = std::max(60, jitter_int(rng, v.bp_sys));
    v.bp_dia = std::max(30, jitter_int(rng, v.bp_dia));

    // spo2/rr/temp are each gated by a t=0 grading threshold in at least
    // one scenario (evaluate_action_correctness's apply_oxygen/spo2<94 for
    // RESPIRATORY_001, initiate_sepsis_bundle/temp>38.5 for
    // SEPSIS_EARLY_001) with a tight enough margin from baseline that a
    // symmetric jitter risks crossing it and making an immediate action
    // wrongly grade as CORRECT. Jitter only toward the *harder* side of
    // whichever threshold this scenario's own baseline already sits
    // closest to, rather than a blanket symmetric range.
    if (def.scenario_id == "RESPIRATORY_001") {
        // baseline spo2=94 sits exactly at apply_oxygen's threshold already
        // -- only allow upward jitter (still not-yet-critical), never down.
        // (jitter_int/jitter_float return a new *value*, not a delta --
        // using them here would double-apply the baseline; a one-sided
        // distribution on the raw delta is what "only upward" actually needs.)
        int spo2_span = std::max(1, static_cast<int>(std::round(v.spo2 * 0.02f)));
        std::uniform_int_distribution<int> spo2_up(0, spo2_span);
        v.spo2 = std::min(100, v.spo2 + spo2_up(rng));
        v.rr = std::max(8, jitter_int(rng, v.rr));
    } else if (def.scenario_id == "SEPSIS_EARLY_001") {
        // baseline temp=37.6 is within ~3% of initiate_sepsis_bundle's
        // 38.5 threshold -- only allow downward jitter.
        float temp_span = std::max(0.05f, v.temp * 0.02f);
        std::uniform_real_distribution<float> temp_down(0.0f, temp_span);
        v.temp = std::max(35.0f, v.temp - temp_down(rng));
        v.rr = std::max(8, jitter_int(rng, v.rr));
    } else {
        v.spo2 = std::max(70, std::min(100, jitter_int(rng, v.spo2)));
        v.rr = std::max(8, jitter_int(rng, v.rr));
        v.temp = jitter_float(rng, v.temp);
    }

    return def;
}
