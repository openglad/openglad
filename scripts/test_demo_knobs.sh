#!/usr/bin/env bash
# openglad_demo environment-knob contract.
#
# scripts/test_demo_smoke.sh covers the production boot: the shuffled pool,
# the seed, capture geometry, the FPS overlay through the SOFTWARE compositor
# (its runs all set OPENGLAD_DEMO_COMPOSITE_DUMP, which forces lockstep).
# This script covers the knobs that script alone never reaches: the uncapped
# GPU render path with the overlay on, the parse/clamp rules of every
# remaining knob, and the two failure modes that are deliberately NOT fatal
# or deliberately ARE. Every case asserts the exit status AND the message.
set -euo pipefail

demo_bin=${1:?usage: test_demo_knobs.sh /path/to/openglad_demo}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

run_demo() { # run_demo <config-dir-name> [VAR=VALUE ...]; sets $output/$status
    local tag=$1
    shift
    set +e
    output=$(
        env \
            SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy \
            SDL_RENDER_DRIVER=software \
            OPENGLAD_CONFIG_DIR="$test_root/$tag-config" \
            "$@" \
            "$demo_bin" 2>&1
    )
    status=$?
    set -e
    printf '=== %s (exit %d) ===\n%s\n' "$tag" "$status" "$output"
}

expect_status() { # expect_status <want> <what>
    if (( status != $1 )); then
        printf '%s: expected exit %d, got %d\n' "$2" "$1" "$status" >&2
        exit 1
    fi
}

expect_message() { # expect_message <fixed string> <what>
    if ! grep -Fq "$1" <<<"$output"; then
        printf '%s: missing message: %s\n' "$2" "$1" >&2
        exit 1
    fi
}

expect_regex() { # expect_regex <ere> <what>
    if ! grep -Eq "$1" <<<"$output"; then
        printf '%s: no line matching: %s\n' "$2" "$1" >&2
        exit 1
    fi
}

reject_message() { # reject_message <fixed string> <what>
    if grep -Fq "$1" <<<"$output"; then
        printf '%s: unexpected message: %s\n' "$2" "$1" >&2
        exit 1
    fi
}

# --- 1. Uncapped pacing with the FPS overlay --------------------------------
# The overlay has two implementations: the software compositor's (smoke,
# always lockstep) and the SDL-renderer strip used by every dump-less run.
# Ten sim ticks so the overlay's per-second counter actually updates, and the
# tick budget is reported exactly because MAX_FRAMES counts sim ticks in both
# pacing modes.
run_demo fps-uncapped \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=10 \
    OPENGLAD_DEMO_SEED=11 \
    OPENGLAD_DEMO_SHOW_FPS=1
expect_status 0 'uncapped FPS run'
expect_message 'openglad_demo: pacing uncapped' 'uncapped FPS run'
expect_message 'openglad_demo: FPS overlay on' 'uncapped FPS run'
expect_message 'openglad_demo: 10 sim ticks,' 'uncapped FPS run'
# Paired control for the oversized-grid case below: a grid that fits is never
# refused.
reject_message 'Display too small' 'uncapped FPS run'
# Paired control for the zoom clamp below: with the knob unset the cell keeps
# the 1.1 default. (Log substitutes {} itself and drops the format's precision
# spec, so the number prints unpadded.)
expect_message 'cell zoom 1.1' 'uncapped FPS run'
# The overlay is not a capture: an unasked-for frame file would mean the
# render path fell into the capture writer.
if [[ -n "$(find "$test_root" -name '*.bmp' -print -quit)" ]]; then
    printf 'the uncapped overlay run wrote a capture frame\n' >&2
    exit 1
fi

# --- 2. OPENGLAD_DEMO_CAMPAIGN_STATE ----------------------------------------
# A campaign-state seed goes through SaveData's write choke, so the knob
# inherits the og.campaign_var name rules. Accepted keys seed silently.
run_demo state-ok \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_CAMPAIGN_STATE=wp10_probe=7
expect_status 0 'accepted campaign-state seed'
reject_message 'OPENGLAD_DEMO_CAMPAIGN_STATE rejected' \
    'accepted campaign-state seed'

# 'BadKey' is uppercase, which valid_campaign_var_name refuses. A rejected
# seed must abort the run: a capture filmed against state the game would not
# have kept is worse than no capture.
run_demo state-bad \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_CAMPAIGN_STATE=BadKey=7
expect_status 1 'rejected campaign-state seed'
expect_message "OPENGLAD_DEMO_CAMPAIGN_STATE rejected entry 'BadKey=7'" \
    'rejected campaign-state seed'

