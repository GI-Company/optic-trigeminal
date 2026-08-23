#pragma once

#include "types.h"
#include <random>
#include <numeric>

class StemClassifier {
private:
    MatrixF W1, W2;
    VectorF b1, b2;
    std::mt19937 rng;
    
    VectorF relu(const VectorF& x) const;
    VectorF relu_backward(const VectorF& x) const;
    VectorF softmax(const VectorF& x) const;
    
public:
    StemClassifier();
    
    SafetyCategory classify(const std::string& text);
    SafetyCategory classify_embedding(const Embedding& emb);
    void adjust_boundary(const std::string& text, SafetyCategory category);
    void train_on_example(const std::string& text, SafetyCategory expected);
    
    MatrixF get_weights1() const { return W1; }
    MatrixF get_weights2() const { return W2; }
    void set_weights(const MatrixF& w1, const MatrixF& w2, 
                     const VectorF& b1_, const VectorF& b2_);
};

class OpticEmbedder {
private:
    MatrixF W1, W2;
    VectorF b1, b2;
    VectorF layer_norm_scale, layer_norm_bias;
    std::mt19937 rng;
    
    VectorF forward_layer(const VectorF& input, const MatrixF& W, const VectorF& b) const;
    VectorF layer_norm(const VectorF& x) const;
    VectorF relu(const VectorF& x) const;
    unsigned int hash_word(const std::string& word) const;
    
public:
    OpticEmbedder();
    
    Embedding embed(const std::string& text);
    Embedding embed_tokens(const VectorI& token_ids);
    VectorI tokenize(const std::string& text);
    void update_from_feedback(const std::string& text, const Embedding& target, float lr = 0.0001f);
    
    MatrixF get_weights1() const { return W1; }
    MatrixF get_weights2() const { return W2; }
    void set_weights(const MatrixF& w1, const MatrixF& w2,
                     const VectorF& b1_, const VectorF& b2_,
                     const VectorF& ln_scale, const VectorF& ln_bias);
};

class VTAPredictor {
private:
    struct RNNState {
        VectorF h_t;
        VectorF c_t;
        VectorF x_t;
    };
    
    MatrixF Wxh, Whh, Why;
    VectorF bh, by;
    std::vector<RNNState> states;
    int hidden_size;
    std::mt19937 rng;
    
    VectorF tanh(const VectorF& x) const;
    VectorF tanh_backward(const VectorF& x) const;
    VectorF sigmoid(const VectorF& x) const;
    
public:
    VTAPredictor();
    
    VectorF predict_next_token_probs(const Embedding& context, int vocab_size);
    float predict_surprise(const Embedding& embedding);
    float predict_dopamine(const std::string& context, bool was_good);
    void reset_state();
    void update_on_sequence(const std::vector<Embedding>& sequence, const VectorI& next_tokens);
    
    MatrixF get_weights_xh() const { return Wxh; }
    MatrixF get_weights_hh() const { return Whh; }
    MatrixF get_weights_hy() const { return Why; }
    void set_weights(const MatrixF& wxh, const MatrixF& whh, const MatrixF& why,
                     const VectorF& bh_, const VectorF& by_);
};

class OpticTrigeminal {
private:
    std::map<std::string, GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::map<std::pair<std::string, std::string>, float> edge_weights;
    int total_nodes;
    std::mt19937 rng;

    // TF-IDF document-frequency table across every node's label, updated
    // incrementally in add_concept. See find_k_neighbors for why this
    // exists alongside the neural embedding.
    std::map<std::string, int> document_frequency_;
    size_t total_documents_ = 0;

    static std::map<std::string, int> tokenize_and_count(const std::string& text);
    float lexical_similarity(const std::map<std::string, int>& query_terms, const GraphNode& node) const;

