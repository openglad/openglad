# LINEUP — teams, seats, fighters and bots on one page

Design for issues #212 (team/player/bot composition), #261 (connected
player details, a mode-aware Networking menu) and #259 (blank level-exit
popup headers). Branch `feature/lineup-networking`, base `a0d85c87`.

The rulings below are binding for implementers. Line anchors were
verified at the base commit by six read-only scouts; **re-verify every
anchor with `grep -n` before editing**, and run
`python3 scripts/parity/check_mutation_pins.py` before any commit that
touches a file named in `tests/parity/scenario_table.h` (of the files
this design touches only `src/resources/save_data.cpp` — pin at line
132, insert below it — and `src/gameplay/game_world.cpp` — max pin 1794
— are pinned).

---

## 0. The model as built (read this before touching anything)

Every earlier draft of this feature got the model wrong, so it is the
first ruling.

| # | Fact | Where |
|---|------|-------|
| M1 | **A character's `guy.teamnum` is its fighting team.** It feeds `set_team_num` at spawn and picks the start marker; `walker::is_friendly` makes team colour the *only* alliance authority — same team never damages, different teams are always hostile, and a company save never implies friendliness. | `src/gameplay/walker.cpp:2264`; `src/server/headless_server_runtime.cpp:195-226` |
| M2 | **A seat is a controller pointed at a team.** Its viewscreen claims walkers from the pool on that team (legacy shared pool locally; owner-locked networked). Several seats may share a team (co-op); seats on different teams oppose. Lobby seat teams are 0..3 only. | `include/openglad/gameplay/sim_control_policy.h`; `src/gameplay/lobby_server.cpp:607-616`; `docs/company-basecamp-design.md:104` |
| M3 | **Seat team and character team are independent facts.** Nothing stamps `seat.team` onto `guy.teamnum` (only the out-of-range sanitize at `lobby_server.cpp:169`). Locally, seats are *derived* from the deployed characters' teams (`my_team` first, padded up to `numplayers`), and a company slot is grouped under the FIRST seat whose team matches, else seat 0. A character whose colour has no seat still fights — under AI. | `src/interface/ui/picker_common.cpp:1305/1336`; `src/interface/ui/picker_lobby_client.cpp:805` |
| M4 | **GO requires one deployed fighter PER SEAT on that seat's team** (two seats sharing team 1 need ≥ 2 deployed fighters on team 1). Refusal: `DEPLOY FOR EVERY PLAYER`. | `picker_common.cpp:1362` `local_seat_teams_have_controls`; `picker_team_build.cpp:2667` |
| M5 | **Networked, a seat owns its company's characters** (`owner_player_index`), only `deployed` slots are assembled, and the seat's team is a *binding* (which walkers it may control), never a stat. | `lobby_state.h:182`; `lobby_server.cpp:1400/1500`; `game_server.cpp:1230` |
| M6 | **Per-character team assignment already exists as one helper**, `og::ui::cycle_guy_team(save, slot, dir)` (mod 4), used by the Base Camp roster chip (hidden when networked or when the zone clears `can_team`), the TRAIN "Playing on Team N" button, and both terminal clients. | `picker_common.cpp:1293`; `menu_screen_specs.cpp:3945/5543-5557`; `picker.cpp:2891` |
| M7 | **The C++ plan phase is gone (#218).** Bots are decided once, in Lua, at stage time: each mask mode's `decide` fold calls `match.census_inputs()` (reads `og.match_setting` from the live staged world), then `match.activation` / `match.fills` / `match.spawn_bots`. FFA and mutant use the band path in `mode_fighters.lua` instead. Preview == launch because the preview *censuses the staged world*, never re-derives. | `campaigns/modes/packs/modes.core/lib/mode_match.lua:340/435/494/790`; `docs/staged-lobby-design.md` |
| M8 | **Every match knob is a scalar short carried through ~15 copy sites** (3 save→settings, 2 settings→save, 4 →equivalent, 2 equivalent→save, 1 staged copy, 2 save→world, snapshot capture/apply/delta). A knob missing from one site is a silent host/joiner divergence, not a compile error. | `src/server/headless_server_runtime.cpp:347-360` (the documented bug class) |

There is therefore **no missing "squad" concept**. The pain is narrower:

1. Characters → team is one row at a time with no overview; "P2 has no
   fighters" is only discovered when GO refuses.
2. Two surfaces disagree about who is in charge: locally the roster chip
   moves characters; networked the chip is hidden and nothing on this
   machine can fix a colour mismatch between a seat and its company.
3. Nothing shows the resulting matchup — characters, seats, bots,
   strength — in one place; bots cannot be shaped per team at all.

## 1. Vocabulary (two nouns, one meaning each)

- **SEAT** — a controller (P1–P4 per machine, 16 global) pointed at a team.
- **TEAM** — a colour. Characters, seats and a bot squad are *on* a team.

The TRAIN button "Playing on Team N" is relabelled **"Team N"** (same
`cycle_guy_team` write, same ordinal 17; three pins move:
`test_menu_pins.cpp:420`, `test_picker_uncovered.cpp:228`,
`test_picker_funcs.cpp:744`).

A team is **on** when anything is on it — a seat, a deployed character,
or a bot squad. Turning a team on or off is never a separate switch; it
is putting something there or taking it away.

## 2. The LINEUP page

**Door**: the SCENARIO subscreen's free grid cell **(210,100)** — the row
becomes `VIEW LEVEL | PROGRESS | LINEUP`, above the `Teams | TROOPS |
Limit` knob row, which is where match composition already lives. The
parked `scenario_spare` ordinal 4 is reused (no table growth; label
`LINEUP`, 6 chars, 12-char budget). Terminals append a `lineup` item
after `difficulty` (Team Build position 12; the two 1-based consumers
are re-pinned). Base Camp itself is at its 73-button ceiling with a
fully packed strip, so it gets no new door.

One engine-hosted screen (`MenuScreenId::Lineup`, a `MenuScreenSpec`
like SCENARIO, not a legacy loop), four **team bands**, one action row.

```
LINEUP                                  SCEN 820: SOCCER: THE PITCH
TEAM 1  POWER 4200   P1 WASD  P3 BOB       <- header line
  [BOTS: AUTO  ] [LV: AUTO]  5 FIGHTERS     <- knob line + census
TEAM 2  POWER 1900   P2 ARROWS
  [BOTS: CASTER] [LV 5    ]  1 FIGHTER
TEAM 3  POWER --     NO SEAT
  [BOTS: NONE  ] [LV: AUTO]  NO FIGHTERS
TEAM 4  ...
[BACK] [FIGHTERS] [SPLIT EVEN] [SPLIT FAIR] [UNITE]
```

### 2.1 Bands

- **Header line**: the team chip (the same 10×10 colour chip Base Camp
  draws), `POWER n` (§4; `--` when no metric), then the seats on the
  team as `P# NAME` (the seat-card owner label, remote seats included).
  Overflow renders `+n`. No seat → `NO SEAT`.
- **Knob line**: two cyclers — bot squad preset (80px face) and bot
  level (a narrower face; budgets pinned) — and the fighter census:
  `n FIGHTERS` (deployed characters on this team across every company in
  the lobby) or `NO FIGHTERS`.
- **Diagnostics** replace the census, in the disabled grey, when they
  apply: `NEEDS k FIGHTERS` when the team has k seats and fewer deployed
  fighters than seats (M4 — the condition GO refuses), `NO SEAT: AI`
  when fighters have no seat (M3), `MAP RULES` on classic (non-versus)
  campaigns when no other diagnostic applies. Informational; GO keeps
  its refusal. A band without a power metric shows `POWER --` (the
  column keeps its edge), not an omitted field.
- Band pitch, chip column, knob column and census column are named
  constants; the layout test asserts equal pitch and shared x across all
  four bands, not only the table.

### 2.2 Actions

- **FIGHTERS** opens the **fighter list**: this machine's company, every
  slot as a row — team chip, name, class, level, `POWER n` — with the
  team cycler (`cycle_guy_team`) and a BENCH/DEPLOY toggle (`deployed`).
  It is the zone-submenu chassis (8 rows/page, PREV/NEXT, 48-char rows)
  fed by a C++ page source, not a Lua page — `CampaignPickerSession`
  gains an `adopt(page)` entry mirroring `CampaignZoneSession::adopt`.
  Row click cycles the team; the BENCH toggle is a second column of the
  same row (keyboard: FIRE cycles team, a second key toggles bench —
  documented on the footer).
- **Networked**: the fighter list is available for **your own company**
  (host or joiner) — this is the one place a colour mismatch between a
  seat and its characters can be repaired, which the hidden Base Camp
  chip cannot. The Base Camp chip stays hidden networked (one home for
  the control in that mode). One predicate,
  `og::ui::lineup_fighter_team_editable(save, slot, zone)`, gates both
  the chip (local) and the list (all modes): `picker_lobby_save_slot_editable`
  ∧ `can_team` ∧ `!assign_mode`. Peers' companies never appear in the
  list; their counts and power appear on the bands.
- **SPLIT EVEN / SPLIT FAIR / ALL TO 1** — §5.
- **BACK** returns to SCENARIO with `MENU_REDRAW`.

### 2.3 Gating

- Bot knobs are host-only (hidden per frame for joiners, nav rewired by
  a full-graph rewire pinned by a BFS test over {host, joiner} ×
  {versus, classic}). Classic (non-versus) campaigns show the knobs
  dimmed with `MAP RULES` as the census — the knobs are stored but the
  map ignores them (D32 precedent).
- `BOTS: NONE` is legal on **any** team (ruling 2026-08-26, replacing
  the earlier refusal): it means "never bots on this team" and nothing
  more — it cannot deactivate a team that has seats or fighters, so
  there is nothing to protect with a refusal, and the three clients
  share one write rule. LINEUP never reseats anyone.
- Blocking-subscreen contract (openglad-menus skill): per-frame
  `picker_lobby_poll`, host-visibility sync + rewire, `MENU_EXIT` only
  for a propagated remote start, level-reload guard (`last_level_id` vs
  `save.scen_num`).

### 2.4 Layout discipline

Declare the grid before writing a rect: title band, four team bands of
equal pitch, one action row; column edges as constants shared with the
fighter list. The visual read-back is mandatory before the PR: every
capture is described in words, column edges traced.

## 3. Per-team bot squads

### 3.1 Data (eight scalars, the existing template)

| Name (all layers) | Values |
|-------|--------|
| `bot_squad_1..4` | `0 = AUTO`, `1 = NONE`, `2.. = preset ordinal` |
| `bot_level_1..4` | `0 = AUTO`, `1..9` |

**AUTO is "the map's own value"** (the PR #245 sentinel rule): all-zero
reproduces today's behaviour byte for byte. Clamps, identical in the
three clamp homes (`sanitize_settings`, `clamp_match_setting`, both
`sync_world_from_save_data` twins + `apply_mode_state`): `bot_squad`
to `[0, 1 + kMaxBotPresets]` with `kMaxBotPresets = 8` (a joiner never
needs the preset list to clamp), `bot_level` to `[0, 9]`.

Chain: `SaveData` (v18 `.gtl` block appended after v17, read default 0)
→ `LobbySettings` (eight i16 appended AFTER `time_limit`) →
`LobbySaveDataEquivalent` (→ `MatchStageInputs` change key for free) →
`GameWorld::ctf_requested_bot_squad[4]` / `ctf_requested_bot_level[4]`
(both `sync_world_from_save_data` twins) → snapshot `serialize_match_knobs`
appended after `time_limit` (offset pins at `test_mode_snapshot.cpp:342`
keep their numbers) → `og.match_setting("bot_squad_1")` … (vocabulary in
`bindings_entity.cpp:2745`) and `og.campaign_match_get/set` (names in
`campaign_hooks.h:196`, slot map + clamp in
`campaign_state_providers.cpp:75/101`). `ViewScenarioKey`
(`picker_team_build.cpp:589`) gains all eight. `docs/modding/og-api.d.lua`
regenerated (api_stub_check).

### 3.2 Semantics in Lua (one implementation)

`match.census_inputs()` reads the eight knobs into `inputs.bot_squad[t]`
/ `inputs.bot_level[t]`; `match.fills` and `match.spawn_bots` consume:

- `AUTO` — unchanged: today's fill and level for that team.
- `NONE` — no squad on `t` even where today's rule fills it (the team
  may still activate through seats/roster; an empty team stays inactive).
- preset `p` — fill `t` with that preset's families **whether or not
  the team is occupied** (this amends invariant I3: "a bot squad spec
  applies to any team; AUTO on an occupied team = no bots"). Squad size
  follows the matched-headcount rule (D34–D40) unless the preset states
  `count`. Modes with a hard shape (basketball 5v5, mutant = one) clamp
  in their own decide fold; the preview shows the clamp.
- `bot_level` `AUTO` — today's source (difficulty formula, or the FAIR
  solve when the preset is FAIR). `1..9` — that level, replacing the
  formula exactly as matched does (D14: `set_difficulty` once per
  walker; the Easy/Hard percent still applies).
- **FAIR on an occupied team** (allies): a per-team LOCAL target =
  `max(f-sum of every other team) − f-sum of this team's humans` (may
  clamp at B(1)/LIMIT when ≤ 0); the empty-team case keeps the D11
  mean, and the local target is never banked, so other teams' AUTO
  squads stay legacy. Explicit levels are stored as the team's MATCHED
  plan (`store_plan`), so respawns and refills reproduce them. Ruling
  from the maintainer conversation of 2026-08-25.

FFA/mutant (band path) honour `NONE`/preset/level through
`mode_fighters.fill_bots` with the same table; PLAN_BASE is untouched.

### 3.3 Presets are campaign Lua

`mode_match.lua` exports `BOT_PRESETS` (ordinal = index + 2):

```lua
match.BOT_PRESETS = {
  { id = "BALANCED",  families = { "core:soldier","core:archer","core:elf","core:mage","core:thief" } },
  { id = "CASTERS",   families = { "core:mage","core:mage","core:cleric","core:elf","core:archer" } },
  { id = "BRUTES",    families = { "core:soldier","core:soldier","core:barbarian","core:orc","core:soldier" } },
  { id = "SKIRMISH",  families = { "core:thief","core:elf","core:archer","core:thief","core:elf" } },
  { id = "FAIR" },                 -- the matched solver
}
```

Names reach the menus through a **fourth campaign hook**, `lineup`
(`kCampaignHookNames` grows to four), whose registration carries
`presets = { "BALANCED", ... }` and `power = function(row) ... end`
(§4). `hooks::campaign_lineup_presets()` returns the names for the
cycler label (`BOTS: <NAME>` ≤ 12 chars, so preset ids are ≤ 6 chars);
a campaign with no hook offers `AUTO`/`NONE` only. The engine never
knows what a preset means.

### 3.4 Preview honesty

VIEW LEVEL's roster report already censuses the staged world; the fill
label for a preset squad reads `BOT SQUAD <NAME> (n)` and an explicit
level appends ` LVk` (no inner space — the worst line is exactly the
48-char budget; pinned). The applied per-team facts the label needs
(preset ordinal + explicit level) are banked by the spawn seam in the
shared mode-var **slot 4**, co-tenant with MATCHED.ANNOUNCED (ones
digit = the announce latch; codes pack at `10·100^t`) — the private
slot bands are provably full in CTF/basketball/onslaught, and growing
`kModeVarCount` costs a snapshot bump. Documented side by side as
`bank_lineup_facts` (mode_match.lua) and `kModeVarLineupFacts`
(picker_common.cpp); all-AUTO never writes the slot. The staged-rules matrix
(`tests/unit/test_staged_rules.cpp`) gains rows: NONE removes a fill
AUTO makes; preset fills an occupied team; explicit level lands once;
FAIR-allies target; basketball/mutant clamp; all-zero knobs produce a
byte-identical staged keyframe.

## 4. POWER

One core: `stat_power` in `mode_match.lua:133`. The menu side prices
company rows with the **engine's own derived stats**
(`og::ui::compute_derived_stats(const guy&)`, `picker_common.cpp:707`,
the same guy-bonus + family-base derivation spawn applies) handed to
the campaign `lineup.power(row)` hook as a row `{family, level, hp, mp,
armor, damage, stepsize, fire_frequency}`; the modes campaign registers
`power = function(r) return match.stat_power(r.hp, r.mp, r.armor,
r.damage, r.stepsize, r.fire_frequency, r.level) end`. Peers' rosters
are replicated `LobbyCharacterData` → `make_guy_from_lobby_character` →
the same derivation. Sum per team over deployed fighters; render
`POWER n`. No hook (classic campaigns) → the band shows `--` and SPLIT
FAIR degrades to level order. Bot squads show no power (their strength
is spawn-measured; VIEW LEVEL shows what actually spawned).

## 5. SPLIT actions (this machine's company, deterministic)

Operate over the **teams that have a seat on this machine** in team
order (M3 derivation, seats sorted by `player_index`); benched
characters are skipped; a single seat makes every action `ALL TO 1`.

- **SPLIT EVEN** — sort by slot index, deal round-robin.
- **SPLIT FAIR** — sort by power descending (tie: slot index), snake
  draft (1,2,…,n,n,…,2,1). No power → level descending.
- **UNITE** (the face label; the action is AllToFirst) — every deployed character to the lowest-numbered team
  that has a seat on this machine.

Pure helper `og::ui::split_company(save, seat_teams, mode, power)` →
vector of `(slot, team)`; applied through a `set_guy_team(save, slot,
team)` setter that `cycle_guy_team` now calls (its mod-4 semantics and
the `test_picker_uncovered.cpp:226` wrap pin unchanged), then the
standard roster-mutation tail. Guarded by the §2.2 predicate per slot
(locked slots are left where they are and the toast says so).

## 6. Networking submenu (#261)

Both button tables (native `picker.cpp:1729`, web `:1713`) become
**mode-dependent** through a per-frame visibility sync + the existing
full-graph rewire, pinned by BFS reachability over the three modes:

Session mode requires an **established** session (ruling 2026-08-26,
after the wasm JOIN-retry e2e caught the gap): hosting = networked ∧
host controls visible; joined = networked ∧ the joiner has received the
lobby state (`picker_lobby_players()` non-empty — `IPickerLobbyClient`
has no connection-state accessor, and `connection_alert()` is a display
string, not a predicate). A merely-connecting or failed joiner is
**Idle** — HOST / JOIN stay visible so a retry replaces the pending
client exactly as it always did. One implementation:
`picker_current_networking_menu_mode()`, which every mode reader in
`configure_networking_body` calls (both builds share that body).

| Mode | Rows shown |
|------|-----------|
| Idle (incl. connecting/failed joiner) | as today: ROOM CODE, ACTIVE GAMES list, DIRECT (LAN) fields, HOST / JOIN |
| Hosting | header `PLAYERS` over the five list rows repurposed as **machine rows**, `ROOM <code>` line, **DISCONNECT** (new ordinal appended after the room rows; drawn in JOIN's rect), HOST/JOIN/LAN hidden |
| Joined | same, rows read-only, DISCONNECT = leave |

**Machine rows** (`build_networking_machine_rows`, 39-char budget
native / 38 web, one per `machine_id` from `picker_lobby_players()`):
`M1 <COMPANY> (HOST) (YOU)  P1 P2  READY` — the COMPANY leads (the
transport `name` is an opaque `net-<hex>` identity on relay lobbies and
is only a fallback when no company name exists). The list shows the
first five machines in the five repurposed row rects (documented cap;
a >5-machine lobby — possible at 16 one-seat machines — lists five,
no pager in v1). Host clicking a
foreign row → `yes_or_no_prompt("KICK <NAME>?")` → `LobbyKickMessage{
machine_id }` (kind 8); own row and host row are inert. Server: host
gate, then the existing `disconnect_client` path after sending a
`LobbyKickedMessage` (kind 9, server→peer) so the kicked client shows
`KICKED BY HOST` in the connection alert and reverts to a local client
via `picker_replace_lobby_client(create_local_picker_lobby_client())`.
**DISCONNECT**: same replace on both roles; a host's server teardown
disconnects every peer through the transport as today. Base Camp's
line-B census and the LINEUP bands read the same `picker_lobby_players()`.
Curses lobby gains `k` (kick the selected foreign seat's machine, host)
and its Esc already leaves; text stays the documented stub.

## 7. Level-exit popup headers (#259)

`sdl_video::draw_dialog` (`video_sdl.cpp:568`) is the only header
painter; no commit in #245–#258 touched it, the fonts, or the palette,
and every ending title string is trace-pinned green. So the mechanism
is *state*, not code, and the rule from the pr-workflow skill applies:
**observe first**. WP-A drives the real post-level sequence
(`test_results_screen_full_ui` shape) with real dialogs forced and
captures the frame; the two live hypotheses are (a) `text_big` is a
fixed-index-40 font (its glyphs ignore the colour argument) and (b)
`text::sizex/sizey` are cached at construction. The regression test is
a header-band pixel read-back derived from `compute_dialog_bounds` (not
hard-coded y), plus the body-band control, and must fail on the
observed tree before the fix lands.

## 8. Terminal clients

Text and curses: `lineup` item after `difficulty` rendering the four
bands as context lines + the two knob items per team, `fighters`
(existing roster rows with `move S N` semantics + bench), the three
SPLIT items; curses networking gains kick/disconnect. All labels from
one `format_*` helper each. The two 1-based consumers are re-pinned in
the same commit.

## 9. Test matrix (teeth, not coverage theatre)

- **Pure**: labels (exact strings, budgets), clamps in all homes, SPLIT
  determinism (fixed rosters → exact `(slot, team)` vectors; snake order;
  bench skipped; locked slot untouched; single-seat = ALL), census and
  diagnostics per band incl. the `NEEDS k` rule, preset ordinal ↔ name,
  `.gtl` v18 triple (reads / v17 defaults / round-trip), LobbySettings
  serialize symmetry (`VALIDATE_SERIALIZATION`), lobby offset pins
  re-derived (not +N).
- **Lua/staged** (`test_staged_rules.cpp`, `test_modes_*`): the §3.4
  rows; `lineup` hook registration + `power` agreement with the C++
  test oracle `measured_walker_f`.
- **Lobby** (`test_lobby_server.cpp`): kick by machine id removes every
  seat of the peer and re-indexes; non-host kick denied; kicked notice
  precedes the disconnect; knobs replicate host → joiner; a joiner's
  SettingsChange is dropped. One live two-peer flow in
  `test_picker_network_client.cpp` (clone of `:5857`): host kicks →
  joiner sees `KICKED BY HOST` and is local again.
- **SDL layout/nav** (`og_test_menu_engine` for fast pins): LINEUP band
  relations, Networking three-mode BFS, fighter list ≡ zone-submenu
  geometry, SCENARIO table re-pin (ordinal 4 now `lineup` at 210,100).
- **SDL flows** (`og_test_lineup`, split out of `og_test_matchup` once
  the LINEUP suite passed ~111s of wall-clock settles; never
  `og_test_menu_ui`): open LINEUP from SCENARIO, cycle a preset and a
  level (label pins), SPLIT FAIR on a 6-character two-seat company and
  the seat rail follows, FIGHTERS row cycles a team networked; hosting
  → kick flow; #259 red-then-green.
- **Team matrix** (openglad-menus skill): {1,2,4 seats} × {ally, pvp}
  through the real launch path, re-run after a SPLIT, asserting
  spawn-at-own-team-point and same-team-can't-damage.

## 10. Version bumps (one coordinated commit)

`kNetworkProtocolVersion` 15 → 16 (`net_transport.h:161`; kinds 8/9 +
eight settings shorts); `kSnapshotFormatVersion` 11 → 12
(`world_snapshot.h:35`); `kReplayFormatVersion` 17 → 18 (`replay.h:33`);
`.gtl` 17 → 18 (`save_data.cpp:1029`). Pins: `test_net_transport.cpp:256
/1323/1508`, `test_input_state_net.cpp:133/149` (`0x0f` → `0x10`),
`test_replay.cpp:347`, lobby offsets `test_net_transport.cpp:3057/3115`
(+16 bytes of settings), `find_first_snapshot_difference` learns the
eight scalars. Mixed builds: an old guest under a new host cannot share
a lobby (version-gated hello) — recorded here so nobody re-derives it.

## 11. Work packages and tiers

| WP | Content | Tier |
|----|---------|------|
| A | #259 observe → red test → fix | opus probe; fable if the mechanism is subtle |
| B | Wire: eight knobs through every copy site, kick/kicked messages, `LobbyServer` kick, `IPickerLobbyClient::kick/disconnect`, bumps + every pin, `og.match_setting` / campaign vocabulary, api stubs | opus |
| C | Lua: `BOT_PRESETS`, `census_inputs` knobs, fills/spawn semantics, allies target, `lineup` hook + `power`, staged-rules rows | fable |
| D | Pure helpers: labels, census/diagnostics, `split_company` + `set_guy_team`, editable predicate, machine-row formatter, hook bridge `campaign_lineup_*`, `compute_derived_stats` rows | opus |
| E | SDL LINEUP screen + fighter list + SCENARIO door + layout/nav/flow tests | **fable** |
| F | SDL Networking three-mode + kick/disconnect UI + flows | **fable** |
| G | Text + curses twins, position-consumer repins | opus |
| H | Team matrix + SPLIT launch tests | opus |
| I | Visual read-back of every screen, media, PR | fable read-back; opus capture mechanics |

Review: one fable reviewer per merged wave; adversarial verify on every
finding before it is acted on.

## 12. Review rulings (wp/review-lua, 2026-08-26)

Findings from the post-build review of the Lua/sim half, each fixed
red-then-green in `tests/unit/test_staged_rules.cpp` /
`tests/unit/test_campaign_hooks.cpp`.

- **R1 — no `error()` is reachable from a legal knob (L1).** FFA and
  mutant gain the team modes' *decide fold*: `fighters.enumerate(obs)`
  (pure) counts the deployed roster, `fighters.planned_count(n, target)`
  answers what the fill will reach (NONE keeps `n`, anything else
  `max(n, target)`), and a band below two fighters raises
  `"<mode>: fewer than two fighters"` **before** `deploy`/`assign`/
  `consume_markers`/`strip` touch the world. The kept post-refusal world is
  therefore the authored one: hero on its seat team, markers standing,
  cast unstripped. The post-fill `count < 2` errors are gone. A mode
  refusal does **not** trip the LobbyServer start gate (that denies
  `StageFailed` alone): GO adopts the kept world under classic rules —
  the documented refused-match shape — so "untouched" is the whole
  guarantee.
- **R2 — a squad beside occupants is sized to the room the hard shape
  leaves (L2).** One helper, `match.squad_room(cap, roster)` =
  `max(cap − roster, 0)` (nil cap = unbounded), applied by `fills()` over
  the census roster and by `spawn_bots` over the live has_guy count
  (identical in the staged world). Basketball: 3 humans + BALANC = 2
  allies, five on court; a full court leaves no room and **no squad row**
  (`row.squad` nil, nothing spawns). A decision row's `count` is now what
  the team will *field*: a company/troops row with a squad beside it
  counts roster + squad, and `row.squad_count` carries the squad alone
  (amends §3.2's "whether or not the team is occupied": the preset still
  applies, sized to the room). The staged report label for that shape
  lives in `picker_common.cpp` (owned by the C++ wave).
- **R3 — `TEAMS MATCHED` is the match-wide solver's signal (L3).**
  `spawn_matched_bots` announces only when its target is the banked
  `MATCHED.TARGET` (TROOPS: FAIR); a FAIR preset's local allies solve
  passes `announce = false` and never latches the shared ones digit. The
  applied-FAIR fact (`60` for team 1) still banks above the latch.
- **R4 — facts are banked for what spawned (L4).** `bank_lineup_facts`
  runs only when ≥ 1 member spawned (both the solved and the plain arms);
  a preset/level pair that fields nothing banks nothing, so the pane
  never names a squad that is not on the floor. An explicit level's plan
  is still stored (respawns reproduce it).
- **R5 — `lineup.presets` must be a sequence (L5).** The registrar walks
  the table and refuses (`luaL_error`, book not registered, stock
  AUTO/NONE wheel) when the entry count differs from `lua_rawlen` — a
  keyed table or a holed array used to register zero or fewer names
  silently.
- **R6 — `lineup.power` answers are int64 or nothing (L6).** Integer
  subtype as is; a float only when finite and inside `[-2^63, 2^63)`,
  truncated toward zero; NaN, ±inf, out-of-range and non-numbers answer
  false with a logged `not a finite integer` / `not a number` error (the
  band shows `--`). The old `static_cast` of NaN rendered `INT64_MIN`.
- **R7 — band modes read team 1's pair only (L7, §3.2).** In FFA/mutant
  every fighter wears a band byte and no score team fields a squad, so
  `bot_squad_2..4` / `bot_level_2..4` are dead there. The fact a menu
  needs to dim them is the mode name the staged world already carries
  (`ModeState::name` = `FFA` / `MUTANT`, the staged report's
  `mode_name`); no extra mode var is banked. Documented at
  `mode_fighters.lua` `band_knob`.
- **Cleanup.** `fills()` sizes a preset squad through
  `match.preset_squad_size` (exported) instead of building the family
  table to take its length; `preset_squad` is the same rule plus the
  table.

---

# Amendment 2026-08-26: TEAMS retires into the band, LIMIT becomes SCORE, LV becomes an offset

Maintainer ruling after the first read of the LINEUP page: "we don't
need TROOPS or TEAMS if we have LINEUP, right?", "what is LIMIT?", and
"LV should be relative". Rulings A1–A6 supersede the matching sentences
above.

| # | Ruling |
|---|--------|
| A1 | **TEAMS (`ctf_team_count`) is retired as a control.** Its only power LINEUP lacked — *deactivating an authored team* — moves onto the band as a wheel value **`OFF`**. The wheel is now `AUTO / OFF / NONE / <presets>`; storage `bot_squad`: `0 = AUTO`, `1 = OFF`, `2 = NONE`, `3.. = preset ordinal (index + 3)`; `kMaxBotSquad = 2 + kMaxBotPresets`. (The field only exists on this branch, so renumbering costs nothing.) |
| A2 | **`OFF` semantics = exactly what `TEAMS: n` did to a dropped team**: the team leaves the active mask — its authored troops, generators and flags are not fielded — decided in the same activation fold. A team with a seat or a deployed fighter is *on* by definition, so `OFF` there is **refused with a toast** (`TEAM n HAS PLAYERS` / `TEAM n HAS FIGHTERS` — the refusal that was wrong for NONE is right for OFF), and the lobby seat domain (`lobby_effective_team_mask`) excludes OFF teams so a joiner cannot move a seat onto one; the existing settings-change reteam handles any residual. `AUTO` keeps meaning the map's own value (manifest default or authored count); a team the map leaves inactive turns on by putting something on it (a seat, or a preset squad). |
| A3 | **`ctf_team_count` stays in the save/wire layout (no format bump) but is inert**: the sanitizer and both world-entry twins snap it to `0`, the campaign vocabulary drops `team_count` (`kCampaignMatchSettingNames`, provider slot map, stub regenerated), `census_inputs` stops reading it, `effective_team_mask` becomes the identity, and every TEAMS control goes: the SCENARIO cycler, the Modes camp MATCH SETUP row (digest loses "Auto sides"), the terminal items and every pin. Legacy `.gtl` values 2/3/4 heal to Auto on first sanitize — a one-time documented migration (D30 precedent). |
| A4 | **TROOPS stays.** `ALL`/`OWN` answers a question LINEUP has no home for — whether the map's *own* authored cast fights — and `FAIR` is kept as the one-click default: *TROOPS sets what `BOTS: AUTO` resolves to on an empty team; LINEUP's per-team value overrides it.* Documented on the band (`AUTO` census hint) rather than duplicated. |
| A5 | **`Limit: Map` is relabelled `SCORE: MAP` / `SCORE: 5`** (the score limit — captures, goals, kills; `MAP` = the level's own). The camp page keeps `TARGET SCORE`. After A3 the SCENARIO knob row re-grids to `TROOPS (30,140) | SCORE (120,140)`, cell (210,140) free; nav and the static-layout pin move with it. |
| A6 | **`LV` is an offset, −5…+5, on top of the AUTO source**: `bot_level` clamps `[-5, 5]`; `0` renders `LV: AUTO`, others `LV +2` / `LV -1`; wheel `AUTO, +1 … +5, -5 … -1, AUTO`. Resolution (one place, `bot_level_for`): `clamp(base + offset, 1, 9)` where `base` is the FAIR solve for a FAIR squad and the difficulty formula otherwise; the *resolved* level is what the team's plan banks (respawns reproduce it), and the preview label reads `LV+2` / `LV-1` (no inner space, budget rule from §3.4). Band modes apply the same offset to team 1's pair. |

Work packages: W4-B (opus) the scale/clamps/vocabulary/mask + unit
pins; then in parallel W4-A (fable) Lua activation OFF + offset
resolver + camp page + staged rows, W4-C (fable) SDL SCENARIO re-grid,
SCORE relabel, LINEUP OFF refusal + LV labels + flows, W4-G (opus)
terminals + position repins. Gate, visual read-back, media refresh.

## As built: the W4-B layer (2026-08-26)

The engine half of A1/A2/A3/A6 is in. What the other three packages
have to know, so nobody re-derives it:

- **One scale, everywhere.** `bot_squad` is `0 AUTO / 1 OFF / 2 NONE /
  3.. preset`, named once in `lobby_state.h` as `kBotSquadAuto`,
  `kBotSquadOff`, `kBotSquadNone`, `kBotSquadPresetBase` and bounded by
  `kMaxBotSquad = kBotSquadPresetBase - 1 + kMaxBotPresets` (10). The
  same numbers ride the knob, the wire, the save AND the banked preview
  fact, so no layer translates. **The Lua half of the renumber is
  W4-A's**: `mode_match.lua preset_for` must become
  `BOT_PRESETS[knob - 2]`, and the raw knob values hand-written in
  `test_staged_report.cpp` / `test_staged_rules.cpp` / `test_modes_*.cpp`
  each move up by one. Until that lands, the three staged tests that
  assert a preset NAME read the wrong entry.
- **The fact code is a mixed-radix pair, and every preset fits.** (This
  bullet as first written claimed `ordinal * 10 + level` and a seventh
  preset that would not fit; W4-A landed the real packing.) A6's offset
  has ELEVEN values (AUTO, ±1..±5), which no decimal digit holds, so
  `bank_lineup_facts` packs `squad * 11 + offset_code` per team into the
  same base-100 field: `squad` is 0 for no applied preset, else
  `ordinal - (kBotSquadPresetBase - 1)` = 1..`kMaxBotPresets`;
  `offset_code` is 0 for AUTO, 1..5 for +1..+5, 6..10 for -1..-5. The
  worst code is `8 * 11 + 10 = 98 < 100`, so all eight registrable
  presets fit with no snapshot bump. C++ twin: `lineup_fact_code` /
  `lineup_fact_preset_index` / `lineup_fact_offset` in
  `picker_common.cpp` (`kModeVarLineupFacts`).
- **`bot_level` is signed now.** `[-5, +5]`, one clamp home
  (`clamp_bot_level`), zero = AUTO. `LV: AUTO` / `LV +2` / `LV -1`, and
  the wheel is AUTO, +1..+5, -5..-1. Save and wire fields were already
  signed 16-bit, so **GTL v18, protocol 16, snapshot v12 and replay v18
  are unchanged** by this whole amendment.
- **OFF narrows the seat domain in `lobby_effective_team_mask`**, after
  the versus check (a classic campaign stores the knobs and the map
  decides, so OFF must not narrow anything there) and with one guard: if
  OFF would empty the domain, the unfiltered mask stands — a lobby with
  nowhere to sit can seat nobody, and a crafted client must not be able
  to arrange that. The settings-change reteam needed no new rule: it
  already sweeps against this mask.
- **`ctf_team_count` is inert, not gone.** The field keeps its place in
  the save and on the wire; `sanitize_settings`, both
  `sync_world_from_save_data` twins and `apply_snapshot` all write 0.
  `og.match_setting("team_count")` still answers (always 0);
  `og.campaign_match_get/set("team_count")` now raise unknown-name —
  **`campaign_picker.lua` still reads it** (the MATCH SETUP TEAMS row and
  the rules digest), which is W4-A's camp-page work and is red until it
  lands.
- **`cycle_ctf_team_count` / `format_ctf_teams_label` are GONE.** W4-B
  left them compiling as stubs (write Auto / answer `"Teams: Auto"`) for
  the three callers W4-C and W4-G still owned; those callers went with
  their waves and the pair was deleted with the last of them, along with
  the pins that only proved the stubs still answered. `ctf_team_count`
  itself keeps its place in the save and on the wire, inert.

## As built: the W4-A layer (2026-08-26)

The Lua half of A1/A2/A6 — the activation fold, the offset resolver and
the camp page. Four rulings the build settled, recorded so nobody
re-derives them from the code:

- **A band mode's OFF is a NONE.** `match.squad_off` answers true for
  both values, and FFA/mutant read team 1's pair through
  `mode_fighters.band_knob`. There is no mask to leave in a band mode —
  every fighter wears a band byte and no score team fields a squad — so
  OFF there means the one thing the two values share: this team fields
  no squad.
- **Outside the authored domain, nothing activates.** Activation's step 3
  ("plus the occupied") is gated on `core.mask_has(authored_mask, team)`:
  a deployed roster or a preset squad on a team the map never authored
  has no anchors and no flag — nowhere to spawn, nothing to score — so it
  still fights under classic rules and never turns a team on. The
  pre-amendment truth, kept deliberately.
- **A preset can turn on a team the map leaves inactive.** The same step 3
  puts an authored-but-inactive team into the mask when its knob names a
  preset (FAIR included). This is what "a team is on when anything is on
  it" means for bots, and it is the reason `BOTS: <NAME>` needs no
  companion switch.
- **Under `TROOPS: ALL`, a deployed roster now activates a
  manifest-inactive team** (behaviour change). Step 1 narrows the base
  mask to the manifest row's `teams` under ALL; step 3 then re-adds any
  authored team carrying a roster, which it previously did not. Parity is
  unaffected — the harness stages no lobby roster onto an inactive
  authored team — and the shape is what the page promises: put a fighter
  on a colour and that colour plays.

---

# Amendment 2 (2026-08-26): one FILL wheel, a MAP UNITS box, no TROOPS, no FIGHTERS

Maintainer ruling on the second read: "way too many options for BOTS
… a FILL: NONE/WEAK/FAIR/STRONG/BRUTAL relative to the weakest team
and get rid of LV; a checkmark per team for the map-shipped units; get
rid of TROOPS; no FIGHTERS button — Base Camp already does that."
Rulings B1–B9 supersede A1–A6 and the matching sentences above.

| # | Ruling |
|---|--------|
| B1 | **The band has exactly two controls: `FILL:` and a `MAP UNITS` box.** `BOTS` presets, `LV`, `OFF` and the preset registration in the `lineup` hook are gone (the hook keeps `power`). |
| B2 | **`FILL` = the matched solver with a multiplier.** Values and storage `fill[4]`: `0 = FAIR` (the default — all-zero stays the default state), `1 = NONE`, `2 = WEAK ×0.75`, `3 = STRONG ×1.25`, `4 = BRUTAL ×1.5`; wheel order NONE, WEAK, FAIR, STRONG, BRUTAL. The squad is the mode's stock `BOT_SQUAD`, sized by the matched-headcount rule; level solved by the existing D22 argmin against `target = reference × m`. |
| B3 | **Reference = the weakest human team's f-sum** (all human teams in the lobby; D11's mean is retired). Allies (FILL on an occupied team): `target = (strongest other team's f-sum − this team's human f-sum) × m`, floor 0 (→ no squad). No human power anywhere → the legacy difficulty formula, as today. Hard-shape modes cap the squad at `cap − roster` (R2). Band modes (FFA/mutant) fill singles at the solved level; NONE with <2 fighters refuses (R1). |
| B4 | **`MAP UNITS` box per team, `map_units[4]`: `0 = on` (default), `1 = off`** — whether the map-shipped units on that team are fielded (the old strip, now per team). Dimmed/inert when the map ships no units on that team (census hint `NO MAP UNITS`). A team is active when anything is on it: a seat, a deployed fighter, fielded map units, or a FILL squad. |
| B5 | **TROOPS is retired everywhere at once** (the SCENARIO cycler, the camp MATCH SETUP row, the terminal item, `og.campaign_match_*("strip_troops")`); `ctf_strip_scenario_troops` stays on disk/wire but is sanitized to 0 like `ctf_team_count`. `bot_squad`/`bot_level` are renamed `fill`/`map_units` at every copy site (all on this branch; no format bump — protocol 16 / GTL 18 / snapshot 12 / replay 18 unchanged). The SCENARIO knob row is `SCORE` alone at (30,140). |
| B6 | **FIGHTERS is deleted** (screen, door, terminal items). Its one unique power — repairing a seat/colour mismatch networked — moves to the Base Camp roster chip: editable for **your own company** in networked sessions through the existing `lineup_fighter_team_editable` predicate (the `!networked` gate on the chip goes; foreign rows stay inert). SPLIT EVEN / SPLIT FAIR / UNITE stay on LINEUP; strip = `BACK | SPLIT EVEN | SPLIT FAIR | UNITE`, flush right, 6px gaps. |
| B7 | **Preview** (VIEW LEVEL) renders what the staged world holds: `MAP TROOPS (n)`, `BOT SQUAD (5) FAIR` / `STRONG`, `COMPANY+BOTS (3+2) WEAK`; the banked facts carry the fill code (no ordinal, no offset). Refusal sentences unchanged. |
| B8 | **No refusals on the wheel.** NONE is legal anywhere; nothing on the band can deactivate a team that has people, so the toasts go. |
| B9 | **Knob line geometry**: `FILL` face (12,y+15,80,15); `MAP UNITS` box = the Base Camp deploy box (14×10, `X` when on) at x=98 with the text `MAP UNITS` at x=116 in the knob line; census/diag text moves to x=190 (21-char budget). Layout relations and BFS pins re-declared. |

Work packages: W5-B (opus) rename/scale/inert-troops/vocabulary/labels
+ pins; then W5-A (fable) solver multiplier + reference + per-team
strip + activation + band modes + camp page + staged rows; W5-C
(fable) the band's two controls, FIGHTERS removal, Base Camp chip
networked, SCENARIO re-grid, flows, captures; W5-G (opus) terminals.
