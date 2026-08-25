#!/usr/bin/env bash
# Guard the fade-ownership rule (#237): every fade is classified.
#
# `fadeblack()` is the only way the window goes to (or comes back from) black,
# and the ownership rule says a screen fades its OWN last frame out at its own
# exit, while that frame is still the render buffer. Ad-hoc fade calls sprinkled
# at door sites are exactly how the "hard cut to black, then fade-in" class of
# bug kept coming back: a clear() or a stale redraw lands between the outgoing
# screen's last present and somebody else's fade-out, and the fade blends black
# into black.
#
# So the set of places allowed to call fadeblack() is small, deliberate, and
# checked in: scripts/fadeblack_sites.txt, one function per line with HOW MANY
# fades it makes and the reason. A new call fails the build until somebody
# classifies it — including a call added next to an existing, classified fade
# in the same function (the count changes), which is exactly where a door-site
# fade tends to get planted.
#
# Sites are keyed by path + enclosing function + count, never by line number,
# so that edits elsewhere in a file do not rot the list.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT}/src"
ALLOWLIST="${ROOT}/scripts/fadeblack_sites.txt"

if [[ ! -d "${SRC_DIR}" ]]; then
    echo "ERROR: cannot find ${SRC_DIR}" >&2
    exit 2
fi
if [[ ! -f "${ALLOWLIST}" ]]; then
    echo "ERROR: cannot find ${ALLOWLIST}" >&2
    exit 2
fi

# Prints "<path>::<enclosing function>" once per fadeblack() call. Comments
# and string literals are stripped first, so commented-out and merely-mentioned
# fades do not count; the definition line of a function itself named fadeblack
# (screen::fadeblack, sdl_video::fadeblack) is a definition, not a call. A
# function definition is an identifier followed by a balanced parameter list
# followed by `{` (rather than `;`), which distinguishes it from a declaration
# and from a multi-line call.
read -r -d '' SITE_KEY_AWK <<'AWK' || true
{
    raw[NR] = $0
    line = $0
    if (in_block) {
        idx = index(line, "*/")
        if (idx == 0) { code[NR] = ""; next }
        line = substr(line, idx + 2)
        in_block = 0
    }
    gsub(/"(\\.|[^"\\])*"/, "\"\"", line)
    gsub(/'(\\.|[^'\\])*'/, "''", line)
    gsub(/\/\*([^*]|\*+[^*\/])*\*+\//, " ", line)
    line_comment = index(line, "//")
    block_open = index(line, "/*")
    if (line_comment > 0 && (block_open == 0 || line_comment < block_open))
        line = substr(line, 1, line_comment - 1)
    else if (block_open > 0) {
        line = substr(line, 1, block_open - 1)
        in_block = 1
    }
    code[NR] = line
}
END {
    n = NR
    for (i = 1; i <= n; i++) {
        name = definition_name(i)
        if (name != "")
            current = name
        if (name ~ /(^|::)fadeblack$/)
            continue
        line = code[i]
        while (match(line, /(^|[^A-Za-z0-9_])fadeblack[ \t]*\(/)) {
            if (current == "")
                current = "<file scope>"
            print rel "::" current
            line = substr(line, RSTART + RLENGTH)
        }
    }
}

# Returns the function name defined at line i, or "" if line i does not open a
# function definition.
function definition_name(i,   t, depth, j, prefix, rest, name, ch, k, seen) {
    t = code[i]
    if (t ~ /^[ \t]*$/) return ""
    if (raw[i] ~ /^[ \t]*#/) return ""
    sub(/^[ \t]*\}?[ \t]*/, "", t)
    # A definition starts with a return type, a class name or a `~`; anything
    # else (`&& live->mouse_on()) {`, `, foo(x)`) is a continuation line.
    if (t !~ /^[A-Za-z_~]/) return ""
    if (t ~ /^(if|for|while|switch|return|else|do|catch|case|default|new|delete|sizeof|throw)([^A-Za-z0-9_]|$)/)
        return ""
    if (index(t, "(") == 0) return ""

    prefix = substr(t, 1, index(t, "(") - 1)
    sub(/[ \t]+$/, "", prefix)
    if (match(prefix, /(~?[A-Za-z_][A-Za-z0-9_]*)(::~?[A-Za-z_][A-Za-z0-9_]*)*$/) == 0)
        return ""
    name = substr(prefix, RSTART, RLENGTH)
    if (name ~ /^(operator)$/) return ""

    # Walk forward from the parameter list until it balances, then look at what
    # follows: `{` means definition, `;` means declaration or call statement.
    depth = 0
    rest = ""
    for (j = i; j <= NR && j <= i + 12; j++) {
        seen = (j == i) ? substr(code[j], index(code[j], "(")) : code[j]
        for (k = 1; k <= length(seen); k++) {
            ch = substr(seen, k, 1)
            if (ch == "(") depth++
            else if (ch == ")") {
                depth--
                if (depth == 0) { rest = substr(seen, k + 1); break }
            }
        }
        if (depth == 0) break
    }
    if (depth != 0) return ""
    # A `)` right after the parameter list means the call was nested inside an
    # enclosing expression, so this was never a definition.
    if (rest ~ /^[ \t]*\)/) return ""

    # `rest` is the tail of the closing line; keep reading until `{` or `;`.
    for (; j <= NR && j <= i + 12; ) {
        gsub(/[ \t]+/, "", rest)
        if (rest != "") {
            # An initializer list `: member(x)` still leads to a definition.
            sub(/^:[^;{]*/, "", rest)
            if (index(rest, ";") > 0 && (index(rest, "{") == 0 || index(rest, ";") < index(rest, "{")))
                return ""
            if (index(rest, "{") > 0) return name
            if (rest != "") {
                # const / noexcept / override / trailing return type: keep going.
                if (rest !~ /^[A-Za-z_>&*-]/) return ""
            }
        }
        j++
        if (j > NR) return ""
        rest = code[j]
    }
    return ""
}
AWK

status=0
FOUND_KEYS_FILE="$(mktemp)"
trap 'rm -f "${FOUND_KEYS_FILE}"' EXIT

# One line per call, then "<path>::<function>=<count>" per function.
{
    while IFS= read -r file; do
        rel="${file#"${ROOT}"/}"
        awk -v rel="${rel}" "${SITE_KEY_AWK}" "${file}"
    done < <(grep -rlE '(^|[^A-Za-z0-9_])fadeblack[[:space:]]*\(' "${SRC_DIR}" | sort)
} | sort | uniq -c | awk '{ print $2 "=" $1 }' | sort > "${FOUND_KEYS_FILE}"

ALLOWED_KEYS_FILE="$(mktemp)"
trap 'rm -f "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}"' EXIT
sed -E 's/#.*//; s/[[:space:]]+$//; s/^[[:space:]]+//' "${ALLOWLIST}" \
    | { grep -v '^$' || true; } | sort -u > "${ALLOWED_KEYS_FILE}"

# A key is "<path>::<function>=<count>". Three ways to disagree: a function
# that fades but is not listed, a listed function whose fade count changed,
# and a listed function that no longer fades (or no longer exists).
FOUND_ONLY="$(comm -23 "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}")"
ALLOWED_ONLY="$(comm -13 "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}")"

