# Master Specification — FREE FOR ALL (mode 7, campaigns/modes)

Line anchors in this document were re-verified at branch `feature/ffa-mode`,
HEAD `97bbf10b`. Every implementer must re-verify an anchor with
`sed -n '<line>p'` before editing — the referenced files move.

Status: **approved design, pre-implementation, judge-synthesized**. This
document is the single authority for the FFA mode; it supersedes the two
designer drafts it was synthesized from (the "minimal blast radius" draft won;
grafts from the rival draft and two judge-found corrections are recorded in
the Decision log). It resolves issues **#203** (non-team deathmatch
scenarios) and **#187** (Mutant should force a true free-for-all). The player
doc is `docs/mp-game-modes.md`; the pack cookbook every script cites is
`docs/lua-classpacks-design.md`.

**One-paragraph summary.** FFA is the seventh scripted mode (`MODE.FFA = 7`).
At `on_mode_init`, mode Lua reseats every deployed fighter onto a unique team
byte in a reserved runtime band **16–31**; sim-native mutual hostility falls
out of `is_friendly`'s team-equality rule with zero combat-code changes
(`walker.cpp:2264`, `walker_combat.cpp:253`). A fixed 16-entry ramp table
maps band bytes to visually distinct palette ramps — 13 existing good ramps
plus 3 synthesized into the flat-grey palette hole 168–191 (art audit: clean,
see D6) — so color is a pure deterministic function of the already-replicated
`team_num`: **no snapshot field, no protocol bump, no menu changes**.
Scoring, roster, and respawn cursors live in the 64 replicated ModeState
vars. Four `og.*` binding guards widen from `team < 4` to a shared
`is_scoring_identity` predicate (`t < 4 || (t >= 16 && t < 32)`). Mutant is
converted to the same fighter machinery, lifting its 4-competitor cap to 16.
Six new arenas ship at ids **850–855**. Wire/protocol triple: untouched.

---

## 1. Goals and non-goals

