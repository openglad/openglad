#!/bin/bash
#
# Build script for test binary.
# Thin wrapper around CMake's ci-test preset.
#
# Outputs openglad_test binary to the project root
# for backward compatibility.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# ----------------------------------------------------------------------------
# Check dependencies
# ----------------------------------------------------------------------------
if ! pkg-config --exists sdl2 SDL2_mixer; then
    echo "ERROR: Missing dependencies. Install with:"
    echo "  sudo apt-get install libsdl2-dev libsdl2-mixer-dev"
    exit 1
fi

echo "Building test binary..."
echo "Using SDL2: $(pkg-config --modversion sdl2)"

# ----------------------------------------------------------------------------
# Build with CMake preset
# ----------------------------------------------------------------------------
cmake --preset ci-test
cmake --build --preset ci-test --target openglad_test

BUILD_DIR="$PROJECT_ROOT/build/ci-test"

# Copy binary to project root for backward compatibility
cp "$BUILD_DIR/openglad_test" "$PROJECT_ROOT/openglad_test"

echo ""
echo "Build complete!"
ls -lh "$PROJECT_ROOT/openglad_test"
echo ""
echo "Run with: ./openglad_test"
