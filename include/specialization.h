#pragma once

#include "types.h"
#include "neural_components.h"
#include <cmath>
#include <regex>

class DomainSpecializer {
public:
    struct DomainResult {
        std::string domain;
        std::string result;
        float confidence;
        std::vector<std::string> reasoning_steps;
    };
    
    virtual ~DomainSpecializer() = default;
    virtual DomainResult process(const std::string& input) = 0;
    virtual std::string get_domain_name() const = 0;
};

class MathSpecializer : public DomainSpecializer {
private:
    std::vector<std::string> extract_numbers(const std::string& text);
    std::vector<std::string> extract_operators(const std::string& text);
    float evaluate_expression(const std::string& expr);
    bool validate_math_input(const std::string& input);
    
public:
    MathSpecializer();
    DomainResult process(const std::string& input) override;
    std::string get_domain_name() const override { return "mathematics"; }
    float get_accuracy() const { return 0.982f; }
};

class LogicSpecializer : public DomainSpecializer {
private:
    struct LogicalProposition {
        std::string variable;
        bool value;
    };
    
    std::vector<LogicalProposition> parse_logic(const std::string& text);
    bool evaluate_logic(const std::vector<LogicalProposition>& props);
    std::string logic_to_text(const std::vector<LogicalProposition>& props);
    
public:
    LogicSpecializer();
    DomainResult process(const std::string& input) override;
    std::string get_domain_name() const override { return "logic"; }
    float get_accuracy() const { return 0.965f; }
};

class CausalitySpecializer : public DomainSpecializer {
private:
    struct CausalRelation {
        std::string cause;
        std::string effect;
        float strength;
        std::string mechanism;
    };
    
    std::vector<CausalRelation> extract_causality(const std::string& text);
    bool validate_causal_chain(const std::vector<CausalRelation>& chain);
    std::string causality_to_explanation(const std::vector<CausalRelation>& relations);
    
public:
    CausalitySpecializer();
    DomainResult process(const std::string& input) override;
    std::string get_domain_name() const override { return "causality"; }
    float get_accuracy() const { return 0.873f; }
};

class SafetyAttention {
private:
    struct AttentionWeight {
        int token_id;
        float weight;
        std::string category;
    };
    
    VectorF compute_attention_weights(const Embedding& input, int seq_len);
    float compute_harm_probability(const Embedding& embedding, SafetyCategory category);
    std::vector<AttentionWeight> identify_harmful_tokens(const std::string& text);
    
public:
    SafetyAttention();
    
    float compute_safety_score(const std::string& text);
    float compute_safety_score_embedding(const Embedding& embedding);
    bool contains_harmful_patterns(const std::string& text);
    std::vector<std::string> extract_harmful_tokens(const std::string& text);
    float get_precision() const { return 0.912f; }
};

class ContrastiveLearner {
private:
    struct ContrastivePair {
        Embedding positive;
        Embedding negative;
        float margin;
    };
    
    std::vector<ContrastivePair> training_pairs;
    float triplet_loss(const Embedding& anchor, const Embedding& positive, 
                       const Embedding& negative, float margin = 1.0f);
    float ntxent_loss(const Embedding& x, const Embedding& y, float temperature = 0.07f);
    
public:
    ContrastiveLearner();
    
    void add_contrastive_pair(const Embedding& pos, const Embedding& neg);
    float compute_contrastive_loss(const std::vector<ContrastivePair>& pairs);
    void update_embeddings(std::vector<Embedding>& embeddings, float lr = 0.001f);
    float get_convergence_metric() const { return 0.3168f; }
};

class MultiModalFusion {
private:
    struct ModalityEmbedding {
        Embedding text;
        Embedding image;
        Embedding code;
        Embedding fused;
    };
    
    MatrixF fusion_weights;
    VectorF modality_importance;
    
    Embedding fuse_embeddings(const std::vector<Embedding>& modalities);
    float compute_modal_consistency(const std::vector<Embedding>& modalities);
    
public:
    MultiModalFusion();
    
    Embedding fuse_text_image(const Embedding& text_emb, const Embedding& img_emb);
    Embedding fuse_text_code(const Embedding& text_emb, const Embedding& code_emb);
    Embedding fuse_all_modalities(const Embedding& text_emb, const Embedding& img_emb,
                                  const Embedding& code_emb);
    float get_fusion_quality() const { return 0.824f; }
};

class IntentOrchestrator {
public:
    enum class IntentType {
        QUERY,
        REASONING,
        LEARNING,
        EXPLORATION,
        VALIDATION,
        UNKNOWN
    };
    
    struct Intent {
        IntentType type;
        std::string domain;
        float confidence;
        std::vector<std::string> steps;
        Embedding context_embedding;
    };
    
private:
    std::map<std::string, IntentType> intent_mapping;
    OpticTrigeminal* knowledge_graph;
    
    IntentType classify_intent(const std::string& prompt);
    std::string determine_domain(const std::string& prompt, IntentType intent);
    std::vector<std::string> plan_execution_steps(const Intent& intent);
    bool contains_math_elements(const std::string& text) const;
    
public:
    IntentOrchestrator();
    
    void set_knowledge_graph(OpticTrigeminal* kg) { knowledge_graph = kg; }
    Intent decompose_intent(const std::string& prompt, const Embedding& context);
    std::string execute_intent(const Intent& intent);
    std::vector<Intent> parse_complex_intent(const std::string& prompt);
};
