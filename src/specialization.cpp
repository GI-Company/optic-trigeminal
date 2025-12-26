#include "specialization.h"
#include <sstream>
#include <cctype>
#include <algorithm>
#include <stack>

MathSpecializer::MathSpecializer() {}

std::vector<std::string> MathSpecializer::extract_numbers(const std::string& text) {
    std::vector<std::string> numbers;
    std::string current;
    bool in_number = false;
    
    for (char c : text) {
        if (std::isdigit(c) || c == '.') {
            current += c;
            in_number = true;
        } else if (in_number && current.length() > 0) {
            numbers.push_back(current);
            current.clear();
            in_number = false;
        }
    }
    
    if (!current.empty()) {
        numbers.push_back(current);
    }
    
    return numbers;
}

std::vector<std::string> MathSpecializer::extract_operators(const std::string& text) {
    std::vector<std::string> operators;
    for (char c : text) {
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '=') {
            operators.push_back(std::string(1, c));
        }
    }
    return operators;
}


int precedence(char op){
    if(op == '+' || op == '-')
        return 1;
    if(op == '*' || op == '/')
        return 2;
    return 0;
}

float applyOp(float a, float b, char op){
    switch(op){
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return a / b;
    }
    return 0;
}

float MathSpecializer::evaluate_expression(const std::string& expr) {
    std::stack<float> values;
    std::stack<char> ops;
    
    for(int i = 0; i < expr.length(); i++){
        if(expr[i] == ' ')
            continue;
        
        if(isdigit(expr[i]) || expr[i] == '.'){
            std::string num_str;
            while(i < expr.length() && (isdigit(expr[i]) || expr[i] == '.')){
                num_str += expr[i];
                i++;
            }
            values.push(std::stof(num_str));
            i--;
        }
        else if(expr[i] == '('){
            ops.push(expr[i]);
        }
        else if(expr[i] == ')'){
            while(!ops.empty() && ops.top() != '('){
                if(values.size() < 2) return 0; // Invalid expression
                float val2 = values.top();
                values.pop();
                
                float val1 = values.top();
                values.pop();
                
                char op = ops.top();
                ops.pop();
                
                values.push(applyOp(val1, val2, op));
            }
            if(!ops.empty())
               ops.pop();
        }
        else if (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/'){
            while(!ops.empty() && precedence(ops.top()) >= precedence(expr[i])){
                if(values.size() < 2) return 0; // Invalid expression
                float val2 = values.top();
                values.pop();
                
                float val1 = values.top();
                values.pop();
                
                char op = ops.top();
                ops.pop();
                
                values.push(applyOp(val1, val2, op));
            }
            ops.push(expr[i]);
        }
    }
    
    while(!ops.empty()){
        if(values.size() < 2) return 0; // Invalid expression
        float val2 = values.top();
        values.pop();
        
        float val1 = values.top();
        values.pop();
        
        char op = ops.top();
        ops.pop();
        
        values.push(applyOp(val1, val2, op));
    }
    
    return values.empty() ? 0 : values.top();
}

bool MathSpecializer::validate_math_input(const std::string& input) {
    auto numbers = extract_numbers(input);
    auto operators = extract_operators(input);
    
    if (numbers.empty()) return false;
    if (operators.empty() && numbers.size() == 1) return true;
    if (operators.size() != numbers.size() - 1) return false;
    
    return true;
}

DomainSpecializer::DomainResult MathSpecializer::process(const std::string& input) {
    DomainResult result;
    result.domain = "mathematics";
    result.confidence = 0.982f;
    result.reasoning_steps.push_back("Extract numbers and operators");
    result.reasoning_steps.push_back("Evaluate expression");
    result.reasoning_steps.push_back("Verify result");
    
    if (!validate_math_input(input)) {
        result.result = "Invalid mathematical expression";
        result.confidence = 0.5f;
        return result;
    }
    
    try {
        float value = evaluate_expression(input);
        result.result = std::to_string(value);
    } catch (...) {
        result.result = "Error evaluating expression";
        result.confidence = 0.1f;
    }
    
    return result;
}

LogicSpecializer::LogicSpecializer() {}

