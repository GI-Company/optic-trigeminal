#include "neural_components.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <sstream>
#include <cctype>

VectorF StemClassifier::relu(const VectorF& x) const {
    VectorF result = x;
    for (auto& v : result) v = std::max(0.0f, v);
    return result;
}

VectorF StemClassifier::relu_backward(const VectorF& x) const {
    VectorF result = x;
    for (auto& v : result) v = v > 0.0f ? 1.0f : 0.0f;
    return result;
}

VectorF StemClassifier::softmax(const VectorF& x) const {
    VectorF result = x;
    float max_val = *std::max_element(result.begin(), result.end());
    float sum = 0.0f;
    for (auto& v : result) {
        v = std::exp(v - max_val);
        sum += v;
    }
    for (auto& v : result) v /= sum;
    return result;
}

StemClassifier::StemClassifier() : rng(std::random_device{}()) {
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    
    W1.resize(768, VectorF(512));
    W2.resize(512, VectorF(10));
    b1.resize(512, 0.0f);
    b2.resize(10, 0.0f);
    
    for (auto& row : W1) {
        for (auto& v : row) v = dist(rng);
    }
    for (auto& row : W2) {
        for (auto& v : row) v = dist(rng);
    }
}

SafetyCategory StemClassifier::classify(const std::string& text) {
    std::vector<std::string> harmful_keywords = {
        "kill", "harm", "violence", "abuse", "hate", "illegal", "drug",
        "explicit", "pornographic", "harassment", "threat", "terrorism"
    };
    
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
    
    for (const auto& keyword : harmful_keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return SafetyCategory::EXPLICIT_HARM;
        }
    }
    
    VectorF input(768, 0.0f);
    for (size_t i = 0; i < text.size() && i < 768; ++i) {
        input[i] = static_cast<float>(text[i]) / 256.0f;
    }
    
    VectorF h(512, 0.0f);
    for (int i = 0; i < 512; ++i) {
        for (int j = 0; j < 768; ++j) {
            h[i] += input[j] * W1[j][i];
        }
        h[i] += b1[i];
    }
    h = relu(h);
    
    VectorF output(10, 0.0f);
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 512; ++j) {
            output[i] += h[j] * W2[j][i];
        }
        output[i] += b2[i];
    }
    output = softmax(output);
    
    auto max_it = std::max_element(output.begin(), output.end());
    int category = std::distance(output.begin(), max_it);
    
    if (*max_it < 0.3f) {
        return SafetyCategory::SAFE;
    }
    
    return static_cast<SafetyCategory>(std::min(category, 9));
}

SafetyCategory StemClassifier::classify_embedding(const Embedding& emb) {
    VectorF input = emb.values;
    if (input.size() < 768) {
        input.resize(768, 0.0f);
    }
    
    VectorF h(512, 0.0f);
    for (int i = 0; i < 512; ++i) {
        for (int j = 0; j < std::min((int)input.size(), 768); ++j) {
            h[i] += input[j] * W1[j][i];
        }
        h[i] += b1[i];
    }
    h = relu(h);
    
    VectorF output(10, 0.0f);
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 512; ++j) {
            output[i] += h[j] * W2[j][i];
        }
        output[i] += b2[i];
    }
    output = softmax(output);
    
    auto max_it = std::max_element(output.begin(), output.end());
    int category = std::distance(output.begin(), max_it);
    return static_cast<SafetyCategory>(std::min(category, 9));
}

void StemClassifier::adjust_boundary(const std::string& text, SafetyCategory category) {
    VectorF input(768, 0.0f);
    for (size_t i = 0; i < text.size() && i < 768; ++i) {
        input[i] = static_cast<float>(text[i]) / 256.0f;
    }
    
    VectorF h(512, 0.0f);
    for (int i = 0; i < 512; ++i) {
        for (int j = 0; j < 768; ++j) {
            h[i] += input[j] * W1[j][i];
        }
        h[i] += b1[i];
    }
    h = relu(h);
    
    float lr = 0.001f;
    int target = static_cast<int>(category);
    
    for (int i = 0; i < 512; ++i) {
        if (h[i] != 0.0f) {
            W2[i][target] += lr;
        }
    }
}

void StemClassifier::train_on_example(const std::string& text, SafetyCategory expected) {
    adjust_boundary(text, expected);
}

void StemClassifier::set_weights(const MatrixF& w1, const MatrixF& w2,
                                 const VectorF& b1_, const VectorF& b2_) {
    W1 = w1;
    W2 = w2;
    b1 = b1_;
    b2 = b2_;
}

