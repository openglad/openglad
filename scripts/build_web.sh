#!/usr/bin/env bash
#
# Build script for Emscripten/WebAssembly target.
# Uses the project's CMake preset to avoid duplicated source/flag drift.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_common.sh"

require_command emcmake "Source emsdk_env.sh first."

cmake --preset web-emscripten
cmake --build --preset web-emscripten --target play -j"$(nproc)"

echo "Web build complete: dist/play.html"
