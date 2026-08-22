#include "admin_cli.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <iomanip>

AdminCLI::AdminCLI(TrainingOrchestrator* orch,
                   CheckpointPersistence* checkpts,
                   ArtifactPersistenceManager* artifacts,
                   DataPipeline* pipeline)
    : orchestrator(orch), 
      checkpoint_manager(checkpts), 
      artifact_manager(artifacts),
      data_pipeline(pipeline),
      running(true) {
    register_commands();
}

void AdminCLI::register_commands() {
    commands["start"] = [this]() { return cmd_start_training(); };
    commands["pause"] = [this]() { return cmd_pause_training(); };
    commands["resume"] = [this]() { return cmd_resume_training(); };
    commands["status"] = [this]() { return cmd_status(); };
    commands["checkpoints"] = [this]() { return cmd_list_checkpoints(); };
    commands["load"] = [this]() { return cmd_load_checkpoint(); };
    commands["artifacts"] = [this]() { return cmd_list_artifacts(); };
    commands["pipeline"] = [this]() { return cmd_pipeline_stats(); };
    commands["report"] = [this]() { return cmd_orchestrator_report(); };
    commands["cleanup"] = [this]() { return cmd_cleanup_checkpoints(); };
    commands["export"] = [this]() { return cmd_export_artifacts(); };
    commands["reset"] = [this]() { return cmd_reset_training(); };
    commands["metrics"] = [this]() { return cmd_metrics(); };
    commands["help"] = [this]() { return cmd_help(); };
    commands["exit"] = [this]() { return cmd_exit(); };
}

void AdminCLI::print_welcome() {
    print_separator();
    std::cout << "  ACmK System Administrator CLI v1.0.0\n";
    std::cout << "  Optic-Trigeminal Artificial Cognition Kernel\n";
    print_separator();
    std::cout << "\nType 'help' for available commands.\n\n";
}

void AdminCLI::print_help() {
    std::cout << "\n" << std::setw(60) << std::setfill('=') << "=" << std::setfill(' ') << "\n";
    std::cout << "Available Commands:\n";
    std::cout << std::setw(60) << std::setfill('=') << "=" << std::setfill(' ') << "\n\n";
    
    std::cout << "Training Control:\n";
    std::cout << "  start         - Start the training pipeline\n";
    std::cout << "  pause         - Pause current training\n";
    std::cout << "  resume        - Resume paused training\n";
    std::cout << "  reset         - Reset training state\n";
    std::cout << "  status        - Show current system status\n";
    std::cout << "  metrics       - Display detailed metrics\n\n";
    
    std::cout << "Checkpoint Management:\n";
    std::cout << "  checkpoints   - List all saved checkpoints\n";
    std::cout << "  load          - Load a specific checkpoint\n";
    std::cout << "  cleanup       - Remove old checkpoints\n\n";
    
    std::cout << "Artifact Management:\n";
    std::cout << "  artifacts     - List all artifacts\n";
    std::cout << "  export        - Export artifacts to JSON\n\n";
    
    std::cout << "System Information:\n";
    std::cout << "  pipeline      - Show data pipeline statistics\n";
    std::cout << "  report        - Display full orchestration report\n";
    std::cout << "  help          - Show this help message\n";
    std::cout << "  exit          - Exit the CLI\n\n";
}

void AdminCLI::run_interactive_mode() {
    print_welcome();
    
    std::string input;
    while (running) {
        std::cout << "acmk> ";
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        
        std::transform(input.begin(), input.end(), input.begin(), ::tolower);
        std::istringstream iss(input);
        std::string command;
        iss >> command;
        
        if (execute_command(command)) {
            std::cout << "\n";
        }
    }
}

bool AdminCLI::execute_command(const std::string& command) {
    auto it = commands.find(command);
    if (it != commands.end()) {
        std::string result = it->second();
        if (!result.empty()) {
            std::cout << result;
        }
        return true;
    } else {
        std::cout << "Unknown command: " << command << ". Type 'help' for available commands.\n";
        return false;
    }
}

std::string AdminCLI::cmd_start_training() {
    orchestrator->execute_training_pipeline();
    return "Training pipeline started.\n" + format_pipeline_status();
}

