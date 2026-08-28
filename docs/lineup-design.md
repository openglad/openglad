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

## As built: the W5-B layer (2026-08-26)

The engine half of B1-B5 and B8 is in. What the other three packages have
to know, so nobody re-derives it:

- **One scale, one name, everywhere.** `bot_squad` is `fill` and
  `bot_level` is `map_units` at every copy site — `SaveData`,
  `LobbySettings`, `LobbySaveDataEquivalent`, `GameWorld`
  (`ctf_requested_fill` / `ctf_requested_map_units`), `WorldSnapshot`,
  `ViewScenarioKey`, the `og.match_setting` names (`"fill_1".."fill_4"`,
  `"map_units_1".."map_units_4"`) and the campaign vocabulary. `fill` is
  `0 FAIR / 1 NONE / 2 WEAK / 3 STRONG / 4 BRUTAL`, named once in
  `lobby_state.h` as `kFillFair`, `kFillNone`, `kFillWeak`, `kFillStrong`,
  `kFillBrutal`, bounded by `kMaxFill`; `map_units` is
  `kMapUnitsOn` / `kMapUnitsOff`, bounded by `kMaxMapUnits`. GTL 18,
  protocol 16, snapshot 12 and replay 18 are all unchanged — the fields
  kept their places and their widths, only their names and their meanings
  moved.
- **The multiplier table is Lua's alone.** The engine stores and clamps a
  FILL CODE and knows nothing else about it: there is no `kFillPercent[]`
  in C++, because the mode Lua is the only layer that solves a target and
  a second copy of `{100, 0, 75, 125, 150}` would be exactly the rule twin
  this branch keeps deleting. **W5-A owns that table**, keyed by the raw
  code `og.match_setting("fill_N")` answers.
- **Two clamp homes, five callers.** `og::sim::clamp_fill` and
  `og::sim::clamp_map_units` (lobby_state.h) are called by
  `sanitize_settings`, `clamp_match_setting`, both
  `sync_world_from_save_data` twins and `apply_mode_state`.
  `clamp_bot_squad` / `clamp_bot_level` and every `kBotSquad*` /
  `kMaxBotPresets` / `kMinBotLevel` / `kMaxBotLevel` constant are gone.
- **TROOPS is inert, not gone.** `ctf_strip_scenario_troops` keeps its
  place in the save and on the wire; `sanitize_settings`, both
  `sync_world_from_save_data` twins and `apply_mode_state` all write 0, so
  a legacy 1/2/3 heals once, silently (the D30 precedent).
  `og.match_setting("strip_troops")` still answers (always 0);
  `og.campaign_match_get/set("strip_troops")` now raise unknown-name.
  `next_ctf_scenario_troops` / `toggle_ctf_scenario_troops` /
  `format_ctf_troops_label` / `change_ctf_troops` /
  `ButtonAction::CycleCtfScenarioTroops` /
  `PickerMenuCommand::ToggleCtfScenarioTroops` are all deleted; the
  SCENARIO row at index 6 is PARKED (`scenario_troops_spare`, zero rect,
  hidden, no nav) so `kScenarioMenuCtfCapsIndex` and the count never
  shifted. **The knob row's re-grid — SCORE alone at (30,140) — is still
  W5-C's**, and the terminal SCENARIO list lost a row, so **the two
  1-based position consumers are W5-G's to re-pin**.
- **The band's two controls.** `og::ui::format_lineup_fill_label` (12-char
  face; "FILL: STRONG" and "FILL: BRUTAL" are exactly 12, so the budget is
  spent, not merely respected), `og::ui::lineup_fill_name` (the bare word,
  shared with the preview pane), `og::ui::cycle_lineup_fill` (the DISPLAY
  order NONE, WEAK, FAIR, STRONG, BRUTAL — deliberately not the storage
  order), `og::ui::format_lineup_map_units_label` ("MAP UNITS: ON" /
  "MAP UNITS: OFF", the terminals' spelling of the SDL box) and
  `og::ui::toggle_lineup_map_units`. `format_lineup_bots_label`,
  `format_lineup_level_label`, `cycle_lineup_bots`, `cycle_lineup_level`
  and `lineup_bots_wheel_next` (with `LineupBotsWheelStep` and both
  refusal toasts) are deleted — B8 left no rule for them to hold.
- **`LineupTeamBand` gains `map_unit_count`**, the staged census of
  authored map units on the team, supplied through a new trailing
  `std::span<const int> map_unit_counts` parameter on
  `build_lineup_bands` (an empty span leaves every count at 0, which reads
  as "the map ships none"). `og::ui::format_lineup_map_units_census`
  answers `"NO MAP UNITS"` when the count is 0 and empty otherwise; it is
  deliberately NOT folded into `format_lineup_census`, so no pinned
  fighter census moves for it. **Feeding the census and placing the hint
  are W5-C's**; the terminals read the same field.
- **The seat domain is the authored mask again (B8).**
  `lobby_effective_team_mask` lost its OFF clause and its
  empty-domain guard with it; nothing a band can hold narrows where a seat
  may sit.
- **THE SHARED SLOT-4 DIGIT LAYOUT.** Written out twice on purpose — here
  and beside `lineup_fact_code` in `picker_common.cpp` — because the two
  halves live in different languages and can only agree on paper:

  ```
  slot value = latch                    (ones digit, MATCHED.ANNOUNCED)
             + code(team 0) * 10
             + code(team 1) * 10 * 100
             + code(team 2) * 10 * 100^2
             + code(team 3) * 10 * 100^3
             + refusal      * 1e9       (mode_match.lua REFUSAL_BASE)

  code = 0            nothing banked — no squad of this team's fielded
                      anybody, so the pane names no fill
       = fill + 1     the APPLIED FILL code plus one:
                      1 = FAIR, 2 = NONE, 3 = WEAK, 4 = STRONG, 5 = BRUTAL
  ```

  The `+ 1` is the whole reason the field is not just the fill code: FAIR
  is 0 and is also the default, so a bare code could not tell "a FAIR
  squad walked on" from "this team banked nothing". This replaces the
  A6-era `squad * 11 + offset_code` pair. C++ side:
  `lineup_fact_code` / `lineup_fact_fill` / `kLineupFactFillBias`.
  **W5-A owns `bank_lineup_facts` writing exactly this**, and R4 still
  holds: bank only when ≥ 1 member spawned.
- **The preview pane (B7).** `ScenarioRosterReport::team_squad_name` and
  `team_squad_level` are replaced by `team_squad_fill` (an
  `std::array<int, 4>` initialised to −1 = nothing banked). Rows read
  `BOT SQUAD (5) FAIR`, `COMPANY+BOTS (3+2) WEAK`, `MAP TROOPS (7)`. The
  worst row —
  `"  YELLOW TEAM  ACTIVE - COMPANY+BOTS (3+2) BRUTAL"` — is 49, one over
  the 48-char budget, so the separator space before the fill word is what
  the budget spends (`(3+2)BRUTAL`); a clipped word would be a different
  word. Both shapes pinned in `test_staged_report.cpp`.
- **The `lineup` hook is `power` alone.** `presets` is an unknown key now
  (the registrar refuses the book and says so), `campaign_lineup_presets`
  and `VmState::campaign_lineup_presets` are gone, and so are
  `hooks::kMaxBotPresets` and `kLineupPresetNameMax`'s reason to exist.
  `docs/modding/og-api.d.lua` regenerated.
- **Reds handed on, by test name.** W5-A: `og_unit_stage`
  (`test_staged_rules.cpp` — the raw knob values and every preset/offset
  row), `og_unit_modes` / `og_unit_ffa` / `og_unit_soccer` /
  `og_unit_basketball` / `og_unit_onslaught` (`test_modes_*.cpp` — the
  `kBotSquad*` constants), and `campaign_picker.lua`, which still
  registers `lineup.presets` and so registers no book at all until it
  drops the key. W5-C: `og_test_lineup`, `og_test_menu_ui`,
  `og_test_menu_engine` (`test_lineup_ui.cpp`, `test_ctf_ui.cpp`,
  `test_menu_layout.cpp`, `test_menu_pins.cpp`, `test_view_team.cpp`,
  `test_picker_funcs.cpp`). W5-G: `test_platform_headless.cpp`,
  `tests/curses/test_curses_picker_client.cpp`,
  `tests/curses/test_curses_network.cpp`,
  `tests/integration/test_menu_model.cpp`, and
  `scripts/test_text_picker_interactive.sh`.

## As built: the W5-A layer (2026-08-26)

The Lua half of B1-B4 and B7 — the solver multiplier, the weakest-human
reference, the per-team strip, the activation fold, the band modes and
the staged rows. The rulings the build settled, recorded so nobody
re-derives them from the code:

- **The multiplier table lives once**, in `mode_match.lua`:
  `FILL_PERCENT = { [0]=100, [1]=0, [2]=75, [3]=125, [4]=150 }`, keyed by
  the raw code `og.match_setting("fill_N")` answers. `fill_percent`
  answers a junk code as FAIR's 100, and `applied_fill` normalises the
  BANKED code the same way, so a crafted off-wheel value stages
  byte-identically to FAIR (the replacement for the old
  unregistered-ordinal identity pin).
- **The census prices teams now.** `census_inputs` reads `fill[t]` /
  `map_units[t]` and adds `teams[t].power` — the human f-sum per team —
  so `fills` decides the ALLIES gap from the inputs alone and the
  decision rows' counts are the fielded counts (the
  apply-executes-decision matrix stayed exact). `strip_troops` and
  `team_count` are read by nobody.
