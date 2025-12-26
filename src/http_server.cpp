#include "http_server.h"
#include "data_loader.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

HTTPServer::HTTPServer(int port) 
    : port(port), running(false) {
    engine = std::make_unique<NativeInferenceEngine>();
    sim.initialize(6); // ACmK Clinical Simulation Init
    analyzer = std::make_unique<ClinicalAnalyzer>(); // Clinical Intelligence Init
}

HTTPServer::~HTTPServer() {
    stop();
}

bool HTTPServer::initialize(const std::string& data_directory) {
    DataLoader loader;
    std::cout << "[HTTPServer] Loading datasets from " << data_directory << "..." << std::endl;
    if (!loader.load_datasets(data_directory)) {
        std::cerr << "Failed to load datasets" << std::endl;
    }
    std::cout << "[HTTPServer] Datasets loaded. Building vocabulary..." << std::endl;
    
    VectorStr vocab;
    loader.build_vocabulary(vocab, VOCAB_SIZE);
    engine->set_vocabulary(vocab); // Set vocabulary in NativeInferenceEngine
    std::cout << "[HTTPServer] Vocabulary built (size: " << vocab.size() << "). Setting sequence decoder..." << std::endl;
    
    // Create SequenceDecoder after vocabulary is built
    std::unique_ptr<SequenceDecoder> sequence_decoder_instance = std::make_unique<SequenceDecoder>(vocab);
    engine->set_sequence_decoder(std::move(sequence_decoder_instance));
    
    engine->set_context_window(4096);
    
    std::cout << "[HTTPServer] Loading training data into knowledge graph..." << std::endl;
    auto examples = loader.get_loaded_examples();
    
    if (!engine->initialize_with_training_data(examples)) {
        std::cerr << "Failed to initialize inference engine with training data" << std::endl;
        return false;
    }
    
    std::cout << "Server initialized successfully" << std::endl;

    std::cout << "  Vocab size: " << engine->get_vocab_size() << std::endl;
    std::cout << "  Graph nodes: " << engine->get_graph_node_count() << std::endl;
    std::cout << "  Episodic memory: " << engine->get_episodic_memory_size() << std::endl;
    std::cout << "  Context window: 4096 tokens" << std::endl;
    
    register_routes();
    
    return true;
}

void HTTPServer::register_routes() {
    routes["/api/inference/native/infer"] = [this](const Request& req) { return handle_infer(req); };
    routes["/api/inference/native/status"] = [this](const Request& req) { return handle_status(req); };
    routes["/api/inference/native/learn"] = [this](const Request& req) { return handle_learn(req); };
    routes["/api/clinical/observations"] = [this](const Request& r) { return handle_observations(r); };
    routes["/api/clinical/scaffold"] = [this](const Request& r) { return handle_scaffold(r); };
    routes["/api/clinical/action"] = [this](const Request& r) { return handle_action(r); };
    routes["/api/training/start"] = [this](const Request& r) { return handle_training_start(r); };
    routes["/api/training/status"] = [this](const Request& r) { return handle_training_status(r); };
    routes["/api/training/action"] = [this](const Request& r) { return handle_training_action(r); };
    routes["/api/training/tick"] = [this](const Request& r) { return handle_training_tick(r); };
    routes["/api/training/end"] = [this](const Request& r) { return handle_training_end(r); };
    routes["/api/training/list"] = [this](const Request& r) { return handle_training_list(r); };
    routes["/api/training/analytics"] = [this](const Request& r) { return handle_training_analytics(r); };
    routes["/api/training/report"] = [this](const Request& r) { return handle_training_report(r); };
    routes["/health"] = [this](const Request& req) { return handle_health(req); };
    
    training_mode_active_ = false;
    analytics_store_ = std::make_unique<TrainingAnalyticsStore>();
    analytics_store_->initialize();
}

bool HTTPServer::start() {
    running = true;
    server_thread = std::thread([this]() { start_server(); });
    return true;
}

void HTTPServer::stop() {
    running = false;
    if (server_thread.joinable()) {
        server_thread.join();
    }
}

