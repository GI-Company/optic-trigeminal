#include "training_analytics.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace fs = std::filesystem;

TrainingAnalyticsStore::TrainingAnalyticsStore(const std::string& store_path)
    : store_path_(store_path), initialized_(false) {
}

TrainingAnalyticsStore::~TrainingAnalyticsStore() {
    if (event_stream_.is_open()) {
        event_stream_.close();
    }
}

bool TrainingAnalyticsStore::initialize() {
    try {
        if (!fs::exists(store_path_)) {
            fs::create_directories(store_path_);
        }
        
        std::string master_log = store_path_ + "/training_events.ndjson";
        event_stream_.open(master_log, std::ios::app);
        
        if (!event_stream_.is_open()) {
            std::cerr << "Failed to open training analytics log: " << master_log << std::endl;
            return false;
        }
        
        initialized_ = true;
        std::cout << "[TrainingAnalytics] Store initialized at: " << store_path_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "TrainingAnalyticsStore initialization error: " << e.what() << std::endl;
        return false;
    }
}

std::string TrainingAnalyticsStore::generate_event_id() {
    static int counter = 0;
    std::stringstream ss;
    ss << "EVT-" << std::time(nullptr) << "-" << (counter++);
    return ss.str();
}

void TrainingAnalyticsStore::append_event(const TrainingEvent& event) {
    if (!initialized_) return;
    
    event_stream_ << event.to_json_string();
    event_stream_.flush();
}

void TrainingAnalyticsStore::record_vitals_snapshot(const std::string& session_id, 
                                                   const std::string& scenario_id,
                                                   int elapsed_sec, int hr, int bp_sys, 
                                                   int spo2, float temp) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "VITALS_SNAPSHOT";
    evt.elapsed_seconds = elapsed_sec;
    
    std::stringstream data;
    data << "hr=" << hr << " bp=" << bp_sys << " spo2=" << spo2 << " temp=" << std::fixed << std::setprecision(1) << temp;
    evt.event_data = data.str();
    
    append_event(evt);
}

void TrainingAnalyticsStore::record_recommendation(const std::string& session_id,
                                                  const std::string& rec_id,
                                                  const std::string& rec_text,
                                                  int elapsed_sec) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = "";
    evt.timestamp = std::time(nullptr);
    evt.event_type = "AI_RECOMMENDATION";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = "rec_id=" + rec_id + " text=" + rec_text;
    
    append_event(evt);
}

void TrainingAnalyticsStore::record_nurse_action(const std::string& session_id,
                                                const std::string& action,
                                                const std::string& nurse_id,
                                                int elapsed_sec,
                                                bool was_timely) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = "";
    evt.timestamp = std::time(nullptr);
    evt.event_type = "NURSE_ACTION";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = "action=" + action + " nurse_id=" + nurse_id + " timely=" + std::string(was_timely ? "true" : "false");
    
    append_event(evt);
}

void TrainingAnalyticsStore::record_failure_condition(const std::string& session_id,
                                                     const std::string& failure_name,
                                                     int elapsed_sec) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = "";
    evt.timestamp = std::time(nullptr);
    evt.event_type = "FAILURE_TRIGGERED";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = "failure=" + failure_name;
    
    append_event(evt);
}

void TrainingAnalyticsStore::record_session_complete(const std::string& session_id,
                                                    const std::string& scenario_id,
                                                    const std::string& nurse_id,
                                                    const std::string& outcome,
                                                    int total_duration) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "SESSION_COMPLETE";
    evt.elapsed_seconds = total_duration;
    evt.event_data = "outcome=" + outcome + " nurse_id=" + nurse_id + " duration=" + std::to_string(total_duration);
    
    append_event(evt);
}

std::vector<TrainingEvent> TrainingAnalyticsStore::get_session_events(const std::string& session_id) {
    std::vector<TrainingEvent> events;
    std::string master_log = store_path_ + "/training_events.ndjson";
    
    std::ifstream infile(master_log);
    if (!infile.is_open()) return events;
    
    std::string line;
    while (std::getline(infile, line)) {
        if (line.find("\"session_id\": \"" + session_id + "\"") != std::string::npos) {
            TrainingEvent evt;
            evt.session_id = session_id;
            evt.event_type = (line.find("VITALS") != std::string::npos) ? "VITALS_SNAPSHOT" :
                            (line.find("RECOMMENDATION") != std::string::npos) ? "AI_RECOMMENDATION" :
                            (line.find("ACTION") != std::string::npos) ? "NURSE_ACTION" :
                            (line.find("FAILURE") != std::string::npos) ? "FAILURE_TRIGGERED" : "SESSION_COMPLETE";
            events.push_back(evt);
        }
    }
    infile.close();
    
    return events;
}

std::vector<TrainingEvent> TrainingAnalyticsStore::get_scenario_events(const std::string& scenario_id) {
    std::vector<TrainingEvent> events;
    std::string master_log = store_path_ + "/training_events.ndjson";
    
    std::ifstream infile(master_log);
    if (!infile.is_open()) return events;
    
    std::string line;
    while (std::getline(infile, line)) {
        if (line.find("\"scenario_id\": \"" + scenario_id + "\"") != std::string::npos) {
            TrainingEvent evt;
            evt.scenario_id = scenario_id;
            evt.event_type = "SCENARIO_EVENT";
            events.push_back(evt);
        }
    }
    infile.close();
    
    return events;
}

