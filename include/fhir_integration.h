#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>
#include "json_lite.h"

namespace FHIR {

enum class FHIRResourceType {
  Patient,
  Observation,
  Condition,
  AllergyIntolerance,
  CarePlan,
  Flag,
  DocumentReference,
  Encounter,
  Provenance,
  AuditEvent
};

enum class FHIRScope {
  PATIENT_READ,
  PATIENT_WRITE,
  OBSERVATION_READ,
  OBSERVATION_WRITE,
  FLAG_READ,
  FLAG_WRITE,
  DOCUMENT_REFERENCE_READ,
  DOCUMENT_REFERENCE_WRITE,
  CONDITION_READ,
  CONDITION_WRITE,
  MEDICATION_REQUEST_READ,
  SERVICE_REQUEST_READ,
  PROCEDURE_READ,
  ALLERGY_INTOLERANCE_READ,
  CARE_PLAN_READ,
  ENCOUNTER_READ,
  PROVENANCE_READ,
  AUDIT_EVENT_READ
};

struct SMARTLaunchContext {
  std::string client_id;
  std::string client_secret;
  std::string redirect_uri;
  std::string ehr_server_url;
  std::vector<FHIRScope> requested_scopes;
  std::string launch_context_patient;
  std::string launch_context_encounter;
  std::string launch_context_practitioner;
  std::string launch_context_role;
  std::string launch_context_location;
};

struct OAuth2Token {
  std::string access_token;
  std::string token_type;
  long expires_in;
  std::string refresh_token;
  std::vector<FHIRScope> granted_scopes;
  std::chrono::system_clock::time_point issued_at;
};

struct FHIRCapabilityStatement {
  std::string canonical_url = "https://acmk-ot.healthcare/fhir/CapabilityStatement/acmk-ot";
  std::string fhir_version = "4.0.1";
  std::string status = "active";
  std::string kind = "application";
  std::string server_role = "client";
  std::string clinical_safety_classification = "Non-decisional Clinical Decision Support";
  
  std::vector<std::string> supported_security = {"OAuth 2.0", "OpenID Connect"};
  std::vector<std::string> supported_launch_contexts = {
    "launch/patient",
    "launch/encounter",
    "launch/practitioner",
    "launch/role",
    "launch/location"
  };
  
  std::vector<FHIRResourceType> read_supported = {
    FHIRResourceType::Patient,
    FHIRResourceType::Observation,
    FHIRResourceType::Condition,
    FHIRResourceType::AllergyIntolerance,
    FHIRResourceType::CarePlan,
    FHIRResourceType::Flag,
    FHIRResourceType::DocumentReference,
    FHIRResourceType::Encounter,
    FHIRResourceType::Provenance,
    FHIRResourceType::AuditEvent
  };
  
  std::vector<FHIRResourceType> write_supported = {
    FHIRResourceType::DocumentReference,
    FHIRResourceType::Flag,
    FHIRResourceType::Observation
  };
};

struct FHIRObservation {
  std::string id;
  std::string patient_id;
  std::string code;
  std::string value;
  std::chrono::system_clock::time_point effective_datetime;
  double value_quantity;
  std::string value_unit;
  std::string status = "final";
  std::string category;
};

struct FHIRFlag {
  std::string id;
  std::string patient_id;
  std::string status;
  std::string category;
  std::string code;
  std::string period_start;
  std::string period_end;
};

struct FHIRDocumentReference {
  std::string id;
  std::string patient_id;
  std::string type;
  std::string status;
  std::chrono::system_clock::time_point date;
  std::string author_practitioner_id;
  std::string content_attachment_url;
  std::string content_attachment_title;
  std::string docstatus;
};

struct FHIRProvenance {
  std::string id;
  std::string agent_who;
  std::string agent_role;
  std::chrono::system_clock::time_point recorded;
  std::string entity_what;
  std::string version_info;
};

class SMARTOnFHIRProvider {
public:
  virtual ~SMARTOnFHIRProvider() = default;
  
  virtual OAuth2Token authorize(const SMARTLaunchContext& context) = 0;
  virtual bool validate_token(const OAuth2Token& token) = 0;
  virtual bool has_scope(const OAuth2Token& token, FHIRScope scope) = 0;
  virtual json refresh_token(const OAuth2Token& token) = 0;
};

class FHIRResourceClient {
public:
  virtual ~FHIRResourceClient() = default;
  
  virtual json read_observation(const std::string& observation_id, 
                               const OAuth2Token& token) = 0;
  virtual json read_patient(const std::string& patient_id, 
                           const OAuth2Token& token) = 0;
  virtual json read_condition(const std::string& condition_id, 
                             const OAuth2Token& token) = 0;
  virtual json read_allergy_intolerance(const std::string& allergy_id, 
                                       const OAuth2Token& token) = 0;
  virtual json read_care_plan(const std::string& care_plan_id, 
                             const OAuth2Token& token) = 0;
  virtual json read_flag(const std::string& flag_id, 
                        const OAuth2Token& token) = 0;
  virtual json read_document_reference(const std::string& doc_ref_id, 
                                      const OAuth2Token& token) = 0;
  virtual json read_encounter(const std::string& encounter_id, 
                             const OAuth2Token& token) = 0;
  virtual json read_provenance(const std::string& provenance_id, 
                              const OAuth2Token& token) = 0;
  virtual json read_audit_event(const std::string& audit_event_id, 
                               const OAuth2Token& token) = 0;
  
  virtual json create_document_reference(const FHIRDocumentReference& doc_ref, 
                                        const OAuth2Token& token) = 0;
  virtual json create_flag(const FHIRFlag& flag, 
                          const OAuth2Token& token) = 0;
  virtual json create_observation(const FHIRObservation& observation, 
                                 const OAuth2Token& token) = 0;
  virtual json create_provenance(const FHIRProvenance& provenance, 
                                const OAuth2Token& token) = 0;
};

class FHIRCapabilityProvider {
public:
  virtual ~FHIRCapabilityProvider() = default;
  
  FHIRCapabilityStatement get_capability_statement() const {
    return capability;
  }
  
protected:
  FHIRCapabilityStatement capability;
};

}