void HTTPServer::start_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Setsockopt failed" << std::endl;
        close(server_fd);
        return;
    }
    
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return;
    }
    
    listen(server_fd, 5);
    std::cout << "HTTP Server listening on port " << port << std::endl;
    
    while (running) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;
        
        std::thread(&HTTPServer::handle_client, this, client_fd).detach();
    }
    
    close(server_fd);
}

void HTTPServer::handle_client(int client_socket) {
    char buffer[8192] = {0};
    int bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        std::string raw_request(buffer);
        Request request = parse_http_request(raw_request);
        
        Response response;
        
        auto it = routes.find(request.path);
        if (it != routes.end()) {
            response = it->second(request);
        } else {
            // Try to serve static file
            response = handle_static(request);
            // If still 404, it stays as the default 404 from handle_static
        }
        
        std::string http_response = build_http_response(response);
        write(client_socket, http_response.c_str(), http_response.length());
    }
    
    close(client_socket);
}

HTTPServer::Request HTTPServer::parse_http_request(const std::string& raw_request) {
    Request request;
    std::istringstream stream(raw_request);
    
    std::string full_path;
    stream >> request.method >> full_path;
    
    // Split path and query string
    size_t question_mark = full_path.find('?');
    if (question_mark != std::string::npos) {
        request.path = full_path.substr(0, question_mark);
        std::string query_string = full_path.substr(question_mark + 1);
        
        // Parse query parameters
        size_t start = 0;
        while (start < query_string.length()) {
            size_t amp = query_string.find('&', start);
            size_t end = (amp == std::string::npos) ? query_string.length() : amp;
            
            std::string param = query_string.substr(start, end - start);
            size_t eq = param.find('=');
            if (eq != std::string::npos) {
                std::string key = param.substr(0, eq);
                std::string value = param.substr(eq + 1);
                request.query_params[key] = value;
            }
            
            start = (amp == std::string::npos) ? query_string.length() : amp + 1;
        }
    } else {
        request.path = full_path;
    }
    
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line == "\r") break;
        
        size_t colon = line.find(":");
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            value.erase(0, value.find_first_not_of(" \t\r"));
            request.headers[key] = value;
        }
    }
    
    std::string body_line;
    while (std::getline(stream, body_line)) {
        request.body += body_line + "\n";
    }
    
    return request;
}

std::string HTTPServer::build_http_response(const Response& response) {
    std::string http_response = "HTTP/1.1 " + std::to_string(response.status_code) + " OK\r\n";
    http_response += "Content-Type: " + response.content_type + "\r\n";
    http_response += "Content-Length: " + std::to_string(response.body.length()) + "\r\n";
    http_response += "Connection: close\r\n";
    
    // Add custom headers
    for (const auto& header : response.headers) {
        http_response += header.first + ": " + header.second + "\r\n";
    }
    
    http_response += "\r\n";
    http_response += response.body;
    
    return http_response;
}

std::string HTTPServer::json_escape(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c;
        }
    }
    return escaped;
}

