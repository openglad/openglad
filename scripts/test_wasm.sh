#!/usr/bin/env bash
#
# Build and run WASM functionality tests via Node.js.
# Requires: emsdk sourced (emcc in PATH), node in PATH.
#
# Usage: ./scripts/test_wasm.sh
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check prerequisites (exit 77 = CTest SKIP)
if ! command -v emcc >/dev/null 2>&1; then
    echo "SKIP: emcc not found. Source emsdk_env.sh to enable this test."
    exit 77
fi

if ! command -v node >/dev/null 2>&1; then
    echo "SKIP: node not found."
    exit 77
fi

BUILD_DIR="${PROJECT_ROOT}/build/wasm-tests"
mkdir -p "$BUILD_DIR"

echo "=== Building WASM sim test ==="
emcc -std=c++20 -O1 \
    -I"${PROJECT_ROOT}/include" \
    -o "${BUILD_DIR}/test_wasm_sim.js" \
    "${PROJECT_ROOT}/tests/wasm/test_wasm_sim.cpp" \
    "${PROJECT_ROOT}/src/sim/simulator.cpp" \
    -sEXIT_RUNTIME=1 \
    -sENVIRONMENT=node

echo "=== Running WASM sim test via Node.js ==="
node "${BUILD_DIR}/test_wasm_sim.js"

echo ""
echo "=== WASM test passed ==="
