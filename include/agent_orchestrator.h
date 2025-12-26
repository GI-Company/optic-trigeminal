#pragma once

#include "types.h"
#include "vfs_manager.h"
#include "rag_dag.h"
#include <map>
#include <memory>
#include <functional>
#include <queue>

enum class AgentRole {
    PLANNER = 0,
    EXECUTOR = 1,
    VERIFIER = 2,
    DEBUGGER = 3,
    OPTIMIZER = 4
};

struct AgentTask {
    std::string task_id;
    std::string description;
    AgentRole role;
    std::string parent_task_id;
    std::vector<std::string> subtask_ids;
    ProcessState current_state;
    float priority;
    int64_t deadline_ms;
    int max_retries;
    int retry_count;
    float success_probability;
    std::vector<std::string> required_tools;
    std::map<std::string, std::string> execution_context;
    int64_t created_at;
    int64_t started_at;
    int64_t completed_at;
    std::string result;
    std::string error_message;
    
    AgentTask() : priority(1.0f), max_retries(3), retry_count(0), 
                  success_probability(0.5f), created_at(0), started_at(0), 
                  completed_at(0), deadline_ms(0), current_state(ProcessState::IDLE) {}
};

struct PlanNode {
    std::string node_id;
    std::string action;
    std::vector<std::string> required_inputs;
    std::vector<std::string> produces_outputs;
    std::vector<std::string> child_nodes;
    std::string parent_node;
    float estimated_cost;
    float confidence;
    int depth;
    
    PlanNode() : estimated_cost(0.0f), confidence(0.5f), depth(0) {}
};

struct ReasoningTrace {
    std::string trace_id;
    std::vector<std::string> process_ids;
    std::vector<DimensionalRetrievalResult> rag_dag_path;
    std::vector<std::pair<std::string, float>> dimension_scores;
    int64_t start_time;
    int64_t end_time;
    float trace_confidence;
    std::vector<std::string> reasoning_steps;
    std::map<std::string, std::string> context_snapshot;
    
    ReasoningTrace() : start_time(0), end_time(0), trace_confidence(0.0f) {}
};

class AgentOrchestrator {
public:
    AgentOrchestrator(VFSManager* vfs, RAGDAGSystem* rag_dag);
    
    std::shared_ptr<AgentTask> spawn_agent_task(const std::string& description,
                                               AgentRole role,
                                               float priority = 1.0f,
                                               const std::string& parent_task_id = "");
    
    std::vector<PlanNode> create_execution_plan(const std::string& goal,
                                               const std::vector<std::string>& available_tools,
                                               int max_depth = 5);
    
    bool execute_plan(const std::vector<PlanNode>& plan,
                     const std::string& goal_task_id);
    
    std::shared_ptr<AgentTask> get_task(const std::string& task_id) const;
    std::vector<std::shared_ptr<AgentTask>> get_active_tasks() const;
    std::vector<std::shared_ptr<AgentTask>> get_tasks_by_role(AgentRole role) const;
    
    void update_task_state(const std::string& task_id, ProcessState new_state,
                          const std::string& result = "");
    
    bool handle_task_failure(const std::string& task_id, const std::string& error_msg);
    
    ReasoningTrace capture_reasoning_trace(const std::string& task_id,
                                          const std::vector<std::string>& process_path);
    
    std::vector<ReasoningTrace> get_task_reasoning_traces(const std::string& task_id) const;
    
    std::string get_execution_plan_json(const std::string& task_id) const;
    
    int get_total_active_tasks() const { return active_tasks.size(); }
    
    void set_rag_dag_system(RAGDAGSystem* rag) { rag_dag_system = rag; }
    void set_vfs_manager(VFSManager* v) { vfs_manager = v; }
    
private:
    VFSManager* vfs_manager;
    RAGDAGSystem* rag_dag_system;
    
    std::map<std::string, std::shared_ptr<AgentTask>> task_map;
    std::set<std::string> active_tasks;
    std::vector<ReasoningTrace> trace_history;
    std::string generate_task_id();
    std::string generate_trace_id();
    
    PlanNode create_plan_node(const std::string& action,
                             const std::vector<std::string>& inputs,
                             const std::vector<std::string>& outputs,
                             float estimated_cost,
                             int depth);
};
