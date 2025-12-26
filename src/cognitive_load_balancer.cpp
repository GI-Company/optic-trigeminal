#include "cognitive_load_balancer.h"
#include <sstream>
#include <algorithm>
#include <cmath>

CognitiveLoadBalancer::CognitiveLoadBalancer(VFSManager* vfs) : vfs_manager(vfs) {}

SystemLoad CognitiveLoadBalancer::measure_system_load() const {
    SystemLoad load;
    
    auto processes = vfs_manager->list_processes();
    load.active_process_count = processes.size();
    
    float total_tokens_used = 0.0f;
    float total_tokens_budget = 0.0f;
    float total_memory_used = 0.0f;
    float total_memory_budget = 0.0f;
    float total_compute_used = 0.0f;
    float total_compute_budget = 0.0f;
    
    for (const auto& proc_id : processes) {
        auto proc = vfs_manager->get_process(proc_id);
        if (!proc) continue;
        
        auto token_it = proc->resources.find(ResourceType::TOKEN_BUDGET);
        if (token_it != proc->resources.end()) {
            total_tokens_used += token_it->second.current_usage;
            total_tokens_budget += token_it->second.max_value;
        }
        
        auto mem_it = proc->resources.find(ResourceType::MEMORY_LIMIT);
        if (mem_it != proc->resources.end()) {
            total_memory_used += mem_it->second.current_usage;
            total_memory_budget += mem_it->second.max_value;
        }
        
        auto compute_it = proc->resources.find(ResourceType::COMPUTATION_TIME);
        if (compute_it != proc->resources.end()) {
            total_compute_used += compute_it->second.current_usage;
            total_compute_budget += compute_it->second.max_value;
        }
    }
    
    if (total_tokens_budget > 0.0f) {
        load.total_token_utilization = (total_tokens_used / total_tokens_budget) * 100.0f;
    }
    
    if (total_memory_budget > 0.0f) {
        load.total_memory_utilization = (total_memory_used / total_memory_budget) * 100.0f;
    }
    
    if (total_compute_budget > 0.0f) {
        load.total_compute_utilization = (total_compute_used / total_compute_budget) * 100.0f;
    }
    
    load.system_temperature = (load.total_token_utilization + 
                              load.total_memory_utilization + 
                              load.total_compute_utilization) / 3.0f;
    
    load.suspended_process_count = suspended_process_states.size();
    load.current_level = compute_load_level(load);
    
    return load;
}

LoadLevel CognitiveLoadBalancer::get_current_load_level() const {
    return measure_system_load().current_level;
}

std::vector<std::string> CognitiveLoadBalancer::identify_suspendable_processes() const {
    std::vector<std::string> suspendable;
    auto processes = vfs_manager->list_processes();
    
    for (const auto& proc_id : processes) {
        if (is_process_suspendable(proc_id)) {
            suspendable.push_back(proc_id);
        }
    }
    
    std::sort(suspendable.begin(), suspendable.end(),
             [this](const std::string& a, const std::string& b) {
                 float score_a = calculate_process_priority_score(vfs_manager->get_process(a));
                 float score_b = calculate_process_priority_score(vfs_manager->get_process(b));
                 return score_a < score_b;
             });
    
    return suspendable;
}

bool CognitiveLoadBalancer::suspend_low_priority_process(const std::string& process_id) {
    auto process = vfs_manager->get_process(process_id);
    if (!process || process->current_state == ProcessState::SUSPENDED) {
        return false;
    }
    
    suspended_process_states[process_id] = process->current_state;
    vfs_manager->transition_process_state(process_id, ProcessState::SUSPENDED,
                                         "Suspended due to high system load");
    
    return true;
}

bool CognitiveLoadBalancer::resume_suspended_process(const std::string& process_id) {
    auto it = suspended_process_states.find(process_id);
    if (it == suspended_process_states.end()) {
        return false;
    }
    
    ProcessState previous_state = it->second;
    suspended_process_states.erase(it);
    vfs_manager->transition_process_state(process_id, previous_state, "Resumed from suspension");
    
    return true;
}

void CognitiveLoadBalancer::rebalance_resources() {
    auto load = measure_system_load();
    
    if (load.current_level >= LoadLevel::HIGH) {
        auto suspendable = identify_suspendable_processes();
        
        int to_suspend = std::max(1, static_cast<int>(suspendable.size() * 0.2f));
        for (int i = 0; i < to_suspend && i < static_cast<int>(suspendable.size()); ++i) {
            suspend_low_priority_process(suspendable[i]);
        }
    }
    
    if (load.current_level <= LoadLevel::MEDIUM && !suspended_process_states.empty()) {
        auto it = suspended_process_states.begin();
        if (it != suspended_process_states.end()) {
            resume_suspended_process(it->first);
        }
    }
}

