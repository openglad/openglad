#!/usr/bin/env bash
# test_text_client_ffa.sh — Headless FFA run on the shipped modes campaign:
# scen850 (FFA: THE MELEE) through the openglad_text protocol
# (docs/ffa-design.md §12 integration paragraph). Verifies:
#   1. The client reaches the playing state (status: ready) on scen850
#   2. All 300 ticks complete with the match still undecided
#   3. The mode block reports the FFA match live: MODE_ID var 7, one band
#      slot per deployed crew member, and at least two live fighters
#      wearing distinct band bytes 16-31
#   4. The client exits cleanly on "quit"
#
# Amendment 4 (E3) sets the headcount this expects. --protocol carries no
# match knobs — there is no picker save behind it — so every FILL band sits
# at its stored NONE and the arena fields NO init bots. The band is the
# two-member crew alone, which is still a live FFA match (two fighters is
# the floor), so everything this harness measures about the band survives;
# only the population moved. Filling the arena's other six seats is a host
# decision now, and the CLI has no way to make it.
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
printf 'tick 300\nstate\nquit\n' | HOME="$TMPHOME" OPENGLAD_CONFIG_DIR="$TMPHOME/config/" timeout "$TEXT_TIMEOUT" "$TEXT_BIN" --protocol --campaign modes --level 850 --team 0,1 --seed 42 > "$TMPOUT" 2>/dev/null
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
if len(lines) < 4:
    print('FAIL: Expected at least 4 JSON lines, got', len(lines), file=sys.stderr)
    sys.exit(1)

# Line 1: ready message on the FFA arena
ready = json.loads(lines[0])
if ready.get('status') != 'ready':
    print('FAIL: First line is not ready:', lines[0], file=sys.stderr)
    sys.exit(1)
if ready.get('level') != 850:
    print('FAIL: Expected level 850, got', ready.get('level'), file=sys.stderr)
    sys.exit(1)

# Line 2: tick results — the whole window runs and the match stays open
tick_result = json.loads(lines[1])
results = tick_result.get('results', [])
if len(results) != 300:
    print(f'FAIL: Expected 300 tick results, got {len(results)}', file=sys.stderr)
    sys.exit(1)
if results[-1].get('tick') != 300:
    print(f'FAIL: Last tick should be 300, got {results[-1].get("tick")}', file=sys.stderr)
    sys.exit(1)
if results[-1].get('game_ended'):
    print('FAIL: FFA match must still be undecided at tick 300', file=sys.stderr)
    sys.exit(1)

# Line 3: state dump — the FFA match is live on the fighter band
state = json.loads(lines[2])
mode = state.get('mode', {})
if mode.get('active') is not True:
    print('FAIL: mode.active should be true:', mode, file=sys.stderr)
    sys.exit(1)
if mode.get('name') != 'FFA':
    print('FAIL: mode.name should be FFA, got', mode.get('name'), file=sys.stderr)
    sys.exit(1)
mode_vars = mode.get('vars', [])
if len(mode_vars) != 64 or mode_vars[0] != 7:
    print('FAIL: MODE_ID var should be 7 (FFA):', mode_vars[:16], file=sys.stderr)
    sys.exit(1)
# One slot per deployed crew member and not one more: E3 says the arena
# backfills nobody unless a host turns a wheel, and --protocol has no wheel
# to turn. The count is the `--team 0,1` list above.
CREW = 2
bitmap = mode_vars[14]
taken = [c for c in range(16) if (bitmap >> c) & 1]
if len(taken) != CREW:
    print(f'FAIL: scen850 should fill {CREW} band slots (the crew, and no '
          f'backfill under E3), bitmap {bitmap} has {len(taken)}',
          file=sys.stderr)
    sys.exit(1)
for c in taken:
    if mode_vars[32 + c] == 0:
        print(f'FAIL: taken slot {c} holds no fighter entity id', file=sys.stderr)
        sys.exit(1)
alive_band = [e['team'] for e in state.get('entities', [])
              if 16 <= e.get('team', 0) < 32 and not e.get('dead')]
if len(alive_band) < 2:
    print(f'FAIL: Expected >= 2 live band fighters at tick 300, got {len(alive_band)}',
          file=sys.stderr)
    sys.exit(1)
if len(set(alive_band)) != len(alive_band):
    print('FAIL: Live fighters must wear distinct band bytes:', sorted(alive_band),
          file=sys.stderr)
    sys.exit(1)

# Line 4: quit confirmation
quit_msg = json.loads(lines[3])
if quit_msg.get('status') != 'ok':
    print('FAIL: Quit message not ok:', lines[3], file=sys.stderr)
    sys.exit(1)

print(f'PASS: 300 FFA ticks on scen850, {len(taken)} band slots (crew only, '
      f'no E3 backfill), {len(alive_band)} live fighters, clean quit')
PY
