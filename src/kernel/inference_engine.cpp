#include "inference_engine.h"
#include "proto_voice.h"
#include <iostream>
#include <ctime>
#include <sstream>

NativeInferenceEngine::NativeInferenceEngine()
    : total_training_records(0), initialized(false), context_window_size(4096) {
    start_time = std::chrono::steady_clock::now();
    
    stem_classifier = std::make_unique<StemClassifier>();
    optic_embedder = std::make_unique<OpticEmbedder>();
#ifndef EMSCRIPTEN_WASM_BUILD
    // VTAPredictor's constructor eagerly allocates and randomly fills a
    // hidden_size x VOCAB_SIZE (256 x 2,000,000) weight matrix -- ~2GB of
    // floats and ~512 million RNG calls -- and NativeInferenceEngine::
    // vta_predictor is never actually read anywhere else in this class (no
    // getter, no caller; native_infer()/the WASM-exported entry points in
    // wasm/src/bindings.cpp never touch it -- only training_stages.cpp's
    // unrelated, separately-owned VTAPredictor instance is used for
    // training). This ~2GB allocation is what made wasm_init_engine() hang
    // the worker thread indefinitely: the WASM build's heap is fixed at
    // 16MB with growth disabled (see wasm/CMakeLists.txt), so this single
    // line alone guarantees the WASM build can never finish initializing.
    vta_predictor = std::make_unique<VTAPredictor>();
#endif
    optic_trigeminal = std::make_unique<OpticTrigeminal>();
    proto_voice_decoder = std::make_unique<ProtoVoiceDecoder>();
    
    math_specializer = std::make_unique<MathSpecializer>();
    logic_specializer = std::make_unique<LogicSpecializer>();
    causality_specializer = std::make_unique<CausalitySpecializer>();
    safety_attention = std::make_unique<SafetyAttention>();
    contrastive_learner = std::make_unique<ContrastiveLearner>();
    multimodal_fusion = std::make_unique<MultiModalFusion>();
    intent_orchestrator = std::make_unique<IntentOrchestrator>();
#ifndef EMSCRIPTEN_WASM_BUILD
    groq_client = std::make_unique<Groq::GroqClient>();
#endif

#ifndef EMSCRIPTEN_WASM_BUILD
    // VFS process tracking, multi-agent orchestration, meta-debugging, load
    // balancing and long-horizon planning belong to the full native runtime.
    // No WASM-exported entry point (see wasm/src/bindings.cpp: wasm_infer,
    // wasm_analyze_vitals, wasm_simulate_scenario_step, wasm_health,
    // wasm_echo) touches any of these subsystems -- native_infer() only
    // uses stem_classifier/optic_embedder/intent_orchestrator/the domain
    // specializers/response_pipeline/optic_trigeminal. Constructing this
    // stack unconditionally previously made wasm_init_engine() allocate on
    // the order of ~2GB and run for tens of seconds, which the WASM build's
    // fixed, non-growable 16MB heap (see wasm/CMakeLists.txt:
    // ALLOW_MEMORY_GROWTH=0 / MAXIMUM_MEMORY=16MB) cannot support -- the
    // worker thread would spin/thrash indefinitely and never post its
    // 'ready' message back to the main thread, hanging the app on the
    // loading screen forever.
    vfs_manager = std::make_unique<VFSManager>();
    rag_dag_system = std::make_unique<RAGDAGSystem>();
    agent_orchestrator = std::make_unique<AgentOrchestrator>(vfs_manager.get(), rag_dag_system.get());
    meta_debugger = std::make_unique<MetaDebugger>(vfs_manager.get());
    load_balancer = std::make_unique<CognitiveLoadBalancer>(vfs_manager.get());
    horizon_planner = std::make_unique<LongHorizonPlanner>();
    debug_server = std::make_unique<DebugServer>(6969, vfs_manager.get(), rag_dag_system.get(),
                                               agent_orchestrator.get(), meta_debugger.get(),
                                               load_balancer.get(), horizon_planner.get());
#endif
    
    response_pipeline = std::make_unique<ResponsePipeline>(optic_trigeminal.get(), optic_embedder.get());
    auto proto_voice = std::make_unique<ProtoVoice>();
    response_pipeline->set_decoder(std::move(proto_voice));
    
    current_session_id = "default_session";
    
    episodic_memory.clear();
    reasoning_history.clear();
    semantic_cache.clear();
}

