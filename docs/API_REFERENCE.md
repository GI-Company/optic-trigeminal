# OpticTrigeminal API Reference

**Version:** 3.0.0  
**Server:** http://localhost:8080  
**Content-Type:** application/json

---

## Overview

OpticTrigeminal exposes 14 REST API endpoints covering:
- **Inference** (3 endpoints): Text understanding and generation
- **Clinical** (3 endpoints): Patient monitoring and charting
- **Training** (8 endpoints): Clinical scenario management
- **Health** (1 endpoint): Server status

---

## Authentication Endpoints

### 1. Sign In
Authenticates against the staff database and issues a bearer token
(256-bit random, stored server-side) for accessing protected endpoints.

**Endpoint:** `POST /api/auth/sign-in`

**Request Body:**
```json
{
  "staff_id": "RN_001",
  "password": "<see credential setup below>"
}
```

**Response (200 OK):**
```json
{
  "token": "tok_9f1c2e...  (256-bit random hex, not a predictable counter)",
  "user_id": "RN_001",
  "staff_name": "Nurse Jane"
}
```

**Response (401 Unauthorized):**
```json
{
  "error": "Invalid credentials"
}
```

**Credential setup:** There are no hardcoded passwords. On first boot, `AuthManager`
seeds `ADMIN_001`, `RN_001`, `CHARGE_001`, `PROVIDER_001`, and `IT_001` from the
corresponding `ACMK_ADMIN_PASSWORD` / `ACMK_RN001_PASSWORD` / `ACMK_CHARGE001_PASSWORD` /
`ACMK_PROVIDER001_PASSWORD` / `ACMK_IT001_PASSWORD` environment variables if set
(min 8 chars); otherwise it generates a random password per account and prints
it once to the server log/stdout:

```
[AuthManager] Generated password for RN_001 (set ACMK_RN001_PASSWORD to override): <random>
```

For local demos, run `./start_demo.sh` instead of the binary directly — it sets
all five accounts to the fixed password `demo1234` so the frontend's Quick Demo
Sign-In buttons work without digging a random password out of the log. See the
README's "Auth credentials" section.

Passwords are stored salted and hashed (not plaintext). Sign-in is rate-limited:
5 failed attempts locks the account for 5 minutes.

---

## Inference API

### 1. Native Inference

**Endpoint:** `POST /api/inference/native/infer`

**Description:** Generate text response using the native inference engine with full reasoning transparency.

**Request:**
```json
{
  "prompt": "what is 2 + 2",
  "max_tokens": 128
}
```

**Parameters:**
- `prompt` (string, required): Input query/prompt
- `max_tokens` (integer, optional): Maximum response length (default: 128, max: 4096)

**Response:**
```json
{
  "prompt": "what is 2 + 2",
  "response": "2 + 2 = 4",
  "type": "mathematics",
  "timestamp": "2025-12-21T07:32:23Z",
  "confidence": 0.95,
  "related_concepts": ["arithmetic", "addition", "numbers"],
  "reasoning_steps": [
    "Identified math domain",
    "Applied MathSpecializer",
    "Evaluated expression"
  ]
}
```

**Status Codes:**
- `200 OK` - Successful inference
- `400 Bad Request` - Invalid prompt
- `500 Internal Server Error` - Engine failure

---

### 2. System Status

**Endpoint:** `GET /api/inference/native/status`

**Description:** Get comprehensive system metrics and performance indicators.

**Request:** No parameters

**Response:**
```json
{
  "status": "ready",
  "vocab_size": 1000000,
  "graph_nodes": 10482,
  "training_records": 11168,
  "episodic_memory": 5000,
  "uptime_ms": 45234,
  "inference_latency_ms": 23.5,
  "last_inference": "2025-12-21T07:32:25Z",
  "embedding_quality": 0.85,
  "safety_precision": 0.912,
  "domain_accuracy": {
    "math": 0.982,
    "logic": 0.965,
    "causality": 0.873
  },
  "multimodal_fusion_quality": 0.824,
  "process_count": 8,
  "load_level": "MEDIUM"
}
```

**Status Codes:**
- `200 OK` - Status retrieved
- `503 Service Unavailable` - System not initialized

---

### 3. Learn from Feedback

**Endpoint:** `POST /api/inference/native/learn`

**Description:** Provide feedback on inference results to update model weights and knowledge graph.

