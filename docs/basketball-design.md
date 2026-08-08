# Master Specification — BASKETBALL (mode 6, campaigns/modes)

Line anchors in this document were re-verified at branch `feature/mode-basketball`,
HEAD `0ed817cf`. Every implementer must re-verify an anchor with `sed -n '<line>p'`
before editing — the referenced files move.

Status: **approved design, pre-implementation, red-team revised**. This
document is the single authority for the basketball mode; it supersedes the
three designer drafts it was synthesized from. A two-auditor red team
(engine-truth + gameplay) reviewed the first synthesis; every finding was
applied — decisions D19-D26 and the amended D6/D7/D17 record the changes. A
post-implementation playtest amendment (2026-08-08, anchors verified at
`456fafb7`) added D27-D28 — the throw now releases under point-blank contact
and refunds its mana; an adversarial review of the first cut then narrowed
D27 to a qualify gate (`fire_check` probes must never be consumed) — with
edge #24 rewritten, edges #28-#29 added, and test-plan rows 38-41. The
player doc is `docs/mp-game-modes.md`; the pack cookbook every script cites is
`docs/lua-classpacks-design.md`.

**One-paragraph summary.** Basketball is the sixth mode of the modes campaign:
a possession ball game over the soccer machinery. The ball has a fake vertical
axis — ground x/y plus height z, all x256 fixed-point mode vars with per-tick
gravity — drawn by offsetting the ball entity's y by z (`setxy`), with a no-act
shadow fx entity marking the ground spot. Walking over a free ball picks it up;
the carrier's next fired weapon is consumed and becomes a throw: an arc shot at
a hoop (2 inside the painted arc, 3 beyond, distance-scaled deterministic
scatter), a lob or chest pass to a teammate, or a flat throw. Carrying the ball
into an enemy hoop's painted zone dunks for 2. Damage fumbles the carrier;
a shot clock turns stale possession over; misses clang off the rim into live
rebound scrums; authored walls give bank shots; blocks are legal low, the apex
is untouchable, and swatting a falling shot near the rim is goaltending (basket
counts). Five new arenas (824-828), everything in pack Lua + the mapgen —
`src/` is byte-untouched.

---

## 0. Decision record (conflict resolutions — binding)

Sources: `draft-mechanics.md` (M), `draft-systems.md` (S), `draft-arenas.md` (A).
Later sections cite decisions by id.

