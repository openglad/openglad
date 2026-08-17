# Camp controls: earned roads, honest dockets, direct knobs

Design for the level-selection and camp-control rework. Four connected
changes: an engine-level progression gate ("earned roads"), a Westlands
docket that tells the truth under it, a random-scenario draw for the
Multiplayer Modes camp, and MATCH SETUP rebuilt as direct knobs. A fifth
moves DIFFICULTY out of the main menu into Base Camp.

## 1. Earned roads: the engine-level progression gate

### The problem

"Closed" used to mean exactly "the scenario file is not in the mounted
package" — no surface consulted `completed_levels` to *allow* anything.
The Westlands camp derives its docket from the campaign cursor, and
clicking a docket row writes that same cursor, so the docket was a
self-advancing loop: seventeen keypresses walked a fresh level-1 company
from THE FOREST ROAD to the campaign finale, with every step confirmed
"Level set to ...". The classic SCENARIO → SET LEVEL browser (and its
free-typed ENTER ID), the PROGRESS report's GO/REPLAY shortcuts, and the
raw terminal `Set Level` prompts had the same freedom. The camp wore
progression language — "the road ahead", `[CLOSED]`, "That road is not
open yet." — while the code never implemented the gate the words imply.

### The rule

A player-facing level-set is allowed iff the target level is in the
ACCESSIBLE set:

    {campaign.yaml first_level} ∪ {save.scen_num}
        ∪ completed_levels[campaign] ∪ exits(completed)

Two campaign classes are exempt, keyed on campaign.yaml metadata:

- `matchup: versus` (the Multiplayer Modes campaign) — an arena picker's
  whole point is free field selection;
- `mode: tower` — Tower Climb runs its own IProgression and never writes
  `completed_levels`.

