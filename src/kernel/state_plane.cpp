#include "acmk_planes.h"
#include "crypto_utils.h"
#include <iostream>
#include <queue>
#include <thread>
#include <condition_variable>
#include <map>
#include <fstream>
#include <sstream>

namespace ACMK {

namespace {
std::string audit_json_escape(const std::string& str) {
  std::string out;
  out.reserve(str.size());
  for (char c : str) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out;
}
} // namespace

class DefaultStatePlane : public StatePlane {
private:
  StateFrame current_state;
  std::vector<StateFrame> state_history;
  std::mutex state_mutex;
  std::vector<std::function<void(const StateFrame&)>> listeners;
  
public:
  DefaultStatePlane() {
    current_state.cognitive_state = CognitiveState::IDLE;
    current_state.risk_posture = RiskPosture::LOW;
    current_state.confidence_global = 0.0;
    current_state.timestamp = std::chrono::system_clock::now();
  }
  
  void emit_state(const StateFrame& frame) override {
    std::lock_guard<std::mutex> lock(state_mutex);
    current_state = frame;
    state_history.push_back(frame);
    
    for (auto& listener : listeners) {
      listener(frame);
    }
  }
  
  StateFrame get_current_state() override {
    std::lock_guard<std::mutex> lock(state_mutex);
    return current_state;
  }
  
  void subscribe(std::function<void(const StateFrame&)> listener) {
    std::lock_guard<std::mutex> lock(state_mutex);
    listeners.push_back(listener);
  }
  
  std::vector<StateFrame> get_state_history(
    const std::string& session_id,
    Timestamp from,
    Timestamp to) override {
    std::lock_guard<std::mutex> lock(state_mutex);
    std::vector<StateFrame> result;
    for (const auto& frame : state_history) {
      if (frame.session_id == session_id && 
          frame.timestamp >= from && frame.timestamp <= to) {
        result.push_back(frame);
      }
    }
    return result;
  }
};

class DefaultTracePlane : public TracePlane {
private:
  std::map<std::string, std::vector<InferenceNode>> inference_graphs;
  std::map<std::string, std::vector<PerceptualArtifact>> perception_artifacts;
  std::map<std::string, DecisionEnvelope> decision_envelopes;
  std::map<std::string, std::vector<TemporalSnapshot>> snapshots;
  std::mutex trace_mutex;
  // create_snapshot's id used to be session_id + "_" + to_time_t(timestamp)
  // alone -- to_time_t truncates to whole seconds, so two snapshots for the
  // same session created within the same second collided on id (found by
  // tests/acmk_planes_test.cpp). Harmless today (nothing looks a snapshot
  // up by id yet -- replay_frame is a no-op stub, get_snapshots returns
  // every snapshot for a session regardless of id), but "id" implies
  // uniqueness and a future real replay_frame implementation would silently
  // replay the wrong one of two colliding snapshots. A monotonic counter
  // suffix guarantees uniqueness regardless of clock resolution.
  uint64_t snapshot_seq = 0;

public:
  void record_frame(const std::string& session_id, const InferenceNode& node) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    inference_graphs[session_id].push_back(node);
  }

  std::vector<InferenceNode> get_inference_graph(
    const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    auto it = inference_graphs.find(session_id);
    if (it == inference_graphs.end()) return {};
    return it->second;
  }

  void record_perceptual_artifact(const std::string& session_id, const PerceptualArtifact& artifact) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    perception_artifacts[session_id].push_back(artifact);
  }

  std::vector<PerceptualArtifact> get_perceptual_artifacts(
    const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (perception_artifacts.find(session_id) != perception_artifacts.end()) {
      return perception_artifacts[session_id];
    }
    return {};
  }

  void record_decision_envelope(const std::string& session_id, const DecisionEnvelope& envelope) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    decision_envelopes[session_id] = envelope;
  }

  bool get_decision_envelope(const std::string& session_id, DecisionEnvelope& out) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    auto it = decision_envelopes.find(session_id);
    if (it == decision_envelopes.end()) return false;
    out = it->second;
    return true;
  }

  std::string create_snapshot(const std::string& session_id,
                             Timestamp timestamp,
                             const std::string& state_hash) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    TemporalSnapshot snapshot;
    snapshot.session_id = session_id;
    snapshot.timestamp = timestamp;
    snapshot.state_hash = state_hash;
    snapshot.snapshot_id = session_id + "_" + std::to_string(
      std::chrono::system_clock::to_time_t(timestamp)) + "_" + std::to_string(snapshot_seq++);

    snapshots[session_id].push_back(snapshot);
    return snapshot.snapshot_id;
  }
  
  void replay_frame(const std::string& session_id,
                   const std::string& snapshot_id) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
  }

  std::vector<TemporalSnapshot> get_snapshots(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(trace_mutex);
    auto it = snapshots.find(session_id);
    if (it == snapshots.end()) return {};
    return it->second;
  }
};

class DefaultControlPlane : public ControlPlane {
private:
  std::map<std::string, std::string> session_states;
  std::mutex control_mutex;
  
public:
  bool request_pause(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(control_mutex);
    session_states[session_id] = "PAUSED";
    return true;
  }
  
  bool request_resume(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(control_mutex);
    session_states[session_id] = "RUNNING";
    return true;
  }
  
