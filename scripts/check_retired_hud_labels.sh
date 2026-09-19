#!/usr/bin/env bash
# Retired HUD row labels must stay retired in prose, too.
#
# THE PROCEDURE, with the 2026 rename as its worked example.
#
# (a) Why a label gets banned.  PR #292 commit 0464c673 gave the counter box
#     one fit rule and one grammar for every row, so the wave countdown now
#     prints "WAVE: {s}s" (it used to print "NEXT WAVE: {s}s") and the foe row
#     prints "FOES: {a}+{w}".  The strings live in src/interface/score_panel.cpp.
#     A README, a mapgen comment or a test comment that still spells the old
#     upper-case label is not a stale opinion -- it is a capitalised NAME for a
#     row that no longer exists, which is the worst kind of stale doc: the
#     reader takes it for the literal text on screen, goes looking for it, and
#     finds a different row.  So each retired label gets a row in RETIRED below
#     and this gate fails on any new occurrence.
#
# (b) The match is CASE-SENSITIVE on purpose.  Only the upper-case label is
#     banned.  The lower-case CONCEPT is ordinary English and stays legal --
#     src/interface/score_panel.cpp's B5 comment says "a countdown to the next
#     wave", which is true, is not a label, and is one of the comments 0464c673
#     moved verbatim with its code.  A case-insensitive gate would ban that
#     sentence and every honest "the next wave of reinforcements".
#
# (c) The match is WRAP-AWARE.  Three of the stale sites this gate was written
#     for had the label split across a line break -- "... show in the NEXT" /
#     "  WAVE HUD." in a README, and "... — the NEXT" / "    // WAVE HUD keeps"
#     in a C++ comment -- so `grep -n 'NEXT WAVE'` saw none of them, and a
#     single-line sed would have "fixed" the file while leaving the label
#     behind.  Each candidate line is therefore also joined with the next one
#     after stripping that line's leading whitespace and an optional //, #, *
#     or -- comment marker.  A hit that spans the break is reported as
#     `path:N-N+1:`; a hit on one line is reported as `path:N:`.
#
# (d) How to retire the NEXT label, and how to keep a historical mention.
#     To retire one: add a row to RETIRED -- the awk ERE that matches it, a
#     cheap fixed string for the `grep -rIl` prefilter, and the shipped text
#     that replaced it (printed in the error so the next author knows what to
#     write instead).  To keep a mention that is deliberately historical -- a
#     test comment whose SUBJECT is what the base tree used to print -- add a
#     `path=count` row to scripts/retired_hud_label_sites.txt.  The count is
#     how many hits that file holds, so a NEW stale mention added beside an
#     allowlisted one changes the count and still fails; that is the whole
#     point of counting instead of listing bare paths.
#
# (e) This script and its self-test are excluded by basename: the label above
#     is this gate's documentation, and the self-test's trees are its fixtures.
#     (The self-test builds its fixture strings from halves anyway.)
#
# (f) Exit codes, per the scan-root ruling that check_no_std_regex.sh follows:
#     0 = scanned and clean, 1 = violation (every offending line named on
#     stderr), 2 = could not run (a missing scan root or a missing allowlist).
#     There is no "skip the roots that happen to exist" filter, because that is
#     how a gate certifies a tree it never read.
#
# Wired as a ctest entry (cmake/OpenGladTests.cmake, beside
# check_script_roots_selftest), not as a build dependency: READMEs, skills and
# mapgen comments are not build inputs, so there is nothing to hang it off.
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
ALLOWLIST="${ROOT}/scripts/retired_hud_label_sites.txt"

# Every tracked place prose lives.  No present[] filtering: a root that is not
# there means this gate cannot answer the question, not that the answer is yes.
ROOTS=(
    src
    include
    tests
    tools
    scripts
    docs
    cmake
    packs
    campaigns
    web
    relay
    .claude/skills
    README.md
    AGENTS.md
    CLAUDE.md
)

for d in "${ROOTS[@]}"; do
    if [[ ! -e "${ROOT}/${d}" ]]; then
        echo "ERROR: cannot find ${ROOT}/${d} — refusing to pass vacuously" >&2
        exit 2
    fi
