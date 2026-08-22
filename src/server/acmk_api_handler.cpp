#include "acmk_api_handler.h"
#include "crypto_utils.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdlib>

ACMKAPIHandler::ACMKAPIHandler(std::shared_ptr<ACMK::ACMKPlanesCoordinator> coordinator)
    : coordinator(coordinator) {}

std::string ACMKAPIHandler::json_escape(const std::string& str) const {
  std::string result;
  for (char c : str) {
    switch (c) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\b': result += "\\b"; break;
      case '\f': result += "\\f"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default:
        if (c < 32) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          result += buf;
        } else {
          result += c;
        }
    }
  }
  return result;
}

std::string ACMKAPIHandler::parse_json_string(const std::string& json_str, const std::string& key) const {
  std::string search_key = "\"" + key + "\"";
  size_t pos = json_str.find(search_key);
  if (pos == std::string::npos) return "";
  
  pos = json_str.find(":", pos);
  if (pos == std::string::npos) return "";
  
  pos = json_str.find("\"", pos);
  if (pos == std::string::npos) return "";
  pos++;
  
  size_t end_pos = json_str.find("\"", pos);
  if (end_pos == std::string::npos) return "";
  
  return json_str.substr(pos, end_pos - pos);
}

std::string ACMKAPIHandler::parse_json_number(const std::string& json_str, const std::string& key) const {
  std::string search_key = "\"" + key + "\"";
  size_t pos = json_str.find(search_key);
  if (pos == std::string::npos) return "0";
  
  pos = json_str.find(":", pos);
  if (pos == std::string::npos) return "0";
  pos++;
  
  while (pos < json_str.length() && (json_str[pos] == ' ' || json_str[pos] == '\t')) pos++;
  
  size_t end_pos = pos;
  while (end_pos < json_str.length() && 
         (isdigit(json_str[end_pos]) || json_str[end_pos] == '.' || json_str[end_pos] == '-')) {
    end_pos++;
  }
  
  if (end_pos == pos) return "0";
  return json_str.substr(pos, end_pos - pos);
}

std::vector<std::string> ACMKAPIHandler::parse_json_array(const std::string& json_str, const std::string& key) const {
  std::vector<std::string> result;
  std::string search_key = "\"" + key + "\"";
  size_t pos = json_str.find(search_key);
  if (pos == std::string::npos) return result;
  
  pos = json_str.find("[", pos);
  if (pos == std::string::npos) return result;
  pos++;
  
  size_t end_pos = json_str.find("]", pos);
  if (end_pos == std::string::npos) return result;
  
  std::string array_content = json_str.substr(pos, end_pos - pos);
  
  size_t curr = 0;
  while (curr < array_content.length()) {
    size_t quote_start = array_content.find("\"", curr);
    if (quote_start == std::string::npos) break;
    quote_start++;
    
    size_t quote_end = array_content.find("\"", quote_start);
    if (quote_end == std::string::npos) break;
    
    result.push_back(array_content.substr(quote_start, quote_end - quote_start));
    curr = quote_end + 1;
  }
  
  return result;
}

std::string ACMKAPIHandler::get_query_param(const Request& req, const std::string& key) const {
  auto it = req.query_params.find(key);
  if (it != req.query_params.end()) {
    return it->second;
  }
  return "";
}

std::string ACMKAPIHandler::get_header(const Request& req, const std::string& key) const {
  auto it = req.headers.find(key);
  if (it != req.headers.end()) {
    return it->second;
  }
  return "";
}

