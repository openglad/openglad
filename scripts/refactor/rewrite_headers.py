#!/usr/bin/env python3
"""Boilerplate header -> one-line S2 header (Stage 2).

Style S2: every pack script opens with ONE line naming the family (or the
file's scope) and what its hooks actually do, ending with the cookbook
pointer — text no other file's header could carry.  The shipped corpus
instead opens with a 4-line-ish boilerplate block (provenance line + a
cookbook restatement that is identical in spirit across all 36 files).

This tool replaces each file's LEADING comment block with a curated S2
header.  Curation is per-file data, not guesswork: the replacement for each
file was written against its actual hooks, and load-bearing per-file notes
that happened to live inside the old header (RNG-order records, caller
contracts, name-resolution quirks — S3 comment kinds 1 and 2) are carried
over verbatim as extra lines below the S2 line.  Files that intentionally
register nothing (tower1, giant_skeleton) keep their why-empty explanation,
as S2 requires.

The tool refuses to touch a file whose leading block does not match the
recorded fingerprint (first line) — if the corpus drifted, re-curate before
running.  Header collapses shift every line below them: all 36 files need
the pin re-point + canary flip flow afterwards (see s2_partition.json).
"""

from __future__ import annotations

import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from lua_corpus import Source, run_rewriter  # noqa: E402

CB = "(cookbook: docs/lua-classpacks-design.md §3)."

