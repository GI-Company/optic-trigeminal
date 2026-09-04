#!/bin/bash
# Runs every independent build/test pipeline this project has and reports
# one pass/fail summary. Exists because the native build (build.sh) and the
# WASM build (wasm/build.sh) are two entirely separate pipelines with no
# cross-check between them -- a source file change that only breaks WASM
# (or leaves web/public/optic-trigeminal.wasm stale relative to a fresh
# build) previously had no signal at all unless someone remembered to run
# wasm/build.sh by hand. That's bitten this project's own history more than
# once (see LIMITATIONS.md / git log for "stale WASM build" fixes) --
# this script is the guard against it recurring silently again.
#
# Usage: ./scripts/verify_all.sh
# Exit code is non-zero if anything failed.

set -uo pipefail
cd "$(dirname "$0")/.."

PASS=()
FAIL=()

section() { echo ""; echo "=== $1 ==="; }

run_step() {
    local name="$1"; shift
    section "$name"
    if "$@"; then
        PASS+=("$name")
    else
        FAIL+=("$name")
        echo "!! $name FAILED"
    fi
}

run_native_build() { ./build.sh; }
run_fuzz_test() { ./build/physiology_fuzz_test; }
run_scoring_golden_tests() { ./build/clinical_scoring_test; }
run_bm25_golden_tests() { ./build/bm25_index_test; }
run_acmk_planes_golden_tests() { ./build/acmk_planes_test; }
run_scenario_ode_golden_tests() { ./build/scenario_ode_physiology_test; }
run_stroke_alert_complication_golden_tests() { ./build/stroke_alert_complication_test; }
run_training_data_pipeline_golden_tests() { ./build/learn_from_training_session_test; }
run_integration_test() { ./build/acmk_integration_test; }

# ASan+UBSan build of the fastest, most adversarial-input-heavy test
# targets (not the ACMK planes or training-data-pipeline tests -- those need
# crypto_utils.cpp's argon2 link dependency (the latter needs essentially
# the whole kernel), and are behavioral/regression suites rather than the
# kind of fuzzed-random-input test sanitizers earn their keep on).
# This isn't redundant with the plain builds above: a fuzz test
# passing its own assertions only proves the *final* clamped output looked
# right, not that nothing undefined happened on the way there. This caught
# a real bug once already (static_cast<int> on a NaN float -- see
# safe_round_to_int in ode_physiology.cpp) that 20,000 plain-build fuzz
# iterations passed every single time despite it. -O1 (not -O3) is
# deliberate: sanitizer stack traces stay accurate, and this only needs to
# run once, not perform.
run_sanitizer_build() {
    local dir
    dir="$(mktemp -d)"
    local cxx="clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude"
    $cxx tests/physiology_fuzz_test.cpp src/clinical/ode_physiology.cpp -o "$dir/physiology_fuzz_test_asan" || return 1
    $cxx tests/clinical_scoring_test.cpp src/clinical/clinical_scoring.cpp -o "$dir/clinical_scoring_test_asan" || return 1
    $cxx tests/bm25_index_test.cpp src/kernel/bm25_index.cpp -o "$dir/bm25_index_test_asan" || return 1
    $cxx tests/scenario_ode_physiology_test.cpp src/clinical/training_scenario.cpp src/clinical/ode_physiology.cpp -o "$dir/scenario_ode_test_asan" || return 1
    $cxx tests/stroke_alert_complication_test.cpp src/clinical/training_scenario.cpp src/clinical/ode_physiology.cpp -o "$dir/stroke_alert_test_asan" || return 1
    "$dir/physiology_fuzz_test_asan" && "$dir/clinical_scoring_test_asan" && "$dir/bm25_index_test_asan" && "$dir/scenario_ode_test_asan" && "$dir/stroke_alert_test_asan"
    local status=$?
    rm -rf "$dir"
    return $status
}

run_wasm_build() {
    if ! command -v emcc &> /dev/null && [ ! -d "emsdk" ]; then
        echo "Emscripten not found and no local emsdk/ -- skipping WASM build."
        echo "(This is a soft skip, not a failure: not every dev environment"
        echo "has Emscripten installed. Run ./wasm/build.sh directly once it"
        echo "is, or install it per wasm/build.sh's own setup step.)"
        return 0
    fi
    ./wasm/build.sh
}

sync_wasm_to_web() {
    local fresh="dist/wasm/optic-trigeminal.wasm"
    local deployed="web/public/optic-trigeminal.wasm"
    if [ ! -f "$fresh" ]; then
        echo "No fresh WASM build to sync (WASM build was skipped above)."
        return 0
    fi

    # A byte-for-byte cmp here would be the wrong check and a source of
    # constant false alarms: two WASM builds from IDENTICAL source still
    # differ by a handful of bytes (LTO/symbol-table ordering isn't
    # deterministic across runs), so "the bytes differ" is not a signal
    # that anything actually changed. The only check that means anything
    # is "is the deployed file the most recent build" -- so just always
    # sync forward, unconditionally, and let the rest of this script (the
    # frontend rebuild + re-embed below) confirm the result still boots and
    # passes every other check. That directly fixes the actual failure mode
    # this project has hit more than once (a WASM-included source file
    # changed and nobody re-ran wasm/build.sh) instead of just detecting it
    # after the fact.
    if cmp -s "$fresh" "$deployed" 2>/dev/null; then
        echo "web/public/optic-trigeminal.wasm already matches (byte-identical)."
        return 0
    fi
    cp "$fresh" "$deployed"
    echo "Synced $fresh -> $deployed."
    echo "Rebuilding frontend + re-embedding into the server binary..."
    (cd web && npm run build) || return 1
    ./build.sh || return 1
    echo "Frontend rebuilt and server binary re-embedded with the fresh WASM."
    return 0
}

run_step "Native build (build.sh)" run_native_build
run_step "Physiology fuzz test (20k iterations)" run_fuzz_test
run_step "Clinical scoring golden tests" run_scoring_golden_tests
run_step "BM25 index golden tests" run_bm25_golden_tests
run_step "ACMK planes golden tests" run_acmk_planes_golden_tests
run_step "Scenario ODE physiology golden tests" run_scenario_ode_golden_tests
run_step "Stroke Alert complication golden tests" run_stroke_alert_complication_golden_tests
run_step "ACmK training-data pipeline golden tests" run_training_data_pipeline_golden_tests
run_step "ASan+UBSan build (physiology + scoring + BM25 + scenario ODE + Stroke Alert tests)" run_sanitizer_build
run_step "ACmK integration test suite" run_integration_test
run_step "WASM build (wasm/build.sh)" run_wasm_build
run_step "Sync fresh WASM into web/public + rebuild server" sync_wasm_to_web

section "Summary"
echo "Passed: ${#PASS[@]}"
for p in "${PASS[@]:-}"; do [ -n "$p" ] && echo "  ✓ $p"; done
echo "Failed: ${#FAIL[@]}"
for f in "${FAIL[@]:-}"; do [ -n "$f" ] && echo "  ✗ $f"; done

if [ "${#FAIL[@]}" -gt 0 ]; then
    exit 1
fi
exit 0