done
if [[ ! -f "${ALLOWLIST}" ]]; then
    echo "ERROR: cannot find ${ALLOWLIST} — refusing to pass vacuously" >&2
    exit 2
fi
cd "${ROOT}"

# The retired labels, one row each (parallel arrays, awk-ERE | prefilter | what
# ships instead).  Four rows today; a future rename adds a row, not a script.
# Rows 2-4 are the #306 untheme of the Multiplayer Arenas campaign: the index
# page's title lost its game count and its per-campaign cover, and a level is
# an ARENA, not a field.  Every residual mention of the three was rewritten in
# that PR -- none of them is allowlisted, and none of them may be spelled in
# scripts/retired_phrases.txt either, which this gate also scans.
RETIRED_RE=('NEXT[-[:space:]]*WAVE'
            'SEVEN[[:space:]]+GAMES'
            'THE[[:space:]]+BOOK[[:space:]]+OF[[:space:]]+[A-Z]'
            'FIELD: [A-Z]')
RETIRED_PREFILTER=('NEXT'
                   'SEVEN'
                   'BOOK'
                   'FIELD:')
RETIRED_SHIPPED=('WAVE: {s}s  (src/interface/score_panel.cpp)'
                 'GAMES  (campaign_picker.lua)'
                 'GAMES'
                 'ARENA: <arena>  (campaign_picker.lua)')

