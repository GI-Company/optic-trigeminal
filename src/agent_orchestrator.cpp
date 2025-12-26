#include "agent_orchestrator.h"
#include <sstream>
#include <chrono>
#include <algorithm>

AgentOrchestrator::AgentOrchestrator(VFSManager* vfs, RAGDAGSystem* rag_dag)
    : vfs_manager(vfs), rag_dag_system(rag_dag) {}

std::string AgentOrchestrator::generate_task_id() {
    static int counter = 0;
    return "task_" + std::to_string(counter++) + "_" + 
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
}

std::string AgentOrchestrator::generate_trace_id() {
    static int counter = 0;
    return "trace_" + std::to_string(counter++) + "_" + 
           std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
}

std::shared_ptr<AgentTask> AgentOrchestrator::spawn_agent_task(const std::string& description,
                                                              AgentRole role,
                                                              float priority,
                                                              const std::string& parent_task_id) {
    auto task = std::make_shared<AgentTask>();
    task->task_id = generate_task_id();
    task->description = description;
    task->role = role;
    task->parent_task_id = parent_task_id;
    task->priority = priority;
    task->current_state = ProcessState::INITIALIZING;
    task->created_at = std::chrono::system_clock::now().time_since_epoch().count();
    task->max_retries = 3;
    task->retry_count = 0;
    task->success_probability = 0.7f;
    
    auto process = vfs_manager->create_process(
        "agent_" + std::string(role == AgentRole::PLANNER ? "planner" :
                              role == AgentRole::EXECUTOR ? "executor" :
                              role == AgentRole::VERIFIER ? "verifier" :
                              role == AgentRole::DEBUGGER ? "debugger" : "optimizer"),
        description,
        parent_task_id.empty() ? "" : parent_task_id
    );
    
    if (process) {
        vfs_manager->initialize_process_resources(process->process_id, 5000.0f, 2000.0f, 120000.0f);
        task->execution_context["process_id"] = process->process_id;
    }
    
    task_map[task->task_id] = task;
    active_tasks.insert(task->task_id);
    
    if (!parent_task_id.empty()) {
        auto parent_it = task_map.find(parent_task_id);
        if (parent_it != task_map.end()) {
            parent_it->second->subtask_ids.push_back(task->task_id);
        }
    }
    
    return task;
}

std::vector<PlanNode> AgentOrchestrator::create_execution_plan(const std::string& goal,
                                                              const std::vector<std::string>& available_tools,
                                                              int max_depth) {
    std::vector<PlanNode> plan;
    
    PlanNode root = create_plan_node("analyze_goal", {goal}, {"goal_analysis"}, 1.0f, 0);
    plan.push_back(root);
    
    if (max_depth > 1) {
        PlanNode retrieval = create_plan_node("retrieve_context", {"goal_analysis"}, 
                                             {"context_nodes"}, 2.0f, 1);
        plan.push_back(retrieval);
        
        PlanNode selection = create_plan_node("select_tools", {"context_nodes", "available_tools"},
                                             {"selected_tools"}, 1.5f, 1);
        plan.push_back(selection);
    }
    
    if (max_depth > 2) {
        for (const auto& tool : available_tools) {
            PlanNode execution = create_plan_node("execute_" + tool, {"selected_tools"},
                                                 {"tool_result_" + tool}, 3.0f, 2);
            plan.push_back(execution);
        }
    }
    
    if (max_depth > 3) {
        PlanNode aggregation = create_plan_node("aggregate_results", 
                                               {"tool_result_0", "tool_result_1"},
                                               {"aggregated_result"}, 2.0f, 3);
        plan.push_back(aggregation);
    }
    
    if (max_depth > 4) {
        PlanNode verification = create_plan_node("verify_result", {"aggregated_result"},
                                                {"verified_result"}, 2.5f, 4);
        plan.push_back(verification);
    }
    
    return plan;
}

bool AgentOrchestrator::execute_plan(const std::vector<PlanNode>& plan,
                                    const std::string& goal_task_id) {
    if (plan.empty()) return false;
    
    auto task_it = task_map.find(goal_task_id);
    if (task_it == task_map.end()) return false;
    
    auto task = task_it->second;
    task->current_state = ProcessState::REASONING;
    task->started_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    for (const auto& node : plan) {
        vfs_manager->transition_process_state(
            task->execution_context["process_id"],
            ProcessState::COMPUTING,
            "Executing plan node: " + node.action
        );
    }
    
    return true;
}

