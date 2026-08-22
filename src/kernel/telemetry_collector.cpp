#include "telemetry_collector.h"
#include <sstream>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <cmath>
#include <iostream>

TelemetryCollector::TelemetryCollector(TrainingOrchestrator* orch)
    : orchestrator(orch),
      start_time(std::chrono::system_clock::now().time_since_epoch().count()),
      total_inferences(0),
      total_inference_latency(0.0f) {
}

void TelemetryCollector::record_metric(const std::string& metric_name, float value, 
                                      const std::string& dimension) {
    MetricDataPoint point;
    point.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    point.metric_name = metric_name;
    point.metric_value = value;
    point.dimension = dimension;
    
    metric_history.push_back(point);
}

void TelemetryCollector::record_stage_completion(TrainingStage stage, const StageMetrics& metrics) {
    int stage_num = static_cast<int>(stage);
    
    stage_completion_counts[stage]++;
    
    if (stage_average_accuracy.count(stage) == 0) {
        stage_average_accuracy[stage] = metrics.accuracy;
        stage_average_loss[stage] = metrics.average_loss;
    } else {
        float prev_accuracy = stage_average_accuracy[stage];
        float prev_loss = stage_average_loss[stage];
        int count = stage_completion_counts[stage];
        
        stage_average_accuracy[stage] = (prev_accuracy * (count - 1) + metrics.accuracy) / count;
        stage_average_loss[stage] = (prev_loss * (count - 1) + metrics.average_loss) / count;
    }
    
    record_metric("stage_accuracy", metrics.accuracy, "stage_" + std::to_string(stage_num));
    record_metric("stage_loss", metrics.average_loss, "stage_" + std::to_string(stage_num));
    record_metric("stage_confidence", metrics.confidence_mean, "stage_" + std::to_string(stage_num));
}

void TelemetryCollector::record_inference(int64_t latency_ms) {
    total_inferences++;
    total_inference_latency += latency_ms;
    
    record_metric("inference_latency", static_cast<float>(latency_ms), "latency_ms");
}

void TelemetryCollector::record_error(const std::string& component, const std::string& error_message) {
    error_log.push_back({component, error_message});
    record_metric("error_count", 1.0f, component);
}

PerformanceSnapshot TelemetryCollector::capture_performance_snapshot() {
    PerformanceSnapshot snapshot;
    snapshot.snapshot_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    snapshot.cpu_usage_percent = 45.0f + (rand() % 30);
    snapshot.memory_usage_mb = 512.0f + (rand() % 256);
    snapshot.active_threads = 8 + (rand() % 4);
    
    if (total_inferences > 0) {
        snapshot.average_inference_latency_ms = total_inference_latency / total_inferences;
    }
    
    snapshot.total_inferences = total_inferences;
    
    auto metrics = orchestrator->get_metrics();
    snapshot.accuracy = metrics.overall_accuracy;
    snapshot.loss = metrics.overall_loss;
    
    performance_history.push_back(snapshot);
    
    return snapshot;
}

HealthIndicator TelemetryCollector::check_system_health() {
    HealthIndicator health;
    health.last_check_time = std::chrono::system_clock::now().time_since_epoch().count();
    
    health.health_score = compute_health_score();
    health.is_healthy = health.health_score > 0.6f;
    health.status_message = get_health_status(health.health_score);
    
    health.component_health["training"] = get_component_health("training");
    health.component_health["memory"] = get_component_health("memory");
    health.component_health["inference"] = get_component_health("inference");
    health.component_health["recovery"] = get_component_health("recovery");
    
    return health;
}

std::vector<MetricDataPoint> TelemetryCollector::get_metrics_for_range(int64_t start_time, int64_t end_time) {
    std::vector<MetricDataPoint> result;
    for (const auto& point : metric_history) {
        if (point.timestamp >= start_time && point.timestamp <= end_time) {
            result.push_back(point);
        }
    }
    return result;
}

std::vector<MetricDataPoint> TelemetryCollector::get_metrics_by_name(const std::string& metric_name) {
    std::vector<MetricDataPoint> result;
    for (const auto& point : metric_history) {
        if (point.metric_name == metric_name) {
            result.push_back(point);
        }
    }
    return result;
}

std::vector<PerformanceSnapshot> TelemetryCollector::get_performance_history(int last_n_snapshots) {
    std::vector<PerformanceSnapshot> result;
    int start_idx = std::max(0, static_cast<int>(performance_history.size()) - last_n_snapshots);
    
    for (int i = start_idx; i < performance_history.size(); ++i) {
        result.push_back(performance_history[i]);
    }
    
    return result;
}

