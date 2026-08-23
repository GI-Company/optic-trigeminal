#include "training_analytics.h"
#include "json_lite.h"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace {
// event_data is stored as a flat "key=value key2=value2" string (see
// record_nurse_action / record_failure_condition / record_session_complete
// below) rather than nested JSON, so it needs its own small parser -- values
// never contain spaces in this codebase (action names, nurse ids, booleans),
// so splitting on whitespace then "=" is exact, not a heuristic.
std::string parse_event_data_field(const std::string& event_data, const std::string& key) {
    std::string needle = key + "=";
    size_t pos = event_data.find(needle);
    if (pos == std::string::npos) return "";
    size_t start = pos + needle.size();
    size_t end = event_data.find(' ', start);
    return event_data.substr(start, end == std::string::npos ? std::string::npos : end - start);
}
}

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

void TrainingAnalyticsStore::record_session_start(const std::string& session_id,
                                                  const std::string& scenario_id,
                                                  const std::string& nurse_id,
                                                  int age, const std::string& sex,
                                                  const std::string& diagnosis,
                                                  int hr, int rr, int spo2,
                                                  int bp_sys, int bp_dia, float temp) {
    // Free-form patient fields (diagnosis, sex) can't safely fit the flat
    // "key=value" convention record_nurse_action etc use -- nested JSON
    // instead, relying on TrainingEvent::to_json_string's json::escape fix
    // for the outer event_data string.
    json patient = json::object();
    patient["nurse_id"] = json(nurse_id);
    patient["age"] = json(static_cast<double>(age));
    patient["sex"] = json(sex);
    patient["diagnosis"] = json(diagnosis);
    patient["hr"] = json(static_cast<double>(hr));
    patient["rr"] = json(static_cast<double>(rr));
    patient["spo2"] = json(static_cast<double>(spo2));
    patient["bp_sys"] = json(static_cast<double>(bp_sys));
    patient["bp_dia"] = json(static_cast<double>(bp_dia));
    patient["temp"] = json(static_cast<double>(temp));

    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "SESSION_START";
    evt.elapsed_seconds = 0;
    evt.event_data = patient.dump();

    append_event(evt);
}

void TrainingAnalyticsStore::record_note_drafted(const std::string& session_id,
                                                const std::string& scenario_id,
                                                int elapsed_sec,
                                                const std::string& ai_draft_content) {
    json data = json::object();
    data["content"] = json(ai_draft_content);

    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "NOTE_DRAFTED";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = data.dump();

    append_event(evt);
}

void TrainingAnalyticsStore::record_note_signed(const std::string& session_id,
                                               const std::string& scenario_id,
                                               const std::string& nurse_id,
                                               int elapsed_sec,
                                               const std::string& final_content,
                                               bool was_edited) {
    json data = json::object();
    data["nurse_id"] = json(nurse_id);
    data["content"] = json(final_content);
    data["was_edited"] = json(was_edited);

    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "NOTE_SIGNED";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = data.dump();

    append_event(evt);
}

void TrainingAnalyticsStore::record_nurse_action(const std::string& session_id,
                                                const std::string& action,
                                                const std::string& nurse_id,
                                                int elapsed_sec,
                                                bool was_timely,
                                                const std::string& grade,
                                                float delta) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = "";
    evt.timestamp = std::time(nullptr);
    evt.event_type = "NURSE_ACTION";
    evt.elapsed_seconds = elapsed_sec;
    evt.event_data = "action=" + action + " nurse_id=" + nurse_id + " timely=" + std::string(was_timely ? "true" : "false")
                    + " grade=" + grade + " delta=" + std::to_string(delta);

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
                                                    int total_duration,
                                                    float final_score) {
    TrainingEvent evt;
    evt.event_id = generate_event_id();
    evt.session_id = session_id;
    evt.scenario_id = scenario_id;
    evt.timestamp = std::time(nullptr);
    evt.event_type = "SESSION_COMPLETE";
    evt.elapsed_seconds = total_duration;
    // final_score is the same 0-100 running score handle_training_end shows
    // the nurse on their own debrief (graded per-action, e.g. -5% for a
    // premature intervention) -- a different, more meaningful number than
    // scenario_effectiveness_score below, which is only a coarse proxy
    // derived from missed_critical_windows. Persisting the real score here
    // so anyone reading this session back later (a nurse's own report, or
    // an instructor's cohort view) sees the same number the nurse did,
    // instead of the proxy silently standing in for it.
    evt.event_data = "outcome=" + outcome + " nurse_id=" + nurse_id + " duration=" + std::to_string(total_duration)
                    + " score=" + std::to_string(final_score);

    append_event(evt);
}