OpticEmbedder::OpticEmbedder() : rng(std::random_device{}()) {
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    std::normal_distribution<float> norm_dist(0.0f, 0.1f);
    
    W1.resize(768, VectorF(512));
    W2.resize(512, VectorF(EMBEDDING_DIM));
    b1.resize(512, 0.0f);
    b2.resize(EMBEDDING_DIM, 0.0f);
    layer_norm_scale.resize(EMBEDDING_DIM, 1.0f);
    layer_norm_bias.resize(EMBEDDING_DIM, 0.0f);
    
    for (auto& row : W1) {
        for (auto& v : row) v = norm_dist(rng) * std::sqrt(2.0f / 768.0f);
    }
    for (auto& row : W2) {
        for (auto& v : row) v = norm_dist(rng) * std::sqrt(2.0f / 512.0f);
    }
}

VectorF OpticEmbedder::forward_layer(const VectorF& input, const MatrixF& W, const VectorF& b) const {
    VectorF output(b.size(), 0.0f);
    for (size_t i = 0; i < b.size(); ++i) {
        for (size_t j = 0; j < input.size() && j < W.size(); ++j) {
            if (i < W[j].size()) {
                output[i] += input[j] * W[j][i];
            }
        }
        output[i] += b[i];
    }
    return output;
}

VectorF OpticEmbedder::layer_norm(const VectorF& x) const {
    float mean = 0.0f;
    for (float v : x) mean += v;
    mean /= x.size();
    
    float var = 0.0f;
    for (float v : x) {
        float diff = v - mean;
        var += diff * diff;
    }
    var /= x.size();
    
    VectorF result(x.size());
    float std = std::sqrt(var + 1e-5f);
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = ((x[i] - mean) / std) * layer_norm_scale[i] + layer_norm_bias[i];
    }
    return result;
}

VectorF OpticEmbedder::relu(const VectorF& x) const {
    VectorF result = x;
    for (auto& v : result) v = std::max(0.0f, v);
    return result;
}

unsigned int OpticEmbedder::hash_word(const std::string& word) const {
    unsigned int hash = 0;
    for (char c : word) {
        hash = (hash * 31) + c; // Simple polynomial rolling hash
    }
    return hash;
}

Embedding OpticEmbedder::embed(const std::string& text) {
    VectorF input(768, 0.0f);
    
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        unsigned int hash_val = hash_word(word);
        size_t index = hash_val % input.size(); // Map hash to an index in the input vector
        input[index] += 1.0f; // Increment count for word presence (simple bag-of-words)
    }
    
    // Normalize input to prevent large values from dominating
    float sum_input = 0.0f;
    for (float v : input) sum_input += v;
    if (sum_input > 0) {
        for (float& v : input) v /= sum_input;
    }
    
    VectorF h1 = forward_layer(input, W1, b1);
    h1 = relu(h1);
    
    VectorF h2 = forward_layer(h1, W2, b2);
    h2 = layer_norm(h2);
    
    Embedding emb(EMBEDDING_DIM);
    emb.values = h2;
    return emb;
}

Embedding OpticEmbedder::embed_tokens(const VectorI& token_ids) {
    VectorF input(768, 0.0f);
    for (size_t i = 0; i < token_ids.size() && i < 768; ++i) {
        input[i] = static_cast<float>(token_ids[i]) / 50000.0f;
    }
    
    VectorF h1 = forward_layer(input, W1, b1);
    h1 = relu(h1);
    
    VectorF h2 = forward_layer(h1, W2, b2);
    h2 = layer_norm(h2);
    
    Embedding emb(EMBEDDING_DIM);
    emb.values = h2;
    return emb;
}

VectorI OpticEmbedder::tokenize(const std::string& text) {
    VectorI tokens;
    for (char c : text) {
        tokens.push_back(static_cast<int>(static_cast<unsigned char>(c)));
    }
    return tokens;
}

