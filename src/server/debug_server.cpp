#include "debug_server.h"
#include <iostream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

DebugServer::DebugServer(int p, 
                        VFSManager* vfs,
                        RAGDAGSystem* rag_dag,
                        AgentOrchestrator* orchestrator,
                        MetaDebugger* debugger,
                        CognitiveLoadBalancer* load_bal,
                        LongHorizonPlanner* planner)
    : port(p), running(false), vfs_manager(vfs), rag_dag_system(rag_dag),
      agent_orchestrator(orchestrator), meta_debugger(debugger),
      load_balancer(load_bal), horizon_planner(planner) {}

DebugServer::~DebugServer() {
    stop();
}

bool DebugServer::start() {
    if (running) return false;
    
    running = true;
    server_thread = std::make_unique<std::thread>(&DebugServer::run_server, this);
    
    std::cout << "[DebugServer] Started on port " << port << std::endl;
    return true;
}

void DebugServer::stop() {
    running = false;
    if (server_thread && server_thread->joinable()) {
        server_thread->join();
    }
}

void DebugServer::run_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "[DebugServer] Socket creation failed" << std::endl;
        return;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "[DebugServer] Setsockopt failed" << std::endl;
        close(server_fd);
        return;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[DebugServer] Bind failed on port " << port << std::endl;
        close(server_fd);
        return;
    }
    
    listen(server_fd, 5);
    std::cout << "[DebugServer] Debug server listening on port " << port << std::endl;
    
    while (running) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;
        
        std::thread([this](int fd) {
            char buffer[8192] = {0};
            int bytes_read = read(fd, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                std::string raw_request(buffer);
                
                std::string method, path, version;
                std::istringstream stream(raw_request);
                stream >> method >> path >> version;
                
                std::string response;
                
                if (method == "GET") {
                    if (path == "/api/vfs/tree") {
                        response = handle_vfs_tree_request();
                    } else if (path == "/api/reasoning/trace") {
                        response = handle_reasoning_trace_request("default");
                    } else if (path == "/api/load") {
                        response = handle_load_report_request();
                    } else if (path.substr(0, 11) == "/api/debug/") {
                        std::string pid = path.substr(11);
                        response = handle_debug_trace_request(pid);
                    } else if (path == "/api/horizon/plan") {
                        response = handle_horizon_plan_request();
                    } else if (path == "/api/agents/tasks") {
                        response = handle_agent_tasks_request();
                    } else {
                        response = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n\r\n";
                        response += "{\"error\": \"Endpoint not found\"}\r\n";
                    }
                } else {
                    response = "HTTP/1.1 405 Method Not Allowed\r\nContent-Type: application/json\r\n\r\n";
                    response += "{\"error\": \"Only GET requests allowed\"}\r\n";
                }
                
                write(fd, response.c_str(), response.length());
            }
            
            close(fd);
        }, client_fd).detach();
    }
    
    close(server_fd);
}

