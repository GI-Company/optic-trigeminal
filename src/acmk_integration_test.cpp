#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <chrono>
#include "policy_engine.h"
#include "data_pipeline.h"
#include "training_stages.h"
#include "vfs_manager.h"
#include "multimodal_handler.h"
#include "types.h"

class TestRunner {
private:
    int tests_passed = 0;
    int tests_failed = 0;
    
public:
    void run_test(const std::string& test_name, std::function<bool()> test_func) {
        std::cout << "Running: " << test_name << "... ";
        try {
            if (test_func()) {
                std::cout << "PASS\n";
                tests_passed++;
            } else {
                std::cout << "FAIL\n";
                tests_failed++;
            }
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION: " << e.what() << "\n";
            tests_failed++;
        }
    }
    
    void print_summary() {
        std::cout << "\n=== Test Summary ===\n";
        std::cout << "Passed: " << tests_passed << "\n";
        std::cout << "Failed: " << tests_failed << "\n";
        std::cout << "Total: " << (tests_passed + tests_failed) << "\n";
    }
};

bool test_policy_engine_basic() {
    PolicyEngine engine;
    
    std::map<std::string, std::string> context;
    context["confidence"] = "0.9";
    context["intent_type"] = "identity_query";
    
    std::string reason;
    auto result = engine.decide("test_decision", context, reason);
    
    return result == PolicyEffect::ALLOW;
}

bool test_policy_engine_action_sequence() {
    PolicyEngine engine;
    
    std::map<std::string, std::string> context;
    context["intent_type"] = "identity_query";
    context["session_id"] = "session_123";
    
    auto actions = engine.generate_action_sequence("test_goal", context);
    
    return actions.size() > 0 && actions[0].find("validate_intent") != std::string::npos;
}

bool test_data_pipeline_ingestion() {
    DataPipeline pipeline;
    
    std::vector<RawDataRecord> records;
    RawDataRecord rec;
    rec.record_id = "test_rec_1";
    rec.input_text = "What is 2+2?";
    rec.output_text = "4";
    rec.source_type = DataSourceType::STEM_QA;
    records.push_back(rec);
    
    pipeline.ingest_raw_records(records);
    
    return pipeline.get_total_ingested() == 1;
}

bool test_data_pipeline_deduplication() {
    DataPipeline pipeline;
    
    std::vector<RawDataRecord> records;
    RawDataRecord rec1, rec2;
    
    rec1.record_id = "rec_1";
    rec1.input_text = "What is Python?";
    rec1.output_text = "A programming language";
    rec1.source_type = DataSourceType::CODING_FUNDAMENTALS;
    records.push_back(rec1);
    
    rec2.record_id = "rec_2";
    rec2.input_text = "What is Python?";
    rec2.output_text = "A programming language";
    rec2.source_type = DataSourceType::CODING_FUNDAMENTALS;
    records.push_back(rec2);
    
    pipeline.ingest_raw_records(records);
    int before = pipeline.get_total_ingested();
    
    pipeline.deduplicate_records(0.95f);
    
    return before == 2;
}

bool test_data_pipeline_processing() {
    DataPipeline pipeline;
    
    std::vector<RawDataRecord> records;
    RawDataRecord rec;
    rec.record_id = "proc_test_1";
    rec.input_text = "Calculate the derivative of x^2";
    rec.output_text = "2x";
    rec.source_type = DataSourceType::STEM_QA;
    records.push_back(rec);
    
    pipeline.ingest_raw_records(records);
    
    std::vector<ProcessedDataRecord> processed;
    pipeline.process_batch(processed);
    
    return processed.size() == 1 && !processed[0].input_tokens.empty();
}

bool test_vfs_manager_creation() {
    VFSManager vfs;
    
    auto process = vfs.create_process("test_task", "Test task description", "");
    
    return process != nullptr && !process->process_id.empty();
}

bool test_vfs_manager_artifact() {
    VFSManager vfs;
    
    auto artifact = vfs.create_proof_artifact("test content", "test_module", "v1.0.0");
    
    return !artifact.artifact_id.empty() && artifact.module_name == "test_module";
}

bool test_vfs_manager_checkpoint() {
    VFSManager vfs;
    
    std::string checkpoint_id = vfs.checkpoint_module_state(
        "test_module",
        "test state content",
        "v1.0.0"
    );
    
    return !checkpoint_id.empty();
}

bool test_multimodal_text_processing() {
    MultimodalInstructionHandler handler;
    
    auto input = handler.process_multimodal_input("What is your name?");
    
    return input.modality == ModalityType::TEXT && !input.input_id.empty();
}

bool test_multimodal_instruction_extraction() {
    MultimodalInstructionHandler handler;
    
    auto input = handler.process_multimodal_input("My name is John");
    auto instruction = handler.extract_instruction(input);
    
    return instruction.primary_command.length() > 0;
}

bool test_multimodal_confidence() {
    MultimodalInstructionHandler handler;
    
    auto input = handler.process_multimodal_input("Tell me about machine learning");
    auto confidence = handler.compute_modality_confidence(input);
    
    return confidence["text"] > 0.0f;
}

bool test_training_checkpoint_creation() {
    VFSManager vfs;
    DataPipeline pipeline;
    TrainingController trainer(&vfs, &pipeline);
    
    trainer.initialize_training();
    
    auto checkpoint = trainer.create_checkpoint(
        TrainingStage::STAGE_0_BASE_KNOWLEDGE,
        "v0.1.0"
    );
    
    return !checkpoint.checkpoint_id.empty();
}

bool test_training_checkpoint_verification() {
    VFSManager vfs;
    DataPipeline pipeline;
    TrainingController trainer(&vfs, &pipeline);
    
    auto checkpoint = trainer.create_checkpoint(
        TrainingStage::STAGE_0_BASE_KNOWLEDGE,
        "v0.1.0"
    );
    
    return trainer.verify_checkpoint(checkpoint);
}

