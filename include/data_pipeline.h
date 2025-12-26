#pragma once

#include "types.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>

enum class DataSourceType {
    STEM_QA = 0,
    CODING_FUNDAMENTALS = 1,
    TOOLING_ORCHESTRATION = 2,
    LOGIC_REASONING = 3,
    SELF_KNOWLEDGE = 4,
    MULTIMODAL = 5,
    GENERIC = 6
};

struct RawDataRecord {
    std::string record_id;
    DataSourceType source_type;
    std::string input_text;
    std::string output_text;
    std::map<std::string, std::string> metadata;
    std::string content_hash;
    int64_t ingested_at;
    
    RawDataRecord() : ingested_at(0) {}
};

struct ProcessedDataRecord {
    std::string record_id;
    std::string source_record_id;
    DataSourceType source_type;
    std::vector<int> input_tokens;
    std::vector<int> output_tokens;
    Embedding input_embedding;
    Embedding output_embedding;
    std::vector<std::string> extracted_features;
    std::map<std::string, float> feature_scores;
    bool is_valid;
    int64_t processed_at;
    
    // Original text content preserved for training components that require it
    std::string input_text;
    std::string output_text;
    
    ProcessedDataRecord() : is_valid(false), processed_at(0), 
                            input_embedding(EMBEDDING_DIM),
                            output_embedding(EMBEDDING_DIM) {}
};

struct DataQualityMetrics {
    int total_records;
    int valid_records;
    int duplicate_records;
    int malformed_records;
    float average_input_length;
    float average_output_length;
    float sparsity_ratio;
    float quality_score;
    
    DataQualityMetrics() : total_records(0), valid_records(0), duplicate_records(0),
                           malformed_records(0), average_input_length(0.0f),
                           average_output_length(0.0f), sparsity_ratio(0.0f),
                           quality_score(0.0f) {}
};

struct DeduplicationResult {
    std::string canonical_record_id;
    std::vector<std::string> duplicate_ids;
    float similarity_score;
    
    DeduplicationResult() : similarity_score(0.0f) {}
};

class TokenizerDeterministic {
public:
    TokenizerDeterministic();
    
    std::vector<int> tokenize(const std::string& text);
    
    std::string detokenize(const std::vector<int>& tokens);
    
    int get_vocab_size() const { return vocab_size; }
    
    static std::vector<int> encode_stem_symbolic(const std::string& text);
    
private:
    int vocab_size;
    std::map<std::string, int> word_to_id;
    std::map<int, std::string> id_to_word;
    
    void build_vocabulary();
};

class FeatureExtractor {
public:
    FeatureExtractor();
    
    std::vector<std::string> extract_features(const std::string& text, DataSourceType source);
    
    std::map<std::string, float> compute_feature_scores(const std::vector<std::string>& features);
    
    Embedding text_to_embedding(const std::string& text);
    
    std::vector<std::string> extract_keywords(const std::string& text);
    
    std::vector<std::string> extract_entities(const std::string& text);
    
private:
    std::vector<std::string> extract_stem_features(const std::string& text);
    std::vector<std::string> extract_coding_features(const std::string& text);
    std::vector<std::string> extract_reasoning_features(const std::string& text);
};

class DeduplicationEngine {
public:
    DeduplicationEngine();
    
    DeduplicationResult find_duplicates(const RawDataRecord& record,
                                       const std::vector<RawDataRecord>& existing_records,
                                       float similarity_threshold = 0.95f);
    
    float compute_similarity(const std::string& text1, const std::string& text2);
    
    float compute_embedding_similarity(const Embedding& emb1, const Embedding& emb2);
    
    std::set<std::string> deduplicate_batch(const std::vector<RawDataRecord>& records,
                                           float threshold = 0.95f);
    
private:
    std::string compute_hash(const std::string& content) const;
    float levenshtein_similarity(const std::string& s1, const std::string& s2);
};

class DataValidator {
public:
    DataValidator();
    
    bool validate_record(const RawDataRecord& record);
    
    bool validate_processed_record(const ProcessedDataRecord& record);
    
    std::vector<std::string> get_validation_errors(const RawDataRecord& record);
    
    DataQualityMetrics assess_batch_quality(const std::vector<ProcessedDataRecord>& records);
    
    bool passes_quality_threshold(const ProcessedDataRecord& record, float min_quality = 0.7f);
    
private:
    int max_input_length;
    int max_output_length;
    int min_input_length;
};

class DataPipeline {
public:
    DataPipeline();
    
    void ingest_raw_records(const std::vector<RawDataRecord>& records);
    
    void process_batch(std::vector<ProcessedDataRecord>& output);
    
    void deduplicate_records(float threshold = 0.95f);
    
    void extract_features_all();
    
    void validate_all_records();
    
    std::vector<ProcessedDataRecord> get_processed_records(DataSourceType source_type);
    
    std::vector<ProcessedDataRecord> get_valid_records_only();
    
    DataQualityMetrics get_quality_metrics();
    
    std::string generate_pipeline_report() const;
    
    int get_total_ingested() const { return raw_records.size(); }
    int get_total_processed() const { return processed_records.size(); }
    int get_total_valid() const;
    
    void reset();

private:
    std::vector<RawDataRecord> raw_records;
    std::vector<ProcessedDataRecord> processed_records;
    std::set<std::string> deduplicated_hashes;
    
    std::unique_ptr<TokenizerDeterministic> tokenizer;
    std::unique_ptr<FeatureExtractor> feature_extractor;
    std::unique_ptr<DeduplicationEngine> deduplication_engine;
    std::unique_ptr<DataValidator> validator;
    
    void normalize_record(RawDataRecord& record);
    ProcessedDataRecord process_single_record(const RawDataRecord& record);
    std::string compute_hash(const std::string& content) const;
};
