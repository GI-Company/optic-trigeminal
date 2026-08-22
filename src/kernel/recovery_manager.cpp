#include "recovery_manager.h"
#include <sstream>
#include <chrono>
#include <iostream>
#include <algorithm>

RecoveryManager::RecoveryManager(CheckpointPersistence* checkpoint_mgr, VFSManager* vfs)
    : checkpoint_manager(checkpoint_mgr), 
      vfs_manager(vfs),
      max_retries_per_stage(3),
      last_recovery_time(0) {
    
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        stage_retry_counts[stage] = 0;
        stage_skip_status[stage] = false;
    }
}

std::string RecoveryManager::generate_failure_id() {
    static int counter = 0;
    std::stringstream ss;
    ss << "failure_" << std::chrono::system_clock::now().time_since_epoch().count() 
       << "_" << (counter++);
    return ss.str();
}

void RecoveryManager::record_stage_failure(TrainingStage stage, const std::string& error_message) {
    FailureRecord failure;
    failure.failure_id = generate_failure_id();
    failure.stage = stage;
    failure.error_message = error_message;
    failure.failure_timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    failure.retry_count = stage_retry_counts[stage];
    failure.suggested_action = determine_recovery_action(failure);
    failure.resolved = false;
    
    auto checkpoints = checkpoint_manager->list_checkpoints_for_stage(stage);
    if (!checkpoints.empty()) {
        failure.last_checkpoint_id = checkpoints[0].checkpoint_id;
    }
    
    failure_log.push_back(failure);
    
    log_recovery_action(RecoveryAction::NONE, 
                       "Stage " + std::to_string(static_cast<int>(stage)) + 
                       " failed: " + error_message);
}

RecoveryAction RecoveryManager::determine_recovery_action(const FailureRecord& failure) {
    return suggest_action(failure);
}

RecoveryAction RecoveryManager::suggest_action(const FailureRecord& failure) {
    if (can_retry_stage(failure.stage)) {
        return RecoveryAction::RETRY_STAGE;
    }
    
    if (!failure.last_checkpoint_id.empty()) {
        return RecoveryAction::ROLLBACK_TO_CHECKPOINT;
    }
    
    return RecoveryAction::SKIP_STAGE;
}

bool RecoveryManager::can_retry_stage(TrainingStage stage) const {
    return stage_retry_counts.at(stage) < max_retries_per_stage;
}

bool RecoveryManager::execute_recovery_action(RecoveryAction action, const FailureRecord& failure) {
    switch (action) {
        case RecoveryAction::RETRY_STAGE:
            return retry_stage(failure.stage);
        case RecoveryAction::ROLLBACK_TO_CHECKPOINT:
            if (!failure.last_checkpoint_id.empty()) {
                return rollback_to_checkpoint(failure.last_checkpoint_id);
            }
            return false;
        case RecoveryAction::SKIP_STAGE:
            return skip_stage(failure.stage);
        case RecoveryAction::ABORT_TRAINING:
            return abort_training();
        default:
            return false;
    }
}

bool RecoveryManager::rollback_to_checkpoint(const std::string& checkpoint_id) {
    CheckpointData checkpoint;
    if (!checkpoint_manager->load_checkpoint(checkpoint_id, checkpoint)) {
        std::cerr << "Failed to load checkpoint for rollback: " << checkpoint_id << std::endl;
        return false;
    }
    
    if (!checkpoint_manager->verify_checkpoint_integrity(checkpoint_id)) {
        std::cerr << "Checkpoint integrity verification failed: " << checkpoint_id << std::endl;
        return false;
    }
    
    log_recovery_action(RecoveryAction::ROLLBACK_TO_CHECKPOINT,
                       "Rolled back to checkpoint: " + checkpoint_id);
    
    last_recovery_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    return true;
}

bool RecoveryManager::retry_stage(TrainingStage stage, int max_retries) {
    if (stage_retry_counts[stage] >= max_retries) {
        std::cerr << "Max retries exceeded for stage " << static_cast<int>(stage) << std::endl;
        return false;
    }
    
    stage_retry_counts[stage]++;
    
    log_recovery_action(RecoveryAction::RETRY_STAGE,
                       "Retrying stage " + std::to_string(static_cast<int>(stage)) +
                       " (attempt " + std::to_string(stage_retry_counts[stage]) + ")");
    
    last_recovery_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    return true;
}

bool RecoveryManager::skip_stage(TrainingStage stage) {
    stage_skip_status[stage] = true;
    
    log_recovery_action(RecoveryAction::SKIP_STAGE,
                       "Skipping stage " + std::to_string(static_cast<int>(stage)));
    
    last_recovery_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    return true;
}

bool RecoveryManager::abort_training() {
    log_recovery_action(RecoveryAction::ABORT_TRAINING, "Aborting training");
    last_recovery_time = std::chrono::system_clock::now().time_since_epoch().count();
    return true;
}

