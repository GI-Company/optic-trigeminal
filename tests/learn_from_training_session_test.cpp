// Golden test for NativeInferenceEngine::learn_from_training_session --
// the "artificial patient" ACmK training-data pipeline (see
// LIMITATIONS.md and include/inference_engine.h's own comment). Verifies
// a completed training session becomes real, retrievable knowledge-graph
// content, grounded entirely in the session's own logged fields.
#include "../include/inference_engine.h"
#include <iostream>
#include <string>

namespace {

int g_checked = 0;
int g_failed = 0;

void check(bool cond, const std::string& what) {
    g_checked++;
    if (!cond) {
        g_failed++;
        std::cerr << "FAIL: " << what << "\n";
    }
}

TrainingSession make_session() {
    TrainingSession session;
    session.session_id = "TEST_SESSION_001";
    session.scenario_id = "HYPOTENSION_001";
    session.nurse_id = "RN_TEST";
    session.nurse_role = "rn";
    session.outcome = "COMPLETED";
    return session;
}

std::vector<TrainingEvent> make_events() {
    std::vector<TrainingEvent> events;

    TrainingEvent start;
    start.event_type = "SESSION_START";
    start.session_id = "TEST_SESSION_001";
    start.event_data = "{\"nurse_id\":\"RN_TEST\",\"age\":62,\"sex\":\"M\",\"diagnosis\":\"Post-op abdominal\","
                        "\"hr\":85,\"rr\":18,\"spo2\":96,\"bp_sys\":115,\"bp_dia\":70,\"temp\":37.2}";
    start.elapsed_seconds = 0;
    events.push_back(start);

    TrainingEvent action;
    action.event_type = "NURSE_ACTION";
    action.session_id = "TEST_SESSION_001";
    action.event_data = "action=apply_iv_fluids nurse_id=RN_TEST timely=true grade=CORRECT delta=0.100000";
    action.elapsed_seconds = 900;
    events.push_back(action);

    TrainingEvent complete;
    complete.event_type = "SESSION_COMPLETE";
    complete.session_id = "TEST_SESSION_001";
    complete.event_data = "outcome=COMPLETED nurse_id=RN_TEST duration=1200 score=85.000000";
    complete.elapsed_seconds = 1200;
    events.push_back(complete);

    return events;
}

void test_adds_graph_nodes() {
    NativeInferenceEngine engine;
    int before = engine.get_knowledge_graph()->node_count();
    engine.learn_from_training_session(make_session(), make_events());
    int after = engine.get_knowledge_graph()->node_count();
    check(after > before, "learn_from_training_session should add new graph nodes, got before=" +
          std::to_string(before) + " after=" + std::to_string(after));
}

void test_nodes_tagged_training_scenario_domain() {
    NativeInferenceEngine engine;
    engine.learn_from_training_session(make_session(), make_events());
    bool found = false;
    for (const auto& [id, node] : engine.get_knowledge_graph()->get_nodes()) {
        if (node.type == "training_scenario") found = true;
    }
    check(found, "at least one new node should be tagged type=\"training_scenario\", "
          "distinguishing it from the general medical_clinical corpus");
}

void test_content_grounded_in_real_fields() {
    // The generated text must actually contain the real logged
    // scenario_id/action/outcome -- not paraphrased or invented.
    NativeInferenceEngine engine;
    engine.learn_from_training_session(make_session(), make_events());
    bool found_scenario_id = false, found_action = false, found_outcome = false;
    for (const auto& [id, node] : engine.get_knowledge_graph()->get_nodes()) {
        if (node.label.find("HYPOTENSION_001") != std::string::npos) found_scenario_id = true;
        if (node.label.find("apply_iv_fluids") != std::string::npos) found_action = true;
        if (node.label.find("COMPLETED") != std::string::npos) found_outcome = true;
    }
    check(found_scenario_id, "generated content should include the real scenario_id");
    check(found_action, "generated content should include the real action name taken");
    check(found_outcome, "generated content should include the real session outcome");
}

void test_retrievable_via_scenario_relevant_query() {
    // BM25's exact-term matching finds this deterministically regardless
    // of the embedder's random init -- a real end-to-end retrievability
    // check, not just "the node exists somewhere in a map".
    NativeInferenceEngine engine;
    engine.learn_from_training_session(make_session(), make_events());

    Embedding q(EMBEDDING_DIM);
    auto results = engine.get_knowledge_graph()->find_related_concepts(q, "HYPOTENSION_001 apply_iv_fluids", 10);
    bool found = false;
    for (const auto& [id, score] : results) {
        if (id.find("HYPOTENSION_001") != std::string::npos || id.find("apply_iv_fluids") != std::string::npos) {
            found = true;
        }
    }
    check(found, "newly-learned session content should be retrievable via a scenario-relevant query");
}

void test_no_events_does_not_crash() {
    // Defensive: an empty event list (e.g. analytics_store_ unavailable)
    // shouldn't crash or add garbage content.
    NativeInferenceEngine engine;
    int before = engine.get_knowledge_graph()->node_count();
    TrainingSession session = make_session();
    engine.learn_from_training_session(session, {});
    int after = engine.get_knowledge_graph()->node_count();
    check(after >= before, "an empty event list should not crash, and should not decrease node_count");
}

} // namespace

int main() {
    test_adds_graph_nodes();
    test_nodes_tagged_training_scenario_domain();
    test_content_grounded_in_real_fields();
    test_retrievable_via_scenario_relevant_query();
    test_no_events_does_not_crash();

    std::cout << "learn_from_training_session golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
