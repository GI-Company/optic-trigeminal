# Known Limitations

This project is an active prototype, not a validated production system. This document
tracks what's real, what's simplified, and what's a documented gap — kept up to date as
the codebase changes, rather than left to drift from the README's marketing language.

## Security

- **Password hashing was upgraded to Argon2id on 2026-08-24** (vendored reference
  implementation from `P-H-C/phc-winner-argon2`, OWASP-baseline parameters: m=19456 KiB,
  t=2, p=1). This is the one deliberate exception to the project's zero-external-
  dependency rule — hand-rolling a memory-hard KDF is exactly the kind of thing that's
  dangerous to DIY, so a vetted reference implementation is vendored instead. Accounts
  hashed before this upgrade keep working (a legacy salted-SHA-256 verification path is
  kept for backward compatibility) but are not automatically re-hashed on next login;
  a forced rotation would need a separate, deliberate migration pass.
- Tokens are bearer tokens with a 1-hour expiry, held in memory only (a server restart
  invalidates every active session — by design, not yet a documented tradeoff of
  "should this persist").
- No formal third-party security review or penetration test has been performed.

## AI / Reasoning

- Embeddings are 256-dimensional, built from bag-of-words hashing over a random linear
  projection (no subword units, no attention, no trained semantic model). This is a
  genuine constraint on semantic retrieval quality — real term overlap (via BM25) is
  what carries most of the retrieval signal today, not the neural embedding.
- **As of 2026-08-25**, `OpticEmbedder::embed()` and `update_from_feedback()` tokenize
  the same way `BM25Index` does (lowercase, alphanumeric spans) instead of a raw
  whitespace split on the literal text -- the old tokenization hashed "Sepsis",
  "sepsis", and "sepsis." to three different input-vector buckets, fragmenting one
  semantic word's signal purely from capitalization or adjacent punctuation. Measured
  effect on real near-duplicate text pairs: cosine similarity of the resulting
  embeddings went from 0.36-0.67 to 0.91-1.00. This fixes a real signal-quality bug in
  the *input representation*, not the deeper limitation above -- there's still no
  general stemming or stop-word handling, and the embedding is still an untrained random
  projection, not a semantic model.
