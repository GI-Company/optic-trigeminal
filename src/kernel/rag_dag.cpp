#include "rag_dag.h"
#include <queue>
#include <algorithm>
#include <cmath>
#include <sstream>

RAGDAGSystem::RAGDAGSystem() : total_edges(0) {}

void RAGDAGSystem::initialize_from_knowledge_graph(const std::map<std::string, GraphNode>& kg_nodes,
                                                   const std::vector<GraphEdge>& kg_edges) {
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    nodes.clear();
    edges_by_dimension.clear();
    total_edges = 0;
    
    int count = 0;
    for (const auto& node_pair : kg_nodes) {
        const auto& kg_node = node_pair.second;
        DAGNode dag_node;
        dag_node.metadata = DAGNodeMetadata(kg_node.id);
        dag_node.metadata.concept = kg_node.label;
        dag_node.metadata.primary_domain = kg_node.type;
        dag_node.metadata.relevance_score = kg_node.importance;
        dag_node.embedding = kg_node.embedding;
        dag_node.metadata.abstraction_level = 0;
        
        nodes[kg_node.id] = dag_node;
        if (++count % 5000 == 0) {
            std::cout << "  RAG-DAG: Integrated " << count << " / " << kg_nodes.size() << " nodes..." << std::endl;
        }
    }
    
    count = 0;
    for (const auto& edge : kg_edges) {
        DimensionType dim = DimensionType::SEMANTIC;
        if (edge.type.find("temporal") != std::string::npos) dim = DimensionType::TEMPORAL;
        else if (edge.type.find("causal") != std::string::npos) dim = DimensionType::CAUSAL;
        else if (edge.type.find("hier") != std::string::npos) dim = DimensionType::HIERARCHICAL;
        
        add_dimensional_edge(edge.source, edge.target, dim, edge.weight, edge.type);
        if (++count % 5000 == 0) {
            std::cout << "  RAG-DAG: Added " << count << " / " << kg_edges.size() << " edges..." << std::endl;
        }
    }
}

std::string RAGDAGSystem::add_node(const std::string& concept, const Embedding& embedding,
                                  const std::string& domain, int abstraction_level) {
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    std::string node_id = "rag_" + concept + "_" + 
                         std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 1000000);
    
    DAGNode dag_node;
    dag_node.metadata = DAGNodeMetadata(node_id);
    dag_node.metadata.concept = concept;
    dag_node.metadata.primary_domain = domain;
    dag_node.metadata.abstraction_level = abstraction_level;
    dag_node.embedding = embedding;
    
    nodes[node_id] = dag_node;
    return node_id;
}

bool RAGDAGSystem::add_dimensional_edge(const std::string& source_id, const std::string& target_id,
                                       DimensionType dimension, float weight,
                                       const std::string& relationship_type) {
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    auto src_it = nodes.find(source_id);
    auto tgt_it = nodes.find(target_id);
    
    if (src_it == nodes.end() || tgt_it == nodes.end()) return false;
    
    src_it->second.outgoing_edges[dimension].insert(target_id);
    tgt_it->second.incoming_edges[dimension].insert(source_id);
    src_it->second.edge_weights[dimension][target_id] = weight;
    
    DimensionalEdge edge(source_id, target_id, dimension, weight, relationship_type, 1);
    edges_by_dimension[source_id + "-" + target_id].push_back(edge);
    total_edges++;
    
    return true;
}

std::vector<DimensionalRetrievalResult> RAGDAGSystem::retrieve_multidimensional(
    const std::string& query_concept,
    const Embedding& query_embedding,
    int top_k,
    const std::set<DimensionType>& active_dimensions) {
    
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    std::vector<DimensionalRetrievalResult> results;
    std::map<std::string, DimensionalRetrievalResult> node_scores;
    
    std::set<DimensionType> dims = active_dimensions.empty() ? 
        std::set<DimensionType>{DimensionType::SEMANTIC, DimensionType::TEMPORAL, 
                               DimensionType::CAUSAL, DimensionType::HIERARCHICAL,
                               DimensionType::DOMAIN_DIM, DimensionType::CONFIDENCE} :
        active_dimensions;
    
    for (const auto& node_pair : nodes) {
        const auto& node_id = node_pair.first;
        const auto& dag_node = node_pair.second;
        
        DimensionalRetrievalResult result;
        result.node_id = node_id;
        result.concept = dag_node.metadata.concept;
        result.path_length = 1;
        result.retrieval_path.push_back(node_id);
        result.confidence = 1.0f;
        
        float composite = 0.0f;
        
        if (dims.count(DimensionType::SEMANTIC)) {
            float sem_score = compute_semantic_similarity(query_embedding, dag_node.embedding);
            result.dimension_scores[DimensionType::SEMANTIC] = sem_score;
            composite += sem_score * 0.3f;
        }
        
        if (dims.count(DimensionType::TEMPORAL)) {
            float temp_score = compute_temporal_proximity(
                std::chrono::system_clock::now().time_since_epoch().count(),
                dag_node.metadata.last_accessed);
            result.dimension_scores[DimensionType::TEMPORAL] = temp_score;
            composite += temp_score * 0.2f;
        }
        
        if (dims.count(DimensionType::DOMAIN_DIM)) {
            float dom_score = compute_domain_alignment(query_concept, dag_node.metadata.primary_domain);
            result.dimension_scores[DimensionType::DOMAIN_DIM] = dom_score;
            composite += dom_score * 0.15f;
        }
        
        if (dims.count(DimensionType::CONFIDENCE)) {
            float conf_score = compute_confidence_score(dag_node.metadata.access_frequency,
                                                       dag_node.metadata.last_accessed);
            result.dimension_scores[DimensionType::CONFIDENCE] = conf_score;
            composite += conf_score * 0.05f;
        }
        
        result.composite_score = composite;
        node_scores[node_id] = result;
    }
    
    for (auto& pair : node_scores) {
        results.push_back(pair.second);
    }
    
    std::sort(results.begin(), results.end(),
             [](const DimensionalRetrievalResult& a, const DimensionalRetrievalResult& b) {
                 return a.composite_score > b.composite_score;
             });
    
    if (results.size() > static_cast<size_t>(top_k)) {
        results.resize(top_k);
    }
    
    for (auto& result : results) {
        auto it = nodes.find(result.node_id);
        if (it != nodes.end()) {
            it->second.metadata.access_frequency++;
            it->second.metadata.last_accessed = 
                std::chrono::system_clock::now().time_since_epoch().count();
        }
    }
    
    return results;
}

