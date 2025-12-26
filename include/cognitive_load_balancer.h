#pragma once

#include "vfs_manager.h"
#include <map>
#include <queue>
#include <memory>

enum class LoadLevel {
    IDLE = 0,
    LOW = 1,
    MEDIUM = 2,
    HIGH = 3,
    CRITICAL = 4
};

struct ProcessPriority {
    std::string process_id;
    float priority_score;
    LoadLevel impact_level;
    bool is_critical;
    int64_t deadline;
    float estimated_completion_time;
    
    ProcessPriority() : priority_score(1.0f), impact_level(LoadLevel::MEDIUM),
                       is_critical(false), deadline(0), estimated_completion_time(0.0f) {}
    
    bool operator<(const ProcessPriority& other) const {
        return priority_score < other.priority_score;
    }
};

struct SystemLoad {
    LoadLevel current_level;
    float total_token_utilization;
    float total_memory_utilization;
    float total_compute_utilization;
    float attention_utilization;
    int active_process_count;
    int suspended_process_count;
    float system_temperature;
    
    SystemLoad() : current_level(LoadLevel::LOW), total_token_utilization(0.0f),
                  total_memory_utilization(0.0f), total_compute_utilization(0.0f),
                  attention_utilization(0.0f), active_process_count(0),
                  suspended_process_count(0), system_temperature(0.0f) {}
};

class CognitiveLoadBalancer {
public:
    CognitiveLoadBalancer(VFSManager* vfs);
    
    SystemLoad measure_system_load() const;
    
    LoadLevel get_current_load_level() const;
    
    std::vector<std::string> identify_suspendable_processes() const;
    
    bool suspend_low_priority_process(const std::string& process_id);
    
    bool resume_suspended_process(const std::string& process_id);
    
    void rebalance_resources();
    
    std::vector<ProcessPriority> get_process_priority_queue() const;
    
    void set_process_priority(const std::string& process_id, float priority,
                             LoadLevel impact = LoadLevel::MEDIUM, bool is_critical = false);
    
    void set_process_deadline(const std::string& process_id, int64_t deadline_ms);
    
    std::string get_load_report() const;
    
    bool is_system_overloaded() const { return measure_system_load().current_level >= LoadLevel::HIGH; }
    
private:
    VFSManager* vfs_manager;
    std::map<std::string, ProcessPriority> process_priorities;
    std::map<std::string, ProcessState> suspended_process_states;
    
    LoadLevel compute_load_level(const SystemLoad& load) const;
    
    float calculate_process_priority_score(const std::shared_ptr<ProcessContext>& process) const;
    
    bool is_process_suspendable(const std::string& process_id) const;
    
    void reallocate_resources(const std::vector<std::string>& active_processes);
};
