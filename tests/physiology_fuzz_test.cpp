// Fuzz/stress test for the ODE physiology engine (include/ode_physiology.h).
// Randomizes InternalPhysiology/Vitals state -- including deliberately
// injected NaN/Inf and out-of-range values -- across many iterations, runs
// the real crisis/drug/step/hard-limits pipeline, and asserts every output
// stays finite and within physiologically-survivable bounds. Real
// confidence in the physiology code added this session, not a corrupted
// copy-paste of an inflated "streak-to-Fatal" fuzzer design.
#include "../include/ode_physiology.h"
#include "../include/clinical_sim.h"
#include <iostream>
#include <random>
#include <cmath>
#include <limits>

namespace {

std::mt19937 rng(12345); // fixed seed -- reproducible failures, not flaky

float randf(float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

int randi(int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

bool chance(float probability) {
    return randf(0.0f, 1.0f) < probability;
}

bool is_bad(float v) {
    return std::isnan(v) || std::isinf(v);
}

InternalPhysiology randomize_physiology(bool extreme) {
    InternalPhysiology s;
    float scale = extreme ? 1.0f : 0.3f;

    s.arterial_pressure = randf(-50.0f, 400.0f);
    s.venous_pressure = randf(-60.0f, 80.0f);
    s.oxygen_debt = randf(-20.0f, 500.0f);
    s.circulating_volume = randf(-1.0f, 3.0f);
    s.systemic_vascular_resistance = randf(-1.0f, 6.0f);
    s.contractility = randf(-1.0f, 4.0f);
    s.oxygenation_efficiency = randf(-1.0f, 2.0f);
    s.infection_burden = randf(-1.0f, 3.0f);
    s.shunt_fraction = randf(-1.0f, 2.0f);
    s.antipyretic_effect = randf(-1.0f, 2.0f);

    // Occasionally inject NaN/Inf directly -- the exact failure mode
    // apply_hard_limits exists to catch.
    if (chance(0.05f * scale)) {
        float bad = chance(0.5f) ? std::numeric_limits<float>::quiet_NaN()
                                  : std::numeric_limits<float>::infinity();
        int which = randi(0, 8);
        switch (which) {
            case 0: s.arterial_pressure = bad; break;
            case 1: s.venous_pressure = bad; break;
            case 2: s.oxygen_debt = bad; break;
            case 3: s.circulating_volume = bad; break;
            case 4: s.systemic_vascular_resistance = bad; break;
            case 5: s.contractility = bad; break;
            case 6: s.oxygenation_efficiency = bad; break;
            case 7: s.infection_burden = bad; break;
            case 8: s.shunt_fraction = bad; break;
        }
    }
    return s;
}

Vitals randomize_vitals() {
    Vitals v{};
    v.hr = randi(-50, 400);
    v.rr = randi(-20, 150);
    v.spo2 = randi(-50, 200);
    v.bp_sys = randi(-100, 500);
    v.bp_dia = randi(-100, 400);
    v.temp = randf(-20.0f, 80.0f);
    v.is_crisis = chance(0.5f);
    v.drift_variance = randf(0.0f, 1.0f);
    v.lactate = randf(-10.0f, 100.0f);
    v.urine_output_ml_hr = randi(-100, 1000);
    v.on_oxygen = chance(0.5f);
    v.consciousness = static_cast<ConsciousnessLevel>(randi(0, 3));

    if (chance(0.05f)) {
        v.temp = chance(0.5f) ? std::numeric_limits<float>::quiet_NaN()
                               : std::numeric_limits<float>::infinity();
    }
    if (chance(0.05f)) {
        v.lactate = chance(0.5f) ? std::numeric_limits<float>::quiet_NaN()
                                  : std::numeric_limits<float>::infinity();
    }
    return v;
}

const char* kCrisisTypes[] = {"", "Respiratory Failure", "Sepsis", "Septic Shock", "Hypovolemic Shock"};

} // namespace

int main() {
    const int kIterations = 20000;
    int nan_inf_injected = 0;
    int nan_inf_survived_hard_limits = 0;
    int out_of_bounds_survived = 0;

    for (int i = 0; i < kIterations; ++i) {
        bool extreme = chance(0.7f);
        InternalPhysiology phys = randomize_physiology(extreme);
        Vitals v = randomize_vitals();

        bool had_bad_input = false;
        for (float x : {phys.arterial_pressure, phys.venous_pressure, phys.oxygen_debt,
                         phys.circulating_volume, phys.systemic_vascular_resistance,
                         phys.contractility, phys.oxygenation_efficiency,
                         phys.infection_burden, phys.shunt_fraction}) {
            if (is_bad(x)) had_bad_input = true;
        }
        if (is_bad(v.temp) || is_bad(v.lactate)) had_bad_input = true;
        if (had_bad_input) nan_inf_injected++;

        std::vector<ActiveDrug> drugs;
        int n_drugs = randi(0, 3);
        for (int d = 0; d < n_drugs; ++d) {
            ActiveDrug drug;
            drug.type = static_cast<DrugType>(randi(0, 4));
            drug.dose = randf(-2.0f, 5.0f);
            drug.remaining_seconds = randf(-10.0f, 3600.0f);
            drugs.push_back(drug);
        }

        std::string crisis = kCrisisTypes[randi(0, 4)];
        float dt = randf(0.0f, 30.0f);

        // Exercise the exact pipeline ClinicalSimulator::update_patient_vitals
        // runs every tick.
        apply_crisis_physiology(phys, crisis, dt);
        apply_drug_effects(phys, drugs, dt);
        step_physiology(phys, v, dt);

        // step_physiology already calls apply_hard_limits internally; call
        // it again explicitly here too, matching how it'd be used as a
        // defensive check on its own (e.g. right after admit/reset).
        apply_hard_limits(phys, v);

        bool still_bad = false;
        for (float x : {phys.arterial_pressure, phys.venous_pressure, phys.oxygen_debt,
                         phys.circulating_volume, phys.systemic_vascular_resistance,
                         phys.contractility, phys.oxygenation_efficiency,
                         phys.infection_burden, phys.shunt_fraction}) {
            if (is_bad(x)) still_bad = true;
        }
        if (is_bad(v.temp) || is_bad(v.lactate)) still_bad = true;
        if (still_bad) {
            nan_inf_survived_hard_limits++;
            std::cerr << "FAIL: NaN/Inf survived apply_hard_limits at iteration " << i << "\n";
        }

        bool out_of_bounds =
            v.hr < 20 || v.hr > 220 || v.rr < 0 || v.rr > 60 ||
            v.spo2 < 40 || v.spo2 > 100 || v.bp_sys < 20 || v.bp_sys > 260 ||
            v.bp_dia < 10 || v.bp_dia > 160 || v.temp < 28.0f || v.temp > 43.0f ||
            v.lactate < 0.3f || v.lactate > 20.0f ||
            v.urine_output_ml_hr < 0 || v.urine_output_ml_hr > 200 ||
            phys.circulating_volume < 0.2f || phys.circulating_volume > 1.5f ||
            phys.systemic_vascular_resistance < 0.15f || phys.systemic_vascular_resistance > 3.0f ||
            phys.oxygenation_efficiency < 0.2f || phys.oxygenation_efficiency > 1.0f ||
            phys.infection_burden < 0.0f || phys.infection_burden > 1.0f;
        if (out_of_bounds) {
            out_of_bounds_survived++;
            std::cerr << "FAIL: out-of-bounds value survived apply_hard_limits at iteration " << i << "\n";
        }
    }

    std::cout << "Physiology fuzz test: " << kIterations << " iterations\n";
    std::cout << "  Inputs with injected NaN/Inf: " << nan_inf_injected << "\n";
    std::cout << "  NaN/Inf that survived apply_hard_limits: " << nan_inf_survived_hard_limits << "\n";
    std::cout << "  Out-of-bounds values that survived: " << out_of_bounds_survived << "\n";

    bool passed = (nan_inf_survived_hard_limits == 0) && (out_of_bounds_survived == 0);
    std::cout << (passed ? "PASSED" : "FAILED") << "\n";
    return passed ? 0 : 1;
}