HTTPServer::Response HTTPServer::handle_infer(const Request& req) {
    std::string prompt;
    std::string session_id = "default";
    int max_tokens = 128;
    
    size_t prompt_pos = req.body.find("\"prompt\"");
    if (prompt_pos != std::string::npos) {
        size_t start = req.body.find("\"", prompt_pos + 9) + 1;
        size_t end = req.body.find("\"", start);
        prompt = req.body.substr(start, end - start);
    }
    
    size_t session_pos = req.body.find("\"session_id\"");
    if (session_pos != std::string::npos) {
        size_t start = req.body.find("\"", session_pos + 13) + 1;
        size_t end = req.body.find("\"", start);
        session_id = req.body.substr(start, end - start);
    }
    
    size_t tokens_pos = req.body.find("\"max_tokens\"");
    if (tokens_pos != std::string::npos) {
        size_t start = req.body.find(":", tokens_pos) + 1;
        size_t end = req.body.find(",", start);
        if (end == std::string::npos) {
            end = req.body.find("}", start);
        }
        std::string token_str = req.body.substr(start, end - start);
        token_str.erase(0, token_str.find_first_not_of(" \t"));
        try {
            max_tokens = std::stoi(token_str);
        } catch (...) {}
    }
    
    engine->set_session_id(session_id);
    
    // --- DEBUG CLINICAL TRIGGER ---
    if (prompt.find("DEBUG: Trigger Respiratory Failure on Patient") != std::string::npos) {
        size_t pos = prompt.find("Patient") + 8;
        try {
            int pid = std::stoi(prompt.substr(pos));
            sim.trigger_crisis(pid, "Respiratory Failure");
            return Response(200, "{\"status\": \"Crisis triggered for patient " + std::to_string(pid) + "\"}");
        } catch (...) {}
    }
    
    InferenceRequest inference_req(prompt, max_tokens);
    InferenceResponse inference_resp = engine->infer(inference_req);
    
    std::string body = "{\n";
    body += "  \"prompt\": \"" + json_escape(inference_resp.prompt) + "\",\n";
    body += "  \"response\": \"" + json_escape(inference_resp.response) + "\",\n";
    body += "  \"type\": \"" + inference_resp.type + "\",\n";
    body += "  \"timestamp\": \"" + inference_resp.timestamp + "\",\n";
    body += "  \"confidence\": " + std::to_string(inference_resp.confidence) + ",\n";
    body += "  \"related_concepts\": [";
    
    for (size_t i = 0; i < inference_resp.related_concepts.size(); ++i) {
        body += "\"" + json_escape(inference_resp.related_concepts[i]) + "\"";
        if (i < inference_resp.related_concepts.size() - 1) {
            body += ", ";
        }
    }
    
    body += "]\n}\n";
    
    return Response(200, body);
}


// ... (Constructor update needed separately or assumed) 

