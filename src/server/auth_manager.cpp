/**
 * Authentication & Authorization Manager Implementation
 * 
 * Phase 1.5: Server-side role enforcement
 */

#include "auth_manager.h"
#include "crypto_utils.h"
#include "json_lite.h"
#include <iostream>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <random>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <filesystem>

// Global auth manager instance
std::unique_ptr<AuthManager> g_auth_manager = nullptr;

namespace {

namespace fs = std::filesystem;

// Seeds one staff member's password from an environment variable if set,
// otherwise generates a random one-time password and prints it once so the
// operator can sign in. Never hardcode or document real credentials.
void seed_staff_password(StaffMember& staff, const char* env_var) {
  const char* env_val = std::getenv(env_var);
  std::string password;
  if (env_val && std::string(env_val).size() >= 8) {
    password = env_val;
  } else {
    password = Crypto::random_hex(9); // ~18 char random password
    std::cout << "[AuthManager] Generated password for " << staff.staff_id
              << " (set " << env_var << " to override): " << password << std::endl;
  }
  staff.password_hash = Crypto::hash_password(password);
}

// Role <-> string, scoped to persistence here -- deliberately independent
// of http_server.cpp's role_to_acmk_role_string (that one's for API
// responses; this one's a storage format and changing it would break
// reading previously-saved rosters, so they're not the same concern even
// though the string values happen to match).
std::string role_to_storage_string(Role role) {
  switch (role) {
    case Role::RN: return "rn";
    case Role::CHARGE_NURSE: return "charge_nurse";
    case Role::PROVIDER: return "provider";
    case Role::ADMIN: return "admin";
    case Role::IT: return "it";
    case Role::INSTRUCTOR: return "instructor";
  }
  return "rn";
}

Role storage_string_to_role(const std::string& s) {
  if (s == "charge_nurse") return Role::CHARGE_NURSE;
  if (s == "provider") return Role::PROVIDER;
  if (s == "admin") return Role::ADMIN;
  if (s == "it") return Role::IT;
  if (s == "instructor") return Role::INSTRUCTOR;
  return Role::RN;
}

} // namespace

bool AuthManager::initialize() {
  std::cout << "[AuthManager] Initializing..." << std::endl;
  setup_role_permissions();

  StaffMember admin{"ADMIN_001", "Admin User", Role::ADMIN, {}};
  seed_staff_password(admin, "ACMK_ADMIN_PASSWORD");
  staff_database_["ADMIN_001"] = admin;

  StaffMember rn{"RN_001", "Nurse Jane", Role::RN, {1, 2, 3}};
  seed_staff_password(rn, "ACMK_RN001_PASSWORD");
  staff_database_["RN_001"] = rn;

  StaffMember charge{"CHARGE_001", "Maria Ortiz", Role::CHARGE_NURSE, {1, 2, 3, 4, 5, 6}};
  seed_staff_password(charge, "ACMK_CHARGE001_PASSWORD");
  staff_database_["CHARGE_001"] = charge;

  StaffMember provider{"PROVIDER_001", "Dr. Alan Reyes", Role::PROVIDER, {1, 2, 3, 4, 5, 6}};
  seed_staff_password(provider, "ACMK_PROVIDER001_PASSWORD");
  staff_database_["PROVIDER_001"] = provider;

  StaffMember it{"IT_001", "System Admin", Role::IT, {}};
  seed_staff_password(it, "ACMK_IT001_PASSWORD");
  staff_database_["IT_001"] = it;

  StaffMember instructor{"INSTRUCTOR_001", "Dr. Patricia Nguyen", Role::INSTRUCTOR, {}};
  seed_staff_password(instructor, "ACMK_INSTRUCTOR001_PASSWORD");
  staff_database_["INSTRUCTOR_001"] = instructor;

  // Bulk-provisioned accounts (e.g. imported class rosters) have no env-var
  // seed to regenerate from, so they're loaded from disk here -- after the
  // 6 demo accounts above, so a persisted entry can never shadow a fresh
  // demo password (load_persisted_staff skips any staff_id already present).
  load_persisted_staff();

  std::cout << "[AuthManager] Initialized successfully" << std::endl;
  return true;
}

