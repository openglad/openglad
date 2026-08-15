# Campaign scripting (issue #206)

Campaigns can now ship Lua that runs at the *campaign* level rather than the
*level* level: a scripted scenario picker, persistent per-company campaign
state (decision tracking), gold sources and sinks, and level scripts that read
the recorded decisions. This document is the engineering design; the
per-campaign content designs live with their packs.

## Goals

1. **Scriptable scenario picker** — a campaign presents its own
   mission-selection pages (the Multiplayer Modes campaign shows a mode
   list, then that mode's arenas) on all three clients (SDL/web, text,
   curses).
2. **Persistent campaign state** — small named integers a campaign script
   reads and writes from the picker, persisted per company save, per
   campaign.
3. **Gold access** — campaign scripts can read the team wallet and run
   priced picker actions (shops, bribes, tolls) through the same
   affordability rules the hire/train screens use.
4. **Team visibility** — campaign scripts can read the roster (family,
   level, stats, name) to brief, gate, or price against team composition.
5. **Decision-reactive levels** — level scripts can read campaign state
   (read-only) so a choice made in the picker changes what happens in the
   sim.

Non-goals (this change): sim-side *writes* to campaign state, replication
of campaign state to networked mirrors, scripted procedural level
generation.

## The dispatch model

One new registrar, legal at chunk load time in `scripts/*.lua` (campaign
packs), same storage/dispatch shape as `og.register_level_hooks`:

```lua
og.register_campaign_hooks({
  vars = { "delve_counted", "watch_paid", "advance_debt" },  -- sim-visible names
  picker_menu = function(page_id) ... end,   -- page description, pure
  picker_action = function(entry_id) ... end, -- priced/scripted action
})
```

- Registered per VM (world VM and the UI fallback VM both replay pack
  chunks, so both carry the hooks). In the declaration VM
  (`VmMode::Declare`) the registrar is a silent no-op, the
  `og.register_hooks` precedent — a family chunk calling it neither
  rejects the pack nor registers anything.
- A **second** registration in the same VM does not raise in the chunk
  (that would kill the chunk's unrelated level hooks and make the winner
  an accident of pack-id sort order). Instead the conflict is recorded
  C++-side with both source names, the has-scripted-picker query answers
  "no scripted picker", and a script error is logged. One campaign, one
  book, or none.
- `vars` is bounded to 64 names, each ≤ 32 chars of the safe-id charset.

### The campaign-dispatch fence

Campaign hooks run in whatever VM `active_world_scripts()` answers — in the
SDL client that is the **world VM with the live world API**, including
`og.rand` over the replicated sim RNG. A menu script drawing from that
stream would silently perturb the sim. So campaign-hook dispatch arms a
`VmState::campaign_dispatch` flag (RAII around the protected call, and it
never arms at nested entry depth) and:

- the existing load-fence closure over `kOgWorldFuncs` gains
  `|| campaign_dispatch`, as do the four hand-fenced entries
  (`og.rand`, `og.is_alive`, `og.entity_id`, `og.set_entity_hooks`);
- the three registrars — `og.register_hooks`, `og.register_level_hooks`,
  `og.register_campaign_hooks` — which are deliberately *outside* the load
  fence, each gain their own campaign-dispatch error (a menu script must
  not rewrite sim hook tables).

Explicitly allowed during campaign dispatch: the new `og.campaign_*`
bindings, `og.div/mod/trunc/log`, `og.max/min/clamp/sign`, `og.C`,
`og.combat.*` (pure formulas over arguments), `og.family_id`, `og.api`,
`og.NIL`, and the sandbox's string/table surface. `og.use`, `og.family`,
`og.anims`, `og.pack` already self-fence at dispatch time. A unit test
walks the `og` table during a campaign dispatch and asserts every entry
outside the allowlist errors.

### Menu-time bindings (campaign-dispatch only)

`og_gameplay` cannot see `SaveData` or the picker (dependency direction),
so every binding below resolves through a **process-global provider
struct** declared in `src/gameplay/script/` with a setter on the SDL-free
`hooks::` facade — *not* the per-world `og.scenario_title` mechanism,
which is installed at level load and unreachable at picker time. Install
sites, each pointing the providers at the SaveData that surface actually
owns: SDL `GameSession` construction, the text picker's init (beside its
[SAVE-R2] slot repoint), curses `install_app_session_context`, unit-test
fixtures — and deliberately none on `openglad_server`. Providers never
dispatch Lua (no re-entry). Calling a binding outside campaign dispatch,
or with no provider installed, is a Lua error.

