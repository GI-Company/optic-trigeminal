cmake_minimum_required(VERSION 3.16)
project(optic-trigeminal-wasm)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Emscripten Configuration
# ============================================================================

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "This project must be built with Emscripten. Use: emcmake cmake ..")
endif()

# Strict WASM memory model for safety-critical system
set(WASM_FLAGS "-sSTRICT=1 -sASSERTIONS=1 -sWASM=1 --no-entry -sEXPORTED_FUNCTIONS=_malloc,_free")
set(WASM_FLAGS "${WASM_FLAGS} -sALLOW_MEMORY_GROWTH=0")  # Fixed memory pool
set(WASM_FLAGS "${WASM_FLAGS} -sINITIAL_MEMORY=16777216") # 16MB initial
set(WASM_FLAGS "${WASM_FLAGS} -sMAXIMUM_MEMORY=16777216") # 16MB max (no growth)

# Optimization & minification
set(WASM_FLAGS "${WASM_FLAGS} -O3")
set(WASM_FLAGS "${WASM_FLAGS} -flto")

# Safety: Check for undefined symbols
set(WASM_FLAGS "${WASM_FLAGS} -Wl,--fatal-warnings")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${WASM_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${WASM_FLAGS}")

# ============================================================================
# Include Directories
# ============================================================================

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# ============================================================================
# Source Files: WASM-Only
# ============================================================================

# Inference engine (compute-only, safe to WASM)
set(WASM_SOURCES
  ../src/kernel/inference_engine.cpp
  ../src/kernel/neural_components.cpp
  ../src/kernel/specialization.cpp
  ../src/kernel/rag_dag.cpp
  ../src/kernel/data_loader.cpp
  ../src/kernel/data_pipeline.cpp
  ../src/kernel/training_stages.cpp
  ../src/kernel/training_orchestrator.cpp
  ../src/kernel/checkpoint_persistence.cpp
  ../src/kernel/weight_updater.cpp
  ../src/kernel/artifact_persistence.cpp
  ../src/kernel/recovery_manager.cpp
  ../src/kernel/telemetry_collector.cpp
  ../src/kernel/vfs_manager.cpp
  ../src/kernel/agent_orchestrator.cpp
  ../src/kernel/meta_debugger.cpp
  ../src/kernel/cognitive_load_balancer.cpp
  ../src/kernel/long_horizon_planner.cpp
  ../src/kernel/kernel_service_registry.cpp
  ../src/kernel/policy_engine.cpp
  ../src/kernel/decoder_compliance_gate.cpp
  ../src/kernel/proto_voice_decoder.cpp
  ../src/kernel/proto_voice.cpp
  ../src/kernel/response_pipeline.cpp
  ../src/kernel/multimodal_handler.cpp
  # NOTE: acmk_planes.cpp / state_plane.cpp intentionally excluded -- they
  # belong to the HTTP backend's ACMK plane coordinator (see
  # src/server/acmk_api_handler.cpp), are unused by inference_engine.cpp or
  # wasm/src/bindings.cpp, and pull in simulation_enforcement.cpp /
  # temporal_controls.cpp which aren't part of this target and would fail
  # to link.
  ../src/clinical/clinical_analyzer.cpp
  ../src/clinical/training_scenario.cpp
  ../src/clinical/training_analytics.cpp
  
  # WASM bindings layer (our addition)
  src/bindings.cpp
)

# ============================================================================
# WASM Module Target
# ============================================================================

add_executable(optic-trigeminal.js ${WASM_SOURCES})

# Export C functions to JavaScript
target_compile_definitions(optic-trigeminal.js PRIVATE EMSCRIPTEN_WASM_BUILD)

# Link with Emscripten runtime
target_link_libraries(optic-trigeminal.js)

# Output configuration
set_target_properties(optic-trigeminal.js PROPERTIES
  SUFFIX ".wasm"
  PREFIX ""
  OUTPUT_NAME "optic-trigeminal"
)

# ============================================================================
# Export Settings for JS Integration
# ============================================================================

# Generate both .wasm and .js loader
set(CMAKE_EXECUTABLE_SUFFIX ".js")

# Export all WASM functions marked with EMSCRIPTEN_KEEPALIVE
set(WASM_EXPORTS
  "wasm_infer"
  "wasm_analyze_vitals"
  "wasm_simulate_scenario_step"
  "wasm_free"
)

# ============================================================================
# Build Output
# ============================================================================

message(STATUS "OpticTrigeminal WASM Build Configuration")
message(STATUS "  Emscripten: ${EMSCRIPTEN}")
message(STATUS "  Memory Model: 16MB fixed (no growth)")
message(STATUS "  Optimization: O3 + LTO")
message(STATUS "  Safety: Strict mode, undefined symbol check")
message(STATUS "")
message(STATUS "Build targets:")
message(STATUS "  optic-trigeminal.wasm - WASM module")
message(STATUS "  optic-trigeminal.js - JS loader")
message(STATUS "")
message(STATUS "Output location: ../dist/wasm/")
