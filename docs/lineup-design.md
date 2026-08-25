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
[BACK] [FIGHTERS] [SPLIT EVEN] [SPLIT FAIR] [ALL TO 1]
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
  when fighters have no seat (M3). Informational; GO keeps its refusal.
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
- `BOTS: NONE` on a team that still has a seat or a deployed fighter is
  refused with a toast (`TEAM n HAS PLAYERS` / `TEAM n HAS FIGHTERS`);
  LINEUP never reseats anyone.
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
- **FAIR on an occupied team** (allies): target =
  `max(f-sum of every other team) − f-sum of this team's humans`; the
  empty-team case keeps the D11 mean. Ruling from the maintainer
  conversation of 2026-08-25.

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
level appends `LV k`, within the 48-char budget. The staged-rules matrix
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
- **ALL TO 1** — every deployed character to the lowest-numbered team
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

| Mode | Rows shown |
|------|-----------|
| Idle | as today: ROOM CODE, ACTIVE GAMES list, DIRECT (LAN) fields, HOST / JOIN |
| Hosting | header `PLAYERS` over the five list rows repurposed as **machine rows**, `ROOM <code>` line, **DISCONNECT** (new ordinal appended after the room rows; drawn in JOIN's rect), HOST/JOIN/LAN hidden |
| Joined | same, rows read-only, DISCONNECT = leave |

**Machine rows** (`format_networking_machine_row`, 39-char budget, one
per `machine_id` from `picker_lobby_players()`): `M1 HOSTNAME  P1 P2
COMPANY  READY` shape, `(HOST)` marker, `(YOU)` marker. Host clicking a
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
- **SDL flows** (`og_test_basecamp` / `og_test_matchup`, never
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