std::string ACMKAPIHandler::serialize_state_frame(const ACMK::StateFrame& frame) const {
  std::ostringstream ss;
  ss << "{\"session_id\":\"" << json_escape(frame.session_id) << "\",";
  ss << "\"timestamp\":" << std::chrono::system_clock::to_time_t(frame.timestamp) << ",";
  
  switch (frame.cognitive_state) {
    case ACMK::CognitiveState::IDLE: ss << "\"cognitive_state\":\"IDLE\","; break;
    case ACMK::CognitiveState::INGESTING: ss << "\"cognitive_state\":\"INGESTING\","; break;
    case ACMK::CognitiveState::RESOLVING: ss << "\"cognitive_state\":\"RESOLVING\","; break;
    case ACMK::CognitiveState::CONVERGING: ss << "\"cognitive_state\":\"CONVERGING\","; break;
    case ACMK::CognitiveState::CONVERGED: ss << "\"cognitive_state\":\"CONVERGED\","; break;
  }
  
  switch (frame.risk_posture) {
    case ACMK::RiskPosture::LOW: ss << "\"risk_posture\":\"LOW\","; break;
    case ACMK::RiskPosture::MODERATE: ss << "\"risk_posture\":\"MODERATE\","; break;
    case ACMK::RiskPosture::ELEVATED: ss << "\"risk_posture\":\"ELEVATED\","; break;
    case ACMK::RiskPosture::CRITICAL: ss << "\"risk_posture\":\"CRITICAL\","; break;
  }
  
  ss << "\"confidence_global\":" << frame.confidence_global << ",";
  ss << "\"patient_id\":\"" << json_escape(frame.patient_id) << "\",";
  ss << "\"input_modalities\":[";
  for (size_t i = 0; i < frame.input_modalities.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << json_escape(frame.input_modalities[i]) << "\"";
  }
  ss << "]}";
  return ss.str();
}

std::string ACMKAPIHandler::serialize_inference_node(const ACMK::InferenceNode& node) const {
  std::ostringstream ss;
  ss << "{\"node_id\":\"" << json_escape(node.node_id) << "\",";
  ss << "\"status\":\"" << json_escape(node.status) << "\",";
  ss << "\"confidence\":" << node.confidence << ",";
  ss << "\"suppression_reason\":\"" << json_escape(node.suppression_reason) << "\",";
  ss << "\"parent_nodes\":[";
  for (size_t i = 0; i < node.parent_nodes.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << json_escape(node.parent_nodes[i]) << "\"";
  }
  ss << "],";
  ss << "\"time_range\":{\"from\":" << node.time_range.first << ",\"to\":" << node.time_range.second << "}}";
  return ss.str();
}

std::string ACMKAPIHandler::serialize_perceptual_artifact(const ACMK::PerceptualArtifact& artifact) const {
  std::ostringstream ss;
  ss << "{\"artifact_id\":\"" << json_escape(artifact.artifact_id) << "\",";
  ss << "\"artifact_type\":\"" << json_escape(artifact.artifact_type) << "\",";
  ss << "\"content_hash\":\"" << json_escape(artifact.content_hash) << "\",";
  ss << "\"timestamp\":" << std::chrono::system_clock::to_time_t(artifact.timestamp) << ",";
  ss << "\"confidence\":" << artifact.confidence << ",";
  ss << "\"alignment_metadata\":[";
  for (size_t i = 0; i < artifact.alignment_metadata.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << json_escape(artifact.alignment_metadata[i]) << "\"";
  }
  ss << "]}";
  return ss.str();
}

std::string ACMKAPIHandler::serialize_decision_envelope(const ACMK::DecisionEnvelope& envelope) const {
  std::ostringstream ss;
  ss << "{\"final_state\":\"" << json_escape(envelope.final_state) << "\",";
  ss << "\"dominant_constraints\":[";
  for (size_t i = 0; i < envelope.dominant_constraints.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "\"" << json_escape(envelope.dominant_constraints[i]) << "\"";
  }
  ss << "],\"rejected_alternatives\":[";
  for (size_t i = 0; i < envelope.rejected_alternatives.size(); ++i) {
    if (i > 0) ss << ",";
    ss << "{\"id\":\"" << json_escape(envelope.rejected_alternatives[i].first)
       << "\",\"reason\":\"" << json_escape(envelope.rejected_alternatives[i].second) << "\"}";
  }
  ss << "],\"confidence_bounds\":{\"low\":" << envelope.confidence_bounds.first
     << ",\"high\":" << envelope.confidence_bounds.second << "}}";
  return ss.str();
}

