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

- Embeddings are 256-dimensional, built from bag-of-words hashing (no real tokenizer,
  no subword units, no attention). This is a genuine constraint on semantic retrieval
  quality — real term overlap (via BM25) is what carries most of the retrieval signal
  today, not the neural embedding.
- The neural components' "training" paths are simplified online updates, not full
  batch training with a validation loop. Weights are close to their random
  initialization for most components.

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

- No continuous integration, static analysis, or sanitizer configuration exists yet.
- Build system uses `file(GLOB_RECURSE ...)` in places, which is convenient for
  development but not fully reproducible-build-safe.