  bool request_replay(const std::string& session_id,
                     long from_timestamp,
                     double speed,
                     const std::string& reason) override {
    std::lock_guard<std::mutex> lock(control_mutex);
    return true;
  }
  
  bool request_freeze(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(control_mutex);
    session_states[session_id] = "FROZEN";
    return true;
  }
  
  bool request_recompute(const std::string& session_id,
                        const std::string& scope) override {
    std::lock_guard<std::mutex> lock(control_mutex);
    return true;
  }
};

// Audit trail: appended to disk (never truncated/rewritten) so a process
// restart doesn't erase compliance-relevant history, in addition to the
// in-memory copy used to serve get_human_events() cheaply. Each line is
// hash-chained (like a minimal blockchain/git-commit-DAG) to the previous
// one, so editing or removing an earlier line breaks every hash after it --
// tamper-evident, not tamper-proof (an editor with file access could still
// rewrite the whole chain; true tamper-proofing needs write-once storage or
// an external anchor, which is out of scope here).
class DefaultEnvironmentIO : public EnvironmentIO {
private:
  std::vector<HumanInterventionEvent> intervention_log;
  std::vector<Provenance> provenance_log;
  std::mutex env_mutex;
  std::string audit_file_path = "audit_log.ndjson";
  std::string chain_hash = "genesis";

public:
  DefaultEnvironmentIO() {
    // Continue the hash chain across restarts instead of resetting it, so
    // the whole file (not just the current process's lifetime) stays
    // verifiable as one chain.
    std::ifstream in(audit_file_path);
    std::string line, last_line;
    while (std::getline(in, line)) {
      if (!line.empty()) last_line = line;
    }
    if (!last_line.empty()) {
      size_t pos = last_line.rfind("\"hash\":\"");
      if (pos != std::string::npos) {
        size_t start = pos + 8;
        size_t end = last_line.find('"', start);
        if (end != std::string::npos) {
          chain_hash = last_line.substr(start, end - start);
        }
      }
    }
  }

private:
  void append_line(const std::string& json_body_without_hash) {
    std::string entry_hash = Crypto::sha256_hex(chain_hash + json_body_without_hash);
    std::string line = json_body_without_hash.substr(0, json_body_without_hash.size() - 1) +
                        ",\"prev_hash\":\"" + chain_hash + "\",\"hash\":\"" + entry_hash + "\"}";
    chain_hash = entry_hash;

    std::ofstream out(audit_file_path, std::ios::app);
    if (out.is_open()) {
      out << line << "\n";
    }
  }

public:
  void record_human_event(const HumanInterventionEvent& event) override {
    std::lock_guard<std::mutex> lock(env_mutex);
    intervention_log.push_back(event);

    std::ostringstream line;
    line << "{\"record_type\":\"human_event\","
         << "\"event_type\":\"" << audit_json_escape(event.event_type) << "\","
         << "\"user_id\":\"" << audit_json_escape(event.user_id) << "\","
         << "\"session_id\":\"" << audit_json_escape(event.session_id) << "\","
         << "\"scope\":\"" << audit_json_escape(event.scope) << "\","
         << "\"content\":\"" << audit_json_escape(event.content) << "\","
         << "\"timestamp\":" << std::chrono::system_clock::to_time_t(event.timestamp) << "}";
    append_line(line.str());
  }

  void record_provenance(const Provenance& prov) override {
    std::lock_guard<std::mutex> lock(env_mutex);
    provenance_log.push_back(prov);

    std::ostringstream line;
    line << "{\"record_type\":\"provenance\","
         << "\"agent_who\":\"" << audit_json_escape(prov.agent_who) << "\","
         << "\"agent_role\":\"" << audit_json_escape(prov.agent_role) << "\","
         << "\"entity_what\":\"" << audit_json_escape(prov.entity_what) << "\","
         << "\"version_info\":\"" << audit_json_escape(prov.version_info) << "\","
         << "\"timestamp\":" << std::chrono::system_clock::to_time_t(prov.recorded) << "}";
    append_line(line.str());
  }

  std::vector<HumanInterventionEvent> get_human_events(
    const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(env_mutex);
    std::vector<HumanInterventionEvent> result;
    for (const auto& event : intervention_log) {
      if (event.session_id == session_id) {
        result.push_back(event);
      }
    }
    return result;
  }
};

class DefaultInferenceCoreProxy : public InferenceCoreProxy {
public:
  void emit_readonly_trace(const InferenceNode& node) override {
  }
  
  void emit_decision_envelope(const DecisionEnvelope& envelope) override {
  }
  
  void emit_error(ErrorClass error_class,
                 const std::string& scope,
                 const std::string& severity) override {
  }
};



std::unique_ptr<ACMKPlanesCoordinator> create_default_planes_coordinator() {
  auto coordinator = std::make_unique<ACMKPlanesCoordinator>();

  coordinator->set_state_plane(std::make_unique<DefaultStatePlane>());
  coordinator->set_trace_plane(std::make_unique<DefaultTracePlane>());
  coordinator->set_control_plane(std::make_unique<DefaultControlPlane>());
  coordinator->set_environment_io(std::make_unique<DefaultEnvironmentIO>());
  coordinator->set_inference_core_proxy(std::make_unique<DefaultInferenceCoreProxy>());
  coordinator->set_simulation_enforcer(create_default_simulation_enforcer());
  coordinator->set_temporal_control_engine(create_default_temporal_control_engine());

  return coordinator;
}

}
