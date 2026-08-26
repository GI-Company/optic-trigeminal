#include "bm25_index.h"
#include <cmath>
#include <cctype>
#include <algorithm>
#include <unordered_map>

namespace {

// Canonicalizes a handful of clinical adjective/noun word-form pairs to one
// shared term (e.g. "septic" and "sepsis" both index/query as "sepsis") so
// BM25 term-overlap matching -- which is exact-string, no stemming --
// doesn't miss a document just because it used a different word form than
// the query. Applied uniformly to both indexing and querying (both go
// through this same function), and to OpticEmbedder's bag-of-words input
// too (see neural_components.cpp's embed(), which calls this directly) --
// one canonicalization point, so index/query/embedding all agree.
//
// Deliberately small and hand-picked, not a general stemmer: every pair
// here is the same clinical concept in a different grammatical form, not
// an inferred or approximate relationship, and each entry is a real,
// measured gap (medical_o1_and_wiki.jsonl alone has ~110 "septic"-only and
// ~140 "sepsis"-only training examples that would otherwise not cross-match).
//
// Deliberately EXCLUDES negated/antonym forms that a naive stemmer would
// wrongly conflate with their root -- "afebrile" (without fever) must
// never canonicalize to "fever", and "normotensive"/"normoxic" (normal BP /
// normal oxygenation) must never canonicalize to "hypotension"/"hypoxia".
// Getting one of those wrong would silently invert a query's meaning, not
// just miss a match, which is exactly the kind of correctness risk a
// hand-picked list is meant to avoid versus algorithmic stemming.
//
// This only affects which documents a lexical/embedding search can find --
// it never rewrites or fabricates the actual retrieved text.
const std::unordered_map<std::string, std::string>& clinical_term_canon() {
    static const std::unordered_map<std::string, std::string> table = {
        {"septic", "sepsis"},
        {"hypoxic", "hypoxia"},
        {"hypoxemic", "hypoxia"},
        {"hypoxemia", "hypoxia"},
        {"tachycardic", "tachycardia"},
        {"bradycardic", "bradycardia"},
        {"hypotensive", "hypotension"},
        {"hypertensive", "hypertension"},
        {"tachypneic", "tachypnea"},
        {"tachypnoeic", "tachypnea"},
        {"febrile", "fever"},
        {"pyrexia", "fever"},
        {"pyrexial", "fever"},
    };
    return table;
}

} // namespace

std::map<std::string, int> BM25Index::tokenize_and_count(const std::string& text) {
    std::map<std::string, int> counts;
    const auto& canon = clinical_term_canon();

    auto emit = [&](std::string& word) {
        if (word.empty()) return;
        auto it = canon.find(word);
        counts[it != canon.end() ? it->second : word]++;
        word.clear();
    };

    std::string word;
    for (unsigned char c : text) {
        if (std::isalnum(c)) {
            word += static_cast<char>(std::tolower(c));
        } else {
            emit(word);
        }
    }
    emit(word);
    return counts;
}

namespace {
// Shared by index_document (re-indexing an existing doc_id must first
// undo its old contribution) and remove_document. Assumes the caller
// already holds the index's mutex -- std::mutex isn't recursive, so this
// can't just call the public remove_document from inside index_document.
void erase_locked(const std::string& doc_id,
                   std::unordered_map<std::string, std::vector<std::pair<std::string, int>>>& inverted,
                   std::unordered_map<std::string, int>& doc_lengths,
                   std::unordered_map<std::string, int>& df,
                   size_t& total_docs,
                   double& total_length) {
    auto len_it = doc_lengths.find(doc_id);
    if (len_it == doc_lengths.end()) return;

    total_length -= len_it->second;
    doc_lengths.erase(len_it);
    total_docs = total_docs > 0 ? total_docs - 1 : 0;

    for (auto& [term, postings] : inverted) {
        auto pos = std::find_if(postings.begin(), postings.end(),
                                 [&](const auto& p) { return p.first == doc_id; });
        if (pos != postings.end()) {
            postings.erase(pos);
            auto df_it = df.find(term);
            if (df_it != df.end()) {
                df_it->second = std::max(0, df_it->second - 1);
            }
        }
    }
}
}  // namespace

void BM25Index::index_document(const std::string& doc_id, const std::map<std::string, int>& term_counts) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Re-indexing (a node's content changed since it was first added):
    // undo the old entry's contribution first so df_/total_length_ don't
    // double-count.
    erase_locked(doc_id, inverted_, doc_lengths_, df_, total_docs_, total_length_);

    int doc_len = 0;
    for (const auto& [term, count] : term_counts) {
        doc_len += count;
        inverted_[term].push_back({doc_id, count});
        df_[term]++;
    }
    doc_lengths_[doc_id] = doc_len;
    total_length_ += doc_len;
    total_docs_++;
}

void BM25Index::remove_document(const std::string& doc_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    erase_locked(doc_id, inverted_, doc_lengths_, df_, total_docs_, total_length_);
}

void BM25Index::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    inverted_.clear();
    doc_lengths_.clear();
    df_.clear();
    total_docs_ = 0;
    total_length_ = 0.0;
}

float BM25Index::idf(const std::string& term) const {
    auto it = df_.find(term);
    int df = (it != df_.end()) ? it->second : 0;
    float n = static_cast<float>(total_docs_);
    // Standard Robertson-Sparck Jones smoothed IDF -- the "+1" inside the
    // log keeps this non-negative even for a term present in every
    // document (unlike the plain log(N/df) form, which goes negative
    // there and would penalize a common-but-relevant term).
    return std::log((n - df + 0.5f) / (df + 0.5f) + 1.0f);
}

std::vector<std::pair<std::string, float>> BM25Index::search(const std::string& query_text, int top_k) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, float>> results;
    if (total_docs_ == 0) return results;

    auto query_terms = tokenize_and_count(query_text);
    if (query_terms.empty()) return results;

    float avg_dl = static_cast<float>(total_length_ / static_cast<double>(total_docs_));
    if (avg_dl <= 0.0f) avg_dl = 1.0f;

    std::unordered_map<std::string, float> scores;
    for (const auto& [term, _] : query_terms) {
        auto inv_it = inverted_.find(term);
        if (inv_it == inverted_.end()) continue;
        float term_idf = idf(term);
        for (const auto& [doc_id, tf] : inv_it->second) {
            auto dl_it = doc_lengths_.find(doc_id);
            float dl = (dl_it != doc_lengths_.end()) ? static_cast<float>(dl_it->second) : avg_dl;
            float numerator = static_cast<float>(tf) * (k1_ + 1.0f);
            float denominator = static_cast<float>(tf) + k1_ * (1.0f - b_ + b_ * (dl / avg_dl));
            scores[doc_id] += term_idf * (numerator / denominator);
        }
    }

    results.assign(scores.begin(), scores.end());
    std::sort(results.begin(), results.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (results.size() > static_cast<size_t>(top_k)) {
        results.resize(top_k);
    }
    return results;
}

size_t BM25Index::document_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_docs_;
}