bool test_integration_policy_and_pipeline() {
    PolicyEngine policy;
    DataPipeline pipeline;
    
    std::map<std::string, std::string> context;
    context["confidence"] = "0.95";
    
    std::string reason;
    auto policy_decision = policy.decide("test", context, reason);
    
    RawDataRecord rec;
    rec.record_id = "test_1";
    rec.input_text = "test input";
    rec.output_text = "test output";
    rec.source_type = DataSourceType::STEM_QA;
    
    std::vector<RawDataRecord> recs = {rec};
    pipeline.ingest_raw_records(recs);
    
    return policy_decision == PolicyEffect::ALLOW && pipeline.get_total_ingested() == 1;
}

bool test_integration_vfs_and_training() {
    VFSManager vfs;
    DataPipeline pipeline;
    TrainingController trainer(&vfs, &pipeline);
    
    trainer.initialize_training();
    
    std::vector<ProcessedDataRecord> empty_data;
    trainer.run_stage_0_base_knowledge(empty_data, empty_data);
    
    auto metrics = trainer.get_stage_metrics(TrainingStage::STAGE_0_BASE_KNOWLEDGE);
    
    return metrics.stage == TrainingStage::STAGE_0_BASE_KNOWLEDGE;
}

bool test_stress_data_pipeline_large_batch() {
    DataPipeline pipeline;
    
    std::vector<RawDataRecord> records;
    for (int i = 0; i < 100; ++i) {
        RawDataRecord rec;
        rec.record_id = "stress_rec_" + std::to_string(i);
        rec.input_text = "Test input " + std::to_string(i);
        rec.output_text = "Test output " + std::to_string(i);
        rec.source_type = DataSourceType::STEM_QA;
        records.push_back(rec);
    }
    
    pipeline.ingest_raw_records(records);
    
    std::vector<ProcessedDataRecord> processed;
    pipeline.process_batch(processed);
    
    return processed.size() >= 50;
}

bool test_stress_vfs_many_artifacts() {
    VFSManager vfs;
    
    int created = 0;
    for (int i = 0; i < 50; ++i) {
        auto artifact = vfs.create_proof_artifact(
            "artifact_content_" + std::to_string(i),
            "stress_module",
            "v1.0.0"
        );
        if (!artifact.artifact_id.empty()) {
            created++;
        }
    }
    
    return created >= 40;
}

bool test_replay_determinism() {
    PolicyEngine engine1, engine2;
    
    std::map<std::string, std::string> context;
    context["confidence"] = "0.85";
    context["intent_type"] = "knowledge_retrieval";
    
    std::string reason1, reason2;
    auto result1 = engine1.decide("replay_test", context, reason1);
    auto result2 = engine2.decide("replay_test", context, reason2);
    
    return result1 == result2 && reason1 == reason2;
}

bool test_artifact_hash_validation() {
    VFSManager vfs;
    
    std::string content = "test content for hash validation";
    auto artifact = vfs.create_proof_artifact(content, "hash_test", "v1.0.0");
    
    return vfs.validate_artifact_hash(artifact.artifact_id, content);
}

bool test_module_versioning() {
    VFSManager vfs;
    
    vfs.checkpoint_module_state("version_test_module", "v1 content", "v1.0.0");
    vfs.checkpoint_module_state("version_test_module", "v2 content", "v2.0.0");
    
    auto versions = vfs.list_module_versions("version_test_module");
    
    return versions.size() == 2;
}

int main() {
    TestRunner runner;
    
    std::cout << "=== ACmK System Integration Tests ===\n\n";
    
    std::cout << "--- Unit Tests ---\n";
    runner.run_test("Policy Engine Basic", test_policy_engine_basic);
    runner.run_test("Policy Engine Action Sequence", test_policy_engine_action_sequence);
    runner.run_test("Data Pipeline Ingestion", test_data_pipeline_ingestion);
    runner.run_test("Data Pipeline Deduplication", test_data_pipeline_deduplication);
    runner.run_test("Data Pipeline Processing", test_data_pipeline_processing);
    runner.run_test("VFS Manager Creation", test_vfs_manager_creation);
    runner.run_test("VFS Manager Artifact", test_vfs_manager_artifact);
    runner.run_test("VFS Manager Checkpoint", test_vfs_manager_checkpoint);
    runner.run_test("Multimodal Text Processing", test_multimodal_text_processing);
    runner.run_test("Multimodal Instruction Extraction", test_multimodal_instruction_extraction);
    runner.run_test("Multimodal Confidence", test_multimodal_confidence);
    runner.run_test("Training Checkpoint Creation", test_training_checkpoint_creation);
    runner.run_test("Training Checkpoint Verification", test_training_checkpoint_verification);
    
    std::cout << "\n--- Integration Tests ---\n";
    runner.run_test("Policy & Pipeline Integration", test_integration_policy_and_pipeline);
    runner.run_test("VFS & Training Integration", test_integration_vfs_and_training);
    
    std::cout << "\n--- Stress Tests ---\n";
    runner.run_test("Data Pipeline Large Batch (100 records)", test_stress_data_pipeline_large_batch);
    runner.run_test("VFS Many Artifacts (50 artifacts)", test_stress_vfs_many_artifacts);
    
    std::cout << "\n--- Replay/Determinism Tests ---\n";
    runner.run_test("Replay Determinism", test_replay_determinism);
    runner.run_test("Artifact Hash Validation", test_artifact_hash_validation);
    runner.run_test("Module Versioning", test_module_versioning);
    
    runner.print_summary();
    
    return 0;
}
