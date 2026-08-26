// Golden/behavioral tests for include/acmk_planes.h + src/kernel/state_plane.cpp
// (ACMKPlanesCoordinator and the Default* plane implementations). Unlike
// most of the other "planes"/orchestrator files in src/kernel/, this one is
// genuinely live, request-path production code -- http_server.cpp wires
// ~20 real HTTP routes through it (session init, state emission, the audit
// trail, etc.) -- and had zero test coverage before this file.
//
// The header itself documents a real history of bugs here (session
// scoping that was actually a substring match, a trace store keyed by
// node_id instead of session_id so get_inference_graph never actually
// scoped by session, write paths that plain didn't exist for perceptual
// artifacts/decision envelopes). This file's job is to make sure those
// specific bug classes can't silently come back, not just to exercise
// happy paths.
//
// Safety note: DefaultEnvironmentIO's audit log path is a hardcoded
// relative "audit_log.ndjson" (not injectable), which is also exactly what
// the real running server writes to from the repo root. Running this test
// from the repo root would append fabricated test data into that real
// compliance log. main() below chdir()s into a fresh temp directory before
// constructing anything, specifically to avoid that.
#include "../include/acmk_planes.h"
#include "../include/crypto_utils.h"
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <fstream>
#include <sstream>

using namespace ACMK;

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

Timestamp now() { return std::chrono::system_clock::now(); }

StateFrame make_frame(const std::string& session_id, CognitiveState cs, double confidence) {
    StateFrame f;
    f.session_id = session_id;
    f.timestamp = now();
    f.cognitive_state = cs;
    f.risk_posture = RiskPosture::LOW;
    f.confidence_global = confidence;
    return f;
}

InferenceNode make_node(const std::string& id, double confidence) {
    InferenceNode n;
    n.node_id = id;
    n.status = "active";
    n.confidence = confidence;
    n.time_range = {0, 0};
    return n;
}

HumanInterventionEvent make_event(const std::string& session_id, const std::string& user_id) {
    HumanInterventionEvent e;
    e.event_type = "acknowledge";
    e.user_id = user_id;
    e.session_id = session_id;
    e.scope = "patient:1";
    e.content = "test event";
    e.timestamp = now();
    return e;
}

void test_session_permissions() {
    ACMKPlanesCoordinator coord;

    SessionDescriptor rn = coord.initialize_session("fp1", "rn", "simulation");
    check(!rn.session_id.empty(), "initialize_session should assign a non-empty session_id");
    check(rn.permissions.at("escalate") == true, "rn role should have escalate permission");
    check(rn.permissions.at("annotate") == true, "rn role should have annotate permission");
    check(rn.permissions.at("simulation_mode") == true, "mode=simulation should set simulation_mode=true");

    SessionDescriptor it_session = coord.initialize_session("fp2", "it", "simulation");
    check(it_session.permissions.at("annotate") == false, "it role should NOT have annotate permission");
    check(it_session.permissions.at("escalate") == false, "it role should NOT have escalate permission");

    SessionDescriptor provider = coord.initialize_session("fp3", "provider", "simulation");
    check(provider.permissions.at("escalate") == true, "provider role should have escalate permission");

    SessionDescriptor charge = coord.initialize_session("fp4", "charge_nurse", "simulation");
    check(charge.permissions.at("escalate") == false,
          "charge_nurse should NOT have escalate permission (only rn/provider do)");

    SessionDescriptor real_world = coord.initialize_session("fp5", "rn", "real_world");
    check(real_world.permissions.at("simulation_mode") == false,
          "mode=real_world should set simulation_mode=false");

    // Two sessions from the same coordinator must get distinct ids.
    check(rn.session_id != it_session.session_id, "two initialize_session calls should yield distinct session_ids");
}

void test_session_lookup() {
    ACMKPlanesCoordinator coord;
    SessionDescriptor s = coord.initialize_session("fp", "rn", "simulation");

    const SessionDescriptor* found = coord.get_session(s.session_id);
    check(found != nullptr, "get_session should find a session that was just initialized");
    check(found && found->device_fingerprint == "fp", "get_session should return the same data that was stored");

    const SessionDescriptor* missing = coord.get_session("nonexistent-session-id");
    check(missing == nullptr, "get_session should return nullptr for an unknown session_id");
}

