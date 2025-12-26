#pragma once

#include "training_stages.h"
#include "training_orchestrator.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>

struct MetricDataPoint {
    int64_t timestamp;
    std::string metric_name;
    float metric_value;
    std::string dimension;
    
    MetricDataPoint() : timestamp(0), metric_value(0.0f) {}
};

struct PerformanceSnapshot {
    int64_t snapshot_time;
    float cpu_usage_percent;
    float memory_usage_mb;
    int active_threads;
    float average_inference_latency_ms;
    int total_inferences;
    float accuracy;
    float loss;
    
    PerformanceSnapshot() : snapshot_time(0), cpu_usage_percent(0.0f), memory_usage_mb(0.0f),
                           active_threads(0), average_inference_latency_ms(0.0f),
                           total_inferences(0), accuracy(0.0f), loss(0.0f) {}
};

struct HealthIndicator {
    bool is_healthy;
    std::string status_message;
    float health_score;
    int64_t last_check_time;
    std::map<std::string, float> component_health;
    
    HealthIndicator() : is_healthy(true), health_score(1.0f), last_check_time(0) {}
};

class TelemetryCollector {
public:
    TelemetryCollector(TrainingOrchestrator* orchestrator);
    
    void record_metric(const std::string& metric_name, float value, const std::string& dimension = "");
    
    void record_stage_completion(TrainingStage stage, const StageMetrics& metrics);
    
    void record_inference(int64_t latency_ms);
    
    void record_error(const std::string& component, const std::string& error_message);
    
    PerformanceSnapshot capture_performance_snapshot();
    
    HealthIndicator check_system_health();
    
    std::vector<MetricDataPoint> get_metrics_for_range(int64_t start_time, int64_t end_time);
    
    std::vector<MetricDataPoint> get_metrics_by_name(const std::string& metric_name);
    
    std::vector<PerformanceSnapshot> get_performance_history(int last_n_snapshots = 100);
    
    std::string generate_telemetry_report() const;
    
    std::string generate_health_report() const;
    
    void export_metrics_to_csv(const std::string& filename);
    
    void clear_old_metrics(int days = 30);
    
    float get_average_accuracy() const;
    
    float get_average_loss() const;
    
    int get_total_inferences() const { return total_inferences; }
    
    int64_t get_uptime_seconds() const;
    
    void reset_metrics();
    
private:
    TrainingOrchestrator* orchestrator;
    
    std::vector<MetricDataPoint> metric_history;
    std::vector<PerformanceSnapshot> performance_history;
    std::vector<std::pair<std::string, std::string>> error_log;
    
    int64_t start_time;
    int total_inferences;
    float total_inference_latency;
    
    std::map<TrainingStage, int> stage_completion_counts;
    std::map<TrainingStage, float> stage_average_accuracy;
    std::map<TrainingStage, float> stage_average_loss;
    
    float compute_health_score() const;
    
    std::string get_health_status(float score) const;
    
    float get_component_health(const std::string& component) const;
};