std::vector<ProcessPriority> CognitiveLoadBalancer::get_process_priority_queue() const {
    std::vector<ProcessPriority> queue;
    
    auto processes = vfs_manager->list_processes();
    for (const auto& proc_id : processes) {
        auto process = vfs_manager->get_process(proc_id);
        if (!process) continue;
        
        ProcessPriority priority;
        priority.process_id = proc_id;
        
        auto it = process_priorities.find(proc_id);
        priority.priority_score = it != process_priorities.end() ? 
                                 it->second.priority_score : 1.0f;
        priority.impact_level = it != process_priorities.end() ? 
                               it->second.impact_level : LoadLevel::MEDIUM;
        priority.is_critical = it != process_priorities.end() ? 
                              it->second.is_critical : false;
        
        queue.push_back(priority);
    }
    
    std::sort(queue.begin(), queue.end(),
             [](const ProcessPriority& a, const ProcessPriority& b) {
                 return a.priority_score > b.priority_score;
             });
    
    return queue;
}

void CognitiveLoadBalancer::set_process_priority(const std::string& process_id, float priority,
                                                LoadLevel impact, bool is_critical) {
    ProcessPriority p;
    p.process_id = process_id;
    p.priority_score = priority;
    p.impact_level = impact;
    p.is_critical = is_critical;
    
    process_priorities[process_id] = p;
}

void CognitiveLoadBalancer::set_process_deadline(const std::string& process_id, int64_t deadline_ms) {
    auto it = process_priorities.find(process_id);
    if (it != process_priorities.end()) {
        it->second.deadline = deadline_ms;
    }
}

std::string CognitiveLoadBalancer::get_load_report() const {
    auto load = measure_system_load();
    std::stringstream ss;
    
    ss << "=== System Load Report ===\n";
    ss << "Current Level: ";
    switch (load.current_level) {
        case LoadLevel::IDLE: ss << "IDLE"; break;
        case LoadLevel::LOW: ss << "LOW"; break;
        case LoadLevel::MEDIUM: ss << "MEDIUM"; break;
        case LoadLevel::HIGH: ss << "HIGH"; break;
        case LoadLevel::CRITICAL: ss << "CRITICAL"; break;
    }
    ss << "\n";
    ss << "Token Utilization: " << load.total_token_utilization << "%\n";
    ss << "Memory Utilization: " << load.total_memory_utilization << "%\n";
    ss << "Compute Utilization: " << load.total_compute_utilization << "%\n";
    ss << "System Temperature: " << load.system_temperature << "%\n";
    ss << "Active Processes: " << load.active_process_count << "\n";
    ss << "Suspended Processes: " << load.suspended_process_count << "\n";
    
    return ss.str();
}

LoadLevel CognitiveLoadBalancer::compute_load_level(const SystemLoad& load) const {
    float avg_util = (load.total_token_utilization + 
                     load.total_memory_utilization + 
                     load.total_compute_utilization) / 3.0f;
    
    if (avg_util < 25.0f) return LoadLevel::IDLE;
    if (avg_util < 50.0f) return LoadLevel::LOW;
    if (avg_util < 75.0f) return LoadLevel::MEDIUM;
    if (avg_util < 90.0f) return LoadLevel::HIGH;
    return LoadLevel::CRITICAL;
}

float CognitiveLoadBalancer::calculate_process_priority_score(const std::shared_ptr<ProcessContext>& process) const {
    if (!process) return 0.0f;
    
    float score = 1.0f;
    
    auto it = process_priorities.find(process->process_id);
    if (it != process_priorities.end()) {
        score = it->second.priority_score;
    }
    
    if (process->current_state == ProcessState::REASONING) {
        score *= 1.5f;
    }
    
    return score;
}

bool CognitiveLoadBalancer::is_process_suspendable(const std::string& process_id) const {
    auto process = vfs_manager->get_process(process_id);
    if (!process) return false;
    
    if (process->current_state == ProcessState::COMPLETE ||
        process->current_state == ProcessState::FAILED ||
        process->current_state == ProcessState::SUSPENDED) {
        return false;
    }
    
    auto it = process_priorities.find(process_id);
    if (it != process_priorities.end() && it->second.is_critical) {
        return false;
    }
    
    return true;
}

void CognitiveLoadBalancer::reallocate_resources(const std::vector<std::string>& active_processes) {
}
