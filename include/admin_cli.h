#pragma once

#include "training_orchestrator.h"
#include "checkpoint_persistence.h"
#include "artifact_persistence.h"
#include "data_pipeline.h"
#include <string>
#include <vector>
#include <functional>
#include <map>

class AdminCLI {
public:
    AdminCLI(TrainingOrchestrator* orchestrator,
             CheckpointPersistence* checkpoints,
             ArtifactPersistenceManager* artifacts,
             DataPipeline* pipeline);
    
    void print_welcome();
    
    void print_help();
    
    void run_interactive_mode();
    
    bool execute_command(const std::string& command);
    
    std::string format_pipeline_status() const;
    
    std::string format_checkpoint_info() const;
    
    std::string format_artifact_info() const;
    
    std::string format_data_pipeline_status() const;
    
private:
    TrainingOrchestrator* orchestrator;
    CheckpointPersistence* checkpoint_manager;
    ArtifactPersistenceManager* artifact_manager;
    DataPipeline* data_pipeline;
    bool running;
    
    std::map<std::string, std::function<std::string()>> commands;
    
    void register_commands();
    
    std::string cmd_start_training();
    std::string cmd_pause_training();
    std::string cmd_resume_training();
    std::string cmd_status();
    std::string cmd_list_checkpoints();
    std::string cmd_load_checkpoint();
    std::string cmd_list_artifacts();
    std::string cmd_pipeline_stats();
    std::string cmd_orchestrator_report();
    std::string cmd_cleanup_checkpoints();
    std::string cmd_export_artifacts();
    std::string cmd_reset_training();
    std::string cmd_metrics();
    std::string cmd_help();
    std::string cmd_exit();
    
    std::vector<std::string> split_command(const std::string& input);
    
    void print_separator();
};