- **Fielded map units activate their team past the manifest default.**
  B4's list is implemented literally in `activation`: an authored team
  with a roster, or with authored npcs/generators whose box is on, is
  active whatever `row.teams` says — the generalisation of the
  2026-08-18 "as many teams as the map actually has" directive (an
  Onslaught board of three foundries on a teams-2 manifest fields all
  three now). The manifest default still governs EMPTY teams: an
  anchors-only side past it gets no FILL squad and stays out (the old
  OWN whole-domain refusal shapes are gone with TROOPS — a 4-anchor
  2-mouth pitch is a clean 2-side match now, not a "no goal rect"
  refusal). `auto_default` keeps its signature.
- **The reference is the weakest human team's f-sum** (`census_power`
  returns it; D11's mean is deleted). `MATCHED.TARGET` banks it at init
  (the latch shape kept); the spawn seam re-censuses the same world for
  the allies gap. Empty team: `target = ref × pct / 100`. Occupied team:
  `target = (strongest other f-sum − own f-sum) × pct / 100`, and a
  target at or below zero is NO SQUAD (`fill_target` answers nil — the
  A-era B(1)-clamped allies are gone). No human power anywhere: the
  legacy difficulty formula, unscaled (B3's own words), which stores no
  plan.
- **`matched` means "a reference exists"**: activation reports it for any
  deployed roster (the strip-sentinel gate died with TROOPS), and
  `matched_size` is always the D34 min-roster rule. Consequence: the
  DEFAULT world with rosters is the matched world — the old TROOPS: FAIR
  behaviour — and the all-zero byte-identity is gone by design (the
  staged-rules suite pins the new default rows instead, and says so).
- **`TEAMS MATCHED` announces for any solved squad**, allies included,
  through the unchanged R3 one-shot latch (`announce_matched` gates on
  init and fires once); the legacy arm and the band modes never announce.
- **The facts bank the applied fill code** (`bank_lineup_facts(team,
  fill)`, code = fill + 1) whenever ≥ 1 member spawned — the legacy arm
  included, so a default no-humans world now banks FAIR on its squads
  (B7: every squad row closes with its fill word). R4 holds: 0 spawned =
  0 banked. The refusal digit (`REFUSAL_BASE`) is untouched.
- **The per-team strip** (`mode_strip.strip_authored_troops()`, no
  arguments now) retires each score team's guy-less livings AND
  generators when its box reads off — non-zero = off, so junk cannot
  field hidden units. Two deliberate narrowings against the old OWN
  strip: wildlife (teams ≥ 4) has no box and always stands, and the
  Onslaught foundry exception is gone — generators follow the box (B4),
  so unchecking an Onslaught side removes its board and the side with it
  (`keep_generators` deleted from the strip and from `fills` opts; the
  no_bots empty arm now deactivates a team with nothing standing, where
  it used to leave an E8 empty-but-active side). `mode_strip` imports
  `mode_match` for the box read (`map_units_fielded`) — the old
  dependency ran the other way and died with `strip.KEEP`.
- **Band modes solve their singles** (`mode_fighters.fill_bots`): the
  reference is the weakest DEPLOYED fighter's f (every human team in a
  band is one fighter), each single is D24-measured on its own base and
  argmin-solved against `ref × pct`; NONE keeps the deployed band (R1's
  decide-fold refusal unchanged); no plan is packed (PLAN_BASE overflows
  band bytes) and no facts are banked — the staged report renders a band
  by its fighters. There is no zero-power fallback: the fold refused
  below two fighters, and a live has_guy fighter always prices above
  zero (the +60 offense floor), so the reference always exists at fill
  time.
- **Deleted Lua** (with their tests): `BOT_PRESETS`,
  `BOT_SQUAD_AUTO/OFF/NONE/PRESET_BASE`, `preset_for`, `preset_families`,
  `preset_squad`, `preset_squad_size`, `fair_target`, `level_offset`,
  `resolve_level`, `resolve_plan`, `offset_code`, `FACT_OFFSET_RADIX`,
  `strip.KEEP`, `strip.OWN`, `strip_everything`; `lineup_fact` is now
  the one-argument `fill + 1`. New exports: `FILL_FAIR`, `FILL_NONE`,
  `FILL_PERCENT`, `fill_percent`, `applied_fill`, `map_units_fielded`,
  `fill_target`.
- **The camp page needed nothing**: W5-B had already cut the TROOPS row
  and the `presets` key; the digest (`map` / `to 5`) and the `lineup =
  { power = ... }` hook stand as they left them.

---

# Amendment 3 (2026-08-27): the match machinery moves to the core pack; the knobs work everywhere

Maintainer ruling: "move all this logic to either a core pack or to
C++, wherever it'll be a cleaner fit." Placement ruling: **the shared
`packs/core`** — C++ would re-home the rules in parity-pinned sim code
and invite a twin; the core pack is installed for every campaign and
keeps the one-implementation doctrine. Rulings C1–C7.

