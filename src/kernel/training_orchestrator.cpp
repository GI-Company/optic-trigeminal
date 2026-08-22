#include "training_orchestrator.h"
#include "holdout_validation_utils.h"
#include <chrono>
#include <sstream>
#include <iostream>
#include <algorithm>

TrainingOrchestrator::TrainingOrchestrator(TrainingController* controller, 
                                         DataPipeline* pipeline,
                                         VFSManager* vfs)
    : training_controller(controller), 
      data_pipeline(pipeline), 
      vfs_manager(vfs),
      current_state(OrchestratorState::IDLE) {
    
    metrics.current_state = OrchestratorState::IDLE;
    metrics.total_stages_completed = 0;
    metrics.total_records_processed = 0;
    metrics.overall_accuracy = 0.0f;
    metrics.overall_loss = 0.0f;
    metrics.orchestration_start_time = 0;
    metrics.orchestration_end_time = 0;
    metrics.status_message = "Initialized";
}

void TrainingOrchestrator::initialize() {
    current_state = OrchestratorState::IDLE;
    metrics.current_state = OrchestratorState::IDLE;
    setup_stage_mappings();
    training_controller->initialize_training();
    log_stage_execution(TrainingStage::STAGE_0_BASE_KNOWLEDGE, "Orchestrator initialized");
}

void TrainingOrchestrator::setup_stage_mappings() {
    stage_mappings.clear();
    
    StageDataMapping stage0;
    stage0.stage = TrainingStage::STAGE_0_BASE_KNOWLEDGE;
    stage0.data_sources = {DataSourceType::STEM_QA, DataSourceType::CODING_FUNDAMENTALS};
    stage0.min_required_records = 10;
    stage0.completed = false;
    stage_mappings[TrainingStage::STAGE_0_BASE_KNOWLEDGE] = stage0;
    
    StageDataMapping stage1;
    stage1.stage = TrainingStage::STAGE_1_AGENTIC_ORCHESTRATION;
    stage1.data_sources = {DataSourceType::TOOLING_ORCHESTRATION};
    stage1.min_required_records = 5;
    stage1.completed = false;
    stage_mappings[TrainingStage::STAGE_1_AGENTIC_ORCHESTRATION] = stage1;
    
    StageDataMapping stage2;
    stage2.stage = TrainingStage::STAGE_2_LONG_HORIZON_PLANNING;
    stage2.data_sources = {DataSourceType::LOGIC_REASONING};
    stage2.min_required_records = 5;
    stage2.completed = false;
    stage_mappings[TrainingStage::STAGE_2_LONG_HORIZON_PLANNING] = stage2;
    
    StageDataMapping stage3;
    stage3.stage = TrainingStage::STAGE_3_SELF_KNOWLEDGE;
    stage3.data_sources = {DataSourceType::SELF_KNOWLEDGE};
    stage3.min_required_records = 3;
    stage3.completed = false;
    stage_mappings[TrainingStage::STAGE_3_SELF_KNOWLEDGE] = stage3;
    
    StageDataMapping stage4;
    stage4.stage = TrainingStage::STAGE_4_MULTIMODAL;
    stage4.data_sources = {DataSourceType::MULTIMODAL};
    stage4.min_required_records = 5;
    stage4.completed = false;
    stage_mappings[TrainingStage::STAGE_4_MULTIMODAL] = stage4;
}

std::vector<ProcessedDataRecord> TrainingOrchestrator::get_data_for_stage(TrainingStage stage) {
    std::vector<ProcessedDataRecord> combined_data;
    
    if (stage_mappings.count(stage) == 0) {
        return combined_data;
    }
    
    const auto& mapping = stage_mappings[stage];
    for (DataSourceType source : mapping.data_sources) {
        auto records = data_pipeline->get_processed_records(source);
        combined_data.insert(combined_data.end(), records.begin(), records.end());
    }
    
    return combined_data;
}

