# Phase 1.5: Server-Side Role Enforcement Implementation Guide

**Status:** Skeleton Created (auth_manager.h/cpp)  
**Timeline:** Parallel track during Phase 1-2  
**Priority:** CRITICAL - Must complete before Phase 3 WASM integration

---

## Overview

The architecture audit identified a **critical gap**: Role enforcement is UI-only. Any client can bypass the frontend and call HTTP endpoints directly with any role.

**Phase 1.5 implements server-side validation** to ensure every sensitive action is authorized before execution.

---

## Architecture

```
HTTP Request
    ↓
Parse Authorization Header
    ↓
Verify Token with AuthManager
    ↓
Check Role-Based Permissions
    ↓
Check Patient Assignment
    ↓
✅ Allow or ❌ Deny
```

---

## Implementation Steps

### Step 1: Complete AuthManager Class

**File:** `include/auth_manager.h` (skeleton created)

Implement these methods:

```cpp
// Authentication
AuthToken authenticate(const std::string& staff_id, const std::string& password);
bool verify_token(const std::string& token_id);
AuthToken* get_token(const std::string& token_id);
void revoke_token(const std::string& token_id);

// Authorization  
bool can_perform_action(AuthToken* token, Permission perm);
bool is_user_assigned_to_patient(AuthToken* token, int patient_id);
bool can_view_patient_vitals(AuthToken* token, int patient_id);
bool can_chart_on_patient(AuthToken* token, int patient_id, const std::string& action);
bool can_initiate_training(AuthToken* token);

// Staff Management
bool add_staff_member(const StaffMember& staff);
bool assign_patient_to_staff(const std::string& staff_id, int patient_id);

// Audit
void log_auth_decision(const std::string& user_id, const std::string& action, bool allowed);
```

**Effort:** 8-16 hours

---

### Step 2: Integrate with HTTP Handlers

**File:** `src/http_server.cpp`

#### 2.1: Add Authentication Header Parsing

```cpp
// In HTTPServer::handle_client():
Request parse_http_request(...) {
  // ... existing parsing ...
  
  // Extract Authorization header
  // Format: "Authorization: Bearer <token_id>"
  auto auth_it = request.headers.find("Authorization");
  if (auth_it != request.headers.end()) {
    std::string auth_header = auth_it->second;
    // Parse "Bearer <token>"
    size_t space = auth_header.find(' ');
    if (space != std::string::npos) {
      request.auth_token = auth_header.substr(space + 1);
    }
  }
  
  return request;
}
```

#### 2.2: Protect Clinical Endpoints

**Example: `handle_action()` - Charting on patient**

```cpp
// BEFORE (current - insecure):
HTTPServer::Response HTTPServer::handle_action(const Request& req) {
  int patient_id = parse_patient_id(req);
  std::string action = parse_action(req);
  
  // NO VALIDATION - ANYONE can chart on ANY patient!
  engine->learn_from_clinical_observation(obs);
  return Response(200, "...");
}

// AFTER (Phase 1.5 - secure):
HTTPServer::Response HTTPServer::handle_action(const Request& req) {
  // Step 1: Verify authentication
  AuthToken* token = g_auth_manager->get_token(req.auth_token);
  if (!token) {
    return Response(401, R"({"error": "Unauthorized - invalid or missing token"})");
  }
  
  // Step 2: Parse request
  int patient_id = parse_patient_id(req);
  std::string action = parse_action(req);
  std::string nurse_id = token->user_id;  // Use authenticated user, not claimed identity
  
  // Step 3: Check authorization
  if (!g_auth_manager->can_chart_on_patient(token, patient_id, "intervention")) {
    g_auth_manager->log_auth_decision(nurse_id, "CHART_DENY_" + action, false, 
                                      "User not authorized or not assigned to patient");
    return Response(403, R"({"error": "Forbidden - insufficient permissions"})");
  }
  
  // Step 4: Log decision
  g_auth_manager->log_auth_decision(nurse_id, "CHART_" + action, true);
  
  // Step 5: Process action
  engine->learn_from_clinical_observation(obs);
  
  return Response(200, R"({"status": "action_logged", "recorded": true})");
}
```

#### 2.3: Protect Training Endpoints

**Example: `handle_training_start()` - Initiate training session**

