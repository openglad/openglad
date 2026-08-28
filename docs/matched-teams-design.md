# Matched Teams — Design Specification ("Teams: Match")

Branch `feature/mode-basketball` @ 429ec46e (PR #190 baseline — basketball is
baseline code here). User intent, verbatim: a "TEAMS:" lobby option that fills
a versus map's EMPTY teams with bot squads of COMPARABLE POWER to the human
team(s); map team capacity comes from what spawn points exist (the existing
authored-anchor contract).

All line anchors below were verified by the fact scouts at this revision.
**Line numbers drift as work packages land — every implementer must re-verify
an anchor with `grep -n` immediately before editing, and run
`python3 scripts/parity/check_mutation_pins.py` before every commit that
touches any file under `src/gameplay/` or `src/resources/`.**

---

> **AMENDED 2026-08-09 (branch @ 30fb2664):** the CONTROL moved from the Teams
> cycler to the Troops cycler — see **"Amendment: control moved to TROOPS"**
> (D25-D33) at the end of this document. Rows D1-D8 below are kept as history
> and are individually marked. The machinery (census, solver, spawn seam,
> announces — PARTs III-IV) stands unchanged.

> **AMENDED 2026-08-10 (branch @ cf02e0f6):** matched squads now MATCH THE
> HUMAN HEADCOUNT — solo = 1v1, duo = 2v2, level-solved within that size —
> see **"Amendment: matched squads match the human headcount"** (D34-D40) at
> the end. D12 and E13 are superseded (marked in place); D26's one-delta
> invariant is amended by D39.

## 0. Decision record (conflict resolutions — binding)

