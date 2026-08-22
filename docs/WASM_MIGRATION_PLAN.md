# WASM Migration & Frontend Modernization Plan

**Status:** In Progress  
**Target:** Separate HTML, compile C++ to WASM, modern modular UI  
**Timeline:** Phase-based implementation

---

## Architecture Overview

```
optic-trigeminal/
├── src/                          # C++ backend (existing)
├── wasm/                         # NEW: WASM compilation
│   ├── CMakeLists.txt
│   ├── build.sh
│   └── optic-trigeminal.ts       # WASM bindings
├── web/                          # Frontend (refactored)
│   ├── index.html               # Entry point only
│   ├── src/
│   │   ├── main.ts              # App initialization
│   │   ├── api/
│   │   │   ├── client.ts        # HTTP client
│   │   │   ├── types.ts         # Type definitions
│   │   │   └── wasm.ts          # WASM integration
│   │   ├── components/
│   │   │   ├── Dashboard.ts
│   │   │   ├── PatientCard.ts
│   │   │   ├── VitalMonitor.ts
│   │   │   ├── ClinicalChart.ts
│   │   │   ├── NurseNotes.ts
│   │   │   ├── PasscodeModal.ts
│   │   │   ├── SignInModal.ts
│   │   │   ├── TrainingPanel.ts
│   │   │   └── SystemLog.ts
│   │   ├── store/
│   │   │   └── state.ts         # State management
│   │   ├── styles/
│   │   │   ├── index.css        # Global styles
│   │   │   ├── components.css
│   │   │   ├── themes.css
│   │   │   └── responsive.css
│   │   └── utils/
│   │       ├── chart.ts         # Chart utilities
│   │       ├── validation.ts
│   │       └── audit.ts
│   ├── package.json
│   ├── tsconfig.json
│   ├── vite.config.ts           # Build config
│   └── dist/                    # Built output
├── build.sh                      # Master build script
└── docker/                       # Docker support
    └── Dockerfile
```

---

## Critical Safety Boundaries (ENFORCED)

**These guardrails are non-negotiable for a clinical safety system. Any deviation requires explicit architectural review.**

### 1. ✅ HTTP Server Will NEVER Be Compiled Into WASM

**Rule:** HTTP server stays native C++. WASM is compute-only.

**What this means:**
- ❌ Do NOT add `#include "http_server.h"` to WASM bindings
- ❌ Do NOT expose listening sockets in WASM
- ❌ Do NOT move request parsing into WASM
- ❌ Do NOT move route handlers into WASM

**Why:** 
- Browsers cannot safely host servers
- Audit boundaries get blurry
- Debugging becomes hostile
- I/O operations are platform-specific

**What stays in native C++:**
- HTTPServer class and all handler methods
- Socket management (listen, accept, bind)
- TLS/SSL termination
- Request parsing and routing
- Response building

**What can move to WASM:**
- Inference engine logic (inference_engine.cpp)
- Clinical analyzers (clinical_analyzer.cpp)
- Training simulation kernels (training_orchestrator.cpp)
- Data processing (specialization.cpp, neural_components.cpp)

**Verification:** Before Phase 1 is complete, audit that no HTTP server code is in WASM bindings.

---

### 2. ✅ Memory Ownership Protocol (WASM ↔ JS Boundary)

**Rule:** Explicit allocation and deallocation contracts at the WASM boundary.

**String Handling:**
- ✅ **WASM allocates, caller frees:** WASM returns pointers to internally-managed buffers, JS calls `free(ptr)`
- ❌ **Never:** Heap-allocate strings in WASM without a corresponding `free()` function
- ❌ **Never:** Return raw pointers without documenting lifetime

**Protocol (to be implemented in Phase 1):**
```cpp
// WASM: Allocate a result buffer
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  char* wasm_infer(const char* prompt, int max_tokens) {
    // Allocate on WASM heap
    std::string result = engine->infer(prompt, max_tokens);
    // Copy to static buffer OR caller-provided buffer
    // Return pointer to buffer
  }
  
  EMSCRIPTEN_KEEPALIVE
  void wasm_free(void* ptr) {
    // JS will call this to release WASM memory
    // Implementation: Clear static buffer or mark for reuse
  }
}
```

**JavaScript side:**
```typescript
const ptr = wasmModule.exports.wasm_infer(promptPtr, 128);
const result = readCString(ptr);
wasmModule.exports.wasm_free(ptr); // Always call free
```

