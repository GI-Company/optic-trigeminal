#include "../include/ode_physiology.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace {

struct Derivatives {
    float d_arterial_pressure;
    float d_venous_pressure;
    float d_oxygen_debt;
};

// Bounded, analytically-computed target pressure -- Ohm's-law-style
// P ~ Flow x Resistance, Flow ~ Volume x Contractility, every factor
// centered at 1.0 baseline so the target is exactly 120 mmHg when every
// parameter is at baseline. arterial_pressure relaxes toward this target
// via a first-order ODE (RK4-integrated below, so it's a smooth transient,
// not an instant jump) -- deliberately not derived from an emergent
// flow-in/flow-out balance, which was tried first and found to be
// genuinely unstable (unbounded divergence under sustained low-SVR
// conditions, venous pressure going negative). The clamp to [30, 220] is
// a second safety net (compounding parameter effects -- e.g. a vasopressor
// and fluids both pushed to their individual maximums at once -- can still
// multiply past a sane range even though each factor alone is bounded);
// 220 also happens to land exactly on NEWS2's own critical-high threshold.
float target_pressure(const InternalPhysiology& s) {
    return std::clamp(120.0f * s.circulating_volume * s.systemic_vascular_resistance * s.contractility,
                       30.0f, 220.0f);
}

float target_venous_pressure(const InternalPhysiology& s) {
    return 8.0f * s.circulating_volume / std::max(s.systemic_vascular_resistance, 0.3f);
}

Derivatives physiology_derivatives(const InternalPhysiology& s) {
    Derivatives d;
    d.d_arterial_pressure = (target_pressure(s) - s.arterial_pressure) * 0.08f;
    d.d_venous_pressure = (target_venous_pressure(s) - s.venous_pressure) * 0.10f;

    // Perfusion proxy (current pressure relative to baseline) x oxygenation
    // efficiency drives O2 delivery; infection raises demand. Debt only
    // grows when demand exceeds delivery and slowly clears once delivery
    // catches back up (floored at 0 after each integration step).
    float o2_delivery = (s.arterial_pressure / 120.0f) * s.oxygenation_efficiency;
    float o2_demand = 1.0f * (1.0f + s.infection_burden * 0.8f);
    d.d_oxygen_debt = (o2_demand - o2_delivery) * 0.15f;

    return d;
}

InternalPhysiology add_scaled(const InternalPhysiology& base, const Derivatives& d, float scale) {
    InternalPhysiology out = base;
    out.arterial_pressure += d.d_arterial_pressure * scale;
    out.venous_pressure += d.d_venous_pressure * scale;
    out.oxygen_debt += d.d_oxygen_debt * scale;
    return out;
}

// Baroreceptor-style response: pressure below the 120 baseline drives
// compensatory tachycardia; infection and poor oxygenation each add their
// own independent tachycardic drive on top.
float heart_rate_from_state(const InternalPhysiology& s) {
    float pressure_error = 120.0f - s.arterial_pressure;
    float sympathetic = std::clamp(pressure_error * 0.35f, -20.0f, 55.0f);
    float hr = 75.0f + sympathetic + s.infection_burden * 25.0f + (1.0f - s.oxygenation_efficiency) * 20.0f;
    return std::clamp(hr, 35.0f, 190.0f);
}

void relax(float& value, float target, float rate, float dt) {
    value += (target - value) * std::clamp(rate * dt, 0.0f, 1.0f);
}

float noise(float span) {
    return ((static_cast<float>(rand() % 1000) / 1000.0f) - 0.5f) * span;
}

} // namespace