bool TrainingOrchestrator::can_execute_stage(TrainingStage stage) const {
    if (stage_mappings.count(stage) == 0) {
        return false;
    }
    
    const auto& mapping = stage_mappings.at(stage);
    
    if (mapping.completed) {
        return false;
    }
    
    int total_records = 0;
    for (DataSourceType source : mapping.data_sources) {
        auto records = data_pipeline->get_processed_records(source);
        total_records += records.size();
    }
    
    return total_records >= mapping.min_required_records;
}

void TrainingOrchestrator::execute_training_pipeline() {
    if (current_state == OrchestratorState::RUNNING) {
        metrics.status_message = "Training already in progress";
        return;
    }
    
    current_state = OrchestratorState::RUNNING;
    metrics.current_state = OrchestratorState::RUNNING;
    metrics.orchestration_start_time = std::chrono::system_clock::now().time_since_epoch().count();
    log_stage_execution(TrainingStage::STAGE_0_BASE_KNOWLEDGE, "Starting training pipeline");
    
    try {
        validate_data_availability();
        
        for (int i = 0; i < 5; ++i) {
            TrainingStage stage = static_cast<TrainingStage>(i);
            if (can_execute_stage(stage)) {
                execute_stage(stage);
            }
        }
        
        if (training_controller->all_stages_complete()) {
            current_state = OrchestratorState::COMPLETED;
            metrics.current_state = OrchestratorState::COMPLETED;
            metrics.orchestration_end_time = std::chrono::system_clock::now().time_since_epoch().count();
            training_controller->finalize_training();
            metrics.status_message = "Training pipeline completed successfully";
            log_stage_execution(TrainingStage::STAGE_4_MULTIMODAL, "Training pipeline completed");
        } else {
            current_state = OrchestratorState::IDLE;
            metrics.current_state = OrchestratorState::IDLE;
            metrics.status_message = "Training pipeline partially completed";
        }
    } catch (const std::exception& e) {
        current_state = OrchestratorState::FAILED;
        metrics.current_state = OrchestratorState::FAILED;
        metrics.status_message = std::string("Training pipeline failed: ") + e.what();
        log_stage_execution(TrainingStage::STAGE_0_BASE_KNOWLEDGE, metrics.status_message);
    }
}

void TrainingOrchestrator::execute_stage(TrainingStage stage) {
    if (!can_execute_stage(stage)) {
        log_stage_execution(stage, "Cannot execute - insufficient data");
        return;
    }
    
    auto& mapping = stage_mappings[stage];
    mapping.started_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    std::string stage_name = "Stage " + std::to_string(static_cast<int>(stage));
    log_stage_execution(stage, stage_name + " - execution started");
    
    // Get all data for the current stage
    auto stage_data = get_data_for_stage(stage);
    
    std::vector<ProcessedDataRecord> training_set;
    std::vector<ProcessedDataRecord> holdout_set;

    // Split data into training and holdout sets deterministically
    split_data_for_holdout(stage_data, training_set, holdout_set, static_cast<int>(stage));

    metrics.total_records_processed += training_set.size(); // Only count training records for processed metric
    
    try {
        switch (stage) {
            case TrainingStage::STAGE_0_BASE_KNOWLEDGE: {
                std::vector<ProcessedDataRecord> stage0_stem_training_data;
                std::vector<ProcessedDataRecord> stage0_coding_training_data;
                for (const auto& record : training_set) {
                    if (record.source_type == DataSourceType::STEM_QA) {
                        stage0_stem_training_data.push_back(record);
                    } else if (record.source_type == DataSourceType::CODING_FUNDAMENTALS) {
                        stage0_coding_training_data.push_back(record);
                    }
                }
                training_controller->run_stage_0_base_knowledge(stage0_stem_training_data, stage0_coding_training_data);
                break;
            }
            case TrainingStage::STAGE_1_AGENTIC_ORCHESTRATION: {
                std::vector<ProcessedDataRecord> stage1_orch_training_data;
                for (const auto& record : training_set) {
                    if (record.source_type == DataSourceType::TOOLING_ORCHESTRATION) {
                        stage1_orch_training_data.push_back(record);
                    }
                }
                training_controller->run_stage_1_agentic_orchestration(stage1_orch_training_data);
                break;
            }
            case TrainingStage::STAGE_2_LONG_HORIZON_PLANNING: {
                std::vector<ProcessedDataRecord> stage2_planning_training_data;
                for (const auto& record : training_set) {
                    if (record.source_type == DataSourceType::LOGIC_REASONING) {
                        stage2_planning_training_data.push_back(record);
                    }
                }
                training_controller->run_stage_2_long_horizon_planning(stage2_planning_training_data);
                break;
            }
            case TrainingStage::STAGE_3_SELF_KNOWLEDGE: {
                std::vector<ProcessedDataRecord> stage3_self_training_data;
                for (const auto& record : training_set) {
                    if (record.source_type == DataSourceType::SELF_KNOWLEDGE) {
                        stage3_self_training_data.push_back(record);
                    }
                }
                training_controller->run_stage_3_self_knowledge(stage3_self_training_data);
                break;
            }
            case TrainingStage::STAGE_4_MULTIMODAL: {
                std::vector<ProcessedDataRecord> stage4_mm_training_data;
                for (const auto& record : training_set) {
                    if (record.source_type == DataSourceType::MULTIMODAL) {
                        stage4_mm_training_data.push_back(record);
                    }
                }
                training_controller->run_stage_4_multimodal(stage4_mm_training_data);
                break;
            }
        }
        
        // Create holdout artifacts after training
        create_holdout_artifacts(holdout_set, static_cast<int>(stage));

        mapping.completed = true;
        mapping.completed_at = std::chrono::system_clock::now().time_since_epoch().count();
        metrics.total_stages_completed++;
        
        auto stage_metrics = training_controller->get_stage_metrics(stage);
        log_stage_execution(stage, stage_name + " - completed with accuracy: " + 
                          std::to_string(stage_metrics.accuracy * 100) + "%");
        
    } catch (const std::exception& e) {
        log_stage_execution(stage, stage_name + " - failed: " + std::string(e.what()));
        throw;
    }
}

