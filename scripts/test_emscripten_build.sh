#!/usr/bin/env bash
#
# Verify that the Emscripten/WebAssembly build compiles successfully.
# This script is intended to be run by CTest as a build verification test.
#
# Prerequisites:
#   - Emscripten toolchain installed (via emsdk or a system package)
#   - ninja-build installed
#
# Exit codes:
#   0 = build succeeded
#   1 = build failed
#   77 = skipped (Emscripten toolchain not available)
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

TOOLCHAIN_FILE=""
if [[ -n "${EMSDK:-}" ]] && [[ -f "$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]]; then
    TOOLCHAIN_FILE="$EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
elif [[ -n "${EMSCRIPTEN:-}" ]] && [[ -f "$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake" ]]; then
    TOOLCHAIN_FILE="$EMSCRIPTEN/cmake/Modules/Platform/Emscripten.cmake"
elif [[ -f "/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]]; then
    TOOLCHAIN_FILE="/usr/share/emscripten/cmake/Modules/Platform/Emscripten.cmake"
fi

if [[ -z "$TOOLCHAIN_FILE" ]]; then
    echo "SKIP: Emscripten toolchain file not found for emcc=$EMCC_BIN"
    exit 77
fi

BUILD_DIR="$PROJECT_ROOT/build/ctest-emscripten-build"
EM_CONFIG_FILE="$BUILD_DIR/.emscripten"
EM_CACHE_DIR="$BUILD_DIR/emcache"
EM_PORTS_DIR="$EM_CACHE_DIR/ports"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 1)"

mkdir -p "$BUILD_DIR" "$EM_CACHE_DIR" "$EM_PORTS_DIR"

export EM_CONFIG="$EM_CONFIG_FILE"
export EM_CACHE="$EM_CACHE_DIR"
export EM_PORTS="$EM_PORTS_DIR"

echo "Generating local Emscripten config at $EM_CONFIG_FILE"
rm -f "$EM_CONFIG_FILE"
EM_CONFIG="$EM_CONFIG_FILE" "$EMCC_BIN" --generate-config >/dev/null

sed -i '/^CACHE = /d;/^PORTS = /d' "$EM_CONFIG_FILE"

# --generate-config guesses system tool locations (LLVM_ROOT=/usr/bin). Under
# an emsdk install the toolchain lives in $EMSDK/upstream and node ships inside
# the SDK, so the isolated config must be pointed there explicitly or every
# compile fails with "cannot find /usr/bin/llvm-ar".
if [[ -n "${EMSDK:-}" ]] && [[ -d "$EMSDK/upstream/bin" ]]; then
    sed -i '/^LLVM_ROOT = /d;/^BINARYEN_ROOT = /d' "$EM_CONFIG_FILE"
    cat >>"$EM_CONFIG_FILE" <<EOF
LLVM_ROOT = '$EMSDK/upstream/bin'
BINARYEN_ROOT = '$EMSDK/upstream'
EOF
    emsdk_node="$(ls -d "$EMSDK"/node/*/bin/node 2>/dev/null | head -1)"
    if [[ -n "$emsdk_node" ]]; then
        sed -i '/^NODE_JS = /d' "$EM_CONFIG_FILE"
        printf "NODE_JS = '%s'\n" "$emsdk_node" >>"$EM_CONFIG_FILE"
    fi
fi
printf '\n' >>"$EM_CONFIG_FILE"
cat >>"$EM_CONFIG_FILE" <<EOF
CACHE = '$EM_CACHE_DIR' # directory
PORTS = '$EM_PORTS_DIR' # directory
EOF

echo "=== Emscripten build verification ==="
echo "emcc version: $("$EMCC_BIN" --version | head -1)"
echo "toolchain file: $TOOLCHAIN_FILE"
echo "build dir: $BUILD_DIR"
echo "EM_CONFIG: $EM_CONFIG"
echo "EM_CACHE: $EM_CACHE"
echo "EM_PORTS: $EM_PORTS"

cd "$PROJECT_ROOT"
cmake --fresh -S "$PROJECT_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" 2>&1
cmake --build "$BUILD_DIR" --target play -j"$JOBS" 2>&1

echo "=== Emscripten build succeeded ==="
ls -lh dist/play.html dist/play.js dist/play.wasm 2>/dev/null || true
