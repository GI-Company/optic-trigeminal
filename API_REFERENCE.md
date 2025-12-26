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

**Description:** Generate comprehensive training report for completed session.

**Request:** Query parameters: `?session_id=TRAIN_2025_0001`

**Response:**
```json
{
  "report": {
    "session_id": "TRAIN_2025_0001",
    "scenario": "respiratory_distress",
    "date": "2025-12-21",
    "duration": "8 minutes",
    "score": 0.88,
    "summary": "Demonstrated good clinical assessment and timely intervention...",
    "detailed_timeline": [...],
    "performance_metrics": {...},
    "recommendations": [...]
  }
}
```

**Status Codes:**
- `200 OK` - Report generated
- `404 Not Found` - Session not found
- `500 Internal Server Error` - Report generation failed

---

## Health Check

### 15. Health Status

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

**Current Status:** Client-side only (role-based)  
**Production Requirement:** Server-side JWT validation with role enforcement

**Planned Headers (Post-Production):**
```
Authorization: Bearer <JWT_TOKEN>
X-User-ID: <USER_ID>
X-User-Role: <ROLE>
X-Session-ID: <SESSION_ID>
```

---

## Rate Limiting

**Current:** None (localhost development)  
**Planned (Production):**
- 100 requests/minute per user
- 1000 requests/minute per IP
- Exponential backoff on 429 responses

---

## Versioning

**Current API Version:** 3.0.0  
**Endpoint Pattern:** `/api/{domain}/endpoint`  
**Future:** Support for `/v1/`, `/v2/` versioning

---

*Last Updated: December 24, 2025*
