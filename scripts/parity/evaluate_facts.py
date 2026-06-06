#!/usr/bin/env python3
"""Python mirror of og::parity::evaluate_facts.

Reads tests/parity/scenario_facts_generated.json (one source of truth
emitted by the CMake-time scenario_facts_dump tool), evaluates the
predicates against any StateDump JSON, and reports pass/fail.

Usage:
  evaluate_facts.py [--side {branch,master}] <scenario_id> <dump.json>

`--side` defaults to `branch` and mirrors the C++ FactSide gate: a
predicate whose `applies_to_<side>` JSON field is false is short-circuited
as a pass on that side.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
FACTS = REPO_ROOT / "tests" / "parity" / "scenario_facts_generated.json"


# These must match state_dump.cpp::family_symbol exactly.
FAMILIES = {
    0:  "FAMILY_SOLDIER",
    1:  "FAMILY_ELF",
    2:  "FAMILY_ARCHER",
    3:  "FAMILY_MAGE",
    4:  "FAMILY_SKELETON",
    5:  "FAMILY_CLERIC",
    6:  "FAMILY_FIREELEMENTAL",
    7:  "FAMILY_FAERIE",
    8:  "FAMILY_SLIME",
    9:  "FAMILY_SMALL_SLIME",
    10: "FAMILY_MEDIUM_SLIME",
    11: "FAMILY_THIEF",
    12: "FAMILY_GHOST",
    13: "FAMILY_DRUID",
    14: "FAMILY_ORC",
    15: "FAMILY_BIG_ORC",
    16: "FAMILY_BARBARIAN",
    17: "FAMILY_ARCHMAGE",
    18: "FAMILY_GOLEM",
    19: "FAMILY_GIANT_SKELETON",
    20: "FAMILY_TOWER1",
}

EVENT_KINDS = [
    "none", "play_sound", "notification", "set_palette",
    "request_redraw", "end_game", "set_end",
    "request_exit_confirmation", "withdraw_to_level", "score_change",
]


def family_symbol(i: int) -> str:
    return FAMILIES.get(i, f"FAMILY_UNKNOWN_{i}")


def evaluate_one(p: dict, dump: dict) -> tuple[bool, str]:
    kind = p["kind"]
    a0   = int(p.get("arg0", 0))
    a1   = int(p.get("arg1", 0))
    a2   = int(p.get("arg2", 0))
    a3   = int(p.get("arg3", 0))
    walkers = dump.get("walkers", [])
    effects = dump.get("effects", [])
    weapons = dump.get("weapons", [])
    events  = dump.get("events", [])

    def cnt_walker(family_id, alive_only=False):
        sym = family_symbol(family_id)
        return sum(1 for w in walkers
                   if w["family"] == sym and (not alive_only or w.get("alive", True)))

    if kind == "TickReached":
        return (dump.get("tick", 0) >= a0, f"tick={dump.get('tick',0)} need>={a0}")
    if kind == "LevelDoneEquals":
        return (dump.get("level_done", 0) == a0,
                f"level_done={dump.get('level_done')} need={a0}")
    if kind == "ScoreDelta":
        s = dump.get("score_per_team", [0,0,0,0])[a0]
        return (a1 <= s <= a2, f"score={s} need in [{a1},{a2}]")
    if kind == "WalkerFamilyCount":
        n = cnt_walker(a0)
        return (a1 <= n <= a2, f"count({family_symbol(a0)})={n} need in [{a1},{a2}]")
    if kind == "WalkerOfTeamAlive":
        n = sum(1 for w in walkers if w["team"] == a0 and w.get("alive", True))
        return (a1 <= n <= a2, f"team{a0}_alive={n} need in [{a1},{a2}]")
    if kind == "WalkerHpRangeAtFinalTick":
        sym = family_symbol(a0)
        lo, hi = a1 / 100.0, a2 / 100.0
        for w in walkers:
            if w["family"] == sym and lo <= w.get("hp", 0.0) <= hi:
                return (True, "")
        return (False, f"no {sym} hp in [{lo},{hi}]")
    if kind == "WalkerKeysApplied":
        keys = dump.get("inventory_keys")
        if keys is None:
            return (True, "indeterminate: inventory_keys absent")
        return (len(keys) >= a0, f"keys={len(keys)} need>={a0}")
    if kind == "WalkerPositionMoved":
        sym = family_symbol(a0)
        for w in walkers:
            if w["family"] == sym and w["xpos"] >= a1 and w["ypos"] >= a2:
                return (True, "")
        return (False, f"no {sym} at xpos>={a1} ypos>={a2}")
    if kind == "WalkerDiedByFinal":
        return (cnt_walker(a0, alive_only=True) == 0,
                f"{family_symbol(a0)} still alive")
    if kind == "WalkerAliveAtFinal":
        n = cnt_walker(a0, alive_only=True)
        return (n >= a1, f"{family_symbol(a0)} alive={n} need>={a1}")
    if kind == "TreasureFamilyRemovedFromOblist":
        return (cnt_walker(a0) == 0, f"{family_symbol(a0)} still in oblist")
    if kind == "StatDeltaOnPickup":
        return (True, "indeterminate: no baseline in v1 dump")
    if kind == "EffectFamilyCount":
        sym = family_symbol(a0)
        a4   = int(p.get("arg4", 0))
        if a3 >= 0 and cnt_walker(a3) == 0:
            return (False, f"qualifier {family_symbol(a3)} absent")
        # tick-window upper bound (arg4 > 0): only count effects whose
        # lifetime <= arg4. arg4 == 0 == "no window filter".
        if a4 > 0:
            n = sum(1 for e in effects
                    if e["family"] == sym and e.get("lifetime", 0) <= a4)
            window = f" (window lifetime<={a4})"
        else:
            n = sum(1 for e in effects if e["family"] == sym)
            window = ""
        return (a1 <= n <= a2,
                f"effects({sym})={n} need in [{a1},{a2}]{window}")
    if kind == "EventKindAtLeast":
        name = EVENT_KINDS[a0] if 0 <= a0 < len(EVENT_KINDS) else ""
        n = sum(1 for e in events if e["kind"] == name)
        return (n >= a1, f"events({name})={n} need>={a1}")
    if kind == "EventKindExactly":
        name = EVENT_KINDS[a0] if 0 <= a0 < len(EVENT_KINDS) else ""
        n = sum(1 for e in events if e["kind"] == name)
        return (n == a1, f"events({name})={n} need={a1}")
    if kind == "WeaponFamilyEmitted":
        sym = family_symbol(a0)
        return (any(w["family"] == sym for w in weapons), f"no weapon {sym}")
    return (False, f"unknown kind {kind}")


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="evaluate_facts.py",
        description="Evaluate scenario predicates against a StateDump JSON.",
    )
    parser.add_argument(
        "--side",
        choices=("branch", "master"),
        default="branch",
        help="Which side's applies_to_<side> gate to honour. Defaults to branch.",
    )
    parser.add_argument("scenario_id")
    parser.add_argument("dump_path")
    args = parser.parse_args()

    if not FACTS.is_file():
        sys.stderr.write(
            f"evaluate_facts: {FACTS} missing — run cmake build to "
            "regenerate scenario_facts_generated.json\n"
        )
        return 2
    facts = json.loads(FACTS.read_text())
    row = next(
        (s for s in facts.get("scenarios", []) if s["id"] == args.scenario_id),
        None,
    )
    if row is None:
        sys.stderr.write(f"unknown scenario: {args.scenario_id}\n")
        return 2
    dump = json.loads(Path(args.dump_path).read_text())
    side_key = "applies_to_branch" if args.side == "branch" else "applies_to_master"
    failed: list[str] = []
    evaluated = 0
    skipped = 0
    for i, p in enumerate(row.get("predicates", [])):
        # Default true mirrors the C++ FactPredicate field default.
        if not bool(p.get(side_key, True)):
            skipped += 1
            continue
        evaluated += 1
        ok, msg = evaluate_one(p, dump)
        if not ok:
            failed.append(f"#{i} kind={p['kind']} {msg}")
    if failed:
        for f in failed:
            sys.stderr.write(f"FAIL {f}\n")
        return 1
    print(
        f"evaluate_facts {args.scenario_id} side={args.side}: OK "
        f"({evaluated} evaluated, {skipped} side-gated)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
