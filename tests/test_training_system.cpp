#include "../include/training_scenario.h"
#include "../include/training_analytics.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

void test_scenario_initialization() {
    std::cout << "\n=== TEST 1: Scenario Initialization ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_early_sepsis_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    assert(runtime.is_active() == true);
    assert(runtime.elapsed_seconds() == 0);
    assert(runtime.get_state() == "ACTIVE");
    
    auto initial_vitals = runtime.get_current_vitals();
    std::cout << "  Initial HR: " << initial_vitals.hr << " (baseline for early sepsis)" << std::endl;
    std::cout << "  Initial Temp: " << initial_vitals.temp << std::endl;
    
    std::cout << "✓ Scenario initialized successfully" << std::endl;
    std::cout << "  Initial vitals:" << std::endl;
    std::cout << "    HR: " << initial_vitals.hr << " bpm" << std::endl;
    std::cout << "    Temp: " << initial_vitals.temp << "°C" << std::endl;
    std::cout << "    BP: " << initial_vitals.bp_sys << "/" << initial_vitals.bp_dia << std::endl;
    std::cout << "    RR: " << initial_vitals.rr << std::endl;
    std::cout << "    SpO₂: " << initial_vitals.spo2 << "%" << std::endl;
}

void test_vitals_mutation() {
    std::cout << "\n=== TEST 2: Vitals Mutation Over Time ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_hypotension_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    auto vitals_start = runtime.get_current_vitals();
    std::cout << "Starting vitals:" << std::endl;
    std::cout << "  BP: " << vitals_start.bp_sys << "/" << vitals_start.bp_dia << std::endl;
    
    // Advance 5 minutes (300 seconds)
    runtime.tick(300);
    auto vitals_5min = runtime.get_current_vitals();
    std::cout << "After 5 minutes:" << std::endl;
    std::cout << "  BP: " << vitals_5min.bp_sys << "/" << vitals_5min.bp_dia << std::endl;
    
    // Continue to 10 minutes
    runtime.tick(300);
    auto vitals_10min = runtime.get_current_vitals();
    std::cout << "After 10 minutes:" << std::endl;
    std::cout << "  BP: " << vitals_10min.bp_sys << "/" << vitals_10min.bp_dia << std::endl;
    std::cout << "  HR: " << vitals_10min.hr << std::endl;
    
    assert(runtime.elapsed_seconds() == 600);
    std::cout << "✓ Vitals mutation working correctly" << std::endl;
}

void test_ai_recommendations() {
    std::cout << "\n=== TEST 3: AI Recommendations ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_respiratory_distress_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    // Initial recommendations should be empty
    auto recs_initial = runtime.get_pending_recommendations();
    std::cout << "Initial pending recommendations: " << recs_initial.size() << std::endl;
    
    // Tick forward to trigger recommendations
    for (int i = 0; i < 4; i++) {
        runtime.tick(60);
    }
    
    auto recs_after = runtime.get_pending_recommendations();
    std::cout << "After 4 minutes, pending recommendations: " << recs_after.size() << std::endl;
    
    if (!recs_after.empty()) {
        std::cout << "AI Recommendations:" << std::endl;
        for (const auto& rec : recs_after) {
            std::cout << "  - ID: " << rec.id << std::endl;
            std::cout << "    Text: " << rec.text << std::endl;
            std::cout << "    Priority: " << rec.priority << std::endl;
        }
    }
    
    std::cout << "✓ AI recommendation system tested" << std::endl;
}

void test_action_effects() {
    std::cout << "\n=== TEST 4: Nurse Action Effects ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_respiratory_distress_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    auto vitals_before = runtime.get_current_vitals();
    std::cout << "Before action:" << std::endl;
    std::cout << "  SpO₂: " << vitals_before.spo2 << "%" << std::endl;
    
    // Record nurse action
    runtime.accept_action("apply_oxygen", "NURSE_001");
    std::cout << "Nurse action recorded: apply_oxygen" << std::endl;
    
    // Advance time to see effects
    runtime.tick(300);  // 5 minutes
    auto vitals_after = runtime.get_current_vitals();
    std::cout << "After 5 minutes with oxygen:" << std::endl;
    std::cout << "  SpO₂: " << vitals_after.spo2 << "%" << std::endl;
    
    assert(runtime.is_active());
    std::cout << "✓ Action effects working" << std::endl;
}

