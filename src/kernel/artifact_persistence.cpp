#include "artifact_persistence.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <cstring>

namespace fs = std::filesystem;

ArtifactPersistenceManager::ArtifactPersistenceManager(const std::string& storage_dir)
    : storage_directory(storage_dir) {
    ensure_storage_directory_exists();
    build_artifact_index();
}

bool ArtifactPersistenceManager::ensure_storage_directory_exists() {
    try {
        if (!fs::exists(storage_directory)) {
            fs::create_directories(storage_directory);
        }
        
        std::vector<std::string> stages = {"stem_qa", "coding", "tooling", "logic", "self_knowledge", "multimodal"};
        for (const auto& stage : stages) {
            std::string stage_dir = get_stage_directory(stage);
            if (!fs::exists(stage_dir)) {
                fs::create_directories(stage_dir);
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create artifact storage directory: " << e.what() << std::endl;
        return false;
    }
}

std::string ArtifactPersistenceManager::get_stage_directory(const std::string& stage) const {
    return storage_directory + "/" + stage;
}

std::string ArtifactPersistenceManager::generate_artifact_id(const ProcessedDataRecord& record) {
    std::stringstream ss;
    ss << "artifact_" << record.source_record_id << "_" 
       << std::chrono::system_clock::now().time_since_epoch().count();
    return ss.str();
}

std::string ArtifactPersistenceManager::compute_record_hash(const ProcessedDataRecord& record) const {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t prime = 0x100000001b3ULL;
    
    for (int token : record.input_tokens) {
        hash ^= token;
        hash *= prime;
    }
    
    for (int token : record.output_tokens) {
        hash ^= token;
        hash *= prime;
    }
    
    for (float val : record.input_embedding.values) {
        uint32_t bits = *reinterpret_cast<const uint32_t*>(&val);
        hash ^= bits;
        hash *= prime;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

bool ArtifactPersistenceManager::save_processed_record(const ProcessedDataRecord& record, 
                                                      const std::string& stage) {
    try {
        std::string stage_dir = get_stage_directory(stage);
        if (!fs::exists(stage_dir)) {
            fs::create_directories(stage_dir);
        }
        
        std::string filepath = stage_dir + "/" + record.record_id + ".art";
        return serialize_record(record, filepath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to save artifact: " << e.what() << std::endl;
        return false;
    }
}

bool ArtifactPersistenceManager::serialize_record(const ProcessedDataRecord& record, 
                                                  const std::string& filepath) {
    try {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        size_t id_len = record.record_id.length();
        file.write(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        file.write(record.record_id.c_str(), id_len);
        
        size_t source_id_len = record.source_record_id.length();
        file.write(reinterpret_cast<char*>(&source_id_len), sizeof(source_id_len));
        file.write(record.source_record_id.c_str(), source_id_len);
        
        int source_type = static_cast<int>(record.source_type);
        file.write(reinterpret_cast<char*>(&source_type), sizeof(source_type));
        
        size_t input_tokens_size = record.input_tokens.size();
        file.write(reinterpret_cast<char*>(&input_tokens_size), sizeof(input_tokens_size));
        for (int token : record.input_tokens) {
            file.write(reinterpret_cast<const char*>(&token), sizeof(int));
        }
        
        size_t output_tokens_size = record.output_tokens.size();
        file.write(reinterpret_cast<char*>(&output_tokens_size), sizeof(output_tokens_size));
        for (int token : record.output_tokens) {
            file.write(reinterpret_cast<const char*>(&token), sizeof(int));
        }
        
        size_t embedding_size = record.input_embedding.values.size();
        file.write(reinterpret_cast<char*>(&embedding_size), sizeof(embedding_size));
        for (float val : record.input_embedding.values) {
            file.write(reinterpret_cast<const char*>(&val), sizeof(float));
        }
        
        size_t output_embedding_size = record.output_embedding.values.size();
        file.write(reinterpret_cast<char*>(&output_embedding_size), sizeof(output_embedding_size));
        for (float val : record.output_embedding.values) {
            file.write(reinterpret_cast<const char*>(&val), sizeof(float));
        }
        
        bool is_valid = record.is_valid;
        file.write(reinterpret_cast<const char*>(&is_valid), sizeof(bool));
        
        file.write(reinterpret_cast<const char*>(&record.processed_at), sizeof(int64_t));
        
        file.close();
        
        ArtifactRecord artifact;
        artifact.artifact_id = generate_artifact_id(record);
        artifact.source_record_id = record.source_record_id;
        artifact.source_type = record.source_type;
        artifact.content_hash = compute_record_hash(record);
        artifact.created_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        artifact.verified = true;
        
        artifact_index[artifact.artifact_id] = artifact;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Serialization failed: " << e.what() << std::endl;
        return false;
    }
}

bool ArtifactPersistenceManager::load_processed_record(const std::string& record_id, 
                                                       ProcessedDataRecord& record) {
    try {
        for (int i = 0; i < 6; ++i) {
            std::string stage;
            switch (i) {
                case 0: stage = "stem_qa"; break;
                case 1: stage = "coding"; break;
                case 2: stage = "tooling"; break;
                case 3: stage = "logic"; break;
                case 4: stage = "self_knowledge"; break;
                case 5: stage = "multimodal"; break;
            }
            
            std::string stage_dir = get_stage_directory(stage);
            std::string filepath = stage_dir + "/" + record_id + ".art";
            
            if (fs::exists(filepath)) {
                return deserialize_record(filepath, record);
            }
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load artifact: " << e.what() << std::endl;
        return false;
    }
}

bool ArtifactPersistenceManager::deserialize_record(const std::string& filepath, 
                                                    ProcessedDataRecord& record) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        size_t id_len;
        file.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        record.record_id.resize(id_len);
        file.read(&record.record_id[0], id_len);
        
        size_t source_id_len;
        file.read(reinterpret_cast<char*>(&source_id_len), sizeof(source_id_len));
        record.source_record_id.resize(source_id_len);
        file.read(&record.source_record_id[0], source_id_len);
        
        int source_type;
        file.read(reinterpret_cast<char*>(&source_type), sizeof(source_type));
        record.source_type = static_cast<DataSourceType>(source_type);
        
        size_t input_tokens_size;
        file.read(reinterpret_cast<char*>(&input_tokens_size), sizeof(input_tokens_size));
        record.input_tokens.resize(input_tokens_size);
        for (size_t i = 0; i < input_tokens_size; ++i) {
            file.read(reinterpret_cast<char*>(&record.input_tokens[i]), sizeof(int));
        }
        
        size_t output_tokens_size;
        file.read(reinterpret_cast<char*>(&output_tokens_size), sizeof(output_tokens_size));
        record.output_tokens.resize(output_tokens_size);
        for (size_t i = 0; i < output_tokens_size; ++i) {
            file.read(reinterpret_cast<char*>(&record.output_tokens[i]), sizeof(int));
        }
        
        record.input_embedding = Embedding(EMBEDDING_DIM);
        size_t embedding_size;
        file.read(reinterpret_cast<char*>(&embedding_size), sizeof(embedding_size));
        record.input_embedding.values.resize(embedding_size);
        for (size_t i = 0; i < embedding_size; ++i) {
            file.read(reinterpret_cast<char*>(&record.input_embedding.values[i]), sizeof(float));
        }
        
        record.output_embedding = Embedding(EMBEDDING_DIM);
        size_t output_embedding_size;
        file.read(reinterpret_cast<char*>(&output_embedding_size), sizeof(output_embedding_size));
        record.output_embedding.values.resize(output_embedding_size);
        for (size_t i = 0; i < output_embedding_size; ++i) {
            file.read(reinterpret_cast<char*>(&record.output_embedding.values[i]), sizeof(float));
        }
        
        file.read(reinterpret_cast<char*>(&record.is_valid), sizeof(bool));
        file.read(reinterpret_cast<char*>(&record.processed_at), sizeof(int64_t));
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Deserialization failed: " << e.what() << std::endl;
        return false;
    }
}

bool ArtifactPersistenceManager::save_batch(const std::vector<ProcessedDataRecord>& records, 
                                           const std::string& stage) {
    int successful = 0;
    for (const auto& record : records) {
        if (save_processed_record(record, stage)) {
            successful++;
        }
    }
    return successful == records.size();
}

std::vector<ProcessedDataRecord> ArtifactPersistenceManager::load_batch_by_stage(const std::string& stage) {
    std::vector<ProcessedDataRecord> result;
    try {
        std::string stage_dir = get_stage_directory(stage);
        if (!fs::exists(stage_dir)) {
            return result;
        }
        
        for (const auto& entry : fs::directory_iterator(stage_dir)) {
            if (entry.path().extension() == ".art") {
                ProcessedDataRecord record;
                if (deserialize_record(entry.path().string(), record)) {
                    result.push_back(record);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to load batch: " << e.what() << std::endl;
    }
    return result;
}

std::vector<ProcessedDataRecord> ArtifactPersistenceManager::load_batch_by_source(DataSourceType source) {
    std::vector<ProcessedDataRecord> result;
    
    std::vector<std::string> stages = {"stem_qa", "coding", "tooling", "logic", "self_knowledge", "multimodal"};
    for (const auto& stage : stages) {
        auto stage_records = load_batch_by_stage(stage);
        for (const auto& record : stage_records) {
            if (record.source_type == source) {
                result.push_back(record);
            }
        }
    }
    
    return result;
}

bool ArtifactPersistenceManager::record_exists(const std::string& record_id) const {
    for (int i = 0; i < 6; ++i) {
        std::string stage;
        switch (i) {
            case 0: stage = "stem_qa"; break;
            case 1: stage = "coding"; break;
            case 2: stage = "tooling"; break;
            case 3: stage = "logic"; break;
            case 4: stage = "self_knowledge"; break;
            case 5: stage = "multimodal"; break;
        }
        
        std::string stage_dir = get_stage_directory(stage);
        std::string filepath = stage_dir + "/" + record_id + ".art";
        if (fs::exists(filepath)) {
            return true;
        }
    }
    return false;
}

bool ArtifactPersistenceManager::delete_artifact(const std::string& artifact_id) {
    try {
        auto it = artifact_index.find(artifact_id);
        if (it != artifact_index.end()) {
            artifact_index.erase(it);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete artifact: " << e.what() << std::endl;
        return false;
    }
}

std::vector<ArtifactRecord> ArtifactPersistenceManager::list_artifacts_by_stage(const std::string& stage) {
    std::vector<ArtifactRecord> result;
    for (const auto& pair : artifact_index) {
        if (pair.second.pipeline_stage == stage) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::vector<ArtifactRecord> ArtifactPersistenceManager::list_all_artifacts() {
    std::vector<ArtifactRecord> result;
    for (const auto& pair : artifact_index) {
        result.push_back(pair.second);
    }
    std::sort(result.begin(), result.end(), 
             [](const ArtifactRecord& a, const ArtifactRecord& b) {
                 return a.created_timestamp > b.created_timestamp;
             });
    return result;
}

int ArtifactPersistenceManager::get_artifact_count() const {
    return artifact_index.size();
}

bool ArtifactPersistenceManager::verify_artifact_integrity(const std::string& artifact_id) {
    auto it = artifact_index.find(artifact_id);
    if (it != artifact_index.end()) {
        return it->second.verified;
    }
    return false;
}

std::string ArtifactPersistenceManager::get_storage_stats() const {
    std::stringstream ss;
    ss << "Artifact Storage Statistics:\n";
    ss << "  Total Artifacts: " << artifact_index.size() << "\n";
    
    int verified_count = 0;
    for (const auto& pair : artifact_index) {
        if (pair.second.verified) {
            verified_count++;
        }
    }
    ss << "  Verified Artifacts: " << verified_count << "\n";
    
    try {
        if (fs::exists(storage_directory)) {
            uint64_t total_size = 0;
            for (const auto& entry : fs::recursive_directory_iterator(storage_directory)) {
                if (fs::is_regular_file(entry)) {
                    total_size += fs::file_size(entry);
                }
            }
            ss << "  Total Storage Size: " << (total_size / 1024.0 / 1024.0) << " MB\n";
        }
    } catch (const std::exception& e) {
        ss << "  Storage Size: (error calculating)\n";
    }
    
    return ss.str();
}

void ArtifactPersistenceManager::export_artifacts_to_json(const std::string& output_file) {
    std::ofstream file(output_file);
    if (!file.is_open()) return;
    
    file << "{\n  \"artifacts\": [\n";
    
    bool first = true;
    for (const auto& pair : artifact_index) {
        if (!first) file << ",\n";
        first = false;
        
        file << "    {\n";
        file << "      \"artifact_id\": \"" << pair.first << "\",\n";
        file << "      \"source_record_id\": \"" << pair.second.source_record_id << "\",\n";
        file << "      \"source_type\": " << static_cast<int>(pair.second.source_type) << ",\n";
        file << "      \"content_hash\": \"" << pair.second.content_hash << "\",\n";
        file << "      \"created_timestamp\": " << pair.second.created_timestamp << ",\n";
        file << "      \"verified\": " << (pair.second.verified ? "true" : "false") << "\n";
        file << "    }";
    }
    
    file << "\n  ]\n}\n";
    file.close();
}

bool ArtifactPersistenceManager::import_artifacts_from_json(const std::string& input_file) {
    return fs::exists(input_file);
}

bool ArtifactPersistenceManager::cleanup_artifacts_older_than(int days) {
    int64_t cutoff_time = std::chrono::system_clock::now().time_since_epoch().count() 
                         - (days * 24 * 3600 * (int64_t)1e9);
    
    std::vector<std::string> to_delete;
    for (const auto& pair : artifact_index) {
        if (pair.second.created_timestamp < cutoff_time) {
            to_delete.push_back(pair.first);
        }
    }
    
    for (const auto& artifact_id : to_delete) {
        delete_artifact(artifact_id);
    }
    
    return true;
}

void ArtifactPersistenceManager::build_artifact_index() {
    artifact_index.clear();
    
    std::vector<std::string> stages = {"stem_qa", "coding", "tooling", "logic", "self_knowledge", "multimodal"};
    for (const auto& stage : stages) {
        auto records = load_batch_by_stage(stage);
        for (const auto& record : records) {
            ArtifactRecord artifact;
            artifact.artifact_id = generate_artifact_id(record);
            artifact.source_record_id = record.source_record_id;
            artifact.source_type = record.source_type;
            artifact.content_hash = compute_record_hash(record);
            artifact.created_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            artifact.pipeline_stage = stage;
            artifact.verified = true;
            
            artifact_index[artifact.artifact_id] = artifact;
        }
    }
}