void OpticEmbedder::update_from_feedback(const std::string& text, const Embedding& target, float lr) {
    // Was: encode `text` as raw byte values (input[i] = text[i]/256), while
    // embed() encodes it as a hashed bag-of-words count vector -- so this
    // trained the network on a completely different input distribution
    // than the one ever used at inference time, making the update
    // essentially useless regardless of how long it ran. It also compared
    // the *hidden* layer h1 directly against `target` (a final-embedding-
    // dimensioned vector) and wrote gradient-free deltas into W2 -- not a
    // real backprop step, and not even touching W1.
    //
    // This now runs the same forward pass embed() uses, computes a real
    // squared-error gradient against the actual output h2, and takes one
    // online SGD step through both layers (delta rule through the linear
    // layers; layer_norm's own gradient is treated as approximately
    // identity here, a standard simplification for a lightweight online
    // update rather than a full batched trainer).
    VectorF input(768, 0.0f);
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        unsigned int hash_val = hash_word(word);
        size_t index = hash_val % input.size();
        input[index] += 1.0f;
    }
    float sum_input = 0.0f;
    for (float v : input) sum_input += v;
    if (sum_input > 0) {
        for (float& v : input) v /= sum_input;
    }

    VectorF h1_pre = forward_layer(input, W1, b1);
    VectorF h1 = relu(h1_pre);
    VectorF h2_pre = forward_layer(h1, W2, b2);
    VectorF h2 = layer_norm(h2_pre);

    size_t out_dim = std::min(h2.size(), target.values.size());
    VectorF output_error(h2.size(), 0.0f);
    for (size_t i = 0; i < out_dim; ++i) {
        output_error[i] = target.values[i] - h2[i];
    }

    // Hidden-layer error, backpropagated through W2 and gated by the ReLU
    // derivative (zero wherever the forward pass clipped that unit).
    VectorF hidden_error(h1.size(), 0.0f);
    for (size_t j = 0; j < h1.size(); ++j) {
        if (h1_pre[j] <= 0.0f) continue;
        float sum = 0.0f;
        for (size_t i = 0; i < output_error.size() && i < W2[j].size(); ++i) {
            sum += output_error[i] * W2[j][i];
        }
        hidden_error[j] = sum;
    }

    for (size_t j = 0; j < h1.size(); ++j) {
        for (size_t i = 0; i < output_error.size() && i < W2[j].size(); ++i) {
            W2[j][i] += lr * output_error[i] * h1[j];
        }
    }
    for (size_t i = 0; i < output_error.size() && i < b2.size(); ++i) {
        b2[i] += lr * output_error[i];
    }

    for (size_t k = 0; k < input.size(); ++k) {
        if (input[k] == 0.0f) continue; // sparse: skip zero input dims
        for (size_t j = 0; j < hidden_error.size() && j < W1[k].size(); ++j) {
            W1[k][j] += lr * hidden_error[j] * input[k];
        }
    }
    for (size_t j = 0; j < hidden_error.size() && j < b1.size(); ++j) {
        b1[j] += lr * hidden_error[j];
    }
}

void OpticEmbedder::set_weights(const MatrixF& w1, const MatrixF& w2,
                                 const VectorF& b1_, const VectorF& b2_,
                                 const VectorF& ln_scale, const VectorF& ln_bias) {
    W1 = w1;
    W2 = w2;
    b1 = b1_;
    b2 = b2_;
    layer_norm_scale = ln_scale;
    layer_norm_bias = ln_bias;
}

VTAPredictor::VTAPredictor() : hidden_size(256), rng(std::random_device{}()) {
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    
    Wxh.resize(EMBEDDING_DIM, VectorF(hidden_size, 0.0f));
    Whh.resize(hidden_size, VectorF(hidden_size, 0.0f));
    Why.resize(hidden_size, VectorF(VOCAB_SIZE, 0.0f));
    bh.resize(hidden_size, 0.0f);
    by.resize(VOCAB_SIZE, 0.0f);
    
    for (auto& row : Wxh) {
        for (auto& v : row) v = dist(rng);
    }
    for (auto& row : Whh) {
        for (auto& v : row) v = dist(rng);
    }
    for (auto& row : Why) {
        for (auto& v : row) v = dist(rng);
    }
}

VectorF VTAPredictor::tanh(const VectorF& x) const {
    VectorF result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = std::tanh(x[i]);
    }
    return result;
}

VectorF VTAPredictor::tanh_backward(const VectorF& x) const {
    VectorF result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        float tanh_x = std::tanh(x[i]);
        result[i] = 1.0f - tanh_x * tanh_x;
    }
    return result;
}

VectorF VTAPredictor::sigmoid(const VectorF& x) const {
    VectorF result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = 1.0f / (1.0f + std::exp(-x[i]));
    }
    return result;
}

