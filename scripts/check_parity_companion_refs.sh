#!/usr/bin/env bash
# The old game the goldens are captured from must be reachable from origin.
#
# Every parity golden is a capture from the old game (branch parity-companion;
# tests/parity/golden/DRIFT_LEDGER.md, header). A behaviour port lands there
# as its own commit and the ledger names it. If that commit is never pushed,
# nothing else notices: CI only byte-compares the committed goldens, so it
# stays green while nobody can rebuild the old game that produced them (#349).
#
# What the ledger must look like (the only places this script reads):
#   - exactly one line
#         Old-game commit the goldens are captured from: `<sha>`
#     (the "pin");
#   - every markdown table whose header row has a cell that is exactly
#     `old-game commit`. In those rows that cell holds one or more
#     backticked 7-40 hex shas (separated by spaces, commas or "and"), or
#     `none`, or an em dash. Anything else in the cell is malformed (rc 2):
#     a sha this script cannot see is a sha it cannot check.
# Prose shas elsewhere in the ledger (History, evidence cells) are NOT read:
# most of them are current-game commits or pre-squash ids.
#
# Assertions, all against REF (default origin/parity-companion):
#   (a) every table sha and the pin resolve to commits that are ancestors of
#       REF -- "ported locally, never pushed" is the failure this exists for --
#       and every table sha is an ancestor of the pin (a port the goldens
#       were not captured with is a port nobody recaptured);
#   (b) tests/parity/scenario_table.h is byte-identical to the pin's
#       tools/parity_scenario_table.h (against the PIN, not REF's tip: a
#       table mirror pushed ahead of its PR must not red master);
#   (c) every old-game commit from the e761 rebuild a2d9d470 (inclusive) to
#       the pin that touches src/ is named in an `old-game commit` cell: an
#       old-game behaviour change with no ledger row is a golden nobody can
#       explain.
#
# When REF is absent (no git, not a work tree, a shallow clone, or a clone
# that never fetched parity-companion) the script prints SKIPPED and exits 0,
# unless the ref is required. Likewise a pin or table sha this clone cannot
# resolve (a stale local origin/parity-companion, most often after fetching
# master alone) is a SKIPPED pass that names each sha and says to fetch, unless
# the ref is required; then it is a violation (exit 1), since in CI the ref is
# fresh and an unknown sha is a typo or a commit that was never pushed.
#
# Exit: 0 clean (or skipped), 1 violation (each one named), 2 cannot run
# (missing ledger or table, a pin-line count other than one, no
# `old-game commit` cells, a malformed cell, a git failure, or -- when the ref
# is required -- an absent ref).
#
# Environment:
#   OG_PARITY_COMPANION_REF  the ref to check against
#                            (default origin/parity-companion)
#   OG_PARITY_REQUIRE_REF    1: an absent ref is exit 2 and an unknown sha is
#                            a violation; 0: both are a SKIPPED pass. Default 1
#                            when GITHUB_ACTIONS=true (every workflow checks
#                            out with fetch-depth: 0, which fetches
#                            refs/heads/*, so the ref is there in CI), else 0.
#   OG_PARITY_E761_REBUILD   the first old-game commit check (c) covers
#                            (default a2d9d470, the e761 rebuild); set only by
#                            scripts/test_check_parity_companion_refs.sh for
#                            its temp repository.
#
# Portable to bash 3.2 and POSIX awk (the macOS Release lane): no mapfile,
# readarray, declare -A, awk interval braces, 3-argument match() or \x escapes.
#
# Usage: check_parity_companion_refs.sh [ROOT] [LEDGER]
set -euo pipefail
export LC_ALL=C

# The e761 rebuild ("Rebuild parity companion from e761 baseline"); the
# override exists for the self-test's temp repository only.
E761_REBUILD="${OG_PARITY_E761_REBUILD:-a2d9d470}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${1:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
LEDGER="${2:-${ROOT}/tests/parity/golden/DRIFT_LEDGER.md}"
TABLE="${ROOT}/tests/parity/scenario_table.h"
REF="${OG_PARITY_COMPANION_REF:-origin/parity-companion}"
if [[ -z "${OG_PARITY_REQUIRE_REF:-}" ]]; then
    if [[ "${GITHUB_ACTIONS:-}" == "true" ]]; then
        OG_PARITY_REQUIRE_REF=1
    else
        OG_PARITY_REQUIRE_REF=0
    fi
fi