void step_physiology(InternalPhysiology& phys, Vitals& v, float dt_seconds) {
    // Standing relaxation toward baseline -- makes crisis/drug parameter
    // pulls self-correcting once they stop being applied, instead of a
    // patient staying pathological forever after a resolved crisis.
    relax(phys.circulating_volume, 1.0f, 0.02f, dt_seconds);
    relax(phys.systemic_vascular_resistance, 1.0f, 0.02f, dt_seconds);
    relax(phys.contractility, 1.0f, 0.02f, dt_seconds);
    relax(phys.oxygenation_efficiency, 1.0f, 0.02f, dt_seconds);
    relax(phys.infection_burden, 0.0f, 0.005f, dt_seconds);
    relax(phys.shunt_fraction, 0.0f, 0.02f, dt_seconds);
    relax(phys.antipyretic_effect, 0.0f, 0.01f, dt_seconds);

    // RK4, sub-stepped for stability at real tick sizes (this simulator's
    // ticks are driven by HTTP polling, often several seconds apart).
    const int kSubsteps = 8;
    float h = dt_seconds / static_cast<float>(kSubsteps);
    for (int i = 0; i < kSubsteps; ++i) {
        Derivatives k1 = physiology_derivatives(phys);
        InternalPhysiology s2 = add_scaled(phys, k1, h * 0.5f);
        Derivatives k2 = physiology_derivatives(s2);
        InternalPhysiology s3 = add_scaled(phys, k2, h * 0.5f);
        Derivatives k3 = physiology_derivatives(s3);
        InternalPhysiology s4 = add_scaled(phys, k3, h);
        Derivatives k4 = physiology_derivatives(s4);

        phys.arterial_pressure += (h / 6.0f) * (k1.d_arterial_pressure + 2.0f * k2.d_arterial_pressure +
                                                  2.0f * k3.d_arterial_pressure + k4.d_arterial_pressure);
        phys.venous_pressure += (h / 6.0f) * (k1.d_venous_pressure + 2.0f * k2.d_venous_pressure +
                                                2.0f * k3.d_venous_pressure + k4.d_venous_pressure);
        phys.oxygen_debt += (h / 6.0f) * (k1.d_oxygen_debt + 2.0f * k2.d_oxygen_debt +
                                            2.0f * k3.d_oxygen_debt + k4.d_oxygen_debt);
        phys.oxygen_debt = std::max(0.0f, phys.oxygen_debt);
    }

    // Map internal state -> observable vitals, with small measurement
    // noise so it still feels "alive" the way the old random walk did --
    // every value is now a consequence of the shared underlying state,
    // not independently randomized per-vital.
    v.bp_sys = static_cast<int>(std::round(phys.arterial_pressure + noise(3.0f)));
    v.bp_dia = static_cast<int>(std::round(phys.arterial_pressure * 0.65f + noise(3.0f)));
    v.hr = static_cast<int>(std::round(heart_rate_from_state(phys) + noise(2.0f)));
    v.spo2 = static_cast<int>(std::round(std::clamp(
        98.0f * phys.oxygenation_efficiency - phys.shunt_fraction * 15.0f + noise(1.0f), 50.0f, 100.0f)));
    v.rr = static_cast<int>(std::round(std::clamp(
        16.0f + (1.0f - phys.oxygenation_efficiency) * 26.0f + phys.infection_burden * 6.0f + noise(1.0f),
        6.0f, 45.0f)));

    // Temperature changes are physiologically slow -- eases toward a
    // target instead of jumping, unlike the other vitals above.
    float target_temp = 37.0f + phys.infection_burden * 2.3f - (1.0f - phys.circulating_volume) * 0.3f
                       - phys.antipyretic_effect * 1.5f;
    v.temp += (target_temp - v.temp) * std::clamp(0.01f * dt_seconds, 0.0f, 1.0f);

    v.lactate = std::clamp(1.0f + phys.oxygen_debt * 0.35f, 0.3f, 20.0f);
    v.urine_output_ml_hr = static_cast<int>(std::clamp(
        (phys.arterial_pressure - 60.0f) * 0.9f - phys.infection_burden * 20.0f, 0.0f, 130.0f));

    apply_hard_limits(phys, v);
}

void apply_crisis_physiology(InternalPhysiology& phys, const std::string& crisis_type, float dt_seconds) {
    if (crisis_type == "Sepsis" || crisis_type == "Septic Shock") {
        phys.infection_burden = std::min(1.0f, phys.infection_burden + 0.015f * dt_seconds);
        phys.systemic_vascular_resistance = std::max(0.3f, phys.systemic_vascular_resistance - 0.01f * dt_seconds);
        phys.circulating_volume = std::max(0.5f, phys.circulating_volume - 0.004f * dt_seconds);
        phys.oxygenation_efficiency = std::max(0.6f, phys.oxygenation_efficiency - 0.003f * dt_seconds);
    } else if (crisis_type == "Hypovolemic Shock") {
        // Distinguishing signature vs. sepsis: volume drops WITHOUT
        // infection_burden rising, and SVR rises (compensatory
        // vasoconstriction) rather than falling (septic vasodilation) --
        // "cold shock" vs. "warm shock," a genuinely different pattern the
        // auto-suggested explanation / vital-history correlation feature
        // (earlier this session) can actually distinguish.
        phys.circulating_volume = std::max(0.35f, phys.circulating_volume - 0.012f * dt_seconds);
        phys.systemic_vascular_resistance = std::min(2.2f, phys.systemic_vascular_resistance + 0.006f * dt_seconds);
    } else if (crisis_type == "Respiratory Failure") {
        phys.oxygenation_efficiency = std::max(0.35f, phys.oxygenation_efficiency - 0.01f * dt_seconds);
        phys.shunt_fraction = std::min(0.5f, phys.shunt_fraction + 0.008f * dt_seconds);
    }
}

