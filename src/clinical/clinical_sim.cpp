#include "../include/clinical_sim.h"
#include "../include/ode_physiology.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <limits>

ClinicalSimulator::ClinicalSimulator() : current_tick(0) {
    std::srand(std::time(nullptr));
}

ClinicalSimulator::~ClinicalSimulator() {}

void ClinicalSimulator::initialize(int patient_count) {
    patients.clear();
    next_patient_id = patient_count + 1;

    const std::vector<std::string> names = {
        "Smith, John", "Doe, Jane", "Chen, Wei", "Garcia, Maria", "Johnson, Robert", "Kim, Sarah"
    };
    
    const std::vector<std::string> diagnoses = {
        "Post-Op: Gallbladder", "Pneumonia", "CHF Exacerbation", "Sepsis Watch", "DKA Protocol", "Stroke Eval"
    };
    
    for (int i = 0; i < patient_count; i++) {
        Patient p;
        p.id = i + 1;
        p.name = (i < names.size()) ? names[i] : "Unknown Patient";
        p.mrn = std::to_string(99823400 + i);
        p.room = "412-" + std::string(1, 'A' + i);
        p.admission_diagnosis = (i < diagnoses.size()) ? diagnoses[i] : "Observation";
        p.acuity_score = 3 + (rand() % 5); // 3-7 baseline
        
        // Initial Vitals (Stable)
        p.vitals.hr = 70 + (rand() % 20);
        p.vitals.rr = 14 + (rand() % 6);
        p.vitals.spo2 = 95 + (rand() % 5);
        p.vitals.bp_sys = 110 + (rand() % 30);
        p.vitals.bp_dia = 70 + (rand() % 20);
        p.vitals.temp = 36.5f + (static_cast<float>(rand() % 15) / 10.0f); // 36.5 - 38.0
        p.vitals.is_crisis = false;
        p.vitals.crisis_type = "";
        p.vitals.drift_variance = 0.0;
        
        // History Init -- all identical at boot (real ticks haven't run
        // yet), so every sample gets the same initial timestamp too. That's
        // honest: nothing happened before the sim started.
        std::time_t init_ts = std::time(nullptr);
        for(int j=0; j<20; j++) {
            p.hr_history.push_back(p.vitals.hr);
            p.rr_history.push_back(p.vitals.rr);
            p.spo2_history.push_back(p.vitals.spo2);
            p.temp_history.push_back(p.vitals.temp);
            p.history_timestamps.push_back(init_ts);
        }

        // Seed a single snapshot -- unlike the display-history arrays above,
        // snapshots don't need to start pre-filled (they're only used to
        // branch a fork from a past point, not to render an initial chart),
        // so it just grows naturally from here.
        p.snapshots.push_back(PatientSnapshot{init_ts, p.vitals, p.physiology, p.active_drugs});

        p.nurse_notes = "";

        patients.push_back(p);
    }
}

void ClinicalSimulator::update(int tick_delta_ms) {
    current_tick++;
    float dt_seconds = std::max(0.0f, static_cast<float>(tick_delta_ms) / 1000.0f);

    for (auto& p : patients) {
        update_patient_vitals(p, dt_seconds);
    }
}

