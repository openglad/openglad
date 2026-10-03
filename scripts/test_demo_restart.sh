#!/usr/bin/env bash
# openglad_demo level-end contract (#335).
#
# A demo session is non-interactive: nobody can click the level-end popup or
# the results panel, so a scenario that ENDS must finish its session and let
# the grid restart instead of blocking the worker thread on a modal dialog.
# One 1x1 soccer match at the shortest time limit the clamp allows (720
# ticks), lockstep (a capture directory with a start frame the run never
# reaches forces lockstep and writes no frame), then 20 more ticks after the
# restart. The run is paced at 60 ticks/s while a tick costs under 16.7 ms;
# under heavy contention it becomes sim-cost-bound and slows down.
#
# Timeout sizing (R-std-5: measured once, never bumped to hide slowness), all
# against the ci-coverage openglad_demo, .gcda deleted before each batch:
#   - 4 copies at once, all pinned to CPUs 0-3 (a 4-vCPU runner under
#     ctest --parallel 4), 3 rounds, host load 24: in-run 12.42-13.54 s
#     (54.65-59.60 ticks/s).
#   - 1 copy plus 3 or 4 CPU-bound burners on the same 4 pinned CPUs, 3 runs
#     each, host load 20: whole script 12.82-14.76 s (51.97-59.57 ticks/s).
#   - unpinned, host load 43-45 with another -j3 build running (round-1
#     review): 12.41 s (59.64 ticks/s), 14.53 s (50.92 ticks/s), and one
#     run the old 26 s timeout stopped at 720/740 ticks with its in-run
#     clock at 29.10 s (24.74 ticks/s), about 29.9 s projected to 740 ticks.
#   Max = 29.9 s. Stanza timeout = 2 x max = 60 s; the ctest TIMEOUT
#   (cmake/OpenGladTests.cmake) = 2 x 60 = 120 s.
# Sanitizer builds (ci-asan: ASan+UBSan) run the sim several times slower
# and were not in that measurement. PR #345's ASan lane (GitHub's 4-vCPU
# runner, ctest --parallel 4, run 37090526673) reached 631 of 740 ticks in
# 58.48 s = 10.79 ticks/s, so a full run projects to about 72 s. The
# sanitizer stanza timeout is sized by the same 2 x max rule from that
# measurement, 150 s, with the ctest TIMEOUT at 2 x 150 = 300 s; CMake
# passes it in OPENGLAD_DEMO_RESTART_TIMEOUT when ENABLE_SANITIZERS is ON.
# (The same ci-asan binary on the 12-CPU dev box, host load 20, 3 runs:
# 12.43-12.64 s, 58.5-59.5 ticks/s: the runner's contention, not ASan
# alone, is what the sizing covers.)
set -euo pipefail

stanza_timeout=${OPENGLAD_DEMO_RESTART_TIMEOUT:-60}

demo_bin=${1:?usage: test_demo_restart.sh /path/to/openglad_demo}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT

set +e
output=$(
    timeout "$stanza_timeout" env \
        SDL_VIDEODRIVER=dummy \
        SDL_AUDIODRIVER=dummy \
        SDL_RENDER_DRIVER=software \
        OPENGLAD_CONFIG_DIR="$test_root/config" \
        OPENGLAD_DEMO_GRID=1x1 \
        OPENGLAD_DEMO_CAMPAIGN=modes \
        OPENGLAD_DEMO_SCENARIOS=820 \
        OPENGLAD_DEMO_MATCH_TIME_LIMIT=720 \
        OPENGLAD_DEMO_CAPTURE_DIR="$test_root/x" \
        OPENGLAD_DEMO_CAPTURE_START=1000000 \
        OPENGLAD_DEMO_MAX_FRAMES=740 \
        "$demo_bin" 2>&1 </dev/null
)
status=$?
set -e
printf '=== restart (exit %d) ===\n%s\n' "$status" "$output"

if (( status != 0 )); then
    if (( status == 124 )); then
        # openglad_demo reports "<N> sim ticks" on the way out; the match
        # clock ends at tick 720, so fewer ticks than that means the stanza
        # timeout expired before the level end was even reachable.
        ticks=$(grep -Eo 'openglad_demo: [0-9]+ sim ticks' <<<"$output" | tail -1 | grep -Eo '[0-9]+' | head -1)
        if grep -Fq 'Session 0 finished (worker thread)' <<<"$output"; then
            # The session ended and the grid restarted: nothing blocked, the
            # run was slower than the stanza timeout allows.
            printf 'level-end restart: exceeded the stanza timeout (%ss) after the session finished (slow run, not a blocked modal)\n' "$stanza_timeout" >&2
            grep -F 'sim ticks' <<<"$output" >&2 || true
        elif [[ -n "${ticks:-}" ]] && (( ticks < 720 )); then
            printf 'level-end restart: the stanza timeout (%ss) expired at tick %s of 740, before the 720-tick match clock could end (slow run, not a blocked modal; see the sizing note in this script)\n' "$stanza_timeout" "$ticks" >&2
            grep -F 'sim ticks' <<<"$output" >&2 || true
        else
            printf 'level-end restart: the match clock ended but the session never finished within %ss (a modal dialog blocked the worker)\n' "$stanza_timeout" >&2
        fi
    else
        printf 'level-end restart: expected exit 0, got %d\n' "$status" >&2
    fi
    exit 1
fi

expect_message() { # expect_message <fixed string>
    if ! grep -Fq "$1" <<<"$output"; then
        printf 'level-end restart: missing message: %s\n' "$1" >&2
        exit 1
    fi
}

expect_regex() { # expect_regex <ere>
    if ! grep -Eq "$1" <<<"$output"; then
        printf 'level-end restart: no line matching: %s\n' "$1" >&2
        exit 1
    fi
}

# The ending popup answered itself: its text reaches the log, and the call
# returned instead of waiting for a click.
expect_regex '(VICTORY!|DEFEAT!|MATCH OVER), SOCCER:'
expect_message 'Session 0 finished (worker thread)'
expect_message 'All sessions finished, restarting...'
expect_message 'openglad_demo: 740 sim ticks'

# The restarted match starts its clock from zero: 20 ticks into a 720-tick
# match is no level end, so exactly ONE restart happens in the whole run. A
# match clock carried over from the finished scenario would end every
# restarted match on its first tick and restart the grid on every tick.
restarts=$(grep -Fc 'All sessions finished, restarting...' <<<"$output" || true)
if (( restarts != 1 )); then
    printf 'level-end restart: expected exactly 1 restart in 740 ticks, saw %d\n' \
        "$restarts" >&2
    exit 1
fi
