#pragma once

#include "types.h"
#include "inference_engine.h"
#include "clinical_sim.h"
#include "clinical_analyzer.h"
#include "training_scenario.h"
#include "training_analytics.h"
#include "acmk_api_handler.h"
#include "acmk_planes.h"
#include "fhir_client.h"
#include "rbac_fhir.h"
#include "auth_manager.h"
#include "cohort_manager.h"
#include <memory>
#include <string>
#include <thread>
#include <map>
#include <set>
#include <mutex>

// Real, persisted chart entries -- "Add Note" in the UI (PatientDetail.ts)
// previously only ever touched browser-local state (store.addChartEntry),
// nothing server-side, so a refresh silently lost every note a nurse wrote.
struct StoredChartEntry {
    std::string entry_id;
    int patient_id;
    std::string entry_type; // "note" | "intervention" | "observation"
    std::string content;
    std::string nurse_id;
    std::string nurse_name;
    std::time_t timestamp;
};

class HTTPServer {
public:
    struct Request {
        std::string method;
        std::string path;
        std::string body;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> query_params;
        std::string auth_token;
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
    
    std::shared_ptr<ACMK::ACMKPlanesCoordinator> acmk_coordinator;
    std::shared_ptr<ACMKAPIHandler> acmk_handler;

    // FHIR integration: base URL / OAuth2 token endpoint come from
    // FHIR_BASE_URL / FHIR_TOKEN_ENDPOINT env vars, unset by default (routes
    // return a clear "not configured" error rather than silently no-op'ing).
    std::string fhir_base_url_;
    std::unique_ptr<FHIR::SMARTOnFHIRProvider> fhir_provider_;
    FHIR::OAuth2Token fhir_service_token_;
    FHIR::SMARTLaunchContext fhir_launch_context_;

    bool ensure_fhir_token();
    ACMK::ClinicalRole role_to_clinical_role(Role role);
    Response handle_audit_recent(const Request& req);
    Response handle_fhir_read(const Request& req, const std::string& resource_type);
    Response handle_fhir_create_flag(const Request& req);
    Response handle_fhir_create_document_reference(const Request& req);
    Response handle_fhir_create_provenance(const Request& req);
    
    // Training mode state -- keyed by the authenticated nurse's user_id, not
    // a single server-wide slot. It used to be one bare active_scenario_
    // member shared by every connection: if a second nurse called
    // POST /api/training/start while a first nurse's scenario was still
    // running, active_scenario_ = std::make_unique<...>(...) would silently
    // destroy the first nurse's in-progress ScenarioRuntime and every
    // subsequent action/tick/status call from either of them would operate
    // on the second nurse's scenario. Fine for one person testing solo;
    // guaranteed data corruption the moment two people train at once, which
    // is the entire point of a classroom/sim-lab deployment.
    struct TrainingUserSession {
        std::unique_ptr<ScenarioRuntime> runtime;
        std::string training_session_id;
        std::string scenario_id;
        std::string scenario_title;
        // Captured from ScenarioDefinition::expected_actions at start time
        // so handle_training_end can report "N of M expected interventions
        // performed" after the ScenarioRuntime (and its private
        // ScenarioDefinition copy) is torn down.
        std::vector<std::string> expected_action_names;
        // ScenarioRuntime::check_failure_conditions() re-evaluates and
        // returns every currently-true failure on each call, with no
        // internal "already reported" memory -- a failure that stays true
        // across several ticks (the common case) would otherwise get
        // logged once per tick instead of once, inflating the debrief's
        // missed-critical-window count.
        std::set<std::string> failures_already_recorded;
        // ScenarioRuntime doesn't track a cumulative score itself; the
        // frontend (TrainingMode.ts) reads it every render, so someone has to.
        float score = 0.0f;
        int actions_taken = 0;
    };
    std::map<std::string, TrainingUserSession> training_sessions_by_user_;
    std::mutex training_mutex_;
    std::unique_ptr<TrainingAnalyticsStore> analytics_store_;

    // In-memory for the server's lifetime (also appended to
    // chart_log.ndjson for durability across restarts -- see
    // handle_chart). Every request runs on its own detached thread (see
    // start_server()), so this needs its own lock; nothing else in this
    // class currently protects its shared state the same way, which is a
    // pre-existing gap this doesn't attempt to fix more broadly.
    std::map<int, std::vector<StoredChartEntry>> chart_entries_;
    std::mutex chart_mutex_;
    int next_chart_entry_id_ = 1;
    
    void register_routes();
    void start_server();
    void handle_client(int client_socket);
    
    Request parse_http_request(const std::string& raw_request);
    std::string build_http_response(const Response& response);
    
    Response handle_infer(const Request& req);
    Response handle_enhanced_infer(const Request& req);
    Response handle_status(const Request& req);
    Response handle_patients(const Request& req);
    Response handle_patient_admit(const Request& req);
    Response handle_patient_discharge(const Request& req);
    Response handle_patient_assign(const Request& req);
    Response handle_alert_override(const Request& req);
    Response handle_staff_list(const Request& req);
    Response handle_staff_create(const Request& req);
    Response handle_instructor_create_cohort(const Request& req);
    Response handle_instructor_list_cohorts(const Request& req);
    Response handle_instructor_import_roster(const Request& req);
    Response handle_instructor_cohort_dashboard(const Request& req);
    Response handle_instructor_remove_student(const Request& req);

    // Shared by handle_training_end (live, right after a session finishes)
    // and handle_training_report (reconstructed later from persisted event
    // data, for a nurse revisiting their own history or an instructor/admin
    // reviewing it) -- same rich shape (transcript, learning_summary,
    // improvement_areas) either way, not a bare-metrics summary for one of
    // the two callers.
    std::string build_training_report_json(
        const std::vector<TrainingEvent>& events,
        const std::string& training_session_id,
        const std::string& scenario_title,
        const std::string& outcome,
        int duration_seconds,
        float final_score_0_to_1,
        const std::vector<std::string>& expected_action_names);
    Response handle_learn(const Request& req);
    Response handle_health(const Request& req);
    Response handle_observations(const Request& req);
    Response handle_scaffold(const Request& req);
    Response handle_action(const Request& req);
    Response handle_chart(const Request& req);
    Response handle_sign_in(const Request& req);
    
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
