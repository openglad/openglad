#!/usr/bin/env bash
# Launch the OpenGlad game from a build directory.
#
# The binary must run with its build directory as the working directory so it
# can find its assets (cfg/, builtin/, pix/, sound/). This script locates a
# built `openglad` binary and runs it from the right place.
#
# Usage:
#   ./run-game.sh            # auto-pick a build (ci-test, then dev-debug, dev-release)
#   ./run-game.sh <preset>   # run a specific build, e.g. ./run-game.sh dev-debug
#   ./run-game.sh -- <args>  # pass extra args through to openglad
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Preferred build presets, in order, unless one is given as the first argument.
PRESETS=(ci-test dev-debug dev-release)
if [[ "${1:-}" != "" && "${1:-}" != "--" ]]; then
    PRESETS=("$1")
    shift
fi
# Drop a leading "--" separator so the rest are passed to the game.
[[ "${1:-}" == "--" ]] && shift

BIN=""
for preset in "${PRESETS[@]}"; do
    candidate="${SCRIPT_DIR}/build/${preset}/openglad"
    if [[ -x "$candidate" ]]; then
        BIN="$candidate"
        break
    fi
done

if [[ -z "$BIN" ]]; then
    echo "error: no built 'openglad' binary found under ${SCRIPT_DIR}/build/{${PRESETS[*]// /,}}/" >&2
    echo "Build it first, e.g.:" >&2
    echo "  cmake --preset ci-test && cmake --build --preset ci-test" >&2
    exit 1
fi

# A graphical display is required.
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "error: no DISPLAY or WAYLAND_DISPLAY set; the game needs a graphical session." >&2
    exit 1
fi

BUILD_DIR="$(dirname "$BIN")"
echo "Starting OpenGlad from ${BUILD_DIR}"
cd "$BUILD_DIR"
exec ./openglad "$@"
