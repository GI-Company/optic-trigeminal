#pragma once

#include "types.h"
#include "inference_engine.h"
#include "clinical_sim.h"
#include "clinical_analyzer.h"
#include "training_scenario.h"
#include "training_analytics.h"
#include <memory>
#include <string>
#include <thread>
#include <map>

class HTTPServer {
public:
    struct Request {
        std::string method;
        std::string path;
        std::string body;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> query_params;
    };
    
    struct Response {
        int status_code;
        std::string body;
        std::string content_type;
        std::map<std::string, std::string> headers;
        
        Response(int code = 200, const std::string& b = "", const std::string& type = "application/json")
            : status_code(code), body(b), content_type(type) {}
    };
    
    using RequestHandler = std::function<Response(const Request&)>;
    
private:
    int port;
    std::unique_ptr<NativeInferenceEngine> engine;
    ClinicalSimulator sim;
    std::unique_ptr<ClinicalAnalyzer> analyzer;
    std::map<std::string, RequestHandler> routes;
    bool running;
    std::thread server_thread;
    
    // Training mode state
    std::unique_ptr<ScenarioRuntime> active_scenario_;
    bool training_mode_active_;
    std::map<std::string, TrainingSession> training_sessions_;
    std::unique_ptr<TrainingAnalyticsStore> analytics_store_;
    std::string current_training_session_id_;
    
    void register_routes();
    void start_server();
    void handle_client(int client_socket);
    
    Request parse_http_request(const std::string& raw_request);
    std::string build_http_response(const Response& response);
    
    Response handle_infer(const Request& req);
    Response handle_status(const Request& req);
    Response handle_learn(const Request& req);
    Response handle_health(const Request& req);
    Response handle_observations(const Request& req);
    Response handle_scaffold(const Request& req);
    Response handle_action(const Request& req);
    
    // Training mode handlers
    Response handle_training_start(const Request& req);
    Response handle_training_status(const Request& req);
    Response handle_training_action(const Request& req);
    Response handle_training_tick(const Request& req);
    Response handle_training_end(const Request& req);
    Response handle_training_list(const Request& req);
    Response handle_training_analytics(const Request& req);
    Response handle_training_report(const Request& req);
    
    Response handle_not_found(const Request& req);
    
    // Static file handling
    Response handle_static(const Request& req);
    std::string get_mime_type(const std::string& path);
    
    std::string json_escape(const std::string& str);
    
public:
    HTTPServer(int port = 8080);
    ~HTTPServer();
    
    bool initialize(const std::string& data_directory = "data");
    bool start();
    void stop();
    bool is_running() const { return running; }
    
    NativeInferenceEngine* get_engine() { return engine.get(); }
};
