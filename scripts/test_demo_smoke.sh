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
grep -Fq 'openglad_demo: campaign gladiator' <<<"$output"
# No capture or dump knob set, so this run takes the uncapped pacing path.
grep -Fq 'openglad_demo: pacing uncapped' <<<"$output"

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
        OPENGLAD_DEMO_CAMPAIGN=concept \
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
grep -Fq 'openglad_demo: campaign concept' <<<"$capture_output"
grep -Fq '  session 0: scenario 605' <<<"$capture_output"
# Capture depends on frame N being sim tick N, so enabling it forces lockstep.
grep -Fq 'openglad_demo: pacing lockstep' <<<"$capture_output"
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
    "OPENGLAD_DEMO_CAPTURE_FOCUS must be player, boss, center or cell:<x>,<y>, got 'sideways'" \
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

# --- Whole-grid FPS overlay -------------------------------------------------
# These runs write composite dumps, so they must stay after the "no *.bmp with
# capture disabled" check above. The overlay-on log line is matched with its
# openglad_demo prefix because gparser logs a bare "FPS overlay on." for the
# --show-fps command-line flag.
fps_on_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=2x1 \
        OPENGLAD_DEMO_MAX_FRAMES=10 \
        OPENGLAD_DEMO_SEED=5 \
        OPENGLAD_DEMO_SHOW_FPS=1 \
        OPENGLAD_DEMO_COMPOSITE_DUMP="$test_root/fps-on.bmp" \
        OPENGLAD_CONFIG_DIR="$test_root/fps-on-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$fps_on_output"
grep -Fq 'openglad_demo: FPS overlay on' <<<"$fps_on_output"
# A composite dump also forces lockstep: the dumped pixels are the byte-pinned
# software-compositor output the reproducibility checks below rely on.
grep -Fq 'openglad_demo: pacing lockstep' <<<"$fps_on_output"

# Unset knob with a fresh config dir: graphics/show_fps is off, so is the
# overlay.
fps_off_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=2x1 \
        OPENGLAD_DEMO_MAX_FRAMES=10 \
        OPENGLAD_DEMO_SEED=5 \
        OPENGLAD_DEMO_COMPOSITE_DUMP="$test_root/fps-off.bmp" \
        OPENGLAD_CONFIG_DIR="$test_root/fps-off-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$fps_off_output"
if grep -Fq 'openglad_demo: FPS overlay on' <<<"$fps_off_output"; then
    printf 'openglad_demo enabled the FPS overlay without the knob\n' >&2
    exit 1
fi

fps_zero_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=5 \
        OPENGLAD_DEMO_SHOW_FPS=0 \
        OPENGLAD_CONFIG_DIR="$test_root/fps-zero-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$fps_zero_output"
if grep -Fq 'openglad_demo: FPS overlay on' <<<"$fps_zero_output"; then
    printf 'OPENGLAD_DEMO_SHOW_FPS=0 still enabled the FPS overlay\n' >&2
    exit 1
fi

# Control for the pixel assertion below: with the overlay off, two runs of the
# same seed, grid and frame count dump byte-identical composites. If that ever
# stops holding, this fails first and the "on differs from off" comparison is
# not silently reduced to a coin flip.
env \
    SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy \
    SDL_RENDER_DRIVER=software \
    OPENGLAD_DEMO_GRID=2x1 \
    OPENGLAD_DEMO_MAX_FRAMES=10 \
    OPENGLAD_DEMO_SEED=5 \
    OPENGLAD_DEMO_COMPOSITE_DUMP="$test_root/fps-off-again.bmp" \
    OPENGLAD_CONFIG_DIR="$test_root/fps-off-again-config" \
    "$demo_bin" >/dev/null 2>&1
if ! cmp -s "$test_root/fps-off.bmp" "$test_root/fps-off-again.bmp"; then
    printf 'composite dumps are not reproducible for a fixed seed\n' >&2
    exit 1
fi

# The overlay is drawn into the presented composite, so it must change those
# pixels. Only inequality is asserted: the readout itself varies run to run.
# Both dumps must exist and be non-empty first — cmp exits 2 on a missing
# operand, which the inequality branch would otherwise treat as a pass.
for dump in "$test_root/fps-on.bmp" "$test_root/fps-off.bmp"; do
    if [[ ! -s "$dump" ]]; then
        printf 'missing composite dump: %s\n' "$dump" >&2
        exit 1
    fi
done
if cmp -s "$test_root/fps-on.bmp" "$test_root/fps-off.bmp"; then
    printf 'FPS overlay left the presented composite unchanged\n' >&2
    exit 1
fi

