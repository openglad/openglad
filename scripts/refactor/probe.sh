#!/usr/bin/env bash
# scripts/refactor/probe.sh — batch parity prober for pack-Lua refactor
# candidates (Lua quality plan, stage 0).
#
# Usage:
#   scripts/refactor/probe.sh [options] [candidate.patch]
#
# The candidate is either the patch file argument (applied with `git apply`
# onto a clean tree) or the tree's existing dirty state (no argument). Either
# way the candidate is IMMEDIATELY preserved as a git stash entry (push
# --include-untracked, then re-applied to the tree), so it survives any
# outcome — including a crash mid-probe — and the stash ref is printed up
# front and again in the verdict.
#
# Gates, in order (fail-fast; later gates print as "skip" after a red):
#   build         cmake --build --preset ci-test — also runs the pack-Lua
#                 statement lint and scripts/parity/check_mutation_pins.py,
#                 which are build dependencies
#   parity-off    ./build/ci-test/og_test_parity from the repo root,
#                 coverage recorder disarmed (the byte-parity oracle)
#   parity-armed  the same oracle with OPENGLAD_LUA_COVERAGE armed — proves
#                 the recorder does not perturb the candidate's sim
#   coverage      Lua-only by default and this is a real gate on the Lua
#                 half: the full ci-test ctest suite runs with the recorder
#                 armed and scripts/coverage/coverage_report.py applies the
#                 95% line / 100% function bar to the Lua half (the C++ half
#                 is reported unmeasured in this mode — measuring it needs a
#                 gcov build). Pass --full-coverage for the full-cycle
#                 ci-coverage preset run that builds instrumented, collects,
#                 and gates BOTH halves plus the union — slower, and the
#                 word before any merge to master. --skip-coverage skips the
#                 gate entirely for quick iteration (never for a landing).
#   pins          scripts/parity/check_mutation_pins.py once more, as its
#                 own row (a drifted pin is a silently-toothless canary)
#   budget        with --budget-check: rerun og_test_parity with
#                 OPENGLAD_LUA_INSTRUCTION_REPORT (per-instruction exact
#                 totals) and fail if any scenario's Lua instruction total
#                 regressed more than 10% against
#                 scripts/refactor/baseline/instruction_baseline.json
#
# Outcome:
#   all green  -> tree keeps the candidate applied (stash entry remains as a
#                 backup); exit 0
#   any red    -> tree is auto-reverted to HEAD (tracked files reset, files
#                 the candidate added removed); the candidate LIVES ON in
#                 the printed stash entry — `git stash apply <ref>` brings
#                 it back; exit 1
#
# Options:
#   --budget-check    also run the instruction-budget gate
#   --skip-coverage   skip the coverage gate (iteration speed; never landing)
#   --full-coverage   full-cycle ci-coverage run (both halves + union)
#   --max-regression F  budget-gate threshold (default 0.10)
#
# Logs land under build/probe-logs/<timestamp>/, one file per gate.

set -u -o pipefail

ROOT=$(git rev-parse --show-toplevel) || {
    echo "probe: not inside a git checkout" >&2
    exit 2
}
cd "$ROOT"

BUDGET_CHECK=0
SKIP_COVERAGE=0
FULL_COVERAGE=0
MAX_REGRESSION=0.10
PATCH_FILE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --budget-check)   BUDGET_CHECK=1 ;;
        --skip-coverage)  SKIP_COVERAGE=1 ;;
        --full-coverage)  FULL_COVERAGE=1 ;;
        --max-regression) shift; MAX_REGRESSION="${1:?--max-regression needs a value}" ;;
        -h|--help)        sed -n '2,60p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        -*)               echo "probe: unknown option $1" >&2; exit 2 ;;
        *)                PATCH_FILE="$1" ;;
    esac
    shift
done
if [[ $SKIP_COVERAGE -eq 1 && $FULL_COVERAGE -eq 1 ]]; then
    echo "probe: --skip-coverage and --full-coverage are mutually exclusive" >&2
    exit 2
fi

STAMP=$(date +%Y%m%d-%H%M%S)
LOGDIR="$ROOT/build/probe-logs/$STAMP"
mkdir -p "$LOGDIR"
BASELINE="$ROOT/scripts/refactor/baseline/instruction_baseline.json"