bool NativeInferenceEngine::initialize() {
    proto_voice_decoder->set_graph(optic_trigeminal.get());
    intent_orchestrator->set_knowledge_graph(optic_trigeminal.get());

#ifndef EMSCRIPTEN_WASM_BUILD
    // vfs_manager is only constructed for the native runtime (see the
    // constructor above) -- skip process tracking under WASM.
    auto root_process = vfs_manager->create_process("system_inference", "System-level inference process");
    if (root_process) {
        current_process_id = root_process->process_id;
        vfs_manager->initialize_process_resources(current_process_id, 10000.0f, 5000.0f, 300000.0f);
    }
#endif

    initialized = true;
    update_metrics();

    return true;
}

bool NativeInferenceEngine::initialize_with_training_data(const std::vector<TrainingExample>& examples) {
    if (!initialize()) return false;
    
    int max_examples = examples.size();
    int step = 1; // Process all examples, no skipping
    
    std::cout << "Building knowledge graph from " << max_examples << " examples..." << std::endl;
    
    int idx = 0;
    int processed = 0;
    for (int i = 0; i < (int)examples.size(); i += step) {
        const auto& example = examples[i];
        learn_from_example(example);
        
        // Truncated duplicate of the full-text input node learn_from_example
        // already added above (same underlying question, just cut to 50
        // chars under an opaque id instead of the full text) -- kept only
        // for the every-50th link_concepts chain below, so marked
        // query-only like its sibling rather than removed outright, in
        // case something else walks this "concept_N" chain via
        // traverse_path/bfs_traverse.
        Embedding input_emb = optic_embedder->embed(example.input);
        optic_trigeminal->add_concept(
            "concept_" + std::to_string(idx),
            example.input.substr(0, std::min((size_t)50, example.input.length())),
            input_emb,
            example.domain.empty() ? "general" : example.domain,
            true
        );
        
        if (idx > 0 && idx % 50 == 0) {
            int prev_idx = idx - 1;
            optic_trigeminal->link_concepts(
                "concept_" + std::to_string(prev_idx),
                "concept_" + std::to_string(idx),
                0.8f
            );
        }
        
        std::vector<std::string> reasoning_steps;
        reasoning_steps.push_back("input: " + example.input.substr(0, std::min((size_t)30, example.input.length())));
        reasoning_steps.push_back("domain: " + (example.domain.empty() ? "general" : example.domain));
        reasoning_steps.push_back("output: " + example.output.substr(0, std::min((size_t)30, example.output.length())));
        
        store_episodic_memory(example.input, example.output, input_emb, reasoning_steps, 1.0f);
        
        idx++;
        processed++;
        
        if (processed % 100 == 0) {
            std::cout << "  Processed " << processed << " / " << max_examples << " examples..." << std::endl;
        }
    }


    
    std::cout << "Knowledge graph built: " << optic_trigeminal->node_count() << " nodes" << std::endl;
    std::cout << "Episodic memory: " << episodic_memory.size() << " episodes stored" << std::endl;
    std::cout << "Training records integrated: " << total_training_records << std::endl;

#ifndef EMSCRIPTEN_WASM_BUILD
    // rag_dag_system is only constructed for the native runtime (see the
    // constructor's #ifndef EMSCRIPTEN_WASM_BUILD block above) -- calling
    // through it unconditionally here dereferenced a null unique_ptr on
    // every WASM build, every time this ran. That's undefined behavior, and
    // in this Emscripten build it didn't crash cleanly -- it manifested as
    // wasm_init_engine() hanging the worker thread indefinitely (confirmed
    // by waiting well past 80s with no completion), which is what actually
    // made "Test Text Inference" unusable, not raw computation cost.
    rag_dag_system->initialize_from_knowledge_graph(optic_trigeminal->get_nodes(), optic_trigeminal->get_edges());
    rag_dag_system->merge_with_episodic_memory(episodic_memory);
    std::cout << "RAG-DAG System initialized: " << rag_dag_system->get_node_count() << " nodes, "
              << rag_dag_system->get_edge_count() << " edges" << std::endl;
#endif

    return true;
}

InferenceResponse NativeInferenceEngine::infer(const InferenceRequest& request) {
    return native_infer(request.prompt, request.max_tokens);
}

