#!/bin/bash
#
# Build script for test binaries.
# Thin wrapper around CMake's ci-test preset.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build_common.sh"

require_sdl2

echo "Building test binaries..."
echo "Using SDL2: $(pkg-config --modversion sdl2)"

# ----------------------------------------------------------------------------
# Build with CMake preset
# ----------------------------------------------------------------------------
cmake --preset ci-test
cmake --build --preset ci-test -j"$(nproc)"

BUILD_DIR="$PROJECT_ROOT/build/ci-test"

echo ""
echo "Build complete!"
find "$BUILD_DIR" -maxdepth 1 -type f \( -name 'og_test_*' -o -name 'og_unit_*' \) -printf "%f\n" | sort
echo ""
echo "Run all tests with: ctest --test-dir $BUILD_DIR --parallel \$(nproc) --output-on-failure --timeout 180"
