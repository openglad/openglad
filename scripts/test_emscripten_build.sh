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
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

EMCC_BIN="${EMCC:-}"
if [[ -z "$EMCC_BIN" ]]; then
    EMCC_BIN="$(command -v emcc || true)"
fi

if [[ -z "$EMCC_BIN" ]]; then
    echo "ERROR: emcc not found. Source emsdk_env.sh or set EMCC."
    exit 1
fi

echo "=== Emscripten build verification ==="
echo "emcc version: $("$EMCC_BIN" --version | head -1)"

cd "$PROJECT_ROOT"
cmake --preset web-emscripten 2>&1
cmake --build --preset web-emscripten --target play -j"$(nproc)" 2>&1

echo "=== Emscripten build succeeded ==="
ls -lh dist/play.html dist/play.js dist/play.wasm 2>/dev/null || true
