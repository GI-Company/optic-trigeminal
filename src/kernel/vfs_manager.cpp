#include "vfs_manager.h"
#include <sstream>
#include <ctime>
#include <iostream>
#include <set>
#include <chrono>

inline std::string process_state_to_string(ProcessState state) {
    switch (state) {
        case ProcessState::IDLE: return "IDLE";
        case ProcessState::INITIALIZING: return "INITIALIZING";
        case ProcessState::REASONING: return "REASONING";
        case ProcessState::COMPUTING: return "COMPUTING";
        case ProcessState::RETRIEVING: return "RETRIEVING";
        case ProcessState::GENERATING: return "GENERATING";
        case ProcessState::COMPLETE: return "COMPLETE";
        case ProcessState::FAILED: return "FAILED";
        case ProcessState::SUSPENDED: return "SUSPENDED";
        default: return "UNKNOWN";
    }
}

VFSManager::VFSManager() {
    root_process_id = "root_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    auto root_process = std::make_shared<ProcessContext>(root_process_id);
    root_process->task_name = "ROOT";
    root_process->task_description = "Root system process";
    root_process->execution_depth = 0;
    root_process->current_state = ProcessState::COMPLETE;
    
    process_map[root_process_id] = root_process;
    directory_map["root"] = std::make_shared<VFSDirectory>("root");
}

std::string VFSManager::generate_process_id() {
    static int counter = 0;
    return "proc_" + std::to_string(counter++) + "_" + 
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
}

std::shared_ptr<ProcessContext> VFSManager::create_process(const std::string& task_name,
                                                          const std::string& description,
                                                          const std::string& parent_id) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::string process_id = generate_process_id();
    auto new_process = std::make_shared<ProcessContext>(process_id);
    
    new_process->task_name = task_name;
    new_process->task_description = description;
    new_process->current_state = ProcessState::INITIALIZING;
    new_process->transition_to(ProcessState::IDLE, "Created");
    
    if (!parent_id.empty()) {
        auto parent_it = process_map.find(parent_id);
        if (parent_it != process_map.end()) {
            new_process->parent_process_id = parent_id;
            new_process->execution_depth = parent_it->second->execution_depth + 1;
            parent_it->second->children_processes.push_back(process_id);
            new_process->parent_path = parent_it->second->parent_path;
            new_process->parent_path.push_back(parent_id);
        }
    } else {
        new_process->parent_process_id = root_process_id;
        new_process->execution_depth = 1;
        process_map[root_process_id]->children_processes.push_back(process_id);
    }
    
    process_map[process_id] = new_process;
    return new_process;
}

bool VFSManager::destroy_process(const std::string& process_id) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = process_map.find(process_id);
    if (it == process_map.end()) return false;
    
    auto process = it->second;
    
    for (const auto& child_id : process->children_processes) {
        destroy_process(child_id);
    }
    
    if (!process->parent_process_id.empty()) {
        auto parent_it = process_map.find(process->parent_process_id);
        if (parent_it != process_map.end()) {
            auto& children = parent_it->second->children_processes;
            children.erase(std::find(children.begin(), children.end(), process_id));
        }
    }
    
    process_map.erase(it);
    return true;
}

std::shared_ptr<ProcessContext> VFSManager::get_process(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = process_map.find(process_id);
    return it != process_map.end() ? it->second : nullptr;
}

bool VFSManager::mkdir(const std::string& dir_path) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    if (directory_map.count(dir_path) > 0) return false;
    
    directory_map[dir_path] = std::make_shared<VFSDirectory>(dir_path);
    return true;
}

bool VFSManager::rmdir(const std::string& dir_path) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = directory_map.find(dir_path);
    if (it == directory_map.end()) return false;
    
    if (!it->second->child_processes.empty()) return false;
    
    directory_map.erase(it);
    return true;
}

