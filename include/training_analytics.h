#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <fstream>
#include <sstream>
#include "json_lite.h"

struct TrainingEvent {
    std::string event_id;
    std::string session_id;
    std::string scenario_id;
    std::time_t timestamp;
    std::string event_type;
    std::string event_data;
    int elapsed_seconds;

    std::string to_json_string() const {
        // event_data was never escaped -- harmless while every value was a
        // synthetic space-free "key=value" token (nothing to escape), but a
        // real hazard once free text (e.g. a signed training note) needs to
        // go through this same append-only log: an embedded quote or
        // newline would have corrupted the NDJSON record. json::escape is
        // the same escaping every other JSON value in this codebase goes
        // through via Json::dump().
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"event_id\": \"" << json::escape(event_id) << "\",\n";
        ss << "  \"session_id\": \"" << json::escape(session_id) << "\",\n";
        ss << "  \"scenario_id\": \"" << json::escape(scenario_id) << "\",\n";
        ss << "  \"timestamp\": " << static_cast<long>(timestamp) << ",\n";
        ss << "  \"event_type\": \"" << json::escape(event_type) << "\",\n";
        ss << "  \"event_data\": \"" << json::escape(event_data) << "\",\n";
        ss << "  \"elapsed_seconds\": " << elapsed_seconds << ",\n";
        ss << "  \"provenance\": {\n";
        ss << "    \"mode\": \"TRAINING\",\n";
        ss << "    \"immutable\": true\n";
        ss << "  }\n";
        ss << "}\n";
        return ss.str();
    }
};

struct TrainingMetrics {
    std::string session_id;
    std::string scenario_id;
    std::string nurse_id;
    std::string nurse_role;
    int total_duration_seconds;
    int total_actions;
    int missed_critical_windows;
    int incorrect_actions;
    float ai_recommendation_acceptance_rate;
    float scenario_effectiveness_score;
    // The actual graded score from the session (0-100, same number
    // handle_training_end showed the nurse on their own debrief) --
    // distinct from scenario_effectiveness_score, which is a coarser proxy
    // derived only from missed_critical_windows. 0 for sessions recorded
    // before this field existed (no SESSION_COMPLETE score= to read).
    float final_score;
    std::string outcome;
    std::time_t session_start;
    std::time_t session_end;
};

class TrainingAnalyticsStore {
public:
    TrainingAnalyticsStore(const std::string& store_path = "data/training_analytics");
    ~TrainingAnalyticsStore();
    
    bool initialize();
    
    void append_event(const TrainingEvent& event);
    // Snapshots the actual (possibly randomized -- see
    // ScenarioLibrary::randomize_case) synthetic patient a session started
    // with, so a report/note rebuilt later from persisted events can show
    // what the nurse actually saw instead of get_scenario_definition_by_id()'s
    // canonical template.
    void record_session_start(const std::string& session_id, const std::string& scenario_id,
                             const std::string& nurse_id, int age, const std::string& sex,
                             const std::string& diagnosis, int hr, int rr, int spo2,
                             int bp_sys, int bp_dia, float temp);
    void record_vitals_snapshot(const std::string& session_id, const std::string& scenario_id,
                               int elapsed_sec, int hr, int bp_sys, int spo2, float temp);
    void record_recommendation(const std::string& session_id, const std::string& rec_id, 
                              const std::string& rec_text, int elapsed_sec);
    // `grade` is the ActionGrade enum's name as a string (e.g. "CORRECT",
    // "CONTRAINDICATED") and `delta` the real score_delta that was applied
    // -- was_timely is kept, derived as grade being CORRECT or
    // PARTIALLY_CORRECT, so calculate_session_metrics's existing
    // incorrect_actions counter (which only reads timely=) keeps working
    // unmodified.
    void record_nurse_action(const std::string& session_id, const std::string& action,
                           const std::string& nurse_id, int elapsed_sec, bool was_timely,
                           const std::string& grade, float delta);
    void record_failure_condition(const std::string& session_id, const std::string& failure_name,
                                 int elapsed_sec);
    // Audit trail of what the AI actually drafted, kept even if the nurse
    // heavily edits before signing -- append-only, like every other event
    // here, so signing isn't a mutation of this record.
    void record_note_drafted(const std::string& session_id, const std::string& scenario_id,
                            int elapsed_sec, const std::string& ai_draft_content);
    // "The" note for a session is the latest NOTE_SIGNED event by
    // timestamp -- signing more than once is allowed (event-sourced, not
    // overwritten), consistent with how every other record in this store
    // is a fold over immutable events rather than in-place mutation.
    void record_note_signed(const std::string& session_id, const std::string& scenario_id,
                           const std::string& nurse_id, int elapsed_sec,
                           const std::string& final_content, bool was_edited);
    void record_session_complete(const std::string& session_id, const std::string& scenario_id,
                                const std::string& nurse_id, const std::string& outcome,
                                int total_duration, float final_score);
    
    std::vector<TrainingEvent> get_session_events(const std::string& session_id);
    std::vector<TrainingEvent> get_scenario_events(const std::string& scenario_id);
    TrainingMetrics calculate_session_metrics(const std::string& session_id);
    
    std::vector<std::string> list_all_sessions();
    
    bool session_exists(const std::string& session_id);
    
private:
    std::string store_path_;
    std::ofstream event_stream_;
    bool initialized_;
    
    std::string generate_event_id();
    std::string events_to_json_string(const std::vector<TrainingEvent>& events);
    std::string event_file_path(const std::string& session_id);
};

class TrainingReplayEngine {
public:
    TrainingReplayEngine(TrainingAnalyticsStore& store);
    
    void replay_session(const std::string& session_id);
    void print_session_timeline(const std::string& session_id);
    std::string generate_session_report(const std::string& session_id);
    
private:
    TrainingAnalyticsStore& store_;
};