std::vector<LogicSpecializer::LogicalProposition> LogicSpecializer::parse_logic(const std::string& text) {
    std::vector<LogicalProposition> props;
    std::istringstream stream(text);
    std::string word;
    std::string current_statement;
    
    // Simple tokenizer for natural language
    auto tokenize_statement = [](const std::string& s) {
        std::vector<std::string> tokens;
        std::istringstream ss(s);
        std::string w;
        while (ss >> w) {
            tokens.push_back(w);
        }
        return tokens;
    };

    // Heuristic for truth value (extremely basic)
    auto infer_truth = [](const std::string& s) {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), ::tolower);
        if (lower_s.find("not") != std::string::npos || lower_s.find("false") != std::string::npos) return false;
        if (lower_s.find("is") != std::string::npos || lower_s.find("are") != std::string::npos || lower_s.find("true") != std::string::npos) return true;
        return true; // Default to true if no clear indicator (naive)
    };

    // Attempt to find "if ... then ..." pattern
    size_t if_pos = text.find("if");
    size_t then_pos = text.find("then");

    if (if_pos != std::string::npos && then_pos != std::string::npos && then_pos > if_pos) {
        std::string premise_str = text.substr(if_pos + 2, then_pos - (if_pos + 2));
        std::string conclusion_str = text.substr(then_pos + 4);

        // Trim whitespace
        premise_str.erase(0, premise_str.find_first_not_of(" \t\n\r"));
        premise_str.erase(premise_str.find_last_not_of(" \t\n\r") + 1);
        conclusion_str.erase(0, conclusion_str.find_first_not_of(" \t\n\r"));
        conclusion_str.erase(conclusion_str.find_last_not_of(" \t\n\r") + 1);

        if (!premise_str.empty()) {
            props.push_back({premise_str, infer_truth(premise_str)});
        }
        if (!conclusion_str.empty()) {
            props.push_back({conclusion_str, infer_truth(conclusion_str)});
        }
    } else {
        // Fallback to basic boolean parsing if no if-then structure
        while (stream >> word) {
            std::string lower_word = word;
            std::transform(lower_word.begin(), lower_word.end(), lower_word.begin(), ::tolower);
            if (lower_word == "true" || lower_word == "1") {
                props.push_back({"true", true});
            } else if (lower_word == "false" || lower_word == "0") {
                props.push_back({"false", false});
            } else if (lower_word == "and" || lower_word == "or" || lower_word == "not") {
                // Operators will be handled by evaluate_logic, for now just skip
            } else {
                current_statement += word + " ";
            }
        }
        if (!current_statement.empty()) {
            props.push_back({current_statement, infer_truth(current_statement)});
        }
    }
    
    return props;
}

bool LogicSpecializer::evaluate_logic(const std::vector<LogicalProposition>& props) {
    if (props.empty()) return true;
    
    // For simple "if-then" interpretations: if premise is true, conclude premise is true.
    // This is extremely basic and only for demonstrating rudimentary NLP logic.
    if (props.size() == 2) {
        bool premise_val = props[0].value;
        bool conclusion_val = props[1].value;
        
        // If "if P then Q" and P is true, then Q is true
        // If P is false, the implication holds (P implies Q is true if P is false)
        return !premise_val || conclusion_val; 
    }
    
    // Fallback for simple boolean evaluation
    bool result = props[0].value;
    for (size_t i = 1; i < props.size(); ++i) {
        // Since operators are no longer explicitly stored, this part becomes limited.
        // For now, if multiple propositions, assume AND for simplicity.
        result = result && props[i].value; 
    }
    
    return result;
}

std::string LogicSpecializer::logic_to_text(const std::vector<LogicalProposition>& props) {
    std::string result;
    
    if (props.empty()) return "No logical propositions found.";
    
    if (props.size() == 2) {
        result += "If \"" + props[0].variable + "\" (is " + (props[0].value ? "true" : "false") + ")";
        result += " then \"" + props[1].variable + "\" (is " + (props[1].value ? "true" : "false") + ")";
    } else {
        for (size_t i = 0; i < props.size(); ++i) {
            result += "\"" + props[i].variable + "\" is " + (props[i].value ? "true" : "false");
            if (i < props.size() - 1) {
                result += " AND "; // Default to AND for multiple simple propositions
            }
        }
    }
    
    return result;
}

DomainSpecializer::DomainResult LogicSpecializer::process(const std::string& input) {
    DomainResult result;
    result.domain = "logic";
    result.confidence = 0.965f;
    result.reasoning_steps.push_back("Parse logical propositions");
    result.reasoning_steps.push_back("Apply logical operations");
    result.reasoning_steps.push_back("Evaluate truth value");
    
    auto props = parse_logic(input);
    if (props.empty()) {
        result.result = "Cannot process complex natural language logic. Please rephrase your query using explicit boolean values (true/false, 1/0) and logical operators (AND, OR, NOT).";
        result.confidence = 0.3f;
        return result;
    }
    
    bool value = evaluate_logic(props);
    result.result = value ? "true" : "false";
    
    return result;
}

