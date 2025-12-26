#!/bin/bash

set -e

echo "Building OpticTrigeminal..."
mkdir -p build

cd build

# Compile all source files
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/neural_components.cpp \
    ../src/specialization.cpp \
    ../src/inference_engine.cpp \
    ../src/data_loader.cpp \
    ../src/data_pipeline.cpp \
    ../src/training_stages.cpp \
    ../src/training_orchestrator.cpp \
    ../src/checkpoint_persistence.cpp \
    ../src/weight_updater.cpp \
    ../src/artifact_persistence.cpp \
    ../src/recovery_manager.cpp \
    ../src/telemetry_collector.cpp \
    ../src/vfs_manager.cpp \
    ../src/rag_dag.cpp \
    ../src/agent_orchestrator.cpp \
    ../src/meta_debugger.cpp \
    ../src/cognitive_load_balancer.cpp \
    ../src/long_horizon_planner.cpp \
    ../src/debug_server.cpp \
    ../src/kernel_service_registry.cpp \
    ../src/policy_engine.cpp \
    ../src/decoder_compliance_gate.cpp \
    ../src/proto_voice_decoder.cpp \
    ../src/proto_voice.cpp \
    ../src/response_pipeline.cpp \
    ../src/multimodal_handler.cpp \
    ../src/clinical_sim.cpp \
    ../src/clinical_analyzer.cpp \
    ../src/training_scenario.cpp \
    ../src/training_analytics.cpp \
    ../src/http_server.cpp \
    ../src/main.cpp \
    -pthread \
    -o optic-trigeminal

echo "Build complete!"
echo "Binary: ./build/optic-trigeminal"

echo ""
echo "Building integration tests..."
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/neural_components.cpp \
    ../src/specialization.cpp \
    ../src/inference_engine.cpp \
    ../src/data_loader.cpp \
    ../src/data_pipeline.cpp \
    ../src/training_stages.cpp \
    ../src/training_orchestrator.cpp \
    ../src/checkpoint_persistence.cpp \
    ../src/weight_updater.cpp \
    ../src/artifact_persistence.cpp \
    ../src/recovery_manager.cpp \
    ../src/telemetry_collector.cpp \
    ../src/vfs_manager.cpp \
    ../src/rag_dag.cpp \
    ../src/agent_orchestrator.cpp \
    ../src/meta_debugger.cpp \
    ../src/cognitive_load_balancer.cpp \
    ../src/long_horizon_planner.cpp \
    ../src/debug_server.cpp \
    ../src/kernel_service_registry.cpp \
    ../src/policy_engine.cpp \
    ../src/decoder_compliance_gate.cpp \
    ../src/proto_voice_decoder.cpp \
    ../src/proto_voice.cpp \
    ../src/response_pipeline.cpp \
    ../src/multimodal_handler.cpp \
    ../src/training_scenario.cpp \
    ../src/training_analytics.cpp \
    ../src/acmk_integration_test.cpp \
    -pthread \
    -o acmk_integration_test

echo "Integration test build complete!"
echo "Test Binary: ./build/acmk_integration_test"

echo ""
echo "Building admin CLI..."
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/neural_components.cpp \
    ../src/specialization.cpp \
    ../src/inference_engine.cpp \
    ../src/data_loader.cpp \
    ../src/data_pipeline.cpp \
    ../src/training_stages.cpp \
    ../src/training_orchestrator.cpp \
    ../src/checkpoint_persistence.cpp \
    ../src/weight_updater.cpp \
    ../src/artifact_persistence.cpp \
    ../src/recovery_manager.cpp \
    ../src/telemetry_collector.cpp \
    ../src/admin_cli.cpp \
    ../src/vfs_manager.cpp \
    ../src/rag_dag.cpp \
    ../src/agent_orchestrator.cpp \
    ../src/meta_debugger.cpp \
    ../src/cognitive_load_balancer.cpp \
    ../src/long_horizon_planner.cpp \
    ../src/debug_server.cpp \
    ../src/kernel_service_registry.cpp \
    ../src/policy_engine.cpp \
    ../src/decoder_compliance_gate.cpp \
    ../src/proto_voice_decoder.cpp \
    ../src/proto_voice.cpp \
    ../src/response_pipeline.cpp \
    ../src/multimodal_handler.cpp \
    ../src/training_scenario.cpp \
    ../src/training_analytics.cpp \
    ../src/acmk_admin_cli.cpp \
    -pthread \
    -o acmk_admin_cli

echo "Admin CLI build complete!"
echo "CLI Binary: ./build/acmk_admin_cli"
