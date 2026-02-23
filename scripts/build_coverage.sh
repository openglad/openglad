#!/bin/bash
#
# Coverage build + report generation (gcov/lcov).
# Thin wrapper around CMake's ci-coverage preset.
#
# Produces:
#   build/ci-coverage/           (instrumented build)
#   coverage/lcov.info           (raw capture)
#   coverage/lcov.info.cleaned   (filtered)
#   coverage/html/index.html     (HTML report)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_common.sh"

require_command lcov "Install with: sudo apt-get update && sudo apt-get install lcov"
require_sdl2

BUILD_DIR="$PROJECT_ROOT/build/ci-coverage"
COV_DIR="$PROJECT_ROOT/coverage"

rm -rf "$COV_DIR"
mkdir -p "$COV_DIR"

echo "Cleaning up leftover test campaigns (prevents hangs on malformed fixtures)..."
# Some tests create campaigns under ~/.openglad/campaigns (e.g. org.openglad.test.*).
# If a prior run was interrupted, malformed fixture campaigns can persist and cause
# picker flows to hang. Remove them before running the suite under coverage.
rm -f "$HOME/.openglad/campaigns/org.openglad.test."*.glad 2>/dev/null || true

echo "Building coverage-instrumented test binaries..."
cmake --preset ci-coverage
cmake --build --preset ci-coverage --target openglad_test og_data_tests og_runtime_tests

echo ""
echo "Zeroing counters..."
# When code changes, stale *.gcda files from previous builds can cause
# libgcov checksum mismatches. Remove them to keep coverage runs reproducible.
find "$BUILD_DIR" -name "*.gcda" -delete 2>/dev/null || true
find "$BUILD_DIR" -name "*.gcov" -delete 2>/dev/null || true
lcov --quiet --directory "$BUILD_DIR" --zerocounters || true

echo ""
echo "Running tests (writes *.gcda)..."
# Run from project root so assets resolve as in normal test runs.
timeout 300 "$BUILD_DIR/openglad_test"
timeout 300 "$BUILD_DIR/og_data_tests"
timeout 300 "$BUILD_DIR/og_runtime_tests"

echo ""
echo "Capturing coverage..."
lcov --quiet --capture --directory "$BUILD_DIR" --output-file "$COV_DIR/lcov.info"

echo ""
echo "Filtering coverage (exclude external, tests, system headers)..."
lcov --quiet --remove "$COV_DIR/lcov.info" \
    "/usr/*" \
    "*/third_party/*" \
    "*/tests/*" \
    "*/build/*" \
    --ignore-errors unused \
    --output-file "$COV_DIR/lcov.info.cleaned"

echo ""
echo "Generating HTML report..."
genhtml --quiet "$COV_DIR/lcov.info.cleaned" --output-directory "$COV_DIR/html"

echo ""
echo "Coverage report generated:"
echo "  $COV_DIR/html/index.html"