void AuthManager::load_persisted_staff() {
  std::ifstream in(staff_store_path_);
  if (!in.is_open()) return; // No persisted roster yet -- normal on first boot.

  std::stringstream buffer;
  buffer << in.rdbuf();
  try {
    json parsed = json::parse(buffer.str());
    if (!parsed.is_array()) return;
    for (const auto& entry_ptr : parsed.items()) {
      const json& entry = *entry_ptr;
      std::string staff_id = entry.contains("staff_id") ? entry.at("staff_id").as_string("") : "";
      if (staff_id.empty() || staff_database_.count(staff_id) > 0) continue;

      StaffMember member;
      member.staff_id = staff_id;
      member.name = entry.contains("name") ? entry.at("name").as_string("") : staff_id;
      member.role = storage_string_to_role(entry.contains("role") ? entry.at("role").as_string("rn") : "rn");
      member.active = true;
      member.last_sign_in = 0;
      member.password_hash = entry.contains("password_hash") ? entry.at("password_hash").as_string("") : "";
      if (entry.contains("assigned_patients")) {
        const json& patients = entry.at("assigned_patients");
        for (const auto& pid_ptr : patients.items()) {
          member.assigned_patients.push_back(static_cast<int>(pid_ptr->as_double(0)));
        }
      }
      staff_database_[staff_id] = member;
    }
    std::cout << "[AuthManager] Loaded " << parsed.size() << " persisted staff record(s) from "
              << staff_store_path_ << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "[AuthManager] Failed to parse " << staff_store_path_ << ": " << e.what() << std::endl;
  }
}

void AuthManager::save_persisted_staff() const {
  fs::path path(staff_store_path_);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);

  json out = json::array();
  for (const auto& [id, member] : staff_database_) {
    json entry = json::object();
    entry["staff_id"] = json(member.staff_id);
    entry["name"] = json(member.name);
    entry["role"] = json(role_to_storage_string(member.role));
    entry["password_hash"] = json(member.password_hash);
    json patients = json::array();
    for (int pid : member.assigned_patients) patients.push_back(json(static_cast<double>(pid)));
    entry["assigned_patients"] = patients;
    out.push_back(entry);
  }

  std::ofstream file(staff_store_path_);
  if (!file.is_open()) {
    std::cerr << "[AuthManager] Failed to write " << staff_store_path_ << std::endl;
    return;
  }
  file << out.dump();
}

bool AuthManager::is_locked_out(const std::string& staff_id) {
  auto it = login_attempts_.find(staff_id);
  if (it == login_attempts_.end()) return false;
  return std::time(nullptr) < it->second.locked_until;
}

void AuthManager::record_failed_attempt(const std::string& staff_id) {
  auto& state = login_attempts_[staff_id];
  state.failed_count++;
  if (state.failed_count >= kMaxFailedAttempts) {
    state.locked_until = std::time(nullptr) + kLockoutSeconds;
    state.failed_count = 0;
    log_auth_decision(staff_id, "LOCKOUT", false, "Too many failed sign-in attempts");
  }
}

void AuthManager::clear_failed_attempts(const std::string& staff_id) {
  login_attempts_.erase(staff_id);
}

AuthToken AuthManager::authenticate(const std::string& staff_id, const std::string& password) {
  AuthToken empty_token;
  empty_token.token_id = "";

  if (is_locked_out(staff_id)) {
    log_auth_decision(staff_id, "SIGN_IN", false, "Account temporarily locked");
    return empty_token;
  }

  auto staff = staff_database_.find(staff_id);
  if (staff == staff_database_.end()) {
    // Still run a hash comparison against a dummy value so failed lookups
    // and failed password checks take comparable time (avoid user enumeration
    // via timing).
    Crypto::verify_password(password, "0000000000000000$0");
    std::cerr << "[Auth] Staff not found: " << staff_id << std::endl;
    record_failed_attempt(staff_id);
    return empty_token;
  }

  if (!Crypto::verify_password(password, staff->second.password_hash)) {
    std::cerr << "[Auth] Invalid password for: " << staff_id << std::endl;
    record_failed_attempt(staff_id);
    return empty_token;
  }

  clear_failed_attempts(staff_id);

  AuthToken token;
  token.token_id = generate_token_id();
  token.user_id = staff_id;
  token.staff_name = staff->second.name;
  token.role = staff->second.role;
  token.issued_at = std::time(nullptr);
  token.expires_at = token.issued_at + 3600; // 1 hour expiry
  token.assigned_patients = { staff->second.assigned_patients.begin(),
                              staff->second.assigned_patients.end() };

  active_tokens_[token.token_id] = token;
  log_auth_decision(staff_id, "SIGN_IN", true, "Successful authentication");

  return token;
}