**Request:**
```json
{
  "prompt": "what is 2 + 2",
  "response": "2 + 2 = 4",
  "was_good": true
}
```

**Parameters:**
- `prompt` (string, required): Original input
- `response` (string, required): System's response
- `was_good` (boolean, required): Whether response was correct/helpful

**Response:**
```json
{
  "status": "learned",
  "updated": true,
  "weight_delta": 0.0047,
  "confidence_increase": 0.03,
  "paths_reinforced": 2,
  "timestamp": "2025-12-21T07:32:26Z"
}
```

**Status Codes:**
- `200 OK` - Feedback processed
- `400 Bad Request` - Invalid feedback format
- `500 Internal Server Error` - Learning failed

---

## Clinical API

### 4. Get Patient Observations

**Endpoint:** `POST /api/clinical/observations`

**Description:** Fetch current vital signs and clinical observations for a patient.

**Request:**
```json
{
  "patient_id": 1
}
```

**Parameters:**
- `patient_id` (integer, required): Patient ID (1-6)

**Response:**
```json
{
  "patient_id": 1,
  "vitals": {
    "hr": 85,
    "rr": 16,
    "spo2": 98,
    "bp_sys": 118,
    "bp_dia": 76,
    "temp": 37.2
  },
  "vitals_history": {
    "hr_trend": "stable",
    "spo2_trend": "rising",
    "temp_trend": "stable"
  },
  "observations": [
    {
      "type": "threshold",
      "severity": "warning",
      "description": "Elevated heart rate",
      "confidence": 0.87
    }
  ],
  "crisis": false,
  "crisis_type": null,
  "timestamp": "2025-12-21T07:32:27Z"
}
```

**Status Codes:**
- `200 OK` - Observations retrieved
- `404 Not Found` - Patient not found
- `500 Internal Server Error` - Simulation error

---

### 5. Generate SBAR Scaffold

**Endpoint:** `POST /api/clinical/scaffold`

**Description:** Generate SBAR (Situation, Background, Assessment, Recommendation) documentation scaffold.

**Request:**
```json
{
  "patient_id": 1,
  "vitals": {
    "hr": 120,
    "rr": 24,
    "spo2": 88
  },
  "findings": ["elevated_heart_rate", "tachypnea", "hypoxia"]
}
```

**Parameters:**
- `patient_id` (integer, required): Patient ID
- `vitals` (object, optional): Current vital signs
- `findings` (array, optional): Clinical findings

**Response:**
```json
{
  "sbar": {
    "situation": "Patient experiencing tachypnea (RR 24) with hypoxia (SpO2 88%) and tachycardia (HR 120)",
    "background": "22-year-old admitted with acute respiratory symptoms",
    "assessment": "Possible respiratory compromise with inadequate oxygenation",
    "recommendation": "Consider supplemental oxygen therapy, monitor closely, notify provider if worsening"
  },
  "confidence": 0.92,
  "priority": "HIGH",
  "suggested_actions": [
    "Administer supplemental oxygen",
    "Continuous pulse oximetry monitoring",
    "Notify provider",
    "Prepare for possible escalation"
  ],
  "timestamp": "2025-12-21T07:32:28Z"
}
```

**Status Codes:**
- `200 OK` - Scaffold generated
- `400 Bad Request` - Invalid patient data
- `500 Internal Server Error` - Generation failed

---

### 6. Log Clinical Action

**Endpoint:** `POST /api/clinical/action`

**Description:** Log a nurse intervention with passcode validation and audit trail.

**Request:**
```json
{
  "patient_id": 1,
  "action": "Initiated oxygen therapy at 2 LPM",
  "passcode_validated": true,
  "passcode_hash": "8d969eef6ecad3c29a3a873e7270e26bf60c3e4d",
  "nurse_id": "RN_001",
  "nurse_name": "Sarah Johnson"
}
```

**Parameters:**
- `patient_id` (integer, required): Patient ID
- `action` (string, required): Action description
- `passcode_validated` (boolean, required): Whether 6-digit passcode verified
- `nurse_id` (string, required): Nursing staff identifier
- `nurse_name` (string, optional): Staff display name

**Response:**
```json
{
  "status": "logged",
  "action_id": "ACT_2025_001234",
  "patient_id": 1,
  "action": "Initiated oxygen therapy at 2 LPM",
  "nurse": "Sarah Johnson (RN_001)",
  "timestamp": "2025-12-21T07:32:29Z",
  "chart_entry": {
    "entry_number": 47,
    "type": "intervention",
    "time": "07:32:29"
  },
  "acmk_learning": {
    "recorded": true,
    "confidence_impact": 0.05
  }
}
```