void TrainingOrchestrator::pause_training() {
    if (current_state == OrchestratorState::RUNNING) {
        current_state = OrchestratorState::PAUSED;
        metrics.current_state = OrchestratorState::PAUSED;
        metrics.status_message = "Training paused";
        log_stage_execution(TrainingStage::STAGE_0_BASE_KNOWLEDGE, "Training paused by user");
    }
}

void TrainingOrchestrator::resume_training() {
    if (current_state == OrchestratorState::PAUSED) {
        current_state = OrchestratorState::RUNNING;
        metrics.current_state = OrchestratorState::RUNNING;
        metrics.status_message = "Training resumed";
        log_stage_execution(TrainingStage::STAGE_0_BASE_KNOWLEDGE, "Training resumed by user");
    }
}

void TrainingOrchestrator::reset_orchestration() {
    current_state = OrchestratorState::IDLE;
    metrics.current_state = OrchestratorState::IDLE;
    metrics.total_stages_completed = 0;
    metrics.total_records_processed = 0;
    metrics.overall_accuracy = 0.0f;
    metrics.overall_loss = 0.0f;
    execution_log.clear();
    setup_stage_mappings();
}

void TrainingOrchestrator::validate_data_availability() {
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        if (!can_execute_stage(stage)) {
            std::string warning = "Stage " + std::to_string(i) + " has insufficient data";
            log_stage_execution(stage, warning);
        }
    }
}

float TrainingOrchestrator::aggregate_stage_metrics() {
    float total_accuracy = 0.0f;
    int stages_with_metrics = 0;
    
    for (int i = 0; i < 5; ++i) {
        TrainingStage stage = static_cast<TrainingStage>(i);
        auto metrics = training_controller->get_stage_metrics(stage);
        if (metrics.accuracy > 0.0f) {
            total_accuracy += metrics.accuracy;
            stages_with_metrics++;
        }
    }
    
    if (stages_with_metrics > 0) {
        metrics.overall_accuracy = total_accuracy / stages_with_metrics;
    }
    
    return metrics.overall_accuracy;
}