# file -> (first-line fingerprint prefix, [replacement header lines])
HEADERS: dict[str, tuple[str, list[str]]] = {
    "archer.lua": ("-- core:archer", [
        f"-- core:archer — arrow volleys, flurry, exploding shot; melee backpedal {CB}",
    ]),
    "archmage.lua": ("-- core:archmage", [
        f"-- core:archmage — teleport/marker, heartburst/chain, summons, mind control {CB}",
        "-- rng order: hit_response rand(3)/rand(2); illusion tier rand(3/5/7/9);",
        "-- mind-control rand(20)/rand(8) + charm_duration draw.",
        "-- Unlike the Mage twin, marker notifications here are NOT gated on",
        "-- user() ~= -1 — an intentional divergence between the twins.",
    ]),
    "barbarian.lua": ("-- core:barbarian", [
        f"-- core:barbarian — boulder-toss special {CB}",
    ]),
    "big_orc.lua": ("-- core:orc_captain", [
        f"-- core:orc_captain — level_up only {CB}",
        '-- (Descriptor .name "ORC CAPTAIN" → family id core:orc_captain.)',
    ]),
    "cleric.lua": ("-- core:cleric", [
        f"-- core:cleric — heal/mace, raise skeleton/ghost, turn undead, resurrect {CB}",
    ]),
    "druid.lua": ("-- core:druid", [
        f"-- core:druid — plant tree, summon faerie, reveal items, protection circle {CB}",
    ]),
    "effect_bomb.lua": ("-- core:bomb and core:explosion", [
        f"-- core:bomb + core:explosion — the thief's bomb and its blast {CB}",
    ]),
    "effect_chain.lua": ("-- core:chain", [
        f"-- core:chain — homing chain-lightning bolt; forks on strike {CB}",
        "--",
        "-- The frame poke `self->set_frame(self->ani[curdir()][0])` reads the walker's",
        "-- animation table through og.ani_frame(entity, row, index); nil covers every",
        "-- case where the C++ `if (self->ani)` guard would have failed, so the",
        "-- set_frame is simply skipped then.",
    ]),
    "effect_cloud.lua": ("-- core:cloud", [
        f"-- core:cloud — drifting, fading poison cloud {CB}",
        "--",
        "-- The drift step is statistics::do_command() (src/gameplay/stats.cpp), reached",
        "-- from `self->stats()->do_command()` whenever the cloud already has a queued",
        "-- walk — which is every tick after the first. walker:s_do_command() is the",
        "-- only binding that EXECUTES the queue (the other s_*_command calls just",
        "-- manipulate it); its short return value is discarded here exactly as in C++.",
    ]),
    "effect_door_open.lua": ("-- core:door_open", [
        f"-- core:door_open — hands the opened-door sprite to a fresh effect {CB}",
    ]),
    "effect_ghost_scare.lua": ("-- core:ghost_scare", [
        f"-- core:ghost_scare — expiring scare cloud frights nearby foes {CB}",
        "--",
        "-- on_death applies the fright through walker:s_force_fright(iterations,",
        "-- dx, dy) — statistics::force_fright, the scare-MERGE variant of",
        "-- force_command that inspects the command queue front (forced +",
        "-- COMMAND_WALK → refresh count/direction instead of prepending).",
        "-- s_force_command is NOT a substitute: it stacks overlapping scares",
        "-- end-to-end, which is exactly what runaway-specials §3.2 removed.",
    ]),
    "effect_knife_back.lua": ("-- core:knife_back", [
        f"-- core:knife_back — thrown blade walks back to its owner, probing hits {CB}",
    ]),
    "effect_shield.lua": ("-- core:magic_shield and core:boomerang", [
        f"-- core:magic_shield + core:boomerang — orbiting guard effects {CB}",
        "-- The ORBIT_X/ORBIT_Y tables are pure constants (R6-clean: no sim state).",
    ]),
    "elf.lua": ("-- core:elf", [
        f"-- core:elf — rock-spread specials {CB}",
        "--",
        "-- elf_rng() (gameplay override if installed, else world rng) is og.rand:",
        "-- the binding routes to world->rng_, which is the same stream in both",
        "-- production and the TESTING sim-override path.",
    ]),
    "faerie.lua": ("-- core:faerie", [
        f"-- core:faerie — level_up only {CB}",
    ]),
    "fire_elemental.lua": ("-- core:elemental", [
        f"-- core:elemental — starburst, parting shot on death, owner drain {CB}",
        '-- (Descriptor name "ELEMENTAL" is unique in the living registry.)',
    ]),
    "ghost.lua": ("-- core:ghost", [
        f"-- core:ghost — scare-cloud special {CB}",
    ]),
    "giant_skeleton.lua": ("-- core:beast (giant skeleton)", [
        f"-- core:beast (giant skeleton) — no hooks {CB}",
        "-- Every behavior-callback slot in the C++ descriptor was null, so this",
        "-- family has no scripted hooks: og.register_hooks with an empty hook table",
        "-- is a load error by design, and registering any hook here would ADD",
        "-- behavior the family never had. Deliberately a no-op chunk.",
    ]),
    "golem.lua": ("-- core:beast (golem", [
        f"-- core:beast (golem) — set_difficulty only {CB}",
        '-- The descriptor .name is "BEAST"; "core:beast" resolves to FAMILY_GOLEM',
        "-- (id 18), the first BEAST-named family in registry scan order (giant",
        "-- skeleton and tower1 share the display name but later ids).",
    ]),
    "mage.lua": ("-- core:mage", [
        f"-- core:mage — teleport/marker, starburst, freeze time, wave, heartburst {CB}",
        "-- rng: the mage's own hooks draw nothing directly.",
    ]),
    "orc.lua": ("-- core:orc", [
        f"-- core:orc — yell stun, corpse eating {CB}",
    ]),
    "skeleton.lua": ("-- core:skeleton", [
        f"-- core:skeleton — ranged self-teleport special {CB}",
    ]),
    "slime.lua": ("-- core:#8 / core:#9 / core:#10", [
        f"-- core:#8/#9/#10 (slime trio) — split on death/ani-complete, grow specials {CB}",
        '-- All three living descriptors share the registry name "SLIME", so the',
        '-- numeric escapes are mandatory (api-reference: "core:#<id>").',
    ]),
    "soldier.lua": ("-- core:soldier", [
        f"-- core:soldier — charge, boomerang, whirlwind, disarm {CB}",
    ]),
    "thief.lua": ("-- core:thief", [
        f"-- core:thief — bomb, cloak, taunt/charm, poison cloud {CB}",
    ]),
    "tower1.lua": ("-- core tower1", [
        f"-- core tower1 — no hooks {CB}",
        "--",
        "-- The tower1 descriptor wires NO behavior callbacks (every hook slot is",
        "-- null; the family is a stationary shooter driven entirely by descriptor",
        "-- data and generic walker/living code), so there is nothing to register",
        "-- here. og.register_hooks with an empty hook table is a load error by",
        "-- design, so this file deliberately makes no registration call.",
        "--",
        '-- Note: the descriptor\'s display name is "BEAST", which it shares with',
        "-- FAMILY_GOLEM (18) and FAMILY_GIANT_SKELETON (19). String-id resolution",
        '-- ("core:beast") returns the first match in registry scan order — the',
        "-- golem — so tower1 (20) is not addressable by name anyway until core pack",
        "-- ids are pinned by classpack.yaml (design doc §5).",
    ]),
    "treasure_consumables.lua": ("-- core treasure consumables", [
        f"-- core drumstick + five potions — on_eat hooks {CB}",
    ]),
    "treasure_ctf.lua": ("-- core CTF treasures", [
        f"-- core:flag + core:waypoint — CTF touch hooks {CB}",
        "--",
        "-- core:waypoint is fully live: control points are occupancy-driven (CTF",
        "-- phase 5 owns them from ctf_run_tick), so a touch is genuinely a no-op",
        "-- returning true (src/gameplay/ctf/ctf.cpp:1170-1176).",
        "--",
        "-- core:flag is one call: og.ctf_on_flag_touch(self, eater) wraps",
        "-- og::sim::ctf_on_flag_touch (src/gameplay/ctf/ctf.cpp) exactly as the C++",
        "-- hook did. The flag's whole body is CtfState machinery — team_active, the",
        "-- per-team flag records, send_flag_home, capture counting and scoring — so",
        "-- the rules stay in the CTF engine rather than being re-invented in Lua.",
    ]),
    "treasure_navigation.lua": ("-- core treasure navigation", [
        f"-- core:exit + core:teleporter — exit pad and warp pad on_eat {CB}",
        "--",
        "-- The exit pad reads and writes campaign progression state through its own",
        "-- bindings:",
        '--   * og.scenario_title("scen<N>")     get_scenario_title (title lookup;',
        '--                                      "none" when no reader is installed,',
        "--                                      the same answer a failed read gives)",
        "--   * og.world_can_exit_whenever()     world.type & TYPE_CAN_EXIT_WHENEVER",
        "--   * og.level_completed(level)        world.completed_levels.count(level)",
        "--   * og.current_scenario()            world.current_scenario",
        "--   * og.set_withdraw_request(level)   world.withdraw_requested/withdraw_level",
        "--   * og.emit_exit_confirmation(...)   EventKind::RequestExitConfirmation",
        "--   * og.emit_withdraw_to_level(level) EventKind::WithdrawToLevel",
        "-- can_exit_now/can_withdraw are C++ locals computed before the branch; Lua's",
        "-- and/or short-circuits, which is safe here because every one of those reads",
        "-- is pure (no sim writes, no draws).",
    ]),
    "treasure_valuables.lua": ("-- core treasure valuables", [
        f"-- core gold/silver bar, life gem, key — on_eat hooks {CB}",
        "--",
        "-- The three scoring families bank through og.award_score(team, points) — the",
        "-- binding for the file-local award_score() helper, which adds to",
        "-- world->m_score[team] and emits EventKind::ScoreChange. Teams outside the",
        "-- score table are dropped inside the binding, exactly like the C++",
        "-- is_valid_score_team() guard, so callers pass the raw team number.",
    ]),
    "weapon_animate.lua": ("-- core:tree, core:blood", [
        f"-- core:tree/blood/circle_protection/glow/sprinkle — on_animate; freeze hit {CB}",
        "--",
        "-- CALLER CONTRACT (weap::animate): a hook returning FALSE makes the caller",
        "-- run death(). tree/blood and glow always return true — glow calls death()",
        "-- itself — while circle_protection returns false to hand the death off,",
        "-- and must NOT call death() on its own.",
    ]),
    "weapon_door.lua": ("-- core:door", [
        f"-- core:door — broken door hands its spot to the opening effect {CB}",
    ]),
    "weapon_knife.lua": ("-- core:knife", [
        f"-- core:knife — returning-blade spawn on death {CB}",
        "--",
        "-- The returning gate reads the knife OWNER's living-family descriptor:",
        '-- og.family_flag("living", owner:family(), "has_returning_weapon"). A nil',
        "-- owner is the C++ `owner_fd == nullptr` case: no special handling.",
    ]),
    "weapon_projectiles.lua": ("-- core:fire_arrow + core:boulder", [
        f"-- core:fire_arrow + core:boulder — explode on death when armed {CB}",
    ]),
    "weapon_rock.lua": ("-- core:rock", [
        f"-- core:rock — bouncing rock reflects off whichever axis is open {CB}",
    ]),
    "weapon_wave.lua": ("-- core:wave and core:wave2", [
        f"-- core:wave/wave2 — three-stage energy wave promotion {CB}",
        "--",
        "-- WAVE dying becomes WAVE2, WAVE2 dying becomes WAVE3, and WAVE3 (no",
        "-- on_death) finally expires. Each stage un-deads itself first so",
        "-- weap::death()'s caller sees a live walker, and refills hitpoints so the",
        "-- new stage starts at full strength.",
    ]),
}


def transform(src: Source):
    entry = HEADERS.get(src.path.name)
    if entry is None:
        return None, [f"no curated header for {src.path.name}; skipped"]
    fingerprint, replacement = entry
    if not src.lines or not src.lines[0].startswith(fingerprint):
        print(f"rewrite_headers: ABORT: {src.path.name} first line does not "
              f"start with {fingerprint!r} — corpus drifted; re-curate",
              file=sys.stderr)
        sys.exit(2)
    # The header block is the CONTIGUOUS leading comment run.  A blank line
    # ends it: comment blocks after a blank belong to the declaration below
    # them (elf's cosmetic-RNG adjudication block must survive).
    end = 0
    while end < len(src.lines) and src.is_comment_line(end):
        end += 1
    tail = src.lines[end:]
    sep = [] if (not tail or src.is_blank_line(end)) else [""]
    new_lines = list(replacement) + sep + tail
    if new_lines == src.lines:
        return None, []
    return new_lines, [f"header collapsed: {end} -> {len(replacement)} line(s)"]


if __name__ == "__main__":
    run_rewriter("rewrite_headers", "boilerplate headers -> S2 one-liners",
                 transform)