std::string DebugServer::handle_vfs_tree_request(const std::string& root_id) {
    std::stringstream json;
    
    json << "{\r\n";
    json << "  \"type\": \"vfs_tree\",\r\n";
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << ",\r\n";
    json << "  \"total_processes\": " << vfs_manager->get_total_process_count() << ",\r\n";
    json << "  \"tree\": \"" << json_escape(vfs_manager->get_process_hierarchy_tree(root_id)) << "\"\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::handle_reasoning_trace_request(const std::string& task_id) {
    std::stringstream json;
    
    json << "{\r\n";
    json << "  \"type\": \"reasoning_trace\",\r\n";
    json << "  \"task_id\": \"" << task_id << "\",\r\n";
    
    auto task = agent_orchestrator->get_task(task_id);
    if (task) {
        auto traces = agent_orchestrator->get_task_reasoning_traces(task_id);
        json << "  \"traces_count\": " << traces.size() << ",\r\n";
        json << "  \"task_state\": " << static_cast<int>(task->current_state) << ",\r\n";
        json << "  \"task_priority\": " << task->priority << ",\r\n";
        json << "  \"description\": \"" << json_escape(task->description) << "\",\r\n";
        json << "  \"traces\": [\r\n";
        for (size_t i = 0; i < traces.size(); ++i) {
            std::string combined_steps;
            for(const auto& s : traces[i].reasoning_steps) {
                combined_steps += s;
                combined_steps += " ";
            }
            json << "    {\"trace\": \"" << json_escape(combined_steps) << "\"}";
            if (i < traces.size() - 1) json << ",";
            json << "\r\n";
        }
        json << "  ]\r\n";
    } else {
        json << "  \"error\": \"Task not found\",\r\n";
        json << "  \"available_task_count\": " << agent_orchestrator->get_active_tasks().size() << "\r\n";
    }
    
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << "\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::handle_load_report_request() {
    auto load = load_balancer->measure_system_load();
    
    std::stringstream json;
    json << "{\r\n";
    json << "  \"type\": \"system_load\",\r\n";
    json << "  \"load_level\": " << static_cast<int>(load.current_level) << ",\r\n";
    json << "  \"load_level_name\": \"";
    switch (load.current_level) {
        case LoadLevel::IDLE: json << "IDLE"; break;
        case LoadLevel::LOW: json << "LOW"; break;
        case LoadLevel::MEDIUM: json << "MEDIUM"; break;
        case LoadLevel::HIGH: json << "HIGH"; break;
        case LoadLevel::CRITICAL: json << "CRITICAL"; break;
        default: json << "UNKNOWN"; break;
    }
    json << "\",\r\n";
    json << "  \"token_utilization\": " << load.total_token_utilization << ",\r\n";
    json << "  \"memory_utilization\": " << load.total_memory_utilization << ",\r\n";
    json << "  \"compute_utilization\": " << load.total_compute_utilization << ",\r\n";
    json << "  \"attention_utilization\": " << load.attention_utilization << ",\r\n";
    json << "  \"active_processes\": " << load.active_process_count << ",\r\n";
    json << "  \"suspended_processes\": " << load.suspended_process_count << ",\r\n";
    json << "  \"system_temperature\": " << load.system_temperature << ",\r\n";
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << "\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::handle_debug_trace_request(const std::string& process_id) {
    auto trace = meta_debugger->analyze_process_failure(process_id);
    
    std::stringstream json;
    json << "{\r\n";
    json << "  \"type\": \"debug_trace\",\r\n";
    json << "  \"process_id\": \"" << process_id << "\",\r\n";
    json << "  \"failure_mode\": " << static_cast<int>(trace.failure_mode) << ",\r\n";
    json << "  \"failure_mode_name\": \"";
    switch (trace.failure_mode) {
        case FailureMode::TIMEOUT: json << "TIMEOUT"; break;
        case FailureMode::RESOURCE_EXHAUSTED: json << "RESOURCE_EXHAUSTED"; break;
        case FailureMode::INVALID_RESULT: json << "INVALID_RESULT"; break;
        case FailureMode::DEPENDENCY_FAILED: json << "DEPENDENCY_FAILED"; break;
        case FailureMode::SAFETY_VIOLATION: json << "SAFETY_VIOLATION"; break;
        case FailureMode::COMPUTATION_ERROR: json << "COMPUTATION_ERROR"; break;
        default: json << "UNKNOWN"; break;
    }
    json << "\",\r\n";
    json << "  \"failure_description\": \"" << json_escape(trace.failure_description) << "\",\r\n";
    json << "  \"recovery_confidence\": " << trace.recovery_confidence << ",\r\n";
    json << "  \"recommended_fixes_count\": " << trace.recommended_fixes.size() << ",\r\n";
    json << "  \"recommended_fixes\": [\r\n";
    for (size_t i = 0; i < trace.recommended_fixes.size(); ++i) {
        json << "    {\"fix\": \"" << json_escape(trace.recommended_fixes[i]) << "\"}";
        if (i < trace.recommended_fixes.size() - 1) json << ",";
        json << "\r\n";
    }
    json << "  ],\r\n";
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << "\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::handle_horizon_plan_request() {
    auto timeline = horizon_planner->get_timeline_visualization();
    auto overall_progress = horizon_planner->estimate_total_progress();
    
    std::stringstream json;
    json << "{\r\n";
    json << "  \"type\": \"horizon_plan\",\r\n";
    json << "  \"overall_progress\": " << overall_progress << ",\r\n";
    json << "  \"timeline_visualization\": \"" << json_escape(timeline) << "\",\r\n";
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << "\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::handle_agent_tasks_request() {
    auto tasks = agent_orchestrator->get_active_tasks();
    
    std::stringstream json;
    json << "{\r\n";
    json << "  \"type\": \"agent_tasks\",\r\n";
    json << "  \"active_tasks\": " << tasks.size() << ",\r\n";
    json << "  \"tasks\": [\r\n";
    
    for (size_t i = 0; i < tasks.size(); ++i) {
        json << "    {\r\n";
        json << "      \"task_id\": \"" << tasks[i]->task_id << "\",\r\n";
        json << "      \"description\": \"" << json_escape(tasks[i]->description) << "\",\r\n";
        json << "      \"priority\": " << tasks[i]->priority << ",\r\n";
        json << "      \"state\": " << static_cast<int>(tasks[i]->current_state) << ",\r\n";
        json << "      \"state_name\": \"";
        switch (tasks[i]->current_state) {
            case ProcessState::IDLE: json << "IDLE"; break;
            case ProcessState::INITIALIZING: json << "INITIALIZING"; break;
            case ProcessState::REASONING: json << "REASONING"; break;
            case ProcessState::COMPUTING: json << "COMPUTING"; break;
            case ProcessState::RETRIEVING: json << "RETRIEVING"; break;
            case ProcessState::GENERATING: json << "GENERATING"; break;
            case ProcessState::COMPLETE: json << "COMPLETE"; break;
            case ProcessState::FAILED: json << "FAILED"; break;
            case ProcessState::SUSPENDED: json << "SUSPENDED"; break;
            default: json << "UNKNOWN"; break;
        }
        json << "\"\r\n";
        json << "    }";
        if (i < tasks.size() - 1) json << ",";
        json << "\r\n";
    }
    
    json << "  ],\r\n";
    json << "  \"timestamp\": " << std::chrono::system_clock::now().time_since_epoch().count() << "\r\n";
    json << "}\r\n";
    
    std::string json_str = json.str();
    
    std::stringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Content-Length: " << json_str.length() << "\r\n";
    response << "\r\n";
    response << json_str;
    
    return response.str();
}

std::string DebugServer::json_escape(const std::string& str) const {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    return escaped;
}
