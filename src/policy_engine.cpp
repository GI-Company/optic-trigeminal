#include "policy_engine.h"
#include <iostream>
#include <algorithm>

PolicyEngine::PolicyEngine(const std::string& initial_policy_blob)
    : locked(false), policy_blob(initial_policy_blob), policy_hash(0) {
    if (policy_blob.empty()) {
        add_default_rules();
    } else {
        parse_policy_blob(policy_blob);
    }
    update_policy_hash();
}

void PolicyEngine::add_rule(const PolicyRule& r) {
    if (locked) {
        throw std::runtime_error("PolicyEngine is locked: cannot add rules.");
    }
    rules.push_back(r);
    update_policy_hash();
}

void PolicyEngine::add_default_rules() {
    rules.push_back({"rule_safety_check_high_conf", "confidence > 0.85", PolicyEffect::ALLOW});
    rules.push_back({"rule_safety_check_medium_conf", "confidence >= 0.7 && confidence <= 0.85", PolicyEffect::ALLOW});
    rules.push_back({"rule_safety_check_low_conf", "confidence < 0.7", PolicyEffect::DENY});
    rules.push_back({"rule_block_unsafe_content", "is_unsafe_content", PolicyEffect::DENY});
    rules.push_back({"rule_allow_identity_query", "intent_type == identity_query", PolicyEffect::ALLOW});
    rules.push_back({"rule_allow_knowledge_retrieval", "intent_type == knowledge_retrieval", PolicyEffect::ALLOW});
    rules.push_back({"rule_deny_malformed_intent", "intent_type == malformed", PolicyEffect::DENY});
    update_policy_hash();
}

bool PolicyEngine::evaluate(const std::string& condition) const {
    if (rules.empty()) {
        return true;
    }
    
    for (const auto& rule : rules) {
        if (rule.condition == condition) {
            if (rule.effect == PolicyEffect::ALLOW) {
                return true;
            } else if (rule.effect == PolicyEffect::DENY) {
                return false;
            }
        }
    }
    
    for (const auto& rule : rules) {
        if (matches_condition(condition, rule.condition)) {
            if (rule.effect == PolicyEffect::ALLOW) {
                return true;
            } else if (rule.effect == PolicyEffect::DENY) {
                return false;
            }
        }
    }
    
    return false;
}

bool PolicyEngine::evaluate_constraint(const std::string& constraint_name, 
                                       const std::map<std::string, std::string>& context) const {
    if (constraint_name.find("confidence") != std::string::npos) {
        auto it = context.find("confidence");
        if (it != context.end()) {
            try {
                float conf = std::stof(it->second);
                if (constraint_name.find("> 0.85") != std::string::npos) return conf > 0.85f;
                if (constraint_name.find(">= 0.7") != std::string::npos) return conf >= 0.7f;
                if (constraint_name.find("< 0.7") != std::string::npos) return conf < 0.7f;
            } catch (...) {}
        }
    }
    
    if (constraint_name.find("intent_type") != std::string::npos) {
        auto it = context.find("intent_type");
        if (it != context.end()) {
            if (constraint_name.find("identity_query") != std::string::npos) {
                return it->second == "identity_query";
            }
            if (constraint_name.find("knowledge_retrieval") != std::string::npos) {
                return it->second == "knowledge_retrieval";
            }
        }
    }
    
    return false;
}

std::vector<std::string> PolicyEngine::generate_action_sequence(
    const std::string& goal,
    const std::map<std::string, std::string>& context) const {
    
    std::vector<std::string> actions;
    
    auto intent_it = context.find("intent_type");
    if (intent_it != context.end()) {
        actions.push_back("validate_intent:" + intent_it->second);
    }
    
    auto confidence_it = context.find("confidence");
    if (confidence_it != context.end()) {
        try {
            float conf = std::stof(confidence_it->second);
            actions.push_back("validate_confidence:" + std::to_string(conf));
        } catch (...) {}
    }
    
    auto session_it = context.find("session_id");
    if (session_it != context.end()) {
        actions.push_back("bind_session:" + session_it->second);
    }
    
    actions.push_back("retrieve_relevant_facts");
    
    auto intent_it2 = context.find("intent_type");
    if (intent_it2 != context.end() && intent_it2->second == "identity_query") {
        actions.push_back("retrieve_episodic_memory");
    }
    
    actions.push_back("apply_reasoning");
    actions.push_back("generate_response");
    actions.push_back("validate_output");
    actions.push_back("emit_response");
    
    return actions;
}

