#include "temporal_controls.h"
#include <mutex>
#include <algorithm>

namespace ACMK {

namespace {

// Clinical-safety rule: any control action on a session must carry a
// non-empty reason. Reasons are what makes pause/replay/rollback/recompute
// defensible in an audit review; refusing empty-reason requests is what
// makes that requirement real instead of a UI convention that can be
// skipped from a raw API call.
TemporalControlResponse require_reason(const TemporalControlRequest& request) {
  TemporalControlResponse resp;
  if (request.session_id.empty()) {
    resp.allowed = false;
    resp.deny_reason = "session_id required";
    return resp;
  }
  if (request.reason.empty()) {
    resp.allowed = false;
    resp.deny_reason = "A reason is required for this control action";
    return resp;
  }
  resp.allowed = true;
  return resp;
}

} // namespace

class DefaultTemporalControlEngine : public TemporalControlEngine {
public:
  TemporalControlResponse validate_step(const TemporalControlRequest& request) override {
    return require_reason(request);
  }

  TemporalControlResponse validate_pause(const TemporalControlRequest& request) override {
    auto resp = require_reason(request);
    if (resp.allowed) log_action(request, "PAUSE");
    return resp;
  }

  TemporalControlResponse validate_resume(const TemporalControlRequest& request) override {
    auto resp = require_reason(request);
    if (resp.allowed) log_action(request, "RESUME");
    return resp;
  }

  TemporalControlResponse validate_rollback(const TemporalControlRequest& request) override {
    auto resp = require_reason(request);
    if (!resp.allowed) return resp;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = snapshots_.find(request.session_id);
    if (it == snapshots_.end() || it->second.empty()) {
      resp.allowed = false;
      resp.deny_reason = "No snapshots available to roll back to";
      return resp;
    }
    resp.available_snapshots = it->second;
    log_action(request, "ROLLBACK");
    return resp;
  }

  TemporalControlResponse validate_replay(const TemporalControlRequest& request) override {
    auto resp = require_reason(request);
    if (resp.allowed) log_action(request, "REPLAY");
    return resp;
  }

  TemporalControlResponse request_control(const TemporalControlRequest& request) override {
    switch (request.action) {
      case TemporalAction::STEP: return validate_step(request);
      case TemporalAction::PAUSE: return validate_pause(request);
      case TemporalAction::RESUME: return validate_resume(request);
      case TemporalAction::ROLLBACK: return validate_rollback(request);
      case TemporalAction::REPLAY: return validate_replay(request);
      case TemporalAction::FREEZE: {
        auto resp = require_reason(request);
        if (resp.allowed) log_action(request, "FREEZE");
        return resp;
      }
      case TemporalAction::RECOMPUTE: {
        auto resp = require_reason(request);
        if (resp.allowed) log_action(request, "RECOMPUTE");
        return resp;
      }
    }
    TemporalControlResponse resp;
    resp.allowed = false;
    resp.deny_reason = "Unknown action";
    return resp;
  }

  std::vector<TemporalControlSnapshot> get_available_snapshots(
      const std::string& session_id, Timestamp start, Timestamp end) override {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TemporalControlSnapshot> out;
    auto it = snapshots_.find(session_id);
    if (it == snapshots_.end()) return out;
    for (const auto& snap : it->second) {
      if (snap.timestamp >= start && snap.timestamp <= end) out.push_back(snap);
    }
    return out;
  }

  TemporalControlSnapshot create_snapshot(const std::string& session_id, Timestamp timestamp) override {
    std::lock_guard<std::mutex> lock(mutex_);
    TemporalControlSnapshot snap;
    snap.session_id = session_id;
    snap.timestamp = timestamp;
    snap.snapshot_id = session_id + "_" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count());
    snapshots_[session_id].push_back(snap);
    return snap;
  }

  void restore_from_snapshot(const TemporalControlSnapshot& snapshot) override {
    std::lock_guard<std::mutex> lock(mutex_);
    last_restored_[snapshot.session_id] = snapshot.snapshot_id;
  }

private:
  void log_action(const TemporalControlRequest& request, const std::string& action) {
    std::lock_guard<std::mutex> lock(mutex_);
    action_log_[request.session_id].push_back(action + " by " + request.user_id + ": " + request.reason);
  }

  std::mutex mutex_;
  std::map<std::string, std::vector<TemporalControlSnapshot>> snapshots_;
  std::map<std::string, std::string> last_restored_;
  std::map<std::string, std::vector<std::string>> action_log_;
};

class DefaultReplayStream : public ReplayStream {
public:
  void start_replay(const std::string& session_id,
                     const TemporalControlSnapshot& from_snapshot,
                     ReplaySpeed speed) override {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id_ = session_id;
    speed_ = speed;
    replaying_ = true;
    progress_ = 0.0;
  }

  void pause_replay() override {
    std::lock_guard<std::mutex> lock(mutex_);
    replaying_ = false;
  }

  void resume_replay() override {
    std::lock_guard<std::mutex> lock(mutex_);
    replaying_ = true;
  }

  void stop_replay() override {
    std::lock_guard<std::mutex> lock(mutex_);
    replaying_ = false;
    progress_ = 0.0;
  }

  bool is_replaying() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return replaying_;
  }

  double get_replay_progress() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return progress_;
  }

private:
  mutable std::mutex mutex_;
  std::string session_id_;
  ReplaySpeed speed_ = ReplaySpeed::NORMAL;
  bool replaying_ = false;
  double progress_ = 0.0;
};

class DefaultSnapshotComparator : public SnapshotComparator {
public:
  SnapshotDiff compare_snapshots(const TemporalControlSnapshot& snapshot1,
                                  const TemporalControlSnapshot& snapshot2) override {
    SnapshotDiff diff;
    if (snapshot1.state_hash != snapshot2.state_hash) {
      diff.state_changes.push_back("state_hash differs: " + snapshot1.state_hash + " -> " + snapshot2.state_hash);
    }
    if (snapshot1.inference_graph_hash != snapshot2.inference_graph_hash) {
      diff.inference_changes.push_back(
          "inference_graph_hash differs: " + snapshot1.inference_graph_hash + " -> " + snapshot2.inference_graph_hash);
    }
    return diff;
  }

  std::vector<std::string> get_decision_path(const std::string& session_id, Timestamp from, Timestamp to) override {
    return {};
  }
};

std::unique_ptr<TemporalControlEngine> create_default_temporal_control_engine() {
  return std::make_unique<DefaultTemporalControlEngine>();
}

std::unique_ptr<ReplayStream> create_default_replay_stream() {
  return std::make_unique<DefaultReplayStream>();
}

std::unique_ptr<SnapshotComparator> create_default_snapshot_comparator() {
  return std::make_unique<DefaultSnapshotComparator>();
}

}