# mawk-safe: no 3-argument match(), no interval braces, no gensub.  Lines are
# stored first so the joined-pair test can look ahead.  A pair is only joined
# when the NEXT line is not itself a hit -- otherwise one stale label spanning
# two lines would be reported twice.
read -r -d '' SCAN_AWK <<'AWK' || true
{ raw[NR] = $0 }
END {
    for (i = 1; i <= NR; i++) {
        if (raw[i] ~ re) {
            printf "%s:%d: %s\n", rel, i, raw[i]
            continue
        }
        if (i < NR && raw[i + 1] !~ re) {
            tail = raw[i + 1]
            sub(/^[ \t]*(\/\/|#|\*|--)?[ \t]*/, "", tail)
            if ((raw[i] " " tail) ~ re)
                printf "%s:%d-%d: %s / %s\n", rel, i, i + 1, raw[i], tail
        }
    }
}
AWK

HITS_FILE="$(mktemp)"
FOUND_COUNTS_FILE="$(mktemp)"
ALLOWED_COUNTS_FILE="$(mktemp)"
trap 'rm -f "${HITS_FILE}" "${FOUND_COUNTS_FILE}" "${ALLOWED_COUNTS_FILE}"' EXIT

: > "${HITS_FILE}"
for (( ri = 0; ri < ${#RETIRED_RE[@]}; ri++ )); do
    # -I skips the binary level data under campaigns/*/scen; the prefilter is a
    # plain fixed word so the expensive awk runs over a handful of candidates.
    # ONE word, always: this grep runs BEFORE the awk that joins wrapped line
    # pairs, so a two-word prefilter ('BOOK OF') would hide exactly the
    # wrapped and double-spaced spellings the regex was written to catch.
    while IFS= read -r file; do
        [[ -n "${file}" ]] || continue
        awk -v rel="${file}" -v re="${RETIRED_RE[ri]}" "${SCAN_AWK}" "${file}" \
            >> "${HITS_FILE}"
    # The excludes go BEFORE the `--`: after it every word is a file operand,
    # which is how this gate first reported its own header as a violation.
    done < <(grep -rIl \
                 --exclude=check_retired_hud_labels.sh \
                 --exclude=test_check_retired_hud_labels.sh \
                 --exclude-dir=__pycache__ \
                 -- "${RETIRED_PREFILTER[ri]}" "${ROOTS[@]}" 2>/dev/null \
                 | sort || true)
done

# "<path>=<hits in that path>", one row per path, both sides sorted the same
# way so comm can take the difference (the check_fadeblack_sites.sh shape).
awk -F: 'NF { print $1 }' "${HITS_FILE}" | sort | uniq -c \
    | awk '{ print $2 "=" $1 }' | sort > "${FOUND_COUNTS_FILE}"
sed -E 's/#.*//; s/[[:space:]]+$//; s/^[[:space:]]+//' "${ALLOWLIST}" \
    | { grep -v '^$' || true; } | sort -u > "${ALLOWED_COUNTS_FILE}"

FOUND_ONLY="$(comm -23 "${FOUND_COUNTS_FILE}" "${ALLOWED_COUNTS_FILE}")"
ALLOWED_ONLY="$(comm -13 "${FOUND_COUNTS_FILE}" "${ALLOWED_COUNTS_FILE}")"

listed_count() { awk -F= -v p="$1" '$1 == p { print $2 }' "${ALLOWED_COUNTS_FILE}"; }
found_count()  { awk -F= -v p="$1" '$1 == p { print $2 }' "${FOUND_COUNTS_FILE}"; }

UNLISTED=""
MISMATCH=""
STALE=""
OFFENDING=""
while IFS= read -r key; do
    [[ -z "${key}" ]] && continue
    path="${key%=*}"
    OFFENDING+="${path}"$'\n'
    listed="$(listed_count "${path}")"
    if [[ -n "${listed}" ]]; then
        MISMATCH+="  ${path}: found ${key#*=}, allowlist says ${listed}"$'\n'
    else
        UNLISTED+="  ${key}"$'\n'
    fi
done <<< "${FOUND_ONLY}"
while IFS= read -r key; do
    [[ -z "${key}" ]] && continue
    path="${key%=*}"
    if [[ -z "$(found_count "${path}")" ]]; then
        STALE+="  ${key}"$'\n'
    fi
done <<< "${ALLOWED_ONLY}"

status=0
if [[ -n "${UNLISTED}" || -n "${MISMATCH}" ]]; then
    # Name every offending LINE, not just the file: the wrapped sites are the
    # ones a reader cannot find by eye, and `path:N-N+1:` is the whole reason
    # this gate is not a grep.
    echo "ERROR: a retired HUD label appears in prose:" >&2
    while IFS= read -r path; do
        [[ -z "${path}" ]] && continue
        awk -v p="${path}:" 'index($0, p) == 1' "${HITS_FILE}" >&2
    done <<< "${OFFENDING}"
    status=1
fi
if [[ -n "${UNLISTED}" ]]; then
    echo "ERROR: file(s) with a retired HUD label and no allowlist row:" >&2
    printf '%s' "${UNLISTED}" >&2
fi
if [[ -n "${MISMATCH}" ]]; then
    echo "ERROR: retired HUD label count changed in allowlisted file(s):" >&2
    printf '%s' "${MISMATCH}" >&2
fi
if [[ -n "${STALE}" ]]; then
    echo "ERROR: allowlisted file(s) that no longer hold a retired HUD label" >&2
    echo "       (delete the row -- an allowlist entry that guards nothing is" >&2
    echo "       an allowlist entry nobody will notice going wrong):" >&2
    printf '%s' "${STALE}" >&2
    status=1
fi

if [[ "${status}" -ne 0 ]]; then
    {
        echo
        echo "The counter box rows are:"
        for (( ri = 0; ri < ${#RETIRED_SHIPPED[@]}; ri++ )); do
            echo "    ${RETIRED_SHIPPED[ri]}"
        done
        cat <<EOF
Write the shipped spelling instead (the campaign READMEs and mapgen comments
say "the WAVE HUD" or "the WAVE countdown"; a comment quoting the row's text
quotes it as it prints).  A mention that is deliberately HISTORICAL -- a test
or capture comment whose subject is what the base tree used to print -- earns a
row in:

  ${ALLOWLIST}

as "<path>=<number of hits in that file>  # why".  See the header of this
script for the whole procedure, including how to retire the next label.
EOF
    } >&2
    exit 1
fi

ALLOWED_ROWS="$(wc -l < "${ALLOWED_COUNTS_FILE}" | tr -d '[:space:]')"
echo "Retired HUD label check: OK (${ALLOWED_ROWS} historical mention(s) allowlisted)"
