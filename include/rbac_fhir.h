#pragma once

#include "fhir_integration.h"
#include <map>
#include <set>
#include <memory>

namespace ACMK {

enum class ClinicalRole {
  BEDSIDE_NURSE,
  CHARGE_NURSE,
  ATTENDING_PROVIDER,
  RESIDENT_FELLOW,
  RAPID_RESPONSE,
  CLINICAL_INFORMATICS,
  QUALITY_SAFETY,
  LEGAL_COMPLIANCE,
  EDUCATION_SIMULATION,
  IT_SECURITY,
  EXECUTIVE_LEADERSHIP,
  PATIENT_FAMILY
};

struct RolePermissions {
  ClinicalRole role;
  std::string display_name;
  std::set<FHIR::FHIRScope> read_scopes;
  std::set<FHIR::FHIRScope> write_scopes;
  bool can_acknowledge = false;
  bool can_annotate = false;
  bool can_flag_anomaly = false;
  bool can_request_recompute = false;
  bool can_freeze_output = false;
  bool can_escalate = false;
  bool can_access_simulation = false;
  bool can_view_audit = false;
  bool can_export_audit = false;
};

class RoleBasedAccessControl {
public:
  RoleBasedAccessControl();
  
  RolePermissions get_role_permissions(ClinicalRole role) const;
  bool check_permission(ClinicalRole role, const std::string& permission) const;
  bool check_fhir_scope(ClinicalRole role, FHIR::FHIRScope scope, 
                       bool is_write = false) const;
  
  std::set<FHIR::FHIRScope> get_granted_scopes(ClinicalRole role, 
                                               bool is_write = false) const;
  
  static ClinicalRole string_to_role(const std::string& role_str);
  static std::string role_to_string(ClinicalRole role);
  
private:
  std::map<ClinicalRole, RolePermissions> role_permissions;
  void initialize_permissions();
};

class ACMKRoleBasedClient {
public:
  ACMKRoleBasedClient(ClinicalRole user_role, 
                     std::unique_ptr<FHIR::FHIRResourceClient> fhir_client,
                     std::unique_ptr<RoleBasedAccessControl> rbac);
  
  json read_observation(const std::string& observation_id, 
                       const FHIR::OAuth2Token& token);
  json read_patient(const std::string& patient_id, 
                   const FHIR::OAuth2Token& token);
  json create_flag(const FHIR::FHIRFlag& flag, 
                  const FHIR::OAuth2Token& token);
  json create_document_reference(const FHIR::FHIRDocumentReference& doc_ref, 
                                const FHIR::OAuth2Token& token);
  json create_provenance(const FHIR::FHIRProvenance& provenance, 
                        const FHIR::OAuth2Token& token);
  
  bool can_acknowledge() const;
  bool can_annotate() const;
  bool can_escalate() const;
  bool can_access_simulation() const;
  
private:
  ClinicalRole user_role;
  std::unique_ptr<FHIR::FHIRResourceClient> fhir_client;
  std::unique_ptr<RoleBasedAccessControl> rbac;
  RolePermissions current_permissions;
  
  void check_scope_or_throw(FHIR::FHIRScope scope, bool is_write);
};

}
