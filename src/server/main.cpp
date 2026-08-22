#include "http_server.h"
#include "debug_server.h"
#include "vfs_manager.h"
#include "rag_dag.h"
#include "agent_orchestrator.h"
#include "meta_debugger.h"
#include "cognitive_load_balancer.h"
#include "long_horizon_planner.h"
#include "policy_engine.h"
#include "data_pipeline.h"
#include "training_stages.h"
#include "training_orchestrator.h"
#include "checkpoint_persistence.h"
#include "weight_updater.h"
#include "multimodal_handler.h"
#include "auth_manager.h"
#include "cohort_manager.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "OpticTrigeminal v3.0.0 - Artificial Cognition Kernel" << std::endl;
    std::cout << "Native AI Operating Environment" << std::endl;
    std::cout << "==================================================" << std::endl << std::endl;
    
    g_auth_manager = std::make_unique<AuthManager>();
    g_auth_manager->initialize();

    g_cohort_manager = std::make_unique<CohortManager>();
    g_cohort_manager->initialize();

    HTTPServer server(8080);
    
    std::cout << "Initializing system..." << std::endl;
    if (!server.initialize("data")) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }
    
    auto vfs = std::make_unique<VFSManager>();
    auto rag_dag = std::make_unique<RAGDAGSystem>();
    auto orchestrator = std::make_unique<AgentOrchestrator>(vfs.get(), rag_dag.get());
    auto debugger = std::make_unique<MetaDebugger>(vfs.get());
    auto load_balancer = std::make_unique<CognitiveLoadBalancer>(vfs.get());
    auto planner = std::make_unique<LongHorizonPlanner>();
    
    auto policy_engine = std::make_unique<PolicyEngine>();
    auto data_pipeline = std::make_unique<DataPipeline>();
    auto training_controller = std::make_unique<TrainingController>(vfs.get(), data_pipeline.get());
    auto checkpoint_persistence = std::make_unique<CheckpointPersistence>("data/checkpoints");
    auto weight_updater = std::make_unique<NeuralWeightUpdater>();
    auto training_orchestrator = std::make_unique<TrainingOrchestrator>(
        training_controller.get(), 
        data_pipeline.get(), 
        vfs.get()
    );
    auto multimodal_handler = std::make_unique<MultimodalInstructionHandler>();
    
    std::cout << "Starting HTTP server..." << std::endl;
    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Starting Debug server..." << std::endl;
    auto debug_server = std::make_unique<DebugServer>(6969, 
                                                       vfs.get(), 
                                                       rag_dag.get(),
                                                       orchestrator.get(),
                                                       debugger.get(),
                                                       load_balancer.get(),
                                                       planner.get());
    if (!debug_server->start()) {
        std::cerr << "Failed to start debug server" << std::endl;
        return 1;
    }
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Server Status:" << std::endl;
    std::cout << "  HTTP Endpoint: http://localhost:8080" << std::endl;
    std::cout << "  Debug Endpoint: http://localhost:6969" << std::endl;
    std::cout << "  API Version: 3.0.0" << std::endl;
    std::cout << "  Mode: Native Inference with Full System Transparency" << std::endl;
    std::cout << "==================================================" << std::endl;
    std::cout << "\nMain API Endpoints (port 8080):" << std::endl;
    std::cout << "  POST   /api/inference/native/infer" << std::endl;
    std::cout << "  GET    /api/inference/native/status" << std::endl;
    std::cout << "  POST   /api/inference/native/learn" << std::endl;
    std::cout << "  GET    /health" << std::endl;
    std::cout << "\nDebug API Endpoints (port 6969):" << std::endl;
    std::cout << "  GET    /api/vfs/tree          (Process hierarchy)" << std::endl;
    std::cout << "  GET    /api/reasoning/trace   (Reasoning chains)" << std::endl;
    std::cout << "  GET    /api/load              (System metrics)" << std::endl;
    std::cout << "  GET    /api/debug/{pid}       (Failure analysis)" << std::endl;
    std::cout << "  GET    /api/horizon/plan      (Task timeline)" << std::endl;
    std::cout << "  GET    /api/agents/tasks      (Agent coordination)" << std::endl;
    std::cout << "\n==================================================" << std::endl;
    
    auto engine = server.get_engine();
    auto metrics = engine->get_metrics();
    
    std::cout << "\nSystem Metrics:" << std::endl;
    std::cout << "  Vocabulary Size: " << engine->get_vocab_size() << " tokens" << std::endl;
    std::cout << "  Knowledge Graph: " << engine->get_graph_node_count() << " nodes" << std::endl;
    std::cout << "  Training Records: " << engine->get_training_record_count() << std::endl;
    std::cout << "  Episodic Memory: " << engine->get_episodic_memory_size() << " episodes" << std::endl;
    std::cout << "  Safety Precision: " << (metrics.safety_precision * 100) << "%" << std::endl;
    std::cout << "  Math Domain Accuracy: " << (metrics.domain_accuracy_math * 100) << "%" << std::endl;
    std::cout << "  Logic Domain Accuracy: " << (metrics.domain_accuracy_logic * 100) << "%" << std::endl;
    std::cout << "  Causality Domain Accuracy: " << (metrics.domain_accuracy_causality * 100) << "%" << std::endl;
    std::cout << "  Multi-Modal Fusion Quality: " << (metrics.multimodal_fusion_quality * 100) << "%" << std::endl;
    
    std::cout << "\nAdvanced Cognitive Features:" << std::endl;
    std::cout << "  ✓ Extended Context Window (4096 tokens)" << std::endl;
    std::cout << "  ✓ Graph-Based Reasoning with Traversal" << std::endl;
    std::cout << "  ✓ Episodic Memory & Context Retention" << std::endl;
    std::cout << "  ✓ Multi-Modal Fusion (Text/Image/Code)" << std::endl;
    std::cout << "  ✓ Semantic Attention Mechanisms" << std::endl;
    std::cout << "  ✓ Contrastive Learning Integration" << std::endl;
    std::cout << "  ✓ Intent Decomposition & Routing" << std::endl;
    std::cout << "  ✓ Domain-Specific Specializers (Math/Logic/Causality)" << std::endl;
    std::cout << "\nAgentic & Transparency Features:" << std::endl;
    std::cout << "  ✓ Multi-Agent Orchestration (5 roles)" << std::endl;
    std::cout << "  ✓ Automatic Failure Detection & Recovery" << std::endl;
    std::cout << "  ✓ Cognitive Load Balancing (5 levels)" << std::endl;
    std::cout << "  ✓ Long-Horizon Task Planning (4 tiers)" << std::endl;
    std::cout << "  ✓ Complete System Transparency (6 debug endpoints)" << std::endl;
    std::cout << "  ✓ Real-time Process Monitoring" << std::endl;
    
    std::cout << "\nACmK System Modules:" << std::endl;
    std::cout << "  ✓ Policy Engine (constraint enforcement & decision-making)" << std::endl;
    std::cout << "  ✓ Data Pipeline (ingestion, deduplication, feature extraction)" << std::endl;
    std::cout << "  ✓ Training Controller (5-stage deterministic learning)" << std::endl;
    std::cout << "  ✓ Training Orchestrator (automated pipeline coordination)" << std::endl;
    std::cout << "  ✓ Checkpoint Persistence (disk-based model checkpoint storage)" << std::endl;
    std::cout << "  ✓ Weight Updater (live neural component weight injection)" << std::endl;
    std::cout << "  ✓ Multimodal Instruction Handler (audio/video/image/text)" << std::endl;
    std::cout << "  ✓ VFS Manager (versioned storage & proof artifacts)" << std::endl;
    std::cout << "  ✓ Knowledge Graph (64,385 nodes, 31,465+ trained examples)" << std::endl;
    
    training_orchestrator->initialize();
    std::cout << "\nCheckpoint Storage:" << std::endl;
    std::cout << "  Storage Directory: data/checkpoints" << std::endl;
    std::cout << "  Existing Checkpoints: " << checkpoint_persistence->get_checkpoint_count() << std::endl;
    
    std::cout << "\n==================================================" << std::endl;
    std::cout << "Press Ctrl+C to shutdown..." << std::endl;
    std::cout << "==================================================" << std::endl;
    
    while (server.is_running()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    std::cout << "\nShutting down gracefully..." << std::endl;
    server.stop();
    
    return 0;
}