- `BM25Index::tokenize_and_count` (shared by BM25 search and, since the above, the
  embedding path too) also canonicalizes a small, hand-picked set of clinical
  adjective/noun word-form pairs to one shared term -- "septic"/"sepsis",
  "hypoxic"/"hypoxemic"/"hypoxemia"/"hypoxia", "tachycardic"/"tachycardia",
  "hypotensive"/"hypotension", "febrile"/"pyrexia"/"fever", etc. (see the table in
  `bm25_index.cpp`). This is deliberately a short, verified list, not algorithmic
  stemming: each pair is the same concept in a different grammatical form, confirmed
  against real training-corpus content (`medical_o1_and_wiki.jsonl` alone has ~110
  "septic"-only and ~140 "sepsis"-only examples that couldn't cross-match before this),
  and negated/antonym forms ("afebrile", "normotensive", "normoxic") are explicitly
  excluded and tested against to make sure they never get merged with their root -- that
  would silently invert a query's meaning, not just miss a match. Only affects which
  documents a search can *find*; never changes the retrieved text itself.
- The neural components' "training" paths are simplified online updates, not full
  batch training with a validation loop. Weights are close to their random
  initialization for most components.
- **Kernel "planes"/orchestrators, audited 2026-08-26** -- `src/kernel/` has a set of
  agentic-sounding files (`long_horizon_planner`, `cognitive_load_balancer`,
  `meta_debugger`, `agent_orchestrator`, `recovery_manager`, `telemetry_collector`,
  `policy_engine`, `decoder_compliance_gate`) whose names imply more than a line-by-line
  read confirms. Findings, evidence-based (call-site grep, not guesswork):
  - `recovery_manager.cpp` and `telemetry_collector.cpp` are **entirely dead code** --
    zero callers anywhere outside their own file. `telemetry_collector.cpp` is also
    worth flagging even though it's unreachable: several of its "metrics" (CPU/memory
    usage, per-component health scores) are generated with `rand()` and presented with
    plausible-looking precision, not measured. If either file is ever wired up later,
    that needs fixing first -- don't assume it reports anything real just because it
    compiles and has a real-looking API.
  - `main.cpp` constructs a second, parallel `PolicyEngine` / `AgentOrchestrator` /
    `MetaDebugger` / `CognitiveLoadBalancer` / `LongHorizonPlanner` stack that only the
    debug server (port 6969) reads from -- it's disconnected from the real inference
    path (`inference_engine.cpp`'s own comment confirms which components it actually
    uses). `CognitiveLoadBalancer::measure_system_load()` does real aggregation math,
    but its inputs (`ProcessContext::allocate_resource`/`release_resource`) are never
    called by real traffic, so that debug endpoint permanently reports 0%/IDLE
    regardless of actual server load.
  - `PolicyEngine::decide()` (the one method with real test coverage, in
    `tests/acmk_integration_test.cpp`) doesn't consult the rule table `add_rule()`
    populates -- it duplicates the default thresholds as hardcoded literals instead, so
    a caller adding a custom rule via `add_rule()` sees zero effect on `decide()`.
    Separately, `evaluate()`'s condition matcher checks for the substring "confidence"/
    "intent" rather than actually comparing values -- latent, since `evaluate()` itself
    has no live caller today.
  - `decoder_compliance_gate.h` is never instantiated anywhere (the `.cpp` is a
    1-line stub) -- `decoder_contract.h` is the abstraction actually in use;
    `decoder_compliance_gate` looks like a superseded duplicate left behind.
  - By contrast, `acmk_planes.cpp`/`state_plane.cpp` (`ACMKPlanesCoordinator` and the
    state/trace/control/environment planes) turned out to be genuinely real, live,
    request-path code -- `http_server.cpp` wires ~20 real HTTP routes through it,
    including a real SHA-256 hash-chained audit log. It had zero test coverage before
    this audit; `tests/acmk_planes_test.cpp` (added 2026-08-26) now covers it,
    including regression tests for two bug classes the header comments document already
    being fixed once (session-scoping that was actually a substring match; a trace
    store keyed by node_id instead of session_id). Writing those tests also found and
    fixed a real one: `DefaultTracePlane::create_snapshot`'s id was
    `session_id + "_" + to_time_t(timestamp)` alone, which collides for two snapshots
    of the same session created within the same second (`to_time_t` is second-grain).
    Harmless today (`replay_frame` is a no-op stub, nothing looks a snapshot up by id
    yet), but fixed anyway with a monotonic counter suffix since "id" implies
    uniqueness and a future real `replay_frame` would otherwise silently replay the
    wrong one of two colliding snapshots.
  - None of this was individually named in this file before 2026-08-26, even though
    the general "no regression coverage" line above already existed -- these are the
    specific, actionable version of that claim.

## Clinical Simulator

- **`ClinicalSimulator` is now ODE-driven, as of 2026-08-24** (`src/clinical/
  ode_physiology.cpp`) — vitals are derived each tick from a small hidden physiological
  state (arterial/venous pressure, circulating volume, SVR, contractility, oxygenation
  efficiency, infection burden), integrated with 4th-order Runge-Kutta, rather than each
  vital independently random-walking toward a target. This is still a teaching-tool-grade
  simplification (a 3-state Windkessel-style model), not a validated physiological
  simulator — it should not be treated as validated against real patient outcomes.