std::vector<std::string> VFSManager::list_processes(const std::string& parent_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::vector<std::string> result;
    std::string search_parent = parent_id.empty() ? root_process_id : parent_id;
    
    auto it = process_map.find(search_parent);
    if (it != process_map.end()) {
        result = it->second->children_processes;
    }
    
    return result;
}

std::vector<std::string> VFSManager::list_directory(const std::string& dir_path) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::vector<std::string> result;
    
    auto it = directory_map.find(dir_path);
    if (it != directory_map.end()) {
        for (const auto& pair : it->second->child_processes) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

void VFSManager::initialize_process_resources(const std::string& process_id,
                                             float token_budget,
                                             float memory_limit,
                                             float compute_time_limit) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = process_map.find(process_id);
    if (it == process_map.end()) return;
    
    auto process = it->second;
    process->resources[ResourceType::TOKEN_BUDGET] = ResourceAllocation(ResourceType::TOKEN_BUDGET, token_budget);
    process->resources[ResourceType::MEMORY_LIMIT] = ResourceAllocation(ResourceType::MEMORY_LIMIT, memory_limit);
    process->resources[ResourceType::COMPUTATION_TIME] = ResourceAllocation(ResourceType::COMPUTATION_TIME, compute_time_limit);
    process->resources[ResourceType::CACHE_SIZE] = ResourceAllocation(ResourceType::CACHE_SIZE, memory_limit * 0.2f);
    process->resources[ResourceType::ATTENTION_BUDGET] = ResourceAllocation(ResourceType::ATTENTION_BUDGET, 100.0f);
}

bool VFSManager::transition_process_state(const std::string& process_id,
                                         ProcessState new_state,
                                         const std::string& reason) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = process_map.find(process_id);
    if (it == process_map.end()) return false;
    
    it->second->transition_to(new_state, reason);
    return true;
}

std::vector<StateTransition> VFSManager::get_process_state_history(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = process_map.find(process_id);
    if (it != process_map.end()) {
        return it->second->state_history;
    }
    return {};
}

std::map<std::string, float> VFSManager::get_process_resource_usage(const std::string& process_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::map<std::string, float> result;
    
    auto it = process_map.find(process_id);
    if (it != process_map.end()) {
        for (const auto& res_pair : it->second->resources) {
            std::string type_name;
            switch (res_pair.first) {
                case ResourceType::TOKEN_BUDGET: type_name = "tokens"; break;
                case ResourceType::MEMORY_LIMIT: type_name = "memory"; break;
                case ResourceType::COMPUTATION_TIME: type_name = "compute_ms"; break;
                case ResourceType::CACHE_SIZE: type_name = "cache"; break;
                case ResourceType::ATTENTION_BUDGET: type_name = "attention"; break;
            }
            result[type_name] = res_pair.second.utilization_percent();
        }
    }
    
    return result;
}

std::string VFSManager::get_process_hierarchy_tree(const std::string& root_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::string start_id = root_id.empty() ? root_process_id : root_id;
    std::stringstream ss;
    
    std::function<void(const std::string&, int)> build_tree = 
        [&](const std::string& pid, int depth) {
        auto it = process_map.find(pid);
        if (it == process_map.end()) return;
        
        std::string indent(depth * 2, ' ');
        auto proc = it->second;
        ss << indent << "[" << process_state_to_string(proc->current_state) << "] " << proc->task_name;
        ss << " (id: " << pid.substr(0, 8) << "...)\n";
        
        for (const auto& child_id : proc->children_processes) {
            build_tree(child_id, depth + 1);
        }
    };
    
    build_tree(start_id, 0);
    return ss.str();
}

std::shared_ptr<VFSDirectory> VFSManager::get_directory(const std::string& path) const {
    auto it = directory_map.find(path);
    return it != directory_map.end() ? it->second : nullptr;
}

