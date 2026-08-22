# Architecture Audit: Critical Safety Boundaries
**Date:** December 25, 2025  
**Status:** Pre-WASM Migration  
**Phase:** Step 1 - Architectural Verification

---

## Executive Summary

Audit of OpticTrigeminal backend against expert architectural review recommendations. **4 of 5 critical boundaries are implemented**. **1 critical gap identified** in role enforcement at server level.

---

## Critical Recommendations Audit

### 1. ✅ HTTP Server Not Coupled to Compute Logic

**Recommendation:** "Do NOT compile the HTTP server into WASM. WASM = pure engine, HTTP server stays native."

**Current State:** **PASS**

**Evidence:**
- `http_server.h:38-40`: HTTP server **owns** NativeInferenceEngine (doesn't embed it)
  ```cpp
  std::unique_ptr<NativeInferenceEngine> engine;
  ClinicalSimulator sim;
  std::unique_ptr<ClinicalAnalyzer> analyzer;
  ```
- `http_server.cpp:18-20`: Initialization pattern - clear separation
  ```cpp
  engine = std::make_unique<NativeInferenceEngine>();
  sim.initialize(6);
  analyzer = std::make_unique<ClinicalAnalyzer>();
  ```
- **No server logic in inference**: Compute stays in `inference_engine.cpp`
- **No I/O in compute**: Inference engine doesn't handle HTTP, sockets, or persistence

**Status:** ✅ Safe to migrate inference logic to WASM

**Action:** None - design is correct

---

### 2. ⚠️ Memory Ownership & String Handling

**Recommendation:** "Lock down memory ownership early. Define who allocates, who frees. Lifetime rules. Never heap-allocate strings in WASM without a free function."

**Current State:** **PENDING DEFINITION**

**Evidence:**
- `types.h:39-59`: Embedding struct with managed vectors (safe)
- `inference_engine.cpp`: Uses std::string (C++ managed - safe)
- **CONCERN:** WASM bindings don't exist yet, so no C++ ↔ JS boundary defined

**Gaps:**
- [ ] No memory ownership protocol documented
- [ ] No buffer allocation rules for WASM ↔ JS strings
- [ ] No free() function signatures defined
- [ ] No lifecycle documentation for returned data

**Status:** ⚠️ Requires explicit protocol before Phase 1 WASM work

**Action:** Define WASM memory contract in WASM_MIGRATION_PLAN.md

---

### 3. ✅ Analytics Persistence (Append-Only, Not in WASM)

**Recommendation:** "Keep training analytics out of WASM. Analytics persistence should remain native C++, append-only, file or DB-backed."

**Current State:** **PASS**

**Evidence:**
- `training_analytics.h:54-90`: TrainingAnalyticsStore class
  - File-backed: `std::ofstream event_stream_`
  - Append-only: `.open(master_log, std::ios::app)` in `training_analytics.cpp:26`
  - Immutable flag: `"immutable": true` in event JSON (line 31)
- `training_analytics.cpp:49-54`: Append pattern only
  ```cpp
  void TrainingAnalyticsStore::append_event(const TrainingEvent& event) {
      if (!initialized_) return;
      event_stream_ << event.to_json_string();
      event_stream_.flush();
  }
  ```
- **No WASM access:** Analytics store is HTTP server only

**Status:** ✅ Audit trail is defensible

**Action:** None - design is correct. Ensure file location is configurable for production (add to TODO list)

---

### 4. ❌ CRITICAL GAP: Role Enforcement Only at UI Level

**Recommendation:** "Enforce role gating in two places: Hidden in UI AND rejected server-side. Do not rely on UI gating alone."

**Current State:** **FAIL - Server-side enforcement missing**

**Evidence:**

#### What's Missing:
- `http_server.cpp:533-589` (handle_action): 
  ```cpp
  // NO role validation!
  // Parses patient_id and action, but does NOT check:
  // - Is the user authorized to perform this action?
  // - Is the user assigned to this patient?
  // - Does their role permit nurse interventions?
  ```

- `http_server.cpp:692-739` (handle_training_action):
  ```cpp
  // NO role validation!
  // Accepts nurse_id but does NOT:
  // - Verify the identity matches a valid staff member
  // - Check if they're authorized for this scenario
  // - Enforce role-based action restrictions
  ```

- `http_server.cpp:591-639` (handle_training_start):
  ```cpp
  // Parses nurse_role, but does NOT enforce it
  std::string nurse_role = "RN";  // Default - could be spoofed!
  size_t role_pos = req.body.find("\"nurse_role\"");
  // ... parses it, but never validates it against stored credentials
  ```

#### What Exists (UI Level Only):
- `web/src/main.ts:46-120`: Role capabilities defined in UI
  ```typescript
  const roleCapabilities: Record<Role, RoleCapabilities> = {
    rn: { canViewVitals: true, canChartActions: true, ... },
    charge_nurse: { ... },
    // ... UI hides buttons based on role
  ```
- `web/src/api/types.ts:7-24`: Role types defined
- **Problem:** UI hides actions but server accepts anything

**Impact Assessment:**
- **Severity:** 🔴 Critical for HIPAA/clinical safety
- **Vulnerability:** Any client can bypass role restrictions
  - Send `{"role": "admin"}` and execute admin actions
  - Impersonate another nurse (no identity verification)
  - Override clinical restrictions
- **Training safety:** Training mode doesn't enforce role restrictions
- **Audit trail:** Analytics log what happened, but don't prevent unauthorized actions

**Gaps:**
- [ ] No authentication mechanism (tokens, sessions, credentials)
- [ ] No role database/store
- [ ] No per-patient assignment tracking
- [ ] No action-to-role mapping enforcement
- [ ] No request signing or integrity checks

**Status:** ❌ Must fix before production use

**Action:** Add server-side role enforcement (see Step 2 update)

---

### 5. ✅ Training vs. Live Separation

**Recommendation:** "Preserve training/live safety boundary. Training immutability, session isolation, audit tagging."

**Current State:** **PASS**

**Evidence:**
- `http_server.h:45-50`: Training state isolated
  ```cpp
  std::unique_ptr<ScenarioRuntime> active_scenario_;
  bool training_mode_active_;
  std::map<std::string, TrainingSession> training_sessions_;
  std::unique_ptr<TrainingAnalyticsStore> analytics_store_;
  ```
- `http_server.cpp:592-593` (handle_training_start): Mode check
  ```cpp
  if (training_mode_active_) {
      return Response(400, "{\"error\": \"Training session already active...\"}");
  }
  ```
- `http_server.cpp:654, 693, 742, 786`: All training handlers check `training_mode_active_`
- `training_analytics.cpp:30-31`: Immutability tag
  ```json
  "provenance": {
    "mode": "TRAINING",
    "immutable": true
  }
  ```
- **Single-session constraint:** Only one training scenario active at a time
- **Analytics isolation:** Separate from live clinical data

**Status:** ✅ Training boundary is enforced

**Action:** None - design is correct

---

## Summary Table

| Boundary | Status | Evidence | Action |
|----------|--------|----------|--------|
| HTTP Server not in WASM | ✅ PASS | Separate ownership, no coupling | None |
| Memory ownership protocol | ⚠️ PENDING | No WASM bindings yet | Document before Phase 1 |
| Analytics append-only & native | ✅ PASS | File-backed NDJSON, immutable flags | None |
| **Role enforcement server-side** | ❌ **FAIL** | No auth, no role checks in handlers | **IMPLEMENT BEFORE PRODUCTION** |
| Training/live separation | ✅ PASS | Flagged, isolated, single-session | None |

---

## Recommendation: Proceed with Caution

**✅ SAFE TO CONTINUE with WASM migration**, PROVIDED:

1. **Before Phase 1 WASM setup:**
   - [ ] Document WASM memory ownership contract
   - [ ] Update WASM_MIGRATION_PLAN.md with memory rules

2. **Before Phase 3 (WASM integration into training):**
   - [ ] Implement server-side role enforcement in HTTP handlers
   - [ ] Add authentication mechanism (tokens, session management)
   - [ ] Add role database/store
   - [ ] Add per-patient assignment enforcement
   - [ ] Test that UI cannot bypass server validation

3. **Before production deployment:**
   - [ ] HIPAA audit trail review
   - [ ] Role-based access control test suite
   - [ ] Penetration testing on clinical endpoints

---

## Next Steps

**Step 1 (Current):** ✅ Audit complete  
**Step 2 (Next):** Update WASM_MIGRATION_PLAN.md with guardrails  
**Step 3 (Then):** Begin Phase 1 WASM pipeline setup  

**Timeline:** All gaps can be closed during Phase 1-2 (weeks 1-2) without blocking WASM architecture.

---

**Auditor:** Code review via Zencoder  
**Confidence:** High (based on direct code examination)  
**Recommendation:** Proceed to Step 2
