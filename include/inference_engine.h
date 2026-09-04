#pragma once

#include "types.h"
#include "specialization.h"
#include "data_loader.h"
#include "vfs_manager.h"
#include "rag_dag.h"
#include "agent_orchestrator.h"
#include "meta_debugger.h"
#include "cognitive_load_balancer.h"
#include "long_horizon_planner.h"
#ifndef EMSCRIPTEN_WASM_BUILD
#include "debug_server.h"
#endif
#include "kernel_service_registry.h"
#include "policy_engine.h" // Include PolicyEngine
#include "decoder_compliance_gate.h" // Include DecoderComplianceGate
#include "proto_voice_decoder.h" // Include ProtoVoiceDecoder
#include "evidence.h" // Include Evidence struct
#include "learning_controller.h" // Include LearningController
#include "decoder_contract.h" // Include DecoderContract
#include "response_pipeline.h" // Include ResponsePipeline
#include "entity_extractor.h" // Include EntityExtractor
#include "clinical_analyzer.h" // Include ClinicalObservation
#include "training_scenario.h" // Include TrainingSession
#include "training_analytics.h" // Include TrainingEvent
#ifndef EMSCRIPTEN_WASM_BUILD
#include "groq_client.h"
#endif
#include <chrono>

using Intent = IntentOrchestrator::Intent;

class NativeInferenceEngine {
private:
    std::unique_ptr<StemClassifier> stem_classifier;
    std::unique_ptr<OpticEmbedder> optic_embedder;
    std::unique_ptr<VTAPredictor> vta_predictor;
    std::unique_ptr<OpticTrigeminal> optic_trigeminal;
    std::unique_ptr<ProtoVoiceDecoder> proto_voice_decoder; // Replaces sequence_decoder and advanced_decoder
    
    std::unique_ptr<MathSpecializer> math_specializer;
    std::unique_ptr<LogicSpecializer> logic_specializer;
    std::unique_ptr<CausalitySpecializer> causality_specializer;
    std::unique_ptr<SafetyAttention> safety_attention;
    std::unique_ptr<ContrastiveLearner> contrastive_learner;
    std::unique_ptr<MultiModalFusion> multimodal_fusion;
    std::unique_ptr<IntentOrchestrator> intent_orchestrator;
    
    std::unique_ptr<PolicyEngine> policy_engine; // Phase 0
    std::unique_ptr<LearningController> learning_controller; // Phase 7
#ifndef EMSCRIPTEN_WASM_BUILD
    std::unique_ptr<Groq::GroqClient> groq_client; // External reasoning API
#endif
    
    std::unique_ptr<ResponsePipeline> response_pipeline;
    std::map<std::string, std::map<std::string, std::string>> session_contexts;
    std::string current_session_id;

    VectorStr vocabulary;
    PerformanceMetrics metrics;
    std::chrono::steady_clock::time_point start_time;
    int total_training_records;
    bool initialized;
    
    KernelServiceRegistry kernel; // Kernel service registry
    
    std::vector<EpisodicMemory> episodic_memory;
    std::vector<std::string> reasoning_history;
    std::map<std::string, Embedding> semantic_cache;
    int context_window_size;
    
    std::unique_ptr<VFSManager> vfs_manager;
    std::unique_ptr<RAGDAGSystem> rag_dag_system;
    std::unique_ptr<AgentOrchestrator> agent_orchestrator;
    std::unique_ptr<MetaDebugger> meta_debugger;
    std::unique_ptr<CognitiveLoadBalancer> load_balancer;
    std::unique_ptr<LongHorizonPlanner> horizon_planner;
#ifndef EMSCRIPTEN_WASM_BUILD
    std::unique_ptr<DebugServer> debug_server;
#endif
    
    std::string current_process_id;
    std::map<std::string, GraphNode> knowledge_graph_nodes;
    std::vector<GraphEdge> knowledge_graph_edges;
    
    std::string route_to_specializer(const Intent& intent, const std::string& prompt);
    void update_metrics();
    
