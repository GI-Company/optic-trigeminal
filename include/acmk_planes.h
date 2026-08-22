#pragma once

#include "types.h"
#include "simulation_enforcement.h"
#include "temporal_controls.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <mutex>

namespace ACMK {

using Timestamp = std::chrono::system_clock::time_point;

struct SessionDescriptor {
  std::string session_id;
  std::string model_version;
  std::string rule_set_version;
  Timestamp clock_anchor;
  std::map<std::string, bool> permissions;
  std::string device_fingerprint;
  std::string user_role;
};

enum class CognitiveState {
  IDLE,
  INGESTING,
  RESOLVING,
  CONVERGING,
  CONVERGED
};

enum class RiskPosture {
  LOW,
  MODERATE,
  ELEVATED,
  CRITICAL
};

struct StateFrame {
  std::string session_id;
  Timestamp timestamp;
  CognitiveState cognitive_state;
  RiskPosture risk_posture;
  std::vector<std::string> input_modalities;
  double confidence_global;
  std::string patient_id;
};

enum class ErrorClass {
  PERCEPTUAL_FAILURE,
  TEMPORAL_INCONSISTENCY,
  CONSTRAINT_CONFLICT,
  CONFIDENCE_COLLAPSE,
  HUMAN_INTERFERENCE
};

struct PerceptualArtifact {
  std::string artifact_id;
  std::string artifact_type;
  std::string content_hash;
  Timestamp timestamp;
  double confidence;
  std::vector<std::string> alignment_metadata;
};

struct TemporalSnapshot {
  std::string snapshot_id;
  std::string session_id;
  Timestamp timestamp;
  // Content-derived fingerprint of the state being snapshotted (caller
  // computes it from whatever it's snapshotting, e.g. sha256 of the
  // decision/recommendation text) -- lets a viewer confirm two snapshots
  // really did capture different states rather than trusting the label.
  std::string state_hash;
};

struct InferenceNode {
  std::string node_id;
  std::vector<std::string> parent_nodes;
  std::string status;
  double confidence;
  std::string suppression_reason;
  std::pair<long, long> time_range;
};

struct DecisionEnvelope {
  std::string final_state;
  std::vector<std::string> dominant_constraints;
  std::vector<std::pair<std::string, std::string>> rejected_alternatives;
  std::pair<double, double> confidence_bounds;
};

struct HumanInterventionEvent {
  std::string event_type;
  std::string user_id;
  Timestamp timestamp;
  std::string scope;
  std::string content;
  // Was missing entirely -- EnvironmentIO::get_human_events(session_id)
  // had nothing real to filter on, so it matched session_id as a
  // substring of user_id instead (see state_plane.cpp), which is not
  // actually session scoping at all.
  std::string session_id;
};

struct Provenance {
  std::string agent_who;
  std::string agent_role;
  Timestamp recorded;
  std::string entity_what;
  std::string version_info;
};

class StatePlane {
public:
  virtual ~StatePlane() = default;
  virtual void emit_state(const StateFrame& frame) = 0;
  virtual StateFrame get_current_state() = 0;
  // DefaultStatePlane (state_plane.cpp) already stores every emitted frame
  // in state_history_ and had a working get_state_history() -- it just
  // wasn't part of this interface, so nothing holding a StatePlane* could
  // reach it, and handle_state_history() (acmk_api_handler.cpp) returned a
  // hardcoded empty array instead.
  virtual std::vector<StateFrame> get_state_history(
    const std::string& session_id, Timestamp from, Timestamp to) = 0;
};

class TracePlane {
public:
  virtual ~TracePlane() = default;
  // record_frame used to take only the node, with no session_id at all --
  // DefaultTracePlane keyed its storage map by node_id instead (see
  // state_plane.cpp), so get_inference_graph(session_id) could never
  // actually scope by session; it just returned every node from every
  // session ever recorded, concatenated together.
  virtual void record_frame(const std::string& session_id, const InferenceNode& node) = 0;
  virtual std::vector<InferenceNode> get_inference_graph(const std::string& session_id) = 0;
  // No write path existed for perceptual artifacts at all -- only the
  // getter was ever defined, so get_perceptual_artifacts() was guaranteed
  // to return empty forever.
  virtual void record_perceptual_artifact(const std::string& session_id, const PerceptualArtifact& artifact) = 0;
  virtual std::vector<PerceptualArtifact> get_perceptual_artifacts(const std::string& session_id) = 0;
  // DecisionEnvelope had no storage or retrieval path anywhere -- emitting
  // one (InferenceCoreProxy::emit_decision_envelope) was a complete no-op.
  virtual void record_decision_envelope(const std::string& session_id, const DecisionEnvelope& envelope) = 0;
  virtual bool get_decision_envelope(const std::string& session_id, DecisionEnvelope& out) = 0;
  virtual std::string create_snapshot(const std::string& session_id, Timestamp timestamp, const std::string& state_hash) = 0;
  virtual void replay_frame(const std::string& session_id, const std::string& snapshot_id) = 0;
  // Same story as StatePlane::get_state_history: DefaultTracePlane already
  // tracks snapshots per session, just never exposed it.
  virtual std::vector<TemporalSnapshot> get_snapshots(const std::string& session_id) = 0;
};

class ControlPlane {
public:
  virtual ~ControlPlane() = default;
  virtual bool request_pause(const std::string& session_id) = 0;
  virtual bool request_resume(const std::string& session_id) = 0;
  virtual bool request_replay(const std::string& session_id, long from_timestamp, 
                              double speed, const std::string& reason) = 0;
  virtual bool request_freeze(const std::string& session_id) = 0;
  virtual bool request_recompute(const std::string& session_id, const std::string& scope) = 0;
};

class InferenceCoreProxy {
public:
  virtual ~InferenceCoreProxy() = default;
  virtual void emit_readonly_trace(const InferenceNode& node) = 0;
  virtual void emit_decision_envelope(const DecisionEnvelope& envelope) = 0;
  virtual void emit_error(ErrorClass error_class, const std::string& scope, 
                         const std::string& severity) = 0;
};

class EnvironmentIO {
public:
  virtual ~EnvironmentIO() = default;
  virtual void record_human_event(const HumanInterventionEvent& event) = 0;
  virtual void record_provenance(const Provenance& prov) = 0;
  virtual std::vector<HumanInterventionEvent> get_human_events(const std::string& session_id) = 0;
};

class ACMKPlanesCoordinator {
public:
  ACMKPlanesCoordinator();

