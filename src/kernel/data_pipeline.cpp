#include "data_pipeline.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <chrono>
#include <numeric>
#include <random>

TokenizerDeterministic::TokenizerDeterministic() : vocab_size(10000) {
    build_vocabulary();
}

void TokenizerDeterministic::build_vocabulary() {
    std::vector<std::string> common_words = {
        "the", "a", "is", "in", "to", "of", "and", "or", "not", "for",
        "be", "have", "do", "say", "get", "make", "know", "take", "see", "come",
        "think", "use", "find", "give", "tell", "work", "call", "try", "ask", "need",
        "feel", "become", "leave", "put", "mean", "keep", "let", "begin", "seem", "help",
        "talk", "turn", "start", "show", "hear", "play", "run", "move", "like", "live",
        "believe", "hold", "bring", "happen", "write", "provide", "sit", "stand", "lose", "pay",
        "meet", "include", "continue", "set", "learn", "change", "lead", "understand", "watch", "follow",
        "stop", "create", "speak", "read", "allow", "add", "spend", "grow", "open", "walk",
        "win", "offer", "remember", "love", "consider", "appear", "buy", "wait", "serve", "die",
        "send", "expect", "build", "stay", "fall", "cut", "reach", "kill", "remain", "suggest"
    };
    
    int id = 0;
    for (const auto& word : common_words) {
        word_to_id[word] = id;
        id_to_word[id] = word;
        id++;
    }
    
    while (id < vocab_size) {
        id_to_word[id] = "token_" + std::to_string(id);
        id++;
    }
}

std::vector<int> TokenizerDeterministic::tokenize(const std::string& text) {
    std::vector<int> tokens;
    std::istringstream iss(text);
    std::string word;
    
    while (iss >> word) {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        
        if (!word.empty() && (word.back() == '.' || word.back() == ',' || word.back() == '!')) {
            word.pop_back();
        }
        
        auto it = word_to_id.find(word);
        if (it != word_to_id.end()) {
            tokens.push_back(it->second);
        } else {
            unsigned int hash = 0;
            for (char c : word) {
                hash = hash * 31 + c;
            }
            tokens.push_back((hash % (vocab_size - 200)) + 100);
        }
    }
    
    return tokens;
}

std::string TokenizerDeterministic::detokenize(const std::vector<int>& tokens) {
    std::stringstream ss;
    for (size_t i = 0; i < tokens.size(); ++i) {
        auto it = id_to_word.find(tokens[i]);
        if (it != id_to_word.end()) {
            ss << it->second;
        } else {
            ss << "unk_" << tokens[i];
        }
        if (i < tokens.size() - 1) ss << " ";
    }
    return ss.str();
}

std::vector<int> TokenizerDeterministic::encode_stem_symbolic(const std::string& text) {
    std::vector<int> encoded;
    
    for (char c : text) {
        encoded.push_back(static_cast<int>(c));
    }
    
    return encoded;
}

FeatureExtractor::FeatureExtractor() {}

std::vector<std::string> FeatureExtractor::extract_features(const std::string& text, DataSourceType source) {
    std::vector<std::string> features;
    
    switch (source) {
        case DataSourceType::STEM_QA:
            features = extract_stem_features(text);
            break;
        case DataSourceType::CODING_FUNDAMENTALS:
            features = extract_coding_features(text);
            break;
        case DataSourceType::LOGIC_REASONING:
            features = extract_reasoning_features(text);
            break;
        default:
            features.push_back("generic_text");
            break;
    }
    
    auto keywords = extract_keywords(text);
    features.insert(features.end(), keywords.begin(), keywords.end());
    
    return features;
}

std::map<std::string, float> FeatureExtractor::compute_feature_scores(const std::vector<std::string>& features) {
    std::map<std::string, float> scores;
    
    for (const auto& feature : features) {
        scores[feature] = 1.0f / (features.size() + 1);
    }
    
    return scores;
}

Embedding FeatureExtractor::text_to_embedding(const std::string& text) {
    Embedding embedding(EMBEDDING_DIM);
    
    size_t hash = 0;
    for (size_t idx = 0; idx < text.length(); ++idx) {
        hash ^= std::hash<char>{}(text[idx]) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    }
    
    std::mt19937 rng(static_cast<unsigned int>(hash));
    std::normal_distribution<float> dist(0.0f, 0.1f);
    
    for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
        embedding.values[i] = dist(rng);
    }
    
    return embedding;
}

