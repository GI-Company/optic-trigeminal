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
#include <vector>
#include <cstdio>
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
namespace {
// A small compiled-in subset of data/medical_research.jsonl so "Test Text
// Inference" in Edge Diagnostics has real clinical content to retrieve
// instead of nothing -- the browser sandbox can't read the server's data/
// directory the way HTTPServer::initialize does, so wasm_init_engine()
// otherwise starts every session from an empty knowledge graph regardless
// of how good the server-side corpus gets.
std::vector<TrainingExample> embedded_clinical_training_data() {
  return {
    {"What are the early signs of sepsis?", "Early signs include fever or hypothermia, tachycardia (HR>90), tachypnea (RR>20), altered mental status, and hypotension. qSOFA flags sepsis risk with any two of: RR>=22, altered mentation, SBP<=100 mmHg.", "medical_clinical"},
    {"What vitals define hypotension requiring nursing escalation?", "Systolic BP below 90 mmHg or MAP below 65 mmHg is generally considered hypotensive and warrants reassessment, provider notification, and consideration of the underlying cause.", "medical_clinical"},
    {"What are signs of respiratory distress in an adult patient?", "Tachypnea, use of accessory muscles, nasal flaring, cyanosis, and SpO2 below 90-92% on room air are hallmark signs requiring immediate assessment.", "medical_clinical"},
    {"What are the FAST criteria for stroke recognition?", "Face drooping, Arm weakness/drift, Speech difficulty, Time to call for help immediately. Onset time is critical for thrombolytic therapy eligibility.", "medical_clinical"},
    {"What is the first-line treatment for anaphylaxis?", "Intramuscular epinephrine (0.3-0.5 mg of 1:1000 concentration) into the anterolateral thigh, given immediately -- delay is the leading cause of fatal anaphylaxis.", "medical_clinical"}
  };
}
}

