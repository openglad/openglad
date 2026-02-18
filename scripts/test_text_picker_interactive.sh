#!/usr/bin/env bash
set -euo pipefail

TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPHOME=$(mktemp -d)
TMPOUT=$(mktemp)
TMPIN=$(mktemp)
trap 'rm -rf "$TMPHOME"; rm -f "$TMPOUT" "$TMPIN"' EXIT

cat > "$TMPIN" << 'INP'
5
3
1
1
4
4
5
3
4
3
1
state
quit
8
INP

HOME="$TMPHOME" timeout 30 "$TEXT_BIN" < "$TMPIN" > "$TMPOUT" 2>/dev/null
rc=$?
if [ $rc -ne 0 ]; then
    echo "FAIL: openglad_text interactive run exited with code $rc" >&2
    exit 1
fi

export TMPOUT TMPHOME
python3 - << 'PY'
import json, os, sys

out_path = os.environ['TMPOUT']
home = os.environ['TMPHOME']
lines = [l.rstrip('\n') for l in open(out_path, encoding='utf-8', errors='replace')]
if not any('OpenGlad Text Picker' in l for l in lines):
    print('FAIL: interactive picker banner not found', file=sys.stderr)
    sys.exit(1)

json_lines = []
for raw in lines:
    pos = raw.find('{')
    if pos < 0:
        continue
    candidate = raw[pos:].strip()
    try:
        json.loads(candidate)
    except Exception:
        continue
    json_lines.append(candidate)
if len(json_lines) < 3:
    print(f'FAIL: expected at least 3 JSON lines, got {len(json_lines)}', file=sys.stderr)
    sys.exit(1)

objs = [json.loads(l) for l in json_lines]
ready = next((o for o in objs if o.get('status') == 'ready'), None)
if ready is None:
    print('FAIL: missing ready message', file=sys.stderr)
    sys.exit(1)

state = next((o for o in objs if o.get('cmd') == 'state'), None)
if state is None:
    print('FAIL: missing state message', file=sys.stderr)
    sys.exit(1)

team_entities = [e for e in state.get('entities', []) if e.get('team') == 0]
families = {e.get('family') for e in team_entities}
if not ({0, 1}.issubset(families)):
    print(f'FAIL: expected loaded team families {{0,1}} in player team, got {sorted(f for f in families if isinstance(f, int))}', file=sys.stderr)
    sys.exit(1)

quit_ok = any(o.get('cmd') == 'quit' and o.get('status') == 'ok' for o in objs)
if not quit_ok:
    print('FAIL: missing protocol quit confirmation', file=sys.stderr)
    sys.exit(1)

save_path = os.path.join(home, '.openglad', 'save', 'text_quicksave.gtl')
if not os.path.exists(save_path):
    print(f'FAIL: expected save file missing: {save_path}', file=sys.stderr)
    sys.exit(1)

print('PASS: interactive picker smoke and save/load round-trip')
PY
