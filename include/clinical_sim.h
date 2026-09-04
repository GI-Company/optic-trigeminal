#pragma once

#include <string>
#include <vector>
#include <map>
#include <ctime>

// AVPU consciousness scale -- the level-of-consciousness input NEWS2/qSOFA/
// MEWS all score (Alert vs. any degree of altered mentation). Real patients
// in this simulator stay Alert except during severe crises.
enum class ConsciousnessLevel {
    Alert = 0,
    Voice = 1,
    Pain = 2,
    Unresponsive = 3
};

// Patient Vital Signs Structure
struct Vitals {
    int hr;
    int rr;
    int spo2;
    int bp_sys;
    int bp_dia;
    float temp; // Temperature in Celsius
    bool is_crisis;
    std::string crisis_type; // "Respiratory Failure", "Hypotension", "Hyperthermia", etc.
    double drift_variance; // 0.0 to 1.0

    // Added for standard clinical scoring (NEWS2/qSOFA/partial-SOFA/MEWS) --
    // see include/clinical_scoring.h. lactate and urine_output_ml_hr are
    // vitals-adjacent bedside measurements this simulator can plausibly
    // produce; true SOFA also needs platelets/bilirubin/creatinine, which
    // would need a lab-value model this simulator doesn't have -- see
    // clinical_scoring.h's partial-SOFA for why those are deliberately
    // omitted rather than fabricated.
    float lactate = 1.0f;               // mmol/L, normal ~0.5-2.0
    int urine_output_ml_hr = 50;
    bool on_oxygen = false;              // supplemental O2 in use (NEWS2 oxygen_score)
    ConsciousnessLevel consciousness = ConsciousnessLevel::Alert;
};

// Hidden internal physiological state that drives the observable Vitals
// above, integrated each tick by a small ODE (see include/ode_physiology.h).
// Replaces the old independent-per-vital random walk: vitals are now a
// *consequence* of this state, not randomized directly, so crisis/drug
// effects that change one thing (e.g. circulating_volume) show up as a
// causally-consistent pattern across multiple vitals instead of unrelated
// coincidental jiggling.
struct InternalPhysiology {
    // Continuous ODE state.
    float arterial_pressure = 120.0f;  // approximates systolic BP, mmHg
    float venous_pressure = 8.0f;      // mmHg -- drives flow balance, not displayed directly
    float oxygen_debt = 0.0f;          // accumulated deficit; drives lactate

    // Parameters. Crisis/drug effects modify these each tick; otherwise a
    // standing per-tick pull relaxes them toward baseline (1.0 for the
    // multiplicative factors, 0.0 for the burden/fraction terms), so
    // recovery after a crisis resolves or a drug wears off is automatic
    // rather than needing a special-cased "recovery" code path.
    float circulating_volume = 1.0f;
    float systemic_vascular_resistance = 1.0f;
    float contractility = 1.0f;
    float oxygenation_efficiency = 1.0f;
    float infection_burden = 0.0f;
    float shunt_fraction = 0.0f;
    // Kussmaul breathing: DKA's compensatory tachypnea for metabolic
    // acidosis is a genuinely different RR-driving mechanism than
    // oxygenation_efficiency/infection_burden below -- SpO2 stays normal in
    // pure DKA (it isn't a hypoxia-driven process), so folding it into
    // oxygenation_efficiency would have wrongly dropped SpO2 too. See its
    // own term in step_physiology's rr formula.
    float metabolic_acidosis = 0.0f;

    // Transient antipyretic effect (0-1): reduces the fever *target*
    // step_physiology eases temperature toward, without touching
    // infection_burden itself -- matches real pharmacology (fever comes
    // down, the underlying infection doesn't). Relaxes back to 0 as the
    // drug wears off, same standing-relaxation pattern as the parameters
    // above.
    float antipyretic_effect = 0.0f;
};

