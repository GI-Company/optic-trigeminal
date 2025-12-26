#include "vfs_manager.h"
#include "rag_dag.h"
#include "inference_engine.h"
#include <iostream>
#include <thread>
#include <chrono>

void test_vfs_basic_operations() {
    std::cout << "\n=== VFS Basic Operations Test ===" << std::endl;
    
    VFSManager vfs;
    
    auto task1 = vfs.create_process("math_solver", "Processes mathematical equations");
    auto task2 = vfs.create_process("logic_analyzer", "Analyzes logical statements");
    auto subtask1 = vfs.create_process("equation_simplifier", "Simplifies equations", task1->process_id);
    
    vfs.initialize_process_resources(task1->process_id, 5000.0f, 2000.0f, 100000.0f);
    vfs.initialize_process_resources(task2->process_id, 3000.0f, 1500.0f, 60000.0f);
    vfs.initialize_process_resources(subtask1->process_id, 1000.0f, 500.0f, 30000.0f);
    
    std::cout << "Created " << vfs.get_total_process_count() << " processes" << std::endl;
    
    vfs.transition_process_state(task1->process_id, ProcessState::REASONING, "Starting math analysis");
    vfs.transition_process_state(task2->process_id, ProcessState::REASONING, "Starting logic analysis");
    vfs.transition_process_state(subtask1->process_id, ProcessState::COMPUTING, "Simplifying equations");
    
    auto usage = vfs.get_process_resource_usage(task1->process_id);
    std::cout << "\nTask1 Resource Usage:" << std::endl;
    for (const auto& [resource, utilization] : usage) {
        std::cout << "  " << resource << ": " << utilization << "%" << std::endl;
    }
    
    vfs.transition_process_state(subtask1->process_id, ProcessState::COMPLETE, "Equations simplified");
    vfs.transition_process_state(task1->process_id, ProcessState::COMPLETE, "Math analysis complete");
    vfs.transition_process_state(task2->process_id, ProcessState::COMPLETE, "Logic analysis complete");
    
    std::cout << "\nProcess Hierarchy:" << std::endl;
    std::cout << vfs.get_process_hierarchy_tree() << std::endl;
}

void test_rag_dag_retrieval() {
    std::cout << "\n=== RAG-DAG Retrieval Test ===" << std::endl;
    
    RAGDAGSystem rag_dag;
    
    Embedding sample_emb(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; ++i) {
        sample_emb.values[i] = 0.1f + (i % 10) * 0.01f;
    }
    
    auto node1 = rag_dag.add_node("mathematical_concept", sample_emb, "math", 0);
    auto node2 = rag_dag.add_node("algebra", sample_emb, "math", 1);
    auto node3 = rag_dag.add_node("geometry", sample_emb, "math", 1);
    auto node4 = rag_dag.add_node("calculus", sample_emb, "math", 2);
    
    rag_dag.add_dimensional_edge(node1, node2, DimensionType::HIERARCHICAL, 0.8f, "is_subfield_of");
    rag_dag.add_dimensional_edge(node1, node3, DimensionType::HIERARCHICAL, 0.8f, "is_subfield_of");
    rag_dag.add_dimensional_edge(node2, node4, DimensionType::HIERARCHICAL, 0.7f, "leads_to");
    rag_dag.add_dimensional_edge(node2, node3, DimensionType::SEMANTIC, 0.6f, "related_to");
    
    std::cout << "RAG-DAG initialized with " << rag_dag.get_node_count() << " nodes and " 
              << rag_dag.get_edge_count() << " edges" << std::endl;
    
    Embedding query_emb(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; ++i) {
        query_emb.values[i] = 0.1f + ((i+3) % 10) * 0.01f;
    }
    
    auto results = rag_dag.retrieve_multidimensional("algebra", query_emb, 3);
    std::cout << "\nRetrieval results for 'algebra':" << std::endl;
    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "  " << (i+1) << ". " << results[i].concept 
                  << " (score: " << results[i].composite_score << ")" << std::endl;
    }
    
    auto causal_path = rag_dag.find_causal_chain(node1, node4, 5);
    std::cout << "\nCausal chain from math_concept to calculus: ";
    if (!causal_path.empty()) {
        std::cout << "path length " << causal_path.size() << std::endl;
    } else {
        std::cout << "no direct causal path" << std::endl;
    }
    
    std::cout << "\n" << rag_dag.get_dag_statistics();
}

void test_integrated_process_reasoning() {
    std::cout << "\n=== Integrated Process Reasoning Test ===" << std::endl;
    
    VFSManager vfs;
    RAGDAGSystem rag_dag;
    
    std::cout << "Creating multi-process reasoning scenario..." << std::endl;
    
    auto master_process = vfs.create_process("query_processor", "Main query processing");
    vfs.initialize_process_resources(master_process->process_id, 10000.0f, 5000.0f, 300000.0f);
    vfs.transition_process_state(master_process->process_id, ProcessState::REASONING, "Processing user query");
    
    auto retrieval_task = vfs.create_process("semantic_retrieval", "Retrieves semantically similar concepts", 
                                             master_process->process_id);
    auto reasoning_task = vfs.create_process("reasoning_engine", "Applies reasoning rules",
                                             master_process->process_id);
    auto response_task = vfs.create_process("response_generator", "Generates final response",
                                            master_process->process_id);
    
    vfs.initialize_process_resources(retrieval_task->process_id, 3000.0f, 1500.0f, 60000.0f);
    vfs.initialize_process_resources(reasoning_task->process_id, 3000.0f, 1500.0f, 60000.0f);
    vfs.initialize_process_resources(response_task->process_id, 2000.0f, 1000.0f, 30000.0f);
    
    vfs.transition_process_state(retrieval_task->process_id, ProcessState::RETRIEVING, "Searching knowledge base");
    vfs.transition_process_state(reasoning_task->process_id, ProcessState::IDLE, "Waiting for retrieval");
    vfs.transition_process_state(response_task->process_id, ProcessState::IDLE, "Waiting for reasoning");
    
    std::cout << "Process pipeline created:" << std::endl;
    std::cout << vfs.get_process_hierarchy_tree() << std::endl;
    
    vfs.transition_process_state(retrieval_task->process_id, ProcessState::COMPLETE, "Retrieved 5 concepts");
    vfs.transition_process_state(reasoning_task->process_id, ProcessState::REASONING, "Applying inference rules");
    
    std::cout << "Sub-processes resource utilization:" << std::endl;
    auto ret_usage = vfs.get_process_resource_usage(retrieval_task->process_id);
    std::cout << "  Retrieval task tokens: " << ret_usage["tokens"] << "%" << std::endl;
    
    vfs.transition_process_state(reasoning_task->process_id, ProcessState::COMPLETE, "Reasoning complete");
    vfs.transition_process_state(response_task->process_id, ProcessState::GENERATING, "Composing response");
    vfs.transition_process_state(response_task->process_id, ProcessState::COMPLETE, "Response generated");
    vfs.transition_process_state(master_process->process_id, ProcessState::COMPLETE, "Query processing complete");
    
    std::cout << "\nFinal process tree (all states):" << std::endl;
    std::cout << vfs.get_process_hierarchy_tree() << std::endl;
}

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "VFS + RAG-DAG System Test Suite" << std::endl;
    std::cout << "==================================================" << std::endl;
    
    try {
        test_vfs_basic_operations();
        test_rag_dag_retrieval();
        test_integrated_process_reasoning();
        
        std::cout << "\n==================================================" << std::endl;
        std::cout << "All tests completed successfully!" << std::endl;
        std::cout << "==================================================" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
