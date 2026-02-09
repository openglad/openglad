#!/bin/bash
#
# Build script for test binary (headless testing)
# Outputs openglad_test binary to the project root
#
# Uses CMake with -DBUILD_TESTING=ON.
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
# Build with CMake
# ----------------------------------------------------------------------------
BUILD_DIR="$PROJECT_ROOT/build-test"

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$BUILD_DIR" --target openglad_test -j$(nproc)

# Copy binary to project root for backward compatibility
cp "$BUILD_DIR/openglad_test" "$PROJECT_ROOT/openglad_test"

echo ""
echo "Build complete!"
ls -lh "$PROJECT_ROOT/openglad_test"
echo ""
echo "Run with: ./openglad_test"
