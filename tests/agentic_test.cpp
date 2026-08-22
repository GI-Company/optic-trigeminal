#include "agent_orchestrator.h"
#include "meta_debugger.h"
#include "cognitive_load_balancer.h"
#include "long_horizon_planner.h"
#include <iostream>
#include <thread>
#include <chrono>

void test_agent_orchestration() {
    std::cout << "\n=== Agent Orchestration Test ===" << std::endl;
    
    VFSManager vfs;
    RAGDAGSystem rag_dag;
    AgentOrchestrator orchestrator(&vfs, &rag_dag);
    
    auto planner_task = orchestrator.spawn_agent_task("Plan solution for complex reasoning",
                                                     AgentRole::PLANNER, 2.0f);
    auto executor_task = orchestrator.spawn_agent_task("Execute planned solution",
                                                      AgentRole::EXECUTOR, 1.5f,
                                                      planner_task->task_id);
    auto verifier_task = orchestrator.spawn_agent_task("Verify solution correctness",
                                                      AgentRole::VERIFIER, 1.2f,
                                                      executor_task->task_id);
    
    std::cout << "Created 3-tier agent hierarchy:" << std::endl;
    std::cout << "  Planner (priority: " << planner_task->priority << ")" << std::endl;
    std::cout << "  Executor (priority: " << executor_task->priority << ")" << std::endl;
    std::cout << "  Verifier (priority: " << verifier_task->priority << ")" << std::endl;
    
    auto plan = orchestrator.create_execution_plan("Solve reasoning task", 
                                                  {"semantic_retrieval", "rule_application", "verification"},
                                                  4);
    std::cout << "\nGenerated execution plan with " << plan.size() << " nodes" << std::endl;
    
    orchestrator.execute_plan(plan, planner_task->task_id);
    
    auto active_tasks = orchestrator.get_active_tasks();
    std::cout << "Active agent tasks: " << active_tasks.size() << std::endl;
    
    orchestrator.update_task_state(planner_task->task_id, ProcessState::COMPLETE, "Analysis complete");
    orchestrator.update_task_state(executor_task->task_id, ProcessState::COMPLETE, "Execution complete");
    orchestrator.update_task_state(verifier_task->task_id, ProcessState::COMPLETE, "Verification complete");
    
    std::cout << "Agent hierarchy execution completed successfully" << std::endl;
}

void test_self_debugging() {
    std::cout << "\n=== Self-Debugging Meta-Process Test ===" << std::endl;
    
    VFSManager vfs;
    MetaDebugger debugger(&vfs);
    
    auto failing_process = vfs.create_process("compute_task", "High-load computation that might fail");
    vfs.initialize_process_resources(failing_process->process_id, 100.0f, 50.0f, 1000.0f);
    vfs.transition_process_state(failing_process->process_id, ProcessState::COMPUTING, "Computing...");
    
    failing_process->resources[ResourceType::TOKEN_BUDGET].current_usage = 95.0f;
    
    auto debug_trace = debugger.analyze_process_failure(failing_process->process_id);
    std::cout << "Analyzed failure: " << static_cast<int>(debug_trace.failure_mode) << std::endl;
    
    auto retry_strategy = debugger.create_retry_strategy(debug_trace, 3);
    std::cout << "Generated retry strategy with " << retry_strategy.max_retries << " max retries" << std::endl;
    std::cout << "Recovery confidence: " << (debug_trace.recovery_confidence * 100) << "%" << std::endl;
    
    std::string new_process_id;
    bool retry_success = debugger.execute_retry(failing_process->process_id, retry_strategy, new_process_id);
    std::cout << "Retry execution: " << (retry_success ? "SUCCESS" : "FAILED") << std::endl;
    
    auto report = debugger.generate_diagnostic_report(failing_process->process_id);
    std::cout << "Generated diagnostic report with " << report.size() << " entries" << std::endl;
}

