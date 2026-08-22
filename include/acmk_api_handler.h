#pragma once

#include "acmk_planes.h"
#include <memory>
#include <string>
#include <map>
#include <sstream>
#include <vector>

struct ACMKRequest {
  std::string method;
  std::string path;
  std::string body;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> query_params;

  // Populated by the HTTP layer after verifying the bearer token. Handlers
  // must use these (not client-supplied body fields) as the identity/role
  // for anything security- or audit-relevant, since the request body is
  // attacker-controlled.
  std::string authenticated_user_id;
  std::string authenticated_role;
};

struct ACMKResponse {
  int status_code;
  std::string body;
  std::string content_type;
  std::map<std::string, std::string> headers;
  
  ACMKResponse(int code = 200, const std::string& b = "", const std::string& type = "application/json")
      : status_code(code), body(b), content_type(type) {}
};

class ACMKAPIHandler {
public:
  using Request = ACMKRequest;
  using Response = ACMKResponse;

  ACMKAPIHandler(std::shared_ptr<ACMK::ACMKPlanesCoordinator> coordinator);
  
  Response handle_state_emit(const Request& req);
  Response handle_state_get(const Request& req);
  Response handle_state_history(const Request& req);
  
  Response handle_control_pause(const Request& req);
  Response handle_control_resume(const Request& req);
  Response handle_control_replay(const Request& req);
  Response handle_control_freeze(const Request& req);
  Response handle_control_recompute(const Request& req);
  
  Response handle_trace_get_graph(const Request& req);
  Response handle_trace_get_artifacts(const Request& req);
  Response handle_trace_snapshot(const Request& req);
  Response handle_trace_snapshots(const Request& req);
  
  Response handle_inference_readonly_trace(const Request& req);
  Response handle_inference_decision_envelope(const Request& req);
  Response handle_inference_get_decision(const Request& req);
  Response handle_inference_error(const Request& req);
  
  Response handle_environment_human_event(const Request& req);
  Response handle_environment_provenance(const Request& req);
  Response handle_environment_human_events(const Request& req);
  
  Response handle_session_init(const Request& req);
  
private:
  std::shared_ptr<ACMK::ACMKPlanesCoordinator> coordinator;
  
  std::string serialize_state_frame(const ACMK::StateFrame& frame) const;
  std::string serialize_inference_node(const ACMK::InferenceNode& node) const;
  std::string serialize_perceptual_artifact(const ACMK::PerceptualArtifact& artifact) const;
  std::string serialize_decision_envelope(const ACMK::DecisionEnvelope& envelope) const;
  std::string serialize_human_event(const ACMK::HumanInterventionEvent& event) const;
  
  std::string json_escape(const std::string& str) const;
  std::string parse_json_string(const std::string& json_str, const std::string& key) const;
  std::string parse_json_number(const std::string& json_str, const std::string& key) const;
  std::vector<std::string> parse_json_array(const std::string& json_str, const std::string& key) const;
  
  std::string get_query_param(const Request& req, const std::string& key) const;
  std::string get_header(const Request& req, const std::string& key) const;

  // Runs a temporal-control request (pause/resume/replay/freeze/recompute)
  // through the coordinator's TemporalControlEngine plus a real-world role
  // check. Returns .allowed=false with a deny_reason if the action should
  // be rejected.
  ACMK::TemporalControlResponse validate_temporal_control(
      const Request& req, const std::string& session_id, ACMK::TemporalAction action, const std::string& reason);
};
