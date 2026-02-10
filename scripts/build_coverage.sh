#!/bin/bash
#
# Coverage build + report generation (gcov/lcov)
#
# Produces:
#   build-coverage/              (instrumented build)
#   coverage/lcov.info           (raw capture)
#   coverage/lcov.info.cleaned   (filtered)
#   coverage/html/index.html     (HTML report)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

if ! command -v lcov >/dev/null 2>&1; then
    echo "ERROR: lcov not found. Install with:"
    echo "  sudo apt-get update && sudo apt-get install lcov"
    exit 1
fi

if ! pkg-config --exists sdl2 SDL2_mixer; then
    echo "ERROR: Missing dependencies. Install with:"
    echo "  sudo apt-get install libsdl2-dev libsdl2-mixer-dev"
    exit 1
fi

BUILD_DIR="$PROJECT_ROOT/build-coverage"
COV_DIR="$PROJECT_ROOT/coverage"

rm -rf "$COV_DIR"
mkdir -p "$COV_DIR"

echo "Building coverage-instrumented test binary..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DENABLE_COVERAGE=ON
cmake --build "$BUILD_DIR" --target openglad_test -j"$(nproc)"

echo ""
echo "Zeroing counters..."
lcov --quiet --directory "$BUILD_DIR" --zerocounters || true

echo ""
echo "Running tests (writes *.gcda)..."
# Run from project root so assets resolve as in normal test runs.
"$BUILD_DIR/openglad_test"

echo ""
echo "Capturing coverage..."
lcov --quiet --capture --directory "$BUILD_DIR" --output-file "$COV_DIR/lcov.info"

echo ""
echo "Filtering coverage (exclude external, tests, system headers)..."
lcov --quiet --remove "$COV_DIR/lcov.info" \
    "/usr/*" \
    "*/src/external/*" \
    "*/tests/*" \
    "*/build-*/*" \
    --ignore-errors unused \
    --output-file "$COV_DIR/lcov.info.cleaned"

echo ""
echo "Generating HTML report..."
genhtml --quiet "$COV_DIR/lcov.info.cleaned" --output-directory "$COV_DIR/html"

echo ""
echo "Coverage report generated:"
echo "  $COV_DIR/html/index.html"
