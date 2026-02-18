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

if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPOUT=$(mktemp)
trap 'rm -f "$TMPOUT"' EXIT

# Run 1000 ticks, dump state, then quit.  Timeout after 30 seconds.
printf 'tick 1000\nstate\nquit\n' | timeout 30 "$TEXT_BIN" --protocol --level 1 --seed 42 > "$TMPOUT" 2>/dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text exited with code $rc" >&2
    exit 1
fi

# Validate with Python (available in CI and dev environments).
python3 -c "
import json, sys

lines = [l.strip() for l in open('$TMPOUT') if l.strip()]
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
    print(f'FAIL: Last tick should be 1000, got {results[-1].get(\"tick\")}', file=sys.stderr)
    sys.exit(1)

# Line 3: state dump
state = json.loads(lines[2])
entities = state.get('entities', [])
dead_count = sum(1 for e in entities if e.get('dead'))
if dead_count == 0:
    print('FAIL: Expected at least 1 dead entity after 1000 ticks, got 0', file=sys.stderr)
    sys.exit(1)

# Line 4: quit confirmation
quit_msg = json.loads(lines[3])
if quit_msg.get('status') != 'ok':
    print('FAIL: Quit message not ok:', lines[3], file=sys.stderr)
    sys.exit(1)

print(f'PASS: 1000 ticks, {len(entities)} entities, {dead_count} dead, clean quit')
"
