#!/bin/bash
# scripts/build_common.sh — shared helpers for build scripts.
#
# Sourced by each build_*.sh after it sets SCRIPT_DIR.
# Provides:
#   PROJECT_ROOT   — absolute path to the repository root
#   require_sdl2   — exit if SDL2/SDL2_mixer are not installed
#   require_command — exit if a required command is missing

PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Exit with an error if SDL2 and SDL2_mixer development packages are missing.
require_sdl2() {
    if ! pkg-config --exists sdl2 SDL2_mixer; then
        echo "ERROR: Missing dependencies. Install with:"
        echo "  sudo apt-get install libsdl2-dev libsdl2-mixer-dev"
        exit 1
    fi
}

# Exit with an error if a required command is not on PATH.
# Usage: require_command <cmd> <install_hint>
require_command() {
    local cmd="$1"
    local hint="$2"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "ERROR: $cmd not found. $hint"
        exit 1
    fi
}