TrainingMetrics TrainingAnalyticsStore::calculate_session_metrics(const std::string& session_id) {
    TrainingMetrics metrics;
    metrics.session_id = session_id;
    metrics.total_actions = 0;
    metrics.missed_critical_windows = 0;
    metrics.incorrect_actions = 0;
    metrics.ai_recommendation_acceptance_rate = 0.0f;
    metrics.scenario_effectiveness_score = 0.0f;
    
    auto events = get_session_events(session_id);
    
    int recommendations = 0;
    int accepted = 0;
    
    for (const auto& evt : events) {
        if (evt.event_type == "NURSE_ACTION") {
            metrics.total_actions++;
        } else if (evt.event_type == "AI_RECOMMENDATION") {
            recommendations++;
        } else if (evt.event_type == "FAILURE_TRIGGERED") {
            metrics.missed_critical_windows++;
        }
    }
    
    if (recommendations > 0) {
        metrics.ai_recommendation_acceptance_rate = static_cast<float>(accepted) / recommendations;
    }
    
    metrics.scenario_effectiveness_score = 
        (1.0f - (static_cast<float>(metrics.missed_critical_windows) / 10.0f)) * 100.0f;
    metrics.scenario_effectiveness_score = 
        std::max(0.0f, std::min(100.0f, metrics.scenario_effectiveness_score));
    
    return metrics;
}

std::vector<std::string> TrainingAnalyticsStore::list_all_sessions() {
    std::vector<std::string> sessions;
    std::string master_log = store_path_ + "/training_events.ndjson";
    
    std::ifstream infile(master_log);
    if (!infile.is_open()) return sessions;
    
    std::string line;
    while (std::getline(infile, line)) {
        size_t pos = line.find("\"session_id\": \"");
        if (pos != std::string::npos) {
            size_t start = pos + 16;
            size_t end = line.find("\"", start);
            std::string session_id = line.substr(start, end - start);
            if (std::find(sessions.begin(), sessions.end(), session_id) == sessions.end()) {
                sessions.push_back(session_id);
            }
        }
    }
    infile.close();
    
    return sessions;
}

bool TrainingAnalyticsStore::session_exists(const std::string& session_id) {
    auto sessions = list_all_sessions();
    return std::find(sessions.begin(), sessions.end(), session_id) != sessions.end();
}

std::string TrainingAnalyticsStore::event_file_path(const std::string& session_id) {
    return store_path_ + "/" + session_id + ".ndjson";
}

std::string TrainingAnalyticsStore::events_to_json_string(const std::vector<TrainingEvent>& events) {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"events\": [\n";
    
    for (size_t i = 0; i < events.size(); i++) {
        ss << events[i].to_json_string();
        if (i < events.size() - 1) ss << ",";
    }
    
    ss << "  ]\n";
    ss << "}\n";
    return ss.str();
}

TrainingReplayEngine::TrainingReplayEngine(TrainingAnalyticsStore& store)
    : store_(store) {
}

void TrainingReplayEngine::replay_session(const std::string& session_id) {
    auto events = store_.get_session_events(session_id);
    
    std::cout << "[TrainingReplay] Replaying session: " << session_id << std::endl;
    std::cout << "Total events: " << events.size() << std::endl;
    
    for (const auto& evt : events) {
        std::cout << "[" << evt.elapsed_seconds << "s] " 
                  << evt.event_type << ": " << evt.event_data << std::endl;
    }
}

void TrainingReplayEngine::print_session_timeline(const std::string& session_id) {
    auto events = store_.get_session_events(session_id);
    
    std::cout << "\n=== SESSION TIMELINE: " << session_id << " ===" << std::endl;
    for (const auto& evt : events) {
        std::cout << std::setw(4) << evt.elapsed_seconds << "s | "
                  << std::setw(20) << evt.event_type << " | "
                  << evt.event_data << std::endl;
    }
    std::cout << "=====================================================\n" << std::endl;
}

std::string TrainingReplayEngine::generate_session_report(const std::string& session_id) {
    auto metrics = store_.calculate_session_metrics(session_id);
    
    std::stringstream report;
    report << "{\n";
    report << "  \"session_id\": \"" << metrics.session_id << "\",\n";
    report << "  \"scenario_id\": \"" << metrics.scenario_id << "\",\n";
    report << "  \"nurse_id\": \"" << metrics.nurse_id << "\",\n";
    report << "  \"total_duration_seconds\": " << metrics.total_duration_seconds << ",\n";
    report << "  \"total_actions\": " << metrics.total_actions << ",\n";
    report << "  \"missed_critical_windows\": " << metrics.missed_critical_windows << ",\n";
    report << "  \"ai_recommendation_acceptance_rate\": " << std::fixed << std::setprecision(2) 
           << metrics.ai_recommendation_acceptance_rate << ",\n";
    report << "  \"scenario_effectiveness_score\": " << metrics.scenario_effectiveness_score << ",\n";
    report << "  \"outcome\": \"" << metrics.outcome << "\"\n";
    report << "}\n";
    
    return report.str();
}
