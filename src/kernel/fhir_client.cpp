#include "fhir_client.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <ctime>
#include <chrono>
#include <unistd.h>

namespace FHIR {

namespace {

std::string run_curl(const std::string& cmd) {
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
  if (!pipe) return "";
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
    result += buffer;
  }
  return result;
}

std::string write_temp_file(const std::string& content) {
  std::string tmp_file = "/tmp/fhir_payload_XXXXXX";
  int fd = mkstemp(&tmp_file[0]);
  if (fd == -1) return "";
  FILE* fp = fdopen(fd, "w");
  if (!fp) { close(fd); return ""; }
  fwrite(content.c_str(), 1, content.size(), fp);
  fclose(fp);
  return tmp_file;
}

std::string iso8601(const std::chrono::system_clock::time_point& tp) {
  std::time_t t = std::chrono::system_clock::to_time_t(tp);
  struct tm tm_buf;
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", gmtime_r(&t, &tm_buf));
  return std::string(buf);
}

} // namespace

void DefaultFHIRResourceClient::validate_fhir_id(const std::string& id) {
  if (id.empty() || id.size() > 64) {
    throw std::invalid_argument("Invalid FHIR resource id");
  }
  for (char c : id) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '.';
    if (!ok) throw std::invalid_argument("Invalid FHIR resource id: illegal character");
  }
}

DefaultFHIRResourceClient::DefaultFHIRResourceClient(const std::string& base_url)
    : base_url_(base_url) {}

json DefaultFHIRResourceClient::get_resource(const std::string& resource_type,
                                              const std::string& resource_id,
                                              const OAuth2Token& token) {
  validate_fhir_id(resource_id);
  if (base_url_.empty()) {
    json err = json::object();
    err["error"] = "FHIR_BASE_URL not configured";
    return err;
  }

  std::string url = base_url_ + "/" + resource_type + "/" + resource_id;
  std::string cmd = "curl -s -X GET \"" + url + "\"" +
                     " -H \"Authorization: Bearer " + token.access_token + "\"" +
                     " -H \"Accept: application/fhir+json\"";

  std::string response = run_curl(cmd);
  if (response.empty()) {
    json err = json::object();
    err["error"] = "Empty response from FHIR server";
    return err;
  }
  try {
    return json::parse(response);
  } catch (...) {
    json err = json::object();
    err["error"] = "Failed to parse FHIR response";
    err["raw"] = response;
    return err;
  }
}

json DefaultFHIRResourceClient::post_resource(const std::string& resource_type,
                                               const json& body,
                                               const OAuth2Token& token) {
  if (base_url_.empty()) {
    json err = json::object();
    err["error"] = "FHIR_BASE_URL not configured";
    return err;
  }

  std::string tmp_file = write_temp_file(body.dump());
  if (tmp_file.empty()) {
    json err = json::object();
    err["error"] = "Failed to stage request payload";
    return err;
  }

  std::string url = base_url_ + "/" + resource_type;
  std::string cmd = "curl -s -X POST \"" + url + "\"" +
                     " -H \"Authorization: Bearer " + token.access_token + "\"" +
                     " -H \"Content-Type: application/fhir+json\"" +
                     " -d @" + tmp_file;

  std::string response = run_curl(cmd);
  remove(tmp_file.c_str());

  if (response.empty()) {
    json err = json::object();
    err["error"] = "Empty response from FHIR server";
    return err;
  }
  try {
    return json::parse(response);
  } catch (...) {
    json err = json::object();
    err["error"] = "Failed to parse FHIR response";
    err["raw"] = response;
    return err;
  }
}

