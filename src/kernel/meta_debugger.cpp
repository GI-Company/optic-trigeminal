#include "meta_debugger.h"
#include <chrono>
#include <sstream>
#include <algorithm>

MetaDebugger::MetaDebugger(VFSManager* vfs) : vfs_manager(vfs) {
    initialize_remedies();
}

void MetaDebugger::initialize_remedies() {
    failure_remedies[FailureMode::TIMEOUT] = {
        "Increase timeout window",
        "Simplify computation",
        "Split into smaller subtasks",
        "Increase available resources"
    };
    
    failure_remedies[FailureMode::RESOURCE_EXHAUSTED] = {
        "Increase token budget",
        "Increase memory allocation",
        "Suspend lower-priority tasks",
        "Enable garbage collection",
        "Reduce batch size"
    };
    
    failure_remedies[FailureMode::INVALID_RESULT] = {
        "Retry with different seed",
        "Use alternative algorithm",
        "Increase verification depth",
        "Add constraint checking"
    };
    
    failure_remedies[FailureMode::DEPENDENCY_FAILED] = {
        "Retry dependency",
        "Use fallback resource",
        "Skip dependency and continue",
        "Request user intervention"
    };
    
    failure_remedies[FailureMode::SAFETY_VIOLATION] = {
        "Reduce safety threshold",
        "Enable human review",
        "Add protective constraints",
        "Escalate to admin"
    };
    
    failure_remedies[FailureMode::COMPUTATION_ERROR] = {
        "Retry computation",
        "Use alternative implementation",
        "Enable error recovery",
        "Rollback to checkpoint"
    };
}

DebugTrace MetaDebugger::analyze_process_failure(const std::string& process_id) {
    DebugTrace trace;
    trace.process_id = process_id;
    trace.failure_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    auto process = vfs_manager->get_process(process_id);
    if (!process) {
        trace.failure_mode = FailureMode::UNKNOWN;
        trace.failure_description = "Process not found";
        return trace;
    }
    
    trace.failure_mode = diagnose_failure_mode(process);
    trace.failure_description = "Process " + process_id.substr(0, 8) + " failed in state " +
                               std::to_string(static_cast<int>(process->current_state));
    
    trace.resource_state = analyze_resource_state(process);
    trace.state_transitions = extract_state_transitions(process);
    trace.diagnostics_run = 1;
    trace.recovery_confidence = estimate_recovery_probability(trace);
    
    if (failure_remedies.count(trace.failure_mode)) {
        trace.recommended_fixes = failure_remedies[trace.failure_mode];
    }
    
    debug_history.push_back(trace);
    return trace;
}

FailureMode MetaDebugger::diagnose_failure_mode(const std::shared_ptr<ProcessContext>& process) {
    if (!process) return FailureMode::UNKNOWN;
    
    auto resources = process->resources;
    
    for (const auto& res_pair : resources) {
        if (res_pair.second.is_over_budget()) {
            return FailureMode::RESOURCE_EXHAUSTED;
        }
    }
    
    if (process->state_history.size() > 0) {
        auto last_transition = process->state_history.back();
        if (last_transition.to_state == ProcessState::SUSPENDED) {
            return FailureMode::TIMEOUT;
        }
    }
    
    if (process->current_state == ProcessState::FAILED) {
        return FailureMode::COMPUTATION_ERROR;
    }
    
    return FailureMode::UNKNOWN;
}

RetryStrategy MetaDebugger::create_retry_strategy(const DebugTrace& trace, int base_retries) {
    RetryStrategy strategy;
    strategy.max_retries = base_retries;
    strategy.current_retry = 0;
    strategy.backoff_ms = 100;
    strategy.backoff_multiplier = 2.0f;
    
    switch (trace.failure_mode) {
        case FailureMode::TIMEOUT:
            strategy.increase_timeout = true;
            strategy.modified_params["timeout_ms"] = std::to_string(int(60000 * strategy.backoff_multiplier));
            break;
            
        case FailureMode::RESOURCE_EXHAUSTED:
            strategy.modified_resources[ResourceType::TOKEN_BUDGET] = 1.5f;
            strategy.modified_resources[ResourceType::MEMORY_LIMIT] = 1.5f;
            strategy.reduce_complexity = true;
            break;
            
        case FailureMode::INVALID_RESULT:
            strategy.modified_params["verification_depth"] = "2";
            break;
            
        case FailureMode::DEPENDENCY_FAILED:
            strategy.modified_params["use_fallback"] = "true";
            break;
            
        case FailureMode::COMPUTATION_ERROR:
            strategy.backoff_ms = 500;
            strategy.modified_params["enable_recovery"] = "true";
            break;
            
        default:
            break;
    }
    
    return strategy;
}

bool MetaDebugger::execute_retry(const std::string& original_process_id,
                                const RetryStrategy& strategy,
                                std::string& new_process_id) {
    auto original = vfs_manager->get_process(original_process_id);
    if (!original) return false;
    
    auto retry_process = vfs_manager->create_process(
        original->task_name + "_retry_" + std::to_string(strategy.current_retry + 1),
        original->task_description,
        original->parent_process_id
    );
    
    if (!retry_process) return false;
    
    new_process_id = retry_process->process_id;
    
    float token_mult = strategy.modified_resources.count(ResourceType::TOKEN_BUDGET) ? 
                      strategy.modified_resources.at(ResourceType::TOKEN_BUDGET) : 1.0f;
    float memory_mult = strategy.modified_resources.count(ResourceType::MEMORY_LIMIT) ?
                       strategy.modified_resources.at(ResourceType::MEMORY_LIMIT) : 1.0f;
    
    vfs_manager->initialize_process_resources(
        retry_process->process_id,
        2000.0f * token_mult,
        1000.0f * memory_mult,
        60000.0f
    );
    
    vfs_manager->transition_process_state(retry_process->process_id,
                                         ProcessState::INITIALIZING,
                                         "Retry attempt " + std::to_string(strategy.current_retry + 1));
    
    return true;
}

