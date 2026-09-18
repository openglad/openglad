#!/usr/bin/env bash
# No translation unit may include <regex>.
#
# libstdc++'s <regex> instantiates std::function machinery that, under
# GCC 15.2 with the nix cc-wrapper's injected -O2 (NIX_DEBUG=1 shows
# "extra flags before: ... -O2 ... -D_FORTIFY_SOURCE=3") plus the ci-asan
# preset's ASan/UBSan, trips -Werror=maybe-uninitialized inside
# bits/std_function.h. That killed the local ci-asan build of three test
# TUs (tests/unit/test_version.cpp, tests/integration/test_gparser_unit.cpp,
# tests/curses/test_curses_mount_guard.cpp) while CI's Ubuntu GCC built them
# fine — a divergence nobody sees until someone runs the sanitizer lane
# locally. All three now match by hand; this gate keeps the header out so
# the lane cannot re-break silently.
#
# The gate is the INCLUDE LINE only: prose may say std::regex freely, and a
# hand-written matcher's comment usually has to. scripts/ is not scanned, so
# this script's own text cannot trip it.
#
# Wired into the build as a dependency of og_game_test (CMakeLists.txt,
# beside check_injector_settles), so a reintroduced include fails the test
# build rather than waiting for someone to run this by hand.
#
# A missing scan root or a failed grep exits 2; 1 is reserved for a violation;
# an optional first argument overrides the repo root (the self-test uses it).
set -euo pipefail

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
for d in src include tests tools; do
    if [[ ! -d "${ROOT}/${d}" ]]; then
        echo "ERROR: cannot find ${ROOT}/${d} — refusing to pass vacuously" >&2
        exit 2
    fi
done
cd "${ROOT}"

# grep's status is captured, never consumed by an `if`: GNU grep returns 2 when
# a scan root is missing EVEN IF it also printed a match, so an `if grep ...`
# gate prints the violation and then declares the tree clean.
set +e
grep -rnE --include='*.cpp' --include='*.h' --include='*.hpp' --include='*.inc' \
    '^[[:space:]]*#[[:space:]]*include[[:space:]]*<regex>' src include tests tools
rc=$?
set -e

case "${rc}" in
    0)
        echo "ERROR: <regex> is banned (libstdc++ <regex> + GCC 15 + -O2 + sanitizers" >&2
        echo "       breaks the ci-asan build). Match by hand; see the files above." >&2
        exit 1
        ;;
    1)
        ;;
    *)
        echo "ERROR: grep failed (rc=${rc}); cannot certify the tree" >&2
        exit 2
        ;;
esac

echo "check_no_std_regex: no TU includes <regex>"