void ClinicalSimulator::update_patient_vitals(Patient& p, float dt_seconds) {
    // Crisis and any active drugs modify the underlying physiology
    // parameters; step_physiology then integrates the ODE and derives
    // every observable vital from the resulting shared state -- see
    // include/ode_physiology.h for why this replaced the old
    // independent-per-vital random walk.
    apply_crisis_physiology(p.physiology, p.vitals.crisis_type, dt_seconds);
    apply_drug_effects(p.physiology, p.active_drugs, dt_seconds);
    step_physiology(p.physiology, p.vitals, dt_seconds);

    // Sepsis escalates to Septic Shock once hypotension sets in on top of
    // significant infection burden -- the same real distinction the crisis
    // physiology modifiers above already encode (Sepsis and "Septic Shock"
    // share identical parameter effects; this only changes crisis_type so
    // it's visible/scoreable/chartable as a distinct, more severe state).
    if (p.vitals.crisis_type == "Sepsis" && p.vitals.bp_sys < 90 && p.physiology.infection_burden > 0.6f) {
        p.vitals.crisis_type = "Septic Shock";
    }

    // Update histories -- appended every tick regardless of whether that
    // vital's own value actually changed this tick (matches temp_history's
    // existing behavior, which already appends every tick despite temp only
    // changing every 20th one). Keeps all four arrays + history_timestamps
    // the same length, in lockstep.
    p.hr_history.erase(p.hr_history.begin());
    p.hr_history.push_back(p.vitals.hr);
    p.rr_history.erase(p.rr_history.begin());
    p.rr_history.push_back(p.vitals.rr);
    p.spo2_history.erase(p.spo2_history.begin());
    p.spo2_history.push_back(p.vitals.spo2);
    p.temp_history.erase(p.temp_history.begin());
    p.temp_history.push_back(p.vitals.temp);
    p.history_timestamps.erase(p.history_timestamps.begin());
    p.history_timestamps.push_back(std::time(nullptr));

    // Full resumable snapshot, ring-buffered to the last 120 ticks -- deeper
    // than the 20-sample display history since CCPC fork-scrubbing needs
    // real range to branch from (see include/clinical_sim.h).
    p.snapshots.push_back(PatientSnapshot{std::time(nullptr), p.vitals, p.physiology, p.active_drugs});
    if (p.snapshots.size() > 120) {
        p.snapshots.erase(p.snapshots.begin());
    }

    // Crisis onset: low-probability random trigger per type, same spirit as
    // before (a real patient's crisis doesn't announce itself in advance).
    // The "hybrid" element the plan called for is the Sepsis -> Septic
    // Shock *escalation* above, driven by a real deteriorating condition
    // (sustained hypotension + rising infection burden) rather than by
    // chance -- a qSOFA-style condition triggering a brand-new crisis from
    // a fully healthy baseline isn't meaningful, since a healthy patient's
    // vitals essentially never cross qSOFA's thresholds by random drift
    // alone; qSOFA's real clinical role is escalation/severity signaling
    // for an already-abnormal patient, which is what it's used for here.
    if (!p.vitals.is_crisis) {
        if ((rand() % 1000) == 0) {
            trigger_crisis(p.id, "Respiratory Failure");
        } else if ((rand() % 1000) == 0) {
            trigger_crisis(p.id, "Sepsis");
        } else if ((rand() % 1000) == 0) {
            trigger_crisis(p.id, "Hypovolemic Shock");
        }
    }
}

const std::vector<Patient>& ClinicalSimulator::get_all_patients() const {
    return patients;
}

