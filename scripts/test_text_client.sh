#!/usr/bin/env bash
# test_text_client.sh — Integration test for the headless text client.
# Runs openglad_text for 1000 ticks on level 1 and verifies:
#   1. --help/-h print the usage block and exit 0
#   2. The client starts successfully (status: ready)
#   3. All 1000 ticks complete without crashing
#   4. The `events` drain is non-empty and carries the kill's ScoreChange and
#      the death PlaySound, every event stamped inside 1..1000
#   5. At least one entity has died by tick 1000
#   6. The client exits cleanly on "quit"
#
# Exit code: 0 on success, 1 on any failure.

set -euo pipefail

# Find the openglad_text binary next to this script's build dir,
# or accept it as an argument / environment variable.
TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
TEXT_TIMEOUT="${OPENGLAD_TEXT_TIMEOUT:-55}"

if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPOUT=$(mktemp)
TMPHOME=$(mktemp -d)
trap 'rm -f "$TMPOUT"; rm -rf "$TMPHOME"' EXIT

# --help/-h must PRINT the usage block, not merely exit 0: main() returns 0 for
# any parse_args() false (src/platform/text/main.cpp:158-159), so an exit-status
# check alone stays green for a --help that prints nothing at all. Pin the
# headline, one option line and one stdin-command line from the block at
# src/platform/text/main.cpp:131-152 (it is written to stderr).
check_help_block() {
    local flag=$1
    local out=""
    local rc=0
    set +e
    out=$("$TEXT_BIN" "$flag" 2>&1)
    rc=$?
    set -e

    if [ $rc -ne 0 ]; then
        echo "FAIL: openglad_text $flag exited $rc, expected 0" >&2
        printf '%s\n' "$out" >&2
        exit 1
    fi

    local needle
    for needle in \
        'Usage: openglad_text [options]' \
        '--protocol          Run JSON protocol mode directly' \
        'events     Drain and print sim events'
    do
        if ! printf '%s' "$out" | grep -Fq -e "$needle"; then
            echo "FAIL: openglad_text $flag output is missing: $needle" >&2
            printf '%s\n' "$out" >&2
            exit 1
        fi
    done
}

check_help_block --help
check_help_block -h

# --probe-unsupported-warnings exists to EMIT the one-time headless warnings
# (src/platform/text/platform_headless.cpp emit_headless_unsupported_warnings_probe,
# which calls each unsupported API TWICE on purpose). An exit-status check alone
# stays green for a probe that prints nothing, and for a std::call_once that has
# degenerated into "warn on every call". Pin both: every warning present, and
# each exactly once. stdout must stay empty — the probe reports on stderr and
# starts no session.
TMPPROBE_OUT=$(mktemp)
TMPPROBE_ERR=$(mktemp)
trap 'rm -f "$TMPOUT" "$TMPPROBE_OUT" "$TMPPROBE_ERR"; rm -rf "$TMPHOME"' EXIT

if ! HOME="$TMPHOME" OPENGLAD_CONFIG_DIR="$TMPHOME/probe-config/" \
    "$TEXT_BIN" --probe-unsupported-warnings > "$TMPPROBE_OUT" 2> "$TMPPROBE_ERR"; then
    echo "FAIL: openglad_text --probe-unsupported-warnings exited non-zero" >&2
    cat "$TMPPROBE_ERR" >&2
    exit 1
fi

if [ -s "$TMPPROBE_OUT" ]; then
    echo "FAIL: --probe-unsupported-warnings wrote to stdout: $(cat "$TMPPROBE_OUT")" >&2
    exit 1
fi

probe_warning_status=0
while IFS= read -r needle; do
    [ -n "$needle" ] || continue
    count=$(grep -Fc -- "$needle" "$TMPPROBE_ERR" || true)
    if [ "$count" != "1" ]; then
        echo "FAIL: headless warning '$needle' appeared $count times, expected exactly 1" >&2
        probe_warning_status=1
    fi
done <<'NEEDLES'
level_data_draw_impl: not supported in headless mode
create_level_render not supported in headless mode
yes_or_no_prompt: not supported in headless mode, returning default
get_input_events: not supported in headless mode
find_follow_leader: not supported in headless mode
load_map_data: not supported in headless mode
load_decor_data: not supported in headless mode
input_state_from_sdl: not supported in headless mode
NEEDLES
if [ $probe_warning_status -ne 0 ]; then
    echo "--- probe stderr ---" >&2
    cat "$TMPPROBE_ERR" >&2
    exit 1
fi

echo "PASS: --probe-unsupported-warnings emits each headless warning exactly once"