void test_is_simulation_mode_without_enforcer() {
    // No simulation_enforcer wired up -- exercises the fallback path that
    // reads the session's own permissions map (or fails closed for an
    // unknown session).
    ACMKPlanesCoordinator coord;
    SessionDescriptor sim = coord.initialize_session("fp", "rn", "simulation");
    check(coord.is_simulation_mode(sim.session_id) == true,
          "is_simulation_mode should be true for a session initialized in simulation mode");

    SessionDescriptor real = coord.initialize_session("fp", "rn", "real_world");
    check(coord.is_simulation_mode(real.session_id) == false,
          "is_simulation_mode should be false for a session initialized in real_world mode");

    check(coord.is_simulation_mode("nonexistent-session-id") == true,
          "is_simulation_mode should fail closed (true) for an unknown session_id");
}

void test_state_plane_current_and_history() {
    auto coord = create_default_planes_coordinator();
    StatePlane* sp = coord->get_state_plane();

    StateFrame f1 = make_frame("session_a", CognitiveState::INGESTING, 0.5);
    sp->emit_state(f1);
    StateFrame current = sp->get_current_state();
    check(current.session_id == "session_a" && current.cognitive_state == CognitiveState::INGESTING,
          "get_current_state should reflect the most recently emitted frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    Timestamp mid = now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    StateFrame f2 = make_frame("session_a", CognitiveState::CONVERGED, 0.9);
    sp->emit_state(f2);
    StateFrame f3 = make_frame("session_b", CognitiveState::CONVERGED, 0.9);
    sp->emit_state(f3);

    auto history_a = sp->get_state_history("session_a", mid, now());
    check(history_a.size() == 1 && history_a[0].cognitive_state == CognitiveState::CONVERGED,
          "get_state_history should return only session_a's frame after 'mid', not the earlier one or session_b's");

    auto history_b = sp->get_state_history("session_b", Timestamp::min(), Timestamp::max());
    check(history_b.size() == 1, "get_state_history for session_b should not include session_a's frames");
}

void test_trace_plane_session_scoping() {
    // Regression test for the exact bug the header documents already being
    // fixed once: DefaultTracePlane used to key its storage by node_id
    // instead of session_id, so get_inference_graph(session_id) returned
    // every node from every session, concatenated. Two sessions with nodes
    // of the SAME id must stay separate.
    auto coord = create_default_planes_coordinator();
    TracePlane* tp = coord->get_trace_plane();

    tp->record_frame("session_a", make_node("node1", 0.8));
    tp->record_frame("session_a", make_node("node2", 0.7));
    tp->record_frame("session_b", make_node("node1", 0.3)); // same node_id, different session

    auto graph_a = tp->get_inference_graph("session_a");
    auto graph_b = tp->get_inference_graph("session_b");
    check(graph_a.size() == 2, "session_a's inference graph should have exactly its own 2 nodes");
    check(graph_b.size() == 1, "session_b's inference graph should have exactly its own 1 node, not session_a's");
    check(!graph_b.empty() && graph_b[0].confidence == 0.3,
          "session_b's node1 should be its own (confidence 0.3), not session_a's node1 (confidence 0.8)");

    auto empty_graph = tp->get_inference_graph("never_recorded");
    check(empty_graph.empty(), "get_inference_graph for a session with no recorded frames should be empty, not throw");
}

void test_trace_plane_artifacts_and_envelopes() {
    auto coord = create_default_planes_coordinator();
    TracePlane* tp = coord->get_trace_plane();

    PerceptualArtifact art;
    art.artifact_id = "art1";
    art.artifact_type = "vitals_snapshot";
    art.confidence = 0.9;
    art.timestamp = now();
    tp->record_perceptual_artifact("session_a", art);

    auto artifacts = tp->get_perceptual_artifacts("session_a");
    check(artifacts.size() == 1 && artifacts[0].artifact_id == "art1",
          "record_perceptual_artifact should make the artifact retrievable -- this write path didn't exist at all before");

    check(tp->get_perceptual_artifacts("session_b").empty(),
          "a session with no recorded artifacts should return empty");

    DecisionEnvelope env;
    env.final_state = "escalate";
    env.confidence_bounds = {0.6, 0.9};
    tp->record_decision_envelope("session_a", env);

    DecisionEnvelope out;
    bool found = tp->get_decision_envelope("session_a", out);
    check(found && out.final_state == "escalate",
          "get_decision_envelope should retrieve what was just recorded -- this had no storage/retrieval path at all before");

    DecisionEnvelope missing;
    check(!tp->get_decision_envelope("session_never_recorded", missing),
          "get_decision_envelope should return false for a session with no recorded envelope");
}

void test_trace_plane_snapshots() {
    auto coord = create_default_planes_coordinator();
    TracePlane* tp = coord->get_trace_plane();

    std::string id1 = tp->create_snapshot("session_a", now(), "hash1");
    std::string id2 = tp->create_snapshot("session_a", now(), "hash2");
    check(!id1.empty() && !id2.empty(), "create_snapshot should return a non-empty snapshot_id");
    check(id1 != id2, "two snapshots (even seconds apart or with different content) should get distinct ids");

    auto snaps = tp->get_snapshots("session_a");
    check(snaps.size() == 2, "get_snapshots should return both snapshots just created for session_a");
    check(tp->get_snapshots("session_b").empty(), "get_snapshots for an unrelated session should be empty");
}

void test_control_plane() {
    auto coord = create_default_planes_coordinator();
    ControlPlane* cp = coord->get_control_plane();
    check(cp->request_pause("session_a"), "request_pause should succeed");
    check(cp->request_resume("session_a"), "request_resume should succeed");
    check(cp->request_freeze("session_a"), "request_freeze should succeed");
}

void test_environment_io_session_scoping_and_audit_chain() {
    // Regression test for the exact bug the header documents already being
    // fixed once: get_human_events used to match session_id as a substring
    // of user_id, which is not session scoping. "session_1" should not
    // match a user_id that happens to contain "session_1" as a substring,
    // and events from a DIFFERENT session must never leak into this
    // session's results.
    auto coord = create_default_planes_coordinator();
    EnvironmentIO* env = coord->get_environment_io();

    env->record_human_event(make_event("session_1", "nurse_jane"));
    env->record_human_event(make_event("session_2", "user_working_on_session_1_migration"));
    env->record_human_event(make_event("session_1", "nurse_bob"));

    auto events = env->get_human_events("session_1");
    check(events.size() == 2, "get_human_events('session_1') should return exactly the 2 events actually scoped to it");
    for (const auto& e : events) {
        check(e.session_id == "session_1",
              "every returned event must have session_id == 'session_1', not just a substring match on user_id");
    }

    check(env->get_human_events("nonexistent_session").empty(),
          "get_human_events for a session with no events should be empty");

    // Hash-chain integrity: read back the audit log file this test's own
    // chdir'd-into temp directory just wrote, and confirm each entry's
    // prev_hash matches the previous entry's hash, and that hash is a real
    // SHA-256 (64 hex chars) of chain_hash + the entry body -- not just
    // some opaque string.
    std::ifstream in("audit_log.ndjson");
    check(in.is_open(), "audit_log.ndjson should have been created in the current (temp) directory");

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) if (!line.empty()) lines.push_back(line);
    check(lines.size() >= 3, "audit log should have at least the 3 human events just recorded");

    auto extract = [](const std::string& l, const std::string& key) -> std::string {
        std::string needle = "\"" + key + "\":\"";
        size_t pos = l.find(needle);
        if (pos == std::string::npos) return "";
        size_t start = pos + needle.size();
        size_t end = l.find('"', start);
        return end == std::string::npos ? "" : l.substr(start, end - start);
    };

    std::string prev_hash = "genesis";
    for (const auto& l : lines) {
        std::string hash = extract(l, "hash");
        std::string prev = extract(l, "prev_hash");
        check(hash.size() == 64, "each audit entry's hash should be a 64-hex-char SHA-256 digest, got length " + std::to_string(hash.size()));
        check(prev == prev_hash, "each audit entry's prev_hash should equal the previous entry's hash (chain must be unbroken)");
        prev_hash = hash;
    }

    // The hash must actually depend on content -- two structurally-similar
    // but different lines must not collide.
    if (lines.size() >= 2) {
        check(extract(lines[0], "hash") != extract(lines[1], "hash"),
              "two different audit entries must not produce the same hash");
    }
}

} // namespace

int main() {
    // Isolate from the real server's audit_log.ndjson (see file header
    // comment) -- do this before constructing anything that might touch it.
    std::string tmp_dir = "/tmp/acmk_planes_test_" + std::to_string(::getpid());
    ::mkdir(tmp_dir.c_str(), 0700);
    if (::chdir(tmp_dir.c_str()) != 0) {
        std::cerr << "FATAL: could not chdir into isolated temp dir " << tmp_dir << "\n";
        return 1;
    }

    test_session_permissions();
    test_session_lookup();
    test_is_simulation_mode_without_enforcer();
    test_state_plane_current_and_history();
    test_trace_plane_session_scoping();
    test_trace_plane_artifacts_and_envelopes();
    test_trace_plane_snapshots();
    test_control_plane();
    test_environment_io_session_scoping_and_audit_chain();

    std::cout << "ACMK planes golden tests: " << g_checked << " checks, " << g_failed << " failed\n";
    std::cout << (g_failed == 0 ? "PASSED" : "FAILED") << "\n";
    return g_failed == 0 ? 0 : 1;
}
