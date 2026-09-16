#!/usr/bin/env bash
# Parity mutation canary — for each PIN in tests/parity/scenario_table.h, apply
# the pin's discriminating mutation, re-evaluate every scenario row that names
# it, diff per-predicate evaluation pre vs post, and assert at least one
# PREDICATE flipped. Restore the worktree on every exit path.
#
# Predicate flips and the gtest verdict are counted SEPARATELY, and the
# predicates are the oracle. Since #283 the SemanticParity gtest byte-compares
# the canonical dump against the golden, so any mutation that moves a single
# byte of the dump reds the row for free — a row whose own facts are inert
# would otherwise read as guarded. Such a row is reported as
# PREDICATE-TOOTHLESS and fails the run exactly like a row that flips nothing.
#
# GROUPED BY PIN, NOT BY ROW. 63 of the C++ rows share 32 distinct pins, so a
# per-row loop paid the same mutation (and its two rebuilds) several times over.
# scripts/parity/canary_plan.py computes the groups; the group key is the pin's
# whole tuple (file, line, from, to, context_before), so two constants that sit
# on ONE line stay separate groups — applying one does not apply the other.
#
# THE STAGED-PACKS FAST PATH. 152 of the 184 pins live in packs/ Lua, which no
# binary embeds: stage_runtime_assets MIRRORS packs/ into ${BUILD_DIR}/packs and
# every binary resolves assets exe-adjacent, so mutating the STAGED copy changes
# behaviour with no compiler involved. A Lua row costs ~0.1 s that way instead
# of ~40 s. The precondition is that the mirror is a mirror: `diff -rq packs
# ${BUILD_DIR}/packs` must be clean before the run (exit 9 otherwise), the
# restore is `cp` from packs/ followed by `cmp` (exit 9 on mismatch), and NO
# BUILD EVER RUNS WHILE A STAGED MUTATION IS IN FLIGHT — a build re-mirrors the
# directory and would silently un-apply the mutation mid-measurement. The plan
# orders the whole staged arm ahead of the rebuilding one, and rebuild_targets
# refuses outright if a staged file is still mutated.
#
# MEASURED (this box, 2026-09-15): the whole 159-row Lua arm — 152 groups, zero
# rebuilds — runs in 40 s, against ~41.7 s PER ROW before (about 1 h 50 m for
# the same arm), and reproduces the same verdicts the rebuild-per-row run
# recorded on 2026-09-08 (zero_flips=0, the same nine PREDICATE-TOOTHLESS rows).
# The C++ half is 32 distinct pins over 63 rows, two rebuilds each.
#
# Usage:
#   run_mutation_canary.sh --scenario <id>
#   run_mutation_canary.sh --all
#   run_mutation_canary.sh --filter <glob>
#   run_mutation_canary.sh --touched <ref>      # on-PR subset, see canary_plan.py
#   ... plus, on any of those:
#   --plan                 print the plan + run the preflight; mutate nothing
#   --report <path>        write the per-row verdicts as JSON
#   --changed-files-from <file>   test seam: use this file's lines as the
#                          changed-file list instead of `git diff --name-only`
#
# Modes are mutually exclusive. Every mode but --plan refuses to start with a
# dirty worktree. The script refuses to mutate files under ../openglad-master/
# or tests/parity/, and exits non-zero if any scenario records zero PREDICATE
# flips — whether or not its gtest flipped.
#
# Environment:
#   OG_CANARY_BUILD_DIR   build tree to measure (default build/ci-test). The
#                         staged fast path needs it INSIDE this checkout.
#   OG_CANARY_TABLE       scenario table to read (default
#                         tests/parity/scenario_table.h).
#   CMAKE_BUILD_PARALLEL_LEVEL
#                         cmake's own knob, honoured by the rebuilds below —
#                         set it on a memory-capped box (the parity link is
#                         the expensive part).
#
# Exit codes:
#   0  every selected row flipped at least one predicate of its own
#   1  teeth failure: a row with zero flips, or a PREDICATE-TOOTHLESS row
#   2  usage error, or the worktree is dirty
#   3  the selection is empty
#   7  environment abort — a binary, a scenario fixture or a measurement is
#      missing. Nothing was measured, so nothing is reported as guarded.
#   9  staged-packs mirror mismatch: ${BUILD_DIR}/packs is not a copy of packs/
#      (before the run), or a staged restore did not compare equal (after it)
#
# A pin whose from-text repeats in its file carries a context_before, which
# travels with the other fields to _apply_mutation.py: the applier exits 8
# rather than mutate a textual twin of the line the pin means.
#
# WARNING — the EXIT trap restores SOURCES, not OBJECTS. If this script is
# interrupted while a rebuild-cpp group is in flight, build/ci-test still holds
# the og_test_parity and parity_runner_smoke built from that mutation. Anything
# you run out of that build dir afterwards — a parity run, a golden capture, a
# gate — is measuring mutated code. Rebuild before trusting any binary there.
# (The staged arm never rebuilds, so it cannot leave mutated objects; it can
# only leave a mutated staged pack file, which the trap copies back from packs/
# and the next run's mirror check would catch anyway.)