std::string ACMKAPIHandler::serialize_human_event(const ACMK::HumanInterventionEvent& event) const {
  std::ostringstream ss;
  ss << "{\"event_type\":\"" << json_escape(event.event_type) << "\",";
  ss << "\"user_id\":\"" << json_escape(event.user_id) << "\",";
  ss << "\"session_id\":\"" << json_escape(event.session_id) << "\",";
  ss << "\"timestamp\":" << std::chrono::system_clock::to_time_t(event.timestamp) << ",";
  ss << "\"scope\":\"" << json_escape(event.scope) << "\",";
  ss << "\"content\":\"" << json_escape(event.content) << "\"}";
  return ss.str();
}

// ============================================================================
// STATE PLANE HANDLERS
// ============================================================================

ACMKAPIHandler::Response ACMKAPIHandler::handle_state_emit(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string patient_id = parse_json_string(req.body, "patient_id");
    std::string state_str = parse_json_string(req.body, "cognitive_state");
    std::string risk_str = parse_json_string(req.body, "risk_posture");
    std::string confidence_str = parse_json_number(req.body, "confidence_global");
    
    ACMK::StateFrame frame;
    frame.session_id = session_id;
    frame.patient_id = patient_id;
    frame.confidence_global = std::stod(confidence_str);
    
    if (state_str == "INGESTING") frame.cognitive_state = ACMK::CognitiveState::INGESTING;
    else if (state_str == "RESOLVING") frame.cognitive_state = ACMK::CognitiveState::RESOLVING;
    else if (state_str == "CONVERGING") frame.cognitive_state = ACMK::CognitiveState::CONVERGING;
    else if (state_str == "CONVERGED") frame.cognitive_state = ACMK::CognitiveState::CONVERGED;
    else frame.cognitive_state = ACMK::CognitiveState::IDLE;
    
    if (risk_str == "MODERATE") frame.risk_posture = ACMK::RiskPosture::MODERATE;
    else if (risk_str == "ELEVATED") frame.risk_posture = ACMK::RiskPosture::ELEVATED;
    else if (risk_str == "CRITICAL") frame.risk_posture = ACMK::RiskPosture::CRITICAL;
    else frame.risk_posture = ACMK::RiskPosture::LOW;
    
    frame.input_modalities = parse_json_array(req.body, "input_modalities");
    frame.timestamp = std::chrono::system_clock::now();
    
    if (!coordinator->get_state_plane()) {
      return Response(500, "{\"error\":\"State plane not initialized\"}", "application/json");
    }
    
    coordinator->get_state_plane()->emit_state(frame);
    
    std::ostringstream response;
    response << "{\"status\":\"state_emitted\",\"frame\":" << serialize_state_frame(frame) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to emit state\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_state_get(const Request& req) {
  try {
    if (!coordinator->get_state_plane()) {
      return Response(500, "{\"error\":\"State plane not initialized\"}", "application/json");
    }
    
    ACMK::StateFrame current = coordinator->get_state_plane()->get_current_state();
    
    std::ostringstream response;
    response << "{\"status\":\"success\",\"frame\":" << serialize_state_frame(current) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get state\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_state_history(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }

    if (!coordinator->get_state_plane()) {
      return Response(500, "{\"error\":\"State plane not initialized\"}", "application/json");
    }

    // Full time range -- from/to filtering by timestamp is exposed on
    // StatePlane::get_state_history for future use, but this route only
    // takes a session_id today.
    auto from = ACMK::Timestamp::min();
    auto to = ACMK::Timestamp::max();
    auto frames = coordinator->get_state_plane()->get_state_history(session_id, from, to);

    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",\"frames\":[";
    for (size_t i = 0; i < frames.size(); ++i) {
      if (i > 0) response << ",";
      response << serialize_state_frame(frames[i]);
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get state history\"}", "application/json");
  }
}

// ============================================================================
// CONTROL PLANE HANDLERS
// ============================================================================

ACMK::TemporalControlResponse ACMKAPIHandler::validate_temporal_control(
    const Request& req, const std::string& session_id, ACMK::TemporalAction action, const std::string& reason) {
  ACMK::TemporalControlResponse resp;
  auto* engine = coordinator->get_temporal_control_engine();
  if (!engine) {
    resp.allowed = true; // no engine configured: don't block on a missing optional component
    return resp;
  }

  ACMK::TemporalControlRequest request;
  request.session_id = session_id;
  request.action = action;
  request.reason = reason;
  request.user_id = req.authenticated_user_id;

  resp = engine->request_control(request);

  // Real-world sessions additionally require an elevated role -- simulation
  // sessions only need a stated reason (checked above by the engine).
  if (resp.allowed && !coordinator->is_simulation_mode(session_id)) {
    static const char* elevated_roles[] = {"provider", "admin", "charge_nurse"};
    bool ok = false;
    for (const char* r : elevated_roles) {
      if (req.authenticated_role == r) { ok = true; break; }
    }
    if (!ok) {
      resp.allowed = false;
      resp.deny_reason = "Real-world session control requires an elevated role";
    }
  }

  // Persist to the hash-chained audit log regardless of outcome -- denied
  // attempts are as relevant to a compliance review as allowed ones.
  if (coordinator->get_environment_io()) {
    static const char* action_names[] = {"STEP", "PAUSE", "RESUME", "ROLLBACK", "REPLAY", "FREEZE", "RECOMPUTE"};
    ACMK::HumanInterventionEvent event;
    event.event_type = "CONTROL_" + std::string(action_names[static_cast<int>(action)]) +
                        (resp.allowed ? "_ALLOWED" : "_DENIED");
    event.user_id = req.authenticated_user_id;
    event.session_id = session_id;
    event.scope = "control_action";
    event.content = resp.allowed ? reason : resp.deny_reason;
    event.timestamp = std::chrono::system_clock::now();
    coordinator->get_environment_io()->record_human_event(event);
  }

  return resp;
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_control_pause(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string reason = parse_json_string(req.body, "reason");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    auto validation = validate_temporal_control(req, session_id, ACMK::TemporalAction::PAUSE, reason);
    if (!validation.allowed) {
      return Response(403, "{\"error\":\"" + json_escape(validation.deny_reason) + "\"}", "application/json");
    }

    if (!coordinator->get_control_plane()) {
      return Response(500, "{\"error\":\"Control plane not initialized\"}", "application/json");
    }

    bool success = coordinator->get_control_plane()->request_pause(session_id);

    std::ostringstream response;
    response << "{\"status\":\"" << (success ? "paused" : "pause_failed") << "\",";
    response << "\"session_id\":\"" << json_escape(session_id) << "\"}";
    return Response(success ? 200 : 400, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to pause\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_control_resume(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string reason = parse_json_string(req.body, "reason");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    auto validation = validate_temporal_control(req, session_id, ACMK::TemporalAction::RESUME, reason);
    if (!validation.allowed) {
      return Response(403, "{\"error\":\"" + json_escape(validation.deny_reason) + "\"}", "application/json");
    }

    if (!coordinator->get_control_plane()) {
      return Response(500, "{\"error\":\"Control plane not initialized\"}", "application/json");
    }

    bool success = coordinator->get_control_plane()->request_resume(session_id);
    
    std::ostringstream response;
    response << "{\"status\":\"" << (success ? "resumed" : "resume_failed") << "\",";
    response << "\"session_id\":\"" << json_escape(session_id) << "\"}";
    return Response(success ? 200 : 400, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to resume\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_control_replay(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string from_str = parse_json_number(req.body, "from_timestamp");
    std::string speed_str = parse_json_number(req.body, "speed");
    std::string reason = parse_json_string(req.body, "reason");
    
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    auto validation = validate_temporal_control(req, session_id, ACMK::TemporalAction::REPLAY, reason);
    if (!validation.allowed) {
      return Response(403, "{\"error\":\"" + json_escape(validation.deny_reason) + "\"}", "application/json");
    }

    if (!coordinator->get_control_plane()) {
      return Response(500, "{\"error\":\"Control plane not initialized\"}", "application/json");
    }

    long from_ts = std::stol(from_str.empty() ? "0" : from_str);
    double speed = std::stod(speed_str.empty() ? "1.0" : speed_str);

    bool success = coordinator->get_control_plane()->request_replay(session_id, from_ts, speed, reason);
    
    std::ostringstream response;
    response << "{\"status\":\"" << (success ? "replay_started" : "replay_failed") << "\",";
    response << "\"session_id\":\"" << json_escape(session_id) << "\",\"speed\":" << speed << "}";
    return Response(success ? 200 : 400, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to replay\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_control_freeze(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string reason = parse_json_string(req.body, "reason");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    auto validation = validate_temporal_control(req, session_id, ACMK::TemporalAction::FREEZE, reason);
    if (!validation.allowed) {
      return Response(403, "{\"error\":\"" + json_escape(validation.deny_reason) + "\"}", "application/json");
    }

    if (!coordinator->get_control_plane()) {
      return Response(500, "{\"error\":\"Control plane not initialized\"}", "application/json");
    }

    bool success = coordinator->get_control_plane()->request_freeze(session_id);
    
    std::ostringstream response;
    response << "{\"status\":\"" << (success ? "frozen" : "freeze_failed") << "\",";
    response << "\"session_id\":\"" << json_escape(session_id) << "\"}";
    return Response(success ? 200 : 400, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to freeze\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_control_recompute(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string scope = parse_json_string(req.body, "scope");
    std::string reason = parse_json_string(req.body, "reason");

    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    if (scope.empty()) scope = "all";

    auto validation = validate_temporal_control(req, session_id, ACMK::TemporalAction::RECOMPUTE, reason);
    if (!validation.allowed) {
      return Response(403, "{\"error\":\"" + json_escape(validation.deny_reason) + "\"}", "application/json");
    }

    if (!coordinator->get_control_plane()) {
      return Response(500, "{\"error\":\"Control plane not initialized\"}", "application/json");
    }

    bool success = coordinator->get_control_plane()->request_recompute(session_id, scope);
    
    std::ostringstream response;
    response << "{\"status\":\"" << (success ? "recompute_started" : "recompute_failed") << "\",";
    response << "\"session_id\":\"" << json_escape(session_id) << "\",\"scope\":\"" << json_escape(scope) << "\"}";
    return Response(success ? 200 : 400, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to recompute\"}", "application/json");
  }
}

// ============================================================================
// TRACE PLANE HANDLERS
// ============================================================================

ACMKAPIHandler::Response ACMKAPIHandler::handle_trace_get_graph(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }
    
    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }
    
    auto nodes = coordinator->get_trace_plane()->get_inference_graph(session_id);
    
    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
      if (i > 0) response << ",";
      response << serialize_inference_node(nodes[i]);
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get inference graph\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_trace_get_artifacts(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }
    
    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }
    
    auto artifacts = coordinator->get_trace_plane()->get_perceptual_artifacts(session_id);
    
    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",\"artifacts\":[";
    for (size_t i = 0; i < artifacts.size(); ++i) {
      if (i > 0) response << ",";
      response << serialize_perceptual_artifact(artifacts[i]);
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get artifacts\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_trace_snapshot(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }
    
    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }
    
    // Optional caller-supplied label folded into the hash so the snapshot
    // fingerprint reflects what was actually being snapshotted when given;
    // falls back to session_id+timestamp (still a real fingerprint of this
    // specific snapshot event, just a less semantically rich one) when not.
    std::string label = parse_json_string(req.body, "label");
    auto now = std::chrono::system_clock::now();
    std::string state_hash = Crypto::sha256_hex(
        session_id + "|" + label + "|" + std::to_string(std::chrono::system_clock::to_time_t(now)));
    std::string snapshot_id = coordinator->get_trace_plane()->create_snapshot(session_id, now, state_hash);

    std::ostringstream response;
    response << "{\"status\":\"snapshot_created\",\"session_id\":\"" << json_escape(session_id) << "\",";
    response << "\"snapshot_id\":\"" << json_escape(snapshot_id) << "\",";
    response << "\"timestamp\":" << std::chrono::system_clock::to_time_t(now) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to create snapshot\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_trace_snapshots(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }

    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }

    auto snapshots = coordinator->get_trace_plane()->get_snapshots(session_id);

    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",\"snapshots\":[";
    for (size_t i = 0; i < snapshots.size(); ++i) {
      if (i > 0) response << ",";
      response << "{\"snapshot_id\":\"" << json_escape(snapshots[i].snapshot_id) << "\","
               << "\"session_id\":\"" << json_escape(snapshots[i].session_id) << "\","
               << "\"timestamp\":" << std::chrono::system_clock::to_time_t(snapshots[i].timestamp) << ","
               << "\"state_hash\":\"" << json_escape(snapshots[i].state_hash) << "\"}";
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get snapshots\"}", "application/json");
  }
}

// ============================================================================
// INFERENCE CORE PROXY HANDLERS
// ============================================================================

ACMKAPIHandler::Response ACMKAPIHandler::handle_inference_readonly_trace(const Request& req) {
  try {
    std::string node_id = parse_json_string(req.body, "node_id");
    std::string status = parse_json_string(req.body, "status");
    std::string confidence_str = parse_json_number(req.body, "confidence");
    std::string suppression_reason = parse_json_string(req.body, "suppression_reason");
    
    ACMK::InferenceNode node;
    node.node_id = node_id;
    node.status = status.empty() ? "active" : status;
    node.confidence = std::stod(confidence_str.empty() ? "0.5" : confidence_str);
    node.suppression_reason = suppression_reason;
    node.parent_nodes = parse_json_array(req.body, "parent_nodes");
    
    if (!coordinator->get_inference_proxy()) {
      return Response(500, "{\"error\":\"Inference proxy not initialized\"}", "application/json");
    }
    
    coordinator->get_inference_proxy()->emit_readonly_trace(node);
    
    std::ostringstream response;
    response << "{\"status\":\"trace_emitted\",\"node\":" << serialize_inference_node(node) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to emit trace\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_inference_decision_envelope(const Request& req) {
  try {
    std::string session_id = parse_json_string(req.body, "session_id");
    std::string final_state = parse_json_string(req.body, "final_state");
    auto constraints = parse_json_array(req.body, "dominant_constraints");

    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id required\"}", "application/json");
    }

    ACMK::DecisionEnvelope envelope;
    envelope.final_state = final_state;
    envelope.dominant_constraints = constraints;

    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }
    // Previously routed through InferenceCoreProxy::emit_decision_envelope,
    // which was a complete no-op (see state_plane.cpp) -- nothing was ever
    // stored, so a decision envelope could never actually be read back.
    coordinator->get_trace_plane()->record_decision_envelope(session_id, envelope);

    std::ostringstream response;
    response << "{\"status\":\"decision_envelope_emitted\",\"final_state\":\"" << json_escape(final_state) << "\",";
    response << "\"dominant_constraints\":[";
    for (size_t i = 0; i < constraints.size(); ++i) {
      if (i > 0) response << ",";
      response << "\"" << json_escape(constraints[i]) << "\"";
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to emit decision envelope\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_inference_get_decision(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }
    if (!coordinator->get_trace_plane()) {
      return Response(500, "{\"error\":\"Trace plane not initialized\"}", "application/json");
    }

    ACMK::DecisionEnvelope envelope;
    bool found = coordinator->get_trace_plane()->get_decision_envelope(session_id, envelope);
    if (!found) {
      return Response(404, "{\"error\":\"No decision envelope recorded for this session\"}", "application/json");
    }

    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",";
    response << "\"envelope\":" << serialize_decision_envelope(envelope) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get decision envelope\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_inference_error(const Request& req) {
  try {
    std::string error_class_str = parse_json_string(req.body, "error_class");
    std::string scope = parse_json_string(req.body, "scope");
    std::string severity = parse_json_string(req.body, "severity");
    
    ACMK::ErrorClass error_class = ACMK::ErrorClass::PERCEPTUAL_FAILURE;
    if (error_class_str == "TEMPORAL_INCONSISTENCY") error_class = ACMK::ErrorClass::TEMPORAL_INCONSISTENCY;
    else if (error_class_str == "CONSTRAINT_CONFLICT") error_class = ACMK::ErrorClass::CONSTRAINT_CONFLICT;
    else if (error_class_str == "CONFIDENCE_COLLAPSE") error_class = ACMK::ErrorClass::CONFIDENCE_COLLAPSE;
    else if (error_class_str == "HUMAN_INTERFERENCE") error_class = ACMK::ErrorClass::HUMAN_INTERFERENCE;
    
    if (scope.empty()) scope = "system";
    if (severity.empty()) severity = "warning";
    
    if (!coordinator->get_inference_proxy()) {
      return Response(500, "{\"error\":\"Inference proxy not initialized\"}", "application/json");
    }
    
    coordinator->get_inference_proxy()->emit_error(error_class, scope, severity);
    
    std::ostringstream response;
    response << "{\"status\":\"error_emitted\",\"error_class\":\"" << json_escape(error_class_str) << "\",";
    response << "\"scope\":\"" << json_escape(scope) << "\",\"severity\":\"" << json_escape(severity) << "\"}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to emit error\"}", "application/json");
  }
}

// ============================================================================
// ENVIRONMENT/IO HANDLERS
// ============================================================================

ACMKAPIHandler::Response ACMKAPIHandler::handle_environment_human_event(const Request& req) {
  try {
    std::string event_type = parse_json_string(req.body, "event_type");
    std::string scope = parse_json_string(req.body, "scope");
    std::string content = parse_json_string(req.body, "content");
    // session_id was never parsed from the body at all, so every event was
    // recorded with an empty session_id -- handle_environment_human_events'
    // session_id filter could never match anything real.
    std::string session_id = parse_json_string(req.body, "session_id");

    if (event_type.empty() || req.authenticated_user_id.empty()) {
      return Response(400, "{\"error\":\"event_type required and caller must be authenticated\"}", "application/json");
    }

    ACMK::HumanInterventionEvent event;
    event.event_type = event_type;
    // Identity comes from the verified auth token, never the request body --
    // otherwise any caller could attribute audit events to another user.
    event.user_id = req.authenticated_user_id;
    event.session_id = session_id;
    event.scope = scope;
    event.content = content;
    event.timestamp = std::chrono::system_clock::now();
    
    if (!coordinator->get_environment_io()) {
      return Response(500, "{\"error\":\"Environment/IO not initialized\"}", "application/json");
    }
    
    coordinator->get_environment_io()->record_human_event(event);
    
    std::ostringstream response;
    response << "{\"status\":\"human_event_recorded\",\"event\":" << serialize_human_event(event) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to record human event\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_environment_provenance(const Request& req) {
  try {
    std::string entity_what = parse_json_string(req.body, "entity_what");
    std::string version_info = parse_json_string(req.body, "version_info");

    ACMK::Provenance prov;
    // Identity comes from the verified auth token, not the request body.
    prov.agent_who = req.authenticated_user_id;
    prov.agent_role = req.authenticated_role;
    prov.entity_what = entity_what;
    prov.version_info = version_info;
    prov.recorded = std::chrono::system_clock::now();
    
    if (!coordinator->get_environment_io()) {
      return Response(500, "{\"error\":\"Environment/IO not initialized\"}", "application/json");
    }
    
    coordinator->get_environment_io()->record_provenance(prov);
    
    std::ostringstream response;
    response << "{\"status\":\"provenance_recorded\",\"agent\":\"" << json_escape(prov.agent_who) << "\",";
    response << "\"role\":\"" << json_escape(prov.agent_role) << "\",";
    response << "\"timestamp\":" << std::chrono::system_clock::to_time_t(prov.recorded) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to record provenance\"}", "application/json");
  }
}

ACMKAPIHandler::Response ACMKAPIHandler::handle_environment_human_events(const Request& req) {
  try {
    std::string session_id = get_query_param(req, "session_id");
    if (session_id.empty()) {
      return Response(400, "{\"error\":\"session_id query parameter required\"}", "application/json");
    }
    
    if (!coordinator->get_environment_io()) {
      return Response(500, "{\"error\":\"Environment/IO not initialized\"}", "application/json");
    }
    
    auto events = coordinator->get_environment_io()->get_human_events(session_id);
    
    std::ostringstream response;
    response << "{\"status\":\"success\",\"session_id\":\"" << json_escape(session_id) << "\",\"events\":[";
    for (size_t i = 0; i < events.size(); ++i) {
      if (i > 0) response << ",";
      response << serialize_human_event(events[i]);
    }
    response << "]}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to get human events\"}", "application/json");
  }
}

// ============================================================================
// SESSION HANDLERS
// ============================================================================

ACMKAPIHandler::Response ACMKAPIHandler::handle_session_init(const Request& req) {
  try {
    std::string device_fp = parse_json_string(req.body, "device_fingerprint");
    std::string requested_mode = parse_json_string(req.body, "mode");

    if (device_fp.empty()) device_fp = "unknown";
    // Server-verified role only -- never trust a client-supplied user_role,
    // it would let any caller claim to be a provider/admin.
    std::string user_role = req.authenticated_role.empty() ? "rn" : req.authenticated_role;

    // Safe default: sessions are simulation-only unless real_world is both
    // explicitly requested AND the caller is authorized for it. A missing
    // or unrecognized mode never falls back to real_world.
    std::string mode = "simulation";
    if (requested_mode == "real_world") {
      static const char* real_world_roles[] = {"provider", "admin", "charge_nurse"};
      bool role_permitted = false;
      for (const char* r : real_world_roles) {
        if (user_role == r) { role_permitted = true; break; }
      }
      const char* real_world_flag = std::getenv("ACMK_ENABLE_REAL_WORLD");
      bool server_enabled = real_world_flag && std::string(real_world_flag) == "1";

      if (!server_enabled) {
        return Response(403, "{\"error\":\"real_world mode is disabled on this server (set ACMK_ENABLE_REAL_WORLD=1 to enable)\"}", "application/json");
      }
      if (!role_permitted) {
        return Response(403, "{\"error\":\"Insufficient role for real_world mode\"}", "application/json");
      }
      mode = "real_world";
    }

    auto session = coordinator->initialize_session(device_fp, user_role, mode);

    std::ostringstream response;
    response << "{\"status\":\"session_initialized\",\"session_id\":\"" << json_escape(session.session_id) << "\",";
    response << "\"model_version\":\"" << json_escape(session.model_version) << "\",";
    response << "\"rule_set_version\":\"" << json_escape(session.rule_set_version) << "\",";
    response << "\"user_role\":\"" << json_escape(session.user_role) << "\",";
    response << "\"mode\":\"" << json_escape(mode) << "\",";
    response << "\"timestamp\":" << std::chrono::system_clock::to_time_t(session.clock_anchor) << "}";
    return Response(200, response.str(), "application/json");
  } catch (const std::exception& e) {
    return Response(400, "{\"error\":\"Failed to initialize session\"}", "application/json");
  }
}