std::vector<std::string> FeatureExtractor::extract_keywords(const std::string& text) {
    std::vector<std::string> keywords;
    std::istringstream iss(text);
    std::string word;
    
    while (iss >> word && keywords.size() < 10) {
        if (word.length() > 4) {
            keywords.push_back("keyword_" + word.substr(0, 3));
        }
    }
    
    return keywords;
}

std::vector<std::string> FeatureExtractor::extract_entities(const std::string& text) {
    std::vector<std::string> entities;
    
    if (text.find("name") != std::string::npos) {
        entities.push_back("entity_name");
    }
    if (text.find("number") != std::string::npos || text.find("count") != std::string::npos) {
        entities.push_back("entity_quantity");
    }
    if (text.find("location") != std::string::npos || text.find("place") != std::string::npos) {
        entities.push_back("entity_location");
    }
    
    return entities;
}

std::vector<std::string> FeatureExtractor::extract_stem_features(const std::string& text) {
    std::vector<std::string> features;
    features.push_back("stem_domain");
    
    if (text.find("math") != std::string::npos) {
        features.push_back("domain_mathematics");
    }
    if (text.find("physics") != std::string::npos) {
        features.push_back("domain_physics");
    }
    if (text.find("chemistry") != std::string::npos) {
        features.push_back("domain_chemistry");
    }
    if (text.find("biology") != std::string::npos) {
        features.push_back("domain_biology");
    }
    
    return features;
}

std::vector<std::string> FeatureExtractor::extract_coding_features(const std::string& text) {
    std::vector<std::string> features;
    features.push_back("coding_fundamental");
    
    if (text.find("function") != std::string::npos) {
        features.push_back("feature_function");
    }
    if (text.find("loop") != std::string::npos) {
        features.push_back("feature_loop");
    }
    if (text.find("variable") != std::string::npos) {
        features.push_back("feature_variable");
    }
    if (text.find("class") != std::string::npos) {
        features.push_back("feature_oop");
    }
    
    return features;
}

std::vector<std::string> FeatureExtractor::extract_reasoning_features(const std::string& text) {
    std::vector<std::string> features;
    features.push_back("reasoning_logic");
    
    if (text.find("if") != std::string::npos) {
        features.push_back("feature_conditional");
    }
    if (text.find("therefore") != std::string::npos) {
        features.push_back("feature_deduction");
    }
    
    return features;
}

DeduplicationEngine::DeduplicationEngine() {}

DeduplicationResult DeduplicationEngine::find_duplicates(const RawDataRecord& record,
                                                         const std::vector<RawDataRecord>& existing_records,
                                                         float similarity_threshold) {
    DeduplicationResult result;
    result.canonical_record_id = record.record_id;
    result.similarity_score = 0.0f;
    
    for (const auto& existing : existing_records) {
        float similarity = compute_similarity(record.input_text, existing.input_text);
        if (similarity > result.similarity_score) {
            result.similarity_score = similarity;
        }
        
        if (similarity > similarity_threshold) {
            result.duplicate_ids.push_back(existing.record_id);
        }
    }
    
    return result;
}

float DeduplicationEngine::compute_similarity(const std::string& text1, const std::string& text2) {
    if (text1.empty() || text2.empty()) return 0.0f;
    
    float lev_sim = levenshtein_similarity(text1, text2);
    return lev_sim;
}

float DeduplicationEngine::compute_embedding_similarity(const Embedding& emb1, const Embedding& emb2) {
    float dot_product = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;
    
    for (size_t i = 0; i < EMBEDDING_DIM; ++i) {
        dot_product += emb1.values[i] * emb2.values[i];
        norm1 += emb1.values[i] * emb1.values[i];
        norm2 += emb2.values[i] * emb2.values[i];
    }
    
    norm1 = std::sqrt(norm1);
    norm2 = std::sqrt(norm2);
    
    if (norm1 < 1e-6f || norm2 < 1e-6f) return 0.0f;
    
    return dot_product / (norm1 * norm2);
}

