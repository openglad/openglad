#!/usr/bin/env bash
# No code in src/ or include/ may name luaL_error: raise through script_raise.
#
# luaL_error is declared `int`, so `return luaL_error(L, ...)` compiles to a
# return path the error never takes, and a bare call needs a dead
# `return x; // unreachable` after it. Each of those is a line the coverage
# gate counts and no test can reach. src/gameplay/script/script_raise.h
# declares the [[noreturn]] replacement; script_raise.cpp carries luaL_error's
# body statement for statement (it does not call luaL_error), so messages are
# byte-identical and no file in the tree is exempt from this check.
#
# The gate is the IDENTIFIER in code, not in prose: comments (// and /* */),
# string and character literals, and raw strings are stripped before matching,
# so a comment explaining luaL_error's semantics stays legal. scripts/ is not
# scanned, so this script's own text cannot trip it. There is no allowlist
# marker.
#
# Wired into the build as a dependency of og_gameplay (CMakeLists.txt, beside
# check_lua_statement_lines), so a reintroduced call fails every build.
#
# A missing scan root or a failed scan exits 2; 1 is reserved for a violation;
# an optional first argument overrides the repo root.
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
for d in src include src/gameplay/script; do
    if [[ ! -d "${ROOT}/${d}" ]]; then
        echo "ERROR: cannot find ${ROOT}/${d} — refusing to pass vacuously" >&2
        exit 2
    fi
done
cd "${ROOT}"

# Collect the scanned files first: an empty list would be a vacuous pass.
# (A read loop, not mapfile: the macOS release lane runs /bin/bash 3.2.)
files=()
while IFS= read -r f; do
    files+=("${f}")
done < <(find src include -type f \
    \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.inc' -o -name '*.c' \) \
    | LC_ALL=C sort)
if [[ ${#files[@]} -eq 0 ]]; then
    echo "ERROR: no C/C++ sources under ${ROOT}/src or ${ROOT}/include" >&2
    exit 2
fi

# Strip comments and literals (state carried across lines for /* */ and raw
# strings), then report every line whose remaining code names luaL_error.
set +e
awk '
FNR == 1 { in_block = 0; in_raw = 0; raw_end = "" }
{
    line = $0; code = ""; i = 1; n = length(line)
    while (i <= n) {
        if (in_block) {
            j = index(substr(line, i), "*/")
            if (j == 0) { i = n + 1; break }
            i += j + 1; in_block = 0; continue
        }
        if (in_raw) {
            j = index(substr(line, i), raw_end)
            if (j == 0) { i = n + 1; break }
            i += j - 1 + length(raw_end); in_raw = 0; code = code " "; continue
        }
        c = substr(line, i, 1); c2 = substr(line, i, 2)
        if (c2 == "//") break
        if (c2 == "/*") { in_block = 1; i += 2; continue }
        if (c == "R" && substr(line, i + 1, 1) == "\"" &&
            (i == 1 || substr(line, i - 1, 1) !~ /[A-Za-z0-9_]/ ||
             substr(line, i - 1, 1) ~ /[uUL8]/)) {
            rest = substr(line, i + 2); p = index(rest, "(")
            if (p > 0) {
                raw_end = ")" substr(rest, 1, p - 1) "\""
                in_raw = 1; i += 2 + p; continue
            }
        }
        if (c == "\"" || c == "\047") {
            q = c; i++
            while (i <= n) {
                d = substr(line, i, 1)
                if (d == "\\") { i += 2; continue }
                i++
                if (d == q) break
            }
            code = code " "; continue
        }
        code = code c; i++
    }
    if (code ~ /(^|[^A-Za-z0-9_])luaL_error([^A-Za-z0-9_]|$)/) {
        print FILENAME ":" FNR ": " $0
        found = 1
    }
}
END { exit(found ? 1 : 0) }
' "${files[@]}"
rc=$?
set -e

case "${rc}" in
    0)
        ;;
    1)
        echo "ERROR: luaL_error named in code above. Raise Lua errors through" >&2
        echo "       og::script::script_raise (src/gameplay/script/script_raise.h):" >&2
        echo "       it is [[noreturn]], so no dead return path follows the call." >&2
        exit 1
        ;;
    *)
        echo "ERROR: scan failed (rc=${rc}); cannot certify the tree" >&2
        exit 2
        ;;
esac

echo "check_lua_error_sites: luaL_error is raised only through script_raise (${#files[@]} files scanned)"