| # | Decision | Ruling & rationale |
|---|----------|--------------------|
| D1 | **[SUPERSEDED by D25]** ~~Control = one new VALUE on the existing Teams cycler; no new row~~ | The user's phrasing ("a TEAMS: lobby option") names the existing cycler, and MATCHED *is* a team-count policy ("all authored teams, empties filled to match"), not an orthogonal knob. Cost: ~3 source files + ~6 pin sites vs ~30 sites, GTL v15, protocol v13, 5 wire-byte pins, ButtonAction 103, nav rewire, and a MATCHUP screen with no free 80px slot. Folding it into the cycler also makes the Teams-vs-BotPower combination space impossible to misconfigure. |
| D2 | **[SUPERSEDED by D27]** ~~Sentinel = `kTeamCountMatched = 5`~~, positive, defined in `include/openglad/gameplay/lobby_state.h` next to the `:193` field | A negative sentinel is silently destroyed today: `sanitize_settings` (`src/gameplay/lobby_server.cpp:70-78`) snaps every `<= 0` to `0`, and the SOLO picker round-trips its own settings through `LocalPickerLobbyClient`. `1` degrades on old builds to `2` (least Auto-like); `5` degrades to `4` (old sanitize) or Auto (old cycler wrap) — both close to MATCHED's real semantics — and reads naturally as "the value after 4". Interface may include gameplay headers, so the constant is visible to the picker. This overrides the power draft's "any value <= 0 = OFF" phrasing: the Lua-side check is `raw == 5`, not a sign test. |
| D3 | **[SUPERSEDED by D28]** ~~Label = `"Teams: Match"`~~ (12 chars, 72px) | Fits the 12-char pin (`tests/unit/test_picker_common.cpp:1330`) and the 80px face exactly; names the feature. `"Teams: Even"` (11) is the fallback if headroom is ever needed. One shared formatter serves all three clients — this is the only label string. |
| D4 | **[SUPERSEDED by D28]** ~~Cycle order: `Auto -> 2 -> 3 -> 4 -> Match -> Auto`~~ (append after 4) | Preserves every "first step from Auto is 2" and "2 -> 3" pin verbatim (`test_ctf_ui.cpp:249-250,541-543,845`, `test_curses_picker_client.cpp:524`); only wrap/full-cycle pins move. Stored junk `9` still wraps straight to Auto (the `>= kTeamCountMatched` branch), so the existing `9 -> 0` pin survives unchanged. |
| D5 | **[SUPERSEDED by D26/D29 — the mask twin is now OWN, not Auto]** ~~MATCHED = Auto team count + matched bot power~~ | For every mask/activation purpose the sentinel is **indistinguishable from Auto** — which per mode may mean the `row.teams` manifest default, NOT the full authored mask: soccer (`mode_soccer_impl.lua:783`), basketball (`:2028`), and onslaught (`:678`) substitute `row.teams` for any requested count `<= 0` before `activate_teams`. The operative invariant is "Matched-mask == Auto-mask" (`team_count_request()` returns count 0 for both), pinned by the twin-mask test — do NOT write "Matched activates all authored teams" assertions; they fail on those three modes. The ONLY behavioral delta is inside the bot spawn seam. This makes I3 free: which teams get squads is unchanged from Auto — the seam already fills only fully-empty active teams; teams with any human/roster presence were never touched and still aren't. |
| D6 | **[SUPERSEDED by D27/D30 — team-count sanitize reverts to plain [2,4]]** ~~Sanitize: exactly `5` passes; `6+` still clamps to `4`; `<= 0` still snaps to `0`~~ | Preserves the `9 -> 4` pins (`tests/unit/test_lobby_server.cpp:387,399`) and keeps the junk-rejection posture. Only the one legal new value is admitted. |
| D7 | **[AMENDED by D27 — still no bumps, but the sentinel now rides `ctf_strip_scenario_troops`]** No protocol bump, no GTL bump, no snapshot/replay change | The sentinel rides the existing `SaveData::ctf_team_count` (v10 read block, `save_data.cpp:558-575`), `LobbySettings::ctf_team_count` i16 (any short fits), and `GameWorld::ctf_requested_team_count`. Writer stays GTL v14; `kNetworkProtocolVersion` stays 12; the 5 literal wire-byte pins and the offset-41 pin stay put. Campaign generator (`mode_levels.lua`) untouched — matched is a lobby setting, not level data. |
| D8 | **[SUPERSEDED by D31 — the mixed-build table is redone for the TROOPS field]** Mixed-BUILD lobbies are possible and fail soft (correction to the "version-gated, impossible" claim) | Because D7 keeps protocol v12, old binaries CAN share a lobby with new ones. All shapes degrade safely: old guest adopts `5`, renders `"Teams: 5"` (ugly, transient), gameplay unaffected (mode Lua and masks are host-authoritative); old host can never produce `5` and sanitizes an inherited `5` to `4` at publish. Old build reading a new `.gtl`: loads fine (v10 block reads any short), self-heals on first sanitize (`5 -> 4`) or first manual cycle (`5 -> 0`). No crash, no desync — exact behavior table in §2.6. |
| D9 | **Power metric = pure-Lua `f(walker)` in `mode_match.lua`; NO `g_heart_value` binding** | `guy::query_heart_value()` (guy.cpp:289-328) loses on three counts: it prices only guys (bots have no guy record, so half the comparison is unreachable anyway); it prices gold, not lethality (train-cost weights: soldier int 25/pt vs str 6/pt — a worked L4 soldier's heart is ~56% intelligence, which buys only mp); it ignores loader combat bases (two untrained families differ only by hiring cost while soldier/mage walker stats differ enormously). Heart value is kept as the **rank-order oracle in C++ tests** (callable directly, no binding). No new `og.*` name ⇒ no api_stub_check churn. |
| D10 | **`f` definition is fixed (§4.1); weights 4 / ½ / 5 / 60 are tuned, not sacred** | The `(L+3)//4` projectile term and the fire-frequency floor are **engine-shaped truncating approximations**, not engine copies: the real multiplier is FLOAT and applies to the WEAPON's base damage (`set_damage((weapon->damage() * (level + 3.0f)) / 4.0f)`, walker.cpp:1186-1187, never truncated), and the guy bonuses are float divisions (guy.cpp:447-465). `og.div(D*(L+3), 4)` is a deliberate integer weight that inherits the engine's *shape*. The multiplicative pool-times-throughput core matches the superlinear heart curve in shape. The oracle test pins team-level rank agreement; weights may be retuned there, never ad hoc. |
| D11 | **Target with multiple human teams = MEAN of human team sums** | Max rejected (one stacked roster makes every bot squad stomp the weaker human team); per-empty-team pairing rejected (an empty team has no pairing key; any invented key is arbitrary and order-fragile). Mean is order-independent, integer, and degrades to "that team's power" in the dominant solo case. **Known residual:** mean HALVES the kingmaker effect, it does not remove it — with a 50k roster vs a 10k roster the bot team lands at 30k (3.0x the weak team, 0.6x the strong one) and can decide the human-vs-human outcome by farming the weak team. Rejected-but-plausible alternative for the next balance pass: match to the WEAKEST human team (ties low) — rejected because it makes stacked-roster players face free-win bots and rewards sandbagging. Per-human-team f-sums are assertable in tests by calling the census directly; no extra telemetry mode vars are spent on them (var slots are scarce). |
| D12 | **[SUPERSEDED by D34/D35]** ~~Squad composition NEVER changes; matching is level-only~~ | TDM/soccer/basketball/CTF keep the 5-family BOT_SQUAD; mutant keeps one bot per seat. Member count is game-shape (5v5 basketball, exactly-one mutant); count changes are priced by the mirror-desync and respawn-queue constraints and pinned by exact alive-count tests. Level ceiling 9 (bot L² scaling far outguns any legal roster: L9 bot soldier hp 1173 vs L9 human soldier ~358), floor 1. **Consequence accepted in writing:** targets below `B(1)` (the full squad at flat L1) are unreachable by construction — see E13 for the stated magnitude. A narrower-squad exception was considered and rejected: it would break the exact alive-count pins, the mirror twins, and the "5v5 basketball" game shape for the weakest-equipped players specifically. The clamp is announced in-game instead (§7). |
| D13 | **Bot base power is MEASURED at spawn; increments are MODELED from a copied tuple table; a model-pin test compares the two** (resolves the scout conflict on `set_difficulty`) | The integration scout read `walker::set_difficulty` (base path: level arg unused for Livings, percent-only, team-0 exempt); the power scout documented `living::set_difficulty` (living.cpp:612-657), the virtual override that runs the family `og.apply_difficulty_scaling` hook with the level (hp/mp/armor scale L², damage L; else default 11/11/4/2). Ruling: the living-override reading wins — and the design is robust to being wrong: spawn-time stats are read back into `base_f_i` (no guessed constants ship), increments come from a tuple table in `mode_match.lua` (a COPY of pack data), and the model-pin test (§8) reds out on day one if the tuple path did not fire or a family rebalance drifts the copy. |
| D14 | **`set_difficulty` at most once per walker OBJECT; matched REPLACES the `difficulty//100+1` level formula; the percent multiplier is retained** | `apply_difficulty_scaling` is additive, so double application on the same walker inflates stats — the single-call-per-object discipline is a hard rule. (The engine itself calls `set_difficulty(entry.level)` on every AI respawn — harmlessly, because each replacement is a FRESH walker, `respawn.cpp classic_fire_ai_respawn`; that is the I5 mechanism, not a violation.) The difficulty formula's only job is "how strong are bots"; matched is a better-informed answer to the same question, so they compose by substitution, not multiplication. The percent stage (living.cpp:634-657) is the user's deliberate handicap knob: matched sets the fair baseline, Easy/Hard tilt it. **State its true magnitude:** the percent multiplies hp/mp AND damage, and `f` = pool x throughput, so bot `f` scales roughly QUADRATICALLY in the percent — Easy(50%) lands matched bots near 0.25x target-f, Hard(200%) near 4x, a ~16x total swing, not the 0.5x/2x "tilt" a casual reading suggests. All ±% accuracy claims in this document are defined at percent = 100. **There is NO team-0 exemption for bots** (see E7): the A12a branch percent-scales team-0 walkers without a myguy; only guy-carrying walkers are exempt, and they never pass through `set_difficulty` at all. Bots percent-scale identically on every team. |
| D15 | **Census in `on_mode_init`, after the troops strip and `respawn_scan_anchors`, human = "team with >= 1 live `has_guy()` walker"; the per-team decision is persisted in mode vars** | Identical predicate and identical point in init to `own_roster_activation` (mode_match.lua:93-130), so activation and matching can never disagree about which teams are human. Roster walkers are guaranteed in the oblist (`spawn_team_from_save` precedes the first tick on both authority paths). Persisting `(L*, k*)` per team is what makes I5 hold: every later spawn reproduces the solved strength without re-census. |
| D16 | **CTF is re-pointed at `match.spawn_bots` via an injectable placer — single-seam prerequisite** | The two spawners are NOT verbatim duplicates: CTF's `place_at_anchor` (`mode_ctf_impl.lua:271-305`) carries a FLAG-HOME fallback the mode_match version (`mode_match.lua:188-215`) lacks — when every anchor probe fails it tries the team's flag-home square (`flag_present` + `og.spawn_spot_clear` on `fget(S.FLAG_HOME, team)`) before the teleport fallback. A plain re-point would silently lose that fallback and turn blocked-anchor CTF spawns into teleports (an RNG draw). Ruling: `match.spawn_bots(team, families, cursor_slot, placer)` grows an optional placement function `placer(w, team, allow_teleport)`; when nil it uses mode_match's own `place_at_anchor(w, team, cursor_slot, ...)`. CTF keeps its local `place_at_anchor` (flag-home fallback intact) and passes it as the placer; only the squad/level logic is shared. WP-A acceptance includes a blocked-anchor CTF placement test (all anchors blocked -> flag-home spawn, no teleport) — `dead_bot_respawns_as_replacement_at_anchor` does not cover that shape. Applying matched in both copies is the rejected fallback. CTF's single-bot respawn does NOT consult Lua at all — it rides the engine respawn queue (see I5/E5). |
| D17 | **Onslaught is explicitly OUT of scope** | It has no bot fill site at all — fielded fighters come only from generators (`customize_spawn`, `mode_onslaught_impl.lua:222-247`) with a GEN-bank/grace-clock empty-team model (`:753-761`). Under MATCHED, onslaught treats the sentinel as Auto for masks and ignores matched power entirely. **Follow-up note (do not silently skip):** "Matched power for Onslaught needs a generator-level rule (scale generator level/count against the human census) — separate design." |
| D18 | **All team-count normalization collapses to two helpers: C++ `effective_team_mask` + Lua `core.team_count_request()`** | C++: `og::sim::effective_team_mask` (`src/gameplay/mode/mode_tick.cpp:63`, not a pinned file) treats `requested == kTeamCountMatched` as `<= 0`. Lua: `core.team_count_request()` returns `(count, matched)` with `count = 0` for Auto OR Matched; `activate_teams` (mode_core.lua:88) and the three per-mode `<= 0` fallback sites (soccer `:782-784`, basketball `:2026-2029`, onslaught `:676-679`) consume it. A constant-equality pin ties `core.MATCHED_TEAM_COUNT` to the C++ value via `og.match_setting` round-trip. |
| D19 | **Gating shape unchanged; no terminal host gate added** | SDL: `is_versus_campaign && picker_lobby_host_controls_visible()` (`picker_team_build.cpp:645-646`). Terminals: `VersusCampaignOnly` only; guests cycle cosmetically and the authoritative echo overwrites (`picker_lobby_network_client.cpp:1635`); the server drops non-host `SettingsChange` (`lobby_server.cpp:1077-1085`). That is the established contract — do not "fix" it here. **[SUPERSEDED in part — see docs/camp-controls-design.md §5.]** The terminal half no longer applies: the flat CTF trio (`ctf_teams`, `ctf_caps`, `ctf_troops`) left terminal Team Build, so there are no cosmetically-cycling guest rows left to gate. Teams and target score are the Modes camp's MATCH SETUP page (host-gated like any camp row); scenario troops keeps its SCENARIO row. The SDL gating shape in the first sentence still stands. |
| D20 | **Mode-var footprint: `MATCHED_LEVEL + team` (0 = not matched), `MATCHED_UP + team` (k), `MATCHED_TARGET` (capped T)** | All integers, persisted in mode vars so later spawns re-resolve without re-census. Levels are >= 1, so 0 is an unambiguous "unmatched" sentinel. **Mode vars are `std::int32_t`** (`mode_state.h:60`) and `og.mode_set` silently truncates via `static_cast` with no range check (`bindings_entity.cpp og_mode_set`), while T is a 64-bit Lua integer superlinear in trainable stats (infinite_gold is a supported lobby setting; a maxed 24-guy roster's f-sum can exceed INT32_MAX). Rule (AS BUILT): the census caps at store time — `record_match_target` writes `og.min(T, TARGET_CAP)` with `TARGET_CAP = 1 << 30` — and EVERY solve, the init-time `spawn_matched_bots` and the D24 backstop alike, consumes the stored capped value via `og.mode_get(MATCHED.TARGET)`; no solve ever sees the unclamped T. Lossless for behavior: any T above the cap is far past `B(9)` and solves to the uniform-L9 clamp (with the LIMIT flag) either way. Vars are written for teams solved at init AND for a wiped human team solved at backstop time (E4). Concrete slot numbers are assigned per mode at implementation time; they must dodge fixture-pinned slots. |
| D21 | **No new C++ binding anywhere in v1** | Everything needed is readable/settable today: `g_*` (guarded by `has_guy()`), `s_*`, `damage()/stepsize()/fire_frequency()`, `s_set_level`, `set_difficulty`, `og.match_setting`, `og.oblist`, `og.respawn_anchor_count`. **Caveat that shapes §4.1:** "readable" means readable as FLOATS — every walker/stat getter except `s_level` pushes `lua_Number` (`W_GET_FLT`/`S_GET_FLT`), so all reads pass through `og.trunc` before integer arithmetic (the §4.1 discipline). `bindings_entity.cpp`/`guy.cpp`/`combat_math.cpp` are pin-free, so a binding would be cheap — but a pure-Lua model needs none, and no binding means no api_stub_check churn and no `og_unit_mode` additions beyond the sentinel read-back pin. |
| D22 | **Solver = single argmin over the FULL `(L, k)` reachable set** (replaces the two-stage solve) | The two-stage form (L* first, then k-upgrades) does not minimize over its own reachable set: stage 1 can land ABOVE the target and stage 2 only moves power UP, so every worst-case miss is an OVERSHOOT (bots too strong — the harmful direction), up to ~39% at level-gap midpoints and ~19% in the old "L*=9 => k=0" band. Full search over `{P(L,k)}` (§5.3) is at most 41 integer evaluations for n=5, still deterministic, ties break (lower L, lower k), and cuts the worst-case miss to ~14% for n=5 at percent 100 — which is what makes the ±15% claim true. |
| D23 | **One-time in-game announcement when matching fires** | Without it the feature has zero in-game legibility: a matched L5 squad is indistinguishable from a stock L2 squad except by dying to it, and a saturated match looks identical to a perfect one — playtest reports could not tell "matched is broken" from "matched saturated". `on_mode_init` announces once via the existing `core.announce(text, sound)` facility (mode_core.lua:106) when any squad was matched-spawned: `"TEAMS MATCHED"` (13 chars), or `"TEAMS MATCHED (LIMIT)"` (21 chars) when any solved team clamped at either end — both inside the 25-char announcement budget documented at the win-call site. Sound: an existing mode sound constant chosen at WP-E (no new asset). Exact strings are part of the spec; pinned in WP-F. |
| D24 | **Wiped human team with no revivable corpses gets a TARGET-strength squad, not a legacy one** | `revive_wiped_teams` keys only on mask membership + zero live + zero pending (`mode_match.lua:366-375`) — it has no roster filter, so a formerly-human team whose corpses were destroyed DOES get a bot squad today. Under matched, letting that squad take the legacy difficulty formula would field a stock-power squad mid-matched-match — an unruled power discontinuity. Ruling: `spawn_bots` uses one unified level-source rule (§5.4): stored matched vars if present; else, when `MATCHED_TARGET > 0`, measure-and-solve NOW against the stored T (same spawn-measure-solve procedure as init — deterministic, no re-census of the live battle) and store the vars; else legacy. The wiped team's replacement squad is matched to the match's baseline. |

---

## 1. Global invariants (restated, with how each is satisfied)

1. **I1 Determinism.** Mode-Lua discipline unchanged: mode vars, integer math
   only (`og.div`, `og.max`), no `pairs`, no rand needed anywhere in the census
   or solver (census order = oblist order, deterministic server-side). All new
   logic lives in `campaigns/modes/**` — parity-invisible (no parity scenario
   loads the modes campaign; no pin touches `campaigns/`). Mode Lua runs on
   the authority only; bots replicate as ordinary entities — no client twin of
   the power model exists (pinned in prose at `tests/modes_pack_fixture.h:718-726`).
2. **I2 Gates stay green.** Every pin in §2.7 is updated, not weakened. Lua
   coverage func = 100 on every new/changed pack Lua function (§8 matrix);
   statement-per-line and luals gates on all edited pack Lua; campaign regen
   (`mode_levels.lua`) untouched; no new test binary ⇒ no
   `recorder_processes.txt` churn; any byte change to `kTestRegistrationLua`
   requires the `scripts/coverage/runtime_only_lua.txt` digest update in the
   same commit; save/lobby round-trips gain `= 5` cases in both halves.
3. **I3 Human teams untouched.** By construction (D5): matched changes bot
   *strength*, never *which* teams are filled. The census writes matched vars
   only for teams that were fully empty at init; the revive backstop schedules
   human corpses whenever any exist. (The one case where a human team receives
   bots — wiped with zero revivable corpses — is today's behavior too and is
   ruled by D24: the replacement squad matches the stored target, never the
   legacy formula.) Pinned by the oblist-sweep no-op test (§8) in the
   `test_modes_mutant.cpp:306-351` idiom.
   **Amended by LINEUP (docs/lineup-design.md §3.2):** an explicit per-team
   bot-squad preset (`bot_squad_N` >= 2) applies to ANY active team,
   occupied or not — a host may field bot allies beside a human roster.
   The invariant survives in its AUTO spelling: `bot_squad_N = 0` on an
   occupied team still means no bots, and all-zero knobs reproduce the
   pre-lineup fills byte for byte.
4. **I4 Networked correctness.** The sentinel rides the existing
   SaveData -> LobbySettings -> start-config -> `sync_world_from_save_data`
   chain, which completes strictly before the first `world.tick()` runs
   `on_mode_init` (ordering verified: `headless_server_runtime.cpp:394-465`,
   SDL twin `game.cpp:102/127/140-142/161`; pinned by a new ordering test).
   Downgrade behavior is documented exactly (§2.6) and fails soft in every
   shape — including same-protocol mixed-build lobbies (D8).
5. **I5 Matched strength survives revives.** Two survival paths, both covered
   — and they are NOT symmetrical (`respawn.cpp` tags entries
   `kind = myguy ? 0 : 1`):
   (a) ROSTER corpses (`myguy` present) revive **in place** —
   `revive_player_walker` refills hp on the same walker object; matched never
   touches these walkers anyway.
   (b) BOT corpses are replaced by **fresh walkers**:
   `classic_fire_ai_respawn` runs `add_ob` + `stats()->set_level(entry.level)`
   + `set_difficulty(entry.level)`, with `entry.level` snapshotted from the
   corpse's stats level (clamped 1..255) at scheduling time. Matched strength
   survives because `spawn_bots` stamped `s_set_level(L*)` (or `L*+1`) at
   spawn — the engine re-derives the replacement from that snapshot with no
   Lua consultation. Scripted spawns (init fills, wiped-team backstops) flow
   through `spawn_bots` and re-resolve from the persisted mode vars.
   Tests therefore assert the **replacement's `s_level` equals the matched
   level**, never walker-object identity. Pinned by extending the four
   existing revive/respawn tests (§8).

---

## 2. PART I — Control surface

### 2.1 Sentinel constant

```cpp
// include/openglad/gameplay/lobby_state.h, next to the :193 field
inline constexpr std::int16_t kTeamCountMatched = 5;
```

Field comments extended in lockstep: `lobby_state.h:193`, `lobby_server.h:19`,
`save_data.h:77` → `// 0 = Auto; 5 = Matched (kTeamCountMatched)`.

### 2.2 Cycler (`src/interface/ui/picker_common.cpp:527-537`)

```cpp
if (save.ctf_team_count <= 0)                       save.ctf_team_count = 2;   // Auto -> 2
else if (save.ctf_team_count >= kTeamCountMatched)  save.ctf_team_count = 0;   // Match (or junk 6+) -> Auto
else  save.ctf_team_count = static_cast<short>(save.ctf_team_count + 1);       // 2->3->4->5(Match)
```

Stale header comments fixed in the same edit:
`include/openglad/interface/ui/picker_common.h:249` →
`// Cycle the requested team count: Auto -> 2 -> 3 -> 4 -> Match -> Auto.`;
`:723` → `// Format the team count label ("Teams: N" / "Teams: Auto" / "Teams: Match").`

### 2.3 Formatter (`src/interface/ui/picker_common.cpp:1702-1707`)

`if (save.ctf_team_count == kTeamCountMatched) return "Teams: Match";` inserted
before the existing `<= 0` -> Auto and numeric branches. 12 chars — the
`ASSERT_LE(size(), 12u)` pin now binds tight (12 == 12); keep it.

### 2.4 Sanitize (`src/gameplay/lobby_server.cpp:70-78`)

```cpp
if (v == kTeamCountMatched)  { /* keep */ }
else if (v > 0)              { /* clamp to [2, 4] — 6..9 still -> 4 */ }
else                         { v = 0; /* Auto */ }
```

"Both directions" verified: host publish -> sanitize keeps 5 ->
`broadcast_state` -> guest adopt (`picker_lobby_network_client.cpp:1635`)
stores 5; the solo `LocalPickerLobbyClient` round-trip returns 5 to the save
unchanged.

### 2.5 Explicitly UNCHANGED surfaces

- **Three clients, zero handler edits.** The value rides the existing
  `CycleCtfTeamCount` command: SDL `change_ctf_teams`
  (`picker.cpp:2875-2887`, both label surfaces + lobby sync + autosave), text
  `text_picker.cpp:823-827` + Matchup dump `:1004-1006` (prints the shared
  formatter's output), curses `curses_picker_client.cpp:1028-1032`. No new
  ButtonAction (103 stays free), no `PickerMenuCommand`, no menu-model row, no
  ordinal churn, no nav rewire, no new SDL rect.
- **Wire/replication untouched** (D7). `append_lobby_settings` /
  `read_lobby_settings`, snapshot, replay, level carry-over, checkpoint copy —
  all already carry the field; a new *value* costs nothing.
- **Persistence untouched.** `ctf_team_count` reads in the GTL v10 block;
  autosave-on-cycle via `picker_settings_autosave()` unchanged.
- **Gating unchanged** (D19).
- The curses screen title `"CTF Teams"`
  (`curses_picker_client.cpp:1030`) is stale but unpinned — left alone
  (recorded, not fixed, to keep this change's surface minimal).

### 2.6 Downgrade / mixed-build behavior (I4, exact)

| Shape | What happens |
|---|---|
| Old build loads a new `.gtl` with `ctf_team_count = 5` | Loads fine (v10 block reads any short). Label renders `"Teams: 5"` via the old `std::format` branch — cosmetic. First sanitize round-trip clamps `5 -> 4`; or first manual cycle wraps `5 -> 0` (old `>= 4` branch). Save self-heals to a legal old value. No crash, no corruption. |
| Old GUEST in a new host's lobby (same protocol v12) | Adopts `5` from the settings broadcast, shows `"Teams: 5"`; seat-chip display clamps locally. Gameplay unaffected: mode Lua and the effective mask are computed host-side; the guest applies snapshots. |
| Old HOST | Can never produce `5`; sanitizes an inherited `5` to `4` at publish. Guests see `Teams: 4`. |
| New build, old server sanitizes `5 -> 0` | Impossible in-tree (server and sanitize ship together), but the failure mode is still soft: `0` = Auto ⇒ mode Lua sees no sentinel ⇒ legacy difficulty-formula bots, byte-identical to today. |

### 2.7 Pin ledger — every touched test site

| File / lines | Today | Becomes |
|---|---|---|
| `tests/unit/test_picker_common.cpp:1275-1294` | cycle `0->2->3->4->0`, `9->0` | `0->2->3->4->5->0`; keep `9->0`; add `1->2` junk case |
| `tests/unit/test_picker_common.cpp:1314-1332` | `"Teams: Auto"`, `"Teams: 4"`, `<=12` | add `format(5) == "Teams: Match"`; keep the tight `<=12` |
| `tests/unit/test_menu_spec.cpp:104-135` | literal cycle array at `:119-120` | `{"Teams: 2","Teams: 3","Teams: 4","Teams: Match","Teams: Auto"}` |
| `tests/unit/test_menu_spec.cpp:236-265` | `"Match Teams"` fallback, spectator `"Teams: Auto"` | UNCHANGED — verify only |
| `tests/integration/test_menu_layout.cpp:1479-1575` | counts/ids/geometry/budgets | UNCHANGED (no new row). VERIFIED: the `:1565-1570` budget sweep asserts STATIC label sizes only — it never walks cycles, so `"Teams: Match"` never reaches it. The 12-char budget for the new label is pinned in `test_picker_common.cpp` instead: the new `format(5)` case carries its own `ASSERT_LE(size(), 12u)` AND an equality assert on the literal, so a future rename re-trips the budget consciously |
| `tests/integration/test_ctf_ui.cpp:249-250,258,536-543,845` | `2->3`, `"Teams: 3"`, Auto->`"Teams: 2"` | ALL UNCHANGED (D4); ADD a flow cycling 4 -> Match -> Auto asserting `"Teams: Match"` on both label surfaces |
| `tests/curses/test_curses_picker_client.cpp:503-549` | Auto->2 gate; `"Teams: 4"` render | UNCHANGED; ADD save=5 renders `"Teams: Match"` |
| `tests/unit/test_lobby_server.cpp:381-427` | `9->4`, `3` passes | keep both; ADD `5->5` kept, `6->4`, non-host `SettingsChange(5)` dropped, `build_save_data_equivalent` carries 5 |
| `tests/unit/test_mode_bindings.cpp:278-303` | `team_count` read-back | ADD sentinel read-back + `core.MATCHED_TEAM_COUNT` equality pin |
| `tests/unit/test_company.cpp:1514,1603` | GTL round-trip `= 3` | ADD a `= 5` value case in both halves |
| `tests/integration/test_picker_funcs.cpp:2870-2976` | Auto vs explicit-2 seat behavior | ADD a Matched variant asserting Auto-identical seat behavior |
| `tests/coverage_internal/text_picker_internal.inc:855-875` | field-change asserts | value-robust — verify, likely no edit |
| `tests/integration/test_menu_model.cpp`, `tests/unit/test_platform_headless.cpp:908-989`, `scripts/test_text_picker_interactive.sh` | command resolution / ordinals | NOT MOVING (no new command, no new row) |

---

## 3. PART II — Setting semantics and plumbing

### 3.1 Normalization (D5, D18)

- **C++:** `og::sim::effective_team_mask` (`src/gameplay/mode/mode_tick.cpp:64`)
  gains one clause: `requested == kTeamCountMatched` behaves as `<= 0`
  (authored mask). Not a pinned file. VERIFIED at WP-B (recorded result):
  every downstream consumer routes through the one helper — lobby seat
  re-resolution via `LobbyServer::effective_team_mask` ->
  `lobby_effective_team_mask` (`src/gameplay/lobby_server.cpp:519-521`,
  consumed by the re-resolution at `:1095`), the Base Camp seat chips via
  `og::sim::lobby_effective_team_mask`
  (`src/interface/ui/menu_screen_specs.cpp:2110-2111`), and the client
  mask fallbacks likewise (`picker_lobby_client.cpp:590`,
  `picker_lobby_network_client.cpp:2909/3966`). No consumer re-derives the
  mask locally. Server-side behavior pinned by
  `PickerFuncs.local_lobby_matched_setting_keeps_auto_seat_behavior`; the
  chip function itself has no direct Matched test and relies on the
  single-helper structure (recorded limitation).
- **Lua:** `core.MATCHED_TEAM_COUNT = 5` and
  `core.team_count_request()` -> `(count, matched)` where `count` is `0` when
  the raw `og.match_setting("team_count")` is Auto OR Matched, and `matched`
  is the boolean. `activate_teams` and the three per-mode `<= 0` fallback
  sites consume it — four normalization sites collapse to one helper.
- **Binding untouched:** `og.match_setting("team_count")`
  (`bindings_entity.cpp:2402-2423`) returns the raw short; Lua sees `5`.
  `docs/modding/og-api.d.lua:548` documents the sentinel.

### 3.2 Ordering to `on_mode_init` (I4)

Per level, authority path (`headless_server_runtime.cpp:394-465`):
`sync_world_from_save_data` (`:425`) -> `level_data.load()` (`:427`) ->
`sync_world_from_save_data` again (`:450`, load clears world fields) ->
difficulty percent (`:451`) -> per-walker `set_difficulty` (`:453-459`) ->
`spawn_team_from_save` (`:461`, roster enters oblist) -> first `world.tick()`
-> `mode_run_tick` -> `respawn_scan_anchors` -> `level_mode_init`. The SDL
twin is `game.cpp:102/127/140-142` + `:161`. Consequences: the sentinel is in
`world.ctf_requested_team_count` before Lua runs; the roster is censusable at
init; `og.respawn_anchor_count` is populated. A new ordering test pins this
(§8).

### 3.3 Persistence

Reuses `ctf_team_count` end to end (D7). Session-only storage (the
`cross_control`/`infinite_gold` precedent) was considered and REJECTED: the
setting is a match rule the user expects to stick, exactly like Teams: 2-4,
and reusing the persisted field costs nothing.

---

## 4. PART III — Power model

### 4.1 The metric `f(walker)` (exact, integer, Lua-readable inputs only)

Inputs (all readable today, `bindings_entity.cpp:2596-2736`):
`s_max_hitpoints` H, `s_max_magicpoints` M, `s_armor` A, `damage()` D,
`fire_frequency()` FF, `s_level` L, `stepsize()` SP.

**Truncation discipline (part of the metric definition, not a style rule).**
Every input except `s_level` reaches Lua as a FLOAT (`W_GET_FLT`/`S_GET_FLT`
push `lua_Number`), and the human derived stats are float divisions in C++
(`strength/4.0f`, `dexterity/54.0f`, `dexterity/47.0f`, guy.cpp:447-465) — a
fresh L1 soldier's `fire_frequency()` is 5.872..., and `og.div`/`og.mod`
`luaL_checkinteger` their arguments while `og.max` returns its winning
argument unchanged, so any untruncated read raises `"number has no integer
representation"` and kills `on_mode_init`. Therefore **every float read is
truncated at the boundary**, the `og.trunc(og.fdiv(...))` pack precedent
(`packs/core/families/living-00-soldier.lua:14`):

```
H  = og.trunc(w:s_max_hitpoints())    M  = og.trunc(w:s_max_magicpoints())
A  = og.trunc(w:s_armor())            D  = og.trunc(w:damage())
SP = og.trunc(w:stepsize())           FF = og.max(1, og.trunc(w:fire_frequency()))

ED   = og.div(D * (L + 3), 4)         -- engine-shaped projectile level term (walker.cpp:1186, D10)
RATE = og.div(120, FF)                -- attacks per unit time
OFF  = ED * RATE + 5 * SP             -- offense throughput + mobility
EHP  = H + 4 * A + og.div(M, 2)       -- pool: armor ~4hp/hit, mp at half credit
f    = og.div(EHP * (OFF + 60), 60)   -- pool x throughput; +60 floors zero-offense walkers
```

**Accepted fidelity limitations (named so the oracle test's scope is honest):**
- *Mage/special utility*: spell effects are underpriced by raw stats; `M/2`
  is partial credit (D9 rationale 2 explains why heart value is worse here).
- *MP-starved throughput*: firing is mp-gated (`walker.cpp:507,1306`) and bot
  base mp is 0, so `RATE` overprices sustained bot offense at low levels —
  matched bots land somewhat UNDER target in long fights (player-friendly
  systematic bias).
- *Range/kiting unpriced*: an all-archer roster at equal `f` plays stronger
  against melee AI than an all-soldier one; matched-vs-ranged-humans is
  systematically easier.
- *Regen/sustain absent*: heal rates and con/int-derived delays do not enter
  `f`.
- *Family `set_difficulty` side effects*: the soldier hook also sets
  `weapons_left` (`living-00-soldier.lua:132-136`) — unpriced, harmless.
Retuning path: an mp-throughput cap on `RATE` or a small range term are legal
future weight adjustments, guarded by the same oracle test (D10).

### 4.2 Calibration points (derived at WP-E, not hand-shipped)

**The authoritative calibration numbers are NOT in this document.** Earlier
drafts hand-computed them under an integer misreading of guy.cpp and every
number was born stale (illustratively: a fresh L1 soldier is f ≈ 1971 under
the integer misreading but f ≈ 2306 under the real trunc-on-read discipline —
FF truncates 5.872 to 5, RATE becomes 24). Rule: **WP-E derives the
calibration table by running the census on fixture-built guys and records the
values into the oracle test**, so the table can never drift from the code
that computes it.

What the spec DOES fix are the calibration *requirements*:
- Rank order vs heart value at TEAM granularity must agree monotonically
  (fresh squad < mixed L4 roster < archmage-anchored roster). Within-roster
  inversions are expected — heart's int-price bias is the reason f exists.
- Bot base stats at spawn: **armor 0, mp 50** (MEASURED at WP-E — the
  earlier "mp 0" reading was wrong; 50 is the `statistics` default the
  loader leaves in place). f's `4*A` term vanishes at bot base; the `M/2`
  term contributes 25. Recorded in the model-pin test comments.
- Continuity calibration (MEASURED, supersedes the draft's B(2) claim): a
  fresh human squad's target T ≈ 6520 sits well under B(2) ≈ 10991, so the
  solver answers **L1 with one upgraded member (k=1)** — slightly gentler
  than today's flat-L2 stock squad, which is the honest reading of
  "comparable power" for a fresh roster. The stock-equivalence guarantee is
  the LEGACY arm (no matched vars -> byte-identical stock squad), pinned by
  the legacy-equivalence test.

### 4.3 Bot side: measured base + modeled increment (D13)

Bot power is a function of (family, level) through
`living::set_difficulty` -> family `og.apply_difficulty_scaling(self, L, hp,
mp, dmg, armor)` — hp/mp/armor scale L², damage L (guy.cpp:393-401); default
tuple 11/11/4/2. Squad-family tuples: soldier 13/8/5/2, archer 11/12/4/1,
mage 7/14/3/0.5, elf/thief default.

Mechanism (single-application discipline, D14):

1. Spawn each squad member (they are the real squad, not probes).
2. Read spawn-time stats -> `base_f_i` (absorbs whatever the loader actually
   sets — no guessed constants ship).
3. Predict `pred_i(L)` by applying the family tuple to the measured base
   inside `f`: `H + hpc*L*L`, `M + mpc*L*L`, `D + dc*L`, `A + ac*L*L`
   (mage's 0.5 armor coefficient → `og.div(L*L, 2)`). The tuple table lives
   in `mode_match.lua`, keyed by family string id — a COPY of pack data,
   guarded by the model-pin test, which iterates EVERY row. AS BUILT the
   table carries a row for every hook-declaring core family (soldier,
   archer, mage, orc, beast `core:#18`, cleric, druid), so any core-family
   roster a future mode names is priced correctly; elf/thief (hookless)
   share the default 11/11/4/2 row, which is genuinely correct for every
   family without a hook. A non-core pack family with its own hook needs a
   TUPLE row before a matched roster may name it.
4. Solve `(L*, k*)` (§5.3), then apply `s_set_level` + `set_difficulty`
   **once**, after `set_team_num` (order preserved from today's seam).

**Measured-side truncation:** `apply_difficulty_scaling` adds FLOATS onto
float stats (guy.cpp:394-401; a mage bot's armor keeps the fractional
`0.5*L²` in the walker while the model floors it), and at difficulty percent
!= 100 the percent stage leaves fractional hp/mp/damage (living.cpp:634-640).
Every measured bot stat therefore passes through the same §4.1 trunc
discipline, the model-vs-measured gap is absorbed by the model-pin ±10%
band, and **the model-pin test runs at percent = 100 only** (other percents
compare a percent-scaled measurement against an unscaled model — meaningless).

### 4.4 Target (D11)

`T = og.div(sum of human team f_sums, human_team_count)`. `T = 0` (no human
power server-side) means no matched vars are written and every spawn takes the
legacy formula — exact current behavior, no special case.

---

## 5. PART IV — Census and squad generation

### 5.1 Census (D15)

One extra pass over `og.oblist()` inside `on_mode_init`, filtering
`order() == ORDER_LIVING`, alive, `has_guy()`. AS BUILT (re-plumbed by the
D41 plan phase, then re-homed by the D42 retirement): the power census
(`census_power`) runs inside `match.bank_match_target(decision, obs)` at
the top of every apply — BEFORE the troops strip, not after as first
drafted — while matched-ness and the headcount (`MATCHED.SIZE`) are
decided by `match.activation` over `match.census_inputs()` (each mode's
in-body `decide` fold at the top of `on_mode_init`; the old
`own_roster_activation`/`census_mask`/`record_match_target` trio is
deleted). Verified strip-invariant: both strip paths skip `has_guy()`
walkers, so the human census cannot change across the strip, and the plan's
activation rule guarantees no human roster team falls outside the Matched
mask. **Every `g_*`/stat read is guarded by `has_guy()`** — unguarded
reads raise `"walker has no guy record"` (`bindings_entity.cpp:101-103`).
All owners' guys count identically (has_guy is owner-blind); ownership never
enters.

### 5.2 Mode-var footprint (D20)

AS BUILT (supersedes the draft's per-team `MATCHED_LEVEL/MATCHED_UP` at
mode-private slots — CTF and Onslaught occupy every private slot 8..63):
the matched state lives in **packed HEADER slots 2/3/4**, verified unused by
all six mode impls and by mode_core's SLOT table.
**Slot 2 = the capped `MATCHED_TARGET`** (D20's `1 << 30` cap; 0 = census
never ran or found no human power).
**Slot 3 = the single base-100 packed per-team PLAN** — one code per team,
code = L * 10 + k (0 = unsolved), max packed value 94,949,494 < 2^31.
**Slot 4 = the one-shot ANNOUNCED latch** (0 none / 1 normal / 2 limit, §7).
Plan digits are written only for teams solved at init or by the D24
backstop; TARGET and ANNOUNCED are written whenever a matched census or
announce runs — the E3 no-op world still records TARGET > 0 with zero
fills (pinned by `matched_with_humans_on_every_seat_is_an_auto_noop`).

### 5.3 Level solve (deterministic, <= 45 model evaluations — D22)

```
B(L)     = sum over members of pred_i(L)                        -- squad at uniform L
P(L, k)  = B(L) + sum_{i<k}(pred_i(L+1) - pred_i(L))            -- k members upgraded one level
(L*, k*) = argmin over the FULL reachable set
           { P(L, k) : L in 1..9, k in 0..n-1, (L = 9 => k = 0) }
           of |P(L, k) - T|                                     -- ties break (lower L, then lower k)
```

Squad = first k* members (fixed squad-index order) at L*+1, rest at L*.
Single argmin over the whole reachable set — NOT a two-stage solve (D22): the
two-stage form could land up to ~39% above target at level-gap midpoints
because stage 2 only moved power up. At most 9x5 = 45 integer evaluations for
n = 5 (41 after the L9 restriction); no runtime tolerance — argmin always
answers. Accuracy contract, defined at percent = 100: within ±15% of T
(worst case ~14.4% for n = 5) whenever T ∈ [B(1), B(9)]. Outside that range:
clamp to uniform L1 / uniform L9, record the miss in `MATCHED_TARGET`, and
announce the limit (§7).

**Mutant carve-out:** n = 1 has no k-interpolation and single-bot power jumps
~2.1-2.3x per level at low levels, so mutant's per-seat accuracy is ~±35% at
gap midpoints (tightening with level). Each seat still solves independently
against its own measured base, so family disparity is priced — only the
granularity is coarse. Stated bound, accepted. *(Generalized by D36: every
n = 1 squad — not just mutant seats — now carries this band; the per-n
accuracy table lives in the headcount amendment.)*

Calibration anchor (as measured at WP-E, §4.2): a fresh human squad solves
to L1/k1 — just under today's stock flat-L2 squad. Stock behavior itself is
guaranteed by the legacy arm (no matched vars), not by the solver landing on
B(2); the legacy-equivalence test pins byte-identical stock squads.

### 5.4 Seam wiring

`mode_match.spawn_bots` (mode_match.lua:216-231) becomes: level source =
matched vars when `MATCHED_LEVEL + team > 0`; else, when `MATCHED_TARGET > 0`,
the D24 measure-and-solve-now arm; else the existing
`og.max(1, og.div(og.match_setting("difficulty"), 100) + 1)`. With D16 landed,
this one seam covers every SCRIPTED spawn: TDM/mutant/soccer/basketball init
fills, CTF init fill, and both revive backstops (`revive_wiped_teams` ->
`spawn_bot_squad`). CTF's single dead-bot replacement does **NOT** route
through Lua — it rides the engine respawn queue, which reproduces the matched
level from the corpse's stamped `s_level` snapshot (I5(b)); no
`bot_level_for` wiring exists or is needed on that path. The L/L+1 member
split is internal to `spawn_bots` (the k* prefix rule).

### 5.5 Difficulty composition (D14)

Matched replaces the level input; the percent multiplier still applies — to
**every** bot team including team 0 (the A12a branch percent-scales team-0
walkers without a myguy; only guy-carrying walkers are exempt, and they never
pass through `set_difficulty` at all). Human census stats are therefore
always percent-free. Magnitude per D14: bot `f` scales roughly quadratically
in the percent (Easy ≈ 0.25x target-f, Hard ≈ 4x). The dead branch at
percent == 100 keeps the parity harness blind to all of this (harness runs
at 100; modes Lua is parity-invisible anyway).

---

## 6. Edge-case ledger

| # | Case | Ruling |
|---|---|---|
| E1 | No human power server-side (all-bot exhibition; dedicated host, zero deployed rosters) | Census finds no has_guy walker, T = 0, matched vars stay 0 -> legacy formula everywhere. Exact current behavior. A dedicated host WITH joined players is NOT this case — their guys are in the oblist via `spawn_team_from_save` before init. |
| E2 | One L1 human vs stacked rosters | Mean target (D11). A single 16-guy shared team -> huge T -> uniform L9 clamp, undershoot recorded in `MATCHED_TARGET` for tests. Solo L1 -> T = 9855 -> mixed L1/L2 squad (k=3): an even rival, not today's flat L2 wall. No member inflation, ever (D12). |
| E3 | Humans on every authored team | No empty active team exists; MATCHED plays identically to Auto. Silent no-op — same precedent as Teams: 4 on a 2-team map clamping silently today. Documented in the cycler comment and `og-api.d.lua`; pinned by the oblist-sweep test. |
| E4 | Team wiped mid-match | `revive_wiped_teams` (soccer `:158` / basketball `:489`) -> `spawn_bot_squad` -> `spawn_bots` -> matched vars -> identical-strength squad. A wiped MATCHED team refills at its stored `(L*, k*)`. A wiped HUMAN team whose corpses are all gone (the backstop has no roster filter — `mode_match.lua:366-375`) gets the D24 arm: measure-and-solve NOW against the stored `MATCHED_TARGET`, store the vars, spawn at match baseline — never the legacy formula mid-matched-match. When corpses exist, they are scheduled instead (human teams come back as themselves). |
| E5 | Respawned/revived bot strength (I5) | Per the corrected I5: (a) roster corpses revive in place (hp refill, same walker); (b) bot corpses are replaced by FRESH walkers whose `s_level` the engine re-derives from the corpse's stamped snapshot (`entry.level`, clamped 1..255) — matched strength rides the `s_set_level` stamp from spawn time, no Lua on that path. Tests assert replacement `s_level == matched level`, not object identity. Pinned by extending soccer `:1227`, basketball `:1531`/`:2997`, CTF `:1723`. |
| E6 | TROOPS:OWN | AS BUILT: the matched census (`record_match_target`) runs at the TOP of `own_roster_activation` — BEFORE the strip — and is strip-invariant: both strip paths and the census key on `has_guy`, so authored troops (no guy record) never pollute T in either order. Pinned by `ModesTdm.matched_troops_own_solo_roster_gets_a_matched_opponent`. (`scenario_troops_strip_runs_before_the_bot_census` pins a DIFFERENT pass — the empty-team FILL census — not this one.) (2026-08-18, issue #218 directive — see the D26 addendum: the old Auto arms, solo-roster+1 and exactly-the-rosters, are gone. Every TEAMS value — Auto resolving to the authored count, or an explicit N — backfills authored non-roster teams up to the count, and each empty one gets its squad; the flagship solo-on-a-2-team-map experience is unchanged in shape.) |
| E7 | Team-0 bot squad | **No team asymmetry exists** (corrects the scouts' stale reading of base `walker::set_difficulty`): the A12a branch in `living::set_difficulty` percent-scales team-0 walkers WITHOUT a myguy whenever percent != 100 — only guy-carrying walkers are exempt, and they never reach `set_difficulty`. Matched bot squads level-match and percent-scale identically on every team including 0. Nothing to equalize; do not pin an exemption. |
| E8 | Onslaught | FAIR masks exactly like OWN (D33(a)), under the one activation rule: roster teams stay, authored generator teams backfill in index order to the count, and TEAMS: Auto = the authored count (2026-08-18 directive) — with a roster OR all-bot (the `row.teams` manifest default substitutes only under TROOPS: ALL). Matched power ignored — no plan, no announce, generator armies identical to the OWN twin; a backfilled team keeps its foundry (keep_generators) and fields no init bots (D17); the shared census still latches `MATCHED_TARGET` when humans are present (follow-up note filed in the PR). Pinned by `ModesOnslaught.matched_request_masks_like_own_and_ignores_power` (solo roster, mask 7, all 3 foundries stand) and `all_bot_own_and_fair_auto_field_every_authored_team` (zero roster, same mask + FAIR==OWN). |
| E9 | Old build / old guest / old host encounters `5` | Fail-soft table in §2.6. No shape crashes or desyncs. |
| E10 | Junk values | Cycler: `9 -> 0` (unchanged), `1 -> 2` (unchanged). Sanitize: `6..9 -> 4` (unchanged), `<= 0 -> 0` (unchanged), only exactly `5` passes. |
| E11 | Mid-lobby cycle to/from Matched | Server re-resolves every seat against the new effective mask and clears non-host readies (`lobby_server.cpp:1087-1211`) — MATCHED's mask equals Auto's, so switching Auto <-> Matched moves no seats; pinned by the `test_picker_funcs.cpp` Matched variant. |
| E12 | Init cost | One oblist pass + <= 45 integer model evaluations (D22) — well inside the 500k budget probes (tdm `:1085`, items `:406`, onslaught `:1727`), which re-measure it. Never bump a budget. |
| E13 | **[SUPERSEDED by D37 — the playtest rejected this clamp]** Target below `B(1)` (low-end saturation) | D12's fixed composition makes sub-`B(1)` targets unreachable by construction: a SINGLE fresh L1 soldier (T roughly half of `B(1)`) faces the full 5-bot flat-L1 squad at ~2x its f-sum — and worse in felt terms, since linear f-summing does not price 5-v-1 focus fire. Accepted in writing: it is still far closer than stock (5x L2 ≈ 5x), the miss is recorded in `MATCHED_TARGET`, and the §7 LIMIT announce tells the player matching saturated. The narrower-squad exception was priced and rejected (D12: alive-count pins, mirror twins, game shape). |

---

## 7. UX legibility (D23)

Without an in-game signal, a matched L5 squad is indistinguishable from a
stock L2 squad except by dying to it, and a saturated match looks identical
to a perfect one. Ruling:

- `on_mode_init` announces **once**, via the existing
  `core.announce(text, sound)` facility (mode_core.lua:106), iff at least one
  squad was matched-spawned:
  - `"TEAMS MATCHED"` (13 chars) — normal solve;
  - `"TEAMS MATCHED (LIMIT)"` (21 chars) — the solve clamped at either end
    (T < B(1) or T > B(9)). Both inside the 25-char worst-case budget
    documented at the win-call site. AS BUILT: the LIMIT flag reflects the
    FIRST solved team — distinguishable from "any" only in Mutant, where
    per-seat squads have different B(1)/B(9); pinned from both sides by
    `matched_borderline_seat_announce_follows_first_solve` (first seat in
    range, later seats clamp — plain variant; clamp premises pinned on the
    fielded bots' measured B(1)) and
    `matched_first_seat_clamp_announces_the_limit` (first seat clamps —
    LIMIT variant).
- Sound: an existing mode sound constant chosen at WP-E (no new asset).
- The announce arm is a row in the function-coverage matrix and pinned by a
  WP-F test (normal and LIMIT variants).
- No per-tick HUD line: the 4 HUD slots belong to the modes' own scoreboards.

---

## Implementation plan

Work-package DAG. Each WP is one-agent-sized, lists owned files, and carries a
difficulty tag (**fable** = power/census/generation Lua and menu-pin-heavy UI;
**opus** = docs/stale-comment/simple-test work). Every WP runs
`cmake --build --preset ci-test && ctest --preset ci-test` plus the Lua gates
(statement-per-line, luals) when pack Lua changed, and
`check_mutation_pins.py` when any `src/` file changed.

```
WP-A (ctf-seam) ────────────────┐
WP-B (sentinel-ui) ─┬─ WP-C (ui-flows) ─────────────┐
                    ├─ WP-D (lua-helper) ── WP-E (power-model) ── WP-F (system-tests) ──┐
                    └─ WP-G (docs) ─────────────────────────────────────────────────────┴─ WP-H (gate-sweep)
   (WP-E depends on BOTH WP-A and WP-D)
```

| WP | Tag | Depends | Scope | Owned files |
|---|---|---|---|---|
| **WP-A ctf-seam** | fable | — | D16: delete CTF's duplicate `spawn_bot_squad` (`mode_ctf_impl.lua:699-712`), re-point at `match.spawn_bots(team, SQUAD, S.ANCHOR_CURSOR)` bridging the 3-arg/4-arg `place_at_anchor` difference; behavior byte-identical (same families/levels). Verify CTF fill + dead-bot respawn tests still pass; func-coverage on any remaining wrapper. | `campaigns/modes/packs/modes.core/lib/mode_ctf_impl.lua`, `tests/unit/test_modes_ctf.cpp` |
| **WP-B sentinel-ui** | fable | — | D2/D3/D4/D6 + C++ normalization: `kTeamCountMatched`, cycler, formatter, sanitize, `effective_team_mask` clause, comment fixes (`picker_common.h:249,723`, `save_data.h:77`, `lobby_state.h:193`, `lobby_server.h:19`). Verify seat re-resolution + Base Camp chips route through `effective_team_mask` (§3.1). Re-pin the unit tests: picker_common, menu_spec, lobby_server, company GTL `=5`, mode_bindings sentinel read-back. | `include/openglad/gameplay/lobby_state.h`, `src/interface/ui/picker_common.cpp`, `include/openglad/interface/ui/picker_common.h`, `src/gameplay/lobby_server.cpp`, `include/openglad/gameplay/lobby_server.h`, `src/gameplay/mode/mode_tick.cpp`, `include/openglad/resources/save_data.h`, `tests/unit/test_picker_common.cpp`, `tests/unit/test_menu_spec.cpp`, `tests/unit/test_lobby_server.cpp`, `tests/unit/test_company.cpp`, `tests/unit/test_mode_bindings.cpp` |
| **WP-C ui-flows** | fable | WP-B | Integration/terminal proof wave: new `test_ctf_ui` injector flow (4 -> Match -> Auto, both label surfaces), curses save=5 render case, `test_picker_funcs` Matched seat variant (E11), run `og_test_menu_ui` unchanged as the no-layout-movement proof, verify `text_picker_internal.inc` needs no edit. | `tests/integration/test_ctf_ui.cpp`, `tests/curses/test_curses_picker_client.cpp`, `tests/integration/test_picker_funcs.cpp` |
| **WP-D lua-helper** | fable | WP-B | D18 Lua half: `core.MATCHED_TEAM_COUNT`, `core.team_count_request()`; convert `activate_teams` + the three `<= 0` fallback sites; constant-equality pin via `og.match_setting` round-trip; direct coverage of all helper arms (Auto / 2-4 / Matched). | `campaigns/modes/packs/modes.core/lib/mode_core.lua`, `.../mode_soccer_impl.lua`, `.../mode_basketball_impl.lua`, `.../mode_onslaught_impl.lua`, `tests/unit/test_mode_bindings.cpp` (constant pin), one modes unit file for helper arms |
| **WP-E power-model** | fable | WP-A, WP-D | §4 + §5 complete: `f`, tuple table, census, solver, mode vars (slot assignment per mode, dodging fixture pins), `spawn_bots` level source, `bot_level_for`, CTF single-replacement wiring. Leveled-hero fixture sibling (`upgrade_to_level(N)` + `stats()->set_level` + `update_derived_stats`, the `headless_server_runtime.cpp:140-155` shape); `runtime_only_lua.txt` digest in the same commit if `kTestRegistrationLua` changes. Direct unit tests for EVERY new function incl. fallback arms (§8 matrix), plus the heart-value oracle test and the model-pin test. | `campaigns/modes/packs/modes.core/lib/mode_match.lua`, `.../mode_anchors.lua` (if signature grows), `tests/modes_pack_fixture.h`, `scripts/coverage/runtime_only_lua.txt`, new/extended cases in `tests/unit/test_modes_tdm.cpp` + `tests/unit/test_modes_mutant.cpp` |
| **WP-F system-tests** | fable | WP-E | Behavioral wave: solo-L5-roster headline (opponent squad hits stored target ±15%); E3 no-op oblist sweep; TROOPS:OWN composition (E6); Matched-mask == Auto-mask twin worlds; I5 revive extensions (soccer/basketball/CTF — assert replacement `s_level == matched level`, never object identity); E4/D24 wiped-human-team backstop arm; §7 announce pins (normal + LIMIT); E13 low-end clamp case; run-twice census determinism (mode vars byte-equal); mirror-harness twins with matched bots (zero hash strikes); instruction-budget headroom check; I4 networked-ordering test (save = 5 -> `world.ctf_requested_team_count == 5` before first tick; a mode var written by `on_mode_init` proves Lua saw it). | `tests/unit/test_modes_tdm.cpp`, `tests/unit/test_modes_mutant.cpp`, `tests/unit/test_modes_soccer.cpp`, `tests/unit/test_modes_basketball.cpp`, `tests/unit/test_modes_ctf.cpp`, headless-runtime ordering test file |
| **WP-G docs** | opus | WP-B | `docs/modding/og-api.d.lua:548` sentinel note; `docs/game-modes.md` / `docs/mp-game-modes.md` Matched section + Onslaught exclusion note (D17); text Matchup dump `"Teams: Match"` assert (shared-formatter one-liner); record the curses stale-title no-change decision. | `docs/modding/og-api.d.lua`, `docs/game-modes.md`, `docs/mp-game-modes.md`, text-picker dump test site |
| **WP-H gate-sweep** | opus | all | Full verification: `ctest --preset ci-test` clean; `check_mutation_pins.py` clean; Lua coverage func = 100 on all changed pack Lua (local `OPENGLAD_LUA_COVERAGE` check); luals + statement-per-line; confirm no `recorder_processes.txt` churn (no new binary) and the `runtime_only_lua.txt` digest matches; confirm parity goldens untouched (`og_test_parity`). | none (verification only; fixes route back to the owning WP) |

---

## Test plan

### Function-coverage matrix (func = 100 gate — every arm needs a direct hit)

| New/changed function | File | Arms and their direct tests |
|---|---|---|
| `core.team_count_request()` | mode_core.lua | raw <= 0 (Auto) / raw 2..4 / raw == 5 — WP-D helper tests; constant-equality pin in `test_mode_bindings` |
| `core.activate_teams` (changed) | mode_core.lua | existing activation tests + Matched-mask == Auto-mask twin (WP-F) |
| `match.walker_power(w)` | mode_match.lua | normal walker; zero-offense floor (`+60` arm) — WP-E |
| `match.predicted_power(family, base, L)` | mode_match.lua | tuple row; default row; mage half-armor arm — WP-E |
| `match.census_power(obs)` | mode_match.lua | no humans (T=0); one team; multiple teams (mean); has_guy-guard skip — WP-E |
| `match.solve_matched_levels(T, bases)` | mode_match.lua | interior solve with tie-low; T < B(1) clamp; T > B(9) clamp (k=0 at L=9); n=1 mutant case — WP-E |
| `match.bot_level_for(team, index)` | mode_match.lua | matched arm (L and L+1 members); unmatched arm (vars 0 -> legacy) — WP-E |
| `match.spawn_bots` (changed) | mode_match.lua | matched-level arm; D24 measure-and-solve-now arm; legacy-formula arm (byte-identical to today) — WP-E |
| matched announce arm | mode impls via shared helper | `"TEAMS MATCHED"` normal; `"TEAMS MATCHED (LIMIT)"` clamped — WP-F pins both (§7) |
| CTF fill/respawn call sites (changed) | mode_ctf_impl.lua | existing CTF fill + `dead_bot_respawns_as_replacement` re-run — WP-A/WP-E |
| per-mode fallback sites (changed) | soccer/basketball/onslaught impls | existing `row.teams` fallback tests re-run + helper-arm tests — WP-D |
| `cycle_ctf_team_count` / `format_ctf_teams_label` / `sanitize_settings` / `effective_team_mask` (changed C++) | picker_common.cpp, lobby_server.cpp, mode_tick.cpp | re-pinned unit tests §2.7 — WP-B (existing functions; C++ func gate already satisfied, line coverage via the new value cases) |

### Suites by binary

- **og_unit_families**: cycler/formatter re-pins; lobby_server sanitize adds.
- **og_test_menu_ui**: unchanged run = no-layout-movement proof.
- **og_test_matchup**: new injector flow (4 -> Match -> Auto, both surfaces).
- **curses**: save=5 render; **text**: Matchup dump assert (WP-G).
- **og_unit_mode**: sentinel read-back + constant pin; snapshot suite untouched (D7).
- **og_unit_modes / og_unit_soccer**: all WP-E/WP-F census, solver, no-op,
  OWN, revive, mirror, determinism, budget cases.
- **og_test_picker**: Matched seat-behavior variant.
- **og_test_parity**: untouched goldens (modes Lua is parity-invisible;
  `mode_tick.cpp` is unpinned) — run as proof, plus the pin checker.

### Oracle and model-pin tests (the two calibration tripwires)

- **Oracle (C++, modes test file):** build a guy ladder (L1 squad, L4 roster,
  L9 archmage), compute `query_heart_value()` directly and f side by side;
  assert TEAM-level rank agreement. Guards the f weights.
- **Model-pin:** spawn one bot per TUPLE row (the 5 squad families plus the
  orc/beast/cleric/druid rows carried for future rosters — 9 families),
  `set_difficulty(L)` for L ∈ {1,3,5}, assert measured f within ±10% of
  `pred(L)`. Converts silent family-rebalance drift of the copied tuple
  table — and any misreading of the `living::set_difficulty` override
  (D13) — into a red test on day one, for every priced family.

---

## Risk register

| # | Risk | Likelihood / impact | Mitigation |
|---|---|---|---|
| R1 | `living::set_difficulty` override reading wrong (scout conflict D13) | low / high | Measured-base design + model-pin test reds immediately; no guessed constants ship. |
| R2 | Core-pack family rebalance drifts the copied tuple table | medium / medium | Model-pin test (±10%); tuple table carries a comment pointing at the pack files. |
| R3 | f mispricing (mage utility, specials) skews matches | medium / low | Accepted limitation (D9/D10); oracle test pins team-level rank order; weights retunable in one place. |
| R4 | Init-cost regression trips an instruction-budget probe | low / medium | Census is one oblist pass + <= 14 integer evaluations; WP-F measures headroom; budgets are never bumped (memory: measure -> fix, never inflate). |
| R5 | Mode-var slot collision with per-mode slots or fixture pins | medium / medium | WP-E assigns slots per mode with an explicit cross-check against `tests/modes_pack_fixture.h` pinned slots; collision shows as a red mode test immediately. |
| R6 | Seat re-resolution or Base Camp chips re-derive the mask locally and miss the sentinel | medium / medium | Explicit WP-B verification item (§3.1); the E11 seat test and the twin-mask test would catch a miss. |
| R7 | `kTestRegistrationLua` byte change without the digest update | medium / low | Same-commit rule stated in WP-E; the coverage gate fails loudly either way (both stale and rotten digests are errors). |
| R8 | Old-build cosmetic `"Teams: 5"` confuses mixed-build lobbies | low / low | Accepted (D8); documented fail-soft table §2.6; gameplay host-authoritative. |
| R9 | The 12 == 12 label budget blocks a future rename | low / low | `"Teams: Even"` recorded as the fallback (D3). |
| R10 | Accidental pin shift from C++ edits | low / high | Touched C++ files (`picker_common.cpp`, `lobby_server.cpp`, `mode_tick.cpp`, headers) carry no pins; `check_mutation_pins.py` runs in every WP touching `src/` regardless (the toast-commit lesson: any sim-file insert needs a full pin-map check). |
| R11 | CTF re-point changes CTF bot behavior subtly (place_at_anchor arg bridge) | low / medium | WP-A acceptance = byte-identical families/levels + existing CTF fill/respawn tests green before any matched logic lands on top. |

---

## Appendix: verified anchors

All verified by the three fact scouts at 429ec46e (see also their caveats).

**Control surface**
- Cycler `src/interface/ui/picker_common.cpp:527-537`; formatter `:1702-1707`;
  stale header comments `include/openglad/interface/ui/picker_common.h:249,723`.
- Sentinel field homes: `save_data.h:77` (short, `0 = Auto`),
  `lobby_state.h:193` (i16), `lobby_server.h:19`, `game_world.h:430` (short).
- Sanitize `src/gameplay/lobby_server.cpp:70-78` (`<= 0 -> 0`, `> 0 ->
  [2,4]`); solo picker round-trips through it via `LocalPickerLobbyClient`.
- Label budget 12 chars / 80px; longest today 11; enforcement pin
  `tests/unit/test_picker_common.cpp:1330`.
- Next free ButtonAction = 103 (`ToggleInfiniteGold = 102`,
  `button.h:350`) — deliberately NOT consumed by this design.
- Host authority: non-host `SettingsChange` dropped
  (`lobby_server.cpp:1077-1085`); guest echo overwrite
  (`picker_lobby_network_client.cpp:1635`); settings change clears non-host
  readies and re-resolves seats (`lobby_server.cpp:1087-1211`).

**Plumbing and ordering**
- SaveData -> world: `screen.cpp:1240-1247` and the REQUIRED twin
  `headless_server_runtime.cpp:86-89`; carriers: `level_runtime_data.cpp:286-290,
  636-640`, `replay.cpp:480-483`, `world_snapshot.cpp:775,784,2340,2381,2970`,
  `glad_gameplay.cpp:101` — all already carry `ctf_team_count`; none change.
- Init order (authority): `headless_server_runtime.cpp:394-465`; SDL twin
  `game.cpp:102/127/140-142/161`. `on_mode_init` fires from `mode_run_tick`
  (`mode_tick.cpp:136-157`) after `respawn_scan_anchors`.
- `og.match_setting` (`bindings_entity.cpp:2402-2423`); `effective_team_mask`
  (`mode_tick.cpp:63`); Lua copy `mode_core.lua:88`.
- GTL: writer v14 (`save_data.cpp:841`); `ctf_team_count` in the v10 block
  (`:558-575`). Protocol: `kNetworkProtocolVersion = 12`
  (`net_transport.h:127`).

**Spawn seam and modes**
- Single spawner `mode_match.lua:216-231`; squad `mode_anchors.lua:6`; CTF
  duplicate `mode_ctf_impl.lua:699-712` (squad `:67`, 3-arg `place_at_anchor`
  `:272`, cursor slot 13 pinned at `tests/modes_pack_fixture.h:141`).
- Fill sites: CTF `:884`, TDM `:119`, mutant `:562` (one per seat), soccer
  `:881`, basketball `:2093`. Onslaught: NONE (generators,
  `mode_onslaught_impl.lua:222-247, 753-761`).
- Revive backstops: `mode_match.lua:330-376` (`revive_wiped_teams`), callers
  soccer `:158`, basketball `:489`; spawns only for active teams with zero
  live Livings AND zero pending revives (`:367-375`).
- Activation: `match.activation` + `match.fills` (`lib/mode_match.lua`),
  consumed by each mode's in-body `decide` fold at the top of its
  `on_mode_init` over `match.census_inputs()` (the D42 retirement; the
  former `census_mask`/`own_roster_activation` are deleted; 2026-08-18:
  the solo/roster arms are gone — one backfill rule, request = explicit N
  or the authored count at Auto; see the D26 addendum).
- Nothing outside the modes framework fills empty teams;
  `matchup: versus` is unique to `campaigns/modes/campaign.yaml:6`.

**Power model**
- `guy::query_heart_value` guy.cpp:289-328 (curve `value^1.85` fixed point,
  `:168-191`); guy-derived stats guy.cpp:435-465/470-557; level-up gains
  `family_descriptor.h:87`; `calculate_exp` guy.cpp:345-382.
- `living::set_difficulty` living.cpp:612-657 (family hook `:620`, default
  `:627-630`, percent `:634-657` — scales team != 0 AND team-0-without-myguy
  (A12a); only myguy carriers exempt); `apply_difficulty_scaling`
  semantics guy.cpp:393-401; projectile multiplier walker.cpp:1186; base
  `walker::set_difficulty` (generators, level arg unused for Livings).
- Difficulty percent table {50,100,200} (`picker_common.cpp:43`,
  `headless_server_runtime.cpp:25-38`); today's bot formula
  `difficulty//100 + 1` -> L1/L2/L3.
- Lua-readable stats: walker methods `bindings_entity.cpp:2596-2687`, stats
  `:2689-2736`, guy accessors `:2738-2756` (guard: `guy_arg` raises at
  `:95-104`). `g_level` read-only; write verb `g_upgrade_to_level`.

**Pins and gates**
- Mutation pins: nine `src/` files (walker.cpp max 2285, walker_combat.cpp
  322, gloader.cpp 822, weap.cpp 142, game_world.cpp 1803,
  walker_movement.cpp 173, effect.cpp 96, living.cpp 124, save_data.cpp 124).
  `bindings_entity.cpp`, `guy.cpp`, `combat_math.cpp`, `mode_tick.cpp`,
  `picker_common.cpp`, `lobby_server.cpp`, and ALL of `campaigns/` are
  pin-free.
- Parity: harness runs at difficulty 100 (percent branches dead); modes Lua
  parity-invisible; `beast_set_difficulty_invariant_scen99` pins the golem
  tuple (default-formula changes CAN move generator-bearing goldens — this
  design changes no default formula).
- Coverage: line >= 95 / func = 100 on src/ alone, pack Lua alone, and the
  union; statement-per-line + luals gates; `runtime_only_lua.txt` digest for
  `kTestRegistrationLua`; `recorder_processes.txt` set equality (no new
  binary needed).
- Instruction budgets: tdm `:1085-1109`, items `:406-440`, onslaught
  `:1727-1744` (500k per probe, 5M per host entry).
- Menu/wire pin inventory: §2.7 of this document (distilled from the lobby-ui
  scout §3, which is the exhaustive source).

---

# Amendment: control moved to TROOPS (2026-08-09, branch @ 30fb2664)

User correction, verbatim: *"TEAMS: was the wrong place for MATCH! It should
be TROOPS: MATCH (in scenario), not TEAMS: MATCH (in seats)."* The
matched-power MACHINERY (census, solver, spawn seam, announces — PARTs
III-IV, D9-D24) is correct and stays. Only the CONTROL moves: the Teams
cycler returns to `Auto -> 2 -> 3 -> 4 -> Auto`, and matching becomes the
third value of the SCENARIO screen's Troops cycler. All anchors below were
re-verified at 30fb2664 (the branch has WP-A..WP-H fully landed, so the
"restoration" rows name real code, not spec intentions).

## A.0 Amendment decision record (D25-D33 — binding; supersedes D1-D8 as marked)

| # | Decision | Ruling & rationale |
|---|----------|--------------------|
| D25 | **Control = a third VALUE on the existing Troops cycler (SCENARIO screen); the Teams cycler is fully restored** | Supersedes D1. TROOPS is the "what fights" knob: ALL = the authored cast fights, OWN = only deployed rosters fight, MATCH = deployed rosters plus generated comparative-power opposition. That family is where matching belongs; team COUNT stays a pure count. Same cost shape as D1: no new row, no ButtonAction (`CycleCtfScenarioTroops = 64`, `button.h:278`, reused), no `PickerMenuCommand` (`ToggleCtfScenarioTroops`, `menu_model.h:55`, reused), no protocol/GTL bump. |
| D26 | **[AMENDED by D39 — the one delta is now power AND headcount]** **THE BUNDLE RULING — TROOPS:MATCH is OWN-like for occupied teams. Invariant: MATCH differs from OWN in exactly one way — generated squads spawn at matched power instead of the difficulty formula.** Everything else — the strip of every authored non-guy Living/generator, `own_roster_activation`'s mask replacement, fill sites, revive backstops — is byte-identical to TROOPS:OWN. | **USER CONFIRMED 2026-08-09** (the veto reserved by the first draft of this row was not exercised). Rationale from the user's own framing ("generate a comparative-power team to my own team(s)"): the census measures deployed ROSTER walkers (`has_guy`, mode_match.lua:266-276); an authored cast is neither "my team" nor a generated one, and a team occupied by authored troops at authored levels is opposition the solver neither measures nor rebalances — keeping it defeats "comparative power". Mechanically the ruling is free: every existing consumer already reads any value above KEEP as strip-on — `format_ctf_troops_label` `> 0` (picker_common.cpp:1730), the Matchup strip preview `> 0` (:2695), the classic-map latch `>= 1` (game_world.cpp:1903), and both Lua gates `<= KEEP` (mode_strip.lua:61, mode_match.lua:342) — so MATCH=3 inherits OWN's whole deployment policy with ZERO consumer edits; a KEEP-like ruling would instead need a `== OWN` rewrite at every one of those sites. **Rejected reading (KEEP-like):** keep the authored cast and matched-fill only authored-empty teams. **Expressiveness lost vs the old TEAMS:Match x TROOPS:{ALL,OWN} matrix:** (i) the (Match, ALL) cell — matched fills coexisting with an authored cast — is gone; (ii) the multi-matched-squad pile-on is gone: under OWN's roster activation (mode_match.lua:336-, "two or more roster teams -> exactly those teams") a >= 2-roster group gets pure OWN with no bots at all, and a solo roster gets exactly ONE matched opponent (the E6 flagship), never "every empty authored team gets a T-strength squad". Loss (i) is judged marginal (unmatched authored power on the board contradicts the feature); loss (ii) is judged an improvement (the old design fielded one T-strength squad PER empty team against a lone human — an unruled pile-on). If the veto lands, the fallback is KEEP-like with the consumer-rewrite cost above. **AMENDED 2026-08-17 (issue #218, maintainer-directed):** D26's loss-(ii) ruling adjudicated TEAMS: Auto only. An explicit lobby TEAMS count now wins the team COUNT under OWN/FAIR — roster teams first, authored backfill in index order, max semantics (a deployed side is never stripped to satisfy a count); composition still per-TROOPS (OWN = legacy squads, FAIR = matched). Auto behavior unchanged (`own_roster_activation`, mode_match.lua; C++ preview twin `og::sim::roster_effective_team_mask`). **AMENDED 2026-08-18 (maintainer directive, supersedes D26's Auto scope entirely):** "TEAMS: Auto means 'as many teams as the map actually has'. Under TROOPS: OWN/FAIR, Auto must resolve to the level's AUTHORED team count and behave exactly like an explicit TEAMS: N with N = that count — roster teams always stay, authored non-roster teams backfill in index order, composition per the TROOPS setting, identical to the explicit-count machinery PR #245 already shipped and tested." This matches the zero-sentinel convention every other match-setup knob uses (0 = the map's own value). Consequence: at Auto the OWN/FAIR active mask is always the full authored mask, and D26's loss-(ii) "no pile-on" shape no longer exists at ANY TEAMS value — the roster+1 solo arm and the exactly-the-rosters arm are gone from `own_roster_activation` and `og::sim::roster_effective_team_mask` (one rule: request = explicit N or authored count). On 2-team maps with one roster this is shape-identical to the old behavior; differences appear only where the authored count exceeds the old roster+1 shape (FOURSQUARE 822, the 4-team basketball court, 3-4 team TDM/CTF/onslaught rows). **Review completion (2026-08-18, #245):** the first landing left the ALL-BOT arm (zero rosters anywhere) on the old shape — soccer/basketball/onslaught still substituted the `row.teams` manifest default at Auto inside the directive's OWN/FAIR scope, diverging from the C++ preview twin on any hand-authored map whose manifest count differs from its authored count (identical on every shipped map). `own_roster_activation` now runs the same one-rule arm from an empty roster (nil = TROOPS: ALL only, where the manifest default legitimately stands); the C++ twin's `roster == 0` fallback already produced exactly this mask. Pinned per mode: `all_bot_own_and_fair_auto_field_every_authored_team` (onslaught), `all_bot_own_auto_matches_the_explicit_count_shape` (soccer + basketball). |
| D27 | **Sentinel = `kTroopsMatched = 3` on `ctf_strip_scenario_troops`; troops sanitize widens `> 2` to `> 3`; `kTeamCountMatched` is DELETED** | The field's value set today: `0` = KEEP ("TROOPS: ALL"), `2` = OWN ("TROOPS: OWN"), `1` = the RETIRED middle state — accepted by sanitize and read as OWN everywhere (lobby_server.cpp:93-100, picker_common.cpp:577-581, mode_strip.lua:9-13). `1` is therefore NOT reusable: legacy saves/peers still hand it over meaning OWN. `3` was never legal, collides with nothing, and — because every consumer reads `> 0`/`>= 1`/`> KEEP` as strip-on — degrades on old builds to exactly OWN, which under D26 is MATCH minus power: the best possible degrade. Sanitize for THIS field is fallback-revert, not clamp (`< 0 \|\| > 2 -> fallback.ctf_strip_scenario_troops`, lobby_server.cpp:96-99); the amendment widens it to `< 0 \|\| > 3` so exactly `3` passes both directions (host publish keeps 3 -> `broadcast_state` -> guest adopt `picker_lobby_network_client.cpp:1638`; the solo `LocalPickerLobbyClient` round-trip returns 3), `1` stays accepted-as-legacy-OWN, `4+` still reverts. The constant lives in `lobby_state.h` next to the `:213` field; `kTeamCountMatched` (`lobby_state.h:196`) is deleted outright — after restoration nothing references it, and a kept-deprecated constant invites accidental reuse of 5 on the wrong field. |
| D28 | **Label = `"TROOPS: FAIR"` (12 chars — exact budget fit; USER-DECIDED 2026-08-09, `"TROOPS: EVEN"` is the recorded alternate); cycle `ALL(0) -> OWN(2) -> FAIR(3) -> ALL(0)`; junk `1 -> 0` preserved** | `"TROOPS: MATCH"` and `"Troops: Match"` are 13 chars — one over the SCENARIO-face budget (80px = 12 chars, stated at picker_common.cpp:1726-1727; both existing labels are 11). Dropping the space (`"TROOPS:MATCH"`, 12) breaks the two-sibling `"TROOPS: "` prefix and reads as a typo — rejected. The user chose `"Fair"` over this row's earlier `"Even"` draft ("Even" stays the recorded alternate if the name is ever revisited); the word is rendered in the troops formatter's existing all-caps convention. MATCH stays the internal name (`kTroopsMatched`, `core.MATCHED_TROOPS`, announce strings unchanged). Cycle: `next_ctf_scenario_troops` (picker_common.cpp:575-581) becomes `0 -> 2; 2 -> 3; else -> 0` — the first-step pin (ALL -> OWN, test_ctf_ui.cpp:254) survives verbatim; the wrap pin (OWN -> ALL, :268) moves to OWN -> FAIR -> ALL; junk `1 -> 0` and `9 -> 0` keep (the `else` arm). Formatter: the `== kTroopsMatched -> "TROOPS: FAIR"` branch must be inserted BEFORE the `> 0` OWN branch (picker_common.cpp:1730) or OWN eats it. |
| D29 | **Arming rewire = ONE Lua edit: `core.team_count_request()` keeps its `(count, matched)` signature; `matched` is now `og.match_setting("strip_troops") == core.MATCHED_TROOPS`; `normalize_team_count` drops the sentinel arm** | The whole downstream machinery is trigger-agnostic by construction: `record_match_target` (mode_match.lua:266-276) is the ONLY consumer of the `matched` boolean, and everything after it — TARGET latch, plan, solver, `spawn_matched_bots`, `bot_level_for`, `announce_matched` — keys off mode vars alone (mode_match.lua:460-533). Keeping the helper's name and signature means the three first-return call sites (soccer_impl:782, onslaught_impl:678, basketball_impl:2253) and the census (mode_match.lua:267) need zero edits. `core.MATCHED_TEAM_COUNT` (mode_core.lua:87) becomes `core.MATCHED_TROOPS = 3`, constant-pinned against `og::sim::kTroopsMatched` via the existing `og.match_setting("strip_troops")` round-trip idiom. Under MATCH the `own_roster_activation` early return (mode_match.lua:342, `<= strip.KEEP`) never fires — matched implies strip-on — so census placement (before the strip, strip-invariant, E6) is unchanged. C++ `effective_team_mask` loses its matched clause (mode_tick.cpp:69-72 reverts to `requested <= 0`): the strip field feeds no mask anywhere, so lobby seat re-resolution, Base Camp chips, and client mask fallbacks are back to master shape — and cycling TROOPS moves no seats, ever (already true for ALL <-> OWN; the E11 test re-targets to assert exactly that). |
| D30 | **MIGRATION — the playtest `.gtl` `ctf_team_count = 5` takes the simple heal: sanitize clamps `5 -> 4` (or a manual cycle wraps `5 -> 0`), matching OFF. One-time, documented, no remap.** | The load path itself never sanitizes (the v10 block reads any short, save_data.cpp:558-575), but no real flow reaches gameplay without a sanitize round-trip: the solo picker round-trips through `LocalPickerLobbyClient` and every lobby publishes through `sanitize_settings`, so the restored plain clamp (`> 0 -> [2,4]`) heals `5 -> 4` before the world ever sees it; the restored cycler wrap (`>= 4 -> 0`) heals it on the first manual touch; and even an unhealed 5 reaching Lua is soft (`activate_teams` clamps to 4, `matched` is false). The fancy remap — load-time `temp_ctf_teams == 5 -> {team_count = 0, strip = 3}` — was priced: ~4 lines plus a save-version story to distinguish "5 written by the playtest build" from hand-edited junk (writer stays v14, save_data.cpp:841, so there is no version signal), a permanent cross-field coupling at load, and its beneficiaries are exclusively this developer's own playtest saves (PR #190 never merged; no user save can contain 5). Rejected. GTL round-trip of troops `= 3` gets its own value case instead. |
| D31 | **Mixed-build / downgrade table redone for the TROOPS field — every shape fails soft, and strictly better than the D8/§2.6 Teams table** | Exact table in §A.2. Headline: an old build reading `strip = 3` — from a `.gtl` (v11 block reads any short, save_data.cpp:578-588, no load sanitize) or from a same-protocol settings broadcast (`read_lobby_settings`, net_transport.cpp:373, plain i16) — plays it as TROOPS:OWN and LABELS it "TROOPS: OWN": semantically honest (the host really is stripping), unlike the old cosmetic-junk `"Teams: 5"`. Old HOST: can never produce 3; sanitize reverts an inherited 3 to its fallback value at publish. No bump anywhere: protocol, GTL writer, snapshot (`world_snapshot.h:252` already carries the field, replicated at world_snapshot.cpp:778/790/2343/2384/2973), replay compare (replay.cpp:483) all untouched. |
| D32 | **Classic (non-scripted) maps: TROOPS:MATCH behaves exactly as TROOPS:OWN — strip, no matched fill** | The classic latch `ctf_requested_strip_scenario_troops >= 1` (game_world.cpp:1903) already admits 3, and matched fill exists only in mode Lua (modes campaign). This is the D26 invariant applied where no generation seam exists: the "generated squads" delta is vacuous, so MATCH degenerates to OWN. No code change at that site (comment note only — and only BELOW game_world.cpp's max mutation pin at :1803, so no pin shifts; `check_mutation_pins.py` runs regardless). Documented in `og-api.d.lua` alongside the sentinel. |
| D33 | **Test-delta rules: the twin-mask oracle re-targets from Auto to OWN; behavior tests re-arm via the strip field through one fixture helper; premise re-adjudication is mandatory wherever a fixture authors non-guy walkers** | Full ledger in §A.4. The load-bearing changes of meaning: (a) the twin invariant is now "MATCH's masks == OWN's masks for every team-count value" — NOT "matched == plain-N", because MATCH inherits OWN's roster activation, which differs from TROOPS:ALL masks in the solo case; the task-level statement "matched-with-N-teams equals plain-N-teams" holds only in the zero-roster (nil-activation) shape; (b) arming MATCH in a fixture now ALSO strips and roster-activates, so any matched test whose world contains authored non-guy walkers, or which asserted multi-team fills under a solo roster, has a changed premise and must be re-adjudicated, not mechanically re-armed; (c) a `modes_pack_fixture.h` `arm_matched()` helper centralizes the trigger so a future move is one line. |

## A.1 The Troops control surface today (restoration baseline, verified at 30fb2664)

- **Field:** `SaveData::ctf_strip_scenario_troops` (`save_data.h:80`, short,
  `0 = keep`), `LobbySettings::ctf_strip_scenario_troops`
  (`lobby_state.h:213`, i16), `lobby_server.h:22`, world mirror
  `GameWorld::ctf_requested_strip_scenario_troops` (`game_world.h:433`),
  snapshot `world_snapshot.h:252`. Lua: `og.match_setting("strip_troops")`
  (`bindings_entity.cpp:2416-2417`).
- **Value set:** `0` KEEP / `2` OWN / `1` retired-middle-state
  (accepted, read as OWN); constants `strip.KEEP = 0`, `strip.OWN = 2`
  (mode_strip.lua:12-13). After the amendment: `3` = MATCH.
- **Cycler/formatter:** `next_ctf_scenario_troops` /
  `toggle_ctf_scenario_troops` (picker_common.cpp:575-587; header
  picker_common.h:274-285), `format_ctf_troops_label`
  (picker_common.cpp:1725-1732; header :731-733).
- **Three clients, zero new handlers:** SDL `change_ctf_troops`
  (picker.cpp:2914-2927; button.cpp:631-632) refreshes the SCENARIO row
  label, lobby-syncs, autosaves; text `text_picker.cpp:833-837` and curses
  `curses_picker_client.cpp:1039-1044` toggle + print the shared formatter
  (curses screen title "Scenario Troops" here; the stale "CTF Teams" title
  at :1030 belongs to the TEAMS command and stays recorded-not-fixed).
  Menu-model rows: `{"ctf_troops", ...}` menu_model.cpp:57 and
  `{"troops", ...}` :73; binding label `menu_binding.cpp:61-62`.
- **Button homes:** SCENARIO screen `{"troops", "TROOPS: ALL", 120,140,80,15}`
  (menu_screen_specs.cpp:1291-1295; static label also pinned at
  test_menu_layout.cpp:1400); MATCHUP's `ctf_troops` row is dormant-hidden
  (menu_screen_specs.cpp:1419-1423, picker_team_build.cpp:469, :789).
  **(#218: the SDL MATCHUP screen is deleted — that dormant row went with
  it, and Match Teams / Score Limit joined TROOPS on the SCENARIO y=140
  band.)**
- **Host gating:** hidden for non-hosts with SET CAMPAIGN / SET LEVEL
  (`sync_scenario_menu_host_control_visibility`,
  picker_team_build.cpp:323-341, nav re-target :312-319); label re-derived
  from the lobby-synced save every frame (:338-341) so joiners see the
  host's value. Non-host `SettingsChange` dropped server-side
  (lobby_server.cpp:1077-1085) — shape unchanged (D19 stands).
- **Replication/persistence:** lobby settings i16
  (net_transport.cpp:353/:373; field since protocol v3, net_transport.h:55);
  GTL v11 read block (save_data.cpp:578-588) / writer (:1141-1143), writer
  version 14 (:841); carriers `screen.cpp:1247`,
  `headless_server_runtime.cpp:89/:307/:351`, `glad_gameplay.cpp:104`,
  `level_runtime_data.cpp:289/:639`, curses_network.cpp:266/:356,
  picker_lobby_client.cpp:684/:885, picker_lobby_network_client.cpp:1584/
  :1638/:1949, lobby_server.cpp:1342 — all plain copies; value 3 rides.
- **Sanitize:** lobby_server.cpp:93-100, fallback-revert outside `[0,2]`
  (amendment: `[0,3]`).
- **Sim consumers:** classic latch game_world.cpp:1903 (`>= 1`); scripted
  strip mode_strip.lua:61 (`<= KEEP`); activation gate mode_match.lua:342;
  Matchup preview picker_common.cpp:2690-2695 + removal footer :2883.
  The removal footer names the chosen mode on NEW builds — `"! REMOVED:
  TROOPS FAIR"` via `ScenarioRosterReport.strip_is_fair` — while the strip
  MECHANISM stays byte-identical to OWN (D26's one-delta rule holds; only
  the label differs). Old builds render OWN for value 3 (§A.2 below).

## A.2 Downgrade / mixed-build behavior for `strip = 3` (replaces §2.6's role)

| Shape | What happens |
|---|---|
| Old build loads a new `.gtl` with `strip = 3` | v11 block reads any short (save_data.cpp:578-588), no load sanitize. Every old consumer reads `> 0`/`>= 1` as strip-on: plays as TROOPS:OWN, labels `"TROOPS: OWN"` — semantically honest (MATCH minus power). First lobby publish: old sanitize `3 > 2 -> fallback` reverts to the previous value; first manual cycle: old toggle (`!= 0 -> 0`) lands on ALL. Self-heals; no crash, no corruption. |
| Old GUEST in a new host's lobby (same protocol) | Adopts 3 from the settings broadcast (plain i16), renders `"TROOPS: OWN"` — correct in substance: the host IS stripping. Gameplay host-authoritative; guests apply snapshots. No desync. |
| Old HOST | Can never produce 3; sanitizes an inherited 3 to its fallback at publish; guests see the fallback label. |
| New build, old `.gtl`/peer hands over `1` | Unchanged legacy path: accepted, read as OWN (never as MATCH — D27 refuses to reuse 1). |
| New-build guest under new host | Trivial; formatter shows `"TROOPS: FAIR"` on all three clients via the one shared formatter. |
| Teams-field residue (`ctf_team_count = 5` from the playtest build) | D30: heals to 4 at first sanitize or 0 at first cycle; matching OFF; old `"Teams: 5"` cosmetic rendering can appear once, transiently. |

## A.3 Restoration ledger — every site that reverts, moves, or appears

**C++ (all in pin-free files except the game_world.cpp comment, which sits
below its max pin):**

| Site | Change |
|---|---|
| `lobby_state.h:196` | DELETE `kTeamCountMatched`; ADD `inline constexpr std::int16_t kTroopsMatched = 3;` next to the `:213` field |
| `lobby_state.h:204`, `lobby_server.h:19`, `save_data.h:77` | field comments revert to plain `0 = Auto` |
| `lobby_state.h:213`, `lobby_server.h:22`, `save_data.h:80`, `game_world.h:433`, `world_snapshot.h:252` | field comments gain `3 = Match (kTroopsMatched)` |
| `picker_common.cpp:528-541` | `cycle_ctf_team_count` reverts to `Auto -> 2 -> 3 -> 4 -> Auto` (`>= 4 -> 0` wrap); comment reverts |
| `picker_common.cpp:1707-1716` | `format_ctf_teams_label` drops the Match branch |
| `picker_common.cpp:575-587` | `next_ctf_scenario_troops` gains the third state (`0 -> 2; 2 -> 3; else -> 0`) |
| `picker_common.cpp:1725-1732` | `format_ctf_troops_label` gains `== og::sim::kTroopsMatched -> "TROOPS: FAIR"` BEFORE the `> 0` branch |
| `picker_common.h:249-250` | teams cycler comment reverts; `:274-285`/`:731-733` troops comments document the third state |
| `lobby_server.cpp:70-80` | teams sanitize reverts to the plain `> 0 -> clamp[2,4]` / `<= 0 -> 0` pair |
| `lobby_server.cpp:93-100` | troops sanitize widens to `< 0 \|\| > 3 -> fallback` |
| `mode_tick.cpp:64-72` | `effective_team_mask` drops the `== kTeamCountMatched` clause (back to `<= 0`) |
| `game_world.cpp:1898-1903` | comment note only: `>= 1` intentionally admits 3 (D32); NO code change; below max pin 1803 |

**Lua:**

| Site | Change |
|---|---|
| `mode_core.lua:87` | `MATCHED_TEAM_COUNT = 5` becomes `MATCHED_TROOPS = 3` (export renamed, :192) |
| `mode_core.lua:94-103` | `normalize_team_count` drops the sentinel arm (`raw <= 0 -> 0`; else raw); `matched` sourced from `og.match_setting("strip_troops") == MATCHED_TROOPS` |
| `mode_core.lua:106-108` | `team_count_request()` keeps name and `(count, matched)` signature (D29) — call sites soccer:782 / onslaught:678 / basketball:2253 / mode_match:267 untouched |
| `mode_match.lua:260-276, 318-342` | comments only: census self-gates on the TROOPS request; the `<= strip.KEEP` early return never fires under MATCH |
| `mode_strip.lua:6-13` | comment documents the third state (MATCH strips identically to OWN) |
| `docs/modding/og-api.d.lua:548` | `(team_count 5 = Matched)` becomes `(strip_troops: 0 keep, 2 own, 3 match; 1 legacy = own)` |

## A.4 Test delta (descends from §2.7 and the WP-C/WP-F waves)

**Reverts / re-targets (control-surface pins):**

| Site | Change |
|---|---|
| `test_picker_common.cpp:1275-1294` | cycle pin reverts: `4 -> 0` wrap (the `:1289-1290` Match step goes); keep `9 -> 0`, `1 -> 2` |
| `test_picker_common.cpp:1333-1347` | `format(5) == "Teams: Match"` case removed; ADD troops-formatter cases: `format(3) == "TROOPS: FAIR"` with its own `<= 12` budget + literal-equality pin, `format(1) == "TROOPS: OWN"` kept (test_ctf_ui.cpp:280-283 idiom) |
| `test_menu_spec.cpp:120` | teams cycle array drops `"Teams: Match"`; a troops cycle array gains `{"TROOPS: OWN","TROOPS: FAIR","TROOPS: ALL"}` |
| `test_lobby_server.cpp:439-500` | `5` passes sanitize -> `5 -> 4` clamps; `equivalent` carries-5 (:472) drops; ADD troops: `3` kept both directions, `4 -> fallback`, `1` kept, non-host `SettingsChange(strip=3)` dropped |
| `test_company.cpp:1637-1657` | GTL `= 5` teams case becomes the troops `= 3` round-trip case |
| `test_ctf_ui.cpp:638, 937-975` | teams 4 -> Match -> Auto injector flow becomes the troops ALL -> OWN -> FAIR -> ALL flow on both live label surfaces; :239-288 first-step pins (0 -> 2) survive, wrap pins move per D28 |
| `test_curses_picker_client.cpp:554-567` | save=5 "Teams: Match" render becomes strip=3 "TROOPS: FAIR" render |
| `text_picker_internal.inc:889-891` | `[Teams: Match]` assert reverts to a numeric/Auto value; the existing troops block (:867-869) gains the `TROOPS: FAIR` print assert |
| `test_picker_funcs.cpp:3058-3115` | E11 re-targets: cycling TROOPS to MATCH re-resolves seats against an UNCHANGED mask — no seat moves (strip feeds no mask) |
| `test_headless_server_runtime.cpp:678-703` | ordering test re-arms: save strip=3 -> `world.ctf_requested_strip_scenario_troops == 3` before first tick |
| `test_mode_bindings.cpp:278-400` | sentinel read-back + constant pin re-target: `og.match_setting("strip_troops")` round-trip, `core.MATCHED_TROOPS == og::sim::kTroopsMatched` |
| `test_save_data_versions.cpp:526-540` | v11 state loop gains state 3; label expectation table gains `"TROOPS: FAIR"` |

**Re-arm (trigger-agnostic machinery tests — census/solver/announce/model-pin/
oracle):** all ~28 `world().ctf_requested_team_count = kTeamCountMatched`
fixture lines across `test_modes_{tdm,mutant,soccer,basketball,ctf,onslaught}.cpp`
and the sites above become `arm_matched()` (new `modes_pack_fixture.h` helper
setting `ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched`).
These tests assert mode vars, spawn levels, announce latches — none read the
trigger — so they survive with the one-line re-arm, SUBJECT to D33(b)
premise re-adjudication: arming now also strips and roster-activates, so any
fixture world containing authored non-guy walkers, or asserting fills on
more than the roster-activation mask, changes meaning and is re-ruled by
hand, not sed.

**Changed-meaning rows:**

- Twin-mask tests (`..._masks_like_auto...`, tdm:1222-1235 helper-arm suite,
  onslaught:400-439 E8): re-target to **MATCH-vs-OWN mask equality** for each
  Teams value (Auto/2/3/4) — D33(a). The old matched-vs-Auto twins are wrong
  under the bundle and must not be mechanically kept.
- E3 (`matched_with_humans_on_every_seat_is_an_auto_noop`): still zero fills
  and TARGET latched > 0, but the world-sweep comparison twin is OWN, not
  ALL — with an all-guy fixture the assertions survive verbatim.
- E6 (`matched_troops_own_solo_roster_gets_a_matched_opponent`): becomes THE
  flagship MATCH test; arming collapses from (team_count=5 + strip=2) to
  (strip=3). The separate "OWN without matching" arm now pins strip=2 spawns
  at legacy levels.
- E8 onslaught: matched request arrives via the strip field; the strip runs
  with `keep_generators` (foundries stay), power ignored, TARGET still
  latched (D17 stands).

**New pins:** troops cycle triple + junk; formatter budget/equality;
sanitize 3-kept/4-fallback/1-kept; GTL troops=3 round-trip; D30 migration
case (v14 `.gtl` with `ctf_team_count = 5` -> sanitize heals to 4, matched
false); D26 one-delta twins (MATCH vs OWN: same masks, same fill sites,
levels differ only where a squad was generated); D32 classic-map arm
(strip=3 fires `classic_strip_authored_troops` — pins the `>= 1` latch).

**Untouched (verified):** `test_menu_layout.cpp:1400` static `"TROOPS: ALL"`
spec label (the budget sweep never walks cycles — §2.7's own finding);
menu-model command resolution / ordinals (no new command); all 5 wire-byte
pins and the offset-41 pin (no protocol change); mutation pins (only
comment edits land in pinned files, below max pins; run
`check_mutation_pins.py` regardless); parity goldens (modes Lua stays
parity-invisible; the strip field was already snapshot/replay-carried);
announce strings `"TEAMS MATCHED"` / `"TEAMS MATCHED (LIMIT)"` (they name
the effect, not the knob); power-model probes 9095/9096 and the whole
§4/§5 machinery test suite bodies.

## A.5 Amendment work packages

| WP | Depends | Scope |
|---|---|---|
| **WP-A1 restore-teams** | — | D25/D27 C++ half: delete `kTeamCountMatched`, revert cycler/formatter/sanitize/`effective_team_mask`, header comments; revert the §A.4 teams-side unit pins (picker_common, menu_spec, lobby_server, company, ctf_ui teams flow, curses, text inc, picker_funcs, headless ordering, mode_bindings) |
| **WP-A2 troops-control** | WP-A1 | D27/D28: `kTroopsMatched`, troops cycler third state, `"TROOPS: FAIR"` formatter branch, troops sanitize widen, new control-surface pins incl. the troops injector flow and save_data_versions state 3 |
| **WP-A3 lua-rewire** | WP-A1 | D29: `core.MATCHED_TROOPS`, `normalize_team_count`/`team_count_request` rewire, comment sweep (mode_core/mode_match/mode_strip), `og-api.d.lua:548`, constant pin |
| **WP-A4 rearm-and-readjudicate** | WP-A2, WP-A3 | D33: `arm_matched()` fixture helper, the ~28-site re-arm, twin re-target to OWN, E3/E6/E8 re-rulings, D26 one-delta twins, D30 migration case, D32 classic-map pin |
| **WP-A5 gate-sweep** | all | `ctest --preset ci-test` clean, `check_mutation_pins.py`, Lua gates on edited pack Lua, parity untouched, docs cross-refs (`docs/game-modes.md` / `docs/mp-game-modes.md` Matched section re-pointed at TROOPS) |

---

# Amendment: matched squads match the human headcount (2026-08-10, branch @ cf02e0f6)

USER DECISION from the PR #190 playtest: a solo fresh soldier under
TROOPS: FAIR faced the full five-bot flat-L1 squad — the documented E13
clamp, working exactly as specified — and the specification is what's wrong.
The ruling: **matched squads never outnumber the roster they were measured
against** — solo = 1v1, duo = 2v2, and the level solve runs within that
size. This supersedes D12 ("squad composition never changes") and rewrites
E13; both rows are marked in place and kept as history. All line anchors
below verified at cf02e0f6.

## B.0 Amendment decision record (D34-D40 — binding)

| # | Decision | Ruling & rationale |
|---|----------|--------------------|
| D34 | **THE HEADCOUNT RULE: `n = clamp(H, 1, #families)`, H latched at census time in `MATCHED.SIZE` (header slot 5). One human team: H = its live `has_guy` headcount. Multiple human teams of differing sizes: H = the MINIMUM of the per-team headcounts.** | H rides the census loop that already sums per-team f (`census_power`, mode_match.lua:163-190) — the same walk gains a per-team count; `record_match_target` (mode_match.lua:267-277) latches SIZE next to TARGET so mid-match spawns never re-census (D15/D24 discipline unchanged). **Why min, not mean:** the guarantee is per-human-team. Init fills under FAIR existed only in the solo-roster shape when this was ruled (2026-08-18: the Auto-resolves-to-authored-count directive backfills empty teams at init whenever the authored count exceeds the roster count — the min rule now guards those init squads too), so multi-team H mattered originally on the D24 backstop path — and that replacement squad spawns to FACE the surviving human teams. Only min keeps every survivor un-outnumbered. Mean (the D11-symmetry candidate) rejected: a 1-human + 3-human lobby would field 2-bot squads against the solo player — the playtest complaint recreated at 2v1. Max rejected outright. The asymmetry vs D11 is deliberate: T averages because power is solvable in level; H mins because headcount is the dimension the solver cannot compensate — that is the whole lesson of the playtest. Rosters >= squad size: n = 5, today's full squad, anchor behavior untouched. **Mutant unaffected:** its per-seat families table is a single-element table (`mode_mutant_impl.lua:562`), so n = min(H, 1) = 1 already — all ten exactly-one-per-seat pins stand. **Accepted residual, stated (adversarial review 2026-08-10):** `MATCHED.SIZE` is latched at init and never re-censused (the D15/D24 no-re-census discipline), so a D24 backstop that fires after post-init permadeath has shrunk the human side can field init-H bots against fewer survivors. Accepted: a mid-match re-census would trade a bounded miss for the latch-once and determinism rules the whole design rests on. |
| D35 | **Composition at small n = the FIRST n of the mode's squad table, in order** (`BOT_SQUAD` soldier, archer, elf, mage, thief — mode_anchors.lua:6; CTF's identical copy mode_ctf_impl.lua:67; TDM's `T.bot_squad` mode_tdm_impl.lua:46). The 1v1 opponent is a soldier whatever the human plays. | Mirror-the-human's-family rejected on three counts: (i) squad identity would become a function of roster contents, extending the TUPLE-row constraint from *pricing* to *spawning* — a pack-family roster would demand the bot side can field that family; (ii) fairness-by-level is the design's mechanism — the solver already prices family disparity through measured bases and tuples, so fairness-by-mirror is redundant with it; (iii) the k-prefix upgrade rule and the model-pin test's coverage (9 TUPLE rows, the 27-line pin at test_modes_tdm.cpp:1516/:1523) stay untouched. Composition stays a mode CONSTANT — deterministic, roster-independent. Recorded alternate: mirror-family 1v1 for sport flavor — revisit only with a spawnability rule for non-core pack families. |
| D36 | **Solver: already n-generic; the mutant ±35% carve-out generalizes into a per-n accuracy table** | `solve_matched_levels` takes n = `#bases` (mode_match.lua:198-247); the reachable set `{L 1..9, k 0..n-1, L = 9 => k = 0}` is 8n+1 evaluations (9 at n = 1, 41 at n = 5); tie-break (lower L, lower k) unchanged. Accuracy at percent 100, T interior: **n = 1 ~±35%** (the k-free pure-L ladder — the existing mutant bound, now the bound for every 1v1); **n = 2 ≈±20%; n = 3 ≈±18%; n = 4 ≈±16%; n = 5 ±15%** (existing, worst ~14.4%). The k-ladder splits each level gap into n rungs, which is where the interpolation comes from; the intermediate rows are design bounds — per the §4.2 discipline the authoritative per-n numbers are derived into the accuracy tests at implementation, never hand-shipped. |
| D37 | **E13 rewritten: the low-end floor drops from B(1)-of-five to B(1)-of-the-n-prefix; interior solves at small n may UNDERSHOOT the human — accepted; LIMIT fires only on a genuine clamp** | With n tracking the humans, the solo-fresh-soldier playtest case is now INTERIOR: T = 2306 lands inside [1643, B(9)] of a single soldier bot and solves L1/k0 — no clamp, no LIMIT, plain announce (worked case in §B.1). Undershoot up to the D36 band (n = 1: ~35%) is accepted in writing — the miss direction matches the playtest verdict (bots err friendly, never overwhelming). Saturation that REMAINS: T below the n-prefix's B(1) (a below-fresh solo human — hp-stripped, or the weakest-family shapes) still floors at uniform L1 with `"TEAMS MATCHED (LIMIT)"`; T above B(9) still ceilings at uniform L9 with LIMIT. The clamp condition is code-identical (`clamped = T outside [b1, b9]`, mode_match.lua:242-246) — b1/b9 are simply computed over n bases now. |
| D38 | **Director small-team rules — GENERAL, not matched-only**: every director rebuilds its member list from live directables each cadence, so these rules cover matched 1v1s AND today's whittled squads alike. Per mode: **soccer n = 1 => striker-only; CTF sole free member => attacker; basketball and TDM need no code change.** | **Soccer:** `run_team_director` (mode_soccer_impl.lua:626-736) assigns the GOALIE first — `ai.nearest_unassigned` toward the defended goal (:679) — so a sole member camps the mouth (leash charge :686-691) and can never score: a 1v1 is a guaranteed stall. Fix: skip the goalie assignment when `#members == 1`; the singleton takes the striker arm (re-commanded onto the ball every cadence, two-phase drive). Defense degrades gracefully — the striker plays the ball wherever it lies, and the approach-point geometry makes a touch near its own goal a goalward clear. n = 2 keeps today's goalie + striker (already sane; pinned at test_modes_soccer.cpp:1436/:1572). Pins that flip: the three single-member goalie fixtures (:1462, :1498 foursquare x4, :1536) re-author with a second afield member so the mouth geometry stays pinned, plus a NEW n = 1 striker pin. **CTF:** the split `defender_count = og.div(#remaining + 2, 3)` (mode_ctf_impl.lua:618) makes a sole free member a permanent home defender — a 1v1 can never produce a director-driven capture. Fix: `defender_count = 0` when `#remaining == 1`; the reactive arms (carrier :551-567, interceptor :569-589 when the own flag is carried, retriever :591-606 when dropped) already supply defense at n = 1. n = 2 keeps 1 defender + 1 attacker (pinned, test_modes_ctf.cpp:2124-2156). Pins that flip: the sole-member => defender pins at :2058-2060 and :2083-2084 re-pin sole => attacker. **Basketball: no change.** Every scheme's FIRST greedy pick is the ball-relevant role and `hold_seam` reaches only unassigned members, so n = 1 degrades to shooter / on-ball defender / ball racer in every state (walked: `run_handler` :1862-1941 falls through the pass rung with no mate; `run_defense` :2001-2007 makes the sole member the on-ball defender BEFORE the threatened branch; `run_loose` :2029-2045 sends it onto the ball; existing n = 1 offense pins at test_modes_basketball.cpp:2693/:2711/:4713). Add the missing pins: n = 1 defense and n = 1 loose ball (present coverage gap). **TDM: no change.** No roles exist — every member independently hunts the nearest scoring enemy (mode_tdm_impl.lua:283-322, no member counting anywhere in the file); the 1v1 flagship flow test is the only addition. **AS FIXED (adversarial review 2026-08-10 — supersedes this row's gate spellings):** the first cut keyed both small-team gates on the DIRECTABLE count alone and shipped two defects. (i) CTF teammate-carrier: with our carrier securing the only enemy flag the attacker arm has no unsecured target, so zeroing the defender left the sole free member COMMANDLESS — and the retired defender was guarding the bank spot the carrier needs (banking requires our flag home). (ii) Mixed human+bot teams: `is_directable` excludes player walkers, so human + 1 bot read as a desperate n = 1 and the bot abandoned goal/home although the human fights on. As-fixed rule: both `run_team_director`s census `team_live` (every live Living on the team) beside the directable list, and the desperation arms apply only when the directables ARE the whole team — soccer's goalie gate is `#members > 1 or team_live > #members`; CTF's is `#remaining == 1 and lead_carrier == nil and team_live <= #members` (a teammate-carrier flips the sole free member back to the home defender, the bank guard). A lone bot beside a live human teammate keeps goal/home; the two additional CTF flips the first cut took (sole REMAINING member after interceptor/retriever assignments attacks — an all-directable team) stand. Pinned by `free_member_guards_home_while_a_teammate_carries`, `lone_bot_defends_home_while_a_human_teammate_fights_on` (CTF) and `lone_bot_keeps_goal_while_a_human_teammate_plays_on` (soccer); the true-solo striker/attacker pins re-stage on teams with no live non-directable teammate. **Accepted residual, stated:** the soccer small-team rule ignores score state — a LEADING true-solo survivor still chases the ball (desperation regardless of score); revisit only on playtest evidence of thrown leads. |
| D39 | **Spawn/respawn/backstop mechanics: truncation lives INSIDE the one spawn seam; every caller is signature-unchanged; D26's one-delta invariant restates as power AND headcount** | `spawn_bots`/`spawn_matched_bots` (mode_match.lua:546-562/:512-536) take the families PREFIX of length `n = og.min(#families, SIZE)` when `MATCHED.SIZE > 0`; SIZE = 0 keeps the full table, so the no-humans legacy arm and the direct 9096 probe arms stay byte-identical. Callers untouched: TDM fill (mode_tdm_impl.lua:119), CTF fill with its flag-home placer (mode_ctf_impl.lua:872), soccer/basketball fills, mutant seats, and `revive_wiped_teams` -> `spawn_bot_squad` (mode_match.lua:698-706). The engine respawn queue is one-for-one (corpse-snapshot replacement, respawn.cpp:436-443), so an n-member squad stays n across deaths with no queue change. The D24 wiped-human-team backstop truncates identically — its replacement squad honors n = min-H, which is exactly why min is the D34 rule (the squad faces the surviving humans). **D26 restated:** MATCH differs from OWN in exactly the generated squads, which spawn at matched power AND matched headcount; everything else stays byte-identical to OWN. The one-delta twin keeps its meaning wherever H >= 5 (the twin fixture authors a 5-guy roster — numbers verbatim). |
| D40 | **`MATCHED.SIZE` = header slot 5; the 9096 probe scratch relocates** | Header slots: 0 MODE_ID, 1 PHASE (mode_core.lua:10-13), 2/3/4 matched TARGET/PLAN/ANNOUNCED — 5 is the next free production slot. It collides with the TEST-owned probe scratch (`kMatchProbeInTarget/Plan/MidMatch` = 5/6/7, tests/modes_pack_fixture.h:154-157): the probe inputs move to 8/9/10 (the probe levels 9095/9096 register their own Lua, so mode-private slots are free there) and gain a `kMatchProbeInSize` input for the new arms. The `kTestRegistrationLua` byte change requires the `scripts/coverage/runtime_only_lua.txt` digest update in the same commit (I2). Mirror mode-var equality loops compare all 64 slots and absorb SIZE automatically. |
| D41 | **THE PLAN PHASE (issue #218): activation and fills live ONCE, in Lua, evaluated identically at launch and at picker preview — the C++ preview twin is deleted.** New optional level hook `on_mode_plan(level, inputs) -> plan` (LevelHook::ModePlan = 8) runs under a machine-enforced fence (VmState::plan_dispatch — every world `og.*` entry errors and the three hook registrars refuse) over ONE plain-data census, `og::sim::build_match_plan_inputs(world)` (request knobs, per-team anchors/roster/npcs/generators, raw CTF `{team, level}` flag rows). At launch `hooks::level_mode_init` chains plan -> init through the Lua stack and the five mask-mode inits EXECUTE the plan (`match.plan_activation` + `match.plan_fills` replace `own_roster_activation`/`census_mask`; `record_match_target` becomes the apply-side `bank_match_target` — SIZE from the plan, TARGET stays the D24 launch-time power measurement). At preview all three clients' `build_scenario_roster_report` runs `respawn_scan_anchors` on the scratch world, marshals the save/lobby request knobs + roster head-counts into the same census, and dispatches the same hook in the already-built VM (`active_world_scripts()` — legal because the fence isolates the plan from whichever world that VM serves). | The preview <-> sim agreement is now STRUCTURAL (same registered Lua function), guarded by the MatchPlan.agreement_* matrix (5 modes x strip/count/roster shapes, incl. the FAIR zero-power degrade row and the CTF banked-FLAG_ENTITY == plan fold pins) instead of asserted-by-twin. `og::sim::roster_effective_team_mask`, its 15-row sweep and the mirror test are deleted (the sweep reborn as MatchPlan.activation_precedence_sweep against the real soccer plan); `save_roster_team_mask` shrinks to `save_roster_team_counts` (pure marshaling). D29 stands: `og::sim::effective_team_mask` stays count-only and is the documented no-plan preview fallback (no packs / unregistered / plan error — the error arm renders `MATCH RULES UNAVAILABLE (SCRIPT ERROR)`); `lobby_effective_team_mask` (seat gating) untouched. Networked preview made EXACT with no protocol change: the SDL client passes `lobby_roster_team_counts(picker_lobby_players())` (every peer already receives every roster); text/curses keep the save-derived counts (the documented bound). The SDL VIEW LEVEL screen gains a per-frame change key {level, 4 knobs, roster digest, campaign+mount} that rebuilds the cached report — closing the pre-existing stale-joiner hole. Report honesty rules: seeded squads never list families (one `BOT CLASSES DRAWN AT START` legend line), matched bots show headcount only (levels are D24 measure-and-solve), the plan arm drops the `MARKERS: n` datum to fit fill labels in 48 chars while the fallback arm keeps the exact legacy lines, and the false `FEWER THAN 2 AUTHORED TEAMS` sentence is fixed in the plan arm only (`MATCH WILL NOT START: FEWER THAN 2 TEAMS`). |
| D42 | **THE PLAN PHASE IS RETIRED (issue #218, staged lobby — supersedes D41's mechanism; D41 stands as history).** `on_mode_init` now runs ONCE, at staging: each lobby owner assembles the real match world through `og::server::MatchStage` before GO, the VIEW LEVEL preview READS that world (host stage / joiner mirror, byte-fed from the same broadcast pair), and the launch adopts it — so preview == launch stopped being an agreement to guard and became an assertable BYTE identity (`StagedAdoption.staged_adoption_identity_*`, `MatchStageTest.adopted_world_keyframe_is_byte_identical_to_preview`). The whole plan seam is deleted: `LevelHook::ModePlan`, the plan-dispatch fence, `og::sim::build_match_plan_inputs`/`MatchPlanSummary`/`match_plan.{h,cpp}`, `hooks::level_mode_plan`, and the chained plan->init dispatch (init is arity-1 again). The rules keep exactly one home, renamed off the plan prefix: `match.activation` + `match.fills` (consumed by each mode's in-body `decide` fold at the top of `on_mode_init`), fed by the new Lua-side `match.census_inputs()` (og.oblist/og.fxlist/og.respawn_anchor_count/og.match_setting — the census IS the world; CTF's flag rows resolve the family by name, killing `kMatchPlanFlagFamily`). `core.team_count_request` (orphaned by D41) is deleted with its tests. Squad provenance becomes a spawn-time fact: `add_squad_member` stamps `BOT_MARK_BIT` (mode_caps, stats bit 65536, snapshot-replicated), and the FAIR label reads the banked `MATCHED.SIZE` var — the preview censuses observable facts, never a rule twin. Test fates: the engine-seam suites died with the seam; the 10 rule oracles transformed into staged-world assertions and the 16-row activation sweep kept exact coverage through a registered direct-Lua probe for `match.activation` (`test_staged_rules.cpp`, group og_unit_stage — og_unit_matchplan is absorbed); the two RED-proven preview guards survive as staged-report assertions (`test_staged_report.cpp`). |

## B.1 The flagship case, end to end (real numbers — every base is pinned)

**Solo fresh L1 soldier under TROOPS: FAIR (the playtest shape):**

1. Census: T = **2306** (pinned at test_modes_tdm.cpp:2042; derivation
   comment :1571-1574 — H 166, M 34, D 23, A 9, SP 4, FF trunc(5.872) = 5,
   RATE 24, ED 23, OFF 572, EHP 219, f = 219*632/60). Headcount H = **1**.
2. n = clamp(1, 1, 5) = **1**; family = BOT_SQUAD[1] = **soldier** (D35).
3. Solve over the single measured soldier base (pinned :1448-1453 — hp 120,
   mp 50, armor 0, dmg 20, sp 4, fd 6): pred(1) = **1643**, pred(2) = **3348**
   (the L2 value is pinned at :1467; 1643 is the same arithmetic at L1 and
   cross-checks against the pinned five-family B(1) = 5006 minus the mutant
   B(1) pins 834/958/969 and mage-L1 602). |1643 - 2306| = 663 beats
   |3348 - 2306| = 1042 → **L* = 1, k = 0, plan code 10**. b1 = 1643 <= T →
   NOT clamped.
4. The player faces **one L1 soldier bot at f 1643 — 71% of the human's
   2306** (29% undershoot, inside the n = 1 band), announce =
   `"TEAMS MATCHED"` (plain — the old LIMIT for this shape is gone with the
   clamp that produced it).
5. Rejected old behavior, for contrast: five L1 bots, squad f 5006 ≈ 2.2x
   the human, plus unpriced 5-v-1 focus fire, LIMIT announce.

**Duo illustration (to be pinned at implementation):** two fresh soldiers on
one team → T = 4612, H = 2 → n = 2 (soldier + archer). Reachable set
includes P(1,0) = 1643+834 = 2477, P(1,1) = 3348+834 = 4182, P(2,0) =
3348+1857 = 5205 → **L1/k1** (miss 430 = 9.3%): a 2v2 against an L2 soldier
and an L1 archer.

**Unchanged shapes, verified:** the E6 flagship (5-guy `author_fresh_squad`
roster → H = 5 → T 6520, plan 11, five bots 2/1/1/1/1 — test_modes_tdm.cpp:1998)
and the ±15% headline (five L5 heroes, :1958) keep every number verbatim.
**>= 2 roster teams stay pure OWN: no fill at init** (mode_match.lua:364-369)
— exactly today. (2026-08-18: no longer — Auto resolves to the authored
count, so init fills any authored side beyond the rosters; see the D26
addendum. The E6/±15% NUMBERS above — targets, plans, per-squad levels —
still hold verbatim; what moved is the mask, which now takes every
authored side, each filled with the same solved squad.)

## B.2 Pin ledger — every test that shifts

| Site | Today | Becomes |
|---|---|---|
| test_modes_tdm.cpp:2032 `matched_low_end_saturation_floors_the_squad_and_says_limit` | solo soldier → 5 bots L1, plan 10, LIMIT (announced 2), `"the squad never narrows (D12)"` | REWRITTEN as the 1v1 flagship: alive 1, plan 10 (unchanged — n = 1 solves L1), announced 1 plain; the D12 message dies. A NEW LIMIT case needs a sub-1643 target (probe input or a weakest-family solo human) |
| test_modes_tdm.cpp:1998 (E6), :1832 (one-delta twin), :1958 (±15%), :1797 (even rival) | 5-guy rosters, 5-bot squads | UNCHANGED (H = 5 → n = 5); the :1832 twin adopts D39's restated language |
| test_modes_tdm.cpp 9096 probe arms (:1631, :1655, :1680, :1708, :1739, :1761) | full-squad probes | UNCHANGED (SIZE unset = full table); ADD SIZE-set arms: n = 1 / n = 2 planned, measure-and-solve, and backstop truncation |
| test_modes_mutant.cpp — all matched pins | exactly 1 per seat | UNCHANGED (n = min(H, 1) = 1); `"n = 1 admits no upgrades"` now the general D36 rule |
| test_modes_soccer.cpp:2045 `kickoff_backstop_matches_a_wiped_human_team_to_target` | two 1-guy human teams; backstop fields 5 bots (:2086) | min-H = 1 → backstop fields **1** bot; target 2306 and plan 10 unchanged |
| test_modes_soccer.cpp:1959 mask twin, :2001 `kickoff_reprovisions_a_wiped_matched_team_at_strength` | 5-alive pins (:1979, :2032) | re-adjudicate per fixture roster size (solo-hero fixtures become 1v1 counts) |
| test_modes_soccer.cpp:1462, :1498, :1536 (single-member goalie fixtures) | sole member = goalie geometry pins | re-author with a second afield member (goalie geometry survives); ADD the n = 1 striker pin (D38) |
| test_modes_basketball.cpp:4110 mask twin (:4129 `"5v5 ... game shape"`), :4156 `wipe_watchdog_refields_a_matched_team_at_strength` (:4162, :4185) | solo-hero court → 5-bot pins | become 1v1 counts (alive 1); ADD n = 1 defense + n = 1 loose-ball director pins (the coverage gap) |
| test_modes_ctf.cpp:2664 `dead_matched_bot_respawns_at_its_matched_level` | solo L5 hero → `ASSERT_EQ(5, ...)` at :2676/:2712 | alive pins become 1; the level-survival assertion (I5(b)) unchanged |
| test_modes_ctf.cpp:2058-2060, :2083-2084 | sole member => defender | sole member => ATTACKER (D38 CTF fix) |
| test_modes_ctf.cpp:2284 5v5 capture pin, :337 blocked-anchor fill | legacy (no matched arm), 5 bots | UNCHANGED — legacy fills never truncate |
| test_headless_server_runtime.cpp:684 matched ordering | vars[2] > 0 post-tick | UNCHANGED; optionally also assert vars[5] > 0 (SIZE latched) |
| onslaught :481/:483 | TARGET latched, power ignored | UNCHANGED (D17 stands; SIZE latched-and-ignored the same way) |
| tests/modes_pack_fixture.h:154-157 probe slots; `SquadLevels` (test_modes_tdm.cpp:1355-1389) | scratch at 5/6/7; five-family reader | scratch moves to 8/9/10 + `kMatchProbeInSize` (D40, digest in same commit); `SquadLevels` gains an n-aware sibling for 1v1 worlds |
| mirror/determinism suites (tdm:2061, :2072 and siblings) | run-to-run digests, all-slot compares | self-absorbing — no literal edits; rerun as proof |

## B.3 Amendment work packages

| WP | Depends | Scope |
|---|---|---|
| **WP-B1 headcount-and-seam** | — | D34/D36/D37/D39/D40: census headcount + min rule + `MATCHED.SIZE` latch, spawn-seam prefix truncation, probe scratch relocation + `runtime_only_lua.txt` digest; rewrite tdm:2032 as the 1v1 flagship, new LIMIT case, new SIZE probe arms, per-n accuracy derivations into tests. Owned: `mode_match.lua`, `tests/modes_pack_fixture.h`, `scripts/coverage/runtime_only_lua.txt`, `tests/unit/test_modes_tdm.cpp` |
| **WP-B2 directors** | — | D38: soccer n = 1 striker-only (`mode_soccer_impl.lua`), CTF sole-free-member attacker (`mode_ctf_impl.lua`); re-author the three soccer goalie fixtures + new striker pin; flip the two CTF sole-member pins; ADD basketball n = 1 defense/loose pins (no impl change). Owned: the two impl Lua files, `test_modes_soccer.cpp`, `test_modes_ctf.cpp`, `test_modes_basketball.cpp` |
| **WP-B3 mode re-adjudication** | WP-B1 | B.2's soccer/basketball/CTF matched-fixture rows (1v1 counts, backstop min-H case), D39 twin language, mutant no-op verification |
| **WP-B4 gate-sweep** | all | `ctest --preset ci-test` clean; `check_mutation_pins.py` (no `src/` sim edits expected — respawn.cpp untouched); Lua gates (func = 100, statement-per-line, luals) on the three edited impl files + mode_match; parity goldens untouched (modes Lua stays parity-invisible); mirror/determinism reruns as proof |