# Keep the script-level timeout below CTest's 60s test timeout so sanitizer
# runs still have headroom under parallel load without masking real hangs.
printf 'tick 1000\nevents\nstate\nquit\n' | HOME="$TMPHOME" OPENGLAD_CONFIG_DIR="$TMPHOME/config/" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --protocol --campaign gladiator --level 1 --team 0,1 --seed 42 > "$TMPOUT" 2>/dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text exited with code $rc" >&2
    exit 1
fi

# Validate with Python (available in CI and dev environments).
python3 - "$TMPOUT" <<'PY'
import json, sys

with open(sys.argv[1], encoding='utf-8') as f:
    lines = [l.strip() for l in f if l.strip()]
if len(lines) < 3:
    print('FAIL: Expected at least 3 JSON lines, got', len(lines), file=sys.stderr)
    sys.exit(1)

# Line 1: ready message
ready = json.loads(lines[0])
if ready.get('status') != 'ready':
    print('FAIL: First line is not ready:', lines[0], file=sys.stderr)
    sys.exit(1)

# Line 2: tick results
tick_result = json.loads(lines[1])
results = tick_result.get('results', [])
if len(results) != 1000:
    print(f'FAIL: Expected 1000 tick results, got {len(results)}', file=sys.stderr)
    sys.exit(1)
if results[-1].get('tick') != 1000:
    print(f'FAIL: Last tick should be 1000, got {results[-1].get("tick")}', file=sys.stderr)
    sys.exit(1)

# Line 3: event drain
events_msg = json.loads(lines[2])
if events_msg.get('cmd') != 'events':
    print('FAIL: events line command mismatch:', lines[2], file=sys.stderr)
    sys.exit(1)
events = events_msg.get('events')
if not isinstance(events, list):
    print('FAIL: events field is not a list:', lines[2], file=sys.stderr)
    sys.exit(1)
# An empty list is not a pass: nothing drains the log during `tick`, so a run
# that killed something (pinned below) must still hold that fight's events.
# ScoreChange (kind 18) is the scoring path — award_score() in
# src/gameplay/walker_combat.cpp and og.award_score from the treasure packs —
# and a death emits a death PlaySound (kind 4). Kind numbers are
# og::sim::EventKind in include/openglad/gameplay/event.h.
if not events:
    print('FAIL: events drain was empty after 1000 ticks with a death',
          file=sys.stderr)
    sys.exit(1)
kinds = {e.get('kind') for e in events if isinstance(e, dict)}
if 18 not in kinds:
    print('FAIL: events drain has no ScoreChange (kind 18); kinds seen: '
          f'{sorted(k for k in kinds if k is not None)}', file=sys.stderr)
    sys.exit(1)
# The death below is the one thing this run guarantees, so pin the event it
# must have produced: walker_combat.cpp emits SOUND_DIE1 (2) or SOUND_DIE2
# (12) as a PlaySound (kind 4) on every visible living death, and nothing
# else in the engine or the packs emits those two ids.
DEATH_SOUNDS = (2, 12)
death_sounds = [e for e in events
                if isinstance(e, dict) and e.get('kind') == 4
                and e.get('a') in DEATH_SOUNDS]
if not death_sounds:
    print('FAIL: events drain has no death PlaySound (kind 4, a in '
          f'{DEATH_SOUNDS}) even though an entity died', file=sys.stderr)
    sys.exit(1)
for i, event in enumerate(events):
    if not isinstance(event, dict):
        print(f'FAIL: event {i} is not an object: {event!r}', file=sys.stderr)
        sys.exit(1)
    for field in ('tick', 'kind', 'a', 'b'):
        if field not in event:
            print(f'FAIL: event {i} missing {field}: {event!r}', file=sys.stderr)
            sys.exit(1)
    # Every drained event was stamped by one of the 1000 ticks just run.
    if not 1 <= event['tick'] <= 1000:
        print(f'FAIL: event {i} tick {event["tick"]} outside 1..1000: {event!r}',
              file=sys.stderr)
        sys.exit(1)

# Line 4: state dump
state = json.loads(lines[3])
entities = state.get('entities', [])
dead_count = sum(1 for e in entities if e.get('dead'))
if dead_count == 0:
    print('FAIL: Expected at least 1 dead entity after 1000 ticks, got 0', file=sys.stderr)
    sys.exit(1)

# Line 5: quit confirmation
quit_msg = json.loads(lines[4])
if quit_msg.get('status') != 'ok':
    print('FAIL: Quit message not ok:', lines[4], file=sys.stderr)
    sys.exit(1)

print(f'PASS: 1000 ticks, {len(entities)} entities, {dead_count} dead, {len(events)} events (kinds {sorted(kinds)}, {len(death_sounds)} death sounds), clean quit')
PY
