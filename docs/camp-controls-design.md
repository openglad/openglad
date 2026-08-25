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
     missing host gate (a networked joiner could write `scen_num` there).
     A joiner is not offered the click at all: the rows draw "(HOST)" in
     the engine's disabled grey where the button was, and the hit test
     answers nothing, so the report stays readable without ever raising
     the refusal. That matters because the refusal here is a POPUP, and
     popup_dialog owns its event loop with no lobby poll in it — a
     joiner parked behind the OK button applies no lobby messages and
     cannot follow a host GO until they dismiss it. The click-time host
     gate stays as the write's own guard.
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

- `graph_rows` adds the CURRENT level as the docket's first row (the
  fight at your feet, named by its own scenario title; the engine marks
  it `[CURRENT]`) and keeps the forward rows — now honestly `[CLOSED]`
  until the current level is completed, opening exactly at forks. The
  fiction: you choose a road once you have won the fight at your feet.
- The row underfoot carries NO note, wherever the docket names it (the
  graph row and the split's own march rows both). The marker plus its gap
  is eleven of the panel's forty-two glyphs, so a note beside it cut the
  row's name mid-word — "THE QUIET VALE - THE FIGHT AT.." on the first
  night. `[CURRENT]` says what the note said. Sweeps: the composed face
  over every cursor in `test_westlands_fire.cpp`, and the same arithmetic
  in the generator's self-check.
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
settable. The row's note is `any game, any field`: the draw is over the
whole manifest, so it can land on a different game than the one the table
is set to, and a note that said only "any field" would read as a reroll
inside tonight's game.

A refused routed set answers on the surface's own terms. The terminals
print two notices in sequence (the refusal, then the action's message).
The SDL toast is ONE slot with one timer, so the two are composed into a
single line — refusal first — and a message that will not fit beside it
is dropped whole rather than cut: the refusal is the answer to the
click, and the pack's line must never speak in its place. Budgets: 34
glyphs on the Base Camp line-B slot, 41 in a zone submenu.

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

**[SUPERSEDED — issue #241.]** The clock is a fourth row now,
`TIME LIMIT: MAP` — map's own → 5M → 10M → 15M → 20M → map's own, stored
in sim ticks like the manifests it overrides. The SaveData + LobbySettings
+ protocol + per-mode chain named above is exactly what was built, because
the manifest clock every mode already ran on was the one match rule the
lobby could not touch. The two summaries — `rules_line()` and the camp's
`rules_digest()` — deliberately stayed at three knobs: the digest's 20-char
note budget has no room for a fourth term, and the row itself is where the
value is turned and read.

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
seat slot one onto DIFFICULTY (the door under that slot's face; the empty slot
used to force a doubled SCENARIO link). The rail's later widening to four
70px slots moved slot one's face left over BACK, but the strip's four doors
still take the rail's four slots one apiece — re-pointing slot one at BACK
would leave the strip a door short of a keyboard route down from the rail. The difficulty screen's
`remote_start` scope becomes `TeamBuildScope` like every other Base Camp
child, so a host GO launches a joiner parked inside it.

**The joiner's face.** Every row on this screen is LobbySettings-backed, so
`sync_difficulty_menu_visibility` hides all six for a non-host — which, now
that the door sits one click off the screen a networked joiner lives on,
means a joiner opens a heading over an empty panel. `difficulty_panel_caption()`
prints "The host sets these for everyone." where the rows would have been,
the way the modes book says "The host calls the rules."; a host or solo
player gets no caption, because six rows that answer to them explain
themselves.

**The main menu.** The difficulty row is gone and the narrow `GAME | CLOUD`
pair it sat under becomes two full-width rows that say what they are:
`GAME SETTINGS` (80,131,140,15) over `CLOUD SAVES` (80,150,140,15), the pair
centered between its neighbour groups (the 13px break above GAME SETTINGS
equals the footer break below CLOUD SAVES — pinned in test_menu_layout).
The grey `SETTINGS` caption at (150,125) is deleted with them — two
spelled-out rows need no caption, so the band it reserved is ordinary
canvas again. The `begin → continue → level_edit → options` chain is preserved
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
The SDL SCENARIO screen keeps its own copies for legacy versus packs
(`Teams:` / `Limit:` rows on the y=140 match-settings band beside TROOPS,
visible read-only to joiners, host-actionable).

The difficulty submenu itself is unchanged and returns to whatever screen
pushed it; from the strip that is a nested `MENU_REDRAW` the Base Camp loop
consumes, which an injector flow now pins end to end.

## 6. Replay: the bounded excursion (#207)

### The problem

Loading a level the save marks completed PURGES it: everything except
team-0 livings, exits and teleporters dies at load (`game.cpp`'s 2002
"Have we already done this scenario?" rule and its headless twin
`apply_completed_level_cleanup`). The PROGRESS screen shipped a REPLAY
button whose write was a plain cursor set, so "replay" delivered an empty
map — the Imaginations campaign's one island replayed as a bare exit pad
under a camp line promising "Every dream can be dreamed again." Worse,
the cursor write itself was the hazard: replaying was the only action
that could move a campaign *backwards*, it hit the disk before the level
started, survived a loss, and in networked play rewrote every machine's
campaign cursor to the replayed level's exit.

### The rule

A replay is a **bounded excursion with restored content**. The arm is a
transient pair on `SaveData` — `{replay_level, replay_origin}`, origin =
the cursor at arm time — never serialized (no GTL change), cleared by
`reset()`/`load()`, and re-carried explicitly across the launch sites'
disk round-trips (the dropped-field pattern: `game.cpp`,
`local_transport_shadow`, `copy_headless_server_save_data`). Arming also
moves `scen_num` onto the level, so go_menu, the lobby publish and joiner
mounts behave exactly as a plain set. Every PLAIN cursor write (PROGRESS
VISIT/GO, SET LEVEL, the camp's plain level rows, the terminal Set Level
tails) and every campaign switch clears the arm before writing — a
re-pointed cursor abandons the excursion, so a stale arm can neither skip
the new level's purge nor restore an origin into a foreign campaign
(which could plant an unearned cursor there).

- **Launch**: both purge sites skip for exactly the armed level. VISIT —
  the plain cursor write — keeps the classic purged walk-through, on
  purpose: it is the cleared-road traversal and re-branching tool (most
  campaigns are exit graphs, and walking a cleared level to a different
  exit is real navigation).
- **Win**: the fold (progression.cpp step 5b) restores
  `scen_num`/`current_levels` to `replay_origin` instead of following
  the walked exit, then clears the arm. Completion marking stays
  idempotent; score/cash/XP pay as on any play; the time bonus was
  already first-completion-only. The networked persist then writes the
  RESTORED cursor to the company file — a replay no longer rewrites the
  table's campaign position.
- **Any other end** (loss, quit): the picker re-entry restore
  (`og::ui::replay_reentry_restore`, called by every client after
  gameplay) rewinds the cursor in memory and on the next disk write,
  then clears the arm. On the web build the native go_menu tail never
  runs, so the restore fires at the Playing → Picker edge of the browser
  state machine instead — and persists immediately for solo/local play,
  because the picker re-entry reloads the company from disk right after.
  A mid-level crash can leave the replayed level on disk: gate-safe (the
  frontier includes completed levels), self-heals on the next cursor
  move.
- **The networked tail** runs the same restore, and has to: a networked
  round folds into the SESSION's own save copy (the host's authoritative
  save, the joiner's mirror) and persists straight to disk — it never
  copies back into the picker's save the way a solo session does. So a
  hosted REPLAY that ends any way but a win returns with the arm still
  live, which would seed the NEXT hosted round into a second replay
  (hosts seed from that very save) and let the next base-camp autosave
  bank the replayed level as the campaign cursor. After a WIN the heal
  is memory-only: the fold already restored the cursor and the networked
  persist already wrote the merged company file, so autosaving the
  pre-round picker copy over it would erase the win.
- **Withdraw stays impossible** during a replay by construction:
  `can_withdraw` requires the CURRENT level uncompleted, and a replayed
  level is completed — pinned by test.

### The surfaces

- **SDL PROGRESS**: cleared rows carry VISIT (plain write) + REPLAY
  (arm); uncleared rows keep GO. The Foes column shows the authored
  count on every row now — the old hardcoded 0 was only accidentally
  true while the purge emptied every replay.
- **Terminal SCENARIO menus** (text + curses): a "Replay Level" prompt —
  the level must exist, pass the earned-roads gate AND be cleared.
- **Lua level rows**: an optional `replay = true` field
  (`CampaignPageEntry.replay`). The ENGINE owns the cleared check: a
  marked row arms only when the level is actually cleared, else it is a
  normal set, on every client's level-set tail. A cleared replay row is
  exempt from the "Already on that level" refusal (arming is a real
  state change even when the cursor does not move — the Imaginations
  loop-home row IS the current row) and answers "Replaying <title>. GO
  when ready." The dream log sets it on every dream row.
- **Dedicated server**: a host level-set onto a level the SERVER's save
  marks completed arms automatically (same campaign only) — a networked
  table re-fights the restored level. The dedicated server has no player
  UI, so a completed landing is the only signal it gets; the empty
  walk-through stays a solo nicety there. No wire change.
- **Networked hosts play under their own history** (the SDL-host parity
  rule): a curses networked host seeds its authoritative session save
  from the host machine's active company save at session create —
  completed set, campaign decision book, cursor, team, and the transient
  replay arm (carried explicitly through
  `copy_headless_server_save_data`, the dropped-field pattern). A hosting
  player therefore means the same thing from every client: an UNARMED
  landing on a level the host cleared purges for the whole table (VISIT
  means VISIT from a curses host), a REPLAY arm reaches the
  authoritative load (restored census, fold origin-restore, and the
  networked persist writes the restored cursor to every machine's
  company file), campaign vars reach the hosted world (a fresh session
  used to play every var as 0), and completion stays campaign-keyed — a
  level id cleared in another campaign never purges this one. The
  seeded session never adopts a completed landing
  (`LobbyStartReplayArm::SeededIntent`): the player's save already says
  what was meant, and the dedicated auto-arm would silently convert
  every hosted VISIT into a replay.
- **Networked joiners** ("every machine"): a joiner cannot click REPLAY —
  its cursor writes are synced — so the arm is adopted at the settings
  apply instead: when the synced cursor MOVES onto a level completed in
  the joiner's OWN save (same campaign), the joiner arms with origin =
  its own pre-sync cursor, and its win fold restores that origin before
  the per-player persist. Host re-picks before launch keep the FIRST
  origin; a landing on a level the joiner never cleared is a plain set
  (the joiner earns the walked exit), and a campaign switch clears the
  arm (no origin to restore to). There is no joiner-side VISIT: the
  joiner cannot see whether the host pressed VISIT or REPLAY, so any
  completed landing protects this machine's own campaign position — the
  dedicated-server adoption rule, per machine. The curses joiner seeds
  its session save from its own company file so the same lobby-config
  arm applies.

### "Always redoable" (#207's second ask)

Always-redoable is the CAMPAIGN kind, not a level bit: both purge sites
skip when `og::data::campaign_matchup(current campaign) == "versus"` — the
same memoized metadata key that already exempts versus campaigns from the
earned-roads gate and roster XP (#213), so arena rematches always replay
the full map with no arm and arena grinding never over-levels a company.
The old exemption keyed on the level's `SCEN_TYPE_SCRIPTED` bit conflated
"level carries scripts" with progression policy (westlands levels run
consequence scripts yet carry no bit); the bit itself is untouched in the
`.fss` format and still means "level carries scripts" where genuinely
consulted. Behavior-preserving for every shipped campaign: the only
bit-carrying levels are the versus campaign's (modes') 39. A legacy
user-dir arena pack that relied on the bit needs one line of
`campaign.yaml` — `matchup: versus`. Everything else is one REPLAY press
away (per-row `replay = true` is the author's switch; tower never marks
completion, so it never purges).