**Status Codes:**
- `200 OK` - Action logged and charted
- `401 Unauthorized` - Passcode validation failed
- `404 Not Found` - Patient not found
- `500 Internal Server Error` - Charting failed

---

## Training API

### 7. Start Training Session

**Endpoint:** `POST /api/training/start`

**Description:** Initialize a clinical training scenario.

**Request:**
```json
{
  "scenario_id": "respiratory_distress",
  "difficulty": "intermediate",
  "patient_preset": "acute_respiratory_failure"
}
```

**Parameters:**
- `scenario_id` (string, required): Scenario identifier
- `difficulty` (string, optional): "beginner" | "intermediate" | "advanced"
- `patient_preset` (string, optional): Patient presentation template

**Response:**
```json
{
  "training_session_id": "TRAIN_2025_0001",
  "status": "running",
  "scenario": {
    "id": "respiratory_distress",
    "title": "Acute Respiratory Distress Response",
    "difficulty": "intermediate",
    "objectives": [
      "Recognize early signs of respiratory compromise",
      "Administer appropriate interventions",
      "Escalate appropriately"
    ]
  },
  "patient": {
    "id": 1,
    "vitals": {
      "hr": 122,
      "rr": 28,
      "spo2": 85,
      "bp_sys": 98,
      "bp_dia": 62
    }
  },
  "elapsed_ms": 0,
  "actions_taken": 0,
  "score": 0.0,
  "timestamp": "2025-12-21T07:32:30Z"
}
```

**Status Codes:**
- `200 OK` - Training started
- `400 Bad Request` - Invalid scenario
- `409 Conflict` - Training already in progress
- `500 Internal Server Error` - Initialization failed

---

### 8. Get Training Status

**Endpoint:** `GET /api/training/status`

**Description:** Get real-time status of active training session.

**Request:** No parameters

**Response:**
```json
{
  "training_session_id": "TRAIN_2025_0001",
  "status": "running",
  "elapsed_ms": 45000,
  "scenario": "respiratory_distress",
  "patient_id": 1,
  "current_vitals": {
    "hr": 118,
    "rr": 24,
    "spo2": 92,
    "bp_sys": 105,
    "bp_dia": 68
  },
  "actions_taken": [
    {
      "time_ms": 5000,
      "action": "Applied pulse oximeter",
      "effectiveness": 0.85
    },
    {
      "time_ms": 12000,
      "action": "Initiated supplemental oxygen 2L",
      "effectiveness": 0.92
    }
  ],
  "score": 0.88,
  "feedback": "Good initial assessment. Consider earlier oxygen administration.",
  "time_remaining_ms": 300000
}
```

**Status Codes:**
- `200 OK` - Status retrieved
- `404 Not Found` - No active training session
- `503 Service Unavailable` - Training system unavailable

---

### 9. Execute Training Action

**Endpoint:** `POST /api/training/action`

**Description:** Execute an action within training scenario.

**Request:**
```json
{
  "action": "administer_oxygen",
  "parameters": {
    "device": "nasal_cannula",
    "lpm": 2
  }
}
```

**Parameters:**
- `action` (string, required): Action identifier
- `parameters` (object, optional): Action-specific parameters

**Response:**
```json
{
  "training_session_id": "TRAIN_2025_0001",
  "action": "administer_oxygen",
  "status": "completed",
  "effectiveness": 0.92,
  "patient_response": {
    "spo2_change": 5,
    "hr_change": -4,
    "feedback": "SpO2 improving"
  },
  "score_delta": 0.08,
  "cumulative_score": 0.96,
  "timestamp": "2025-12-21T07:32:45Z"
}
```

**Valid Actions:**
- `assess_patient` - Perform initial assessment
- `check_vitals` - Measure vital signs
- `administer_oxygen` - Start supplemental oxygen
- `position_patient` - Reposition patient
- `notify_provider` - Call physician/provider
- `prepare_intubation` - Prepare for airway management
- `initiate_iv` - Start intravenous access
- `administer_medication` - Give medication

**Status Codes:**
- `200 OK` - Action executed
- `400 Bad Request` - Invalid action
- `409 Conflict` - Action not available in current state
- `500 Internal Server Error` - Execution failed

---