| Binding | Meaning |
|---|---|
| `og.campaign_state_get(key)` | int32 value, 0 when unset |
| `og.campaign_state_set(key, v)` | write-through to the active company's per-campaign store; the binding is the bounds-enforcement point (over-long key or entry overflow raises *before* mutating, so a script bug can never brick the save file) |
| `og.campaign_gold()` | wallet balance of the acting team (resolved C++-side, below) |
| `og.campaign_spend_gold(amount)` | affordability-checked debit; answers true/false; honors infinite gold by skipping the debit |
| `og.campaign_grant_gold(amount)` | saturating credit |
| `og.campaign_team()` | local company roster snapshot: array of plain tables `{name, family, level, exp, strength, dexterity, constitution, intelligence, armor, team}` — values, not handles; `family` is the display-name string; `team` clamped to [0,3] |
| `og.campaign_level_completed(id)` | menu-time twin of the sim's `og.level_completed` |
| `og.campaign_current_level()` | the campaign cursor (`save.scen_num`) |
| `og.campaign_scenario_title(id)` | cheap `.fss` header title read, "" when absent |

**Whose wallet:** the acting team is resolved C++-side to a locally-owned
team (the lowest team present on the roster, `my_team` fallback — the
Base Camp gold-label rule), never script-chosen. In a networked lobby the
autosave merge only persists owned teams' wallets, so this rule is what
keeps every scripted debit durable. All wallet writes ride the same
clamp/guard discipline as hire/train, and every mutation is followed by
the standard after-roster-mutation tail (lobby sync + company autosave)
by the calling UI code.

## The picker contract

The script is **pure**: page id in, page description out. All navigation
state (which page is open, scrolling, selection) lives in C++. This keeps
Lua stateless across the VM-rebuild hazard and makes the same contract
renderable by three different clients. The C++ session fetches a page
**once per navigation or action** and caches it — never per frame.

```lua
picker_menu = function(page_id)      -- "" = root page
  return {
    title = "CHOOSE A GAME",
    lines = { "The Gamesmaster opens the book." },
    entries = {
      { id = "tdm", label = "TEAM DEATHMATCH", kind = "page", note = "6 arenas" },
      { id = "300", label = "THE CIRCLE", kind = "level", level = 300, note = "4 teams" },
      { id = "buy_kit", label = "FIELD KIT  60g", kind = "action", cost = 60 },
    },
  }
end

picker_action = function(entry_id)   -- runs the priced/scripted entry
  ...
  return { message = "Kit stowed for the road." }   -- optional toast
end
```

- `kind = "level"`: choosing it has **the same consequence as SET LEVEL,
  applied by each client's existing level-set tail** (not by calling the
  SDL-only `do_set_scen_level`): a shared validated helper in
  `picker_common` checks the id exists (cheap `.fss` header read), writes
  `save.scen_num`, and invokes a per-client lobby-sync callback; the SDL
  client additionally runs its load-with-rollback. A missing `label` is
  filled by C++ from the scenario title; C++ decorates level rows with
  CLEARED/CURRENT markers from the save.
- `kind = "page"`: C++ calls `picker_menu(entry.id)` and renders the new
  page (with Back).
- `kind = "action"`: **C++ owns the `cost` debit** — shared affordability
  predicate (infinite-gold aware, row disabled/refused when
  unaffordable), debit beside the three existing wallet sites, *then*
  dispatch `picker_action(entry_id)`. `og.campaign_spend_gold` exists for
  variable-priced flows inside actions only. The page is re-requested
  afterwards so labels/state re-derive. A spend already applied sticks
  even if the action later errors — actions should mutate state first,
  and cheaply.
- A page may also carry `lines` — up to six non-selectable text rows
  rendered above the entries (terminal context-line pattern; text rows on
  the SDL page). This is the narrative surface for ledgers, shop patter,
  and decision recaps, without touching briefing budgets.
- Hard bounds, enforced C++-side: ≤ 24 entries per page, labels clipped to
  the row budget, page depth ≤ 4, and a malformed or erroring hook answers
  "no scripted picker" (the stock UI remains reachable).

### Gating

- **Level rows are host-gated** (they publish `scenario_id`), same
  predicate as SET LEVEL. **Pages and actions are open to every
  machine** — they touch only the local wallet and the local decision
  book, and a joiner must be able to keep their own book.
- Terminal contract: rows are always listed; a gated selection prints the
  guard message (host gate: the existing guard string; no registered
  picker: "This campaign keeps no mission book."). The SDL surface hides
  the MISSIONS button when no picker is registered and disables level
  rows for non-hosts.
- On a dedicated server every campaign var reads 0 (its SaveData comes
  from the lobby config, which carries no campaign state); the var==0
  rule guarantees stock behavior there. Campaigns that gate content on
  vars are player-hosted by design.

### Surfaces

