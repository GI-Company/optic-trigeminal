#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include "json_lite.h"

namespace ACMK {

enum class SimulationMode {
  REAL_WORLD,
  SIMULATION
};

enum class SimulationScenarioType {
  SEPSIS_PROGRESSION,
  RESPIRATORY_DISTRESS,
  CARDIAC_ARRHYTHMIA,
  NEUROTRAUMA,
  DETERIORATION,
  HANDOFF_CHALLENGE,
  CUSTOM
};

struct SimulationScenario {
  std::string scenario_id;
  SimulationScenarioType scenario_type;
  std::string title;
  std::string description;
  std::string difficulty;
  std::vector<std::string> learning_objectives;
  int duration_minutes;
  json initial_patient_state;
  std::vector<json> trigger_events;
};

struct SimulationSession {
  std::string simulation_session_id;
  std::string session_id;
  std::string scenario_id;
  SimulationMode mode;
  std::chrono::system_clock::time_point start_time;
  std::chrono::system_clock::time_point end_time;
  bool is_active;
  int elapsed_ms;
  json current_state;
};

struct SimulationInput {
  std::string input_id;
  std::string synthetic_data_type;
  json synthetic_payload;
  std::chrono::system_clock::time_point injected_at;
};

struct SimulationOutput {
  std::string output_id;
  std::string output_type;
  json output_payload;
  bool is_tagged_non_operative;
  std::string tag_reason;
};

class SimulationEnforcer {
public:
  virtual ~SimulationEnforcer() = default;
  
  virtual void enable_simulation_mode(const std::string& session_id) = 0;
  virtual void disable_simulation_mode(const std::string& session_id) = 0;
  
  virtual bool is_simulation_mode(const std::string& session_id) const = 0;
  
  virtual void inject_synthetic_input(const std::string& session_id,
                                     const SimulationInput& input) = 0;
  
  virtual void tag_output_non_operative(const std::string& session_id,
                                       const SimulationOutput& output) = 0;
  
  virtual bool allow_real_world_hooks(const std::string& session_id) = 0;
  
  virtual SimulationSession get_simulation_session(
    const std::string& session_id) const = 0;
};

class DeterministicReplayEngine {
public:
  virtual ~DeterministicReplayEngine() = default;
  
  virtual void record_deterministic_trace(
    const std::string& session_id,
    const std::string& trace_id,
    const json& state_snapshot) = 0;
  
  virtual json replay_deterministically(
    const std::string& session_id,
    const std::string& from_trace_id) = 0;
  
  virtual std::vector<std::string> get_available_traces(
    const std::string& session_id) = 0;
  
  virtual double get_replay_progress(const std::string& session_id) const = 0;
};

class SimulationScenarioLibrary {
public:
  virtual ~SimulationScenarioLibrary() = default;
  
  virtual std::vector<SimulationScenario> list_scenarios() const = 0;
  virtual SimulationScenario get_scenario(const std::string& scenario_id) const = 0;
  virtual std::vector<SimulationScenario> search_scenarios(
    const std::string& query) const = 0;
  
  virtual void register_scenario(const SimulationScenario& scenario) = 0;
  virtual void unregister_scenario(const std::string& scenario_id) = 0;
  
  virtual std::vector<SimulationScenario> get_scenarios_by_difficulty(
    const std::string& difficulty) const = 0;
  virtual std::vector<SimulationScenario> get_scenarios_by_type(
    SimulationScenarioType type) const = 0;
};

class SimulationBannerProvider {
public:
  virtual ~SimulationBannerProvider() = default;

  virtual std::string get_simulation_banner_html() const = 0;
  virtual std::string get_simulation_banner_text() const = 0;
  virtual bool should_display_banner(const std::string& session_id) const = 0;
};

std::unique_ptr<SimulationEnforcer> create_default_simulation_enforcer();
std::unique_ptr<DeterministicReplayEngine> create_default_replay_engine();
std::unique_ptr<SimulationScenarioLibrary> create_default_scenario_library();
std::unique_ptr<SimulationBannerProvider> create_default_banner_provider();

}