- Real standard clinical scores are computed live: **NEWS2** (full, RCP 2017), **qSOFA**
  (full, Sepsis-3), a **partial SOFA** (respiratory/cardiovascular/CNS sub-scores only —
  coagulation/liver/renal are deliberately not computed, since this simulator has no
  lab-value model for platelets/bilirubin/creatinine, and fabricating those would be
  exactly the kind of invented clinical number this project's standing rule prohibits),
  and **MEWS** (one commonly-used five-parameter variant — several exist in practice,
  this isn't singularly "the" standard the way NEWS2 is). See `include/clinical_scoring.h`.
- Crisis triggering is a hybrid: still a low-probability random roll for onset
  (Respiratory Failure, Sepsis, Hypovolemic Shock), but Sepsis → Septic Shock escalation
  is now driven by a real deteriorating condition (sustained hypotension + rising
  infection burden), not chance.
- Five drug interventions (IV fluids, supplemental O2, a vasopressor, broad-spectrum
  antibiotics, an antipyretic) have real, mechanical, time-limited effects on the
  physiology model via `HTTPServer::handle_action` — illustrative typical-dose defaults,
  not calibrated to any real dosing protocol.
- **Causal Counterfactual Patient Charts (CCPC), as of 2026-08-24** (`PatientFork` in
  `include/clinical_sim.h`, `VitalHistoryPanel.ts`): a nurse/instructor can branch a
  counterfactual from any past snapshot ("what if a vasopressor had started here?") and
  see the projected trajectory overlaid, dashed, on the real one — always clearly labeled
  as a projection, never rendered as if it were observed data, and never mutating the real
  patient. The "causal attribution band" under the live chart names which single hidden
  physiology term or active drug (`dominant_physiology_driver` in `ode_physiology.cpp`)
  most explains the current vitals; it is a transparency layer onto this model's own
  internal state, not a diagnosis and not a claim about a real patient's actual
  pathophysiology — its magnitude scale is normalized against this simulator's own
  parameter bounds, not a clinical severity scale. A "phase portrait" view plots the same
  history as HR vs. systolic BP instead of against time, with a shaded illustrative
  normal-adult reference box (SBP 90-140, HR 60-100) — a rough teaching anchor, not a
  validated clinical cutoff or a NEWS2/qSOFA threshold in its own right. Its axes use
  fixed bounds (40-220 mmHg, 30-200 bpm) chosen to comfortably cover what this model's
  tuned physiology actually produces, not this simulator's absolute hard clamps. A
  "score-contribution ribbon" (`dominant_news2_contributor` in `clinical_scoring.cpp`)
  shows which single NEWS2 parameter is contributing the most points at each point in the
  visible window — a genuinely different signal than the causal attribution band above it
  (that one is the hidden physiology *causing* the vitals; this is which *observable,
  scored* parameter is actually moving the escalation number), and the two can disagree,
  e.g. a patient can be physiologically septic before any single NEWS2 parameter has
  crossed into scoring territory.
- `src/clinical/training_scenario.cpp`'s `ScenarioRuntime` is a separate, independent
  system for instructor-authored scripted scenarios — it doesn't share this simulator's
  limitations or its eventual improvements one-for-one, since its design goal
  (reproducible, authored teaching scenarios) is different from a general-purpose
  patient model.
- This is a teaching tool, not a high-fidelity physiological simulator and not a
  clinical decision-support system. It should not be treated as validated against real
  patient outcomes.

## Compliance

- Documents in this repo referencing Board of Nursing compliance, Epic App Orchard
  submission, or similar formal packages describe aspirational target states, not
  completed external reviews, formal risk analyses, or actual submissions/acceptances.
  Treat them as design intent, not certification.

## Operational

- No continuous integration exists yet -- `scripts/verify_all.sh` (added 2026-08-25) is a
  local stand-in: it runs the native build, the physiology fuzz test, the
  clinical-scoring golden tests, the BM25 index golden tests, an ASan+UBSan sanitizer
  build of all three test targets, the integration suite, and the WASM build (syncing
  `web/public/optic-trigeminal.wasm` forward if it's stale) -- but nothing enforces
  running it before a change ships, the way real CI would, and the sanitizer coverage is
  limited to those three fast test targets, not the whole codebase. It already found one
  real bug: `static_cast<int>` on a NaN float in `ode_physiology.cpp`'s vitals mapping is
  undefined behavior that 20,000 plain fuzz-test iterations passed every time (the final
  clamped output looked fine regardless) -- fixed via `safe_round_to_int`.
- Test coverage is still thin relative to the codebase's surface area: the physiology
  engine has a fuzz test, NEWS2/qSOFA/partial-SOFA/MEWS have golden boundary tests
  (`tests/clinical_scoring_test.cpp`), BM25 (the retrieval system's real, trusted lexical
  signal -- see `include/bm25_index.h`) has its own golden tests
  (`tests/bm25_index_test.cpp`), `ACMKPlanesCoordinator`/the state/trace/control/
  environment planes (real, live, request-path code -- see "Kernel planes/orchestrators"
  under AI/Reasoning above) has its own (`tests/acmk_planes_test.cpp`), and there's a
  general integration suite -- but most of the AI kernel (the neural embedding path
  beyond its tokenization, most of the other "planes"/orchestrator files -- several of
  which turned out to be dead code, see above) has no regression coverage at all, and
  wouldn't need any to stay fully honest, since they're unreachable.
- Build system uses `file(GLOB_RECURSE ...)` in places, which is convenient for
  development but not fully reproducible-build-safe.
