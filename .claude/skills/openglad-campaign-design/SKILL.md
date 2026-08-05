---
name: openglad-campaign-design
description: Designing, generating, playtesting, and shipping OpenGlad campaigns (levels, story, balance). Use when creating or modifying campaigns, levels, or the mapgen tools.
---

# OpenGlad campaign design

Every rule here was paid for building War of the Westlands (26 levels, 6 waves of
fixes, one human playtest with 29 bug reports). The reference implementation is
tools/westlands_mapgen + tests/unit/test_westlands_levels.cpp.

## Campaign anatomy

A campaign SHIPS as `<id>.glad` (a zip) but is COMMITTED as a plain source
tree `campaigns/<id>/`: campaign.yaml (title, first_level, suggested_power),
icon.png (32x32 indexed our.pal), scen/scenN.fss, pix/ grids, and — for
pack-bearing campaigns — the hand-authored `packs/<pack-id>/` subtree. The
build composes that ONE tree into `build/<preset>/builtin/<id>.glad`
deterministically, mirroring it 1:1 (scripts/make_glad.py via
og_builtin_campaigns; the per-campaign README.md provenance note is the
one file excluded). Generators rewrite only the generated entries —
export_campaign_tree preserves packs/ and README.md.
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
Regeneration must be reproducible: the tool writes campaigns/<id>/ and an
unchanged tree must regenerate to a clean `git status` (the built archive is
fully deterministic too — scripts/make_glad.py pins order, mtimes, method).

## Hard budgets (silently truncated or test-enforced)

- Title ≤30 bytes; briefing lines auto-flow at render time (keep them under
  ~60 chars for editor readability; the mapgen audits allow up to 120);
  names ≤11 chars ("Ranger-King" exactly fits); grid_file = scen{:04d}
  (8 bytes). Existing shipped campaigns still pin a 33-char briefing budget
  in their unit tests — regenerating those must keep honoring it.
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
- npc_flags (v10+ reserved[3]): bit0 specials_disabled, bit1 guard hold-post,
  bit2 protected. Start-as-guard is NOT a flag bit — it rides the per-object
  command byte (writer round-trips act_type; loader applies only
  Living+ACT_GUARD). spawn_delay (reserved[4-5]) makes dormant walkers: they
  hold level_done open, show in the NEXT WAVE HUD, are not
  switchable/targetable, and wake with a flash. The openscen SELECT panel
  authors this as the "Delay" prompt (ticks, 12/sec), and the OBJECT panel
  presets it on the brush so a whole wave places already delayed; only
  Livings and Generators can carry one.
- Guard wake policy (2026-07-11): a plain ACT_GUARD is an AMBUSH post — it
  holds until a foe is inside lineofsight() range with a clear sight ray
  (same-floor Bresenham over opaque tiles), then converts to ACT_RANDOM and
  hunts, permanently. Design consequence: room-by-room encounters survive
  (no aggro through walls), but every wakeable guard the crew can see WILL
  join the fight — guard-dense open levels get much hotter than their posted
  layout suggests. npc_flags bit1 ("hold post") keeps the classic never-move
  sentry: REQUIRED for allied-team (0/1) guards — escorts, door-wards,
  garrisons — or the wake rule marches them off the chokepoint they exist to
  hold (both mapgen self-checks enforce allied⟹hold-post on the reloaded
  package). The openscen SELECT panel authors this as the AI cycler
  ROAM/GUARD/HOLD.
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
- The harness reads the STAGED archive (build/ci-test/builtin/, composed from
  campaigns/<id>/) — the generate scripts rebuild og_builtin_campaigns after
  writing the tree (verify when adding new tools). Generators output FILE
  trees; regeneration reviews as git diffs over campaigns/<id>/.
- Engine-adjacent edits (passability, movement, spawning) are parity-gated:
  og_test_parity 187+ goldens must stay byte-identical unless the change is a
  documented deliberate fix (audit every regenerated golden, add a
  docs/GAMEPLAY_FIXES_FROM_CLASSIC.md row); canary pins in scenario_table.h
  are line+text anchored — repin after insertions.
- Capture scenes for the review site: keyframed spectator camera per
  scripts/fx_review/README.md; SAVE_ALL must be stripped and the lead
  film-armored in recorders (documented in test_game_loop.cpp).
