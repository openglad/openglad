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
# checked in: scripts/fadeblack_sites.txt, one site per line with the reason it
# is allowed to fade. A new call fails the build until somebody classifies it.
#
# Sites are keyed by path + enclosing function, never by line number, so that
# edits elsewhere in a file do not rot the list.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT}/src"
ALLOWLIST="${ROOT}/scripts/fadeblack_sites.txt"

# The two definition sites: screen::fadeblack (the pure delegate) and
# sdl_video::fadeblack (the implementation, plus the FadeBetween prose around
# it). Everything else in src/ is a caller and must be classified.
EXCLUDED_FILES=(
    "src/interface/screen.cpp"
    "src/platform/sdl/video_sdl.cpp"
)

if [[ ! -d "${SRC_DIR}" ]]; then
    echo "ERROR: cannot find ${SRC_DIR}" >&2
    exit 2
fi
if [[ ! -f "${ALLOWLIST}" ]]; then
    echo "ERROR: cannot find ${ALLOWLIST}" >&2
    exit 2
fi

# Keys a call site to "<path>::<enclosing function>". Comments and string
# literals are stripped first, so commented-out and merely-mentioned fades do
# not count. A function definition is an identifier followed by a balanced
# parameter list followed by `{` (rather than `;`), which distinguishes it from
# a declaration and from a multi-line call.
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
        if (code[i] ~ /(^|[^A-Za-z0-9_])fadeblack[ \t]*\(/) {
            if (current == "")
                current = "<file scope>"
            print rel "::" current
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

{
    while IFS= read -r file; do
        rel="${file#"${ROOT}"/}"
        skip=0
        for excluded in "${EXCLUDED_FILES[@]}"; do
            [[ "${rel}" == "${excluded}" ]] && skip=1
        done
        [[ "${skip}" -eq 1 ]] && continue
        awk -v rel="${rel}" "${SITE_KEY_AWK}" "${file}"
    done < <(grep -rlE '(^|[^A-Za-z0-9_])fadeblack[[:space:]]*\(' "${SRC_DIR}" | sort)
} | sort -u > "${FOUND_KEYS_FILE}"

ALLOWED_KEYS_FILE="$(mktemp)"
trap 'rm -f "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}"' EXIT
sed -E 's/#.*//; s/[[:space:]]+$//; s/^[[:space:]]+//' "${ALLOWLIST}" \
    | { grep -v '^$' || true; } | sort -u > "${ALLOWED_KEYS_FILE}"

UNLISTED="$(comm -23 "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}")"
STALE="$(comm -13 "${FOUND_KEYS_FILE}" "${ALLOWED_KEYS_FILE}")"

if [[ -n "${UNLISTED}" ]]; then
    echo "ERROR: unclassified fadeblack() call site(s):" >&2
    while IFS= read -r key; do
        echo "  ${key}" >&2
    done <<< "${UNLISTED}"
    status=1
fi

if [[ -n "${STALE}" ]]; then
    echo "ERROR: allowlisted fadeblack() call site(s) that no longer exist:" >&2
    while IFS= read -r key; do
        echo "  ${key}" >&2
    done <<< "${STALE}"
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

Keys are "<path>::<enclosing function>" — the same key the errors above print.
EOF
    exit 2
fi

echo "OK: $(wc -l < "${FOUND_KEYS_FILE}") classified fadeblack() call site(s)."
