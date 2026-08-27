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
#   Base camp / Team Build (12 items, §2.5 substitution + the #206 Camp door
#     inserted before Back; the flat CTF trio left for the camp's MATCH SETUP
#     page and 11=Difficulty was appended in its place —
#     docs/camp-controls-design.md — with 12=Lineup appended below it,
#     docs/lineup-design.md §8, so no ordinal above moved): 3=Hire Troops
#     (n/h/b — the hire AUTOSAVES the company, §3.8), 1=Roster (deploy 2
#     toggles + blank exits), 4=Deploy (prompt re-deploys row 2), 7=Camp
#     (gladiator composes no camp, so the guard line prints and Team Build
#     re-presents without consuming further input), 10=Scenario,
#     11=Difficulty, 12=Lineup, 6=GO!, 8=Back.
#   Lineup page (host rows: 1..8 the four teams' FILL/MAP UNITS knobs,
#     9=Split even, 10=Split fair, 11=Unite, 12=Back — amendment B6 deleted
#     the FIGHTERS row, so the strip moved up one): 1 steps TEAM 1's FILL
#     wheel one place (amendment 3 C5 retired the classic gating, so the
#     knob is live on gladiator too, and W7-G's staged census makes the
#     bands read what the SDL screen reads); 12 backs out.
#   Scenario submenu (7 items — the missions door retired into the camp;
#     6=Replay Level, #207; amendment B5 retired the TROOPS row, which is
#     the one row this branch REMOVED, so replay and back each moved up
#     one): 4=Matchup (set preferred-team metadata, blank exits),
#     3=View Scenario (blank dismisses), 7=Back.
#   Difficulty submenu (7 items): 7=Back straight out.
#   Protocol session after GO!: state, quit.
#   Main: 6=Quit (the difficulty door left this menu, so every row below it
#     moved up one).
cat > "$TMPIN" << 'INP'
1


8
2
3
n
h
b
1
deploy 2

4
2
7
10
4
play 1

3

7
11
7
12
1
12
6
state
quit
8
6
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

# #206 Camp: the gladiator campaign composes no camp, so the door answers
# with the guard line instead of opening a second copy of the roster. (The
# literal is pinned in C++ by tests/unit/test_platform_headless.cpp; a shell
# drive cannot reference the exported constant, so this is the one place it
# is spelled twice — deliberately, as the end-to-end proof.)
if not any('This campaign keeps no camp.' in l for l in lines):
    print('FAIL: expected the Camp door guard line', file=sys.stderr)
    sys.exit(1)
if any('Camp # ' in l for l in lines):
    print('FAIL: the guard path must never open the camp prompt', file=sys.stderr)
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
# Amendment A1/A3: TEAMS is not a control any more, so its readout is gone
# from this screen too. How many teams fight is the LINEUP page's answer.
if any('Teams:' in l for l in lines):
    print('FAIL: the retired TEAMS readout leaked into text Matchup',
          file=sys.stderr)
    sys.exit(1)

# LINEUP (docs/lineup-design.md §8): the four bands and the two shared
# band labels (amendments B1/B6 — one FILL wheel, one MAP UNITS box, and no
# FIGHTERS page behind them).
if not any('--- Lineup ---' in l for l in lines):
    print('FAIL: expected the Lineup page banner', file=sys.stderr)
    sys.exit(1)
if not any('TEAM 1 RED' in l for l in lines):
    print('FAIL: expected the TEAM 1 band header', file=sys.stderr)
    sys.exit(1)
# gladiator is a CLASSIC (non-versus) campaign, and since amendment 3 C5 the
# match machinery lives in packs/core and runs on a mode-less level, so the
# knob rows are LIVE here: no MAP RULES mark, no refusal, and one press steps
# the wheel. This drive asserted the opposite until C5.
if not any('TEAM 1  FILL: FAIR' in l for l in lines):
    print('FAIL: expected the bare FILL row on a classic campaign',
          file=sys.stderr)
    sys.exit(1)
if not any('TEAM 1  MAP UNITS: ON' in l for l in lines):
    print('FAIL: expected the MAP UNITS row beside the FILL wheel',
          file=sys.stderr)
    sys.exit(1)
# W7-G: the page censuses the world its own VIEW LEVEL stages, so a stored
# default RESOLVES here exactly as it does on the SDL band. Gladiator scen 1
# authors nothing onto teams 3 and 4, so the terminal must say NONE there —
# it read FAIR everywhere until the census was fed, which is the disagreement
# between the three clients this drive now pins shut end to end.
for row in ('TEAM 3  FILL: NONE', 'TEAM 4  FILL: NONE'):
    if not any(row in l for l in lines):
        print('FAIL: expected ' + row + ' from the staged presence census',
              file=sys.stderr)
        sys.exit(1)
if any('MAP RULES' in l for l in lines):
    print('FAIL: the retired MAP RULES gating leaked into the Lineup page',
          file=sys.stderr)
    sys.exit(1)
if not any('FILL: STRONG' in l for l in lines):
    print('FAIL: a classic campaign must cycle the FILL wheel', file=sys.stderr)
    sys.exit(1)
# B6: FIGHTERS is deleted, not hidden. MATCHUP moves a colour and the DEPLOY
# row benches, so nothing on this page offers a second door to them.
if any('--- Fighters ---' in l for l in lines):
    print('FAIL: the retired FIGHTERS page leaked into the Lineup strip',
          file=sys.stderr)
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