PolicyEffect PolicyEngine::decide(const std::string& decision_id,
                                  const std::map<std::string, std::string>& context,
                                  std::string& reason) {
    if (locked == false) {
        lock();
    }
    
    float confidence = 0.5f;
    auto it = context.find("confidence");
    if (it != context.end()) {
        try {
            confidence = std::stof(it->second);
        } catch (...) {}
    }
    
    PolicyEffect effect = PolicyEffect::DENY;
    
    if (confidence > 0.85f) {
        effect = PolicyEffect::ALLOW;
        reason = "High confidence decision";
    } else if (confidence >= 0.7f) {
        effect = PolicyEffect::ALLOW;
        reason = "Medium confidence decision";
    } else {
        effect = PolicyEffect::DENY;
        reason = "Low confidence decision";
    }
    
    auto intent_it = context.find("intent_type");
    if (intent_it != context.end()) {
        if (intent_it->second == "identity_query" || 
            intent_it->second == "knowledge_retrieval") {
            effect = PolicyEffect::ALLOW;
            reason = "Allowed intent type: " + intent_it->second;
        }
    }
    
    return effect;
}

void PolicyEngine::lock() {
    if (!locked) {
        locked = true;
        update_policy_hash();
    }
}

uint64_t PolicyEngine::compute_hash(const std::string& s) const {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t prime = 0x100000001b3ULL;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= prime;
    }
    return hash;
}

void PolicyEngine::update_policy_hash() {
    std::string rules_str;
    for (const auto& rule : rules) {
        rules_str += rule.id + ":" + rule.condition + ":" + 
                    (rule.effect == PolicyEffect::ALLOW ? "ALLOW" : "DENY") + ";";
    }
    policy_hash = compute_hash(rules_str);
}

void PolicyEngine::parse_policy_blob(const std::string& blob) {
    std::istringstream iss(blob);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty()) continue;
        
        size_t first_comma = line.find(',');
        size_t second_comma = line.find(',', first_comma + 1);
        
        if (first_comma != std::string::npos && second_comma != std::string::npos) {
            std::string id = line.substr(0, first_comma);
            std::string condition = line.substr(first_comma + 1, second_comma - first_comma - 1);
            std::string effect_str = line.substr(second_comma + 1);
            
            PolicyEffect effect = PolicyEffect::UNDEFINED;
            if (effect_str == "ALLOW") effect = PolicyEffect::ALLOW;
            else if (effect_str == "DENY") effect = PolicyEffect::DENY;
            
            if (effect != PolicyEffect::UNDEFINED) {
                rules.push_back(PolicyRule(id, condition, effect));
            }
        }
    }
    update_policy_hash();
}

bool PolicyEngine::matches_condition(const std::string& query, 
                                     const std::string& rule_condition) const {
    if (query == rule_condition) return true;
    
    if (query.find("confidence") != std::string::npos && 
        rule_condition.find("confidence") != std::string::npos) {
        return true;
    }
    
    if (query.find("intent") != std::string::npos && 
        rule_condition.find("intent") != std::string::npos) {
        return true;
    }
    
    return false;
}

bool PolicyEngine::is_locked() const {
    return locked;
}

uint64_t PolicyEngine::get_policy_hash() const {
    return policy_hash;
}

const std::string& PolicyEngine::get_policy_blob() const {
    return policy_blob;
}

std::vector<PolicyRule> PolicyEngine::get_rules() const {
    return rules;
}

std::string PolicyEngine::to_json() const {
    std::string json = "{";
    json += "\"policy_hash\": " + std::to_string(policy_hash) + ", ";
    json += "\"locked\": " + std::string(locked ? "true" : "false") + ", ";
    json += "\"rule_count\": " + std::to_string(rules.size()) + ", ";
    json += "\"rules\": [";
    
    for (size_t i = 0; i < rules.size(); ++i) {
        json += "{\"id\": \"" + rules[i].id + "\", ";
        json += "\"condition\": \"" + rules[i].condition + "\", ";
        std::string effect_str = (rules[i].effect == PolicyEffect::ALLOW) ? "ALLOW" : "DENY";
        json += "\"effect\": \"" + effect_str + "\"}";
        if (i < rules.size() - 1) json += ", ";
    }
    
    json += "]}";
    return json;
}