std::set<std::string> DeduplicationEngine::deduplicate_batch(const std::vector<RawDataRecord>& records,
                                                            float threshold) {
    std::set<std::string> unique_records;
    std::set<std::string> seen_hashes;
    
    for (const auto& record : records) {
        std::string hash = compute_hash(record.input_text + record.output_text);
        
        if (seen_hashes.find(hash) == seen_hashes.end()) {
            unique_records.insert(record.record_id);
            seen_hashes.insert(hash);
        }
    }
    
    return unique_records;
}

std::string DeduplicationEngine::compute_hash(const std::string& content) const {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t prime = 0x100000001b3ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= prime;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

float DeduplicationEngine::levenshtein_similarity(const std::string& s1, const std::string& s2) {
    size_t len1 = s1.length();
    size_t len2 = s2.length();
    
    if (len1 == 0) return len2 == 0 ? 1.0f : 0.0f;
    if (len2 == 0) return 0.0f;
    
    std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1, 0));
    
    for (size_t i = 0; i <= len1; ++i) d[i][0] = i;
    for (size_t j = 0; j <= len2; ++j) d[0][j] = j;
    
    for (size_t i = 1; i <= len1; ++i) {
        for (size_t j = 1; j <= len2; ++j) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
        }
    }
    
    int distance = d[len1][len2];
    int max_len = std::max(len1, len2);
    
    return 1.0f - (static_cast<float>(distance) / max_len);
}

DataValidator::DataValidator() 
    : max_input_length(10000), max_output_length(5000), min_input_length(5) {}

bool DataValidator::validate_record(const RawDataRecord& record) {
    if (record.input_text.empty() || record.output_text.empty()) return false;
    if (record.input_text.length() < min_input_length) return false;
    if (record.input_text.length() > max_input_length) return false;
    if (record.output_text.length() > max_output_length) return false;
    
    return true;
}

bool DataValidator::validate_processed_record(const ProcessedDataRecord& record) {
    if (record.input_tokens.empty() || record.output_tokens.empty()) return false;
    if (record.input_tokens.size() > 1000) return false;
    if (record.output_tokens.size() > 500) return false;
    
    return true;
}

std::vector<std::string> DataValidator::get_validation_errors(const RawDataRecord& record) {
    std::vector<std::string> errors;
    
    if (record.input_text.empty()) errors.push_back("empty_input");
    if (record.output_text.empty()) errors.push_back("empty_output");
    if (record.input_text.length() < min_input_length) errors.push_back("input_too_short");
    if (record.input_text.length() > max_input_length) errors.push_back("input_too_long");
    if (record.output_text.length() > max_output_length) errors.push_back("output_too_long");
    
    return errors;
}

DataQualityMetrics DataValidator::assess_batch_quality(const std::vector<ProcessedDataRecord>& records) {
    DataQualityMetrics metrics;
    metrics.total_records = records.size();
    metrics.valid_records = 0;
    metrics.malformed_records = 0;
    
    float total_input_len = 0.0f;
    float total_output_len = 0.0f;
    
    for (const auto& record : records) {
        if (validate_processed_record(record)) {
            metrics.valid_records++;
        } else {
            metrics.malformed_records++;
        }
        
        total_input_len += record.input_tokens.size();
        total_output_len += record.output_tokens.size();
    }
    
    if (metrics.total_records > 0) {
        metrics.average_input_length = total_input_len / metrics.total_records;
        metrics.average_output_length = total_output_len / metrics.total_records;
        metrics.quality_score = static_cast<float>(metrics.valid_records) / metrics.total_records;
    }
    
    return metrics;
}

bool DataValidator::passes_quality_threshold(const ProcessedDataRecord& record, float min_quality) {
    if (!validate_processed_record(record)) return false;
    
    float feature_coverage = 0.0f;
    if (!record.feature_scores.empty()) {
        for (const auto& score : record.feature_scores) {
            feature_coverage += score.second;
        }
        feature_coverage /= record.feature_scores.size();
    }
    
    return feature_coverage >= min_quality;
}

DataPipeline::DataPipeline()
    : tokenizer(std::make_unique<TokenizerDeterministic>()),
      feature_extractor(std::make_unique<FeatureExtractor>()),
      deduplication_engine(std::make_unique<DeduplicationEngine>()),
      validator(std::make_unique<DataValidator>()) {}

void DataPipeline::ingest_raw_records(const std::vector<RawDataRecord>& records) {
    for (auto record : records) {
        normalize_record(record);
        if (validator->validate_record(record)) {
            raw_records.push_back(record);
        }
    }
}