    std::vector<std::string> build_reasoning_chain(const std::string& prompt, const Intent& intent);
    std::vector<std::pair<std::string, float>> retrieve_episodic_memory(const Embedding& query, int top_k = 5);
    std::vector<std::pair<std::string, float>> retrieve_episodic_memory_by_session(const std::string& session_id, const Embedding& query, int top_k = 5);
    void store_episodic_memory(const std::string& input, const std::string& output, 
                              const Embedding& context, const std::vector<std::string>& reasoning_steps, float success);
    void store_episodic_memory_with_session(const std::string& session_id, const std::string& input, const std::string& output,
                                           const Embedding& context, const std::map<std::string, std::string>& entities, float success);
    std::string generate_with_reasoning(const std::string& prompt, int max_tokens);
    std::string apply_multimodal_fusion(const std::string& prompt, const std::vector<std::string>& context);
    
public:
    NativeInferenceEngine();
    ~NativeInferenceEngine() = default;
    
    bool initialize();
    bool initialize_with_training_data(const std::vector<TrainingExample>& examples);
    
    InferenceResponse infer(const InferenceRequest& request);
    InferenceResponse native_infer(const std::string& prompt, int max_tokens = 4096);
    InferenceResponse enhanced_infer(const std::string& prompt, const std::string& model, int max_tokens = 4096);
    
    void learn_from_feedback(const std::string& prompt, const std::string& response, bool was_good);
    void learn_from_example(const TrainingExample& example);
    void learn_from_clinical_observation(const ClinicalObservation& obs);
    // "Artificial patient" knowledge: a completed training-scenario session
    // (real synthetic patient + real graded nurse actions + real outcome,
    // never fabricated) becomes one TrainingExample, same learn_from_example
    // path real clinical charting already uses. Call once, from
    // handle_training_end after the session is finalized -- a single
    // deliberate "session complete" event, matching the discipline that
    // fixed handle_observations' earlier learn-on-read bug (see
    // LIMITATIONS.md), not a per-tick or read-triggered call.
    void learn_from_training_session(const TrainingSession& session, const std::vector<TrainingEvent>& events);

    
    void add_training_data(const std::vector<TrainingExample>& examples);
    void build_vocabulary(const std::vector<std::string>& texts);
    
    bool save_state(const std::string& filepath);
    bool load_state(const std::string& filepath);
    
    PerformanceMetrics get_metrics() const { return metrics; }
    VectorStr get_vocabulary() const { return vocabulary; }
    int get_vocab_size() const { return vocabulary.size(); }
    void set_sequence_decoder(std::unique_ptr<SequenceDecoder> sd);
    void set_vocabulary(const VectorStr& vocab);

    int get_graph_node_count() const;
    int get_training_record_count() const { return total_training_records; }
    int get_episodic_memory_size() const { return episodic_memory.size(); }
    void set_context_window(int size) { context_window_size = size; }
    
    SafetyCategory get_safety_category(const std::string& text) const;
    std::vector<std::pair<std::string, float>> find_related_concepts(const std::string& text);
    
    std::shared_ptr<ProcessContext> create_inference_process(const std::string& task_name,
                                                            const std::string& task_description);
    bool transition_inference_process(const std::string& process_id, ProcessState new_state);
    std::vector<DimensionalRetrievalResult> retrieve_with_rag_dag(const std::string& query,
                                                                  const Embedding& query_embedding,
                                                                  const std::string& query_domain = "",
                                                                  const std::string& reference_node_id = "",
                                                                  int top_k = 5);
    std::string get_vfs_process_tree() const;
    std::string get_rag_dag_statistics() const;

    AgentOrchestrator* get_agent_orchestrator() { return agent_orchestrator.get(); }
    MetaDebugger* get_meta_debugger() { return meta_debugger.get(); }
    CognitiveLoadBalancer* get_load_balancer() { return load_balancer.get(); }
    LongHorizonPlanner* get_horizon_planner() { return horizon_planner.get(); }
#ifndef EMSCRIPTEN_WASM_BUILD
    DebugServer* get_debug_server() { return debug_server.get(); }
#endif
    RAGDAGSystem* get_rag_dag() { return rag_dag_system.get(); }
    OpticTrigeminal* get_knowledge_graph() { return optic_trigeminal.get(); }
    Embedding embed_text(const std::string& text) { return optic_embedder->embed(text); }
    
    void set_session_id(const std::string& session_id);
    void set_session_context(const std::string& session_id, const std::string& key, const std::string& value);
    std::string get_session_context(const std::string& session_id, const std::string& key) const;
    
    void start_debug_server(int port = 6969);
    void stop_debug_server();
};