VectorF VTAPredictor::predict_next_token_probs(const Embedding& context, int vocab_size) {
    VectorF output(vocab_size, 0.0f);
    VectorF h(hidden_size, 0.0f);
    
    for (int i = 0; i < hidden_size; ++i) {
        for (int j = 0; j < EMBEDDING_DIM && j < (int)context.values.size(); ++j) {
            h[i] += context.values[j] * Wxh[j][i];
        }
        h[i] += bh[i];
    }
    h = tanh(h);
    
    for (int i = 0; i < vocab_size && i < (int)Why.size(); ++i) {
        for (int j = 0; j < hidden_size; ++j) {
            output[i] += h[j] * Why[j][i];
        }
        output[i] += by[i];
    }
    
    float max_val = *std::max_element(output.begin(), output.end());
    float sum = 0.0f;
    for (auto& v : output) {
        v = std::exp(v - max_val);
        sum += v;
    }
    for (auto& v : output) v /= sum;
    
    return output;
}

float VTAPredictor::predict_surprise(const Embedding& embedding) {
    VectorF probs = predict_next_token_probs(embedding, 100);
    float entropy = 0.0f;
    for (float p : probs) {
        if (p > 1e-8f) {
            entropy -= p * std::log(p);
        }
    }
    return std::tanh(entropy / 4.0f);
}

float VTAPredictor::predict_dopamine(const std::string& context, bool was_good) {
    return was_good ? 0.5f : -0.3f;
}

void VTAPredictor::reset_state() {
    states.clear();
}

void VTAPredictor::update_on_sequence(const std::vector<Embedding>& sequence, const VectorI& next_tokens) {
    float lr = 0.0001f;
    for (size_t t = 0; t < sequence.size() && t < next_tokens.size(); ++t) {
        const auto& context = sequence[t];
        int target = next_tokens[t];
        
        VectorF probs = predict_next_token_probs(context, VOCAB_SIZE);
        if (target >= 0 && target < (int)probs.size()) {
            float error = -std::log(std::max(probs[target], 1e-8f));
            probs[target] -= 1.0f;
            
            for (int i = 0; i < hidden_size; ++i) {
                for (int j = 0; j < EMBEDDING_DIM && j < (int)context.values.size(); ++j) {
                    Wxh[j][i] += lr * error * context.values[j];
                }
            }
        }
    }
}

void VTAPredictor::set_weights(const MatrixF& wxh, const MatrixF& whh, const MatrixF& why,
                               const VectorF& bh_, const VectorF& by_) {
    Wxh = wxh;
    Whh = whh;
    Why = why;
    bh = bh_;
    by = by_;
}

OpticTrigeminal::OpticTrigeminal() : total_nodes(0), rng(std::random_device{}()) {}

std::map<std::string, int> OpticTrigeminal::tokenize_and_count(const std::string& text) {
    std::map<std::string, int> counts;
    std::string word;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            word += static_cast<char>(std::tolower(c));
        } else if (!word.empty()) {
            counts[word]++;
            word.clear();
        }
    }
    if (!word.empty()) counts[word]++;
    return counts;
}

void OpticTrigeminal::add_concept(const std::string& id, const std::string& label,
                                  const Embedding& embedding, const std::string& type,
                                  bool is_query_only) {
    if (nodes.find(id) == nodes.end()) {
        GraphNode node(id);
        node.label = label;
        node.type = type;
        node.embedding = embedding;
        node.is_query_only = is_query_only;
        node.term_counts = tokenize_and_count(label);
        for (const auto& [term, count] : node.term_counts) {
            document_frequency_[term]++;
        }
        total_documents_++;
        nodes[id] = node;
        total_nodes++;
    }
}

void OpticTrigeminal::add_edge(const std::string& source, const std::string& target,
                               float weight, const std::string& type) {
    edges.emplace_back(source, target, weight, type);
    edge_weights[{source, target}] = weight;
}

void OpticTrigeminal::reinforce_path(const Embedding& from, const Embedding& to, float reward) {
    // No query text available here -- this reinforces edges between
    // existing graph nodes from their embeddings alone, not a user query,
    // so it falls back to pure neural similarity (empty query_text).
    auto from_neighbors = find_k_neighbors(from, "", 5);
    auto to_neighbors = find_k_neighbors(to, "", 5);
    
    for (const auto& [from_id, _1] : from_neighbors) {
        for (const auto& [to_id, _2] : to_neighbors) {
            auto key = std::make_pair(from_id, to_id);
            if (edge_weights.find(key) != edge_weights.end()) {
                edge_weights[key] += reward * 0.1f;
            } else {
                add_edge(from_id, to_id, reward * 0.1f);
            }
        }
    }
}

void OpticTrigeminal::link_concepts(const std::string& from_id, const std::string& to_id, float strength) {
    add_edge(from_id, to_id, strength, "user_feedback");
    if (nodes.find(from_id) != nodes.end()) {
        nodes[from_id].importance += strength * 0.1f;
    }
}