# --- 3. OPENGLAD_DEMO_ZOOM clamp and OPENGLAD_DEMO_LOCKSTEP=on --------------
# Zoom is clamped at 1.0 (a sub-1 cell zoom would sample outside the 320x200
# session surface), and "on" is an accepted spelling of the lockstep knob
# alongside "1".
run_demo zoom-clamp \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_ZOOM=0.5 \
    OPENGLAD_DEMO_LOCKSTEP=on
expect_status 0 'clamped zoom'
expect_regex 'cell zoom 1$' 'clamped zoom'
expect_message 'openglad_demo: pacing lockstep' 'clamped zoom'

run_demo zoom-bad \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_ZOOM=wide
expect_status 1 'unparsable zoom'
expect_message "OPENGLAD_DEMO_ZOOM must be a number, got 'wide'" \
    'unparsable zoom'

# --- 4. OPENGLAD_DEMO_TEAM_SIZE ---------------------------------------------
run_demo team-size-bad \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_TEAM_SIZE=big
expect_status 1 'unparsable team size'
expect_message "OPENGLAD_DEMO_TEAM_SIZE must be an integer, got 'big'" \
    'unparsable team size'

# --- 5. Capture focus 'player' and an unusable capture directory ------------
# The smoke script exercises the boss and center cameras; 'player' is the
# third. It is also the positive arm for the directory check below: the same
# knobs with a writable directory produce the frame.
run_demo capture-player \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_SEED=12 \
    OPENGLAD_DEMO_CAPTURE_DIR="$test_root/capture-player" \
    OPENGLAD_DEMO_CAPTURE_FOCUS=player \
    OPENGLAD_DEMO_CAPTURE_LIMIT=1
expect_status 0 'player-focus capture'
expect_message 'openglad_demo: captured 1 frames' 'player-focus capture'
if [[ ! -s "$test_root/capture-player/frame00000.bmp" ]]; then
    printf 'the player-focus capture wrote no frame\n' >&2
    exit 1
fi

# A capture directory that cannot be created (a path component is a regular
# file) is fatal BEFORE any session boots — the alternative is a long render
# whose frames go nowhere.
: > "$test_root/not-a-directory"
run_demo capture-bad-dir \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_CAPTURE_DIR="$test_root/not-a-directory/frames" \
    OPENGLAD_DEMO_CAPTURE_FOCUS=player
expect_status 1 'unusable capture directory'
expect_message \
    "OPENGLAD_DEMO_CAPTURE_DIR '$test_root/not-a-directory/frames' is not usable" \
    'unusable capture directory'

# --- 6. A grid wider than the display ---------------------------------------
run_demo huge-grid \
    OPENGLAD_DEMO_GRID=99999x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1
expect_status 1 'oversized grid'
expect_message 'Display too small for a 99999x1 grid' 'oversized grid'

# --- 7. Composite dump: written, and non-fatal when it cannot be ------------
run_demo dump-ok \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_SEED=13 \
    OPENGLAD_DEMO_COMPOSITE_DUMP="$test_root/dump-ok.bmp"
expect_status 0 'composite dump'
reject_message 'composite dump to' 'composite dump'
if [[ ! -s "$test_root/dump-ok.bmp" ]]; then
    printf 'the composite dump wrote no file\n' >&2
    exit 1
fi

# The dump is a debugging aid, so a bad path is logged and the run still
# reports its tick summary and exits clean.
run_demo dump-fail \
    OPENGLAD_DEMO_GRID=1x1 \
    OPENGLAD_DEMO_MAX_FRAMES=1 \
    OPENGLAD_DEMO_SEED=13 \
    OPENGLAD_DEMO_COMPOSITE_DUMP="$test_root/no-such-dir/dump.bmp"
expect_status 0 'failed composite dump'
expect_message "composite dump to '$test_root/no-such-dir/dump.bmp' failed" \
    'failed composite dump'
expect_message 'openglad_demo: 1 sim ticks,' 'failed composite dump'
if [[ -e "$test_root/no-such-dir/dump.bmp" ]]; then
    printf 'the failed composite dump left a file behind\n' >&2
    exit 1
fi

printf 'openglad_demo knob contract: all cases passed\n'
