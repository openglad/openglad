#!/usr/bin/env bash
#
# Verify that the Emscripten/WebAssembly build compiles successfully.
# This script is intended to be run by CTest as a build verification test.
#
# Prerequisites:
#   - emsdk installed and sourced (emcc/emcmake in PATH)
#   - ninja-build installed
#
# Exit codes:
#   0 = build succeeded
#   1 = build failed
#   77 = skipped (emsdk not available)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EMCC_BIN="${EMCC:-}"
if [[ -z "$EMCC_BIN" ]]; then
    EMCC_BIN="$(command -v emcc || true)"
fi

if [[ -z "$EMCC_BIN" ]]; then
    echo "SKIP: emcc not found in PATH. Source emsdk_env.sh to enable this test."
    exit 77
fi

EMSDK_ROOT="${EMSDK:-}"
if [[ -z "$EMSDK_ROOT" ]]; then
    EMCC_DIR="$(cd "$(dirname "$EMCC_BIN")" && pwd -P)"
    CANDIDATE_EMSDK="$(cd "$EMCC_DIR/../.." && pwd -P)"
    if [[ -f "$CANDIDATE_EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]]; then
        EMSDK_ROOT="$CANDIDATE_EMSDK"
    fi
fi

if [[ -z "$EMSDK_ROOT" ]]; then
    echo "SKIP: EMSDK is not set and could not be inferred from emcc path."
    exit 77
fi

if [[ ! -f "$EMSDK_ROOT/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]]; then
    echo "SKIP: Emscripten toolchain file not found under EMSDK=$EMSDK_ROOT"
    exit 77
fi

export EMSDK="$EMSDK_ROOT"

echo "=== Emscripten build verification ==="
echo "emcc version: $("$EMCC_BIN" --version | head -1)"
echo "emsdk root: $EMSDK_ROOT"

cd "$PROJECT_ROOT"
cmake --preset web-emscripten 2>&1
cmake --build --preset web-emscripten --target play -j"$(nproc)" 2>&1

echo "=== Emscripten build succeeded ==="
ls -lh dist/play.html dist/play.js dist/play.wasm 2>/dev/null || true
