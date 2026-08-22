#pragma once

// Instructor-owned class rosters for mass/institutional education adoption.
//
// An instructor creates a Cohort, bulk-imports a student roster into it
// (each student becomes a real, individually-authenticated staff account
// via AuthManager so their training sessions are attributable and can't
// collide with each other -- see http_server.cpp's per-user training
// session map), and later reviews aggregate progress across the cohort
// using real TrainingAnalyticsStore data. No fabricated metrics: anything
// this doesn't have real event data for, it omits rather than guesses.

#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <memory>

struct CohortStudent {
  std::string staff_id;     // Real AuthManager account id, e.g. "STU_a1b2c3d4"
  std::string display_name;
  std::string external_id;  // Instructor-supplied roster identifier (email/student ID), optional
};

struct Cohort {
  std::string cohort_id;
  std::string name;
  std::string instructor_id;
  std::time_t created_at = 0;
  std::vector<CohortStudent> students;
};

struct ImportedCredential {
  std::string staff_id;
  std::string display_name;
  std::string password;  // Plaintext, returned exactly once -- never persisted in plaintext.
};

class CohortManager {
public:
  bool initialize();

  Cohort create_cohort(const std::string& instructor_id, const std::string& name);
  std::vector<Cohort> list_cohorts_for_instructor(const std::string& instructor_id) const;
  const Cohort* get_cohort(const std::string& cohort_id) const;
  bool instructor_owns_cohort(const std::string& instructor_id, const std::string& cohort_id) const;

  // Bulk-provisions one Role::RN auth account per (display_name, external_id)
  // pair via g_auth_manager, adds each to the cohort roster, and returns the
  // one-time plaintext credentials for the instructor to hand out. Entries
  // with an empty display_name are skipped.
  std::vector<ImportedCredential> import_roster(
      const std::string& cohort_id,
      const std::vector<std::pair<std::string, std::string>>& students);

  bool remove_student(const std::string& cohort_id, const std::string& staff_id);

private:
  std::map<std::string, Cohort> cohorts_;
  std::string store_path_ = "data/cohorts/cohorts.json";

  void load();
  void save() const;
  std::string generate_cohort_id() const;
  std::string generate_student_staff_id() const;
};

extern std::unique_ptr<CohortManager> g_cohort_manager;
