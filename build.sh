#!/bin/bash

set -e

echo "Building OpticTrigeminal..."
mkdir -p build

cd build

# Compile all source files
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/specialization.cpp \
    ../src/kernel/inference_engine.cpp \
    ../src/kernel/data_loader.cpp \
    ../src/kernel/data_pipeline.cpp \
    ../src/kernel/training_stages.cpp \
    ../src/kernel/training_orchestrator.cpp \
    ../src/kernel/checkpoint_persistence.cpp \
    ../src/kernel/weight_updater.cpp \
    ../src/kernel/artifact_persistence.cpp \
    ../src/kernel/recovery_manager.cpp \
    ../src/kernel/telemetry_collector.cpp \
    ../src/kernel/vfs_manager.cpp \
    ../src/kernel/rag_dag.cpp \
    ../src/kernel/agent_orchestrator.cpp \
    ../src/kernel/meta_debugger.cpp \
    ../src/kernel/cognitive_load_balancer.cpp \
    ../src/kernel/long_horizon_planner.cpp \
    ../src/server/debug_server.cpp \
    ../src/server/auth_manager.cpp \
    ../src/server/cohort_manager.cpp \
    ../src/kernel/kernel_service_registry.cpp \
    ../src/kernel/crypto_utils.cpp \
    ../src/kernel/simulation_enforcement.cpp \
    ../src/kernel/temporal_controls.cpp \
    ../src/kernel/fhir_client.cpp \
    ../src/clinical/rbac_fhir.cpp \
    ../src/kernel/policy_engine.cpp \
    ../src/kernel/decoder_compliance_gate.cpp \
    ../src/kernel/proto_voice_decoder.cpp \
    ../src/kernel/proto_voice.cpp \
    ../src/kernel/response_pipeline.cpp \
    ../src/kernel/multimodal_handler.cpp \
    ../src/clinical/clinical_sim.cpp \
    ../src/clinical/clinical_analyzer.cpp \
    ../src/clinical/training_scenario.cpp \
    ../src/clinical/training_analytics.cpp \
    ../src/server/http_server.cpp \
    ../src/kernel/groq_client.cpp \
    ../src/server/acmk_api_handler.cpp \
    ../src/kernel/acmk_planes.cpp \
    ../src/kernel/state_plane.cpp \
    ../src/server/main.cpp \
    -pthread \
    -o optic-trigeminal

echo "Build complete!"
echo "Binary: ./build/optic-trigeminal"

echo ""
echo "Building integration tests..."
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/specialization.cpp \
    ../src/kernel/inference_engine.cpp \
    ../src/kernel/data_loader.cpp \
    ../src/kernel/data_pipeline.cpp \
    ../src/kernel/training_stages.cpp \
    ../src/kernel/training_orchestrator.cpp \
    ../src/kernel/checkpoint_persistence.cpp \
    ../src/kernel/weight_updater.cpp \
    ../src/kernel/artifact_persistence.cpp \
    ../src/kernel/recovery_manager.cpp \
    ../src/kernel/telemetry_collector.cpp \
    ../src/kernel/vfs_manager.cpp \
    ../src/kernel/rag_dag.cpp \
    ../src/kernel/agent_orchestrator.cpp \
    ../src/kernel/meta_debugger.cpp \
    ../src/kernel/cognitive_load_balancer.cpp \
    ../src/kernel/long_horizon_planner.cpp \
    ../src/server/debug_server.cpp \
    ../src/server/auth_manager.cpp \
    ../src/kernel/kernel_service_registry.cpp \
    ../src/kernel/crypto_utils.cpp \
    ../src/kernel/simulation_enforcement.cpp \
    ../src/kernel/temporal_controls.cpp \
    ../src/kernel/fhir_client.cpp \
    ../src/clinical/rbac_fhir.cpp \
    ../src/kernel/policy_engine.cpp \
    ../src/kernel/decoder_compliance_gate.cpp \
    ../src/kernel/proto_voice_decoder.cpp \
    ../src/kernel/proto_voice.cpp \
    ../src/kernel/response_pipeline.cpp \
    ../src/kernel/multimodal_handler.cpp \
    ../src/clinical/training_scenario.cpp \
    ../src/clinical/training_analytics.cpp \
    ../tests/acmk_integration_test.cpp \
    ../src/kernel/groq_client.cpp \
    ../src/server/acmk_api_handler.cpp \
    ../src/kernel/acmk_planes.cpp \
    ../src/kernel/state_plane.cpp \
    -pthread \
    -o acmk_integration_test

echo "Integration test build complete!"
echo "Test Binary: ./build/acmk_integration_test"

echo ""
echo "Building admin CLI..."
clang++ -std=c++17 -O3 -march=native -I../include \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/specialization.cpp \
    ../src/kernel/inference_engine.cpp \
    ../src/kernel/data_loader.cpp \
    ../src/kernel/data_pipeline.cpp \
    ../src/kernel/training_stages.cpp \
    ../src/kernel/training_orchestrator.cpp \
    ../src/kernel/checkpoint_persistence.cpp \
    ../src/kernel/weight_updater.cpp \
    ../src/kernel/artifact_persistence.cpp \
    ../src/kernel/recovery_manager.cpp \
    ../src/kernel/telemetry_collector.cpp \
    ../src/tools/admin_cli.cpp \
    ../src/kernel/vfs_manager.cpp \
    ../src/kernel/rag_dag.cpp \
    ../src/kernel/agent_orchestrator.cpp \
    ../src/kernel/meta_debugger.cpp \
    ../src/kernel/cognitive_load_balancer.cpp \
    ../src/kernel/long_horizon_planner.cpp \
    ../src/server/debug_server.cpp \
    ../src/server/auth_manager.cpp \
    ../src/kernel/kernel_service_registry.cpp \
    ../src/kernel/crypto_utils.cpp \
    ../src/kernel/simulation_enforcement.cpp \
    ../src/kernel/temporal_controls.cpp \
    ../src/kernel/fhir_client.cpp \
    ../src/clinical/rbac_fhir.cpp \
    ../src/kernel/policy_engine.cpp \
    ../src/kernel/decoder_compliance_gate.cpp \
    ../src/kernel/proto_voice_decoder.cpp \
    ../src/kernel/proto_voice.cpp \
    ../src/kernel/response_pipeline.cpp \
    ../src/kernel/multimodal_handler.cpp \
    ../src/clinical/training_scenario.cpp \
    ../src/clinical/training_analytics.cpp \
    ../src/tools/acmk_admin_cli.cpp \
    ../src/kernel/groq_client.cpp \
    ../src/server/acmk_api_handler.cpp \
    ../src/kernel/acmk_planes.cpp \
    ../src/kernel/state_plane.cpp \
    -pthread \
    -o acmk_admin_cli

echo "Admin CLI build complete!"
echo "CLI Binary: ./build/acmk_admin_cli"
