#include "rbac_fhir.h"
#include <stdexcept>

namespace ACMK {

RoleBasedAccessControl::RoleBasedAccessControl() {
  initialize_permissions();
}

void RoleBasedAccessControl::initialize_permissions() {
  using FS = FHIR::FHIRScope;
  
  RolePermissions bedside_nurse;
  bedside_nurse.role = ClinicalRole::BEDSIDE_NURSE;
  bedside_nurse.display_name = "Bedside Nurse (RN/LPN)";
  bedside_nurse.read_scopes = {
    FS::PATIENT_READ, FS::OBSERVATION_READ, FS::CONDITION_READ,
    FS::ALLERGY_INTOLERANCE_READ, FS::CARE_PLAN_READ, FS::FLAG_READ,
    FS::DOCUMENT_REFERENCE_READ, FS::ENCOUNTER_READ
  };
  bedside_nurse.write_scopes = {
    FS::DOCUMENT_REFERENCE_WRITE, FS::FLAG_WRITE, FS::OBSERVATION_WRITE
  };
  bedside_nurse.can_acknowledge = true;
  bedside_nurse.can_annotate = true;
  bedside_nurse.can_flag_anomaly = true;
  bedside_nurse.can_escalate = true;
  bedside_nurse.can_access_simulation = true;
  role_permissions[ClinicalRole::BEDSIDE_NURSE] = bedside_nurse;
  
  RolePermissions charge_nurse;
  charge_nurse.role = ClinicalRole::CHARGE_NURSE;
  charge_nurse.display_name = "Charge Nurse";
  charge_nurse.read_scopes = bedside_nurse.read_scopes;
  charge_nurse.write_scopes = {
    FS::DOCUMENT_REFERENCE_WRITE, FS::FLAG_WRITE
  };
  charge_nurse.can_acknowledge = true;
  charge_nurse.can_annotate = true;
  charge_nurse.can_escalate = true;
  charge_nurse.can_access_simulation = true;
  role_permissions[ClinicalRole::CHARGE_NURSE] = charge_nurse;
  
  RolePermissions provider;
  provider.role = ClinicalRole::ATTENDING_PROVIDER;
  provider.display_name = "Attending Provider (MD/DO/NP/PA)";
  provider.read_scopes = {
    FS::PATIENT_READ, FS::OBSERVATION_READ, FS::CONDITION_READ,
    FS::ALLERGY_INTOLERANCE_READ, FS::CARE_PLAN_READ, FS::FLAG_READ,
    FS::DOCUMENT_REFERENCE_READ, FS::ENCOUNTER_READ, FS::MEDICATION_REQUEST_READ,
    FS::SERVICE_REQUEST_READ, FS::PROCEDURE_READ
  };
  provider.write_scopes = { FS::DOCUMENT_REFERENCE_WRITE };
  provider.can_acknowledge = true;
  provider.can_annotate = true;
  provider.can_escalate = true;
  provider.can_access_simulation = true;
  role_permissions[ClinicalRole::ATTENDING_PROVIDER] = provider;
  
  RolePermissions resident;
  resident.role = ClinicalRole::RESIDENT_FELLOW;
  resident.display_name = "Resident/Fellow";
  resident.read_scopes = provider.read_scopes;
  resident.write_scopes = {
    FS::DOCUMENT_REFERENCE_WRITE, FS::FLAG_WRITE
  };
  resident.can_acknowledge = true;
  resident.can_annotate = true;
  resident.can_access_simulation = true;
  role_permissions[ClinicalRole::RESIDENT_FELLOW] = resident;
  
  RolePermissions rapid_response;
  rapid_response.role = ClinicalRole::RAPID_RESPONSE;
  rapid_response.display_name = "Rapid Response / Code Team";
  rapid_response.read_scopes = {
    FS::OBSERVATION_READ, FS::ENCOUNTER_READ, FS::CONDITION_READ, FS::FLAG_READ
  };
  rapid_response.write_scopes = {
    FS::DOCUMENT_REFERENCE_WRITE, FS::FLAG_WRITE
  };
  rapid_response.can_acknowledge = true;
  rapid_response.can_annotate = true;
  role_permissions[ClinicalRole::RAPID_RESPONSE] = rapid_response;
  
  RolePermissions clinical_informatics;
  clinical_informatics.role = ClinicalRole::CLINICAL_INFORMATICS;
  clinical_informatics.display_name = "Clinical Informatics (CMIO/CNIO)";
  clinical_informatics.read_scopes = {
    FS::OBSERVATION_READ, FS::ENCOUNTER_READ, FS::FLAG_READ
  };
  clinical_informatics.can_view_audit = true;
  clinical_informatics.can_access_simulation = true;
  role_permissions[ClinicalRole::CLINICAL_INFORMATICS] = clinical_informatics;
  
  RolePermissions quality_safety;
  quality_safety.role = ClinicalRole::QUALITY_SAFETY;
  quality_safety.display_name = "Quality & Patient Safety";
  quality_safety.read_scopes = {
    FS::PATIENT_READ, FS::OBSERVATION_READ, FS::CONDITION_READ,
    FS::ENCOUNTER_READ, FS::DOCUMENT_REFERENCE_READ, FS::PROVENANCE_READ,
    FS::AUDIT_EVENT_READ
  };
  quality_safety.write_scopes = { FS::DOCUMENT_REFERENCE_WRITE };
  quality_safety.can_view_audit = true;
  role_permissions[ClinicalRole::QUALITY_SAFETY] = quality_safety;
  
  RolePermissions legal_compliance;
  legal_compliance.role = ClinicalRole::LEGAL_COMPLIANCE;
  legal_compliance.display_name = "Legal / Compliance";
  legal_compliance.read_scopes = {
    FS::DOCUMENT_REFERENCE_READ, FS::AUDIT_EVENT_READ, FS::PROVENANCE_READ
  };
  legal_compliance.can_view_audit = true;
  legal_compliance.can_export_audit = true;
  role_permissions[ClinicalRole::LEGAL_COMPLIANCE] = legal_compliance;
  
  RolePermissions education;
  education.role = ClinicalRole::EDUCATION_SIMULATION;
  education.display_name = "Education / Simulation";
  education.read_scopes = {
    FS::PATIENT_READ, FS::OBSERVATION_READ, FS::ENCOUNTER_READ
  };
  education.write_scopes = { FS::DOCUMENT_REFERENCE_WRITE };
  education.can_access_simulation = true;
  role_permissions[ClinicalRole::EDUCATION_SIMULATION] = education;
  
  RolePermissions it_security;
  it_security.role = ClinicalRole::IT_SECURITY;
  it_security.display_name = "IT / Security / Infrastructure";
  it_security.read_scopes = {
    FS::AUDIT_EVENT_READ, FS::PROVENANCE_READ
  };
  it_security.can_view_audit = true;
  role_permissions[ClinicalRole::IT_SECURITY] = it_security;
  
  RolePermissions executive;
  executive.role = ClinicalRole::EXECUTIVE_LEADERSHIP;
  executive.display_name = "Executive Leadership";
  role_permissions[ClinicalRole::EXECUTIVE_LEADERSHIP] = executive;
  
  RolePermissions patient_family;
  patient_family.role = ClinicalRole::PATIENT_FAMILY;
  patient_family.display_name = "Patient / Family";
  patient_family.read_scopes = {
    FS::PATIENT_READ, FS::OBSERVATION_READ, FS::DOCUMENT_REFERENCE_READ
  };
  role_permissions[ClinicalRole::PATIENT_FAMILY] = patient_family;
}

RolePermissions RoleBasedAccessControl::get_role_permissions(ClinicalRole role) const {
  auto it = role_permissions.find(role);
  if (it != role_permissions.end()) {
    return it->second;
  }
  throw std::runtime_error("Unknown role");
}

bool RoleBasedAccessControl::check_permission(ClinicalRole role, 
                                             const std::string& permission) const {
  auto perms = get_role_permissions(role);
  
  if (permission == "acknowledge") return perms.can_acknowledge;
  if (permission == "annotate") return perms.can_annotate;
  if (permission == "flag_anomaly") return perms.can_flag_anomaly;
  if (permission == "request_recompute") return perms.can_request_recompute;
  if (permission == "freeze_output") return perms.can_freeze_output;
  if (permission == "escalate") return perms.can_escalate;
  if (permission == "simulation") return perms.can_access_simulation;
  if (permission == "view_audit") return perms.can_view_audit;
  if (permission == "export_audit") return perms.can_export_audit;
  
  return false;
}

bool RoleBasedAccessControl::check_fhir_scope(ClinicalRole role, 
                                             FHIR::FHIRScope scope,
                                             bool is_write) const {
  auto perms = get_role_permissions(role);
  
  if (is_write) {
    return perms.write_scopes.count(scope) > 0;
  } else {
    return perms.read_scopes.count(scope) > 0;
  }
}

std::set<FHIR::FHIRScope> RoleBasedAccessControl::get_granted_scopes(
    ClinicalRole role, bool is_write) const {
  auto perms = get_role_permissions(role);
  return is_write ? perms.write_scopes : perms.read_scopes;
}

ClinicalRole RoleBasedAccessControl::string_to_role(const std::string& role_str) {
  if (role_str == "rn" || role_str == "bedside_nurse") return ClinicalRole::BEDSIDE_NURSE;
  if (role_str == "charge_nurse") return ClinicalRole::CHARGE_NURSE;
  if (role_str == "provider" || role_str == "attending") return ClinicalRole::ATTENDING_PROVIDER;
  if (role_str == "resident") return ClinicalRole::RESIDENT_FELLOW;
  if (role_str == "rapid_response") return ClinicalRole::RAPID_RESPONSE;
  if (role_str == "informatics") return ClinicalRole::CLINICAL_INFORMATICS;
  if (role_str == "quality") return ClinicalRole::QUALITY_SAFETY;
  if (role_str == "legal") return ClinicalRole::LEGAL_COMPLIANCE;
  if (role_str == "education") return ClinicalRole::EDUCATION_SIMULATION;
  if (role_str == "it") return ClinicalRole::IT_SECURITY;
  if (role_str == "executive") return ClinicalRole::EXECUTIVE_LEADERSHIP;
  if (role_str == "patient") return ClinicalRole::PATIENT_FAMILY;
  throw std::runtime_error("Unknown role string: " + role_str);
}

std::string RoleBasedAccessControl::role_to_string(ClinicalRole role) {
  switch (role) {
    case ClinicalRole::BEDSIDE_NURSE: return "rn";
    case ClinicalRole::CHARGE_NURSE: return "charge_nurse";
    case ClinicalRole::ATTENDING_PROVIDER: return "provider";
    case ClinicalRole::RESIDENT_FELLOW: return "resident";
    case ClinicalRole::RAPID_RESPONSE: return "rapid_response";
    case ClinicalRole::CLINICAL_INFORMATICS: return "informatics";
    case ClinicalRole::QUALITY_SAFETY: return "quality";
    case ClinicalRole::LEGAL_COMPLIANCE: return "legal";
    case ClinicalRole::EDUCATION_SIMULATION: return "education";
    case ClinicalRole::IT_SECURITY: return "it";
    case ClinicalRole::EXECUTIVE_LEADERSHIP: return "executive";
    case ClinicalRole::PATIENT_FAMILY: return "patient";
    default: return "unknown";
  }
}

ACMKRoleBasedClient::ACMKRoleBasedClient(
    ClinicalRole user_role,
    std::unique_ptr<FHIR::FHIRResourceClient> fhir_client_,
    std::unique_ptr<RoleBasedAccessControl> rbac_)
  : user_role(user_role), fhir_client(std::move(fhir_client_)), 
    rbac(std::move(rbac_)) {
  current_permissions = rbac->get_role_permissions(user_role);
}

json ACMKRoleBasedClient::read_observation(const std::string& observation_id,
                                          const FHIR::OAuth2Token& token) {
  check_scope_or_throw(FHIR::FHIRScope::OBSERVATION_READ, false);
  return fhir_client->read_observation(observation_id, token);
}

json ACMKRoleBasedClient::read_patient(const std::string& patient_id,
                                      const FHIR::OAuth2Token& token) {
  check_scope_or_throw(FHIR::FHIRScope::PATIENT_READ, false);
  return fhir_client->read_patient(patient_id, token);
}

json ACMKRoleBasedClient::create_flag(const FHIR::FHIRFlag& flag,
                                     const FHIR::OAuth2Token& token) {
  check_scope_or_throw(FHIR::FHIRScope::FLAG_WRITE, true);
  return fhir_client->create_flag(flag, token);
}

json ACMKRoleBasedClient::create_document_reference(
    const FHIR::FHIRDocumentReference& doc_ref,
    const FHIR::OAuth2Token& token) {
  check_scope_or_throw(FHIR::FHIRScope::DOCUMENT_REFERENCE_WRITE, true);
  return fhir_client->create_document_reference(doc_ref, token);
}

json ACMKRoleBasedClient::create_provenance(const FHIR::FHIRProvenance& provenance,
                                           const FHIR::OAuth2Token& token) {
  return fhir_client->create_provenance(provenance, token);
}

bool ACMKRoleBasedClient::can_acknowledge() const {
  return current_permissions.can_acknowledge;
}

bool ACMKRoleBasedClient::can_annotate() const {
  return current_permissions.can_annotate;
}

bool ACMKRoleBasedClient::can_escalate() const {
  return current_permissions.can_escalate;
}

bool ACMKRoleBasedClient::can_access_simulation() const {
  return current_permissions.can_access_simulation;
}

void ACMKRoleBasedClient::check_scope_or_throw(FHIR::FHIRScope scope, bool is_write) {
  if (!rbac->check_fhir_scope(user_role, scope, is_write)) {
    throw std::runtime_error("Insufficient FHIR scope for this operation");
  }
}

}