std::vector<std::string> MetaDebugger::generate_diagnostic_report(const std::string& process_id) const {
    std::vector<std::string> report;
    
    auto process = vfs_manager->get_process(process_id);
    if (!process) {
        report.push_back("ERROR: Process not found");
        return report;
    }
    
    report.push_back("=== Process Diagnostic Report ===");
    report.push_back("Process ID: " + process_id);
    report.push_back("Task Name: " + process->task_name);
    report.push_back("Current State: " + std::to_string(static_cast<int>(process->current_state)));
    
    report.push_back("\nResource Usage:");
    for (const auto& res_pair : process->resources) {
        std::string type_name;
        switch (res_pair.first) {
            case ResourceType::TOKEN_BUDGET: type_name = "Tokens"; break;
            case ResourceType::MEMORY_LIMIT: type_name = "Memory"; break;
            case ResourceType::COMPUTATION_TIME: type_name = "Compute"; break;
            case ResourceType::CACHE_SIZE: type_name = "Cache"; break;
            case ResourceType::ATTENTION_BUDGET: type_name = "Attention"; break;
        }
        float util = res_pair.second.utilization_percent();
        report.push_back(type_name + ": " + std::to_string(util) + "%");
    }
    
    report.push_back("\nState Transitions:");
    for (const auto& transition : process->state_history) {
        report.push_back("  " + std::to_string(static_cast<int>(transition.from_state)) + " -> " +
                        std::to_string(static_cast<int>(transition.to_state)));
    }
    
    return report;
}

std::string MetaDebugger::get_failure_summary(const std::string& process_id) const {
    std::stringstream ss;
    
    auto process = vfs_manager->get_process(process_id);
    if (!process) {
        ss << "Process not found: " << process_id;
        return ss.str();
    }
    
    ss << "Process " << process_id.substr(0, 8) << " - " << process->task_name << "\n";
    ss << "Current State: " << static_cast<int>(process->current_state) << "\n";
    
    for (const auto& res_pair : process->resources) {
        if (res_pair.second.is_over_budget()) {
            ss << "RESOURCE EXHAUSTED: ";
            switch (res_pair.first) {
                case ResourceType::TOKEN_BUDGET: ss << "Tokens"; break;
                case ResourceType::MEMORY_LIMIT: ss << "Memory"; break;
                default: ss << "Unknown"; break;
            }
            ss << "\n";
        }
    }
    
    return ss.str();
}

bool MetaDebugger::is_process_recoverable(const FailureMode mode) const {
    return mode != FailureMode::SAFETY_VIOLATION && mode != FailureMode::UNKNOWN;
}

float MetaDebugger::estimate_recovery_probability(const DebugTrace& trace) const {
    float base_probability = 0.5f;
    
    switch (trace.failure_mode) {
        case FailureMode::TIMEOUT:
            return 0.8f;
        case FailureMode::RESOURCE_EXHAUSTED:
            return 0.75f;
        case FailureMode::INVALID_RESULT:
            return 0.6f;
        case FailureMode::DEPENDENCY_FAILED:
            return 0.5f;
        case FailureMode::SAFETY_VIOLATION:
            return 0.1f;
        case FailureMode::COMPUTATION_ERROR:
            return 0.65f;
        default:
            return base_probability;
    }
}

std::map<std::string, float> MetaDebugger::analyze_resource_state(const std::shared_ptr<ProcessContext>& process) {
    std::map<std::string, float> state;
    
    if (!process) return state;
    
    for (const auto& res_pair : process->resources) {
        std::string type_name;
        switch (res_pair.first) {
            case ResourceType::TOKEN_BUDGET: type_name = "tokens"; break;
            case ResourceType::MEMORY_LIMIT: type_name = "memory"; break;
            case ResourceType::COMPUTATION_TIME: type_name = "compute"; break;
            case ResourceType::CACHE_SIZE: type_name = "cache"; break;
            case ResourceType::ATTENTION_BUDGET: type_name = "attention"; break;
        }
        state[type_name] = res_pair.second.utilization_percent();
    }
    
    return state;
}

std::vector<std::string> MetaDebugger::extract_state_transitions(const std::shared_ptr<ProcessContext>& process) const {
    std::vector<std::string> transitions;
    
    if (!process) return transitions;
    
    for (const auto& transition : process->state_history) {
        transitions.push_back(std::to_string(static_cast<int>(transition.from_state)) + " -> " +
                            std::to_string(static_cast<int>(transition.to_state)));
    }
    
    return transitions;
}

std::string MetaDebugger::failure_mode_to_string(FailureMode mode) const {
    switch (mode) {
        case FailureMode::TIMEOUT: return "TIMEOUT";
        case FailureMode::RESOURCE_EXHAUSTED: return "RESOURCE_EXHAUSTED";
        case FailureMode::INVALID_RESULT: return "INVALID_RESULT";
        case FailureMode::DEPENDENCY_FAILED: return "DEPENDENCY_FAILED";
        case FailureMode::SAFETY_VIOLATION: return "SAFETY_VIOLATION";
        case FailureMode::COMPUTATION_ERROR: return "COMPUTATION_ERROR";
        default: return "UNKNOWN";
    }
}
