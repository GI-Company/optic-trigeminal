#pragma once

#include "vfs_manager.h"
#include "rag_dag.h"
#include "agent_orchestrator.h"
#include "meta_debugger.h"
#include "cognitive_load_balancer.h"
#include "long_horizon_planner.h"
#include <string>
#include <memory>
#include <thread>
#include <map>

class DebugServer {
public:
    DebugServer(int port, 
               VFSManager* vfs,
               RAGDAGSystem* rag_dag,
               AgentOrchestrator* orchestrator,
               MetaDebugger* debugger,
               CognitiveLoadBalancer* load_balancer,
               LongHorizonPlanner* planner);
    
    ~DebugServer();
    
    bool start();
    void stop();
    bool is_running() const { return running; }
    
private:
    int port;
    bool running;
    std::unique_ptr<std::thread> server_thread;
    
    VFSManager* vfs_manager;
    RAGDAGSystem* rag_dag_system;
    AgentOrchestrator* agent_orchestrator;
    MetaDebugger* meta_debugger;
    CognitiveLoadBalancer* load_balancer;
    LongHorizonPlanner* horizon_planner;
    
    void run_server();
    
    std::string handle_vfs_tree_request(const std::string& root_id = "");
    std::string handle_reasoning_trace_request(const std::string& task_id);
    std::string handle_load_report_request();
    std::string handle_debug_trace_request(const std::string& process_id);
    std::string handle_horizon_plan_request();
    std::string handle_agent_tasks_request();
    
    std::string json_escape(const std::string& str) const;
};
