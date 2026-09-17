#!/usr/bin/env bash
# A fact this repo's prose states, and that a PR made false, may not stay in
# the tree as a current statement.
#
# (a) What a "retired phrase" is.  PR #292 moved the parity mutation canary
#     into CI, made `run_mutation_canary.sh --all` exit 0 on a healthy tree,
#     deleted the `kMut_save_corrupt` pin (so src/resources/save_data.cpp
#     carries no canary pin at all), renamed the three capture scenes and
#     replaced the per-row right-align idiom with one fit rule.  Nine short
#     strings in the tree's prose asserted the opposite.  Each of them gets a
#     row in scripts/retired_phrases.txt -- an ERE, a sample line the regex
#     must flag, and the reason printed beside every hit -- and this gate
#     fails on any occurrence that is not annotated.
#
# (b) The acceptance rule is the ANNOTATED BLOCK, not the file.  Yan's rule is
#     "no statement in the tree may still describe the pre-PR state as
#     current", and a statement carrying a dated correction does not describe
#     it as current -- it records what was designed and says what changed.  So
#     a hit passes when its BLOCK also holds one of
#         Update (YYYY-MM-DD ... | [SUPERSEDED ... | **Superseded ( ...
#     (the phase-4 format plus the two conventions already in the tree:
#     docs/camp-controls-design.md's `**[SUPERSEDED -- issue #241.]**`
#     paragraph and docs/game-modes.md's `**Superseded (`, blockquote).  That
#     is what lets a dated design snapshot -- docs/tower-triple-design.md is
#     pinned to `fd097693` by its own line 3 -- keep its text verbatim, while
#     a living doc or skill just gets rewritten and needs no note at all.
#
#     A block is a run of consecutive non-blank lines, and a markdown table
#     row (`|...`) is a block of its own.  Both halves matter: the blank line
#     is what stops an Errata paragraph from laundering a stale statement two
#     paragraphs above it, and the table-row rule is what stops a note in one
#     row's cell from laundering a stale cell three rows up.
#
#     Accepted residual, stated rather than hidden: two stale statements in
#     ONE block share one note.  Nothing in the tree has that shape today
#     (the two table cells are separate rows; the two numbered lists have one
#     hit each), and the alternative -- pairing each note with the statement
#     it corrects -- needs an allowlist keyed by line number or by a copy of
#     the sentence, which is the shape that rots.
#
# (c) The match is WRAP-AWARE, using the join rule of
#     scripts/check_retired_hud_labels.sh (which was written for three sites
#     whose label was split across a line break and which `grep -n` could not
#     see).  Each line is also matched joined to the next one, after stripping
#     that line's leading whitespace and an optional //, #, * or -- marker.  A
#     hit that spans the break is reported as `path:N-N+1:`, and both lines
#     count as part of line N's block.  Without this, re-wrapping a paragraph
#     at 74 columns silently disarms the gate.
#
# (d) What is scanned, and what is NOT.  Scanned: docs/ (md + txt),
#     .claude/skills/ (md + txt), campaigns/**/*.md, scripts/**/*.md,
#     tools/**/*.{cpp,h}, README.md, AGENTS.md, CLAUDE.md -- campaign READMEs
#     and mapgen comments are level-design prose and a stale fact reads the
#     same there as in docs/.
#     NOT scanned, on purpose:
#       - tests/ and scripts/*.sh, because a regression test's comment
#         DESCRIBES the base tree it was written against and must keep the old
#         wording verbatim: tests/integration/test_glad_hud.cpp says "the base
#         tree right-aligns them to rm-2 with max(lm, rm - 2 - 6*len)", which
#         is the historical fact the test exists to pin.
#       - src/ and include/, because those comments are code: the heritage
#         rules and the component checks own them.
#       - tests/parity/golden/DRIFT_LEDGER.md, whose whole job is to record
#         retired facts (it is where kMut_save_corrupt's deletion is written
#         down).
#     Gating those roots would need a per-path count allowlist, and this gate
#     deliberately has none -- the annotation IS the allowlist, it lives beside
#     the sentence it excuses, and it cannot rot out of sync with a line
#     number.  This script and its self-test are .sh files, so they are not in
#     the scanned set and need no self-exclusion.
#
# (e) Exit codes, per the scan-root ruling check_no_std_regex.sh follows:
#     0 = scanned and clean, 1 = a retired phrase with no dated note (every
#     hit named on stdout), 2 = could not run (a missing scan root, a missing
#     or malformed table, a regex this system's grep rejects).  There is no
#     "scan the roots that happen to exist" arm: that is how a gate certifies
#     a tree it never read.
#
# The rule's home is .claude/skills/openglad-pr-workflow/SKILL.md, "Pre-merge
# sweep", item 5.  Wired as a ctest entry (cmake/OpenGladTests.cmake, beside
# check_retired_hud_labels), not as a build dependency: prose is not a build
# input.
#
# Usage: check_retired_phrases.sh [ROOT] [TABLE]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="${1:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
TABLE="${2:-${SCRIPT_DIR}/retired_phrases.txt}"

