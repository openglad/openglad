---
name: openglad-campaign-design
description: Designing, generating, playtesting, and shipping OpenGlad campaigns (levels, story, balance). Use when creating or modifying campaigns, levels, or the mapgen tools.
---

# OpenGlad campaign design

Every rule here was paid for building War of the Westlands (26 levels, 6 waves of
fixes, one human playtest with 29 bug reports). The reference implementation is
tools/westlands_mapgen + tests/unit/test_westlands_levels.cpp.

## Campaign anatomy

A campaign is `builtin/<id>.glad` (a zip): campaign.yaml (title, first_level,
suggested_power), icon.png (32x32 indexed our.pal), scen/scenN.fss, pix/ grids.
Drop-in discovery — zero code registration. Scen numbers are PER-CAMPAIGN
namespaces; use 1..N (get_accessible_levels hardcodes 1 as always-accessible).
Only ONE campaign is mounted at a time; cross-campaign exit destinations dangle.

## Build campaigns with a generator tool, never by hand

Clone the westlands_mapgen shape: multi-file (one TU per act; builders.h shared
helpers), ExpectedLevel rows per level, and a SELF-CHECK that hard-fails on:
footing (every entity passable at its footprint), aligned stair pairs per floor
boundary, briefing/title/name budgets, army counts, MAXOBS, exit destinations
exist in the package, EVERY living+generator A*-reachable from the lead start
marker (empty allowlist unless deliberate), and no walkable-adjacent PIX_AIR
over unstandable ground (fall-line rule). The tool's expectations and the CI
test pins move in LOCKSTEP with the package — regenerate + re-pin in one change.
Regeneration must be content-reproducible (compare zip MEMBER hashes, not the
container — mtimes differ).

## Hard budgets (silently truncated or test-enforced)

- Title ≤30 bytes; briefing lines ≤33 chars (COUNT them); names ≤11 chars
  ("Ranger-King" exactly fits); grid_file = scen{:04d} (8 bytes).
- MAXOBS 150 total livings INCLUDING generator output — author ≤120.
- Start markers: ≥8, 2x2 tiles of clearance each, LEAD MARKER FIRST (oblist
  order = deploy order).

## Level mechanics that bite

- Exits are Treasure/FAMILY_EXIT objects; the DESTINATION is the exit's stats
  level. Multiple exits = branching (accessible-levels expands exits of
  completed levels). Backtrack exits to predecessors power the withdraw flow.
- A level WITH any exit NEVER auto-ends — someone must walk to the exit
  (ACT_CONTROL only; AI can never exit). A level with NO exit auto-advances to
  id+1 on extermination — the destination must exist.
- SCEN_TYPE_SAVE_ALL fails the mission when a protected walker dies. Scoping:
  summoned/owned walkers NEVER count; if any placed NPC has npc_flags bit2
  ("protected"), ONLY bit2 walkers are watched — set bit2 on exactly the
  characters whose death should end the mission (else every named ally is a
  loss condition; archmage summons are literally named "Phantom").
- npc_flags (v10+ reserved[3]): bit0 specials_disabled, bit1 start-as-guard
  (ACT_GUARD holds posts), bit2 protected. spawn_delay (reserved[4-5]) makes
  dormant walkers: they hold level_done open, show in the NEXT WAVE HUD, are
  not switchable/targetable, and wake with a flash.
- Weather: ≥40 snow tiles on any floor forces WeatherKind::Snow after the roll.
- Decor layer (v11): paint ambience decor (torches/braziers/shrubs/bones/
  pebbles/stones) on the decor plane; base autotiles underneath. Blocking decor
  must stay OFF required routes (the reachability check catches). DECOR_SHRUB
  conceals (forestwalk + LOS decay) — keep it off battle lanes or smoke bounds
  shift. Writer downgrades v11→v10/v9 when all decor planes are empty.

## Story rules (the "story bible" discipline)

One narrative voice across all briefings; briefings are the ONLY narrative
channel — each one advances the tale AND names the player's branch choices
in-fiction. No verbatim text from source sagas when adapting (paraphrase).
Character arcs must be continuous: nobody appears after dying; returns are
foreshadowed; a betrayal needs a prior guide-beat. A coherence JUDGE pass that
recounts every budget and re-walks the exit graph catches what designers miss.

## Playtesting and calibration (the harness)

scripts/westlands_playtest.sh drives openglad_text --protocol (--team-level,
census command: per-team alive/dormant + named-hero hp). Interpretation guards:
- The AI stand-in crew is a VERY pessimistic floor (brawler AI cannot kite,
  heal, or exit). Judge RELATIVE difficulty and structural pathologies, not
  absolute winnability.
- Exit-bearing levels never end in unattended runs — read foe-decay curves,
  not game_ended. Bearer deaths that occur AFTER the crew wipes are artifacts.
- Structural pathologies the sweeps catch: sides that never engage (pathing
  dead zones), generator flooding, waves arriving after the battle is decided,
  wedged melee pairs (fixed in engine, but watch for new shapes).
Calibration method: bracket sweeps at crew {curve-1, curve, curve+1} x 3 seeds
x 2 rosters; per-type gates (kill-all: clear at curve 2/3 seeds; exit: escape
viability + protected survival; defense: hold the design band); tune builders
only; the curve table lives in campaign_meta and a cheap CI test pins it.

## Pitfalls

- cfg clobber: headless runs with nothing mounted rewrite repo cfg/openglad.yaml;
  mount in-test, heal PhysFS after sabotage-style tests (heal_unit_filesystem).
- The harness reads the STAGED package (build/ci-test/builtin/) — regenerate
  restages it (generate script does this; verify when adding new tools).
- Engine-adjacent edits (passability, movement, spawning) are parity-gated:
  og_test_parity 187+ goldens must stay byte-identical unless the change is a
  documented deliberate fix (audit every regenerated golden, add a
  docs/GAMEPLAY_FIXES_FROM_CLASSIC.md row); canary pins in scenario_table.h
  are line+text anchored — repin after insertions.
- Capture scenes for the review site: keyframed spectator camera per
  scripts/fx_review/README.md; SAVE_ALL must be stripped and the lead
  film-armored in recorders (documented in test_game_loop.cpp).