Delivered, user-visible:
- Up to **16 mutually hostile fighters** (humans + bot fill) on dedicated FFA
  arenas — every deployed character fights every other, including same-seat
  and same-lobby-team characters (#203's "override teams and just force a
  free-for-all").
- **Randomized distinct looks**: each fighter draws in one of 16 distinct
  8-shade palette ramps, shuffled per match.
- **Mutant becomes a true FFA** (#187): competitors are individual deployed
  characters on band bytes, cap 16, not lobby teams 0–3 capped at 4.
- Winner announced by **character name**.

Non-goals (rejected with reasons in the Decision log): widening
`SCORE_TEAM_COUNT`/`m_score`, lobby teams beyond 4, per-walker color wire
fields, new SCEN_TYPE bits, MATCHUP layout changes, two-tone sprite colors
(the engine has exactly one remappable 8-shade range per sprite,
`video_sdl.cpp:2010-2012`).

## 2. Hostility model — the fighter band

- **Band constants**: `kFfaTeamBase = 16`, `kFfaTeamCount = 16` in
  `include/openglad/core/constants.h` beside `MAX_TEAM`. Byte 16+c is fighter
  color index c (0..15). Rationale for 16..31: above the loader clamp
  (`level_file_io.cpp:78-85` clamps authored objects to ≤7), clear of
  wildlife/retire convention (bytes 4–7), clear of lobby seats (0–3) and the
  archmage berserk `og.rand(8)` roll; `16 % 8 == 0` makes the curses
  `team_num % 8` map fighter *i* → terminal color *i mod 8* with no curses
  change.
- **Fighter = deployed character.** At `on_mode_init` (after the engine's
  step-0 anchor scan, `mode_tick.cpp:144-156`), enumerate `Order::Living`
  walkers with `has_guy`: **bound walkers first** (`user() >= 0`, so every
  seat's controlled hero is guaranteed a slot), then unbound `has_guy`
  livings, oblist order. Cap 16; extras are retired via `mode_strip.retire`
  with a toast. Fewer than 2 fighters after bot fill → `error('ffa: fewer
  than two fighters')` (TDM precedent).
- **Assignment**: Fisher–Yates shuffle of the 16 band bytes with `og.rand`
  (sim RNG; mirrors receive bytes via snapshots and never run init). Fighter
  j gets its shuffled byte via `w:set_team_num(byte)`; entity_id recorded in
  var 32+c; bit c set in the assignment bitmap (var 14). Scatter each fighter
  to a rotated anchor (see §6) so seat-team clusters don't start stacked
  (init-time placement may pass `allow_teleport` — the blessed RNG fallback).
  Consume all four teams' start markers; retire non-guy score-range livings
  and generators (TDM strip semantics); wildlife (4–7) untouched. Write
  `SLOT.MODE_ID = 7` **last** (latch discipline, `mode_core.lua:7-10`).
- **Bots** fill up to the manifest row's `fighters` count (default 8):
  mutant-style singles from the shared `bot_roster`, one per free band slot,
  `set_real_team_num(255)`.
- Engine AI deathmatches automatically (`find_far_foe` targets any non-equal
  team, `game_world.cpp:1199-1236`) — this is exactly what Mutant's FFA phase
  already relies on. The Lua director only repairs broken foes (§5).

## 3. Seat seams (the four places a seat meets a band byte)

1. **Death → respawn retention.** FFA schedules the corpse's respawn
   synchronously in `on_entity_death` (plus the per-tick `schedule_dead`
   sweep as backstop), so `respawn_retains_player_control`'s scripted arm —
   verified team-agnostic: `!win_latched && already_scheduled &&
   !live_duplicate_exists` (`respawn.cpp:338-351`) — holds with no gap tick.
   Team-wipe EndGame is suppressed for the whole undecided match
   (`respawn_suppress_team_wipe_endgame`, `respawn.cpp:358-365`).
2. **bind_player / mid-match rebind.** The scripted-world claim arm currently
   requires `w->team_num() == team_num` before the owner check
   (`game_server.cpp:1247-1267`, verified). Change: in the `TYPE_SCRIPTED`
   scan only, drop the team filter — `myguy->owner_player_index` matching the
   binder is a strictly stronger identity. Classic worlds unchanged.
   `game_server.cpp` has zero canary pins.
3. **Reacquire scan hardening.** `sim_find_next_control_owned`
   (`sim_control_policy.cpp:209-241`) gains an owner-match arm for
   `TYPE_SCRIPTED` worlds (owner tag beside the `my_team` filter). This is
   belt-and-braces for hook-error-latched matches and also neutralizes the
   "team-0 props steal control" quirk in scripted play. Pin-free file;
   gated on TYPE_SCRIPTED which no parity golden sets.
4. **SwitchChar** (`sim_input_handler.cpp:186-196`) finds nothing in FFA —
   character switching is deliberately disabled (each fighter is an
   independent combatant). Documented in `mp-game-modes.md`. Unchanged code.

**Charm** works untouched: a charmed fighter temporarily wears the charmer's
byte (frags credit the charmer — correct), `real_team_num` restore returns it
to the band on uncharm. Two verified holes are closed by re-assertion:
`revive_player_walker` restores team only for `team < 4` (`respawn.cpp:123`)
**and clears `real_team_num` to 255**, so a fighter who dies *while charmed*
would revive permanently on the charmer's byte. Fixes (both Lua):
`on_respawn` re-asserts the slot's assigned byte from var 32+c, and a
per-cadence renormalize pass re-asserts the byte on any registered fighter
with `real_team_num == 255` and a wrong `team_num` (also heals berserk-charm
residue). Charmed fighters (`real_team_num != 255`) are left alone.

**Mid-match join** (`on_entity_spawn`, gated `MODE_ID == 7`): a `has_guy`
Living appearing with team < 16 gets the next free band byte from the bitmap.
Band full → retire the lowest-frag bot (ascending index tiebreak), reset that
slot's frags to 0, and adopt the joiner into it; no bots → the joiner stays
on its seat team (still fights everyone; documented cap).

**Guy persistence**: `myguy->teamnum` is never written by `set_team_num`;
saves and the netsession merge keep seat teams 0–3. No save contamination.

## 4. Color: 16 ramps, derived, free on the wire

Color = pure function of the replicated team byte. New helper
`og::sim::team_ramp_base(int team)` (declared in `mode_state.h`, implemented
in `mode_tick.cpp` beside `team_color_name` — both pin-free): returns
`kFfaRampBases[team - 16]` for band bytes, else the classic
`uchar(team*16+40)`. `walker::query_team_color` (`walker.cpp:2255-2262`)
becomes a call to it — the single choke point that covers all sprite blits
(flash/invisible/outline/reflect/ghost), radar dots/gems/beacon fallback,
demo, obmap/path debug for free (render map honor list).

**The ramp table** (color index → palette base; all verified 8-shade
non-animated runs, none of 0/7/208/224, no window intersecting the cycled
208–231, coherent depth for radar gems base/+2/+4/+6 and generator dots +1):

| c | base | name | c | base | name |
|---|------|------|---|------|------|
| 0 | 40 | RED | 8 | 200 | LAVENDER |
| 1 | 56 | GREEN | 9 | 32 | SALMON |
| 2 | 72 | BLUE | 10 | 128 | ORANGE |
| 3 | 88 | YELLOW | 11 | 144 | PINK |
| 4 | 104 | MAGENTA | 12 | 192 | VIOLET |
| 5 | 120 | CYAN | 13 | 168 | TEAL\* |
| 6 | 136 | TAN | 14 | 176 | GOLD\* |
| 7 | 152 | ROSE | 15 | 184 | SLATE\* |

\* Synthesized into the only content-free palette region — the 24 flat-grey
entries 168–191 (`our_palette.cpp:41-43`) — baked into `our_pal_lookup` so
they survive every `set_palette` reinstall path and redpalette derives
automatically. Exact 6-bit values (brightest at +0, descending like RED 40):

```
TEAL  168-175: (0,57,50) (0,50,44) (0,43,38) (0,36,32) (0,29,26) (0,22,20) (0,15,14) (0,9,8)
GOLD  176-183: (57,45,10) (51,40,8) (45,35,6) (39,30,4) (33,25,3) (27,20,2) (21,15,1) (15,10,0)
SLATE 184-191: (44,48,57) (39,43,51) (34,38,45) (29,33,39) (24,28,33) (19,23,27) (14,18,21) (9,13,15)
```

**D6 — the PLTE contract trap (judge-found; neither draft had it).** The
sprite loader hard-rejects any indexed PNG whose embedded 256-entry PLTE
differs from `our_pal_lookup` by more than ±1 per channel at ANY entry
(`og_file.cpp:869-884`) — including entries the art never uses. Changing
168–191 without a companion fix bricks every shipped sprite. Fix: exempt
entries 168–191 from the PLTE verification (named constants
`kSynthRampFirst = 168` / `kSynthRampLast = 191`, comment referencing this
spec), so both old-grey and new-ramp PLTEs load. Companion: regenerate
`pix/openglad.gpl` via `scripts/migrate_pix_to_aseprite.py --emit-gpl` so
future art exports carry the new colors. The **art audit is already done**:
all 616 tracked indexed PNGs (pix/, packs/, campaigns/) decoded and scanned —
zero pixels reference 168–191. The 13-ramp fallback from the drafts is dead;
16 ramps ship unconditionally.

**Non-walker team-ID color sites** switch to `team_ramp_base`:
`mode_hud_color` (`score_panel.cpp:240-245`), results winner banner and
scoreboard segments (`results_screen.cpp:793-818`, currently unclamped
`winner_team*16+40` — this removes a wrap landmine), radar beacon blip
fallback (`radar.cpp:502-506`). `og::sim::team_color_name` gains the 16 band
names from the table (default stays YELLOW). The ending popup drops the word
"TEAM" for band winners (`"{mode.name}: TEAL WINS!"`); classic teams keep the
existing format. Seat/roster chips, MATCHUP swatches, wallets: unchanged
(seats keep lobby teams 0–3).

**Curses/text**: `glyph_map` keeps `team_num % 8` — fighter i → terminal
color i mod 8; the honest 8-color degradation, existing
`GlyphMap.team_color_ramp_wraps` pin untouched. **Invisibility**: invisible
fighters vanish for everyone (no same-team viewer) — correct for deathmatch,
documented. **Menu portraits** stay red (pixie drawMix hardcodes RED,
pre-existing).

## 5. Scoring, win, HUD

**Storage** — per-fighter frags in ModeState vars (replicated,
`world_snapshot.cpp:855-916`; never `m_score`). FFA var map (MODE_ID 7,
header slots 0–7 shared as usual):

| var | use |
|-----|-----|
| 8 | fighter count N |
| 9 | resolved score limit (lobby Limit > manifest `score_limit` > default 15; clamp 1–255) |
| 10 | time-limit deadline tick (manifest `time_limit`, default 7200) |
| 11–12 | `mode_items` pad state pair |
| 13 | respawn/anchor rotation cursor |
| 14 | band-byte assignment bitmap |
| 15 | announce/phase flags |
| 16–31 | frags for fighter c (may go negative) |
| 32–47 | entity_id of fighter c (0 = free slot) |
| 48–63 | reserved |

**Frag rules** (TDM semantics, `mode_tdm_impl.lua:136-196` template, band
shifted): `on_entity_death` resolves killer_team from the 48-tick attack
stamp; killer in band ∧ victim is a registered fighter or its owned spawn
(`owns_its_life`) → `frags[killer−16] += 1`; killer_team == victim's band
byte (suicide) → −1; nil/stale/environment → nothing; non-owning victims get
`core.scrub_corpse`. Generator/summon kills credit the owner automatically
(the stamp carries the owner-chain-root team = band byte,
`walker_combat.cpp:324-329`); mutual kills score via the stamped team even
with a nil killer handle.

**Win**: first fighter with frags ≥ limit (ascending color index tiebreak);
at the deadline, leader by frags, tie → lowest index. Declared via
`og.declare_winner(band_byte)` — through the engine win latch, never by
writing world fields; first arming flush-revives pending respawns.
`winner_is_player` (live myguy with `team_num()==winner`,
`mode_tick.cpp:97-123`) and int8 `winner_team` both hold for 16–31 unmodified.

**Winner naming**: by character name — `w:g_name()` (the existing guy-name
binding, `bindings_entity.cpp:879`; **no new binding needed** — the rival
draft's `hero_name` export is rejected). `has_guy` and non-empty name
required, else fall back to the band color name. Toast `"WINNER: {name}"`
(≤25 chars, name clipped) + HUD slot 0 tinted by the winner byte.

**HUD budget** (4×25 replicated lines): slot 0 = `"1ST {name} {n}"` tinted by
leader byte; slot 1 = `"2ND {name} {n}"`; slot 2 = `"GOAL {limit}"` (255
default tint). Beacon slot 0 on the leader when within 3 frags of the limit
(Mutant-style endgame drama). The classic HUD's `SC: 0` for band-team
controls is an accepted cosmetic (the mode row carries the standing); the
full 16-fighter scoreboard is the results TROOPS tab, which already lists
every myguy walker with live team-colored sprites.

**Exact C++ guard changes (complete list)** — shared predicate
`og::sim::is_scoring_identity(int t)` = `t < 4 || (kFfaTeamBase <= t <
kFfaTeamBase + kFfaTeamCount)`, in `mode_state.h`/`mode_tick.cpp`:

| Site | Today | Becomes |
|------|-------|---------|
| `og.declare_winner` (`bindings_entity.cpp:2181-2190`) | error ≥4 | error unless `is_scoring_identity` |
| `og.set_hud_line` team arg (`:2252-2262`) | error ≥4 | accept band (255 default unchanged) |
| `og.set_beacon` team arg (`:2300-2310`) | error ≥4 | accept band |
| `og.team_color_name` (`:2384-2394`) | error ≥4 | error unless `is_scoring_identity`; C++ switch gains 16 band names |
| `og.C.FFA_TEAM_BASE` / `og.C.FFA_TEAM_COUNT` | — | new exports beside `C.SCORE_TEAM_COUNT` (`:3028`) |
| `og.award_score` (`:1703`) / `og.team_score` (`:2321`) | drop/error ≥4 | **unchanged** — FFA never calls them; m_score stays 4-wide |
| `og.respawn_pending_count/anchor_count/anchor` (`:2490/2505/2520`) | error ≥4 | **unchanged** — anchors are position pools, addressed 0–3 |

While in `mode_state.h`: fix the stale "Not yet wire-replicated" comment
(lines 11–14) and the HUD/beacon team-byte doc ("0–3 or band 16–31 ramp").

**AI director** (cadence 15, TDM shape): repair broken foes to the nearest
*registered fighter* — `mode_match.foe_scores` filters team ≥ 4 as wildlife,
so FFA uses band-aware nearest/eligibility helpers in `lib/mode_fighters.lua`
rather than editing the shared filter (existing modes provably unaffected).
Endgame focus: retarget everyone at the leader when within 3 of the limit.
The charm renormalize pass (§3) rides the same cadence.

## 6. Respawns — no anchor-block widening

`RespawnState`'s `[4][16]` wire arrays and the `< 4` scan stay untouched
(widening = wire layout change + `test_mode_snapshot` offset pins 77–80 +
version triple). Instead:

- FFA arenas author **4 start-marker clusters (teams 0–3) interleaved
  uniformly around the arena**, scanned into the existing arrays as normal.
  FFA treats them as **position pools, not identities**.
- Placement calls the existing `match.place_at_anchor(w,
  og.mod(cursor, 4), cursor_slot, allow_teleport)` with the rotating cursor
  (var 13) — signature verified (`mode_match.lua:474-524`), rotation +
  `og.spawn_spot_clear` probing + the PR-#195 deterministic ring fallback all
  included; **no mode_match change needed** (the winning draft's
  `place_at_point` refactor is rejected as needless shared-lib churn).
- Revive rides the team-agnostic `RespawnEntry` queue (entry.team is a full
  byte by design, `world_snapshot.cpp:809-821`); `on_respawn` repositions and
  re-asserts the slot's band byte (§3).
- Scheduling: synchronously in `on_entity_death` (control retention) with the
  per-tick `schedule_dead` sweep as backstop. Delay: lobby
  `og.match_setting('respawn_ticks')` wins, else FFA default **90** ticks
  (between TDM 120 and Mutant 60).

## 7. Scenario authoring (#203)

- New builder `tools/modes_mapgen/levels_ffa.cpp`, wired in `main.cpp`;
  **ids 850–855** (free hole 844–899 inside the scanned 300–899 band; 844–849
  left for mutant growth).
- Six arenas, titles `"FFA: <ARENA>"` — arena names ≤ 11 chars so the full
  title fits the 16-char SET LEVEL row budget:
  850 `FFA: THE MELEE` (8 fighters, open colosseum, pillar ring),
  851 `FFA: CROSSFIRE` (10, four bridges over water),
  852 `FFA: SHARDS` (12, broken-wall rooms, LOS breakers),
  853 `FFA: THE ROSE` (16, radial spokes, center item pad),
  854 `FFA: SCRAMBLE` (16, scatter cover, twin pads),
  855 `FFA: NIGHTFALL` (16, torch-lit corridor loop).
- Each: `SCEN_TYPE` 0x20, 4 interleaved marker clusters (§6), no exits,
  spawn caps, drumstick + speed-potion pads (`item_interval 180`), briefings
  ending `-- THE GAMESMASTER` with a "FREE FOR ALL — team choice does not
  restrict targets" line.
- Manifest rows: `mode='ffa'`, `fighters=8..16` per arena (new row field),
  `time_limit=7200`, `score_limit=15`, `spawn_caps`, `item_pads`. Mutant rows
  840–843 gain `fighters=4` (keeps current feel, allows growth).
- `campaign.yaml` description count: "thirty-three" → **"thirty-nine"**
  (`main.cpp:106-125` regenerates the text — update the literal there).
- Regenerate flow: `scripts/generate_modes_campaign.sh` (rewrites campaign +
  `lib/mode_levels.lua` + rebuilds the staged archive; never hand-edit the
  manifest).
- New pack files: `scripts/mode_ffa.lua` (style-A registration:
  `core.register_mode(match.rows_for(levels), 'ffa', hooks)`; `MODE.FFA = 7`
  added to `mode_core.lua`), `lib/mode_ffa_impl.lua`, and the shared
  `lib/mode_fighters.lua` (§8).

## 8. Mutant forces FFA (#187)

Mutant converts to the fighter band via the shared **`lib/mode_fighters.lua`**
(enumeration, shuffle, byte assignment, mid-join adoption, renormalize,
frag-ledger and nearest-fighter helpers — one implementation, two modes; a
new file rather than edits to `mode_match.lua`, so shared-mode behavior is
provably unchanged and the coverage denominator is additive):

- Competitors = every deployed character + bot fill, each on its own band
  byte at init. Two humans seated on one lobby team become separate
  competitors — the literal ask of #187. Cap 4 → 16.
- Phase machine, crown/inherit, on_damage gate (non-mutant vs non-mutant → no
  damage), HP decay, kill_heal, item pads: unchanged in shape — they key on
  entity ids and `MUTANT_TEAM` (now stores the band byte; int32 fits). Crown
  beacon rides the widened `og.set_beacon`. Score vars move from `16+team`
  (0–3) to `16+color_index` (16 slots) — mode-private namespace.
- Bot fill: one bot per empty slot up to the row's `fighters` count.
- Respawn placement switches to the §6 rotation; `declare_winner(band byte)`
  rides the widened binding.
- Docs `mp-game-modes.md:15, 264-267` ("FFA until first blood… capped at 4")
  rewritten: Mutant is a true free-for-all, up to 16 competitors, regardless
  of lobby teams.
- A lobby-side "one human per team" rule was rejected: it still caps
  competitors at 4 and widens the lobby domain end-to-end for one mode.

## 9. Lobby / UI / wire — deliberately zero

- **No new SCEN_TYPE bit, no LobbySettings field, no protocol change.** The
  manifest `row.mode` string is the per-level mode authority, exactly like
  CTF. FFA ignores `og.match_setting('team_count')` (the maps' constraint:
  forced FFA must override, not expect a big count through the [2,4] clamp).
- Seats keep lobby teams 0–3 (wallets, saves, results economy unchanged).
  MATCHUP unchanged — its 4 rows still truthfully describe seat/economy
  grouping; the level title prefix + briefing carry FFA identity, plus an
  init toast (`"FREE FOR ALL"` / `"{n} FIGHTERS ENTER"`). **Zero menu test
  re-pins** (matchup_static_layout, nav BFS, label cycles, text ordinals all
  untouched). A locked `Teams: FFA` label is an explicitly deferred
  follow-up.
- **Version triple (protocol 12 / snapshot 10 / replay 14): untouched.** Team
  bytes are already u8 on the wire (EntitySnapshot BIT_TEAM_NUM,
  `world_snapshot.cpp:809-813` blesses the full range); FFA state rides the
  replicated ModeState vars/HUD/beacons; RespawnState and m_score keep their
  layout. The 5 literal wire-byte tests, `test_mode_snapshot` offset pins,
  LobbyState offset pin, and replay-compare table all stay as-is.

## 10. Decision log

- **D1 — Band 16–31, not 4–15, not lobby widening.** Bytes 4–7 collide with
  all four wildlife conventions (never-target filter, strip immunity, census,
  retire byte 4); 8–15 hit flat-grey/water-cycled ramps under `t*16+40`;
  lobby widening drags sanitize clamps, u8 masks (8-team hard ceiling), seat
  UI, resolve_team, protocol v13 — maximal churn for the same visible result.
- **D2 — Reseat in Lua at init; hostility is `is_friendly` equality.** The
  friendly-fire early-return precedes the Lua damage gate
  (`walker_combat.cpp:253` vs `:319`), so no hook-based FFA exists; Mutant's
  FFA phase is the shipped existence proof of band hostility.
- **D3 — Fighter = deployed character, bound-first enumeration, cap 16.**
  Avoids the unsolvable "squad" ambiguity (benchwarmers carry no seat
  identity); guarantees every seat's hero a slot.
- **D4 — Color is a pure function of the replicated team byte.** Strictly
  better than a wire field: free, deterministic on mirrors, and curses/text
  inherit it. Randomization = the byte shuffle; the byte→ramp table is fixed.
- **D5 — 13 existing ramps + 3 synthesized (168–191).** Judged over ramp
  reuse: the audit (616/616 indexed PNGs clean) removes the only risk; the
  drafts' 13-ramp fallback clause is deleted.
- **D6 — PLTE exemption for 168–191 (judge-found, mandatory).** The loader
  rejects PNGs on palette mismatch at ANY entry (`og_file.cpp:869-884`);
  without the exemption, changing the palette bricks all shipped sprites.
  Exempt the band, regenerate `pix/openglad.gpl`. Rewriting 616 PNG PLTEs was
  rejected (breaks third-party art, huge binary churn).
- **D7 — Frags in ModeState vars; m_score untouched.** Widening m_score fans
  into WorldSnapshot/GTL v6 4-pair block/replay compare/~60
  SCORE_TEAM_COUNT sites and buys nothing 16 vars don't already deliver.
- **D8 — Four binding guards widen via `is_scoring_identity`** (the rival
  draft's cleaner predicate name, grafted). award_score/team_score/respawn_*
  deliberately stay 4-team.
- **D9 — Winner named via existing `g_name`/`name` bindings.** Verified
  `s_name`/`g_name` exist (`bindings_entity.cpp:794/879`); the rival draft's
  new `hero_name` export is rejected as dead weight.
- **D10 — Respawn placement = rotation over the 4 anchor pools via the
  existing `match.place_at_anchor`** (rival-draft graft; signature verified).
  The winning draft's `place_at_point` factoring of mode_match is rejected —
  shared-lib churn with zero UX difference.
- **D11 — bind_player owner arm + reacquire owner arm** (rival-draft graft
  for the second): owner tag beats team filter in scripted worlds; both files
  pin-free; both arms TYPE_SCRIPTED-gated so parity goldens can't move.
- **D12 — Charm/revive re-assertion is mandatory, not optional.** Judge
  verified `revive_player_walker` clears `real_team_num` to 255 while
  skipping the team restore for band bytes — a fighter who dies charmed would
  otherwise permanently wear the charmer's byte. on_respawn re-assert +
  cadence renormalize close it.
- **D13 — Mutant converts via shared `lib/mode_fighters.lua`** (rival-draft
  file layout grafted): new file, zero pins, no `mode_match.lua` edits, both
  modes exercise it for coverage.
- **D14 — Ids 850–855; arena names ≤ 11 chars.** The rival draft's 860-band
  titles (`FFA: SHATTERED KEEP`) overflow the 16-char SET LEVEL budget.
- **D15 — Zero lobby/protocol/menu churn** (both drafts agreed; adopted).
- **D16 — SwitchChar disabled in FFA** (team play mechanism; documented).
- **D17 — Curses mod-8 wrap accepted** (8 terminal colors cannot show 16
  fighters; base 16 aligns fighter i → color i mod 8).
- **D18 — Respawn delay default 90 ticks** (judged between the drafts' 90/60:
  deathmatch pace without corpse-rush chaos on 8-fighter maps).
- **D19 — Mid-join adoption replaces the lowest-frag bot** when the band is
  full (rival draft left the joiner permanently on its seat team even when
  bots occupied slots; judged worse).
- **D20 — Band winner popup drops the word "TEAM"** (rival-draft graft;
  one formatting branch in results_screen, pin-free).

## 11. Edges and risks

1. **Hook error mid-match** latches Lua dead but the mode stays active and
   undecided → EndGame stays suppressed. Mitigations: the reacquire owner arm
   (D11) keeps seats functional; tests drive every hook with 30-seed shuffle
   sweeps.
2. **Berserk charm** parks a fighter on `og.rand(8)` bytes while charmed —
   frags credit whoever the stamp says (accepted); renormalize heals the byte
   after uncharm/revive (D12).
3. **Probe-eats**: `og.spawn_spot_clear` consumption is deterministic and
   already handled inside `place_at_anchor`; FFA adds no new probe sites.
4. **>16 candidates**: extras retired via `mode_strip.retire` (byte 4,
   standard) + toast; deterministic bound-first order decides who plays.
5. **Instruction budget at 16 fighters + bots**: director is cadence-15
   repair-only; gate = `full_mode_tick_fits_a_tenth_of_the_instruction_budget`
   (500k override) on a busy 16-fighter world.
6. **walker.cpp pin churn**: pin 2303 sits below `query_team_color` (2255) —
   `scripts/parity/check_mutation_pins.py --fix`, rebuild `og_test_parity`,
   **and an apply/canary run for the shifted pin** (a `--fix` alone doesn't
   prove teeth).
7. **Cosmetics accepted**: `SC: 0` on classic HUD for band controls, curses
   mod-8 wrap, invisible fighters invisible to everyone, red menu portraits.

## 12. Test plan

Unit (existing binaries — no `recorder_processes.txt` churn):

1. `tests/unit/test_modes_ffa.cpp` → **og_unit_modes**
   (`OpenGladTests.cmake:878-890`): registration of rows 850–855; init
   assigns exactly N distinct band bytes (exact-set assertion, no banned `>0`
   shapes); deterministic shuffle (same seed ⇒ identical bytes; different
   seed ⇒ different permutation) pinned via `rng_.state_` (never the RNG
   spy); bound-walkers-first cap rule; >16 retirement; bot fill to
   `row.fighters`; MODE_ID=7 written last; frag ledger
   (kill/suicide/environment/mutual/generator-owner/charmed-killer); win
   latch + re-assert + flush-revive; timeout ladder + tie-break;
   `winner_is_player` for a band winner; death-hook scheduling
   (`already_scheduled` true on the death tick); anchor rotation + ring
   fallback determinism; band byte survives revive; charmed-death revive
   re-assert; renormalize pass; mid-join adoption incl. bot replacement;
   winner-name toast + 25-char budgets; instruction-budget headroom.
2. `test_mode_tick.cpp` / `test_mode_bindings.cpp` (og_unit_mode):
   `team_ramp_base` exact 16-entry mapping pin + classic formula for 0–15
   outside the band; `is_scoring_identity` truth table;
   declare_winner/set_hud_line/set_beacon/team_color_name accept 16–31 and
   still error on 4–15 and ≥32; 16 band names pinned; `og.C.FFA_TEAM_BASE/
   FFA_TEAM_COUNT` exports.
3. Palette: pin `our_pal_lookup[168*3 .. 191*3+2]` to the exact D5 values;
   loader accepts a sprite whose PLTE carries the OLD flat grey in 168–191
   (shipped PNGs keep their bytes) and one with the new ramps; programmatic
   assert every `kFfaRampBases` entry ∉ {0,7,208,224} with windows disjoint
   from 208–231.
4. `test_modes_mutant.cpp`: band competitors, two-humans-one-lobby-team
   mutually hostile (the #187 acceptance test), cap 16, crown beacon on a
   band byte, slot-map migration.
5. Control seams: bind_player owner-arm claim of an off-team scripted hero;
   `sim_find_next_control_owned` owner arm (unit, both worlds classic vs
   scripted).
6. `test_modes_levels` covers the regenerated manifest automatically.

Lua gates: `mode_ffa_impl.lua` / `mode_fighters.lua` / `mode_ffa.lua` +
touched `mode_core.lua`/`mode_mutant_impl.lua` clear line ≥95 / func 100 on
the Lua-alone bar (`mode_fighters` exercised by both ffa and mutant suites);
LuaLS-clean; one-statement-per-line; determinism cookbook (og.rand only, no
pairs, state only in mode vars).

Integration/e2e: host+join case (test_ctf_network pattern) on scen850 —
mirror team bytes byte-equal to server after init and after a death→respawn
cycle; seat retains control across the reseat and the cycle; a frag
replicates via ModeState; winner popup fires. Headless: a text-client
scripted run on scen850 via the `openglad_text_sim` harness pattern. 30-seed
`--gtest_shuffle` sweep on the new unit suite.

Repo gates (final): `cmake --preset ci-test` build + full ctest;
`og_test_parity` from repo root 226/226 byte-identical (band bytes are
unreachable in classic play — loader ≤7, lobby ≤3 — and every C++ change is
band- or TYPE_SCRIPTED-gated or render-only);
`scripts/parity/check_mutation_pins.py` clean + apply/canary proof for the
shifted walker.cpp pin; `OPENGLAD_LUA_COVERAGE` local run; `check_luals`;
`check_lua_statement_lines`; `scripts/generate_modes_campaign.sh` self-check;
zero menu-pin diffs.

## 13. Work plan

WP1 C++ band core → WP2 palette+PLTE / WP3 seat seams / WP4 scenarios (all
parallel with WP1, file-disjoint) → WP5 FFA Lua (needs WP1+WP4) → WP6 Mutant
conversion (needs WP5) and WP7 UI color fixes (needs WP1) → WP8
integration/e2e (needs WP3+WP5) → WP9 docs → WP10 verification → WP11 media.
Full package table in the orchestrator routing JSON accompanying this spec.