json DefaultFHIRResourceClient::read_observation(const std::string& id, const OAuth2Token& t) {
  return get_resource("Observation", id, t);
}
json DefaultFHIRResourceClient::read_patient(const std::string& id, const OAuth2Token& t) {
  return get_resource("Patient", id, t);
}
json DefaultFHIRResourceClient::read_condition(const std::string& id, const OAuth2Token& t) {
  return get_resource("Condition", id, t);
}
json DefaultFHIRResourceClient::read_allergy_intolerance(const std::string& id, const OAuth2Token& t) {
  return get_resource("AllergyIntolerance", id, t);
}
json DefaultFHIRResourceClient::read_care_plan(const std::string& id, const OAuth2Token& t) {
  return get_resource("CarePlan", id, t);
}
json DefaultFHIRResourceClient::read_flag(const std::string& id, const OAuth2Token& t) {
  return get_resource("Flag", id, t);
}
json DefaultFHIRResourceClient::read_document_reference(const std::string& id, const OAuth2Token& t) {
  return get_resource("DocumentReference", id, t);
}
json DefaultFHIRResourceClient::read_encounter(const std::string& id, const OAuth2Token& t) {
  return get_resource("Encounter", id, t);
}
json DefaultFHIRResourceClient::read_provenance(const std::string& id, const OAuth2Token& t) {
  return get_resource("Provenance", id, t);
}
json DefaultFHIRResourceClient::read_audit_event(const std::string& id, const OAuth2Token& t) {
  return get_resource("AuditEvent", id, t);
}

json DefaultFHIRResourceClient::create_document_reference(const FHIRDocumentReference& doc_ref, const OAuth2Token& token) {
  json body = json::object();
  body["resourceType"] = "DocumentReference";
  body["status"] = doc_ref.status;
  body["docStatus"] = doc_ref.docstatus;
  body["type"] = doc_ref.type;
  body["subject"] = json::object();
  body["subject"]["reference"] = "Patient/" + doc_ref.patient_id;
  body["date"] = iso8601(doc_ref.date);
  json author = json::object();
  author["reference"] = "Practitioner/" + doc_ref.author_practitioner_id;
  body["author"] = json::array();
  body["author"].push_back(author);
  json content_item = json::object();
  json attachment = json::object();
  attachment["url"] = doc_ref.content_attachment_url;
  attachment["title"] = doc_ref.content_attachment_title;
  content_item["attachment"] = attachment;
  body["content"] = json::array();
  body["content"].push_back(content_item);
  return post_resource("DocumentReference", body, token);
}

json DefaultFHIRResourceClient::create_flag(const FHIRFlag& flag, const OAuth2Token& token) {
  json body = json::object();
  body["resourceType"] = "Flag";
  body["status"] = flag.status;
  body["category"] = flag.category;
  body["code"] = flag.code;
  body["subject"] = json::object();
  body["subject"]["reference"] = "Patient/" + flag.patient_id;
  json period = json::object();
  period["start"] = flag.period_start;
  period["end"] = flag.period_end;
  body["period"] = period;
  return post_resource("Flag", body, token);
}

json DefaultFHIRResourceClient::create_observation(const FHIRObservation& observation, const OAuth2Token& token) {
  json body = json::object();
  body["resourceType"] = "Observation";
  body["status"] = observation.status;
  body["category"] = observation.category;
  body["code"] = observation.code;
  body["subject"] = json::object();
  body["subject"]["reference"] = "Patient/" + observation.patient_id;
  body["effectiveDateTime"] = iso8601(observation.effective_datetime);
  json value_quantity = json::object();
  value_quantity["value"] = observation.value_quantity;
  value_quantity["unit"] = observation.value_unit;
  body["valueQuantity"] = value_quantity;
  return post_resource("Observation", body, token);
}

json DefaultFHIRResourceClient::create_provenance(const FHIRProvenance& provenance, const OAuth2Token& token) {
  json body = json::object();
  body["resourceType"] = "Provenance";
  body["recorded"] = iso8601(provenance.recorded);
  json agent = json::object();
  json who = json::object();
  who["display"] = provenance.agent_who;
  agent["who"] = who;
  agent["role"] = provenance.agent_role;
  body["agent"] = json::array();
  body["agent"].push_back(agent);
  body["entity"] = json::object();
  body["entity"]["what"] = provenance.entity_what;
  return post_resource("Provenance", body, token);
}

DefaultSMARTOnFHIRProvider::DefaultSMARTOnFHIRProvider(const std::string& token_endpoint)
    : token_endpoint_(token_endpoint) {}