- **SDL/web**: a new SCENARIO-subscreen button (MISSIONS), shown only when
  the mounted campaign registers a picker. It opens a dynamic-row
  subscreen (Company List row-template pattern: fixed row macro table,
  `PageModel`, `MenuSpecRow` dispatch, installed state pointer).
- **text/curses**: the scripted pages render through the established
  dynamic-rows pattern (client prints numbered rows, prompts for a
  number — never `Menu::choose`, whose digit-jump stops at row 9),
  reachable beside the existing "Set level" prompt.
- The page-fetch/select/act state machine is SDL-free C++
  (`CampaignPickerSession`, a new TU added to the interface component
  list **and both duplicated terminal source lists**), consumed by all
  three surfaces; the Lua boundary crossing lives in
  `src/gameplay/script/` behind a plain-struct facade in
  `include/openglad/gameplay/script/` (Lua headers never leave the script
  directory).

## Persistent campaign state

New `SaveData` field: per-campaign ordered key/value list —
`std::map<std::string, std::vector<std::pair<std::string, std::int32_t>>>`
keyed by campaign id. Bounds: key ≤ 32 chars of the safe-id charset,
≤ 128 entries per campaign, ≤ 128 campaigns (matching the existing
campaign-list bound). The *write* choke is `og.campaign_state_set`
(raises before mutating); the *load*-side rejection stays for hostile
files, in the existing bounds style.

- **GTL v15**, appended at the tail after the tower block in both mirrored
  functions; `read_company_header` is untouched (the header prefix does
  not change, so its byte-offset pins are re-asserted, not moved).
