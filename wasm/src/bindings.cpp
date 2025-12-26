/**
 * OpticTrigeminal WASM Bindings
 * 
 * Phase 1: Minimal inference wrapper demonstrating memory ownership contract
 * 
 * Rules enforced:
 * - All results returned as JSON strings
 * - Static buffers (no heap allocation)
 * - All calls are pure (no state mutation)
 * - No I/O, persistence, or auth logic
 */

#include <emscripten/emscripten.h>
#include <cstring>
#include <string>
#include <sstream>
#include "inference_engine.h"

// ============================================================================
// Global WASM State
// ============================================================================

// Static buffer for results (65KB - sufficient for clinical data)
static const int RESULT_BUFFER_SIZE = 65536;
static char result_buffer[RESULT_BUFFER_SIZE];

// Single inference engine instance (shared across calls)
static NativeInferenceEngine* g_engine = nullptr;

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize WASM engine (called once from JavaScript on module load)
 * 
 * Required: Call this before using any other WASM functions
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  int wasm_init_engine() {
    try {
      g_engine = new NativeInferenceEngine();
      if (!g_engine->initialize()) {
        return -1; // Initialization failed
      }
      return 0; // Success
    } catch (...) {
      return -2; // Exception during init
    }
  }

  EMSCRIPTEN_KEEPALIVE
  void wasm_destroy_engine() {
    if (g_engine) {
      delete g_engine;
      g_engine = nullptr;
    }
  }
}

// ============================================================================
// Helper: JSON Building
// ============================================================================

/**
 * Safely copy string to static result buffer
 * Always null-terminates
 */
static const char* buffer_result(const std::string& json_str) {
  if (json_str.length() >= RESULT_BUFFER_SIZE) {
    // Truncate if too large (shouldn't happen for clinical data)
    strncpy(result_buffer, json_str.c_str(), RESULT_BUFFER_SIZE - 1);
    result_buffer[RESULT_BUFFER_SIZE - 1] = '\0';
  } else {
    strcpy(result_buffer, json_str.c_str());
  }
  return result_buffer;
}

/**
 * Escape JSON string (handle quotes, newlines, etc.)
 */
static std::string json_escape(const std::string& str) {
  std::string escaped;
  for (char c : str) {
    switch (c) {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      case '/': escaped += "\\/"; break;
      case '\b': escaped += "\\b"; break;
      case '\f': escaped += "\\f"; break;
      default:
        if (c < 32) {
          // Control character - escape as \uXXXX
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
          escaped += buf;
        } else {
          escaped += c;
        }
    }
  }
  return escaped;
}

// ============================================================================
// Inference API
// ============================================================================