OAuth2Token DefaultSMARTOnFHIRProvider::authorize(const SMARTLaunchContext& context) {
  OAuth2Token token;
  if (token_endpoint_.empty() || context.client_id.empty() || context.client_secret.empty()) {
    return token; // empty access_token signals failure to the caller
  }

  std::string cmd = "curl -s -X POST \"" + token_endpoint_ + "\"" +
                     " -H \"Content-Type: application/x-www-form-urlencoded\"" +
                     " -u \"" + context.client_id + ":" + context.client_secret + "\"" +
                     " -d \"grant_type=client_credentials\"";

  std::string response = run_curl(cmd);
  if (response.empty()) return token;

  try {
    json j = json::parse(response);
    token.access_token = j.at("access_token").as_string();
    token.token_type = j.contains("token_type") ? j.at("token_type").as_string() : "Bearer";
    token.expires_in = j.contains("expires_in") ? j.at("expires_in").as_long(3600) : 3600;
    token.issued_at = std::chrono::system_clock::now();
    token.granted_scopes = context.requested_scopes;
  } catch (...) {
    // leave token empty; caller treats an empty access_token as a failed authorize()
  }
  return token;
}

bool DefaultSMARTOnFHIRProvider::validate_token(const OAuth2Token& token) {
  if (token.access_token.empty()) return false;
  auto expires_at = token.issued_at + std::chrono::seconds(token.expires_in);
  return std::chrono::system_clock::now() < expires_at;
}

bool DefaultSMARTOnFHIRProvider::has_scope(const OAuth2Token& token, FHIRScope scope) {
  for (auto s : token.granted_scopes) {
    if (s == scope) return true;
  }
  return false;
}

json DefaultSMARTOnFHIRProvider::refresh_token(const OAuth2Token& token) {
  json result = json::object();
  if (token_endpoint_.empty() || token.refresh_token.empty()) {
    result["error"] = "No refresh_token or token endpoint configured";
    return result;
  }
  std::string cmd = "curl -s -X POST \"" + token_endpoint_ + "\"" +
                     " -H \"Content-Type: application/x-www-form-urlencoded\"" +
                     " -d \"grant_type=refresh_token&refresh_token=" + token.refresh_token + "\"";
  std::string response = run_curl(cmd);
  try {
    return json::parse(response);
  } catch (...) {
    result["error"] = "Failed to parse refresh response";
    return result;
  }
}

std::string fhir_scope_to_string(FHIRScope scope) {
  switch (scope) {
    case FHIRScope::PATIENT_READ: return "patient/Patient.read";
    case FHIRScope::PATIENT_WRITE: return "patient/Patient.write";
    case FHIRScope::OBSERVATION_READ: return "patient/Observation.read";
    case FHIRScope::OBSERVATION_WRITE: return "patient/Observation.write";
    case FHIRScope::FLAG_READ: return "patient/Flag.read";
    case FHIRScope::FLAG_WRITE: return "patient/Flag.write";
    case FHIRScope::DOCUMENT_REFERENCE_READ: return "patient/DocumentReference.read";
    case FHIRScope::DOCUMENT_REFERENCE_WRITE: return "patient/DocumentReference.write";
    case FHIRScope::CONDITION_READ: return "patient/Condition.read";
    case FHIRScope::CONDITION_WRITE: return "patient/Condition.write";
    case FHIRScope::MEDICATION_REQUEST_READ: return "patient/MedicationRequest.read";
    case FHIRScope::SERVICE_REQUEST_READ: return "patient/ServiceRequest.read";
    case FHIRScope::PROCEDURE_READ: return "patient/Procedure.read";
    case FHIRScope::ALLERGY_INTOLERANCE_READ: return "patient/AllergyIntolerance.read";
    case FHIRScope::CARE_PLAN_READ: return "patient/CarePlan.read";
    case FHIRScope::ENCOUNTER_READ: return "patient/Encounter.read";
    case FHIRScope::PROVENANCE_READ: return "patient/Provenance.read";
    case FHIRScope::AUDIT_EVENT_READ: return "patient/AuditEvent.read";
    default: return "";
  }
}

} // namespace FHIR