void apply_drug_effects(InternalPhysiology& phys, std::vector<ActiveDrug>& active_drugs, float dt_seconds) {
    for (auto& drug : active_drugs) {
        switch (drug.type) {
            case DrugType::Crystalloid:
                // Was reaching its cap in ~20s at a typical dose -- too fast
                // for a bolus that in reality takes minutes to redistribute
                // and show hemodynamic effect. Slowed ~4x (full effect now
                // ~75s at dose 1.0).
                phys.circulating_volume = std::min(1.3f, phys.circulating_volume + 0.004f * drug.dose * dt_seconds);
                break;
            case DrugType::Oxygen:
                phys.oxygenation_efficiency = std::min(1.0f, phys.oxygenation_efficiency + 0.02f * drug.dose * dt_seconds);
                break;
            case DrugType::Norepinephrine:
                // Ceiling lowered from 2.5/1.8 to 1.7/1.4 -- the higher
                // ceiling represented a maximal/dangerous dose with no
                // titration logic to stop short of it (this model is
                // open-loop: dose stays constant until the drug wears off,
                // unlike real practice where a clinician titrates to a
                // target MAP), so a continuously-applied dose would ride the
                // effect all the way to that ceiling and overshoot into
                // hypertension. 1.7/1.4 represents a typical, non-maximal
                // clinical dose instead.
                phys.systemic_vascular_resistance = std::min(1.7f, phys.systemic_vascular_resistance + 0.02f * drug.dose * dt_seconds);
                phys.contractility = std::min(1.4f, phys.contractility + 0.01f * drug.dose * dt_seconds);
                break;
            case DrugType::BroadSpectrumAntibiotic:
                phys.infection_burden = std::max(0.0f, phys.infection_burden - 0.006f * drug.dose * dt_seconds);
                break;
            case DrugType::Antipyretic:
                phys.antipyretic_effect = std::min(1.0f, phys.antipyretic_effect + 0.08f * drug.dose * dt_seconds);
                break;
        }
        drug.remaining_seconds -= dt_seconds;
    }
    active_drugs.erase(
        std::remove_if(active_drugs.begin(), active_drugs.end(),
                        [](const ActiveDrug& d) { return d.remaining_seconds <= 0.0f; }),
        active_drugs.end());
}

void reset_physiology_to_baseline(InternalPhysiology& phys) {
    phys = InternalPhysiology{};
}

void apply_hard_limits(InternalPhysiology& phys, Vitals& v) {
    auto safe = [](float& x, float fallback, float lo, float hi) {
        if (std::isnan(x) || std::isinf(x)) x = fallback;
        x = std::clamp(x, lo, hi);
    };

    safe(phys.arterial_pressure, 120.0f, 20.0f, 260.0f);
    safe(phys.venous_pressure, 8.0f, -5.0f, 30.0f);
    safe(phys.oxygen_debt, 0.0f, 0.0f, 200.0f);
    safe(phys.circulating_volume, 1.0f, 0.2f, 1.5f);
    safe(phys.systemic_vascular_resistance, 1.0f, 0.15f, 3.0f);
    safe(phys.contractility, 1.0f, 0.15f, 2.0f);
    safe(phys.oxygenation_efficiency, 1.0f, 0.2f, 1.0f);
    safe(phys.infection_burden, 0.0f, 0.0f, 1.0f);
    safe(phys.shunt_fraction, 0.0f, 0.0f, 0.6f);
    safe(phys.antipyretic_effect, 0.0f, 0.0f, 1.0f);

    if (std::isnan(v.temp) || std::isinf(v.temp)) v.temp = 37.0f;
    if (std::isnan(v.lactate) || std::isinf(v.lactate)) v.lactate = 1.0f;

    v.hr = std::clamp(v.hr, 20, 220);
    v.rr = std::clamp(v.rr, 0, 60);
    v.spo2 = std::clamp(v.spo2, 40, 100);
    v.bp_sys = std::clamp(v.bp_sys, 20, 260);
    v.bp_dia = std::clamp(v.bp_dia, 10, 160);
    v.temp = std::clamp(v.temp, 28.0f, 43.0f);
    v.lactate = std::clamp(v.lactate, 0.3f, 20.0f);
    v.urine_output_ml_hr = std::clamp(v.urine_output_ml_hr, 0, 200);
}
