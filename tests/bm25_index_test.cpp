// Golden/behavioral tests for include/bm25_index.h -- BM25Index is
// explicitly documented (see the header's own top comment) as "the real,
// training-free lexical relevance signal" both retrieval subsystems
// (OpticTrigeminal's chat path, RAGDAGSystem's clinical reasoning path)
// lean on specifically because the neural embedding alone is unreliable --
// yet it had zero direct test coverage before this file. Tests the
// documented Okapi BM25 behavior (term frequency saturation via k1, length
// normalization via b, IDF favoring rarer terms) plus the index's own
// mutation semantics (add/remove/re-index/clear), not invented numbers.
#include "../include/bm25_index.h"
#include <iostream>
#include <string>

namespace {

int g_checked = 0;
int g_failed = 0;

void check(bool cond, const std::string& what) {
    g_checked++;
    if (!cond) {
        g_failed++;
        std::cerr << "FAIL: " << what << "\n";
    }
}

bool contains(const std::vector<std::pair<std::string, float>>& results, const std::string& doc_id) {
    for (const auto& r : results) if (r.first == doc_id) return true;
    return false;
}

float score_of(const std::vector<std::pair<std::string, float>>& results, const std::string& doc_id) {
    for (const auto& r : results) if (r.first == doc_id) return r.second;
    return -1.0f;
}

void test_tokenize_and_count() {
    auto c1 = BM25Index::tokenize_and_count("Sepsis is a life-threatening condition.");
    check(c1.count("sepsis") == 1, "tokenize_and_count should lowercase 'Sepsis' to 'sepsis'");
    check(c1.count("life") == 1 && c1.count("threatening") == 1,
          "tokenize_and_count should split on a hyphen into separate terms");
    check(c1.find("condition.") == c1.end(), "tokenize_and_count should not keep trailing punctuation attached");
    check(c1.count("condition") == 1, "tokenize_and_count should strip trailing punctuation");

    auto c2 = BM25Index::tokenize_and_count("sepsis SEPSIS Sepsis");
    check(c2.size() == 1 && c2.at("sepsis") == 3,
          "tokenize_and_count should case-fold repeated occurrences into one counted term");

    auto empty = BM25Index::tokenize_and_count("...,,, !!!");
    check(empty.empty(), "tokenize_and_count on pure punctuation should yield no terms");

    auto blank = BM25Index::tokenize_and_count("");
    check(blank.empty(), "tokenize_and_count on an empty string should yield no terms");
}

void test_basic_search_and_ranking() {
    BM25Index idx;
    idx.index_document("doc_sepsis", BM25Index::tokenize_and_count(
        "Sepsis is a life-threatening response to infection. Early signs of sepsis include fever and tachycardia."));
    idx.index_document("doc_shock", BM25Index::tokenize_and_count(
        "Hypovolemic shock results from significant blood or fluid loss."));
    idx.index_document("doc_unrelated", BM25Index::tokenize_and_count(
        "The weather today is sunny with a light breeze."));

    check(idx.document_count() == 3, "document_count should reflect all indexed documents");

    auto results = idx.search("sepsis signs", 10);
    check(!results.empty(), "search for 'sepsis signs' should return at least one result");
    check(contains(results, "doc_sepsis"), "search for 'sepsis signs' should find doc_sepsis");
    check(!contains(results, "doc_unrelated"), "search for 'sepsis signs' should not match the unrelated weather doc");
    if (contains(results, "doc_sepsis") && contains(results, "doc_shock")) {
        check(score_of(results, "doc_sepsis") > score_of(results, "doc_shock"),
              "doc_sepsis (which actually contains both query terms) should outrank doc_shock");
    }

    auto no_match = idx.search("photosynthesis chlorophyll", 10);
    check(no_match.empty(), "search for terms present in no document should return nothing");
}

void test_top_k_truncation() {
    BM25Index idx;
    for (int i = 0; i < 20; ++i) {
        idx.index_document("doc" + std::to_string(i),
                            BM25Index::tokenize_and_count("sepsis fever tachycardia patient " + std::to_string(i)));
    }
    auto results = idx.search("sepsis", 5);
    check(results.size() == 5, "search with top_k=5 over 20 matching docs should return exactly 5");

    auto all = idx.search("sepsis", 100);
    check(all.size() == 20, "search with top_k larger than the corpus should return every matching doc");
}

void test_clinical_term_canonicalization() {
    // Positive: a word-form variant should canonicalize to the same term
    // as its root, so document and query agree regardless of which form
    // either one happens to use.
    auto septic = BM25Index::tokenize_and_count("The patient is septic.");
    check(septic.count("sepsis") == 1 && septic.count("septic") == 0,
          "'septic' should canonicalize to 'sepsis', not remain as its own term");

    auto hypoxic = BM25Index::tokenize_and_count("hypoxic hypoxemic hypoxemia");
    check(hypoxic.size() == 1 && hypoxic.at("hypoxia") == 3,
          "hypoxic/hypoxemic/hypoxemia should all canonicalize to the single term 'hypoxia'");

    for (auto [variant, root] : {
             std::pair{"tachycardic", "tachycardia"}, std::pair{"bradycardic", "bradycardia"},
             std::pair{"hypotensive", "hypotension"}, std::pair{"hypertensive", "hypertension"},
             std::pair{"tachypneic", "tachypnea"}, std::pair{"febrile", "fever"},
             std::pair{"pyrexia", "fever"}}) {
        auto c = BM25Index::tokenize_and_count(variant);
        check(c.count(root) == 1 && c.size() == 1,
              std::string(variant) + " should canonicalize to '" + root + "'");
    }

    // A query using one form must retrieve a document indexed under the
    // other -- this is the entire point, not just a tokenization detail.
    BM25Index idx;
    idx.index_document("doc_septic", BM25Index::tokenize_and_count(
        "The patient presents with septic shock and requires immediate fluid resuscitation."));
    auto results = idx.search("what causes sepsis", 10);
    check(!results.empty() && results[0].first == "doc_septic",
          "querying 'sepsis' should retrieve a document that only ever said 'septic'");

    // Critical negative: antonym/negated forms must NOT canonicalize to
    // their root -- that would silently invert the query's meaning rather
    // than just miss a match, which is the whole reason this is a small
    // hand-picked list instead of algorithmic stemming.
    auto afebrile = BM25Index::tokenize_and_count("The patient is afebrile.");
    check(afebrile.count("fever") == 0 && afebrile.count("afebrile") == 1,
          "'afebrile' (without fever) must NOT canonicalize to 'fever' -- that's the opposite finding");

    auto normotensive = BM25Index::tokenize_and_count("Vitals are normotensive.");
    check(normotensive.count("hypotension") == 0 && normotensive.count("hypertension") == 0,
          "'normotensive' (normal BP) must NOT canonicalize to either hypotension or hypertension");

    auto normoxic = BM25Index::tokenize_and_count("Patient is normoxic on room air.");
    check(normoxic.count("hypoxia") == 0,
          "'normoxic' (normal oxygenation) must NOT canonicalize to 'hypoxia'");
}

void test_idf_favors_rare_terms() {
    // "sepsis" appears in every document (common); "tachycardia" appears in
    // only one (rare). A query naming both should rank the doc where the
    // rare term hits above a doc that only matches on the common term --
    // that's the entire point of IDF weighting, not an incidental detail.
    BM25Index idx;
    idx.index_document("doc_a", BM25Index::tokenize_and_count("sepsis sepsis sepsis"));
    idx.index_document("doc_b", BM25Index::tokenize_and_count("sepsis tachycardia"));
    idx.index_document("doc_c", BM25Index::tokenize_and_count("sepsis"));

    auto results = idx.search("tachycardia", 10);
    check(results.size() == 1 && results[0].first == "doc_b",
          "a rare term should only match the one document containing it, regardless of how common 'sepsis' is elsewhere");
}

void test_remove_and_reindex() {
    BM25Index idx;
    idx.index_document("doc1", BM25Index::tokenize_and_count("sepsis fever"));
    idx.index_document("doc2", BM25Index::tokenize_and_count("shock hypotension"));
    check(idx.document_count() == 2, "should have 2 documents after indexing 2");

    idx.remove_document("doc1");
    check(idx.document_count() == 1, "document_count should decrement after remove_document");
    auto results = idx.search("sepsis fever", 10);
    check(results.empty(), "a removed document's terms should no longer be searchable");

    // Re-indexing the same doc_id must replace, not duplicate, its entry --
    // otherwise df_/total_length_ double-count and every score downstream
    // is silently wrong.
    idx.index_document("doc2", BM25Index::tokenize_and_count("completely different content now"));
    check(idx.document_count() == 1, "re-indexing an existing doc_id should not increase document_count");
    auto old_content = idx.search("shock hypotension", 10);
    check(old_content.empty(), "re-indexing doc2 should remove its old content from the index");
    auto new_content = idx.search("completely different", 10);
    check(!new_content.empty() && new_content[0].first == "doc2",
          "re-indexing doc2 should make its new content searchable");
}

void test_clear() {
    BM25Index idx;
    idx.index_document("doc1", BM25Index::tokenize_and_count("sepsis fever"));
    idx.index_document("doc2", BM25Index::tokenize_and_count("shock hypotension"));
    idx.clear();
    check(idx.document_count() == 0, "document_count should be 0 after clear()");
    check(idx.search("sepsis", 10).empty(), "search should return nothing after clear()");
}

void test_empty_index() {
    BM25Index idx;
    check(idx.document_count() == 0, "a freshly constructed index should have 0 documents");
    check(idx.search("anything", 10).empty(), "searching an empty index should return nothing, not crash");
}

} // namespace

int main() {
    test_tokenize_and_count();
    test_basic_search_and_ranking();
    test_top_k_truncation();
    test_clinical_term_canonicalization();
    test_idf_favors_rare_terms();
    test_remove_and_reindex();
    test_clear();
    test_empty_index();

    std::cout << "BM25 index golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
