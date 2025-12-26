#include "../include/clinical_sim.h"
#include <cstdlib>
#include <cmath>
#include <iostream>

ClinicalSimulator::ClinicalSimulator() : current_tick(0) {
    std::srand(std::time(nullptr));
}

ClinicalSimulator::~ClinicalSimulator() {}

void ClinicalSimulator::initialize(int patient_count) {
    patients.clear();
    
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
        
        // History Init
        for(int j=0; j<20; j++) {
            p.hr_history.push_back(p.vitals.hr);
            p.spo2_history.push_back(p.vitals.spo2);
            p.temp_history.push_back(p.vitals.temp);
        }
        
        p.nurse_notes = "";
        
        patients.push_back(p);
    }
}

void ClinicalSimulator::update(int tick_delta_ms) {
    current_tick++;
    
    for (auto& p : patients) {
        update_patient_vitals(p);
    }
}

void ClinicalSimulator::update_patient_vitals(Patient& p) {
    // Determine target based on state
    int target_hr = 75;
    int target_rr = 16;
    int target_spo2 = 98;
    int target_sys = 120;
    float target_temp = 37.0f;
    
    if (p.vitals.is_crisis) {
        // Crisis targets
        if (p.vitals.crisis_type == "Respiratory Failure") {
             target_hr = 115;
             target_rr = 32;
             target_spo2 = 82;
             target_sys = 150;
        } else if (p.vitals.crisis_type == "Sepsis") {
             target_hr = 120;
             target_rr = 24;
             target_spo2 = 92;
             target_sys = 85; // Hypotension
        }
    }
    
    // Random walk towards target
    // HR
    int hr_drift = (rand() % 5) - 2;
    if (p.vitals.hr < target_hr) hr_drift += 1;
    if (p.vitals.hr > target_hr) hr_drift -= 1;
    p.vitals.hr += hr_drift;
    
    // SpO2
    int spo2_drift = (rand() % 3) - 1;
    if (p.vitals.spo2 < target_spo2) spo2_drift += 1;
    if (p.vitals.spo2 > target_spo2) spo2_drift -= 1;
    p.vitals.spo2 += spo2_drift;
    if (p.vitals.spo2 > 100) p.vitals.spo2 = 100;
    
    // RR
    if (current_tick % 5 == 0) { // Slower update for RR
        int rr_drift = (rand() % 3) - 1;
        if (p.vitals.rr < target_rr) rr_drift += 1;
        if (p.vitals.rr > target_rr) rr_drift -= 1;
        p.vitals.rr += rr_drift;
    }
    
    // BP
    if (current_tick % 10 == 0) {
        int sys_drift = (rand() % 5) - 2;
        if (p.vitals.bp_sys < target_sys) sys_drift += 1;
        if (p.vitals.bp_sys > target_sys) sys_drift -= 1;
        p.vitals.bp_sys += sys_drift;
        p.vitals.bp_dia = p.vitals.bp_sys * 0.6 + ((rand()%10)-5);
    }
    
    // Temp
    if (current_tick % 20 == 0) {
        float temp_drift = (static_cast<float>(rand() % 3) - 1.0f) / 10.0f; // -0.1 to 0.1
        if (p.vitals.temp < target_temp) temp_drift += 0.05f;
        if (p.vitals.temp > target_temp) temp_drift -= 0.05f;
        p.vitals.temp += temp_drift;
    }
    
    // Update histories
    p.hr_history.erase(p.hr_history.begin());
    p.hr_history.push_back(p.vitals.hr);
    p.spo2_history.erase(p.spo2_history.begin());
    p.spo2_history.push_back(p.vitals.spo2);
    p.temp_history.erase(p.temp_history.begin());
    p.temp_history.push_back(p.vitals.temp);
    
    // Random Crisis Trigger (very rare)
    if (!p.vitals.is_crisis && (rand() % 1000) == 0) {
        trigger_crisis(p.id, "Respiratory Failure");
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
            break;
        }
    }
}