HTTPServer::Response HTTPServer::handle_status(const Request& req) {
    auto metrics = engine->get_metrics();
    
    // --- ACmK Clinical Simulation Update ---
    // Advance simulation state
    sim.update(1000); // Assume ~1s delta or derive from clock
    
    std::string body = "{\n";
    body += "  \"status\": \"ready\",\n";
    body += "  \"vocab_size\": " + std::to_string(metrics.vocab_size) + ",\n";
    body += "  \"uptime_ms\": " + std::to_string(metrics.uptime_ms) + ",\n";
    
    // Check for specific patient request
    if (req.query_params.find("patient_id") != req.query_params.end()) {
        int pid = std::stoi(req.query_params.at("patient_id"));
        const Patient* p = sim.get_patient(pid);
        
        if (p) {
            body += "  \"patient\": {\n";
            body += "    \"id\": " + std::to_string(p->id) + ",\n";
            body += "    \"name\": \"" + p->name + "\",\n";
            body += "    \"mrn\": \"" + p->mrn + "\",\n";
            body += "    \"room\": \"" + p->room + "\",\n";
            body += "    \"admission_diagnosis\": \"" + p->admission_diagnosis + "\",\n";
            body += "    \"acuity_score\": " + std::to_string(p->acuity_score) + ",\n";
            body += "    \"hr\": " + std::to_string(p->vitals.hr) + ",\n";
            body += "    \"rr\": " + std::to_string(p->vitals.rr) + ",\n";
            body += "    \"spo2\": " + std::to_string(p->vitals.spo2) + ",\n";
            body += "    \"bp_dia\": " + std::to_string(p->vitals.bp_dia) + ",\n";
            body += "    \"temp\": " + std::to_string(p->vitals.temp) + ",\n";
            float stability = 1.0f - ((p->acuity_score - 1) / 9.0f);
            if (p->vitals.is_crisis) stability *= 0.5f;
            body += "    \"stability\": " + std::to_string(stability) + ",\n";
            body += "    \"nurse_notes\": \"" + json_escape(p->nurse_notes) + "\",\n";
            body += "    \"is_crisis\": " + std::string(p->vitals.is_crisis ? "true" : "false") + "\n";
            body += "  }\n";
        } else {
            body += "  \"error\": \"Patient not found\"\n";
        }
    } else {
        // Dashboard View: Return list of all patients
        body += "  \"patients\": [\n";
        const auto& all_patients = sim.get_all_patients();
        for (size_t i = 0; i < all_patients.size(); ++i) {
            const auto& p = all_patients[i];
            body += "    {\n";
            body += "      \"id\": " + std::to_string(p.id) + ",\n";
            body += "      \"name\": \"" + p.name + "\",\n";
            body += "      \"room\": \"" + p.room + "\",\n";
            body += "      \"acuity\": " + std::to_string(p.acuity_score) + ",\n";
            body += "      \"hr\": " + std::to_string(p.vitals.hr) + ",\n";
            body += "      \"spo2\": " + std::to_string(p.vitals.spo2) + ",\n";
            body += "      \"temp\": " + std::to_string(p.vitals.temp) + ",\n";
            float stability = 1.0f - ((p.acuity_score - 1) / 9.0f);
            if (p.vitals.is_crisis) stability *= 0.5f;
            body += "      \"stability\": " + std::to_string(stability) + ",\n";
            body += "      \"is_crisis\": \"" + std::string(p.vitals.is_crisis ? "true" : "false") + "\"\n";
            body += "    }";
            if (i < all_patients.size() - 1) {
                body += ",";
            }
            body += "\n";
        }
        body += "  ]\n";
    }
    
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_learn(const Request& req) {
    std::string prompt, response;
    bool was_good = true;
    
    size_t prompt_pos = req.body.find("\"prompt\"");
    if (prompt_pos != std::string::npos) {
        size_t start = req.body.find("\"", prompt_pos + 9) + 1;
        size_t end = req.body.find("\"", start);
        prompt = req.body.substr(start, end - start);
    }
    
    size_t resp_pos = req.body.find("\"response\"");
    if (resp_pos != std::string::npos) {
        size_t start = req.body.find("\"", resp_pos + 11) + 1;
        size_t end = req.body.find("\"", start);
        response = req.body.substr(start, end - start);
    }
    
    size_t good_pos = req.body.find("\"was_good\"");
    if (good_pos != std::string::npos) {
        size_t start = req.body.find(":", good_pos);
        std::string value = req.body.substr(start, 10);
        was_good = value.find("true") != std::string::npos;
    }
    
    engine->learn_from_feedback(prompt, response, was_good);
    
    std::string body = "{\n";
    body += "  \"status\": \"learned\",\n";
    body += "  \"updated\": true\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_health(const Request& req) {
    std::string body = "{\n";
    body += "  \"status\": \"healthy\",\n";
    body += "  \"timestamp\": \"" + current_timestamp() + "\"\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_observations(const Request& req) {
    // Require patient_id query parameter
    if (req.query_params.find("patient_id") == req.query_params.end()) {
        std::string body = "{\"error\": \"Missing required parameter: patient_id\"}\n";
        return Response(400, body);
    }
    
    int patient_id = std::stoi(req.query_params.at("patient_id"));
    const Patient* patient = sim.get_patient(patient_id);
    
    if (!patient) {
        std::string body = "{\"error\": \"Patient not found\"}\n";
        return Response(404, body);
    }
    
    // Analyze patient and get observations
    auto observations = analyzer->analyze_patient(*patient, engine->get_rag_dag(), engine.get());
    
    // Train the engine on these observations (ACmK Learning Loop)
    for (const auto& obs : observations) {
        engine->learn_from_clinical_observation(obs);
    }
    
    // Build JSON response
    std::string body = "{\n";
    body += "  \"patient_id\": " + std::to_string(patient_id) + ",\n";
    body += "  \"patient_name\": \"" + patient->name + "\",\n";
    body += "  \"timestamp\": \"" + current_timestamp() + "\",\n";
    body += "  \"observations\": [\n";
    
    for (size_t i = 0; i < observations.size(); i++) {
        const auto& obs = observations[i];
        body += "    {\n";
        body += "      \"type\": \"" + obs.observation_type + "\",\n";
        body += "      \"severity\": \"" + obs.severity + "\",\n";
        body += "      \"description\": \"" + json_escape(obs.description) + "\",\n";
        body += "      \"rationale\": \"" + json_escape(obs.rationale) + "\",\n";
        body += "      \"confidence\": " + std::to_string(obs.confidence) + ",\n";
        body += "      \"requires_attention\": " + std::string(obs.requires_nurse_attention ? "true" : "false") + ",\n";
        body += "      \"suggested_actions\": [\n";
        
        for (size_t j = 0; j < obs.suggested_actions.size(); j++) {
            body += "        \"" + json_escape(obs.suggested_actions[j]) + "\"";
            if (j < obs.suggested_actions.size() - 1) {
                body += ",";
            }
            body += "\n";
        }
        
        body += "      ]\n";
        body += "    }";
        if (i < observations.size() - 1) {
            body += ",";
        }
        body += "\n";
    }
    
    body += "  ]\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_scaffold(const Request& req) {
    if (req.query_params.find("patient_id") == req.query_params.end()) {
        return Response(400, "{\"error\": \"Missing required parameter: patient_id\"}\n");
    }
    
    int patient_id = std::stoi(req.query_params.at("patient_id"));
    const Patient* patient = sim.get_patient(patient_id);
    
    if (!patient) {
        return Response(404, "{\"error\": \"Patient not found\"}\n");
    }
    
    // 1. Get current observations
    auto observations = analyzer->analyze_patient(*patient, engine->get_rag_dag(), engine.get());
    
    // 2. Generate SBAR note
    DocumentationScaffold scaffold;
    std::string sbar = scaffold.generate_sbar(*patient, observations);
    
    // 3. Return JSON
    std::string body = "{\n";
    body += "  \"patient_id\": " + std::to_string(patient_id) + ",\n";
    body += "  \"patient_name\": \"" + patient->name + "\",\n";
    body += "  \"sbar_note\": \"" + json_escape(sbar) + "\"\n";
    body += "}\n";
    
    return Response(200, body);
}


HTTPServer::Response HTTPServer::handle_action(const Request& req) {
    if (req.method != "POST") {
        return Response(405, "{\"error\": \"Method not allowed\"}");
    }
    
    // Parse patient_id and action from body
    int pid = -1;
    std::string action;
    
    // Robust search for patient_id
    size_t pid_key = req.body.find("\"patient_id\"");
    if (pid_key != std::string::npos) {
        size_t colon = req.body.find(":", pid_key);
        if (colon != std::string::npos) {
            size_t val_start = req.body.find_first_of("0123456789", colon);
            if (val_start != std::string::npos) {
                pid = std::stoi(req.body.substr(val_start));
            }
        }
    }
    
    // Robust search for action
    size_t act_key = req.body.find("\"action\"");
    if (act_key != std::string::npos) {
        size_t colon = req.body.find(":", act_key);
        if (colon != std::string::npos) {
            size_t val_start = req.body.find("\"", colon);
            if (val_start != std::string::npos) {
                size_t start = val_start + 1;
                size_t end = req.body.find("\"", start);
                if (end != std::string::npos) {
                    action = req.body.substr(start, end - start);
                }
            }
        }
    }
    
    if (pid == -1 || action.empty()) {
        return Response(400, "{\"error\": \"Missing patient_id or action\"}");
    }
    
    // Create a synthetic "observation" representing the nurse's intervention
    ClinicalObservation obs;
    obs.patient_id = pid;
    obs.observation_type = "nurse_intervention";
    obs.severity = "info";
    obs.description = "Nurse performed: " + action;
    obs.rationale = "Clinician intervened based on system recommendations or assessment.";
    obs.confidence = 1.0;
    obs.timestamp = std::time(nullptr);
    obs.requires_nurse_attention = false;
    
    // Feed to kernel learning loop
    engine->learn_from_clinical_observation(obs);
    
    return Response(200, "{\"status\": \"action_logged\", \"message\": \"Nurse intervention recorded by ACmK.\"}");
}

HTTPServer::Response HTTPServer::handle_training_start(const Request& req) {
    if (training_mode_active_) {
        return Response(400, "{\"error\": \"Training session already active. Cannot start new session.\"}");
    }
    
    std::string scenario_id = "HYPOTENSION_001";
    std::string nurse_id = "TRAIN_NURSE_001";
    std::string nurse_role = "RN";
    
    size_t scenario_pos = req.body.find("\"scenario_id\"");
    if (scenario_pos != std::string::npos) {
        size_t start = req.body.find("\"", scenario_pos + 14) + 1;
        size_t end = req.body.find("\"", start);
        scenario_id = req.body.substr(start, end - start);
    }
    
    size_t nurse_pos = req.body.find("\"nurse_id\"");
    if (nurse_pos != std::string::npos) {
        size_t start = req.body.find("\"", nurse_pos + 11) + 1;
        size_t end = req.body.find("\"", start);
        nurse_id = req.body.substr(start, end - start);
    }
    
    size_t role_pos = req.body.find("\"nurse_role\"");
    if (role_pos != std::string::npos) {
        size_t start = req.body.find("\"", role_pos + 13) + 1;
        size_t end = req.body.find("\"", start);
        nurse_role = req.body.substr(start, end - start);
    }
    
    ScenarioDefinition scenario_def;
    if (scenario_id == "HYPOTENSION_001") {
        scenario_def = ScenarioLibrary::create_hypotension_scenario();
    } else if (scenario_id == "RESPIRATORY_001") {
        scenario_def = ScenarioLibrary::create_respiratory_distress_scenario();
    } else if (scenario_id == "SEPSIS_EARLY_001") {
        scenario_def = ScenarioLibrary::create_early_sepsis_scenario();
    } else if (scenario_id == "CARDIAC_ARREST_001") {
        scenario_def = ScenarioLibrary::create_cardiac_arrest_scenario();
    } else if (scenario_id == "STROKE_ALERT_001") {
        scenario_def = ScenarioLibrary::create_stroke_alert_scenario();
    } else if (scenario_id == "DKA_CRISIS_001") {
        scenario_def = ScenarioLibrary::create_dka_crisis_scenario();
    } else if (scenario_id == "ANAPHYLAXIS_001") {
        scenario_def = ScenarioLibrary::create_anaphylaxis_scenario();
    } else if (scenario_id == "SEVERE_BLEEDING_001") {
        scenario_def = ScenarioLibrary::create_severe_bleeding_scenario();
    } else {
        return Response(400, "{\"error\": \"Unknown scenario_id\"}");
    }
    
    active_scenario_ = std::make_unique<ScenarioRuntime>(scenario_def);
    training_mode_active_ = true;
    
    auto session = active_scenario_->get_session_record();
    session.nurse_id = nurse_id;
    session.nurse_role = nurse_role;
    training_sessions_[session.session_id] = session;
    
    std::string body = "{\n";
    body += "  \"status\": \"training_session_started\",\n";
    body += "  \"session_id\": \"" + session.session_id + "\",\n";
    body += "  \"scenario_id\": \"" + scenario_id + "\",\n";
    body += "  \"mode\": \"TRAINING\",\n";
    body += "  \"immutable\": true,\n";
    body += "  \"nurse_id\": \"" + nurse_id + "\",\n";
    body += "  \"nurse_role\": \"" + nurse_role + "\"\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_status(const Request& req) {
    if (!training_mode_active_ || !active_scenario_) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    
    ScenarioVitals vitals = active_scenario_->get_current_vitals();
    auto recommendations = active_scenario_->get_pending_recommendations();
    
    std::string body = "{\n";
    body += "  \"mode\": \"TRAINING\",\n";
    body += "  \"active\": " + std::string(active_scenario_->is_active() ? "true" : "false") + ",\n";
    body += "  \"elapsed_seconds\": " + std::to_string(active_scenario_->elapsed_seconds()) + ",\n";
    body += "  \"current_state\": \"" + active_scenario_->get_state() + "\",\n";
    body += "  \"vitals\": {\n";
    body += "    \"hr\": " + std::to_string(vitals.hr) + ",\n";
    body += "    \"rr\": " + std::to_string(vitals.rr) + ",\n";
    body += "    \"spo2\": " + std::to_string(vitals.spo2) + ",\n";
    body += "    \"bp_sys\": " + std::to_string(vitals.bp_sys) + ",\n";
    body += "    \"bp_dia\": " + std::to_string(vitals.bp_dia) + ",\n";
    body += "    \"temp\": " + std::to_string(vitals.temp) + "\n";
    body += "  },\n";
    body += "  \"pending_recommendations\": [\n";
    
    for (size_t i = 0; i < recommendations.size(); i++) {
        body += "    {\n";
        body += "      \"id\": \"" + recommendations[i].id + "\",\n";
        body += "      \"text\": \"" + recommendations[i].text + "\",\n";
        body += "      \"priority\": \"" + recommendations[i].priority + "\"\n";
        body += "    }";
        if (i < recommendations.size() - 1) body += ",";
        body += "\n";
    }
    
    body += "  ]\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_action(const Request& req) {
    if (!training_mode_active_ || !active_scenario_) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    
    std::string action;
    std::string nurse_id;
    
    size_t action_pos = req.body.find("\"action\"");
    if (action_pos != std::string::npos) {
        size_t start = req.body.find("\"", action_pos + 9) + 1;
        size_t end = req.body.find("\"", start);
        action = req.body.substr(start, end - start);
    }
    
    size_t nurse_pos = req.body.find("\"nurse_id\"");
    if (nurse_pos != std::string::npos) {
        size_t start = req.body.find("\"", nurse_pos + 11) + 1;
        size_t end = req.body.find("\"", start);
        nurse_id = req.body.substr(start, end - start);
    }
    
    if (action.empty()) {
        return Response(400, "{\"error\": \"Missing action\"}");
    }
    
    active_scenario_->accept_action(action, nurse_id);
    
    std::string feedback;
    bool is_correct = active_scenario_->evaluate_action_correctness(action, feedback);
    
    // Record action with correctness flag for AI learning
    if (analytics_store_) {
        analytics_store_->record_nurse_action(current_training_session_id_, action, nurse_id, 
                                             active_scenario_->elapsed_seconds(), is_correct);
    }
    
    std::string body = "{\n";
    body += "  \"status\": \"action_accepted\",\n";
    body += "  \"action\": \"" + action + "\",\n";
    body += "  \"correct\": " + std::string(is_correct ? "true" : "false") + ",\n";
    body += "  \"feedback\": \"" + feedback + "\",\n";
    body += "  \"mode\": \"TRAINING\",\n";
    body += "  \"recorded\": true\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_tick(const Request& req) {
    if (!training_mode_active_ || !active_scenario_) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    
    int delta_seconds = 30;
    
    auto delta_pos = req.query_params.find("delta_seconds");
    if (delta_pos != req.query_params.end()) {
        delta_seconds = std::stoi(delta_pos->second);
    }
    
    active_scenario_->tick(delta_seconds);
    
    ScenarioVitals vitals = active_scenario_->get_current_vitals();
    auto failures = active_scenario_->check_failure_conditions();
    
    std::string body = "{\n";
    body += "  \"status\": \"tick_processed\",\n";
    body += "  \"delta_seconds\": " + std::to_string(delta_seconds) + ",\n";
    body += "  \"elapsed_seconds\": " + std::to_string(active_scenario_->elapsed_seconds()) + ",\n";
    body += "  \"vitals\": {\n";
    body += "    \"hr\": " + std::to_string(vitals.hr) + ",\n";
    body += "    \"spo2\": " + std::to_string(vitals.spo2) + ",\n";
    body += "    \"bp_sys\": " + std::to_string(vitals.bp_sys) + "\n";
    body += "  },\n";
    body += "  \"failure_conditions_triggered\": [\n";
    
    for (size_t i = 0; i < failures.size(); i++) {
        body += "    \"" + failures[i] + "\"";
        if (i < failures.size() - 1) body += ",";
        body += "\n";
    }
    
    body += "  ],\n";
    
    std::string escalation_reason = active_scenario_->get_escalation_reason();
    body += "  \"escalation_reason\": \"" + (escalation_reason.empty() ? "None" : escalation_reason) + "\",\n";
    body += "  \"mode\": \"TRAINING\"\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_end(const Request& req) {
    if (!training_mode_active_ || !active_scenario_) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    
    std::string outcome = "COMPLETED";
    
    size_t outcome_pos = req.body.find("\"outcome\"");
    if (outcome_pos != std::string::npos) {
        size_t start = req.body.find("\"", outcome_pos + 10) + 1;
        size_t end = req.body.find("\"", start);
        outcome = req.body.substr(start, end - start);
    }
    
    auto session = active_scenario_->get_session_record();
    active_scenario_->finalize_session(outcome);
    
    std::string body = "{\n";
    body += "  \"status\": \"training_session_ended\",\n";
    body += "  \"session_id\": \"" + session.session_id + "\",\n";
    body += "  \"outcome\": \"" + outcome + "\",\n";
    body += "  \"duration_seconds\": " + std::to_string(active_scenario_->elapsed_seconds()) + ",\n";
    body += "  \"mode\": \"TRAINING\"\n";
    body += "}\n";
    
    training_mode_active_ = false;
    active_scenario_.reset();
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_list(const Request& req) {
    auto scenarios = ScenarioLibrary::list_available_scenarios();
    
    std::string body = "{\n";
    body += "  \"available_scenarios\": [\n";
    
    for (size_t i = 0; i < scenarios.size(); i++) {
        body += "    \"" + scenarios[i] + "\"";
        if (i < scenarios.size() - 1) body += ",";
        body += "\n";
    }
    
    body += "  ]\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_analytics(const Request& req) {
    if (!analytics_store_) {
        return Response(500, "{\"error\": \"Analytics store not initialized\"}");
    }
    
    std::string session_id = "";
    if (req.query_params.find("session_id") != req.query_params.end()) {
        session_id = req.query_params.at("session_id");
    }
    
    if (session_id.empty()) {
        auto sessions = analytics_store_->list_all_sessions();
        std::string body = "{\n";
        body += "  \"available_sessions\": [\n";
        
        for (size_t i = 0; i < sessions.size(); i++) {
            body += "    \"" + sessions[i] + "\"";
            if (i < sessions.size() - 1) body += ",";
            body += "\n";
        }
        
        body += "  ]\n";
        body += "}\n";
        
        return Response(200, body);
    }
    
    auto events = analytics_store_->get_session_events(session_id);
    
    std::string body = "{\n";
    body += "  \"session_id\": \"" + session_id + "\",\n";
    body += "  \"event_count\": " + std::to_string(events.size()) + ",\n";
    body += "  \"mode\": \"TRAINING\",\n";
    body += "  \"immutable\": true\n";
    body += "}\n";
    
    return Response(200, body);
}

HTTPServer::Response HTTPServer::handle_training_report(const Request& req) {
    if (!analytics_store_) {
        return Response(500, "{\"error\": \"Analytics store not initialized\"}");
    }
    
    std::string session_id = "";
    if (req.query_params.find("session_id") != req.query_params.end()) {
        session_id = req.query_params.at("session_id");
    }
    
    if (session_id.empty()) {
        return Response(400, "{\"error\": \"Missing required parameter: session_id\"}");
    }
    
    if (!analytics_store_->session_exists(session_id)) {
        return Response(404, "{\"error\": \"Session not found\"}");
    }
    
    TrainingReplayEngine replay(*analytics_store_);
    std::string report = replay.generate_session_report(session_id);
    
    return Response(200, report);
}

HTTPServer::Response HTTPServer::handle_not_found(const Request& req) {
    std::string body = "{\n";
    body += "  \"error\": \"Not found\",\n";
    body += "  \"path\": \"" + req.path + "\"\n";
    body += "}\n";
    
    return Response(404, body);
}

HTTPServer::Response HTTPServer::handle_static(const Request& req) {
    std::string path = req.path;
    if (path == "/") {
        path = "/index.html";
    }
    
    // Remove query string if present
    size_t query_pos = path.find("?");
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }
    
    // Security check: prevent directory traversal
    if (path.find("..") != std::string::npos) {
        return handle_not_found(req);
    }
    
    std::string full_path = "web" + path;
    
    // Check if file exists
    if (!fs::exists(full_path) || fs::is_directory(full_path)) {
        return handle_not_found(req);
    }
    
    std::ifstream file(full_path, std::ios::binary);
    if (!file) {
        return handle_not_found(req);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    std::string mime_type = get_mime_type(full_path);
    
    return Response(200, content, mime_type);
}

std::string HTTPServer::get_mime_type(const std::string& path) {
    if (path.length() >= 5 && path.substr(path.length() - 5) == ".html") return "text/html";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".css") return "text/css";
    if (path.length() >= 3 && path.substr(path.length() - 3) == ".js") return "application/javascript";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".png") return "image/png";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".jpg") return "image/jpeg";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".gif") return "image/gif";
    if (path.length() >= 4 && path.substr(path.length() - 4) == ".ico") return "image/x-icon";
    return "text/plain";
}
