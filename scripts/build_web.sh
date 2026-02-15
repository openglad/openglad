#!/usr/bin/env bash
#
# Build script for Emscripten/WebAssembly target.
# Uses the project's CMake preset to avoid duplicated source/flag drift.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

if ! command -v emcmake >/dev/null 2>&1; then
    echo "ERROR: emcmake not found in PATH. Source emsdk_env.sh first."
    exit 1
fi

cmake --preset web-emscripten
cmake --build --preset web-emscripten --target play -j"$(nproc)"

echo "Web build complete: dist/play.html"