| # | Ruling |
|---|--------|
| C1 | **The match lib moves** — `stat_power`, the census, the D22 solver, squad sizing/spawn, the per-team `map_units` strip and the `FILL` execution leave `campaigns/modes/packs/modes.core/lib/` for `packs/core/lib/` (layout per implementer, one lib family). `modes.core`'s decide folds consume the SAME lib via pack-qualified `og.use` — if `og.use` cannot yet resolve across installed packs, it learns a qualified form (`og.use("core:…")`); no copy of any rule stays behind. |
| C2 | **A stage step for mode-less levels**: `MatchStage` (and the tick-side twin that serves un-staged worlds, the `mode_stage_init` precedent exactly) dispatches a `lineup` stage hook that `packs/core` registers, run only when NO mode owns the level. It applies the per-team strip and FILL squads on the staged world — preview == launch on classic campaigns for free. |
| C3 | **All-default is a proven no-op on classic worlds**: with `fill = FAIR` and `map_units = on` everywhere, a level whose active teams all carry authored troops strips nothing, solves nothing, spawns nothing, **writes no mode var and draws no RNG**. Pinned by a staged-world byte-identity test; `og_test_parity` must stay 257/257 untouched (parity scenarios are all-default). |
| C4 | **Classic levels never refuse.** The fewer-than-2-teams / fewer-than-2-fighters refusals are match-mode rules; a classic level with `FILL: NONE` on its enemy side simply fields fewer enemies, and the campaign's own win logic governs. Squads spawned on a classic level's team are ordinary walkers — remaining-foes counting, XP and drops unchanged. |
| C5 | **POWER everywhere**: the `lineup.power` pricing registers from the core pack itself (default = `stat_power` over the engine-derived stats), so the band prices rosters on every campaign; a campaign hook may still override. `MAP RULES` and the classic dim retire — the knobs are live on every campaign, every client. |
| C6 | **Gates**: new `packs/core` Lua enters the parity-canary pin discipline (append-only; run the pin check) and the Lua coverage denominator (every function exercised); the modes campaign's behaviour is byte-identical through the move (its staged matrices must not change). |
| C7 | **No format changes**: the knobs already ride the save/wire; protocol 16 / GTL 18 / snapshot 12 / replay 18 unchanged. |

## As built: the W6-B layer — the two engine seams (2026-08-27)

C1's cross-pack `og.use` and C2's mode-less stage dispatch. The library
itself still lives in `campaigns/modes/packs/modes.core/lib/` until W6-A
moves it; this layer is the pair of holes it moves through, and both are
inert until a pack uses them.

- **Qualified `og.use("<pack-id>:<module>")`.** The pack-relative form was
  the only one there was, so C1's escape hatch had to be built. Only the
  RESOLUTION ROOT moves: the memo key is `"<pack>/<name>"` whichever form
  asked for it, so `packs/core`'s own `og.use("mode_match")` and a campaign
  pack's `og.use("core:mode_match")` compile the chunk once between them and
  share one frozen export; the loader still swaps `current_pack` for the
  duration of a module's load, so a module pulled in across the boundary
  resolves ITS dependencies against ITS own pack, never the caller's. The
  load-time-only gate is checked BEFORE the name is parsed, so a
  dispatch-time qualified call gets the same bind-at-load-time instruction
  as the bare form; cycles and the failure latch are the same status table.
  A module name is a file stem, so the first `:` splits unambiguously and
  `":m"` / `"p:"` / `"p:a:b"` are refused by name. Two distinct errors, on
  purpose: `no installed pack 'x' with lib modules` (nothing answers to that
  id) vs. the pack-relative `no module 'y' in pack 'x'` — sending an author
  to a directory that does not exist is worse than not answering.
  Unqualified resolution, and every unqualified error string, are unchanged.
- **The stage step is a LEVEL HOOK, not a new registrar.**
  `on_lineup_stage(level)` joins `kLevelHookNames` (`LevelHook::LineupStage`,
  bit 8) and is registered through `og.register_level_hooks`, normally with
  the `-1` wildcard — `packs/core` arms every level of every campaign in one
  call. That shape was chosen over an `og.register_lineup_stage(fn)` binding
  because a level has exactly ONE stage step: `on_mode_init` when a mode
  claims the level, `on_lineup_stage` when none does. They are peers, so
  they belong in one vocabulary — same per-VM table (`level_hooks_ref`, held
  exactly like the mode hooks), same campaign-dispatch fence, same coverage
  labels, same per-level override seam, same generated stub row, and no
  second registrar to keep in step. `hooks::level_lineup_stage` reports "a
  hook RAN", not "it succeeded": a hook that errored half way through still
  fielded whatever it fielded, and the caller must assume the world changed.
- **Exactly two call sites, mirroring `mode_stage_init`'s two.**
  `GameWorld::run_lineup_stage_step()` is the one home for the rule (skip
  when `mode.active` — a mode-owned level already had its step). The stager
  calls it in `match_stage.cpp` step **8b**, immediately after step 8's
  `mode_stage_init`, so it covers a classic map AND a scripted map whose
  init refused and fell back to classic rules (R1's kept world is the
  authored one, so the step is coherent on it). The lazy arm is in
  `GameWorld::tick`'s classic `else` branch at `level_tick_count_ == 1`,
  beside `classic_strip_authored_troops` — the un-staged worlds' path.
