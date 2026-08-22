#include "training_scenario.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <regex>

ScenarioRuntime::ScenarioRuntime(const ScenarioDefinition& def)
    : definition_(def),
      elapsed_time_sec_(0),
      last_tick_sec_(0),
      is_active_(true),
      current_state_("INITIALIZING") {
    
    current_vitals_ = def.synthetic_patient.baseline_vitals;
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
    
    elapsed_time_sec_ += delta_seconds;
    last_vitals_ = current_vitals_;
    
    apply_timeline_events();
    apply_action_effects();
    mutate_vitals_baseline();
    trigger_ai_recommendations();
    
    auto failures = check_failure_conditions();
    if (!failures.empty()) {
        current_state_ = "ESCALATED";
    }
}

void ScenarioRuntime::apply_timeline_events() {
    int current_minute = elapsed_time_sec_ / 60;
    
    for (const auto& event : definition_.timeline) {
        if (event.t_min == current_minute) {
            for (const auto& [vital, delta] : event.vitals_delta) {
                if (vital == "HR") current_vitals_.hr += static_cast<int>(delta);
                else if (vital == "BP") {
                    current_vitals_.bp_sys += static_cast<int>(delta);
                }
                else if (vital == "RR") current_vitals_.rr += static_cast<int>(delta);
                else if (vital == "SpO2") current_vitals_.spo2 += static_cast<int>(delta);
                else if (vital == "Temp") current_vitals_.temp += delta;
            }
        }
    }
}

void ScenarioRuntime::apply_action_effects() {
    std::vector<std::string> expired_actions;
    
    for (auto& [action_name, start_time] : active_actions_) {
        int action_age_sec = elapsed_time_sec_ - start_time;
        
        for (const auto& action_def : definition_.expected_actions) {
            if (action_def.action_name == action_name) {
                if (action_age_sec >= action_def.onset_delay_sec &&
                    action_age_sec < action_def.onset_delay_sec + action_def.duration_sec) {
                    
                    for (const auto& [vital, effect] : action_def.vital_effects) {
                        float actual_effect = effect * action_def.efficacy_multiplier;
                        
                        if (vital == "HR") current_vitals_.hr += static_cast<int>(actual_effect);
                        else if (vital == "BP_sys") current_vitals_.bp_sys += static_cast<int>(actual_effect);
                        else if (vital == "SpO2") current_vitals_.spo2 += static_cast<int>(actual_effect);
                    }
                }
                
                if (action_age_sec > action_def.onset_delay_sec + action_def.duration_sec) {
                    expired_actions.push_back(action_name);
                }
            }
        }
    }
    
    for (const auto& action : expired_actions) {
        active_actions_.erase(action);
    }
}

void ScenarioRuntime::mutate_vitals_baseline() {
    if (elapsed_time_sec_ % 60 == 0) {
        current_vitals_.hr += (std::rand() % 5 - 2);
        current_vitals_.bp_sys += (std::rand() % 4 - 2);
        current_vitals_.rr += (std::rand() % 3 - 1);
    }
    
    // Floor was 40 -- physiologically reasonable for every other scenario
    // (none starts below HR 78, see ScenarioLibrary::create_*), but wrong
    // as a *global* constraint: it silently propped CARDIAC_ARREST_001's
    // pulseless-patient HR (baseline 0) back up to 40 on the very first
    // tick, which made both current_vitals_.hr == 0 checks -- this file's
    // "no_rosc" failure condition and evaluate_action_correctness's
    // "Initiate CPR" feedback branch -- permanently unreachable no matter
    // what a nurse did.
    current_vitals_.hr = std::max(0, std::min(160, current_vitals_.hr));
    current_vitals_.bp_sys = std::max(60, std::min(180, current_vitals_.bp_sys));
    current_vitals_.spo2 = std::max(70, std::min(100, current_vitals_.spo2));
    current_vitals_.rr = std::max(8, std::min(40, current_vitals_.rr));
    
    current_vitals_.timestamp = std::time(nullptr);
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
    
    for (auto& rec : definition_.ai_recommendations) {
        if (rec.text.find(action) != std::string::npos) {
            rec.was_accepted = true;
            rec.accepted_by = nurse_id;
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
    // taken), just phrased differently per scenario. Matches whatever
    // active_actions_.empty() already meant for the two
    // "no_action_within_20_min" scenarios this originally worked for.
    {
        static const std::regex no_action_within_min(R"(no_.*?(\d+)_?min)");
        std::smatch m;
        if (std::regex_search(expr, m, no_action_within_min)) {
            int threshold_sec = std::stoi(m[1].str()) * 60;
            return (elapsed_time_sec_ > threshold_sec && active_actions_.empty()) ? 1.0f : 0.0f;
        }
    }

    // Still unimplemented -- these need a specific action's timing, not
    // just "any" action, and a real clinical deadline this codebase
    // doesn't have grounded data for (e.g. exactly when tPA dosing becomes
    // "delayed" for a specific patient). Returning 0 (never triggers)
    // rather than inventing a threshold: "action_after_270min",
    // "delayed_tpa_dosing", "rapid_fluid_admin_early", "insulin_too_early",
    // "no_observation_period", "delayed_massive_transfusion".
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
            "progress_to_shock"
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
            "require_intubation"
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
            "progress_to_septic_shock"
        }
    };
    
    return scenario;
}

