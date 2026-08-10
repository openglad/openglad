#!/usr/bin/env bash
# imaginations_playtest.sh — Imaginations balance playtest harness (NOT CI).
#
# Drives openglad_text --protocol over every Imaginations level x 3 seeds,
# with a crew leveled per the campaign's difficulty curve
# (campaigns/imaginations/README.md, "Difficulty curve" — anchored at
# crew 1 for fresh teams). Each run ticks in 300-tick slices up to 6000
# ticks (post-game slices cost 1 tick each — the protocol's tick command
# breaks early once game_ended), taking a census every slice, and emits
# ONE summary JSON line into $OUT.
#
# The summary line per run:
#   level, seed, crew_level, title, ticks, game_ended, ending, next_level,
#   level_done  — final win/loss flags (NOTE: levels with exits never
#                 auto-end; level_done==1 means "foes cleared, exit waits")
#   named       — every named Living seen: {name, team, min_hp, dead}
#   crew_alive/crew_dead/crew_min_hp — the spawned stand-in crew
#   crew_wipe_tick / foes_cleared_tick / level_done_tick / crew_alive_at
#   teams_final/dormant_final — per-team alive/dormant counts at the end
#   timeline    — [[tick, alive_t0..t7, dormant_t0..t7], ...] per census
#
# Usage:
#   scripts/imaginations_playtest.sh                  # full sweep
#   scripts/imaginations_playtest.sh --levels "1"     # subset
#   OUT=/tmp/results.jsonl JOBS=8 scripts/imaginations_playtest.sh
#
# Env knobs: BUILD_DIR (build/ci-test), OUT
#            ($TMPDIR/imaginations_playtest_results.jsonl),
#            SEEDS ("42 1337 2025"), JOBS (4), CREW ("0,0,0,0" = 4 soldiers),
#            SLICE (300), MAX_SLICES (20), RUN_TIMEOUT (300s per run),
#            CL_DELTA (0) — added to every level's curve crew level (clamped
#            to >= 1); the bracket sweeps run CL_DELTA in {-1, 0, +1}.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/ci-test}"
BIN="${OPENGLAD_TEXT:-$BUILD_DIR/openglad_text}"
OUT="${OUT:-${TMPDIR:-/tmp}/imaginations_playtest_results.jsonl}"
SEEDS="${SEEDS:-42 1337 2025}"
JOBS="${JOBS:-4}"
CREW="${CREW:-0,0,0,0}"          # 4x FAMILY_SOLDIER: the balance-smoke stand-in
SLICE="${SLICE:-300}"
MAX_SLICES="${MAX_SLICES:-20}"   # 20 x 300 = 6000 ticks max
RUN_TIMEOUT="${RUN_TIMEOUT:-300}"
CL_DELTA="${CL_DELTA:-0}"        # bracket-sweep offset from the curve level
CAMPAIGN="imaginations"

# Crew power entering each level (campaigns/imaginations/README.md,
# "Difficulty curve"): the dream-log stays anchored at crew 1.
declare -A CREW_LEVEL=(
    [1]=1
)
ALL_LEVELS="1"

if [ ! -x "$BIN" ]; then
    echo "FAIL: openglad_text not found at $BIN (build with: cmake --build --preset ci-test --target openglad_text)" >&2
    exit 1
fi

# Asset discovery (builtin/ campaigns, cfg fallback) is cwd-relative; run
# from the repo root like the unit-test working directory.
cd "$ROOT"