/**
 * wasm_infer - Single inference call
 * 
 * Input JSON: { "prompt": "...", "max_tokens": 128, "session_id": "..." }
 * Output JSON: { "prompt": "...", "response": "...", "confidence": 0.95, ... }
 * 
 * Memory: Returns pointer to static 65KB buffer
 * Contract: Caller may call wasm_free() after reading (no-op for static buffer)
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_infer(const char* request_json, int max_tokens) {
    if (!g_engine) {
      return buffer_result("{\"error\": \"Engine not initialized\"}");
    }

    if (!request_json) {
      return buffer_result("{\"error\": \"Null request\"}");
    }

    try {
      // Parse request JSON (simplified - production would use proper JSON parser)
      std::string json_str(request_json);
      
      // Extract prompt between quotes (naive parsing for Phase 1)
      size_t prompt_start = json_str.find("\"prompt\"") + 11;
      size_t prompt_end = json_str.find("\"", prompt_start);
      std::string prompt = json_str.substr(prompt_start, prompt_end - prompt_start);

      // Create inference request
      InferenceRequest inf_req(prompt, max_tokens);
      
      // Call inference engine
      InferenceResponse inf_resp = g_engine->infer(inf_req);

      // Build response JSON
      std::stringstream response;
      response << "{\n";
      response << "  \"prompt\": \"" << json_escape(inf_resp.prompt) << "\",\n";
      response << "  \"response\": \"" << json_escape(inf_resp.response) << "\",\n";
      response << "  \"type\": \"" << inf_resp.type << "\",\n";
      response << "  \"timestamp\": \"" << inf_resp.timestamp << "\",\n";
      response << "  \"confidence\": " << inf_resp.confidence << ",\n";
      response << "  \"related_concepts\": [";
      
      for (size_t i = 0; i < inf_resp.related_concepts.size(); ++i) {
        response << "\"" << json_escape(inf_resp.related_concepts[i]) << "\"";
        if (i < inf_resp.related_concepts.size() - 1) {
          response << ", ";
        }
      }
      response << "]\n";
      response << "}\n";

      return buffer_result(response.str());

    } catch (const std::exception& e) {
      std::string error_msg = "{\"error\": \"";
      error_msg += json_escape(std::string(e.what()));
      error_msg += "\"}";
      return buffer_result(error_msg);
    } catch (...) {
      return buffer_result("{\"error\": \"Unknown exception in wasm_infer\"}");
    }
  }
}

// ============================================================================
// Clinical Analysis API
// ============================================================================

/**
 * wasm_analyze_vitals - Analyze patient vital signs
 * 
 * Input JSON: { "patient_id": 1, "hr": 120, "spo2": 88, ... }
 * Output JSON: { "severity": "critical", "alerts": [...], "recommendations": [...] }
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_analyze_vitals(const char* vitals_json) {
    if (!g_engine) {
      return buffer_result("{\"error\": \"Engine not initialized\"}");
    }

    if (!vitals_json) {
      return buffer_result("{\"error\": \"Null vitals data\"}");
    }

    try {
      // Phase 1: Placeholder implementation
      // In Phase 3, this will call clinical_analyzer->analyze(vitals)
      
      std::stringstream response;
      response << "{\n";
      response << "  \"severity\": \"info\",\n";
      response << "  \"alerts\": [],\n";
      response << "  \"recommendations\": [\"Monitor patient\"],\n";
      response << "  \"confidence\": 0.95\n";
      response << "}\n";

      return buffer_result(response.str());

    } catch (const std::exception& e) {
      std::string error_msg = "{\"error\": \"";
      error_msg += json_escape(std::string(e.what()));
      error_msg += "\"}";
      return buffer_result(error_msg);
    }
  }
}

// ============================================================================
// Training Simulation API
// ============================================================================

/**
 * wasm_simulate_scenario_step - Execute one step of training scenario
 * 
 * Input: Scenario state JSON
 * Output: New state + events JSON
 * 
 * CRITICAL: This must be pure - same input always produces same output
 * No state persistence between calls
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_simulate_scenario_step(const char* scenario_json) {
    if (!g_engine) {
      return buffer_result("{\"error\": \"Engine not initialized\"}");
    }

    if (!scenario_json) {
      return buffer_result("{\"error\": \"Null scenario data\"}");
    }

    try {
      // Phase 1: Placeholder implementation
      // In Phase 3, this will call training_orchestrator->step(scenario)
      
      std::stringstream response;
      response << "{\n";
      response << "  \"scenario_id\": \"TRAINING_001\",\n";
      response << "  \"status\": \"running\",\n";
      response << "  \"events\": [],\n";
      response << "  \"immutable\": true\n";
      response << "}\n";

      return buffer_result(response.str());

    } catch (const std::exception& e) {
      std::string error_msg = "{\"error\": \"";
      error_msg += json_escape(std::string(e.what()));
      error_msg += "\"}";
      return buffer_result(error_msg);
    }
  }
}

// ============================================================================
// Memory Management
// ============================================================================

/**
 * wasm_free - Deallocate result from WASM
 * 
 * For static buffers: No-op (managed by WASM)
 * For heap allocations: Would delete pointer
 * 
 * Current implementation: Static buffer only, so this is a no-op
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  void wasm_free(void* ptr) {
    // Current implementation uses static buffer:
    // - result_buffer is allocated on WASM heap
    // - Lifetime: entire process
    // - No deallocation needed
    
    // If future phases use heap allocation:
    // if (ptr) {
    //   free(ptr);
    // }
    
    (void)ptr; // Suppress unused parameter warning
  }
}

// ============================================================================
// Health Check API
// ============================================================================

/**
 * wasm_health - Check WASM engine status
 * 
 * Returns JSON with engine metrics
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_health() {
    if (!g_engine) {
      return buffer_result("{\"status\": \"not_initialized\"}");
    }

    try {
      auto metrics = g_engine->get_metrics();
      
      std::stringstream response;
      response << "{\n";
      response << "  \"status\": \"ready\",\n";
      response << "  \"vocab_size\": " << metrics.vocab_size << ",\n";
      response << "  \"graph_nodes\": " << metrics.graph_nodes << ",\n";
      response << "  \"training_records\": " << metrics.training_records << ",\n";
      response << "  \"uptime_ms\": " << metrics.uptime_ms << "\n";
      response << "}\n";

      return buffer_result(response.str());

    } catch (const std::exception& e) {
      std::string error_msg = "{\"status\": \"error\", \"message\": \"";
      error_msg += json_escape(std::string(e.what()));
      error_msg += "\"}";
      return buffer_result(error_msg);
    }
  }
}

// ============================================================================
// Debug / Test Functions
// ============================================================================

/**
 * wasm_echo - Echo input (for testing memory boundary)
 * 
 * Useful for verifying string passing works correctly
 */
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_echo(const char* input) {
    if (!input) {
      return buffer_result("{\"error\": \"Null input\"}");
    }

    std::stringstream response;
    response << "{\"echo\": \"" << json_escape(std::string(input)) << "\"}";
    return buffer_result(response.str());
  }
}

// ============================================================================
// Module Metadata
// ============================================================================

extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_version() {
    return "3.0.0-wasm-phase1";
  }

  EMSCRIPTEN_KEEPALIVE
  const char* wasm_build_date() {
    return __DATE__ " " __TIME__;
  }
}