**Why this matters:**
- Prevents silent memory leaks
- Safari has strict memory constraints
- Training replay must not corrupt state
- Audit trail integrity

---

### 3. ✅ Persistence & Audit Logging: Native C++ Only

**Rule:** Training analytics, audit logs, and persistent data remain in native C++. WASM never writes files.

**What WASM can do:**
- Emit events (return event objects)
- Compute metrics (return computed values)
- Process signals (return processed output)

**What WASM CANNOT do:**
- ❌ Write to filesystem
- ❌ Open file handles
- ❌ Call persistence APIs
- ❌ Access databases
- ❌ Own audit trails

**Implementation:**
```cpp
// WASM: Returns event objects
struct SimulationEvent {
  std::string event_type;
  std::string data;
  uint64_t timestamp;
};

extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_simulate_step(const char* state_json) {
    // Compute new state
    // Return event JSON (no persistence)
    // Native C++ handles persistence
    return json_event.c_str();
  }
}
```

**Native C++ (HTTP server):**
```cpp
// Receives events from WASM and persists them
TrainingAnalyticsStore analytics;
// ... WASM returns event
analytics.append_event(event); // Native persistence
```

**Verification:** Analytics remain in `data/training_analytics/` only. No writes from WASM.

---

### 4. ⚠️ CRITICAL: Role Enforcement Server-Side (Before Production)

**Rule:** All role/permission checks happen server-side. UI gating is secondary.

**Current status:** ❌ Not implemented. UI-only enforcement.

**Must implement before production deployment:**

- [ ] Authentication mechanism (JWT tokens, session IDs, or similar)
- [ ] Role database/store (which staff have which roles)
- [ ] Per-patient assignment tracking (which nurse is assigned to which patient)
- [ ] Action-to-role mapping enforcement in every HTTP handler

**Example fix required in `http_server.cpp`:**
```cpp
// BEFORE (current - WRONG):
HTTPServer::Response HTTPServer::handle_action(const Request& req) {
  // Parse patient_id and action
  // NO role check!
  // ANYONE can perform ANY action
  engine->learn_from_clinical_observation(obs);
  return Response(200, "...");
}

// AFTER (required):
HTTPServer::Response HTTPServer::handle_action(const Request& req) {
  // 1. Extract authentication token/session from request
  std::string auth_token = req.headers["Authorization"];
  
  // 2. Verify token is valid
  if (!verify_token(auth_token)) {
    return Response(401, "{\"error\": \"Unauthorized\"}");
  }
  
  // 3. Get user role from token
  std::string user_role = get_role_from_token(auth_token);
  std::string user_id = get_user_id_from_token(auth_token);
  
  // 4. Parse action request
  int patient_id = parse_patient_id(req);
  std::string action = parse_action(req);
  
  // 5. Check: Is user assigned to this patient?
  if (!is_user_assigned_to_patient(user_id, patient_id)) {
    return Response(403, "{\"error\": \"Not assigned to this patient\"}");
  }
  
  // 6. Check: Does user's role permit this action?
  if (!can_perform_action(user_role, action)) {
    return Response(403, "{\"error\": \"Role does not permit this action\"}");
  }
  
  // 7. Only then proceed
  engine->learn_from_clinical_observation(obs);
  return Response(200, "...");
}
```

**Timeline:** Implement during Phase 1-2 (weeks 1-2), before Phase 3 integrates WASM into training.

---

### 5. ✅ Training/Live Separation: Absolute Boundary

**Rule:** Training mode and live mode are mutually exclusive and isolated.

**Current status:** ✅ Implemented. `training_mode_active_` flag enforced.

**Verification checklist:**
- [ ] Only one training session active at a time (checked in `handle_training_start`)
- [ ] Training routes (`/api/training/*`) require `training_mode_active_ == true`
- [ ] Clinical routes (`/api/clinical/*`) require `training_mode_active_ == false`
- [ ] Analytics tagged with `"mode": "TRAINING"` immutable flag
- [ ] Training data isolated in separate session objects

**This boundary must NOT be violated during WASM integration.**

---

## Architectural Principles

**Before Phase 1 starts, agree on these:**

1. **C++ = I/O, Persistence, Auth**
   - HTTP server, sockets, routing, auth tokens, role checks, file I/O, databases

2. **WASM = Pure Compute**
   - Inference, embeddings, clinical rules, training kernels, signal processing

