#include "simulation_enforcement.h"
#include <mutex>
#include <algorithm>

namespace ACMK {

class DefaultSimulationEnforcer : public SimulationEnforcer {
public:
  void enable_simulation_mode(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& session = sessions_[session_id];
    session.simulation_session_id = session_id;
    session.session_id = session_id;
    session.mode = SimulationMode::SIMULATION;
    session.start_time = std::chrono::system_clock::now();
    session.is_active = true;
  }

  void disable_simulation_mode(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& session = sessions_[session_id];
    session.simulation_session_id = session_id;
    session.session_id = session_id;
    session.mode = SimulationMode::REAL_WORLD;
    if (session.start_time.time_since_epoch().count() == 0) {
      session.start_time = std::chrono::system_clock::now();
    }
    session.is_active = true;
  }

  bool is_simulation_mode(const std::string& session_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    // Unknown sessions default to simulation: fail closed, never fail open
    // into treating an unrecognized session as real-world.
    if (it == sessions_.end()) return true;
    return it->second.mode == SimulationMode::SIMULATION;
  }

  void inject_synthetic_input(const std::string& session_id,
                               const SimulationInput& input) override {
    std::lock_guard<std::mutex> lock(mutex_);
    inputs_[session_id].push_back(input);
  }

  void tag_output_non_operative(const std::string& session_id,
                                 const SimulationOutput& output) override {
    std::lock_guard<std::mutex> lock(mutex_);
    SimulationOutput tagged = output;
    tagged.is_tagged_non_operative = true;
    if (tagged.tag_reason.empty()) tagged.tag_reason = "simulation_mode";
    outputs_[session_id].push_back(tagged);
  }

  bool allow_real_world_hooks(const std::string& session_id) override {
    // The simulation enforcer only ever reports what a session was
    // authorized for at initialize_session() time (see
    // ACMKPlanesCoordinator::initialize_session) -- it never grants
    // real-world access on its own.
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return false;
    return it->second.mode == SimulationMode::REAL_WORLD;
  }

  SimulationSession get_simulation_session(const std::string& session_id) const override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) return SimulationSession{};
    return it->second;
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, SimulationSession> sessions_;
  std::map<std::string, std::vector<SimulationInput>> inputs_;
  std::map<std::string, std::vector<SimulationOutput>> outputs_;
};

class DefaultDeterministicReplayEngine : public DeterministicReplayEngine {
public:
  void record_deterministic_trace(const std::string& session_id,
                                   const std::string& trace_id,
                                   const json& state_snapshot) override {
    std::lock_guard<std::mutex> lock(mutex_);
    traces_[session_id].push_back({trace_id, state_snapshot});
  }

  json replay_deterministically(const std::string& session_id,
                                 const std::string& from_trace_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = traces_.find(session_id);
    if (it == traces_.end()) return json::object();
    for (const auto& entry : it->second) {
      if (entry.first == from_trace_id) return entry.second;
    }
    return json::object();
  }

  std::vector<std::string> get_available_traces(const std::string& session_id) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    auto it = traces_.find(session_id);
    if (it == traces_.end()) return ids;
    for (const auto& entry : it->second) ids.push_back(entry.first);
    return ids;
  }

  double get_replay_progress(const std::string& session_id) const override {
    return 0.0;
  }

private:
  mutable std::mutex mutex_;
  std::map<std::string, std::vector<std::pair<std::string, json>>> traces_;
};

class DefaultSimulationScenarioLibrary : public SimulationScenarioLibrary {
public:
  DefaultSimulationScenarioLibrary() {
    SimulationScenario sepsis;
    sepsis.scenario_id = "sepsis_progression_01";
    sepsis.scenario_type = SimulationScenarioType::SEPSIS_PROGRESSION;
    sepsis.title = "Early Sepsis Recognition";
    sepsis.description = "Patient trends toward SIRS criteria over 20 minutes.";
    sepsis.difficulty = "intermediate";
    sepsis.duration_minutes = 20;
    scenarios_[sepsis.scenario_id] = sepsis;

    SimulationScenario resp;
    resp.scenario_id = "respiratory_distress_01";
    resp.scenario_type = SimulationScenarioType::RESPIRATORY_DISTRESS;
    resp.title = "Acute Respiratory Distress";
    resp.description = "Declining SpO2 with increasing work of breathing.";
    resp.difficulty = "beginner";
    resp.duration_minutes = 15;
    scenarios_[resp.scenario_id] = resp;
  }

  std::vector<SimulationScenario> list_scenarios() const override {
    std::vector<SimulationScenario> out;
    for (const auto& kv : scenarios_) out.push_back(kv.second);
    return out;
  }

  SimulationScenario get_scenario(const std::string& scenario_id) const override {
    auto it = scenarios_.find(scenario_id);
    if (it == scenarios_.end()) return SimulationScenario{};
    return it->second;
  }

  std::vector<SimulationScenario> search_scenarios(const std::string& query) const override {
    std::vector<SimulationScenario> out;
    for (const auto& kv : scenarios_) {
      if (kv.second.title.find(query) != std::string::npos ||
          kv.second.description.find(query) != std::string::npos) {
        out.push_back(kv.second);
      }
    }
    return out;
  }

  void register_scenario(const SimulationScenario& scenario) override {
    scenarios_[scenario.scenario_id] = scenario;
  }

  void unregister_scenario(const std::string& scenario_id) override {
    scenarios_.erase(scenario_id);
  }

  std::vector<SimulationScenario> get_scenarios_by_difficulty(const std::string& difficulty) const override {
    std::vector<SimulationScenario> out;
    for (const auto& kv : scenarios_) {
      if (kv.second.difficulty == difficulty) out.push_back(kv.second);
    }
    return out;
  }

  std::vector<SimulationScenario> get_scenarios_by_type(SimulationScenarioType type) const override {
    std::vector<SimulationScenario> out;
    for (const auto& kv : scenarios_) {
      if (kv.second.scenario_type == type) out.push_back(kv.second);
    }
    return out;
  }

private:
  std::map<std::string, SimulationScenario> scenarios_;
};

class DefaultSimulationBannerProvider : public SimulationBannerProvider {
public:
  std::string get_simulation_banner_html() const override {
    return "<div class=\"acmk-simulation-banner\" role=\"alert\">"
           "SIMULATION MODE &mdash; no real patient data is affected. "
           "Outputs are tagged non-operative.</div>";
  }

  std::string get_simulation_banner_text() const override {
    return "SIMULATION MODE — no real patient data is affected. Outputs are tagged non-operative.";
  }

  bool should_display_banner(const std::string& session_id) const override {
    // Conservative default: always show unless the caller knows the
    // session is real-world (checked against the enforcer separately by
    // the API layer, which has access to the coordinator's session table).
    return true;
  }
};

std::unique_ptr<SimulationEnforcer> create_default_simulation_enforcer() {
  return std::make_unique<DefaultSimulationEnforcer>();
}

std::unique_ptr<DeterministicReplayEngine> create_default_replay_engine() {
  return std::make_unique<DefaultDeterministicReplayEngine>();
}

std::unique_ptr<SimulationScenarioLibrary> create_default_scenario_library() {
  return std::make_unique<DefaultSimulationScenarioLibrary>();
}

std::unique_ptr<SimulationBannerProvider> create_default_banner_provider() {
  return std::make_unique<DefaultSimulationBannerProvider>();
}

}