set -euo pipefail

# Suppress .pyc generation so helper imports do not litter the worktree
# with __pycache__ directories that would trip the porcelain check on
# the next run.
export PYTHONDONTWRITEBYTECODE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PRESET="ci-test"
BUILD_DIR="${OG_CANARY_BUILD_DIR:-${REPO_ROOT}/build/${PRESET}}"
PARITY_BIN="${BUILD_DIR}/og_test_parity"
SMOKE_BIN="${BUILD_DIR}/parity_runner_smoke"
APPLY_MUT="${SCRIPT_DIR}/_apply_mutation.py"
PLAN_TOOL="${SCRIPT_DIR}/canary_plan.py"
TABLE_HEADER="${OG_CANARY_TABLE:-${REPO_ROOT}/tests/parity/scenario_table.h}"
FACTS_JSON="${REPO_ROOT}/tests/parity/scenario_facts_generated.json"

# ${BUILD_DIR} as a repository-relative path, or empty when it lives outside
# the checkout. _apply_mutation.py only accepts repository-relative paths
# (deliberately: it is the thing that must never write outside this tree), so
# an external build dir can be PLANNED against but not staged-mutated.
BUILD_REL=""
case "${BUILD_DIR}/" in
    "${REPO_ROOT}/"*) BUILD_REL="${BUILD_DIR#"${REPO_ROOT}"/}" ;;
esac

# Mutated paths the EXIT trap must restore.
#   MUTATED_FILES — repository sources; restored with `git checkout --`.
#   STAGED_FILES  — repo-relative pack paths whose STAGED copy under
#                   ${BUILD_DIR} was mutated; restored by copying packs/ back.
declare -a MUTATED_FILES=()
declare -a STAGED_FILES=()

usage() {
    cat >&2 <<'EOF'
usage:
  run_mutation_canary.sh --scenario <id>
  run_mutation_canary.sh --all
  run_mutation_canary.sh --filter <glob>
  run_mutation_canary.sh --touched <ref>
options:
  --plan                      print the plan and run the preflight only
  --report <path>             write per-row verdicts as JSON
  --changed-files-from <file> test seam: changed-file list for --touched
EOF
}

# Tracked at startup; populated after argv parsing.
TMPDIR_CANARY=""

# Copy the repository's packs/<rel> over the staged copy and prove the two
# compare equal. A restore that did not restore is the one failure mode the
# fast path can have that no later measurement would notice: every subsequent
# row would be measured against a still-mutated pack and quietly report
# whatever that mutation does.
restore_staged_file() {
    local rel="$1"
    local src="${REPO_ROOT}/${rel}"
    local dst="${BUILD_DIR}/${rel}"
    cp -- "${src}" "${dst}"
    if ! cmp -s -- "${src}" "${dst}"; then
        echo "canary: staged restore FAILED — ${dst} still differs from ${src}" >&2
        return 1
    fi
    return 0
}