3. **JavaScript = UI, State, Orchestration**
   - Rendering, event handling, state management, user interactions

4. **Audit Trail = Append-Only, Immutable**
   - Training events marked `"immutable": true`
   - All writes go through native C++ analytics store
   - WASM can emit events, not persist them

---

## Phase 1: Architecture & Setup (Week 1)

### 1.1 Define WASM Memory & Binding Contract (CRITICAL)

**Before writing any WASM code, document these:**

- [ ] **Memory ownership rules**
  - Who allocates strings? (WASM or JS)
  - Who calls free? (JS must call wasm_free)
  - Document in `wasm/BINDINGS_CONTRACT.md`

- [ ] **Function signatures**
  - `wasm_infer(const char* prompt, int max_tokens) -> char*`
  - `wasm_free(void* ptr) -> void`
  - `wasm_get_patient_vitals(int patient_id) -> char*` (returns JSON)
  - All return pointers require wasm_free() call

- [ ] **Audit checklist**
  - [ ] No I/O operations in WASM
  - [ ] No persistence in WASM
  - [ ] No auth logic in WASM
  - [ ] All data returned as JSON strings

### 1.2 Create WASM Build Pipeline
- [x] Design directory structure
- [x] Set up Emscripten SDK
- [x] Create WASM CMakeLists.txt
- [x] Create WASM wrapper C++ code (following binding contract)
- [x] Test basic WASM compilation with minimal inference function
- [x] Verify: No HTTP server code in WASM bindings

### 1.3 Initialize Modern Web Stack (ALREADY DONE)
- [x] Create `web/package.json` with dependencies
- [x] Set up TypeScript configuration
- [x] Configure Vite build system
- [x] Create entry HTML file

### 1.4 Extract Current Frontend Code (ALREADY DONE)
- [x] Parse current 2,896-line HTML
- [x] Identify logical components
- [x] Create component stubs
- [x] Extract CSS to separate files

---

## Phase 1.5: Server-Side Role Enforcement (Week 1-2, Parallel Track)

**This MUST be done before Phase 3 WASM integration. UI-only enforcement is insufficient for clinical safety.**

### Implementation Tasks
- [ ] Create authentication module (`http_server.cpp` additions)
  - [ ] Token generation on sign-in
  - [ ] Token validation in every request
  - [ ] Role extraction from token
  
- [ ] Create role database (`src/role_database.cpp`)
  - [ ] Staff member registry with roles
  - [ ] Per-patient assignment tracking
  - [ ] Action-to-role mapping

- [ ] Add enforcement to all clinical handlers
  - [ ] `handle_action()` - Verify role, patient assignment
  - [ ] `handle_scaffold()` - Verify role can access patient
  - [ ] `handle_observations()` - Verify role can view vitals
  
- [ ] Add enforcement to training handlers
  - [ ] `handle_training_action()` - Verify authenticated user
  - [ ] `handle_training_start()` - Verify role can initiate training

**Verification:** Unit tests that confirm UI cannot bypass server validation.

---

## Phase 2: Component Modularization (Week 2)

### 2.1 Break Down Monolithic HTML
Current structure (single file):
- 2,896 lines of HTML/CSS/JS mixed
- 57 JavaScript functions
- Inline styles and hardcoded layout

Target structure (modular):
- 20+ TypeScript components
- Separated concerns (logic, rendering, styling)
- Reusable component library

### 2.2 Create Core Components

#### Dashboard Component (`src/components/Dashboard.ts`)
```typescript
interface DashboardProps {
  patients: Patient[];
  onPatientSelect: (id: number) => void;
  onRefresh: () => void;
}

class Dashboard {
  constructor(container: HTMLElement, props: DashboardProps);
  render(): void;
  update(patients: Patient[]): void;
}
```

#### PatientCard Component (`src/components/PatientCard.ts`)
```typescript
class PatientCard {
  constructor(patient: Patient, onSelect: (id: number) => void);
  render(): HTMLElement;
  updateVitals(vitals: Vitals): void;
}
```

#### VitalMonitor Component (`src/components/VitalMonitor.ts`)
```typescript
class VitalMonitor {
  constructor(containerId: string);
  updateVital(type: 'hr' | 'spo2' | 'rr' | 'bp', value: number): void;
  renderWaveform(type: string, data: number[]): void;
}
```

