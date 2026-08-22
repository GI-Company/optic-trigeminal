#pragma once

#include "fhir_integration.h"

namespace FHIR {

// Talks to a real FHIR server over HTTPS using the system `curl` binary,
// matching the rest of this project's "no external library deps" approach
// (see Groq::GroqClient). Base URL and bearer token come from the
// environment (FHIR_BASE_URL) / from SMARTOnFHIRProvider, never hardcoded.
//
// NOTE on production Epic use: this implements the OAuth2 client_credentials
// grant (client_id + client_secret -> access_token), which is fine against
// most standards-based FHIR sandboxes. Epic's actual backend-services SMART
// launch requires a JWT client-assertion signed with RS384 (private key
// held by the app, public JWKS registered with Epic) instead of a shared
// secret -- that signing flow is NOT implemented here (it needs an RSA
// implementation this project doesn't have and hand-rolling one is not a
// safe thing to improvise). To go live against real Epic, replace
// DefaultSMARTOnFHIRProvider::authorize() with a JWT-assertion flow.
class DefaultFHIRResourceClient : public FHIRResourceClient {
public:
  explicit DefaultFHIRResourceClient(const std::string& base_url);

  json read_observation(const std::string& observation_id, const OAuth2Token& token) override;
  json read_patient(const std::string& patient_id, const OAuth2Token& token) override;
  json read_condition(const std::string& condition_id, const OAuth2Token& token) override;
  json read_allergy_intolerance(const std::string& allergy_id, const OAuth2Token& token) override;
  json read_care_plan(const std::string& care_plan_id, const OAuth2Token& token) override;
  json read_flag(const std::string& flag_id, const OAuth2Token& token) override;
  json read_document_reference(const std::string& doc_ref_id, const OAuth2Token& token) override;
  json read_encounter(const std::string& encounter_id, const OAuth2Token& token) override;
  json read_provenance(const std::string& provenance_id, const OAuth2Token& token) override;
  json read_audit_event(const std::string& audit_event_id, const OAuth2Token& token) override;

  json create_document_reference(const FHIRDocumentReference& doc_ref, const OAuth2Token& token) override;
  json create_flag(const FHIRFlag& flag, const OAuth2Token& token) override;
  json create_observation(const FHIRObservation& observation, const OAuth2Token& token) override;
  json create_provenance(const FHIRProvenance& provenance, const OAuth2Token& token) override;

private:
  std::string base_url_;

  json get_resource(const std::string& resource_type, const std::string& resource_id, const OAuth2Token& token);
  json post_resource(const std::string& resource_type, const json& body, const OAuth2Token& token);

  // Throws if id contains anything outside the FHIR id grammar
  // ([A-Za-z0-9\-\.]{1,64}) -- this is what stands between a resource id
  // from an HTTP request and a shell command line built via popen().
  static void validate_fhir_id(const std::string& id);
};

class DefaultSMARTOnFHIRProvider : public SMARTOnFHIRProvider {
public:
  explicit DefaultSMARTOnFHIRProvider(const std::string& token_endpoint);

  OAuth2Token authorize(const SMARTLaunchContext& context) override;
  bool validate_token(const OAuth2Token& token) override;
  bool has_scope(const OAuth2Token& token, FHIRScope scope) override;
  json refresh_token(const OAuth2Token& token) override;

private:
  std::string token_endpoint_;
};

std::string fhir_scope_to_string(FHIRScope scope);

} // namespace FHIR