float OpticTrigeminal::lexical_similarity(const std::map<std::string, int>& query_terms, const GraphNode& node) const {
    if (query_terms.empty() || node.term_counts.empty()) return 0.0f;

    auto idf = [&](const std::string& term) {
        auto it = document_frequency_.find(term);
        int df = (it != document_frequency_.end()) ? it->second : 0;
        // Standard smoothed IDF: +1 so an unseen term doesn't divide by
        // zero, +1 on the whole log so a term present in every node still
        // carries a small nonzero weight rather than dropping to 0.
        return std::log((static_cast<float>(total_documents_) + 1.0f) / (static_cast<float>(df) + 1.0f)) + 1.0f;
    };

    float dot = 0.0f, query_norm = 0.0f, node_norm = 0.0f;
    for (const auto& [term, qcount] : query_terms) {
        float weight = idf(term);
        float qw = static_cast<float>(qcount) * weight;
        query_norm += qw * qw;
        auto nit = node.term_counts.find(term);
        if (nit != node.term_counts.end()) {
            dot += qw * (static_cast<float>(nit->second) * weight);
        }
    }
    for (const auto& [term, ncount] : node.term_counts) {
        float nw = static_cast<float>(ncount) * idf(term);
        node_norm += nw * nw;
    }

    if (query_norm <= 0.0f || node_norm <= 0.0f) return 0.0f;
    return dot / (std::sqrt(query_norm) * std::sqrt(node_norm));
}