# --- Pacing modes -----------------------------------------------------------
# OPENGLAD_DEMO_MAX_FRAMES counts sim ticks in both modes, so an uncapped run
# reports exactly its budget in the exit summary. The rendered-frame count is
# hardware-dependent and deliberately unasserted.
uncapped_ticks_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=3 \
        OPENGLAD_DEMO_SEED=6 \
        OPENGLAD_CONFIG_DIR="$test_root/uncapped-ticks-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$uncapped_ticks_output"
grep -Fq 'openglad_demo: pacing uncapped' <<<"$uncapped_ticks_output"
grep -Fq 'openglad_demo: 3 sim ticks,' <<<"$uncapped_ticks_output"

# OPENGLAD_DEMO_LOCKSTEP forces the lockstep path without a capture or dump.
lockstep_forced_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=6 \
        OPENGLAD_DEMO_LOCKSTEP=1 \
        OPENGLAD_CONFIG_DIR="$test_root/lockstep-forced-config" \
        "$demo_bin" 2>&1
)
printf '%s\n' "$lockstep_forced_output"
grep -Fq 'openglad_demo: pacing lockstep' <<<"$lockstep_forced_output"

# --- Bootstrap save failure ---------------------------------------------------
# Every session boots from the save slot it writes first. A directory standing
# where save/save0.gtl belongs makes that write fail (as root too, unlike a
# chmod); the demo must refuse to run, loudly, rather than play whatever the
# slot held before. Bounded: a demo that skipped the check would block in the
# load-failure dialog, and timeout's 124 must not stall the whole script.
mkdir -p "$test_root/save-fail-config/save/save0.gtl"
set +e
save_fail_output=$(
    env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_MAX_FRAMES=1 \
        OPENGLAD_DEMO_SEED=8 \
        OPENGLAD_CONFIG_DIR="$test_root/save-fail-config" \
        timeout 60 "$demo_bin" 2>&1
)
save_fail_status=$?
set -e
printf '%s\n' "$save_fail_output"
if (( save_fail_status == 0 )); then
    printf 'openglad_demo ran without being able to write its bootstrap save\n' >&2
    exit 1
fi
grep -Fq 'openglad_demo failed to bootstrap save0 for scenario' \
    <<<"$save_fail_output"

# --- Clean shutdown on an OS quit request -------------------------------------
# SDL turns SIGINT into SDL_EVENT_QUIT; the demo's loop must stop and the
# process exit 0 through its normal shutdown (exit summary, worker join).
# MAX_FRAMES=0 runs unbounded, so only the quit can end the run: a quit the
# loop ignores becomes timeout's status 124. The signal is sent once the log
# says the main loop is about to start -- a poll on that line, never a fixed
# sleep -- and goes to the demo itself (its pid is written before the exec),
# not to timeout, which would report a forwarded signal as 128+2. A
# background job starts with SIGINT ignored, and SDL only claims a signal
# whose handler is still the default, so the demo is exec'd with SIGINT reset
# (env --default-signal). -k: a demo that ignores the quit (and timeout's
# TERM) is killed rather than left running.
sigint_log="$test_root/sigint.log"
sigint_pidfile="$test_root/sigint.pid"
env \
    SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy \
    SDL_RENDER_DRIVER=software \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=0 \
    OPENGLAD_DEMO_SEED=9 \
    OPENGLAD_CONFIG_DIR="$test_root/sigint-config" \
    timeout -k 5 60 sh -c 'echo $$ >"$0"; exec env --default-signal=INT "$1"' \
    "$sigint_pidfile" "$demo_bin" \
    >"$sigint_log" 2>&1 &
sigint_pid=$!
sigint_ready=0
for (( poll = 0; poll < 600; poll++ )); do
    if [[ -s "$sigint_pidfile" ]] &&
       grep -Fq 'spawning 1 worker threads' "$sigint_log"; then
        sigint_ready=1
        break
    fi
    kill -0 "$sigint_pid" 2>/dev/null || break
    sleep 0.1
done
if (( sigint_ready == 0 )); then
    cat "$sigint_log"
    kill -KILL "$sigint_pid" 2>/dev/null || true
    printf 'openglad_demo never reached its main loop\n' >&2
    exit 1
fi
kill -INT "$(cat "$sigint_pidfile")"
set +e
wait "$sigint_pid"
sigint_status=$?
set -e
cat "$sigint_log"
if (( sigint_status != 0 )); then
    printf 'openglad_demo exited with status %d on SIGINT (124/137 = never quit)\n' \
        "$sigint_status" >&2
    exit 1
fi
grep -Eq 'openglad_demo: [0-9]+ sim ticks,' "$sigint_log"
