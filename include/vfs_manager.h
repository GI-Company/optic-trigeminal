#pragma once

#include "types.h"
#include <queue>
#include <memory>
#include <mutex>

enum class ProcessState {
    IDLE = 0,
    INITIALIZING = 1,
    REASONING = 2,
    COMPUTING = 3,
    RETRIEVING = 4,
    GENERATING = 5,
    COMPLETE = 6,
    FAILED = 7,
    SUSPENDED = 8
};

enum class ResourceType {
    TOKEN_BUDGET = 0,
    MEMORY_LIMIT = 1,
    COMPUTATION_TIME = 2,
    CACHE_SIZE = 3,
    ATTENTION_BUDGET = 4
};

struct ResourceAllocation {
    ResourceType type;
    float max_value;
    float current_usage;
    float peak_usage;
    int64_t allocated_at;
    
    ResourceAllocation() : max_value(0.0f), current_usage(0.0f), 
                          peak_usage(0.0f), allocated_at(0) {}
    ResourceAllocation(ResourceType t, float max_v)
        : type(t), max_value(max_v), current_usage(0.0f), 
          peak_usage(0.0f), allocated_at(std::chrono::system_clock::now().time_since_epoch().count()) {}
    
    float utilization_percent() const {
        return max_value > 0 ? (current_usage / max_value) * 100.0f : 0.0f;
    }
    
    bool is_over_budget() const {
        return current_usage > max_value;
    }
};

struct StateTransition {
    ProcessState from_state;
    ProcessState to_state;
    int64_t transition_time;
    std::string trigger_reason;
    float confidence;
    
    StateTransition() : confidence(0.0f), transition_time(0) {}
    StateTransition(ProcessState from, ProcessState to, const std::string& reason, float conf = 1.0f)
        : from_state(from), to_state(to), trigger_reason(reason), confidence(conf),
          transition_time(std::chrono::system_clock::now().time_since_epoch().count()) {}
};

struct ProcessContext {
    std::string process_id;
    std::string parent_process_id;
    std::string task_name;
    std::string task_description;
    ProcessState current_state;
    Embedding task_embedding;
    
    std::map<ResourceType, ResourceAllocation> resources;
    std::vector<StateTransition> state_history;
    
    std::map<std::string, std::string> local_context;
    std::map<std::string, VectorF> local_embeddings;
    std::map<std::string, float> local_scores;
    
    std::vector<std::string> parent_path;
    std::vector<std::string> children_processes;
    
    int64_t created_at;
    int64_t updated_at;
    int execution_depth;
    int parent_task_depth;
    
    ProcessContext() : current_state(ProcessState::IDLE), execution_depth(0), 
                      parent_task_depth(0), created_at(0), updated_at(0),
                      task_embedding(EMBEDDING_DIM) {}
    
    explicit ProcessContext(const std::string& pid)
        : process_id(pid), current_state(ProcessState::IDLE), execution_depth(0),
          parent_task_depth(0), task_embedding(EMBEDDING_DIM) {
        created_at = std::chrono::system_clock::now().time_since_epoch().count();
        updated_at = created_at;
    }
    
    void transition_to(ProcessState new_state, const std::string& reason, float confidence = 1.0f) {
        StateTransition trans(current_state, new_state, reason, confidence);
        state_history.push_back(trans);
        current_state = new_state;
        updated_at = std::chrono::system_clock::now().time_since_epoch().count();
    }
    
    bool allocate_resource(ResourceType type, float amount) {
        auto it = resources.find(type);
        if (it == resources.end()) return false;
        
        if (it->second.current_usage + amount > it->second.max_value) {
            return false;
        }
        
        it->second.current_usage += amount;
        it->second.peak_usage = std::max(it->second.peak_usage, it->second.current_usage);
        return true;
    }
    
    void release_resource(ResourceType type, float amount) {
        auto it = resources.find(type);
        if (it != resources.end() && it->second.current_usage >= amount) {
            it->second.current_usage -= amount;
        }
    }
    
