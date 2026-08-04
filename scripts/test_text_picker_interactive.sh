#!/usr/bin/env bash
set -euo pipefail

TEXT_BIN="${1:-${OPENGLAD_TEXT:-./openglad_text}}"
TEXT_TIMEOUT="${OPENGLAD_TEXT_TIMEOUT:-55}"
if [ ! -x "$TEXT_BIN" ]; then
    echo "FAIL: Cannot find executable: $TEXT_BIN" >&2
    exit 1
fi

TMPHOME=$(mktemp -d)
TMPOUT=$(mktemp)
TMPIN=$(mktemp)
trap 'rm -rf "$TMPHOME"; rm -f "$TMPOUT" "$TMPIN"' EXIT

# Drive sequence (1-based menu indices):
#   Main: 1=Begin New Game; blank accepts the generated company name (§2.2
#     name entry); blank keeps the campaign. Back, then 2=Continue returns to
#     Base Camp. The retired 1–4 player-count rows are no longer present.
#   Base camp / Team Build (12 items, §2.5 substitution): 3=Hire Troops
#     (n/h/b — the hire AUTOSAVES the company, §3.8), 1=Roster
#     (deploy 2 toggles + blank exits), 4=Deploy (prompt re-deploys row 2),
#     9=Scenario, 6=GO!, 7=Back.
#   Scenario submenu (7 items): 4=Matchup (set preferred-team metadata, blank
#     exits), 3=View Scenario (blank dismisses), 6=Scenario Troops, 7=Back.
#   Protocol session after GO!: state, quit.
#   Main: 7=Quit.
cat > "$TMPIN" << 'INP'
1


7
2
3
n
h
b
1
deploy 2

4
2
9
4
play 1

3

7
6
state
quit
7
7
INP

HOME="$TMPHOME" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" < "$TMPIN" > "$TMPOUT" 2>/dev/null
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
if not any('OpenGlad Main Menu' in l for l in lines):
    print('FAIL: interactive picker banner not found', file=sys.stderr)
    sys.exit(1)

# §2.2: Begin New Game opens the name-entry screen (generated company name).
if not any('Found Your Company' in l for l in lines):
    print('FAIL: expected the name-entry banner', file=sys.stderr)
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
if not any(e.get('family') == 0 for e in team_entities):
    print('FAIL: expected at least one player team family=0 entity', file=sys.stderr)
    sys.exit(1)
if not any(isinstance(e.get('family'), int) and e.get('family') != 0 for e in team_entities):
    print('FAIL: expected at least one non-zero family in player team after hire/load', file=sys.stderr)
    sys.exit(1)

# §2.5 roster: 'deploy 2' benches the hired member, the Deploy item (4)
# re-deploys it; both print the toggle confirmation.
if not any(l.endswith('benched.') for l in lines):
    print('FAIL: expected a roster bench confirmation', file=sys.stderr)
    sys.exit(1)
if not any(l.endswith('deployed.') for l in lines):
    print('FAIL: expected a deploy confirmation', file=sys.stderr)
    sys.exit(1)
if not any('DEP ' in l for l in lines):
    print('FAIL: expected the roster DEP n/m footer', file=sys.stderr)
    sys.exit(1)

# Scenario submenu: the nested menu between Team Build and its screens.
if not any('=== Scenario ===' in l for l in lines):
    print('FAIL: expected the Scenario submenu banner', file=sys.stderr)
    sys.exit(1)

# Matchup screen: roster rows grouped by team plus the play/move sub-prompt.
if not any('--- Matchup ---' in l for l in lines):
    print('FAIL: expected the Matchup screen roster header', file=sys.stderr)
    sys.exit(1)
if not any('RED TEAM' in l for l in lines):
    print('FAIL: expected a RED TEAM roster row', file=sys.stderr)
    sys.exit(1)
if any('1 Player' in l or '2 Player' in l or
       '3 Player' in l or '4 Player' in l for l in lines):
    print('FAIL: retired player-count row leaked into terminal Main', file=sys.stderr)
    sys.exit(1)
if not any('Preferred-team metadata is now RED;' in l for l in lines):
    print('FAIL: expected the honest preferred-team metadata confirmation', file=sys.stderr)
    sys.exit(1)
if any(' TEAM (P' in l or 'P1 plays' in l or 'P2 plays' in l for l in lines):
    print('FAIL: text Matchup must not claim playable P# seats', file=sys.stderr)
    sys.exit(1)

# View Scenario: the shared roster report from a scratch headless load.
if not any(l.startswith('--- SCEN ') for l in lines):
    print('FAIL: expected the View Scenario report header', file=sys.stderr)
    sys.exit(1)

quit_ok = any(o.get('cmd') == 'quit' and o.get('status') == 'ok' for o in objs)
if not quit_ok:
    print('FAIL: missing protocol quit confirmation', file=sys.stderr)
    sys.exit(1)

# §3.8: the hire/deploy mutations AUTOSAVED the active company slot (the
# text client's quicksave slot) — no manual save command exists anymore.
save_path = os.path.join(home, '.openglad', 'save', 'text_quicksave.gtl')
if not os.path.exists(save_path):
    print(f'FAIL: expected autosaved company file missing: {save_path}', file=sys.stderr)
    sys.exit(1)

print('PASS: interactive picker smoke and mutation-autosave round-trip')
PY