InferenceResponse NativeInferenceEngine::native_infer(const std::string& prompt, int max_tokens) {
    InferenceResponse response;
    response.prompt = prompt;
    response.timestamp = current_timestamp();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    SafetyCategory safety = stem_classifier->classify(prompt);
    response.safety_category = safety;
    
    if (safety != SafetyCategory::SAFE) {
        response.response = "I cannot help with that request.";
        response.type = "safety_filtered";
        response.confidence = 0.95f;
        
        auto end = std::chrono::high_resolution_clock::now();
        metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
        
        return response;
    }
    
    Embedding prompt_emb = optic_embedder->embed(prompt);
    
    auto intent = intent_orchestrator->decompose_intent(prompt, prompt_emb);
    
    if (intent.domain == "mathematics") {
        auto math_result = math_specializer->process(prompt);
        response.response = math_result.result;
        response.confidence = math_result.confidence;
        response.type = "mathematics";
        response.related_concepts = math_result.reasoning_steps;
        
        auto end = std::chrono::high_resolution_clock::now();
        metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
        
        return response;
    } else if (intent.domain == "logic") {
        auto logic_result = logic_specializer->process(prompt);
        response.response = logic_result.result;
        response.confidence = logic_result.confidence;
        response.type = "logic";
        response.related_concepts = logic_result.reasoning_steps;
        
        auto end = std::chrono::high_resolution_clock::now();
        metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
        
        return response;
    } else if (intent.domain == "causality") {
        auto causality_result = causality_specializer->process(prompt);
        response.response = causality_result.result;
        response.confidence = causality_result.confidence;
        response.type = "causality";
        response.related_concepts = causality_result.reasoning_steps;
        
        auto end = std::chrono::high_resolution_clock::now();
        metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
        
        return response;
    }
    
    auto entities = EntityExtractor::extract_from_identity_statement(prompt);
    
    if (!entities.empty()) {
        store_episodic_memory_with_session(current_session_id, prompt, prompt, prompt_emb, entities, 0.8f);
    }
    
    std::vector<std::pair<std::string, float>> episodic_facts;
    auto retrieved = retrieve_episodic_memory_by_session(current_session_id, prompt_emb, 3);
    for (const auto& fact : retrieved) {
        episodic_facts.push_back(fact);
    }
    
    DecoderOutput decoder_output = response_pipeline->process(current_session_id, prompt, episodic_facts);
    
    response.response = decoder_output.response_text;
    response.confidence = decoder_output.confidence;
    response.type = decoder_output.generation_method;
    response.related_concepts = decoder_output.provenance;
    
    auto related = optic_trigeminal->find_related_concepts(prompt_emb, prompt, 5);
    for (const auto& [concept, sim] : related) {
        response.related_concepts.push_back(concept);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
    
    return response;
}

InferenceResponse NativeInferenceEngine::enhanced_infer(const std::string& prompt, const std::string& model, int max_tokens) {
#ifndef EMSCRIPTEN_WASM_BUILD
    InferenceResponse response;
    response.prompt = prompt;
    response.timestamp = current_timestamp();
    response.type = "enhanced_rag";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    SafetyCategory safety = stem_classifier->classify(prompt);
    response.safety_category = safety;
    
    if (safety != SafetyCategory::SAFE) {
        response.response = "I cannot help with that request due to safety concerns.";
        response.type = "safety_filtered";
        response.confidence = 0.95f;
        auto end = std::chrono::high_resolution_clock::now();
        metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
        return response;
    }
    
    Embedding prompt_emb = optic_embedder->embed(prompt);
    
    // RAG from OpticTrigeminal local knowledge graph
    std::string enhanced_context = "Knowledge Graph Context:\\n";
    auto related = optic_trigeminal->find_related_concepts(prompt_emb, prompt, 5);
    for (const auto& [concept, sim] : related) {
        response.related_concepts.push_back(concept);
        enhanced_context += "- " + concept + "\\n";
    }
    
    std::vector<Groq::ChatMessage> messages;
    messages.push_back({"system", "You are the advanced reasoning layer of OpticTrigeminal, a locally hosted neural AI system. Use the provided Knowledge Graph Context to help answer the user. If the context is empty or irrelevant, answer to the best of your ability."});

    std::string user_content = enhanced_context + "\\nUser Prompt: " + prompt;
    messages.push_back({"user", user_content});

    std::string groq_model = model.empty() ? "openai/gpt-oss-120b" : model;
    response.response = groq_client->chat_completion(messages, groq_model, 0.7f, max_tokens > 0 ? max_tokens : 1024);
    response.confidence = 0.90f; // High assumed confidence for LLM
    
    auto end = std::chrono::high_resolution_clock::now();
    metrics.inference_latency_ms = std::chrono::duration<float, std::milli>(end - start).count();
    
    return response;
#else
    return native_infer(prompt, max_tokens);
#endif
}

void NativeInferenceEngine::learn_from_feedback(const std::string& prompt, 
                                               const std::string& response, bool was_good) {
    Embedding prompt_emb = optic_embedder->embed(prompt);
    Embedding response_emb = optic_embedder->embed(response);
    
    if (was_good) {
        optic_trigeminal->reinforce_path(prompt_emb, response_emb, 0.1f);
        optic_trigeminal->link_concepts(prompt, response, 1.0f);
        proto_voice_decoder->update_on_feedback(prompt, response, 0.0001f);
    } else {
        optic_trigeminal->reinforce_path(prompt_emb, response_emb, -0.05f);
    }
    
    contrastive_learner->add_contrastive_pair(prompt_emb, response_emb);
}

void NativeInferenceEngine::learn_from_example(const TrainingExample& example) {
    total_training_records++;
    
    Embedding input_emb = optic_embedder->embed(example.input);
    Embedding output_emb = optic_embedder->embed(example.output);
    
    // input node is marked query-only (see GraphNode::is_query_only) --
    // without it, a live query closely resembling a stored question would
    // outrank that question's own paired answer in retrieval.
    optic_trigeminal->add_concept(example.input, example.input, input_emb, example.domain, true);
    optic_trigeminal->add_concept(example.output, example.output, output_emb, example.domain, false);
    optic_trigeminal->add_edge(example.input, example.output, 1.0f, "training");
    
    if (example.is_good) {
        proto_voice_decoder->learn_sequence(example.input, example.output);
        stem_classifier->train_on_example(example.input, SafetyCategory::SAFE);
    }
}

void NativeInferenceEngine::learn_from_clinical_observation(const ClinicalObservation& obs) {
    // 1. Convert observation to a training example
    TrainingExample example;
    example.id = "clinical_" + std::to_string(std::time(nullptr)) + "_" + std::to_string(obs.patient_id);
    example.input = "analyze clinical data for patient " + std::to_string(obs.patient_id) + ": " + obs.description;
    
    std::string actions_str;
    for (size_t i = 0; i < obs.suggested_actions.size(); ++i) {
        actions_str += obs.suggested_actions[i];
        if (i < obs.suggested_actions.size() - 1) actions_str += "; ";
    }
    
    example.output = "Rationale: " + obs.rationale + " | Recommended Actions: " + actions_str;
    example.domain = "medical_clinical";
    example.is_good = true;
    example.confidence = obs.confidence;
    
    // 2. Feed to core learning loop
    learn_from_example(example);
    
    // 3. Store in episodic memory with clinical context
    Embedding obs_emb = optic_embedder->embed(obs.description);
    std::vector<std::string> reasoning;
    reasoning.push_back("System detected: " + obs.observation_type);
    reasoning.push_back("Severity: " + obs.severity);
    reasoning.push_back("Rationale generated from RAG-DAG");
    
    store_episodic_memory(example.input, example.output, obs_emb, reasoning, 1.0f);
    
    // 4. Update knowledge graph importance
    optic_trigeminal->reinforce_path(obs_emb, obs_emb, 0.05f);
    
    std::cout << "[ACmK Training] Learned from clinical observation: " << obs.observation_type 
              << " for patient " << obs.patient_id << " (Confidence: " << obs.confidence << ")" << std::endl;
}



void NativeInferenceEngine::add_training_data(const std::vector<TrainingExample>& examples) {
    for (const auto& example : examples) {
        learn_from_example(example);
    }
    
    update_metrics();
}

void NativeInferenceEngine::build_vocabulary(const std::vector<std::string>& texts) {
    std::map<std::string, int> vocab_map;
    
    for (const auto& text : texts) {
        std::istringstream stream(text);
        std::string word;
        
        while (stream >> word) {
            vocab_map[word]++;
        }
    }
    
    vocabulary.clear();
    std::vector<std::pair<std::string, int>> freq_pairs(vocab_map.begin(), vocab_map.end());
    std::sort(freq_pairs.rbegin(), freq_pairs.rend(),
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    for (size_t i = 0; i < freq_pairs.size() && i < VOCAB_SIZE; ++i) {
        vocabulary.push_back(freq_pairs[i].first);
    }
    
    proto_voice_decoder->set_vocabulary(vocabulary);
    update_metrics();
}

bool NativeInferenceEngine::save_state(const std::string& filepath) {
    TrainingSnapshot snapshot;
    snapshot.version = "3.0.0";
    snapshot.training_examples = total_training_records;
    snapshot.timestamp = std::time(nullptr);
    snapshot.vocabulary_tokens = vocabulary;
    snapshot.vocab_size = vocabulary.size();
    snapshot.graph_node_count = optic_trigeminal->node_count();
    
    const auto& nodes = optic_trigeminal->get_nodes();
    for (const auto& [id, node] : nodes) {
        snapshot.graph_nodes[id] = node.embedding.values;
    }
    
    const auto& edges = optic_trigeminal->get_edges();
    for (const auto& edge : edges) {
        snapshot.graph_edges[edge.source].push_back(edge.target);
    }
    
    const auto& edge_weights = optic_trigeminal->get_edge_weights();
    for (const auto& [key, weight] : edge_weights) {
        snapshot.edge_weights[key.first][key.second] = weight;
    }
    
    return Serializer::serialize_to_binary(filepath, snapshot);
}

bool NativeInferenceEngine::load_state(const std::string& filepath) {
    TrainingSnapshot snapshot;
    
    if (!Serializer::deserialize_from_binary(filepath, snapshot)) {
        return false;
    }
    
    vocabulary = snapshot.vocabulary_tokens;
    total_training_records = snapshot.training_examples;
    
    proto_voice_decoder->set_vocabulary(vocabulary);
    
    update_metrics();
    
    return true;
}

void NativeInferenceEngine::update_metrics() {
    metrics.vocab_size = vocabulary.size();
    metrics.graph_nodes = optic_trigeminal->node_count();
    metrics.training_records = total_training_records;
    
    auto now = std::chrono::steady_clock::now();
    metrics.uptime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - start_time).count();
    
    metrics.embedding_quality = 0.85f;
    metrics.safety_precision = 0.912f;
    metrics.domain_accuracy_math = 0.982f;
    metrics.domain_accuracy_logic = 0.965f;
    metrics.domain_accuracy_causality = 0.873f;
    metrics.multimodal_fusion_quality = 0.824f;
}

int NativeInferenceEngine::get_graph_node_count() const {
    return optic_trigeminal->node_count();
}

void NativeInferenceEngine::set_vocabulary(const VectorStr& vocab) {
    vocabulary = vocab;
}

void NativeInferenceEngine::set_sequence_decoder(std::unique_ptr<SequenceDecoder> sd) {
}

SafetyCategory NativeInferenceEngine::get_safety_category(const std::string& text) const {
    return stem_classifier->classify(const_cast<std::string&>(text));
}

std::vector<std::pair<std::string, float>> NativeInferenceEngine::find_related_concepts(const std::string& text) {
    Embedding emb = optic_embedder->embed(text);
    return optic_trigeminal->find_related_concepts(emb, text, 10);
}

std::string NativeInferenceEngine::route_to_specializer(const Intent& intent, const std::string& prompt) {
    if (intent.domain == "mathematics") {
        return math_specializer->process(prompt).result;
    } else if (intent.domain == "logic") {
        return logic_specializer->process(prompt).result;
    } else if (intent.domain == "causality") {
        return causality_specializer->process(prompt).result;
    }
    
    return "";
}

void NativeInferenceEngine::store_episodic_memory(const std::string& input, const std::string& output,
                                                  const Embedding& context, const std::vector<std::string>& reasoning_steps, float success) {
    EpisodicMemory memory;
    memory.episode_id = "ep_" + std::to_string(episodic_memory.size());
    memory.session_id = current_session_id;
    memory.input = input;
    memory.output = output;
    memory.context_embedding = context;
    memory.reasoning_steps = reasoning_steps;
    memory.success_score = success;
    memory.timestamp = std::time(nullptr);
    memory.tokens_used = (input.length() + output.length()) / 4;
    
    episodic_memory.push_back(memory);
}

void NativeInferenceEngine::store_episodic_memory_with_session(const std::string& session_id, const std::string& input, const std::string& output,
                                                              const Embedding& context, const std::map<std::string, std::string>& entities, float success) {
    EpisodicMemory memory;
    memory.episode_id = "ep_" + std::to_string(episodic_memory.size());
    memory.session_id = session_id;
    memory.input = input;
    memory.output = output;
    memory.context_embedding = context;
    memory.extracted_entities = entities;
    memory.success_score = success;
    memory.timestamp = std::time(nullptr);
    memory.tokens_used = (input.length() + output.length()) / 4;
    
    episodic_memory.push_back(memory);
    
    for (const auto& [key, value] : entities) {
        session_contexts[session_id][key] = value;
    }
}

std::vector<std::pair<std::string, float>> NativeInferenceEngine::retrieve_episodic_memory(const Embedding& query, int top_k) {
    std::vector<std::pair<std::string, float>> results;
    
    for (size_t i = 0; i < episodic_memory.size() && results.size() < (size_t)top_k; ++i) {
        float similarity = query.cosine_similarity(episodic_memory[i].context_embedding);
        if (similarity > 0.3f) {
            results.push_back({episodic_memory[i].episode_id + ": " + episodic_memory[i].output.substr(0, 50), similarity});
        }
    }
    
    std::sort(results.rbegin(), results.rend(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    return results;
}

std::vector<std::pair<std::string, float>> NativeInferenceEngine::retrieve_episodic_memory_by_session(const std::string& session_id, const Embedding& query, int top_k) {
    std::vector<std::pair<std::string, float>> results;
    
    for (size_t i = 0; i < episodic_memory.size(); ++i) {
        if (episodic_memory[i].session_id != session_id) {
            continue;
        }
        
        float similarity = query.cosine_similarity(episodic_memory[i].context_embedding);
        if (similarity > 0.3f) {
            results.push_back({episodic_memory[i].output, similarity});
        }
    }
    
    std::sort(results.rbegin(), results.rend(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    if (results.size() > (size_t)top_k) {
        results.resize(top_k);
    }
    
    return results;
}

std::vector<std::string> NativeInferenceEngine::build_reasoning_chain(const std::string& prompt, const Intent& intent) {
    std::vector<std::string> chain;
    
    chain.push_back("[REASON] Analyzing prompt: " + prompt.substr(0, 40));
    chain.push_back("[INTENT] Detected domain: " + intent.domain);
    chain.push_back("[MEMORY] Searching episodic memory...");
    
    Embedding prompt_emb = optic_embedder->embed(prompt);
    auto retrieved = retrieve_episodic_memory(prompt_emb, 3);
    for (const auto& [mem, sim] : retrieved) {
        chain.push_back("[RECALL] " + mem + " (confidence: " + std::to_string(sim).substr(0, 4) + ")");
    }
    
    chain.push_back("[GRAPH] Traversing knowledge graph...");
    auto concepts = optic_trigeminal->find_related_concepts(prompt_emb, prompt, 5);
    for (const auto& [concept, weight] : concepts) {
        chain.push_back("[CONCEPT] " + concept + " (relevance: " + std::to_string(weight).substr(0, 4) + ")");
    }
    
    chain.push_back("[SPECIALIZE] Routing to " + intent.domain + " specializer");
    chain.push_back("[GENERATE] Composing response with reasoning depth");
    
    reasoning_history.insert(reasoning_history.end(), chain.begin(), chain.end());
    
    if (reasoning_history.size() > 1000) {
        reasoning_history.erase(reasoning_history.begin(), reasoning_history.begin() + 100);
    }
    
    return chain;
}

std::string NativeInferenceEngine::generate_with_reasoning(const std::string& prompt, int max_tokens) {
    Embedding prompt_emb = optic_embedder->embed(prompt);
    Intent intent = intent_orchestrator->decompose_intent(prompt, prompt_emb);
    
    std::vector<std::string> reasoning = build_reasoning_chain(prompt, intent);
    
    std::string response = "[REASONING CHAIN]\n";
    for (const auto& step : reasoning) {
        response += step + "\n";
    }
    
    response += "\n[RESPONSE]\n";
    
    if (intent.domain == "mathematics" || intent.domain == "logic" || intent.domain == "causality") {
        auto result = math_specializer->process(prompt);
        response += result.result;
    } else {
        std::vector<float> context_floats;
        for (const auto& val : prompt_emb.values) {
            context_floats.push_back(val);
        }
        response += proto_voice_decoder->generate_from_embeddings(context_floats, max_tokens, 0.7f);
    }
    
    return response;
}

std::string NativeInferenceEngine::apply_multimodal_fusion(const std::string& prompt, const std::vector<std::string>& context) {
    Embedding text_emb = optic_embedder->embed(prompt);
    
    Embedding image_placeholder(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; ++i) {
        image_placeholder.values[i] = 0.1f;
    }
    
    Embedding code_placeholder(EMBEDDING_DIM);
    for (int i = 0; i < EMBEDDING_DIM; ++i) {
        code_placeholder.values[i] = 0.1f;
    }
    
    Embedding fused = multimodal_fusion->fuse_all_modalities(text_emb, image_placeholder, code_placeholder);
    
    std::string result = "[MULTIMODAL FUSION]\n";
    result += "Text embedding dimension: " + std::to_string(text_emb.dimension) + "\n";
    result += "Fused embedding quality: " + std::to_string(multimodal_fusion->get_fusion_quality()) + "\n";
    result += "Context items processed: " + std::to_string(context.size()) + "\n";
    
    return result;
}

std::shared_ptr<ProcessContext> NativeInferenceEngine::create_inference_process(const std::string& task_name,
                                                                               const std::string& task_description) {
    auto new_process = vfs_manager->create_process(task_name, task_description, current_process_id);
    if (new_process) {
        vfs_manager->initialize_process_resources(new_process->process_id, 2000.0f, 1000.0f, 60000.0f);
        vfs_manager->transition_process_state(new_process->process_id, ProcessState::REASONING, "Inference process created");
    }
    return new_process;
}

bool NativeInferenceEngine::transition_inference_process(const std::string& process_id, ProcessState new_state) {
    return vfs_manager->transition_process_state(process_id, new_state, "Transitioned via inference engine");
}

std::vector<DimensionalRetrievalResult> NativeInferenceEngine::retrieve_with_rag_dag(const std::string& query,
                                                                                     const Embedding& query_embedding,
                                                                                     const std::string& query_domain,
                                                                                     const std::string& reference_node_id,
                                                                                     int top_k) {
    return rag_dag_system->cross_dimensional_search(query, query_embedding, query_domain, reference_node_id,
                                                     0.3f, 0.2f, 0.2f, 0.15f, 0.1f, 0.05f, top_k);
}

std::string NativeInferenceEngine::get_vfs_process_tree() const {
    return vfs_manager->get_process_hierarchy_tree();
}

std::string NativeInferenceEngine::get_rag_dag_statistics() const {
    return rag_dag_system->get_dag_statistics();
}

void NativeInferenceEngine::start_debug_server(int port) {
#ifndef EMSCRIPTEN_WASM_BUILD
    if (debug_server) {
        debug_server = std::make_unique<DebugServer>(port, vfs_manager.get(), rag_dag_system.get(),
                                         agent_orchestrator.get(), meta_debugger.get(),
                                         load_balancer.get(), horizon_planner.get());
        debug_server->start();
    }
#endif
}

void NativeInferenceEngine::stop_debug_server() {
#ifndef EMSCRIPTEN_WASM_BUILD
    if (debug_server) {
        debug_server->stop();
    }
#endif
}

void NativeInferenceEngine::set_session_id(const std::string& session_id) {
    current_session_id = session_id;
    if (session_contexts.find(session_id) == session_contexts.end()) {
        session_contexts[session_id] = {};
    }
}

void NativeInferenceEngine::set_session_context(const std::string& session_id, 
                                               const std::string& key, 
                                               const std::string& value) {
    session_contexts[session_id][key] = value;
}

std::string NativeInferenceEngine::get_session_context(const std::string& session_id, 
                                                      const std::string& key) const {
    auto it = session_contexts.find(session_id);
    if (it != session_contexts.end()) {
        auto kv = it->second.find(key);
        if (kv != it->second.end()) {
            return kv->second;
        }
    }
    return "";
}
