#!/usr/bin/env bash
# Phase 02 mutation canary — for each scenario, apply the spec's
# discriminating_mutation, rebuild og_test_parity / parity_runner_smoke,
# diff per-predicate evaluation pre vs post, and assert at least one
# predicate (or the scenario's gtest pass/fail) flipped. Restore the
# worktree on every exit path.
#
# Usage:
#   run_mutation_canary.sh --scenario <id>
#   run_mutation_canary.sh --all
#   run_mutation_canary.sh --filter <glob>
#
# Modes are mutually exclusive. The script refuses to start with a dirty
# worktree, refuses to mutate files under ../openglad-master/ or
# tests/parity/, and exits non-zero if any scenario records zero flips.

set -euo pipefail

# Suppress .pyc generation so helper imports do not litter the worktree
# with __pycache__ directories that would trip the porcelain check on
# the next run.
export PYTHONDONTWRITEBYTECODE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PRESET="ci-test"
BUILD_DIR="${REPO_ROOT}/build/${PRESET}"
PARITY_BIN="${BUILD_DIR}/og_test_parity"
SMOKE_BIN="${BUILD_DIR}/parity_runner_smoke"
APPLY_MUT="${SCRIPT_DIR}/_apply_mutation.py"
TABLE_HEADER="${REPO_ROOT}/tests/parity/scenario_table.h"
FACTS_JSON="${REPO_ROOT}/tests/parity/scenario_facts_generated.json"

# Mutated paths the EXIT trap must restore. Use repo-relative paths; the
# trap walks this list, runs `git checkout --` on each, then unsets.
declare -a MUTATED_FILES=()

usage() {
    cat >&2 <<'EOF'
usage:
  run_mutation_canary.sh --scenario <id>
  run_mutation_canary.sh --all
  run_mutation_canary.sh --filter <glob>
EOF
}

# Tracked at startup; populated after argv parsing.
TMPDIR_CANARY=""

