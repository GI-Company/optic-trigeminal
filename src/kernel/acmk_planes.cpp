#include "acmk_planes.h"
#include <uuid/uuid.h>
#include <sstream>
#include <iomanip>

namespace ACMK {

std::string generate_uuid() {
  uuid_t uuid;
  uuid_generate(uuid);
  char uuid_str[37];
  uuid_unparse(uuid, uuid_str);
  return std::string(uuid_str);
}

ACMKPlanesCoordinator::ACMKPlanesCoordinator() {}

SessionDescriptor ACMKPlanesCoordinator::initialize_session(
    const std::string& device_fp,
    const std::string& user_role,
    const std::string& mode) {
  
  std::lock_guard<std::mutex> lock(session_mutex);
  
  SessionDescriptor session;
  session.session_id = generate_uuid();
  session.model_version = "acmk-ot-v1.0";
  session.rule_set_version = "rules-v1.0";
  session.clock_anchor = std::chrono::system_clock::now();
  session.device_fingerprint = device_fp;
  session.user_role = user_role;
  
  session.permissions["read_state"] = true;
  session.permissions["read_trace"] = true;
  session.permissions["request_control"] = true;
  session.permissions["annotate"] = user_role != "it";
  session.permissions["escalate"] = user_role == "rn" || user_role == "provider";
  session.permissions["simulation_mode"] = mode != "real_world";

  sessions[session.session_id] = session;

  if (simulation_enforcer) {
    if (mode == "real_world") {
      simulation_enforcer->disable_simulation_mode(session.session_id);
    } else {
      simulation_enforcer->enable_simulation_mode(session.session_id);
    }
  }

  return session;
}

void ACMKPlanesCoordinator::set_state_plane(std::unique_ptr<StatePlane> plane) {
  state_plane = std::move(plane);
}

void ACMKPlanesCoordinator::set_trace_plane(std::unique_ptr<TracePlane> plane) {
  trace_plane = std::move(plane);
}

void ACMKPlanesCoordinator::set_control_plane(std::unique_ptr<ControlPlane> plane) {
  control_plane = std::move(plane);
}

void ACMKPlanesCoordinator::set_inference_core_proxy(std::unique_ptr<InferenceCoreProxy> proxy) {
  inference_proxy = std::move(proxy);
}

void ACMKPlanesCoordinator::set_environment_io(std::unique_ptr<EnvironmentIO> env_io) {
  environment_io = std::move(env_io);
}

void ACMKPlanesCoordinator::set_simulation_enforcer(std::unique_ptr<SimulationEnforcer> enforcer) {
  simulation_enforcer = std::move(enforcer);
}

void ACMKPlanesCoordinator::set_temporal_control_engine(std::unique_ptr<TemporalControlEngine> engine) {
  temporal_control_engine = std::move(engine);
}

const SessionDescriptor* ACMKPlanesCoordinator::get_session(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(session_mutex);
  auto it = sessions.find(session_id);
  if (it == sessions.end()) return nullptr;
  return &it->second;
}

bool ACMKPlanesCoordinator::is_simulation_mode(const std::string& session_id) {
  if (simulation_enforcer) {
    return simulation_enforcer->is_simulation_mode(session_id);
  }
  std::lock_guard<std::mutex> lock(session_mutex);
  auto it = sessions.find(session_id);
  if (it == sessions.end()) return true; // fail closed
  auto perm_it = it->second.permissions.find("simulation_mode");
  return perm_it == it->second.permissions.end() || perm_it->second;
}

}
