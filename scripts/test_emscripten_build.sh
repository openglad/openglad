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

if ! command -v emcc >/dev/null 2>&1; then
    echo "SKIP: emcc not found in PATH. Source emsdk_env.sh to enable this test."
    exit 77
fi

echo "=== Emscripten build verification ==="
echo "emcc version: $(emcc --version | head -1)"

cd "$PROJECT_ROOT"
cmake --preset web-emscripten 2>&1
cmake --build --preset web-emscripten --target play -j"$(nproc)" 2>&1

echo "=== Emscripten build succeeded ==="
ls -lh dist/play.html dist/play.js dist/play.wasm 2>/dev/null || true
