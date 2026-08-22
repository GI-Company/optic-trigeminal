#include "cohort_manager.h"
#include "auth_manager.h"
#include "crypto_utils.h"
#include "json_lite.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>

std::unique_ptr<CohortManager> g_cohort_manager = nullptr;

namespace fs = std::filesystem;

bool CohortManager::initialize() {
  load();
  return true;
}

std::string CohortManager::generate_cohort_id() const {
  return "COHORT_" + Crypto::random_hex(4);
}

std::string CohortManager::generate_student_staff_id() const {
  return "STU_" + Crypto::random_hex(4);
}

Cohort CohortManager::create_cohort(const std::string& instructor_id, const std::string& name) {
  Cohort cohort;
  cohort.cohort_id = generate_cohort_id();
  cohort.name = name;
  cohort.instructor_id = instructor_id;
  cohort.created_at = std::time(nullptr);
  cohorts_[cohort.cohort_id] = cohort;
  save();
  return cohort;
}

std::vector<Cohort> CohortManager::list_cohorts_for_instructor(const std::string& instructor_id) const {
  std::vector<Cohort> result;
  for (const auto& [id, cohort] : cohorts_) {
    if (cohort.instructor_id == instructor_id) result.push_back(cohort);
  }
  return result;
}

const Cohort* CohortManager::get_cohort(const std::string& cohort_id) const {
  auto it = cohorts_.find(cohort_id);
  return it == cohorts_.end() ? nullptr : &it->second;
}

bool CohortManager::instructor_owns_cohort(const std::string& instructor_id, const std::string& cohort_id) const {
  const Cohort* cohort = get_cohort(cohort_id);
  return cohort != nullptr && cohort->instructor_id == instructor_id;
}

std::vector<ImportedCredential> CohortManager::import_roster(
    const std::string& cohort_id,
    const std::vector<std::pair<std::string, std::string>>& students) {
  std::vector<ImportedCredential> result;
  auto it = cohorts_.find(cohort_id);
  if (it == cohorts_.end()) return result;

  for (const auto& [display_name, external_id] : students) {
    if (display_name.empty()) continue;

    std::string staff_id = generate_student_staff_id();
    std::string password = Crypto::random_hex(4);  // 8 hex chars, meets the 8+ char minimum used elsewhere

    StaffMember member;
    member.staff_id = staff_id;
    member.name = display_name;
    member.role = Role::RN;
    member.assigned_patients = {};
    member.active = true;
    member.last_sign_in = 0;
    member.password_hash = Crypto::hash_password(password);

    if (!g_auth_manager->add_staff_member(member)) continue;  // staff_id collision, effectively unreachable

    it->second.students.push_back(CohortStudent{staff_id, display_name, external_id});
    result.push_back(ImportedCredential{staff_id, display_name, password});
  }

  save();
  return result;
}

bool CohortManager::remove_student(const std::string& cohort_id, const std::string& staff_id) {
  auto it = cohorts_.find(cohort_id);
  if (it == cohorts_.end()) return false;

  auto& students = it->second.students;
  auto pos = std::find_if(students.begin(), students.end(),
                           [&](const CohortStudent& s) { return s.staff_id == staff_id; });
  if (pos == students.end()) return false;

  students.erase(pos);
  save();
  return true;
}

void CohortManager::load() {
  std::ifstream in(store_path_);
  if (!in.is_open()) return;  // No persisted cohorts yet -- normal on first boot.

  std::stringstream buffer;
  buffer << in.rdbuf();
  try {
    json parsed = json::parse(buffer.str());
    if (!parsed.is_array()) return;
    for (const auto& entry_ptr : parsed.items()) {
      const json& entry = *entry_ptr;
      Cohort cohort;
      cohort.cohort_id = entry.contains("cohort_id") ? entry.at("cohort_id").as_string("") : "";
      if (cohort.cohort_id.empty()) continue;
      cohort.name = entry.contains("name") ? entry.at("name").as_string("") : "";
      cohort.instructor_id = entry.contains("instructor_id") ? entry.at("instructor_id").as_string("") : "";
      cohort.created_at = entry.contains("created_at") ? static_cast<std::time_t>(entry.at("created_at").as_double(0)) : 0;

      if (entry.contains("students")) {
        const json& students = entry.at("students");
        for (const auto& s_ptr : students.items()) {
          const json& s = *s_ptr;
          CohortStudent student;
          student.staff_id = s.contains("staff_id") ? s.at("staff_id").as_string("") : "";
          student.display_name = s.contains("display_name") ? s.at("display_name").as_string("") : "";
          student.external_id = s.contains("external_id") ? s.at("external_id").as_string("") : "";
          if (!student.staff_id.empty()) cohort.students.push_back(student);
        }
      }

      cohorts_[cohort.cohort_id] = cohort;
    }
    std::cout << "[CohortManager] Loaded " << cohorts_.size() << " persisted cohort(s) from "
              << store_path_ << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "[CohortManager] Failed to parse " << store_path_ << ": " << e.what() << std::endl;
  }
}

void CohortManager::save() const {
  fs::path path(store_path_);
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);

  json out = json::array();
  for (const auto& [id, cohort] : cohorts_) {
    json entry = json::object();
    entry["cohort_id"] = json(cohort.cohort_id);
    entry["name"] = json(cohort.name);
    entry["instructor_id"] = json(cohort.instructor_id);
    entry["created_at"] = json(static_cast<double>(cohort.created_at));

    json students = json::array();
    for (const auto& s : cohort.students) {
      json s_entry = json::object();
      s_entry["staff_id"] = json(s.staff_id);
      s_entry["display_name"] = json(s.display_name);
      s_entry["external_id"] = json(s.external_id);
      students.push_back(s_entry);
    }
    entry["students"] = students;

    out.push_back(entry);
  }

  std::ofstream file(store_path_);
  if (!file.is_open()) {
    std::cerr << "[CohortManager] Failed to write " << store_path_ << std::endl;
    return;
  }
  file << out.dump();
}
