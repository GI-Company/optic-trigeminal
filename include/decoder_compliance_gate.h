#pragma once

struct DecoderContext {
    bool intent_resolved;
    bool evidence_present;
    bool safety_approved;
    float confidence;
};

class DecoderComplianceGate {
public:
    bool allow_emit(const DecoderContext& ctx) const {
        if (!ctx.intent_resolved) return false;
        if (!ctx.evidence_present) return false;
        if (!ctx.safety_approved) return false;
        if (ctx.confidence < confidence_threshold) return false;
        return true;
    }

    void set_confidence_threshold(float t) {
        confidence_threshold = t;
    }

private:
    float confidence_threshold = 0.75f;
};