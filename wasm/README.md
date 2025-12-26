# OpticTrigeminal WASM Module - Phase 1

**Status:** Architecture Setup Complete  
**Target:** Compile C++ inference engine to WebAssembly for browser execution

---

## Quick Start

### Prerequisites

1. **Emscripten SDK** (required for WASM compilation)
   ```bash
   git clone https://github.com/emscripten-core/emsdk.git
   cd emsdk
   ./emsdk install latest
   ./emsdk activate latest
   source emsdk_env.sh  # On Linux/Mac
   # or emsdk_env.bat on Windows
   ```

2. **CMake 3.16+**
   ```bash
   # macOS
   brew install cmake
   
   # Ubuntu/Debian
   sudo apt-get install cmake
   ```

3. **C++17 Compiler** (comes with Emscripten)

### Build WASM Module

```bash
cd wasm
./build.sh
```

**Output:**
- `dist/wasm/optic-trigeminal.wasm` - WebAssembly module (~2-5MB)
- `dist/wasm/optic-trigeminal.js` - JavaScript loader

### Integration in Web App

The WASM module is loaded and used via the `WasmBridge` class:

```typescript
// In web/src/api/wasm-bridge.ts
import { initializeWasm, getWasmBridge } from '@api/wasm-bridge';

// Load at app startup
const wasm = await initializeWasm('/dist/wasm/optic-trigeminal.wasm');

// Use in inference
const result = wasm.infer('what is 2 + 2', 128);
console.log(result.response); // "2 + 2 = 4"
```

---

## Architecture: Memory & Safety

### Critical Contract: BINDINGS_CONTRACT.md

All WASM bindings follow explicit memory ownership rules:

1. **WASM allocates result buffer**
   - Static 65KB buffer per call
   - No heap fragmentation
   - Safe for Safari/Firefox memory limits

2. **JavaScript must call `wasm_free()` after reading**
   - Prevents stale pointer access
   - Enables buffer reuse
   - Audit trail: all deallocations tracked

3. **No persistent state in WASM**
   - Each call is pure (same input → same output)
   - Prevents corruption across calls
   - Training replay is deterministic

4. **Data flows as JSON**
   - Not binary structures
   - Human-readable in logs
   - Easier debugging

### Memory Layout

```
WASM Heap (16MB fixed, no growth)
├── Static result_buffer (65KB)
│   └── Reused for all function results
├── Inference engine state
│   ├── Graph nodes (10K+ nodes)
│   ├── Embeddings (256D vectors)
│   └── Vocabulary (1M tokens)
└── Temporary allocations
    └── Freed after each call
```

**Why "no growth":**
- Clinical systems require predictable memory
- Browser WASM has memory constraints
- Prevents out-of-memory surprises

---

## Phase 1 Functions (Implemented)

### Inference

```cpp
const char* wasm_infer(const char* request_json, int max_tokens)
```

- Input: JSON with `prompt` field
- Output: JSON with `response`, `confidence`, `related_concepts`
- Returns: Pointer to static 65KB buffer

### Clinical Analysis

```cpp
const char* wasm_analyze_vitals(const char* vitals_json)
```

- Input: JSON with `hr`, `spo2`, `bp_sys`, etc.
- Output: JSON with `severity`, `alerts`, `recommendations`
- Returns: Pointer to static 65KB buffer

### Training Simulation

```cpp
const char* wasm_simulate_scenario_step(const char* scenario_json)
```

- Input: Scenario state JSON
- Output: New state + events JSON
- Returns: Pointer with `immutable: true` flag
- **CRITICAL:** Pure function - no side effects

### Memory Management

```cpp
void wasm_free(void* ptr)
```

- Called by JavaScript after reading result
- For static buffers: no-op (safe to call multiple times)
- For future heap allocations: actual deallocation

### Health & Debug

```cpp
const char* wasm_health()
const char* wasm_echo(const char* input)
const char* wasm_version()
```

---

## Directory Structure

```
wasm/
├── README.md                    # This file
├── BINDINGS_CONTRACT.md         # Memory ownership rules
├── CMakeLists.txt              # Emscripten build config
├── build.sh                    # Build script
├── src/
│   └── bindings.cpp           # WASM C++ wrapper
└── build/                      # Generated build files (git-ignored)
```

---

## Development Workflow

### 1. Modify WASM Bindings

Edit `src/bindings.cpp` to add or change WASM functions.

**Rules:**
- All inputs/outputs as JSON strings
- Use static buffers (not heap)
- No I/O operations
- No auth logic
- Pure functions only

### 2. Rebuild WASM

```bash
cd wasm
./build.sh
```

### 3. Test in Browser

The WASM bridge will load and test the new function:

```bash
cd web
npm run dev
# Open http://localhost:5173
```

### 4. Verify Output

Check browser console for any WASM errors:

```typescript
const wasm = getWasmBridge();
console.log(wasm.health()); // Should show engine metrics
```

---

## Phase 1 Checklist

- [x] Define memory ownership contract (BINDINGS_CONTRACT.md)
- [x] Create Emscripten CMakeLists.txt
- [x] Create build.sh script
- [x] Implement minimal bindings.cpp (5 functions)
- [x] Create JavaScript bridge (wasm-bridge.ts)
- [ ] **Next: Phase 1.2** - Set up Emscripten SDK & test compilation
- [ ] **Then: Phase 1.3** - Verify WASM compiles without errors
- [ ] **Then: Phase 1.5** - Implement server-side role enforcement (parallel)

---

## Troubleshooting

### Build Fails: "emcmake not found"

**Solution:** Source Emscripten environment

```bash
source /path/to/emsdk/emsdk_env.sh
```

### WASM Module Too Large (> 5MB)

**Solution:** Enable optimization in CMakeLists.txt

```cmake
set(WASM_FLAGS "${WASM_FLAGS} -O3 -flto")
```

### JavaScript Can't Load WASM

**Solution:** Check CORS headers and file path

```typescript
const wasm = await initializeWasm('/dist/wasm/optic-trigeminal.wasm');
// Verify file exists: dist/wasm/optic-trigeminal.wasm
```

### Memory Errors in Safari

**Solution:** Ensure using static buffers, not heap

```cpp
// ✅ Correct
static char buffer[65536];
strcpy(buffer, result.c_str());
return buffer;

// ❌ Wrong
char* buffer = new char[result.length()];
return buffer; // Safari will crash
```

---

## Next Phases

- **Phase 2:** Component modularization (web frontend)
- **Phase 3:** WASM integration into training mode
- **Phase 4:** Type system & state management
- **Phase 5:** API integration & HTTP client
- **Phase 6:** Build system (Vite + WASM)
- **Phase 7:** Testing & optimization

---

## References

- [Emscripten Documentation](https://emscripten.org/docs/)
- [WebAssembly Memory Model](https://webassembly.github.io/spec/core/exec/runtime.html#memory)
- [BINDINGS_CONTRACT.md](./BINDINGS_CONTRACT.md) - Memory ownership rules
- [WASM_MIGRATION_PLAN.md](../WASM_MIGRATION_PLAN.md) - Full migration timeline

---

**Phase 1 Status:** ✅ Architecture Ready - Ready for Emscripten Setup
