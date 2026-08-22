#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <chrono>

namespace ACMK {

struct ConstraintRef {
  std::string constraint_id;
  std::string constraint_name;
  double weight;
  bool dominated;
};

struct RationaleBlock {
  std::string block_id;
  std::string plain_language_text;
  std::vector<ConstraintRef> cited_constraints;
  std::string reasoning_step;
  double confidence_score;
};

struct ConfidenceMathInput {
  std::string input_label;
  double input_value;
  std::string formula;
};

struct KnownUnknown {
  std::string category;
  std::string description;
  std::string potential_impact;
};

struct ExplainabilitySchema {
  std::string session_id;
  std::string decision_id;
  std::vector<RationaleBlock> rationale_blocks;
  std::vector<ConstraintRef> dominant_constraints;
  std::vector<std::pair<std::string, std::string>> rejected_alternatives;
  std::vector<ConfidenceMathInput> confidence_math_inputs;
  std::vector<KnownUnknown> known_unknowns;
  std::chrono::system_clock::time_point generated_at;
};

struct NaturalLanguageExplanation {
  std::string narrative;
  std::string format;
  bool exportable;
  bool printable;
};

class ExplainabilityGenerator {
public:
  virtual ~ExplainabilityGenerator() = default;
  
  virtual ExplainabilitySchema generate_schema(
    const std::string& session_id,
    const std::string& decision_id) = 0;
  
  virtual NaturalLanguageExplanation generate_narrative(
    const ExplainabilitySchema& schema) = 0;
  
  virtual std::string generate_constraint_citations(
    const ExplainabilitySchema& schema) = 0;
  
  virtual std::string format_confidence_bounds(
    double lower_bound, double upper_bound,
    const std::vector<ConfidenceMathInput>& inputs) = 0;
  
  virtual std::string format_known_unknowns(
    const std::vector<KnownUnknown>& unknowns) = 0;
};

class ExplainabilityPanel {
public:
  virtual ~ExplainabilityPanel() = default;
  
  virtual void set_schema(const ExplainabilitySchema& schema) = 0;
  virtual ExplainabilitySchema get_schema() const = 0;
  
  virtual void render_plain_language() = 0;
  virtual void render_constraint_citations() = 0;
  virtual void render_confidence_bounds() = 0;
  virtual void render_known_unknowns() = 0;
  
  virtual std::string export_as_text() = 0;
  virtual std::string export_as_pdf() = 0;
  virtual std::string export_as_json() = 0;
};

class ConstraintLineage {
public:
  virtual ~ConstraintLineage() = default;
  
  struct ConstraintHistory {
    std::string constraint_id;
    std::vector<std::pair<std::chrono::system_clock::time_point, double>> 
      weight_history;
    std::vector<std::pair<std::chrono::system_clock::time_point, bool>>
      dominance_history;
  };
  
  virtual ConstraintHistory trace_constraint(
    const std::string& session_id,
    const std::string& constraint_id) = 0;
  
  virtual std::vector<std::string> get_constraint_interactions(
    const std::string& session_id) = 0;
  
  virtual std::string explain_dominance(
    const std::string& session_id,
    const std::string& dominant_constraint_id,
    const std::string& rejected_alternative_id) = 0;
};

}