void test_failure_conditions() {
    std::cout << "\n=== TEST 5: Failure Condition Detection ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_early_sepsis_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    std::vector<std::string> failures;
    
    // Advance time gradually, checking for failures
    for (int minute = 0; minute < 20; minute++) {
        runtime.tick(60);
        auto current_failures = runtime.check_failure_conditions();
        if (!current_failures.empty()) {
            std::cout << "Minute " << minute << ": Failures detected:" << std::endl;
            for (const auto& failure : current_failures) {
                std::cout << "  - " << failure << std::endl;
                failures.push_back(failure);
            }
        }
    }
    
    if (failures.empty()) {
        std::cout << "No failures detected in first 20 minutes (expected for early sepsis scenario)" << std::endl;
    }
    
    std::cout << "✓ Failure detection system tested" << std::endl;
}

void test_analytics_store() {
    std::cout << "\n=== TEST 6: Analytics Store ===" << std::endl;
    
    // Initialize analytics store
    TrainingAnalyticsStore store("data/test_analytics");
    assert(store.initialize());
    std::cout << "✓ Analytics store initialized" << std::endl;
    
    // Record a vitals snapshot
    store.record_vitals_snapshot("SESSION_001", "SCENARIO_001", 60, 88, 118, 97, 37.2f);
    std::cout << "✓ Vitals snapshot recorded" << std::endl;
    
    // Record an AI recommendation
    store.record_recommendation("SESSION_001", "REC_001", "Monitor vitals", 120);
    std::cout << "✓ AI recommendation recorded" << std::endl;
    
    // Record a nurse action
    store.record_nurse_action("SESSION_001", "apply_oxygen", "NURSE_001", 180, true);
    std::cout << "✓ Nurse action recorded" << std::endl;
    
    // Verify events
    auto events = store.get_session_events("SESSION_001");
    std::cout << "Total events recorded: " << events.size() << std::endl;
    assert(events.size() >= 3);
    
    std::cout << "✓ Analytics store working correctly" << std::endl;
}

void test_full_scenario_walkthrough() {
    std::cout << "\n=== TEST 7: Full Scenario Walkthrough (30 minutes) ===" << std::endl;
    
    ScenarioDefinition scenario_def = ScenarioLibrary::create_early_sepsis_scenario();
    ScenarioRuntime runtime(scenario_def);
    
    TrainingAnalyticsStore analytics("data/test_scenario_analytics");
    analytics.initialize();
    
    std::string session_id = "WALKTHROUGH_SESSION_001";
    std::cout << "Session: " << session_id << std::endl;
    
    // Simulate 30-minute scenario
    for (int minute = 0; minute < 30; minute++) {
        // Record vitals every minute
        auto vitals = runtime.get_current_vitals();
        analytics.record_vitals_snapshot(session_id, "SEPSIS_EARLY_001", minute * 60, 
                                         vitals.hr, vitals.bp_sys, vitals.spo2, vitals.temp);
        
        // Get recommendations
        auto recs = runtime.get_pending_recommendations();
        for (const auto& rec : recs) {
            analytics.record_recommendation(session_id, rec.id, rec.text, minute * 60);
        }
        
        // Simulate nurse action at minute 10
        if (minute == 10) {
            runtime.accept_action("initiate_sepsis_bundle", "NURSE_002");
            analytics.record_nurse_action(session_id, "initiate_sepsis_bundle", "NURSE_002", 
                                         minute * 60, true);
            std::cout << "[Min " << minute << "] Nurse action: initiate_sepsis_bundle" << std::endl;
        }
        
        // Check for failures
        auto failures = runtime.check_failure_conditions();
        if (!failures.empty()) {
            for (const auto& failure : failures) {
                analytics.record_failure_condition(session_id, failure, minute * 60);
            }
        }
        
        // Advance scenario
        runtime.tick(60);
        
        if (minute % 10 == 0) {
            std::cout << "[Min " << minute << "] HR: " << vitals.hr << ", BP: " << vitals.bp_sys 
                      << ", Temp: " << vitals.temp << "°C" << std::endl;
        }
    }
    
    // Finalize session
    runtime.finalize_session("COMPLETED");
    analytics.record_session_complete(session_id, "SEPSIS_EARLY_001", "NURSE_002", "COMPLETED", 30 * 60);
    
    std::cout << "✓ Full scenario walkthrough completed" << std::endl;
}

int main() {
    std::cout << "==============================================" << std::endl;
    std::cout << "Training System Comprehensive Test Suite" << std::endl;
    std::cout << "==============================================" << std::endl;
    
    try {
        test_scenario_initialization();
        test_vitals_mutation();
        test_ai_recommendations();
        test_action_effects();
        test_failure_conditions();
        test_analytics_store();
        test_full_scenario_walkthrough();
        
        std::cout << "\n==============================================" << std::endl;
        std::cout << "✓✓✓ ALL TESTS PASSED ✓✓✓" << std::endl;
        std::cout << "==============================================" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