#### ClinicalChart Component (`src/components/ClinicalChart.ts`)
```typescript
class ClinicalChart {
  constructor(patientId: number);
  addEntry(entry: ChartEntry): void;
  addIntervention(action: string, timestamp: Date): void;
  render(): HTMLElement;
}
```

#### PasscodeModal Component (`src/components/PasscodeModal.ts`)
```typescript
class PasscodeModal {
  constructor(onValidate: (code: string) => Promise<boolean>);
  show(): void;
  hide(): void;
  render(): HTMLElement;
}
```

#### SignInModal Component (`src/components/SignInModal.ts`)
```typescript
class SignInModal {
  constructor(onConfirm: (role: Role) => void);
  show(): void;
  render(): HTMLElement;
}
```

#### TrainingPanel Component (`src/components/TrainingPanel.ts`)
```typescript
class TrainingPanel {
  constructor(onAction: (action: string) => void);
  startScenario(scenario: Scenario): void;
  updateStatus(status: TrainingStatus): void;
}
```

#### SystemLog Component (`src/components/SystemLog.ts`)
```typescript
class SystemLog {
  constructor(containerId: string);
  log(message: string, type: 'info' | 'warning' | 'error' | 'meta'): void;
  render(): HTMLElement;
}
```

---

## Phase 3: WASM Integration (Week 3)

### 3.1 Create WASM Bindings

**File: `wasm/optic-trigeminal.ts`**
```typescript
// Load and initialize WASM module
export async function initWasm(wasmPath: string): Promise<Module> {
  const response = await fetch(wasmPath);
  const buffer = await response.arrayBuffer();
  return WebAssembly.instantiate(buffer);
}

// WASM function wrappers
export class WasmEngine {
  module: Module;
  
  constructor(module: Module) {
    this.module = module;
  }
  
  // Inference
  infer(prompt: string): InferenceResponse {
    // Call WASM function
  }
  
  // Clinical
  getPatientObservations(patientId: number): Observation[] {
    // Call WASM function
  }
  
  // Training
  startTraining(scenarioId: string): TrainingSession {
    // Call WASM function
  }
}
```

### 3.2 Create WASM C++ Wrapper

**File: `wasm/src/bindings.cpp`**
```cpp
#include <emscripten/emscripten.h>
#include "http_server.h"
#include "inference_engine.h"
#include "clinical_sim.h"

// Inference wrapper
extern "C" {
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_infer(const char* prompt, int max_tokens) {
    // Implementation
  }
  
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_get_patient_vitals(int patient_id) {
    // Implementation
  }
  
  EMSCRIPTEN_KEEPALIVE
  const char* wasm_log_action(const char* action_json) {
    // Implementation
  }
}
```

---

## Phase 4: Type System & State Management (Week 3)

### 4.1 Define Type System

**File: `web/src/api/types.ts`**
```typescript
// Patients
export interface Patient {
  id: number;
  name: string;
  mrn: string;
  room: string;
  admission_diagnosis: string;
  acuity_score: number;
  vitals: Vitals;
}

export interface Vitals {
  hr: number;
  rr: number;
  spo2: number;
  bp_sys: number;
  bp_dia: number;
  temp: number;
}

// Clinical
export interface ClinicalObservation {
  patient_id: number;
  observation_type: string;
  severity: 'info' | 'warning' | 'critical';
  description: string;
  confidence: number;
}

export interface ChartEntry {
  timestamp: Date;
  type: 'intervention' | 'note' | 'observation';
  content: string;
  nurse: string;
}

// Roles
export type Role = 'rn' | 'charge_nurse' | 'provider' | 'admin' | 'it';

export interface RoleCapabilities {
  displayName: string;
  canViewVitals: boolean;
  canChartActions: boolean;
  canAddNotes: boolean;
  canAcceptRecommendations: boolean;
  canViewOrders: boolean;
  canAdmitPatient: boolean;
  canViewAllPatients: boolean;
  canOverrideAlerts: boolean;
  canAccessAdmin: boolean;
}

// Training
export interface TrainingScenario {
  id: string;
  title: string;
  category: string;
  difficulty: 'beginner' | 'intermediate' | 'advanced';
  objectives: string[];
  duration_min: number;
}

export interface TrainingSession {
  id: string;
  scenario_id: string;
  status: 'running' | 'paused' | 'completed';
  elapsed_ms: number;
  score: number;
}
```

### 4.2 State Management