enum class DrugType {
    Crystalloid,           // IV fluid bolus -- volume up
    Oxygen,                 // supplemental O2 -- oxygenation up
    Norepinephrine,          // vasopressor -- SVR/contractility up
    BroadSpectrumAntibiotic, // infection_burden down, over time
    Antipyretic,              // fever-reduction, temp target down
    Epinephrine,              // alpha+beta agonist -- SVR up (vasoconstriction) AND
                               // oxygenation up / shunt down (bronchodilation) at once,
                               // faster-acting and shorter-duration than Norepinephrine/
                               // Oxygen alone -- see apply_drug_effects in
                               // ode_physiology.cpp
    Insulin                   // stops ketogenesis -- metabolic_acidosis down, over time.
                               // Doesn't touch circulating_volume; DKA's own volume
                               // depletion is addressed by Crystalloid/iv_fluids
                               // separately, matching real practice (fluids first,
                               // insulin second).
};

// A timed, decaying intervention effect -- administered via
// HTTPServer::handle_action, applied/decayed each tick by
// apply_drug_effects() in ode_physiology.cpp.
struct ActiveDrug {
    DrugType type;
    float dose = 1.0f;             // normalized 0-2ish, 1.0 = typical dose
    float remaining_seconds = 0.0f;
};

// A full, resumable snapshot of a patient's simulatable state at one
// instant -- everything step_physiology/apply_crisis_physiology/
// apply_drug_effects need to keep going from here, not just the display
// value the hr_history/etc. arrays capture. This is what a counterfactual
// fork (see PatientFork below) actually branches from.
struct PatientSnapshot {
    std::time_t timestamp;
    Vitals vitals;
    InternalPhysiology physiology;
    std::vector<ActiveDrug> active_drugs;
};

// A counterfactual branch: "if intervention X had been applied at time T
// instead of what actually happened, here's how the patient's physiology
// would have evolved." Stored separately from Patient -- creating or
// stepping a fork never mutates the real patient's state or history.
struct PatientFork {
    std::string fork_id;
    int source_patient_id;
    std::time_t forked_from_timestamp;   // which snapshot this branched from
    std::time_t created_at;
    std::string intervention_label;       // e.g. "Started norepinephrine", "No action"
    Vitals vitals;                        // fork's current (end-of-trajectory) state
    InternalPhysiology physiology;
    std::vector<ActiveDrug> active_drugs;
    // Same shape as Patient::snapshots -- one entry per simulated tick this
    // fork was stepped forward, so the (future) frontend can render a fork
    // exactly like the real observed-vitals history, just as an overlay.
    std::vector<PatientSnapshot> trajectory;
};

// Patient Data Structure
struct Patient {
    int id;
    std::string name;
    std::string mrn;
    std::string room;
    std::string admission_diagnosis;
    int acuity_score; // 1-10
    Vitals vitals;
    
    // Arrays for history (simple ring buffer via vector). All four arrays
    // (plus history_timestamps) are appended to in lockstep, once per
    // update_patient_vitals() call, so they always stay the same length --
    // history_timestamps[i] is when hr_history[i]/rr_history[i]/etc. were
    // sampled.
    std::vector<int> hr_history;
    std::vector<int> rr_history;
    std::vector<int> spo2_history;
    std::vector<float> temp_history;
    std::vector<std::time_t> history_timestamps;

    // Full resumable state snapshots, one per tick, ring-buffered to the
    // last 120 (deeper than the 20-sample display history above -- CCPC
    // scrubbing/forking needs real range to branch from). See
    // PatientSnapshot.
    std::vector<PatientSnapshot> snapshots;

    // Hidden physiological state driving vitals (see InternalPhysiology
    // above), and any currently-active interventions modifying it.
    InternalPhysiology physiology;
    std::vector<ActiveDrug> active_drugs;

    // Nurse Notes
    std::string nurse_notes;

