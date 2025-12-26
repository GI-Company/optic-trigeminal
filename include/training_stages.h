#pragma once

#include "types.h"
#include "data_pipeline.h"
#include "vfs_manager.h"
#include "neural_components.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

enum class TrainingStage {
    STAGE_0_BASE_KNOWLEDGE = 0,
    STAGE_1_AGENTIC_ORCHESTRATION = 1,
    STAGE_2_LONG_HORIZON_PLANNING = 2,
    STAGE_3_SELF_KNOWLEDGE = 3,
    STAGE_4_MULTIMODAL = 4
};

struct StageMetrics {
    TrainingStage stage;
    int total_examples;
    int examples_processed;
    float average_loss;
    float accuracy;
    float confidence_mean;
    int64_t stage_start_time;
    int64_t stage_end_time;
    std::string version;
    
    StageMetrics() : examples_processed(0), average_loss(0.0f), accuracy(0.0f),
                     confidence_mean(0.0f), stage_start_time(0), stage_end_time(0) {}
};

struct CheckpointData {
    std::string checkpoint_id;
    TrainingStage stage;
    std::string version;
    std::vector<float> model_weights;
    std::vector<float> model_biases;
    StageMetrics metrics;
    std::string content_hash;
    int64_t created_at;
    bool verified;
    
    CheckpointData() : created_at(0), verified(false) {}
};

class TrainingController {
public:
    TrainingController(VFSManager* vfs, DataPipeline* pipeline);
    
    void initialize_training();
    
    void run_stage_0_base_knowledge(const std::vector<ProcessedDataRecord>& stem_qa_data,
                                   const std::vector<ProcessedDataRecord>& coding_data);
    
    void run_stage_1_agentic_orchestration(const std::vector<ProcessedDataRecord>& orchestration_data);
    
    void run_stage_2_long_horizon_planning(const std::vector<ProcessedDataRecord>& planning_data);
    
    void run_stage_3_self_knowledge(const std::vector<ProcessedDataRecord>& self_knowledge_data);
    
    void run_stage_4_multimodal(const std::vector<ProcessedDataRecord>& multimodal_data);
    
    CheckpointData create_checkpoint(TrainingStage stage, const std::string& version);
    
    bool verify_checkpoint(const CheckpointData& checkpoint);
    
    bool load_checkpoint(const std::string& checkpoint_id);
    
    std::vector<CheckpointData> list_checkpoints_for_stage(TrainingStage stage);
    
    StageMetrics get_stage_metrics(TrainingStage stage) const;
    
    std::string generate_training_report();
    
    void finalize_training();
    
    bool all_stages_complete() const;
    
private:
    VFSManager* vfs_manager;
    DataPipeline* data_pipeline;
    
    std::map<TrainingStage, StageMetrics> stage_metrics;
    std::vector<CheckpointData> checkpoints;
    std::map<TrainingStage, bool> stage_completion;
    
    std::string current_version;
    int64_t training_start_time;
    
    float simulate_training_epoch(const std::vector<ProcessedDataRecord>& data);
    float compute_accuracy(const std::vector<ProcessedDataRecord>& data);
    std::string compute_checkpoint_hash(const std::vector<float>& weights);
    std::vector<float> extract_embeddings_as_weights(const std::vector<Embedding>& embeddings);
    
    // Neural Components
    std::unique_ptr<StemClassifier> stem_classifier;
    std::unique_ptr<OpticEmbedder> optic_embedder;
    std::unique_ptr<VTAPredictor> vta_predictor;
    std::unique_ptr<OpticTrigeminal> optic_trigeminal;
    std::unique_ptr<SequenceDecoder> sequence_decoder;
};