UNLISTED=""
MISMATCH=""
STALE=""
# Keys never contain "=" except before the count, so field 1 is the function.
listed_count() { awk -F= -v fn="$1" '$1 == fn { print $2 }' "${ALLOWED_KEYS_FILE}"; }
found_count()  { awk -F= -v fn="$1" '$1 == fn { print $2 }' "${FOUND_KEYS_FILE}"; }
while IFS= read -r key; do
    [[ -z "${key}" ]] && continue
    fn="${key%=*}"
    listed="$(listed_count "${fn}")"
    if [[ -n "${listed}" ]]; then
        MISMATCH+="  ${fn}: found ${key#*=} fadeblack() call(s), allowlist says ${listed}"$'\n'
    else
        UNLISTED+="  ${key}"$'\n'
    fi
done <<< "${FOUND_ONLY}"
while IFS= read -r key; do
    [[ -z "${key}" ]] && continue
    fn="${key%=*}"
    if [[ -z "$(found_count "${fn}")" ]]; then
        STALE+="  ${key}"$'\n'
    fi
done <<< "${ALLOWED_ONLY}"

if [[ -n "${UNLISTED}" ]]; then
    echo "ERROR: unclassified fadeblack() call site(s):" >&2
    printf '%s' "${UNLISTED}" >&2
    status=1
fi

if [[ -n "${MISMATCH}" ]]; then
    echo "ERROR: fadeblack() call count changed in classified function(s):" >&2
    printf '%s' "${MISMATCH}" >&2
    status=1
fi

if [[ -n "${STALE}" ]]; then
    echo "ERROR: allowlisted fadeblack() call site(s) that no longer exist:" >&2
    printf '%s' "${STALE}" >&2
    status=1
fi

if [[ "${status}" -ne 0 ]]; then
    cat >&2 <<EOF

Fade ownership (#237): a screen fades its own last frame out at its own exit,
while that frame is still the render buffer. Ad-hoc fades at door sites are how
"hard cut to black, then fade in" keeps coming back — a clear() or a stale
redraw lands between the last present and the fade-out, and the fade blends
black into black.

Before adding a fade, check whether the runner already owns it:
og::ui::ScreenFadeScope (run_menu_screen) and og::ui::LegacyMenuFade bracket
every menu screen already. If the site genuinely needs its own fade, add its
key with the reason to:

  ${ALLOWLIST}

Keys are "<path>::<enclosing function>=<count>" — the count is how many
fadeblack() calls that function makes, so a fade added beside an existing one
has to be classified too. The errors above print the exact key.
EOF
    exit 2
fi

TOTAL_CALLS="$(awk -F= '{ s += $2 } END { print s + 0 }' "${FOUND_KEYS_FILE}")"
echo "OK: ${TOTAL_CALLS} classified fadeblack() call(s) in $(wc -l < "${FOUND_KEYS_FILE}") function(s)."
