#include "data_loader.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <cctype>
#include <algorithm>

class SimpleJSON {
public:
    static std::string extract_value(const std::string& json_str, const std::string& key) {
        size_t pos = json_str.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        
        pos = json_str.find(":", pos);
        if (pos == std::string::npos) return "";
        
        pos = json_str.find("\"", pos);
        if (pos == std::string::npos) return "";
        
        pos++;
        size_t end = json_str.find("\"", pos);
        if (end == std::string::npos) return "";
        
        return json_str.substr(pos, end - pos);
    }
    
    static bool is_valid_json(const std::string& str) {
        return !str.empty() && (str[0] == '{' || str[0] == '[');
    }
};

BPETokenizer::BPETokenizer(int max_vocab) 
    : vocab_size(max_vocab) {
    for (char c = 'a'; c <= 'z'; ++c) {
        subword_vocab[std::string(1, c)] = 1;
    }
    for (char c = 'A'; c <= 'Z'; ++c) {
        subword_vocab[std::string(1, c)] = 1;
    }
    for (char c = '0'; c <= '9'; ++c) {
        subword_vocab[std::string(1, c)] = 1;
    }
    subword_vocab["##"] = 1;
    subword_vocab["##"] = 1;
    for (int i = 32; i < 127; ++i) {
        char c = static_cast<char>(i);
        if (!std::isalnum(c)) {
            subword_vocab[std::string(1, c)] = 1;
        }
    }
}

void BPETokenizer::build_from_text(const std::string& text, int target_vocab_size) {
    std::istringstream stream(text);
    std::string word;
    
    while (stream >> word) {
        for (size_t i = 0; i < word.length(); ++i) {
            if (i < word.length() - 1) {
                std::string bigram = word.substr(i, 2);
                pair_frequencies[std::make_pair(std::string(1, word[i]), std::string(1, word[i+1]))]++;
                subword_vocab[bigram]++;
            }
            if (i < word.length() - 2) {
                std::string trigram = word.substr(i, 3);
                subword_vocab[trigram]++;
            }
        }
    }
}

std::vector<std::string> BPETokenizer::tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    
    std::string current;
    for (char c : text) {
        if (std::isspace(c)) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    
    if (!current.empty()) {
        tokens.push_back(current);
    }
    
    return tokens;
}

DataLoader::DataLoader() {
    stats.files_processed = 0;
    stats.records_ingested = 0;
    stats.records_failed = 0;
    stats.tokens_added = 0;
    stats.graph_nodes = 0;
    stats.graph_edges = 0;
}

bool DataLoader::load_json_file(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        if (!SimpleJSON::is_valid_json(content)) return false;
        
        size_t pos = 0;
        while ((pos = content.find("{", pos)) != std::string::npos) {
            size_t end = content.find("}", pos);
            if (end == std::string::npos) break;
            
            std::string item = content.substr(pos, end - pos + 1);
            
            TrainingExample example;
            
            std::string input = SimpleJSON::extract_value(item, "input");
            std::string output = SimpleJSON::extract_value(item, "output");
            
            if (input.empty() || output.empty()) {
                input = SimpleJSON::extract_value(item, "prompt");
                output = SimpleJSON::extract_value(item, "response");
            }
            
            if (input.empty() || output.empty()) {
                input = SimpleJSON::extract_value(item, "question");
                output = SimpleJSON::extract_value(item, "answer");
            }
            
            if (input.empty() || output.empty()) {
                input = SimpleJSON::extract_value(item, "problem");
                output = SimpleJSON::extract_value(item, "answer");
            }
            
            if (input.empty() || output.empty()) {
                input = SimpleJSON::extract_value(item, "text");
                output = SimpleJSON::extract_value(item, "content");
            }
            
            if (!input.empty() && !output.empty()) {
                example.input = input;
                example.output = output;
                example.domain = SimpleJSON::extract_value(item, "domain");
                
                loaded_examples.push_back(example); // Use member variable
                all_texts.push_back(example.input);
                all_texts.push_back(example.output);
                update_vocabulary_counts(example.input);
                update_vocabulary_counts(example.output);
            }
            pos = end + 1;
        }
        
        std::cout << "[DataLoader] Loaded " << loaded_examples.size() << " records from " << filepath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading JSON file " << filepath << ": " << e.what() << std::endl;
        return false;
    }
}

