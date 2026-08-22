#pragma once

#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>
#include <cmath>
#include <array>
#include <stdexcept>
#include <iostream>
#include <chrono>
#include <algorithm>

using MatrixF = std::vector<std::vector<float>>;
using VectorF = std::vector<float>;
using VectorI = std::vector<int>;
using VectorStr = std::vector<std::string>;

constexpr int EMBEDDING_DIM = 256;
constexpr int HIDDEN_DIM = 512;
constexpr int VOCAB_SIZE = 2000000;
constexpr int MAX_GRAPH_NODES = 50000;
constexpr int BPE_VOCAB_SIZE = 1000000;

enum class SafetyCategory {
    SAFE = 0,
    IMPLICIT_HARM = 1,
    EXPLICIT_HARM = 2,
    HARASSMENT = 3,
    HATE_SPEECH = 4,
    SEXUAL = 5,
    DECEPTION = 6,
    ILLEGAL = 7,
    VIOLENCE = 8,
    SELF_HARM = 9
};

struct Embedding {
    VectorF values;
    int dimension;
    
    Embedding() : dimension(0) {}
    explicit Embedding(int dim) : values(dim, 0.0f), dimension(dim) {}
    
    float cosine_similarity(const Embedding& other) const {
        if (values.size() != other.values.size() || values.empty()) return 0.0f;
        
        float dot = 0.0f, norm1 = 0.0f, norm2 = 0.0f;
        for (size_t i = 0; i < values.size(); ++i) {
            dot += values[i] * other.values[i];
            norm1 += values[i] * values[i];
            norm2 += other.values[i] * other.values[i];
        }
        
        float denom = std::sqrt(norm1) * std::sqrt(norm2);
        return denom > 1e-8f ? dot / denom : 0.0f;
    }
};

struct Token {
    std::string text;
    int id;
    float frequency;
    
    Token() : id(-1), frequency(0.0f) {}
    Token(const std::string& t, int i, float f = 1.0f) 
        : text(t), id(i), frequency(f) {}
};

struct GraphNode {
    std::string id;
    std::string label;
    std::string type;
    Embedding embedding;
    std::map<std::string, float> attributes;
    float importance;
    int access_count;
    
    GraphNode() : importance(0.0f), access_count(0) {}
    explicit GraphNode(const std::string& node_id) 
        : id(node_id), importance(0.0f), access_count(0),
          embedding(EMBEDDING_DIM) {}
};

struct GraphEdge {
    std::string source;
    std::string target;
    std::string type;
    float weight;
    int direction; // 0: undirected, 1: source->target, -1: bidirectional
    
    GraphEdge() : weight(0.0f), direction(0) {}
    GraphEdge(const std::string& src, const std::string& tgt, 
              float w, const std::string& t = "default", int d = 0)
        : source(src), target(tgt), type(t), weight(w), direction(d) {}
};

struct TrainingExample {
    std::string id;
    std::string input;
    std::string output;
    std::string domain;
    bool is_good;
    float confidence;
    
    TrainingExample() : is_good(true), confidence(1.0f) {}
    TrainingExample(const std::string& in, const std::string& out,
                    const std::string& dom = "general", bool good = true)
        : input(in), output(out), domain(dom), is_good(good), confidence(1.0f) {}
};

struct InferenceRequest {
    std::string prompt;
    int max_tokens;
    float temperature;
    int top_k;
    std::vector<std::string> context_history;
    bool use_reasoning_chain;
    bool enable_multimodal;
    
    InferenceRequest() : max_tokens(4096), temperature(0.7f), top_k(50), 
                         use_reasoning_chain(true), enable_multimodal(true) {}
    InferenceRequest(const std::string& p, int t = 4096)
        : prompt(p), max_tokens(t), temperature(0.7f), top_k(50),
          use_reasoning_chain(true), enable_multimodal(true) {}
};

struct InferenceResponse {
    std::string prompt;
    std::string response;
    std::string type;
    std::string timestamp;
    float confidence;
    SafetyCategory safety_category;
    std::vector<std::string> related_concepts;
    std::vector<std::string> reasoning_chain;
    std::vector<std::string> memory_retrieval;
    std::map<std::string, float> attention_weights;
    int context_window_used;
    float reasoning_depth;
    
    InferenceResponse() : confidence(1.0f), 
                         safety_category(SafetyCategory::SAFE),
                         context_window_used(0),
                         reasoning_depth(0.0f) {}
};

struct EpisodicMemory {
    std::string episode_id;
    std::string session_id;
    std::string input;
    std::string output;
    Embedding context_embedding;
    std::vector<std::string> reasoning_steps;
    std::vector<std::pair<std::string, float>> accessed_concepts;
    std::map<std::string, std::string> extracted_entities;
    float success_score;
    int64_t timestamp;
    int tokens_used;
    
    EpisodicMemory() : success_score(0.0f), timestamp(0), tokens_used(0) {}
};

struct TrainingSnapshot {
    VectorStr vocabulary_tokens;
    std::map<std::string, VectorF> graph_nodes;
    std::map<std::string, std::vector<std::string>> graph_edges;
    std::map<std::string, std::map<std::string, float>> edge_weights;
    std::map<std::string, MatrixF> embedder_weights;
    std::map<std::string, MatrixF> classifier_weights;
    std::string version;
    int training_examples;
    int64_t timestamp;
    int vocab_size;
    int graph_node_count;
    
    TrainingSnapshot() : version("3.0.0"), training_examples(0), 
                        timestamp(0), vocab_size(0), graph_node_count(0) {}
};

struct PerformanceMetrics {
    float inference_latency_ms;
    float embedding_quality;
    float safety_precision;
    float domain_accuracy_math;
    float domain_accuracy_logic;
    float domain_accuracy_causality;
    float multimodal_fusion_quality;
    int vocab_size;
    int graph_nodes;
    int training_records;
    int64_t uptime_ms;
    
    PerformanceMetrics() 
        : inference_latency_ms(0.0f), embedding_quality(0.0f),
          safety_precision(0.0f), domain_accuracy_math(0.0f),
          domain_accuracy_logic(0.0f), domain_accuracy_causality(0.0f),
          multimodal_fusion_quality(0.0f), vocab_size(0),
          graph_nodes(0), training_records(0), uptime_ms(0) {}
};

inline std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // gmtime() writes into a single shared static buffer; this server
    // handles every connection on its own detached thread (see
    // HTTPServer::start_server), so two concurrent requests calling
    // gmtime() at the same time could tear each other's struct tm fields
    // mid-write -- producing garbage dates (observed: a "year 2262"
    // timestamp). gmtime_r() writes into a caller-owned buffer instead.
    struct tm tm_buf;
    char buffer[30];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", gmtime_r(&time, &tm_buf));
    return std::string(buffer) + "Z";
}

inline float euclidean_distance(const VectorF& a, const VectorF& b) {
    if (a.size() != b.size()) return 1e9f;
    float dist = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        float diff = a[i] - b[i];
        dist += diff * diff;
    }
    return std::sqrt(dist);
}

inline float manhattan_distance(const VectorF& a, const VectorF& b) {
    if (a.size() != b.size()) return 1e9f;
    float dist = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dist += std::abs(a[i] - b[i]);
    }
    return dist;
}
