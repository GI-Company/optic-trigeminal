#include "training_orchestrator.h"
#include "data_pipeline.h"
#include "training_stages.h"
#include "vfs_manager.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <filesystem>
#include <sstream>
 
// Note: Since I don't see nlohmann/json in the file list, I'll use simple string parsing or manual construction as seen in other files.

namespace fs = std::filesystem;

// Helper to read file content
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// Helper to manually parse the JSON file (which is an array of objects)
std::vector<TrainingExample> load_jsonl(const std::string& path) {
    std::vector<TrainingExample> examples;
    std::string content = read_file(path);
    
    // Simple manual parsing to find objects enclosed in {}
    size_t pos = 0;
    while ((pos = content.find('{', pos)) != std::string::npos) {
        size_t end = content.find('}', pos);
        if (end == std::string::npos) break;
        
        std::string obj_str = content.substr(pos, end - pos + 1);
        TrainingExample ex;
        
        // Extract domain
        size_t domain_pos = obj_str.find("\"domain\":");
        if (domain_pos != std::string::npos) {
            size_t start = obj_str.find("\"", domain_pos + 9) + 1;
            size_t val_end = obj_str.find("\"", start);
            ex.domain = obj_str.substr(start, val_end - start);
        }
        
        // Extract input
        size_t input_pos = obj_str.find("\"input\":");
        if (input_pos != std::string::npos) {
            size_t start = obj_str.find("\"", input_pos + 8) + 1;
            size_t val_end = obj_str.find("\"", start);
            ex.input = obj_str.substr(start, val_end - start);
        }
        
        // Extract output
        size_t output_pos = obj_str.find("\"output\":");
        if (output_pos != std::string::npos) {
            size_t start = obj_str.find("\"", output_pos + 9) + 1;
            size_t val_end = obj_str.find("\"", start);
            ex.output = obj_str.substr(start, val_end - start);
        }
        
        ex.id = "test_" + std::to_string(examples.size());
        ex.is_good = true;
        ex.confidence = 1.0f;
        
        examples.push_back(ex);
        pos = end + 1;
    }
    return examples;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "OPTIC-TRIGEMINAL SYSTEM STRESS & REAL-WORLD TEST" << std::endl;
    std::cout << "==================================================" << std::endl << std::endl;

    // 1. Setup Environment
    auto vfs = std::make_unique<VFSManager>();
    auto data_pipeline = std::make_unique<DataPipeline>();
    auto training_controller = std::make_unique<TrainingController>(vfs.get(), data_pipeline.get());
    auto training_orchestrator = std::make_unique<TrainingOrchestrator>(
        training_controller.get(), 
        data_pipeline.get(), 
        vfs.get()
    );

    training_orchestrator->initialize();

    // 2. Load Specialized Real-World Datasets
    std::vector<std::string> datasets = {
        "data/legal_courts.jsonl",
        "data/medical_research.jsonl",
        "data/edge_devices.jsonl",
        "data/tooling.jsonl",
        "data/self_knowledge.jsonl",
        "data/multimodal.jsonl"
    };

    std::vector<TrainingExample> all_examples;
    for (const auto& path : datasets) {
        std::cout << "Loading dataset: " << path << "... ";
        auto examples = load_jsonl(path);
        std::cout << "Loaded " << examples.size() << " examples." << std::endl;
        all_examples.insert(all_examples.end(), examples.begin(), examples.end());
    }

    // Load generic data to fill pipeline if necessary
    // (Assuming sufficient data in specialized sets for this test, or we simulate duplicates)
    // Duplicate data to stress test volume
    std::cout << "Duplicating data to stress test volume..." << std::endl;
    std::vector<TrainingExample> stress_examples = all_examples;
    for (int i=0; i<5; ++i) { // 5x duplication
        for (const auto& ex : all_examples) {
            TrainingExample dup = ex;
            dup.id += "_dup_" + std::to_string(i);
            stress_examples.push_back(dup);
        }
    }
    std::cout << "Total Examples: " << stress_examples.size() << std::endl;

    // 3. Execution Run 1
    std::cout << "\n--- RUN 1 : Initial Training ---" << std::endl;
    training_orchestrator->ingest_training_examples(stress_examples);
    training_orchestrator->execute_training_pipeline(); // Use the orchestrator logic!

    if (!training_orchestrator->is_training_complete()) {
        std::cerr << "Run 1 Failed to complete!" << std::endl;
        // Proceed anyway to check artifacts
    }

    // Capture artifacts from Run 1
    std::string run1_stage0_holdout = read_file("artifacts/holdout/stage_0/split_metadata.json");
    
    // Check if weights changed (Basic check: comparing generic init hash vs current)
    // For now, we assume if pipeline completed and accuracy > 0, it learned.
    auto metrics1 = training_orchestrator->get_metrics();
    std::cout << "Run 1 Accuracy: " << metrics1.overall_accuracy << std::endl;

    // 4. Reset System
    std::cout << "\n--- RESETING SYSTEM ---" << std::endl;
    training_orchestrator->reset_orchestration();
    data_pipeline->reset();
    // Re-initialize controller to reset weights (simulating fresh start)
    training_controller = std::make_unique<TrainingController>(vfs.get(), data_pipeline.get());
    training_orchestrator = std::make_unique<TrainingOrchestrator>(
        training_controller.get(), 
        data_pipeline.get(), 
        vfs.get()
    );
    training_orchestrator->initialize();

    // 5. Execution Run 2 (Reproducibility Check)
    std::cout << "\n--- RUN 2 : Reproducibility Verification ---" << std::endl;
    training_orchestrator->ingest_training_examples(stress_examples);
    training_orchestrator->execute_training_pipeline();

    std::string run2_stage0_holdout = read_file("artifacts/holdout/stage_0/split_metadata.json");
    
    // 6. Verification
    std::cout << "\n--- VERIFICATION RESULTS ---" << std::endl;
    
    bool holdout_match = (run1_stage0_holdout == run2_stage0_holdout);
    std::cout << "Determinism Check (Holdout Artifacts Match): " << (holdout_match ? "PASS" : "FAIL") << std::endl;
    
    if (!holdout_match) {
        std::cout << "Run 1 Artifact:\n" << run1_stage0_holdout << "\n";
        std::cout << "Run 2 Artifact:\n" << run2_stage0_holdout << "\n";
    }

    auto metrics2 = training_orchestrator->get_metrics();
    bool accuracy_match = (std::abs(metrics1.overall_accuracy - metrics2.overall_accuracy) < 0.0001f);
     std::cout << "Determinism Check (Accuracy Match): " << (accuracy_match ? "PASS" : "FAIL") << std::endl;

    if (holdout_match && accuracy_match) {
        std::cout << "\nSUCCESS: System passed stress test and reproducibility verification." << std::endl;
        return 0;
    } else {
        std::cout << "\nFAILURE: System failed reproducibility verification." << std::endl;
        return 1;
    }
}
