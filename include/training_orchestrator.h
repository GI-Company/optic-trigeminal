#pragma once

#include "training_stages.h"
#include "data_pipeline.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

enum class OrchestratorState {
    IDLE,
    RUNNING,
    PAUSED,
    COMPLETED,
    FAILED
};

struct StageDataMapping {
    TrainingStage stage;
    std::vector<DataSourceType> data_sources;
    int min_required_records;
    bool completed;
    int64_t started_at;
    int64_t completed_at;
};

struct OrchestrationMetrics {
    OrchestratorState current_state;
    int total_stages_completed;
    int total_records_processed;
    float overall_accuracy;
    float overall_loss;
    int64_t orchestration_start_time;
    int64_t orchestration_end_time;
    std::string status_message;
};

class TrainingOrchestrator {
public:
    TrainingOrchestrator(TrainingController* controller, DataPipeline* pipeline, VFSManager* vfs);
    
    void initialize();
    
    void execute_training_pipeline();
    
    void execute_stage(TrainingStage stage);
    
    bool can_execute_stage(TrainingStage stage) const;
    
    std::vector<ProcessedDataRecord> get_data_for_stage(TrainingStage stage);
    
    void pause_training();
    
    void resume_training();
    
    void reset_orchestration();
    
    OrchestratorState get_current_state() const { return current_state; }
    
    OrchestrationMetrics get_metrics() const { return metrics; }
    
    std::string get_status_report() const;
    
    bool is_training_complete() const;
    
    void log_stage_execution(TrainingStage stage, const std::string& status);
    
    void ingest_training_examples(const std::vector<TrainingExample>& examples);
    
private:
    TrainingController* training_controller;
    DataPipeline* data_pipeline;
    VFSManager* vfs_manager;
    
    OrchestratorState current_state;
    OrchestrationMetrics metrics;
    std::map<TrainingStage, StageDataMapping> stage_mappings;
    std::vector<std::string> execution_log;
    
    void setup_stage_mappings();
    
    void validate_data_availability();
    
    float aggregate_stage_metrics();
    
    std::string state_to_string(OrchestratorState state) const;
    
    DataSourceType map_domain_to_source_type(const std::string& domain) const;
    
    DataSourceType infer_domain_from_content(const std::string& input_text, 
                                            const std::string& output_text) const;
    
    RawDataRecord convert_example_to_record(const TrainingExample& example, int record_index) const;

    // New methods for holdout validation
    void split_data_for_holdout(const std::vector<ProcessedDataRecord>& stage_data,
                                std::vector<ProcessedDataRecord>& training_set,
                                std::vector<ProcessedDataRecord>& holdout_set,
                                int stage_id);

    void create_holdout_artifacts(const std::vector<ProcessedDataRecord>& holdout_set,
                                  int stage_id);
};