void TrainingOrchestrator::log_stage_execution(TrainingStage stage, const std::string& status) {
    int64_t timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::stringstream ss;
    ss << "[" << timestamp << "] Stage " << static_cast<int>(stage) << ": " << status;
    execution_log.push_back(ss.str());
    std::cout << ss.str() << std::endl; // Output to CLI
    
    if (vfs_manager) {
        vfs_manager->checkpoint_module_state(
            "orchestrator_log",
            status,
            "v1.0.0"
        );
    }
}

std::string TrainingOrchestrator::state_to_string(OrchestratorState state) const {
    switch (state) {
        case OrchestratorState::IDLE: return "IDLE";
        case OrchestratorState::RUNNING: return "RUNNING";
        case OrchestratorState::PAUSED: return "PAUSED";
        case OrchestratorState::COMPLETED: return "COMPLETED";
        case OrchestratorState::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

std::string TrainingOrchestrator::get_status_report() const {
    std::stringstream ss;
    ss << "=== Training Orchestration Report ===\n";
    ss << "State: " << state_to_string(metrics.current_state) << "\n";
    ss << "Status: " << metrics.status_message << "\n";
    ss << "Stages Completed: " << metrics.total_stages_completed << " / 5\n";
    ss << "Records Processed: " << metrics.total_records_processed << "\n";
    ss << "Overall Accuracy: " << (metrics.overall_accuracy * 100) << "%\n";
    ss << "Overall Loss: " << metrics.overall_loss << "\n";
    
    if (metrics.orchestration_start_time > 0) {
        int64_t end_time = metrics.orchestration_end_time > 0 ? 
                          metrics.orchestration_end_time : 
                          std::chrono::system_clock::now().time_since_epoch().count();
        float duration = (end_time - metrics.orchestration_start_time) / 1e9f;
        ss << "Duration: " << duration << " seconds\n";
    }
    
    ss << "\nExecution Log (last 10 entries):\n";
    int start_idx = std::max(0, static_cast<int>(execution_log.size()) - 10);
    for (int i = start_idx; i < execution_log.size(); ++i) {
        ss << "  " << execution_log[i] << "\n";
    }
    
    return ss.str();
}

bool TrainingOrchestrator::is_training_complete() const {
    return current_state == OrchestratorState::COMPLETED && 
           metrics.total_stages_completed == 5;
}

void TrainingOrchestrator::ingest_training_examples(const std::vector<TrainingExample>& examples) {
    if (examples.empty()) {
        std::cout << "[TrainingOrchestrator] No examples to ingest\n";
        return;
    }
    
    std::cout << "[TrainingOrchestrator] Ingesting " << examples.size() << " training examples...\n";
    
    std::vector<RawDataRecord> raw_records;
    for (size_t i = 0; i < examples.size(); ++i) {
        raw_records.push_back(convert_example_to_record(examples[i], i));
    }
    
    data_pipeline->ingest_raw_records(raw_records);
    
    std::vector<ProcessedDataRecord> processed_output;
    data_pipeline->process_batch(processed_output);
    data_pipeline->deduplicate_records(0.95f);
    data_pipeline->extract_features_all();
    data_pipeline->validate_all_records();
    
    std::cout << "[TrainingOrchestrator] Successfully ingested " << raw_records.size() << " records\n";
    std::cout << "[TrainingOrchestrator] Total ingested: " << data_pipeline->get_total_ingested() << "\n";
    std::cout << "[TrainingOrchestrator] Total processed: " << data_pipeline->get_total_processed() << "\n";
}

DataSourceType TrainingOrchestrator::map_domain_to_source_type(const std::string& domain) const {
    if (domain.empty() || domain == "general" || domain == "generic") {
        return DataSourceType::GENERIC;
    }
    
    std::string lower_domain = domain;
    std::transform(lower_domain.begin(), lower_domain.end(), lower_domain.begin(), ::tolower);
    
    if (lower_domain.find("stem") != std::string::npos || 
        lower_domain.find("science") != std::string::npos ||
        lower_domain.find("math") != std::string::npos ||
        lower_domain.find("physics") != std::string::npos ||
        lower_domain.find("chemistry") != std::string::npos ||
        lower_domain.find("biology") != std::string::npos ||
        lower_domain.find("medical") != std::string::npos || // Medical
        lower_domain.find("medicine") != std::string::npos || // Medical
        lower_domain.find("health") != std::string::npos || // Medical
        lower_domain.find("clinical") != std::string::npos || // Medical
        lower_domain.find("research") != std::string::npos || // Research
        lower_domain.find("geology") != std::string::npos ||
        lower_domain.find("astronomy") != std::string::npos ||
        lower_domain.find("geography") != std::string::npos ||
        lower_domain.find("engineering") != std::string::npos) {
        return DataSourceType::STEM_QA;
    }
    
    if (lower_domain.find("coding") != std::string::npos ||
        lower_domain.find("programming") != std::string::npos ||
        lower_domain.find("algorithm") != std::string::npos ||
        lower_domain.find("ruby") != std::string::npos ||
        lower_domain.find("php") != std::string::npos ||
        lower_domain.find("python") != std::string::npos ||
        lower_domain.find("javascript") != std::string::npos ||
        lower_domain.find("java") != std::string::npos ||
        lower_domain.find("cpp") != std::string::npos ||
        lower_domain.find("c++") != std::string::npos ||
        lower_domain.find("code") != std::string::npos ||
        lower_domain.find("software") != std::string::npos ||
        lower_domain.find("edge") != std::string::npos || // Edge devices
        lower_domain.find("embedded") != std::string::npos || // Edge devices
        lower_domain.find("device") != std::string::npos || // Edge devices
        lower_domain.find("iot") != std::string::npos || // Edge devices
        lower_domain.find("development") != std::string::npos) {
        return DataSourceType::CODING_FUNDAMENTALS;
    }
    
    if (lower_domain.find("tool") != std::string::npos ||
        lower_domain.find("orchestration") != std::string::npos ||
        lower_domain.find("workflow") != std::string::npos ||
        lower_domain.find("task") != std::string::npos ||
        lower_domain.find("agent") != std::string::npos ||
        lower_domain.find("multi-step") != std::string::npos ||
        lower_domain.find("multistep") != std::string::npos ||
        lower_domain.find("execution") != std::string::npos ||
        lower_domain.find("action") != std::string::npos) {
        return DataSourceType::TOOLING_ORCHESTRATION;
    }
    
    if (lower_domain.find("logic") != std::string::npos ||
        lower_domain.find("reasoning") != std::string::npos ||
        lower_domain.find("deduction") != std::string::npos ||
        lower_domain.find("causality") != std::string::npos ||
        lower_domain.find("causal") != std::string::npos ||
        lower_domain.find("inference") != std::string::npos ||
        lower_domain.find("proof") != std::string::npos ||
        lower_domain.find("legal") != std::string::npos || // Legal
        lower_domain.find("court") != std::string::npos || // Legal
        lower_domain.find("law") != std::string::npos || // Legal
        lower_domain.find("contract") != std::string::npos || // Legal
        lower_domain.find("theorem") != std::string::npos) {
        return DataSourceType::LOGIC_REASONING;
    }
    
    if (lower_domain.find("self") != std::string::npos ||
        lower_domain.find("knowledge") != std::string::npos ||
        lower_domain.find("axon") != std::string::npos ||
        lower_domain.find("reflection") != std::string::npos ||
        lower_domain.find("model") != std::string::npos ||
        lower_domain.find("introspection") != std::string::npos ||
        lower_domain.find("meta") != std::string::npos ||
        lower_domain.find("ai") != std::string::npos) {
        return DataSourceType::SELF_KNOWLEDGE;
    }
    
    if (lower_domain.find("multi") != std::string::npos ||
        lower_domain.find("brainstorm") != std::string::npos ||
        lower_domain.find("image") != std::string::npos ||
        lower_domain.find("audio") != std::string::npos ||
        lower_domain.find("video") != std::string::npos ||
        lower_domain.find("visual") != std::string::npos ||
        lower_domain.find("modal") != std::string::npos ||
        lower_domain.find("synthesis") != std::string::npos) {
        return DataSourceType::MULTIMODAL;
    }
    
    return DataSourceType::GENERIC;
}

DataSourceType TrainingOrchestrator::infer_domain_from_content(const std::string& input_text,
                                                              const std::string& output_text) const {
    std::string combined = input_text + " " + output_text;
    std::transform(combined.begin(), combined.end(), combined.begin(), ::tolower);
    
    int stem_score = 0, coding_score = 0, tooling_score = 0, logic_score = 0, 
        self_score = 0, multimodal_score = 0;
    
    std::vector<std::pair<std::string, int*>> patterns = {
        {"equation", &stem_score}, {"formula", &stem_score}, {"calculate", &stem_score},
        {"math", &stem_score}, {"physics", &stem_score}, {"chemistry", &stem_score},
        {"biology", &stem_score}, {"solve", &stem_score}, {"number", &stem_score},
        
        {"function", &coding_score}, {"code", &coding_score}, {"program", &coding_score},
        {"variable", &coding_score}, {"loop", &coding_score}, {"syntax", &coding_score},
        {"algorithm", &coding_score}, {"class", &coding_score}, {"method", &coding_score},
        {"return", &coding_score}, {"print", &coding_score}, {"if", &coding_score},
        
        {"orchestrate", &tooling_score}, {"execute", &tooling_score}, {"workflow", &tooling_score},
        {"step", &tooling_score}, {"action", &tooling_score}, {"agent", &tooling_score},
        {"task", &tooling_score}, {"pipeline", &tooling_score}, {"process", &tooling_score},
        
        {"logic", &logic_score}, {"reason", &logic_score}, {"proof", &logic_score},
        {"deduction", &logic_score}, {"inference", &logic_score}, {"theorem", &logic_score},
        {"hypothesis", &logic_score}, {"causal", &logic_score}, {"because", &logic_score},
        
        {"self", &self_score}, {"introspect", &self_score}, {"meta", &self_score},
        {"model", &self_score}, {"knowledge", &self_score}, {"understand", &self_score},
        {"learn", &self_score}, {"capability", &self_score}, {"limitation", &self_score},
        
        {"image", &multimodal_score}, {"visual", &multimodal_score}, {"picture", &multimodal_score},
        {"audio", &multimodal_score}, {"sound", &multimodal_score}, {"text", &multimodal_score},
        {"combine", &multimodal_score}, {"fusion", &multimodal_score}, {"modal", &multimodal_score}
    };
    
    for (const auto& pattern : patterns) {
        if (combined.find(pattern.first) != std::string::npos) {
            (*pattern.second)++;
        }
    }
    
    int max_score = std::max({stem_score, coding_score, tooling_score, logic_score, 
                             self_score, multimodal_score});
    
    if (max_score == 0) {
        return DataSourceType::GENERIC;
    }
    
    if (stem_score == max_score) return DataSourceType::STEM_QA;
    if (coding_score == max_score) return DataSourceType::CODING_FUNDAMENTALS;
    if (logic_score == max_score) return DataSourceType::LOGIC_REASONING;
    if (tooling_score == max_score) return DataSourceType::TOOLING_ORCHESTRATION;
    if (self_score == max_score) return DataSourceType::SELF_KNOWLEDGE;
    if (multimodal_score == max_score) return DataSourceType::MULTIMODAL;
    
    return DataSourceType::GENERIC;
}

RawDataRecord TrainingOrchestrator::convert_example_to_record(const TrainingExample& example, int record_index) const {
    RawDataRecord record;
    record.record_id = "record_" + std::to_string(record_index) + "_" + 
                       std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    
    record.source_type = map_domain_to_source_type(example.domain);
    
    if (record.source_type == DataSourceType::GENERIC) {
        record.source_type = infer_domain_from_content(example.input, example.output);
    }
    
    record.input_text = example.input;
    record.output_text = example.output;
    record.metadata["domain"] = example.domain;
    record.metadata["is_good"] = example.is_good ? "true" : "false";
    record.metadata["confidence"] = std::to_string(example.confidence);
    record.ingested_at = std::chrono::system_clock::now().time_since_epoch().count();
    
    return record;
}

void TrainingOrchestrator::split_data_for_holdout(const std::vector<ProcessedDataRecord>& stage_data,
                                                  std::vector<ProcessedDataRecord>& training_set,
                                                  std::vector<ProcessedDataRecord>& holdout_set,
                                                  int stage_id) {
    training_set.clear();
    holdout_set.clear();

    const float HOLDOUT_RATIO = 0.15f; // 15% holdout

    // Sort the data deterministically by source_record_id to ensure consistent splits
    std::vector<ProcessedDataRecord> sorted_data = stage_data;
    std::sort(sorted_data.begin(), sorted_data.end(), [](const ProcessedDataRecord& a, const ProcessedDataRecord& b) {
        return a.source_record_id < b.source_record_id;
    });

    for (const auto& record : sorted_data) {
        size_t hash = HoldoutValidationUtils::deterministic_hash(record.source_record_id);

        // Assign to holdout if hash falls into the specified ratio
        // Using modulo operator with a large prime to distribute hashes evenly
        if ((hash % 10007) < (HOLDOUT_RATIO * 10007)) { // 10007 is a prime number
            holdout_set.push_back(record);
        } else {
            training_set.push_back(record);
        }
    }
    log_stage_execution(static_cast<TrainingStage>(stage_id), 
                        "Split data for holdout: " + std::to_string(training_set.size()) + 
                        " training, " + std::to_string(holdout_set.size()) + " holdout.");
}

void TrainingOrchestrator::create_holdout_artifacts(const std::vector<ProcessedDataRecord>& holdout_set,
                                                    int stage_id) {
    if (holdout_set.empty()) {
        log_stage_execution(static_cast<TrainingStage>(stage_id), "No holdout artifacts to create (holdout set is empty).");
        return;
    }

    std::string holdout_path = "artifacts/holdout/stage_" + std::to_string(stage_id);
    
    std::vector<std::string> holdout_record_ids;
    for (const auto& record : holdout_set) {
        holdout_record_ids.push_back(record.source_record_id); 
    }

    std::string combined_ids_for_hash;
    for(const auto& id : holdout_record_ids) {
        combined_ids_for_hash += id;
    }

    // Manually construct JSON content
    std::string metadata_content = "{\n";
    metadata_content += "    \"stage_id\": " + std::to_string(stage_id) + ",\n";
    metadata_content += "    \"holdout_count\": " + std::to_string(holdout_set.size()) + ",\n";
    metadata_content += "    \"holdout_record_ids\": [\n";
    for (size_t i = 0; i < holdout_record_ids.size(); ++i) {
        metadata_content += "        \"" + holdout_record_ids[i] + "\"";
        if (i < holdout_record_ids.size() - 1) {
            metadata_content += ",";
        }
        metadata_content += "\n";
    }
    metadata_content += "    ],\n";
    metadata_content += "    \"timestamp\": " + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ",\n";
    metadata_content += "    \"holdout_set_hash\": \"" + std::to_string(HoldoutValidationUtils::deterministic_hash(combined_ids_for_hash)) + "\"\n";
    metadata_content += "}\n";

    std::string metadata_file_path = holdout_path + "/split_metadata.json";
    
    if (vfs_manager) {
        // Create the virtual directory in VFSManager
        vfs_manager->mkdir(holdout_path); // Use mkdir from VFSManager

        // Use VFSManager's checkpointing mechanism to persist the holdout metadata.
        // The module_name will include the stage ID for clear identification.
        std::string checkpoint_module_name = "holdout_stage_" + std::to_string(stage_id) + "_metadata";
        vfs_manager->checkpoint_module_state(checkpoint_module_name, metadata_content, "v1.0.0");
        log_stage_execution(static_cast<TrainingStage>(stage_id), 
                            "Holdout metadata checkpointed by VFSManager for module: " + checkpoint_module_name);
    } else {
         log_stage_execution(static_cast<TrainingStage>(stage_id), "VFSManager not available to checkpoint holdout metadata.");
    }

    log_stage_execution(static_cast<TrainingStage>(stage_id), 
                        "Created holdout artifacts at: " + holdout_path + 
                        " for " + std::to_string(holdout_set.size()) + " records.");
}