std::string AdminCLI::cmd_pause_training() {
    orchestrator->pause_training();
    return "Training paused.\n";
}

std::string AdminCLI::cmd_resume_training() {
    orchestrator->resume_training();
    return "Training resumed.\n";
}

std::string AdminCLI::cmd_status() {
    return format_pipeline_status();
}

std::string AdminCLI::cmd_list_checkpoints() {
    std::stringstream ss;
    auto checkpoints = checkpoint_manager->list_all_checkpoints();
    
    ss << "=== Checkpoint List ===\n";
    ss << "Total Checkpoints: " << checkpoints.size() << "\n\n";
    
    if (checkpoints.empty()) {
        ss << "No checkpoints found.\n";
        return ss.str();
    }
    
    for (const auto& cp : checkpoints) {
        ss << "  ID: " << cp.checkpoint_id << "\n";
        ss << "    Stage: " << static_cast<int>(cp.stage) << "\n";
        ss << "    Version: " << cp.version << "\n";
        ss << "    Accuracy: " << (cp.metrics.accuracy * 100) << "%\n";
        ss << "    Loss: " << cp.metrics.average_loss << "\n";
        ss << "    Verified: " << (cp.verified ? "Yes" : "No") << "\n\n";
    }
    
    return ss.str();
}

std::string AdminCLI::cmd_load_checkpoint() {
    std::cout << "Enter checkpoint ID: ";
    std::string checkpoint_id;
    std::getline(std::cin, checkpoint_id);
    
    CheckpointData checkpoint;
    if (checkpoint_manager->load_checkpoint(checkpoint_id, checkpoint)) {
        std::stringstream ss;
        ss << "Successfully loaded checkpoint: " << checkpoint_id << "\n";
        ss << "  Stage: " << static_cast<int>(checkpoint.stage) << "\n";
        ss << "  Version: " << checkpoint.version << "\n";
        ss << "  Accuracy: " << (checkpoint.metrics.accuracy * 100) << "%\n";
        return ss.str();
    } else {
        return "Failed to load checkpoint: " + checkpoint_id + "\n";
    }
}

std::string AdminCLI::cmd_list_artifacts() {
    std::stringstream ss;
    auto artifacts = artifact_manager->list_all_artifacts();
    
    ss << "=== Artifact List ===\n";
    ss << "Total Artifacts: " << artifacts.size() << "\n\n";
    
    if (artifacts.empty()) {
        ss << "No artifacts found.\n";
        return ss.str();
    }
    
    for (const auto& artifact : artifacts) {
        ss << "  ID: " << artifact.artifact_id << "\n";
        ss << "    Source Record: " << artifact.source_record_id << "\n";
        ss << "    Source Type: " << static_cast<int>(artifact.source_type) << "\n";
        ss << "    Verified: " << (artifact.verified ? "Yes" : "No") << "\n\n";
    }
    
    return ss.str();
}

std::string AdminCLI::cmd_pipeline_stats() {
    std::stringstream ss;
    ss << "=== Data Pipeline Statistics ===\n";
    ss << "Total Ingested Records: " << data_pipeline->get_total_ingested() << "\n";
    ss << "Total Processed Records: " << data_pipeline->get_total_processed() << "\n";
    ss << "Valid Records: " << data_pipeline->get_total_valid() << "\n\n";
    
    auto metrics = data_pipeline->get_quality_metrics();
    ss << "Quality Metrics:\n";
    ss << "  Quality Score: " << (metrics.quality_score * 100) << "%\n";
    ss << "  Duplicate Records: " << metrics.duplicate_records << "\n";
    ss << "  Malformed Records: " << metrics.malformed_records << "\n";
    ss << "  Sparsity Ratio: " << metrics.sparsity_ratio << "\n";
    ss << "  Avg Input Length: " << metrics.average_input_length << "\n";
    ss << "  Avg Output Length: " << metrics.average_output_length << "\n\n";
    
    return ss.str();
}

std::string AdminCLI::cmd_orchestrator_report() {
    return orchestrator->get_status_report() + "\n";
}