std::shared_ptr<AgentTask> AgentOrchestrator::get_task(const std::string& task_id) const {
    auto it = task_map.find(task_id);
    return it != task_map.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<AgentTask>> AgentOrchestrator::get_active_tasks() const {
    std::vector<std::shared_ptr<AgentTask>> result;
    for (const auto& task_id : active_tasks) {
        auto it = task_map.find(task_id);
        if (it != task_map.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

std::vector<std::shared_ptr<AgentTask>> AgentOrchestrator::get_tasks_by_role(AgentRole role) const {
    std::vector<std::shared_ptr<AgentTask>> result;
    for (const auto& pair : task_map) {
        if (pair.second->role == role) {
            result.push_back(pair.second);
        }
    }
    return result;
}

void AgentOrchestrator::update_task_state(const std::string& task_id, ProcessState new_state,
                                         const std::string& result) {
    auto it = task_map.find(task_id);
    if (it == task_map.end()) return;
    
    auto task = it->second;
    task->current_state = new_state;
    task->result = result;
    
    if (new_state == ProcessState::COMPLETE) {
        task->completed_at = std::chrono::system_clock::now().time_since_epoch().count();
        active_tasks.erase(task_id);
    }
}

bool AgentOrchestrator::handle_task_failure(const std::string& task_id, const std::string& error_msg) {
    auto it = task_map.find(task_id);
    if (it == task_map.end()) return false;
    
    auto task = it->second;
    task->error_message = error_msg;
    task->retry_count++;
    
    if (task->retry_count < task->max_retries) {
        task->current_state = ProcessState::INITIALIZING;
        return true;
    }
    
    task->current_state = ProcessState::FAILED;
    active_tasks.erase(task_id);
    return false;
}

ReasoningTrace AgentOrchestrator::capture_reasoning_trace(const std::string& task_id,
                                                         const std::vector<std::string>& process_path) {
    ReasoningTrace trace;
    trace.trace_id = generate_trace_id();
    trace.process_ids = process_path;
    trace.start_time = std::chrono::system_clock::now().time_since_epoch().count();
    trace.trace_confidence = 0.85f;
    
    auto it = task_map.find(task_id);
    if (it != task_map.end()) {
        auto task = it->second;
        trace.context_snapshot["task_id"] = task->task_id;
        trace.context_snapshot["description"] = task->description;
        trace.context_snapshot["state"] = std::to_string(static_cast<int>(task->current_state));
    }
    
    trace_history.push_back(trace);
    return trace;
}

std::vector<ReasoningTrace> AgentOrchestrator::get_task_reasoning_traces(const std::string& task_id) const {
    std::vector<ReasoningTrace> result;
    for (const auto& trace : trace_history) {
        auto it = task_map.find(task_id);
        if (it != task_map.end() && 
            !trace.process_ids.empty() &&
            trace.context_snapshot.count("task_id") &&
            trace.context_snapshot.at("task_id") == task_id) {
            result.push_back(trace);
        }
    }
    return result;
}

std::string AgentOrchestrator::get_execution_plan_json(const std::string& task_id) const {
    std::stringstream ss;
    ss << "{\"task_id\": \"" << task_id << "\", \"plan\": [";
    
    auto it = task_map.find(task_id);
    if (it != task_map.end()) {
        auto task = it->second;
        ss << "{\"goal\": \"" << task->description << "\"}";
    }
    
    ss << "]}";
    return ss.str();
}

PlanNode AgentOrchestrator::create_plan_node(const std::string& action,
                                            const std::vector<std::string>& inputs,
                                            const std::vector<std::string>& outputs,
                                            float estimated_cost,
                                            int depth) {
    PlanNode node;
    static int counter = 0;
    node.node_id = "plan_" + std::to_string(counter++);
    node.action = action;
    node.required_inputs = inputs;
    node.produces_outputs = outputs;
    node.estimated_cost = estimated_cost;
    node.confidence = 0.8f;
    node.depth = depth;
    return node;
}
