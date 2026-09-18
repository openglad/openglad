#!/usr/bin/env bash
# CLI contract for the mutation canary's PLAN half —
# scripts/parity/canary_plan.py and the `--plan` mode of
# scripts/parity/run_mutation_canary.sh.
#
# What this pins, and why it is worth a ctest entry: the canary's verdict is
# only as good as its selection. A grouping key that loses a field silently
# merges two pins and measures one of them twice while the other is never
# applied; a --touched arm that drops a rule silently stops running rows; a
# missing staged-packs precondition silently measures the wrong pack tree. All
# three failures are INVISIBLE in the canary's own output — it reports OK for
# every row it did run. So the counts here are computed INDEPENDENTLY from the
# table via lint_scenario_facts (never from canary_plan.py, which is the thing
# under test) and compared exactly.
#
# The table read is HEAD's copy, not the working tree's: --touched compares the
# working table against `git show <ref>:...`, so pinning both ends to HEAD
# makes the expected sets a function of the commit rather than of whatever is
# half-edited in the tree right now. The logic under test is unaffected.
set -euo pipefail

parity_bin=${1:?usage: test_mutation_canary_plan.sh <og_test_parity> <parity_runner_smoke>}
smoke_bin=${2:?usage: test_mutation_canary_plan.sh <og_test_parity> <parity_runner_smoke>}

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=$(cd "$(dirname "$parity_bin")" && pwd)
canary="$repo_root/scripts/parity/run_mutation_canary.sh"
plan_tool="$repo_root/scripts/parity/canary_plan.py"
test_root=$(mktemp -d)
moved_fixtures=()