# Trap restorer — runs on any exit (success, failure, signal). Captures
# the original $? FIRST so subsequent `rm -rf` / `git checkout` calls
# don't mask it. Reads MUTATED_FILES and `git checkout` each entry to
# wipe any leftover mutation, then deletes the canary tmpdir.
restore_mutations() {
    local rc=$?
    if (( ${#MUTATED_FILES[@]} > 0 )); then
        for f in "${MUTATED_FILES[@]}"; do
            [[ -z "${f}" ]] && continue
            git -C "${REPO_ROOT}" checkout -- "${f}" >/dev/null 2>&1 || true
        done
        MUTATED_FILES=()
    fi
    if [[ -n "${TMPDIR_CANARY}" && -d "${TMPDIR_CANARY}" ]]; then
        rm -rf "${TMPDIR_CANARY}" 2>/dev/null || true
    fi
    exit "${rc}"
}
trap restore_mutations EXIT INT TERM

require_clean_worktree() {
    local dirty
    dirty="$(git -C "${REPO_ROOT}" status --porcelain)"
    if [[ -n "${dirty}" ]]; then
        echo "canary: worktree not clean (commit/stash first); refusing to start" >&2
        echo "${dirty}" >&2
        exit 2
    fi
}

# Master-comparable scenario ids emitted by Phase 01's scenario_facts_dump.
# Branch-internal invariant rows deliberately have no discriminating mutation;
# the same exclusion is enforced by lint.parse_scenarios().
all_scenario_ids() {
    python3 - "${FACTS_JSON}" <<'PY'
import json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
for s in data.get("scenarios", []):
    if not s.get("is_branch_internal", False):
        print(s["id"])
PY
}

# Deterministic glob match against the same id set. fnmatch.fnmatchcase
# semantics; empty match set is a hard error.
match_glob() {
    local pattern="$1"
    python3 - "${FACTS_JSON}" "${pattern}" <<'PY'
import fnmatch, json, sys
with open(sys.argv[1]) as f:
    data = json.load(f)
ids = [
    s["id"]
    for s in data.get("scenarios", [])
    if not s.get("is_branch_internal", False)
]
hits = [i for i in ids if fnmatch.fnmatchcase(i, sys.argv[2])]
if not hits:
    sys.stderr.write(f"canary: --filter {sys.argv[2]!r} matched zero scenarios\n")
    sys.exit(3)
for i in hits:
    print(i)
PY
}

# Map scenario id -> (file, line, from, to) by reusing the lint parser.
# Returns one tab-separated line; aborts the canary if the id has no
# valid mutation declaration.
lookup_mutation() {
    local sid="$1"
    PYTHONPATH="${SCRIPT_DIR}" python3 - "${TABLE_HEADER}" "${sid}" <<'PY'
import sys
from pathlib import Path
import lint_scenario_facts as lint

text = Path(sys.argv[1]).read_text(encoding="utf-8")
muts = lint.parse_mutation_constants(text)
rows = lint.parse_scenarios(text)
target = next((r for r in rows if r["id"] == sys.argv[2]), None)
if target is None:
    sys.stderr.write(f"lookup_mutation: unknown scenario {sys.argv[2]}\n")
    sys.exit(4)
tok = target.get("mutation_token", "")
if not tok or tok.startswith("{"):
    sys.stderr.write(f"lookup_mutation: {sys.argv[2]} has default-constructed mutation\n")
    sys.exit(5)
m = muts.get(tok)
if m is None:
    sys.stderr.write(f"lookup_mutation: {sys.argv[2]} references unknown {tok}\n")
    sys.exit(6)
print(f"{m['file']}\t{m['line']}\t{m['from']}\t{m['to']}")
PY
}

# Build targets needed for the canary loop. `--target` accepts multiple
# names so Ninja can schedule them together; the binaries share most TUs
# so the incremental rebuild after a single source edit is one .cpp +
# one .a re-archive + two executable re-links.
rebuild_targets() {
    cmake --build --preset "${PRESET}" \
        --target og_test_parity parity_runner_smoke >/dev/null 2>&1 \
        || { cmake --build --preset "${PRESET}" \
                --target og_test_parity parity_runner_smoke; exit 1; }
}

# Run a single scenario through the smoke runner with --evaluate-facts;
# write the per-fact JSON to ${1}.
capture_eval() {
    local sid="$1"
    local out="$2"
    "${SMOKE_BIN}" --scenario "${sid}" --evaluate-facts --out "${out}" \
        >/dev/null 2>&1 \
        || { echo "canary: parity_runner_smoke failed for ${sid}" >&2; return 7; }
}

# Run the gtest filter for the scenario; print "PASS" or "FAIL".
capture_gtest() {
    local sid="$1"
    if "${PARITY_BIN}" --gtest_filter="Parity.${sid}" \
            --gtest_color=no --gtest_print_time=0 >/dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL"
    fi
}

# Diff two --evaluate-facts JSON dumps and the two gtest verdicts; count
# any flip (per-predicate ok delta or gtest delta) and print
# "<flips>\t<details>" so the caller can decide pass/fail.
diff_eval() {
    local pre_eval="$1"
    local post_eval="$2"
    local pre_gtest="$3"
    local post_gtest="$4"
    python3 - "${pre_eval}" "${post_eval}" "${pre_gtest}" "${post_gtest}" <<'PY'
import json, sys

with open(sys.argv[1]) as f: pre = json.load(f)
with open(sys.argv[2]) as f: post = json.load(f)
pre_gtest, post_gtest = sys.argv[3], sys.argv[4]

pre_facts  = {p["index"]: p for p in pre.get("facts", [])}
post_facts = {p["index"]: p for p in post.get("facts", [])}

indexes = sorted(set(pre_facts) | set(post_facts))
flips = []
for i in indexes:
    a, b = pre_facts.get(i), post_facts.get(i)
    if a is None or b is None:
        flips.append(f"#{i}=structural-change")
        continue
    if a["ok"] != b["ok"]:
        flips.append(f"#{i}={a['kind']}({a['ok']}->{b['ok']})")
gtest_flip = pre_gtest != post_gtest
if gtest_flip:
    flips.append(f"gtest({pre_gtest}->{post_gtest})")
print(f"{len(flips)}\t{','.join(flips) if flips else '-'}")
PY
}

# ----- Main -----------------------------------------------------------------

mode=""
scenario_arg=""
filter_arg=""

while (( $# > 0 )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --scenario)
            shift
            [[ $# -gt 0 ]] || { usage; exit 2; }
            [[ -z "${mode}" ]] || { echo "canary: --scenario / --all / --filter are mutually exclusive" >&2; exit 2; }
            mode="scenario"; scenario_arg="$1"
            ;;
        --all)
            [[ -z "${mode}" ]] || { echo "canary: --scenario / --all / --filter are mutually exclusive" >&2; exit 2; }
            mode="all"
            ;;
        --filter)
            shift
            [[ $# -gt 0 ]] || { usage; exit 2; }
            [[ -z "${mode}" ]] || { echo "canary: --scenario / --all / --filter are mutually exclusive" >&2; exit 2; }
            mode="filter"; filter_arg="$1"
            ;;
        *) echo "canary: unrecognised argument: $1" >&2; usage; exit 2 ;;
    esac
    shift
done

[[ -n "${mode}" ]] || { usage; exit 2; }

require_clean_worktree

if [[ ! -x "${PARITY_BIN}" || ! -x "${SMOKE_BIN}" ]]; then
    echo "canary: bootstrapping initial build of og_test_parity + parity_runner_smoke" >&2
    cmake --build --preset "${PRESET}" \
        --target og_test_parity parity_runner_smoke
fi

declare -a scenarios=()
case "${mode}" in
    scenario) scenarios=("${scenario_arg}") ;;
    all)      mapfile -t scenarios < <(all_scenario_ids) ;;
    filter)   mapfile -t scenarios < <(match_glob "${filter_arg}") ;;
esac

if (( ${#scenarios[@]} == 0 )); then
    echo "canary: scenario list is empty" >&2
    exit 3
fi

TMPDIR_CANARY="$(mktemp -d -t parity_canary.XXXXXX)"

declare -i total_processed=0
declare -i total_zero_flips=0
declare -a zero_flip_log=()

echo "canary: processing ${#scenarios[@]} scenarios under preset=${PRESET}"

for sid in "${scenarios[@]}"; do
    total_processed+=1
    echo "--- ${sid} ---"

    if ! lookup_line="$(lookup_mutation "${sid}")"; then
        echo "  SKIP: mutation lookup failed (see stderr)"
        total_zero_flips+=1
        zero_flip_log+=("${sid}: lookup_mutation failed")
        continue
    fi
    IFS=$'\t' read -r mut_file mut_line mut_from mut_to <<<"${lookup_line}"
    echo "  mutation: ${mut_file}:${mut_line}"

    pre_eval="${TMPDIR_CANARY}/${sid}.pre.json"
    post_eval="${TMPDIR_CANARY}/${sid}.post.json"

    if ! capture_eval "${sid}" "${pre_eval}"; then
        total_zero_flips+=1
        zero_flip_log+=("${sid}: pre-capture failed")
        continue
    fi
    pre_gtest="$(capture_gtest "${sid}")"
    echo "  pre:  gtest=${pre_gtest}"

    # Record file for the trap BEFORE invoking the mutator, so a crash
    # mid-mutation still gets the worktree restored.
    MUTATED_FILES+=("${mut_file}")
    if ! python3 "${APPLY_MUT}" "${mut_file}" "${mut_line}" "${mut_from}" "${mut_to}"; then
        echo "  SKIP: _apply_mutation refused"
        # File untouched (apply aborted before writing); drop from list.
        MUTATED_FILES=()
        total_zero_flips+=1
        zero_flip_log+=("${sid}: _apply_mutation failed")
        continue
    fi

    if ! rebuild_targets; then
        echo "  FAIL: rebuild after mutation"
        git -C "${REPO_ROOT}" checkout -- "${mut_file}" >/dev/null 2>&1 || true
        MUTATED_FILES=()
        rebuild_targets >/dev/null 2>&1 || true
        total_zero_flips+=1
        zero_flip_log+=("${sid}: rebuild after mutation failed")
        continue
    fi

    if ! capture_eval "${sid}" "${post_eval}"; then
        git -C "${REPO_ROOT}" checkout -- "${mut_file}" >/dev/null 2>&1 || true
        MUTATED_FILES=()
        rebuild_targets >/dev/null 2>&1 || true
        total_zero_flips+=1
        zero_flip_log+=("${sid}: post-capture failed")
        continue
    fi
    post_gtest="$(capture_gtest "${sid}")"
    echo "  post: gtest=${post_gtest}"

    # Restore and rebuild back to clean BEFORE recording flip count so
    # the next scenario sees the same baseline.
    git -C "${REPO_ROOT}" checkout -- "${mut_file}" >/dev/null 2>&1 || true
    MUTATED_FILES=()
    rebuild_targets

    diff_line="$(diff_eval "${pre_eval}" "${post_eval}" "${pre_gtest}" "${post_gtest}")"
    flips="${diff_line%%$'\t'*}"
    detail="${diff_line#*$'\t'}"
    echo "  flips=${flips}  detail=${detail}"
    if (( flips == 0 )); then
        total_zero_flips+=1
        zero_flip_log+=("${sid}: 0 flips (mutation = ${mut_file}:${mut_line})")
    fi
done

echo
echo "canary: processed ${total_processed} scenarios, ${total_zero_flips} with zero flips"

if (( total_zero_flips > 0 )); then
    echo "canary: FAIL — scenarios with zero flips:" >&2
    for entry in "${zero_flip_log[@]}"; do
        echo "  - ${entry}" >&2
    done
    exit 1
fi

echo "canary: OK — every scenario flipped at least one predicate"
