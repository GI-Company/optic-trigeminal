#!/bin/bash

set -e

cd "$(dirname "$0")"

echo "Building OpticTrigeminal..."

# The frontend build is embedded directly into the server binary (see
# include/embedded_web_assets.h) so the compiled server is a genuinely
# single, self-contained executable -- no web/dist/ directory has to
# travel with it. Build the frontend first if it hasn't been built yet,
# then regenerate the embedded-assets source from it.
if [ ! -d web/dist ]; then
    echo "No web/dist/ found -- building frontend first..."
    (cd web && npm install --silent && npm run build)
fi
echo "Embedding frontend build into the server binary..."
python3 scripts/embed_web_assets.py

mkdir -p build

cd build

# Argon2id (password hashing) is vendored C, not C++ -- clang++ force-compiles
# .c files as C++ (breaking void*-conversion code that's valid C but not
# C++), so these are compiled once here with a real C compiler into object
# files, then linked into all three targets below alongside the C++ objects.
# argon2.h wraps its declarations in `extern "C" { ... }` under __cplusplus,
# so the C++ callers in crypto_utils.cpp link against them correctly.
ARGON2_INC="-I../third_party/argon2/include -I../third_party/argon2/src"
ARGON2_OBJS="argon2.o core.o encoding.o ref.o thread.o blake2b.o"
echo "Compiling vendored Argon2id..."
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/argon2.c -o argon2.o
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/core.c -o core.o
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/encoding.c -o encoding.o
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/ref.c -o ref.o
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/thread.c -o thread.o
clang -std=c99 -O3 $ARGON2_INC -c ../third_party/argon2/src/blake2/blake2b.c -o blake2b.o

# Compile all source files
clang++ -std=c++17 -O3 -march=native -I../include $ARGON2_INC \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/bm25_index.cpp \
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
    ../src/clinical/ode_physiology.cpp \
    ../src/clinical/clinical_analyzer.cpp \
    ../src/clinical/clinical_scoring.cpp \
    ../src/clinical/training_scenario.cpp \
    ../src/clinical/training_analytics.cpp \
    ../src/server/http_server.cpp \
    ../src/server/embedded_web_assets.cpp \
    ../src/kernel/groq_client.cpp \
    ../src/server/acmk_api_handler.cpp \
    ../src/kernel/acmk_planes.cpp \
    ../src/kernel/state_plane.cpp \
    ../src/server/main.cpp \
    $ARGON2_OBJS \
    -pthread \
    -o optic-trigeminal

echo "Build complete!"
echo "Binary: ./build/optic-trigeminal"

echo ""
echo "Building integration tests..."
clang++ -std=c++17 -O3 -march=native -I../include $ARGON2_INC \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/bm25_index.cpp \
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
    $ARGON2_OBJS \
    -pthread \
    -o acmk_integration_test

echo "Integration test build complete!"
echo "Test Binary: ./build/acmk_integration_test"

echo ""
echo "Building admin CLI..."
clang++ -std=c++17 -O3 -march=native -I../include $ARGON2_INC \
    ../src/kernel/neural_components.cpp \
    ../src/kernel/bm25_index.cpp \
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
    $ARGON2_OBJS \
    -pthread \
    -o acmk_admin_cli

echo "Admin CLI build complete!"
echo "CLI Binary: ./build/acmk_admin_cli"

echo ""
echo "Building physiology fuzz test..."
clang++ -std=c++17 -O2 -I../include \
    ../src/clinical/ode_physiology.cpp \
    ../tests/physiology_fuzz_test.cpp \
    -o physiology_fuzz_test

echo "Physiology fuzz test build complete!"
echo "Test Binary: ./build/physiology_fuzz_test"