std::vector<std::string> RAGDAGSystem::bfs_find_path(const std::string& start,
                                                   const std::string& end,
                                                   DimensionType dimension,
                                                   int max_hops) {
    std::vector<std::string> path;
    
    if (nodes.find(start) == nodes.end() || nodes.find(end) == nodes.end()) {
        return path;
    }
    
    std::queue<std::pair<std::string, std::vector<std::string>>> q;
    std::set<std::string> visited;
    
    q.push({start, {start}});
    visited.insert(start);
    
    while (!q.empty()) {
        auto [current, current_path] = q.front();
        q.pop();
        
        if (current == end) {
            return current_path;
        }
        
        if (static_cast<int>(current_path.size()) >= max_hops) {
            continue;
        }
        
        auto it = nodes.find(current);
        if (it == nodes.end()) continue;
        
        auto dim_it = it->second.outgoing_edges.find(dimension);
        if (dim_it == it->second.outgoing_edges.end()) continue;
        
        for (const auto& next_node : dim_it->second) {
            if (visited.find(next_node) == visited.end()) {
                visited.insert(next_node);
                auto new_path = current_path;
                new_path.push_back(next_node);
                q.push({next_node, new_path});
            }
        }
    }
    
    return path;
}

std::vector<std::string> RAGDAGSystem::find_causal_chain(const std::string& start_node,
                                                       const std::string& end_node,
                                                       int max_hops) {
    return bfs_find_path(start_node, end_node, DimensionType::CAUSAL, max_hops);
}

std::vector<DimensionalRetrievalResult> RAGDAGSystem::cross_dimensional_search(
    const std::string& query,
    const Embedding& query_embedding,
    float semantic_weight,
    float temporal_weight,
    float causal_weight,
    float hierarchical_weight,
    float domain_weight,
    float confidence_weight,
    int top_k) {
    
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    std::vector<DimensionalRetrievalResult> results;
    std::map<std::string, float> composite_scores;
    
    for (const auto& node_pair : nodes) {
        const auto& node_id = node_pair.first;
        const auto& dag_node = node_pair.second;
        
        float composite = 0.0f;
        
        float sem = compute_semantic_similarity(query_embedding, dag_node.embedding) * semantic_weight;
        float temp = compute_temporal_proximity(
            std::chrono::system_clock::now().time_since_epoch().count(),
            dag_node.metadata.last_accessed) * temporal_weight;
        float dom = compute_domain_alignment(query, dag_node.metadata.primary_domain) * domain_weight;
        float conf = compute_confidence_score(dag_node.metadata.access_frequency,
                                             dag_node.metadata.last_accessed) * confidence_weight;
        
        composite = sem + temp + dom + conf;
        composite_scores[node_id] = composite;
    }
    
    for (const auto& pair : composite_scores) {
        DimensionalRetrievalResult result;
        result.node_id = pair.first;
        result.composite_score = pair.second;
        result.concept = nodes[pair.first].metadata.concept;
        results.push_back(result);
    }
    
    std::sort(results.begin(), results.end(),
             [](const DimensionalRetrievalResult& a, const DimensionalRetrievalResult& b) {
                 return a.composite_score > b.composite_score;
             });
    
    if (results.size() > static_cast<size_t>(top_k)) {
        results.resize(top_k);
    }
    
    return results;
}

std::vector<DimensionalRetrievalResult> RAGDAGSystem::reasoning_path_search(
    const std::string& reasoning_type,
    const std::string& query_concept,
    const Embedding& query_embedding,
    int max_path_length) {
    
    return retrieve_multidimensional(query_concept, query_embedding, max_path_length,
                                    {DimensionType::SEMANTIC, DimensionType::CAUSAL,
                                     DimensionType::HIERARCHICAL});
}

