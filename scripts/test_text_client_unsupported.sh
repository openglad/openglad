#!/usr/bin/env bash
set -euo pipefail

TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
TEXT_TIMEOUT="${OPENGLAD_TEXT_TIMEOUT:-55}"
if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPOUT=$(mktemp)
TMPOERR=$(mktemp)
TMPHOME=$(mktemp -d)
trap 'rm -f "$TMPOUT" "$TMPOERR"; rm -rf "$TMPHOME"' EXIT

printf 'input 0 left\nquit\n' | timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --protocol --level 1 --seed 42 > "$TMPOUT" 2>/dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text unsupported-path run exited with code $rc" >&2
    exit 1
fi

export TMPOUT
python3 - << 'PY'
import json, os, sys

lines = [l.strip() for l in open(os.environ['TMPOUT'], encoding='utf-8', errors='replace') if l.strip()]
if len(lines) < 3:
    print(f'FAIL: expected at least 3 JSON lines, got {len(lines)}', file=sys.stderr)
    sys.exit(1)

ready = json.loads(lines[0])
if ready.get('status') != 'ready':
    print('FAIL: first line is not ready', file=sys.stderr)
    sys.exit(1)

error = json.loads(lines[1])
if error.get('cmd') != 'error' or 'unknown command: input' not in error.get('message', ''):
    print(f'FAIL: unsupported input command did not emit expected error: {lines[1]}', file=sys.stderr)
    sys.exit(1)

quit_msg = json.loads(lines[2])
if quit_msg.get('status') != 'ok':
    print('FAIL: quit confirmation missing', file=sys.stderr)
    sys.exit(1)

print('PASS: unsupported protocol command emits expected error')
PY

HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --probe-unsupported-warnings > /dev/null 2> "$TMPOERR"
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text warning probe run exited with code $rc" >&2
    exit 1
fi

export TMPOERR
python3 - << 'PY'
import os, sys

stderr_text = open(os.environ['TMPOERR'], encoding='utf-8', errors='replace').read()
expected = [
    "level_data_draw_impl: not supported in headless mode",
    "create_level_render not supported in headless mode",
    "yes_or_no_prompt: not supported in headless mode, returning default",
    "get_input_events: not supported in headless mode",
    "find_follow_leader: not supported in headless mode",
    "load_map_data: not supported in headless mode",
    "input_state_from_sdl: not supported in headless mode",
]

for needle in expected:
    count = stderr_text.count(needle)
    if count != 1:
        print(f"FAIL: expected warning '{needle}' exactly once, found {count}", file=sys.stderr)
        sys.exit(1)

print("PASS: unsupported headless warnings emitted exactly once")
PY