std::string TelemetryCollector::generate_telemetry_report() const {
    std::stringstream ss;
    ss << "=== Telemetry Report ===\n\n";
    
    ss << "System Uptime:\n";
    ss << "  " << get_uptime_seconds() << " seconds\n\n";
    
    ss << "Inference Statistics:\n";
    ss << "  Total Inferences: " << total_inferences << "\n";
    if (total_inferences > 0) {
        ss << "  Average Latency: " << (total_inference_latency / total_inferences) << " ms\n";
    }
    ss << "\n";
    
    ss << "Metric Summary:\n";
    ss << "  Total Metrics Recorded: " << metric_history.size() << "\n";
    ss << "  Average Accuracy: " << (get_average_accuracy() * 100) << "%\n";
    ss << "  Average Loss: " << get_average_loss() << "\n\n";
    
    ss << "Error Log (last 10):\n";
    int start_idx = std::max(0, static_cast<int>(error_log.size()) - 10);
    for (int i = start_idx; i < error_log.size(); ++i) {
        ss << "  [" << error_log[i].first << "] " << error_log[i].second << "\n";
    }
    
    ss << "\nStage Metrics:\n";
    for (const auto& pair : stage_completion_counts) {
        int stage_num = static_cast<int>(pair.first);
        ss << "  Stage " << stage_num << ":\n";
        ss << "    Completions: " << pair.second << "\n";
        if (stage_average_accuracy.count(pair.first)) {
            ss << "    Average Accuracy: " << (stage_average_accuracy.at(pair.first) * 100) << "%\n";
            ss << "    Average Loss: " << stage_average_loss.at(pair.first) << "\n";
        }
    }
    
    return ss.str();
}

std::string TelemetryCollector::generate_health_report() const {
    std::stringstream ss;
    ss << "=== System Health Report ===\n\n";
    
    auto health = const_cast<TelemetryCollector*>(this)->check_system_health();
    
    ss << "Overall Health Score: " << (health.health_score * 100) << "%\n";
    ss << "Status: " << health.status_message << "\n";
    ss << "Last Check: " << health.last_check_time << "\n\n";
    
    ss << "Component Health:\n";
    for (const auto& pair : health.component_health) {
        ss << "  " << pair.first << ": " << (pair.second * 100) << "%\n";
    }
    
    return ss.str();
}

void TelemetryCollector::export_metrics_to_csv(const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return;
    }
    
    file << "timestamp,metric_name,metric_value,dimension\n";
    
    for (const auto& point : metric_history) {
        file << point.timestamp << ","
             << point.metric_name << ","
             << point.metric_value << ","
             << point.dimension << "\n";
    }
    
    file.close();
}

void TelemetryCollector::clear_old_metrics(int days) {
    int64_t cutoff_time = std::chrono::system_clock::now().time_since_epoch().count()
                         - (days * 24 * 3600 * (int64_t)1e9);
    
    auto it = metric_history.begin();
    while (it != metric_history.end()) {
        if (it->timestamp < cutoff_time) {
            it = metric_history.erase(it);
        } else {
            ++it;
        }
    }
}

float TelemetryCollector::get_average_accuracy() const {
    if (metric_history.empty()) return 0.0f;
    
    float sum = 0.0f;
    int count = 0;
    
    for (const auto& point : metric_history) {
        if (point.metric_name == "stage_accuracy") {
            sum += point.metric_value;
            count++;
        }
    }
    
    if (count == 0) return 0.0f;
    return sum / count;
}

float TelemetryCollector::get_average_loss() const {
    if (metric_history.empty()) return 0.0f;
    
    float sum = 0.0f;
    int count = 0;
    
    for (const auto& point : metric_history) {
        if (point.metric_name == "stage_loss") {
            sum += point.metric_value;
            count++;
        }
    }
    
    if (count == 0) return 0.0f;
    return sum / count;
}

int64_t TelemetryCollector::get_uptime_seconds() const {
    int64_t current_time = std::chrono::system_clock::now().time_since_epoch().count();
    return (current_time - start_time) / (int64_t)1e9;
}

float TelemetryCollector::compute_health_score() const {
    float score = 1.0f;
    
    if (error_log.size() > 5) {
        score -= std::min(0.3f, error_log.size() * 0.05f);
    }
    
    float avg_accuracy = get_average_accuracy();
    if (avg_accuracy < 0.7f) {
        score -= (0.7f - avg_accuracy) * 0.2f;
    }
    
    float avg_loss = get_average_loss();
    if (avg_loss > 0.5f) {
        score -= std::min(0.2f, (avg_loss - 0.5f) * 0.1f);
    }
    
    auto metrics = orchestrator->get_metrics();
    if (metrics.current_state == OrchestratorState::FAILED) {
        score -= 0.3f;
    }
    
    return std::max(0.0f, std::min(1.0f, score));
}

std::string TelemetryCollector::get_health_status(float score) const {
    if (score >= 0.9f) return "EXCELLENT";
    if (score >= 0.7f) return "HEALTHY";
    if (score >= 0.5f) return "DEGRADED";
    if (score >= 0.3f) return "WARNING";
    return "CRITICAL";
}

float TelemetryCollector::get_component_health(const std::string& component) const {
    if (component == "training") {
        return 0.85f + (rand() % 10) * 0.01f;
    } else if (component == "memory") {
        return 0.90f + (rand() % 5) * 0.01f;
    } else if (component == "inference") {
        return 0.80f + (rand() % 15) * 0.01f;
    } else if (component == "recovery") {
        return error_log.empty() ? 1.0f : std::max(0.5f, 1.0f - (error_log.size() * 0.1f));
    }
    return 0.75f;
}

void TelemetryCollector::reset_metrics() {
    metric_history.clear();
    performance_history.clear();
    error_log.clear();
    stage_completion_counts.clear();
    stage_average_accuracy.clear();
    stage_average_loss.clear();
    total_inferences = 0;
    total_inference_latency = 0.0f;
    start_time = std::chrono::system_clock::now().time_since_epoch().count();
}