    VectorF shortest_path_bfs(const std::string& start, const std::string& end) const;
    // query_text is optional (defaults to none): purely-internal callers
    // that only have an embedding to compare (graph traversal between
    // existing nodes, not a user query) fall back to pure neural cosine
    // similarity unchanged. Callers that have the real query text should
    // pass it -- that's what makes the TF-IDF blend below apply.
    std::vector<std::pair<std::string, float>> find_k_neighbors(
        const Embedding& embedding, const std::string& query_text, int k, float threshold = 0.3f) const;

public:
    OpticTrigeminal();

    void add_concept(const std::string& id, const std::string& label,
                     const Embedding& embedding, const std::string& type = "general",
                     bool is_query_only = false);
    void add_edge(const std::string& source, const std::string& target,
                  float weight = 1.0f, const std::string& type = "related");
    void reinforce_path(const Embedding& from, const Embedding& to, float reward);
    void link_concepts(const std::string& from_id, const std::string& to_id, float strength = 1.0f);

    std::vector<std::pair<std::string, float>> find_related_concepts(
        const Embedding& embedding, const std::string& query_text, int top_k = 10) const;
    std::vector<std::string> traverse_path(const std::string& start, int depth = 3) const;
    std::vector<std::string> bfs_traverse(const std::string& start, int max_depth = 3) const;
    
    GraphNode* get_node(const std::string& id);
    const GraphNode* get_node(const std::string& id) const;
    
    int node_count() const { return total_nodes; }
    int edge_count() const { return edges.size(); }
    const std::map<std::string, GraphNode>& get_nodes() const { return nodes; }
    const std::vector<GraphEdge>& get_edges() const { return edges; }
    const std::map<std::pair<std::string, std::string>, float>& get_edge_weights() const { 
        return edge_weights; 
    }
};

class SequenceDecoder {
private:
    MatrixF Wxh, Whh, Why;
    VectorF bh, by;
    int hidden_size;
    int vocab_size;
    VectorI token_sequence;
    VectorF hidden_state; // New private member for RNN hidden state
    VectorStr vocabulary; // New private member for vocabulary
    std::mt19937 rng;
    
    VectorF sigmoid(const VectorF& x) const;
    VectorF softmax(const VectorF& x) const;
    VectorF tanh(const VectorF& x) const;
    
public:
    SequenceDecoder(const VectorStr& vocab);
    
    std::string generate(const std::string& prompt, int max_tokens = 128,
                         float temperature = 0.7f, int top_k = 50);
    std::string generate_from_embeddings(const std::vector<Embedding>& context,
                                         int max_tokens = 128, float temperature = 0.7f);
    void learn_sequence(const std::string& input, const std::string& output);
    void update_on_feedback(const std::string& input, const std::string& output, float lr = 0.0001f);
    
    MatrixF get_weights_xh() const { return Wxh; }
    MatrixF get_weights_hh() const { return Whh; }
    MatrixF get_weights_hy() const { return Why; }
    void set_weights(const MatrixF& wxh, const MatrixF& whh, const MatrixF& why,
                     const VectorF& bh_, const VectorF& by_);
};

class AdvancedDecoder {
private:
    OpticTrigeminal* graph_ptr;
    std::vector<std::string> vocabulary;
    std::mt19937 rng;
    
    std::vector<std::string> extract_key_concepts(const std::string& prompt);
    std::vector<std::string> traverse_concept_path(
        const std::vector<std::string>& start_concepts, int depth);
    std::string concepts_to_text(const std::vector<std::string>& concepts);
    
public:
    AdvancedDecoder();
    
    void set_graph(OpticTrigeminal* g) { graph_ptr = g; }
    void set_vocabulary(const VectorStr& vocab) { vocabulary = vocab; }
    
    std::string generate_from_knowledge(const std::string& prompt, int max_tokens = 128,
                                       float temperature = 0.7f);
    std::string generate_from_concepts(const std::vector<std::string>& concepts,
                                      int max_tokens = 128);
    std::string traverse_and_generate(const Embedding& start_embedding, int depth = 3);
    
    VectorStr get_vocabulary() const { return vocabulary; }
};