void RAGDAGSystem::merge_with_episodic_memory(const std::vector<EpisodicMemory>& episodes) {
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    int count = 0;
    for (const auto& episode : episodes) {
        std::string node_id = "ep_" + episode.episode_id;
        
        if (nodes.find(node_id) == nodes.end()) {
            DAGNode dag_node;
            dag_node.metadata = DAGNodeMetadata(node_id);
            dag_node.metadata.concept = episode.input.substr(0, 50);
            dag_node.metadata.primary_domain = "episodic";
            dag_node.metadata.abstraction_level = 0;
            dag_node.embedding = episode.context_embedding;
            nodes[node_id] = dag_node;
        }
        
        if (++count % 5000 == 0) {
            std::cout << "  RAG-DAG: Merged " << count << " / " << episodes.size() << " episodes..." << std::endl;
        }
    }
}

std::map<std::string, std::vector<DimensionalRetrievalResult>> RAGDAGSystem::get_all_dimensional_views(
    const std::string& node_id) {
    
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    std::map<std::string, std::vector<DimensionalRetrievalResult>> views;
    
    auto it = nodes.find(node_id);
    if (it == nodes.end()) return views;
    
    std::vector<DimensionType> dimensions = {
        DimensionType::SEMANTIC, DimensionType::TEMPORAL, DimensionType::CAUSAL,
        DimensionType::HIERARCHICAL, DimensionType::DOMAIN_DIM, DimensionType::CONFIDENCE
    };
    
    for (const auto& dim : dimensions) {
        std::vector<DimensionalRetrievalResult> dim_results;
        
        auto out_it = it->second.outgoing_edges.find(dim);
        if (out_it != it->second.outgoing_edges.end()) {
            for (const auto& target_id : out_it->second) {
                DimensionalRetrievalResult result;
                result.node_id = target_id;
                result.concept = nodes[target_id].metadata.concept;
                result.path_length = 1;
                result.retrieval_path = {node_id, target_id};
                
                dim_results.push_back(result);
            }
        }
        
        std::string dim_name;
        switch (dim) {
            case DimensionType::SEMANTIC: dim_name = "semantic"; break;
            case DimensionType::TEMPORAL: dim_name = "temporal"; break;
            case DimensionType::CAUSAL: dim_name = "causal"; break;
            case DimensionType::HIERARCHICAL: dim_name = "hierarchical"; break;
            case DimensionType::DOMAIN_DIM: dim_name = "domain"; break;
            case DimensionType::CONFIDENCE: dim_name = "confidence"; break;
        }
        
        views[dim_name] = dim_results;
    }
    
    return views;
}

std::string RAGDAGSystem::get_dag_statistics() const {
    std::lock_guard<std::recursive_mutex> lock(dag_mutex);
    
    std::stringstream ss;
    ss << "RAG-DAG Statistics:\n";
    ss << "  Nodes: " << nodes.size() << "\n";
    ss << "  Edges: " << total_edges << "\n";
    ss << "  Avg degree: " << (nodes.empty() ? 0 : total_edges * 2.0f / nodes.size()) << "\n";
    
    int total_access = 0;
    for (const auto& node_pair : nodes) {
        total_access += node_pair.second.metadata.access_frequency;
    }
    ss << "  Total accesses: " << total_access << "\n";
    
    return ss.str();
}

float RAGDAGSystem::compute_semantic_similarity(const Embedding& a, const Embedding& b) const {
    return a.cosine_similarity(b);
}

float RAGDAGSystem::compute_temporal_proximity(int64_t t1, int64_t t2) const {
    int64_t diff = std::abs(t1 - t2);
    float days_ago = diff / (86400000.0f);
    return std::max(0.0f, 1.0f - (days_ago / 365.0f));
}

float RAGDAGSystem::compute_causal_strength(const std::string& src, const std::string& tgt) const {
    auto src_it = nodes.find(src);
    auto tgt_it = nodes.find(tgt);
    
    if (src_it == nodes.end() || tgt_it == nodes.end()) return 0.0f;
    
    auto causal_it = src_it->second.edge_weights.find(DimensionType::CAUSAL);
    if (causal_it == src_it->second.edge_weights.end()) return 0.0f;
    
    auto edge_it = causal_it->second.find(tgt);
    return edge_it != causal_it->second.end() ? edge_it->second : 0.0f;
}

float RAGDAGSystem::compute_hierarchical_distance(int level1, int level2) const {
    int diff = std::abs(level1 - level2);
    return std::max(0.0f, 1.0f - (diff * 0.2f));
}

float RAGDAGSystem::compute_domain_alignment(const std::string& d1, const std::string& d2) const {
    if (d1 == d2) return 1.0f;
    if (d1.empty() || d2.empty()) return 0.5f;
    return 0.3f;
}

float RAGDAGSystem::compute_confidence_score(int access_freq, int64_t recency) const {
    float freq_score = std::min(1.0f, access_freq / 10.0f);
    float recency_score = compute_temporal_proximity(
        std::chrono::system_clock::now().time_since_epoch().count(), recency);
    return (freq_score * 0.6f) + (recency_score * 0.4f);
}