CausalitySpecializer::CausalitySpecializer() {}

std::vector<CausalitySpecializer::CausalRelation> CausalitySpecializer::extract_causality(const std::string& text) {
    std::vector<CausalRelation> relations;
    
    size_t pos = 0;
    while ((pos = text.find("because", pos)) != std::string::npos) {
        std::string cause = text.substr(std::max(0, (int)pos - 20), 20);
        std::string effect = text.substr(pos + 7, 20);
        
        relations.push_back({cause, effect, 0.8f, "because"});
        pos += 7;
    }
    
    pos = 0;
    while ((pos = text.find("causes", pos)) != std::string::npos) {
        std::string cause = text.substr(std::max(0, (int)pos - 20), 20);
        std::string effect = text.substr(pos + 6, 20);
        
        relations.push_back({cause, effect, 0.7f, "causes"});
        pos += 6;
    }
    
    return relations;
}

bool CausalitySpecializer::validate_causal_chain(const std::vector<CausalRelation>& chain) {
    if (chain.empty()) return false;
    
    for (const auto& rel : chain) {
        if (rel.cause.empty() || rel.effect.empty() || rel.cause.length() < 5 || rel.effect.length() < 5) {
            return false;
        }
    }
    
    return true;
}

std::string CausalitySpecializer::causality_to_explanation(const std::vector<CausalRelation>& relations) {
    std::string explanation;
    
    for (size_t i = 0; i < relations.size(); ++i) {
        explanation += relations[i].cause + " " + relations[i].mechanism + " " + relations[i].effect;
        if (i < relations.size() - 1) {
            explanation += ". ";
        }
    }
    
    return explanation;
}

DomainSpecializer::DomainResult CausalitySpecializer::process(const std::string& input) {
    DomainResult result;
    result.domain = "causality";
    result.confidence = 0.873f;
    result.reasoning_steps.push_back("Identify causal relationships");
    result.reasoning_steps.push_back("Validate causal chain");
    result.reasoning_steps.push_back("Generate explanation");
    
    auto relations = extract_causality(input);
    
    if (!validate_causal_chain(relations)) {
        result.result = "Cannot process complex causal reasoning. Please rephrase your query using explicit causal keywords like 'because', 'causes', or 'cause' to identify simple cause-and-effect pairs.";
        result.confidence = 0.4f;
        return result;
    }
    
    result.result = causality_to_explanation(relations);
    
    return result;
}

SafetyAttention::SafetyAttention() {}

VectorF SafetyAttention::compute_attention_weights(const Embedding& input, int seq_len) {
    VectorF weights(seq_len, 0.0f);
    
    float sum = 0.0f;
    for (int i = 0; i < seq_len; ++i) {
        if (i < (int)input.values.size()) {
            weights[i] = std::abs(input.values[i]);
        }
        sum += weights[i];
    }
    
    if (sum > 1e-8f) {
        for (auto& w : weights) w /= sum;
    }
    
    return weights;
}

float SafetyAttention::compute_harm_probability(const Embedding& embedding, SafetyCategory category) {
    float base_prob = 0.1f;
    
    switch (category) {
        case SafetyCategory::SAFE:
            return 0.01f;
        case SafetyCategory::IMPLICIT_HARM:
            return 0.3f;
        case SafetyCategory::EXPLICIT_HARM:
            return 0.9f;
        case SafetyCategory::HARASSMENT:
            return 0.85f;
        case SafetyCategory::HATE_SPEECH:
            return 0.9f;
        case SafetyCategory::SEXUAL:
            return 0.8f;
        case SafetyCategory::DECEPTION:
            return 0.6f;
        case SafetyCategory::ILLEGAL:
            return 0.95f;
        case SafetyCategory::VIOLENCE:
            return 0.88f;
        case SafetyCategory::SELF_HARM:
            return 0.92f;
        default:
            return base_prob;
    }
}

std::vector<SafetyAttention::AttentionWeight> SafetyAttention::identify_harmful_tokens(const std::string& text) {
    std::vector<AttentionWeight> harmful_tokens;
    
    std::vector<std::string> harmful_words = {
        "kill", "hate", "violence", "abuse", "illegal", "drug",
        "explicit", "pornographic", "harassment", "threat"
    };
    
    std::istringstream stream(text);
    std::string word;
    int token_id = 0;
    
    while (stream >> word) {
        for (const auto& harmful : harmful_words) {
            if (word.find(harmful) != std::string::npos) {
                harmful_tokens.push_back({token_id, 0.8f, harmful});
            }
        }
        token_id++;
    }
    
    return harmful_tokens;
}