### 10. Advance Training Tick

**Endpoint:** `POST /api/training/tick`

**Description:** Advance training simulation by time interval (manual tick control).

**Request:**
```json
{
  "tick_ms": 5000
}
```

**Parameters:**
- `tick_ms` (integer, optional): Milliseconds to advance (default: 1000)

**Response:**
```json
{
  "training_session_id": "TRAIN_2025_0001",
  "elapsed_ms": 50000,
  "new_vitals": {
    "hr": 116,
    "rr": 22,
    "spo2": 94,
    "bp_sys": 108,
    "bp_dia": 70
  },
  "new_events": [
    {
      "type": "observation",
      "message": "SpO2 improving"
    }
  ],
  "crisis_status": false,
  "timestamp": "2025-12-21T07:32:50Z"
}
```

**Status Codes:**
- `200 OK` - Tick advanced
- `404 Not Found` - No active training session
- `500 Internal Server Error` - Simulation error

---

### 11. End Training Session

**Endpoint:** `POST /api/training/end`

**Description:** Terminate training scenario and generate performance report.

**Request:**
```json
{
  "early_termination": false
}
```

**Parameters:**
- `early_termination` (boolean, optional): Whether user ended early

**Response:**
```json
{
  "training_session_id": "TRAIN_2025_0001",
  "status": "completed",
  "scenario": "respiratory_distress",
  "duration_ms": 480000,
  "final_score": 0.88,
  "performance": {
    "assessment": "Good",
    "time_to_intervention": "Timely",
    "intervention_selection": "Appropriate",
    "escalation": "Correct"
  },
  "learning_summary": [
    "Successfully identified respiratory compromise",
    "Appropriately administered supplemental oxygen",
    "Correctly escalated to provider"
  ],
  "improvement_areas": [
    "Consider earlier vital sign assessment",
    "Monitor for secondary complications"
  ],
  "transcript": [
    { "time": "00:05", "action": "Applied pulse oximeter", "score": 0.85 },
    { "time": "00:12", "action": "Initiated O2 2L", "score": 0.92 },
    { "time": "00:25", "action": "Notified provider", "score": 0.95 }
  ],
  "timestamp": "2025-12-21T07:40:30Z"
}
```

**Status Codes:**
- `200 OK` - Training ended
- `404 Not Found` - No active training session
- `500 Internal Server Error` - Report generation failed

---

### 12. List Training Scenarios

**Endpoint:** `GET /api/training/list`

**Description:** Get available clinical training scenarios.

**Request:** No parameters

**Response:**
```json
{
  "scenarios": [
    {
      "id": "respiratory_distress",
      "title": "Acute Respiratory Distress Response",
      "category": "Respiratory",
      "difficulty": ["beginner", "intermediate", "advanced"],
      "duration_min": 5,
      "learning_objectives": [
        "Recognize respiratory compromise",
        "Administer oxygen therapy",
        "Escalate appropriately"
      ]
    },
    {
      "id": "sepsis_recognition",
      "title": "Sepsis Recognition and Management",
      "category": "Critical Care",
      "difficulty": ["intermediate", "advanced"],
      "duration_min": 10,
      "learning_objectives": [
        "Identify sepsis signs",
        "Initiate sepsis protocol",
        "Monitor response to treatment"
      ]
    },
    {
      "id": "hypotensive_response",
      "title": "Hypotensive Patient Response",
      "category": "Cardiac",
      "difficulty": ["beginner", "intermediate"],
      "duration_min": 5
    }
  ],
  "count": 3
}
```

**Status Codes:**
- `200 OK` - Scenarios retrieved
- `503 Service Unavailable` - Training system unavailable

---

### 13. Get Training Analytics

**Endpoint:** `GET /api/training/analytics`

**Description:** Get aggregate training statistics and performance analytics.

**Request:** No parameters (query parameters optional: `?period=week` | `?user=RN_001`)

**Response:**
```json
{
  "period": "week",
  "total_sessions": 24,
  "total_duration_ms": 7200000,
  "average_score": 0.82,
  "score_distribution": {
    "excellent": 8,
    "good": 12,
    "satisfactory": 4
  },
  "scenarios_completed": {
    "respiratory_distress": 8,
    "sepsis_recognition": 10,
    "hypotensive_response": 6
  },
  "improvement_trend": "upward",
  "common_errors": [
    "Delayed oxygen administration",
    "Insufficient escalation timing"
  ],
  "timestamp": "2025-12-21T07:45:00Z"
}
```