**File: `web/src/store/state.ts`**
```typescript
import { reactive } from 'vue'; // or use plain object

export const appState = reactive({
  // Auth
  signedIn: false,
  currentRole: null as Role | null,
  currentStaffName: '',
  
  // Patient Data
  patients: [] as Patient[],
  selectedPatientId: null as number | null,
  
  // Clinical
  chartEntries: [] as ChartEntry[],
  observations: [] as ClinicalObservation[],
  
  // Training
  trainingActive: false,
  currentTrainingSession: null as TrainingSession | null,
  
  // UI
  viewMode: 'dashboard' as 'dashboard' | 'detail' | 'training',
  showPasscodeModal: false,
  showSignInModal: false,
  
  // Audit
  auditLog: [] as AuditEntry[]
});

export function updatePatient(patient: Patient) {
  const idx = appState.patients.findIndex(p => p.id === patient.id);
  if (idx >= 0) {
    appState.patients[idx] = patient;
  }
}

export function addChartEntry(entry: ChartEntry) {
  appState.chartEntries.push(entry);
}
```

---

## Phase 5: API Integration Layer (Week 4)

### 5.1 HTTP Client

**File: `web/src/api/client.ts`**
```typescript
export class ApiClient {
  private baseUrl = 'http://localhost:8080';
  
  async infer(prompt: string): Promise<InferenceResponse> {
    const response = await fetch(`${this.baseUrl}/api/inference/native/infer`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ prompt })
    });
    return response.json();
  }
  
  async getPatientObservations(patientId: number): Promise<Observation[]> {
    const response = await fetch(`${this.baseUrl}/api/clinical/observations`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ patient_id: patientId })
    });
    return response.json();
  }
  
  async logAction(action: string, patientId: number): Promise<void> {
    await fetch(`${this.baseUrl}/api/clinical/action`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        patient_id: patientId,
        action,
        passcode_validated: true
      })
    });
  }
  
  async startTraining(scenarioId: string): Promise<TrainingSession> {
    const response = await fetch(`${this.baseUrl}/api/training/start`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ scenario_id: scenarioId })
    });
    return response.json();
  }
}
```

### 5.2 WASM Integration

**File: `web/src/api/wasm.ts`**
```typescript
export class WasmClient {
  private wasmModule: any;
  
  async initialize(): Promise<void> {
    const response = await fetch('/wasm/optic-trigeminal.wasm');
    const buffer = await response.arrayBuffer();
    const wasmModule = await WebAssembly.instantiate(buffer);
    this.wasmModule = wasmModule.instance;
  }
  
  infer(prompt: string): string {
    // Call WASM function directly
    const ptr = this.wasmModule.exports.wasm_infer(
      this.stringToPtr(prompt),
      128
    );
    return this.ptrToString(ptr);
  }
  
  private stringToPtr(str: string): number {
    // Convert JS string to WASM memory pointer
  }
  
  private ptrToString(ptr: number): string {
    // Convert WASM memory pointer to JS string
  }
}
```

---

## Phase 6: Build System (Week 4)

### 6.1 Vite Configuration

**File: `web/vite.config.ts`**
```typescript
import { defineConfig } from 'vite';

export default defineConfig({
  root: 'web',
  build: {
    outDir: '../dist/web',
    minify: 'terser',
    sourcemap: true,
    rollupOptions: {
      output: {
        entryFileNames: 'js/[name]-[hash].js',
        chunkFileNames: 'js/[name]-[hash].js',
        assetFileNames: ({ name }: any) => {
          if (name.endsWith('.css')) {
            return 'css/[name]-[hash][extname]';
          }
          return 'assets/[name]-[hash][extname]';
        }
      }
    }
  },
  server: {
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true
      }
    }
  }
});
```

### 6.2 Package.json

**File: `web/package.json`**
```json
{
  "name": "optic-trigeminal-web",
  "version": "3.0.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview",
    "type-check": "tsc --noEmit",
    "lint": "eslint src/"
  },
  "dependencies": {
    "typescript": "^5.3.0"
  },
  "devDependencies": {
    "vite": "^5.0.0",
    "typescript": "^5.3.0",
    "@types/node": "^20.10.0"
  }
}
```

### 6.3 Master Build Script