# ---------------------------------------------------------------------------
# Candidate intake: apply the patch (if any), then preserve the dirty state
# as a stash entry and put it straight back in the tree.
# ---------------------------------------------------------------------------

if [[ -n "$PATCH_FILE" ]]; then
    if [[ -n "$(git status --porcelain)" ]]; then
        echo "probe: tree is dirty; a patch-file candidate needs a clean tree" >&2
        echo "probe: (stash or commit your local changes first)" >&2
        exit 2
    fi
    if ! git apply --stat "$PATCH_FILE" >"$LOGDIR/patch.log" 2>&1 ||
       ! git apply "$PATCH_FILE" >>"$LOGDIR/patch.log" 2>&1; then
        echo "probe: git apply failed for $PATCH_FILE (see $LOGDIR/patch.log)" >&2
        exit 2
    fi
    echo "probe: applied candidate patch $PATCH_FILE"
fi

STASH_SHA=""
if [[ -n "$(git status --porcelain)" ]]; then
    git stash push --include-untracked -m "probe candidate $STAMP" \
        >"$LOGDIR/stash.log" 2>&1 || {
        echo "probe: git stash push failed (see $LOGDIR/stash.log)" >&2
        exit 2
    }
    STASH_SHA=$(git rev-parse stash@{0})
    git stash apply "$STASH_SHA" >>"$LOGDIR/stash.log" 2>&1 || {
        echo "probe: could not re-apply the candidate from stash $STASH_SHA" >&2
        echo "probe: the candidate is SAFE in that stash entry; tree left clean" >&2
        exit 2
    }
    echo "probe: candidate preserved as stash $STASH_SHA (git stash list to see it)"
else
    echo "probe: tree is clean — probing HEAD as a no-op candidate (nothing to stash)"
fi

revert_tree() {
    [[ -n "$STASH_SHA" ]] || return 0
    git reset --hard HEAD >/dev/null
    # Remove exactly the files the candidate ADDED: the stash commit's third
    # parent holds the untracked set when there was one.
    if git rev-parse -q --verify "$STASH_SHA^3" >/dev/null 2>&1; then
        git ls-tree -r --name-only "$STASH_SHA^3" | while IFS= read -r f; do
            rm -f -- "$ROOT/$f"
        done
    fi
}

# ---------------------------------------------------------------------------
# Gate machinery
# ---------------------------------------------------------------------------

GATE_NAMES=()
GATE_RESULTS=()
GATE_DETAILS=()
FAILED=0

record() { # name result detail
    GATE_NAMES+=("$1")
    GATE_RESULTS+=("$2")
    GATE_DETAILS+=("$3")
    [[ "$2" == "FAIL" ]] && FAILED=1
}

run_gate() { # name log-basename command...
    local name="$1" log="$LOGDIR/$2.log"
    shift 2
    if [[ $FAILED -eq 1 ]]; then
        record "$name" "skip" "(after earlier red)"
        return
    fi
    local start end rc
    start=$(date +%s)
    if "$@" >"$log" 2>&1; then rc=0; else rc=$?; fi
    end=$(date +%s)
    if [[ $rc -eq 0 ]]; then
        record "$name" "PASS" "$((end - start))s"
    else
        record "$name" "FAIL" "$((end - start))s, exit $rc — $log"
        echo "---- $name FAILED (last 15 lines of $log) ----"
        tail -15 "$log"
        echo "----"
    fi
}

parity_pass_count() { # log file -> e.g. "188/188"
    local log="$1" passed total
    passed=$(grep -oE '\[  PASSED  \] [0-9]+ tests' "$log" | grep -oE '[0-9]+' | head -1)
    total=$(grep -oE '\[==========\] [0-9]+ tests from' "$log" | grep -oE '[0-9]+' | head -1)
    [[ -n "$passed" && -n "$total" ]] && echo " $passed/$total" || echo ""
}

gate_build() {
    if [[ ! -f "$ROOT/build/ci-test/CMakeCache.txt" ]]; then
        cmake --preset ci-test || return 1
    fi
    cmake --build --preset ci-test
}

gate_coverage_lua_only() {
    local covdir="$ROOT/build/ci-test/probe-luacov"
    rm -rf "$covdir" && mkdir -p "$covdir" || return 1
    OPENGLAD_LUA_COVERAGE="$covdir" ctest --preset ci-test --output-on-failure || return 1
    python3 "$ROOT/scripts/coverage/coverage_report.py" \
        --lua-raw-dir "$covdir" \
        --lines-tool "$ROOT/build/ci-test/og_lua_lines" \
        --output-dir "$ROOT/build/ci-test/probe-coverage"
}

