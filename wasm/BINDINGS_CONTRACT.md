# WASM Bindings Contract
**Status:** Phase 1.1 - Architecture Definition  
**Purpose:** Enforce explicit memory ownership and boundary contracts between C++ WASM and JavaScript

---

## Core Principles

1. **WASM is compute-only** - No I/O, no persistence, no auth
2. **Memory is explicit** - All allocation/deallocation rules documented
3. **Boundaries are enforced** - No hidden allocations or resource leaks
4. **Data flows as JSON** - Strings transferred as UTF-8 JSON payloads

---

## Memory Ownership Model

### Rule 1: WASM Allocates, JavaScript Frees

**Pattern:**
```cpp
// WASM side: Allocate and return pointer
extern "C" {
  char* wasm_infer(const char* prompt, int max_tokens) {
    std::string result = compute_inference(prompt, max_tokens);
    
    // Allocate static buffer (not heap)
    static char buffer[65536]; // 64KB result buffer
    strncpy(buffer, result.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    return buffer; // Pointer to static buffer
  }
  
  void wasm_free(void* ptr) {
    // For static buffers: no-op or clear
    // For heap: delete/free pointer
    if (ptr) {
      // Clear buffer if heap-allocated
      // Static buffers are managed by WASM runtime
    }
  }
}
```

**JavaScript side:**
```typescript
const promptPtr = stringToPtr(prompt);
const resultPtr = wasmModule.exports.wasm_infer(promptPtr, 128);
const result = ptrToString(resultPtr);

// Always call free after reading
wasmModule.exports.wasm_free(resultPtr);
```

**Why:**
- WASM controls its own memory layout
- Prevents dangling pointers
- Safari/Firefox have memory constraints
- Predictable lifecycle

### Rule 2: Static Buffers Preferred Over Heap

**✅ Preferred:**
```cpp
extern "C" {
  const char* wasm_infer(const char* prompt, int max_tokens) {
    static char buffer[65536]; // Fixed allocation
    std::string result = engine->infer(prompt, max_tokens);
    strncpy(buffer, result.c_str(), sizeof(buffer) - 1);
    return buffer;
  }
}
```

**❌ Avoid:**
```cpp
extern "C" {
  const char* wasm_infer(const char* prompt, int max_tokens) {
    std::string result = engine->infer(prompt, max_tokens);
    char* ptr = new char[result.length() + 1]; // Heap alloc
    strcpy(ptr, result.c_str());
    return ptr; // Caller must free (error-prone)
  }
}
```

**Why:**
- Static buffers: Clear lifetime, no fragmentation
- Heap allocations: Risk of leaks, Safari crashes
- Maximum 65KB result per call (sufficient for clinical data)

### Rule 3: No Persistent State in WASM Memory

**❌ WRONG:**
```cpp
// Global state persists between calls
std::map<int, PatientData> patient_cache;

extern "C" {
  void wasm_cache_patient(int id, const char* data) {
    patient_cache[id] = parse_json(data); // Persists!
  }
}
```

**✅ CORRECT:**
```cpp
// Each call is stateless
extern "C" {
  const char* wasm_compute_vitals(const char* patient_json) {
    PatientData patient = parse_json(patient_json);
    VitalAnalysis analysis = compute_vitals(patient);
    return to_json_cstring(analysis);
  }
}
```

**Why:**
- JavaScript manages state (global app state)
- WASM only computes (pure functions)
- Replay engines can't corrupt internal state
- Audit trail stays clean

---

## Function Signatures

### Inference Functions

#### 1. `wasm_infer` - Single inference call

```cpp
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_infer(const char* prompt_json, int max_tokens) {
    // Input: JSON { "prompt": "...", "session_id": "...", ... }
    // Output: JSON { "response": "...", "confidence": 0.95, ... }
    // Returns: Pointer to static buffer (65KB max)
  }
}
```

**Input JSON Schema:**
```json
{
  "prompt": "what is 2 + 2",
  "max_tokens": 128,
  "session_id": "session_123",
  "temperature": 0.7
}
```

**Output JSON Schema:**
```json
{
  "prompt": "what is 2 + 2",
  "response": "2 + 2 = 4",
  "type": "mathematics",
  "confidence": 0.95,
  "related_concepts": ["arithmetic", "addition"],
  "reasoning_steps": ["Identified math domain", "Applied MathSpecializer"]
}
```

**Memory:** Returns pointer to static 65KB buffer

---

### Clinical Functions

#### 2. `wasm_analyze_vitals` - Clinical vital analysis

```cpp
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_analyze_vitals(const char* vitals_json) {
    // Input: JSON { "hr": 120, "spo2": 88, "bp_sys": 85, ... }
    // Output: JSON { "severity": "critical", "observations": [...] }
    // Returns: Pointer to static buffer
  }
}
```

**Input JSON:**
```json
{
  "patient_id": 1,
  "hr": 120,
  "rr": 28,
  "spo2": 88,
  "bp_sys": 85,
  "bp_dia": 55,
  "temp": 39.2,
  "is_crisis": true
}
```

**Output JSON:**
```json
{
  "severity": "critical",
  "alerts": [
    {
      "vital": "spo2",
      "value": 88,
      "threshold": 94,
      "status": "critical"
    }
  ],
  "recommendations": [
    "Initiate oxygen therapy",
    "Notify provider immediately"
  ],
  "confidence": 0.92
}
```