- Site list for the new field (the "documented dropped-field bug class"):
  the writer, the version-gated reader, **`SaveData::reset()`** (clear the
  map beside `completed_levels.clear()`), `reset_campaign` (clear that
  campaign's list), the headless-server field-by-field copy,
  `persist_networked_win`, and the networked-lobby autosave merge —
  in the merges the state is **overlaid from the session** (the machine
  that made the decision persists it).
- Pin discipline: all `save_data.cpp` edits stay **below line 124** (the
  `kMut_save_corrupt` canary anchor); run
  `scripts/parity/check_mutation_pins.py` and rebuild `og_test_parity`
  either way. The two GTL layout comment blocks gain the v15 description
  in lockstep. The seven version-literal pins and the raw-offset pins in
  `test_save_data_versions.cpp` are updated/re-asserted.
- **Networked semantics (documented)**: campaign state is per-machine,
  like the campaign cursor. The host's picker drives the shared session
  (its choices reach peers as `scenario_id`); each company file keeps its
  own decision book. The load-bearing chain into the authoritative sim:
  the display save is GTL-round-tripped through the `netsession` slot at
  game start and the shadow/headless server loads it — the field reaches
  the server *because* it is serialized. It must never become
  session-only.

## Decision-reactive levels

`GameWorld` gains a non-serialized `campaign_vars` list (name → int32),
declared in the header only (no `game_world.cpp` line churn — three
parity pins anchor in that file). At level load, the **authoritative**
sync sites (`screen::sync_world_from_save_data` and the headless twin)
**clear then copy** the values of the campaign's registered `vars` names
from the save — replace, never merge, so stale values cannot leak across
levels or campaigns and `GameWorld::clear()` needs no edit. Level hooks
read them with `og.campaign_var(name)` (read-only, 0 when absent,
world-fenced like every sim binding). Mirrors never run Lua, so the vars
are deliberately not in the snapshot — no protocol change.

Replays: the registered vars are stamped into the replay header at record
time and applied to the world at playback start (replay format bump; the
format static_assert moves), so a replay of a decision-taken run verifies
on any machine.

Rule for content built on this: **var == 0 must reproduce today's level
byte-for-byte** — the hook's *first statement* is the `== 0` early
return, before any world read, spawn, or RNG draw — so every existing
calibration floor, battle smoke, and parity golden stays honest, and only
the taken-decision paths need new coverage.

## Build gates this change trips (by design)

- **api_stub_check**: the new `og.campaign_*` luaL_Reg table and the
  campaign hook-name table must be taught to
  `scripts/modding/gen_api_stubs.py`, and `docs/modding/og-api.d.lua`
  regenerated, in the same change. `kPackApiVersion` bumps 3 → 4 so packs
  can feature-detect the campaign API.
- **Menu pins** (adding the shared Scenario row + SDL MISSIONS button):
  the Scenario item-count asserts (7 → 8) and expected-command table in
  `test_menu_model.cpp`; the Scenario ordinals + 7-item comment in
  `scripts/test_text_picker_interactive.sh`; the ordinal script in
  `test_platform_headless.cpp`; a new `kScenarioMenuMissionsIndex` +
  count + nav rewire in `kScenarioMenuRows` with its `test_menu_layout`
  index/nav/variant pins; the host-visibility rewire arm. The shared row
  is appended before Back per the table's own discipline; the curses
  digit-jump budget (rows 1–9) is respected.
- **Generators**: `westlands_mapgen` and `longseason_mapgen` gain the
  one-line `stage_pack_tree` call plus a concept-style pack self-check
  (registration fired, pages within budget, one var-injected tick), so
  the generators audit the archive players actually get. Staging-zip
  changes do not touch `campaigns/`, so the drift job needs no
  regeneration.
- **Coverage**: every new C++ TU is named with its owning test group;
  every fallback/malformation branch gets a dedicated unit case; an
  `OPENGLAD_LUA_COVERAGE` run exercises all three campaigns' pages and
  actions (both the var==0 early returns and every taken branch) before
  the PR.

## What the engine does NOT decide

Mode names, page structure, prices, prose, and which decisions exist are
all pack content. The engine ships: the registrar, the fence, the
providers, the session state machine, the three surfaces, and the save
field. Everything a campaign does with them is data.

## Test plan

- Unit: registrar validation (dup registration → no-picker + logged
  conflict, declare-mode no-op, bad hook names, vars bounds), fence
  behavior (og-table walk during campaign dispatch), provider bindings
  against a scripted SaveData, page decoration
  (titles/CLEARED/affordability), session state machine (paging, depth
  cap, action refresh, cost debit ownership), GTL v15 round-trip +
  version-gate reads + bounds rejections + reset()/reset_campaign
  clears, campaign_vars replace-not-merge sync, replay header round-trip.
- Integration: SDL MISSIONS subscreen flow (injector pattern), modes
  campaign picker end-to-end (open → mode page → arena select →
  scen_num + lobby republish), westlands/longseason shop actions debit
  wallet + persist through autosave, level-hook `og.campaign_var`
  consequences fire only when set (census + RNG-state pin at var==0).
- Terminal: text + curses scripted-picker drives (ordinal scripts), the
  two guard strings pinned.
- Regression: og_test_parity untouched goldens; calibration/smoke tables
  unchanged with all vars at 0; mutation-pin check + og_test_parity
  rebuild.

## Adjacent issues designed through the same architecture

These ship in the same change, each taking the scripting-first shape (a
static flag only where the declarative metadata already exists).

### #212 — scripted match setup

Menu-time twins of the sim's read-only `og.match_setting`:

- `og.campaign_match_get(name)` / `og.campaign_match_set(name, value)`
  over the same name vocabulary, mapped to the persisted knobs the
  MATCHUP screen edits: `team_count`, `score_limit`, `respawn_ticks`,
  `strip_troops`, `respawn_mode`, `generator_rate`.
- `og.campaign_is_host()` — so a script can shape host-only pages.
- **Policy lives in the provider, not the script**: `match_set` clamps
  like the lobby sanitizer and returns false for non-hosts (local play is
  always host). A write-through marks the session "match settings
  changed"; the calling surface then runs the standard
  sync-settings-from-save tail so joiners follow. Campaign content: the
  Gamesmaster's Book gains a MATCH SETUP page of one-action presets.
  This *advances* #212 (a per-slot roster editor is future work).

### #209 — radar_ping

A declarative family presentation key, `radar_ping = true`, through the
full declaration pipeline (schema validation with did-you-mean, harvest,
`FamilyDescriptor`, api-reference §4). Renderers make a pinged entity
loud: the SDL radar draws an oversized pulsing blip (cosmetic frame
counter — render-only, parity-inert), the curses radar promotes the
glyph to bold/standout. The modes pack declares it on the soccer and
basketball balls; any pack can use it.

### #210 — basketball HUD

Pure mode Lua: `mode_basketball_impl` publishes one HUD score row per
active team (the TDM pattern), pinned by a bindings test.

### #207 — replay

Completed levels become selectable everywhere: the PROGRESS screen's
cleared rows regain their per-row button, labeled REPLAY (terminals
already accept any id at the set-level prompt; scripted pickers list
cleared rows with their CLEARED marker by design). The Imaginations
campaign additionally ships a dream-log picker page in its existing pack
— every dream replayable, in the campaign's voice. Win-fold semantics
for replayed levels are unchanged (completion marking is idempotent).

### #213 — no XP from versus arenas

Campaign metadata drives it: when the mounted campaign's `matchup:` is
`versus`, roster persistence keeps exp/level as the company file holds
them — at both paths (the local win fold's roster update and the
networked owned-guys merge, which re-levels from exp). No new yaml key
until a campaign needs the override; `matchup` is already the
declarative statement that a campaign is an arena, not an adventure.
