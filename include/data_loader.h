#pragma once

#include "types.h"
#include <map>
#include <queue>
#include <regex>

class BPETokenizer {
private:
    std::map<std::string, int> subword_vocab;
    std::map<std::pair<std::string, std::string>, int> pair_frequencies;
    int vocab_size;
    
public:
    BPETokenizer(int max_vocab = 1000000);
    
    std::vector<std::string> tokenize(const std::string& text);
    void build_from_text(const std::string& text, int target_vocab_size);
    const std::map<std::string, int>& get_vocabulary() const { return subword_vocab; }
    int get_vocab_size() const { return subword_vocab.size(); }
};

class DataLoader {
public:
    struct DataStats {
        int files_processed;
        int records_ingested;
        int records_failed;
        int tokens_added;
        int graph_nodes;
        int graph_edges;
        VectorStr processed_files;
    };
    
private:
    DataStats stats;
    std::map<std::string, int> vocabulary_counts;
    VectorStr all_texts;
    std::vector<TrainingExample> loaded_examples;
    BPETokenizer bpe_tokenizer;
    
    bool load_json_file(const std::string& filepath);
    bool load_jsonl_file(const std::string& filepath);
    bool load_gob_file(const std::string& filepath);
    
    std::vector<std::string> extract_all_texts_from_examples(
        const std::vector<TrainingExample>& examples);
    void update_vocabulary_counts(const std::string& text);
    void extract_all_json_fields(const std::string& json_str);
    
    void traverse_directory(const std::string& dirpath, int depth, int max_depth);
    
public:
    DataLoader();
    
    bool load_datasets(const std::string& data_directory);
    void build_vocabulary(VectorStr& vocab, int max_vocab_size = VOCAB_SIZE);
    
    std::vector<TrainingExample> get_loaded_examples() const;
    DataStats get_stats() const { return stats; }
    VectorStr get_all_texts() const { return all_texts; }
    
    void clear();
};

class Serializer {
public:
    struct SerializationOptions {
        bool include_graph;
        bool include_embeddings;
        bool include_classifier_weights;
        bool compress;
    };
    
    static bool serialize_to_binary(const std::string& filepath,
                                   const TrainingSnapshot& snapshot);
    static bool deserialize_from_binary(const std::string& filepath,
                                       TrainingSnapshot& snapshot);
    
    static std::string serialize_to_json(const TrainingSnapshot& snapshot);
    static bool deserialize_from_json(const std::string& json_str,
                                     TrainingSnapshot& snapshot);
    
private:
    static std::vector<uint8_t> compress_data(const std::vector<uint8_t>& data);
    static std::vector<uint8_t> decompress_data(const std::vector<uint8_t>& data);
};