float SafetyAttention::compute_safety_score(const std::string& text) {
    auto harmful = identify_harmful_tokens(text);
    
    if (harmful.empty()) {
        return 0.95f;
    }
    
    float penalty = harmful.size() * 0.15f;
    return std::max(0.0f, 1.0f - penalty);
}

float SafetyAttention::compute_safety_score_embedding(const Embedding& embedding) {
    float max_val = 0.0f;
    for (float v : embedding.values) {
        max_val = std::max(max_val, std::abs(v));
    }
    
    return std::max(0.0f, 1.0f - max_val * 0.5f);
}

bool SafetyAttention::contains_harmful_patterns(const std::string& text) {
    auto harmful = identify_harmful_tokens(text);
    return !harmful.empty();
}

std::vector<std::string> SafetyAttention::extract_harmful_tokens(const std::string& text) {
    std::vector<std::string> tokens;
    
    auto harmful = identify_harmful_tokens(text);
    for (const auto& h : harmful) {
        tokens.push_back(h.category);
    }
    
    return tokens;
}

ContrastiveLearner::ContrastiveLearner() {}

float ContrastiveLearner::triplet_loss(const Embedding& anchor, const Embedding& positive,
                                       const Embedding& negative, float margin) {
    float pos_dist = euclidean_distance(anchor.values, positive.values);
    float neg_dist = euclidean_distance(anchor.values, negative.values);
    
    float loss = std::max(0.0f, margin + pos_dist - neg_dist);
    return loss;
}

float ContrastiveLearner::ntxent_loss(const Embedding& x, const Embedding& y, float temperature) {
    float sim = x.cosine_similarity(y);
    float exp_sim = std::exp(sim / temperature);
    
    float loss = -std::log(exp_sim / (exp_sim + 1.0f));
    return loss;
}

void ContrastiveLearner::add_contrastive_pair(const Embedding& pos, const Embedding& neg) {
    training_pairs.push_back({pos, neg, 1.0f});
}

float ContrastiveLearner::compute_contrastive_loss(const std::vector<ContrastivePair>& pairs) {
    if (pairs.empty()) return 0.0f;
    
    float total_loss = 0.0f;
    for (const auto& pair : pairs) {
        Embedding dummy(EMBEDDING_DIM);
        total_loss += triplet_loss(pair.positive, pair.positive, pair.negative, pair.margin);
    }
    
    return total_loss / pairs.size();
}

void ContrastiveLearner::update_embeddings(std::vector<Embedding>& embeddings, float lr) {
    for (auto& emb : embeddings) {
        for (auto& val : emb.values) {
            val += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * lr;
        }
    }
}

MultiModalFusion::MultiModalFusion() {
    fusion_weights.resize(EMBEDDING_DIM, VectorF(3, 0.33f));
    modality_importance = {0.33f, 0.33f, 0.33f};
}

Embedding MultiModalFusion::fuse_embeddings(const std::vector<Embedding>& modalities) {
    Embedding fused(EMBEDDING_DIM);
    
    for (const auto& modality : modalities) {
        for (size_t i = 0; i < fused.values.size() && i < modality.values.size(); ++i) {
            fused.values[i] += modality.values[i] / modalities.size();
        }
    }
    
    return fused;
}

float MultiModalFusion::compute_modal_consistency(const std::vector<Embedding>& modalities) {
    if (modalities.size() < 2) return 1.0f;
    
    float consistency = 0.0f;
    for (size_t i = 0; i < modalities.size() - 1; ++i) {
        consistency += modalities[i].cosine_similarity(modalities[i + 1]);
    }
    
    return consistency / (modalities.size() - 1);
}

Embedding MultiModalFusion::fuse_text_image(const Embedding& text_emb, const Embedding& img_emb) {
    std::vector<Embedding> modalities = {text_emb, img_emb};
    return fuse_embeddings(modalities);
}

Embedding MultiModalFusion::fuse_text_code(const Embedding& text_emb, const Embedding& code_emb) {
    std::vector<Embedding> modalities = {text_emb, code_emb};
    return fuse_embeddings(modalities);
}

Embedding MultiModalFusion::fuse_all_modalities(const Embedding& text_emb, const Embedding& img_emb,
                                                const Embedding& code_emb) {
    std::vector<Embedding> modalities = {text_emb, img_emb, code_emb};
    return fuse_embeddings(modalities);
}