    float get_resource_utilization(ResourceType type) const {
        auto it = resources.find(type);
        return it != resources.end() ? it->second.utilization_percent() : 0.0f;
    }
};

struct ProofArtifact {
    std::string artifact_id;
    std::string content_hash;
    std::string version;
    std::string module_name;
    std::map<std::string, std::string> metadata;
    int64_t created_at;
    int64_t verified_at;
    bool verified;
    
    ProofArtifact() : created_at(0), verified_at(0), verified(false) {}
};

struct VFSDirectory {
    std::string dir_name;
    std::string parent_dir;
    std::map<std::string, std::shared_ptr<ProcessContext>> child_processes;
    std::map<std::string, std::string> metadata;
    std::vector<ProofArtifact> artifacts;
    std::string version;
    int64_t created_at;
    
    VFSDirectory() : created_at(std::chrono::system_clock::now().time_since_epoch().count()), version("v0.1.0") {}
    explicit VFSDirectory(const std::string& name) : dir_name(name), version("v0.1.0") {
        created_at = std::chrono::system_clock::now().time_since_epoch().count();
    }
};

class VFSManager {
public:
    VFSManager();
    
    std::shared_ptr<ProcessContext> create_process(const std::string& task_name,
                                                   const std::string& description,
                                                   const std::string& parent_id = "");
    
    bool destroy_process(const std::string& process_id);
    
    std::shared_ptr<ProcessContext> get_process(const std::string& process_id) const;
    
    bool mkdir(const std::string& dir_path);
    bool rmdir(const std::string& dir_path);
    
    std::vector<std::string> list_processes(const std::string& parent_id = "") const;
    std::vector<std::string> list_directory(const std::string& dir_path) const;
    
    void initialize_process_resources(const std::string& process_id,
                                     float token_budget = 1000.0f,
                                     float memory_limit = 500.0f,
                                     float compute_time_limit = 30000.0f);
    
    bool transition_process_state(const std::string& process_id,
                                 ProcessState new_state,
                                 const std::string& reason);
    
    std::vector<StateTransition> get_process_state_history(const std::string& process_id) const;
    
    std::map<std::string, float> get_process_resource_usage(const std::string& process_id) const;
    
    std::string get_process_hierarchy_tree(const std::string& root_id = "") const;
    
    ProofArtifact create_proof_artifact(const std::string& content,
                                       const std::string& module_name,
                                       const std::string& version = "v0.1.0");
    
    bool verify_artifact(const std::string& artifact_id);
    
    ProofArtifact get_artifact(const std::string& artifact_id) const;
    
    std::vector<ProofArtifact> get_artifacts_by_module(const std::string& module_name) const;
    
    std::vector<ProofArtifact> get_artifacts_by_version(const std::string& version) const;
    
    bool validate_artifact_hash(const std::string& artifact_id, const std::string& content);
    
    std::string checkpoint_module_state(const std::string& module_name,
                                        const std::string& state_content,
                                        const std::string& version);
    
    std::string get_module_checkpoint(const std::string& module_name, const std::string& version) const;
    
    std::vector<std::string> list_module_versions(const std::string& module_name) const;
    
    int get_total_process_count() const { return process_map.size(); }
    
    int get_total_artifact_count() const { return artifact_map.size(); }
    
private:
    std::map<std::string, std::shared_ptr<ProcessContext>> process_map;
    std::map<std::string, std::shared_ptr<VFSDirectory>> directory_map;
    std::map<std::string, ProofArtifact> artifact_map;
    std::map<std::string, std::vector<std::string>> module_artifacts;
    std::string root_process_id;
    mutable std::mutex vfs_mutex;
    
    std::string generate_process_id();
    std::shared_ptr<VFSDirectory> get_directory(const std::string& path) const;
    std::string compute_hash(const std::string& content) const;
};
