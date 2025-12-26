#pragma once

#include "data_pipeline.h"
#include "types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

struct ArtifactRecord {
    std::string artifact_id;
    std::string source_record_id;
    DataSourceType source_type;
    std::string content_hash;
    int64_t created_timestamp;
    std::string pipeline_stage;
    std::string checksum;
    bool verified;
    std::map<std::string, std::string> metadata;
    
    ArtifactRecord() : created_timestamp(0), verified(false) {}
};

class ArtifactPersistenceManager {
public:
    ArtifactPersistenceManager(const std::string& storage_dir = "data/artifacts");
    
    bool save_processed_record(const ProcessedDataRecord& record, const std::string& stage);
    
    bool save_batch(const std::vector<ProcessedDataRecord>& records, const std::string& stage);
    
    bool load_processed_record(const std::string& record_id, ProcessedDataRecord& record);
    
    std::vector<ProcessedDataRecord> load_batch_by_stage(const std::string& stage);
    
    std::vector<ProcessedDataRecord> load_batch_by_source(DataSourceType source);
    
    bool record_exists(const std::string& record_id) const;
    
    bool delete_artifact(const std::string& artifact_id);
    
    std::vector<ArtifactRecord> list_artifacts_by_stage(const std::string& stage);
    
    std::vector<ArtifactRecord> list_all_artifacts();
    
    int get_artifact_count() const;
    
    bool verify_artifact_integrity(const std::string& artifact_id);
    
    std::string get_storage_stats() const;
    
    void export_artifacts_to_json(const std::string& output_file);
    
    bool import_artifacts_from_json(const std::string& input_file);
    
    bool cleanup_artifacts_older_than(int days);
    
private:
    std::string storage_directory;
    std::map<std::string, ArtifactRecord> artifact_index;
    
    bool ensure_storage_directory_exists();
    
    std::string get_stage_directory(const std::string& stage) const;
    
    bool serialize_record(const ProcessedDataRecord& record, const std::string& filepath);
    
    bool deserialize_record(const std::string& filepath, ProcessedDataRecord& record);
    
    std::string compute_record_hash(const ProcessedDataRecord& record) const;
    
    void build_artifact_index();
    
    std::string generate_artifact_id(const ProcessedDataRecord& record);
};