IntentOrchestrator::IntentOrchestrator() : knowledge_graph(nullptr) {
    intent_mapping["analyze"] = IntentType::REASONING;
    intent_mapping["explain"] = IntentType::REASONING;
    intent_mapping["reason"] = IntentType::REASONING;
    intent_mapping["think"] = IntentType::REASONING;
    intent_mapping["learn"] = IntentType::LEARNING;
    intent_mapping["remember"] = IntentType::LEARNING;
    intent_mapping["update"] = IntentType::LEARNING;
    intent_mapping["explore"] = IntentType::EXPLORATION;
    intent_mapping["discover"] = IntentType::EXPLORATION;
    intent_mapping["check"] = IntentType::VALIDATION;
    intent_mapping["verify"] = IntentType::VALIDATION;
    intent_mapping["validate"] = IntentType::VALIDATION;
}

IntentOrchestrator::IntentType IntentOrchestrator::classify_intent(const std::string& prompt) {
    for (const auto& [keyword, intent] : intent_mapping) {
        if (prompt.find(keyword) != std::string::npos) {
            return intent;
        }
    }
    
    return IntentType::QUERY;
}

bool IntentOrchestrator::contains_math_elements(const std::string& text) const {
    bool has_number = false;
    bool has_operator = false;
    for (char c : text) {
        if (std::isdigit(c)) {
            has_number = true;
        } else if (c == '+' || c == '-' || c == '*' || c == '/') {
            has_operator = true;
        }
    }
    return has_number && has_operator;
}

std::string IntentOrchestrator::determine_domain(const std::string& prompt, IntentType intent) {
    if (contains_math_elements(prompt)) {
        return "mathematics";
    }
    if (prompt.find("math") != std::string::npos || 
        prompt.find("calculate") != std::string::npos) {
        return "mathematics";
    }
    
    if (prompt.find("code") != std::string::npos ||
        prompt.find("write function") != std::string::npos ||
        prompt.find("python function") != std::string::npos ||
        prompt.find("javascript code") != std::string::npos ||
        prompt.find("create class") != std::string::npos) {
        return "code_generation";
    }
    
    if (prompt.find("cause") != std::string::npos ||
        prompt.find("effect") != std::string::npos) {
        return "causality";
    }
    
    if (prompt.find("logic") != std::string::npos ||
        prompt.find("reason") != std::string::npos ||
        prompt.find("if") != std::string::npos ||
        prompt.find("then") != std::string::npos ||
        prompt.find("made of") != std::string::npos ||
        prompt.find("puzzle") != std::string::npos) {
        return "logic";
    }
    
    return "general";
}

std::vector<std::string> IntentOrchestrator::plan_execution_steps(const Intent& intent) {
    std::vector<std::string> steps;
    
    steps.push_back("Encode input");
    
    if (intent.type == IntentType::REASONING) {
        steps.push_back("Retrieve relevant knowledge");
        steps.push_back("Apply domain reasoning");
        steps.push_back("Validate logic");
    } else if (intent.type == IntentType::LEARNING) {
        steps.push_back("Extract knowledge");
        steps.push_back("Update graph");
        steps.push_back("Reinforce patterns");
    } else if (intent.type == IntentType::EXPLORATION) {
        steps.push_back("Explore knowledge space");
        steps.push_back("Identify new connections");
        steps.push_back("Summarize findings");
    }
    
    steps.push_back("Generate response");
    
    return steps;
}

IntentOrchestrator::Intent IntentOrchestrator::decompose_intent(const std::string& prompt,
                                                                 const Embedding& context) {
    Intent intent;
    intent.type = classify_intent(prompt);
    intent.domain = determine_domain(prompt, intent.type);
    intent.confidence = 0.75f;
    intent.context_embedding = context;
    intent.steps = plan_execution_steps(intent);
    
    return intent;
}

std::string IntentOrchestrator::execute_intent(const Intent& intent) {
    std::string result = "Executing ";
    
    switch (intent.type) {
        case IntentType::QUERY:
            result += "query";
            break;
        case IntentType::REASONING:
            result += "reasoning task";
            break;
        case IntentType::LEARNING:
            result += "learning task";
            break;
        case IntentType::EXPLORATION:
            result += "exploration";
            break;
        case IntentType::VALIDATION:
            result += "validation";
            break;
        default:
            result += "unknown task";
    }
    
    result += " in domain: " + intent.domain;
    
    return result;
}

std::vector<IntentOrchestrator::Intent> IntentOrchestrator::parse_complex_intent(const std::string& prompt) {
    std::vector<Intent> intents;
    
    Embedding dummy(EMBEDDING_DIM);
    Intent primary = decompose_intent(prompt, dummy);
    intents.push_back(primary);
    
    return intents;
}
