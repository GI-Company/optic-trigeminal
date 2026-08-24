#pragma once

// Real Okapi BM25 (k1=1.2, b=0.75 -- standard defaults, tunable, not magic
// numbers), zero external dependencies. Shared by OpticTrigeminal (chat
// query path, include/neural_components.h) and RAGDAGSystem (clinical
// reasoning path, include/rag_dag.h) so both retrieval subsystems use the
// same real, training-free lexical relevance signal instead of each
// re-implementing it -- see docs/plans for why: OpticEmbedder's neural
// embedding is randomly initialized and never trained before live use, so
// cosine similarity over it alone is closer to noise than genuine semantic
// relevance at corpus scale.

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <mutex>

class BM25Index {
public:
    BM25Index(float k1 = 1.2f, float b = 0.75f) : k1_(k1), b_(b) {}

    // term_counts: term -> count within this document (e.g. GraphNode::term_counts).
    // Re-indexing the same doc_id replaces its previous entry.
    void index_document(const std::string& doc_id, const std::map<std::string, int>& term_counts);
    void remove_document(const std::string& doc_id);

    // Resets the index to empty. For callers that rebuild their whole
    // corpus from scratch (e.g. RAGDAGSystem::initialize_from_knowledge_graph)
    // rather than incrementally adding/removing individual documents --
    // BM25Index holds a std::mutex so it can't be copy/move-assigned to a
    // fresh instance instead.
    void clear();

    // Tokenizes query_text with the same rule index_document's callers use
    // (lowercase alphanumeric spans -- see tokenize_and_count), scores
    // every document sharing at least one term via real Okapi BM25, and
    // returns the top_k highest-scoring (doc_id, score) pairs, descending.
    std::vector<std::pair<std::string, float>> search(const std::string& query_text, int top_k) const;

    static std::map<std::string, int> tokenize_and_count(const std::string& text);

    size_t document_count() const;

private:
    float k1_;
    float b_;

    // term -> [(doc_id, term_frequency_in_doc)]
    std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> inverted_;
    std::unordered_map<std::string, int> doc_lengths_;
    std::unordered_map<std::string, int> df_;  // term -> number of docs containing it
    size_t total_docs_ = 0;
    double total_length_ = 0.0;  // sum of doc_lengths_, for avg_dl

    mutable std::mutex mutex_;

    float idf(const std::string& term) const;  // caller holds mutex_
};
