#!/usr/bin/env bash
# scripts/fuzz.sh — Build and run OpenGlad fuzz harnesses locally.
#
# Usage:
#   ./scripts/fuzz.sh [target] [duration_seconds]
#
# Examples:
#   ./scripts/fuzz.sh                   # Build only
#   ./scripts/fuzz.sh scenario 60       # Fuzz scenario parser for 60 seconds
#   ./scripts/fuzz.sh all 300           # Fuzz all targets for 300s each
#
# Requires: clang/clang++, cmake, ninja, libsdl2-dev, libsdl2-mixer-dev

set -euo pipefail

# Disable leak detection: SDL2 has known benign leaks that would
# cause false positives in fuzz runs.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build/ci-fuzz"
CORPUS_DIR="$PROJECT_DIR/fuzz/corpus"
CRASH_DIR="$PROJECT_DIR/fuzz/crashes"

TARGETS=(fuzz_scenario fuzz_savefile)
CORPUS_MAP=(
    "fuzz_scenario:scenario"
    "fuzz_savefile:savefile"
)

TARGET="${1:-}"
DURATION="${2:-0}"

# Verify clang is available
if ! command -v clang++ &>/dev/null; then
    echo "Error: clang++ not found. Install clang to use libFuzzer."
    exit 1
fi

# Build
echo "=== Configuring fuzz build ==="
cmake --preset ci-fuzz

echo "=== Building fuzz targets ==="
cmake --build --preset ci-fuzz

if [ -z "$TARGET" ] || [ "$DURATION" = "0" ] && [ "$TARGET" != "all" ]; then
    echo ""
    echo "Build complete. Fuzz targets are in: $BUILD_DIR/"
    echo ""
    echo "Available targets: ${TARGETS[*]}"
    echo ""
    echo "To run a fuzzer:"
    echo "  $0 scenario 60     # Fuzz scenario parser for 60 seconds"
    echo "  $0 all 300         # Fuzz all targets for 300s each"
    exit 0
fi

mkdir -p "$CRASH_DIR"

run_fuzzer() {
    local target="$1"
    local corpus_name="$2"
    local duration="$3"

    echo ""
    echo "=== Fuzzing $target for ${duration}s ==="
    local corpus_path="$CORPUS_DIR/$corpus_name"
    mkdir -p "$corpus_path"

    "$BUILD_DIR/$target" \
        "$corpus_path" \
        -max_total_time="$duration" \
        -artifact_prefix="$CRASH_DIR/${target}_" \
        -print_final_stats=1 \
        || {
            echo "CRASH FOUND in $target! Artifacts saved to $CRASH_DIR/"
            return 1
        }
}

get_corpus_for_target() {
    local target="$1"
    for mapping in "${CORPUS_MAP[@]}"; do
        local key="${mapping%%:*}"
        local val="${mapping#*:}"
        if [ "$key" = "$target" ]; then
            echo "$val"
            return
        fi
    done
    echo "$target"
}

FAILED=0

if [ "$TARGET" = "all" ]; then
    for t in "${TARGETS[@]}"; do
        corpus=$(get_corpus_for_target "$t")
        run_fuzzer "$t" "$corpus" "$DURATION" || FAILED=1
    done
else
    # Resolve short name to full target name
    FULL_TARGET="fuzz_${TARGET}"
    found=false
    for t in "${TARGETS[@]}"; do
        if [ "$t" = "$FULL_TARGET" ] || [ "$t" = "$TARGET" ]; then
            FULL_TARGET="$t"
            found=true
            break
        fi
    done
    if [ "$found" = false ]; then
        echo "Unknown target: $TARGET"
        echo "Available: ${TARGETS[*]}"
        exit 1
    fi
    corpus=$(get_corpus_for_target "$FULL_TARGET")
    run_fuzzer "$FULL_TARGET" "$corpus" "$DURATION" || FAILED=1
fi

if [ "$FAILED" -ne 0 ]; then
    echo ""
    echo "FUZZING FOUND CRASHES — check $CRASH_DIR/"
    exit 1
fi

echo ""
echo "All fuzzing runs completed without crashes."
