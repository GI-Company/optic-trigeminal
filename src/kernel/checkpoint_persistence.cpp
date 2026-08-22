#include "checkpoint_persistence.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace fs = std::filesystem;

CheckpointPersistence::CheckpointPersistence(const std::string& storage_dir)
    : storage_directory(storage_dir) {
    ensure_storage_directory_exists();
}

bool CheckpointPersistence::ensure_storage_directory_exists() {
    try {
        if (!fs::exists(storage_directory)) {
            fs::create_directories(storage_directory);
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create checkpoint storage directory: " << e.what() << std::endl;
        return false;
    }
}

std::string CheckpointPersistence::get_stage_directory(TrainingStage stage) const {
    return storage_directory + "/stage_" + std::to_string(static_cast<int>(stage));
}

std::string CheckpointPersistence::get_checkpoint_path(const std::string& checkpoint_id) const {
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        std::string stage_dir = get_stage_directory(stage);
        std::string filepath = stage_dir + "/" + checkpoint_id + ".ckpt";
        if (fs::exists(filepath)) {
            return filepath;
        }
    }
    return "";
}

bool CheckpointPersistence::save_checkpoint(const CheckpointData& checkpoint) {
    try {
        std::string stage_dir = get_stage_directory(checkpoint.stage);
        if (!fs::exists(stage_dir)) {
            fs::create_directories(stage_dir);
        }
        
        std::string filepath = stage_dir + "/" + checkpoint.checkpoint_id + ".ckpt";
        return serialize_checkpoint(checkpoint, filepath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to save checkpoint: " << e.what() << std::endl;
        return false;
    }
}

bool CheckpointPersistence::serialize_checkpoint(const CheckpointData& checkpoint, 
                                                 const std::string& filepath) {
    try {
        std::ofstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        size_t id_len = checkpoint.checkpoint_id.length();
        file.write(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        file.write(checkpoint.checkpoint_id.c_str(), id_len);
        
        int stage = static_cast<int>(checkpoint.stage);
        file.write(reinterpret_cast<char*>(&stage), sizeof(stage));
        
        size_t version_len = checkpoint.version.length();
        file.write(reinterpret_cast<char*>(&version_len), sizeof(version_len));
        file.write(checkpoint.version.c_str(), version_len);
        
        size_t weights_size = checkpoint.model_weights.size();
        file.write(reinterpret_cast<char*>(&weights_size), sizeof(weights_size));
        for (float w : checkpoint.model_weights) {
            file.write(reinterpret_cast<const char*>(&w), sizeof(float));
        }
        
        size_t biases_size = checkpoint.model_biases.size();
        file.write(reinterpret_cast<char*>(&biases_size), sizeof(biases_size));
        for (float b : checkpoint.model_biases) {
            file.write(reinterpret_cast<const char*>(&b), sizeof(float));
        }
        
        file.write(reinterpret_cast<const char*>(&checkpoint.metrics.total_examples), sizeof(int));
        file.write(reinterpret_cast<const char*>(&checkpoint.metrics.examples_processed), sizeof(int));
        file.write(reinterpret_cast<const char*>(&checkpoint.metrics.average_loss), sizeof(float));
        file.write(reinterpret_cast<const char*>(&checkpoint.metrics.accuracy), sizeof(float));
        file.write(reinterpret_cast<const char*>(&checkpoint.metrics.confidence_mean), sizeof(float));
        
        size_t hash_len = checkpoint.content_hash.length();
        file.write(reinterpret_cast<char*>(&hash_len), sizeof(hash_len));
        file.write(checkpoint.content_hash.c_str(), hash_len);
        
        file.write(reinterpret_cast<const char*>(&checkpoint.created_at), sizeof(int64_t));
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Serialization failed: " << e.what() << std::endl;
        return false;
    }
}

bool CheckpointPersistence::load_checkpoint(const std::string& checkpoint_id, CheckpointData& checkpoint) {
    try {
        std::string filepath = get_checkpoint_path(checkpoint_id);
        if (filepath.empty() || !fs::exists(filepath)) {
            return false;
        }
        
        return deserialize_checkpoint(filepath, checkpoint);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load checkpoint: " << e.what() << std::endl;
        return false;
    }
}

bool CheckpointPersistence::deserialize_checkpoint(const std::string& filepath, CheckpointData& checkpoint) {
    try {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        size_t id_len;
        file.read(reinterpret_cast<char*>(&id_len), sizeof(id_len));
        checkpoint.checkpoint_id.resize(id_len);
        file.read(&checkpoint.checkpoint_id[0], id_len);
        
        int stage;
        file.read(reinterpret_cast<char*>(&stage), sizeof(stage));
        checkpoint.stage = static_cast<TrainingStage>(stage);
        
        size_t version_len;
        file.read(reinterpret_cast<char*>(&version_len), sizeof(version_len));
        checkpoint.version.resize(version_len);
        file.read(&checkpoint.version[0], version_len);
        
        size_t weights_size;
        file.read(reinterpret_cast<char*>(&weights_size), sizeof(weights_size));
        checkpoint.model_weights.resize(weights_size);
        for (size_t i = 0; i < weights_size; ++i) {
            file.read(reinterpret_cast<char*>(&checkpoint.model_weights[i]), sizeof(float));
        }
        
        size_t biases_size;
        file.read(reinterpret_cast<char*>(&biases_size), sizeof(biases_size));
        checkpoint.model_biases.resize(biases_size);
        for (size_t i = 0; i < biases_size; ++i) {
            file.read(reinterpret_cast<char*>(&checkpoint.model_biases[i]), sizeof(float));
        }
        
        file.read(reinterpret_cast<char*>(&checkpoint.metrics.total_examples), sizeof(int));
        file.read(reinterpret_cast<char*>(&checkpoint.metrics.examples_processed), sizeof(int));
        file.read(reinterpret_cast<char*>(&checkpoint.metrics.average_loss), sizeof(float));
        file.read(reinterpret_cast<char*>(&checkpoint.metrics.accuracy), sizeof(float));
        file.read(reinterpret_cast<char*>(&checkpoint.metrics.confidence_mean), sizeof(float));
        
        size_t hash_len;
        file.read(reinterpret_cast<char*>(&hash_len), sizeof(hash_len));
        checkpoint.content_hash.resize(hash_len);
        file.read(&checkpoint.content_hash[0], hash_len);
        
        file.read(reinterpret_cast<char*>(&checkpoint.created_at), sizeof(int64_t));
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Deserialization failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<CheckpointData> CheckpointPersistence::list_checkpoints_for_stage(TrainingStage stage) {
    std::vector<CheckpointData> result;
    try {
        std::string stage_dir = get_stage_directory(stage);
        if (!fs::exists(stage_dir)) {
            return result;
        }
        
        for (const auto& entry : fs::directory_iterator(stage_dir)) {
            if (entry.path().extension() == ".ckpt") {
                std::string filename = entry.path().filename().string();
                std::string checkpoint_id = filename.substr(0, filename.length() - 5);
                
                CheckpointData checkpoint;
                if (deserialize_checkpoint(entry.path().string(), checkpoint)) {
                    result.push_back(checkpoint);
                }
            }
        }
        
        std::sort(result.begin(), result.end(), 
                 [](const CheckpointData& a, const CheckpointData& b) {
                     return a.created_at > b.created_at;
                 });
    } catch (const std::exception& e) {
        std::cerr << "Failed to list checkpoints: " << e.what() << std::endl;
    }
    return result;
}

std::vector<CheckpointData> CheckpointPersistence::list_all_checkpoints() {
    std::vector<CheckpointData> result;
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        auto stage_checkpoints = list_checkpoints_for_stage(stage);
        result.insert(result.end(), stage_checkpoints.begin(), stage_checkpoints.end());
    }
    return result;
}

bool CheckpointPersistence::delete_checkpoint(const std::string& checkpoint_id) {
    try {
        std::string filepath = get_checkpoint_path(checkpoint_id);
        if (!filepath.empty() && fs::exists(filepath)) {
            fs::remove(filepath);
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to delete checkpoint: " << e.what() << std::endl;
        return false;
    }
}

bool CheckpointPersistence::checkpoint_exists(const std::string& checkpoint_id) const {
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        std::string stage_dir = get_stage_directory(stage);
        std::string filepath = stage_dir + "/" + checkpoint_id + ".ckpt";
        if (fs::exists(filepath)) {
            return true;
        }
    }
    return false;
}

bool CheckpointPersistence::restore_latest_checkpoint(TrainingStage stage, CheckpointData& checkpoint) {
    auto checkpoints = list_checkpoints_for_stage(stage);
    if (checkpoints.empty()) {
        return false;
    }
    checkpoint = checkpoints[0];
    return true;
}

std::vector<std::string> CheckpointPersistence::get_checkpoint_versions(TrainingStage stage) {
    std::vector<std::string> versions;
    auto checkpoints = list_checkpoints_for_stage(stage);
    for (const auto& cp : checkpoints) {
        versions.push_back(cp.version);
    }
    return versions;
}

bool CheckpointPersistence::verify_checkpoint_integrity(const std::string& checkpoint_id) {
    CheckpointData checkpoint;
    if (!load_checkpoint(checkpoint_id, checkpoint)) {
        return false;
    }
    return checkpoint.verified;
}

void CheckpointPersistence::cleanup_old_checkpoints(int keep_latest) {
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        auto checkpoints = list_checkpoints_for_stage(stage);
        
        if (checkpoints.size() > keep_latest) {
            for (size_t j = keep_latest; j < checkpoints.size(); ++j) {
                delete_checkpoint(checkpoints[j].checkpoint_id);
            }
        }
    }
}

int CheckpointPersistence::get_checkpoint_count() const {
    try {
        int count = 0;
        if (fs::exists(storage_directory)) {
            for (const auto& entry : fs::recursive_directory_iterator(storage_directory)) {
                if (entry.path().extension() == ".ckpt") {
                    count++;
                }
            }
        }
        return count;
    } catch (const std::exception& e) {
        std::cerr << "Failed to count checkpoints: " << e.what() << std::endl;
        return 0;
    }
}
