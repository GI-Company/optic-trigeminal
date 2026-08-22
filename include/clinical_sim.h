#pragma once

#include <string>
#include <vector>
#include <map>
#include <ctime>

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
    
    // Arrays for history (simple ring buffer via vector)
    std::vector<int> hr_history;
    std::vector<int> spo2_history;
    std::vector<float> temp_history;
    
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

private:
    std::vector<Patient> patients;
    int current_tick;
    int next_patient_id = 1;

    // Helper to generate random vitals based on baseline + drift
    void update_patient_vitals(Patient& p);
};
