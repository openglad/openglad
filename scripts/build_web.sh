#!/usr/bin/env bash
#
# Build script for Emscripten/WebAssembly target.
# Uses the project's CMake preset to avoid duplicated source/flag drift.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_common.sh"

require_command emcmake "Source emsdk_env.sh, or use 'nix develop' (it ships emscripten)."

# The preset locates the toolchain file at $EMSDK/upstream/emscripten/... which
# is the emsdk layout. A distro/nix emscripten has no EMSDK and puts the same
# file under <prefix>/share/emscripten/. Derive a compatible EMSDK from emcc's
# real location so one preset serves both installs.
if [[ -z "${EMSDK:-}" ]]; then
    emcc_real="$(readlink -f "$(command -v emcc)")"
    em_root="$(dirname "$(dirname "${emcc_real}")")"   # <prefix>/bin/emcc -> <prefix>
    for candidate in "${em_root}/share/emscripten" "${em_root}"; do
        if [[ -f "${candidate}/cmake/Modules/Platform/Emscripten.cmake" ]]; then
            # The preset appends /upstream/emscripten, so EMSDK must be the
            # directory two levels above the emscripten root. A symlink farm in
            # the build tree gives us that shape without touching the store.
            shim="${SCRIPT_DIR}/../build/.emsdk-shim"
            mkdir -p "${shim}/upstream"
            ln -sfn "${candidate}" "${shim}/upstream/emscripten"
            export EMSDK="$(cd "${shim}" && pwd)"
            echo "build_web: derived EMSDK=${EMSDK} -> ${candidate}"
            break
        fi
    done
    if [[ -z "${EMSDK:-}" ]]; then
        echo "build_web: could not locate Emscripten.cmake near ${emcc_real}" >&2
        exit 1
    fi
fi

# emcc caches to its own (read-only in nix) prefix unless told otherwise.
export EM_CACHE="${EM_CACHE:-${SCRIPT_DIR}/../build/.emscripten-cache}"

cmake --preset web-emscripten
cmake --build --preset web-emscripten --target play -j"$(nproc)"

echo "Web build complete: dist/play.html"
