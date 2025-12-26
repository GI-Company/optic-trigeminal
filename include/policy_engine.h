#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <functional>
#include <sstream>

// PolicyEffect enum and PolicyRule struct
enum class PolicyEffect {
    ALLOW,
    DENY,
    UNDEFINED
};

struct PolicyRule {
    std::string id; // Unique identifier for the rule
    std::string condition; // A simple string condition, e.g., "intent == query_name", "confidence < 0.7"
    PolicyEffect effect;

    // Constructor to allow initializer list initialization
    PolicyRule(const std::string& rule_id, const std::string& rule_condition, PolicyEffect rule_effect)
        : id(rule_id), condition(rule_condition), effect(rule_effect) {}
};

// PolicyEngine class
class PolicyEngine {
public:
    PolicyEngine(const std::string& initial_policy_blob = "");
    
    void add_rule(const PolicyRule& r);
    
    bool evaluate(const std::string& condition) const;
    
    bool evaluate_constraint(const std::string& constraint_name, 
                           const std::map<std::string, std::string>& context) const;
    
    std::vector<std::string> generate_action_sequence(
        const std::string& goal,
        const std::map<std::string, std::string>& context) const;
    
    PolicyEffect decide(const std::string& decision_id,
                       const std::map<std::string, std::string>& context,
                       std::string& reason);
    
    void lock();
    bool is_locked() const;
    uint64_t get_policy_hash() const;
    const std::string& get_policy_blob() const;
    std::vector<PolicyRule> get_rules() const;
    std::string to_json() const;

private:
    bool locked;
    std::string policy_blob;
    uint64_t policy_hash;
    std::vector<PolicyRule> rules;
    
    void add_default_rules();
    void parse_policy_blob(const std::string& blob);
    uint64_t compute_hash(const std::string& s) const;
    void update_policy_hash();
    bool matches_condition(const std::string& query, 
                          const std::string& rule_condition) const;
};
