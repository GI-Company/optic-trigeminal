/**
 * Authentication & Authorization Manager
 * 
 * Phase 1.5: Server-side role enforcement (PARALLEL TRACK)
 * 
 * Critical requirement from architecture audit:
 * - UI-only role enforcement is insufficient for clinical safety
 * - All sensitive actions must be validated server-side
 * - No client request can bypass role/permission checks
 */

#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>
#include <ctime>
#include <memory>

// ============================================================================
// Role & Permission Types
// ============================================================================

enum class Role {
  RN = 0,
  CHARGE_NURSE = 1,
  PROVIDER = 2,
  ADMIN = 3,
  IT = 4
};

enum class Permission {
  // Vital sign access
  VIEW_VITALS = 1,
  
  // Charting
  CHART_ACTIONS = 2,
  ADD_NOTES = 3,
  
  // Recommendations
  ACCEPT_RECOMMENDATIONS = 4,
  
  // Orders
  VIEW_ORDERS = 5,
  ACKNOWLEDGE_ORDERS = 6,
  
  // Admissions
  ADMIT_PATIENT = 7,
  DISCHARGE_PATIENT = 8,
  ASSIGN_PATIENTS = 9,
  
  // Viewing
  VIEW_ALL_PATIENTS = 10,
  
  // Alerts
  OVERRIDE_ALERTS = 11,
  
  // Analytics
  VIEW_ANALYTICS = 12,
  
  // Admin
  ACCESS_ADMIN = 13,
  
  // Training
  INITIATE_TRAINING = 14,
  VIEW_TRAINING_RESULTS = 15
};

// ============================================================================
// Authentication Token
// ============================================================================

struct AuthToken {
  std::string token_id;              // Unique identifier
  std::string user_id;               // Staff member ID
  std::string staff_name;            // Full name
  Role role;                         // User's role
  std::time_t issued_at;             // When token was created
  std::time_t expires_at;            // When token expires
  std::set<int> assigned_patients;   // Patient IDs assigned to this user
  
  /**
   * Check if token is still valid
   */
  bool is_valid() const {
    return std::time(nullptr) < expires_at;
  }
  
  /**
   * Check if user is assigned to patient
   */
  bool is_assigned_to_patient(int patient_id) const {
    return assigned_patients.count(patient_id) > 0;
  }
};

// ============================================================================
// Staff Member Database
// ============================================================================

struct StaffMember {
  std::string staff_id;
  std::string name;
  Role role;
  std::vector<int> assigned_patients;
  bool active;
  std::time_t last_sign_in;
};

// ============================================================================
// Role-to-Permission Mapping
// ============================================================================

struct RolePermissions {
  Role role;
  std::set<Permission> permissions;
  std::string display_name;
  
  /**
   * Check if this role has a specific permission
   */
  bool has_permission(Permission perm) const {
    return permissions.count(perm) > 0;
  }
};

// ============================================================================
// Authentication Manager
// ============================================================================

class AuthManager {
public:
  AuthManager() = default;
  ~AuthManager() = default;
  
  /**
   * Initialize authentication system
   * 
   * Loads staff database, sets up role permissions, etc.
   */
  bool initialize();
  
  // =========================================================================
  // Authentication (Sign In)
  // =========================================================================
  
  /**
   * Authenticate a user and generate a token
   * 
   * Returns: AuthToken if successful, or empty token if failed
   * 
   * Typical flow:
   * 1. Staff enters their ID and password
   * 2. System verifies credentials (would check against directory/LDAP in production)
   * 3. Creates a session token with role and assigned patients
   * 4. Token included in subsequent HTTP requests
   */
  AuthToken authenticate(const std::string& staff_id, const std::string& password);
  
  /**
   * Verify a token is valid and not expired
   */
  bool verify_token(const std::string& token_id);
  
  /**
   * Get auth info from token ID
   */
  AuthToken* get_token(const std::string& token_id);
  
  /**
   * Invalidate a token (sign out)
   */
  void revoke_token(const std::string& token_id);
  
  /**
   * Invalidate all tokens for a user (e.g., after password change)
   */
  void revoke_user_sessions(const std::string& user_id);
  
  // =========================================================================
  // Authorization (Permission Checks)
  // =========================================================================
  
  /**
   * Check if user has a specific permission
   * 
   * This is the core function used by HTTP handlers:
   * 
   * Example in handle_action():
   *   AuthToken* token = auth_mgr->get_token(request.auth_token);
   *   if (!token || !auth_mgr->can_perform_action(token, Permission::CHART_ACTIONS)) {
   *     return Response(403, "{\"error\": \"Permission denied\"}");
   *   }
   */
  bool can_perform_action(AuthToken* token, Permission perm);
  
  /**
   * Check if user is assigned to specific patient
   * 
   * Clinical safety requirement: Users can only access their assigned patients
   * (except for provider/admin roles with broader access)
   */
  bool is_user_assigned_to_patient(AuthToken* token, int patient_id);
  
  /**
   * Check if user can view patient observations
   * 
   * Combines: is_user_assigned_to_patient + can_perform_action(VIEW_VITALS)
   */
  bool can_view_patient_vitals(AuthToken* token, int patient_id);
  
  /**
   * Check if user can perform clinical action on patient
   * 
   * Combines: is_user_assigned_to_patient + can_perform_action(CHART_ACTIONS)
   */
  bool can_chart_on_patient(AuthToken* token, int patient_id, const std::string& action_type);
  
  /**
   * Check if user can initiate training session
   */
  bool can_initiate_training(AuthToken* token);
  
  // =========================================================================
  // Staff Management
  // =========================================================================
  
  /**
   * Add/register a new staff member
   * 
   * Called during onboarding or admin operations
   */
  bool add_staff_member(const StaffMember& staff);
  
  /**
   * Update staff assignments
   * 
   * Example: Assign nurse to patients during shift change
   */
  bool assign_patient_to_staff(const std::string& staff_id, int patient_id);
  
  /**
   * Deassign patient from staff
   */
  bool deassign_patient_from_staff(const std::string& staff_id, int patient_id);
  
  /**
   * Get staff info
   */
  StaffMember* get_staff_member(const std::string& staff_id);
  
  // =========================================================================
  // Audit & Logging
  // =========================================================================
  
  /**
   * Record an authorization decision (for audit trail)
   * 
   * Every auth check should be logged:
   *   - Who requested action
   *   - What action they requested
   *   - Was it allowed/denied
   *   - When
   * 
   * This creates an immutable audit trail for compliance
   */
  void log_auth_decision(
    const std::string& user_id,
    const std::string& action,
    bool allowed,
    const std::string& reason = ""
  );
  
private:
  // Staff database
  std::map<std::string, StaffMember> staff_database_;
  
  // Active tokens (in production, use Redis/Memcached)
  std::map<std::string, AuthToken> active_tokens_;
  
  // Role-to-permission mappings
  std::map<Role, RolePermissions> role_permissions_;
  
  /**
   * Initialize role-to-permission mappings
   */
  void setup_role_permissions();
  
  /**
   * Generate a unique token ID
   */
  std::string generate_token_id();
};

// ============================================================================
// Global Authentication Manager Instance
// ============================================================================

// Typically accessed as: auth_manager->verify_token(token_id)
extern std::unique_ptr<AuthManager> g_auth_manager;

#endif