FailureRecord RecoveryManager::get_failure_record(const std::string& failure_id) const {
    for (const auto& failure : failure_log) {
        if (failure.failure_id == failure_id) {
            return failure;
        }
    }
    return FailureRecord();
}

RecoveryState RecoveryManager::get_recovery_state() const {
    RecoveryState state;
    state.total_failures = failure_log.size();
    state.resolved_failures = 0;
    state.unresolved_failures = 0;
    
    for (const auto& failure : failure_log) {
        if (failure.resolved) {
            state.resolved_failures++;
        } else {
            state.unresolved_failures++;
        }
    }
    
    state.last_recovery_timestamp = last_recovery_time;
    state.failure_log = failure_log;
    
    return state;
}

std::vector<FailureRecord> RecoveryManager::get_failure_history(TrainingStage stage) {
    std::vector<FailureRecord> result;
    
    for (const auto& failure : failure_log) {
        if (stage == static_cast<TrainingStage>(-1) || failure.stage == stage) {
            result.push_back(failure);
        }
    }
    
    std::sort(result.begin(), result.end(),
             [](const FailureRecord& a, const FailureRecord& b) {
                 return a.failure_timestamp > b.failure_timestamp;
             });
    
    return result;
}

bool RecoveryManager::has_unresolved_failures() const {
    for (const auto& failure : failure_log) {
        if (!failure.resolved) {
            return true;
        }
    }
    return false;
}

std::string RecoveryManager::generate_recovery_report() const {
    std::stringstream ss;
    ss << "=== Recovery Manager Report ===\n\n";
    
    ss << "Summary:\n";
    ss << "  Total Failures: " << failure_log.size() << "\n";
    
    int resolved = 0, unresolved = 0;
    for (const auto& failure : failure_log) {
        if (failure.resolved) {
            resolved++;
        } else {
            unresolved++;
        }
    }
    
    ss << "  Resolved: " << resolved << "\n";
    ss << "  Unresolved: " << unresolved << "\n";
    ss << "  Max Retries per Stage: " << max_retries_per_stage << "\n\n";
    
    ss << "Stage Retry Counts:\n";
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        ss << "  Stage " << i << ": " << stage_retry_counts.at(stage) << " retries\n";
    }
    ss << "\n";
    
    ss << "Stage Skip Status:\n";
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        ss << "  Stage " << i << ": " << (stage_skip_status.at(stage) ? "Skipped" : "Active") << "\n";
    }
    ss << "\n";
    
    ss << "Recent Failures (last 10):\n";
    int start_idx = std::max(0, static_cast<int>(failure_log.size()) - 10);
    for (int i = start_idx; i < failure_log.size(); ++i) {
        const auto& failure = failure_log[i];
        ss << "  [" << i << "] Stage " << static_cast<int>(failure.stage) << ": "
           << failure.error_message << "\n";
        ss << "       Suggested Action: ";
        
        switch (failure.suggested_action) {
            case RecoveryAction::RETRY_STAGE:
                ss << "RETRY (attempt " << failure.retry_count << ")\n";
                break;
            case RecoveryAction::ROLLBACK_TO_CHECKPOINT:
                ss << "ROLLBACK to " << failure.last_checkpoint_id << "\n";
                break;
            case RecoveryAction::SKIP_STAGE:
                ss << "SKIP\n";
                break;
            case RecoveryAction::ABORT_TRAINING:
                ss << "ABORT\n";
                break;
            default:
                ss << "NONE\n";
        }
    }
    
    return ss.str();
}

bool RecoveryManager::auto_recover() {
    if (failure_log.empty()) {
        return true;
    }
    
    bool all_recovered = true;
    for (auto& failure : failure_log) {
        if (!failure.resolved) {
            RecoveryAction action = determine_recovery_action(failure);
            if (execute_recovery_action(action, failure)) {
                failure.resolved = true;
            } else {
                all_recovered = false;
            }
        }
    }
    
    return all_recovered;
}

void RecoveryManager::clear_recovery_log() {
    failure_log.clear();
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        stage_retry_counts[stage] = 0;
        stage_skip_status[stage] = false;
    }
}

void RecoveryManager::log_recovery_action(RecoveryAction action, const std::string& details) {
    std::stringstream ss;
    ss << "[" << std::chrono::system_clock::now().time_since_epoch().count() << "] ";
    
    switch (action) {
        case RecoveryAction::RETRY_STAGE:
            ss << "RETRY: ";
            break;
        case RecoveryAction::ROLLBACK_TO_CHECKPOINT:
            ss << "ROLLBACK: ";
            break;
        case RecoveryAction::SKIP_STAGE:
            ss << "SKIP: ";
            break;
        case RecoveryAction::ABORT_TRAINING:
            ss << "ABORT: ";
            break;
        default:
            ss << "RECORD: ";
    }
    
    ss << details;
    
    if (vfs_manager) {
        vfs_manager->checkpoint_module_state(
            "recovery_log",
            details,
            "v1.0.0"
        );
    }
}