bool AuthManager::verify_token(const std::string& token_id) {
  auto it = active_tokens_.find(token_id);
  if (it == active_tokens_.end()) {
    return false;
  }
  return it->second.is_valid();
}

AuthToken* AuthManager::get_token(const std::string& token_id) {
  auto it = active_tokens_.find(token_id);
  if (it == active_tokens_.end()) {
    return nullptr;
  }
  if (!it->second.is_valid()) {
    active_tokens_.erase(it);
    return nullptr;
  }
  return &it->second;
}

void AuthManager::revoke_token(const std::string& token_id) {
  active_tokens_.erase(token_id);
}

void AuthManager::revoke_user_sessions(const std::string& user_id) {
  for (auto it = active_tokens_.begin(); it != active_tokens_.end(); ) {
    if (it->second.user_id == user_id) {
      it = active_tokens_.erase(it);
    } else {
      ++it;
    }
  }
}

bool AuthManager::can_perform_action(AuthToken* token, Permission perm) {
  if (!token || !token->is_valid()) return false;
  
  auto it = role_permissions_.find(token->role);
  if (it == role_permissions_.end()) return false;
  
  return it->second.has_permission(perm);
}

bool AuthManager::is_user_assigned_to_patient(AuthToken* token, int patient_id) {
  if (!token || !token->is_valid()) return false;

  // Roles whose frontend capability set (main-refactored.ts:
  // roleCapabilities) claims canViewAllPatients bypass individual
  // assignment -- this was previously PROVIDER/ADMIN only, so a
  // CHARGE_NURSE not personally assigned to a patient would be silently
  // blocked from charting on them despite the UI telling them they had
  // unit-wide access.
  if (token->role == Role::PROVIDER || token->role == Role::ADMIN || token->role == Role::CHARGE_NURSE) {
    return true;
  }
  return token->is_assigned_to_patient(patient_id);
}

bool AuthManager::can_view_patient_vitals(AuthToken* token, int patient_id) {
  return is_user_assigned_to_patient(token, patient_id) &&
         can_perform_action(token, Permission::VIEW_VITALS);
}

bool AuthManager::can_chart_on_patient(AuthToken* token, int patient_id, const std::string& action_type) {
  return is_user_assigned_to_patient(token, patient_id) &&
         can_perform_action(token, Permission::CHART_ACTIONS);
}

bool AuthManager::can_initiate_training(AuthToken* token) {
  return can_perform_action(token, Permission::INITIATE_TRAINING);
}

bool AuthManager::add_staff_member(const StaffMember& staff) {
  if (staff_database_.count(staff.staff_id) > 0) return false;
  staff_database_[staff.staff_id] = staff;
  save_persisted_staff();
  return true;
}

bool AuthManager::assign_patient_to_staff(const std::string& staff_id, int patient_id) {
  auto it = staff_database_.find(staff_id);
  if (it == staff_database_.end()) return false;

  auto& assignments = it->second.assigned_patients;
  if (std::find(assignments.begin(), assignments.end(), patient_id) == assignments.end()) {
    assignments.push_back(patient_id);
  }
  // AuthToken.assigned_patients is a snapshot copied at sign-in (see
  // authenticate()) -- without this, a charge nurse assigning a patient to
  // an RN mid-shift would have no effect until that RN signed out and back
  // in, since every permission check reads the token's snapshot, not the
  // staff database this just updated.
  for (auto& [tok_id, token] : active_tokens_) {
    if (token.user_id == staff_id) {
      token.assigned_patients.insert(patient_id);
    }
  }
  return true;
}

bool AuthManager::deassign_patient_from_staff(const std::string& staff_id, int patient_id) {
  auto it = staff_database_.find(staff_id);
  if (it == staff_database_.end()) return false;

  auto& assignments = it->second.assigned_patients;
  auto pos = std::find(assignments.begin(), assignments.end(), patient_id);
  if (pos != assignments.end()) {
    assignments.erase(pos);
  }
  for (auto& [tok_id, token] : active_tokens_) {
    if (token.user_id == staff_id) {
      token.assigned_patients.erase(patient_id);
    }
  }
  return true;
}

StaffMember* AuthManager::get_staff_member(const std::string& staff_id) {
  auto it = staff_database_.find(staff_id);
  return (it == staff_database_.end()) ? nullptr : &it->second;
}

std::vector<StaffMember> AuthManager::list_staff() {
  std::vector<StaffMember> result;
  result.reserve(staff_database_.size());
  for (const auto& [id, staff] : staff_database_) {
    result.push_back(staff);
  }
  return result;
}

