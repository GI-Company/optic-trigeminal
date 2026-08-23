#include "http_server.h"
#include "data_loader.h"
#include "auth_manager.h"
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
#include <cstdlib>
#include "fhir_client.h"
#include "crypto_utils.h"
#include "embedded_web_assets.h"
#include <chrono>
#include <cstdint>
#include <iomanip>

namespace fs = std::filesystem;

namespace {
// event_data is a flat "key=value key2=value2" string (see
// TrainingAnalyticsStore::record_nurse_action et al. in
// src/clinical/training_analytics.cpp); training_analytics.cpp has its own
// copy of this same small parser rather than sharing a header for an
// 8-line string helper.
std::string parse_event_data_field(const std::string& event_data, const std::string& key) {
  std::string needle = key + "=";
  size_t pos = event_data.find(needle);
  if (pos == std::string::npos) return "";
  size_t start = pos + needle.size();
  size_t end = event_data.find(' ', start);
  return event_data.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Maps the system auth Role enum to the string vocabulary the ACMK-OT layer
// (and ACMK::RoleBasedAccessControl::string_to_role) understands. Kept here
// rather than in acmk_api_handler.cpp so that layer stays decoupled from the
// system Role enum and only deals with server-verified strings.
std::string role_to_acmk_role_string(Role role) {
  switch (role) {
    case Role::RN: return "rn";
    case Role::CHARGE_NURSE: return "charge_nurse";
    case Role::PROVIDER: return "provider";
    case Role::ADMIN: return "admin";
    case Role::IT: return "it";
    case Role::INSTRUCTOR: return "instructor";
    default: return "rn";
  }
}

// Reverse of role_to_acmk_role_string, for the one endpoint that takes a
// role choice from the client (handle_staff_create, ADMIN-only). Returns
// false for anything unrecognized rather than silently defaulting -- an
// admin fat-fingering a role string should get a 400, not a surprise RN
// account.
bool acmk_role_string_to_role(const std::string& s, Role& out) {
  if (s == "rn") { out = Role::RN; return true; }
  if (s == "charge_nurse") { out = Role::CHARGE_NURSE; return true; }
  if (s == "provider") { out = Role::PROVIDER; return true; }
  if (s == "admin") { out = Role::ADMIN; return true; }
  if (s == "it") { out = Role::IT; return true; }
  if (s == "instructor") { out = Role::INSTRUCTOR; return true; }
  return false;
}

std::string action_grade_to_string(ActionGrade grade) {
  switch (grade) {
    case ActionGrade::CORRECT: return "correct";
    case ActionGrade::PARTIALLY_CORRECT: return "partially_correct";
    case ActionGrade::PREMATURE: return "premature";
    case ActionGrade::INCORRECT: return "incorrect";
    case ActionGrade::CONTRAINDICATED: return "contraindicated";
  }
  return "partially_correct";
}

// The frontend (web/src/api/types.ts: TrainingScenario, consumed by
// ScenarioSelector.ts) needs id/title/category/difficulty/duration_min --
// none of which a bare scenario_id string can supply. list_available_scenarios()
// only returns IDs, and handle_training_list used to hand those back
// under the wrong field name too (available_scenarios vs. the client's
// response.scenarios), so the scenario picker was silently always empty.
ScenarioDefinition get_scenario_definition_by_id(const std::string& scenario_id) {
    if (scenario_id == "HYPOTENSION_001") return ScenarioLibrary::create_hypotension_scenario();
    if (scenario_id == "RESPIRATORY_001") return ScenarioLibrary::create_respiratory_distress_scenario();
    if (scenario_id == "SEPSIS_EARLY_001") return ScenarioLibrary::create_early_sepsis_scenario();
    if (scenario_id == "CARDIAC_ARREST_001") return ScenarioLibrary::create_cardiac_arrest_scenario();
    if (scenario_id == "STROKE_ALERT_001") return ScenarioLibrary::create_stroke_alert_scenario();
    if (scenario_id == "DKA_CRISIS_001") return ScenarioLibrary::create_dka_crisis_scenario();
    if (scenario_id == "ANAPHYLAXIS_001") return ScenarioLibrary::create_anaphylaxis_scenario();
    if (scenario_id == "SEVERE_BLEEDING_001") return ScenarioLibrary::create_severe_bleeding_scenario();
    return ScenarioLibrary::create_hypotension_scenario();
}

// scenario_id prefix already encodes the clinical category better than
// context_unit does (a ward name, e.g. "6 West").
std::string scenario_category_from_id(const std::string& id) {
    size_t underscore = id.rfind('_');
    std::string prefix = underscore == std::string::npos ? id : id.substr(0, underscore);
    std::string category;
    for (size_t i = 0; i < prefix.size(); ++i) {
        char c = prefix[i];
        if (c == '_') { category += ' '; continue; }
        category += (i == 0 || prefix[i - 1] == '_') ? c : static_cast<char>(std::tolower(c));
    }
    return category;
}

std::string difficulty_from_tier(const std::string& tier) {
    if (tier == "FOUNDATIONAL") return "beginner";
    if (tier == "INTERMEDIATE") return "intermediate";
    return "advanced"; // CRISIS and anything else
}

int duration_minutes_from_timeline(const ScenarioDefinition& def) {
    int max_t = 10;
    for (const auto& event : def.timeline) {
        if (event.t_min > max_t) max_t = event.t_min;
    }
    return max_t + 5; // buffer past the last scripted event
}

// Display labels for the action ids ScenarioRuntime::evaluate_action_correctness
// (src/clinical/training_scenario.cpp) actually scores. These ids used to only
// exist there -- ScenarioDefinition::expected_actions (which drives real vitals
// changes in apply_action_effects) used different human-readable strings like
// "IV fluids", and the four training-mode buttons sent a third, unrelated
// vocabulary (assess_patient/administer_oxygen/position_patient/notify_provider)
// that matched none of it. Every click fell through to evaluate_action_correctness's
// catch-all, which unconditionally returns correct -- so the score went up
// regardless of what you pressed or when. expected_actions now uses these same
// ids (see training_scenario.cpp), so this label map is the single source of
// truth connecting "what the button says" to "what gets scored" to "what
// actually moves the patient's vitals".
std::string action_label(const std::string& action_id) {
    static const std::map<std::string, std::string> labels = {
        {"apply_iv_fluids", "Apply IV Fluids"},
        {"start_vasopressor", "Start Vasopressor"},
        {"apply_oxygen", "Apply Oxygen"},
        {"call_respiratory", "Call Respiratory Therapy"},
        {"initiate_sepsis_bundle", "Initiate Sepsis Bundle"},
        {"get_blood_cultures", "Obtain Blood Cultures"},
        {"initiate_cpr", "Initiate CPR"},
        {"defibrillate", "Defibrillate"},
        {"epinephrine", "Administer Epinephrine"},
        {"activate_stroke_alert", "Activate Stroke Alert"},
        {"ct_head", "Order CT Head"},
        {"tpa_administration", "Administer tPA"},
        {"iv_fluids", "Start IV Fluids"},
        {"insulin_infusion", "Start Insulin Infusion"},
        {"obtain_labs", "Obtain Labs"},
        {"epinephrine_im", "Give IM Epinephrine"},
        {"airway_management", "Manage Airway"},
        {"fluid_bolus", "Administer Fluid Bolus"},
        {"hemorrhage_control", "Apply Hemorrhage Control"},
        {"massive_transfusion", "Initiate Massive Transfusion"},
        {"emergency_surgery", "Call for Emergency Surgery"},
        {"notify_provider", "Notify Provider"}
    };
    auto it = labels.find(action_id);
    return it != labels.end() ? it->second : action_id;
}
} // namespace

HTTPServer::HTTPServer(int port)
    : port(port), running(false) {
    engine = std::make_unique<NativeInferenceEngine>();
    sim.initialize(6); // ACmK Clinical Simulation Init
    analyzer = std::make_unique<ClinicalAnalyzer>(); // Clinical Intelligence Init

    const char* base_url = std::getenv("FHIR_BASE_URL");
    const char* token_endpoint = std::getenv("FHIR_TOKEN_ENDPOINT");
    const char* client_id = std::getenv("FHIR_CLIENT_ID");
    const char* client_secret = std::getenv("FHIR_CLIENT_SECRET");
    fhir_base_url_ = base_url ? base_url : "";
    fhir_provider_ = std::make_unique<FHIR::DefaultSMARTOnFHIRProvider>(token_endpoint ? token_endpoint : "");
    fhir_launch_context_.client_id = client_id ? client_id : "";
    fhir_launch_context_.client_secret = client_secret ? client_secret : "";
    fhir_launch_context_.ehr_server_url = fhir_base_url_;

    // Reload chart entries written by earlier runs -- without this, chart
    // notes would survive a browser refresh (now stored server-side) but
    // still vanish on every server restart, which isn't really "persisted"
    // for anything meant to be a clinical record.
    std::ifstream chart_in("chart_log.ndjson");
    std::string chart_line;
    while (std::getline(chart_in, chart_line)) {
        if (chart_line.empty()) continue;
        try {
            json parsed = json::parse(chart_line);
            StoredChartEntry entry;
            entry.entry_id = parsed.at("entry_id").as_string();
            entry.patient_id = static_cast<int>(parsed.at("patient_id").as_double());
            entry.entry_type = parsed.at("type").as_string();
            entry.content = parsed.at("content").as_string();
            entry.nurse_id = parsed.at("nurse_id").as_string();
            entry.nurse_name = parsed.at("nurse_name").as_string();
            entry.timestamp = static_cast<std::time_t>(parsed.at("timestamp").as_long());
            chart_entries_[entry.patient_id].push_back(entry);
            next_chart_entry_id_++;
        } catch (...) {
            // Skip a malformed line rather than fail startup over it.
        }
    }
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
    
    std::cout << "[HTTPServer] Initializing ACMK-OT planes coordinator..." << std::endl;
    acmk_coordinator = ACMK::create_default_planes_coordinator();
    acmk_handler = std::make_shared<ACMKAPIHandler>(acmk_coordinator);
    std::cout << "[HTTPServer] ACMK-OT planes initialized successfully" << std::endl;
    
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
    routes["/api/inference/enhanced"] = [this](const Request& req) { return handle_enhanced_infer(req); };
    routes["/api/inference/native/status"] = [this](const Request& req) { return handle_status(req); };
    routes["/api/inference/native/learn"] = [this](const Request& req) { return handle_learn(req); };
    routes["/api/clinical/patients"] = [this](const Request& r) { return handle_patients(r); };
    routes["/api/clinical/patients/admit"] = [this](const Request& r) { return handle_patient_admit(r); };
    routes["/api/clinical/patients/discharge"] = [this](const Request& r) { return handle_patient_discharge(r); };
    routes["/api/clinical/patients/assign"] = [this](const Request& r) { return handle_patient_assign(r); };
    routes["/api/clinical/alerts/override"] = [this](const Request& r) { return handle_alert_override(r); };
    routes["/api/staff/list"] = [this](const Request& r) { return handle_staff_list(r); };
    routes["/api/staff/create"] = [this](const Request& r) { return handle_staff_create(r); };
    routes["/api/instructor/cohorts/create"] = [this](const Request& r) { return handle_instructor_create_cohort(r); };
    routes["/api/instructor/cohorts"] = [this](const Request& r) { return handle_instructor_list_cohorts(r); };
    routes["/api/instructor/cohorts/roster"] = [this](const Request& r) { return handle_instructor_import_roster(r); };
    routes["/api/instructor/cohorts/remove-student"] = [this](const Request& r) { return handle_instructor_remove_student(r); };
    routes["/api/instructor/cohort"] = [this](const Request& r) { return handle_instructor_cohort_dashboard(r); };
    routes["/api/clinical/observations"] = [this](const Request& r) { return handle_observations(r); };
    routes["/api/clinical/scaffold"] = [this](const Request& r) { return handle_scaffold(r); };
    routes["/api/clinical/action"] = [this](const Request& r) { return handle_action(r); };
    routes["/api/clinical/chart"] = [this](const Request& r) { return handle_chart(r); };
    routes["/api/auth/sign-in"] = [this](const Request& r) { return handle_sign_in(r); };
    routes["/api/training/start"] = [this](const Request& r) { return handle_training_start(r); };
    routes["/api/training/status"] = [this](const Request& r) { return handle_training_status(r); };
    routes["/api/training/action"] = [this](const Request& r) { return handle_training_action(r); };
    routes["/api/training/tick"] = [this](const Request& r) { return handle_training_tick(r); };
    routes["/api/training/end"] = [this](const Request& r) { return handle_training_end(r); };
    routes["/api/training/list"] = [this](const Request& r) { return handle_training_list(r); };
    routes["/api/training/analytics"] = [this](const Request& r) { return handle_training_analytics(r); };
    routes["/api/training/report"] = [this](const Request& r) { return handle_training_report(r); };
    routes["/api/training/note/draft"] = [this](const Request& r) { return handle_training_note_draft(r); };
    routes["/api/training/note/sign"] = [this](const Request& r) { return handle_training_note_sign(r); };
    routes["/health"] = [this](const Request& req) { return handle_health(req); };
    
    auto convert_request = [](const Request& http_req, AuthToken* token) -> ACMKRequest {
      ACMKRequest acmk_req;
      acmk_req.method = http_req.method;
      acmk_req.path = http_req.path;
      acmk_req.body = http_req.body;
      acmk_req.headers = http_req.headers;
      acmk_req.query_params = http_req.query_params;
      acmk_req.authenticated_user_id = token->user_id;
      acmk_req.authenticated_role = role_to_acmk_role_string(token->role);
      return acmk_req;
    };

    auto convert_response = [](const ACMKResponse& acmk_resp) -> Response {
      Response http_resp(acmk_resp.status_code, acmk_resp.body, acmk_resp.content_type);
      http_resp.headers = acmk_resp.headers;
      return http_resp;
    };

    // Every ACMK-OT route requires a valid bearer token. The verified
    // identity/role is what gets passed through to the handler -- request
    // bodies are never trusted for identity (see acmk_api_handler.cpp).
    using ACMKMethod = ACMKAPIHandler::Response (ACMKAPIHandler::*)(const ACMKRequest&);
    auto wrap_acmk = [this, convert_request, convert_response](ACMKMethod method) -> RequestHandler {
      return [this, convert_request, convert_response, method](const Request& req) -> Response {
        AuthToken* token = g_auth_manager->get_token(req.auth_token);
        if (!token) {
          return Response(401, "{\"error\":\"Unauthorized - missing or invalid token\"}");
        }
        ACMKRequest acmk_req = convert_request(req, token);
        return convert_response((acmk_handler.get()->*method)(acmk_req));
      };
    };

    routes["/api/acmk/session/init"] = wrap_acmk(&ACMKAPIHandler::handle_session_init);

    routes["/api/acmk/state/emit"] = wrap_acmk(&ACMKAPIHandler::handle_state_emit);
    routes["/api/acmk/state/get"] = wrap_acmk(&ACMKAPIHandler::handle_state_get);
    routes["/api/acmk/state/history"] = wrap_acmk(&ACMKAPIHandler::handle_state_history);

    routes["/api/acmk/control/pause"] = wrap_acmk(&ACMKAPIHandler::handle_control_pause);
    routes["/api/acmk/control/resume"] = wrap_acmk(&ACMKAPIHandler::handle_control_resume);
    routes["/api/acmk/control/replay"] = wrap_acmk(&ACMKAPIHandler::handle_control_replay);
    routes["/api/acmk/control/freeze"] = wrap_acmk(&ACMKAPIHandler::handle_control_freeze);
    routes["/api/acmk/control/recompute"] = wrap_acmk(&ACMKAPIHandler::handle_control_recompute);

    routes["/api/acmk/trace/graph"] = wrap_acmk(&ACMKAPIHandler::handle_trace_get_graph);
    routes["/api/acmk/trace/artifacts"] = wrap_acmk(&ACMKAPIHandler::handle_trace_get_artifacts);
    routes["/api/acmk/trace/snapshot"] = wrap_acmk(&ACMKAPIHandler::handle_trace_snapshot);
    routes["/api/acmk/trace/snapshots"] = wrap_acmk(&ACMKAPIHandler::handle_trace_snapshots);

    routes["/api/acmk/inference/trace"] = wrap_acmk(&ACMKAPIHandler::handle_inference_readonly_trace);
    routes["/api/acmk/inference/decision"] = wrap_acmk(&ACMKAPIHandler::handle_inference_decision_envelope);
    routes["/api/acmk/inference/decision/get"] = wrap_acmk(&ACMKAPIHandler::handle_inference_get_decision);
    routes["/api/acmk/inference/error"] = wrap_acmk(&ACMKAPIHandler::handle_inference_error);

    routes["/api/acmk/environment/human-event"] = wrap_acmk(&ACMKAPIHandler::handle_environment_human_event);
    routes["/api/acmk/environment/provenance"] = wrap_acmk(&ACMKAPIHandler::handle_environment_provenance);
    routes["/api/acmk/environment/human-events"] = wrap_acmk(&ACMKAPIHandler::handle_environment_human_events);

    routes["/api/acmk/fhir/patient"] = [this](const Request& req) { return handle_fhir_read(req, "patient"); };
    routes["/api/acmk/fhir/observation"] = [this](const Request& req) { return handle_fhir_read(req, "observation"); };
    routes["/api/acmk/fhir/flag"] = [this](const Request& req) { return handle_fhir_create_flag(req); };
    routes["/api/acmk/fhir/document-reference"] = [this](const Request& req) { return handle_fhir_create_document_reference(req); };
    routes["/api/acmk/fhir/provenance"] = [this](const Request& req) { return handle_fhir_create_provenance(req); };

    routes["/api/acmk/audit/recent"] = [this](const Request& req) { return handle_audit_recent(req); };

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

        if (request.method == "OPTIONS") {
            // CORS preflight: no route dispatch, just the allow-headers.
            response = Response(204, "");
        } else {
            auto it = routes.find(request.path);
            if (it != routes.end()) {
                response = it->second(request);
            } else {
                // Try to serve static file
                response = handle_static(request);
                // If still 404, it stays as the default 404 from handle_static
            }
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
            // std::getline only strips the trailing '\n', so a trailing '\r'
            // from the HTTP CRLF line ending was previously left attached to
            // every header value -- e.g. "Bearer <token>\r" -- which meant
            // no Authorization header ever matched a stored token and every
            // auth-gated endpoint silently rejected valid tokens.
            size_t last_good = value.find_last_not_of(" \t\r");
            value.erase(last_good == std::string::npos ? 0 : last_good + 1);
            request.headers[key] = value;
            
            if (key == "Authorization") {
                size_t space = value.find(' ');
                if (space != std::string::npos && value.substr(0, space) == "Bearer") {
                    request.auth_token = value.substr(space + 1);
                }
            }
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
    // CORS: auth is bearer-token based (not cookies), so a permissive origin
    // doesn't expose credentialed requests. Needed for the dev frontend
    // (Vite on :5173) to call this server (:8080) at all.
    http_response += "Access-Control-Allow-Origin: *\r\n";
    http_response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    http_response += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    http_response += "Access-Control-Max-Age: 86400\r\n";

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
    // This whole handler (and the 14 others like it -- see git history for
    // the full list) had no auth check at all: anyone who could reach the
    // port could call it with no token. auth-gating the newer ACMK-OT
    // routes earlier this session didn't touch this original API surface,
    // which is what the app actually runs on.
    if (!g_auth_manager->get_token(req.auth_token)) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::string prompt;
    std::string session_id = "default";
    int max_tokens = 128;
    
    size_t prompt_pos = req.body.find("\"prompt\"");
    if (prompt_pos != std::string::npos) {
        size_t start = req.body.find("\"", prompt_pos + 8) + 1;
        size_t end = req.body.find("\"", start);
        prompt = req.body.substr(start, end - start);
    }
    
    size_t session_pos = req.body.find("\"session_id\"");
    if (session_pos != std::string::npos) {
        size_t start = req.body.find("\"", session_pos + 12) + 1;
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

HTTPServer::Response HTTPServer::handle_enhanced_infer(const Request& req) {
    // Also protects the Groq API key/quota from anonymous use -- this route
    // makes a real paid external API call per request.
    if (!g_auth_manager->get_token(req.auth_token)) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::string prompt;
    // Groq model. llama3-8b-8192 was the model here until it was
    // decommissioned by Groq (deprecation is routine on their platform --
    // verify against GET https://api.groq.com/openai/v1/models before
    // assuming any hardcoded id is still live). gpt-oss-120b is a real,
    // currently-served general-purpose instruct model there.
    std::string model = "openai/gpt-oss-120b";
    int max_tokens = 1024;
    
    size_t prompt_pos = req.body.find("\"prompt\"");
    if (prompt_pos != std::string::npos) {
        size_t start = req.body.find("\"", prompt_pos + 8) + 1;
        size_t end = req.body.find("\"", start);
        prompt = req.body.substr(start, end - start);
    }
    
    size_t model_pos = req.body.find("\"model\"");
    if (model_pos != std::string::npos) {
        size_t start = req.body.find("\"", model_pos + 7) + 1;
        size_t end = req.body.find("\"", start);
        model = req.body.substr(start, end - start);
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
    
    InferenceResponse inference_resp = engine->enhanced_infer(prompt, model, max_tokens);
    
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
    AuthToken* status_token = g_auth_manager->get_token(req.auth_token);
    if (!status_token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

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
        int pid = -1;
        // std::stoi throws on non-numeric input, which would otherwise
        // crash this thread uncaught -- and since every connection runs on
        // its own std::thread (see start_server()), an uncaught exception
        // here calls std::terminate() and takes down the whole server
        // process, not just this one request.
        try {
            pid = std::stoi(req.query_params.at("patient_id"));
        } catch (...) {
            return Response(400, "{\"error\": \"Invalid patient_id\"}");
        }
        // This branch hands back a real patient's identity + live vitals --
        // require the same clinical-vitals permission handle_action does,
        // not just "logged in as something".
        if (!g_auth_manager->can_view_patient_vitals(status_token, pid)) {
            return Response(403, "{\"error\": \"Forbidden\"}");
        }
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

HTTPServer::Response HTTPServer::handle_patients(const Request& req) {
    // The dashboard's patient roster (main-refactored.ts: loadPatients())
    // used to come entirely from web/src/store/mockData.ts -- fabricated
    // names/rooms/vitals that never matched what the rest of the app
    // (handle_status, handle_action, training) treats as the real patient
    // set from `sim`. This is the real roster, filtered to whichever
    // patients this user is actually allowed to see (can_view_patient_vitals),
    // in the exact shape web/src/api/types.ts's Patient interface expects.
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    sim.update(1000);

    std::ostringstream body;
    body << "{\"patients\":[";
    const auto& all_patients = sim.get_all_patients();
    bool first = true;
    for (const auto& p : all_patients) {
        if (!p.active) continue;
        if (!g_auth_manager->can_view_patient_vitals(token, p.id)) {
            continue;
        }
        if (!first) body << ",";
        first = false;

        body << "{\"id\":" << p.id
             << ",\"name\":\"" << json_escape(p.name) << "\""
             << ",\"mrn\":\"" << json_escape(p.mrn) << "\""
             << ",\"room\":\"" << json_escape(p.room) << "\""
             << ",\"admission_diagnosis\":\"" << json_escape(p.admission_diagnosis) << "\""
             << ",\"acuity_score\":" << p.acuity_score
             << ",\"vitals\":{"
                 << "\"hr\":" << p.vitals.hr
                 << ",\"rr\":" << p.vitals.rr
                 << ",\"spo2\":" << p.vitals.spo2
                 << ",\"bp_sys\":" << p.vitals.bp_sys
                 << ",\"bp_dia\":" << p.vitals.bp_dia
                 << ",\"temp\":" << p.vitals.temp
                 << ",\"is_crisis\":" << (p.vitals.is_crisis ? "true" : "false")
                 << ",\"drift_variance\":" << p.vitals.drift_variance
             << "}"
             << ",\"hr_history\":[";
        for (size_t i = 0; i < p.hr_history.size(); ++i) {
            if (i > 0) body << ",";
            body << p.hr_history[i];
        }
        body << "],\"spo2_history\":[";
        for (size_t i = 0; i < p.spo2_history.size(); ++i) {
            if (i > 0) body << ",";
            body << p.spo2_history[i];
        }
        body << "],\"temp_history\":[";
        for (size_t i = 0; i < p.temp_history.size(); ++i) {
            if (i > 0) body << ",";
            body << p.temp_history[i];
        }
        body << "]"
             << ",\"nurse_notes\":\"" << json_escape(p.nurse_notes) << "\""
             << "}";
    }
    body << "]}";

    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_patient_admit(const Request& req) {
    // Charge Nurse / Provider only (Permission::ADMIT_PATIENT) -- the
    // permission has existed since Phase 1.5 but nothing ever called it, so
    // "Admit Patient" was a dead capability flag with no backing endpoint.
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_perform_action(token, Permission::ADMIT_PATIENT)) {
        g_auth_manager->log_auth_decision(token->user_id, "ADMIT_PATIENT_DENY", false, "Missing permission");
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    try {
        json parsed = json::parse(req.body);
        std::string name = parsed.contains("name") ? parsed.at("name").as_string("") : "";
        std::string mrn = parsed.contains("mrn") ? parsed.at("mrn").as_string("") : "";
        std::string room = parsed.contains("room") ? parsed.at("room").as_string("") : "";
        std::string diagnosis = parsed.contains("diagnosis") ? parsed.at("diagnosis").as_string("") : "";
        int acuity = parsed.contains("acuity_score") ? static_cast<int>(parsed.at("acuity_score").as_double(3)) : 3;

        if (name.empty() || mrn.empty() || room.empty()) {
            return Response(400, "{\"error\": \"name, mrn, and room are required\"}");
        }

        int new_id = sim.admit_patient(name, mrn, room, diagnosis, acuity);
        g_auth_manager->log_auth_decision(token->user_id, "ADMIT_PATIENT", true, "Patient " + std::to_string(new_id) + " admitted");

        std::ostringstream body;
        body << "{\"status\":\"admitted\",\"patient_id\":" << new_id << "}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_patient_discharge(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_perform_action(token, Permission::DISCHARGE_PATIENT)) {
        g_auth_manager->log_auth_decision(token->user_id, "DISCHARGE_PATIENT_DENY", false, "Missing permission");
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    try {
        json parsed = json::parse(req.body);
        int pid = parsed.contains("patient_id") ? static_cast<int>(parsed.at("patient_id").as_double(-1)) : -1;
        std::string reason = parsed.contains("reason") ? parsed.at("reason").as_string("") : "";

        if (pid <= 0) {
            return Response(400, "{\"error\": \"patient_id is required\"}");
        }

        bool ok = sim.discharge_patient(pid, reason);
        if (!ok) {
            return Response(404, "{\"error\": \"Patient not found or already discharged\"}");
        }
        g_auth_manager->log_auth_decision(token->user_id, "DISCHARGE_PATIENT", true, "Patient " + std::to_string(pid) + " discharged");

        std::ostringstream body;
        body << "{\"status\":\"discharged\",\"patient_id\":" << pid << "}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_staff_list(const Request& req) {
    // Backs the Charge Nurse's "assign patient to nurse" picker -- needs to
    // see who's on shift. Scoped to ASSIGN_PATIENTS since it's the only
    // feature that needs a staff roster right now.
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_perform_action(token, Permission::ASSIGN_PATIENTS)) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    std::ostringstream body;
    body << "{\"staff\":[";
    auto all_staff = g_auth_manager->list_staff();
    for (size_t i = 0; i < all_staff.size(); ++i) {
        const auto& s = all_staff[i];
        if (i > 0) body << ",";
        body << "{\"staff_id\":\"" << json_escape(s.staff_id) << "\""
             << ",\"name\":\"" << json_escape(s.name) << "\""
             << ",\"role\":\"" << role_to_acmk_role_string(s.role) << "\""
             << ",\"assigned_patients\":[";
        for (size_t j = 0; j < s.assigned_patients.size(); ++j) {
            if (j > 0) body << ",";
            body << s.assigned_patients[j];
        }
        body << "]}";
    }
    body << "]}";
    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_staff_create(const Request& req) {
    // ADMIN-only: provisions a single named staff account of any role
    // (typically used to create the first INSTRUCTOR_001-style account for
    // a real school/hospital deployment, since instructors themselves have
    // no self-service signup -- someone with ACCESS_ADMIN has to create
    // that first account, same as any other staff onboarding). Bulk student
    // provisioning goes through handle_instructor_import_roster instead;
    // this is for the occasional single account.
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (token->role != Role::ADMIN) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    try {
        json parsed = json::parse(req.body);
        std::string staff_id = parsed.contains("staff_id") ? parsed.at("staff_id").as_string("") : "";
        std::string name = parsed.contains("name") ? parsed.at("name").as_string("") : "";
        std::string role_str = parsed.contains("role") ? parsed.at("role").as_string("") : "";

        if (staff_id.empty() || name.empty()) {
            return Response(400, "{\"error\": \"staff_id and name are required\"}");
        }
        Role role;
        if (!acmk_role_string_to_role(role_str, role)) {
            return Response(400, "{\"error\": \"role must be one of: rn, charge_nurse, provider, admin, it, instructor\"}");
        }
        if (g_auth_manager->get_staff_member(staff_id)) {
            return Response(409, "{\"error\": \"staff_id already exists\"}");
        }

        std::string password = Crypto::random_hex(9);
        StaffMember member;
        member.staff_id = staff_id;
        member.name = name;
        member.role = role;
        member.assigned_patients = {};
        member.active = true;
        member.last_sign_in = 0;
        member.password_hash = Crypto::hash_password(password);

        if (!g_auth_manager->add_staff_member(member)) {
            return Response(500, "{\"error\": \"Failed to create staff member\"}");
        }
        g_auth_manager->log_auth_decision(token->user_id, "STAFF_CREATE", true, "Created " + staff_id + " (" + role_str + ")");

        std::ostringstream body;
        body << "{\"status\":\"created\""
             << ",\"staff_id\":\"" << json_escape(staff_id) << "\""
             << ",\"role\":\"" << role_str << "\""
             << ",\"password\":\"" << json_escape(password) << "\"}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_patient_assign(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_perform_action(token, Permission::ASSIGN_PATIENTS)) {
        g_auth_manager->log_auth_decision(token->user_id, "ASSIGN_PATIENTS_DENY", false, "Missing permission");
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    try {
        json parsed = json::parse(req.body);
        std::string staff_id = parsed.contains("staff_id") ? parsed.at("staff_id").as_string("") : "";
        int pid = parsed.contains("patient_id") ? static_cast<int>(parsed.at("patient_id").as_double(-1)) : -1;
        bool unassign = parsed.contains("unassign") && parsed.at("unassign").as_bool(false);

        if (staff_id.empty() || pid <= 0) {
            return Response(400, "{\"error\": \"staff_id and patient_id are required\"}");
        }
        if (!g_auth_manager->get_staff_member(staff_id)) {
            return Response(404, "{\"error\": \"Staff member not found\"}");
        }
        const Patient* p = sim.get_patient(pid);
        if (!p || !p->active) {
            return Response(404, "{\"error\": \"Patient not found or discharged\"}");
        }

        bool ok = unassign
            ? g_auth_manager->deassign_patient_from_staff(staff_id, pid)
            : g_auth_manager->assign_patient_to_staff(staff_id, pid);
        if (!ok) {
            return Response(500, "{\"error\": \"Assignment update failed\"}");
        }
        g_auth_manager->log_auth_decision(token->user_id, unassign ? "UNASSIGN_PATIENT" : "ASSIGN_PATIENT", true,
                                           staff_id + " <-> patient " + std::to_string(pid));

        std::ostringstream body;
        body << "{\"status\":\"" << (unassign ? "unassigned" : "assigned") << "\",\"staff_id\":\"" << json_escape(staff_id)
             << "\",\"patient_id\":" << pid << "}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_alert_override(const Request& req) {
    // Provider / Charge Nurse only -- overriding a vitals-threshold alert is
    // a clinical judgment call (deriveAlerts() in ClinicalDashboard.ts
    // computes it purely from live vitals with no persisted alert-ID to
    // reference), so this records the override as an audited human-in-the-
    // loop event rather than mutating any alert state.
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_perform_action(token, Permission::OVERRIDE_ALERTS)) {
        g_auth_manager->log_auth_decision(token->user_id, "OVERRIDE_ALERTS_DENY", false, "Missing permission");
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    try {
        json parsed = json::parse(req.body);
        int pid = parsed.contains("patient_id") ? static_cast<int>(parsed.at("patient_id").as_double(-1)) : -1;
        std::string alert_description = parsed.contains("alert_description") ? parsed.at("alert_description").as_string("") : "";
        std::string reason = parsed.contains("reason") ? parsed.at("reason").as_string("") : "";

        if (pid <= 0 || alert_description.empty() || reason.empty()) {
            return Response(400, "{\"error\": \"patient_id, alert_description, and reason are required\"}");
        }
        if (!g_auth_manager->is_user_assigned_to_patient(token, pid)) {
            return Response(403, "{\"error\": \"Not assigned to this patient\"}");
        }

        if (acmk_coordinator && acmk_coordinator->get_environment_io()) {
            ACMK::HumanInterventionEvent event;
            event.event_type = "alert_override";
            event.user_id = token->user_id;
            event.session_id = parsed.contains("session_id") ? parsed.at("session_id").as_string("") : "";
            event.scope = "patient:" + std::to_string(pid);
            event.content = alert_description + " | reason: " + reason;
            event.timestamp = std::chrono::system_clock::now();
            acmk_coordinator->get_environment_io()->record_human_event(event);
        }
        g_auth_manager->log_auth_decision(token->user_id, "OVERRIDE_ALERT", true,
                                           "patient " + std::to_string(pid) + ": " + alert_description);

        std::ostringstream body;
        body << "{\"status\":\"override_recorded\",\"patient_id\":" << pid
             << ",\"alert_description\":\"" << json_escape(alert_description) << "\""
             << ",\"reason\":\"" << json_escape(reason) << "\""
             << ",\"overridden_by\":\"" << json_escape(token->staff_name) << "\"}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_learn(const Request& req) {
    // Feeds the model's learning loop -- unauthenticated access would let
    // anyone poison it with arbitrary prompt/response/was_good triples.
    if (!g_auth_manager->get_token(req.auth_token)) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::string prompt, response;
    bool was_good = true;
    
    size_t prompt_pos = req.body.find("\"prompt\"");
    if (prompt_pos != std::string::npos) {
        size_t start = req.body.find("\"", prompt_pos + 8) + 1;
        size_t end = req.body.find("\"", start);
        prompt = req.body.substr(start, end - start);
    }
    
    size_t resp_pos = req.body.find("\"response\"");
    if (resp_pos != std::string::npos) {
        size_t start = req.body.find("\"", resp_pos + 10) + 1;
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
    AuthToken* obs_token = g_auth_manager->get_token(req.auth_token);
    if (!obs_token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    // Accept patient_id from either the query string or the JSON body --
    // the frontend (web/src/api/client.ts: getPatientObservations) POSTs it
    // in the body.
    std::string patient_id_str;
    auto qp = req.query_params.find("patient_id");
    if (qp != req.query_params.end()) {
        patient_id_str = qp->second;
    } else {
        size_t pid_key = req.body.find("\"patient_id\"");
        if (pid_key != std::string::npos) {
            size_t colon = req.body.find(":", pid_key);
            size_t val_start = colon != std::string::npos ? req.body.find_first_of("0123456789", colon) : std::string::npos;
            if (val_start != std::string::npos) {
                size_t val_end = req.body.find_first_not_of("0123456789", val_start);
                patient_id_str = req.body.substr(val_start, val_end == std::string::npos ? std::string::npos : val_end - val_start);
            }
        }
    }

    if (patient_id_str.empty()) {
        std::string body = "{\"error\": \"Missing required parameter: patient_id\"}\n";
        return Response(400, body);
    }

    int patient_id;
    try {
        patient_id = std::stoi(patient_id_str);
    } catch (...) {
        return Response(400, "{\"error\": \"Invalid patient_id\"}\n");
    }
    if (!g_auth_manager->can_view_patient_vitals(obs_token, patient_id)) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

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

    // Build JSON response. Field names match the frontend's
    // PatientObservation type (web/src/api/types.ts) exactly --
    // observation_type/requires_nurse_attention, not type/requires_attention.
    std::string timestamp = current_timestamp();
    std::string body = "{\n";
    body += "  \"patient_id\": " + std::to_string(patient_id) + ",\n";
    body += "  \"patient_name\": \"" + patient->name + "\",\n";
    body += "  \"timestamp\": \"" + timestamp + "\",\n";
    body += "  \"observations\": [\n";

    for (size_t i = 0; i < observations.size(); i++) {
        const auto& obs = observations[i];
        body += "    {\n";
        body += "      \"patient_id\": " + std::to_string(patient_id) + ",\n";
        body += "      \"observation_type\": \"" + obs.observation_type + "\",\n";
        body += "      \"severity\": \"" + obs.severity + "\",\n";
        body += "      \"description\": \"" + json_escape(obs.description) + "\",\n";
        body += "      \"rationale\": \"" + json_escape(obs.rationale) + "\",\n";
        body += "      \"confidence\": " + std::to_string(obs.confidence) + ",\n";
        body += "      \"requires_nurse_attention\": " + std::string(obs.requires_nurse_attention ? "true" : "false") + ",\n";
        body += "      \"timestamp\": \"" + timestamp + "\",\n";
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
    AuthToken* scaffold_token = g_auth_manager->get_token(req.auth_token);
    if (!scaffold_token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    // Accept patient_id from either the query string or the JSON body --
    // the frontend (web/src/api/client.ts: generateScaffold) POSTs it in
    // the body, so query-param-only here meant every "Generate SBAR" click
    // in the UI 400'd before it could even run.
    std::string patient_id_str;
    auto qp = req.query_params.find("patient_id");
    if (qp != req.query_params.end()) {
        patient_id_str = qp->second;
    } else {
        size_t pid_key = req.body.find("\"patient_id\"");
        if (pid_key != std::string::npos) {
            size_t colon = req.body.find(":", pid_key);
            size_t val_start = colon != std::string::npos ? req.body.find_first_of("0123456789", colon) : std::string::npos;
            if (val_start != std::string::npos) {
                size_t val_end = req.body.find_first_not_of("0123456789", val_start);
                patient_id_str = req.body.substr(val_start, val_end == std::string::npos ? std::string::npos : val_end - val_start);
            }
        }
    }

    if (patient_id_str.empty()) {
        return Response(400, "{\"error\": \"Missing required parameter: patient_id\"}\n");
    }

    int patient_id;
    try {
        patient_id = std::stoi(patient_id_str);
    } catch (...) {
        return Response(400, "{\"error\": \"Invalid patient_id\"}\n");
    }
    if (!g_auth_manager->can_view_patient_vitals(scaffold_token, patient_id)) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    const Patient* patient = sim.get_patient(patient_id);

    if (!patient) {
        return Response(404, "{\"error\": \"Patient not found\"}\n");
    }

    auto observations = analyzer->analyze_patient(*patient, engine->get_rag_dag(), engine.get());

    // Structured components, not just the concatenated note -- the
    // frontend's SBARResponse type (web/src/api/types.ts) expects
    // sbar.{situation,background,assessment,recommendation} plus
    // confidence/priority/suggested_actions, not a single opaque string.
    DocumentationScaffold scaffold;
    std::string situation = scaffold.generate_situation(*patient, observations);
    std::string background = scaffold.generate_background(*patient);
    std::string assessment = scaffold.generate_assessment(observations);
    std::string recommendation = scaffold.generate_recommendation(observations);

    std::string priority = "LOW";
    float confidence_sum = 0.0f;
    bool has_critical = false, has_warning = false;
    for (const auto& obs : observations) {
        confidence_sum += obs.confidence;
        if (obs.severity == "critical") has_critical = true;
        else if (obs.severity == "warning") has_warning = true;
    }
    if (has_critical) priority = "CRITICAL";
    else if (has_warning) priority = "MEDIUM";
    float confidence = observations.empty() ? 1.0f : confidence_sum / observations.size();

    // Emit this reasoning pass into the ACMK-OT cognitive planes -- the
    // trace/explainability/perception endpoints (/api/acmk/trace/*,
    // /api/acmk/inference/decision/get) previously had no producer at all
    // feeding them real data (state emission, inference nodes, decision
    // envelopes, and perceptual artifacts were either never called or
    // routed through complete no-ops -- see state_plane.cpp). This is the
    // real clinical reasoning that just ran (ClinicalAnalyzer::analyze_patient
    // above), so it's what a nurse reviewing "why did it say that" should
    // actually see. Only runs when the caller has a real ACMK session
    // (established at sign-in via POST /api/acmk/session/init) to attach it to.
    std::string session_id;
    try {
        json body_json = json::parse(req.body);
        if (body_json.contains("session_id")) {
            session_id = body_json.at("session_id").as_string("");
        }
    } catch (const std::exception&) {
        // Body wasn't parseable JSON (e.g. patient_id came from the query
        // string with no body at all) -- reasoning trace is a nice-to-have
        // enrichment, not a reason to fail the SBAR generation itself.
    }

    if (!session_id.empty() && acmk_coordinator) {
        auto now = std::chrono::system_clock::now();

        if (acmk_coordinator->get_state_plane()) {
            ACMK::StateFrame frame;
            frame.session_id = session_id;
            frame.timestamp = now;
            frame.cognitive_state = ACMK::CognitiveState::CONVERGED;
            frame.risk_posture = has_critical ? ACMK::RiskPosture::CRITICAL
                                : has_warning  ? ACMK::RiskPosture::MODERATE
                                                : ACMK::RiskPosture::LOW;
            frame.input_modalities = {"vitals", "clinical_notes"};
            frame.confidence_global = confidence;
            frame.patient_id = std::to_string(patient_id);
            acmk_coordinator->get_state_plane()->emit_state(frame);
        }

        if (acmk_coordinator->get_trace_plane()) {
            std::vector<std::string> dominant_constraints;
            std::vector<std::pair<std::string, std::string>> rejected_alternatives;
            double min_conf = observations.empty() ? 0.0 : 1.0;
            double max_conf = 0.0;

            for (size_t i = 0; i < observations.size(); ++i) {
                const auto& obs = observations[i];
                min_conf = std::min(min_conf, static_cast<double>(obs.confidence));
                max_conf = std::max(max_conf, static_cast<double>(obs.confidence));

                ACMK::InferenceNode node;
                node.node_id = "obs_" + std::to_string(i) + "_" + obs.observation_type;
                node.status = obs.requires_nurse_attention ? "active" : "suppressed";
                node.confidence = obs.confidence;
                node.suppression_reason = obs.requires_nurse_attention ? "" : "below nurse-attention threshold";
                auto epoch_s = std::chrono::system_clock::to_time_t(now);
                node.time_range = {static_cast<long>(epoch_s), static_cast<long>(epoch_s)};
                acmk_coordinator->get_trace_plane()->record_frame(session_id, node);

                // The decision envelope's dominant_constraints/rejected_alternatives
                // mirror the same true/false split the SBAR's assessment/
                // recommendation text was built from (DocumentationScaffold
                // above), just in the ACMK-OT structured form instead of prose.
                if (obs.requires_nurse_attention) {
                    dominant_constraints.push_back(obs.description);
                } else {
                    rejected_alternatives.push_back({obs.observation_type + "_" + std::to_string(i), obs.description});
                }
            }

            ACMK::DecisionEnvelope envelope;
            envelope.final_state = recommendation;
            envelope.dominant_constraints = dominant_constraints;
            envelope.rejected_alternatives = rejected_alternatives;
            envelope.confidence_bounds = {min_conf, max_conf};
            acmk_coordinator->get_trace_plane()->record_decision_envelope(session_id, envelope);

            // Fingerprints the actual vitals reading this analysis ran
            // against, so "what did the system perceive" is answerable and
            // tamper-evident (a changed vitals value changes the hash).
            std::ostringstream vitals_str;
            vitals_str << patient->vitals.hr << "," << patient->vitals.rr << "," << patient->vitals.spo2 << ","
                       << patient->vitals.bp_sys << "," << patient->vitals.bp_dia << "," << patient->vitals.temp;
            ACMK::PerceptualArtifact artifact;
            artifact.artifact_id = "vitals_" + std::to_string(patient_id) + "_" + std::to_string(std::chrono::system_clock::to_time_t(now));
            artifact.artifact_type = "vitals_snapshot";
            artifact.content_hash = Crypto::sha256_hex(vitals_str.str());
            artifact.timestamp = now;
            artifact.confidence = 1.0; // directly measured, not inferred
            artifact.alignment_metadata = {"hr", "rr", "spo2", "bp_sys", "bp_dia", "temp"};
            acmk_coordinator->get_trace_plane()->record_perceptual_artifact(session_id, artifact);

            // A snapshot marks this reasoning pass as a point a nurse could
            // roll back to / replay from -- nothing previously called
            // create_snapshot() during real usage, so the temporal-control
            // UI (rollback/replay) always had an empty timeline. The hash
            // is a real fingerprint of this pass's actual output, so two
            // snapshots differ iff the reasoning actually differed.
            std::string snapshot_hash = Crypto::sha256_hex(recommendation + "|" + priority + "|" + std::to_string(confidence));
            acmk_coordinator->get_trace_plane()->create_snapshot(session_id, now, snapshot_hash);
        }
    }

    std::ostringstream body;
    body << "{\n";
    body << "  \"patient_id\": " << patient_id << ",\n";
    body << "  \"patient_name\": \"" << json_escape(patient->name) << "\",\n";
    body << "  \"sbar\": {\n";
    body << "    \"situation\": \"" << json_escape(situation) << "\",\n";
    body << "    \"background\": \"" << json_escape(background) << "\",\n";
    body << "    \"assessment\": \"" << json_escape(assessment) << "\",\n";
    body << "    \"recommendation\": \"" << json_escape(recommendation) << "\"\n";
    body << "  },\n";
    body << "  \"confidence\": " << confidence << ",\n";
    body << "  \"priority\": \"" << priority << "\",\n";
    body << "  \"suggested_actions\": [";
    for (size_t i = 0; i < observations.size(); ++i) {
        for (size_t j = 0; j < observations[i].suggested_actions.size(); ++j) {
            if (i > 0 || j > 0) body << ", ";
            body << "\"" << json_escape(observations[i].suggested_actions[j]) << "\"";
        }
    }
    body << "],\n";
    body << "  \"timestamp\": \"" << current_timestamp() << "\"\n";
    body << "}\n";

    return Response(200, body.str());
}


HTTPServer::Response HTTPServer::handle_action(const Request& req) {
    if (req.method != "POST") {
        return Response(405, "{\"error\": \"Method not allowed\"}");
    }
    
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized - missing or invalid token\"}");
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
                // A patient_id with too many digits overflows int and makes
                // stoi throw std::out_of_range -- uncaught, that would
                // crash the whole server (see handle_status for why).
                try {
                    pid = std::stoi(req.body.substr(val_start));
                } catch (...) {
                    pid = -1;
                }
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
    
    if (!g_auth_manager->can_chart_on_patient(token, pid, action)) {
        g_auth_manager->log_auth_decision(token->user_id, "CHART_DENY_" + action, false, "Not assigned or missing perm");
        return Response(403, "{\"error\": \"Forbidden - insufficient permissions\"}");
    }
    
    g_auth_manager->log_auth_decision(token->user_id, "CHART_ALLOW_" + action, true);
    
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

HTTPServer::Response HTTPServer::handle_chart(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    if (req.method == "GET") {
        auto qp = req.query_params.find("patient_id");
        if (qp == req.query_params.end()) {
            return Response(400, "{\"error\": \"patient_id query parameter required\"}");
        }
        int pid;
        try {
            pid = std::stoi(qp->second);
        } catch (...) {
            return Response(400, "{\"error\": \"Invalid patient_id\"}");
        }
        if (!g_auth_manager->can_view_patient_vitals(token, pid)) {
            return Response(403, "{\"error\": \"Forbidden\"}");
        }

        std::lock_guard<std::mutex> lock(chart_mutex_);
        std::ostringstream body;
        body << "{\"status\":\"success\",\"patient_id\":" << pid << ",\"entries\":[";
        auto it = chart_entries_.find(pid);
        if (it != chart_entries_.end()) {
            for (size_t i = 0; i < it->second.size(); ++i) {
                const auto& e = it->second[i];
                if (i > 0) body << ",";
                body << "{\"entry_id\":\"" << json_escape(e.entry_id) << "\","
                     << "\"type\":\"" << json_escape(e.entry_type) << "\","
                     << "\"content\":\"" << json_escape(e.content) << "\","
                     << "\"nurse\":\"" << json_escape(e.nurse_name) << "\","
                     << "\"timestamp\":" << e.timestamp << "}";
            }
        }
        body << "]}";
        return Response(200, body.str());
    }

    if (req.method != "POST") {
        return Response(405, "{\"error\": \"Method not allowed\"}");
    }

    try {
        json parsed = json::parse(req.body);
        int pid = static_cast<int>(parsed.at("patient_id").as_double(-1));
        std::string entry_type = parsed.contains("type") ? parsed.at("type").as_string("note") : "note";
        std::string content = parsed.contains("content") ? parsed.at("content").as_string("") : "";

        if (pid <= 0 || content.empty()) {
            return Response(400, "{\"error\": \"patient_id and content are required\"}");
        }
        if (!g_auth_manager->can_chart_on_patient(token, pid, entry_type)) {
            g_auth_manager->log_auth_decision(token->user_id, "CHART_NOTE_DENY", false, "Not assigned or missing perm");
            return Response(403, "{\"error\": \"Forbidden\"}");
        }

        StoredChartEntry entry;
        entry.patient_id = pid;
        entry.entry_type = entry_type;
        entry.content = content;
        entry.nurse_id = token->user_id;
        entry.nurse_name = token->staff_name;
        entry.timestamp = std::time(nullptr);

        std::string chart_log_path = "chart_log.ndjson";
        {
            std::lock_guard<std::mutex> lock(chart_mutex_);
            entry.entry_id = "chart_" + std::to_string(next_chart_entry_id_++);
            chart_entries_[pid].push_back(entry);

            json line = json::object();
            line["entry_id"] = entry.entry_id;
            line["patient_id"] = entry.patient_id;
            line["type"] = entry.entry_type;
            line["content"] = entry.content;
            line["nurse_id"] = entry.nurse_id;
            line["nurse_name"] = entry.nurse_name;
            line["timestamp"] = static_cast<long>(entry.timestamp);
            std::ofstream out(chart_log_path, std::ios::app);
            if (out.is_open()) out << line.dump() << "\n";
        }

        g_auth_manager->log_auth_decision(token->user_id, "CHART_NOTE_ALLOW", true);

        std::ostringstream body;
        body << "{\"status\":\"chart_entry_recorded\","
             << "\"entry_id\":\"" << json_escape(entry.entry_id) << "\","
             << "\"patient_id\":" << entry.patient_id << ","
             << "\"type\":\"" << json_escape(entry.entry_type) << "\","
             << "\"content\":\"" << json_escape(entry.content) << "\","
             << "\"nurse\":\"" << json_escape(entry.nurse_name) << "\","
             << "\"timestamp\":" << entry.timestamp << "}";
        return Response(200, body.str());
    } catch (const std::exception& e) {
        return Response(400, "{\"error\": \"Failed to record chart entry\"}");
    }
}

HTTPServer::Response HTTPServer::handle_sign_in(const Request& req) {
    if (req.method != "POST") return Response(405, "{\"error\": \"Method not allowed\"}");
    
    std::string staff_id = "";
    std::string password = "";
    
    size_t id_pos = req.body.find("\"staff_id\"");
    if (id_pos != std::string::npos) {
        size_t start = req.body.find("\"", id_pos + 10) + 1;
        size_t end = req.body.find("\"", start);
        staff_id = req.body.substr(start, end - start);
    }
    size_t pw_pos = req.body.find("\"password\"");
    if (pw_pos != std::string::npos) {
        size_t start = req.body.find("\"", pw_pos + 10) + 1;
        size_t end = req.body.find("\"", start);
        password = req.body.substr(start, end - start);
    }
    
    AuthToken token = g_auth_manager->authenticate(staff_id, password);
    if (token.token_id.empty()) {
        return Response(401, "{\"error\": \"Invalid credentials\"}");
    }
    
    std::stringstream response;
    response << "{\n"
             << "  \"token\": \"" << token.token_id << "\",\n"
             << "  \"user_id\": \"" << token.user_id << "\",\n"
             << "  \"staff_name\": \"" << token.staff_name << "\",\n"
             << "  \"role\": \"" << role_to_acmk_role_string(token.role) << "\",\n"
             << "  \"expires_at\": " << token.expires_at << "\n"
             << "}\n";
    return Response(200, response.str());
}

HTTPServer::Response HTTPServer::handle_training_start(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    if (!g_auth_manager->can_initiate_training(token)) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    // Identity comes from the verified token, never the request body -- a
    // client-supplied nurse_id/nurse_role here would let anyone attribute a
    // training session (and its scored actions) to a different nurse.
    std::string nurse_id = token->user_id;
    std::string nurse_role = role_to_acmk_role_string(token->role);

    {
        std::lock_guard<std::mutex> lock(training_mutex_);
        if (training_sessions_by_user_.count(nurse_id) > 0) {
            return Response(400, "{\"error\": \"You already have a training session active. End it before starting another.\"}");
        }
    }

    std::string scenario_id = "HYPOTENSION_001";
    size_t scenario_pos = req.body.find("\"scenario_id\"");
    if (scenario_pos != std::string::npos) {
        size_t colon = req.body.find(":", scenario_pos);
        size_t quote_start = colon != std::string::npos ? req.body.find("\"", colon) : std::string::npos;
        if (quote_start != std::string::npos) {
            size_t start = quote_start + 1;
            size_t end = req.body.find("\"", start);
            scenario_id = req.body.substr(start, end - start);
        }
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

    // Randomized per-session (age + a conservative baseline-vitals jitter,
    // never the grading rubric itself -- see randomize_case) so the same
    // scenario doesn't play out identically every run. Seeded from the
    // same CSPRNG auth tokens already use (Crypto::random_hex), not
    // time(nullptr) -- two sessions started within the same wall-clock
    // second (easy to hit back-to-back, e.g. an instructor demoing
    // several quick sessions) would otherwise collide on an identical
    // seed and produce byte-identical "randomized" cases.
    uint32_t seed = 0;
    {
        std::string entropy = Crypto::random_hex(4);
        seed = static_cast<uint32_t>(std::stoul(entropy, nullptr, 16));
    }
    scenario_def = ScenarioLibrary::randomize_case(scenario_def, seed);

    TrainingUserSession user_session;
    user_session.runtime = std::make_unique<ScenarioRuntime>(scenario_def);
    auto session = user_session.runtime->get_session_record();
    session.nurse_id = nurse_id;
    session.nurse_role = nurse_role;
    user_session.training_session_id = session.session_id;
    user_session.scenario_id = scenario_def.scenario_id;
    user_session.scenario_title = scenario_def.title;
    for (const auto& exp : scenario_def.expected_actions) {
        user_session.expected_action_names.push_back(exp.action_name);
    }

    if (analytics_store_) {
        const auto& sp0 = scenario_def.synthetic_patient;
        const auto& v0 = sp0.baseline_vitals;
        analytics_store_->record_session_start(user_session.training_session_id, scenario_def.scenario_id,
                                               nurse_id, sp0.age, sp0.sex, sp0.diagnosis,
                                               v0.hr, v0.rr, v0.spo2, v0.bp_sys, v0.bp_dia, v0.temp);
    }

    {
        std::lock_guard<std::mutex> lock(training_mutex_);
        training_sessions_by_user_[nurse_id] = std::move(user_session);
    }

    const auto& sp = scenario_def.synthetic_patient;
    const auto& v = sp.baseline_vitals;
    // synthetic_patient.patient_id is a string like "TRAIN-PT-00001"; the
    // frontend Patient type needs a number. Offset well clear of the real
    // patient roster (ids 1-6) so there's no collision.
    int synthetic_numeric_id = 90000;
    for (char c : sp.patient_id) {
        if (isdigit(static_cast<unsigned char>(c))) synthetic_numeric_id = synthetic_numeric_id * 10 + (c - '0');
    }

    // Shape matches the frontend's TrainingSession type exactly (see
    // web/src/api/types.ts and web/src/components/TrainingMode.ts, which
    // dereferences session.scenario.title / session.patient.vitals.hr
    // unconditionally on the very first render -- the old flat
    // {status,session_id,scenario_id,...} response had neither, so opening
    // Training Mode threw immediately).
    std::ostringstream body;
    body << "{\n";
    body << "  \"training_session_id\": \"" << json_escape(session.session_id) << "\",\n";
    body << "  \"status\": \"running\",\n";
    body << "  \"scenario\": {\n";
    body << "    \"id\": \"" << json_escape(scenario_def.scenario_id) << "\",\n";
    body << "    \"title\": \"" << json_escape(scenario_def.title) << "\",\n";
    body << "    \"difficulty\": \"" << difficulty_from_tier(scenario_def.tier) << "\",\n";
    body << "    \"description\": \"" << json_escape(scenario_def.description) << "\",\n";
    body << "    \"objectives\": [\"" << json_escape(scenario_def.description) << "\"],\n";
    body << "    \"available_actions\": [";
    for (size_t i = 0; i < scenario_def.expected_actions.size(); ++i) {
        const std::string& action_id = scenario_def.expected_actions[i].action_name;
        if (i > 0) body << ",";
        body << "{\"id\":\"" << json_escape(action_id) << "\",\"label\":\""
             << json_escape(action_label(action_id)) << "\"}";
    }
    // Notifying a provider is always clinically reasonable regardless of
    // scenario state, so it's offered everywhere in addition to the
    // scenario-specific actions above.
    body << ",{\"id\":\"notify_provider\",\"label\":\"" << json_escape(action_label("notify_provider")) << "\"}";
    body << "]\n";
    body << "  },\n";
    body << "  \"patient\": {\n";
    body << "    \"id\": " << synthetic_numeric_id << ",\n";
    body << "    \"name\": \"" << json_escape(sp.diagnosis) << " (Age " << sp.age << ", " << sp.sex << ")\",\n";
    body << "    \"mrn\": \"" << json_escape(sp.patient_id) << "\",\n";
    body << "    \"room\": \"" << json_escape(scenario_def.context_unit) << "\",\n";
    body << "    \"admission_diagnosis\": \"" << json_escape(sp.diagnosis) << "\",\n";
    body << "    \"acuity_score\": " << (scenario_def.tier == "CRISIS" ? 8 : 5) << ",\n";
    body << "    \"vitals\": {\n";
    body << "      \"hr\": " << v.hr << ", \"rr\": " << v.rr << ", \"spo2\": " << v.spo2 << ",\n";
    body << "      \"bp_sys\": " << v.bp_sys << ", \"bp_dia\": " << v.bp_dia << ", \"temp\": " << v.temp << ",\n";
    body << "      \"is_crisis\": false, \"drift_variance\": 0\n";
    body << "    },\n";
    body << "    \"hr_history\": [], \"spo2_history\": [], \"temp_history\": [], \"nurse_notes\": \"\"\n";
    body << "  },\n";
    body << "  \"elapsed_ms\": 0,\n";
    body << "  \"actions_taken\": 0,\n";
    body << "  \"score\": 0,\n";
    body << "  \"timestamp\": \"" << current_timestamp() << "\"\n";
    body << "}\n";

    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_training_status(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::lock_guard<std::mutex> lock(training_mutex_);
    auto it = training_sessions_by_user_.find(token->user_id);
    if (it == training_sessions_by_user_.end()) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    ScenarioRuntime* active_scenario_ = it->second.runtime.get();

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
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::lock_guard<std::mutex> lock(training_mutex_);
    auto it = training_sessions_by_user_.find(token->user_id);
    if (it == training_sessions_by_user_.end()) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    TrainingUserSession& user_session = it->second;
    ScenarioRuntime* active_scenario_ = user_session.runtime.get();
    // Identity comes from the verified token, not a client-supplied body
    // field -- the frontend (web/src/api/types.ts: TrainingAction) never
    // actually sends nurse_id at all, so this was silently recording every
    // action under an empty nurse_id string.
    const std::string& nurse_id = token->user_id;

    std::string action;
    size_t action_pos = req.body.find("\"action\"");
    if (action_pos != std::string::npos) {
        size_t colon = req.body.find(":", action_pos);
        size_t quote_start = colon != std::string::npos ? req.body.find("\"", colon) : std::string::npos;
        if (quote_start != std::string::npos) {
            size_t start = quote_start + 1;
            size_t end = req.body.find("\"", start);
            action = req.body.substr(start, end - start);
        }
    }

    if (action.empty()) {
        return Response(400, "{\"error\": \"Missing action\"}");
    }

    ScenarioVitals before = active_scenario_->get_current_vitals();
    active_scenario_->accept_action(action, nurse_id);

    ActionEvaluation eval = active_scenario_->evaluate_action_correctness(action);
    if (eval.triggers_complication) {
        active_scenario_->arm_complication(eval.complication_name);
    }
    // Complication (if any) and the action's own physiology curve both
    // start contributing as of this action, so recompute once more before
    // reading vitals back -- otherwise "after" would still reflect last
    // tick's overlay, not this action's.
    active_scenario_->tick(0);
    ScenarioVitals after = active_scenario_->get_current_vitals();

    bool was_timely = (eval.grade == ActionGrade::CORRECT || eval.grade == ActionGrade::PARTIALLY_CORRECT);
    std::string grade_str = action_grade_to_string(eval.grade);

    if (analytics_store_) {
        analytics_store_->record_nurse_action(user_session.training_session_id, action, nurse_id,
                                             active_scenario_->elapsed_seconds(), was_timely,
                                             grade_str, eval.score_delta);
    }

    // TrainingMode.ts renders this as (session.score * 100).toFixed(0) + '%',
    // so score is a 0..1 fraction, not a raw point total.
    user_session.score = std::max(0.0f, std::min(1.0f, user_session.score + eval.score_delta));
    user_session.actions_taken += 1;

    // Shape matches TrainingActionResponse (web/src/api/types.ts), which
    // main-refactored.ts reads unconditionally (response.cumulative_score)
    // after every action -- the old {status,action,correct,feedback,...}
    // response left that undefined, showing "NaN%" as the session score.
    std::ostringstream body;
    body << "{\n";
    body << "  \"training_session_id\": \"" << json_escape(user_session.training_session_id) << "\",\n";
    body << "  \"action\": \"" << json_escape(action) << "\",\n";
    body << "  \"status\": \"" << grade_str << "\",\n";
    body << "  \"effectiveness\": " << (was_timely ? 1.0 : 0.0) << ",\n";
    body << "  \"patient_response\": {\n";
    body << "    \"spo2_change\": " << (after.spo2 - before.spo2) << ",\n";
    body << "    \"hr_change\": " << (after.hr - before.hr) << ",\n";
    body << "    \"feedback\": \"" << json_escape(eval.feedback) << "\"\n";
    body << "  },\n";
    body << "  \"score_delta\": " << eval.score_delta << ",\n";
    body << "  \"cumulative_score\": " << user_session.score << ",\n";
    body << "  \"timestamp\": \"" << current_timestamp() << "\"\n";
    body << "}\n";

    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_training_tick(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::lock_guard<std::mutex> lock(training_mutex_);
    auto it = training_sessions_by_user_.find(token->user_id);
    if (it == training_sessions_by_user_.end()) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    TrainingUserSession& user_session = it->second;
    ScenarioRuntime* active_scenario_ = user_session.runtime.get();

    int delta_seconds = 30;

    auto delta_pos = req.query_params.find("delta_seconds");
    if (delta_pos != req.query_params.end()) {
        try {
            delta_seconds = std::stoi(delta_pos->second);
        } catch (...) {
            return Response(400, "{\"error\": \"Invalid delta_seconds\"}");
        }
    }

    active_scenario_->tick(delta_seconds);

    ScenarioVitals vitals = active_scenario_->get_current_vitals();
    auto failures = active_scenario_->check_failure_conditions();

    // Feeds handle_training_end's "improvement_areas" -- computed here but
    // never previously written anywhere, so a missed critical window was
    // invisible in every debrief. Deduplicated against
    // failures_already_recorded since check_failure_conditions() re-reports
    // every still-true condition on every tick, not just newly-true ones.
    if (analytics_store_) {
        for (const auto& failure_name : failures) {
            if (user_session.failures_already_recorded.insert(failure_name).second) {
                analytics_store_->record_failure_condition(user_session.training_session_id, failure_name,
                                                             active_scenario_->elapsed_seconds());
            }
        }
    }

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
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }

    std::lock_guard<std::mutex> lock(training_mutex_);
    auto it = training_sessions_by_user_.find(token->user_id);
    if (it == training_sessions_by_user_.end()) {
        return Response(400, "{\"error\": \"No active training session\"}");
    }
    TrainingUserSession user_session = std::move(it->second);
    training_sessions_by_user_.erase(it);
    ScenarioRuntime* active_scenario_ = user_session.runtime.get();

    // The client (web/src/api/client.ts: endTraining) actually sends
    // {"early_termination": bool}, not an "outcome" string -- this outcome
    // parsing block below reads a field the client never sends, so
    // early_termination was silently ignored and every session logged as
    // COMPLETED regardless of how it actually ended.
    std::string outcome = "COMPLETED";
    size_t early_term_pos = req.body.find("\"early_termination\"");
    if (early_term_pos != std::string::npos) {
        size_t colon = req.body.find(":", early_term_pos);
        size_t value_start = req.body.find_first_not_of(" \t", colon + 1);
        if (colon != std::string::npos && value_start != std::string::npos &&
            req.body.compare(value_start, 4, "true") == 0) {
            outcome = "EARLY_TERMINATION";
        }
    }

    auto session = active_scenario_->get_session_record();
    active_scenario_->finalize_session(outcome);
    int duration_seconds = active_scenario_->elapsed_seconds();

    // Was defined but never once called -- calculate_session_metrics() (and
    // therefore this debrief) depends on a SESSION_COMPLETE event existing
    // to recover scenario_id/nurse_id/duration/outcome for the session.
    if (analytics_store_) {
        analytics_store_->record_session_complete(user_session.training_session_id, user_session.scenario_id,
                                                    token->user_id, outcome, duration_seconds,
                                                    user_session.score * 100.0f);
    }

    // Build the debrief from the real event log for this session -- the
    // whole point of simulation training is the post-scenario reflection,
    // and until now the frontend just showed a toast and navigated back to
    // the dashboard, discarding everything the server had just computed.
    std::vector<TrainingEvent> events = analytics_store_ ? analytics_store_->get_session_events(user_session.training_session_id)
                                                          : std::vector<TrainingEvent>();

    std::string report = build_training_report_json(
        events, user_session.training_session_id, user_session.scenario_title,
        outcome, duration_seconds, user_session.score, user_session.expected_action_names);

    return Response(200, report);
}

std::string HTTPServer::build_training_report_json(
    const std::vector<TrainingEvent>& events,
    const std::string& training_session_id,
    const std::string& scenario_title,
    const std::string& outcome,
    int duration_seconds,
    float final_score_0_to_1,
    const std::vector<std::string>& expected_action_names) {
    std::ostringstream transcript;
    std::ostringstream learning_summary;
    std::ostringstream improvement_areas;
    bool has_transcript = false, has_learning = false, has_improvement = false;
    std::set<std::string> correct_action_names, incorrect_seen;
    int first_correct_elapsed = -1;
    bool escalated = false;

    auto append_json_str_item = [](std::ostringstream& out, bool& has_any, const std::string& text) {
        if (has_any) out << ",";
        out << "\"" << text << "\"";
        has_any = true;
    };

    transcript << "[";
    for (const auto& evt : events) {
        if (evt.event_type != "NURSE_ACTION") continue;
        std::string action_name = parse_event_data_field(evt.event_data, "action");
        bool is_correct = parse_event_data_field(evt.event_data, "timely") == "true";
        if (action_name.empty()) continue;

        if (has_transcript) transcript << ",";
        transcript << "{\"time\":\"" << evt.elapsed_seconds << "s\",\"action\":\""
                   << json_escape(action_label(action_name)) << "\",\"score\":"
                   << (is_correct ? 0.1 : -0.05) << "}";
        has_transcript = true;

        if (action_name == "notify_provider") escalated = true;

        if (is_correct) {
            if (first_correct_elapsed < 0) first_correct_elapsed = evt.elapsed_seconds;
            if (correct_action_names.insert(action_name).second) {
                append_json_str_item(learning_summary, has_learning,
                    json_escape("Correctly performed: " + action_label(action_name) + " (at " + std::to_string(evt.elapsed_seconds) + "s)"));
            }
        } else {
            if (incorrect_seen.insert(action_name).second) {
                append_json_str_item(improvement_areas, has_improvement,
                    json_escape(action_label(action_name) + " was not appropriate at that point in the scenario (at " + std::to_string(evt.elapsed_seconds) + "s)"));
            }
        }
    }
    transcript << "]";

    for (const auto& evt : events) {
        if (evt.event_type != "FAILURE_TRIGGERED") continue;
        append_json_str_item(improvement_areas, has_improvement,
            json_escape("Missed critical window: " + evt.event_data + " (at " + std::to_string(evt.elapsed_seconds) + "s)"));
    }

    int expected_performed = 0;
    for (const auto& expected : expected_action_names) {
        if (correct_action_names.count(expected) > 0) {
            expected_performed++;
        } else {
            append_json_str_item(improvement_areas, has_improvement,
                json_escape("Expected intervention not performed: " + action_label(expected)));
        }
    }

    std::string assessment = final_score_0_to_1 >= 0.8f ? "Excellent -- demonstrated strong clinical judgment throughout"
                            : final_score_0_to_1 >= 0.5f ? "Satisfactory -- met most critical objectives"
                            : "Needs improvement -- review this scenario's core objectives";
    std::string time_to_intervention = first_correct_elapsed >= 0
        ? std::to_string(first_correct_elapsed) + "s to first correct intervention"
        : "No correct intervention taken";
    std::string intervention_selection = std::to_string(expected_performed) + " of "
        + std::to_string(expected_action_names.size()) + " expected interventions performed";
    std::string escalation = escalated ? "Provider notified during scenario" : "Provider not notified";

    std::ostringstream body;
    body << "{\n";
    body << "  \"training_session_id\": \"" << json_escape(training_session_id) << "\",\n";
    body << "  \"status\": \"" << json_escape(outcome) << "\",\n";
    body << "  \"scenario\": \"" << json_escape(scenario_title) << "\",\n";
    body << "  \"duration_ms\": " << (duration_seconds * 1000) << ",\n";
    body << "  \"final_score\": " << final_score_0_to_1 << ",\n";
    body << "  \"performance\": {\n";
    body << "    \"assessment\": \"" << json_escape(assessment) << "\",\n";
    body << "    \"time_to_intervention\": \"" << json_escape(time_to_intervention) << "\",\n";
    body << "    \"intervention_selection\": \"" << json_escape(intervention_selection) << "\",\n";
    body << "    \"escalation\": \"" << json_escape(escalation) << "\"\n";
    body << "  },\n";
    body << "  \"learning_summary\": [" << learning_summary.str() << "],\n";
    body << "  \"improvement_areas\": [" << improvement_areas.str() << "],\n";
    body << "  \"transcript\": " << transcript.str() << ",\n";
    body << "  \"timestamp\": \"" << current_timestamp() << "\"\n";
    body << "}\n";

    return body.str();
}

std::string HTTPServer::build_training_note_draft_json(
    const std::vector<TrainingEvent>& events,
    const std::string& session_id,
    const std::string& scenario_id,
    const std::string& scenario_title,
    const std::string& outcome,
    int duration_seconds,
    const std::vector<std::string>& expected_action_names,
    const ScenarioDefinition::SyntheticPatient& fallback_patient) {

    int age = fallback_patient.age;
    std::string sex = fallback_patient.sex;
    std::string diagnosis = fallback_patient.diagnosis;
    int hr = fallback_patient.baseline_vitals.hr, rr = fallback_patient.baseline_vitals.rr,
        spo2 = fallback_patient.baseline_vitals.spo2, bp_sys = fallback_patient.baseline_vitals.bp_sys,
        bp_dia = fallback_patient.baseline_vitals.bp_dia;
    float temp = fallback_patient.baseline_vitals.temp;

    // Prefer the actual (possibly randomized) patient this session started
    // with over the canonical scenario template, so a note for a
    // randomized case shows what the nurse actually saw.
    for (const auto& evt : events) {
        if (evt.event_type != "SESSION_START") continue;
        try {
            json data = json::parse(evt.event_data);
            if (data.contains("age")) age = static_cast<int>(data.at("age").as_double(age));
            if (data.contains("sex")) sex = data.at("sex").as_string(sex);
            if (data.contains("diagnosis")) diagnosis = data.at("diagnosis").as_string(diagnosis);
            if (data.contains("hr")) hr = static_cast<int>(data.at("hr").as_double(hr));
            if (data.contains("rr")) rr = static_cast<int>(data.at("rr").as_double(rr));
            if (data.contains("spo2")) spo2 = static_cast<int>(data.at("spo2").as_double(spo2));
            if (data.contains("bp_sys")) bp_sys = static_cast<int>(data.at("bp_sys").as_double(bp_sys));
            if (data.contains("bp_dia")) bp_dia = static_cast<int>(data.at("bp_dia").as_double(bp_dia));
            if (data.contains("temp")) temp = static_cast<float>(data.at("temp").as_double(temp));
        } catch (const std::exception&) {
            // Malformed/missing snapshot -- fall back to the canonical
            // scenario template already loaded above.
        }
        break;
    }

    std::vector<std::pair<int, std::string>> correct_actions;  // elapsed_sec, label
    std::vector<std::string> missed_windows;
    std::set<std::string> performed;
    for (const auto& evt : events) {
        if (evt.event_type == "NURSE_ACTION") {
            std::string action_name = parse_event_data_field(evt.event_data, "action");
            bool is_correct = parse_event_data_field(evt.event_data, "timely") == "true";
            if (action_name.empty()) continue;
            if (is_correct && performed.insert(action_name).second) {
                correct_actions.push_back({evt.elapsed_seconds, action_label(action_name)});
            }
        } else if (evt.event_type == "FAILURE_TRIGGERED") {
            missed_windows.push_back(evt.event_data);
        }
    }
    std::vector<std::string> not_performed;
    for (const auto& expected : expected_action_names) {
        if (performed.count(expected) == 0) not_performed.push_back(action_label(expected));
    }

    std::ostringstream note;
    note << "=== SIMULATION TRAINING NOTE -- NOT FOR CLINICAL USE ===\n\n";
    note << "SITUATION\n";
    note << "Completed \"" << scenario_title << "\" training scenario. Outcome: " << outcome
         << ". Duration: " << (duration_seconds / 60) << "m " << (duration_seconds % 60) << "s.\n\n";
    note << "BACKGROUND\n";
    note << "Simulated patient, age " << age << ", " << sex << ". " << diagnosis << ".\n";
    note << "Baseline vitals: HR " << hr << ", RR " << rr << ", SpO2 " << spo2 << "%, BP "
         << bp_sys << "/" << bp_dia << ", Temp " << std::fixed << std::setprecision(1) << temp << "C.\n\n";
    note << "ASSESSMENT\n";
    if (correct_actions.empty()) {
        note << "No correct interventions recorded this session.\n";
    } else {
        for (const auto& [t, label] : correct_actions) {
            note << "- Performed " << label << " at " << t << "s.\n";
        }
    }
    for (const auto& mw : missed_windows) {
        note << "- Missed critical window: " << mw << "\n";
    }
    note << "\nRECOMMENDATION (follow-up learning focus)\n";
    if (not_performed.empty()) {
        note << "All expected interventions for this scenario were performed.\n";
    } else {
        for (const auto& na : not_performed) {
            note << "- Review: " << na << " was not performed during this session.\n";
        }
    }

    json out = json::object();
    out["session_id"] = json(session_id);
    out["scenario_id"] = json(scenario_id);
    out["draft_content"] = json(note.str());
    out["generated_at"] = json(current_timestamp());
    return out.dump();
}

HTTPServer::Response HTTPServer::handle_training_note_draft(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (!analytics_store_) return Response(500, "{\"error\": \"Analytics store not initialized\"}");

    std::string session_id = req.query_params.count("session_id") ? req.query_params.at("session_id") : "";
    if (session_id.empty()) return Response(400, "{\"error\": \"Missing required parameter: session_id\"}");
    if (!analytics_store_->session_exists(session_id)) return Response(404, "{\"error\": \"Session not found\"}");

    // nurse_id/outcome are both only populated by the SESSION_COMPLETE
    // event (calculate_session_metrics), so an incomplete session always
    // has an empty nurse_id too -- check completeness first, or every
    // incomplete session reports a misleading 403 "Forbidden" (nurse_id
    // "" != token->user_id) instead of the real "not yet complete" state.
    TrainingMetrics metrics = analytics_store_->calculate_session_metrics(session_id);
    if (metrics.outcome.empty()) return Response(400, "{\"error\": \"Session is not yet complete\"}");
    // Drafting/signing is an act of authorship, not review -- owner-only,
    // stricter than handle_training_report's owner-or-instructor/admin ACL.
    if (metrics.nurse_id != token->user_id) return Response(403, "{\"error\": \"Forbidden\"}");

    ScenarioDefinition scenario_def = get_scenario_definition_by_id(metrics.scenario_id);
    std::vector<std::string> expected_action_names;
    for (const auto& exp : scenario_def.expected_actions) expected_action_names.push_back(exp.action_name);

    auto events = analytics_store_->get_session_events(session_id);
    std::string draft_json = build_training_note_draft_json(
        events, session_id, metrics.scenario_id, scenario_def.title, metrics.outcome,
        metrics.total_duration_seconds, expected_action_names, scenario_def.synthetic_patient);

    std::string draft_content;
    try {
        json parsed = json::parse(draft_json);
        draft_content = parsed.at("draft_content").as_string("");
    } catch (const std::exception&) {
        return Response(500, "{\"error\": \"Failed to build draft\"}");
    }
    analytics_store_->record_note_drafted(session_id, metrics.scenario_id, metrics.total_duration_seconds, draft_content);

    return Response(200, draft_json);
}

HTTPServer::Response HTTPServer::handle_training_note_sign(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (!analytics_store_) return Response(500, "{\"error\": \"Analytics store not initialized\"}");

    try {
        json parsed = json::parse(req.body);
        std::string session_id = parsed.contains("session_id") ? parsed.at("session_id").as_string("") : "";
        std::string content = parsed.contains("content") ? parsed.at("content").as_string("") : "";

        if (session_id.empty() || content.empty()) {
            return Response(400, "{\"error\": \"session_id and content are required\"}");
        }
        if (!analytics_store_->session_exists(session_id)) return Response(404, "{\"error\": \"Session not found\"}");

        // Same ordering fix as handle_training_note_draft: outcome-empty
        // must be checked before ownership, since nurse_id is also only
        // populated by the same SESSION_COMPLETE event.
        TrainingMetrics metrics = analytics_store_->calculate_session_metrics(session_id);
        if (metrics.outcome.empty()) return Response(400, "{\"error\": \"Session is not yet complete\"}");
        if (metrics.nurse_id != token->user_id) return Response(403, "{\"error\": \"Forbidden\"}");

        // was_edited is informational only (audit trail) -- the frontend
        // doesn't currently distinguish, so this just records whatever the
        // client sends, defaulting false rather than guessing.
        bool was_edited = parsed.contains("was_edited") && parsed.at("was_edited").as_bool(false);

        analytics_store_->record_note_signed(session_id, metrics.scenario_id, token->user_id,
                                            metrics.total_duration_seconds, content, was_edited);

        std::ostringstream body;
        body << "{\"status\":\"signed\",\"session_id\":\"" << json_escape(session_id)
             << "\",\"signed_at\":\"" << current_timestamp() << "\"}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_training_list(const Request& req) {
    if (!g_auth_manager->get_token(req.auth_token)) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
    auto scenario_ids = ScenarioLibrary::list_available_scenarios();

    std::ostringstream body;
    body << "{\n  \"scenarios\": [\n";

    for (size_t i = 0; i < scenario_ids.size(); i++) {
        ScenarioDefinition def = get_scenario_definition_by_id(scenario_ids[i]);
        body << "    {\n";
        body << "      \"id\": \"" << json_escape(def.scenario_id) << "\",\n";
        body << "      \"title\": \"" << json_escape(def.title) << "\",\n";
        body << "      \"category\": \"" << json_escape(scenario_category_from_id(def.scenario_id)) << "\",\n";
        body << "      \"difficulty\": \"" << difficulty_from_tier(def.tier) << "\",\n";
        body << "      \"duration_min\": " << duration_minutes_from_timeline(def) << ",\n";
        body << "      \"learning_objectives\": [\"" << json_escape(def.description) << "\"]\n";
        body << "    }";
        if (i < scenario_ids.size() - 1) body << ",";
        body << "\n";
    }

    body << "  ]\n}\n";

    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_training_analytics(const Request& req) {
    if (!g_auth_manager->get_token(req.auth_token)) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
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
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\": \"Unauthorized\"}");
    }
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

    // A session's transcript is per-nurse clinical performance data -- only
    // the nurse who ran it, an instructor (reviewing their cohort), or an
    // admin should be able to read it. Previously any authenticated staff
    // member could pull any other nurse's report by guessing/enumerating
    // session_id, since this only ever checked that *a* token was present.
    TrainingMetrics owner_check = analytics_store_->calculate_session_metrics(session_id);
    bool is_owner = owner_check.nurse_id == token->user_id;
    bool is_reviewer = token->role == Role::INSTRUCTOR || token->role == Role::ADMIN;
    if (!is_owner && !is_reviewer) {
        return Response(403, "{\"error\": \"Forbidden\"}");
    }

    // Reconstruct the same rich shape handle_training_end returns live --
    // expected_action_names/scenario_title aren't in the persisted event
    // log (they lived only on the in-memory TrainingUserSession, gone once
    // the session ended), but they're a pure function of scenario_id, so
    // get_scenario_definition_by_id() recovers them exactly.
    ScenarioDefinition scenario_def = get_scenario_definition_by_id(owner_check.scenario_id);
    std::vector<std::string> expected_action_names;
    for (const auto& exp : scenario_def.expected_actions) expected_action_names.push_back(exp.action_name);

    auto events = analytics_store_->get_session_events(session_id);
    std::string report = build_training_report_json(
        events, session_id, scenario_def.title, owner_check.outcome,
        owner_check.total_duration_seconds, owner_check.final_score / 100.0f, expected_action_names);

    return Response(200, report);
}

// ============================================================================
// Instructor / cohort management (mass education adoption)
// ============================================================================

HTTPServer::Response HTTPServer::handle_instructor_create_cohort(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (token->role != Role::INSTRUCTOR) return Response(403, "{\"error\": \"Forbidden\"}");

    try {
        json parsed = json::parse(req.body);
        std::string name = parsed.contains("name") ? parsed.at("name").as_string("") : "";
        if (name.empty()) return Response(400, "{\"error\": \"name is required\"}");

        Cohort cohort = g_cohort_manager->create_cohort(token->user_id, name);

        std::ostringstream body;
        body << "{\"cohort_id\":\"" << json_escape(cohort.cohort_id) << "\""
             << ",\"name\":\"" << json_escape(cohort.name) << "\""
             << ",\"created_at\":" << static_cast<long>(cohort.created_at)
             << ",\"student_count\":0}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_instructor_list_cohorts(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (token->role != Role::INSTRUCTOR) return Response(403, "{\"error\": \"Forbidden\"}");

    auto cohorts = g_cohort_manager->list_cohorts_for_instructor(token->user_id);
    std::ostringstream body;
    body << "{\"cohorts\":[";
    for (size_t i = 0; i < cohorts.size(); ++i) {
        const auto& c = cohorts[i];
        if (i > 0) body << ",";
        body << "{\"cohort_id\":\"" << json_escape(c.cohort_id) << "\""
             << ",\"name\":\"" << json_escape(c.name) << "\""
             << ",\"created_at\":" << static_cast<long>(c.created_at)
             << ",\"student_count\":" << c.students.size() << "}";
    }
    body << "]}";
    return Response(200, body.str());
}

HTTPServer::Response HTTPServer::handle_instructor_import_roster(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (token->role != Role::INSTRUCTOR) return Response(403, "{\"error\": \"Forbidden\"}");

    try {
        json parsed = json::parse(req.body);
        std::string cohort_id = parsed.contains("cohort_id") ? parsed.at("cohort_id").as_string("") : "";
        if (cohort_id.empty() || !g_cohort_manager->instructor_owns_cohort(token->user_id, cohort_id)) {
            return Response(404, "{\"error\": \"Cohort not found\"}");
        }
        if (!parsed.contains("students")) {
            return Response(400, "{\"error\": \"students array is required\"}");
        }

        std::vector<std::pair<std::string, std::string>> students;
        const json& students_json = parsed.at("students");
        for (const auto& s_ptr : students_json.items()) {
            const json& s = *s_ptr;
            std::string sname = s.contains("name") ? s.at("name").as_string("") : "";
            std::string sext = s.contains("external_id") ? s.at("external_id").as_string("") : "";
            students.emplace_back(sname, sext);
        }

        auto credentials = g_cohort_manager->import_roster(cohort_id, students);
        g_auth_manager->log_auth_decision(token->user_id, "COHORT_IMPORT_ROSTER", true,
            std::to_string(credentials.size()) + " student(s) into " + cohort_id);

        std::ostringstream body;
        body << "{\"imported\":" << credentials.size() << ",\"credentials\":[";
        for (size_t i = 0; i < credentials.size(); ++i) {
            const auto& c = credentials[i];
            if (i > 0) body << ",";
            body << "{\"staff_id\":\"" << json_escape(c.staff_id) << "\""
                 << ",\"name\":\"" << json_escape(c.display_name) << "\""
                 << ",\"password\":\"" << json_escape(c.password) << "\"}";
        }
        body << "]}";
        return Response(200, body.str());
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_instructor_remove_student(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (token->role != Role::INSTRUCTOR) return Response(403, "{\"error\": \"Forbidden\"}");

    try {
        json parsed = json::parse(req.body);
        std::string cohort_id = parsed.contains("cohort_id") ? parsed.at("cohort_id").as_string("") : "";
        std::string staff_id = parsed.contains("staff_id") ? parsed.at("staff_id").as_string("") : "";
        if (cohort_id.empty() || staff_id.empty() || !g_cohort_manager->instructor_owns_cohort(token->user_id, cohort_id)) {
            return Response(404, "{\"error\": \"Cohort not found\"}");
        }
        bool ok = g_cohort_manager->remove_student(cohort_id, staff_id);
        if (!ok) return Response(404, "{\"error\": \"Student not found in cohort\"}");
        return Response(200, "{\"status\":\"removed\"}");
    } catch (const std::exception&) {
        return Response(400, "{\"error\": \"Invalid request body\"}");
    }
}

HTTPServer::Response HTTPServer::handle_instructor_cohort_dashboard(const Request& req) {
    // The main instructor view: per-student session history/scores plus a
    // cohort-wide "most commonly missed interventions" breakdown, built
    // entirely from real TrainingAnalyticsStore event data (nothing here is
    // computed from a field that doesn't have a genuine source event -- see
    // calculate_session_metrics's comments on ai_recommendation_acceptance_rate
    // for the standard this follows: omit rather than fabricate).
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\": \"Unauthorized\"}");
    if (token->role != Role::INSTRUCTOR) return Response(403, "{\"error\": \"Forbidden\"}");
    if (!analytics_store_) return Response(500, "{\"error\": \"Analytics store not initialized\"}");

    std::string cohort_id = req.query_params.count("cohort_id") ? req.query_params.at("cohort_id") : "";
    const Cohort* cohort = g_cohort_manager->get_cohort(cohort_id);
    if (cohort_id.empty() || !cohort || cohort->instructor_id != token->user_id) {
        return Response(404, "{\"error\": \"Cohort not found\"}");
    }

    std::map<std::string, const CohortStudent*> student_by_id;
    for (const auto& s : cohort->students) student_by_id[s.staff_id] = &s;

    struct SessionSummary {
        std::string session_id, scenario_id, outcome;
        float score;
        int duration_seconds;
        int missed_critical_windows;
    };
    std::map<std::string, std::vector<SessionSummary>> sessions_by_student;
    std::map<std::pair<std::string, std::string>, int> missed_intervention_counts;  // (scenario_id, failure) -> count

    for (const auto& sid : analytics_store_->list_all_sessions()) {
        TrainingMetrics m = analytics_store_->calculate_session_metrics(sid);
        if (student_by_id.find(m.nurse_id) == student_by_id.end()) continue;  // not a member of this cohort
        if (m.outcome.empty()) continue;  // session never completed -- no SESSION_COMPLETE event yet

        sessions_by_student[m.nurse_id].push_back(
            {sid, m.scenario_id, m.outcome, m.final_score, m.total_duration_seconds, m.missed_critical_windows});

        for (const auto& evt : analytics_store_->get_session_events(sid)) {
            if (evt.event_type != "FAILURE_TRIGGERED") continue;
            std::string failure = parse_event_data_field(evt.event_data, "failure");
            if (!failure.empty()) missed_intervention_counts[{m.scenario_id, failure}]++;
        }
    }

    std::ostringstream body;
    body << "{\"cohort\":{\"cohort_id\":\"" << json_escape(cohort->cohort_id) << "\""
         << ",\"name\":\"" << json_escape(cohort->name) << "\""
         << ",\"created_at\":" << static_cast<long>(cohort->created_at) << "}"
         << ",\"students\":[";
    for (size_t i = 0; i < cohort->students.size(); ++i) {
        const auto& s = cohort->students[i];
        if (i > 0) body << ",";
        auto it = sessions_by_student.find(s.staff_id);
        const std::vector<SessionSummary>* sessions = (it != sessions_by_student.end()) ? &it->second : nullptr;
        float avg_score = 0.0f;
        if (sessions && !sessions->empty()) {
            float sum = 0.0f;
            for (const auto& ss : *sessions) sum += ss.score;
            avg_score = sum / static_cast<float>(sessions->size());
        }
        body << "{\"staff_id\":\"" << json_escape(s.staff_id) << "\""
             << ",\"name\":\"" << json_escape(s.display_name) << "\""
             << ",\"external_id\":\"" << json_escape(s.external_id) << "\""
             << ",\"session_count\":" << (sessions ? sessions->size() : 0)
             << ",\"avg_score\":" << avg_score
             << ",\"sessions\":[";
        if (sessions) {
            for (size_t j = 0; j < sessions->size(); ++j) {
                const auto& ss = (*sessions)[j];
                if (j > 0) body << ",";
                body << "{\"session_id\":\"" << json_escape(ss.session_id) << "\""
                     << ",\"scenario_id\":\"" << json_escape(ss.scenario_id) << "\""
                     << ",\"outcome\":\"" << json_escape(ss.outcome) << "\""
                     << ",\"score\":" << ss.score
                     << ",\"duration_seconds\":" << ss.duration_seconds
                     << ",\"missed_critical_windows\":" << ss.missed_critical_windows << "}";
            }
        }
        body << "]}";
    }
    body << "],\"top_missed_interventions\":[";
    {
        std::vector<std::pair<std::pair<std::string, std::string>, int>> sorted(
            missed_intervention_counts.begin(), missed_intervention_counts.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (i > 0) body << ",";
            body << "{\"scenario_id\":\"" << json_escape(sorted[i].first.first) << "\""
                 << ",\"failure\":\"" << json_escape(sorted[i].first.second) << "\""
                 << ",\"count\":" << sorted[i].second << "}";
        }
    }
    body << "]}";
    return Response(200, body.str());
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

    // Frontend build is embedded directly into this binary (see
    // include/embedded_web_assets.h, scripts/embed_web_assets.py) so the
    // compiled server is a genuinely single, self-contained executable --
    // no web/dist/ directory has to travel with it. Falls back to reading
    // web/dist/ off disk only if nothing embedded matches, so a local dev
    // workflow that edits dist output without rebuilding the C++ binary
    // still works.
    if (const EmbeddedAsset* asset = find_embedded_web_asset(path)) {
        std::string content(reinterpret_cast<const char*>(asset->data), asset->size);
        return Response(200, content, get_mime_type(path));
    }

    std::string full_path = "web/dist" + path;

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

HTTPServer::Response HTTPServer::handle_audit_recent(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\":\"Unauthorized\"}");
    }
    if (token->role != Role::ADMIN) {
        g_auth_manager->log_auth_decision(token->user_id, "AUDIT_VIEW_DENY", false, "Not ADMIN");
        return Response(403, "{\"error\":\"Only ADMIN may view the audit trail\"}");
    }

    int limit = 100;
    auto it = req.query_params.find("limit");
    if (it != req.query_params.end()) {
        try { limit = std::max(1, std::min(1000, std::stoi(it->second))); } catch (...) {}
    }

    // audit_log.ndjson is the hash-chained, append-only trail written by
    // ACMK::DefaultEnvironmentIO (src/kernel/state_plane.cpp). Read the
    // whole file and keep only the tail -- it's small enough in practice
    // that this is simpler and safer than seeking from the end by bytes
    // (which risks starting mid-line).
    std::ifstream in("audit_log.ndjson");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }

    size_t start = lines.size() > static_cast<size_t>(limit) ? lines.size() - static_cast<size_t>(limit) : 0;

    std::ostringstream body;
    body << "{\"status\":\"success\",\"count\":" << (lines.size() - start) << ",\"entries\":[";
    for (size_t i = start; i < lines.size(); ++i) {
        if (i > start) body << ",";
        body << lines[i];
    }
    body << "]}";

    g_auth_manager->log_auth_decision(token->user_id, "AUDIT_VIEW", true);
    return Response(200, body.str(), "application/json");
}

ACMK::ClinicalRole HTTPServer::role_to_clinical_role(Role role) {
    // The system Role enum (auth_manager.h) is coarser than ACMK::ClinicalRole
    // (rbac_fhir.h) -- map conservatively. ADMIN/IT never get direct clinical
    // read/write scopes; they land on audit-facing roles instead.
    switch (role) {
        case Role::RN: return ACMK::ClinicalRole::BEDSIDE_NURSE;
        case Role::CHARGE_NURSE: return ACMK::ClinicalRole::CHARGE_NURSE;
        case Role::PROVIDER: return ACMK::ClinicalRole::ATTENDING_PROVIDER;
        case Role::ADMIN: return ACMK::ClinicalRole::CLINICAL_INFORMATICS;
        case Role::IT: return ACMK::ClinicalRole::IT_SECURITY;
        case Role::INSTRUCTOR: return ACMK::ClinicalRole::EDUCATION_SIMULATION;
        default: return ACMK::ClinicalRole::BEDSIDE_NURSE;
    }
}

bool HTTPServer::ensure_fhir_token() {
    if (fhir_provider_->validate_token(fhir_service_token_)) {
        return true;
    }
    fhir_service_token_ = fhir_provider_->authorize(fhir_launch_context_);
    return !fhir_service_token_.access_token.empty();
}

HTTPServer::Response HTTPServer::handle_fhir_read(const Request& req, const std::string& resource_type) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) {
        return Response(401, "{\"error\":\"Unauthorized\"}");
    }
    if (fhir_base_url_.empty()) {
        return Response(503, "{\"error\":\"FHIR_BASE_URL not configured on this server\"}");
    }
    auto it = req.query_params.find("id");
    if (it == req.query_params.end() || it->second.empty()) {
        return Response(400, "{\"error\":\"id query parameter required\"}");
    }
    if (!ensure_fhir_token()) {
        return Response(502, "{\"error\":\"Failed to obtain FHIR access token\"}");
    }

    try {
        ACMK::ACMKRoleBasedClient client(
            role_to_clinical_role(token->role),
            std::make_unique<FHIR::DefaultFHIRResourceClient>(fhir_base_url_),
            std::make_unique<ACMK::RoleBasedAccessControl>());

        json result;
        if (resource_type == "patient") {
            result = client.read_patient(it->second, fhir_service_token_);
        } else if (resource_type == "observation") {
            result = client.read_observation(it->second, fhir_service_token_);
        } else {
            return Response(400, "{\"error\":\"Unsupported resource type\"}");
        }
        return Response(200, result.dump(), "application/json");
    } catch (const std::exception& e) {
        g_auth_manager->log_auth_decision(token->user_id, "FHIR_READ_DENY_" + resource_type, false, e.what());
        return Response(403, "{\"error\":\"" + json_escape(e.what()) + "\"}");
    }
}

HTTPServer::Response HTTPServer::handle_fhir_create_flag(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\":\"Unauthorized\"}");
    if (fhir_base_url_.empty()) return Response(503, "{\"error\":\"FHIR_BASE_URL not configured on this server\"}");
    if (!ensure_fhir_token()) return Response(502, "{\"error\":\"Failed to obtain FHIR access token\"}");

    try {
        json body = json::parse(req.body);
        FHIR::FHIRFlag flag;
        flag.patient_id = body.at("patient_id").as_string();
        flag.status = body.contains("status") ? body.at("status").as_string() : "active";
        flag.category = body.contains("category") ? body.at("category").as_string() : "clinical";
        flag.code = body.contains("code") ? body.at("code").as_string() : "";

        ACMK::ACMKRoleBasedClient client(
            role_to_clinical_role(token->role),
            std::make_unique<FHIR::DefaultFHIRResourceClient>(fhir_base_url_),
            std::make_unique<ACMK::RoleBasedAccessControl>());
        json result = client.create_flag(flag, fhir_service_token_);
        return Response(200, result.dump(), "application/json");
    } catch (const std::exception& e) {
        g_auth_manager->log_auth_decision(token->user_id, "FHIR_CREATE_FLAG_DENY", false, e.what());
        return Response(403, "{\"error\":\"" + json_escape(e.what()) + "\"}");
    }
}

HTTPServer::Response HTTPServer::handle_fhir_create_document_reference(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\":\"Unauthorized\"}");
    if (fhir_base_url_.empty()) return Response(503, "{\"error\":\"FHIR_BASE_URL not configured on this server\"}");
    if (!ensure_fhir_token()) return Response(502, "{\"error\":\"Failed to obtain FHIR access token\"}");

    try {
        json body = json::parse(req.body);
        FHIR::FHIRDocumentReference doc_ref;
        doc_ref.patient_id = body.at("patient_id").as_string();
        doc_ref.type = body.contains("type") ? body.at("type").as_string() : "clinical-note";
        doc_ref.status = body.contains("status") ? body.at("status").as_string() : "current";
        doc_ref.docstatus = body.contains("docstatus") ? body.at("docstatus").as_string() : "preliminary";
        doc_ref.author_practitioner_id = token->user_id;
        doc_ref.content_attachment_url = body.contains("content_url") ? body.at("content_url").as_string() : "";
        doc_ref.content_attachment_title = body.contains("title") ? body.at("title").as_string() : "";
        doc_ref.date = std::chrono::system_clock::now();

        ACMK::ACMKRoleBasedClient client(
            role_to_clinical_role(token->role),
            std::make_unique<FHIR::DefaultFHIRResourceClient>(fhir_base_url_),
            std::make_unique<ACMK::RoleBasedAccessControl>());
        json result = client.create_document_reference(doc_ref, fhir_service_token_);
        return Response(200, result.dump(), "application/json");
    } catch (const std::exception& e) {
        g_auth_manager->log_auth_decision(token->user_id, "FHIR_CREATE_DOCREF_DENY", false, e.what());
        return Response(403, "{\"error\":\"" + json_escape(e.what()) + "\"}");
    }
}

HTTPServer::Response HTTPServer::handle_fhir_create_provenance(const Request& req) {
    AuthToken* token = g_auth_manager->get_token(req.auth_token);
    if (!token) return Response(401, "{\"error\":\"Unauthorized\"}");
    if (fhir_base_url_.empty()) return Response(503, "{\"error\":\"FHIR_BASE_URL not configured on this server\"}");
    if (!ensure_fhir_token()) return Response(502, "{\"error\":\"Failed to obtain FHIR access token\"}");

    try {
        json body = json::parse(req.body);
        FHIR::FHIRProvenance prov;
        // agent identity comes from the verified token, not the request body
        prov.agent_who = token->user_id;
        prov.agent_role = role_to_acmk_role_string(token->role);
        prov.entity_what = body.contains("entity_what") ? body.at("entity_what").as_string() : "";
        prov.version_info = body.contains("version_info") ? body.at("version_info").as_string() : "";
        prov.recorded = std::chrono::system_clock::now();

        ACMK::ACMKRoleBasedClient client(
            role_to_clinical_role(token->role),
            std::make_unique<FHIR::DefaultFHIRResourceClient>(fhir_base_url_),
            std::make_unique<ACMK::RoleBasedAccessControl>());
        json result = client.create_provenance(prov, fhir_service_token_);
        return Response(200, result.dump(), "application/json");
    } catch (const std::exception& e) {
        g_auth_manager->log_auth_decision(token->user_id, "FHIR_CREATE_PROVENANCE_DENY", false, e.what());
        return Response(403, "{\"error\":\"" + json_escape(e.what()) + "\"}");
    }
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
