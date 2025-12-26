#pragma once

#include "types.h"
#include "vfs_manager.h"
#include <map>
#include <memory>
#include <vector>

enum class FailureMode {
    TIMEOUT = 0,
    RESOURCE_EXHAUSTED = 1,
    INVALID_RESULT = 2,
    DEPENDENCY_FAILED = 3,
    SAFETY_VIOLATION = 4,
    COMPUTATION_ERROR = 5,
    UNKNOWN = 6
};

struct DebugTrace {
    std::string process_id;
    FailureMode failure_mode;
    std::string failure_description;
    std::map<std::string, float> resource_state;
    std::vector<std::string> state_transitions;
    int64_t failure_time;
    int diagnostics_run;
    std::vector<std::string> recommended_fixes;
    float recovery_confidence;
    
    DebugTrace() : diagnostics_run(0), recovery_confidence(0.0f), failure_time(0) {}
};

struct RetryStrategy {
    int max_retries;
    int current_retry;
    float backoff_multiplier;
    int backoff_ms;
    std::map<ResourceType, float> modified_resources;
    std::map<std::string, std::string> modified_params;
    bool reduce_complexity;
    bool increase_timeout;
    
    RetryStrategy() : max_retries(3), current_retry(0), backoff_multiplier(2.0f),
                     backoff_ms(100), reduce_complexity(false), increase_timeout(false) {}
};

class MetaDebugger {
public:
    MetaDebugger(VFSManager* vfs);
    
    DebugTrace analyze_process_failure(const std::string& process_id);
    
    FailureMode diagnose_failure_mode(const std::shared_ptr<ProcessContext>& process);
    
    RetryStrategy create_retry_strategy(const DebugTrace& trace, int base_retries = 3);
    
    bool execute_retry(const std::string& original_process_id,
                      const RetryStrategy& strategy,
                      std::string& new_process_id);
    
    std::vector<std::string> generate_diagnostic_report(const std::string& process_id) const;
    
    std::string get_failure_summary(const std::string& process_id) const;
    
    bool is_process_recoverable(const FailureMode mode) const;
    
    float estimate_recovery_probability(const DebugTrace& trace) const;
    
    std::vector<DebugTrace> get_debug_history() const { return debug_history; }
    
    void clear_history() { debug_history.clear(); }
    
private:
    VFSManager* vfs_manager;
    std::vector<DebugTrace> debug_history;
    std::map<FailureMode, std::vector<std::string>> failure_remedies;
    
    void initialize_remedies();
    
    std::map<std::string, float> analyze_resource_state(const std::shared_ptr<ProcessContext>& process);
    
    std::vector<std::string> extract_state_transitions(const std::shared_ptr<ProcessContext>& process) const;
    
    std::string failure_mode_to_string(FailureMode mode) const;
};
