#pragma once

#include "training_stages.h"
#include <string>
#include <vector>
#include <memory>

class CheckpointPersistence {
public:
    CheckpointPersistence(const std::string& storage_dir = "data/checkpoints");
    
    bool save_checkpoint(const CheckpointData& checkpoint);
    
    bool load_checkpoint(const std::string& checkpoint_id, CheckpointData& checkpoint);
    
    std::vector<CheckpointData> list_checkpoints_for_stage(TrainingStage stage);
    
    std::vector<CheckpointData> list_all_checkpoints();
    
    bool delete_checkpoint(const std::string& checkpoint_id);
    
    bool checkpoint_exists(const std::string& checkpoint_id) const;
    
    std::string get_checkpoint_path(const std::string& checkpoint_id) const;
    
    bool restore_latest_checkpoint(TrainingStage stage, CheckpointData& checkpoint);
    
    std::vector<std::string> get_checkpoint_versions(TrainingStage stage);
    
    bool verify_checkpoint_integrity(const std::string& checkpoint_id);
    
    void cleanup_old_checkpoints(int keep_latest = 3);
    
    int get_checkpoint_count() const;
    
private:
    std::string storage_directory;
    
    bool ensure_storage_directory_exists();
    
    std::string get_stage_directory(TrainingStage stage) const;
    
    bool serialize_checkpoint(const CheckpointData& checkpoint, const std::string& filepath);
    
    bool deserialize_checkpoint(const std::string& filepath, CheckpointData& checkpoint);
    
    std::string compute_checkpoint_hash(const CheckpointData& checkpoint) const;
};