bool DataLoader::load_jsonl_file(const std::string& filepath) {
    try {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        
        std::string line;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            
            try {
                if (!SimpleJSON::is_valid_json(line)) {
                    stats.records_failed++;
                    continue;
                }
                
                TrainingExample example;
                
                std::string input = SimpleJSON::extract_value(line, "input");
                std::string output = SimpleJSON::extract_value(line, "output");
                
                if (input.empty() || output.empty()) {
                    input = SimpleJSON::extract_value(line, "prompt");
                    output = SimpleJSON::extract_value(line, "response");
                }
                
                if (input.empty() || output.empty()) {
                    input = SimpleJSON::extract_value(line, "question");
                    output = SimpleJSON::extract_value(line, "answer");
                }
                
                if (input.empty() || output.empty()) {
                    input = SimpleJSON::extract_value(line, "problem");
                    output = SimpleJSON::extract_value(line, "answer");
                }
                
                if (input.empty() || output.empty()) {
                    input = SimpleJSON::extract_value(line, "text");
                    output = SimpleJSON::extract_value(line, "content");
                }
                
                if (!input.empty() && !output.empty()) {
                    example.input = input;
                    example.output = output;
                    example.domain = SimpleJSON::extract_value(line, "domain");
                    
                    loaded_examples.push_back(example); // Use member variable
                    all_texts.push_back(example.input);
                    all_texts.push_back(example.output);
                    update_vocabulary_counts(example.input);
                    update_vocabulary_counts(example.output);
                }
            } catch (const std::exception& e) {
                stats.records_failed++;
                continue;
            }
        }
        
        std::cout << "[DataLoader] Loaded " << loaded_examples.size() << " records from " << filepath << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error loading JSONL file " << filepath << ": " << e.what() << std::endl;
        return false;
    }
}

bool DataLoader::load_gob_file(const std::string& filepath) {
    return true;
}

std::vector<std::string> DataLoader::extract_all_texts_from_examples(
    const std::vector<TrainingExample>& examples) {
    std::vector<std::string> texts;
    for (const auto& example : examples) {
        texts.push_back(example.input);
        texts.push_back(example.output);
    }
    return texts;
}

void DataLoader::update_vocabulary_counts(const std::string& text) {
    if (text.empty()) return;
    
    std::istringstream stream(text);
    std::string word;
    
    while (stream >> word) {
        vocabulary_counts[word]++;
    }
}

void DataLoader::extract_all_json_fields(const std::string& json_str) {
    if (!SimpleJSON::is_valid_json(json_str)) return;
    
    std::string current_key;
    bool in_key = false;
    bool in_value = false;
    std::string current_value;
    
    for (size_t i = 0; i < json_str.length(); ++i) {
        char c = json_str[i];
        
        if (c == '"' && (i == 0 || json_str[i-1] != '\\')) {
            if (!in_key && !in_value) {
                in_key = true;
                current_key.clear();
            } else if (in_key) {
                in_key = false;
                if (json_str[i+1] == ':' || 
                    (i+1 < json_str.length() && std::isspace(json_str[i+1]) && 
                     i+2 < json_str.length() && json_str[i+2] == ':')) {
                    in_value = false;
                }
            } else if (in_value) {
                in_value = false;
                update_vocabulary_counts(current_value);
                current_value.clear();
            } else {
                in_value = true;
                current_value.clear();
            }
        } else if (in_key && c != '"') {
            current_key += c;
        } else if (in_value && c != '"') {
            current_value += c;
        }
    }
    
    std::vector<std::string> common_keys = {
        "prompt", "response", "input", "output", "question", "answer",
        "text", "content", "description", "instruction", "explanation",
        "title", "abstract", "summary", "comment", "code", "context",
        "reasoning", "solution", "query", "result", "data", "label",
        "instruction_text", "response_text", "query_text", "answer_text"
    };
    
    for (const auto& key : common_keys) {
        std::string value = SimpleJSON::extract_value(json_str, key);
        if (!value.empty()) {
            update_vocabulary_counts(value);
        }
    }
}