void test_load_balancing() {
    std::cout << "\n=== Cognitive Load Balancing Test ===" << std::endl;
    
    VFSManager vfs;
    CognitiveLoadBalancer balancer(&vfs);
    
    auto task1 = vfs.create_process("primary_task", "Main reasoning task");
    auto task2 = vfs.create_process("secondary_task", "Secondary analysis");
    auto task3 = vfs.create_process("utility_task", "Utility processing");
    
    vfs.initialize_process_resources(task1->process_id, 5000.0f, 2000.0f, 100000.0f);
    vfs.initialize_process_resources(task2->process_id, 3000.0f, 1500.0f, 60000.0f);
    vfs.initialize_process_resources(task3->process_id, 1000.0f, 500.0f, 30000.0f);
    
    balancer.set_process_priority(task1->process_id, 3.0f, LoadLevel::MEDIUM, true);
    balancer.set_process_priority(task2->process_id, 2.0f, LoadLevel::MEDIUM, false);
    balancer.set_process_priority(task3->process_id, 1.0f, LoadLevel::LOW, false);
    
    auto load = balancer.measure_system_load();
    std::cout << "System load level: " << static_cast<int>(load.current_level) << std::endl;
    std::cout << "Active processes: " << load.active_process_count << std::endl;
    
    auto priority_queue = balancer.get_process_priority_queue();
    std::cout << "Priority queue size: " << priority_queue.size() << std::endl;
    
    bool suspended = balancer.suspend_low_priority_process(task3->process_id);
    std::cout << "Suspended low-priority task: " << (suspended ? "YES" : "NO") << std::endl;
    
    load = balancer.measure_system_load();
    std::cout << "Updated suspended process count: " << load.suspended_process_count << std::endl;
    
    bool resumed = balancer.resume_suspended_process(task3->process_id);
    std::cout << "Resumed suspended task: " << (resumed ? "YES" : "NO") << std::endl;
    
    std::cout << balancer.get_load_report();
}

void test_long_horizon_planning() {
    std::cout << "\n=== Long-Horizon Planning Test ===" << std::endl;
    
    LongHorizonPlanner planner;
    
    int64_t now_ms = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
    
    auto immediate_goal = planner.create_horizon_task(
        "Analyze user query", 
        TimeHorizon::IMMEDIATE,
        now_ms + 5000
    );
    
    auto short_term_goal = planner.create_horizon_task(
        "Retrieve relevant knowledge",
        TimeHorizon::SHORT_TERM,
        now_ms + 30000,
        immediate_goal.task_id
    );
    
    auto medium_term_goal = planner.create_horizon_task(
        "Integrate multiple perspectives",
        TimeHorizon::MEDIUM_TERM,
        now_ms + 300000
    );
    
    auto long_term_goal = planner.create_horizon_task(
        "Build long-term understanding",
        TimeHorizon::LONG_TERM,
        now_ms + 3600000
    );
    
    std::cout << "Created multi-horizon task hierarchy:" << std::endl;
    std::cout << "  Immediate: " << immediate_goal.goal << std::endl;
    std::cout << "  Short-term: " << short_term_goal.goal << std::endl;
    std::cout << "  Medium-term: " << medium_term_goal.goal << std::endl;
    std::cout << "  Long-term: " << long_term_goal.goal << std::endl;
    
    planner.mark_milestone(immediate_goal.task_id, "Query analysis milestone");
    planner.mark_milestone(short_term_goal.task_id, "Knowledge retrieval complete");
    planner.mark_milestone(medium_term_goal.task_id, "Integration complete");
    
    auto immediate_tasks = planner.get_tasks_by_horizon(TimeHorizon::IMMEDIATE);
    auto short_term_tasks = planner.get_tasks_by_horizon(TimeHorizon::SHORT_TERM);
    auto medium_term_tasks = planner.get_tasks_by_horizon(TimeHorizon::MEDIUM_TERM);
    auto long_term_tasks = planner.get_tasks_by_horizon(TimeHorizon::LONG_TERM);
    
    std::cout << "\nTask distribution by horizon:" << std::endl;
    std::cout << "  Immediate: " << immediate_tasks.size() << std::endl;
    std::cout << "  Short-term: " << short_term_tasks.size() << std::endl;
    std::cout << "  Medium-term: " << medium_term_tasks.size() << std::endl;
    std::cout << "  Long-term: " << long_term_tasks.size() << std::endl;
    
    planner.update_task_progress(immediate_goal.task_id, 100.0f);
    planner.mark_task_complete(immediate_goal.task_id);
    
    std::cout << "\nOverall progress: " << planner.estimate_total_progress() << "%" << std::endl;
    
    std::cout << planner.get_timeline_visualization();
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "Advanced Agentic Features Test Suite\n";
    std::cout << "==================================================\n";
    
    try {
        test_agent_orchestration();
        test_self_debugging();
        test_load_balancing();
        test_long_horizon_planning();
        
        std::cout << "\n==================================================\n";
        std::cout << "All agentic feature tests completed successfully!\n";
        std::cout << "==================================================\n";
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