| # | Decision | Ruling & rationale |
|---|----------|--------------------|
| D1 | Scenario id band & count | **824-828, five arenas, row count 28 → 33** (A wins over M/S's four). All five courts earn their slot (reference / tight / 4-team / gimmick / generator); the band is inside both registration scans (`mode_core.lua:113-114` caps 300-899; per-script scans run 0-1023), contiguous with soccer on the numeric-sorted shelf, and 829-839 stays spare. |
| D2 | Manifest schema | **Minimal**: `hoops = {[defending team] = {x, y}}` pixel centers, court-level `arc_radius` (px), `jump_ball = {x, y}` — plus the existing `mode/teams/time_limit/score_limit/spawn_caps`. S's `key_rects` field and A's per-hoop `{rim, arc, dunk}` records are dropped: rim radius is a tuning constant (D14), the dunk zone is derived (hoop center ± 24 px box, D3), the key is cosmetic paint. Saves 8 mode vars and keeps the 28 existing rows byte-identical. `goal_rects`/`kickoff` stay soccer-only names. |
| D3 | Hoop geometry | **A's paint grammar**: hoop tile = `PIX_CARPET_M2` (id 34, verified passable) centered in a 3x3 `PIX_CARPET_M` dunk carpet inside a cosmetic cobble key; backboard = perimeter wall / authored stubs. Dunk zone = Chebyshev box `|dx| <= 24 and |dy| <= 24` matching the painted carpet exactly, replacing M's L1 radius 20. |
| D4 | Arc metric | **Euclidean, integer d²** (A+S over M's L1 diamond): 3 points when release `dx*dx + dy*dy > arc_radius²`. The painted ring is a circle; the predicate matches the paint. Per-court shot range = `arc_radius + 64` px (A's rule), same d² compare, replacing M's fixed 192. Contact radii stay L1 (soccer parity). Flight-time distances stay L1 (cheap; the solver hits the target exactly regardless). |
| D5 | Rules depth | **M's state machine adopted wholesale**: FREE/CARRIED/SHOT/PASS/REBOUND, timer-based shot resolution, pass targeting with catch/intercept, grace bars, goaltending awards the basket, rebound rim-plane crossings score tip-ins/banks with soccer's LAST_TOUCH1/2 attribution ladder. S's simplifications (block-only rule, "attribution-trivial" no-ladder §1.5) rejected — the approved concept names goaltending explicitly, and the crossing rule IS the bank-shot mechanism the BANKHOUSE arena is built on. |
| D6 | Slot map | **M's map** (slots 8-58) — it fits precisely because D2 dropped KEY_POS/KEY_SIZE. Slot 39 renamed JUMP_POS to match the manifest field. The red-team fixes claim the former spares: 59 GRACE_ENTITY (D24), 60 THROW_WATERMARK (D19), 61 POSSESS_SINCE (D21), 62 DUNK_OK (D23); 63 is the LAST spare (R4 escalated). Full map in §2.2. |
| D7 | Shot clock | **420 ticks (35 s)**, red-team amended from S's 288 (M's 120 stays rejected). Sizing: the worst defensive-stop advance is 828 — hoops 640 px apart, shot range 224, a rebound behind the own rim needs ~430+ px of BALL advance; at the 1 px/tick slow-family floor, 288 forced structural turnovers on every stop, while 420 clears it with one completed upcourt pass (chest 8 px/tick) in the chain. Clock lifecycle (the anti-launder rule, D25): the deadline is cleared ONLY by SHOT release, the turnover itself, and center resets; it PERSISTS across every loose-ball transition and every same-team regain — an uncaught own pass, a fumble scoop or a rolled-dead flat throw never refreshes it, and any clock-team regain at `now >= CLOCK_UNTIL` is an immediate turnover at the gain point. A possession gain by a DIFFERENT team re-arms fresh. HUD countdown suffix at <= 120 remaining, one-shot "SHOT CLOCK!" announce at exactly 36 remaining. |
| D8 | Spawns | **`markers_per_team = 5`** (A) over S's 12 — five-a-side, and `anchors.spawn_bot_squad`'s 5-family squad maps one bot per anchor exactly. |
| D9 | Score values | `score_limit = 21` default (825 plays to 11), **`point_score = 100`** (M) — `og.award_score` deltas 200/2pt, 300/3pt — over S's 150. |
| D10 | Time limits | **7200 ticks (10 min) default, 5400 on 825** (A) over M's 10800. |
| D11 | Sprites & families | `bball.png` 12x12x8 + **`bshadow.png` 12x12x4** (S's uniform 12x12 frame box and painters; M's 12x6 shadow rejected). Families `fx-bball.lua` (auto effect id 22) and `fx-bshadow.lua` (auto 23) — both sort after `fx-ball.lua` (I5). The shadow family declares **no** `on_act` (fx-list entities never act; a hook there would be a permanently uncovered function under the function=100 gate). |
| D12 | Wall reflection vs height | **z-gated (M)**: the ball reflects off impassable tiles only while `z_px < wall_top = 24`. Shots and lobs clear walls; chest passes and flat throws bank off them. This is A's "milder BANKHOUSE reading" — the pillar quartet breaks flat lanes, not lobs — and A confirmed the court plays under it. |
| D13 | Test wiring | `tests/unit/test_modes_basketball.cpp` **joins `og_unit_modes`** (S's Option A: one line at `cmake/OpenGladTests.cmake:878`; no recorder-processes edit). Fixture level-id block **9701-9709**. |
| D14 | Rim numbers | `rim_r = 12` (A's cross-court constant, soccer contact scale) + `rim_lip = 6` (M) over M's rim_r 10. Rim radius never varies per court — shot difficulty scales through arc distance, court length and furniture only. |
| D15 | Vertical physics | **M's ladder**: gravity 96 fp (0.375 px/tick²), carry 12 / head 20 / wall_top 24 / block 24 / rim 32 / goaltend 48 px, with M's ballistic solver. S's gravity 144 + alternate ladder rejected (M's arcs are solved against its ladder: a mid-range shot apexes ~40 px — above the block ceiling, inside the goaltend window). |
| D16 | Manifest names | `jump_ball` (S+A) over M's `center`; court-level `arc_radius` over A's per-hoop `arc`. |
| D17 | Shared-helper hoist | Verified (corrected by red team): `iabs`, `walker_center`, `dir8`, `drive_geometry`, `chaser_drives`, `revive_wiped_teams` exist ONLY in `mode_soccer_impl.lua` today. **`run_death_scan` does NOT** — three mode-specific, non-interchangeable variants exist: soccer's (`mode_soccer_impl.lua:564-597`, honors the `respawn_mode` submenu), CTF's (`mode_ctf_impl.lua:241`, ignores the submenu entirely), Onslaught's (`mode_onslaught_impl.lua:251`, hero-only with waypoint delay-halving). Copying soccer's verbatim would trip statement-lint rule 8 (token-identical `local function` bodies >= 4 lines across shipped sources). Ruling: **hoist** — `iabs`/`walker_center` into `mode_core`; `FACING_X/Y`, `dir8`, `drive_geometry`, `chaser_drives` (parameterized by a geometry-constants table) into `mode_ai`; `revive_wiped_teams(mask, ticks, cursor_slot)` and **soccer's submenu-honoring `run_death_scan(mask, ticks)` variant only** into `mode_match` — and refactor soccer to consume them. `mode_ctf_impl` and `mode_onslaught_impl` keep their own variants untouched (they are outside WP3's owned files; their bodies differ, so rule 8 is not tripped). Behavior identity is proven by the existing soccer suite (its director tests pin exact GOTO targets). |
| D18 | Arc paint is cosmetic | The painted three-point ring is advisory; the manifest `arc_radius` number is the sim truth. Paint order legitimately truncates rings (826 center runners, 828 alcoves), so S's paint-vs-predicate lockstep self-check is dropped; the mapgen pins arc sanity bounds and a per-quadrant runner-presence check per hoop instead (§5.5) — no whole side of an in-bounds arc may go unpainted. |
| D19 | Throw provenance watermark | The consumed-throw scan must not eat a weapon fired BEFORE possession (a stale in-flight arrow — or a returning boomerang — would vanish mid-flight and release a garbage-aimed throw; soccer's cited `run_shots` precedent is proximity-gated at `hit_radius` of the BALL, `mode_soccer_impl.lua:308`, not owner-gated). Ruling: at every possession gain (T1/T2/T13) stamp `THROW_WATERMARK` (slot 60) = the highest entity id in `og.weaplist()`; §3.1 consumes only carrier-owned live weapons with id > watermark. Entity ids are monotonic (`game_world.cpp:454-456,506-507`). A proximity gate was rejected: returning weapons (boomerangs) re-enter any radius. |
| D20 | Scatter retune + pressure | The first-draft curve (free 48 / div 6 / cap 20) made the mid-range 2 strictly dominant: 100% inside 89 px (EV 2.00) vs ~19-23% at the arc (EV ~0.6) — threes and dunks were dead content and nothing contested a standing shot. Ruling: `scatter_free = 16`, `scatter_div = 6`, `scatter_cap = 16`, plus a deterministic pressure term — `E += pressure_scatter (6)` per enemy Living within `press_radius` L1 of the shooter at release, total clamped to `scatter_cap_total = 24`. Open curve: 100% only inside 57 px (layup scale), ~77% at 72, ~50% at 88, ~37% at 100, 28.7% at/past the arc — EV(3) ~0.86 vs EV(2)@100 ~0.74, roughly flat, and pressure degrades all of it while the drive/dunk stays pressure-immune. Scatter distance D is **Euclidean** (impl-local integer `isqrt` over `euclid2`) so accuracy no longer varies up to 41% with approach angle. Dunk stays worth 2; rating it 3 is the recorded playtest fallback if drives still under-perform (R1). `shot_sweet`/`dunk_drive_range` re-derived in D23. |
| D21 | Fumble discipline | Any-hit fumbles made chip fire a full-court possession-denial button (the engine damage floor makes every landed hit >= 1, `combat_math.cpp:23-32`). Ruling: `fumble_min_damage = 2` (floor-value chip never fumbles) and `possession_grace = 12` ticks after gaining possession during which damage lands but does not arm FUMBLE (`POSSESS_SINCE`, slot 61, stamped at T1/T2/T13). Retuned jointly with D20 so drive/shoot/pass are three live corners. |
| D22 | Receiver lead + catch radius | Passes to moving receivers essentially never connected (drift = speed x Tf up to 72 px vs catch 12). Ruling: no engine change — the director GOTOs the intended receiver onto the pass landing point at release and does not re-command it while the PASS flies; the INTENDED receiver's catch radius becomes `receiver_catch_radius = 20` (interception stays 12, favoring completion over steals). A landing-spot beacon was rejected: beacons are entity-bound (`og.set_beacon` takes an entity) and no entity stands at the landing point. |
| D23 | Contested catches, dunk re-entry, ladder reorder | (a) The catch no longer unconditionally beats interception: when the receiver AND >= 1 eligible interceptor are in contact range on the same tick, oblist order among the contenders decides (the receiver keeps exclusive right only in the z band `(grab_z, catch_z]` where interceptors cannot reach). (b) Alley-oop box camping: a CATCH while inside an enemy dunk box does not presence-dunk — `DUNK_OK` (slot 62) is 0 until the carrier exits every enemy box and re-enters; ground pickups (T1/T2) always arm it, preserving the rebound put-back. (c) Handler ladder reordered for the D20 curve: open-shot-in-sweet before drive; `shot_sweet = 88` (Euclid; open P ~50%), `dunk_drive_range = 80`. (d) Throw classification uses the JOINT candidate set — hoops in shot range AND teammates in pass range compete in one best-alignment compare (tie -> hoop), so aiming dead at a cutter is a PASS even when the hoop sits behind it; "open" is defined (§4.2): no enemy Living within `press_radius` of the cutter and none within `press_radius` of the carrier-cutter midpoint. |
| D24 | Grace pinned to an entity | `GRACE_TEAM1 = 0` dereferenced LAST_TOUCHER live, so any swat restamp migrated the bar onto the blocker (barring the defender the rule exists to reward, freeing the ex-carrier). Ruling: `GRACE_ENTITY` (slot 59) is written at every grace ARM (T5 fumble -> ex-carrier; pass/flat release -> thrower) and the entity-scoped pickup bar checks it, never LAST_TOUCHER. Cleared wherever GRACE_UNTIL clears; the newest grace event overwrites both slots. |
| D25 | Turnover + wipe watchdog | T7 now CLEARS `CLOCK_*` like every other clock exit (leaving it armed re-fired T7 every regrab — announce spam and an unwinnable poison loop), and the lockout is the grace: `turnover_grace` raised 24 -> **120** ticks. Babysit deadlock (respawn-off, wiped opponents, winner loitering by the ball so the dead-ball reset never fires): `STALL_SINCE` doubles as a **wipe watchdog** — while any ACTIVE team has zero live Livings and zero pending revives, the reset countdown runs in EVERY ball state and ignores attendance; at `dead_ball_ticks` it forces a center reset, whose `revive_wiped_teams` backstop restores the wiped team. No deadlock, no babysit, no extra slot. |
| D26 | Release readability + public-geometry AI | Shot, lob, chest and flat all looked identical at release. Ruling: positional release sounds — SHOT = SOUND_BOLT, LOB = SOUND_YO, chest/flat silent — and a release announce `"THREE UP!"` when SHOT_VALUE = 3. The rebounder AI pre-positions off the target HOOP center (public, replicated geometry), never off `SHOT_LAND` — the exact scatter outcome stays sim-internal, killing the psychic boxout. |
| D27 | Point-blank release: consume QUALIFYING dead weapons | Playtest: a guarded carrier's shoot key often released nothing — a point-blank weapon dies on its own act step (blocked `walk()` → `attack(collide_ob)` → `set_dead(1)` + `death()`, `walker.cpp:1428-1448`) BEFORE pipeline step 6, and the live-only scan skipped it. Edge #24's design-time acceptance was wrong in practice: being guarded is the normal state when release matters most. Ruling: `consume_throw` consumes the FIRST **qualifying** weapon in `og.weaplist()` order owned by the carrier and above THROW_WATERMARK. Qualifying = **alive** (the original rule), OR **dead with `death_called() == 1`** (died in its own act this tick — the point-blank walk-in, wall hit, or spent range; any carrier), OR **dead under an ACT_CONTROL carrier** (the `fire()`-time pad-blocked spawn: `walker::fire()` deducts cost `walker.cpp:514`, melees the pad blocker and bare-`set_dead(1)`s its own weapon, `:594-628` — `death()` is never called there). A blanket alive-or-dead rule (the first cut of this decision) was falsified in adversarial review: **`walker::fire_check` (`walker.cpp:1245-1346`) creates a REAL weaplist weapon as its ray probe** — owner set by `create_weapon`, heading by `set_weapon_heading` — and bare-`set_dead(1)`s it on every denial/miss/success path **without paying `weapon_cost`**; an AI carrier engaging an in-range foe produces one per engagement tick (`act_random` `living.cpp:772`, COMMAND_FIRE/ATTACK/SEARCH dispatch `stats.cpp:407/548/1080`), so the blanket rule released phantom throws the carrier never triggered and the D28 refund minted unpaid mana (empirically: bot carrier + in-range foe → phantom release + `+weapon_cost` within 4 ticks). Gate soundness: probes never reach `death()` (every `fire_check` exit is a bare `set_dead(1)`), so the `death_called() == 1` arm is probe-proof, while every act-phase weapon death runs `set_dead(1)` + `death()` (`walker.cpp:1428-1448`). The `fire()`-time death is per-weapon indistinguishable from a probe (both dead, `death_called() == 0`, owner = carrier, aim set, at the pad) — the discriminator is the CARRIER: **ACT_CONTROL walkers never execute `fire_check`** (player input fires via bare `init_fire()`, `sim_input_handler.cpp:354-374`; `hit_response` returns before queueing anything on ACT_CONTROL, `stats.cpp:653`; every COMMAND_FIRE/ATTACK/SEARCH enqueue site sits inside AI acts; `mode_ai.is_directable` skips ACT_CONTROL and user-bound walkers), so a dead `death_called() == 0` weapon under an ACT_CONTROL carrier can only be that carrier's own `fire()`-time spawn. Non-qualifying dead weapons do NOT stop the scan (a later same-tick live weapon still releases). Engine soundness of consuming the dead: `GameWorld::tick` order is act phase (`game_world.cpp:1796-1846`) → `mode_run_tick` (`:1886`) → cross-ref scrub + dead sweep (`:1955-2002`; the weaplist `std::erase_if` reaps every dead weapon unconditionally), so any dead weapon the scan sees died THIS tick, its `owner()` still resolves (the scrub runs after the mode tick), and its `lastx/lasty` aim step survives death — no death path writes them (`weap::death()` `weap.cpp:158-172`; a blocked `walk()` never touches them; the only writers are ctor zeroing and the fire aim-sets). The consumption's `set_dead(1)` on an already-dead weapon is a no-op. Non-cases, verified: same-pass re-consumption is impossible (the scan `return`s after release); a weapon fired BEFORE pickup that dies this tick still fails the strict `>` watermark gate (watermark = highest weapon id at possession gain); the swat scan kills no weapons and runs after consumption anyway. Two carrier weapons fired the same tick: the FIRST qualifying one in weaplist order becomes the throw; the release clears CARRIER and leaves an airborne state, so the scan does not run again that possession — the second flies on as a plain weapon forever. "One weapon per tick" is really **one weapon per possession**. Watermark stamp timing unchanged (T1/T2/T13 only). Accepted residual: an **AI carrier's** `fire()`-time pad-blocked spawn stays unconsumed (indistinguishable from a probe from Lua; cost stands, no release — the pre-D27 behavior for exactly that shape). It is moot for bot play quality: bot throws are the director's script-invoked `release_throw` (§4.2), never weapon consumption; the AI's weapons are incidental combat. A clean fix (engine-side probe marking, or keeping probes off the weaplist) needs a `src/` change and is out of scope for this pack-Lua amendment. Supersedes the §3.1 live-only scan, the original #24 acceptance, and this decision's own first-cut blanket rule. Pinned by tests 38 (all three consumable shapes) and 41 (probe immunity). |
| D28 | Throw refunds its mana | Playtest: "throwing spends a shot" drained carriers until they could not throw at all — `walker::fire()` spawns nothing when `magicpoints < weapon_cost` (`walker.cpp:506-507`), so there was nothing to consume. Ruling: when a weapon is consumed as a throw, **refund the carrier's `weapon_cost` to its `magicpoints`** — passing and shooting are mana-neutral; non-carrying combat still costs. Exact bindings (verified): cost read `carrier:s_weapon_cost()` (integer getter, `bindings_entity.cpp:638`, registered `:2705`, declared `og-api.d.lua:184`); mp read/write via the `magicpoints` value property (`:2083`; it is in the no-method value-read list, `:2054-2056`). **The setter does NOT clamp** — `S_SET_FLT` (`:600-606`) narrows into `OG_STATS_DIRTY_FIELD`'s plain assignment (`statistics.h:64-70`) — so the Lua clamps: `carrier.magicpoints = og.min(og.fadd(carrier.magicpoints, cost), carrier.max_magicpoints)`, guarded by `cost > 0`. Float discipline: exactly one float op (`og.fadd`, C++ per-op rounding); `og.min` is a compare (std tie semantics, no rounding); **no `og.trunc`** — the value feeds a float field, not an integer branch, and truncation would destroy magic-regen fractions. Placement: the refund lands at the consumption site, immediately after `set_dead(1)` and before `release_throw` — observationally neutral today (nothing in the throw resolution reads `magicpoints`; the impl is grep-clean) and it makes *consumed ⟺ refunded* a single-site invariant. Implemented inline in `consume_throw` — no new Lua function, so the func=100 denominator is unchanged. Exploit review (revised after adversarial review): net zero holds **because of D27's qualify gate, not by construction** — the first cut refunded on any dead consumption, and `walker::fire_check`'s costless dead probes then MINTED `weapon_cost` (a drained bot self-refueled through NoMagic-denial probes, bypassing edge #28). Under the gate, every qualifying arm implies `walker::fire()` deducted the same field this tick (`walker.cpp:514`): alive and `death_called() == 1` weapons exist only via a real `fire()`, and the ACT_CONTROL dead arm is that carrier's own `fire()`-time spawn (D27's soundness argument); probes pay nothing and are never consumed (edge #29, test 41). Cost 0 families refund nothing (guard skips); a negative cost (unshipped) is refused by the same guard; the clamp kills any overfill; the AI director's weaponless `release_throw` entrance (§3.1) deducted nothing and refunds nothing. Known engine leak, accepted and out of Lua reach: `fire_check`'s no-foe exit leaves its probe ALIVE on the weaplist (`walker.cpp:1265-1283` — the one path with no `set_dead`), indistinguishable from a real shot; it was consumable before D27/D28 too (phantom), and now also refunds ≤ cost — reachable only when a queued COMMAND_FIRE dispatches after the carrier's foe slot has emptied (the per-tick auto-foe normally refills it). Known residual, accepted: a carrier already below `weapon_cost` cannot fire at all — the gate is pre-spawn in the engine, out of Lua reach; magic regen must climb past the cost before the next release (edge #28, pinned by test 40). |
| Errata | — | M's L1-diamond arc superseded by D4; M/S's 28→32 superseded by D1 (33); S §1.5 attribution ruling superseded by D5; A's shown ledger arithmetic for 825/826 was garbled but the final values (65/71) are correct — recomputed in §6. Red-team pass: first-synthesis scatter constants (48/6/20), shot_clock 288, turnover_grace 24, shot_sweet 144, dunk_drive_range 96, the owner-only throw scan, the clock-clears-on-loose-ball rule and the LAST_TOUCHER-dereferenced grace are all superseded by D19-D25. |

---

## 1. Global invariants (apply to every work package)

- **I1 — src/ byte-untouched.** Everything lives in pack Lua, `tools/modes_mapgen`,
  `scripts/gen_modes_sprites.py`, regenerated `campaigns/modes` output, `tests/`,
  `docs/`. No new binding, no protocol/replay bump, no mutation-pin churn, and
  `api_stub_check` is a no-op.
- **I2 — determinism.** Durable sim state ONLY in mode vars (64 int32 slots,
  header 0-7 owned by `mode_core.SLOT`, MODE_ID written LAST in init as the
  activation latch). Randomness ONLY `og.rand` at the pipeline points marked in
  §2.8. Integer math only (`og.div/mod/clamp/sign/trunc/min/max`); float reads
  are limited to `og.trunc()` over `w:damage()`, `w:lastx()` and `w:lasty()` —
  the three soccer-blessed reads (`mode_soccer_impl.lua:285,309-312`); no other
  float may enter mode arithmetic. No `pairs()` in sandbox code. No mutable
  upvalues (R6); the constants tables `S`/`T` are the one legitimate upvalue.
- **I3 — every existing gate stays green.** Campaign regeneration is
  byte-stable (run the generator twice, then prove `git status --porcelain --
  campaigns/` empty a third time); `test_modes_levels` invariants hold for the
  five new rows (33-char briefing lines, `-- THE GAMESMASTER` sign-off, obmap
  ledger <= 190, closed perimeter, reachability); Lua coverage line >= 95 /
  function = 100 on ALL new pack Lua — design nothing untestable, every function
  must be executable from headless unit tests; `og.award_score` never receives a
  negative (it is unsigned: `bindings_entity.cpp:1701-1703`).
- **I4 — mirror correctness.** The ball and shadow render only through
  replicated entity fields (`setxy`/`set_frame`); nothing render-side re-derives
  sim state. Mirrors never run `act()`/`animate()`; the authority writes the
  drawn position and frame every tick and snapshots carry them.
- **I5 — family ordinality.** New family files under
  `packs/modes.core/families/` sort lexicographically AFTER `fx-ball.lua` so
  `modes:ball` keeps auto id 21. `fx-bball.lua` (22) and `fx-bshadow.lua` (23)
  satisfy this; any future basketball family must too.
- **I6 — identity & registration.** Mode id 6, mode name `"BASKETBALL"`
  (10 chars < `kModeNameBytes` 12), scenario ids 824-828 (D1), manifest emitted
  by the mapgen only (never hand-edit `mode_levels.lua`), row-count literals
  28 → 33 everywhere §11.4 lists.
- **I7 — metric discipline** (new). Every distance comparison names its metric:
  contact radii (pickup/catch/swat/goaltend/dead-ball) and director geometry are
  **L1**; arc classification, shot range, AI shot_sweet AND the scatter distance
  D are **Euclidean** (d² compares; scatter takes `isqrt(euclid2)` so accuracy
  never depends on approach angle, D20); the dunk zone is a **Chebyshev box**.
  Flight-time Tf distances stay L1 (pacing only, D4). No comparison may mix
  metrics.
- **I8 — text budgets** (new). Announces and HUD lines <= 25 bytes, mode name
  <= 11, level titles <= 30, briefing lines <= 33. Every string in §8 is counted.

---

## 2. PART I — Ball physics and state machine

### 2.1 Coordinates and conventions

- Ground position/velocity and height z are **x256 fixed point** ("fp") in mode
  vars; 256 fp = 1 px; `z_px = og.div(z, 256)`. Tick rate 12/s.
- Packed positions use `core.pos_pack` (each coordinate 0-4095; every court is
  <= 255 tiles). `pos_pack(0,0) == 0` doubles as "none" — safe, (0,0) is
  perimeter wall on every court.
- The ball entity (`modes:bball`, `og.add_ob`) is the drawn, acting surface; the
  shadow (`modes:bshadow`, `og.add_fx_ob`) is the drawn ground spot. Sim truth
  is the mode vars alone (I4).

### 2.2 Mode-private slot map `S` (slots 8-62; 63 spare)

```lua
local S = {
  SCORE_LIMIT   = 8,  -- points to win (post-clamp), 1..255
  RESPAWN_TICKS = 9,  -- resolved respawn delay
  TEAM_COUNT    = 10, -- active team count 2..4
  TEAM_MASK     = 11, -- core.TEAM_BIT mask of active teams
  TIME_LIMIT    = 12, -- level ticks; timeout verdict (buzzer rule §3.7)
  ANCHOR_CURSOR = 13, -- match.place_at_anchor rotation cursor
  BALL_ENTITY   = 14, -- entity id of modes:bball (og.add_ob)
  SHADOW_ENTITY = 15, -- entity id of modes:bshadow (og.add_fx_ob)
  BALL_PX = 16, -- ball GROUND center x, fp
  BALL_PY = 17, -- ball GROUND center y, fp
  BALL_PZ = 18, -- height above ground, fp, >= 0
  BALL_VX = 19, -- px/tick fp
  BALL_VY = 20, -- px/tick fp
  BALL_VZ = 21, -- px/tick fp, signed
  BALL_STATE = 22, -- 0 FREE, 1 CARRIED, 2 SHOT, 3 PASS, 4 REBOUND
  CARRIER    = 23, -- entity id, 0 = none (nonzero iff BALL_STATE == 1)
  CLOCK_UNTIL = 24, -- absolute tick the shot clock expires; 0 = disarmed
  CLOCK_TEAM1 = 25, -- score team + 1 owning the armed clock; 0 = none
  LAST_TOUCH1 = 26, -- last-touching score team + 1; 0 = untouched
  LAST_TOUCH2 = 27, -- last DISTINCT other score team + 1 (own-basket credit)
  LAST_TOUCHER = 28, -- entity id of the last toucher, 0 = none
  GRACE_UNTIL = 29, -- absolute tick a pickup bar lifts; 0 = no bar
  GRACE_TEAM1 = 30, -- 0 = bar is entity-scoped on GRACE_ENTITY (self grace,
                    -- D24 — never a live LAST_TOUCHER deref);
                    -- else team + 1 barred from pickup (turnover grace)
  SHOT_VALUE = 31, -- 0 none / 2 / 3, classified at RELEASE (§3.1)
  SHOT_HOOP1 = 32, -- defending team + 1 of the target hoop; 0 = none
  SHOT_TEAM1 = 33, -- shooter's score team + 1 at release
  SHOT_LAND  = 34, -- pos_pack pixel landing target (scatter already applied)
  FLIGHT_TICKS = 35, -- SHOT/PASS ticks remaining; resolution fires at 0
  PASS_TARGET  = 36, -- intended receiver entity id; 0 = flat throw
  FUMBLE_TICK  = 37, -- absolute tick a qualifying hit landed on the carrier
                     -- (written by the on_damage hook); 0 = none
  JUMP_UNTIL = 38, -- absolute tick the jump-ball freeze ends
  JUMP_POS   = 39, -- packed pixel center-court spot (row.jump_ball)
  STALL_SINCE = 40, -- reset-watchdog start tick (dead-ball stall, and the
                    -- D25 wipe watchdog in any state); 0 = running play
  BALL_SPIN   = 41, -- spin phase 0..spin_cycle-1 (soccer run_spin shape)
  POINTS   = 42, -- 42..45, +team: the mode metric (may go negative, forfeit)
  HOOP_POS = 46, -- 46..49, +team: packed pixel HOOP CENTER team t DEFENDS
                 -- (banked for active teams only; 0 = no hoop)
  ARC_RADIUS = 50, -- three-point release distance, px (row.arc_radius)
  SPAWN_CAP  = 51, -- 51..58, +team byte 0-7 (caps.bank_caps; -1 = uncapped)
  GRACE_ENTITY    = 59, -- entity id the entity-scoped grace bars (D24);
                        -- written at every grace ARM, cleared with GRACE_UNTIL
  THROW_WATERMARK = 60, -- highest weaplist entity id at possession gain (D19);
                        -- only carrier weapons with id above it are consumable
  POSSESS_SINCE   = 61, -- tick the current possession began (D21 fumble grace)
  DUNK_OK         = 62, -- 1 = presence dunk armed; 0 after a catch inside an
                        -- enemy box until exit + re-entry (D23)
  -- 63 spare (LAST one — R4; repack candidates: DUNK_OK, GRACE_TEAM1,
  -- SHOT_VALUE are all sub-byte)
}
```

`PHASE` stays 1 for the whole match; `BALL_STATE` is the real machine.

### 2.3 Tuning table `T` (authoritative; all integers)

```lua
local T = {
  -- Vertical physics (D15). Integration per tick: z += vz; vz -= gravity.
  gravity = 96,          -- fp px/tick^2 = 0.375

  -- Height ladder (px). Below block_ceiling is contestable; (block_ceiling,
  -- rim_z] ascending is the untouchable arc; (rim_z, goaltend_ceiling] near
  -- the target hoop, descending, is the goaltend window.
  carry_z = 12,          -- carried/release height; pass catch height
  head_z = 20,           -- above this walkers cannot touch the ball
  wall_top = 24,         -- above this the ball clears wall tiles (D12)
  block_ceiling = 24,    -- weapon swats connect at or below
  rim_z = 32,            -- rim plane; SHOT solves to land at this height
  goaltend_ceiling = 48,
  goaltend_radius = 24,  -- L1 from the target hoop for the goaltend arm

  -- Contact radii (L1; soccer kick/hit_radius = 12 parity; pickup 12 ==
  -- GOTO arrival distance 12, the anti-milling identity).
  pickup_radius = 12,    -- FREE/REBOUND ground grab
  catch_radius = 12,     -- interception contact (any non-receiver)
  receiver_catch_radius = 20, -- INTENDED receiver only (D22: moving receivers
                         -- must be able to complete; steals stay at 12)
  swat_radius = 12,      -- weapon-to-ball swat contact
  grab_z = 20,           -- = head_z: max z_px for pickup/interception
  catch_z = 24,          -- intended receiver may catch slightly higher

  -- Rim (D14). At SHOT resolution, d = L1(ground, target hoop):
  -- d <= rim_r basket; rim_r < d <= rim_r + rim_lip clang; beyond, airball.
  rim_r = 12,
  rim_lip = 6,

  -- Throw classification (§3.1). Aim cone |cross| * aim_den <= dot * aim_num,
  -- tan(half-angle) = 1/2 (~26.6 degrees either side).
  aim_num = 1,
  aim_den = 2,
  -- Shot range is PER COURT: euclid2(carrier, hoop) <= (ARC_RADIUS + 64)^2 (D4).
  shot_range_pad = 64,
  chest_range = 96,      -- L1; pass at or below = chest, above = lob
  pass_range_max = 192,  -- L1; beyond, no pass target is recognized

  -- Flight time Tf (ticks) from D = L1 px release->target:
  --   SHOT  Tf = clamp(div(D, arc_speed), shot_tf_min, shot_tf_max)
  --   CHEST Tf = clamp(div(D, chest_speed), chest_tf_min, chest_tf_max)
  --   LOB   Tf = clamp(div(D, lob_speed), lob_tf_min, lob_tf_max)
  --   FLAT  Tf = flat_tf fixed, range flat_speed * flat_tf = 96 px
  arc_speed = 6,  shot_tf_min = 10,  shot_tf_max = 30,
  chest_speed = 8, chest_tf_min = 4, chest_tf_max = 12,
  lob_speed = 4,  lob_tf_min = 16,  lob_tf_max = 36,
  flat_speed = 8, flat_tf = 12,

  -- Shot scatter (D20), drawn ONCE at release, folded into SHOT_LAND.
  -- D = isqrt(euclid2(release, target)) (Euclidean, I7 — impl-local integer
  -- isqrt, deterministic):
  --   base  = min(scatter_cap, div(max(0, D - scatter_free), scatter_div))
  --   press = pressure_scatter * (enemy Livings within press_radius L1 of the
  --           shooter at release; "enemy" = a SCORE team differing from the
  --           shooter's — neutral/wildlife livings never press)
  --   E     = min(scatter_cap_total, base + press)
  --   off = og.rand(2 * E + 1) - E per axis (bound >= 1 always; lint rule 7
  --   needs no rand0 guard).
  -- Open curve: 100% at D <= 57 (E <= 6 keeps |ox|+|oy| <= rim_r), ~77% at 72,
  -- ~50% at 88, ~37% at 100, 28.7% at/past the arc (cap) — EV(3) ~0.86 vs
  -- EV(2)@100 ~0.74; pressure degrades everything, the drive does not care.
  scatter_free = 16,
  scatter_div = 6,
  scatter_cap = 16,
  pressure_scatter = 6,
  scatter_cap_total = 24,

  -- Rim clang: pop + push away from the hoop + per-axis random deflection
  -- v_axis = dir8_component * rim_speed + (og.rand(2*rim_scatter+1) - rim_scatter).
  rim_pop = 512,      -- 2 px/tick up
  rim_speed = 768,    -- 3 px/tick away
  rim_scatter = 512,

  -- Ground bounce (REBOUND at z <= 0 with vz < 0); settle to FREE when the
  -- bounced |vz| < settle_vz.
  floor_rest_z = 128,   -- keep half the vertical speed
  floor_rest_xy = 192,  -- keep three quarters horizontal
  settle_vz = 256,

  -- Grounded roll (FREE): soccer's linear friction and substep bound.
  roll_friction = 64,
  substep_px = 8,

  -- Spin frames: soccer constants for the roll; airborne states advance a
  -- constant air_spin per tick signed by the dominant axis (backspin look).
  spin_step = 256, spin_cycle = 2048, spin_divisor = 8,
  air_spin = 192,

  -- Loose-ball pops. fumble_pop apexes ~8 px (under head_z: grabbable through
  -- the scramble); horizontal scatter per axis in whole px/tick:
  -- v = (og.rand(2 * fumble_scatter + 1) - fumble_scatter) * 256.
  fumble_pop = 640,
  fumble_scatter = 3,
  toss_pop = 768,       -- jump-ball toss (apex ~12 px)

  -- Clocks (D7, D25). 420 = 35 s; countdown suffix at <= clock_hud remaining;
  -- one-shot announce at exactly clock_warn remaining. The deadline clears
  -- ONLY on SHOT release / turnover / center reset and persists across every
  -- same-team regain (§3.6).
  shot_clock = 420,
  clock_hud = 120,
  clock_warn = 36,
  turnover_grace = 120, -- team barred after a shot-clock turnover (D25)
  self_grace = 12,      -- thrower barred from re-grabbing its own release
  possession_grace = 12, -- ticks after gaining possession before damage can
                         -- arm FUMBLE (D21)
  fumble_min_damage = 2, -- on_damage amount below this never fumbles (D21;
                         -- the engine floor makes every landed hit >= 1)
  jump_freeze = 36,     -- soccer kickoff_freeze parity
  dead_ball_ticks = 600, dead_ball_radius = 160,  -- soccer parity; the same
                        -- 600 drives the wipe watchdog (D25)

  -- Scoring (D3, D9, D10).
  dunk_half = 24,       -- Chebyshev half-extent of the dunk box (3x3 carpet)
  score_limit = 21,
  point_score = 100,    -- og.award_score delta per point (200/2pt, 300/3pt)
  time_limit_ticks = 7200,
  respawn_ticks = 60,

  -- AI director (§4; ranges re-derived from the D20 curve per D23).
  ai_cadence = 15,
  dunk_drive_range = 80,   -- L1: pressed bot carrier inside this drives
  shot_sweet = 88,         -- Euclidean px: open bot shot range (P ~50%+)
  press_radius = 24,       -- L1: an enemy this close = contested (also the
                           -- pressure-scatter and open-cutter radius)
  clock_panic = 36,        -- clock remaining at/below forces the shot
  pass_gain = 32,          -- cutter must be this much closer to earn a pass
  cutter_perp = 32, cutter_standoff = 40,
  rim_protect_standoff = 32,  -- retuned for the 24 px dunk box (D3)
  rebound_boxout = 12,
  drive_offset = 8, drive_cross = 24, drive_cross_hold = 32, drive_reach = 10,
                           -- soccer chaser_drives constants, via ai.* (D17)
  oob_ring_max = 8,        -- landing-legality ring scan bound (tiles)
  shadow_band = 12,        -- shadow frame = clamp(div(z_px, 12), 0, 3)
}
```

Solver sanity (derivations to keep in the impl comments): a Tf=20 shot rises
`vz0 = div(20*256, 20) + div(96*19, 2) = 1168` fp and apexes ~40 px (above
wall_top/block_ceiling, inside goaltend_ceiling); a Tf=24 lob apexes ~37 px
(over head_z for the middle of its flight); a Tf=8 chest pass apexes ~14 px
(under head_z the whole way — interceptable).

### 2.4 Per-state behavior

**FREE** — loose ball on the ground (z = 0), possibly rolling. Movement =
soccer `run_flight` shape: sub-stepped horizontal motion, axis-separated wall
reflection via `og.query_grid_passable` on the 12x12 rect, linear
`roll_friction`. The basketball **never calls `attack`** — zero combat RNG from
the ball. Pickup (§2.5 T1) for any live Living within `pickup_radius`, oblist
order, honoring grace bars and the jump freeze. The dead-ball arm of the reset
watchdog runs here only (the wipe arm runs in every state, D25).

**CARRIED** — the mode pins BALL_PX/PY to the carrier's center each tick,
z = `carry_z`, velocities 0. The carrier is the interaction surface: damage
arms a fumble via `on_damage` (T5) once `possession_grace` has elapsed and the
hit is `>= fumble_min_damage` (D21). The shot clock runs; dunk detection runs
(gated on DUNK_OK, D23); a carrier-fired weapon with entity id above
THROW_WATERMARK becomes a throw (§3.1, D19). While CARRIED and outside every
enemy dunk box, DUNK_OK re-arms to 1.

**SHOT** — ballistic arc at a specific hoop; outcome frozen at release
(SHOT_VALUE/HOOP1/TEAM1/LAND, FLIGHT_TICKS). Constant vx/vy toward SHOT_LAND;
z += vz, vz -= gravity. Wall reflection only while `z_px < wall_top` (D12).
Walkers never touch a SHOT — bodies do not block shots, weapons do (§3.5).
FLIGHT_TICKS decrements; at 0 the shot resolves at the rim (§3.2). Timer-based
resolution deliberately avoids crossing-detection degeneracy on short flat arcs.

**PASS** — same integrator; `PASS_TARGET` names the intended receiver (0 =
flat throw). Contact each tick after movement (D23): gather the tick's
contenders — the intended receiver if live, within `receiver_catch_radius`,
`z_px <= catch_z` (checked against its CURRENT position); every other live
Living within `catch_radius` at `z_px <= grab_z` (grace bars applied — the
entity bar reads GRACE_ENTITY). One contender → it takes the ball. Receiver
plus >= 1 interceptor on the same tick → **oblist order among the contenders
decides** (a defender standing on the receiver has a real, deterministic
chance; the receiver keeps exclusive right only in the z band
`(grab_z, catch_z]` where interceptors cannot reach). A catch by a receiver
whose live team differs from the thrower's team at release (charm mid-flight)
resolves as an interception (§9 #22). Then weapon swat at
`z_px <= block_ceiling` (§3.5). At FLIGHT_TICKS 0 uncaught, convert to REBOUND
with current velocity — **the shot clock stays armed** (D7/D25). A PASS
descending across the rim plane inside a rim scores like a rebound crossing
(§3.3) — the lob toss-in is legal.

**REBOUND** — live airborne loose ball (rim clangs, blocks, fumbles, uncaught
passes, airballs, the jump toss). Full 3D integration: substeps + wall
reflection while `z_px < wall_top` (this is where backboard banks happen),
gravity, ground bounce with `floor_rest_z/xy`, settle to FREE when the bounced
`|vz| < settle_vz` (z clamps to 0, horizontal speed carries into the roll).
Air grab: pickup rules apply whenever `z_px <= grab_z`. Weapon swat while
`z_px <= block_ceiling` re-launches it (tip). Scoring: a descending crossing of
the rim plane (`prev z_px > rim_z >= new z_px`, vz < 0) inside `rim_r` of any
ACTIVE hoop scores 2 for LAST_TOUCH1 (§3.3).

### 2.5 Transition table (complete)

| # | From | To | Trigger |
|---|------|----|---------|
| T1 | FREE | CARRIED | live Living within `pickup_radius`, `now >= JUMP_UNTIL`, grace bars pass; FIRST in oblist order wins contention. Every possession gain (T1/T2/T13) stamps THROW_WATERMARK + POSSESS_SINCE (D19/D21) and runs the clock-team late-regain check (§3.6) BEFORE re-arm |
| T2 | REBOUND | CARRIED | same, plus `z_px <= grab_z` |
| T3 | CARRIED | SHOT | carrier-fired weapon (id > THROW_WATERMARK) consumed, aim classifies SHOT (§3.1) |
| T4 | CARRIED | PASS | consumed weapon classifies chest/lob pass or flat throw |
| T5 | CARRIED | REBOUND | fumble: FUMBLE_TICK set by on_damage (hit >= `fumble_min_damage` landing at/after POSSESS_SINCE + `possession_grace`, D21), resolved in the mode tick with `fumble_pop` + scatter; entity-scoped self grace on the ex-carrier (GRACE_ENTITY = ex-carrier, D24). Clock stays armed (D25) |
| T6 | CARRIED | REBOUND | carrier death or vanished handle: drop-in-place, same pop/scatter; clock stays armed |
| T7 | CARRIED | REBOUND | shot-clock expiry (in CARRIED, or fired at a clock-team regain past the deadline, §3.6): turnover pop + TEAM-scoped `turnover_grace` on the ex-carrier's team, **CLOCK_\* cleared** (D25), announce "TURNOVER!". `drop_ball` scrubs the in-flight facts (`clear_flight`) — the mid-catch late regain is the one entrance where PASS_TARGET/FLIGHT_TICKS are still live, and a REBOUND must carry no pass residue |
| T8 | SHOT | (score) | FLIGHT_TICKS 0, `L1(ground, hoop) <= rim_r` — basket (§3.2), center reset |
| T9 | SHOT | REBOUND | FLIGHT_TICKS 0, rim band `(rim_r, rim_r + rim_lip]`: clang pop + away-push + scatter |
| T10 | SHOT | REBOUND | FLIGHT_TICKS 0, airball: velocity carries on |
| T11 | SHOT | REBOUND | weapon swat at `z_px <= block_ceiling` (BLOCK) or offensive tip in the goaltend window: swat impulse, toucher restamped |
| T12 | SHOT | (score) | defensive swat in the goaltend window: GOALTENDING — SHOT_VALUE to SHOT_TEAM1, center reset (§3.5) |
| T13 | PASS | CARRIED | catch or interception (same-tick contention by oblist order, D23; charmed receiver = interception); a catch inside an enemy dunk box sets DUNK_OK = 0 (D23) |
| T14 | PASS | REBOUND | FLIGHT_TICKS 0 uncaught (clock stays armed, D25); or block-window swat |
| T15 | PASS | (score) | descending rim-plane crossing inside a rim, 2 pts (§3.3) |
| T16 | REBOUND | (score) | descending rim-plane crossing inside a rim (tip-in/bank), 2 pts |
| T17 | REBOUND | FREE | ground settle (clock stays armed, D25) |
| T18 | any | FREE | center reset after every score, dead-ball reset or wipe-watchdog firing (D25); landing-legality relocation (§2.7) re-spots in place instead |
| T19 | FREE | REBOUND | jump toss at `now == JUMP_UNTIL` exactly: one-shot `toss_pop`, vz only |

Every `(score)` funnels through one `score_basket(team, value, label)` /
`own_basket(hoop_team, value)` pair (§3.3), which always ends in `center_reset`
(§3.8).

### 2.6 Ballistic release solver (shared by T3/T4)

Given release ground center `(x0, y0)` fp — re-pinned to the carrier's
CURRENT post-act center at the top of `release_throw` on both entrances, so
classification, scatter and the solved flight share one origin (run_carry's
pin is one act-phase step stale on the consumed-weapon tick) — target pixel
`(tx, ty)`, target height `hz` px (rim_z for shots, carry_z for passes, 0 for
flat throws), and Tf from the distance rule:

```
vx  = og.div(tx * 256 - x0, Tf)
vy  = og.div(ty * 256 - y0, Tf)
vz0 = og.div((hz - carry_z) * 256, Tf) + og.div(T.gravity * (Tf - 1), 2)
```

With `z += vz; vz -= gravity` per tick, after exactly Tf ticks
`z = hz*256 ± Tf/2` fp (truncation < 0.06 px, absorbed by the rim bands).
`FLIGHT_TICKS = Tf`; the ARC-SHOT arm additionally records
`SHOT_LAND = pos_pack(tx, ty)` (scatter already folded in) — passes and flat
throws leave SHOT_LAND at 0.

### 2.7 Landing legality (OOB rule)

Whenever an airborne ball would occupy impassable ground at low altitude
(REBOUND ground touch, or any SHOT/PASS → REBOUND conversion moment), test
`og.query_grid_passable` on the ball rect (grid-only; draws no RNG). If
impassable: deterministic re-spot — enumerate L1 tile rings r = 1..
`oob_ring_max` around the ball's tile, each ring walked clockwise from due
north; the first tile whose 12x12 ball rect passes (probe point `tile*16 + 2`)
takes the ball as a FREE dead ball (v = 0, z = 0), no grace, attribution kept.
No candidate within 8 rings (pathological) → full center reset. Covers arc
shots sailing over a backboard stub into the perimeter and lobs onto scenery.

### 2.8 on_mode_tick pipeline (deterministic; `*` marks the RNG draw points)

1. `match.run_death_scan(mask, ticks)` (respawn-submenu semantics, D17).
2. Single oblist walk: livings list, generators list, marked-spawn counts.
3. Carrier liveness: CARRIER set but handle nil/dead → T6 drop (* scatter).
4. Fumble resolution: FUMBLE_TICK set and still CARRIED → T5 (* scatter).
5. Dunk check (§3.4) — a dunk beats a same-tick throw.
6. Throw consumption + release (§3.1) (* shot scatter).
7. Swat scan (§3.5).
8. Ball physics by state (§2.4): movement, catches/intercepts, rim resolution
   (* rim scatter), crossings, ground bounce, landing legality.
9. Pickup scan (T1/T2).
10. Reset watchdog (§3.9): dead-ball stall (FREE only) + wipe watchdog (every
    state, D25) — both run on STALL_SINCE.
11. Shot-clock expiry + warning (§3.6).
12. Director + `caps.apply_caps` at `og.mod(og.world_tick(), ai_cadence) == 0`.
13. Win / timeout check (§3.7).
14. Presentation pinning: ball `setxy(gx - 6, gy - z_px - 6)` + spin frame;
    shadow `setxy(gx - 6, gy - 6)` + height frame
    `og.clamp(og.div(z_px, T.shadow_band), 0, 3)`; HUD (§8); beacon 0 = shadow,
    beacon 1 = carrier or nil.

The `on_damage` hook (`on_damage(target, attacker, amount)`) runs during the
act phase, strictly before all of the above. It gates on the MODE_ID latch, and
when `og.entity_id(target) == CARRIER` AND `amount >= T.fumble_min_damage` AND
`og.world_tick() >= POSSESS_SINCE + T.possession_grace` (D21) writes
`FUMBLE_TICK = og.world_tick()`. It always returns nil — authored damage
untouched (the return-0-is-not-a-cancel trap never arises). Damage therefore
beats a same-tick release by pipeline construction: a hit carrier's fired
weapon is NOT consumed and flies as a plain weapon. Sub-threshold chip and
hits inside the possession grace still hurt the carrier — they just do not
strip the ball.

---

## 3. PART II — Throws, scoring, clocks

### 3.1 Throw resolution (consumed weapon → shot / lob / chest / flat)

Runs while CARRIED (pipeline step 6). Scan `og.weaplist()` in list order for
the FIRST **qualifying** weapon whose `owner()` is the carrier **and whose
entity id is above THROW_WATERMARK** (D19 — a weapon fired before
possession, a returning boomerang included, is never consumed; it flies on as
a plain weapon, and the strict `>` gate excludes it even on the tick it dies).
Qualifying (D27): alive; dead with `death_called() == 1` (died in its OWN
act this tick — point-blank wall/enemy contact; the dead sweep runs after
the mode tick, `game_world.cpp:1996`, so it is still on the list with owner
and aim step intact, and its contact damage stands); or dead under an
ACT_CONTROL carrier (the `fire()`-time pad-blocked spawn). A dead
`death_called() == 0` weapon under an AI carrier is treated as a
`walker::fire_check` scratch probe and skipped WITHOUT stopping the scan
(D27 — consuming probes released phantom throws and minted the D28 refund;
edge #29). Note the
real soccer precedent is proximity-gated, not owner-gated
(`run_shots` requires `hit_radius` of the ball, `mode_soccer_impl.lua:308`);
the watermark is basketball's provenance gate. The matched weapon is
**consumed** — `shot:set_dead(1)`, a no-op when it is already dead — the
carrier's `weapon_cost` is refunded to its `magicpoints`, clamped at
`max_magicpoints` (D28: throwing is mana-neutral; ammo still stands), and its
per-tick step
`(ax, ay) = (og.trunc(shot:lastx()), og.trunc(shot:lasty()))` is the aim
vector; a (0,0) step aims along the carrier's facing (`ai.FACING_X/Y`, soccer's
dead-center rule). One weapon per **possession** (D27): the release clears
CARRIER, so a later same-tick weapon flies on normally and never qualifies
again. Exploding families die without attacking (the engine reaps; a cosmetic
death puff is accepted).

Classification (D23d): build ONE candidate set — hoops of OTHER active teams
(own hoop is never a shot target, so a SHOT can never be an own basket) with
`euclid2(carrier, hoop) <= (ARC_RADIUS + shot_range_pad)²` (D4), AND live
teammates within `pass_range_max` L1. Aim test per candidate offset `(hx, hy)`
from carrier center: `dot = ax*hx + ay*hy > 0` and
`iabs(cross) * T.aim_den <= dot * T.aim_num` (`cross = ax*hy - ay*hx`). The
single **best-aligned** candidate wins across the whole set — compare
`iabs(cross_a) * dot_b` vs `iabs(cross_b) * dot_a` (cross-multiplied, no
division); exact ties go to a hoop over a teammate, then lower team index /
earlier oblist slot. Aiming dead at a cutter is therefore a PASS even when its
hoop sits behind it — the hoop wins only when it is genuinely better aligned.

1. **ARC SHOT** — winner is a hoop. Value at RELEASE: `euclid2 > ARC_RADIUS²`
   → 3, else 2. Draw scatter (T formula, D20: D = isqrt(euclid2), pressure
   counted at release), pack SHOT_LAND, solve §2.6 to rim_z, clear the shot
   clock, stamp toucher = shooter, state = SHOT. Release readability (D26):
   positional SOUND_BOLT at the shooter; `"THREE UP!"` announced when the
   value is 3 (the rim speaks for the rest).
2. **PASS** — winner is a teammate. `D <= chest_range` → **CHEST** (fast,
   flat, interceptable); else **LOB** (slow, apex over head_z — safe in the
   middle, contestable at the ends; positional SOUND_YO at release, D26).
   PASS_TARGET = receiver id, entity-scoped `self_grace` on the thrower
   (GRACE_ENTITY = thrower, D24), **clock keeps running** (§3.6), state = PASS.
3. **FLAT THROW** — empty candidate set: target = carrier center +
   `dir8(ax, ay) * flat_speed * flat_tf` clamped to [0, 4095] per axis, target
   height 0, Tf = flat_tf, PASS_TARGET = 0, self grace, clock keeps running,
   state = PASS. A flat throw is an uncatchable-by-design pass — anyone may
   take it low. Silent at release (D26).

The AI director calls the same internal `release_throw(carrier, ax, ay)` with a
synthetic aim vector and no weapon to consume (§4.2) — one classification body,
two entrances.

### 3.2 Shot resolution (FLIGHT_TICKS == 0)

`d = L1(ball ground center, HOOP_POS[SHOT_HOOP1 - 1])`:

- `d <= rim_r` — **basket**: SHOT_VALUE points to SHOT_TEAM1-1
  (`"BASKET! {COLOR} +2"` / `"THREE! {COLOR} +3"`, SOUND_MONEY), center reset.
- `rim_r < d <= rim_r + rim_lip` — **rim clang**: SOUND_CLANG, state REBOUND
  with `vz = rim_pop`, horizontal = `dir8(ball - hoop) * rim_speed` + per-axis
  rim_scatter draw. The scrum is live; LAST_TOUCH stays the shooter until
  someone touches.
- beyond — **airball**: state REBOUND, velocity carries, gravity finishes it.

Only the TARGET hoop is tested; the hoop-separation authoring rule (§5.5) makes
cross-rim landings impossible. The outcome is decided at release — the flight
is presentation plus the two interaction windows (block, goaltend).

### 3.3 Crossing baskets, attribution, own baskets

REBOUND/PASS descending rim-plane crossings inside an ACTIVE hoop's `rim_r`
score **2** for `LAST_TOUCH1 - 1`. Attribution mirrors soccer's
`stamp_toucher` exactly: every possession gain, throw release and swat restamps
LAST_TOUCHER/LAST_TOUCH1 and demotes a different outgoing score team into
LAST_TOUCH2; an unowned-weapon swat clears LAST_TOUCH1 only. The ball entity
wears the toucher's team color (`set_team_num`), neutral after resets.

**Own basket** (crossing team == the hoop's defending team t): beneficiary =
soccer's `own_goal_beneficiary` logic verbatim — two active teams: the
opponent; 3-4 teams: LAST_TOUCH2 if valid/active/different; otherwise
**forfeit**: `POINTS[t] -= value` on the mode metric ONLY — `og.award_score`
is unsigned and NEVER receives the negative (I3). Announce
`"OWN BASKET! {COLOR} +2"` / `"OWN BASKET! {COLOR} -2"` (max 21 chars).
Untouched crossings (LAST_TOUCH1 == 0) score nothing and play on.

### 3.4 Dunk

While CARRIED, after fumble/death resolution and BEFORE throw consumption:
if `DUNK_OK == 1` and the carrier center is inside the dunk box
(`|dx| <= dunk_half and |dy| <= dunk_half`, D3) of any ACTIVE team t ~= the
carrier's score team — **2 points** to the carrier's team
(`"DUNK! {COLOR} +2"`, SOUND_MONEY), center reset. The carrier had to walk the
ball through the defense; that is the whole gate. Dunking one's own hoop is
impossible by the `t ~= team` predicate.

**DUNK_OK arming (D23b)** — kills the alley-oop auto-dunk while preserving the
put-back: set to 1 at every GROUND pickup (T1/T2 — a rebound scooped inside
the box still dunks next tick) and at a CATCH whose catch point is outside
every enemy dunk box; set to 0 at a catch INSIDE an enemy box. While CARRIED,
the first tick the carrier center is outside all enemy boxes re-arms it to 1 —
a box-camping catcher must step out and drive back in through the defense.

### 3.5 Block windows and goaltending (weapon swats)

Swat scan (airborne states, after throw consumption, before movement): first
live weapon in weaplist order within `swat_radius` L1 of the ball GROUND
center that CAN swat; weapon z ignored (weapons fly flat; their reach is the
abstraction); jump freeze guards it. **Zero-step ruling**: a weapon whose
per-tick step is (0, 0) — a boomerang at turnaround — has no impulse
direction, cannot swat, and is passed over: a later in-radius weapon with a
real step still contests the ball the same tick. (The goaltend arm needs no
step — the interference itself is the offense.)

- **Goaltend window** — state SHOT, vz < 0, `rim_z < z_px <= goaltend_ceiling`,
  ball within `goaltend_radius` L1 of the TARGET hoop. Swat by a team ~=
  SHOT_TEAM1-1 (owner-chain-root Living's score team) = **GOALTENDING**: the
  basket counts — SHOT_VALUE to SHOT_TEAM1-1, `"GOALTEND! {COLOR} +N"`, center
  reset. Swat by the shooter's own team = offensive tip: convert to REBOUND
  with the swat impulse, restamp — the crossing rule may still make it 2. The
  tip is SILENT: `"BLOCK!"` belongs to the block window only.
- **Block window** — `z_px <= block_ceiling`, any airborne state: legal
  contest. Convert to REBOUND: horizontal = weapon step direction scaled
  `clamp(trunc(damage) * 2, 4, 12)` px/tick (soccer shot-impulse constants),
  `vz += 512` pop; restamp toucher to the weapon's owner root (unowned → clear
  LAST_TOUCH1); announce `"BLOCK!"` when the victim state was SHOT. The
  swatting weapon is NOT consumed — only the carrier's own throw is. A restamp
  never moves a grace bar: the entity-scoped bar is pinned to GRACE_ENTITY
  (D24), so a blocker who swats during someone else's self-grace window may
  grab the ball immediately.
- Between the windows — ascending above block_ceiling, or outside the goaltend
  cone — the arc is untouchable by design (the apex sanctuary).

### 3.6 Shot clock (D7, D25)

Team-scoped. **On every possession gain** by team t (pickup OR catch, from any
state), in order:

1. Late-regain turnover: `CLOCK_TEAM1 == t + 1` armed and
   `now >= CLOCK_UNTIL` → immediate turnover at the gain point (T7 shape:
   pop, team grace, clear CLOCK_*, announce) — checked BEFORE any re-arm.
2. Same-team persistence: `CLOCK_TEAM1 == t + 1` armed → the deadline
   PERSISTS unchanged. An uncaught own pass, a flat throw rolled dead and
   regrabbed, a fumble scooped by a teammate, an offensive ground pickup —
   none of them refresh the clock. This is the anti-launder rule: a deliberate
   miss is never better than a catch.
3. Otherwise (different team, or disarmed): `CLOCK_TEAM1 = t + 1,
   CLOCK_UNTIL = now + shot_clock` — fresh clock, defensive rebounds and
   steals included.

**Clearing is exhaustive** (D25): SHOT release (a genuine attempt satisfies
the clock; the rebound re-arms fresh for whoever gains it — offensive rebounds
earn a new clock, real-rules style), the turnover itself (T7 — the 120-tick
team grace is the lockout), and every center reset (score, dead ball, wipe
watchdog). **No loose-ball transition clears it**: T5 fumble, T6 carrier
death, T14 uncaught pass and T17 ground settle all leave the deadline armed.
Expiry fires in CARRIED (T7) or at a clock-team regain (rule 1). HUD countdown
suffix while remaining <= `clock_hud` (§8); one-shot `"SHOT CLOCK!"` announce
at exactly `clock_warn` remaining (equality against a fixed deadline fires
once; no flag slot). If the carrier's live team ever differs from
CLOCK_TEAM1-1 mid-possession (charm), re-arm fresh for the new team.

### 3.7 Win, timeout, buzzer beater

After ball logic each tick: any active team with `POINTS >= SCORE_LIMIT` →
`core.declare_team_win` (first in team order). Timeout: at
`level_tick >= TIME_LIMIT`, **deferred while BALL_STATE == SHOT** — a buzzer
beater resolves first (bounded: FLIGHT_TICKS <= shot_tf_max = 30; a rim clang
ends the deferral — tips after the buzzer do not count). The deferral rides
the same ball/shadow handles success that ran ball logic this tick:
FLIGHT_TICKS only counts down while ball logic runs, so a vanished ball
handle (§9 #13) cannot freeze the buzzer forever. Verdict ladder:
highest POINTS, then `og.team_score`, then lowest team byte
(`match.timeout_leader` with POINTS as the metric). The engine re-asserts the
latch and runs no more Lua afterward.

### 3.8 Center reset (jump ball)

`CARRIER = 0`; state FREE; velocities and z zeroed; LAST_TOUCH1/2,
LAST_TOUCHER, GRACE_* (GRACE_ENTITY included), SHOT_*, PASS_TARGET,
FUMBLE_TICK, CLOCK_*, THROW_WATERMARK, POSSESS_SINCE cleared; DUNK_OK = 1;
STALL_SINCE 0; spin 0 + `set_frame(0)`; ball team neutral; ball placed at
JUMP_POS; `JUMP_UNTIL = now + jump_freeze`;
`match.revive_wiped_teams(mask, ticks, S.ANCHOR_CURSOR)` (soccer's kickoff
backstop, hoisted per D17 — a wiped active team is revived or reprovisioned at
every reset regardless of the respawn submenu). During the freeze: no pickup,
no swat, no ball motion. At `now == JUMP_UNTIL` exactly: the toss (T19), and
the grab race is live — first Living in oblist order wins the tip.

### 3.9 Turnover shapes and the reset watchdog (complete list)

Steal = fumble by damage (T5, D21 thresholds); interception / contested-catch
loss (T13, D23); shot clock (T7, 120-tick team grace, clock cleared — D25);
carrier death (T6); dead ball — FREE, motionless, no live Living within
`dead_ball_radius` for `dead_ball_ticks` → `"BALL RESET"`, center reset
(soccer `run_dead_ball` shape, includes the revive backstop); score (center
reset); OOB-equivalent = landing legality (§2.7, re-spot in place, no grace —
a neutral scramble).

**Wipe watchdog (D25)**: STALL_SINCE doubles as the wipe clock. While any
ACTIVE team has zero live Livings AND zero pending revives
(`og.respawn_pending_count(team) == 0`), the countdown runs in EVERY ball
state and ignores attendance; continuous for `dead_ball_ticks` → center reset
(→ `revive_wiped_teams`). A winner cannot babysit a free ball, ride the
turnover cycle, or keep the ball carried to deny a wiped opponent its revive:
600 ticks after the wipe, the game resets and the team returns. When no team
is wiped, STALL_SINCE reverts to plain dead-ball semantics (FREE only,
attendance clears it).

---

## 4. PART III — AI director

Cadence 15, per active team in team order, members in oblist order, humans
excluded by `ai.is_directable`, zero RNG. Primitives: `ai.is_directable /
issue_front / may_preempt / dist_to / nearest_unassigned / dir8 /
chaser_drives` (last two hoisted per D17), `set_foe / set_leader`,
`C.COMMAND_GOTO`. Anti-milling doctrine: GOTO's arrival distance (12) equals
the pickup/contact radius (12), so every contact-seeking command targets a
point ON or THROUGH the objective with the half-body top-left correction —
never a standoff point.

### 4.1 Per-cadence bookkeeping

Build the directable member list per team; assign roles with
`ai.nearest_unassigned` (ties to earlier oblist slot). Non-engaged members get
`set_foe(nil)` + `set_leader(ball)` (suppresses the auto-foe backstop; brawls
stay brief). Soccer's peel rule stands: at most ONE non-role member per team
may keep an active combat command (`not ai.may_preempt(front)`); everyone else
is pulled back into the scheme.

### 4.2 Team in possession

- **HANDLER** = the carrier. Human → untouched. Bot decision ladder (D23c),
  first match wins (target hoop = nearest active enemy hoop by L1); the
  ordering follows the D20 curve — open shot first, pressure-immune drive when
  contested, pass when covered:
  1. No enemy Living within `press_radius` of the carrier AND
     `euclid2(carrier, hoop) <= shot_sweet²` → SHOOT:
     `release_throw(carrier, hx, hy)` with aim = hoop − carrier. The
     script-invoked release IS the mode's throw function — bots need no input
     path, and both entrances share one classification body.
  2. `dist_l1(carrier, hoop) <= dunk_drive_range` → DRIVE: GOTO through the
     hoop — target = hoop center + `dir8(hoop - carrier) * drive_offset` −
     half-body; the dunk box (half-extent 24 > arrival 12) triggers en route.
     Contact en route risks the fumble (post-grace, D21) — the intended
     tension, and under pressure the dunk is the only scatter-free 2.
  3. Clock remaining <= `clock_panic` → SHOOT regardless of pressure. (A panic
     release from beyond shot range classifies as a flat heave — accepted.)
  4. An **open** cutter within `pass_range_max` L1 of the carrier with
     `dist(cutter, hoop) + pass_gain <= dist(carrier, hoop)` → PASS:
     `release_throw` with aim = cutter − carrier. The range gate mirrors the
     joint classifier's candidate bound (§3.1): beyond `pass_range_max` the
     release would silently classify as a 96 px flat heave — a wasted
     possession — so the rung must not fire. With the cutter in range the
     exact aim vector resolves as the PASS (chest vs lob by distance on its
     own) unless carrier, cutter and an in-range hoop are exactly collinear:
     §3.1's tie rule then hands the release to the hoop as a SHOT. **Open
     (defined, D23d)**: no enemy Living within `press_radius` L1 of the
     cutter AND no enemy Living within `press_radius` L1 of the
     carrier-cutter segment midpoint — two deterministic L1 checks. When the
     release actually classified as a PASS, the director immediately GOTOs
     the actual receiver onto the pass landing point (`tx, ty` − half-body —
     the solver already knows it, D22) and skips re-commanding the receiver
     while the PASS is in flight; on the collinear tie-SHOT it issues NO
     landing command — the returned point is then the private SHOT_LAND the
     director must never act on (D26), and the next cadence's shot-flight
     boxout takes over.
  5. Else ADVANCE: `ai.chaser_drives` predicates (goal-side / corridor with
     facing hysteresis / reach) against the target hoop; drive through when
     they pass, approach point when they do not.
- **CUTTERS** — the two members nearest the target hoop (after the handler):
  GOTO flank posts `hoop + dir8(jump_pos - hoop) * cutter_standoff ± perp *
  cutter_perp`, one per side by assignment order. They are the pass menu. A
  cutter named PASS_TARGET keeps its landing-point GOTO (D22) until the PASS
  resolves.
- **SAFETY** — everyone else: GOTO the midpoint of own hoop and center court.

### 4.3 Teams defending

- **ON-BALL DEFENDER** — nearest member to the carrier: `set_foe(carrier)` +
  `issue_front(GOTO, 30, carrier_center − half-body)`; the combat backstop
  swings on contact, and any landed hit forces the fumble. This is the steal.
- **RIM PROTECTOR** — nearest member to own hoop: GOTO `own_hoop +
  dir8(carrier - own_hoop) * rim_protect_standoff` − half-body — a body in the
  dunk lane.
- **HELP** — remaining members: GOTO the midpoint of carrier and own hoop with
  the soccer STAGGER fan `{0, +16, -16}`.

### 4.4 Multi-team discipline (3-4 teams)

Only the **threatened team** (defender of the hoop the handler ladder attacks)
plays the full §4.3 scheme. Every OTHER defending team sends exactly ONE
opportunist (its nearest member) at the carrier as a fumble vulture and keeps
the rest home in HELP shape around its own hoop — the §4.3 STAGGER fan
centered ON the hoop itself, oriented across the hoop-carrier axis. Bounded
mobbing, deterministic, and it reads correctly: the team about to be scored on
defends hardest.

### 4.5 Loose ball and shot flight

- During SHOT flight: **REBOUNDER** — nearest member of every active team
  pre-positions at `target_hoop + dir8(own_hoop - target_hoop) *
  rebound_boxout` − half-body (box-out at the rim). The director reads only
  public, replicated geometry — never SHOT_LAND, whose scatter outcome stays
  sim-internal (D26; a human cannot see the landing point either, and the
  actual landing is within `scatter_cap_total + rim_r + rim_lip` = 42 px of
  the rim regardless). Everyone else holds their posts.
- FREE/REBOUND on the floor: per team, the nearest TWO members race — GOTO
  onto the ball ground center (drive-through; arriving IS picking up). Rest:
  midpoint of own hoop and ball, staggered.
- Jump freeze: every directable member GOTO `jump_pos + dir8(own_hoop -
  jump_pos) * 24` − half-body — a face-off ring; at the toss the race is on.

### 4.6 Budget

Per tick O(oblist) scans + <= 2 substeps; director O(n) per team at cadence
15. Must pass the soccer-shape instruction-budget proof: 10x-reduced budget
(500000) over init + 45 ticks with zero budget errors (§11.2 #24).

---

## 5. PART IV — Manifest and mapgen

### 5.1 Identity

Mode id **6** (`MODE.BASKETBALL = 6` in `mode_core.lua:16-22`); mode name
`"BASKETBALL"`; mode string `"basketball"` (manifest field + script stem
`scripts/mode_basketball.lua`). Scenario ids **824-828** (D1). Registered
hooks: `on_mode_init`, `on_mode_tick`, `on_respawn`, `on_damage` — hook mask
`kModeInit | kModeTick | kRespawn | kDamage`.

### 5.2 Manifest row schema (emitted by manifest.cpp only — never hand-edit)

```lua
[824] = {
  mode = "basketball",
  teams = 2,
  time_limit = 7200,
  score_limit = 21,
  hoops = {                    -- PIXEL rim centers, keyed by DEFENDING team
    [0] = { x = 56, y = 200 },
    [1] = { x = 664, y = 200 },
  },
  arc_radius = 160,            -- three-point release distance, px (D4)
  jump_ball = { x = 360, y = 200 },  -- PIXEL center of the jump tile
  -- spawn_caps only on 828 (existing emission path)
},
```

No `item_pads`/`item_interval` ever (soccer/onslaught ruling extended; the
levels-sweep invariant then enforces pad-free rows automatically). Emission is
gated on populated fields, so the 28 existing rows stay byte-identical.

Consumed semantics: hoop center = arc-shot landing target and dunk-box center;
`arc_radius` = the 2/3-point release threshold AND (+64) the shot-range bound;
`jump_ball` = the neutral re-spot.

### 5.3 ExpectedLevel additions (`tools/modes_mapgen/modes_mapgen.h:93-133`)

```cpp
// Basketball (default-empty; non-basketball rows untouched):
std::vector<TilePos> hoops;   // index = DEFENDING team; the PIX_CARPET_M2 tile
int arc_radius = 0;           // three-point distance, PIXELS from each rim
TilePos jump_ball;            // center-court jump-ball tile
```

TILE units in the struct, PIXEL units in the emitted manifest (`tx*16 + 8`
centers, the `kickoff` convention).

### 5.4 Generator change list (file by file)

Run order: edit sources, run `scripts/generate_modes_campaign.sh` — the first
run exits nonzero by design (regenerate-and-diff rewrites the stale
`mode_levels.lua`), rerun to green, run a third time to prove byte-stability.

| File | Anchor | Edit |
|---|---|---|
| `modes_mapgen.h` | `:3-6` | header roster += "Basketball 824-828", 28 → 33 |
| | `:48-57` | `enum class ModeKind` gains `Basketball` (between Soccer and Mutant, id order); `mode_name` decl unchanged |
| | `:93-133` | `ExpectedLevel` gains §5.3 members |
| | `:168-178` | declare `build_basketball()` / `basketball_expectations()` between the soccer and mutant pairs |
| `builders_common.cpp` | `:40-51` | `mode_name()` gains `"basketball"` |
| | `:187-196` | `all_expectations()` concatenates basketball between soccer and mutant (keeps `M.levels` ascending by id) |
| `levels_basketball.cpp` | NEW | five builders (§6) + `basketball_expectations()`; TU-local `paint_arc_ring` (§6.0); `basketball_row(...)` helper mirroring `soccer_row` |
| `main.cpp` | `:5-7` | header roster/count comment |
| | `:104-131` | campaign.yaml description: "six games", roster gains basketball, "thirty-three fields" |
| | `:235-246` | obmap ledger ball term: soccer 1, **basketball 2** (ball + shadow) |
| | `:520-535` | item-pad OFF arm gains Basketball |
| | `:586-618` | basketball structural arm (§5.5) after the soccer arm |
| | `:698-706` | reachability probes += each hoop tile and `jump_ball` |
| | `:775-783` | `self_check_pack_art()` += `check_sprite(... "bball.png", 12, 12, 8)` and `(... "bshadow.png", 12, 12, 4)` |
| | `:903-905` | `rows.size() != 28` → `!= 33` |
| | `:936-940` | `build_basketball();` between soccer and mutant |
| | `:964-968` | `self_check_mode_dispatch(ModeKind::Basketball, 824);` (the init announce satisfies its Notification assert; absent the pack script it self-downgrades to SKIP — land generator and pack together) |
| `manifest.cpp` | `:5` | "five" → "six" |
| | `:56-91` | header field docs: mode enum line += `"basketball"`; doc lines for `hoops` / `arc_radius` / `jump_ball` |
| | after `:127` | three emission blocks gated on populated fields: `hoops` (tile→pixel centers), `arc_radius` (raw int when > 0), `jump_ball` (tile center) |
| `CMakeLists.txt` | `:914-931` | add `levels_basketball.cpp` to the `modes_mapgen` target; comment `:907-913` five → six |

### 5.5 Basketball self-check arm (main.cpp, mirroring the soccer arm)

1. Closed perimeter on all four edges (soccer loop; condition becomes
   `Soccer || Basketball`).
2. Per authored hoop: the 3x3 dunk carpet — 8 outer tiles exactly
   `PIX_CARPET_M`, center exactly `PIX_CARPET_M2`; hoop tile == carpet center;
   all 9 tiles passable.
3. `jump_ball` tile passable.
4. Arc sanity: `32 <= arc_radius` and
   `arc_radius < min(grid_w, grid_h) * GRID_SIZE / 2`; per hoop, **per
   axis-aligned quadrant** around the hoop center (N/E/S/W half-planes split
   at the diagonals): every quadrant that contains at least one PASSABLE tile
   whose center distance² lies in `[(arc_radius-8)², (arc_radius+8)²)` must
   contain at least 2 painted `PIX_CARPET_SMALL_HOR/VER` tiles in that band
   (and at least 8 band runners total per hoop). A presence check, not locus
   equality (D18/D26: paint order legitimately truncates rings near keys and
   furniture, but no whole in-bounds SIDE of an arc may go unpainted — a
   player must never lose a point of shot value on an invisible boundary).
5. Hoop separation: pairwise hoop-center L1 distance >
   `2 * (scatter_cap_total + rim_r + rim_lip)` = 84 px (makes cross-rim
   landings impossible; trivially true on all five courts).
6. Reachability probes at every hoop tile and the jump tile — every rebound
   scrum spot is provably walkable.
7. Ledger `+2` term, pad-free arm, dispatch roll-call (§5.4).

---

## 6. PART V — Arenas

### 6.0 Shared court grammar

Tile vocabulary (existing art only; ids verified in
`include/openglad/core/pixdefs.h` and the passability accept-list at
`src/gameplay/game_world.cpp:724-803` — `PIX_CARPET_M2` and all small runners
are passable):

| Element | Paint |
|---|---|
| Hardwood court | `PIX_FLOOR1` interior |
| Perimeter | 1-tile `PIX_H_WALL1` ring (closed; self-checked) |
| Center line | `PIX_CARPET_SMALL_VER` column (+ `_HOR` row on 826) |
| Center circle | `cobble_disc` at the exact center tile |
| Key ("the paint") | `cobble_rect` against each baseline (cosmetic) |
| Free-throw circle | small `cobble_disc` at the key front (cosmetic) |
| Three-point arc | dashed ring of `PIX_CARPET_SMALL_HOR/VER` (cosmetic, D18) |
| Dunk zone | 3x3 `PIX_CARPET_M` with center `PIX_CARPET_M2` = the hoop tile |
| Backboard | baseline wall behind the hoop; 827 adds protruding stubs |
| Posts | `DECOR_COLUMN_BOTTOM` flanking the key baseline corners |

**All grid dims are ODD**: a true center tile exists, the jump spot is an
exact tile center, and mirror maps (`x -> (w-1)-x`; 826's `(x,y) -> (40-y,x)`)
are exact symmetries.

TU-local arc painter (Euclidean band, integer math):

```cpp
// Dashed ring of carpet runners at pixel radius r around a hoop center.
static void paint_arc_ring(Canvas& c, int hx, int hy, int r)
{
    for (int ty = 1; ty < c.h() - 1; ++ty)
        for (int tx = 1; tx < c.w() - 1; ++tx) {
            const int dx = tx * 16 + 8 - hx;
            const int dy = ty * 16 + 8 - hy;
            const int d2 = dx * dx + dy * dy;
            if (d2 >= (r - 8) * (r - 8) && d2 < (r + 8) * (r + 8))
                c.set(tx, ty, (dx * dx >= dy * dy) ? PIX_CARPET_SMALL_VER
                                                   : PIX_CARPET_SMALL_HOR);
        }
}
```

**Paint order is load-bearing** (Canvas has no getter; later paint wins):
walls + hardwood → arc rings → center line(s) → center circle → keys →
free-throw discs → dunk carpet + M2 → gimmick walls (stubs/pillars/alcoves) →
decor. Consequences (intended): keys/dunk overwrite ring tiles near the hoop;
826's center runners cross its rings; 828's alcoves truncate its corner arcs.

Spawns: `markers_per_team = 5` (D8) — PG lead marker (2x2 clearance), two
wings, two bigs. `place_at` authors position/team only; tick-0 facing is
cosmetic (the first director cadence re-commands everyone). Row constants
shared by all five: `flags = 0`, `control_points = 0`, `doors = 0`,
`other_weapons = 0`, `authored_livings = 0`, `score_limit = 21` (825: 11),
`time_limit = 7200` (825: 5400). Titles carry the `"Basketball: "` prefix,
<= 30 bytes; briefings <= 33 chars/line ending `-- THE GAMESMASTER`.

Obmap ledger formula per row: `gens + treasures + flags + cps + doors +
livings + caps_total + 16 + 20 + 25 + 2(ball+shadow)`.

### 6.1 Arena 824 — "Basketball: CENTER COURT" (par 6)

The reference court. 2 teams, W/E hoops. Canvas **45x25** (720x400 px).

```cpp
Canvas c(45, 25);
c.hline(0, 44, 0, PIX_H_WALL1);   c.hline(0, 44, 24, PIX_H_WALL1);
c.vline(0, 0, 24, PIX_H_WALL1);   c.vline(44, 0, 24, PIX_H_WALL1);
c.rect(1, 1, 43, 23, PIX_FLOOR1);
paint_arc_ring(c, 56, 200, 160);  paint_arc_ring(c, 664, 200, 160);
c.vline(22, 1, 23, PIX_CARPET_SMALL_VER);
c.cobble_disc(44, 24, 7);                  // center (22,12), r 3.5
c.cobble_rect(1, 9, 7, 15);       c.cobble_rect(37, 9, 43, 15);   // keys
c.cobble_disc(16, 24, 5);         c.cobble_disc(72, 24, 5);       // FT circles
c.carpet_rect(2, 11, 4, 13);      c.set(3, 12, PIX_CARPET_M2);
c.carpet_rect(40, 11, 42, 13);    c.set(41, 12, PIX_CARPET_M2);
c.set_decor(1, 9, DECOR_COLUMN_BOTTOM);  c.set_decor(1, 15, DECOR_COLUMN_BOTTOM);
c.set_decor(43, 9, DECOR_COLUMN_BOTTOM); c.set_decor(43, 15, DECOR_COLUMN_BOTTOM);
```

Row data: hoops `[0]` tile (3,12) → px {56,200}, `[1]` tile (41,12) → {664,200};
`arc_radius = 160`; `jump_ball` tile (22,12) → {360,200}. Anchors: team 0 lead
{17,12}, wings {14,9},{14,15}, bigs {9,10},{9,14}; team 1 mirror `x -> 44-x`.
Treasures: 6 drumsticks {12,2},{22,2},{32,2},{12,22},{22,22},{32,22}. No
generators. Briefing (8 lines):

```
A NEW GAME, CONTENDERS.
CARRY THE BALL TO THE HOOP AND
STEP IN: TWO POINTS. OR THROW
IT HIGH - TWO INSIDE THE ARC,
THREE BEYOND. BLEED AND YOU
DROP IT. FIRST TO 21 TAKES
THE COURT.
-- THE GAMESMASTER
```

Pins: teams 2, markers 5/team, treasures 6, gens 0, decor_cells **4**, grid
45x25, par 6, time 7200, score 21. Ledger 0+6+0+0+0+0+0+16+20+25+2 = **69**.

### 6.2 Arena 825 — "Basketball: THE PLAYGROUND" (par 6)

Tight half-court brawler. 2 teams. Canvas **31x19** (496x304 px).

```cpp
Canvas c(31, 19);
c.hline(0, 30, 0, PIX_H_WALL1);   c.hline(0, 30, 18, PIX_H_WALL1);
c.vline(0, 0, 18, PIX_H_WALL1);   c.vline(30, 0, 18, PIX_H_WALL1);
c.rect(1, 1, 29, 17, PIX_FLOOR1);
paint_arc_ring(c, 56, 152, 96);   paint_arc_ring(c, 440, 152, 96);
c.vline(15, 1, 17, PIX_CARPET_SMALL_VER);
c.cobble_disc(30, 18, 5);                  // center (15,9), r 2.5
c.cobble_rect(1, 7, 6, 11);       c.cobble_rect(24, 7, 29, 11);
c.cobble_disc(14, 18, 5);         c.cobble_disc(46, 18, 5);
c.carpet_rect(2, 8, 4, 10);       c.set(3, 9, PIX_CARPET_M2);
c.carpet_rect(26, 8, 28, 10);     c.set(27, 9, PIX_CARPET_M2);
c.set_decor(1, 7, DECOR_COLUMN_BOTTOM);  c.set_decor(1, 11, DECOR_COLUMN_BOTTOM);
c.set_decor(29, 7, DECOR_COLUMN_BOTTOM); c.set_decor(29, 11, DECOR_COLUMN_BOTTOM);
```

Row data: hoops (3,9) → {56,152}, (27,9) → {440,152}; `arc_radius = 96`;
`jump_ball` (15,9) → {248,152}. Anchors: team 0 lead {11,9}, wings
{9,6},{9,12}, bigs {6,8},{6,10}; team 1 mirror `x -> 30-x`. Treasures: 2
drumsticks {15,2},{15,16} (contested). `score_limit = 11`,
`time_limit = 5400`. Briefing (6 lines):

```
THE PLAYGROUND. A CRAMPED
LITTLE COURT WHERE EVERY SPOT
IS A SHOOTING SPOT AND EVERY
DRIVE IS A BRAWL. SHORT ARC,
SHORT TEMPERS. FIRST TO 11.
-- THE GAMESMASTER
```

Pins: teams 2, markers 5, treasures 2, gens 0, decor_cells **4**, grid 31x19,
par 6. Ledger 0+2+0+0+0+0+0+16+20+25+2 = **65**.

### 6.3 Arena 826 — "Basketball: FOUR HOOPS" (par 8)

The multi-team court: 4 teams, one hoop per wall (team k defends wall k: 0 N,
1 E, 2 S, 3 W). Also the 3-team court: inactive-team hoops are not banked
(HOOP_POS = 0) so they neither score nor count. Canvas **41x41** (656x656 px).

```cpp
Canvas c(41, 41);
c.hline(0, 40, 0, PIX_H_WALL1);   c.hline(0, 40, 40, PIX_H_WALL1);
c.vline(0, 0, 40, PIX_H_WALL1);   c.vline(40, 0, 40, PIX_H_WALL1);
c.rect(1, 1, 39, 39, PIX_FLOOR1);
paint_arc_ring(c, 328, 56, 144);   // N
paint_arc_ring(c, 600, 328, 144);  // E
paint_arc_ring(c, 328, 600, 144);  // S
paint_arc_ring(c, 56, 328, 144);   // W
c.vline(20, 1, 39, PIX_CARPET_SMALL_VER);
c.hline(1, 39, 20, PIX_CARPET_SMALL_HOR);
c.cobble_disc(40, 40, 7);                  // center (20,20), r 3.5
c.cobble_rect(17, 1, 23, 6);      // N key (team 0)
c.cobble_rect(34, 17, 39, 23);    // E (team 1)
c.cobble_rect(17, 34, 23, 39);    // S (team 2)
c.cobble_rect(1, 17, 6, 23);      // W (team 3)
c.carpet_rect(19, 2, 21, 4);      c.set(20, 3, PIX_CARPET_M2);   // N
c.carpet_rect(36, 19, 38, 21);    c.set(37, 20, PIX_CARPET_M2);  // E
c.carpet_rect(19, 36, 21, 38);    c.set(20, 37, PIX_CARPET_M2);  // S
c.carpet_rect(2, 19, 4, 21);      c.set(3, 20, PIX_CARPET_M2);   // W
// 8 posts flanking each key mouth on its baseline:
// (16,1) (24,1) (39,16) (39,24) (16,39) (24,39) (1,16) (1,24)
```

Row data: hoops N (20,3) → {328,56}, E (37,20) → {600,328}, S (20,37) →
{328,600}, W (3,20) → {56,328}; `arc_radius = 144`; `jump_ball` (20,20) →
{328,328}. Adjacent hoop centers ~385 px apart > 2x144: arcs never overlap.
Anchors, team 0 (N) rotated `(x,y) -> (40-y, x)` per team: T0 lead {20,15},
wings {17,12},{23,12}, bigs {15,8},{25,8}; T1 lead {25,20}, wings
{28,17},{28,23}, bigs {32,15},{32,25}; T2 lead {20,25}, wings {23,28},{17,28},
bigs {25,32},{15,32}; T3 lead {15,20}, wings {12,23},{12,17}, bigs
{8,25},{8,15}. Treasures: 8 drumsticks {5,5},{35,5},{35,35},{5,35} +
{12,12},{28,12},{28,28},{12,28}. `teams = 4` (manifest default; 2/3-team lobby
requests supported). Briefing (6 lines):

```
FOUR HOOPS, FOUR BANDS, ONE
BALL. GUARD YOUR OWN RIM AND
SCORE ON ANY RIVAL'S. EVERY
REBOUND HAS FOUR CLAIMANTS.
FIRST TO 21 TAKES THE CIRCUS.
-- THE GAMESMASTER
```

Pins: teams 4, markers 5/team (20 anchors), treasures 8, gens 0, decor_cells
**8**, grid 41x41, par 8. Ledger 0+8+0+0+0+0+0+16+20+25+2 = **71**.

### 6.4 Arena 827 — "Basketball: THE BANKHOUSE" (par 8)

The gimmick court: protruding backboard stubs, wing pillars, mid-court pillar
quartet. Straight lanes die; banks and angles live (D12: pillars break flat
and chest passes, lobs and shots clear them). 2 teams. Canvas **45x27**
(720x432 px).

```cpp
Canvas c(45, 27);
c.hline(0, 44, 0, PIX_H_WALL1);   c.hline(0, 44, 26, PIX_H_WALL1);
c.vline(0, 0, 26, PIX_H_WALL1);   c.vline(44, 0, 26, PIX_H_WALL1);
c.rect(1, 1, 43, 25, PIX_FLOOR1);
paint_arc_ring(c, 56, 216, 176);  paint_arc_ring(c, 664, 216, 176);
c.vline(22, 1, 25, PIX_CARPET_SMALL_VER);
c.cobble_disc(44, 26, 7);                  // center (22,13), r 3.5
c.cobble_rect(1, 10, 7, 16);      c.cobble_rect(37, 10, 43, 16);
c.carpet_rect(2, 12, 4, 14);      c.set(3, 13, PIX_CARPET_M2);
c.carpet_rect(40, 12, 42, 14);    c.set(41, 13, PIX_CARPET_M2);
// GIMMICK 1: protruding backboard stubs (1 tile in, 5 tall; rim gap 24 px).
c.vline(1, 11, 15, PIX_H_WALL1);  c.vline(43, 11, 15, PIX_H_WALL1);
// GIMMICK 2: wing pillars off the key's front corners (bank pockets).
c.set(5, 9, PIX_H_WALL1);   c.set(5, 17, PIX_H_WALL1);
c.set(39, 9, PIX_H_WALL1);  c.set(39, 17, PIX_H_WALL1);
// GIMMICK 3: mid-court pillar quartet (breaks flat sideline lanes).
c.set(17, 7, PIX_H_WALL1);  c.set(17, 19, PIX_H_WALL1);
c.set(27, 7, PIX_H_WALL1);  c.set(27, 19, PIX_H_WALL1);
// Decor: pebble scuff in the four rebound pockets.
c.set_decor(8, 4, DECOR_PEBBLES);   c.set_decor(36, 4, DECOR_PEBBLES);
c.set_decor(8, 22, DECOR_PEBBLES);  c.set_decor(36, 22, DECOR_PEBBLES);
```

Row data: hoops (3,13) → {56,216}, (41,13) → {664,216}; `arc_radius = 176`;
`jump_ball` (22,13) → {360,216}. No posts (the stubs are the furniture); decor
= 4 pebble cells. Reachability: the stub seals column x=1 rows 11-15; (1,10)
and (1,16) stay reachable from x=2; every pillar is a lone wall tile with all
four neighbors open. No ring tile lands on a pillar (checked: (17,7) d² =
59392 outside band [28224, 33856); (5,9) d² = 5120). Anchors: team 0 lead
{18,13}, wings {15,10},{15,16}, bigs {11,12},{11,14}; team 1 mirror
`x -> 44-x`. Treasures: 6 drumsticks {12,2},{22,2},{32,2},{12,24},{22,24},
{32,24}. Briefing (6 lines):

```
THE BANKHOUSE. WALLS JUT AND
PILLARS CROWD THE LANES: A
STRAIGHT PASS DIES, A CLEVER
BANK LIVES. PLAY THE ANGLES.
FIRST TO 21.
-- THE GAMESMASTER
```

Pins: teams 2, markers 5, treasures 6, gens 0, decor_cells **4**, grid 45x27,
par 8. Ledger **69**.

### 6.5 Arena 828 — "Basketball: BENCHWARMERS" (par 10)

The generator court: each side's tent raises skeleton bench players (capped by
`mode_caps`). 2 teams. Canvas **47x29** (752x464 px).

```cpp
Canvas c(47, 29);
c.hline(0, 46, 0, PIX_H_WALL1);   c.hline(0, 46, 28, PIX_H_WALL1);
c.vline(0, 0, 28, PIX_H_WALL1);   c.vline(46, 0, 28, PIX_H_WALL1);
c.rect(1, 1, 45, 27, PIX_FLOOR1);
paint_arc_ring(c, 56, 232, 160);  paint_arc_ring(c, 696, 232, 160);
c.vline(23, 1, 27, PIX_CARPET_SMALL_VER);
c.cobble_disc(46, 28, 7);                  // center (23,14), r 3.5
c.cobble_rect(1, 11, 7, 17);      c.cobble_rect(39, 11, 45, 17);
c.cobble_disc(16, 28, 5);         c.cobble_disc(76, 28, 5);
c.carpet_rect(2, 13, 4, 15);      c.set(3, 14, PIX_CARPET_M2);
c.carpet_rect(42, 13, 44, 15);    c.set(43, 14, PIX_CARPET_M2);
// Bench alcoves on the north wall, 3x3 interior, 1-wide south mouth
// (BONEYARD CUP's shape, which passes audit_generator_spawn_exits).
c.hline(8, 12, 1, PIX_H_WALL1);   c.hline(8, 12, 5, PIX_H_WALL1);
c.vline(8, 1, 5, PIX_H_WALL1);    c.vline(12, 1, 5, PIX_H_WALL1);
c.cobble_rect(9, 2, 11, 4);
c.set(10, 5, Canvas::cobble(10, 5));        // mouth
c.hline(34, 38, 1, PIX_H_WALL1);  c.hline(34, 38, 5, PIX_H_WALL1);
c.vline(34, 1, 5, PIX_H_WALL1);   c.vline(38, 1, 5, PIX_H_WALL1);
c.cobble_rect(35, 2, 37, 4);
c.set(36, 5, Canvas::cobble(36, 5));
// Decor: posts at key baseline corners + bones at the bench mouths.
c.set_decor(1, 11, DECOR_COLUMN_BOTTOM);  c.set_decor(1, 17, DECOR_COLUMN_BOTTOM);
c.set_decor(45, 11, DECOR_COLUMN_BOTTOM); c.set_decor(45, 17, DECOR_COLUMN_BOTTOM);
c.set_decor(10, 6, DECOR_BONES);          c.set_decor(36, 6, DECOR_BONES);
```

Row data: hoops (3,14) → {56,232}, (43,14) → {696,232}; `arc_radius = 160`;
`jump_ball` (23,14) → {376,232}. Generators:
`place_at(world, Order::Generator, FAMILY_TENT, 0, {9, 2}, 1)` and team 1 at
{36,2} — the alcove interior's NW corner (BONEYARD's corner-anchor idiom):
the 32x32 tent body spans the two NORTH interior rows, keeping the tile above
the 1-wide south mouth open so spawns can walk out; anchoring at {10,3} would
cover (10,4) and seal the mouth approach. `spawn_caps = {[0] = 4, [1] = 4}`. Anchors: team 0 lead {19,14}, wings
{16,11},{16,17}, bigs {12,13},{12,15}; team 1 mirror `x -> 46-x`. Treasures: 6
drumsticks {17,2},{23,2},{29,2},{17,26},{23,26},{29,26} (clear of both
alcoves). If `audit_generator_spawn_exits` flags the 1-wide mouth anyway, the
documented fallback is widening the mouth to 2 tiles — NOT a waiver. Briefing
(6 lines):

```
BENCHWARMERS. EACH SIDE'S OLD
TENT RAISES BONY SUBSTITUTES
WHO NEVER TIRE AND NEVER
FLINCH. DEEP BENCHES, CHEAP
FOULS. FIRST TO 21.
-- THE GAMESMASTER
```

Pins: teams 2, markers 5, treasures 6, gens 1/team, caps_total 8, decor_cells
**6**, grid 47x29, par 10. Ledger 2+6+0+0+0+0+8+16+20+25+2 = **79**.

### 6.6 Cross-arena matrix

| Arena | Court px | Arc | Hoop-wall gap | Teams | Score | Twist |
|---|---|---|---|---|---|---|
| 824 CENTER COURT | 720x400 | 160 | 40 px | 2 | 21 | the reference |
| 825 THE PLAYGROUND | 496x304 | 96 | 40 px | 2 | 11 | everything is a three |
| 826 FOUR HOOPS | 656x656 | 144 | 40 px | 4 (2-4) | 21 | four scrums, one ball |
| 827 THE BANKHOUSE | 720x432 | 176 | 24 px (stub) | 2 | 21 | banks beat lanes |
| 828 BENCHWARMERS | 752x464 | 160 | 40 px | 2 | 21 | generator benches, caps 4+4 |

Rim radius is constant (12 px) across the roster on purpose (D14): difficulty
scales through arc distance, court length and furniture — never the target.

---

## 7. PART VI — Sprites and families

### 7.1 Sprites (`scripts/gen_modes_sprites.py`; run from repo root)

Two painters beside `make_ball_frames` (`:299-356`); both pass
`check_band(..., team_ok=False)` (no team-band indices 248-255) and the
palette drift check:

1. **`make_bball_frames(size=12, frames=8)`** — the soccer painter's
   lit-sphere loop with an ORANGE body ramp (3-4 adjacent
   `pix/openglad.gpl` orange entries, named `BBALL_*` constants) and the patch
   lattice replaced by dark-brown **seam great-circles** (two orthogonal
   circles + silhouette rim, rotated 1/8 turn per frame). Keep the
   all-frames-distinct assertion. Output `bball.png` 12x96 +
   `aseprite_sidecar("bball", 12, 12, 8)`.
2. **`make_bshadow_frames(size=12, frames=4)`** — solid dark ellipse
   (flag-gray set), frame 0 = full ~10x5 ellipse shrinking to frame 3 = ~4x2.
   Output `bshadow.png` 12x48 + sidecar (12, 12, 4).

Neither sidecar carries a `footprint` member — world size 12x12; the FATAL
footprint machinery is not engaged. `main()`'s roll-call gains both write
blocks.

### 7.2 Families (I5, D11)

**`families/fx-bball.lua`** — clone of `fx-ball.lua`'s one-call shape: effect
order, `id = "modes:bball"`, `wire_id = "auto"` (→ 22), `name = "BBALL"`,
`flags = { "NO_COLLIDE" }`, `sprite = "packs/modes.core/sprites/bball.png"`,
glyph `"b"` yellow bold, `radar_landmark = true`, NO animation table, and an
`on_act` returning true (the engine never moves/animates/expires it; the impl
drives `set_frame`, and the resolved frame replicates — mirrors never run
`animate()`). Spawned with **`og.add_ob`** (oblist — the fx list never acts).

**`families/fx-bshadow.lua`** — same shape MINUS `on_act` (D11) and with
`radar_landmark = false`, glyph `"."` dark, sprite `bshadow.png`. Spawned with
**`og.add_fx_ob`** (fxlist: never acts, renders under everything, replicates).

Both family bytes (22, 23 >= NUM_FAMILIES) travel the snapshot wire — the
mirror-replication test (§11.2 #22) is the regression for the historic
family-byte clamp bug and for I5 ordinality.

---

## 8. PART VII — HUD, announce, beacons

- **HUD** (4 slots, 25 bytes): slot = team index,
  `core.hud_score_line(team, team, POINTS[team], SCORE_LIMIT)` — 4-team courts
  fill all four slots. Shot-clock countdown: while possession is live and
  remaining <= `clock_hud`, the POSSESSING team's own line gets suffix
  `" !N"` (seconds remaining) via direct `og.set_hud_line` — worst case
  `"YELLOW 999/999 !24"` = 18 <= 25. No dedicated clock line (4-team courts
  have no spare slot).
- **Beacons**: slot 0 = the SHADOW (ground truth for radar/off-screen — the
  ball entity is drawn offset up to ~40 px at apex); slot 1 = the carrier
  (team-tinted) or nil. The ball family still radar-blips itself (landmark);
  the shadow family is not a landmark, avoiding a double blip.
- **Announces** (all <= 25 bytes, counted): `"BASKETBALL! FIRST TO N"` +
  SOUND_CHARGE (init; satisfies the mapgen dispatch proof);
  `"JUMP BALL!"` + SOUND_YO; `"BASKET! {COLOR} +2"` / `"THREE! {COLOR} +3"` +
  SOUND_MONEY; `"DUNK! {COLOR} +2"` + SOUND_MONEY; `"GOALTEND! {COLOR} +N"` +
  SOUND_MONEY; `"OWN BASKET! {COLOR} +2"` / `"OWN BASKET! {COLOR} -2"`;
  `"TURNOVER!"` + SOUND_YO; `"BLOCK!"` + SOUND_BOLT; `"SHOT CLOCK!"` +
  SOUND_YO; `"BALL RESET"` + SOUND_YO; rim clang = positional SOUND_CLANG, no
  text; win via `core.declare_team_win`.
- **Release readability (D26)**: SHOT release = positional SOUND_BOLT at the
  shooter, plus `"THREE UP!"` (9 bytes) when SHOT_VALUE = 3 — a player knows
  at release which throw class fired and what it is worth; LOB release =
  positional SOUND_YO; chest and flat releases are silent (their low, fast
  flight is its own tell).

---

## 9. PART VIII — Edge-case ledger (each with its ruling)

1. **Carrier dies mid-stride** — T6: ball drops at the corpse with pop +
   scatter; LAST_TOUCH stays the dead carrier's team (a rolling FREE ball
   cannot score; only crossings and dunks do).
2. **Carrier respawns with the ball** — impossible: CARRIER clears in pipeline
   step 3 of the tick the death is visible; the revived walker returns
   ball-less at its anchors (`on_respawn` = `place_at_anchor`, no teleport,
   zero RNG).
3. **Ball lands on impassable terrain** — §2.7 ring scan; total,
   deterministic, RNG-free.
4. **Simultaneous pickup contention** — first eligible Living in oblist order
   (soccer precedent); earlier slots (players) win ties. Accepted.
5. **Throw during the jump freeze** — unreachable: a center reset clears
   CARRIER and the freeze bars pickup, so no carrier exists while frozen; a
   weapon fired during the freeze is never consumed and the swat/impulse paths
   are JUMP_UNTIL-guarded.
6. **Shot in flight when the win latch fires** — the engine re-asserts the
   latch and runs no more Lua; the ball freezes mid-air. Only the timeout
   verdict defers for a SHOT (§3.7); score-limit wins are immediate.
7. **Wiped team during possession** — play continues; the every-tick death
   scan honors the submenu and `revive_wiped_teams` fires at every center
   reset. Respawn-Off + total wipe: the wipe watchdog (D25) runs the 600-tick
   countdown in EVERY ball state, ignoring attendance — a winner standing over
   the free ball, riding the turnover cycle, or keeping the ball carried
   cannot stall the reset. No deadlock, no babysit.
8. **Block-swat attribution** — restamp to the swatting weapon's owner-chain
   root's team; unowned/generator weapons clear LAST_TOUCH1 only; an
   unattributed crossing scores nothing.
9. **Fumble during shot windup** — no windup exists; within a tick, damage
   (act phase → FUMBLE_TICK → step 4) strictly precedes consumption (step 6).
   The hit wins; the throw never happens.
10. **Shot clock expiring airborne** — a SHOT cleared the clock at release. A
    PASS (or any loose ball) keeps the deadline armed (D25): ANY regain by the
    clock team at `now >= CLOCK_UNTIL` — catch or pickup — turns over on the
    spot; an interception or enemy pickup re-arms fresh. Loose-ball
    transitions never clear it.
11. **3-4 team fairness** — all resets are NEUTRAL jump balls (no possession
    arrow; the scramble arbitrates); team-scoped clock; per-team hoops with
    shooter-team crediting; own baskets pay LAST_TOUCH2; timeout ladder breaks
    ties lowest-byte; mobbing bounded by the threatened-team rule (§4.4).
12. **Carrier charmed mid-possession** — attribution reads the carrier's team
    live at every stamp; the clock re-arms for the new team. No special case.
13. **Ball or shadow handle unresolvable** — skip all ball logic that tick
    (soccer's nil-guard shape).
14. **Pickup during the jump freeze** — barred by `now >= JUMP_UNTIL`.
15. **Two graces colliding** — one GRACE slot trio by design: the newest event
    owns the bar (a turnover grace overwrites a pending self grace);
    GRACE_UNTIL/GRACE_TEAM1/GRACE_ENTITY are always written together (D24).
16. **Shot released from inside the dunk box** — cannot happen in the same
    tick: step 5 scores the dunk first and the weapon flies unconsumed
    (deliberate — the drive already earned the 2).
17. **PASS target dies in flight** — the catch check never matches; the ball
    lands and reverts to REBOUND/FREE. Interceptors unaffected.
18. **award_score sign safety** — every award funnels through `score_basket`
    with value 2 or 3 by construction; forfeits touch POINTS only (I3, pinned).
19. **Consumed-weapon side effects** — `set_dead(1)` on the fired weapon; the
    engine reaps it; exploding families die without attacking (a cosmetic
    death puff is accepted; test #6 pins no damage lands).
20. **Arc paint truncation** — cosmetic only (D18); classification reads the
    manifest number, never tiles; the per-quadrant presence check (§5.5 #4)
    guarantees no whole in-bounds side of an arc goes unpainted.
21. **Stale in-flight carrier weapon at pickup** — never consumed: the
    THROW_WATERMARK gate (D19) admits only weapons fired after possession
    began. Arrows launched while chasing the loose ball — and returning
    boomerangs — fly on untouched, and no phantom throw releases.
22. **Receiver charmed mid-flight** — a catch by a receiver whose live team
    differs from the CLOCK team resolves as an INTERCEPTION in the possession
    outcome (the CLOCK_TEAM1 compare inside `gain_possession` — SHOT_TEAM1 is
    write-through bookkeeping on passes, nothing reads it there): restamps to
    the new team, clock re-arms fresh. Per §2.4 the charmed receiver KEEPS
    its catch envelope — `receiver_catch_radius`, the `(grab_z, catch_z]`
    band and the grace exemption; only the possession outcome is enemy.
23. **Buzzer PASS / lob toss-in** — the timeout deferral covers SHOT only
    (§3.7). A pass crossing the rim plane after the buzzer does not count —
    accepted and intended: only a genuine shot attempt earns the buzzer.
24. **Fired weapon dies before consumption** — CONSUMED ANYWAY when it
    qualifies (D27, reversing this row's original live-only acceptance after
    playtest): a point-blank weapon that hits a wall or adjacent enemy on its
    own act step dies with `death_called() == 1` and is still on the weaplist
    during the mode tick (the dead sweep runs last, `game_world.cpp:1996`),
    with its owner and `lastx/lasty` aim step intact — the throw releases
    for every carrier, and the incidental contact damage it dealt stands.
    The `fire()`-time pad-blocked spawn (dead, `death_called() == 0`)
    releases under an ACT_CONTROL carrier; under an AI carrier it is
    indistinguishable from a fire_check probe and stays unconsumed (D27
    residual — bots throw via the director's script release, §4.2). The
    scan's `set_dead(1)` is a no-op on the dead (`death_called` guard,
    `weap.cpp:164-167`). Melee-range fire while NOT carrying is untouched.
    Pinned by test 38.
25. **Rebounder AI foreknowledge** — resolved by D26: the director boxes out
    at the target HOOP center (public geometry); SHOT_LAND is never read by
    AI, so bots have no psychic edge over humans on the scatter outcome.
26. **Swat during an entity grace window** — the bar stays pinned on
    GRACE_ENTITY (D24); the restamp moves attribution only. The blocker may
    grab the ball it just blocked immediately; the barred thrower/ex-carrier
    stays barred for the full window.
27. **Alley-oop onto a box camper** — the catch is contestable (same-tick
    contenders race in oblist order, D23a) and even a clean catch inside the
    box does not dunk until the catcher exits and re-enters (DUNK_OK, D23b);
    ground put-backs are exempt. The defense has real answers: contest the
    catch, fumble the re-entry drive, or hold the box.
28. **Carrier below `weapon_cost` cannot release** — engine-gated pre-spawn:
    `walker::fire()` returns before creating any weapon when
    `magicpoints < weapon_cost` (`walker.cpp:506-507`), so nothing exists for
    the mode to consume and no Lua hook fires. Accepted residual of D28: with
    refunds, a carrier only lands here when it arrived at possession already
    drained; magic regen must climb past the cost before the next release.
    Out of Lua reach by design. Pinned by test 40.
29. **`fire_check` scratch probes on the weaplist** — the engine's aim probe
    is a REAL weapon (owner = the prober, heading set) bare-`set_dead(1)`d
    on every denial/miss/success path without paying `weapon_cost`
    (`walker.cpp:1245-1346`); an engaged AI carrier makes one per engagement
    tick. D27's qualify gate keeps them out of the throw scan (probes never
    reach `death()`, and the ACT_CONTROL dead arm cannot see AI probes), so
    no phantom release and no D28 mint — pinned by test 41. Two accepted
    engine leaks stay out of Lua reach: the no-foe exit leaves its probe
    ALIVE (`walker.cpp:1265-1283`, no `set_dead`) and therefore consumable
    as if genuinely fired (pre-existing phantom, now also refunded ≤ cost;
    reachable only when a queued COMMAND_FIRE dispatches after the foe slot
    empties), and an AI carrier's `fire()`-time pad-blocked spawn is
    probe-indistinguishable and stays unconsumed (see #24). An engine-side
    probe marker would close both; that is a `src/` change beyond this
    amendment.

---

## Implementation plan

DAG of work packages. Difficulty: **fable** = mode impl Lua, arena/mapgen
authoring, tricky tests; **opus** = sprite painter, docs,
registration/CMake/fixture wiring, straightforward tests. Each WP is sized for
one agent and owns its files exclusively.

| WP | Name | Difficulty | Owned files | Depends on |
|----|------|-----------|-------------|------------|
| WP1 | Sprite painter | opus | `scripts/gen_modes_sprites.py`; generated `campaigns/modes/packs/modes.core/sprites/bball.png`, `bball.json`, `bshadow.png`, `bshadow.json` | — |
| WP2 | Families + mode constant + roster prose | opus | `campaigns/modes/packs/modes.core/families/fx-bball.lua` (new), `families/fx-bshadow.lua` (new); `lib/mode_core.lua` (MODE.BASKETBALL = 6 only); `lib/mode_strip.lua:1`, `lib/mode_match.lua:1,75` (prose "five" → "six") | WP1 (family sprite paths must load) |
| WP3 | Shared-helper hoist (D17) | opus | `lib/mode_core.lua` (iabs, walker_center), `lib/mode_ai.lua` (FACING_X/Y, dir8, drive_geometry, chaser_drives), `lib/mode_match.lua` (revive_wiped_teams, plus SOCCER's submenu-honoring run_death_scan variant ONLY — `mode_ctf_impl.lua`/`mode_onslaught_impl.lua` keep their own variants and are NOT owned files), `lib/mode_soccer_impl.lua` (consume the hoisted versions; delete locals). Proof: og_unit_soccer green unchanged — its director tests pin exact GOTO targets | — (parallel with WP1/WP2; coordinate mode_core/mode_match edits with WP2) |
| WP4 | Mode impl + entry script | fable | `campaigns/modes/packs/modes.core/lib/mode_basketball_impl.lua` (new), `scripts/mode_basketball.lua` (new) | WP2, WP3 |
| WP5 | Mapgen + arenas + regeneration | fable | `tools/modes_mapgen/levels_basketball.cpp` (new), `modes_mapgen.h`, `builders_common.cpp`, `main.cpp`, `manifest.cpp`; `CMakeLists.txt:914-931`; regenerated `campaigns/modes/{campaign.yaml, icon.png, scen/scen824-828.fss, pix/scen0824-0828*.png, packs/modes.core/lib/mode_levels.lua}` | WP1 (check_sprite), WP4 (dispatch proof needs the pack script) |
| WP6 | Test fixture wiring | opus | `tests/modes_pack_fixture.h` (9701-9709 constants + kTestRegistrationLua basketball rows); `scripts/coverage/runtime_only_lua.txt` (digest update, SAME commit); `cmake/OpenGladTests.cmake:878` (one line) | WP4 |
| WP7 | Mechanism tests | fable | `tests/unit/test_modes_basketball.cpp` (new; §11.2's 37 tests) | WP6 |
| WP8 | Levels-sweep updates | opus | `tests/unit/test_modes_levels.cpp` (pin rows, 28→33, ledger +2 term, hook masks 824/828, sprite rows, tick-clean += 824, `basketball_pins()` sweep per §11.4) | WP5 |
| WP9 | Docs | opus | `docs/mp-game-modes.md` (:3-4, :10-14, :21, :134-152, :153-159), `docs/ARCHITECTURE.md` (:669, :700-703) | WP5 (describes shipped reality) |
| WP10 | Integration + gate run | fable | no exclusive files — runs §11.5's gate checklist end to end; regressions route back to the owning WP | WP7, WP8, WP9 |

Parallelism: {WP1, WP3} first wave; WP2 after WP1; WP4 after {WP2, WP3};
{WP5, WP6} after WP4; {WP7, WP8, WP9} next; WP10 last.

---

## Test plan

### 11.1 Wiring

`tests/unit/test_modes_basketball.cpp` joins `og_unit_modes` (D13): one line at
`cmake/OpenGladTests.cmake:878`; `OG_MODES_PACK_SOURCE_DIR` is already on the
target and `og_unit_modes` is already in
`scripts/coverage/recorder_processes.txt`. Fixture: `ModesCtfWorld` +
`var()/team_var()` readers; the suite re-declares the impl slot map as a local
enum (the loud-break convention). Test levels 9701-9709 in
`tests/modes_pack_fixture.h`, synthetic rows registered through
`bball.make_hooks` in `kTestRegistrationLua` — **any byte change to that
literal updates the sha256 in `scripts/coverage/runtime_only_lua.txt` in the
same commit**. Suggested synthetic rows on the 640x960 default grid: 9701
2-team reference; 9702 4-team; 9703 missing-hoops error row; 9704 generator +
caps row.

### 11.2 Mechanism tests (one per mechanism)

1. `init_banks_manifest_row` — HOOP_POS/ARC_RADIUS/JUMP_POS match the row;
   ball in oblist, shadow in fxlist; mode name "BASKETBALL"; MODE_ID == 6
   written last.
2. `init_missing_hoop_errors_to_classic` — failed init, classic next tick.
3. `jump_ball_freeze_blocks_pickup` — no carrier during the freeze; carrier
   after; toss fires at the boundary tick exactly; the airborne toss pins
   run_spin (BALL_SPIN = air_spin per tick, drawn frame = phase/256).
4. `pickup_and_carry` — CARRIER set; ground vars track carrier center;
   z == carry_z; drawn ball y == ground y − carry_z; shadow frame > 0.
5. `airborne_ball_clears_heads` — flight at z > head_z over a parked ACT_SIT
   walker: no contact; same trajectory at z <= head_z interacts.
6. `throw_consumes_weapon_and_classifies` — shot / far-lob (apex > head_z) /
   near-chest (apex <= head_z) sub-cases; weapon dead after consume; no damage
   from the consumed weapon; possession cleared.
7. `arc_shot_scores_2_inside_3_outside` — pin `world().rng_.state_` (the
   sim-random-override trap: pin state, never the spy); on-axis release so
   L1 == Euclid; POINTS +2 inside / +3 beyond; m_score grows by
   value * point_score.
8. `rim_miss_rebounds_live` — scatter forced off-rim via pinned rng state: no
   score, REBOUND with nonzero velocity, shot state cleared.
9. `bank_shot_reflects_off_backboard` — rebound trajectory into the wall
   behind the hoop reflects (z-gated, D12) and crosses the rim plane → 2 pts
   (T16).
10. `dunk_scores_2` — carrier stepped into the enemy dunk box → +2 + center
    reset; own box never triggers.
11. `fumble_on_carrier_damage` — attacker staged `set_damage(3.0)` (the
    d < 4.0 deterministic rule; note next(1) still advances the LCG) →
    loose ball, carrier cleared, scatter applied, self grace on the victim.
12. `carrier_death_drops_ball` — kill the carrier → ball free at the corpse.
13. `shot_clock_turnover` — hold 420 ticks → "TURNOVER!", CLOCK_* cleared
    (D25), 120-tick team grace denies re-pickup until GRACE_UNTIL, opponent
    may take it immediately; HUD suffix appears at <= clock_hud remaining.
14. `block_window_by_height` — weapon swat at z <= block_ceiling deflects
    with the EXACT L1-normalized impulse (staged damage 5 → 10 px/tick,
    diagonal step, plus both clamp bounds 4 and 12); identical weapon at
    apex does not (the sanctuary); a (0,0)-step weapon is passed over and
    a later in-radius weapon still blocks (the §3.5 zero-step ruling).
15. `goaltend_awards_basket` — enemy weapon swat in the goaltend window (vz <
    0, rim_z < z <= goaltend_ceiling, within goaltend_radius) → SHOT_VALUE to
    the shooter, center reset; same-team swat converts to REBOUND instead —
    silently (no "BLOCK!": the §3.5 tip rule).
16. `dead_ball_resets_and_revives_wiped_team` — motionless unattended 600
    ticks → jump ball; a wiped active team is revived/reprovisioned (backstop
    semantics: pending-revive gate, guy/owner eligibility).
17. `win_by_score_and_timeout_ladder` — score-limit latch; timeout ladder
    POINTS → team_score → lowest byte; buzzer deferral while a SHOT flies.
18. `four_team_court_attribution` — 4-team row: team 2 dunks on team 0 →
    POINTS[2] only; all four hoops banked; own-basket crossing pays
    LAST_TOUCH2; forfeit decrements POINTS only.
19. `respawn_honors_submenu` — respawn_mode 0/1/2 sweep on the death scan;
    on_respawn places at own anchors.
20. `director_roles` — ACT_SIT bots + `align_before_cadence`: on-ball defender
    GOTO onto the carrier, rim protector at its standoff, chasers onto the
    ball ground center, cutter posts, one-peel engagement kept.
21. `spawn_caps_pause_generators` — 9704 row: `kPauseFireFrequency = 16384`
    re-declared locally; caps enforced at cadence.
22. `mirror_replication_120_ticks` — **required**: `ModeMirror` +
    `replicate_to_mirror`, 0 hash strikes over 120 ticks with the ball mid-arc
    and the shadow live (family bytes 22/23 on the wire; the I4 proof).
23. `determinism_digest` — two identical bot matches, `digest_world` extended
    to walk fxlist, equal at tick N (run on the 4-team row).
24. `instruction_budget_headroom` — `g_test_world_instruction_budget = 500000`
    before first dispatch (RAII guard, so an early ASSERT cannot leak the
    reduced budget), init + 45 ticks, no budget error, restore 0.
25. `landing_legality_respots` — force a REBOUND onto an impassable tile → the
    ring scan re-spots deterministically as a FREE dead ball; attribution kept.
26. `stale_inflight_weapon_not_consumed` — carrier-owned weapon already in
    flight BEFORE the pickup: first CARRIED tick releases no throw, the weapon
    flies on live; a weapon fired after pickup IS consumed (D19 watermark).
27. `clock_persists_through_pass_and_regrab` — armed clock + uncaught own
    pass + same-team regrab: CLOCK_UNTIL unchanged (no launder); a catch by
    the clock team past the deadline turns over WITH the in-flight facts
    scrubbed (PASS_TARGET/FLIGHT_TICKS zero on the REBOUND); a late ground
    PICKUP by the clock team also turns over (rule 1 of §3.6).
28. `turnover_regrab_defined` — post-T7: clock disarmed, no repeat announce;
    ex-team regrab denied for 120 ticks, then arms a fresh clock; opponent
    pickup any time arms fresh.
29. `grace_stays_on_thrower_after_block` — enemy blocks during the thrower's
    self-grace window: LAST_TOUCHER restamps to the blocker but GRACE_ENTITY
    still bars only the thrower; the blocker grabs immediately (D24).
30. `classification_prefers_best_aligned_target` — release aimed dead at a
    cutter posted hoop-side: classifies PASS, not SHOT; aimed dead at the
    hoop with the cutter off-axis: classifies SHOT (D23d joint candidate set).
31. `handler_ladder_shot_over_drive_when_open` — open bot inside shot_sweet
    shoots (rung 1); the same bot with a defender inside press_radius drives
    (rung 2); covered cutter is refused by the open predicate (rung 4); a
    cutter beyond pass_range_max is refused by the range gate (no flat
    heave — rung 5 advances); the exact carrier-cutter-hoop collinear tie
    releases a SHOT and issues NO landing GOTO (D26).
32. `chip_damage_no_fumble` — amount 1 hit on the carrier never fumbles;
    amount >= 2 does; an amount >= 2 hit inside possession_grace does not;
    an amount >= 2 hit on a NON-carrier while someone carries strips
    nothing (the target-identity gate). Uses the staged `set_damage` idiom
    (d < 4.0 deterministic rule, D21).
33. `contested_catch_and_box_reentry` — defender standing on the receiver at
    catch tick → oblist-order race decides (D23a); an alley-oop catch inside
    the enemy box does not dunk until exit + re-entry, while a ground pickup
    inside the box still put-back dunks (D23b, DUNK_OK transitions pinned).
34. `moving_receiver_pass_completes` — receiver walking perpendicular,
    re-directed by the release-time landing-point GOTO, completes the catch at
    receiver_catch_radius 20 (D22); interception radius stays 12.
35. `wipe_watchdog_resets_and_revives` — respawn-off, opponent wiped, winner
    keeps the ball carried (or loiters by it): center reset fires at exactly
    600 ticks and `revive_wiped_teams` restores the team (D25).
36. `pressure_scatter_applies` — identical release with and without an enemy
    inside press_radius (pinned rng state): the pressured landing draws the
    larger E deterministically (D20); E caps at scatter_cap_total, and an
    enemy at exactly L1 25 (one px outside the radius) adds nothing.
37. `director_nonthreatened_defense_stays_home` — 3-active-team court: the
    team NOT defending the attacked hoop sends its one vulture at the
    carrier and posts its remaining member ON its own hoop (§4.4's home
    fan), never at the carrier midpoint.
38. `point_blank_release_still_throws` — D27: a defender parked adjacent in
    the fired weapon's path so the weapon dies on its own act tick
    (`death_called() == 1`); the throw still releases (BALL_STATE leaves
    CARRIED per the aim), the weapon is gone by id (`weapon_present` — dead
    pointers dangle after the tick, judge by id only), the defender keeps
    the chip damage, possession cleared. Second arm: two carrier weapons
    spawned the same tick → the first in weaplist order is consumed, the
    second stays alive (`weapon_present` true), flies on over subsequent
    ticks, and never triggers a second release. Third arm: a defender ON
    the weapon spawn pad — `walker::fire()` deducts cost, melees, kills its
    own spawn (`death_called() == 0`) and returns nullptr; the dead weapon
    (captured from `weaplist.back()` pre-tick) is consumed via the
    ACT_CONTROL arm, the throw releases, and mp returns float-exactly to
    the pre-fire value (D28 net zero across a `fire()`-time death). A gate
    keyed on `death_called() == 1` alone flips this arm. Edge #24 had NO
    pinned test before this row — nothing flips;
    `stale_inflight_weapon_not_consumed` (row 26) stays green because the
    watermark gate, not the dead gate, excludes its stale weapon.
39. `throw_refund_is_mana_neutral` — D28: drive `walker::fire()` directly on
    the carrier (aim staged via `set_lastx/set_lasty`) so the engine really
    deducts `weapon_cost` (`walker.cpp:514`), then tick; post-release
    `magicpoints` equals the pre-fire value float-exactly (net zero across
    fire + consume). Clamp arm: set `magicpoints = max_magicpoints` and
    consume a fixture-spawned (unpaid) weapon → mp stays exactly max (the
    setter does not clamp; the Lua `og.min` must). Control arm: a
    NON-carrier's fired weapon still costs — no refund without consumption.
40. `drained_carrier_release_residual` — edge #28 pin: carrier `magicpoints`
    set below `weapon_cost`; `fire()` returns nullptr (`walker.cpp:506-507`),
    no weapon spawns, no throw releases, BALL_STATE stays CARRIED, mp
    unchanged. Documents the accepted engine-gated residual of D28.
41. `fire_check_probes_never_release_or_mint` — D27 probe immunity / edge
    #29 pin: the carrier is morphed into an engine-AI bot (ACT_RANDOM,
    `user` 0 so `is_directable` skips it and the director's script release
    stays out; specials disabled, regen inert, `busy` staged huge so
    `init_fire` refuses and no real `fire()` can occur), an in-range foe is
    kept set, and COMMAND_FIRE is forced every tick so the dispatch runs
    `fire_check` each tick. Arm 1: mp staged below cost → NoMagic-denial
    probes; eight ticks of CARRIED + float-exact mp (the drained self-refuel
    exploit must not bypass edge #28). Arm 2: mp available, curdir aligned →
    the ray probe (`collide_ob` set on a hit), the shape closest to a real
    `fire()`-time death; same assertions. Both arms fail against the
    blanket alive-or-dead scan (phantom release tick 0), proving teeth.

### 11.3 Function-coverage matrix (Lua function = 100 gate)

The denominator picks the new files up automatically (`lua_inventory.py` roots
include `campaigns/`). Every declared function maps to a driving test; nothing
untestable may be written (I3).

| Function(s) in mode_basketball_impl.lua | Driven by |
|---|---|
| entry-script top-level chunk | fixture mount (refresh_pack_scripts) |
| `make_hooks` + closures | registration + every dispatch test |
| `on_mode_init`, `center_reset`, `sync_render`, small helpers (`fget/fset/bball_active/ball_ground/euclid2/isqrt/in_dunk_box/dist_l1/hoop_center/lone_rival/points_of`) | 1, 3, 7, 16 |
| `run_pickup`, `try_catch`, `gain_possession`, `stamp_toucher`, `grace_barred`, `arm_grace`, `weapon_watermark`, `enemy_box_at` (watermark + POSSESS_SINCE stamps, late-regain check, DUNK_OK arming) | 3, 4, 22, 26, 27, 33 |
| `run_carry`, `drop_ball` | 4, 11, 12 |
| `consume_throw` (watermark gate; D27 qualify gate — alive / `death_called` / ACT_CONTROL arms, probe skip; D28 inline refund + clamp — no new function, denominator unchanged), `release_throw` (joint classification, open/aligned compares), `solve_flight` (all 3 arms + scatter + pressure), `press_count` | 6, 7, 20, 26, 30, 36, 38, 39, 40, 41 |
| `run_air`, `move_substeps`, `run_free`, `run_ground_touch` (substeps, reflection, gravity, bounce, settle; contested-catch race) | 5, 8, 9, 25, 33, 34 |
| `run_swat`, `swat_velocity`, `in_goaltend_window`, `owner_root` (block + goaltend arms; exact impulse pin; zero-step pass-over; GRACE_ENTITY pinning) | 14, 15, 29 |
| `resolve_shot`, `run_crossing`, `score_basket`, `own_basket` | 7, 8, 9, 15, 18 |
| `run_dunk` (DUNK_OK gate + re-arm) | 10, 18, 33 |
| `run_shot_clock`, `turnover` (persistence, late-regain turnover, clearing set) | 13, 27, 28 |
| `run_reset_watchdog` (dead-ball arm + wipe arm) | 16, 35 |
| `on_damage` (threshold + possession grace + target identity) | 11, 32 |
| `on_respawn` | 19 |
| `run_win_check` | 17 |
| `update_hud` | 1, 13 |
| `run_spin` (airborne backspin frame pin) | 3 (toss arm), 22 |
| director set — `run_director`, `run_team_director`, `run_offense`, `run_handler`, `run_support`, `run_defense`, `run_loose`, `run_shot_flight`, `run_faceoff`, `hold_seam`, `send_to`, `directable_members`, `fresh_flags`, `nearest_enemy_hoop` | 20 (`director_roles`), `director_loose_shot_flight_and_faceoff`, 31, 37 |
| `landing_legal`, `respot_at` (ring scan) | 25 |
| fx-bball `on_act` | any ticking test (engine dispatches it) |
| fx-bshadow | declares NO functions (D11 — deliberate) |
| hoisted `ai.dir8/drive_geometry/chaser_drives`, `match.revive_wiped_teams/run_death_scan`, `core.iabs/walker_center` | existing soccer suite + tests 16, 19, 20 |

### 11.4 Levels-sweep updates and the 28→33 literal ledger

`tests/unit/test_modes_levels.cpp`: `shipped_levels()` += 5 rows (`:228-317`);
`EXPECT_EQ(28u, ...)` → 33u (`:444`); ledger expression gains the basketball
+2 term (`:678-705`); hook-mask rows for 824 and 828 =
`kModeInit|kModeTick|kRespawn|kDamage` (`:797-812`); sprite pin rows
`bball.png` 12x12x8 and `bshadow.png` 12x12x4 (`:983-1000`); tick-clean list
+= 824 (`:1025`); new sweep `basketball_courts_match_the_manifest` over
`bball_pins()` (the `soccer_pins()` analog): closed perimeter, 3x3 dunk
carpet with M2 center per hoop, jump tile passable, manifest field spot-checks
through the sandbox.

Full literal ledger (single sweep list): `main.cpp:903-905` (count) and
`:5-7`, `:104-131` (prose); `modes_mapgen.h:3-6`;
`test_modes_levels.cpp:444`; `docs/mp-game-modes.md:3-4,21`;
`docs/ARCHITECTURE.md:669,700-703`; "five modes" prose in `manifest.cpp:5`,
`CMakeLists.txt:907-913`, `mode_strip.lua:1`, `mode_match.lua:1,75`,
`tests/unit/test_modes_strip.cpp:1`, `tests/modes_pack_fixture.h:11`.
Generated outputs (regen produces them, never hand-edit): campaign.yaml
description, `mode_levels.lua`, `scen/scen824-828.fss`,
`pix/scen0824-0828*.png`.

### 11.5 Gate checklist (I3 as commands)

1. `scripts/generate_modes_campaign.sh` twice (first fails by design), then a
   third run + `git status --porcelain -- campaigns/` empty (byte-stability).
2. `cmake --preset ci-test && cmake --build --preset ci-test && ctest --preset
   ci-test` (statement lint runs as an og_gameplay build dep).
3. `OPENGLAD_LUA_COVERAGE=<dir> ctest --preset ci-test` (a PATH, never `1`) —
   line >= 95 / function = 100 on all new pack Lua; judge local numbers by
   before/after delta (CI merges until-pass:3).
4. `cmake --build --preset ci-coverage --target check_luals api_stub_check
   check_lua_statement_lines_full` (api_stub_check is a no-op — no binding
   changes).
5. `python3 scripts/parity/check_mutation_pins.py` — expected clean:
   `campaigns/**` carries no pins and src/ is untouched; this is a tripwire,
   not a task.

---

## Risk register

| R# | Risk | Severity | Mitigation |
|----|------|----------|------------|
| R1 | Walker-speed assumptions (shot clock, AI ranges) miscalibrated — courts too big or clock too tight | medium | D7's 420 was sized against the worst case (828 defensive-stop advance at the 1 px/tick floor, one upcourt pass in the chain); every knob lives in `T` (§2.3, single surface); one human playtest per court before ship — the recorded playtest questions are (a) all-soldier pace on 828, (b) whether drives still under-perform after D20/D21 (fallback: dunk pays 3), (c) PIX_CARPET_M2 legibility (R9); WP10 owns the calibration pass |
| R2 | `kTestRegistrationLua` digest forgotten → coverage hard-fails with "recorded pack script is not repository content" | high | WP6's own checklist: literal edit + `runtime_only_lua.txt` digest in the same commit |
| R3 | Instruction budget: basketball's per-tick pipeline is heavier than soccer's (state machine + swat scan + crossings) | medium | one oblist walk per tick, weaplist scan bounded, <= 2 substeps; test #24 proves 10x headroom; if it trips, hoist per-tick work behind state guards before touching the budget |
| R4 | Mode-var exhaustion — slot 63 is the ONLY spare after the red-team fixes (D19/D21/D23/D24 claimed 59-62) | high | D2 chose the minimal manifest; any future feature must pass a slot review before claiming 63; documented repack candidates if more are ever needed: DUNK_OK, GRACE_TEAM1 and SHOT_VALUE are all sub-byte and can share a slot behind `fget/fset` helpers |
| R5 | Family ordinality regression — a future family file sorting before `fx-ball.lua` silently shifts wire ids | high | I5 naming rule + test #22 puts bytes 21/22/23 on the wire every CI run |
| R6 | `audit_generator_spawn_exits` flags 828's 1-wide alcove mouths | low | BONEYARD-shape copy passes today; documented fallback = widen the mouth to 2 tiles, never a waiver |
| R7 | Ballistic truncation drift — landing off SHOT_LAND by accumulated `og.div` truncation | low | error bound < 0.06 px per solve (§2.6), absorbed by rim bands; test #7 pins exact landing distances |
| R8 | D17 hoist changes soccer behavior | medium | pure parameterization, no math change; og_unit_soccer's director tests pin exact GOTO targets and the soccer determinism digest must stay green before WP4 builds on the hoisted libs |
| R9 | PIX_CARPET_M2 reads poorly as "the hoop" at a glance (art legibility) | low | human eyeball item at first playtest; fallback is a decor mark or a distinct carpet id — a paint change only, no schema impact |
| R10 | 826 four-team scrums: announce/sound spam from frequent rim clangs | low | rim clang is positional-only (no text); if playtest confirms spam, add a per-sound cooldown var from the spare slots |
| R11 | Campaign regeneration not byte-stable (env-dependent output) | high | the third-run check in §11.5 step 1; the generator already redirects OPENGLAD_CONFIG_DIR and runs from the repo root |

---

## Explicitly rejected (record in the PR description)

- Real `worldz` for the ball — Lua has no setter; adding one violates I1, and
  mirror worldz steps at snapshot rate (ugly arcs) while setxy-y interpolates.
- Engine ground shadow — `casts_ground_effects` is Living/Weapon only;
  extending it is a src/ render change (I1).
- Reusing `goal_rects`/`kickoff` names for basketball rows — wrong vocabulary,
  couples two modes' schemas (D2).
- Negative `og.award_score` arms — unsigned; forfeits touch the POINTS metric
  only (§3.3, I3).
- A dedicated shot-clock HUD line — collides with 4-team score lines; suffix
  on the possessing team's line instead (§8).
- fx-bshadow `on_act` stub — a permanently uncovered function under the
  function = 100 gate (D11).
- Dedicated `og_unit_basketball` group — two extra roster edits for negligible
  parallelism (D13).
- `key_rects` / per-hoop `{rim, arc, dunk}` manifest records — derived or
  constant; 8 mode vars and schema surface saved (D2).
- Paint-vs-predicate arc lockstep self-check — the ring is cosmetic and paint
  order legitimately truncates it (D18); the §5.5 quadrant presence check is
  the retained floor.
- M's 120-tick shot clock — untenable at walker speeds (D7); S's 288 likewise
  fell to the 828 backcourt arithmetic (red team, D7 amended).
- Proximity gate for throw provenance — returning weapons (boomerangs)
  re-enter any radius; the id watermark is airtight (D19).
- A pass-landing beacon for human receivers — `og.set_beacon` is entity-bound
  and no entity stands at the landing point; the shadow already tracks the
  ball's ground line (D22).
- Attendance-eligibility dead-ball counting (auditor's alternative babysit
  fix) — subsumed by the wipe watchdog (D25), which also covers the
  carried-ball stall the attendance rule cannot reach.
- Backcourt-gated clock start (arm on pickup, count from frontcourt entry) —
  needs an armed-but-dormant clock state that complicates the D25 persistence
  rules; the flat 420 covers the same arithmetic with one number (D7).

---

## Appendix: verified anchors

Quick-reference of load-bearing file:line claims (from the scout fact files;
items marked `[rv]` were re-verified directly at `0ed817cf` during synthesis).
Re-verify any anchor before editing.

- `campaigns/modes/packs/modes.core/lib/mode_core.lua:16-22` — MODE table ends
  at MUTANT = 5 `[rv]`; `:113-114` — registration scan band 300-899 `[rv]`.
- `campaigns/modes/packs/modes.core/lib/mode_soccer_impl.lua` — slot map
  `:14-37`, tuning `:39-102`, `run_kicks` `:268-293`, `run_shots` `:298-332`,
  `run_flight` `:338-409`, `run_spin` `:420-440`, `own_goal_beneficiary`
  `:461-476`, forfeit comment `:511-519`, `run_dead_ball` `:533-557`,
  `run_death_scan` `:564-597`, director `:794-904`, init `:931-1071`,
  `make_hooks` `:1121-1129` `[rv, read in full]`.
- `src/gameplay/script/bindings_entity.cpp` — `og.mode_get/set` `:2200-2227`
  (64 int32 vars); `og.award_score` unsigned `:1696-1710`; `set_hud_line`
  `:2246-2271`; `set_beacon` `:2288-2313`; `set_mode_name` `:2229-2241`;
  `og_team_color_name` `:2386` `[rv]`; no worldz/vz setter in kWalkerMethods
  `:2600-2714`.
- `include/openglad/gameplay/mode/mode_state.h:27-31` — kModeVarCount 64,
  kModeHudLines 4, kModeNameBytes 12.
- `src/gameplay/game_world.cpp:610-628` — add_ob vs add_fx_ob; `:1860-1875` —
  fxlist never acts; `:724-803` — tile passability accept-list includes
  PIX_CARPET_M2 and all small runners `[rv]`; `query_grid_passable`
  `:672-772` — grid-only, no RNG.
- `src/gameplay/effect.cpp:40,85-88` — effects ignore(1), on_act true skips
  engine motion.
- `src/interface/render/walker_draw.cpp:256-316` — mirror x/y interpolation;
  `:749-763` — ground shadows exclude effects.
- `src/gameplay/world_snapshot.cpp:709-760, 1221-1223, 1650, 1771` — ModeState
  + fxlist + position + frame replication.
- `include/openglad/core/pixdefs.h` — PIX_CARPET_M 33, **PIX_CARPET_M2 34**,
  PIX_CARPET_SMALL_HOR/VER 127/128, PIX_FLOOR1 6, PIX_COBBLE 72/73/75/76
  `[rv]`; `include/openglad/core/decordefs.h:20-33` — DECOR_PEBBLES 9,
  DECOR_COLUMN_BOTTOM 10, DECOR_BONES 13 `[rv]`.
- `tools/modes_mapgen/main.cpp:903-905` — `rows.size() != 28` hard pin `[rv]`;
  `:936-940` — build call roster `[rv]`; `:964-968` — dispatch roll-call
  `[rv]`; `:235-246` — obmap ledger ball term; `:775-783` — check_sprite
  roll-call; `builders_common.cpp:110-136` — save_level briefing enforcement;
  `manifest.cpp:159-188` — regenerate-and-diff.
- `tests/unit/test_modes_levels.cpp:444` — `EXPECT_EQ(28u, ...)` `[rv]`;
  `:678-705` — ledger sweep; `:797-812` — hook masks; `:983-1000` — sprite
  pins; `:1019-1034` — tick-clean list.
- `tests/modes_pack_fixture.h:59-100` — test level-id blocks (9701+ free);
  `:143-327` — kTestRegistrationLua (sha256 `545a263d…` in
  `scripts/coverage/runtime_only_lua.txt`); `:655-708` — ModeMirror.
- `tests/unit/test_modes_soccer.cpp:205-234` — digest_world; `:1816-1833` —
  instruction-budget proof; `:1848-1907` — pack-family-byte mirror regression.
- `cmake/OpenGladTests.cmake:873-884` — og_unit_modes group + pack-source
  define; `:878` — the one-line test-source insertion point.
- `src/gameplay/script/world_scripts.cpp:1031-1040` — valid hook names;
  `src/gameplay/mode/mode_tick.cpp:136-184` — tick phases, failed-init latch,
  win re-assert.
- D17 verification `[rv]`, corrected by the red team and re-run over all seven
  names: `grep -n "local function \(iabs\|walker_center\|dir8\|drive_geometry\|chaser_drives\|revive_wiped_teams\|run_death_scan\)"
  campaigns/modes/packs/modes.core/lib/*.lua` — the first six match ONLY
  `mode_soccer_impl.lua`; `run_death_scan` matches THREE files
  (`mode_soccer_impl.lua:564`, `mode_ctf_impl.lua:241`,
  `mode_onslaught_impl.lua:251`) with mode-specific semantics (only soccer's
  reads `og.match_setting("respawn_mode")`). The hoist is required, not
  optional, and moves soccer's variant only (D17).
- Statement-lint rules (incl. rule 8 duplication ban):
  `scripts/check_lua_statement_lines.py` header lines 31-79; coverage bar:
  `.github/workflows/coverage.yml:212-213` (line 95 / function 100).