const Patient* ClinicalSimulator::get_patient(int id) const {
    for (const auto& p : patients) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void ClinicalSimulator::trigger_crisis(int patient_id, std::string type) {
    for (auto& p : patients) {
        if (p.id == patient_id) {
            p.vitals.is_crisis = true;
            p.vitals.crisis_type = type;
            p.acuity_score = 9;
            break;
        }
    }
}

void ClinicalSimulator::reset_patient(int patient_id) {
    for (auto& p : patients) {
        if (p.id == patient_id) {
            p.vitals.is_crisis = false;
            p.vitals.crisis_type = "";
            p.acuity_score = 3;
            p.vitals.hr = 75;
            p.vitals.spo2 = 98;
            // Explicit, instant reset of the hidden physiology state too --
            // without this, a reset patient's *displayed* vitals looked
            // healthy immediately but the underlying InternalPhysiology
            // (circulating_volume, infection_burden, etc.) stayed wherever
            // the crisis had driven it, so the very next tick would pull
            // vitals right back toward the old crisis state.
            reset_physiology_to_baseline(p.physiology);
            p.active_drugs.clear();
            break;
        }
    }
}

int ClinicalSimulator::admit_patient(const std::string& name, const std::string& mrn,
                                      const std::string& room, const std::string& diagnosis,
                                      int acuity_score) {
    Patient p;
    p.id = next_patient_id++;
    p.name = name;
    p.mrn = mrn;
    p.room = room;
    p.admission_diagnosis = diagnosis;
    p.acuity_score = std::max(1, std::min(10, acuity_score));

    // Same baseline-stable-vitals generation as initialize() uses for the
    // seed roster, so a freshly-admitted patient looks like a real patient
    // rather than a blank/zeroed one.
    p.vitals.hr = 70 + (rand() % 20);
    p.vitals.rr = 14 + (rand() % 6);
    p.vitals.spo2 = 95 + (rand() % 5);
    p.vitals.bp_sys = 110 + (rand() % 30);
    p.vitals.bp_dia = 70 + (rand() % 20);
    p.vitals.temp = 36.5f + (static_cast<float>(rand() % 15) / 10.0f);
    p.vitals.is_crisis = false;
    p.vitals.crisis_type = "";
    p.vitals.drift_variance = 0.0;

    std::time_t admit_ts = std::time(nullptr);
    for (int j = 0; j < 20; j++) {
        p.hr_history.push_back(p.vitals.hr);
        p.rr_history.push_back(p.vitals.rr);
        p.spo2_history.push_back(p.vitals.spo2);
        p.temp_history.push_back(p.vitals.temp);
        p.history_timestamps.push_back(admit_ts);
    }
    p.snapshots.push_back(PatientSnapshot{admit_ts, p.vitals, p.physiology, p.active_drugs});

    p.active = true;
    patients.push_back(p);
    return p.id;
}

bool ClinicalSimulator::administer_drug(int patient_id, DrugType type, float dose, float duration_seconds) {
    for (auto& p : patients) {
        if (p.id == patient_id) {
            ActiveDrug drug;
            drug.type = type;
            drug.dose = dose;
            drug.remaining_seconds = duration_seconds;
            p.active_drugs.push_back(drug);
            if (type == DrugType::Oxygen) {
                p.vitals.on_oxygen = true;
            }
            return true;
        }
    }
    return false;
}

std::string ClinicalSimulator::create_fork(int patient_id, std::time_t from_timestamp,
                                            const std::string& intervention_label,
                                            bool has_intervention, DrugType intervention_type,
                                            float intervention_dose, float duration_seconds) {
    const Patient* patient = nullptr;
    for (const auto& p : patients) {
        if (p.id == patient_id) { patient = &p; break; }
    }
    if (!patient) return "";

    // Newest snapshot at or before from_timestamp -- snapshots are stored
    // oldest-first (ring buffer, same as the history arrays), so scan from
    // the end for the first one that qualifies.
    const PatientSnapshot* source = nullptr;
    for (auto it = patient->snapshots.rbegin(); it != patient->snapshots.rend(); ++it) {
        if (it->timestamp <= from_timestamp) { source = &(*it); break; }
    }
    if (!source) return "";

    PatientFork fork;
    fork.fork_id = "fork_" + std::to_string(next_fork_id++);
    fork.source_patient_id = patient_id;
    fork.forked_from_timestamp = source->timestamp;
    fork.created_at = std::time(nullptr);
    fork.intervention_label = intervention_label;
    fork.vitals = source->vitals;
    fork.physiology = source->physiology;
    fork.active_drugs = source->active_drugs;

    if (has_intervention) {
        ActiveDrug drug;
        drug.type = intervention_type;
        drug.dose = intervention_dose;
        drug.remaining_seconds = 1800.0f; // matches handle_action's typical durations
        fork.active_drugs.push_back(drug);
        if (intervention_type == DrugType::Oxygen) {
            fork.vitals.on_oxygen = true;
        }
    }

    // Simulate forward -- the exact same three calls update_patient_vitals
    // makes on the real patient, just against the fork's own cloned state.
    // No new random crisis triggers (see create_fork's doc comment in the
    // header for why): the fork only continues whatever crisis state
    // existed at the source snapshot, plus its own escalation logic.
    float duration = std::clamp(duration_seconds, 0.0f, 3600.0f);
    int ticks = static_cast<int>(duration);
    for (int t = 0; t < ticks; ++t) {
        apply_crisis_physiology(fork.physiology, fork.vitals.crisis_type, 1.0f);
        apply_drug_effects(fork.physiology, fork.active_drugs, 1.0f);
        step_physiology(fork.physiology, fork.vitals, 1.0f);

        if (fork.vitals.crisis_type == "Sepsis" && fork.vitals.bp_sys < 90 &&
            fork.physiology.infection_burden > 0.6f) {
            fork.vitals.crisis_type = "Septic Shock";
        }

        fork.trajectory.push_back(PatientSnapshot{
            fork.forked_from_timestamp + static_cast<std::time_t>(t + 1),
            fork.vitals, fork.physiology, fork.active_drugs});
    }

    // Cap at 10 forks per patient -- evict the oldest by created_at rather
    // than let this grow unbounded (see the header doc comment: the same
    // failure mode as HTTPServer::handle_observations, fixed elsewhere this
    // session).
    std::vector<std::string> this_patient_forks;
    for (const auto& [id, f] : forks) {
        if (f.source_patient_id == patient_id) this_patient_forks.push_back(id);
    }
    if (this_patient_forks.size() >= 10) {
        std::string oldest_id;
        std::time_t oldest_time = std::numeric_limits<std::time_t>::max();
        for (const auto& id : this_patient_forks) {
            std::time_t t = forks.at(id).created_at;
            if (t < oldest_time) { oldest_time = t; oldest_id = id; }
        }
        if (!oldest_id.empty()) forks.erase(oldest_id);
    }

    std::string fork_id = fork.fork_id;
    forks[fork_id] = std::move(fork);
    return fork_id;
}

const PatientFork* ClinicalSimulator::get_fork(const std::string& fork_id) const {
    auto it = forks.find(fork_id);
    return it != forks.end() ? &it->second : nullptr;
}

std::vector<const PatientFork*> ClinicalSimulator::get_forks_for_patient(int patient_id) const {
    std::vector<const PatientFork*> result;
    for (const auto& [id, f] : forks) {
        if (f.source_patient_id == patient_id) result.push_back(&f);
    }
    return result;
}

bool ClinicalSimulator::delete_fork(const std::string& fork_id) {
    return forks.erase(fork_id) > 0;
}

bool ClinicalSimulator::discharge_patient(int patient_id, const std::string& reason) {
    for (auto& p : patients) {
        if (p.id == patient_id) {
            if (!p.active) return false;
            p.active = false;
            p.discharge_reason = reason;
            return true;
        }
    }
    return false;
}