std::string AdminCLI::cmd_cleanup_checkpoints() {
    checkpoint_manager->cleanup_old_checkpoints(3);
    return "Cleaned up old checkpoints (keeping latest 3).\n";
}

std::string AdminCLI::cmd_export_artifacts() {
    std::cout << "Enter output filename (default: artifacts.json): ";
    std::string filename;
    std::getline(std::cin, filename);
    
    if (filename.empty()) {
        filename = "artifacts.json";
    }
    
    artifact_manager->export_artifacts_to_json(filename);
    return "Artifacts exported to: " + filename + "\n";
}

std::string AdminCLI::cmd_reset_training() {
    std::cout << "Are you sure you want to reset training state? (yes/no): ";
    std::string confirm;
    std::getline(std::cin, confirm);
    
    if (confirm == "yes") {
        orchestrator->reset_orchestration();
        return "Training state reset.\n";
    }
    return "Reset cancelled.\n";
}

std::string AdminCLI::cmd_metrics() {
    std::stringstream ss;
    auto metrics = orchestrator->get_metrics();
    
    ss << "=== System Metrics ===\n";
    ss << "State: ";
    switch (metrics.current_state) {
        case OrchestratorState::IDLE: ss << "IDLE\n"; break;
        case OrchestratorState::RUNNING: ss << "RUNNING\n"; break;
        case OrchestratorState::PAUSED: ss << "PAUSED\n"; break;
        case OrchestratorState::COMPLETED: ss << "COMPLETED\n"; break;
        case OrchestratorState::FAILED: ss << "FAILED\n"; break;
    }
    
    ss << "Stages Completed: " << metrics.total_stages_completed << " / 5\n";
    ss << "Records Processed: " << metrics.total_records_processed << "\n";
    ss << "Overall Accuracy: " << (metrics.overall_accuracy * 100) << "%\n";
    ss << "Overall Loss: " << metrics.overall_loss << "\n";
    ss << "Status: " << metrics.status_message << "\n\n";
    
    ss << artifact_manager->get_storage_stats();
    
    return ss.str();
}

std::string AdminCLI::cmd_help() {
    std::stringstream ss;
    print_help();
    return "";
}

std::string AdminCLI::cmd_exit() {
    running = false;
    std::cout << "Exiting CLI. Goodbye!\n";
    return "";
}

std::string AdminCLI::format_pipeline_status() const {
    std::stringstream ss;
    auto metrics = orchestrator->get_metrics();
    
    ss << "\nPipeline Status:\n";
    ss << "  State: ";
    
    switch (metrics.current_state) {
        case OrchestratorState::IDLE: ss << "IDLE\n"; break;
        case OrchestratorState::RUNNING: ss << "RUNNING\n"; break;
        case OrchestratorState::PAUSED: ss << "PAUSED\n"; break;
        case OrchestratorState::COMPLETED: ss << "COMPLETED\n"; break;
        case OrchestratorState::FAILED: ss << "FAILED\n"; break;
    }
    
    ss << "  Stages Completed: " << metrics.total_stages_completed << " / 5\n";
    ss << "  Records Processed: " << metrics.total_records_processed << "\n";
    ss << "  Overall Accuracy: " << (metrics.overall_accuracy * 100) << "%\n";
    
    return ss.str();
}

std::string AdminCLI::format_checkpoint_info() const {
    std::stringstream ss;
    int count = checkpoint_manager->get_checkpoint_count();
    ss << "Checkpoints: " << count << "\n";
    return ss.str();
}

std::string AdminCLI::format_artifact_info() const {
    std::stringstream ss;
    int count = artifact_manager->get_artifact_count();
    ss << "Artifacts: " << count << "\n";
    return ss.str();
}

std::string AdminCLI::format_data_pipeline_status() const {
    std::stringstream ss;
    ss << "Pipeline Records: " << data_pipeline->get_total_processed() << "\n";
    return ss.str();
}

std::vector<std::string> AdminCLI::split_command(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

void AdminCLI::print_separator() {
    std::cout << std::setw(60) << std::setfill('=') << "=" << std::setfill(' ') << "\n";
}
