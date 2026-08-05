#!/usr/bin/env bash
# test_text_client.sh — Integration test for the headless text client.
# Runs openglad_text for 1000 ticks on level 1 and verifies:
#   1. The client starts successfully (status: ready)
#   2. All 1000 ticks complete without crashing
#   3. At least one entity has died by tick 1000
#   4. The client exits cleanly on "quit"
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

if ! "$TEXT_BIN" --help > /dev/null 2>&1; then
    echo "FAIL: openglad_text --help exited non-zero" >&2
    exit 1
fi

if ! "$TEXT_BIN" -h > /dev/null 2>&1; then
    echo "FAIL: openglad_text -h exited non-zero" >&2
    exit 1
fi

if ! HOME="$TMPHOME" OPENGLAD_CONFIG_DIR="$TMPHOME/probe-config/" \
    "$TEXT_BIN" --probe-unsupported-warnings > /dev/null 2>&1; then
    echo "FAIL: openglad_text --probe-unsupported-warnings exited non-zero" >&2
    exit 1
fi

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
for i, event in enumerate(events):
    if not isinstance(event, dict):
        print(f'FAIL: event {i} is not an object: {event!r}', file=sys.stderr)
        sys.exit(1)
    for field in ('tick', 'kind', 'a', 'b'):
        if field not in event:
            print(f'FAIL: event {i} missing {field}: {event!r}', file=sys.stderr)
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

print(f'PASS: 1000 ticks, {len(entities)} entities, {dead_count} dead, clean quit')
PY
