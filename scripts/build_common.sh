#!/bin/bash
# scripts/build_common.sh — shared helpers for build scripts.
#
# Sourced by each build_*.sh after it sets SCRIPT_DIR.
# Provides:
#   PROJECT_ROOT   — absolute path to the repository root
#   require_sdl3   — advise if no system SDL3 is installed (build can fetch it)
#   require_command — exit if a required command is missing

PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

# Advise (do not fail) when no system SDL3 development package is present:
# the CMake build falls back to fetching SDL3 release-3.4.8 via FetchContent.
require_sdl3() {
    if ! pkg-config --exists sdl3; then
        echo "NOTE: system sdl3 not found; the build will fetch SDL3 release-3.4.8"
        echo "      (or install libsdl3-dev / nix sdl3 to use a system copy)."
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