    // Soft-delete flag for discharge: real chart/audit history should stay
    // attached to the patient ID rather than disappearing when they leave
    // the unit, so discharge marks a patient inactive instead of erasing
    // them from `patients`.
    bool active = true;
    std::string discharge_reason;
};

class ClinicalSimulator {
public:
    ClinicalSimulator();
    ~ClinicalSimulator();

    // Initialize with n patients
    void initialize(int patient_count);

    // Update simulation state for all patients (call this on server tick or status request)
    void update(int tick_delta_ms);

    // Get all patients
    const std::vector<Patient>& get_all_patients() const;

    // Get specific patient by ID (returns nullptr if not found)
    const Patient* get_patient(int id) const;
    
    // Trigger a crisis on a specific patient manually
    void trigger_crisis(int patient_id, std::string type);

    // Reset specific patient
    void reset_patient(int patient_id);

    // Admit a new patient onto the unit with baseline stable vitals.
    // Returns the newly-assigned patient ID.
    int admit_patient(const std::string& name, const std::string& mrn,
                       const std::string& room, const std::string& diagnosis,
                       int acuity_score);

    // Soft-discharge: marks the patient inactive (still in `patients` for
    // chart/audit history) rather than erasing them. Returns false if the
    // patient doesn't exist or is already discharged.
    bool discharge_patient(int patient_id, const std::string& reason);

    // Starts a timed drug effect on a patient (applied/decayed each tick by
    // apply_drug_effects, see include/ode_physiology.h) -- called from
    // HTTPServer::handle_action so nurse actions in the dashboard flow have
    // real mechanical consequences on vitals, not just a logged note.
    // Administering Oxygen also sets vitals.on_oxygen immediately (stays
    // true until a future discharge/reset -- not itself timed, matching how
    // real supplemental O2 runs until a clinician orders it stopped, not on
    // a fixed timer). Returns false if the patient doesn't exist.
    bool administer_drug(int patient_id, DrugType type, float dose, float duration_seconds);

    // Counterfactual forks (CCPC foundation) -- see PatientFork above.
    //
    // Clones the patient's newest snapshot at or before from_timestamp,
    // optionally applies one intervention, and simulates forward for
    // duration_seconds of *simulated* time (computed synchronously in this
    // call, 1-second ticks; duration_seconds is clamped to [0, 3600] to
    // bound the cost of one request). Returns the new fork's id, or "" if
    // the patient doesn't exist or has no snapshot at/before from_timestamp.
    //
    // A fork continues whatever crisis state existed at the source
    // snapshot (including the existing Sepsis -> Septic Shock escalation)
    // but does NOT roll new random crisis triggers -- injecting fresh
    // random bad luck into a projection would defeat the point of isolating
    // the chosen intervention's effect. That makes a fork's physiology
    // trajectory deterministic given its inputs, aside from the small
    // cosmetic measurement noise step_physiology already adds to displayed
    // vitals.
    std::string create_fork(int patient_id, std::time_t from_timestamp,
                             const std::string& intervention_label,
                             bool has_intervention, DrugType intervention_type,
                             float intervention_dose, float duration_seconds);

    const PatientFork* get_fork(const std::string& fork_id) const;
    std::vector<const PatientFork*> get_forks_for_patient(int patient_id) const;
    bool delete_fork(const std::string& fork_id);

private:
    std::vector<Patient> patients;
    int current_tick;
    int next_patient_id = 1;
    int next_fork_id = 1;

    // Capped at 10 per patient (oldest evicted first) -- a deliberate
    // safeguard against unbounded growth, the same failure mode just found
    // and fixed elsewhere this session (HTTPServer::handle_observations
    // growing the knowledge graph without bound on every call).
    std::map<std::string, PatientFork> forks;

    // Advances one patient's physiology (see include/ode_physiology.h) by
    // dt_seconds and derives the observable vitals from it.
    void update_patient_vitals(Patient& p, float dt_seconds);
};