void DataLoader::traverse_directory(const std::string& dirpath, int depth, int max_depth) {
    if (depth > max_depth) return;
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dirpath)) {
            std::cout << "[DataLoader] Processing: " << entry.path().string() << std::endl;
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                std::string ext = entry.path().extension().string();
                
                if (ext == ".json") {
                    if (load_json_file(path)) {
                        stats.files_processed++;
                        stats.processed_files.push_back(path);
                    }
                } else if (ext == ".jsonl") {
                    if (load_jsonl_file(path)) {
                        stats.files_processed++;
                        stats.processed_files.push_back(path);
                    }
                }
            } else if (entry.is_directory()) {
                traverse_directory(entry.path().string(), depth + 1, max_depth);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error traversing directory " << dirpath << ": " << e.what() << std::endl;
    }
}

bool DataLoader::load_datasets(const std::string& data_directory) {
    loaded_examples.clear(); // Clear any previous loads
    
    try {
        traverse_directory(data_directory, 0, 4);
    } catch (const std::exception& e) {
        std::cerr << "Error traversing directory: " << e.what() << std::endl;
    }
    
    stats.records_ingested = loaded_examples.size(); // Update ingested records count
    for (const auto& example : loaded_examples) {
        stats.graph_nodes++;
        stats.graph_edges += 2;
    }
    
    stats.tokens_added = vocabulary_counts.size();
    
    std::cerr << "Data loading complete:" << std::endl;
    std::cerr << "  Files: " << stats.files_processed << std::endl;
    std::cerr << "  Records: " << stats.records_ingested << std::endl;
    std::cerr << "  Failed: " << stats.records_failed << std::endl;
    std::cerr << "  Tokens: " << stats.tokens_added << std::endl;
    
    return true;
}