**Status Codes:**
- `200 OK` - Analytics retrieved
- `503 Service Unavailable` - Analytics system unavailable

---

### 14. Get Training Report

**Endpoint:** `GET /api/training/report`

**Description:** Reconstructs the same rich report `POST /api/training/end` returned live when the session finished -- transcript, learning summary, improvement areas -- from persisted event data, not just the summary metrics. `expected_action_names`/`scenario` are recovered from the scenario definition (a pure function of `scenario_id`) since they aren't themselves in the event log.

**Request:** Query parameters: `?session_id=SESSION_a1b2c3d4`

**Response:**
```json
{
  "training_session_id": "SESSION_a1b2c3d4",
  "status": "COMPLETED",
  "scenario": "Acute Hypotension Management",
  "duration_ms": 120000,
  "final_score": 0.10,
  "performance": {
    "assessment": "Needs improvement -- review this scenario's core objectives",
    "time_to_intervention": "60s to first correct intervention",
    "intervention_selection": "0 of 2 expected interventions performed",
    "escalation": "Provider notified during scenario"
  },
  "learning_summary": ["Correctly performed: Notify Provider (at 60s)"],
  "improvement_areas": ["Expected intervention not performed: Apply IV Fluids", "Expected intervention not performed: Start Vasopressor"],
  "transcript": [{ "time": "60s", "action": "Notify Provider", "score": 0.1 }],
  "timestamp": "2026-08-22T..."
}
```

**Status Codes:**
- `200 OK` - Report generated
- `404 Not Found` - Session not found
- `500 Internal Server Error` - Report generation failed

**Access control:** restricted to the nurse who ran the session, or an
`INSTRUCTOR`/`ADMIN` reviewing it. Previously any authenticated staff member
could pull any other nurse's report by guessing/enumerating `session_id`
(only checked that *a* token was present, not who it belonged to) -- fixed
alongside the Instructor API below, since that's the first place a
different role legitimately needs to read someone else's report.

---

## Instructor API

Manages **cohorts** -- named class rosters for mass/institutional education
adoption. All endpoints require an `INSTRUCTOR`-role token; see
`include/cohort_manager.h` and the `handle_instructor_*` handlers in
`src/server/http_server.cpp`.

### 15. Create Cohort

**Endpoint:** `POST /api/instructor/cohorts/create`

**Request Body:** `{ "name": "NURS 310 -- Fall 2026" }`

**Response:** `{ "cohort_id": "COHORT_a1b2c3d4", "name": "...", "created_at": 1755835200, "student_count": 0 }`

### 16. List Cohorts

**Endpoint:** `GET /api/instructor/cohorts`

**Response:** `{ "cohorts": [ { "cohort_id": "...", "name": "...", "created_at": ..., "student_count": 3 } ] }` -- only cohorts owned by the calling instructor.

### 17. Import Roster

**Endpoint:** `POST /api/instructor/cohorts/roster`

**Request Body:** `{ "cohort_id": "COHORT_a1b2c3d4", "students": [ { "name": "Jane Doe", "external_id": "jane.doe@school.edu" } ] }`

**Description:** Provisions one real, individually-authenticated `RN`-role staff account per student (`STU_xxxxxxxx`) and adds each to the cohort roster.

**Response:** `{ "imported": 1, "credentials": [ { "staff_id": "STU_a1b2c3d4", "name": "Jane Doe", "password": "9f1c2e4a" } ] }` -- the password is plaintext and returned **exactly once**; only a salted hash is ever persisted.

### 18. Get Cohort Dashboard

**Endpoint:** `GET /api/instructor/cohort`

**Request:** Query parameters: `?cohort_id=COHORT_a1b2c3d4`