  // mode must already be authorization-checked by the caller (real_world
  // sessions require an explicitly-permitted role and the server operator
  // opting in via ACMK_ENABLE_REAL_WORLD=1) -- this just records the
  // decision and enables/disables the simulation enforcer accordingly.
  SessionDescriptor initialize_session(const std::string& device_fp,
                                       const std::string& user_role,
                                       const std::string& mode);

  void set_state_plane(std::unique_ptr<StatePlane> plane);
  void set_trace_plane(std::unique_ptr<TracePlane> plane);
  void set_control_plane(std::unique_ptr<ControlPlane> plane);
  void set_inference_core_proxy(std::unique_ptr<InferenceCoreProxy> proxy);
  void set_environment_io(std::unique_ptr<EnvironmentIO> env_io);
  void set_simulation_enforcer(std::unique_ptr<SimulationEnforcer> enforcer);
  void set_temporal_control_engine(std::unique_ptr<TemporalControlEngine> engine);

  StatePlane* get_state_plane() const { return state_plane.get(); }
  TracePlane* get_trace_plane() const { return trace_plane.get(); }
  ControlPlane* get_control_plane() const { return control_plane.get(); }
  InferenceCoreProxy* get_inference_proxy() const { return inference_proxy.get(); }
  EnvironmentIO* get_environment_io() const { return environment_io.get(); }
  SimulationEnforcer* get_simulation_enforcer() const { return simulation_enforcer.get(); }
  TemporalControlEngine* get_temporal_control_engine() const { return temporal_control_engine.get(); }

  // Returns nullptr if the session doesn't exist.
  const SessionDescriptor* get_session(const std::string& session_id);

  // True unless the session was initialized in "real_world" mode.
  bool is_simulation_mode(const std::string& session_id);

private:
  std::unique_ptr<StatePlane> state_plane;
  std::unique_ptr<TracePlane> trace_plane;
  std::unique_ptr<ControlPlane> control_plane;
  std::unique_ptr<InferenceCoreProxy> inference_proxy;
  std::unique_ptr<EnvironmentIO> environment_io;
  std::unique_ptr<SimulationEnforcer> simulation_enforcer;
  std::unique_ptr<TemporalControlEngine> temporal_control_engine;
  std::map<std::string, SessionDescriptor> sessions;
  std::mutex session_mutex;
};

std::unique_ptr<ACMKPlanesCoordinator> create_default_planes_coordinator();

}