```cpp
HTTPServer::Response HTTPServer::handle_training_start(const Request& req) {
  // Step 1: Verify authentication
  AuthToken* token = g_auth_manager->get_token(req.auth_token);
  if (!token) {
    return Response(401, R"({"error": "Unauthorized"})");
  }
  
  // Step 2: Check if user can initiate training
  if (!g_auth_manager->can_initiate_training(token)) {
    return Response(403, R"({"error": "Role cannot initiate training"})");
  }
  
  // Step 3: Prevent concurrent training sessions (security)
  if (training_mode_active_) {
    return Response(400, R"({"error": "Training already in progress"})");
  }
  
  // Step 4: Use authenticated user's ID (not claimed identity)
  std::string nurse_id = token->user_id;
  std::string scenario_id = parse_scenario_id(req);
  
  // ... rest of training start ...
  
  g_auth_manager->log_auth_decision(nurse_id, "TRAINING_START_" + scenario_id, true);
  
  training_mode_active_ = true;
  return Response(200, R"({"status": "training_started"})");
}
```

#### 2.4: Protect Observation Endpoints

**Example: `handle_observations()` - View patient vitals**

```cpp
HTTPServer::Response HTTPServer::handle_observations(const Request& req) {
  // Step 1: Verify authentication
  AuthToken* token = g_auth_manager->get_token(req.auth_token);
  if (!token) {
    return Response(401, R"({"error": "Unauthorized"})");
  }
  
  // Step 2: Parse request
  int patient_id = parse_patient_id(req);
  
  // Step 3: Check authorization
  if (!g_auth_manager->can_view_patient_vitals(token, patient_id)) {
    return Response(403, R"({"error": "Cannot access this patient's data"})");
  }
  
  // Step 4: Process request
  // ... get observations ...
  
  return Response(200, observations_json);
}
```

**Effort:** 4-8 hours per endpoint (10 endpoints total = 40-80 hours)

---

### Step 3: Add Sign-In Endpoint

**New endpoint:** `POST /api/auth/sign-in`

```cpp
HTTPServer::Response HTTPServer::handle_sign_in(const Request& req) {
  // Parse credentials from request
  std::string staff_id = parse_staff_id(req);
  std::string password = parse_password(req);
  
  // Authenticate with AuthManager
  AuthToken token = g_auth_manager->authenticate(staff_id, password);
  
  // Check if authentication succeeded
  if (token.token_id.empty()) {
    return Response(401, R"({"error": "Invalid credentials"})");
  }
  
  // Return token to client
  std::stringstream response;
  response << "{\n";
  response << R"(  "token": ")" << token.token_id << "\",\n";
  response << R"(  "user_id": ")" << token.user_id << "\",\n";
  response << R"(  "staff_name": ")" << token.staff_name << "\",\n";
  response << R"(  "role": ")" << role_to_string(token.role) << "\",\n";
  response << R"(  "expires_at": )" << token.expires_at << "\n";
  response << "}\n";
  
  return Response(200, response.str());
}

// Register the route
routes["/api/auth/sign-in"] = [this](const Request& r) { return handle_sign_in(r); };
```

**Effort:** 2-4 hours

---

### Step 4: Update Frontend to Use Tokens

**File:** `web/src/api/client.ts`

```typescript
// Store token from sign-in
this.authToken: string | null = null;

async signIn(staffId: string, password: string): Promise<{ token: string; role: string }> {
  const response = await fetch(`${this.baseUrl}/api/auth/sign-in`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ staff_id: staffId, password })
  });
  
  if (!response.ok) {
    throw new Error('Sign-in failed');
  }
  
  const data = await response.json();
  this.authToken = data.token; // Store for future requests
  return data;
}

// Include token in all API requests
private async request<T>(...) {
  const options: RequestInit = {
    method,
    headers: {
      'Content-Type': 'application/json',
      'Authorization': `Bearer ${this.authToken}` // Add this!
    }
  };
  // ... rest of request logic ...
}
```

**Effort:** 2-4 hours

---

## Testing Strategy

### Unit Tests (Required)

