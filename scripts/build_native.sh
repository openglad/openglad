#!/bin/bash
#
# Build script for native (Linux) target.
# Thin wrapper around CMake's dev-release preset.
#
# Outputs openglad and openscen binaries to the project root
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

echo "Using SDL2: $(pkg-config --modversion sdl2)"
echo "Using SDL2_mixer: $(pkg-config --modversion SDL2_mixer)"

# ----------------------------------------------------------------------------
# Build with CMake preset
# ----------------------------------------------------------------------------
cmake --preset dev-release
cmake --build --preset dev-release

BUILD_DIR="$PROJECT_ROOT/build/dev-release"

# Copy binaries to project root for backward compatibility
cp "$BUILD_DIR/openglad" "$PROJECT_ROOT/openglad"
cp "$BUILD_DIR/openscen" "$PROJECT_ROOT/openscen"

echo ""
echo "Build complete!"
ls -lh "$PROJECT_ROOT/openglad" "$PROJECT_ROOT/openscen"
echo ""
echo "Run with: ./openglad"