void DataPipeline::process_batch(std::vector<ProcessedDataRecord>& output) {
    output.clear();
    
    for (const auto& raw : raw_records) {
        auto processed = process_single_record(raw);
        if (validator->validate_processed_record(processed)) {
            processed_records.push_back(processed);
            output.push_back(processed);
        }
    }
}

void DataPipeline::deduplicate_records(float threshold) {
    auto unique_ids = deduplication_engine->deduplicate_batch(raw_records, threshold);
    
    std::vector<RawDataRecord> dedup_records;
    for (const auto& record : raw_records) {
        if (unique_ids.find(record.record_id) != unique_ids.end()) {
            dedup_records.push_back(record);
        }
    }
    
    raw_records = dedup_records;
}

void DataPipeline::extract_features_all() {
    for (auto& record : processed_records) {
        record.extracted_features = feature_extractor->extract_features(
            tokenizer->detokenize(record.input_tokens),
            record.source_type
        );
        record.feature_scores = feature_extractor->compute_feature_scores(record.extracted_features);
    }
}

void DataPipeline::validate_all_records() {
    for (auto& record : processed_records) {
        record.is_valid = validator->validate_processed_record(record);
    }
}

std::vector<ProcessedDataRecord> DataPipeline::get_processed_records(DataSourceType source_type) {
    std::vector<ProcessedDataRecord> result;
    for (const auto& record : processed_records) {
        if (record.source_type == source_type) {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<ProcessedDataRecord> DataPipeline::get_valid_records_only() {
    std::vector<ProcessedDataRecord> result;
    for (const auto& record : processed_records) {
        if (record.is_valid) {
            result.push_back(record);
        }
    }
    return result;
}

DataQualityMetrics DataPipeline::get_quality_metrics() {
    return validator->assess_batch_quality(processed_records);
}

std::string DataPipeline::generate_pipeline_report() const {
    std::stringstream ss;
    ss << "=== Data Pipeline Report ===\n";
    ss << "Total Raw Records Ingested: " << raw_records.size() << "\n";
    ss << "Total Processed Records: " << processed_records.size() << "\n";
    ss << "Total Valid Records: " << get_total_valid() << "\n";
    
    int stem_count = 0, coding_count = 0, logic_count = 0;
    for (const auto& record : processed_records) {
        if (record.source_type == DataSourceType::STEM_QA) stem_count++;
        if (record.source_type == DataSourceType::CODING_FUNDAMENTALS) coding_count++;
        if (record.source_type == DataSourceType::LOGIC_REASONING) logic_count++;
    }
    
    ss << "\nRecords by Type:\n";
    ss << "  STEM QA: " << stem_count << "\n";
    ss << "  Coding: " << coding_count << "\n";
    ss << "  Logic: " << logic_count << "\n";
    
    return ss.str();
}

int DataPipeline::get_total_valid() const {
    int count = 0;
    for (const auto& record : processed_records) {
        if (record.is_valid) count++;
    }
    return count;
}

void DataPipeline::reset() {
    raw_records.clear();
    processed_records.clear();
    deduplicated_hashes.clear();
}

void DataPipeline::normalize_record(RawDataRecord& record) {
    std::transform(record.input_text.begin(), record.input_text.end(),
                  record.input_text.begin(), ::tolower);
    record.content_hash = compute_hash(record.input_text + record.output_text);
}

ProcessedDataRecord DataPipeline::process_single_record(const RawDataRecord& record) {
    ProcessedDataRecord processed;
    processed.record_id = "proc_" + record.record_id;
    processed.source_record_id = record.record_id;
    processed.source_type = record.source_type;
    processed.processed_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    // Preserve original text
    processed.input_text = record.input_text;
    processed.output_text = record.output_text;
    
    processed.input_tokens = tokenizer->tokenize(record.input_text);
    processed.output_tokens = tokenizer->tokenize(record.output_text);
    
    processed.input_embedding = feature_extractor->text_to_embedding(record.input_text);
    processed.output_embedding = feature_extractor->text_to_embedding(record.output_text);
    
    processed.is_valid = validator->validate_processed_record(processed);
    
    return processed;
}

std::string DataPipeline::compute_hash(const std::string& content) const {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t prime = 0x100000001b3ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= prime;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}