**File: `build.sh` (updated)**
```bash
#!/bin/bash

echo "=== OpticTrigeminal Build Pipeline ==="

# 1. Build C++ backend
echo "[1/3] Building C++ backend..."
if [ ! -d "build" ]; then
  mkdir build
fi
cd build
cmake ..
make -j$(nproc)
cd ..

# 2. Build WASM
echo "[2/3] Building WASM module..."
cd wasm
./build.sh
cd ..

# 3. Build frontend
echo "[3/3] Building frontend..."
cd web
npm install
npm run build
cd ..

echo "=== Build Complete ==="
echo "Backend: ./build/optic-trigeminal"
echo "WASM: ./dist/wasm/optic-trigeminal.wasm"
echo "Web: ./dist/web/"
```

---

## Phase 7: Testing & Optimization (Week 5)

### 7.1 WASM Compatibility Testing
- [ ] Test WASM module in Chrome
- [ ] Test WASM module in Firefox
- [ ] Test WASM module in Safari
- [ ] Test memory management
- [ ] Test error handling

### 7.2 Performance Optimization
- [ ] Measure bundle size
- [ ] Implement tree shaking
- [ ] Minify CSS/JS
- [ ] Optimize images
- [ ] Implement lazy loading

### 7.3 Browser Compatibility
- [ ] Chrome 90+
- [ ] Firefox 88+
- [ ] Safari 15+
- [ ] Edge 90+
- [ ] Mobile browsers

---

## Directory Structure - Current vs Target

### Current
```
web/
└── index.html (2,896 lines - everything in one file)
```

### Target
```
web/
├── index.html (minimal entry point)
├── package.json
├── tsconfig.json
├── vite.config.ts
├── src/
│   ├── main.ts (200 lines)
│   ├── api/
│   │   ├── client.ts (150 lines)
│   │   ├── types.ts (300 lines)
│   │   └── wasm.ts (100 lines)
│   ├── components/
│   │   ├── Dashboard.ts (150 lines)
│   │   ├── PatientCard.ts (100 lines)
│   │   ├── VitalMonitor.ts (120 lines)
│   │   ├── ClinicalChart.ts (120 lines)
│   │   ├── NurseNotes.ts (80 lines)
│   │   ├── PasscodeModal.ts (100 lines)
│   │   ├── SignInModal.ts (80 lines)
│   │   ├── TrainingPanel.ts (100 lines)
│   │   └── SystemLog.ts (80 lines)
│   ├── store/
│   │   └── state.ts (150 lines)
│   ├── styles/
│   │   ├── index.css (300 lines)
│   │   ├── components.css (400 lines)
│   │   ├── themes.css (150 lines)
│   │   └── responsive.css (200 lines)
│   └── utils/
│       ├── chart.ts (80 lines)
│       ├── validation.ts (60 lines)
│       └── audit.ts (70 lines)
└── dist/
    ├── index.html
    ├── css/
    │   └── [hashed files]
    ├── js/
    │   └── [hashed files]
    └── assets/
```

---

## Benefits of This Approach

### Performance
- **Tree shaking**: Only bundle used code
- **Lazy loading**: Load components on demand
- **WASM**: Direct C++ execution (10-100x faster for compute-heavy tasks)
- **Caching**: Long-lived hashed filenames
- **Minification**: Smaller bundle size

### Maintainability
- **Modular components**: Easy to test and update
- **Type safety**: TypeScript prevents runtime errors
- **Clear separation**: Logic, rendering, styling separated
- **Reusable**: Component library for other projects

### Developer Experience
- **Hot reload**: Instant feedback during development
- **Type checking**: IDE support and error detection
- **Build tooling**: Modern development workflow
- **Source maps**: Easy debugging

### Scalability
- **Horizontal components**: Add new features independently
- **WASM integration**: Scale compute-heavy operations
- **Worker threads**: Offload processing to background
- **PWA-ready**: Progressive Web App capabilities

---

## Implementation Order

1. **Week 1:** Setup (WASM pipeline, web scaffolding)
2. **Week 2:** Modularization (break HTML into components)
3. **Week 3:** WASM integration + type system
4. **Week 4:** Build system + API layer
5. **Week 5:** Testing & optimization

**Total effort:** ~160 hours (2 weeks full-time for experienced developer)

---

## Rollback Plan

If WASM compilation fails:
1. Keep existing HTTP API as fallback
2. Disable WASM, use HTTP-only mode
3. No functionality loss, just performance impact
4. Can retry WASM integration later

---

*Plan created: December 24, 2025*