std::string VFSManager::compute_hash(const std::string& content) const {
    uint64_t hash = 0xcbf29ce484222325ULL;
    uint64_t prime = 0x100000001b3ULL;
    for (unsigned char c : content) {
        hash ^= c;
        hash *= prime;
    }
    
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

ProofArtifact VFSManager::create_proof_artifact(const std::string& content,
                                               const std::string& module_name,
                                               const std::string& version) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    ProofArtifact artifact;
    static int counter = 0;
    artifact.artifact_id = "artifact_" + std::to_string(counter++) + "_" + 
                          std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    artifact.content_hash = compute_hash(content);
    artifact.version = version;
    artifact.module_name = module_name;
    artifact.created_at = std::chrono::system_clock::now().time_since_epoch().count();
    artifact.verified = false;
    
    artifact_map[artifact.artifact_id] = artifact;
    module_artifacts[module_name].push_back(artifact.artifact_id);
    
    return artifact;
}

bool VFSManager::verify_artifact(const std::string& artifact_id) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = artifact_map.find(artifact_id);
    if (it == artifact_map.end()) return false;
    
    it->second.verified = true;
    it->second.verified_at = std::chrono::system_clock::now().time_since_epoch().count();
    return true;
}

ProofArtifact VFSManager::get_artifact(const std::string& artifact_id) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = artifact_map.find(artifact_id);
    if (it != artifact_map.end()) {
        return it->second;
    }
    return ProofArtifact();
}

std::vector<ProofArtifact> VFSManager::get_artifacts_by_module(const std::string& module_name) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::vector<ProofArtifact> result;
    auto it = module_artifacts.find(module_name);
    
    if (it != module_artifacts.end()) {
        for (const auto& artifact_id : it->second) {
            auto artifact_it = artifact_map.find(artifact_id);
            if (artifact_it != artifact_map.end()) {
                result.push_back(artifact_it->second);
            }
        }
    }
    
    return result;
}

std::vector<ProofArtifact> VFSManager::get_artifacts_by_version(const std::string& version) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::vector<ProofArtifact> result;
    for (const auto& pair : artifact_map) {
        if (pair.second.version == version) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

bool VFSManager::validate_artifact_hash(const std::string& artifact_id, const std::string& content) {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = artifact_map.find(artifact_id);
    if (it == artifact_map.end()) return false;
    
    std::string computed_hash = compute_hash(content);
    return it->second.content_hash == computed_hash;
}

std::string VFSManager::checkpoint_module_state(const std::string& module_name,
                                               const std::string& state_content,
                                               const std::string& version) {
    ProofArtifact artifact = create_proof_artifact(state_content, module_name, version);
    
    std::lock_guard<std::mutex> lock(vfs_mutex);
    artifact_map[artifact.artifact_id].metadata["type"] = "checkpoint";
    artifact_map[artifact.artifact_id].metadata["module"] = module_name;
    artifact_map[artifact.artifact_id].verified = true;
    artifact_map[artifact.artifact_id].verified_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    return artifact.artifact_id;
}

std::string VFSManager::get_module_checkpoint(const std::string& module_name, const std::string& version) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    auto it = module_artifacts.find(module_name);
    if (it == module_artifacts.end()) return "";
    
    for (const auto& artifact_id : it->second) {
        auto artifact_it = artifact_map.find(artifact_id);
        if (artifact_it != artifact_map.end() &&
            artifact_it->second.version == version &&
            artifact_it->second.metadata.count("type") &&
            artifact_it->second.metadata.at("type") == "checkpoint") {
            return artifact_id;
        }
    }
    
    return "";
}

std::vector<std::string> VFSManager::list_module_versions(const std::string& module_name) const {
    std::lock_guard<std::mutex> lock(vfs_mutex);
    
    std::vector<std::string> versions;
    auto it = module_artifacts.find(module_name);
    
    if (it != module_artifacts.end()) {
        std::set<std::string> unique_versions;
        for (const auto& artifact_id : it->second) {
            auto artifact_it = artifact_map.find(artifact_id);
            if (artifact_it != artifact_map.end()) {
                unique_versions.insert(artifact_it->second.version);
            }
        }
        versions.insert(versions.end(), unique_versions.begin(), unique_versions.end());
    }
    
    return versions;
}
