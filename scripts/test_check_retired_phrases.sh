#!/usr/bin/env bash
# The teeth of scripts/check_retired_phrases.sh, on temp trees.
#
# The gate's job is to fail when a fact PR #292 falsified reappears in prose
# as a current statement, and to stay quiet when the statement carries a dated
# correction.  Three of its parts can rot silently and none of them would show
# on a clean tree:
#
#   - a regex in scripts/retired_phrases.txt that no longer matches the phrase
#     it was written for (or that mawk cannot compile) -- the gate still says
#     OK, because OK is what it says when it finds nothing.  Case 12 plants
#     every row's own `sample` column and demands a hit for each, so a rotted
#     or dropped rule reds HERE instead of going quiet on the real tree, and
#     it pins the row COUNT, because a loop over the table cannot notice a row
#     that was deleted from under it.
#   - the block rule.  Case 3 (note in the block) vs case 4 (a blank line
#     between) and case 5 (a note in the next table row) are the difference
#     between "this statement is annotated" and "somebody wrote a note
#     somewhere in this file".
#   - the joined-pair match.  Case 13 wraps a phrase across a line break, the
#     shape `grep -n` cannot see; without it, re-wrapping a paragraph disarms
#     the gate.
#
# Plus the contract: 0 clean, 1 violation NAMED with its reason, 2 cannot run
# (missing root, missing table, malformed rule) -- and the scope ruling, since
# a gate that read tests/ would fail on the regression comments that quote the
# base tree on purpose (case 7).
#
# This file spells no retired phrase of its own: every fixture string is read
# out of the real table at run time, which is also what makes case 12 a test
# of the table rather than of a copy of it.
set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TABLE="${SCRIPTS_DIR}/retired_phrases.txt"
tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM

out="${tmp}/stdout"
err="${tmp}/stderr"
rc=0
CASES=0

run_check() {  # run_check <root> [table]
    rc=0
    bash "${SCRIPTS_DIR}/check_retired_phrases.sh" "$@" >"${out}" 2>"${err}" || rc=$?
}

fail() {  # fail <case> <why>
    echo "FAIL case $1: $2" >&2
    echo "  observed rc: ${rc}" >&2
    echo "  --- stdout ---" >&2
    sed 's/^/  /' "${out}" >&2
    echo "  --- stderr ---" >&2
    sed 's/^/  /' "${err}" >&2
    exit 1
}

expect_rc() {  # expect_rc <case> <expected>
    [[ "${rc}" -eq "$2" ]] || fail "$1" "expected rc $2"
}
expect_stdout() {  # expect_stdout <case> <substring>
    grep -qF -- "$2" "${out}" || fail "$1" "stdout does not contain '$2'"
}
expect_no_stdout() {  # expect_no_stdout <case> <substring>
    if grep -qF -- "$2" "${out}"; then
        fail "$1" "stdout must NOT contain '$2'"
    fi
}
expect_stderr() {  # expect_stderr <case> <substring>
    grep -qF -- "$2" "${err}" || fail "$1" "stderr does not contain '$2'"
}
pass() {  # numbers the cases so the tally cannot disagree with itself
    CASES=$((CASES + 1))
    echo "PASS case ${CASES}"
}

OK_LINE='check_retired_phrases: OK'
NOTE='**Update (2026-09-17, PR #292):** the fact changed; here is the record.'

