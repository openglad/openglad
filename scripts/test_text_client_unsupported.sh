#!/usr/bin/env bash
set -euo pipefail

TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPOUT=$(mktemp)
trap 'rm -f "$TMPOUT"' EXIT

printf 'input 0 left\nquit\n' | timeout 30 "$TEXT_BIN" --protocol --level 1 --seed 42 > "$TMPOUT" 2>/dev/null
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