---

### Training Functions

#### 3. `wasm_simulate_scenario_step` - Single training scenario step

```cpp
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_simulate_scenario_step(const char* scenario_json) {
    // Input: Scenario state JSON
    // Output: New state + events (JSON)
    // Returns: Pointer to static buffer
    // NOTE: This is PURE FUNCTION - same input -> same output
  }
}
```

**Input JSON:**
```json
{
  "scenario_id": "RESPIRATORY_001",
  "session_id": "session_456",
  "elapsed_seconds": 30,
  "patient_state": {
    "hr": 115,
    "rr": 32,
    "spo2": 87,
    "consciousness": "alert"
  },
  "nurse_actions": ["elevated_head_of_bed", "applied_oxygen"]
}
```

**Output JSON:**
```json
{
  "scenario_id": "RESPIRATORY_001",
  "session_id": "session_456",
  "elapsed_seconds": 30,
  "new_state": {
    "hr": 112,
    "rr": 28,
    "spo2": 91,
    "consciousness": "alert",
    "status": "improving"
  },
  "events": [
    {
      "type": "vitals_change",
      "data": "SpO2 improved to 91%"
    },
    {
      "type": "ai_observation",
      "data": "Patient responding to oxygen therapy"
    }
  ],
  "immutable": true
}
```

---

### Memory Management

#### 4. `wasm_free` - Deallocate WASM memory

```cpp
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  void wasm_free(void* ptr) {
    // For static buffers: no-op
    // For heap allocations: delete ptr
    // Called by JavaScript after reading result
  }
}
```

**Usage:**
```typescript
const resultPtr = wasmModule.exports.wasm_infer(promptPtr, 128);
const result = ptrToString(resultPtr);
wasmModule.exports.wasm_free(resultPtr); // Always call
```

---

## String Handling Utilities

### JavaScript to WASM (string → pointer)

```typescript
function stringToPtr(str: string): number {
  const encoder = new TextEncoder();
  const data = encoder.encode(str + '\0'); // UTF-8 + null terminator
  
  // Allocate in WASM linear memory
  const ptr = wasmModule.exports.malloc(data.length);
  
  // Write to WASM memory
  const memory = new Uint8Array(wasmModule.exports.memory.buffer);
  memory.set(data, ptr);
  
  return ptr;
}
```

### WASM to JavaScript (pointer → string)

```typescript
function ptrToString(ptr: number): string {
  const memory = new Uint8Array(wasmModule.exports.memory.buffer);
  
  // Read until null terminator
  let length = 0;
  while (memory[ptr + length] !== 0) {
    length++;
  }
  
  // Convert bytes to string
  const bytes = memory.subarray(ptr, ptr + length);
  return new TextDecoder().decode(bytes);
}
```

---

## Boundary Enforcement Checklist

### Before Each WASM Function Is Written:

- [ ] **Is it pure compute?** (Same input → same output)
- [ ] **Does it return JSON?** (Not raw data structures)
- [ ] **Is result < 65KB?** (Clinical data fits)
- [ ] **Does it allocate a static buffer?** (Not heap)
- [ ] **Is there a free() call?** (Caller will call wasm_free)
- [ ] **No I/O operations?** (No file, network, DB access)
- [ ] **No persistence?** (No internal state mutation)
- [ ] **No auth checks?** (Server handles auth, WASM is trusted)

### JavaScript Integration Checklist:

- [ ] **Always call wasm_free()** after reading result
- [ ] **Validate JSON schema** before use
- [ ] **Catch exceptions** from WASM calls
- [ ] **Set reasonable timeouts** (WASM may hang)
- [ ] **Don't cache pointers** (Reuse after wasm_free is undefined)

---

## Boundary Violations (Red Flags)

🚩 **IMMEDIATE STOP - Architectural Review Required:**

- WASM writing to filesystem
- WASM making HTTP requests
- WASM calling native auth functions
- WASM persisting state between calls
- Heap allocations without corresponding free()
- WASM functions that don't return JSON strings
- Global mutable state in WASM
- Callbacks from WASM to JavaScript

---

## Compliance Verification

**Emscripten Build Flags:**
```bash
# Strict memory model
emcc -sSTRICT=1 \
     -sASSERTIONS=1 \
     -sALLOW_MEMORY_GROWTH=0 \
     -sWASM=1 \
     --no-entry
```

**JavaScript Wrapper Audit:**
- [ ] All WASM exports listed in WASM_EXPORTS.ts
- [ ] All exports have corresponding free() calls
- [ ] Memory.buffer access only via stringToPtr/ptrToString
- [ ] No direct pointer arithmetic in JS

---

## Phase 1 Deliverables

1. ✅ This contract document
2. [ ] `wasm/src/bindings.cpp` - WASM C++ wrapper (Phase 1.3)
3. [ ] `web/src/api/wasm-bridge.ts` - JavaScript bridge (Phase 1.3)
4. [ ] Unit tests verifying memory ownership (Phase 7)
5. [ ] CMakeLists.txt with Emscripten flags (Phase 1.2)

---

**Document Status:** Ready for Phase 1.2 (Emscripten setup)  
**Last Updated:** December 25, 2025