# --- the real table, read once: SAMPLE[i] / REASON[i], 1-based --------------
SAMPLE=()
REASON=()
while IFS=$'\t' read -r re sample reason; do
    [[ "${re}" == \#* || -z "${re}" ]] && continue
    SAMPLE+=("${sample}")
    REASON+=("${reason}")
done < "${TABLE}"
ROWS="${#SAMPLE[@]}"
sample_of() { printf '%s' "${SAMPLE[$(($1 - 1))]}"; }
reason_of() { printf '%s' "${REASON[$(($1 - 1))]}"; }

# Rows this self-test addresses by number.  If the table is reordered, move
# these, do not delete them: each case needs a phrase of a known shape.
R_CANARY=1    # the --all exit code       (used for the block/wrap cases)
R_MANUAL=3    # "manual canary run"       (the tools/ scope case)
R_PIN118=7    # the deleted save_data pin (the campaigns/ scope case)

make_tree() {  # every root the gate insists on, each holding clean prose
    local root="$1"
    mkdir -p "${root}/docs" "${root}/.claude/skills/og" \
             "${root}/campaigns/x" "${root}/tools/x_mapgen" \
             "${root}/scripts/media" "${root}/tests"
    printf 'The canary runs in CI and exits 0 on a healthy tree.\n' \
        > "${root}/docs/ok.md"
    printf 'plain notes\n' > "${root}/docs/ok.txt"
    printf '# A skill with nothing stale in it.\n' \
        > "${root}/.claude/skills/og/SKILL.md"
    printf 'A campaign ledger.\n' > "${root}/campaigns/x/README.md"
    printf '// a mapgen comment\n' > "${root}/tools/x_mapgen/main.cpp"
    printf '// a mapgen header\n' > "${root}/tools/x_mapgen/main.h"
    printf '# a script README\n' > "${root}/scripts/README.md"
    : > "${root}/README.md"
    : > "${root}/AGENTS.md"
    : > "${root}/CLAUDE.md"
}

# --- case 1: a clean tree is certified clean --------------------------------
make_tree "${tmp}/c1"
run_check "${tmp}/c1"
expect_rc 1 0
expect_stdout 1 "${OK_LINE}"
pass

# --- case 2: an un-annotated statement is rc 1, NAMED, with its reason ------
cp -a "${tmp}/c1" "${tmp}/c2"
{
    printf 'A paragraph about the canary.\n'
    printf 'Two lines of context first.\n'
    printf 'The design says %s.\n' "$(sample_of "${R_CANARY}")"
} > "${tmp}/c2/docs/x.md"
run_check "${tmp}/c2"
expect_rc 2 1
expect_stdout 2 'docs/x.md:3'
expect_stdout 2 "$(sample_of "${R_CANARY}")"
expect_stdout 2 "$(reason_of "${R_CANARY}")"
expect_no_stdout 2 "${OK_LINE}"
expect_stderr 2 'without a dated Update note'
pass

# --- case 3: a dated note in the SAME block accepts the statement -----------
cp -a "${tmp}/c2" "${tmp}/c3"
printf '%s\n' "${NOTE}" >> "${tmp}/c3/docs/x.md"
run_check "${tmp}/c3"
expect_rc 3 0
expect_stdout 3 "${OK_LINE}"
pass

# --- case 4: the note must be in the same block, not merely in the file -----
cp -a "${tmp}/c2" "${tmp}/c4"
{
    printf '\n'
    printf '%s\n' "${NOTE}"
} >> "${tmp}/c4/docs/x.md"
run_check "${tmp}/c4"
expect_rc 4 1
expect_stdout 4 'docs/x.md:3'
expect_no_stdout 4 "${OK_LINE}"
pass

# --- case 5: a markdown table row is its own block --------------------------
# One note in row 2's Errata cell must not launder row 1's stale cell.
cp -a "${tmp}/c1" "${tmp}/c5"
{
    printf '| D12 | wording | %s |\n' "$(sample_of "${R_CANARY}")"
    printf '| Errata | pin map | %s |\n' "${NOTE}"
} > "${tmp}/c5/docs/t.md"
run_check "${tmp}/c5"
expect_rc 5 1
expect_stdout 5 'docs/t.md:1'
expect_no_stdout 5 "${OK_LINE}"
pass

# --- case 6: campaign READMEs and mapgen sources are prose roots too --------
cp -a "${tmp}/c1" "${tmp}/c6"
printf 'Level 14: %s, so the ledger says.\n' "$(sample_of "${R_PIN118}")" \
    > "${tmp}/c6/campaigns/x/README.md"
printf '// the checklist: %s\n' "$(sample_of "${R_MANUAL}")" \
    > "${tmp}/c6/tools/x_mapgen/main.cpp"
run_check "${tmp}/c6"
expect_rc 6 1
expect_stdout 6 'campaigns/x/README.md:1'
expect_stdout 6 'tools/x_mapgen/main.cpp:1'
expect_no_stdout 6 "${OK_LINE}"
pass

# --- case 7: tests/ and scripts/*.sh are deliberately out of scope ----------
# Their comments describe the BASE tree a regression test was written against
# (tests/integration/test_glad_hud.cpp is the live example) and must keep the
# old wording verbatim.
cp -a "${tmp}/c1" "${tmp}/c7"
printf '// %s -- what the base tree did\n' "$(sample_of "${R_CANARY}")" \
    > "${tmp}/c7/tests/x.cpp"
printf '# %s -- the before half of the capture\n' "$(sample_of "${R_CANARY}")" \
    > "${tmp}/c7/scripts/media/x.sh"
run_check "${tmp}/c7"
expect_rc 7 0
expect_stdout 7 "${OK_LINE}"
pass

# --- case 8: a missing scan root is "cannot run" (2), never a vacuous pass --
cp -a "${tmp}/c1" "${tmp}/c8"
rm -rf "${tmp}/c8/.claude/skills"
run_check "${tmp}/c8"
expect_rc 8 2
expect_stderr 8 'cannot find'
expect_no_stdout 8 "${OK_LINE}"
pass

# --- case 9: a missing table is "cannot run" too ----------------------------
run_check "${tmp}/c1" "${tmp}/no_such_table.txt"
expect_rc 9 2
expect_stderr 9 'cannot find'
expect_no_stdout 9 "${OK_LINE}"
pass

# --- case 10: a row that is not <regex> TAB <sample> TAB <reason> ------------
# Skipping it would mean a rule silently stops running; it is rc 2.
printf 'exits with no tabs at all\n' > "${tmp}/bad_table.txt"
run_check "${tmp}/c1" "${tmp}/bad_table.txt"
expect_rc 10 2
expect_stderr 10 'malformed rule'
expect_no_stdout 10 "${OK_LINE}"
pass

# --- case 11: the older in-tree marker is accepted as an annotation ---------
# docs/camp-controls-design.md:157's shape.
cp -a "${tmp}/c1" "${tmp}/c11"
{
    printf 'The design says %s.\n' "$(sample_of "${R_CANARY}")"
    printf '**[SUPERSEDED — issue #241.]** it exits 0 now.\n'
} > "${tmp}/c11/docs/s.md"
run_check "${tmp}/c11"
expect_rc 11 0
expect_stdout 11 "${OK_LINE}"
pass

# --- case 12: every row of the real table has teeth on its own sample -------
cp -a "${tmp}/c1" "${tmp}/c12"
[[ "${ROWS}" -eq 22 ]] || {
    echo "FAIL case 12: the table has ${ROWS} rule rows, expected 22 --" >&2
    echo "  a row was added or deleted; update this number (and the case" >&2
    echo "  numbering above) in the same commit." >&2
    exit 1
}
for (( i = 1; i <= ROWS; i++ )); do
    printf '%s\n' "$(sample_of "${i}")" > "${tmp}/c12/docs/row${i}.md"
done
run_check "${tmp}/c12"
expect_rc 12 1
for (( i = 1; i <= ROWS; i++ )); do
    expect_stdout 12 "docs/row${i}.md:1"
done
expect_no_stdout 12 "${OK_LINE}"
pass

# --- case 13: a phrase wrapped across a line break is still a hit -----------
cp -a "${tmp}/c1" "${tmp}/c13"
WRAP="$(sample_of "${R_CANARY}")"
HEAD_HALF="${WRAP% *}"
TAIL_HALF="${WRAP##* }"
{
    printf 'The design snapshot says %s\n' "${HEAD_HALF}"
    printf '%s, which was true then.\n' "${TAIL_HALF}"
} > "${tmp}/c13/docs/w.md"
run_check "${tmp}/c13"
expect_rc 13 1
expect_stdout 13 'docs/w.md:1-2'
expect_no_stdout 13 "${OK_LINE}"
# and the same wrapped pair, annotated in its block, is accepted
printf '%s\n' "${NOTE}" >> "${tmp}/c13/docs/w.md"
run_check "${tmp}/c13"
expect_rc 13 0
expect_stdout 13 "${OK_LINE}"
pass

echo "test_check_retired_phrases: ${CASES}/${CASES} PASS"