if [[ ! -f "${TABLE}" ]]; then
    echo "ERROR: cannot find ${TABLE} -- refusing to pass vacuously" >&2
    exit 2
fi
for d in docs .claude/skills campaigns tools scripts; do
    if [[ ! -d "${ROOT}/${d}" ]]; then
        echo "ERROR: cannot find ${ROOT}/${d} -- refusing to pass vacuously" >&2
        exit 2
    fi
done
for f in README.md AGENTS.md CLAUDE.md; do
    if [[ ! -f "${ROOT}/${f}" ]]; then
        echo "ERROR: cannot find ${ROOT}/${f} -- refusing to pass vacuously" >&2
        exit 2
    fi
done
cd "${ROOT}"

tmp="$(mktemp -d)"
trap 'rm -rf -- "${tmp}"' EXIT INT TERM

# --- the table: <regex> TAB <sample> TAB <reason> ---------------------------
# A row with fewer than three fields is a malformed rule, not a rule to skip:
# a sample-less row cannot be self-tested and a reason-less hit cannot be
# acted on, so both are "cannot run".
awk -F'\t' '
    { line = $0; sub(/#.*/, "", line) }
    line ~ /^[ \t]*$/ { next }
    { if (NF < 3 || $1 == "") { printf "MALFORMED %d\n", FNR; next }
      # The sample column ($2) belongs to the self-test, not to the gate;
      # the gate only insists that the row HAS one.
      printf "%s\t%s\n", $1, $3 > REGEXES }
' REGEXES="${tmp}/rules" "${TABLE}" > "${tmp}/parse"

if grep -q '^MALFORMED ' "${tmp}/parse"; then
    while IFS=' ' read -r _ n; do
        echo "ERROR: malformed rule at ${TABLE}:${n} (want <regex> TAB <sample> TAB <reason>)" >&2
    done < <(grep '^MALFORMED ' "${tmp}/parse")
    exit 2
fi
if [[ ! -s "${tmp}/rules" ]]; then
    echo "ERROR: ${TABLE} holds no rules -- refusing to pass vacuously" >&2
    exit 2
fi
cut -f1 "${tmp}/rules" > "${tmp}/regexes"
RULE_COUNT="$(wc -l < "${tmp}/regexes" | tr -d '[:space:]')"

# A regex this grep rejects would make every later grep return 2, which reads
# exactly like "no match" to an unchecked `if`.  Compile them against nothing.
set +e
grep -E -f "${tmp}/regexes" /dev/null
rc=$?
set -e
if [[ "${rc}" -eq 2 ]]; then
    echo "ERROR: bad regex in ${TABLE} -- grep refused to compile the rule set" >&2
    exit 2
fi

# --- the prose file set ------------------------------------------------------
{
    find docs .claude/skills -type f \( -name '*.md' -o -name '*.txt' \)
    find campaigns scripts -type f -name '*.md'
    find tools -type f \( -name '*.cpp' -o -name '*.h' \)
    printf '%s\n' README.md AGENTS.md CLAUDE.md
} | sort > "${tmp}/files"
FILE_COUNT="$(wc -l < "${tmp}/files" | tr -d '[:space:]')"

# --- prefilter: one joined view of every file, one grep ----------------------
# Single-line grep cannot see a wrapped phrase, so the prefilter reads the same
# joined view the scanner does.  The filename is appended AFTER a tab so that a
# rule can never match a path and then be mistaken for a hit; over-selecting a
# candidate is harmless (the scanner decides), under-selecting is not.
read -r -d '' JOIN_AWK <<'AWK' || true
FNR == 1 { have = 0 }
{
    if (have) {
        tail = $0
        sub(/^[ \t]*(\/\/|#|\*|--)?[ \t]*/, "", tail)
        print prev " " tail "\t" FILENAME
    }
    print $0 "\t" FILENAME
    prev = $0
    have = 1
}
AWK

xargs -r -d '\n' awk "${JOIN_AWK}" < "${tmp}/files" > "${tmp}/joined"
set +e
grep -E -f "${tmp}/regexes" "${tmp}/joined" > "${tmp}/prefiltered"
rc=$?
set -e
if [[ "${rc}" -eq 2 ]]; then
    echo "ERROR: grep failed while prefiltering the prose roots" >&2
    exit 2
fi
awk -F'\t' '{ print $NF }' "${tmp}/prefiltered" | sort -u > "${tmp}/candidates"

# --- the block / marker / hit pass ------------------------------------------
# mawk-safe: no 3-argument match(), no interval braces, no gensub.  Lines are
# stored first because the block id of line i depends on the blank lines before
# it and the joined-pair test looks one line ahead.
read -r -d '' SCAN_AWK <<'AWK' || true
BEGIN { FS = "\t" }
NR == FNR { nre++; RE[nre] = $1; REASON[nre] = $2; next }
{ raw[FNR] = $0; n = FNR }
END {
    blk = 1
    for (i = 1; i <= n; i++) {
        if (raw[i] ~ /^[ \t]*$/) { blk++; block[i] = 0; continue }
        # A table row is one statement: it gets a block of its own so that a
        # note in a later row cannot annotate it, and its own note cannot
        # annotate the prose above it either.
        if (raw[i] ~ /^[ \t]*\|/) { blk++; block[i] = blk; blk++; continue }
        block[i] = blk
    }
    for (i = 1; i <= n; i++) {
        if (block[i] == 0) continue
        if (raw[i] ~ /Update \(20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]/ ||
            raw[i] ~ /\[SUPERSEDED/ ||
            raw[i] ~ /\*\*Superseded \(/)
            annotated[block[i]] = 1
    }
    for (i = 1; i <= n; i++) {
        tail = ""
        if (i < n) {
            tail = raw[i + 1]
            sub(/^[ \t]*(\/\/|#|\*|--)?[ \t]*/, "", tail)
        }
        for (r = 1; r <= nre; r++) {
            if (raw[i] ~ RE[r]) {
                report(i, i, raw[i], r)
                continue
            }
            # Only when the NEXT line is not itself a hit: otherwise one
            # phrase spanning a break would be reported twice.
            if (i < n && raw[i + 1] !~ RE[r] && (raw[i] " " tail) ~ RE[r])
                report(i, i + 1, raw[i] " / " tail, r)
        }
    }
    exit 0
}
function report(a, b, text, r,   loc) {
    if (block[a] == 0 || annotated[block[a]]) return
    loc = (a == b) ? a "" : a "-" b
    sub(/^[ \t]+/, "", text)
    printf "%s:%s: %s <- %s\n", rel, loc, text, REASON[r]
}
AWK

printf '%s\n' "${SCAN_AWK}" > "${tmp}/scan.awk"
: > "${tmp}/hits"
while IFS= read -r file; do
    [[ -n "${file}" ]] || continue
    awk -v rel="${file}" -f "${tmp}/scan.awk" "${tmp}/rules" "${file}" \
        >> "${tmp}/hits"
done < "${tmp}/candidates"

if [[ -s "${tmp}/hits" ]]; then
    cat "${tmp}/hits"
    {
        echo "ERROR: retired phrase(s) in prose without a dated Update note."
        echo "  A living doc or skill is REWRITTEN: make the sentence true, no note needed."
        echo "  A dated design snapshot keeps its text and gets a"
        echo "  '**Update (YYYY-MM-DD, PR #N):** ...' note in the SAME paragraph, list"
        echo "  item or table cell (no blank line between; a table row's note goes in"
        echo "  that row).  The rule lives in .claude/skills/openglad-pr-workflow/SKILL.md,"
        echo "  'Pre-merge sweep' item 5; when your PR retires a phrase, add its row"
        echo "  (regex, sample, reason) to ${TABLE}."
    } >&2
    exit 1
fi

echo "check_retired_phrases: OK -- ${FILE_COUNT} files scanned, ${RULE_COUNT} rules"
