#!/usr/bin/env bash
# The teeth of scripts/check_retired_hud_labels.sh, on temp trees.
#
# That gate exists because three of the stale sites it was written for had the
# retired row label split across a line break -- a README paragraph wrapped
# mid-label, and two C++ comments wrapped after the first word -- so `grep -n`
# found none of them and a single-line sed would have left them behind while
# reporting success. A gate that is not wrap-aware would be a green liar on
# exactly the shape that survived the rename, so the joined-pair match gets
# cases of its own here (a // wrap and a markdown wrap), as does the
# hyphenated spelling.
#
# The rest is the contract: the three exit codes (0 scanned and clean,
# 1 violation NAMED by path:line, 2 could not run), the allowlist's count arms
# (a new mention beside a historical one changes the count; a row that guards
# nothing is stale), the self-exclusion of the two gate scripts, and the
# lower-case concept staying legal.
#
# Temp trees only, and this script never spells the retired label: the fixture
# strings are assembled from halves at run time, so the gate's basename
# exclusion is belt and this is braces.
set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM

out="${tmp}/stdout"
err="${tmp}/stderr"
rc=0
CASES=0

run_check() {  # run_check <root>
    rc=0
    bash "${SCRIPTS_DIR}/check_retired_hud_labels.sh" "$1" >"${out}" 2>"${err}" || rc=$?
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
pass() {  # pass -- numbers the cases so the tally cannot disagree with itself
    CASES=$((CASES + 1))
    echo "PASS case ${CASES}"
}

# The retired label, never written whole in this file.
HALF_A='NEXT'
HALF_B='WAVE'
LBL="${HALF_A} ${HALF_B}"
HYPHENATED="${HALF_A}-${HALF_B}"

OK_LINE='Retired HUD label check: OK'

# The three #306 labels, likewise never written whole in this file.
HALF_GAMES='GAMES'
HALF_BOOK='BOOK'
HALF_FIELD='FIELD'

make_tree() {  # every root the gate scans, plus the two allowlisted fixtures
    local root="$1"
    mkdir -p "${root}/src" "${root}/include" "${root}/tests/integration" \
             "${root}/tools" "${root}/scripts/media" "${root}/docs" \
             "${root}/cmake" "${root}/packs" "${root}/campaigns" \
             "${root}/web" "${root}/relay" "${root}/.claude/skills"
    : > "${root}/README.md"
    : > "${root}/AGENTS.md"
    : > "${root}/CLAUDE.md"
    # Both gate scripts live under scripts/ in the real tree and spell the
    # label in their documentation and fixtures; copying them in proves the
    # basename exclusion, not just that it is written down.
    cp "${SCRIPTS_DIR}/check_retired_hud_labels.sh" "${root}/scripts/"
    cp "${SCRIPTS_DIR}/test_check_retired_hud_labels.sh" "${root}/scripts/"
    {
        echo "# fixture allowlist: <path>=<hits in that file>"
        echo "tests/integration/test_glad_hud.cpp=2   # base-tree descriptions"
        echo "scripts/media/capture_pr292.sh=1        # capture scene's before-half"
    } > "${root}/scripts/retired_hud_label_sites.txt"
    printf '// the base tree right-aligns "%s: 120s" to rm-2\n' "${LBL}" \
        > "${root}/tests/integration/test_glad_hud.cpp"
    printf '// the band the base tree hangs %s into at rm-92\n' "${LBL}" \
        >> "${root}/tests/integration/test_glad_hud.cpp"
    printf '#   the base tree paints "%s: 120s" over the world\n' "${LBL}" \
        > "${root}/scripts/media/capture_pr292.sh"
}

# --- a complete, clean tree is certified clean, and the lower-case concept
#     and the shipped spelling are both legal ---------------------------------
make_tree "${tmp}/c1"
printf 'The WAVE HUD counts the packs down.\n' > "${tmp}/c1/docs/ok.md"
# Reaches the scanner (it has the upper-case first word) and must still pass:
# only the upper-case LABEL is banned, never the English concept.
printf '// The NEXT frame shows a countdown to the next wave, which is\n' \
    > "${tmp}/c1/src/ok.cpp"
printf '// a description, not the name of a row.\n' >> "${tmp}/c1/src/ok.cpp"
run_check "${tmp}/c1"
expect_rc 1 0
expect_stdout 1 "${OK_LINE}"
pass

# --- a single-line occurrence exits 1 and NAMES path:line --------------------
cp -a "${tmp}/c1" "${tmp}/c2"
printf 'The %s HUD is what this row used to be called.\n' "${LBL}" \
    > "${tmp}/c2/docs/x.md"
run_check "${tmp}/c2"
expect_rc 2 1
expect_stderr 2 'docs/x.md:1:'
expect_stderr 2 'retired HUD label'
expect_no_stdout 2 "${OK_LINE}"
pass

# --- the hyphenated, adjectival spelling counts too --------------------------
cp -a "${tmp}/c1" "${tmp}/c3"
mkdir -p "${tmp}/c3/campaigns/foo"
printf 'the throne, and the %s reliefs at ticks 500/800.\n' "${HYPHENATED}" \
    > "${tmp}/c3/campaigns/foo/README.md"
run_check "${tmp}/c3"
expect_rc 3 1
expect_stderr 3 'campaigns/foo/README.md:1:'
expect_no_stdout 3 "${OK_LINE}"
pass

# --- wrapped across a line break with a // comment marker --------------------
# The shape `grep -n` cannot see; reported as path:N-N+1.
cp -a "${tmp}/c1" "${tmp}/c4"
printf '// The sim runs 12 ticks/second, matching the %s\n' "${HALF_A}" \
    > "${tmp}/c4/src/x.cpp"
printf '// %s HUD countdown.\n' "${HALF_B}" >> "${tmp}/c4/src/x.cpp"
run_check "${tmp}/c4"
expect_rc 4 1
expect_stderr 4 'src/x.cpp:1-2:'
expect_no_stdout 4 "${OK_LINE}"
pass

# --- wrapped across a line break in markdown (leading whitespace, no marker) --
cp -a "${tmp}/c1" "${tmp}/c5"
{
    printf 'A paragraph whose first line is innocent.\n'
    printf 'delayed spawns hold level_done open and show in the %s\n' "${HALF_A}"
    printf '  %s HUD.\n' "${HALF_B}"
} > "${tmp}/c5/docs/y.md"
run_check "${tmp}/c5"
expect_rc 5 1
expect_stderr 5 'docs/y.md:2-3:'
expect_no_stdout 5 "${OK_LINE}"
pass

# --- a NEW mention beside allowlisted historical ones changes the count ------
# This is why the allowlist counts instead of listing bare paths.
cp -a "${tmp}/c1" "${tmp}/c6"
printf '// and a third, freshly stale %s mention\n' "${LBL}" \
    >> "${tmp}/c6/tests/integration/test_glad_hud.cpp"
run_check "${tmp}/c6"
expect_rc 6 1
expect_stderr 6 'found 3, allowlist says 2'
expect_no_stdout 6 "${OK_LINE}"
pass

# --- an allowlist row that guards nothing is stale --------------------------
cp -a "${tmp}/c1" "${tmp}/c7"
rm -f "${tmp}/c7/scripts/media/capture_pr292.sh"
run_check "${tmp}/c7"
expect_rc 7 1
expect_stderr 7 'no longer'
expect_stderr 7 'scripts/media/capture_pr292.sh=1'
expect_no_stdout 7 "${OK_LINE}"
pass

# --- a missing scan root is "cannot run" (2), never a vacuous pass ----------
cp -a "${tmp}/c1" "${tmp}/c8"
rm -rf "${tmp}/c8/docs"
run_check "${tmp}/c8"
expect_rc 8 2
expect_stderr 8 'cannot find'
expect_no_stdout 8 "${OK_LINE}"
pass

# --- a missing allowlist is "cannot run" too --------------------------------
cp -a "${tmp}/c1" "${tmp}/c9"
rm -f "${tmp}/c9/scripts/retired_hud_label_sites.txt"
run_check "${tmp}/c9"
expect_rc 9 2
expect_stderr 9 'cannot find'
expect_no_stdout 9 "${OK_LINE}"
pass

# --- every RETIRED_RE row has teeth on its own sample -----------------------
# The phrases gate's self-test case 12 in one sentence: a regex that rots --
# or that this system's awk cannot compile -- must red HERE, not go quietly
# toothless on a clean tree.  The sample column is assembled from halves for
# the same reason the label above is.
cp -a "${tmp}/c1" "${tmp}/c10"
SAMPLES=("${LBL}" "SEVEN ${HALF_GAMES}" "THE ${HALF_BOOK} OF STARS" \
         "${HALF_FIELD}: DUNGEON")
# Counted with awk over the array block, not by halving every quote in a sed
# range: a range ending at /)$/ runs past an array collapsed onto ONE line,
# into the next )-terminated line, and silently doubles the count.  \047 is
# the single quote, which no awk program written in single quotes can spell.
DECLARED_ROWS="$(awk '
    /^RETIRED_RE=\(/            { inside = 1 }
    inside                      { n += gsub(/\047[^\047]*\047/, "") }
    inside && /\)[[:space:]]*$/ { print n; exit }
' "${SCRIPTS_DIR}/check_retired_hud_labels.sh")"
[[ "${DECLARED_ROWS}" -eq "${#SAMPLES[@]}" ]] || {
    echo "FAIL case 10: the gate declares ${DECLARED_ROWS} retired labels," >&2
    echo "  and this case plants ${#SAMPLES[@]} samples -- a row was added or" >&2
    echo "  deleted; add (or drop) its sample here in the same commit." >&2
    exit 1
}
for (( i = 0; i < ${#SAMPLES[@]}; i++ )); do
    printf 'prose that still spells %s today.\n' "${SAMPLES[i]}" \
        > "${tmp}/c10/docs/row$((i + 1)).md"
done
# The joined-pair path, on the one row whose prefilter used to be two words:
# grep -rIl runs BEFORE the awk, so a prefilter of 'BOOK OF' never handed this
# file over and the wrap passed silently.  A single-word prefilter does.
printf 'THE %s\nOF STARS in the old cover.\n' "${HALF_BOOK}" \
    > "${tmp}/c10/docs/row_wrapped.md"
run_check "${tmp}/c10"
expect_rc 10 1
for (( i = 0; i < ${#SAMPLES[@]}; i++ )); do
    expect_stderr 10 "docs/row$((i + 1)).md:1:"
done
expect_stderr 10 'docs/row_wrapped.md:1-2:'
expect_no_stdout 10 "${OK_LINE}"
pass

echo "test_check_retired_hud_labels: ${CASES}/${CASES} PASS"