```cpp
// tests/test_auth_manager.cpp

void test_authenticate_valid_user() {
  AuthToken token = auth_mgr->authenticate("RN_001", "password123");
  assert(!token.token_id.empty());
  assert(token.user_id == "RN_001");
}

void test_authenticate_invalid_password() {
  AuthToken token = auth_mgr->authenticate("RN_001", "wrongpassword");
  assert(token.token_id.empty());
}

void test_role_based_permission() {
  AuthToken token = auth_mgr->authenticate("RN_001", "password");
  assert(auth_mgr->can_perform_action(&token, Permission::CHART_ACTIONS));
  assert(!auth_mgr->can_perform_action(&token, Permission::ADMIN));
}

void test_patient_assignment() {
  AuthToken token = auth_mgr->authenticate("RN_001", "password");
  assert(auth_mgr->is_user_assigned_to_patient(&token, 1));
  assert(!auth_mgr->is_user_assigned_to_patient(&token, 999));
}
```

### Integration Tests (Required)

```bash
# Test that UI cannot bypass server validation
curl -X POST http://localhost:8080/api/clinical/action \
  -H "Content-Type: application/json" \
  -d '{"patient_id": 1, "action": "give_medication"}'
# Expected: 401 Unauthorized (no token)

# Test with valid token
curl -X POST http://localhost:8080/api/clinical/action \
  -H "Authorization: Bearer token_xxx" \
  -d '{"patient_id": 1, "action": "give_medication"}'
# Expected: 200 OK (authorized) or 403 Forbidden (insufficient permission)
```

**Effort:** 8-16 hours

---

## Security Best Practices

### Password Storage

❌ **WRONG:**
```cpp
std::string password = "password123";  // Never store plaintext!
```

✅ **CORRECT:**
```cpp
// Use bcrypt or similar
std::string password_hash = bcrypt::hash("password123");
bool valid = bcrypt::verify("password123", password_hash);
```

### Token Generation

❌ **WRONG:**
```cpp
std::string token = "token_" + std::to_string(user_id);
```

✅ **CORRECT:**
```cpp
// Cryptographically secure random
unsigned char token[32];
RAND_bytes(token, 32);
// Convert to hex string
```

### Token Expiry

Always set expiry times:
```cpp
token.expires_at = std::time(nullptr) + 3600; // 1 hour
```

### HTTPS Requirement

In production, ONLY use HTTPS. Tokens should be sent only over encrypted channels.

---

## Timeline

**Week 1 (Days 1-2):** Complete AuthManager class
**Week 1 (Days 3-5):** Integrate with HTTP handlers (5 endpoints)
**Week 2 (Days 1-3):** Complete remaining endpoints (5 more)
**Week 2 (Days 4-5):** Testing & security review

**Total:** ~80-120 hours (1-2 weeks full-time)

---

## Verification Checklist

Before proceeding to Phase 2:

- [ ] All AuthManager methods implemented
- [ ] Sign-in endpoint works (`POST /api/auth/sign-in`)
- [ ] Clinical endpoints require valid token
- [ ] Training endpoints require valid token
- [ ] Role-based permissions enforced server-side
- [ ] Patient assignment checked for all patient-specific actions
- [ ] Audit log records all auth decisions
- [ ] UI cannot bypass server validation (integration tests)
- [ ] Tokens expire correctly
- [ ] Expired tokens are rejected
- [ ] Invalid tokens are rejected
- [ ] Passwords are hashed (never plaintext)
- [ ] HTTPS enforced in production config

---

## Integration with WASM (Phase 3)

⚠️ **Critical:** WASM is compute-only. Auth happens server-side.

```
JavaScript → HTTP Request + Token
    ↓
HTTP Server (C++)
    ├─ Verify Token (AuthManager)
    ├─ Check Permissions (AuthManager)
    └─ Call WASM if authorized
        ↓
    WASM (compute-only, no auth)
        ↓
    Return result
    ↓
HTTP Response to JavaScript
```

WASM never sees auth tokens or makes authorization decisions.

---

## References

- **ARCHITECTURE_AUDIT.md** - Gap analysis
- **WASM_MIGRATION_PLAN.md** - Critical Safety Boundaries section
- **include/auth_manager.h** - Header file
- **src/auth_manager.cpp** - Skeleton implementation

---

**Phase 1.5 Status:** Skeleton ready, implementation blocked on design decisions

**Next:** Choose between:
1. Simple in-memory token store (for development/demo)
2. File-based persistent token store
3. Redis-backed token store (for production)

Recommend: Option 2 (file-based) for Phase 1, migrate to Option 3 in production hardening phase.
