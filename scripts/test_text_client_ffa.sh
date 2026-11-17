#!/usr/bin/env bash
# test_text_client_ffa.sh — Headless FFA run on the shipped modes campaign:
# scen850 (FFA: THE MELEE) through the openglad_text protocol
# (docs/ffa-design.md §12 integration paragraph). Verifies:
#   1. The client reaches the playing state (status: ready) on scen850
#   2. All 300 ticks complete with the match still undecided
#   3. The mode block reports the FFA match at three samples (tick 1, tick
#      150 and tick 300): MODE_ID var 7 and one band slot per deployed crew
#      member, each slot holding the same fighter all match long, and that
#      fighter wearing its slot's band byte (16 + slot). On tick 1 both
#      fighters are alive. Later a fighter may be dead: the two trade real
#      damage, a dead fighter keeps its slot and its byte until its respawn,
#      and which of the two is down on a given tick is the fight's outcome,
#      which this harness does not judge. (Under seed 42 with the setting
#      off the soldier is down from about tick 230. With Gameplay Fixes on,
#      since fix BD, where a shooter's line check flies its test shot the
#      way the shot flies, the elf is down from about tick 140 to 230.)
#   4. The client exits cleanly on "quit"
#
# Amendment 4 (E3) sets the headcount this expects. --protocol carries no
# match knobs — there is no picker save behind it — so every FILL band sits
# at its stored NONE and the arena fields NO init bots. The band is the
# two-member crew alone, which is still a live FFA match (two fighters is
# the floor), so everything this harness measures about the band survives;
# only the population moved. Filling the arena's other six seats is a host
# decision now, and the CLI has no way to make it. Amendment 7 (#276) deals
# FILL: FAIR onto an arena's defined teams in the PICKERS (SDL, text,
# curses) and the demo; this knob-less CLI shape is deliberately left as
# the headless baseline, so the count below still holds.
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

# Two deployed heroes (soldier + archer crew) hold the 8-fighter arena on
# their own — under E3 no wheel is turned, so no init bots join them. Keep
# the script-level timeout below CTest's test timeout so sanitizer runs
# still have headroom without masking real hangs.
printf 'tick 1\nstate\ntick 149\nstate\ntick 150\nstate\nquit\n' | HOME="$TMPHOME" OPENGLAD_CONFIG_DIR="$TMPHOME/config/" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --protocol --campaign modes --level 850 --team 0,1 --seed 42 > "$TMPOUT" 2>/dev/null
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
if len(lines) < 8:
    print('FAIL: Expected at least 8 JSON lines, got', len(lines), file=sys.stderr)
    sys.exit(1)

# Line 1: ready message on the FFA arena
ready = json.loads(lines[0])
if ready.get('status') != 'ready':
    print('FAIL: First line is not ready:', lines[0], file=sys.stderr)
    sys.exit(1)
if ready.get('level') != 850:
    print('FAIL: Expected level 850, got', ready.get('level'), file=sys.stderr)
    sys.exit(1)

# Lines 2, 4 and 6: tick results (1, 149 and 150 ticks); the match stays open
results = (json.loads(lines[1]).get('results', []) + json.loads(lines[3]).get('results', [])
           + json.loads(lines[5]).get('results', []))
if len(results) != 300:
    print(f'FAIL: Expected 300 tick results, got {len(results)}', file=sys.stderr)
    sys.exit(1)
if (results[0].get('tick') != 1 or results[149].get('tick') != 150
        or results[-1].get('tick') != 300):
    print(f'FAIL: Ticks should reach 1, 150, then 300, got {results[0].get("tick")} / '
          f'{results[149].get("tick")} / {results[-1].get("tick")}', file=sys.stderr)
    sys.exit(1)
if results[-1].get('game_ended'):
    print('FAIL: FFA match must still be undecided at tick 300', file=sys.stderr)
    sys.exit(1)

# One slot per deployed crew member and not one more: E3 says the arena
# backfills nobody unless a host turns a wheel, and --protocol has no wheel
# to turn. The count is the `--team 0,1` list above.
CREW = 2