**Description:** The main instructor view -- per-student session history and scores, plus a cohort-wide "most-missed interventions" breakdown. Built entirely from real `TrainingAnalyticsStore` event data; a student with zero completed sessions shows `avg_score: 0` and an empty `sessions` array rather than a fabricated number. `score` on each session is the same graded 0-100 score (`final_score`) the student saw on their own debrief -- not a separately-derived proxy. Each `session_id` here can be passed to `GET /api/training/report` (#14) for that session's full transcript -- the frontend's per-student session table does exactly this on "View Report".

**Response:**
```json
{
  "cohort": { "cohort_id": "COHORT_a1b2c3d4", "name": "NURS 310 -- Fall 2026", "created_at": 1755835200 },
  "students": [
    {
      "staff_id": "STU_a1b2c3d4", "name": "Jane Doe", "external_id": "jane.doe@school.edu",
      "session_count": 2, "avg_score": 5.0,
      "sessions": [
        { "session_id": "...", "scenario_id": "HYPOTENSION_001", "outcome": "COMPLETED", "score": 0.0, "duration_seconds": 120, "missed_critical_windows": 0 },
        { "session_id": "...", "scenario_id": "HYPOTENSION_001", "outcome": "COMPLETED", "score": 10.0, "duration_seconds": 120, "missed_critical_windows": 0 }
      ]
    }
  ],
  "top_missed_interventions": [ { "scenario_id": "ANAPHYLAXIS_001", "failure": "epinephrine_delay", "count": 4 } ]
}
```

### 19. Remove Student

**Endpoint:** `POST /api/instructor/cohorts/remove-student`

**Request Body:** `{ "cohort_id": "COHORT_a1b2c3d4", "staff_id": "STU_a1b2c3d4" }`

**Description:** Removes the student from the cohort roster. Does not delete their staff account or training history -- only the cohort membership.

### 20. Create Staff Account (Admin)

**Endpoint:** `POST /api/staff/create`

**Request Body:** `{ "staff_id": "INSTRUCTOR_002", "name": "Dr. Jane Lee", "role": "instructor" }`

**Description:** `ADMIN`-only. One-off provisioning for the occasional single account -- most commonly the first `INSTRUCTOR` account for a real school/hospital deployment, since instructors have no self-service signup. `role` is one of `rn`, `charge_nurse`, `provider`, `admin`, `it`, `instructor`.

**Response:** `{ "status": "created", "staff_id": "INSTRUCTOR_002", "role": "instructor", "password": "..." }` -- password shown once, same convention as roster import.

---

## Health Check

### 21. Health Status

**Endpoint:** `GET /health`

**Description:** Simple health check for load balancers and monitoring systems.

**Request:** No parameters

**Response:**
```json
{
  "status": "healthy",
  "timestamp": "2025-12-21T07:45:10Z"
}
```

**Status Codes:**
- `200 OK` - System healthy
- `503 Service Unavailable` - System unhealthy

---

## Error Responses

All endpoints return structured error responses:

```json
{
  "error": "Invalid patient_id",
  "status": 400,
  "timestamp": "2025-12-21T07:45:15Z",
  "request_id": "REQ_2025_abc123"
}
```

---

## Authentication & Authorization

**Current status:** Server-side, enforced on every `/api/clinical/*`,
`/api/training/*`, and `/api/acmk/*` route. Requests without a valid bearer
token get `401`; requests from an authenticated-but-underprivileged role get
`403`. Client-side role checks in the UI are a UX convenience only — they are
not what makes anything secure, so a request that skips the UI (raw curl,
another client) is still subject to the same server-side checks.

**Headers:**
```
Authorization: Bearer <token>
```

Tokens are 256-bit random values (not derived from a counter or timestamp),
expire after 1 hour, and carry the signed-in user's role and patient
assignments server-side (`AuthToken` in `include/auth_manager.h`) — the
request body's own `user_id`/`role`/`patient_id` fields are never trusted for
identity.

**ACMK-OT specifics:** `POST /api/acmk/session/init` defaults to
`mode: "simulation"`. Requesting `mode: "real_world"` requires both an
elevated role (provider/admin/charge_nurse) and the server operator opting in
via `ACMK_ENABLE_REAL_WORLD=1` — otherwise it's rejected with `403`, it never
silently downgrades. Control actions (`pause`/`resume`/`replay`/`freeze`/
`recompute`) require a non-empty `reason` field, validated server-side by
`ACMK::TemporalControlEngine`.

---

## Rate Limiting

**Sign-in (`/api/auth/sign-in`):** 5 failed attempts locks the account for 5
minutes (`AuthManager::kMaxFailedAttempts` / `kLockoutSeconds`).

**Other endpoints:** No rate limiting yet. If you expose this server beyond
localhost, put a reverse proxy with rate limiting in front of it.

---

## Versioning

**Current API Version:** 3.0.0  
**Endpoint Pattern:** `/api/{domain}/endpoint`  
**Future:** Support for `/v1/`, `/v2/` versioning

---

*Last Updated: December 24, 2025*