extern "C" {
  EMSCRIPTEN_KEEPALIVE
  int wasm_init_engine() {
    try {
      g_engine = new NativeInferenceEngine();
      if (!g_engine->initialize_with_training_data(embedded_clinical_training_data())) {
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

/**
 * Extract a numeric field from a flat JSON object using the same
 * colon-then-value naive parsing convention as wasm_infer's string
 * extraction above (no full JSON parser is linked into this module).
 * Returns `fallback` if the key is absent or not a parseable number.
 */
static double extract_json_number(const std::string& json_str, const std::string& key, double fallback) {
  size_t key_pos = json_str.find("\"" + key + "\"");
  if (key_pos == std::string::npos) return fallback;
  size_t colon_pos = json_str.find(":", key_pos);
  if (colon_pos == std::string::npos) return fallback;
  size_t value_start = json_str.find_first_not_of(" \t\r\n", colon_pos + 1);
  if (value_start == std::string::npos) return fallback;
  try {
    size_t consumed = 0;
    double value = std::stod(json_str.substr(value_start), &consumed);
    return value;
  } catch (...) {
    return fallback;
  }
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
      
      // Extract prompt between quotes (naive parsing for Phase 1). Find the
      // colon after the key, then the first quote after that, rather than a
      // hardcoded offset -- a fixed offset silently drops leading
      // characters whenever the JSON has (or lacks) a space after the
      // colon, e.g. "prompt": "What..." previously came out as "hat...".
      std::string prompt;
      size_t key_pos = json_str.find("\"prompt\"");
      if (key_pos != std::string::npos) {
        size_t colon_pos = json_str.find(":", key_pos);
        size_t quote_start = colon_pos != std::string::npos ? json_str.find("\"", colon_pos) : std::string::npos;
        if (quote_start != std::string::npos) {
          size_t prompt_start = quote_start + 1;
          size_t prompt_end = json_str.find("\"", prompt_start);
          if (prompt_end != std::string::npos) {
            prompt = json_str.substr(prompt_start, prompt_end - prompt_start);
          }
        }
      }

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
      // Real threshold-based severity classification, matching the same
      // nursing-practice thresholds ClinicalAnalyzer uses server-side
      // (src/clinical/clinical_analyzer.cpp) -- this used to ignore its
      // input entirely and always return the same hardcoded
      // {severity:"info", alerts:[], recommendations:["Monitor patient"]}
      // regardless of how deranged the actual vitals were.
      std::string json_str(vitals_json);
      double hr = extract_json_number(json_str, "heart_rate", -1);
      double bp_sys = extract_json_number(json_str, "blood_pressure", -1);
      double spo2 = extract_json_number(json_str, "spo2", -1);
      double temp = extract_json_number(json_str, "temperature", -1);
      double rr = extract_json_number(json_str, "respiratory_rate", -1);

      std::vector<std::string> alerts;
      std::vector<std::string> recommendations;
      std::string severity = "info";
      auto escalate = [&](const std::string& s) {
        // info < warning < critical
        if (s == "critical") severity = "critical";
        else if (s == "warning" && severity != "critical") severity = "warning";
      };

      if (hr >= 0) {
        if (hr < 40 || hr > 130) {
          alerts.push_back("Heart rate " + std::to_string((int)hr) + " bpm is critically abnormal");
          recommendations.push_back("Notify provider immediately; consider cardiac monitoring");
          escalate("critical");
        } else if (hr < 50 || hr > 110) {
          alerts.push_back("Heart rate " + std::to_string((int)hr) + " bpm is outside normal range");
          recommendations.push_back("Reassess heart rate within 15 minutes");
          escalate("warning");
        }
      }
      if (spo2 >= 0) {
        if (spo2 < 85) {
          alerts.push_back("SpO2 " + std::to_string((int)spo2) + "% is critically low");
          recommendations.push_back("Apply supplemental oxygen and notify provider");
          escalate("critical");
        } else if (spo2 < 90) {
          alerts.push_back("SpO2 " + std::to_string((int)spo2) + "% is below normal");
          recommendations.push_back("Recheck oxygen saturation; consider supplemental O2");
          escalate("warning");
        }
      }
      if (bp_sys >= 0) {
        if (bp_sys < 80 || bp_sys > 180) {
          alerts.push_back("Systolic BP " + std::to_string((int)bp_sys) + " mmHg is critically abnormal");
          recommendations.push_back("Notify provider immediately; reassess perfusion");
          escalate("critical");
        } else if (bp_sys < 90 || bp_sys > 160) {
          alerts.push_back("Systolic BP " + std::to_string((int)bp_sys) + " mmHg is outside normal range");
          recommendations.push_back("Recheck blood pressure within 15 minutes");
          escalate("warning");
        }
      }
      if (rr >= 0) {
        if (rr < 8 || rr > 30) {
          alerts.push_back("Respiratory rate " + std::to_string((int)rr) + "/min is critically abnormal");
          recommendations.push_back("Assess airway and breathing immediately");
          escalate("critical");
        } else if (rr < 10 || rr > 24) {
          alerts.push_back("Respiratory rate " + std::to_string((int)rr) + "/min is outside normal range");
          recommendations.push_back("Reassess respiratory status within 15 minutes");
          escalate("warning");
        }
      }
      if (temp >= 0) {
        char temp_buf[16];
        snprintf(temp_buf, sizeof(temp_buf), "%.1f", temp);
        if (temp > 39.5 || temp < 35.0) {
          alerts.push_back("Temperature " + std::string(temp_buf) + "C is critically abnormal");
          recommendations.push_back("Notify provider; initiate temperature management protocol");
          escalate("critical");
        } else if (temp > 38.0 || temp < 36.0) {
          alerts.push_back("Temperature " + std::string(temp_buf) + "C is outside normal range");
          recommendations.push_back("Recheck temperature within 30 minutes");
          escalate("warning");
        }
      }

      if (alerts.empty()) {
        recommendations.push_back("Continue routine monitoring");
      }

      std::stringstream response;
      response << "{\n";
      response << "  \"severity\": \"" << severity << "\",\n";
      response << "  \"alerts\": [";
      for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) response << ", ";
        response << "\"" << json_escape(alerts[i]) << "\"";
      }
      response << "],\n";
      response << "  \"recommendations\": [";
      for (size_t i = 0; i < recommendations.size(); ++i) {
        if (i > 0) response << ", ";
        response << "\"" << json_escape(recommendations[i]) << "\"";
      }
      response << "],\n";
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