def band_fighters(state, when):
    """slot -> the fighter that holds it, for one state dump. The FFA match
    is on (MODE_ID var 7), exactly CREW band slots are taken, and each taken
    slot c holds the entity id (var 32 + c) of a living that wears the
    slot's band byte, 16 + c. A fighter's shots and effects wear its band
    byte too (with Gameplay Fixes AT, #446, the elf's blows on the soldier
    leave hit effects standing); they are not livings (order 0) and no slot
    names them, so they are not counted."""
    mode = state.get('mode', {})
    if mode.get('active') is not True:
        print(f'FAIL: mode.active should be true {when}:', mode, file=sys.stderr)
        sys.exit(1)
    if mode.get('name') != 'FFA':
        print(f'FAIL: mode.name should be FFA {when}, got', mode.get('name'), file=sys.stderr)
        sys.exit(1)
    mode_vars = mode.get('vars', [])
    if len(mode_vars) != 64 or mode_vars[0] != 7:
        print(f'FAIL: MODE_ID var should be 7 (FFA) {when}:', mode_vars[:16], file=sys.stderr)
        sys.exit(1)
    bitmap = mode_vars[14]
    taken = [c for c in range(16) if (bitmap >> c) & 1]
    if len(taken) != CREW:
        print(f'FAIL: scen850 should fill {CREW} band slots {when} (the crew, and no '
              f'backfill under E3), bitmap {bitmap} has {len(taken)}',
              file=sys.stderr)
        sys.exit(1)
    by_eid = {e.get('eid'): e for e in state.get('entities', [])}
    fighters = {}
    for c in taken:
        eid = mode_vars[32 + c]
        if eid == 0:
            print(f'FAIL: taken slot {c} holds no fighter entity id {when}', file=sys.stderr)
            sys.exit(1)
        fighter = by_eid.get(eid)
        if fighter is None or fighter.get('order') != 0:
            print(f'FAIL: slot {c} names entity {eid} {when}, which is not a living in the '
                  f'world:', fighter, file=sys.stderr)
            sys.exit(1)
        if fighter.get('team') != 16 + c:
            print(f'FAIL: the fighter of slot {c} should wear band byte {16 + c} {when}, '
                  f'wears {fighter.get("team")}', file=sys.stderr)
            sys.exit(1)
        fighters[c] = fighter
    return fighters


# Line 3: state dump at tick 1. The crew is deployed, each member alive in
# a slot of its own. Nobody can be down yet: the fight has had one tick.
deployed = band_fighters(json.loads(lines[2]), 'at tick 1')
down = [c for c, fighter in deployed.items() if fighter.get('dead')]
if down:
    print(f'FAIL: every fighter should be alive at tick 1, slots {down} are dead',
          file=sys.stderr)
    sys.exit(1)

# Lines 5 and 7: state dumps at tick 150 and tick 300. The same fighters
# hold the same slots and wear the same bytes, alive or waiting to respawn.
alive_at = {}
for line, tick in ((4, 150), (6, 300)):
    fighters = band_fighters(json.loads(lines[line]), f'at tick {tick}')
    for c, fighter in deployed.items():
        if c not in fighters or fighters[c].get('eid') != fighter.get('eid'):
            print(f'FAIL: slot {c} should still hold entity {fighter.get("eid")} at tick '
                  f'{tick}, the slots read', {s: f.get('eid') for s, f in fighters.items()},
                  file=sys.stderr)
            sys.exit(1)
    alive_at[tick] = sum(1 for fighter in fighters.values() if not fighter.get('dead'))

# Line 8: quit confirmation
quit_msg = json.loads(lines[7])
if quit_msg.get('status') != 'ok':
    print('FAIL: Quit message not ok:', lines[7], file=sys.stderr)
    sys.exit(1)

print(f'PASS: 300 FFA ticks on scen850, {len(deployed)} band slots (crew only, '
      f'no E3 backfill), each held by its fighter at ticks 1, 150 and 300 '
      f'({alive_at[150]} and {alive_at[300]} of them alive), clean quit')
PY