for f in "${LEDGER}" "${TABLE}"; do
    if [[ ! -f "${f}" ]]; then
        echo "ERROR: cannot find ${f} -- refusing to pass vacuously" >&2
        exit 2
    fi
done

tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM

# --- parse: PIN <sha> <line> / SHA <sha> <line> / MALFORMED <line> <why> ----
read -r -d '' PARSE_AWK <<'AWK' || true
function trim(s) { sub(/^[ \t]+/, "", s); sub(/[ \t]+$/, "", s); return s }
function ishex(s) { return s ~ /^[0-9a-f]+$/ && length(s) >= 7 && length(s) <= 40 }
function cells(line, arr,   n) {
    gsub(/\\\|/, "\001", line)          # an escaped pipe is cell text
    sub(/^[ \t]*\|/, "", line); sub(/\|[ \t]*$/, "", line)
    n = split(line, arr, "|")
    return n
}
{
    if ($0 ~ /^Old-game commit the goldens are captured from: `/) {
        s = $0; sub(/^Old-game commit the goldens are captured from: `/, "", s)
        sub(/`.*$/, "", s)
        if (ishex(s)) print "PIN " s " " NR
        else print "MALFORMED " NR " pin-line"
    }
    istable = ($0 ~ /^[ \t]*\|/)
    if (!istable) { intable = 0; col = 0; next }
    if (!intable) {                      # header row
        intable = 1; col = 0
        n = cells($0, hdr)
        for (i = 1; i <= n; i++) if (trim(hdr[i]) == "old-game commit") col = i
        next
    }
    if (col == 0 || $0 ~ /^[ \t]*\|[ \t:|-]*$/) next   # not ours / separator
    n = cells($0, c)
    cell = trim(c[col])
    if (cell == "none" || cell == "\342\200\224" || cell == "-") next
    rest = cell
    found = 0
    while (match(rest, /`[0-9a-f]+`/)) {
        tok = substr(rest, RSTART + 1, RLENGTH - 2)
        if (!ishex(tok)) { print "MALFORMED " NR " bad-sha"; next }
        print "SHA " tok " " NR
        found++
        rest = substr(rest, 1, RSTART - 1) " " substr(rest, RSTART + RLENGTH)
    }
    gsub(/,|and|[ \t]/, "", rest)
    if (found == 0 || rest != "") print "MALFORMED " NR " cell"
}
AWK
awk "${PARSE_AWK}" "${LEDGER}" > "${tmp}/parse"

if grep -q '^MALFORMED ' "${tmp}/parse"; then
    while read -r _ n why; do
        echo "ERROR: ${LEDGER}:${n}: malformed old-game commit (${why}); want backticked shas, none, or an em dash" >&2
    done < <(grep '^MALFORMED ' "${tmp}/parse")
    exit 2
fi
PIN_COUNT="$(grep -c '^PIN ' "${tmp}/parse" || true)"
SHA_COUNT="$(grep -c '^SHA ' "${tmp}/parse" || true)"
if [[ "${PIN_COUNT}" -ne 1 ]]; then
    echo "ERROR: ${LEDGER} has ${PIN_COUNT} pin lines, want exactly one" >&2
    exit 2
fi
if [[ "${SHA_COUNT}" -eq 0 ]]; then
    echo "ERROR: ${LEDGER} has no 'old-game commit' cells -- refusing to pass vacuously" >&2
    exit 2
fi
PIN="$(awk '$1 == "PIN" { print $2 }' "${tmp}/parse")"

# --- is the ref here at all? -------------------------------------------------
skip() {
    if [[ "${OG_PARITY_REQUIRE_REF}" == "1" ]]; then
        echo "ERROR: $1 (OG_PARITY_REQUIRE_REF=1)" >&2
        exit 2
    fi
    echo "check_parity_companion_refs: SKIPPED -- $1; run 'git fetch origin parity-companion' to check"
    exit 0
}
command -v git >/dev/null 2>&1 || skip "no git on PATH"
git -C "${ROOT}" rev-parse --git-dir >/dev/null 2>&1 || skip "${ROOT} is not a git work tree"
if [[ "$(git -C "${ROOT}" rev-parse --is-shallow-repository)" == "true" ]]; then
    skip "shallow clone: ancestry cannot be proven"
fi
git -C "${ROOT}" rev-parse --verify -q "${REF}^{commit}" >/dev/null || skip "${REF} is not in this clone"

# --- shas this clone does not have --------------------------------------------
# Locally that is almost always a stale ref, so it is a notice and a skip; where
# the ref is required it falls through to (a), which names it as a violation.
: > "${tmp}/unknown"
while read -r _ sha line; do
    git -C "${ROOT}" rev-parse --verify -q "${sha}^{commit}" >/dev/null ||
        echo "${sha} ${line}" >> "${tmp}/unknown"
done < <(grep -E '^(PIN|SHA) ' "${tmp}/parse")
if [[ -s "${tmp}/unknown" && "${OG_PARITY_REQUIRE_REF}" != "1" ]]; then
    while read -r sha line; do
        echo "check_parity_companion_refs: notice -- ${LEDGER##"${ROOT}"/}:${line}: ${sha} is not a commit in this clone"
    done < "${tmp}/unknown"
    echo "check_parity_companion_refs: SKIPPED -- the ledger names old-game commits this clone does not have; run 'git fetch origin parity-companion' to check (a sha still unknown after the fetch is a typo)"
    exit 0
fi

: > "${tmp}/violations"
violation() { echo "$1" >> "${tmp}/violations"; }

# --- (a) ancestry --------------------------------------------------------------
: > "${tmp}/named"
while read -r kind sha line; do
    full="$(git -C "${ROOT}" rev-parse --verify -q "${sha}^{commit}" || true)"
    if [[ -z "${full}" ]]; then
        violation "${LEDGER##"${ROOT}"/}:${line}: ${sha} is not a commit in this clone (never pushed to ${REF}, or a typo)"
        continue
    fi
    [[ "${kind}" == "SHA" ]] && echo "${full}" >> "${tmp}/named"
    set +e
    git -C "${ROOT}" merge-base --is-ancestor "${full}" "${REF}"
    rc=$?
    set -e
    case "${rc}" in
        0) ;;
        1) violation "${LEDGER##"${ROOT}"/}:${line}: ${sha} is not an ancestor of ${REF} (push parity-companion)" ;;
        *) echo "ERROR: git merge-base failed (rc=${rc}) on ${sha}" >&2; exit 2 ;;
    esac
    # A port the ledger names must be IN the old game the goldens came from.
    if [[ "${kind}" == "SHA" ]] &&
       ! git -C "${ROOT}" merge-base --is-ancestor "${full}" "${PIN}" 2>/dev/null; then
        violation "${LEDGER##"${ROOT}"/}:${line}: ${sha} is not an ancestor of the pin ${PIN} (recapture at the new tip and move the pin)"
    fi
done < <(grep -E '^(PIN|SHA) ' "${tmp}/parse")

# --- (b) the scenario table at the pin -------------------------------------------
if git -C "${ROOT}" cat-file -e "${PIN}^{commit}" 2>/dev/null; then
    git -C "${ROOT}" show "${PIN}:tools/parity_scenario_table.h" > "${tmp}/table" || {
        echo "ERROR: ${PIN} has no tools/parity_scenario_table.h" >&2; exit 2; }
    cmp -s "${tmp}/table" "${TABLE}" ||
        violation "tests/parity/scenario_table.h differs from ${PIN}:tools/parity_scenario_table.h (mirror the table into the old game and move the pin)"

    # --- (c) every old-game src/ change is in the ledger ----------------------------
    # ^@ (all parents) rather than ^: the self-test's rebuild is a root commit.
    git -C "${ROOT}" rev-list "${PIN}" --not "${E761_REBUILD}^@" -- src         > "${tmp}/src_raw" || { echo "ERROR: git rev-list failed from ${E761_REBUILD} to ${PIN}" >&2; exit 2; }
    sort "${tmp}/src_raw" > "${tmp}/src_commits"
    sort -u "${tmp}/named" > "${tmp}/named_sorted"
    comm -23 "${tmp}/src_commits" "${tmp}/named_sorted" > "${tmp}/unlisted"
    while read -r full; do
        violation "old-game commit $(git -C "${ROOT}" log -1 --format='%h %s' "${full}") changes src/ and has no 'old-game commit' cell in the ledger"
    done < "${tmp}/unlisted"
fi

if [[ -s "${tmp}/violations" ]]; then
    cat "${tmp}/violations"
    echo "ERROR: the ledger's old-game commits are not all on ${REF} (see above)." >&2
    exit 1
fi
echo "check_parity_companion_refs: OK -- pin ${PIN}, ${SHA_COUNT} ledger shas on ${REF}, table cmp-identical"
