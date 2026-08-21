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

## The hunt AI does not pathfind — design for it

ACT_RANDOM hunters walk a STRAIGHT line at their acquired foe
(walker::act_random -> walkstep with wall-slide only); there is no
routing. Consequences for every level layout:

- Any impassable band (water, walls, tree lines) between two forces
  turns approaching units into visible statues grinding at the
  boundary. Either keep opposing forces' likely sight lines free of
  barriers, or provide crossings wide and frequent enough that the
  slide finds one within a couple of tiles from any angle.
- A woken guard hunts forever; if its target leaves, it jams against
  whatever stands where it happens to be. Levels that post guards need
  a level-script rule that stands disengaged hunters back down (the
  engine's own act_guard sight test then re-wakes them honestly).
- Every AI unit follows these rules regardless of origin — placed,
  generator-spawned, or script-converted.
- Moat/water-ring maps: any roamer whose straight line to the crew
  crosses the water jitters in place at the edge. On such maps every
  placed foe must be a posted guard (ambush wake); roamers only where
  chase ground is open. Gates through wall rings must be ≥3 tiles wide
  — the beeline AI funnels by wall-sliding and 2-wide mouths stall
  unattended runs.
- Teleport-strand rule: NO standable ground across open water anywhere
  a level has teleporting families (mage/skeleton placed OR
  generator-spawned) — a boss teleporting onto a decorative islet
  softlocks kill-all missions. Decorative islets are TREE atolls;
  bosses that must stay put carry specials_disabled.
- Diagnosis tool: the `openglad_text` protocol `state` command dumps
  live entity positions — the fastest way to find a stranded or wedged
  unit in an unattended run.

Every level whose layout puts ANY blocker between opposing forces must
ship an unattended-sim stationarity test: run the level headless for
~1800+ ticks under the scenarios players actually create (crew parked
near the front line; garrison woken then abandoned), and fail on any
living with near-zero displacement that is neither posted nor in
contact with an enemy. Prove the test red on a layout that jams before
trusting it green.

## Verify behavior rules per construction path

Entities that belong to one category can be built by different paths
(authored placement, runtime generation, delayed activation, team
conversion, state conversion), and the paths do NOT share guarantees —
derived stats, hook timing, and AI state can all differ. Any rule that
targets a category ("all X behave like Y") must be validated against
EVERY path that produces members of that category, with a behavioral
test per path proven red on a broken tree before the rule ships. A rule
proven on one construction path and assumed for the rest is the
standard way levels ship invisible statues, dead triggers, and
unreachable objectives.

## Palette, art, and font rules

- Palette indices 208–231 are do_cycle-ROTATED every frame (water bands
  + the fire ramp starting at COLOR_FIRE=224). Static art or effect
  colors in that range STROBE in-game while headless render tests stay
  blind (they never run do_cycle). Indices 232–239 are a STATIC copy of
  the fire ramp — use those for stable warm colors, and pin new effect
  colors with a test that runs do_cycle in-test.
- Any `our_palette.cpp` change re-embeds into every generated campaign's
  PNGs: CI's Campaign Regeneration Drift check regenerates ALL generated
  campaigns, so run every `scripts/generate_*_campaign.sh` and commit
  the binary PNG churn, or CI reds. The sprite loader's PLTE contract
  (og_file.cpp) rejects PNGs whose palette mismatches our_pal_lookup at
  any entry outside the synthesized-ramp exemption band — don't widen
  the exemption casually.
- The bitmap font (pix/text.png) has glyphs 0..124 only, and '&' and '|'
  are BLANK — briefings and HUD lines using them render holes (use
  "and", dashes). The apostrophe exists. Budget labels against the
  actual glyph set, not ASCII.

## Bootstrapping a NEW builtin campaign (the pin list)

- Chicken-and-egg: adding the id to OG_BUILTIN_CAMPAIGN_IDS makes the
  build compose the tree BEFORE the generator ever ran → "no members
  collected". Seed a placeholder campaign.yaml once; the generator
  overwrites the tree.
- The full registration list (all needed, none optional):
  OG_BUILTIN_CAMPAIGN_IDS in CMakeLists, the campaign-drift workflow's
  generator list in .github/workflows/test.yml, test_builtin_archives
  campaign_ids, picker_common kShelf + its test lists, and the
  og_add_unit_group registration for the campaign's test file.
- A 1-level (or newest-level) campaign needs a loop-home exit: no-exit
  auto-advance targets id+1, which must exist.
- Generators run FROM REPO ROOT (relative staging paths), and mapgens
  must build on the shared og::mapgen library — no private helper
  clones. `og::mapgen::place_living` takes an EXPLICIT hold_post bool;
  team number never implies guard policy.

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