# Trap restorer — runs on any exit (success, failure, signal). Captures
# the original $? FIRST so subsequent `rm -rf` / `git checkout` calls
# don't mask it. Restores BOTH mutation lists, then deletes the canary tmpdir.
restore_mutations() {
    local rc=$?
    if (( ${#MUTATED_FILES[@]} > 0 )); then
        for f in "${MUTATED_FILES[@]}"; do
            [[ -z "${f}" ]] && continue
            git -C "${REPO_ROOT}" checkout -- "${f}" >/dev/null 2>&1 || true
        done
        MUTATED_FILES=()
        cat >&2 <<EOF
canary: a source mutation was in flight — ${BUILD_DIR} still holds binaries
built from it. Rebuild before trusting anything in that build directory.
EOF
    fi
    if (( ${#STAGED_FILES[@]} > 0 )); then
        for f in "${STAGED_FILES[@]}"; do
            [[ -z "${f}" ]] && continue
            restore_staged_file "${f}" || true
        done
        STAGED_FILES=()
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

# The mutation the tree currently carries, as the JSON declaration
# check_mutation_pins.py accepts — empty whenever the tree is clean.
#
# That check is a build dependency of og_test_parity, so every rebuild below
# runs it. On a shared anchor (several pins on one line) the mutated line
# matches neither side of the siblings' substitutions, and the check would red
# and abort the canary. The declaration is how the canary says which single
# known mutation it applied; the check recognises that one state and nothing
# else. An UNdeclared tree is judged exactly as a hand-edited tree is — see
# accepts() in check_mutation_pins.py.
IN_FLIGHT_JSON=""
declare -i TOTAL_REBUILDS=0

# Build targets needed for the canary loop. `--target` accepts multiple
# names so Ninja can schedule them together; the binaries share most TUs
# so the incremental rebuild after a single source edit is one .cpp +
# one .a re-archive + two executable re-links.
#
# Refuses while a staged pack mutation is live: the build re-mirrors packs/
# into the build tree, which would un-apply that mutation without saying so
# and turn the rest of the group into a measurement of nothing.
rebuild_targets() {
    if (( ${#STAGED_FILES[@]} > 0 )); then
        echo "canary: refusing to build while a staged pack mutation is in flight" >&2
        exit 9
    fi
    TOTAL_REBUILDS+=1
    OPENGLAD_MUTATION_IN_FLIGHT="${IN_FLIGHT_JSON}" \
        cmake --build --preset "${PRESET}" \
            --target og_test_parity parity_runner_smoke \
            </dev/null >/dev/null 2>&1 \
        || { OPENGLAD_MUTATION_IN_FLIGHT="${IN_FLIGHT_JSON}" \
                cmake --build --preset "${PRESET}" \
                    --target og_test_parity parity_runner_smoke </dev/null; exit 1; }
}

# Run a single scenario through the smoke runner with --evaluate-facts;
# write the per-fact JSON to ${2}.
#
# A capture that does not happen is not a measurement, so this aborts the
# whole run instead of letting the loop record a verdict for a scenario it
# never evaluated. The tool's own stderr goes out first: exit 3 means the
# campaign mount is broken, and that is a fact about the environment, not
# about the pin.
capture_eval() {
    local sid="$1"
    local out="$2"
    local log status
    log="$("${SMOKE_BIN}" --scenario "${sid}" --evaluate-facts --out "${out}" \
            </dev/null 2>&1)" \
        && return 0
    status=$?
    printf '%s\n' "${log}" >&2
    cat >&2 <<EOF
canary: ABORT — parity_runner_smoke exited ${status}
  scenario   : ${sid}
  smoke bin  : ${SMOKE_BIN}
  config dir : ${OPENGLAD_CONFIG_DIR:-<unset — parity_runner_smoke picks a private per-process temp dir>}
This is an environment failure, not a toothless pin. Nothing was measured,
so nothing is being reported as guarded or unguarded.
EOF
    exit 7
}

# Run the gtest filter for the scenario; print "PASS" or "FAIL".
capture_gtest() {
    local sid="$1"
    if "${PARITY_BIN}" --gtest_filter="Parity.${sid}" \
            --gtest_color=no --gtest_print_time=0 </dev/null >/dev/null 2>&1; then
        echo "PASS"
    else
        echo "FAIL"
    fi
}

# Diff two --evaluate-facts JSON dumps and the two gtest verdicts, and print
# "<predicate_flips>\t<gtest_flip 0|1>\t<details>" so the caller can decide
# pass/fail. The two counts stay apart on purpose: the gtest verdict of a
# SemanticParity row includes the #283 golden byte compare, which flips on any
# perturbation of the dump and therefore says nothing about the row's own
# facts.
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
pred_flips = len(flips)
gtest_flip = pre_gtest != post_gtest
if gtest_flip:
    flips.append(f"gtest({pre_gtest}->{post_gtest})")
print(f"{pred_flips}\t{1 if gtest_flip else 0}\t"
      f"{','.join(flips) if flips else '-'}")
PY
}

# ----- Main -----------------------------------------------------------------

mode=""
selector=""
plan_only=0
report_path=""
changed_files_from=""

while (( $# > 0 )); do
    case "$1" in
        --help|-h) usage; exit 0 ;;
        --scenario|--filter|--touched)
            local_mode="${1#--}"
            shift
            [[ $# -gt 0 ]] || { usage; exit 2; }
            [[ -z "${mode}" ]] || { echo "canary: --scenario / --all / --filter / --touched are mutually exclusive" >&2; exit 2; }
            mode="${local_mode}"; selector="$1"
            ;;
        --all)
            [[ -z "${mode}" ]] || { echo "canary: --scenario / --all / --filter / --touched are mutually exclusive" >&2; exit 2; }
            mode="all"
            ;;
        --plan) plan_only=1 ;;
        --report)
            shift
            [[ $# -gt 0 ]] || { usage; exit 2; }
            report_path="$1"
            ;;
        --changed-files-from)
            shift
            [[ $# -gt 0 ]] || { usage; exit 2; }
            changed_files_from="$1"
            ;;
        *) echo "canary: unrecognised argument: $1" >&2; usage; exit 2 ;;
    esac
    shift
done

[[ -n "${mode}" ]] || { usage; exit 2; }
if [[ -n "${changed_files_from}" && "${mode}" != "touched" ]]; then
    echo "canary: --changed-files-from only means anything with --touched" >&2
    exit 2
fi

# --plan mutates nothing, so it does not care what else is in the tree; every
# other mode does, because it is about to `git checkout --` files.
(( plan_only == 1 )) || require_clean_worktree

TMPDIR_CANARY="$(mktemp -d -t parity_canary.XXXXXX)"

# ----- Plan -----------------------------------------------------------------

declare -a plan_args=(--table "${TABLE_HEADER}" --facts "${FACTS_JSON}")
case "${mode}" in
    all)      plan_args+=(--mode all) ;;
    scenario) plan_args+=(--mode scenario --pattern "${selector}") ;;
    filter)   plan_args+=(--mode filter --pattern "${selector}") ;;
    touched)
        plan_args+=(--mode touched)
        base_table="${TMPDIR_CANARY}/base_scenario_table.h"
        if ! git -C "${REPO_ROOT}" show "${selector}:tests/parity/scenario_table.h" \
                > "${base_table}" 2>/dev/null; then
            echo "canary: cannot read tests/parity/scenario_table.h at ${selector}" >&2
            exit 2
        fi
        plan_args+=(--base-table "${base_table}")
        changed_list="${TMPDIR_CANARY}/changed-files"
        if [[ -n "${changed_files_from}" ]]; then
            cp -- "${changed_files_from}" "${changed_list}"
        elif ! git -C "${REPO_ROOT}" diff --name-only "${selector}" \
                > "${changed_list}" 2>/dev/null; then
            echo "canary: cannot diff against ${selector}" >&2
            exit 2
        fi
        plan_args+=(--changed-files-from "${changed_list}")
        ;;
esac

PLAN_DIR="${TMPDIR_CANARY}/plan"
plan_rc=0
python3 "${PLAN_TOOL}" "${plan_args[@]}" --emit records --out "${PLAN_DIR}" \
    > "${TMPDIR_CANARY}/plan.txt" || plan_rc=$?
if (( plan_rc != 0 )); then
    cat "${TMPDIR_CANARY}/plan.txt" >&2 || true
    exit "${plan_rc}"
fi

plan_rows=$(sed -n 's/^rows=//p' "${PLAN_DIR}/summary")
plan_groups=$(sed -n 's/^groups=//p' "${PLAN_DIR}/summary")
plan_staged=$(sed -n 's/^staged_lua=//p' "${PLAN_DIR}/summary")

# ----- Preflight ------------------------------------------------------------
#
# Everything the run needs, checked once, before anything is mutated — and
# checked by --plan too, which is the cheap way to ask "could this run at all?"

if [[ ! -x "${PARITY_BIN}" || ! -x "${SMOKE_BIN}" ]]; then
    if (( plan_only == 1 )); then
        echo "canary: ${PARITY_BIN} / ${SMOKE_BIN} are not both executable" >&2
        exit 7
    fi
    echo "canary: bootstrapping initial build of og_test_parity + parity_runner_smoke" >&2
    cmake --build --preset "${PRESET}" \
        --target og_test_parity parity_runner_smoke
fi

# The staged fast path is only valid while the staged tree IS a mirror.
if (( plan_staged > 0 )); then
    if [[ ! -d "${BUILD_DIR}/packs" ]]; then
        echo "canary: ${BUILD_DIR}/packs does not exist (build once so stage_runtime_assets mirrors packs/)" >&2
        exit 9
    fi
    mirror_diff="$(diff -rq "${REPO_ROOT}/packs" "${BUILD_DIR}/packs" 2>&1 | head -n 1 || true)"
    if [[ -n "${mirror_diff}" ]]; then
        cat >&2 <<EOF
canary: ${BUILD_DIR}/packs is not a mirror of packs/ — the staged fast path
would measure something other than the repository's packs.
  ${mirror_diff}
Rebuild (stage_runtime_assets re-mirrors) and try again.
EOF
        exit 9
    fi
    # Only a run that is about to MUTATE needs the staged tree to be
    # addressable as a repository-relative path (_apply_mutation.py accepts
    # nothing else). --plan is happy to describe an external build dir.
    if (( plan_only == 0 )) && [[ -z "${BUILD_REL}" ]]; then
        echo "canary: ${BUILD_DIR} is outside the checkout; the staged pack fast path needs a build dir inside it" >&2
        exit 7
    fi
fi

# Scenario fixtures. Most rows load scen/scen1.fss, which lives in temp/scen/
# and is PRODUCED by og_test_level; a missing one turns every capture into an
# environment abort, one row at a time, with nothing saying why.
while IFS= read -r fixture; do
    [[ -z "${fixture}" ]] && continue
    base="${fixture##*/}"
    if [[ -f "${REPO_ROOT}/${fixture}" || -f "${REPO_ROOT}/scen/${base}" \
          || -f "${REPO_ROOT}/temp/scen/${base}" ]]; then
        continue
    fi
    cat >&2 <<EOF
canary: scenario fixture ${fixture} is missing (looked for ${fixture},
scen/${base} and temp/scen/${base} under ${REPO_ROOT}).
run ctest -R ^og_test_level$ first — that suite produces temp/scen/*.fss.
EOF
    exit 7
done < "${PLAN_DIR}/fixtures"

if (( plan_only == 1 )); then
    cat "${TMPDIR_CANARY}/plan.txt"
    exit 0
fi

cat "${TMPDIR_CANARY}/plan.txt"
echo "canary: processing ${plan_rows} rows in ${plan_groups} groups under preset=${PRESET}"

# ----- Baseline -------------------------------------------------------------
#
# Captured ONCE for every selected row, off the clean tree, before the first
# mutation. The old per-row loop re-measured the same baseline for every row.

declare -A PRE_GTEST=()
while IFS= read -r sid; do
    [[ -z "${sid}" ]] && continue
    capture_eval "${sid}" "${TMPDIR_CANARY}/${sid}.pre.json"
    PRE_GTEST["${sid}"]="$(capture_gtest "${sid}")"
done < "${PLAN_DIR}/rows"

# ----- The loop -------------------------------------------------------------

declare -i total_processed=0
declare -i total_zero_flips=0
declare -a zero_flip_log=()
# Rows whose facts did not move but whose gtest did: since #283 that gtest is a
# byte compare against the golden, so these rows are red-under-mutation for
# free and their own predicates guard nothing.
declare -i total_pred_toothless=0
declare -a pred_toothless_log=()
declare -a report_lines=()
declare -a error_lines=()

record_row() {
    # sid file line group_mode pred_flips gtest_flip detail
    report_lines+=("$1"$'\t'"$2"$'\t'"$3"$'\t'"$4"$'\t'"$5"$'\t'"$6"$'\t'"$7")
}

# A row the plan could not resolve to a pin is a teeth failure, not a skip:
# the table says the row is guarded and nothing is guarding it.
while IFS=$'\t' read -r sid why; do
    [[ -z "${sid}" ]] && continue
    total_processed+=1
    total_zero_flips+=1
    zero_flip_log+=("${sid}: ${why}")
    error_lines+=("::error::canary ${sid}: no usable discriminating_mutation (${why})")
    record_row "${sid}" "-" "0" "unresolved" "0" "0" "${why}"
done < "${PLAN_DIR}/unresolved"

while read -r gidx gmode gcount; do
    [[ -z "${gidx}" ]] && continue
    if ! { IFS= read -r -d '' mut_file
           IFS= read -r -d '' mut_line
           IFS= read -r -d '' mut_from
           IFS= read -r -d '' mut_to
           IFS= read -r -d '' mut_ctx
           IFS= read -r -d '' mut_decl
         } < "${PLAN_DIR}/${gidx}.pin"; then
        echo "canary: malformed plan record for group ${gidx}" >&2
        exit 2
    fi
    mapfile -t group_rows < "${PLAN_DIR}/${gidx}.rows"

    echo "=== group ${gidx}/${plan_groups} ${gmode} ${mut_file}:${mut_line} rows=${gcount}"

    if [[ "${gmode}" == "staged-lua" ]]; then
        # Mutate the STAGED copy. The repository file is never touched, so the
        # worktree stays clean and no rebuild is needed or allowed.
        STAGED_FILES+=("${mut_file}")
        if ! python3 "${APPLY_MUT}" "${BUILD_REL}/${mut_file}" "${mut_line}" \
                 "${mut_from}" "${mut_to}" "${mut_ctx}"; then
            echo "  SKIP: _apply_mutation refused"
            STAGED_FILES=()
            for sid in "${group_rows[@]}"; do
                total_processed+=1
                total_zero_flips+=1
                zero_flip_log+=("${sid}: _apply_mutation failed (${mut_file}:${mut_line})")
                error_lines+=("::error::canary ${sid}: _apply_mutation refused (${mut_file}:${mut_line})")
                record_row "${sid}" "${mut_file}" "${mut_line}" "${gmode}" "0" "0" "apply-refused"
            done
            continue
        fi
    else
        # Record for the trap BEFORE invoking the mutator, so a crash
        # mid-mutation still gets the worktree restored.
        MUTATED_FILES+=("${mut_file}")
        if ! python3 "${APPLY_MUT}" "${mut_file}" "${mut_line}" "${mut_from}" \
                 "${mut_to}" "${mut_ctx}"; then
            echo "  SKIP: _apply_mutation refused"
            MUTATED_FILES=()
            for sid in "${group_rows[@]}"; do
                total_processed+=1
                total_zero_flips+=1
                zero_flip_log+=("${sid}: _apply_mutation failed (${mut_file}:${mut_line})")
                error_lines+=("::error::canary ${sid}: _apply_mutation refused (${mut_file}:${mut_line})")
                record_row "${sid}" "${mut_file}" "${mut_line}" "${gmode}" "0" "0" "apply-refused"
            done
            continue
        fi
        # Declared only between a successful apply and the restore below: the
        # window in which the pinned line legitimately holds a mutated state.
        IN_FLIGHT_JSON="${mut_decl}"
        rebuild_targets
    fi

    unset POST_GTEST
    declare -A POST_GTEST=()
    for sid in "${group_rows[@]}"; do
        capture_eval "${sid}" "${TMPDIR_CANARY}/${sid}.post.json"
        POST_GTEST["${sid}"]="$(capture_gtest "${sid}")"
    done

    # Restore BEFORE recording verdicts, so the next group starts from the
    # same baseline the pre-capture was taken against.
    if [[ "${gmode}" == "staged-lua" ]]; then
        restore_staged_file "${mut_file}" || exit 9
        STAGED_FILES=()
    else
        git -C "${REPO_ROOT}" checkout -- "${mut_file}" >/dev/null 2>&1 || true
        MUTATED_FILES=()
        IN_FLIGHT_JSON=""
        rebuild_targets
    fi

    for sid in "${group_rows[@]}"; do
        total_processed+=1
        diff_line="$(diff_eval "${TMPDIR_CANARY}/${sid}.pre.json" \
                               "${TMPDIR_CANARY}/${sid}.post.json" \
                               "${PRE_GTEST[${sid}]}" "${POST_GTEST[${sid}]}")"
        IFS=$'\t' read -r pred_flips gtest_flip detail <<< "${diff_line}"
        echo "  ${sid}: predicate_flips=${pred_flips}  gtest_flip=${gtest_flip}  detail=${detail}"
        record_row "${sid}" "${mut_file}" "${mut_line}" "${gmode}" \
                   "${pred_flips}" "${gtest_flip}" "${detail}"
        if (( pred_flips == 0 && gtest_flip == 0 )); then
            total_zero_flips+=1
            zero_flip_log+=("${sid}: 0 flips (mutation = ${mut_file}:${mut_line})")
            error_lines+=("::error::canary ${sid}: 0 flips (${mut_file}:${mut_line})")
        elif (( pred_flips == 0 )); then
            total_pred_toothless+=1
            pred_toothless_log+=("${sid}: 0 predicate flips, gtest only (mutation = ${mut_file}:${mut_line})")
            error_lines+=("::error::canary ${sid}: PREDICATE-TOOTHLESS (${mut_file}:${mut_line})")
        fi
    done
    unset POST_GTEST
done < "${PLAN_DIR}/index"

echo
echo "canary: processed ${total_processed} rows, ${total_zero_flips} with zero flips, ${total_pred_toothless} predicate-toothless"

if [[ -n "${report_path}" ]]; then
    # The rows go through a FILE, not a pipe: `python3 - <<EOF` already spends
    # stdin on the program text, so a piped payload would be read by nobody and
    # printf would die of SIGPIPE.
    printf '%s\n' "${report_lines[@]+"${report_lines[@]}"}" \
        > "${TMPDIR_CANARY}/report.tsv"
    python3 - "${report_path}" "${TMPDIR_CANARY}/report.tsv" \
               "${total_zero_flips}" "${total_pred_toothless}" \
               "${TOTAL_REBUILDS}" <<'REPORT_PY'
import json, sys
rows = []
with open(sys.argv[2], encoding="utf-8") as fh:
    for line in fh.read().splitlines():
        if not line.strip():
            continue
        sid, f, ln, mode, pred, gflip, detail = line.split("\t", 6)
        rows.append({"id": sid, "file": f, "line": int(ln), "mode": mode,
                     "pred_flips": int(pred), "gtest_flip": int(gflip),
                     "detail": detail})
report = {"rows": rows,
          "tallies": {"rows": len(rows),
                      "zero_flips": int(sys.argv[3]),
                      "predicate_toothless": int(sys.argv[4]),
                      "rebuilds": int(sys.argv[5])}}
with open(sys.argv[1], "w", encoding="utf-8") as f:
    json.dump(report, f, indent=2, sort_keys=True)
    f.write("\n")
REPORT_PY
    echo "canary: report written to ${report_path}"
fi

# One machine-readable line per offending row. GitHub renders `::error::`
# annotations in the job log and on the PR; elsewhere it is a grep target.
for line in "${error_lines[@]+"${error_lines[@]}"}"; do
    echo "${line}"
done

declare -i canary_rc=0

if (( total_zero_flips > 0 )); then
    echo "canary: FAIL — scenarios with zero flips (nothing moved at all):" >&2
    for entry in "${zero_flip_log[@]}"; do
        echo "  - ${entry}" >&2
    done
    canary_rc=1
fi

if (( total_pred_toothless > 0 )); then
    echo "canary: PREDICATE-TOOTHLESS (carried by the byte compare only) — the mutation moves the dump, so the #283 golden byte compare reds the row, but not one of the row's own facts changed verdict. Retune a fact to read the consequence (an exact ScoreDelta / count / hp is usually the cheapest discriminator):" >&2
    for entry in "${pred_toothless_log[@]}"; do
        echo "  - ${entry}" >&2
    done
    canary_rc=1
fi

echo "canary: rows=${total_processed} groups=${plan_groups} rebuilds=${TOTAL_REBUILDS} zero_flips=${total_zero_flips} predicate_toothless=${total_pred_toothless}"

if (( canary_rc != 0 )); then
    exit "${canary_rc}"
fi

echo "canary: OK — every scenario flipped at least one predicate of its own"
