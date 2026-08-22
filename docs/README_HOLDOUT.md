# ACmK Holdout Validation Implementation Details

This document outlines the implementation of deterministic per-stage holdout validation within the Optic-Trigeminal ACmK project, as per the specified constraints.

## Overview

The goal of this feature is to provide a deterministic 85% TRAIN / 15% HOLDOUT split for each training stage. This ensures that a consistent subset of data is held out for validation, making training outcomes auditable and reproducible across runs and environments.

## Implementation Strategy

Due to strict constraints regarding modifications to core modules and API signatures, the holdout logic is implemented directly within the `TrainingOrchestrator` module, leveraging existing data structures and VFS capabilities.

### Key Components and Modifications:

1.  **`include/holdout_validation_utils.h`**:
    *   A new header-only utility file introduced to provide a cross-platform deterministic hashing function (`deterministic_hash`).
    *   Uses the FNV-1a hashing algorithm to ensure hash consistency across different compilers and operating systems. This hash is applied to the `source_record_id` of `ProcessedDataRecord`s.

2.  **`include/training_orchestrator.h`**:
    *   **New Private Methods:**
        *   `void split_data_for_holdout(const std::vector<ProcessedDataRecord>& stage_data, std::vector<ProcessedDataRecord>& training_set, std::vector<ProcessedDataRecord>& holdout_set, int stage_id)`: Takes a vector of processed data records for a stage, deterministically splits them into training and holdout sets, and populates the output vectors.
        *   `void create_holdout_artifacts(const std::vector<ProcessedDataRecord>& holdout_set, int stage_id)`: Responsible for persisting the holdout set and its metadata.
    *   **Removed Private Method:** The `std::string compute_hash(const std::string& content) const` declaration was removed as its functionality is superseded by `HoldoutValidationUtils::deterministic_hash`.

3.  **`src/training_orchestrator.cpp`**:
    *   **Includes:** Added `#include "holdout_validation_utils.h"` and `#include <nlohmann/json.hpp>` (for JSON serialization of metadata).
    *   **Removed Implementation:** The implementation of `TrainingOrchestrator::compute_hash` was removed to align with the header changes.
    *   **`TrainingOrchestrator::convert_example_to_record`**: The line assigning `record.content_hash` using the old `compute_hash` was removed. The `RawDataRecord`'s content hash is now assumed to be managed by the `DataPipeline` during ingestion.
    *   **`split_data_for_holdout` Implementation**:
        *   Takes the combined `ProcessedDataRecord`s for a stage.
        *   Sorts the records deterministically by `source_record_id`.
        *   For each record, it computes a hash of its `source_record_id` using `HoldoutValidationUtils::deterministic_hash`.
        *   Based on a modulo operation on the hash and a `HOLDOUT_RATIO` (15%), records are assigned to either `training_set` or `holdout_set`.
        *   Logs the split counts for auditability.
    *   **`create_holdout_artifacts` Implementation**:
        *   Creates a directory `artifacts/holdout/stage_<stage_id>` using the `VFSManager`.
        *   Generates a `split_metadata.json` file in this directory.
        *   `split_metadata.json` contains: `stage_id`, `holdout_count`, a list of `holdout_record_ids` (using `source_record_id`), a timestamp, and a hash of the entire holdout set's IDs for deep auditability.
        *   The actual `ProcessedDataRecord` content for the holdout set is not directly serialized into separate files in this minimal implementation, only their `source_record_id`s are recorded in `split_metadata.json`. Future enhancements could involve full serialization if needed.
        *   Logs the creation of artifacts.
    *   **`execute_stage` Modification**:
        *   After fetching all data for a stage (`get_data_for_stage`), it calls `split_data_for_holdout` to get the `training_set` and `holdout_set`.
        *   The `training_set` is then dynamically filtered by `DataSourceType` within each `switch` case (e.g., `STAGE_0_BASE_KNOWLEDGE` filters for `STEM_QA` and `CODING_FUNDAMENTALS`) before being passed to the respective `training_controller->run_stage_X_...` method. This ensures that the existing `training_controller` API signatures are respected.
        *   After the training call, `create_holdout_artifacts` is invoked with the `holdout_set` to persist validation data.
        *   The `metrics.total_records_processed` is updated with the size of the `training_set` only.

## Auditability

*   **Deterministic Splitting:** The use of FNV-1a hash on `source_record_id` ensures that for the same input data, the split into training and holdout sets will always be identical.
*   **Metadata Persistence:** `split_metadata.json` stores all `source_record_id`s assigned to the holdout set for a given stage, along with a hash of these IDs. This allows for re-verification of the exact holdout composition.
*   **Artifact Location:** All holdout-related artifacts are stored under `artifacts/holdout/<stage_id>/`, providing a clear and isolated location for inspection.

## Verification Notes

To verify the implementation, follow these steps:

1.  **Code Integrity Check:** Perform a `git diff` to ensure that only `include/holdout_validation_utils.h`, `include/training_orchestrator.h`, and `src/training_orchestrator.cpp` have been modified. No other modules should show changes.
2.  **Compilation Test:** Ensure the entire ACmK project builds successfully without warnings or errors.
3.  **Deterministic Reproducibility Test:**
    *   Run the training pipeline twice with the exact same input dataset.
    *   Compare the `split_metadata.json` files generated for each stage in `artifacts/holdout/`. They should be byte-for-byte identical.
    *   The `holdout_set_hash` within `split_metadata.json` should match across runs.
4.  **Artifact Verification:** Check the contents of `artifacts/holdout/<stage_id>/split_metadata.json` to confirm that the `holdout_record_ids` match the expected 15% split and correspond to valid `source_record_id`s from your input data.
5.  **No Side-Effect Test:** Run the full training pipeline and confirm that if holdouts are not explicitly used by downstream components, the training outputs remain unchanged compared to a pre-holdout implementation run (assuming `training_controller` only uses the provided training data).
6.  **Metrics Logging Verification:** Confirm that only stage-level holdout creation metrics (as logged by `log_stage_execution`) are present, and no modifications to `TelemetryCollector` or existing logging pipelines were introduced.
