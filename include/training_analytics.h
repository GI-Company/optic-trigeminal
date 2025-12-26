#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <memory>
#include <fstream>
#include <sstream>

struct TrainingEvent {
    std::string event_id;
    std::string session_id;
    std::string scenario_id;
    std::time_t timestamp;
    std::string event_type;
    std::string event_data;
    int elapsed_seconds;
    
    std::string to_json_string() const {
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"event_id\": \"" << event_id << "\",\n";
        ss << "  \"session_id\": \"" << session_id << "\",\n";
        ss << "  \"scenario_id\": \"" << scenario_id << "\",\n";
        ss << "  \"timestamp\": " << static_cast<long>(timestamp) << ",\n";
        ss << "  \"event_type\": \"" << event_type << "\",\n";
        ss << "  \"event_data\": \"" << event_data << "\",\n";
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
    void record_vitals_snapshot(const std::string& session_id, const std::string& scenario_id, 
                               int elapsed_sec, int hr, int bp_sys, int spo2, float temp);
    void record_recommendation(const std::string& session_id, const std::string& rec_id, 
                              const std::string& rec_text, int elapsed_sec);
    void record_nurse_action(const std::string& session_id, const std::string& action, 
                           const std::string& nurse_id, int elapsed_sec, bool was_timely);
    void record_failure_condition(const std::string& session_id, const std::string& failure_name, 
                                 int elapsed_sec);
    void record_session_complete(const std::string& session_id, const std::string& scenario_id,
                                const std::string& nurse_id, const std::string& outcome, 
                                int total_duration);
    
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
