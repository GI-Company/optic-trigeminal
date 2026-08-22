#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include <queue>

namespace ACMK {

using Timestamp = std::chrono::system_clock::time_point;

enum class TemporalAction {
  STEP,
  PAUSE,
  RESUME,
  ROLLBACK,
  REPLAY,
  FREEZE,
  RECOMPUTE
};

enum class ReplaySpeed {
  SLOW = 0,
  NORMAL = 1,
  FAST = 2,
  VERY_FAST = 3
};

struct TemporalControlSnapshot {
  std::string snapshot_id;
  std::string session_id;
  Timestamp timestamp;
  std::string state_hash;
  std::string inference_graph_hash;
  std::map<std::string, std::string> context;
};

struct TemporalControlRequest {
  std::string session_id;
  TemporalAction action;
  Timestamp from_timestamp;
  Timestamp to_timestamp;
  ReplaySpeed speed;
  std::string reason;
  std::string user_id;
};

struct TemporalControlResponse {
  bool allowed;
  std::string deny_reason;
  std::string snapshot_id;
  std::vector<TemporalControlSnapshot> available_snapshots;
};

class TemporalControlEngine {
public:
  virtual ~TemporalControlEngine() = default;
  
  virtual TemporalControlResponse validate_step(
    const TemporalControlRequest& request) = 0;
  
  virtual TemporalControlResponse validate_pause(
    const TemporalControlRequest& request) = 0;
  
  virtual TemporalControlResponse validate_resume(
    const TemporalControlRequest& request) = 0;
  
  virtual TemporalControlResponse validate_rollback(
    const TemporalControlRequest& request) = 0;
  
  virtual TemporalControlResponse validate_replay(
    const TemporalControlRequest& request) = 0;
  
  virtual TemporalControlResponse request_control(
    const TemporalControlRequest& request) = 0;
  
  virtual std::vector<TemporalControlSnapshot> get_available_snapshots(
    const std::string& session_id, Timestamp start, Timestamp end) = 0;
  
  virtual TemporalControlSnapshot create_snapshot(const std::string& session_id,
                                          Timestamp timestamp) = 0;
  
  virtual void restore_from_snapshot(const TemporalControlSnapshot& snapshot) = 0;
};

class ReplayStream {
public:
  virtual ~ReplayStream() = default;
  
  virtual void start_replay(const std::string& session_id,
                           const TemporalControlSnapshot& from_snapshot,
                           ReplaySpeed speed) = 0;
  
  virtual void pause_replay() = 0;
  virtual void resume_replay() = 0;
  virtual void stop_replay() = 0;
  
  virtual bool is_replaying() const = 0;
  virtual double get_replay_progress() const = 0;
};

class SnapshotComparator {
public:
  virtual ~SnapshotComparator() = default;
  
  struct SnapshotDiff {
    std::vector<std::string> state_changes;
    std::vector<std::string> inference_changes;
    std::vector<std::string> confidence_deltas;
  };
  
  virtual SnapshotDiff compare_snapshots(const TemporalControlSnapshot& snapshot1,
                                        const TemporalControlSnapshot& snapshot2) = 0;

  virtual std::vector<std::string> get_decision_path(
    const std::string& session_id, Timestamp from, Timestamp to) = 0;
};

// is_simulation_mode: caller must supply the session's current mode (from
// ACMKPlanesCoordinator::is_simulation_mode) -- the engine itself only
// tracks control-action history/snapshots, not session mode, to avoid a
// second, driftable copy of the same state.
std::unique_ptr<TemporalControlEngine> create_default_temporal_control_engine();
std::unique_ptr<ReplayStream> create_default_replay_stream();
std::unique_ptr<SnapshotComparator> create_default_snapshot_comparator();

}