std::vector<TrainingEvent> TrainingAnalyticsStore::get_session_events(const std::string& session_id) {
    std::vector<TrainingEvent> events;
    std::string master_log = store_path_ + "/training_events.ndjson";

    std::ifstream infile(master_log);
    if (!infile.is_open()) return events;

    // Each event is written as one pretty-printed (multi-line) JSON object
    // per append_event() -- to_json_string() emits "{\n...\n}\n" -- so a
    // record is everything from a line that's just "{" to the matching "}".
    // Previously this matched the session_id via a raw substring search on
    // single LINES and then only ever set event_type/session_id on the
    // result, discarding event_id/scenario_id/timestamp/event_data/
    // elapsed_seconds entirely -- every field a debrief or metrics
    // calculation actually needs.
    std::string line;
    std::string record;
    bool in_record = false;
    while (std::getline(infile, line)) {
        if (!in_record) {
            if (line == "{") { in_record = true; record = line + "\n"; }
            continue;
        }
        record += line + "\n";
        if (line == "}") {
            in_record = false;
            try {
                json obj = json::parse(record);
                if (obj.contains("session_id") && obj.at("session_id").as_string("") == session_id) {
                    TrainingEvent evt;
                    evt.event_id = obj.contains("event_id") ? obj.at("event_id").as_string("") : "";
                    evt.session_id = session_id;
                    evt.scenario_id = obj.contains("scenario_id") ? obj.at("scenario_id").as_string("") : "";
                    evt.timestamp = obj.contains("timestamp") ? static_cast<std::time_t>(obj.at("timestamp").as_long(0)) : 0;
                    evt.event_type = obj.contains("event_type") ? obj.at("event_type").as_string("") : "";
                    evt.event_data = obj.contains("event_data") ? obj.at("event_data").as_string("") : "";
                    evt.elapsed_seconds = obj.contains("elapsed_seconds") ? static_cast<int>(obj.at("elapsed_seconds").as_long(0)) : 0;
                    events.push_back(evt);
                }
            } catch (const std::exception&) {
                // Malformed/partial record (e.g. a truncated write from a
                // prior crash) -- skip it rather than losing the rest of the log.
            }
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
    // No interaction in this product lets a nurse "accept" or "reject" an
    // AI recommendation (recommendations are informational only -- see
    // handle_training_status's pending_recommendations, which the frontend
    // doesn't even render), so there's no real acceptance event to count.
    // Previously computed as accepted/recommendations with `accepted` never
    // incremented anywhere -- always 0, dressed up as a real rate. Left
    // unset (0) here rather than fabricating a number for an interaction
    // that doesn't exist.
    metrics.ai_recommendation_acceptance_rate = 0.0f;
    metrics.scenario_effectiveness_score = 0.0f;
    metrics.final_score = 0.0f;

    auto events = get_session_events(session_id);

    for (const auto& evt : events) {
        if (evt.event_type == "NURSE_ACTION") {
            metrics.total_actions++;
            // was_timely doubles as "was_correct" at the call site (see
            // record_nurse_action's caller in http_server.cpp) -- this was
            // computed and stored correctly but never read back, so
            // incorrect_actions was always 0 regardless of performance.
            if (parse_event_data_field(evt.event_data, "timely") == "false") {
                metrics.incorrect_actions++;
            }
        } else if (evt.event_type == "FAILURE_TRIGGERED") {
            metrics.missed_critical_windows++;
        } else if (evt.event_type == "SESSION_COMPLETE") {
            // Was never populated at all -- generate_session_report() below
            // printed empty/zero for scenario_id, nurse_id, and
            // total_duration_seconds on every report.
            metrics.scenario_id = evt.scenario_id;
            metrics.outcome = parse_event_data_field(evt.event_data, "outcome");
            metrics.nurse_id = parse_event_data_field(evt.event_data, "nurse_id");
            metrics.total_duration_seconds = evt.elapsed_seconds;
            std::string score_str = parse_event_data_field(evt.event_data, "score");
            if (!score_str.empty()) {
                try {
                    metrics.final_score = std::stof(score_str);
                } catch (const std::exception&) {
                    metrics.final_score = 0.0f;
                }
            }
        }
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

    // Was `pos + 16` into `"session_id": "` (a 15-character literal), so
    // every session_id this returned was missing its first character --
    // session_exists() (and therefore handle_training_report's 404 check)
    // could never actually match a real session_id.
    std::string line;
    std::string record;
    bool in_record = false;
    while (std::getline(infile, line)) {
        if (!in_record) {
            if (line == "{") { in_record = true; record = line + "\n"; }
            continue;
        }
        record += line + "\n";
        if (line == "}") {
            in_record = false;
            try {
                json obj = json::parse(record);
                std::string session_id = obj.contains("session_id") ? obj.at("session_id").as_string("") : "";
                if (!session_id.empty() && std::find(sessions.begin(), sessions.end(), session_id) == sessions.end()) {
                    sessions.push_back(session_id);
                }
            } catch (const std::exception&) {
                // skip malformed/partial record
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
    report << "  \"final_score\": " << metrics.final_score << ",\n";
    report << "  \"outcome\": \"" << metrics.outcome << "\"\n";
    report << "}\n";
    
    return report.str();
}