std::vector<std::pair<std::string, float>> OpticTrigeminal::find_k_neighbors(
    const Embedding& embedding, const std::string& query_text, int k, float threshold) const {
    std::vector<std::pair<std::string, float>> neighbors;

    // OpticEmbedder's weights are randomly initialized (see its
    // constructor) and never actually trained before being used live --
    // the only training pipeline that touches them (training_stages.cpp)
    // trains a separate, throwaway embedder instance and checkpoints it
    // lossily (capped well below W1's real size, placeholder biases), and
    // nothing loads that checkpoint back into the live engine at startup.
    // So neural cosine similarity here is closer to noise than genuine
    // semantic relevance at corpus scale (confirmed live: a medical query
    // was returning marketing copy as its top match). TF-IDF lexical
    // overlap needs no training at all and is a real, verifiable
    // relevance signal, so it dominates the blend below; neural still
    // contributes a modest amount rather than being discarded outright,
    // since it's real, functioning infrastructure -- just not a trained
    // one yet.
    std::map<std::string, int> query_terms;
    if (!query_text.empty()) {
        query_terms = tokenize_and_count(query_text);
    }

    for (const auto& [id, node] : nodes) {
        // A stored question is never itself a useful answer to a live
        // query -- see GraphNode::is_query_only. Internal callers with no
        // real query_text (graph traversal between existing nodes) still
        // consider every node, unaffected.
        if (!query_terms.empty() && node.is_query_only) continue;

        float neural_sim = embedding.cosine_similarity(node.embedding);
        float combined_sim = neural_sim;
        if (!query_terms.empty()) {
            float lexical_sim = lexical_similarity(query_terms, node);
            combined_sim = 0.85f * lexical_sim + 0.15f * neural_sim;
        }
        if (combined_sim > threshold) {
            // Bulk-loaded nodes (NativeInferenceEngine::initialize_with_training_data)
            // are keyed by an opaque "concept_N" id with the real text stored
            // separately in node.label -- returning the bare id here is what
            // surfaced literal strings like "concept_1583" as a "related
            // concept" in API responses instead of anything a person could
            // read. Fall back to id only for nodes that never got a label.
            neighbors.emplace_back(node.label.empty() ? id : node.label, combined_sim);
        }
    }

    std::sort(neighbors.rbegin(), neighbors.rend(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    if (neighbors.size() > (size_t)k) {
        neighbors.resize(k);
    }
    
    return neighbors;
}

std::vector<std::pair<std::string, float>> OpticTrigeminal::find_related_concepts(
    const Embedding& embedding, const std::string& query_text, int top_k) const {
    return find_k_neighbors(embedding, query_text, top_k, 0.3f);
}

std::vector<std::string> OpticTrigeminal::traverse_path(const std::string& start, int depth) const {
    std::vector<std::string> path = {start};
    std::string current = start;
    
    for (int d = 0; d < depth; ++d) {
        std::string next = current;
        float max_weight = 0.0f;
        
        for (const auto& edge : edges) {
            if (edge.source == current && edge.weight > max_weight) {
                max_weight = edge.weight;
                next = edge.target;
            }
        }
        
        if (next == current) break;
        path.push_back(next);
        current = next;
    }
    
    return path;
}

std::vector<std::string> OpticTrigeminal::bfs_traverse(const std::string& start, int max_depth) const {
    std::vector<std::string> visited;
    std::vector<std::pair<std::string, int>> queue = {{start, 0}};
    
    for (size_t i = 0; i < queue.size(); ++i) {
        const auto& [current, depth] = queue[i];
        if (depth >= max_depth) continue;
        if (std::find(visited.begin(), visited.end(), current) != visited.end()) continue;
        
        visited.push_back(current);
        
        for (const auto& edge : edges) {
            if (edge.source == current) {
                queue.emplace_back(edge.target, depth + 1);
            }
        }
    }
    
    return visited;
}

GraphNode* OpticTrigeminal::get_node(const std::string& id) {
    auto it = nodes.find(id);
    return it != nodes.end() ? &it->second : nullptr;
}

const GraphNode* OpticTrigeminal::get_node(const std::string& id) const {
    auto it = nodes.find(id);
    return it != nodes.end() ? &it->second : nullptr;
}

SequenceDecoder::SequenceDecoder(const VectorStr& vocab)
    : hidden_size(2048), vocab_size(vocab.size()), vocabulary(vocab), rng(std::random_device{}()) {
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    
    Wxh.resize(768, VectorF(hidden_size, 0.0f));
    Whh.resize(hidden_size, VectorF(hidden_size, 0.0f));
    Why.resize(hidden_size, VectorF(vocab_size, 0.0f));
    bh.resize(hidden_size, 0.0f);
    by.resize(vocab_size, 0.0f);
    hidden_state.resize(hidden_size, 0.0f); // Initialize hidden state

    
    for (auto& row : Wxh) {
        for (auto& v : row) v = dist(rng);
    }
    for (auto& row : Whh) {
        for (auto& v : row) v = dist(rng);
    }
    for (auto& row : Why) {
        for (auto& v : row) v = dist(rng);
    }
}

VectorF SequenceDecoder::sigmoid(const VectorF& x) const {
    VectorF result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = 1.0f / (1.0f + std::exp(-x[i]));
    }
    return result;
}

VectorF SequenceDecoder::softmax(const VectorF& x) const {
    VectorF result = x;
    float max_val = *std::max_element(result.begin(), result.end());
    float sum = 0.0f;
    for (auto& v : result) {
        v = std::exp(v - max_val);
        sum += v;
    }
    for (auto& v : result) v /= sum;
    return result;
}

VectorF SequenceDecoder::tanh(const VectorF& x) const {
    VectorF result(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        result[i] = std::tanh(x[i]);
    }
    return result;
}

std::string SequenceDecoder::generate(const std::string& prompt, int max_tokens,
                                     float temperature, int top_k) {
    std::string result = prompt;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // Initialize hidden_state for this generation call
    hidden_state = VectorF(hidden_size, 0.0f);

    // Process the prompt to set an initial hidden state
    for (size_t i = 0; i < prompt.size(); ++i) {
        int char_idx = static_cast<unsigned char>(prompt[i]); // Use unsigned char for index safety
        if (char_idx < (int)Wxh.size()) {
            for (int j = 0; j < hidden_size; ++j) {
                hidden_state[j] += Wxh[char_idx][j];
            }
        }
    }
    hidden_state = tanh(hidden_state); // Apply activation after processing initial prompt

    for (int t = 0; t < max_tokens; ++t) {
        // Calculate output logits from current hidden_state
        VectorF logits(vocab_size, 0.0f);
        for (int i = 0; i < vocab_size && i < (int)Why.size(); ++i) {
            for (int j = 0; j < hidden_size; ++j) {
                logits[i] += hidden_state[j] * Why[j][i];
            }
            logits[i] += by[i];
            logits[i] /= temperature;
        }
        
        VectorF probs = softmax(logits);
        
        std::vector<int> top_indices(std::min(top_k, vocab_size));
        std::iota(top_indices.begin(), top_indices.end(), 0);
        std::partial_sort(top_indices.begin(), 
                         top_indices.begin() + std::min(top_k, vocab_size),
                         top_indices.end(),
                         [&probs](int a, int b) { return probs[a] > probs[b]; });
        
        float rand_val = dist(rng);
        float cumsum = 0.0f;
        int selected = 32;
        
        for (int i = 0; i < std::min(top_k, vocab_size); ++i) {
            cumsum += probs[top_indices[i]];
            if (rand_val <= cumsum) {
                selected = top_indices[i];
                break;
            }
        }
        
        if (selected >= 32 && selected < 127) {
            result += static_cast<char>(selected);
        } else if (selected == 10) {
            result += '\n';
        } else {
            result += ' ';
        }

        // Update hidden_state for the next step based on the selected token
        VectorF input_contribution(hidden_size, 0.0f);
        int next_input_char_idx = selected;
        if (next_input_char_idx >= 0 && next_input_char_idx < (int)Wxh.size()) {
            for (int j = 0; j < hidden_size; ++j) {
                input_contribution[j] = Wxh[next_input_char_idx][j];
            }
        }

        VectorF prev_hidden_state_contribution(hidden_size, 0.0f);
        for (int j = 0; j < hidden_size; ++j) {
            prev_hidden_state_contribution[j] = hidden_state[j] * Whh[j][j]; // still simplistic
        }

        // Create a temporary VectorF for the input to tanh
        VectorF pre_tanh_input(hidden_size);
        for (int j = 0; j < hidden_size; ++j) {
            pre_tanh_input[j] = input_contribution[j] + prev_hidden_state_contribution[j] + bh[j];
        }
        hidden_state = tanh(pre_tanh_input); // Apply tanh to the vector and assign back
    }
    
    return result;
}

std::string SequenceDecoder::generate_from_embeddings(const std::vector<Embedding>& context,
                                                      int max_tokens, float temperature) {
    std::string result;
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    
    // Initialize hidden_state from context embeddings
    hidden_state = VectorF(hidden_size, 0.0f);
    for (const auto& emb : context) {
        for (int j = 0; j < hidden_size && j < (int)emb.values.size(); ++j) {
            hidden_state[j] += emb.values[j];
        }
    }
    hidden_state = tanh(hidden_state); // Apply activation
    
    for (int t = 0; t < max_tokens; ++t) {
        // Calculate output logits from current hidden_state
        VectorF logits(vocab_size, 0.0f);
        for (int i = 0; i < vocab_size && i < (int)Why.size(); ++i) {
            for (int j = 0; j < hidden_size; ++j) {
                logits[i] += hidden_state[j] * Why[j][i];
            }
            logits[i] += by[i];
            logits[i] /= temperature;
        }
        
        VectorF probs = softmax(logits);
        
        // Sampling logic (using top_k as 50 by default for now)
        int top_k = 50; // Default for simplicity
        std::vector<int> top_indices(std::min(top_k, vocab_size));
        std::iota(top_indices.begin(), top_indices.end(), 0);
        std::partial_sort(top_indices.begin(), 
                         top_indices.begin() + std::min(top_k, vocab_size),
                         top_indices.end(),
                         [&probs](int a, int b) { return probs[a] > probs[b]; });
        
        float rand_val = dist(rng);
        float cumsum = 0.0f;
        int selected = 32;
        
        for (int i = 0; i < std::min(top_k, vocab_size); ++i) {
            cumsum += probs[top_indices[i]];
            if (rand_val <= cumsum) {
                selected = top_indices[i];
                break;
            }
        }
        
        if (selected >= 32 && selected < 127) {
            result += static_cast<char>(selected);
        } else if (selected == 10) {
            result += '\n';
        } else {
            result += ' ';
        }

        // Update hidden_state for the next step based on the selected token
        VectorF input_contribution(hidden_size, 0.0f);
        int next_input_char_idx = selected;
        if (next_input_char_idx >= 0 && next_input_char_idx < (int)Wxh.size()) {
            for (int j = 0; j < hidden_size; ++j) {
                input_contribution[j] = Wxh[next_input_char_idx][j];
            }
        }

        VectorF prev_hidden_state_contribution(hidden_size, 0.0f);
        for (int j = 0; j < hidden_size; ++j) {
            prev_hidden_state_contribution[j] = hidden_state[j] * Whh[j][j]; // still simplistic
        }

        // Create a temporary VectorF for the input to tanh
        VectorF pre_tanh_input(hidden_size);
        for (int j = 0; j < hidden_size; ++j) {
            pre_tanh_input[j] = input_contribution[j] + prev_hidden_state_contribution[j] + bh[j];
        }
        hidden_state = tanh(pre_tanh_input); // Apply tanh to the vector and assign back
    }
    
    return result;
}

void SequenceDecoder::learn_sequence(const std::string& input, const std::string& output) {
    float lr = 0.0001f; // Learning rate
    
    // Process input to get an initial hidden state
    VectorF current_hidden_state(hidden_size, 0.0f);
    for (size_t i = 0; i < input.size(); ++i) {
        int input_char_idx = static_cast<unsigned char>(input[i]);
        if (input_char_idx >= 0 && input_char_idx < (int)Wxh.size()) {
            for (int j = 0; j < hidden_size; ++j) {
                current_hidden_state[j] += Wxh[input_char_idx][j];
            }
        }
    }
    current_hidden_state = tanh(current_hidden_state); // Apply activation

    // Update weights based on target output
    for (size_t t = 0; t < output.size(); ++t) {
        int output_char_idx = static_cast<unsigned char>(output[t]);

        // Update Why weights (hidden to output)
        if (output_char_idx >= 0 && output_char_idx < (int)Why[0].size()) { // Check output char index validity
            for (int j = 0; j < hidden_size; ++j) {
                // Simplistic update: push output towards target given hidden state
                Why[j][output_char_idx] += lr * current_hidden_state[j];
            }
        }

        // Simulate hidden state update for next step (simplified)
        // In a real RNN, this would involve input_t+1 and full recurrent connections.
        // For this simple model, we'll just 'pass' the current hidden state forward.
        // This is still very basic, but better than no-op.
    }
}

void SequenceDecoder::update_on_feedback(const std::string& input, const std::string& output, float lr) {
    // A simple feedback mechanism:
    // If feedback is positive (was_good), reinforce the learned sequence.
    // If feedback is negative, subtly discourage the sequence (this is very simplistic).
    // For now, we just use the positive reinforcement, as discouragement would require more complex backprop.
    learn_sequence(input, output);
}

void SequenceDecoder::set_weights(const MatrixF& wxh, const MatrixF& whh, const MatrixF& why,
                                  const VectorF& bh_, const VectorF& by_) {
    Wxh = wxh;
    Whh = whh;
    Why = why;
    bh = bh_;
    by = by_;
}

AdvancedDecoder::AdvancedDecoder() : graph_ptr(nullptr), rng(std::random_device{}()) {}

std::vector<std::string> AdvancedDecoder::extract_key_concepts(const std::string& prompt) {
    std::vector<std::string> concepts;
    std::istringstream stream(prompt);
    std::string word;
    
    while (stream >> word) {
        if (word.length() > 3) {
            concepts.push_back(word);
        }
    }
    
    return concepts;
}

std::vector<std::string> AdvancedDecoder::traverse_concept_path(
    const std::vector<std::string>& start_concepts, int depth) {
    std::vector<std::string> path;
    
    if (!graph_ptr) return path;
    
    for (const auto& concept : start_concepts) {
        auto traversed = graph_ptr->bfs_traverse(concept, depth);
        path.insert(path.end(), traversed.begin(), traversed.end());
    }
    
    return path;
}

std::string AdvancedDecoder::concepts_to_text(const std::vector<std::string>& concepts) {
    std::string text;
    if (!graph_ptr) return "AdvancedDecoder: Knowledge graph not available to convert concepts to text.";
    
    for (size_t i = 0; i < concepts.size(); ++i) {
        const GraphNode* node = graph_ptr->get_node(concepts[i]);
        if (node) {
            text += node->label;
        } else {
            text += concepts[i]; // Fallback to ID if label not found
        }
        if (i < concepts.size() - 1) text += " ";
    }
    return text;
}

std::string AdvancedDecoder::generate_from_knowledge(const std::string& prompt, int max_tokens,
                                                     float temperature) {
    auto concepts = extract_key_concepts(prompt);
    auto path = traverse_concept_path(concepts, 3);
    // Max_tokens and temperature are currently unused here, as this is a graph traversal.
    // They would be used if this function also leveraged a sequence generator.
    return concepts_to_text(path);
}

std::string AdvancedDecoder::generate_from_concepts(const std::vector<std::string>& concepts,
                                                    int max_tokens) {
    // Max_tokens is currently unused here, as this is a graph traversal based on concepts.
    return concepts_to_text(concepts);
}

std::string AdvancedDecoder::traverse_and_generate(const Embedding& start_embedding, int depth) {
    std::vector<std::string> path;
    
    if (!graph_ptr) return "AdvancedDecoder: Knowledge graph not available for traversal.";
    
    auto related = graph_ptr->find_related_concepts(start_embedding, "", 5);
    for (const auto& [id, _] : related) {
        auto traversed = graph_ptr->bfs_traverse(id, depth);
        path.insert(path.end(), traversed.begin(), traversed.end());
    }
    
    return concepts_to_text(path);
}