cleanup() {
    local rc=$?
    local i
    for (( i = 0; i < ${#moved_fixtures[@]}; i += 2 )); do
        mv -f -- "${moved_fixtures[i+1]}" "${moved_fixtures[i]}" 2>/dev/null || true
    done
    rm -rf -- "$test_root"
    exit "$rc"
}
trap cleanup EXIT INT TERM

fail() { printf 'test_mutation_canary_plan: %s\n' "$1" >&2; exit 1; }

export PYTHONDONTWRITEBYTECODE=1

# --- (1) the plan helper's own self-test ------------------------------------
python3 "$plan_tool" --self-test || fail 'canary_plan.py --self-test failed'

# --- independent expectations, straight from the table ----------------------
table="$test_root/table.h"
git -C "$repo_root" show HEAD:tests/parity/scenario_table.h > "$table" ||
    fail 'cannot read HEAD:tests/parity/scenario_table.h'

facts_for() {
    PYTHONPATH="$repo_root/scripts/parity" python3 - "$table" "$1" <<'PY'
from pathlib import Path
import sys
import lint_scenario_facts as lint

text = Path(sys.argv[1]).read_text(encoding="utf-8")
muts = lint.parse_mutation_constants(text)
rows = lint.parse_scenarios(text)
pins = {}
for r in rows:
    m = muts[r["mutation_token"]]
    key = (m["file"], str(m["line"]), m["from"], m["to"],
           m.get("context_before", ""))
    pins.setdefault(key, []).append(r["id"])

what = sys.argv[2]
if what == "groups":
    print(len(pins))
elif what == "rows":
    print(len(rows))
elif what == "ids":
    print("\n".join(sorted(r["id"] for r in rows)))
elif what == "lua_groups":
    print(sum(1 for k in pins if k[0].startswith("packs/")))
elif what == "lua_ids":
    print("\n".join(sorted(i for k, ids in pins.items()
                           if k[0].startswith("packs/") for i in ids)))
elif what == "weap_groups":
    print(sum(1 for k in pins if k[0] == "src/gameplay/weap.cpp"))
else:
    raise SystemExit(f"unknown query {what}")
PY
}

expected_groups=$(facts_for groups)
expected_rows=$(facts_for rows)
expected_lua_groups=$(facts_for lua_groups)
expected_weap_groups=$(facts_for weap_groups)
facts_for ids > "$test_root/expected_ids"
facts_for lua_ids > "$test_root/expected_lua_ids"

# Sanity on the oracle itself: a table that parsed to nothing would make every
# comparison below vacuous.
(( expected_groups > 0 && expected_rows > 0 && expected_lua_groups > 0 && expected_weap_groups > 0 )) ||
    fail "the independent table parse produced empty expectations (groups=$expected_groups rows=$expected_rows lua=$expected_lua_groups weap=$expected_weap_groups)"

run_plan() {  # run_plan <out-file> <args...>
    local out="$1"; shift
    local rc=0
    OG_CANARY_TABLE="$table" OG_CANARY_BUILD_DIR="$build_dir" \
        bash "$canary" "$@" > "$out" 2>"$out.err" || rc=$?
    return "$rc"
}

group_ids() {  # every id named by the `group ...` lines of a plan dump
    sed -n 's/^group [0-9]*\/[0-9]* [a-z-]* [^ ]* rows=[0-9]*: //p' "$1" |
        tr ',' '\n' | sed '/^$/d'
}

summary_field() { sed -n "s/^plan: .*$1=\([0-9]*\).*/\1/p" "$2"; }

# --- (2) --plan --all: one group per pin tuple, every id exactly once --------
run_plan "$test_root/all.txt" --plan --all ||
    fail "--plan --all exited nonzero: $(cat "$test_root/all.txt.err")"

actual_groups=$(grep -c '^group ' "$test_root/all.txt" || true)
[[ "$actual_groups" == "$expected_groups" ]] ||
    fail "--plan --all printed $actual_groups groups, the table has $expected_groups distinct pin tuples"
[[ "$(summary_field groups "$test_root/all.txt")" == "$expected_groups" ]] ||
    fail 'the summary line disagrees with the group lines it summarises'
[[ "$(summary_field rows "$test_root/all.txt")" == "$expected_rows" ]] ||
    fail "--plan --all summarised $(summary_field rows "$test_root/all.txt") rows, the table has $expected_rows"

group_ids "$test_root/all.txt" | sort > "$test_root/all_ids"
diff -u "$test_root/expected_ids" "$test_root/all_ids" > "$test_root/ids.diff" ||
    fail "--plan --all does not name every master-comparable row exactly once:
$(cat "$test_root/ids.diff")"
[[ "$(wc -l < "$test_root/all_ids")" == "$expected_rows" ]] ||
    fail 'a row id is named by more than one group'

# --- (3) --touched with an empty change set: the free Lua arm, no rebuilds ---
: > "$test_root/changed-none"
run_plan "$test_root/touched.txt" --plan --touched HEAD \
    --changed-files-from "$test_root/changed-none" ||
    fail "--plan --touched exited nonzero: $(cat "$test_root/touched.txt.err")"

[[ "$(summary_field 'rebuild-cpp' "$test_root/touched.txt")" == "0" ]] ||
    fail "--touched with no changed files planned rebuild-cpp groups"
[[ "$(summary_field 'staged-lua' "$test_root/touched.txt")" == "$expected_lua_groups" ]] ||
    fail "--touched planned $(summary_field 'staged-lua' "$test_root/touched.txt") staged-lua groups, the table has $expected_lua_groups"
group_ids "$test_root/touched.txt" | sort > "$test_root/touched_ids"
diff -u "$test_root/expected_lua_ids" "$test_root/touched_ids" > "$test_root/lua.diff" ||
    fail "--touched with no changed files must be exactly the packs/-pinned rows:
$(cat "$test_root/lua.diff")"

# --- (4) a changed C++ file adds exactly that file's pins -------------------
echo 'src/gameplay/weap.cpp' > "$test_root/changed-weap"
run_plan "$test_root/weap.txt" --plan --touched HEAD \
    --changed-files-from "$test_root/changed-weap" ||
    fail "--plan --touched (weap.cpp) exited nonzero: $(cat "$test_root/weap.txt.err")"

weap_groups=$(grep -c '^group .* rebuild-cpp ' "$test_root/weap.txt" || true)
[[ "$weap_groups" == "$expected_weap_groups" ]] ||
    fail "a changed weap.cpp planned $weap_groups rebuild-cpp groups, the table pins $expected_weap_groups there"
if grep '^group .* rebuild-cpp ' "$test_root/weap.txt" |
        grep -qv ' src/gameplay/weap.cpp:'; then
    fail 'a changed weap.cpp pulled in a pin from another file'
fi
[[ "$(summary_field 'staged-lua' "$test_root/weap.txt")" == "$expected_lua_groups" ]] ||
    fail 'the free Lua arm did not ride along with a changed C++ file'

# --- (5) a changed harness file escalates to the full set -------------------
echo 'scripts/parity/x.py' > "$test_root/changed-harness"
run_plan "$test_root/escalate.txt" --plan --touched HEAD \
    --changed-files-from "$test_root/changed-harness" ||
    fail "--plan --touched (harness) exited nonzero: $(cat "$test_root/escalate.txt.err")"

grep -q '^escalation: scripts/parity/x.py ' "$test_root/escalate.txt" ||
    fail 'a changed scripts/parity/ file did not print the escalation line'
[[ "$(summary_field groups "$test_root/escalate.txt")" == "$expected_groups" ]] ||
    fail 'the escalated plan is not the full plan'
[[ "$(summary_field all_required "$test_root/escalate.txt")" == "1" ]] ||
    fail 'the escalated plan did not set all_required'

# --- (6) the staged-packs mirror precondition -------------------------------
# The fast path mutates ${BUILD_DIR}/packs instead of packs/, which is only
# sound while the two are identical. Build a stand-in build dir whose packs
# copy differs by one byte and require the run to refuse (exit 9) and name the
# file.
diff -rq "$repo_root/packs" "$build_dir/packs" > "$test_root/mirror.diff" 2>&1 ||
    fail "the real staged packs mirror already differs, so this case would be testing the wrong difference:
$(cat "$test_root/mirror.diff")"

mirror="$test_root/mirror"
mkdir -p "$mirror"
ln -s "$parity_bin" "$mirror/og_test_parity"
ln -s "$smoke_bin"  "$mirror/parity_runner_smoke"
cp -r "$build_dir/packs" "$mirror/packs"
victim="$mirror/packs/core/lib/lc.lua"
[[ -f "$victim" ]] || victim=$(find "$mirror/packs" -name '*.lua' | sort | head -1)
[[ -f "$victim" ]] || fail 'no staged .lua file to corrupt'
printf '\n' >> "$victim"

set +e
mirror_out=$(OG_CANARY_TABLE="$table" OG_CANARY_BUILD_DIR="$mirror" \
    bash "$canary" --plan --all 2>&1)
mirror_rc=$?
set -e
(( mirror_rc == 9 )) ||
    fail "a corrupted staged packs mirror exited $mirror_rc, expected 9
$mirror_out"
grep -Fq "${victim#"$mirror/"}" <<<"$mirror_out" ||
    fail "the mirror refusal did not name the differing file
$mirror_out"

# --- (7) a missing scenario fixture aborts the environment, not the pin -----
# coverage_catchall_scen99 loads scen99.fss, which og_test_level produces into
# temp/scen/. Without it every capture would abort one row at a time with
# nothing saying why, so the preflight names it up front.
hidden=0
for candidate in "$repo_root/temp/scen/scen99.fss" "$repo_root/scen/scen99.fss"; do
    if [[ -f "$candidate" ]]; then
        stash="$test_root/hidden-$hidden.fss"
        mv -- "$candidate" "$stash"
        # Pairs of (original, stash); the EXIT trap puts every one back, so an
        # abort in the middle of this case cannot leave the tree without a
        # fixture the rest of the suite needs.
        moved_fixtures+=("$candidate" "$stash")
        hidden=$(( hidden + 1 ))
    fi
done
if (( hidden == 0 )); then
    fail 'no scen99.fss fixture was present to hide — run ctest -R ^og_test_level$ first'
fi

set +e
missing_out=$(OG_CANARY_TABLE="$table" OG_CANARY_BUILD_DIR="$build_dir" \
    bash "$canary" --plan --scenario coverage_catchall_scen99 2>&1)
missing_rc=$?
set -e
(( missing_rc == 7 )) ||
    fail "a missing scenario fixture exited $missing_rc, expected 7
$missing_out"
grep -Fq 'scen99.fss' <<<"$missing_out" ||
    fail "the fixture refusal did not name the fixture
$missing_out"
grep -Fq 'og_test_level' <<<"$missing_out" ||
    fail "the fixture refusal did not say which suite produces it
$missing_out"

echo 'test_mutation_canary_plan: OK'