- **The latch is the on_load latch's discipline, without a format change.**
  `claim_staged_lineup_stage()` / `consume_staged_lineup_stage_claim()`,
  keyed on the level id and never serialized: `adopt_staged_world` claims it
  beside `claim_level_load_latch()` (only when `!staged.mode.active`, i.e.
  only on the worlds step 8b covered — a claim left latched on a mode world
  would never be consumed and would suppress the NEXT level's step), and the
  adopted world's first tick consumes it instead of dispatching. Mirrors
  never tick, so the flag stays inert on them exactly as the on_load latch
  does (PR #195). Nothing new rides the wire: **protocol 16 / GTL 18 /
  snapshot 12 / replay 18 unchanged** (C7).
- **One behaviour ruling the tick arm forced.** The lazy arm runs AFTER this
  tick's foe scan, which is fused into the act loops, so a step that FIELDS
  fighters would let a kill-all map declare victory on the same tick the
  enemy squad walked on. When the step ran *and `oblist` grew*, tick 1 sets
  `level_done = 0` and the completion decision re-runs next tick against the
  world the step built. Scoped to "grew" on purpose: the strip half retires
  by marking dead, never by erasing, so it can only shrink the live
  population and a stale scan may stand for one more tick there exactly as
  it does for the classic strip. **A step that spawns nothing does not move
  the completion tick**, which is what keeps every all-default classic level
  (and every parity scenario) on its existing schedule once W6-A arms the
  hook.
- **C3, pinned.** With no hook registered the dispatch writes nothing and
  draws no RNG — `level_vm_state` early-outs before any state is touched —
  and `MatchStageTest.lineup_stage_no_op_hook_keeps_the_keyframe_identical`
  byte-compares a staged classic keyframe with and without an empty
  registered hook. `og_test_parity` is 257/257 untouched.
- **What W6-A needs from this layer.** Register with
  `og.register_level_hooks(-1, { on_lineup_stage = function(level) ... end })`
  from a `packs/core` script; reach the shared lib from the modes campaign
  with `og.use("core:<module>")`. Two things the hook must respect: it runs
  on a world that may already be mid-stage (the strip/fill order is its own),
  and `level_hook_kinds_for` reads a level's OWN registrations only, so the
  `-1` wildcard does NOT move the shipped-registration matrix in
  `test_modes_levels.cpp`.

## As built: the W6-A layer — the lib move and the classic semantics (2026-08-27)

C1/C3/C4/C6 whole, C5's Lua half. The rulings the build settled, recorded
so nobody re-derives them:

- **One lib file, `packs/core/lib/lineup.lua`.** The moved family — the f
  power metric (`stat_power`, `measured_base`, `walker_power`,
  `predicted_power`, the difficulty-tuple table), the censuses
  (`census_power`, the core half of `census_inputs`), the D22 solver and
  the packed plan, the FILL/MAP UNITS knob reads, `squad_room` /
  `fill_target`, squad sizing (`matched_families` — MATCHED.SIZE — and the
  hard-shape cap), the anchor-rotation placer + `ring_offset`, the one
  squad seam (`spawn_bots` / `spawn_matched_bots` / `add_squad_member`),
  the bot mark (`BOT_MARK_BIT` / `mark_bot`), the stock `BOT_SQUAD`, the
  per-team strip (`strip_authored_troops`) and the fact banking
  (`bank_lineup_facts`, `bank_match_target`) — is one module, bound as
  `og.use("core:lineup")`.
- **`mode_match` is the modes' FACADE over it, not a copy.** The
  mode-specific rules stay there in full (activation, `fills`, the mask
  helpers' consumers, `consume_markers`, `strip_inactive_teams`, the
  refusal digit, `announce_matched`, the flag census, scheduling/revive/foe
  helpers), and every moved name is a re-export of the core function
  object. Chosen over re-pointing every consumer because the shipped
  impls, `campaign_picker`'s knob rows, the staged-rules probes AND
  `tests/modes_pack_fixture.h`'s registration literal (owned by no wave
  this round, digest-pinned in the coverage manifest) all bind
  `match.<name>` — the facade keeps every one of them byte-identical, which
  is exactly C6's guarantee, proven by the untouched staged matrices going
  green unchanged. The qualified `og.use("core:lineup")` lives in six
  modes.core files: mode_match, mode_strip (whole-module re-export),
  mode_caps (the bot mark pair), mode_core (`iabs` — the solver took the
  one shared abs with it), mode_anchors (`BOT_SQUAD`) and campaign_picker
  (`lineup.power` now prices straight off the core lib, C5's Lua half).
- **Announces are a callback now.** `spawn_matched_bots`/`spawn_bots` take
  a trailing `announce(clamped)`; the mode facade's `spawn_bots` wrapper
  threads `announce_matched` in (same call point, same latch, byte-same),
  and the classic stage passes nothing — TEAMS MATCHED is match-mode
  vocabulary and never plays on a campaign level.
- **The classic stage** (`packs/core/scripts/lineup_stage.lua`, the `-1`
  wildcard on W6-B's `on_lineup_stage`; a campaign overrides per level id):
  - *Activation*: the teams the map authors — pre-strip units or a start
    marker — plus seats/roster. A FILL squad can NOT turn on an unauthored
    team (no anchors, nowhere to stand).
  - *Fill rule*: a squad walks on only where an authored team ends up
    EMPTY. Two ways there: the box stripped its units (any wheel value but
    NONE — FAIR included, the B4 "trade them for a solved squad" reading),
    or a ships-empty authored team (marker, no units) whose wheel names an
    explicit non-FAIR value. **FAIR on a ships-empty team is the map's own
    value — nothing spawns.** This is the ruling that keeps C3: classic
    maps ship player-start markers on teams they field nobody on, and a
    default that grew squads there would rewrite every campaign. A team
    with a deployed roster gets no squad — the allies gap is a match-mode
    rule and classic keeps out of it (C4's "the campaign's own win logic
    governs").
  - *Solver*: the one spawn seam, so B3 exactly — weakest human team's
    f-sum × the wheel's percent; no human power = the legacy difficulty
    formula, which stores no plan. Non-default classic stages therefore
    write MATCHED.PLAN and the slot-4 fact digits (preview == launch reads
    them); the all-default stage writes nothing, draws nothing, spawns
    nothing — pinned byte-identical on a staged REAL gladiator level with
    the dispatch proven live, and og_test_parity stays 257/257 with the
    wildcard armed on every scenario.
  - *Placement rule (documented per C1's ask)*: the squad anchors at the
    retired units' centroid, grid-snapped, when the team authored units,
    else at its start marker; each member takes the anchor tile, then the
    mode placer's blocked-cell discipline — the deterministic clockwise
    ring walk, radius 1..3 — then the blessed teleport draw. Same seed,
    same cells (pinned).
  - The strip retires by `set_dead(1)`, never by erasing — the lazy arm's
    oblist-growth completion guard depends on it, and the comment now
    stands on `retire` itself (W6-B's open note, closed).
- **C5's engine half was BLOCKED on a missing seam, recorded honestly**
  (closed by W6-D below).
  The campaign-book registrar is one-book-first-wins and a second
  `og.register_campaign_hooks` poisons the whole book ("no scripted picker
  will be served"), so packs/core cannot register the default
  `lineup.power` without killing every campaign that ships a book (modes,
  westlands, longseason, imaginations). The default IMPLEMENTATION ships
  (core `stat_power`), the modes campaign now registers it from the core
  lib, but gladiator's band still shows `--` until an engine wave adds the
  seam: a default-lineup slot in `world_scripts.cpp` that
  `campaign_fighter_power` / `campaign_lineup_registered` consult when the
  active book carries no `lineup` — registered by a shipped-pack-only path
  (the `on_lineup_stage` default/override shape, applied to the power
  hook). One function and two query fallbacks; no format change.
- **Mode-var co-tenancy on classic levels.** A non-default classic stage
  is the first CLASSIC writer of MATCHED.PLAN (slot 3) and the slot-4 fact
  digits. No shipped campaign level script touches mode vars today; a
  future classic level script claiming slots 3/4 would co-tenant with the
  stage's plan exactly as the modes co-tenant slot 4 — the header-band
  convention (slots 0-7 shared) now binds classic levels too.
- **C7 holds**: protocol 16 / GTL 18 / snapshot 12 / replay 18 unchanged —
  nothing new rides the wire; the stage writes only replicated mode vars
  that already existed.

## As built: the W6-C layer — the SDL half of C5 (2026-08-27)

The classic dim retired from the SDL surfaces, and VIEW LEVEL's classic
arm now renders the fills census. The rulings the build settled:

- **`lineup_knob_row_state` is host-only now.** The `!is_versus_campaign
  ⟹ Disabled` clause and the `MAP RULES` band census are gone from
  `menu_screen_specs.cpp`; the B4 dim (a team the map ships no units for)
  is the ONE dim left, and the MAP UNITS caption follows only that axis.
  The `change_lineup_fill` / `change_lineup_map_units` classic belts in
  `picker.cpp` went with them — the host's wheel turns on every campaign,
  every client. Joiner gating (Hidden + the host-denial popup) unchanged.
- **The staged census fold has one home.** The per-team
  company/troops/bots/generators fold that lived inline in
  `build_scenario_roster_report`'s mode arm is hoisted to
  `census_staged_teams` (picker_common.cpp, anonymous namespace) and now
  ALSO runs for a staged CLASSIC world: same team lines
  (`  GREEN TEAM  ACTIVE - MAP TROOPS (12)`, squad rows closing with
  their banked fill word), **headerless** — there is no mode to name and
  no match to count teams for, so the classic block starts at its first
  team line — and with **no activation clamp** (C4: classic levels never
  refuse; any team with anything standing gets its line). The formatter's
  census condition is `report.staged && report.mode_census`, with the
  `MATCH:` header emitted only under `is_versus`. Non-staged classic
  reports are unchanged (seats + roster rows only), and every versus
  branch (mode census, refusal sentence, count-only fallback) is
  byte-identical.
- **`ViewScenarioKey` needed nothing**: it has carried `fill` and
  `map_units` since §3.1 (picker_team_build.cpp:595-596, read at
  :701-702) — verified, no change.
- **Gladiator authors no marker-only side teams.** Every start marker on
  every gladiator level is team 0 (scanned across `scen/*.fss`), so the
  "empty authored team gets an explicit squad" arm of the classic rules
  has no gladiator stage; the classic path to a squad there is the
  box-trade (MAP UNITS off + a non-NONE wheel). The flows pin exactly
  that: scen 1's all-default `COMPANY (2)` / `MAP TROOPS (12)` block, the
  traded `BOT SQUAD (5) STRONG` row replacing the troops, and the
  FILL: NONE + box-off shape where GREEN's line (and its roster rows)
  drop entirely. **A classic solved squad reads `BOT SQUAD`, never
  `MATCHED BOTS`**: the row's noun tracks the MATCHED.SIZE latch, which
  only the match-mode activation fold banks — MATCHED is match-mode
  vocabulary (the W6-A ruling), so the classic solve stores its plan and
  banks its fill fact but the pane's noun stays the plain squad, sized by
  the full stock table (SIZE unlatched = no headcount truncation).
- **Left deliberately for W6-G**: the terminal MAP RULES twins —
  `kTerminalLineupMapRulesMark` / `kTerminalLineupMapRulesLine`
  (terminal_menu_model.h), the census precedence in
  `terminal_menu_model.cpp`, their pins in `test_platform_headless.cpp`,
  `tests/curses/test_curses_picker_client.cpp` and
  `scripts/test_text_picker_interactive.sh` — one wave owns the terminal
  surfaces, so the SDL wave did not half-edit them.

## As built: the W6-D layer — the default-lineup seam closes C5 (2026-08-27)

The gap W6-A named. `packs/core` now prices the LINEUP bands on every
campaign, gladiator included, and the rulings the build settled:

- **A registrar of its own, not a fifth campaign-book key.**
  `og.register_default_lineup({ power = fn })`, load-time only, held in a
  per-VM slot (`VmState::default_lineup_power_ref`) that is NOT the
  campaign book's registry table. The shape was forced by the blocker
  itself: `og.register_campaign_hooks` is one-campaign-one-book and a
  second call poisons the whole registration, so a default riding that
  registrar would have killed the picker of every campaign that ships a
  book. The two halves cannot reach each other in either direction — a
  campaign that registers twice keeps its bands priced (the conflict
  nulls `campaign_vm_state`, and the default slot is read through
  `default_lineup_vm_state`, which asks for no book at all), and a pack
  that registers a default twice raises nothing and conflicts nothing
  (last registration wins, the `og.register_level_hooks` wildcard
  precedent).
- **The TABLE is the campaign book's `lineup` table, spelled identically.**
  A pack's default and a campaign's override differ only in which
  registrar they are handed to — same `{ power = fn }`, same
  `og.CampaignLineup` stub class, same `LineupPowerRow` input, same
  fence, same "a price the engine cannot read is no price" refusals. The
  one-key typo pass runs before the declaration bow-out, the
  `og.register_campaign_hooks` discipline.
- **Two query fallbacks, and the book still wins.**
  `campaign_lineup_registered` answers true for a book `lineup` OR a
  registered default; `campaign_fighter_power` pushes the book's
  `lineup.power` first and falls through to the default slot when the
  book registered none (or is conflicted, or is absent). Errors on either
  side answer false — the band shows `--` — and the recorded refusal now
  names which pricer refused (`campaign:lineup_power` vs
  `default:lineup_power`), because an author reading the log has to be
  told.
- **`packs/core/scripts/lineup_power.lua`** is the whole pack half: one
  `og.use("lineup")` and `stat_power` over the engine-derived row. A NEW
  file, which is what keeps the parity canary's line+text pins intact
  (`og_test_parity` 257/257 untouched; the registration draws no RNG and
  writes no state).
- **The modes campaign's numbers did not move.** Its book registers the
  same core `stat_power`, so the pinned row (SOLDIER Lv 2, 100/20/4/10/3/6)
  still prices 661 — `ModesBookTest.lineup_hook_registers_and_prices_with_stat_power`
  unchanged, and `LineupUi.modes_power_pin_survives_the_shipped_default`
  asserts that number on BOTH mounts: one metric, one currency.
- **What the band shows now.** `LineupUi.classic_campaign_knobs_are_live`
  asserts a NUMERIC `POWER` on gladiator's deployed company (was `--`),
  and the regenerated `lineup_gladiator_live_knobs` capture shows it.
  `POWER --` survives on TEAM 2-4 there for the right reason: those bands
  field nobody, so there is nothing to price — that is the empty-band
  reading, never the missing-metric one.

## C8 (2026-08-27): the default FILL resolves to NONE on unauthored teams

Maintainer ruling: "default to FILL: NONE for teams that lack any
units (e.g. the two unauthored teams of gladiator scen 1)." The stored
scale is unchanged (`0` = default); the DEFAULT now resolves per team
at stage time: **FAIR where the team has any authored presence on the
level (map units, generators, start markers or anchors) or any
seat/fighter; NONE where it has none.** The band label, the terminal
rows and the banked facts all render the resolved value (`FILL: NONE`
on gladiator scen 1's empty sides), so the page never advertises a
fill the placement rule would refuse anyway. An explicit wheel value
is stored as itself, exactly as before; cycling writes explicit values
and cannot return to the resolved default (knob precedent). Modes
maps' empty *authored* teams keep the FAIR default — only teams the
map does not author flip.

## As built: C8 — one resolver, one home (2026-08-27)

The rulings the build settled:

- **The ONE rule is `lineup.resolved_fill(knob, row)`** in
  `packs/core/lib/lineup.lua`, over the `team_present` fold (any of
  units / generators / markers / anchors / roster / seats > 0). An
  explicit wheel value returns unchanged — junk codes included: the
  degrade-to-FAIR clamp predates C8 and stays an *explicit* fair, so
  only a stored 0 ever resolves. The classic stage executes the
  resolved value (`lineup_stage.lua`), and every band surface renders
  it through a QUERY of the same function — no menu re-derives it.
- **The C3 byte no-op is upheld, and it bounds the banking.** The
  resolution maps the default onto FAIR or NONE only, neither of which
  is a "touch", so the all-default stage still returns on the fast path
  having written nothing, drawn nothing, spawned nothing
  (`all_default_stage_is_a_byte_noop_on_gladiator`; parity 257/257
  untouched). Consequently the resolved-NONE **fact** banks only on a
  stage something else already made run: on any TOUCHED classic stage,
  every team whose stored default resolved NONE banks fact code 2 —
  the one deliberate exception to R4's spawned-only rule, because "no
  squad, by resolution" *is* what was applied. Explicit NONE stays
  unbanked. VIEW LEVEL is unaffected either way: a team that banks a
  resolved NONE has nothing standing, so the census renders no row for
  it (verified by the staged-report flows).
- **The band query rides the W6-D registrar.** The lineup table grew
  its second member: `og.register_default_lineup({ power, default_fill })`
  and the campaign book's `lineup.default_fill` — same table, spelled
  identically, book wins, refusals name `campaign:`/`default:
  lineup_default_fill`. Registration is whole-table last-wins: a later
  default that omits `default_fill` clears the resolver. The engine
  query is `og::script::hooks::campaign_lineup_resolved_fill(stored,
  LineupResolveRow, out)`; any refusal (no packs, error, non-integer,
  off-wheel code) makes the surface fall back to the STORED value —
  the honest fallback, rendering exactly as pre-C8.
- **Presence is censused, never twinned.** The C++ side only *gathers*
  counts (`census_lineup_presence` over the loaded picker level:
  units, generators, markers with dead ones included — the anchor
  scan's own population, so the anchors column stays 0 — plus the
  band's seats/fighters) and hands them to the resolver. SDL reads the
  loaded picker world (level-reload-guard currency); the terminal
  model takes a `presence` span. The two live terminal clients load no
  level in their pickers and pass none, so their cells honestly render
  the stored code — the documented no-census fallback.
- **Seat vs stage asymmetry, accepted.** The Lua stage cannot see
  seats, so its presence row ends at the roster; a seat parked on an
  otherwise bare team flips the BAND to FAIR while a stage taken at
  that instant would bank NONE for the team. GO cannot launch that
  shape (M4: one deployed fighter per seat), so the launched world
  never disagrees with its band.
- **Both label surfaces resolve.** The SDL faces re-derive per frame
  through `picker_lineup_resolved_fills()` (bands with an empty power
  fn — no pricing pcall in the rewire), and the click callback writes
  the resolved label too, so a wheel that lands back on 0 immediately
  reads its resolved value. The query is memoized beside the pricing
  memo and cleared by the same `lineup_power_cache_clear`, keyed on
  the full counts row (a book's resolver may weigh counts, so no
  boolean folding in the key).

## D-series (2026-08-27): the playtest corrections — explicit FAIR exists, explicit FILL always fields

Maintainer playtest on gladiator scen 1 found three failures; rulings
D1–D4 supersede the matching earlier sentences (notably W6-A's
"a FILL squad may not turn on an unauthored team" and W5-A's
"troops-only teams get no squad").

| # | Ruling |
|---|--------|
| D1 | **Explicit FAIR gets its own code.** Scale: `0 = DEFAULT` (resolves per C8), explicit `1 = NONE, 2 = WEAK, 3 = FAIR, 4 = STRONG, 5 = BRUTAL`; sanitize `[0,5]`. The wheel is always the five explicit values in order `NONE → WEAK → FAIR → STRONG → BRUTAL` (wrap); a band on the DEFAULT enters the wheel at its resolved value's position. C8's swallowed-FAIR wheel (`NONE, WEAK, NONE, STRONG, BRUTAL`) was the direct consequence of FAIR sharing code 0 with the default. |
| D2 | **An explicit FILL on an unauthored team FIELDS a squad** and turns the team on (hostile to all, ordinary walkers). Site-less placement: deterministic from the match seed — prefer the walkable region farthest from every existing team's centroid, and never land adjacent to a hostile (the spawn-safety rule). The C8 default still resolves to NONE there; only an explicit choice fields. |
| D3 | **FILL on a troops-occupied team fields a squad beside the troops.** Occupancy for the allies rule means HUMAN occupancy only; a troops-only team solves like an empty one (weakest human × m). With a level-50 hero on team 1, `FILL: BRUTAL` on the elves' team must produce a squad that actually threatens: target = weakest-human f-sum × 1.5, pinned within solver tolerance. Hard-shape caps still apply on modes maps. |
| D4 | **The testing bar rises to outcomes.** Every FILL behavior gets a test through the REAL UI path (injector sets the knob → launch → count the team's spawned walkers and assert the solved squad's f-sum tracks the multiplier monotonically WEAK < FAIR < STRONG < BRUTAL against a fixed roster including a high-level hero); the wheel gets full-cycle label-sequence pins on authored AND unauthored teams; a restage-after-knob-change test proves a knob set in LINEUP reaches the world the launch adopts. Label-only pins no longer count as covering a FILL behavior. |

## As built: D1's scale and wheel, the C++ half (2026-08-27)

The engine side of D1. The Lua half (the pack constants, the resolver's
own arithmetic and the banked fact) renumbers to meet it.

- **The scale is `0 = DEFAULT` and five EXPLICIT codes**, in wheel order:
  `og::sim::kFillDefault = 0`, `kFillNone = 1`, `kFillWeak = 2`,
  `kFillFair = 3`, `kFillStrong = 4`, `kFillBrutal = 5`, `kMaxFill = 5`,
  `clamp_fill` over `[0, 5]`. `kFillFair` kept its NAME and lost its old
  double duty; the DEFAULT that used to hide behind it now has a spelling
  of its own, which is the whole of D1. Display order and storage order
  are finally the same order, so the wheel table and the scale can no
  longer drift apart.
- **The wheel holds the five explicit codes and nothing else.**
  `cycle_lineup_fill(current, resolved, dir)` — the new middle argument is
  the value the band is SHOWING. An explicit `current` enters at its own
  slot and ignores it; a stored DEFAULT enters at `resolved`'s slot,
  because a wheel that steps from a position the player cannot see is what
  made gladiator's empty sides skip WEAK on the first click. Junk on both
  counts enters at FAIR, where the clamp would have put it. Every step
  returns an explicit code: a turned knob is a choice and stays one.
- **The click callback reads the resolution BEFORE the write and renders
  the STORED code after it** (`change_lineup_fill`). Re-resolving the
  result is what painted the wheel's FAIR stop with the word NONE; an
  explicit stop is never run back through a many-to-one map. A stored
  default still renders its resolution per C8 — but a click can no longer
  leave one behind. The two terminal clients census no level, so they pass
  the stored code as its own resolution, which is the documented
  no-census fallback and leaves their observed wheel unmoved.
- **`format_lineup_fill_label` is unchanged in spelling**, and the rule it
  obeys is now pinned: `label(explicit)` is that value's own word — the
  five are distinct, which is what lets a player read the wheel's position
  off the face — and `label(stored DEFAULT)` is the word of whatever it
  resolved to. `lineup_fill_name(0)` still reads FAIR, for the callers
  with nothing to resolve against.
- **The banked fact code lost its +1 bias.** A code in the shared slot IS
  the explicit fill it applied, `1..5`, and `0` — the digit pair an
  unbanked team leaves — is the one value no fill can collide with, which
  is exactly why the bias existed and exactly why it no longer needs to.
  A team whose stored DEFAULT resolved banks the code it resolved TO.
- **A resolver may only answer an explicit code.** The
  `lineup.default_fill` seam accepts `1..5` and refuses the DEFAULT along
  with everything off the wheel: a resolver that hands back the code it
  was asked to resolve has resolved nothing, and the caller's fallback
  (the stored value) says so more honestly.
- **No version bump.** The knobs, the GTL v18 save field and the wire
  slots that carry them are all branch-local — nothing outside this branch
  has ever written a fill code, so there is no old byte to reinterpret.

## As built: W7-A — the Lua half of the D-series (2026-08-27)

The pack side of D1 and the whole of D2/D3, meeting the C++ scale above.

- **One scale, renumbered** (`packs/core/lib/lineup.lua`): `FILL_DEFAULT
  = 0`, then the five explicit wheel codes `FILL_NONE 1, FILL_WEAK 2,
  FILL_FAIR 3, FILL_STRONG 4, FILL_BRUTAL 5`. `FILL_PERCENT` is keyed by
  the explicit codes alone (`{[1]=0, [2]=75, [3]=100, [4]=125,
  [5]=150}`) — the DEFAULT deliberately has no multiplier row, so
  `applied_fill(0)` degrades like any crafted value and a stored 0 can
  never *quietly* act as a fill. `resolved_fill` answers the explicit
  FAIR / explicit NONE, never the default.
- **The banked fact IS the applied code** (D1's Lua half):
  `FACT_FILL_BIAS` and `lineup_fact` are deleted; `bank_lineup_facts`
  banks the code raw, `mode_match`'s preview rows carry
  `lineup.applied_fill(knob)` directly, and a team whose stored default
  resolved banks the code it resolved TO (C8's resolved-NONE banks 1).
- **Every seam reads a RESOLVED code, and the resolution's presence row
  is the deciding fold's own domain** — three spellings of one rule,
  each documented where it lives:
  * the classic stage resolves over its census `presence_row` and hands
    the result INTO `lineup.spawn_bots` (new optional 7th arg
    `resolved`);
  * `mode_match.fills` resolves inside the active arm with the
    activation itself as the presence row (`{units = 1, roster}`): a
    team in the active mask is authored by the mode's own domain (flags,
    anchors, foundries — the mask already folded it), which is why CTF's
    flag-only sides keep their default-FAIR backfill;
  * `mode_fighters.band_knob(target, fighters)` resolves over the
    manifest row's fighter target as the band's authored units (a
    bot-only band still resolves FAIR) plus the deployed roster;
  * `spawn_bots`' own nil fallback resolves over `{units = 1}` — the
    seam is only ever invoked for a team its caller's fold already ruled
    fields a squad, so the implied row is authored. The W7-B note about
    the seam re-reading the raw knob is closed by this.
- **D3, the classic gate** (`packs/core/scripts/lineup_stage.lua`,
  `classic_wants_squad(row, raw, knob, fielded)`): an EXPLICIT wheel
  value always fields unless it is NONE or a roster fighter stands
  (allies stay a match-mode rule); the stored DEFAULT fields only the
  box-trade (units authored, box off) — **the default never adds a
  squad beside anything**; only an explicit choice puts bots beside
  troops, and a default on a ships-empty or unauthored team is the
  map's own value. The troops-fielded anchor is the standing troops'
  centroid, so the D3 squad walks on beside them; the solve target is
  unchanged (troops carry no guy, so the empty-team arm prices
  weakest-human × m, exactly the probe's prediction).
- **D2, site-less placement** (`classic_anchor` answers (-1,-1);
  `classic_centroids` / `classic_hostiles` / `classic_reach` /
  `farthest_open_spot` / the extended `classic_place`): deterministic
  from the match seed — a row-major two-tile-lattice scan keeps the
  first walkable, non-hostile-adjacent cell maximising the minimum
  squared distance to every existing team's centroid (existing = any
  units/roster/markers, centroids measured where those troops stand on
  the STAGED world, grid-snapped). Members ring-walk that anchor under
  the same spawn-safety rule (never adjacent to a hostile's tile or its
  eight neighbours); on exhaustion the tail is the blessed teleport
  scatter with a BOUNDED safety re-probe (eight seeded draws, first
  safe landing wins, the last landing stands — a placed squad beats a
  refusal, C4). The scan's reach is the authored extent plus a six-tile
  margin: the engine exposes no map dimensions to scripts, and
  `spawn_spot_clear` refuses every off-map cell, so the estimate costs
  probes, not correctness.
- **D4, the Lua half of the outcome tests**
  (`tests/unit/test_staged_rules.cpp`, real staged gladiator worlds
  through MatchStage, level-50 thief fixture): explicit FAIR on
  unauthored ground fields a safe five-squad with seed-deterministic
  cells; BRUTAL beside the twelve elves lands f-sum within ±6% of
  weakest-human × 1.5; the ladder WEAK < FAIR < STRONG < BRUTAL is
  strictly monotone on BOTH team shapes; the default stays squadless
  beside troops and empty on unauthored ground; the all-default stage
  remains a proven byte no-op (C3) and og_test_parity stays 257/257.
  All four new tests were run red against the pre-W7-A staged pack
  (0/4 pass) before going green.

## As built: W7-C — the face reads the choice, and D4's outcome gate (2026-08-27)

The SDL half of D1's label rule, and the end-to-end half of D4.

- **The per-frame face resolves only a stored DEFAULT.** `lineup_menu_rewire`
  renders `format_lineup_fill_label(resolved_fills[t])` for a band still
  holding `kFillDefault` and the STORED code's own word everywhere else —
  the same rule the click callback already obeyed. Between the two there is
  no third site: nothing else in the SDL page calls `cycle_lineup_fill` or
  `format_lineup_fill_label`, so an explicit stop can no longer be run back
  through the many-to-one resolver on any path.
- **A test can census the world a launch actually adopted.** Under
  `TESTING` the game loop compiles its `redraw()` out, so the presenter
  handshake the menu captures use has nothing to freeze during a level, and
  the main-thread task queue only pumps inside `run_menu_screen`. The seam
  is a one-shot, TESTING-only observer (`picker_testing_observe_next_game_frame`,
  fired from `picker_testing_mark_frame_advance()`) — the one point where
  the sim is provably between ticks.
- **D4's measured anchors, gladiator scen 1, one level-50 hero at
  60/60/60/60 with armour 40.** The reference (weakest human f-sum) is
  **131597**. `FILL: BRUTAL` on the elves' band fields 12 + 5 livings whose
  squad f-sum is **191085** against a target of 197395 (−3.2%, inside the
  ±12% solver band); `FILL: FAIR` on the unauthored band fields **5**
  livings, **0** guys, f-sum **129358**, and every one of them counts in the
  hero's own `remaining_foes`. Re-entering the page and walking the elves
  down to WEAK restages (trace `stage: restaged gen=`) and the second launch
  fields **104232** against 98697 (+5.6%) — strictly under BRUTAL's 191085,
  which is D4's ladder where a player can feel it. The stat block is SET,
  not levelled: the level-up ladder prices a level-50 slot at f = 6,989,687,
  an order of magnitude past the solver's L9 ceiling, where every wheel stop
  clamps to the same squad and the multiplier stops being observable at all.
- **Known overflow, left for the report formatter's owner.** D3's row
  `"  GREEN TEAM  ACTIVE - MAP TROOPS+BOTS (12+5) BRUTAL"` is 51 characters
  against `kMaxReportLine = 48`. The formatter spends the separator space
  first, as B7 designed, and then the clip takes the word's tail: the pane
  reads `(12+5)BRU`. B7's budget note reasoned about
  `"COMPANY+BOTS (3+2) BRUTAL"` (49, one over, glue enough) and the longer
  `MAP TROOPS+BOTS` noun is past what gluing can save. B7 says a clipped
  word is a different word, so this is a real violation of the rule the code
  was written to keep; the choice between widening the budget and shortening
  the noun is the maintainer's, and the test pins only what is true today
  (the surviving letters must be BRUTAL's own prefix).

## As built: W7-G — the terminals census the world they would launch (2026-08-27)

- **`TerminalLineupInputs::presence` is fed at last**, from the world the
  clients' own VIEW LEVEL stages. The shared seam is
  `og::ui::census_staged_lineup_presence(MatchStage&, const SaveData&,
  difficulty, match_seed, out)`: it builds `MatchStageInputs` exactly as
  `view_scenario()` does, runs `observe_inputs` + `ensure_current`
  synchronously — a page redraw cannot wait out `kStageDebounceMs` — and
  answers `census_lineup_presence(*stage.world())`.
- **The three no-world shapes keep the documented stored-code fallback**:
  an unmounted campaign, a stage that is not `Staged`, and a stage that
  fell back to a level other than `save.scen_num`. The seam returns false
  and leaves `out` untouched, and the callers pass an EMPTY span.
- **One stage per page loop** (`lineup_screen()` in text, `lineup_flow()` in
  curses), so an untouched page is free and a turned knob restages exactly
  once. Both clients now pass `model.bands[team].resolved_fill` as
  `cycle_lineup_fill`'s middle argument, so a band on the DEFAULT enters
  the wheel where the face reads on the terminals as on SDL.
- **Agreement at rest is pinned** on gladiator scen 1 in all three clients:
  `TEAM 1  FILL: FAIR`, `TEAM 2  FILL: FAIR`, `TEAM 3  FILL: NONE`,
  `TEAM 4  FILL: NONE` — in text, in curses, and in the interactive shell
  drive, which now refuses unless both NONE rows appear.
- **Two censuses, by design, from different worlds.** SDL censuses the
  loaded PICKER world; the terminals census the STAGED one. They agree on
  gladiator scen 1 and on any team whose authored population the stage does
  not move — but a stage step that spawns ONTO a team would make the
  terminal band see presence the SDL band does not. Worth revisiting if a
  mode ever does that before the page paints.
- **Cost note.** Entering LINEUP on a mounted campaign now stages a real
  world (one level load plus a per-world Lua VM), and each knob turn
  restages once. Twelve presses cost 215 ms on gladiator scen 1; a heavy
  level will make the page's first paint cost what VIEW LEVEL costs.

# Amendment 4 (2026-08-28): FILL: NONE is the default on every map

Maintainer ruling: "make FILL: NONE the default for all maps." Rulings
E1–E5 supersede C8 and the D1 scale.

| # | Ruling |
|--|--|
| E1 | **Scale**: `0 = NONE` (the default), `1 = WEAK`, `2 = FAIR`, `3 = STRONG`, `4 = BRUTAL`; sanitize `[0,4]`. There is no DEFAULT/explicit distinction any more: stored 0 *is* NONE, and the wheel from a fresh band enters at NONE's slot (one click = WEAK, two = FAIR). |
| E2 | **The per-team default resolver retires**: `resolved_fill`, the `lineup.default_fill` registrar member and campaign-book override, `campaign_lineup_resolved_fill`, `LineupResolveRow`, `LineupTeamBand::resolved_fill` and the presence-to-resolver plumbing are deleted (no dead seams). The presence/map-unit census stays only for the `MAP UNITS` dim and the band diagnostics. |
| E3 | **Semantics**: no squad fields anywhere unless the host sets FILL — modes maps' empty authored teams included (the FAIR-by-default "matched teams" behaviour is gone; a solo player on a two-team mode map sees the honest `MATCH WILL NOT START: FEWER THAN 2 TEAMS` in VIEW LEVEL until a wheel is turned). Explicit fills behave exactly per D2/D3. All-default is a no-op on every map, trivially. |
| E4 | **Facts**: a banked fill code is the stored code of a squad that spawned (1..4); NONE never spawns and never banks; 0 = nothing banked, unambiguous. |
| E5 | **Pins move, not weaken**: the modes staged matrices' FAIR-by-default rows become NONE-by-default rows plus explicit-FAIR rows that preserve the old expectations; every label/wheel/fact pin follows the new scale; captures regenerate (every band reads `FILL: NONE` at rest). |

## E1/E2 as built (W8-B, the C++ half)

Two rulings E2 left to the implementation, recorded here because a later
reader will otherwise go looking for the seams:

- **The presence census went with the resolver.** E2 kept "the
  presence/map-unit census... for the `MAP UNITS` dim and the band
  diagnostics", and as built neither reads it: the dim is
  `census_lineup_map_units` (a separate walk over the same two worlds) and
  the diagnostics are seats-vs-fighters arithmetic. `LineupTeamPresence`,
  `census_lineup_presence`, `og::ui::lineup_resolved_fill` and its memo
  therefore go too, along with the `presence` span on
  `build_lineup_bands`, `LineupTeamBand::resolved_fill`,
  `picker_lineup_team_presence` and `picker_lineup_resolved_fills`. The
  terminals' `census_staged_lineup_presence` is now
  `census_staged_lineup_map_units` — same stage, same three inputs, the
  one column anybody still reads.
- **Junk reads NONE.** `lineup_fill_name` answered FAIR for anything off
  the wheel while FAIR was the resolver's presence arm. With the resolver
  gone the honest word for an unrecognised code is NONE: it is the storage
  default, it is where `clamp_fill` lands a negative, and it promises no
  squad the level will not field. The wheel enters there for the same
  reason.

## As built: W8-A — the Lua half of Amendment 4 (E1/E3/E4/E5)

- **The scale is renumbered in `packs/core/lib/lineup.lua`**: `FILL_NONE =
  0` (the stored default), `FILL_WEAK = 1`, `FILL_FAIR = 2`, `FILL_STRONG
  = 3`, `FILL_BRUTAL = 4`; `FILL_DEFAULT` is deleted and `FILL_PERCENT`
  carries rows for 1..4 only. `squad_off(knob)` answers true for any code
  without a multiplier row — NONE and junk alike (E2's "junk reads NONE",
  applied on the spawn side: a code off the wheel fields nothing).
  `fill_percent` dropped its degrade-to-FAIR arm: every apply seam gates
  on `squad_off` first, so the row exists by construction.
- **The resolver seams are gone with their tests**: `resolved_fill`,
  `team_present` and `applied_fill` (an identity once the degrade arm
  died) left `lineup.lua` and both export tables; `presence_row` and the
  resolved-NONE fact banking left `lineup_stage.lua`; the raw/resolved
  knob split and `FILL_DEFAULT` left `mode_match.lua`'s fills rows (the
  troops arm's explicit gate is now simply "not squad_off"); mode_fighters'
  `band_knob` reads `og.match_setting("fill_1")` bare. `spawn_bots` lost
  its seventh (`resolved`) parameter and reads the stored knob itself —
  the classic stage no longer pre-resolves anything.
- **E4 as built**: `bank_lineup_facts` receives the stored code of a squad
  that spawned (1..4), NONE never reaches it, and the classic stage's
  resolved-NONE banking exception is deleted — an untouched world banks
  nothing, so slot 4 stays 0 wherever no wheel was turned.
- **E3/E5 pins moved, not weakened**: the FAIR-by-default fixture rows
  across test_staged_rules / test_staged_report / test_modes_{ctf,tdm,
  soccer,basketball,ffa,mutant,items} became explicit-FAIR rows carrying
  the old matched-solver numbers, plus NONE-by-default rows (nothing
  spawned, nothing banked, empty teams narrowed out). The E3 honesty pin
  is StagedRules.solo_human_all_none_refuses_and_explicit_fair_plays (TDM:
  all-NONE refuses with the mode's own fewer-than-two-teams sentence, FILL:
  FAIR on the other team plays); the staged matrix now runs every shape on
  both wheel bases (base_fill NONE and FAIR) and gained the mode hard-shape
  cap so the harness prices the troops-arm squads the court actually
  fields. The all-default classic byte-noop pin (C3) stands, now meaning
  "all-NONE".
- **Band rules follow E1**: FfaRig turns the band's one wheel to FAIR in
  its constructor (the suites were written against the FAIR default); the
  NONE tests set the knob back to 0 and now pin the DEFAULT band shape —
  an untouched band keeps only its deployed fighters, and one fighter
  under all-NONE refuses with FEWER THAN 2 FIGHTERS before any world
  write.

## As built: the merged wave (W8-C, W8-G, and the integration pass)

The two halves that landed without a doc append, and the skew the merge
itself turned up:

- **The terminals' two wheel drives collapsed into one vector** (W8-G).
  `text_picker_lineup_wheel_cycles_both_bands_fully` and
  `CursesPickerClient.lineup_wheel_cycles_both_bands_fully` used to carry
  an authored vector starting at STRONG and an unauthored one starting at
  WEAK, because the two bands entered the wheel at different slots. Under
  E1 both enter at NONE and walk WEAK, FAIR, STRONG, BRUTAL, NONE, WEAK —
  one shared expectation asserted against both bands, so a "two entry
  points" regression shows up as a diff in either arm. The SDL twin
  (`LineupUi.fill_wheel_full_cycle_on_authored_and_unauthored_bands`) goes
  further and asserts the two walks are EQUAL, which is the actual E1
  claim.
- **The E3 flow's map is scen 500** (W8-C). `CTF: FIRST BLOOD` is the CTF
  map that authors no units of its own, so a solo company leaves exactly
  one team standing and the refusal is the map's own honest answer rather
  than an artifact of stripping something.
- **A refusal replaces the census rows, it does not annotate them.** The
  report emits `MATCH WILL NOT START: FEWER THAN 2 TEAMS` *instead of* the
  per-team `ACTIVE - …` lines, so under E3 an at-rest mode map has no
  previewed per-team counts to read at all. Every preview-vs-launch oracle
  had to learn that shape: the pane refuses while the launched world still
  contains the company (classic rules over the refused state), and the two
  halves agree again the moment a wheel is turned. The seat block is a
  property of the save, not of the match, so a refusal never hides it.
- **A knob-less client cannot reach a wheel-fed mode.** `--protocol` (the
  CLI shape) carries no match knobs by design — there is no picker save
  behind it — so under E3 it can only activate a mode whose level ships
  its own two sides. Onslaught still does; CTF and soccer no longer do
  from that entry point. `PlatformHeadless.text_protocol_serializes_shipped_mode_state`
  pins both halves: the active mode block on onslaught 801, and the honest
  inactive block on the CTF map it used to drive.