Cleared levels stay replayable (the #207 contract), the current level is
always in the set, and existing over-advanced saves keep their cursor via
the `{scen_num}` seed — grandfathered, self-consistent.

### Mechanics

- The frontier computation lives in og::data
  (`src/resources/level_selection.cpp`): `accessible_levels(save)`,
  `level_selection_allowed(save, level)` and the exemption predicate
  `level_selection_gating_active(save)` — in resources so the SDL-free
  terminal clients apply the same rule the SDL client does.
  `get_accessible_levels()` (the PROGRESS report's source) forwards to it.
  Per-level exit scans are memoized per (mounted campaign, level) and
  invalidated with the campaign-metadata cache.
- The gate is applied at every activation tail, refusing BEFORE any load
  or autosave tail, with the existing voice line
  `kCampaignLevelClosedMessage` ("That road is not open yet."):
  1. `do_set_scen_level` — the single choke for the SCENARIO browser
     click AND the free-typed ENTER ID.
  2. The PROGRESS GO/REPLAY click — which also gains the previously
     missing host gate (a networked joiner could write `scen_num` there;
     it now refuses with the host-guard line).
  3. Both SDL Lua level-set tails (Base Camp zone rows and the zone
     submenu), after the host gate, before the load.
  4. The shared terminal decoration: `row.available` is now
     `file_exists && allowed`, so every terminal surface (and the SDL
     `[CLOSED]` paint + spent face) inherits the gate and the refusal
     from the one derivation both sessions share. A gate-closed road
     keeps its real title — the road exists, it is just not open yet.
  5. The raw terminal `Set Level` prompts (text and curses): existence +
     the predicate.
- The SCENARIO level browser paints LOCKED (dim) in the status column for
  inaccessible rows, beside the existing CLEARED/CURRENT.
- The `[CURRENT]` refusal became a signpost:
  `kCampaignLevelUnchangedMessage` is now "Already on that level. GO when
  ready." (ASCII only — the sprite font has blank glyphs for several
  punctuation marks). This fixes the one-level-campaign trap where the
  only replay row refused in the exact post-win state: GO has always
  replayed the current level, and the toast now says so.
- `SaveData::reset_campaign` also rewinds the cursor: the gate keys off
  `scen_num`, so a reset company parked past its wiped frontier would
  find every road closed. Resetting the current campaign rewinds
  `scen_num`/`current_levels[campaign]` to the campaign's declared
  `first_level`; resetting any other campaign erases its stored cursor so
  the next switch re-enters at `first_level`.
- Untouched by design: the win fold, in-level withdraw, the load-failure
  fallback, campaign switching, replay/demo/CLI/editor entry points, and
  the joiner settings-apply path (it mirrors a now-gated host).

## 2. Westlands docket honesty

With the engine gate in place, `fire.lua` stops pretending:

- `graph_rows` adds the CURRENT level as the docket's first row ("the
  fight at your feet"; the engine marks it `[CURRENT]`) and keeps the
  forward rows — now honestly `[CLOSED]` until the current level is
  completed, opening exactly at forks. The fiction: you choose a road
  once you have won the fight at your feet.
- No gating logic is duplicated in Lua: the C++ decoration owns it.
- The ledger and quartermaster are untouched; the split act already keys
  off completion.

## 3. Random Scenario (Modes) — replaces TONIGHT'S CARD

The card/shuffle ceremony in the Modes camp gives way to one host-only
action row, `RANDOM SCENARIO`: a click draws a uniformly random arena
from the full manifest (skipping the current field when there is a
choice) and sets it through the engine's own level-set tail, whose toast
names the arena. Backed by a new campaign-dispatch-only binding
`og.campaign_random(n)` over a menu-side provider (deterministic in
tests; never the sim RNG). `CampaignActionResult` gains an optional
`level` so a picker action can route into each client's existing SetLevel
tail — host gate, load-rollback and confirmation included. The versus
campaign is exempt from the earned-roads gate, so any drawn arena is
settable.

## 4. MATCH SETUP: direct knobs

The preset pages give way to three cycle-on-click rows, each labeled with
the current value at fetch:

- `TEAMS: AUTO` — Auto → 2 → 3 → 4 → Auto.
- `TARGET SCORE: MAP` — map's own → 1 → 3 → 5 → 10 → map's own.
- `TROOPS: ALL` — all → own → fair → all.

Each click writes through `og.campaign_match_set` and toasts the new
state in plain words. Host enforcement is unchanged (the provider refuses
non-hosts; the dirty-flag sync publishes to joiners). A match-clock knob
is out of scope: there is no `time_limit` key, and wiring one is a
SaveData + LobbySettings + protocol + per-mode change out of proportion
to this rework.

## 5. DIFFICULTY into Base Camp; main menu split

Difficulty, respawns, permadeath, spawn delay, generator rate and infinite
gold describe the fight a company is about to take. They were on the
program's front door. They belong where the company is, so the door moved to
the Base Camp command strip and the main menu kept only what is genuinely
about the program.

**The strip re-grids.** The word is DIFFICULTY, in full — no abbreviation
survived reading. That needs a 68px face, which the 50px slot HIRE vacated
could not give, so the strip took the width out of GO (the only face with
slack) and re-laid on one 6px gutter, still closing flush on the panel's
right rail at x=312:

| door | rect | label budget `(w-8)/6` | label |
|---|---|---|---|
| BACK | 8,178,44,18 | 6 | 4 |
| DIFFICULTY | 58,178,68,18 | 10 | 10 |
| SCENARIO | 132,178,62,18 | 9 | 8 |
| NETWORK | 200,178,56,18 | 8 | 7 |
| GO / READY | 262,178,50,18 | 7 | 2 / 5 |

GO and NETWORK were wasm-E2E coordinate contracts; the specs moved with the
code in the same commit (`wasm_helpers.js`, `wasm-networking.spec.js`,
`wasm-touch.spec.js`). The new row is appended at ordinal 72 —
`kCreateMenuDifficultyIndex` — rather than carved out of the three parked
zone spares, so no established Base Camp ordinal moved; the ceiling
(`kCreateMenuButtonCount`, `MAX_BUTTONS`, `SessionState::kMaxButtons`) goes
72 → 73. The per-frame rewire runs BACK ↔ DIFFICULTY ↔ SCENARIO and drops
seat card one onto DIFFICULTY (it is the door under that card's face; the
empty slot used to force a doubled SCENARIO link). The difficulty screen's
`remote_start` scope becomes `TeamBuildScope` like every other Base Camp
child, so a host GO launches a joiner parked inside it.

**The main menu.** The difficulty row is gone and the narrow `GAME | CLOUD`
pair it sat under becomes two full-width rows that say what they are:
`GAME SETTINGS` (80,135,140,15) over `CLOUD SAVES` (80,154,140,15). The grey
`SETTINGS` caption at (150,125) is deleted with them — two spelled-out rows
need no caption, and the y=119..134 band it reserved is ordinary canvas
again. The `begin → continue → level_edit → options` chain is preserved
deliberately: the wasm DISPLAY tests reach the settings door in exactly two
downward steps, and (10,10) and (10,190) stay inert for the two E2E probes
that tap them as blank.

**Terminals.** Main loses its Difficulty item (8 rows; every row below it
moved up one 1-based position) and its Cloud row is relabeled `Cloud Saves`
to match the SDL face. Team Build gains `Difficulty` at position 11, past
the curses digit-jump budget — the arrow walk reaches it, and unlike a match
rule it is not something a player retunes every round. The flat CTF trio
(`ctf_teams`, `ctf_caps`, `ctf_troops`) left Team Build in the same change:
teams and target score are the Modes camp's MATCH SETUP page now (§4), one
source of truth in plain words, and scenario troops keeps its SCENARIO row.
The SDL MATCHUP screen keeps its own copies for legacy versus packs.

The difficulty submenu itself is unchanged and returns to whatever screen
pushed it; from the strip that is a nested `MENU_REDRAW` the Base Camp loop
consumes, which an injector flow now pins end to end.