bool ScenarioRuntime::evaluate_action_correctness(const std::string& action, std::string& feedback) {
    feedback = "";
    
    if (definition_.scenario_id == "HYPOTENSION_001") {
        if (action == "apply_iv_fluids" || action == "start_vasopressor") {
            if (current_vitals_.bp_sys < 90) {
                feedback = "Correct action - appropriate for hypotensive patient";
                return true;
            } else {
                feedback = "Premature action - BP not critically low yet; should wait for AI recommendation threshold";
                return false;
            }
        }
    } else if (definition_.scenario_id == "RESPIRATORY_001") {
        if (action == "apply_oxygen") {
            if (current_vitals_.spo2 < 94) {
                feedback = "Correct action - oxygen indicated for low SpO2";
                return true;
            } else {
                feedback = "Premature action - SpO2 not yet critical; monitor first";
                return false;
            }
        } else if (action == "call_respiratory") {
            if (current_vitals_.rr > 25) {
                feedback = "Appropriate escalation - high respiratory rate warrants specialist";
                return true;
            }
        }
    } else if (definition_.scenario_id == "SEPSIS_EARLY_001") {
        if (action == "initiate_sepsis_bundle") {
            if (current_vitals_.temp > 38.0f && current_vitals_.rr > 20) {
                feedback = "Correct action - sepsis bundle appropriate for fever + tachypnea";
                return true;
            } else {
                feedback = "Early action - wait for more vital sign deterioration before full bundle";
                return false;
            }
        } else if (action == "get_blood_cultures") {
            if (current_vitals_.temp > 37.5f) {
                feedback = "Correct action - blood cultures essential for suspected infection";
                return true;
            }
        }
    } else if (definition_.scenario_id == "CARDIAC_ARREST_001") {
        if (action == "initiate_cpr") {
            if (current_vitals_.hr == 0) {
                feedback = "Correct action - CPR initiated for cardiac arrest";
                return true;
            }
        } else if (action == "defibrillate") {
            feedback = "Correct action - defibrillation appropriate for VF/pulseless VT";
            return true;
        } else if (action == "epinephrine") {
            feedback = "Correct action - epinephrine per ACLS guidelines";
            return true;
        }
    } else if (definition_.scenario_id == "STROKE_ALERT_001") {
        if (action == "activate_stroke_alert") {
            feedback = "Correct action - stroke alert activation is time-critical";
            return true;
        } else if (action == "ct_head") {
            feedback = "Correct action - imaging needed to rule out hemorrhage";
            return true;
        } else if (action == "tpa_administration") {
            if (elapsed_time_sec_ < 16200) {
                feedback = "Correct action - tPA within 4.5-hour window";
                return true;
            } else {
                feedback = "Too late - tPA window has closed";
                return false;
            }
        }
    } else if (definition_.scenario_id == "DKA_CRISIS_001") {
        if (action == "obtain_labs") {
            feedback = "Correct action - labs essential for DKA diagnosis";
            return true;
        } else if (action == "iv_fluids") {
            if (current_vitals_.rr > 24) {
                feedback = "Correct action - aggressive hydration needed";
                return true;
            }
        } else if (action == "insulin_infusion") {
            if (current_vitals_.temp > 37.8f && current_vitals_.rr > 20) {
                feedback = "Correct action - insulin infusion appropriate";
                return true;
            }
        }
    } else if (definition_.scenario_id == "ANAPHYLAXIS_001") {
        if (action == "epinephrine_im") {
            if (elapsed_time_sec_ < 60) {
                feedback = "Correct action - immediate IM epinephrine is lifesaving";
                return true;
            }
        } else if (action == "airway_management") {
            feedback = "Correct action - prepare airway for potential intubation";
            return true;
        } else if (action == "fluid_bolus") {
            feedback = "Correct action - fluid resuscitation for anaphylaxis";
            return true;
        }
    } else if (definition_.scenario_id == "SEVERE_BLEEDING_001") {
        if (action == "hemorrhage_control") {
            if (current_vitals_.bp_sys < 100) {
                feedback = "Correct action - direct pressure/tourniquet for active bleeding";
                return true;
            }
        } else if (action == "massive_transfusion") {
            if (current_vitals_.bp_sys < 90) {
                feedback = "Correct action - massive transfusion protocol for hemorrhagic shock";
                return true;
            }
        } else if (action == "emergency_surgery") {
            feedback = "Correct action - operating room needed for definitive bleeding control";
            return true;
        }
    }
    
    feedback = "Action recorded - monitoring for clinical effect";
    return true;
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
            "cerebral_hemorrhage"
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
            "fatal_cerebral_edema"
        },
        {
            "hypoglycemia",
            "insulin_too_early",
            "severe_hypoglycemia"
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
            "death_from_blood_loss"
        },
        {
            "disseminated_coagulopathy",
            "delayed_massive_transfusion",
            "irreversible_shock"
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
