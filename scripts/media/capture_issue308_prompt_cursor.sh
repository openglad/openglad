#!/usr/bin/env bash
# Capture the real hiring name prompt with the native X11 cursor. Artifacts
# stay in build/media (gitignored); use the before/after PNGs as review proof.
# Run inside `nix develop` so xvfb-run, xdotool and scrot are available.
#
# Usage: scripts/media/capture_issue308_prompt_cursor.sh [openglad-bin] [output-dir]

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GAME_BIN="${1:-$REPO_ROOT/build/ci-test/openglad}"
OUT_DIR="${2:-$REPO_ROOT/build/media/pr308}"

if [[ ! -x "$GAME_BIN" ]]; then
    printf 'OpenGlad executable not found or not executable: %s\n' "$GAME_BIN" >&2
    exit 1
fi
for tool in xvfb-run xdotool scrot magick; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf '%s not found; run this script inside `nix develop`.\n' "$tool" >&2
        exit 1
    fi
done

mkdir -p "$OUT_DIR"
sha256sum "$GAME_BIN" > "$OUT_DIR/binary.sha256"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT

export OPENGLAD_CONFIG_DIR="$WORK_DIR/user"
export SDL_AUDIODRIVER=dummy
mkdir -p "$OPENGLAD_CONFIG_DIR"

xvfb-run -a -s '-screen 0 800x600x24' bash -s -- \
    "$GAME_BIN" "$OUT_DIR" "$WORK_DIR" <<'CAPTURE'
set -euo pipefail
GAME_BIN="$1"
OUT_DIR="$2"
WORK_DIR="$3"

"$GAME_BIN" >"$OUT_DIR/run.log" 2>&1 &
game_pid=$!
cleanup() {
    kill "$game_pid" 2>/dev/null || true
    wait "$game_pid" 2>/dev/null || true
}
trap cleanup EXIT

# Dismiss the native intro, open Begin New Game, accept the generated company,
# choose the highlighted Gladiator campaign, then leave its briefing for Team
# Build. Coordinates are X11 pixels for the fixed 320x200 canvas at 2x scale.
sleep 3
xdotool key space
sleep 4
click_at() {
    xdotool mousemove --sync "$1" "$2" mousedown 1
    sleep 0.35
    xdotool mouseup 1
    sleep "${3:-1}"
}
click_at 300 130 1
click_at 400 218 1
click_at 390 380 2
xdotool key Escape
sleep 3

# Hire Troops on Team Build, then HIRE ME. That invokes the same blocking
# production text prompt used in normal play (not the TESTING shortcut).
click_at 460 40 3
click_at 250 360 2
sleep 1

# Set a stable name and move onto ACCEPT. SDL text input replaces the generated
# value on its first character. scrot captures the actual XFixes cursor.
xdotool type --clearmodifiers --delay 80 CURSORHIRE
xdotool mousemove --sync 425 158
sleep 0.5
scrot --pointer "$WORK_DIR/prompt-full.png"
magick "$WORK_DIR/prompt-full.png" -crop 640x400+0+0 +repage \
    "$OUT_DIR/prompt.png"

# Click ACCEPT with a held pointer, then record the returned hire screen.
click_at 425 158 3
scrot --pointer "$WORK_DIR/after-accept-full.png"
magick "$WORK_DIR/after-accept-full.png" -crop 640x400+0+0 +repage \
    "$OUT_DIR/after-accept.png"

# Return to the company roster so the accepted name is visible as well. Use
# the visible BACK control; an ephemeral Escape key tap can be consumed by the
# frame between prompt completion and the hire screen's input loop.
click_at 60 360 2
scrot --pointer "$WORK_DIR/after-roster-full.png"
magick "$WORK_DIR/after-roster-full.png" -crop 640x400+0+0 +repage \
    "$OUT_DIR/after-roster.png"
CAPTURE

printf 'Captured real hiring prompt and ACCEPT result in %s\n' "$OUT_DIR"