gate_coverage_full() {
    cmake --preset ci-coverage || return 1
    cmake --build --preset ci-coverage || return 1
    find "$ROOT/build/ci-coverage" -type f -name '*.gcda' -delete
    cmake --build --preset ci-coverage --target coverage_run || return 1
    cmake --build --preset ci-coverage --target coverage_report
}

gate_budget() {
    if [[ ! -f "$BASELINE" ]]; then
        echo "no committed baseline at $BASELINE" >&2
        echo "capture one first — see scripts/refactor/README.md" >&2
        return 1
    fi
    local raw="$LOGDIR/instructions.tsv"
    rm -f "$raw"
    OPENGLAD_LUA_INSTRUCTION_REPORT="$raw" "$ROOT/build/ci-test/og_test_parity" || return 1
    python3 "$ROOT/scripts/refactor/instruction_budget.py" check \
        --raw "$raw" --baseline "$BASELINE" --max-regression "$MAX_REGRESSION"
}

# ---------------------------------------------------------------------------
# The probe
# ---------------------------------------------------------------------------

run_gate "build" build gate_build

run_gate "parity-off" parity-off env -u OPENGLAD_LUA_COVERAGE -u OPENGLAD_LUA_INSTRUCTION_REPORT \
    "$ROOT/build/ci-test/og_test_parity"
[[ "${GATE_RESULTS[-1]}" == "PASS" ]] &&
    GATE_DETAILS[-1]+="$(parity_pass_count "$LOGDIR/parity-off.log")"

ARMED_DIR=$(mktemp -d)
run_gate "parity-armed" parity-armed env OPENGLAD_LUA_COVERAGE="$ARMED_DIR" \
    -u OPENGLAD_LUA_INSTRUCTION_REPORT "$ROOT/build/ci-test/og_test_parity"
[[ "${GATE_RESULTS[-1]}" == "PASS" ]] &&
    GATE_DETAILS[-1]+="$(parity_pass_count "$LOGDIR/parity-armed.log")"
rm -rf "$ARMED_DIR"

if [[ $SKIP_COVERAGE -eq 1 ]]; then
    record "coverage" "skip" "(--skip-coverage; not valid for a landing)"
elif [[ $FULL_COVERAGE -eq 1 ]]; then
    run_gate "coverage" coverage gate_coverage_full
    [[ "${GATE_RESULTS[-1]}" == "PASS" ]] && GATE_DETAILS[-1]+=" (full: C++ + Lua + union)"
else
    run_gate "coverage" coverage gate_coverage_lua_only
    [[ "${GATE_RESULTS[-1]}" == "PASS" ]] && GATE_DETAILS[-1]+=" (Lua half only; --full-coverage for both)"
fi

run_gate "pins" pins python3 "$ROOT/scripts/parity/check_mutation_pins.py"

if [[ $BUDGET_CHECK -eq 1 ]]; then
    run_gate "budget" budget gate_budget
else
    record "budget" "skip" "(pass --budget-check to enable)"
fi

# ---------------------------------------------------------------------------
# Verdict
# ---------------------------------------------------------------------------

echo
echo "==================== probe verdict ===================="
for i in "${!GATE_NAMES[@]}"; do
    printf '  %-14s %-5s %s\n' "${GATE_NAMES[$i]}" "${GATE_RESULTS[$i]}" "${GATE_DETAILS[$i]}"
done
echo "logs: $LOGDIR"

if [[ $FAILED -eq 1 ]]; then
    echo "-------------------------------------------------------"
    echo "RED: reverting the working tree to HEAD."
    revert_tree
    if [[ -n "$STASH_SHA" ]]; then
        echo "The candidate is preserved as stash commit $STASH_SHA"
        echo "  restore it:   git stash apply $STASH_SHA"
        echo "  inspect it:   git stash show -p $STASH_SHA"
    fi
    echo "======================================================="
    exit 1
fi

echo "-------------------------------------------------------"
echo "GREEN: candidate stays applied in the working tree."
if [[ -n "$STASH_SHA" ]]; then
    echo "(backup stash entry $STASH_SHA remains; drop it once committed)"
fi
echo "======================================================="
exit 0
