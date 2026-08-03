#!/usr/bin/env bash
set -euo pipefail

demo_bin=${1:?usage: test_demo_smoke.sh /path/to/openglad_demo}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

set +e
output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=2x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=1 \
        OPENGLAD_CONFIG_DIR="$test_root/config" \
        "$demo_bin" 2>&1
)
status=$?
set -e

printf '%s\n' "$output"
if (( status != 0 )); then
    printf 'openglad_demo smoke exited with status %d\n' "$status" >&2
    exit 1
fi

grep -Fq 'grid: 2x1 = 2 sessions' <<<"$output"
grep -Fq 'openglad_demo: seed 1' <<<"$output"
mapfile -t scenarios < <(
    sed -n 's/^  session [01]: scenario \([0-9][0-9]*\)$/\1/p' \
        <<<"$output"
)
if (( ${#scenarios[@]} != 2 )); then
    printf 'expected two demo session scenario logs, got %d\n' \
        "${#scenarios[@]}" >&2
    exit 1
fi
readonly demo_pool=' 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 9411 9412 9413 9414 '
for scenario in "${scenarios[@]}"; do
    if [[ "$demo_pool" != *" $scenario "* ]]; then
        printf 'scenario %s is not in the production demo pool\n' \
            "$scenario" >&2
        exit 1
    fi
done
if [[ "${scenarios[0]}" == "${scenarios[1]}" ]]; then
    printf 'demo selector repeated a scenario before exhausting its pool\n' >&2
    exit 1
fi
grep -Fq \
    'openglad_demo: 2 sessions initialized, spawning 2 worker threads' \
    <<<"$output"
grep -Fq 'openglad_demo: campaign org.openglad.gladiator' <<<"$output"

# The capture knobs are opt-in: a production run writes no frames at all.
if [[ -n "$(find "$test_root" -name '*.bmp' -print -quit)" ]]; then
    printf 'openglad_demo wrote a capture frame with capture disabled\n' >&2
    exit 1
fi

set +e
invalid_seed_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=not-a-number \
        OPENGLAD_CONFIG_DIR="$test_root/invalid-seed-config" \
        "$demo_bin" 2>&1
)
invalid_seed_status=$?
set -e

printf '%s\n' "$invalid_seed_output"
if (( invalid_seed_status == 0 )); then
    printf 'openglad_demo accepted an invalid deterministic seed\n' >&2
    exit 1
fi
grep -Fq \
    "OPENGLAD_DEMO_SEED must be an unsigned integer, got 'not-a-number'" \
    <<<"$invalid_seed_output"

# ---------------------------------------------------------------------------
# Frame capture (showcase media pipeline)
# ---------------------------------------------------------------------------
# Little-endian scalar out of a binary header, so the BMPs can be checked
# without a Python or ImageMagick dependency.
read_le() { # read_le <file> <offset> <byte-width>
    od --address-radix=n --format="u${3}" --skip-bytes="$2" \
        --read-bytes="$3" "$1" | tr -d '[:space:]'
}

expect_capture_frames() { # expect_capture_frames <dir> <count> <width> <height>
    local dir=$1 want=$2 width=$3 height=$4 i path
    for (( i = 0; i < want; i++ )); do
        path=$(printf '%s/frame%05d.bmp' "$dir" "$i")
        if [[ ! -f "$path" ]]; then
            printf 'missing capture frame %s\n' "$path" >&2
            exit 1
        fi
        if [[ "$(head -c 2 "$path")" != "BM" ]]; then
            printf '%s is not a BMP\n' "$path" >&2
            exit 1
        fi
        # The GIF/PNG tools require 8-bit indexed frames at the captured size.
        local got_w got_h got_bpp
        got_w=$(read_le "$path" 18 4)
        got_h=$(read_le "$path" 22 4)
        got_bpp=$(read_le "$path" 28 2)
        if [[ "$got_w" != "$width" || "$got_h" != "$height" ||
              "$got_bpp" != "8" ]]; then
            printf '%s is %sx%s at %s bpp, expected %sx%s at 8 bpp\n' \
                "$path" "$got_w" "$got_h" "$got_bpp" "$width" "$height" >&2
            exit 1
        fi
    done
    if [[ -f "$(printf '%s/frame%05d.bmp' "$dir" "$want")" ]]; then
        printf 'capture wrote more than %d frames into %s\n' "$want" "$dir" >&2
        exit 1
    fi
}

# One targeted session, static arena camera, every other frame.
capture_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_SEED=3 \
        OPENGLAD_DEMO_CAMPAIGN=org.openglad.concept \
        OPENGLAD_DEMO_SCENARIOS=605 \
        OPENGLAD_DEMO_TEAM_SIZE=2 \
        OPENGLAD_DEMO_CAPTURE_DIR="$test_root/capture" \
        OPENGLAD_DEMO_CAPTURE_FOCUS=center \
        OPENGLAD_DEMO_CAPTURE_EVERY=2 \
        OPENGLAD_DEMO_CAPTURE_START=1 \
        OPENGLAD_DEMO_CAPTURE_LIMIT=3 \
        OPENGLAD_CONFIG_DIR="$test_root/capture-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$capture_output"
grep -Fq 'openglad_demo: campaign org.openglad.concept' <<<"$capture_output"
grep -Fq '  session 0: scenario 605' <<<"$capture_output"
grep -Fq 'openglad_demo: captured 3 frames' <<<"$capture_output"
expect_capture_frames "$test_root/capture" 3 320 200

# Whole-grid capture with the boss camera: two cells side by side in one image.
grid_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=2x1 \
        OPENGLAD_DEMO_SEED=4 \
        OPENGLAD_DEMO_SCENARIOS=1,2 \
        OPENGLAD_DEMO_CAPTURE_DIR="$test_root/capture-grid" \
        OPENGLAD_DEMO_CAPTURE_SESSION=-1 \
        OPENGLAD_DEMO_CAPTURE_FOCUS=boss \
        OPENGLAD_DEMO_CAPTURE_LIMIT=2 \
        OPENGLAD_CONFIG_DIR="$test_root/capture-grid-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$grid_output"
grep -Fq 'openglad_demo: captured 2 frames' <<<"$grid_output"
expect_capture_frames "$test_root/capture-grid" 2 640 200

set +e
bad_focus_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_CAPTURE_DIR="$test_root/capture-bad" \
        OPENGLAD_DEMO_CAPTURE_FOCUS=sideways \
        OPENGLAD_CONFIG_DIR="$test_root/capture-bad-config" \
        "$demo_bin" 2>&1
)
bad_focus_status=$?
set -e
printf '%s\n' "$bad_focus_output"
if (( bad_focus_status == 0 )); then
    printf 'openglad_demo accepted an unknown capture focus\n' >&2
    exit 1
fi
grep -Fq \
    "OPENGLAD_DEMO_CAPTURE_FOCUS must be player, boss or center, got 'sideways'" \
    <<<"$bad_focus_output"

set +e
bad_scenarios_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SCENARIOS=1,,3 \
        OPENGLAD_CONFIG_DIR="$test_root/bad-scenarios-config" \
        "$demo_bin" 2>&1
)
bad_scenarios_status=$?
set -e
printf '%s\n' "$bad_scenarios_output"
if (( bad_scenarios_status == 0 )); then
    printf 'openglad_demo accepted a malformed scenario list\n' >&2
    exit 1
fi
grep -Fq \
    "OPENGLAD_DEMO_SCENARIOS must be a comma-separated scenario id list" \
    <<<"$bad_scenarios_output"
