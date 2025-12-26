#pragma once

#include "types.h"
#include <set>
#include <memory>
#include <map>
#include <mutex>

enum class DimensionType {
    SEMANTIC = 0,
    TEMPORAL = 1,
    CAUSAL = 2,
    HIERARCHICAL = 3,
    DOMAIN_DIM = 4,
    CONFIDENCE = 5
};

struct DAGNodeMetadata {
    std::string node_id;
    std::string concept;
    float relevance_score;
    int access_frequency;
    int64_t first_encountered;
    int64_t last_accessed;
    std::string primary_domain;
    int abstraction_level;
    
    DAGNodeMetadata() : relevance_score(0.0f), access_frequency(0), 
                       first_encountered(0), last_accessed(0), abstraction_level(0) {}
    
    explicit DAGNodeMetadata(const std::string& id) : node_id(id), 
                                                      relevance_score(0.0f),
                                                      access_frequency(0),
                                                      abstraction_level(0) {
        first_encountered = std::chrono::system_clock::now().time_since_epoch().count();
        last_accessed = first_encountered;
    }
};

struct DimensionalEdge {
    std::string source_id;
    std::string target_id;
    DimensionType dimension;
    float weight;
    std::string relationship_type;
    int hop_distance;
    bool is_bidirectional;
    
    DimensionalEdge() : weight(0.0f), hop_distance(0), is_bidirectional(false) {}
    DimensionalEdge(const std::string& src, const std::string& tgt,
                   DimensionType dim, float w, const std::string& rel_type, int hops = 1)
        : source_id(src), target_id(tgt), dimension(dim), weight(w),
          relationship_type(rel_type), hop_distance(hops), is_bidirectional(false) {}
};

struct DimensionalRetrievalResult {
    std::string node_id;
    std::string concept;
    float composite_score;
    std::map<DimensionType, float> dimension_scores;
    int path_length;
    std::vector<std::string> retrieval_path;
    float confidence;
    
    DimensionalRetrievalResult() : composite_score(0.0f), path_length(0), confidence(0.0f) {}
};

class RAGDAGSystem {
public:
    RAGDAGSystem();
    
    void initialize_from_knowledge_graph(const std::map<std::string, GraphNode>& kg_nodes,
                                        const std::vector<GraphEdge>& kg_edges);
    
    std::string add_node(const std::string& concept, const Embedding& embedding,
                        const std::string& domain = "general", int abstraction_level = 0);
    
    bool add_dimensional_edge(const std::string& source_id, const std::string& target_id,
                             DimensionType dimension, float weight,
                             const std::string& relationship_type);
    
    std::vector<DimensionalRetrievalResult> retrieve_multidimensional(
        const std::string& query_concept,
        const Embedding& query_embedding,
        int top_k = 5,
        const std::set<DimensionType>& active_dimensions = {});
    
    std::vector<std::string> find_causal_chain(const std::string& start_node,
                                              const std::string& end_node,
                                              int max_hops = 5);
    
    std::vector<DimensionalRetrievalResult> cross_dimensional_search(
        const std::string& query,
        const Embedding& query_embedding,
        float semantic_weight = 0.3f,
        float temporal_weight = 0.2f,
        float causal_weight = 0.2f,
        float hierarchical_weight = 0.15f,
        float domain_weight = 0.1f,
        float confidence_weight = 0.05f,
        int top_k = 5);
    
    std::vector<DimensionalRetrievalResult> reasoning_path_search(
        const std::string& reasoning_type,
        const std::string& query_concept,
        const Embedding& query_embedding,
        int max_path_length = 7);
    
    void merge_with_episodic_memory(const std::vector<EpisodicMemory>& episodes);
    
    std::map<std::string, std::vector<DimensionalRetrievalResult>> get_all_dimensional_views(
        const std::string& node_id);
    
    int get_node_count() const { return nodes.size(); }
    int get_edge_count() const { return total_edges; }
    
    std::string get_dag_statistics() const;
    
private:
    struct DAGNode {
        DAGNodeMetadata metadata;
        Embedding embedding;
        std::map<DimensionType, std::set<std::string>> outgoing_edges;
        std::map<DimensionType, std::set<std::string>> incoming_edges;
        std::map<DimensionType, std::map<std::string, float>> edge_weights;
    };
    
    std::map<std::string, DAGNode> nodes;
    std::map<std::string, std::vector<DimensionalEdge>> edges_by_dimension;
    int total_edges;
    mutable std::recursive_mutex dag_mutex;
    
    float compute_semantic_similarity(const Embedding& a, const Embedding& b) const;
    float compute_temporal_proximity(int64_t t1, int64_t t2) const;
    float compute_causal_strength(const std::string& src, const std::string& tgt) const;
    float compute_hierarchical_distance(int level1, int level2) const;
    float compute_domain_alignment(const std::string& d1, const std::string& d2) const;
    float compute_confidence_score(int access_freq, int64_t recency) const;
    
    std::vector<std::string> bfs_find_path(const std::string& start,
                                          const std::string& end,
                                          DimensionType dimension,
                                          int max_hops);
};