void AuthManager::log_auth_decision(const std::string& user_id, const std::string& action, bool allowed, const std::string& reason) {
  std::cout << "[Auth] " << (allowed ? "ALLOW" : "DENY") << " "
            << user_id << " -> " << action
            << (reason.empty() ? "" : " (" + reason + ")") << std::endl;
}

void AuthManager::setup_role_permissions() {
  // Setup ADMIN
  RolePermissions admin;
  admin.role = Role::ADMIN;
  admin.permissions = {Permission::VIEW_VITALS, Permission::CHART_ACTIONS, Permission::ADD_NOTES, Permission::ACCESS_ADMIN, Permission::INITIATE_TRAINING};
  role_permissions_[Role::ADMIN] = admin;

  // Setup RN. INITIATE_TRAINING belongs here: the simulation/training
  // feature exists for "new nurse onboarding" and "annual skills refresher"
  // per docs/BOARD_OF_NURSING_COMPLIANCE.md -- bedside RNs are the primary
  // intended user, not an afterthought. It was previously omitted, so the
  // "Enter Training Mode" button (shown to every clinical role) 403'd for
  // the one role it's mainly built for.
  RolePermissions rn;
  rn.role = Role::RN;
  rn.permissions = {Permission::VIEW_VITALS, Permission::CHART_ACTIONS, Permission::ADD_NOTES,
                     Permission::ACCEPT_RECOMMENDATIONS, Permission::VIEW_ORDERS, Permission::ACKNOWLEDGE_ORDERS,
                     Permission::INITIATE_TRAINING, Permission::VIEW_TRAINING_RESULTS};
  role_permissions_[Role::RN] = rn;

  // Setup CHARGE_NURSE
  RolePermissions charge;
  charge.role = Role::CHARGE_NURSE;
  charge.permissions = {Permission::VIEW_VITALS, Permission::CHART_ACTIONS, Permission::ADD_NOTES,
                         Permission::ACCEPT_RECOMMENDATIONS, Permission::VIEW_ORDERS, Permission::ACKNOWLEDGE_ORDERS,
                         Permission::ADMIT_PATIENT, Permission::DISCHARGE_PATIENT, Permission::ASSIGN_PATIENTS,
                         Permission::VIEW_ALL_PATIENTS, Permission::OVERRIDE_ALERTS, Permission::VIEW_ANALYTICS,
                         Permission::INITIATE_TRAINING, Permission::VIEW_TRAINING_RESULTS};
  role_permissions_[Role::CHARGE_NURSE] = charge;

  // Setup PROVIDER. Also gets training access -- providers participate in
  // interdisciplinary simulation drills too, and the dashboard shows the
  // same "Enter Training Mode" button to every clinical role, so leaving
  // this role without the permission would reproduce the same silent-403
  // bug RN just had.
  RolePermissions provider;
  provider.role = Role::PROVIDER;
  provider.permissions = {Permission::VIEW_VITALS, Permission::ADD_NOTES, Permission::VIEW_ORDERS,
                           Permission::ADMIT_PATIENT, Permission::DISCHARGE_PATIENT,
                           Permission::VIEW_ALL_PATIENTS, Permission::OVERRIDE_ALERTS,
                           Permission::INITIATE_TRAINING, Permission::VIEW_TRAINING_RESULTS};
  role_permissions_[Role::PROVIDER] = provider;

  // Setup IT -- no clinical data access, oversight/analytics only.
  RolePermissions it;
  it.role = Role::IT;
  it.permissions = {Permission::VIEW_ANALYTICS, Permission::ACCESS_ADMIN};
  role_permissions_[Role::IT] = it;

  // Setup INSTRUCTOR -- manages class cohorts and views training analytics
  // for their students, same as IT: no clinical data access at all. Cohort
  // management itself is gated by a direct Role::INSTRUCTOR check in the
  // handlers (see handle_instructor_* in http_server.cpp) rather than a
  // dedicated permission, since it's a capability no other role shares.
  RolePermissions instructor;
  instructor.role = Role::INSTRUCTOR;
  instructor.permissions = {Permission::VIEW_ANALYTICS, Permission::VIEW_TRAINING_RESULTS};
  role_permissions_[Role::INSTRUCTOR] = instructor;
}

std::string AuthManager::generate_token_id() {
  // 256 bits of CSPRNG output, not derivable from timestamp/counter.
  return "tok_" + Crypto::random_hex(32);
}
