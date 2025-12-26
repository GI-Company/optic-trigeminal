#pragma once

#include "training_stages.h"
#include "checkpoint_persistence.h"
#include "vfs_manager.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

enum class RecoveryAction {
    NONE,
    RETRY_STAGE,
    ROLLBACK_TO_CHECKPOINT,
    SKIP_STAGE,
    ABORT_TRAINING
};

struct FailureRecord {
    std::string failure_id;
    TrainingStage stage;
    std::string error_message;
    int64_t failure_timestamp;
    RecoveryAction suggested_action;
    int retry_count;
    std::string last_checkpoint_id;
    bool resolved;
    
    FailureRecord() : failure_timestamp(0), retry_count(0), resolved(false) {}
};

struct RecoveryState {
    int total_failures;
    int resolved_failures;
    int unresolved_failures;
    RecoveryAction last_action_taken;
    int64_t last_recovery_timestamp;
    std::vector<FailureRecord> failure_log;
};

class RecoveryManager {
public:
    RecoveryManager(CheckpointPersistence* checkpoint_mgr, VFSManager* vfs);
    
    void record_stage_failure(TrainingStage stage, const std::string& error_message);
    
    RecoveryAction determine_recovery_action(const FailureRecord& failure);
    
    bool execute_recovery_action(RecoveryAction action, const FailureRecord& failure);
    
    bool rollback_to_checkpoint(const std::string& checkpoint_id);
    
    bool retry_stage(TrainingStage stage, int max_retries = 3);
    
    bool skip_stage(TrainingStage stage);
    
    bool abort_training();
    
    FailureRecord get_failure_record(const std::string& failure_id) const;
    
    RecoveryState get_recovery_state() const;
    
    std::vector<FailureRecord> get_failure_history(TrainingStage stage = static_cast<TrainingStage>(-1));
    
    bool has_unresolved_failures() const;
    
    std::string generate_recovery_report() const;
    
    bool auto_recover();
    
    void clear_recovery_log();
    
    int get_max_retries() const { return max_retries_per_stage; }
    
    void set_max_retries(int max_retries) { max_retries_per_stage = max_retries; }
    
private:
    CheckpointPersistence* checkpoint_manager;
    VFSManager* vfs_manager;
    
    std::vector<FailureRecord> failure_log;
    std::map<TrainingStage, int> stage_retry_counts;
    std::map<TrainingStage, bool> stage_skip_status;
    
    int max_retries_per_stage;
    int64_t last_recovery_time;
    
    std::string generate_failure_id();
    
    RecoveryAction suggest_action(const FailureRecord& failure);
    
    bool can_retry_stage(TrainingStage stage) const;
    
    void log_recovery_action(RecoveryAction action, const std::string& details);
};
