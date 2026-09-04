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
  patient model. **As of 2026-08-26**: `HYPOTENSION_001`, `SEVERE_BLEEDING_001`,
  `ANAPHYLAXIS_001`, `RESPIRATORY_001`, and `DKA_CRISIS_001`
  (`ScenarioDefinition::uses_ode_physiology = true`) now drive their vitals from the real
  ODE engine (`apply_crisis_physiology`/`apply_drug_effects`/`step_physiology`) instead of
  the curve-overlay model the other 3 scenarios still use. Two things had to be worked out,
  not assumed, before the first (Hypotension) migration was safe:
  - `step_physiology`'s standing relaxation-toward-baseline saturates (clamps to full
    effect) at dt >= ~50s for several parameters -- calling it once with Training Mode's
    full 60s-per-tick delta would silently erase that same call's own crisis progression.
    Fixed by sub-stepping in ~1s increments internally.
  - Even sub-stepped, the existing crisis rates (tuned for the ambient simulator's much
    smaller, frequent real-time ticks) would reach the scenario's grading threshold
    within the first tick or two -- ~15x faster than the original curve-authored
    timeline intended. A `kOdeTimeScale` constant (1/15, empirically verified against
    HYPOTENSION_001's own `severe_hypotension` timeline marker, not guessed) scales the
    *effective* dt fed into the same unmodified functions.
  - **Early Sepsis Recognition was investigated for the same migration and found NOT to
    transfer cleanly** -- not a tuning gap, a structural one: it depends on two vitals
    (Temp+BP_sys) driven by two genuinely *independent* physiology state variables
    (`infection_burden` for temp, `circulating_volume`/`systemic_vascular_resistance` for
    BP) that the existing crisis model moves at very different relative rates from each
    other (verified empirically: BP crashes to near-lethal shock roughly 15-20 ticks
    before temperature ever crosses its own grading threshold, at every time-scale tried
    -- no single scale factor reconciles both, since the mismatch is in how each vital's
    own formula is scaled relative to its threshold, not in overall pacing). Cardiac
    Arrest remains on the original curve engine for a different, structural reason (see
    its own bullet below), not a rate mismatch within an existing crisis. Stroke Alert's
    own grading never reads vitals at all
    (`activate_stroke_alert`/`ct_head` are unconditionally correct, `tpa_administration`
    gates purely on elapsed time and CT-before-tPA ordering) -- an ODE migration for it
    would be purely cosmetic, so it stays on the legacy curve engine by design, not as an
    unaddressed gap (see its own complication fix below).
  - **Respiratory Distress was also originally flagged alongside Sepsis as "doesn't
    migrate cleanly," and that conclusion was re-examined and corrected once actually
    tested against this scenario's own grading logic** (not just re-assumed): unlike
    Sepsis, `evaluate_action_correctness`'s RESPIRATORY_001 branch grades `apply_oxygen`
    and `call_respiratory` on ONE vital each, independently -- neither depends on a
    compound condition. The scenario's compound *failure* condition (`SpO2 < 85 AND
    RR > 30`) genuinely doesn't reliably fire under the ODE engine (matching the earlier
    finding), but a probe against the **unmodified legacy curve engine's own scripted
    timeline** found it was already marginal/near-unreachable there too (RR's total
    authored delta is only +6, 20->26, barely brushing 30 even with random jitter) --
    meaning that specific finding was comparing the ODE engine's timing against an
    implicit expectation the *original* scenario's own authored numbers didn't actually
    support either, not a regression this migration introduces. RR isn't an independent
    physiology variable in this engine at all -- it's a formula output of
    `oxygenation_efficiency`/`infection_burden` (see `step_physiology`) -- so both graded
    vitals move together off the same shared state, unlike Sepsis's genuinely independent
    BP/temp mechanisms. Reuses the existing "Respiratory Failure" crisis type and
    `DrugType::Oxygen` unmodified -- zero new engine surface, the smallest of the four
    migrations so far. `call_respiratory`'s own grading has no negative branch at all
    (`CORRECT` if `RR > 25`, else `PARTIALLY_CORRECT`/"reasonable to involve" -- never
    penalized), making this migration's grading-timing risk lower than even Hypotension's.
  - **A real gap affecting every migrated scenario, found while investigating Stroke
    Alert and fixed 2026-08-26**: the legacy engine's `recompute_vitals_with_overlay`
    applies a triggered `FailureCondition`'s `complication_effect` curve (real, non-empty
    data for `HYPOTENSION_001`'s `no_intervention_20min` and both of
    `SEVERE_BLEEDING_001`'s failure conditions), but
    `update_vitals_via_ode_physiology` had no equivalent step at all -- that authored
    effect was silently dropped for any scenario on the ODE engine. Fixed via a new
    `apply_complication_effects_via_ode`, called once at the start of each tick, which
    applies a one-time physiology-state nudge (not a repeating curve -- a curve with its
    own onset/duration doesn't map cleanly onto continuous ODE state the way it does onto
    the legacy engine's baseline+overlay split) the tick after a complication first arms.
    **The first version of this fix (nudging `circulating_volume` for a `BP_sys` delta)
    was verified with a single probe run, looked correct, and was wrong** -- a more
    rigorous windowed-average check (comparing average per-tick decline before vs. after
    the complication fires, to average out `noise()`'s per-tick jitter) showed no real
    effect. Root cause: Hypovolemic Shock's own crisis code floors `circulating_volume` at
    0.35, and once a prolonged crisis has already saturated that floor (which it has, well
    before a 20-minute complication typically fires), `apply_crisis_physiology`'s
    `max(0.35f, ...)` instantly re-floors any nudge that pushes it lower on the very next
    sub-step -- the original single-run probe's apparent step-change turned out to be
    almost entirely a coincidental `noise()` artifact, not the fix actually working.
    Corrected to nudge `arterial_pressure` directly instead (no competing floor from
    `apply_crisis_physiology`), then re-verified with a true paired A/B -- two runs given
    the identical `std::srand()` seed (so `noise()` is byte-for-byte identical in both),
    differing only in whether an action absent from `ode_drug_for_action` (so it has zero
    physiological effect of its own, only populates `action_history_`) was taken early
    enough to keep the complication from ever arming -- confirmed robust across 30
    explicit seeds via a standalone probe, then across `SEVERE_BLEEDING_001`'s own
    complications too, before being written into a golden test the same way.
  - **`STROKE_ALERT_001`'s `hemorrhagic_transform` complication, fixed 2026-08-26**: this
    scenario stays on the legacy engine (see above -- its grading never reads vitals), but
    investigating it surfaced two things. First, its complication mechanism was never
    dead code the way an earlier pass through this file assumed -- `arm_complication` ->
    `recompute_vitals_with_overlay`'s `triggered_failures_` loop is real, live,
    already-working code for any legacy-engine scenario (this is what the ODE-engine gap
    above was missing an equivalent of). Second, the complication's own authored data was
    a coarse proxy the scenario's own comment already flagged as "low confidence": tPA
    given without a CT first produced only `{"HR": +8.0f}`, when this vitals model already
    has a better, clinically-grounded pattern for the exact same mechanism (rising
    intracranial pressure) sitting one scenario over -- `DKA_CRISIS_001`'s
    `cerebral_edema` complication models it as early Cushing's triad (bradycardia + rising
    BP). Corrected `hemorrhagic_transform` to the same `{"HR": -10.0f, "BP_sys": 10.0f}`
    pattern. This mechanism (the legacy engine's complication pipeline generally, not just
    this one scenario's data) had zero existing test coverage anywhere in this codebase
    before `tests/stroke_alert_complication_test.cpp` -- confirmed live via direct API
    calls too (HR 90->~79, bp_sys 145->~153, holding as a stable plateau matching the
    complication's own onset/duration curve).
  - **`SEVERE_BLEEDING_001` migrated the same way, reusing "Hypovolemic Shock" as its
    crisis type** (hemorrhage is volume loss, the same underlying physiology as
    Hypotension) but needed two things Hypotension didn't:
    - Its authored baseline already starts mid-crisis (`bp_sys=88` at t=0, not a healthy
      start that later declines), and its `massive_transfusion`/`emergency_surgery`
      actions grade unconditionally correct or are trivially satisfied by that baseline
      (only `massive_transfusion` has a real vitals gate, `bp_sys<90`) -- there's
      essentially no grading-timing risk here, unlike Hypotension. The constructor now
      activates the crisis immediately for any `uses_ode_physiology` scenario whose first
      timeline event sits at `t_min<=0` with a non-empty delta, since `tick()`'s
      `(minutes_before, minutes_after]` range check structurally can never fire a t=0
      event (`minutes_before` starts at 0 too) -- a pre-existing quirk shared with the
      legacy curve engine, not introduced here.
    - `hemorrhage_control` and `emergency_surgery` are source-control actions -- they stop
      the bleed rather than replace lost volume, so they don't map to a drug dose the way
      `apply_iv_fluids`/`start_vasopressor` do. Modeled instead via a new
      `ode_action_stops_crisis()` that clears `active_crisis_type_`, letting
      `step_physiology`'s own standing relaxation recover the patient once the cause is
      addressed. **A real bug was caught by the pre-implementation timing probe**: the
      existing onset-scan (which auto-activates the crisis when a timeline event is
      crossed) re-armed the crisis on this scenario's later events (`increased_bleeding`
      at t=7, `hemorrhagic_shock` at t=14) even after `hemorrhage_control` had correctly
      stopped it, silently undoing source control a few ticks later -- confirmed live in
      the browser before the fix (bp_sys declining 88->83 after `hemorrhage_control`
      instead of recovering) and fixed after (88->112, holds). Fixed via a one-shot
      `crisis_ever_activated_` latch, decoupled from `active_crisis_type_`'s current
      value, so a later timeline event can never re-trigger a crisis a nurse's action
      already stopped. `massive_transfusion` deliberately does *not* stop the crisis
      (Crystalloid dose=2.0, weaker than the 0.012/s depletion rate) -- transfusion
      without source control is meant to be an uphill battle, matching this scenario's
      own REC-001-before-REC-002 teaching order; `emergency_surgery` stops the crisis
      *and* adds a modest Crystalloid boost (dose=1.0 -- a larger dose plus the recovery
      already unlocked by stopping the crisis overshot into frank hypertension during
      timing-probe verification).
    - The ODE engine's heart-rate formula (`heart_rate_from_state`) drives entirely off
      arterial-pressure error (baroreceptor reflex) -- it has no way to represent
      trauma/pain-driven tachycardia beyond what the seeded pressure implies. Since this
      scenario's authored baseline HR (115) is elevated for reasons beyond pure
      baroreceptor response, the first tick shows a real, visible HR drop toward the
      formula's own baroreceptor-only estimate (observed live: 115 -> 86). This is a
      known display artifact, not a grading bug -- HR isn't part of this scenario's
      `evaluate_action_correctness`/`evaluate_condition` logic at all -- but it's honest
      to name: a future improvement would add a small decaying "sympathetic surge" state
      variable rather than relying on pressure error alone.
    - `randomize_case`'s jitter is shared, flat-percentage code applied identically
      across all 8 scenarios' baselines -- fine for Hypotension's healthy-start baseline,
      but this scenario's `bp_sys=88` sits only 2 points below its own `massive_transfusion`
      grading threshold (90), so jitter measurably (verified: ~32% of 200 seeds) can push
      a session to start on the non-shock side. Pre-existing, identical under the legacy
      curve engine, and not a grading-correctness bug (massive_transfusion still grades
      correctly either way, just against a different starting state) -- not fixed here to
      avoid touching the shared jitter function's calibration for the other 7 scenarios.
  - **`ANAPHYLAXIS_001` needed genuinely new engine surface, not just reuse of existing
    crisis types/drugs** -- the first of the three migrated scenarios where that was true.
    All three of its actions grade on elapsed time or unconditionally (no vitals
    threshold anywhere in `evaluate_action_correctness`/`evaluate_condition` for this
    scenario), so unlike Hypotension this migration carried essentially no
    grading-timing risk; the work here was entirely about physiological plausibility.
    - Added a new `apply_crisis_physiology` crisis type, `"Anaphylaxis"`, combining
      vasodilation (Sepsis's own mechanism, minus the `infection_burden`/
      `circulating_volume` involvement that wouldn't be accurate for a non-infectious
      process) with bronchoconstriction (Respiratory Failure's own
      `oxygenation_efficiency`/`shunt_fraction` rates, reused directly after an initial,
      faster guess was found via timing probe to bottom out SpO2 far more than this
      scenario's own authored -12 delta supports).
    - Added a new `DrugType::Epinephrine` (`clinical_sim.h`) modeling real alpha+beta
      pharmacology as three simultaneous physiology terms (SVR up, oxygenation up, shunt
      down) in one `apply_drug_effects` case -- the first drug in this engine that isn't
      single-mechanism. Its SVR rate was tuned down from an initial guess (0.05f/s) after
      the timing probe showed it, combined with this action's own short 180s authored
      duration, reversed the crisis within 1-2 ticks and then overshot into sustained
      hypertension well after the dose had already worn off (arterial pressure's own
      relax lag catching up to an SVR peak banked early in the dose window) -- confirmed
      live in the browser afterward (bp_sys/spo2 recovering together, 77/91 -> 110/98,
      without the overshoot).
    - The constructor's baseline-seeding (previously only `arterial_pressure`/
      `circulating_volume`, added for Hypotension) is now also extended to
      `oxygenation_efficiency`, since this scenario's authored baseline is hypoxic
      (spo2=88) as well as hypotensive (bp_sys=78) -- without it, SpO2 would have shown
      the same kind of first-tick jump toward a healthy ~98% that Severe Bleeding's HR
      shows (see above), except this one was avoidable, unlike HR's single-mechanism
      formula limitation.
    - Adding a new `DrugType` enum value meant checking every `switch` over it
      (`apply_drug_effects` needed a case or the new drug would silently no-op every
      tick it's active -- this switch has no `default:`, so a missing case fails
      silently, not loudly; `dominant_physiology_driver`'s attribution-band switch
      already had a `default:` and got a case added too, for CCPC-payoff completeness
      even though Training Mode doesn't consume that band yet). `http_server.cpp`'s
      `dashboard_action_to_drug` (the *ambient* simulator's own, separate action
      vocabulary) was deliberately left untouched -- out of scope for a training-scenario
      migration.
  - A pre-existing, unrelated bug was noticed while live-testing this scenario's debrief
    screen (not caused by this migration -- fixed separately, see "Debrief transcript
    score display" below) -- a `PARTIALLY_CORRECT`-graded action's Action Transcript row
    showed the wrong score delta (+10% shown, +2% actually applied).
  - **`DKA_CRISIS_001` needed one new mechanism, much smaller than originally scoped** --
    an earlier pass through this project's own plan assumed DKA would need "a genuinely
    new metabolic ODE subsystem -- glucose, ketones, pH/bicarb, and potassium kinetics."
    Once actually investigated (the same correction pattern as Respiratory Distress and
    Stroke Alert above), its grading turned out to be entirely unconditional or
    ordering-based -- `obtain_labs` unconditional, `iv_fluids` near-unconditional (baseline
    RR is already >24), `insulin_infusion` gated only on "was `iv_fluids` given first," the
    same shape as Stroke Alert's tPA/CT check -- so glucose/ketone/pH/potassium values were
    never actually needed for teaching correctness, only for a faithful *vitals*
    trajectory. The one real gap: Kussmaul breathing (compensatory tachypnea for metabolic
    acidosis) needed a genuinely new mechanism, since it raises RR without lowering SpO2
    (DKA isn't a hypoxia-driven process, unlike Respiratory Failure/Sepsis, the only
    existing RR-raising mechanisms) -- folding it into `oxygenation_efficiency` would have
    wrongly dropped SpO2 too. Added a new `InternalPhysiology::metabolic_acidosis` state
    variable, a `"Diabetic Ketoacidosis"` crisis type (depletes `circulating_volume` at
    roughly half Hypovolemic Shock's rate, matching this scenario's own gentler authored
    BP_sys delta, and raises `metabolic_acidosis`), a new term in `step_physiology`'s RR
    formula, and a new `DrugType::Insulin` (reduces `metabolic_acidosis`; doesn't touch
    volume, matching real pharmacology and this scenario's own fluids-then-insulin
    teaching order). **Two rate bugs were caught by timing probe before either shipped**:
    the crisis's first `metabolic_acidosis` accumulation rate (0.012f/s) sat *below*
    `metabolic_acidosis`'s own standing relax rate (0.02f/s) -- since this scenario starts
    already acidotic (seeded from its authored baseline RR, the same seeding pattern
    Anaphylaxis's SpO2 uses), the seeded value was already above its own equilibrium and
    actually *declined* under an untreated run instead of rising; raised to 0.03f/s.
    Separately, `DrugType::Insulin`'s first reversal rate (0.006f/s) was far too weak
    relative to the crisis's accumulation to produce any visible effect at all within a
    normal playthrough; raised to 0.04f/s (insulin is DKA's definitive fix, not a
    stopgap like Hypotension's deliberately-weaker Crystalloid, matching this scenario's
    own REC-003 framing). Verified live via direct API calls (RR 27->19 after
    `insulin_infusion`, bp_sys 93->102 after `iv_fluids`) -- which also incidentally
    exercised `apply_complication_effects_via_ode` (see above) against a third scenario
    beyond the two it was built and tested against, confirming that fix's generality: the
    session's own pre-existing `cerebral_edema` complication (real Cushing's-triad data,
    fires if fluids are given before labs) applied correctly through the same code path.
  - **Cardiac Arrest stays on the legacy engine for a genuinely structural reason, not a
    scope choice**: `heart_rate_from_state` (the ODE engine's only HR-driving formula)
    clamps its result to a floor of 35 -- this scenario's entire premise, HR=0
    (pulselessness), is literally unrepresentable by the current formula, not just poorly
    calibrated. Its own grading is otherwise the lowest-risk of any scenario investigated
    (`initiate_cpr` correct if `hr==0` else still-positive `PARTIALLY_CORRECT`,
    `defibrillate`/`epinephrine` unconditionally correct -- no negative branch at all,
    which is clinically accurate: any of the three is always appropriate during an active
    arrest), and both its failure conditions are appropriately escalation-only (no
    complication_effect data to fix, unlike Stroke Alert's). Migrating it would need a
    genuinely separate state representation (a discrete pulse/rhythm state distinct from
    the continuous formula, handing off to the standard engine once ROSC is achieved) --
    a real, separate undertaking, not attempted here.
- **Debrief transcript score display, fixed 2026-08-26**
  (`HTTPServer::build_training_report_json`, `src/server/http_server.cpp`): the debrief's
  Action Transcript (`TrainingDebrief.ts`'s per-row `+X%`) was hardcoding each action's
  displayed score to a flat `+0.1`/`-0.05` based on the event log's flattened `timely`
  boolean, discarding the real `ActionEvaluation.score_delta` that `record_nurse_action`
  (`training_analytics.cpp`) had already logged right alongside it (as `delta=...` in the
  same `event_data` string). A `PARTIALLY_CORRECT` action (real delta 0.02) displayed as
  +10%, and `PREMATURE` (real delta -0.02) displayed as -5% -- confirmed both live (curl
  round-trip against `/api/training/action` + `/api/training/end`) before and after. The
  session's own cumulative score was never affected (it always summed the real deltas
  directly, not through this display path) -- only the per-row transcript display was
  wrong. Fixed to parse the real `delta=` field, with a safe fallback to the old flattened
  value for already-persisted events that predate that field (confirmed some do --
  `data/training_analytics/training_events.ndjson` has early hand-authored entries with
  only `action=... nurse_id=... timely=true`, no `grade=`/`delta=` -- `std::stof` on an
  empty string would otherwise throw and crash the debrief/report endpoint for any session
  touching that older data). Verified live for both `PARTIALLY_CORRECT` (now 0.02, was
  0.1) and `CORRECT` (still 0.1, no regression) via direct API calls, plus a full
  `verify_all.sh` pass. No golden test was added for this one -- `build_training_report_json`
  is a private `HTTPServer` method with no existing test harness for the HTTP route layer
  in this codebase (unlike the kernel/clinical logic, which has one throughout), and
  standing one up (heavy dependencies: crypto/argon2, the full data-loading path, the same
  file-isolation concerns `acmk_planes_test.cpp` had to work around) was judged
  disproportionate to a small, mechanical, now-live-verified parsing fix.
- **"Artificial patient" training data, as of 2026-08-26**
  (`NativeInferenceEngine::learn_from_training_session`,
  `src/kernel/inference_engine.cpp`): a completed training-scenario session becomes one
  `TrainingExample` fed into ACmK's real knowledge graph, via the same
  `learn_from_example` path real nurse charting already uses. Grounded entirely in the
  session's own logged `TrainingEvent`s (scenario id, synthetic patient, each graded
  action, the final outcome) -- never a fabricated fact -- and tagged with a distinct
  `domain="training_scenario"` so it stays identifiable as simulation-derived rather than
  general clinical knowledge. Called once, from `handle_training_end`, on session
  completion -- not per-tick or read-triggered, matching the discipline that fixed
  `handle_observations`' earlier learn-on-read graph-growth bug. Does **not** reach
  `RAGDAGSystem` (a one-time snapshot rebuilt only at server startup -- a live-added
  concept has no rebuild trigger to reach it) or the 5-stage `TrainingOrchestrator`/
  `DataPipeline` machinery (fully implemented but never actually invoked with real data
  anywhere in this codebase today, confirmed by grep) -- wiring either of those up is a
  separate, larger undertaking, not attempted here.
- **Training Mode causal-attribution badge, added 2026-08-26** (`ScenarioRuntime::
  get_dominant_driver`, `training_scenario.h`; `/api/training/tick`'s `dominant_driver`
  field, `http_server.cpp`; `TrainingMode.ts`'s per-tick badge): the original 3-scenario
  ODE-migration plan flagged exposing `dominant_physiology_driver` (the ambient
  dashboard's own CCPC attribution-band signal) to Training Mode as a real payoff of the
  migration, not built in that pass. Now surfaced as a single live badge (not a full
  historical band -- Training Mode doesn't keep a per-tick snapshot history the way the
  ambient simulator does), reusing `DRIVER_META`/its color vocabulary directly. Returns
  the `{"baseline", 0}` sentinel for the 3 scenarios still on the legacy engine, same as
  the ambient dashboard shows for "nothing meaningfully deviated," rather than a separate
  not-applicable case the frontend would need its own branch for. Only wired into the
  `/tick` response, not `/start` -- so the badge reads "Near baseline" for the few seconds
  before a session's first tick, not a bug, just a scope choice (the session was already
  underway with real data within one tick either way).
  **A real, meaningful bug was caught by actually looking at the rendered badge, not just
  confirming the API returned a value**: `ANAPHYLAXIS_001` initially showed "Low volume"
  (hypovolemia) as its dominant driver, when this scenario's own crisis mechanism is
  vasodilation and never touches `circulating_volume` at all. Root cause: the
  constructor's generic `circulating_volume` seeding (backing out volume from `bp_sys` so
  `target_pressure` matches the authored starting point, avoiding a first-tick jump toward
  a healthy ~120 -- originally added for Hypotension, where volume genuinely is the
  mechanism) was being applied unconditionally to every `uses_ode_physiology` scenario,
  including ones whose crisis never reads or writes that variable.
  `dominant_physiology_driver`'s "hypovolemia" candidate has no way to know a seeded value
  isn't a real signal, so it read as spuriously dominant. This wasn't just a display
  quirk -- it was actively naming the wrong physiological mechanism to a student. Fixed by
  seeding `systemic_vascular_resistance` instead for `ANAPHYLAXIS_001` specifically (the
  variable its own crisis actually manipulates), preserving the same
  target_pressure-matching goal without the false attribution; confirmed the observable
  `bp_sys` trajectory is unchanged (same math, `120 * volume * SVR * contractility`, either
  variable holding the same 0.66-ish factor) via the existing golden tests, all of which
  still passed. `HYPOTENSION_001`/`SEVERE_BLEEDING_001` (Hypovolemic Shock -- volume loss
  is genuinely their mechanism) and `DKA_CRISIS_001` (osmotic diuresis, same reasoning)
  keep the original volume-based seeding correctly. `RESPIRATORY_001` has the same latent
  gap in principle but its authored baseline (`bp_sys=118`) is close enough to the
  InternalPhysiology default of 120 that the resulting magnitude (~0.03) stays under
  `dominant_physiology_driver`'s own 0.15 "meaningfully deviated" threshold -- confirmed
  via the same reasoning, not just assumed, before leaving it as the generic case. A
  regression test (`test_anaphylaxis_driver_attribution_is_vasodilation_not_hypovolemia`)
  guards this specifically. Verified live end-to-end: the badge correctly showed
  "Vasodilation" at rest and transitioned to "Epinephrine" once the drug's own SVR/
  oxygenation effect became dominant -- the crisis-to-treatment handoff the original CCPC
  work was designed to show.
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
  under AI/Reasoning above) has its own (`tests/acmk_planes_test.cpp`),
  `HYPOTENSION_001`, `SEVERE_BLEEDING_001`, `ANAPHYLAXIS_001`, `RESPIRATORY_001`, and `DKA_CRISIS_001`'s ODE-physiology paths share one file
  (`tests/scenario_ode_physiology_test.cpp`, including 200-seed sweeps of
  `randomize_case`'s own t=0 invariants and a regression test for the crisis-relatching
  bug described above), `STROKE_ALERT_001`'s (legacy-engine) complication mechanism has
  its own (`tests/stroke_alert_complication_test.cpp` -- the first coverage of
  `arm_complication`/`recompute_vitals_with_overlay`'s complication pipeline for any
  scenario), the "artificial patient" training-data pipeline
  has its own (`tests/learn_from_training_session_test.cpp`), and there's a general
  integration suite -- but most of the AI kernel (the neural embedding path beyond its
  tokenization, most of the other "planes"/orchestrator files -- several of which turned
  out to be dead code, see above) has no regression coverage at all, and wouldn't need
  any to stay fully honest, since they're unreachable.
- Build system uses `file(GLOB_RECURSE ...)` in places, which is convenient for
  development but not fully reproducible-build-safe.