void DataLoader::build_vocabulary(VectorStr& vocab, int max_vocab_size) {
    std::map<std::string, int> merged_counts = vocabulary_counts;
    
    const auto& bpe_vocab = bpe_tokenizer.get_vocabulary();
    for (const auto& [token, freq] : bpe_vocab) {
        merged_counts[token] += freq;
    }
    
    std::vector<std::pair<std::string, int>> freq_pairs(merged_counts.begin(),
                                                        merged_counts.end());
    
    std::sort(freq_pairs.rbegin(), freq_pairs.rend(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    vocab.clear();
    int count = 0;
    for (const auto& [token, freq] : freq_pairs) {
        if (count >= max_vocab_size) break;
        if (!token.empty() && token.size() < 256) {
            vocab.push_back(token);
            count++;
        }
    }
    
    stats.tokens_added = vocab.size();
}

std::vector<TrainingExample> DataLoader::get_loaded_examples() const {
    return loaded_examples;
}

void DataLoader::clear() {
    vocabulary_counts.clear();
    all_texts.clear();
    stats = {};
}

bool Serializer::serialize_to_binary(const std::string& filepath, const TrainingSnapshot& snapshot) {
    try {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;
        
        uint32_t version_size = snapshot.version.size();
        file.write(reinterpret_cast<const char*>(&version_size), sizeof(version_size));
        file.write(snapshot.version.c_str(), version_size);
        
        file.write(reinterpret_cast<const char*>(&snapshot.training_examples), sizeof(snapshot.training_examples));
        file.write(reinterpret_cast<const char*>(&snapshot.timestamp), sizeof(snapshot.timestamp));
        file.write(reinterpret_cast<const char*>(&snapshot.vocab_size), sizeof(snapshot.vocab_size));
        
        uint32_t vocab_count = snapshot.vocabulary_tokens.size();
        file.write(reinterpret_cast<const char*>(&vocab_count), sizeof(vocab_count));
        
        for (const auto& token : snapshot.vocabulary_tokens) {
            uint32_t token_size = token.size();
            file.write(reinterpret_cast<const char*>(&token_size), sizeof(token_size));
            file.write(token.c_str(), token_size);
        }
        
        uint32_t node_count = snapshot.graph_nodes.size();
        file.write(reinterpret_cast<const char*>(&node_count), sizeof(node_count));
        
        for (const auto& [id, embedding] : snapshot.graph_nodes) {
            uint32_t id_size = id.size();
            file.write(reinterpret_cast<const char*>(&id_size), sizeof(id_size));
            file.write(id.c_str(), id_size);
            
            uint32_t emb_size = embedding.size();
            file.write(reinterpret_cast<const char*>(&emb_size), sizeof(emb_size));
            file.write(reinterpret_cast<const char*>(embedding.data()), emb_size * sizeof(float));
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error serializing: " << e.what() << std::endl;
        return false;
    }
}

bool Serializer::deserialize_from_binary(const std::string& filepath, TrainingSnapshot& snapshot) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) return false;
        
        uint32_t version_size;
        file.read(reinterpret_cast<char*>(&version_size), sizeof(version_size));
        
        char version_buffer[256];
        file.read(version_buffer, version_size);
        snapshot.version = std::string(version_buffer, version_size);
        
        file.read(reinterpret_cast<char*>(&snapshot.training_examples), sizeof(snapshot.training_examples));
        file.read(reinterpret_cast<char*>(&snapshot.timestamp), sizeof(snapshot.timestamp));
        file.read(reinterpret_cast<char*>(&snapshot.vocab_size), sizeof(snapshot.vocab_size));
        
        uint32_t vocab_count;
        file.read(reinterpret_cast<char*>(&vocab_count), sizeof(vocab_count));
        
        for (uint32_t i = 0; i < vocab_count; ++i) {
            uint32_t token_size;
            file.read(reinterpret_cast<char*>(&token_size), sizeof(token_size));
            
            char token_buffer[256];
            file.read(token_buffer, token_size);
            snapshot.vocabulary_tokens.push_back(std::string(token_buffer, token_size));
        }
        
        uint32_t node_count;
        file.read(reinterpret_cast<char*>(&node_count), sizeof(node_count));
        
        for (uint32_t i = 0; i < node_count; ++i) {
            uint32_t id_size;
            file.read(reinterpret_cast<char*>(&id_size), sizeof(id_size));
            
            char id_buffer[256];
            file.read(id_buffer, id_size);
            std::string id(id_buffer, id_size);
            
            uint32_t emb_size;
            file.read(reinterpret_cast<char*>(&emb_size), sizeof(emb_size));
            
            VectorF embedding(emb_size);
            file.read(reinterpret_cast<char*>(embedding.data()), emb_size * sizeof(float));
            
            snapshot.graph_nodes[id] = embedding;
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error deserializing: " << e.what() << std::endl;
        return false;
    }
}

std::string Serializer::serialize_to_json(const TrainingSnapshot& snapshot) {
    std::string json = "{\n";
    json += "  \"version\": \"" + snapshot.version + "\",\n";
    json += "  \"training_examples\": " + std::to_string(snapshot.training_examples) + ",\n";
    json += "  \"timestamp\": " + std::to_string(snapshot.timestamp) + ",\n";
    json += "  \"vocab_size\": " + std::to_string(snapshot.vocab_size) + ",\n";
    json += "  \"vocabulary_tokens\": [\n";
    
    for (size_t i = 0; i < snapshot.vocabulary_tokens.size(); ++i) {
        json += "    \"" + snapshot.vocabulary_tokens[i] + "\"";
        if (i < snapshot.vocabulary_tokens.size() - 1) {
            json += ",";
        }
        json += "\n";
    }
    
    json += "  ]\n}\n";
    return json;
}

bool Serializer::deserialize_from_json(const std::string& json_str, TrainingSnapshot& snapshot) {
    try {
        snapshot.version = SimpleJSON::extract_value(json_str, "version");
        
        size_t pos = json_str.find("\"training_examples\"");
        if (pos != std::string::npos) {
            pos = json_str.find(":", pos);
            pos = json_str.find_first_not_of(" \t", pos + 1);
            size_t end = json_str.find(",", pos);
            if (end == std::string::npos) {
                end = json_str.find("}", pos);
            }
            snapshot.training_examples = std::stoi(json_str.substr(pos, end - pos));
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error deserializing JSON: " << e.what() << std::endl;
        return false;
    }
}