run_one() {
    local level="$1" seed="$2"
    local crew_level=$(( CREW_LEVEL[$level] + CL_DELTA ))
    if [ "$crew_level" -lt 1 ]; then crew_level=1; fi
    local tmphome tmpout
    tmphome=$(mktemp -d)
    tmpout=$(mktemp)
    # Sandbox HOME/config: headless runs must not clobber the user's (or the
    # repo's) openglad.yaml.
    {
        for ((i = 0; i < MAX_SLICES; i++)); do
            printf 'tick %d\ncensus\n' "$SLICE"
        done
        printf 'events\nquit\n'
    } | HOME="$tmphome" OPENGLAD_CONFIG_DIR="$tmphome/config/" \
        timeout "$RUN_TIMEOUT" "$BIN" --protocol \
        --campaign "$CAMPAIGN" --level "$level" --team "$CREW" \
        --team-level "$crew_level" --seed "$seed" \
        > "$tmpout" 2>/dev/null || {
        echo "{\"level\":$level,\"seed\":$seed,\"crew_level\":$crew_level,\"error\":\"run failed or timed out\"}"
        rm -f "$tmpout"
        rm -rf "$tmphome"
        return 0
    }

    python3 - "$tmpout" "$level" "$seed" "$crew_level" <<'PY'
import json, sys

path, level, seed, crew_level = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
title = None
last_tick = {}
timeline = []
named = {}          # name -> {team, min_hp, dead, death_tick}
crew_final = []
teams_final = [0] * 8
dormant_final = [0] * 8
save_all_loss = False
endgame_events = 0
crew_wipe_tick = None
foes_cleared_tick = None
level_done_tick = None
crew_alive_at = {}

with open(path, encoding="utf-8") as f:
    for raw in f:
        raw = raw.strip()
        if not raw:
            continue
        try:
            msg = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if msg.get("status") == "ready":
            title = msg.get("title")
        elif msg.get("cmd") == "tick":
            results = msg.get("results", [])
            if results:
                last_tick = results[-1]
            if level_done_tick is None:
                for r in results:
                    if r.get("level_done") == 1:
                        level_done_tick = r.get("tick", 0)
                        break
        elif msg.get("cmd") == "census":
            teams = msg.get("team_counts", [0] * 8)
            dormant = msg.get("dormant_counts", [0] * 8)
            teams_final, dormant_final = teams, dormant
            tick = msg.get("tick", 0)
            timeline.append([tick] + teams + dormant)
            if foes_cleared_tick is None and \
                    sum(teams[1:]) + sum(dormant[1:]) == 0:
                foes_cleared_tick = tick
            for entry in msg.get("named", []):
                name = entry.get("name", "")
                rec = named.setdefault(
                    name, {"team": entry.get("team", -1),
                           "min_hp": entry.get("hp", 0), "dead": False,
                           "death_tick": None})
                rec["min_hp"] = min(rec["min_hp"], entry.get("hp", 0))
                if entry.get("dead") and not rec["dead"]:
                    rec["death_tick"] = tick
                rec["dead"] = rec["dead"] or bool(entry.get("dead"))
            crew_final = msg.get("crew", [])
            alive_now = sum(1 for c in crew_final if not c.get("dead"))
            crew_alive_at[str(tick)] = alive_now
            if crew_wipe_tick is None and crew_final and alive_now == 0:
                crew_wipe_tick = tick
        elif msg.get("cmd") == "events":
            for ev in msg.get("events", []):
                if ev.get("kind") == 13:  # EventKind::EndGame
                    endgame_events += 1
                    if ev.get("a") == 4:  # SCEN_TYPE_SAVE_ALL
                        save_all_loss = True

crew_alive = sum(1 for c in crew_final if not c.get("dead"))
crew_dead = sum(1 for c in crew_final if c.get("dead"))
crew_min_hp = min((c.get("hp", 0) for c in crew_final), default=None)

summary = {
    "level": level,
    "seed": seed,
    "crew_level": crew_level,
    "title": title,
    "ticks": last_tick.get("tick", 0),
    "game_ended": last_tick.get("game_ended", False),
    "ending": last_tick.get("ending", -1),
    "next_level": last_tick.get("next_level", -1),
    "level_done": last_tick.get("level_done", -1),
    "save_all_loss": save_all_loss,
    "endgame_events": endgame_events,
    "named": [{"name": k, **v} for k, v in sorted(named.items())],
    "crew_wipe_tick": crew_wipe_tick,
    "foes_cleared_tick": foes_cleared_tick,
    "level_done_tick": level_done_tick,
    "crew_alive_at": crew_alive_at,
    "crew_alive": crew_alive,
    "crew_dead": crew_dead,
    "crew_min_hp": crew_min_hp,
    "teams_final": teams_final,
    "dormant_final": dormant_final,
    "timeline": timeline,
}
print(json.dumps(summary, separators=(",", ":")))
PY
    rm -f "$tmpout"
    rm -rf "$tmphome"
}

if [ "${1:-}" = "--one" ]; then
    run_one "$2" "$3"
    exit 0
fi

LEVELS="$ALL_LEVELS"
if [ "${1:-}" = "--levels" ]; then
    LEVELS="$2"
fi

# Each parallel run writes its own file; the parent concatenates afterwards
# so summary lines can never interleave regardless of size.
RUNDIR=$(mktemp -d)
trap 'rm -rf "$RUNDIR"' EXIT
export RUNDIR
SCRIPT="$ROOT/scripts/imaginations_playtest.sh"
export SCRIPT

start=$(date +%s)
for level in $LEVELS; do
    for seed in $SEEDS; do
        echo "$level $seed"
    done
done | xargs -P "$JOBS" -n 2 bash -c \
    'bash "$SCRIPT" --one "$1" "$2" > "$RUNDIR/run-$(printf "%02d" "$1")-$2.jsonl"' _
cat "$RUNDIR"/run-*.jsonl > "$OUT"
elapsed=$(( $(date +%s) - start ))

runs=$(wc -l < "$OUT")
echo "wrote $runs run summaries to $OUT in ${elapsed}s" >&2
