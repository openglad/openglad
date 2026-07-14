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
source "$SCRIPT_DIR/build_common.sh"

require_sdl3

echo "Using SDL3: $(pkg-config --modversion sdl3 2>/dev/null || echo 'fetched release-3.4.8')"

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
