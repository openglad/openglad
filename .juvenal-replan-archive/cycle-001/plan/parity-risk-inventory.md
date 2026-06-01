# Parity Risk Inventory — `wip/networking` vs `origin/master`

Branch `wip/networking` is **341 commits** ahead of `origin/master`
(`16963de0eea0bdccdbe9e0b85825bac9cc1ab0cd`). Diff stats below come from
`git diff origin/master..HEAD --stat` against a freshly-fetched `origin/master`.
Every file referenced here exists on disk and was verified via `git ls-tree`
(master side) and the local working tree (branch side).

## Subsystems at risk

| # | subsystem | branch files | origin/master-equivalent files | diff size | risk level | failure mode if broken |
|---|---|---|---|---:|---|---|
| 1 | Walker AI and movement | `src/gameplay/walker.cpp`, `src/gameplay/walker_movement.cpp`, `src/gameplay/walker_pathing.cpp`, `include/openglad/gameplay/walker.h`, `include/openglad/gameplay/sim_entity.h` | `src/gameplay/walker.cpp`, `src/gameplay/walker_movement.cpp`, `src/gameplay/walker_pathing.cpp`, `include/openglad/gameplay/walker.h`, `include/openglad/gameplay/sim_entity.h` | walker.cpp 1025 / walker_movement.cpp 195 / walker.h 179 / sim_entity.h 229 | **HIGH** | Enemies stop wandering, pathfinding takes wrong turns, walkers wedge against walls, or AI patrol radii diverge. Final positions in headless replay no longer match master. |
| 2 | Combat math and damage | `src/gameplay/walker_combat.cpp`, `src/gameplay/living.cpp`, `src/gameplay/stats.cpp`, `include/openglad/gameplay/statistics.h` | `src/gameplay/walker_combat.cpp`, `src/gameplay/living.cpp`, `src/gameplay/stats.cpp`, `include/openglad/gameplay/statistics.h` | walker_combat 187 / living.cpp 382 / stats.cpp 432 / statistics.h 114 | **HIGH** | HP, defense, level-up curve or random damage rolls drift. Goldens show wrong final `hp` or `max_hp`; survivability of fixed-seed encounters flips. |
| 3 | Special abilities per family (cleric/druid/mage/archer/elf/thief/archmage/barbarian/soldier/orc/slime/skeleton/ghost/fire_elemental) | `src/gameplay/families/family_*.cpp` (15 files), `src/gameplay/walker_specials.cpp` | same paths on master | family_archmage 227, family_cleric 136, family_mage 136, family_thief 86, family_elf 69, family_soldier 68, family_slime 52, family_barbarian 52, family_druid 48, family_archer 43, family_fire_elemental 36, family_orc 26, family_skeleton 12, family_ghost 6, walker_specials 60 | **HIGH** | A family's special (heal, summon, projectile cast, backstab, teleport, shockwave) fires on the wrong tick or with wrong magnitude. Detected via per-family scenario probes. |
| 4 | Effect lifecycle (bombs, chains, doors, clouds, ghost-scare, knife-back, shields) | `src/gameplay/effect.cpp`, `src/gameplay/families/effect_family_*.cpp` (7 files), `include/openglad/gameplay/effect.h` | same paths on master | effect.cpp 50, effect_family_chain 90, effect_family_shield 67, effect_family_knife_back 60, effect_family_bomb 51, effect_family_cloud 20, effect_family_ghost_scare 14, effect_family_door_open 14 | **HIGH** | Effects persist too long / vanish too early; chain-lightning hits wrong number of targets; bomb radius or damage curve drifts. |
| 5 | Summon and pet behavior | `src/gameplay/families/family_druid.cpp`, `src/gameplay/families/family_cleric.cpp`, `src/gameplay/families/family_archmage.cpp`, `src/gameplay/families/weapon_family_animate.cpp`, `include/openglad/gameplay/summon.h`, `src/gameplay/walker_specials.cpp` | same paths on master | druid 48, cleric 136, archmage 227, weapon_family_animate 43, summon.h 6 | **HIGH** | Summoned pets fail to inherit `real_team_num`, refuse to obey master, attack their summoner, or count toward wrong team's foe-tally. |
| 6 | Scoring and team statistics | `src/gameplay/stats.cpp`, `include/openglad/gameplay/statistics.h`, `include/openglad/gameplay/game_world.h` (`m_score[4]`, `my_team`, `allied_mode` at lines 227–229) | `src/gameplay/stats.cpp`, `include/openglad/gameplay/statistics.h`, `include/openglad/gameplay/game_world.h` | stats.cpp 432, statistics.h 114, game_world.h 125 | **MEDIUM** | XP, kill counts, gold totals, allied-mode award splits diverge. Goldens fail on `score_per_team[]`. |
| 7 | Save format read / write | `src/resources/save_data.cpp`, `include/openglad/resources/save_data.h`, `src/resources/io/zip_api.cpp` | same paths on master | save_data.cpp 4, save_data.h 2, zip_api.cpp 7 (small but on the critical persistence path; on-disk byte layout must remain compatible) | **MEDIUM** | A save written by master fails to load on the branch, or a save round-trips with shifted fields. Failure detected by loading a fixed save blob and asserting walker count / hp on first tick. |
| 8 | Scenario load and exit-trigger firing | `src/resources/gloader.cpp`, `src/resources/gparser.cpp`, `src/resources/level_file_io.cpp`, `src/gameplay/game_world.cpp` (`level_done`, `next_level`, `ending` at game_world.h:214-217), `src/gameplay/families/treasure_family_navigation.cpp` (exit / portal treasures) | same paths on master | gloader 48, gparser 9, level_file_io 36, game_world.cpp 710, treasure_family_navigation 30 | **HIGH** | `.fss` parse drift, exit trigger never fires, `next_level` ends up wrong. Detected by running `scen/scen9301.fss` to its scripted exit and asserting `level_done == 1` + `level_exited` event at a known tick. |
| 9 | Tick cadence (sim-vs-render decoupling) | `src/gameplay/game_world.cpp::tick()`, `include/openglad/gameplay/game_world.h:208` (`void tick();`), `include/openglad/core/sim_cadence.h` (new), `src/platform/sdl/game_loop.cpp` (`FrameDeadlinePacer` call site), `src/platform/sdl/local_transport_shadow.cpp` (drives `tick()` once per client step) | `src/gameplay/game_world.cpp::tick()`, `include/openglad/gameplay/game_world.h::tick()`, `src/platform/sdl/game_loop.cpp` (master drives `walker->act()` per-walker out of `world().timer_wait`) | game_world.cpp 710, game_loop.cpp 452, sim_cadence.h 40 (new), local_transport_shadow.cpp 1132 (new) | **CRITICAL** | An off-by-one in `tick_budget` enforcement (branch calls `world.tick()` N times via the new pacer; master ran the per-walker loop driven by `timer_wait`) shifts every projectile / AI decision by one frame. Both branches share the `GameWorld::tick()` signature, but the call site and pacing differ — see plan.md §3 phase 3 `## Tick-cadence parity contract`. Detected when fixed-input replays produce different walker positions at the same `tick_count_`. |
| 10 | RNG seeding and determinism | `include/openglad/gameplay/game_world.h:211` — public field `og::sim::SimRandom rng_`; `SimRandom::state_` public at game_world.h:50; consumed in `src/gameplay/walker_combat.cpp`, `src/gameplay/families/*.cpp`, `src/gameplay/walker.cpp` | same field at the same header path on master (`include/openglad/gameplay/game_world.h::rng_`); same `SimRandom` type | game_world.h 125 | **CRITICAL** | The canonical seeding surface is `world().rng_.state_ = seed;`. If any branch-side call site silently mutates `state_` before the first `tick()` (e.g., new event-log init, `dirty_field_bits.h` setup, or `world_snapshot.cpp` initialization at 2570 LOC), the same nominal seed yields different rolls. Every fixed-seed scenario diverges immediately. |
| 11 | Per-frame transport shadow (single-player path) | `src/platform/sdl/local_transport_shadow.cpp` (new, 1132 LOC), `include/openglad/platform/local_transport_shadow.h` (new, 60 LOC), `src/gameplay/game_client.cpp` (new, 1126), `src/gameplay/game_server.cpp` (new, 2347), `src/gameplay/input_state_net.cpp` (new, 159) | **none — pure addition; master had no client/server split for single-player** | additions only, but every single-player tick now flows through this code | **CRITICAL** | A frame's input is dropped, applied a tick late, or duplicated. Detected by replaying a fixed input-script scenario and asserting walker position equals the master companion at the same `tick_count_`. |
| 12 | World snapshot + dirty-field bookkeeping | `src/gameplay/world_snapshot.cpp` (new, 2570 LOC), `include/openglad/gameplay/world_snapshot.h` (new, 502 LOC), `include/openglad/gameplay/dirty_field_bits.h` (new, 116 LOC), `include/openglad/gameplay/sim_event_log.h` (delta from stub, 26 LOC) | **header `world_snapshot.h` does not exist on master**; `sim_event_log.h` exists as a stub | additions only, but every `SimEntity` setter now marks a dirty bit | **HIGH** | A setter forgets to `mark_dirty(bit)`, so a server-side mutation never propagates to the client mirror — in single-player this means the rendered walker lags the simulated one. Detected by comparing branch in-process `walkers[]` to the master companion. |

## Proposed probes

Each probe is the simplest observable that would catch a regression. Probes are
black-box: drive the headless `og::sim::GameWorld` directly (no SDL), seed
`world.rng_.state_ = scenario.rng_seed`, run `world.tick()` N times, dump state.

1. **Walker AI and movement** — Load `scen/scen9301.fss` (intro-style level), seed
   `rng_.state_ = 0x1`, no player input, drive 300 ticks. Dump
   `(team, family, xpos, ypos)` for every walker. Branch and master companion
   must agree on every walker's final tile. Implemented in
   `tests/parity/test_parity_scenarios.cpp::TEST(Parity, ai_idle_wander_scen9301)`.
2. **Combat math and damage** — Load `temp/scen/scen99.fss` (compact custom test
   scenario), seed `rng_.state_ = 0x42`, scripted input: hold attack on tick 5
   for 60 ticks. Dump `walkers[*].hp` after 200 ticks. Any drift in HP or
   `damage` is flagged. `TEST(Parity, combat_attack_scen99)`.
3. **Special abilities per family** — One scenario per family using
   `temp/scen/scen123.fss`, `temp/scen/scen124.fss`, `temp/scen/scen126.fss`,
   `temp/scen/scen789.fss` (existing custom test scenarios). Each test scripts a
   single-character team, fixed seed `0xF00D`, forces the special-cast input
   sequence on a known tick, then asserts the resulting effect's
   `(family, xpos, ypos, lifetime)` at the dump tick.
   `TEST(Parity, special_<family>_<scenario>)`.
4. **Effect lifecycle** — Load `temp/scen/scen99.fss`, seed `0xBEEF`, script a
   bomb-throw at tick 10, dump at tick 60. Effect must still be in
   `effects[]` on both branches with matching `lifetime` and position, or both
   must have removed it. `TEST(Parity, effect_bomb_lifetime)`.
5. **Summon and pet behavior** — Custom scenario `temp/scen/scen950.fss`, seed
   `0xCAFE`, script druid `summon` on tick 8, dump at tick 80. Summoned pet must
   appear in `walkers[]` with matching `(team, real_team_num, family)` and a
   stable position. `TEST(Parity, summon_druid_pet)`.
6. **Scoring and team statistics** — Reuse the combat probe; additionally dump
   `score_per_team[]` and assert exact integer equality on each team's index.
   `TEST(Parity, scoring_after_combat)`.
7. **Save format read/write** — In-process probe: build the same `walker` /
   `save_data` blob in memory (one fighter team, fixed stats), call the branch's
   `save_data::write` to a `std::stringstream`, then read it back with
   `save_data::read`. Capture the same blob from the master companion. Both must
   yield byte-identical serialized output. `TEST(ParitySave, write_read_roundtrip)`.
8. **Scenario load and exit-trigger firing** — Load `scen/scen9302.fss`,
   seed `0x7`, scripted input drives the team to the exit treasure, dump at the
   tick the exit fires. Assert `level_done == 1`, `next_level` matches, and
   `events[]` contains `level_exited`. `TEST(Parity, exit_trigger_scen9302)`.
9. **Tick cadence (sim-vs-render decoupling)** — Pick `scen/scen9301.fss`,
   declare `tick_budget = 600`. After driving 600 invocations of `world.tick()`,
   `world.tick_count_` must equal 600 on **both** branches. Then assert byte-
   equal walker positions vs master. The probe verifies the
   `## Tick-cadence parity contract` from plan.md phase 3.
   `TEST(Parity, tick_cadence_scen9301)`.
10. **RNG seeding and determinism** — A baseline probe: seed `rng_.state_ = 0`,
    run zero ticks, dump `rng_state` immediately. Confirm both branches read
    back the seed unchanged before any `tick()`. Then seed `rng_.state_ = 1`,
    run 1 tick on an empty scenario (`temp/scen/scen99.fss`), and assert the
    branch's post-tick `state_` equals the master companion's. Detects any
    extra `state_` mutation introduced by `world_snapshot.cpp` or
    `dirty_field_bits.h` setup. `TEST(Parity, rng_seed_stable_after_one_tick)`.
11. **Per-frame transport shadow (single-player path)** — Replay a fixed input
    script (`(tick, player_id, key_mask)` tuples) on `scen/scen9301.fss` with
    seed `0x10` for 200 ticks, with the input flowing through the branch's
    `local_transport_shadow` path on the branch side and applied directly to
    `world.input_state_` on the master side. Walker positions and HPs must
    match. `TEST(Parity, scripted_input_scen9301)`.
12. **World snapshot + dirty-field bookkeeping** — On the branch, after running
    50 ticks on `scen/scen9301.fss` with seed `0x55`, dump the world via the
    server-mirror (the dirty-tracked snapshot) and independently via direct
    `world->walkers` iteration. The two dumps must be byte-equal. (Master has
    only the direct path; this probe is **branch-internal** and exists to
    prove the snapshot does not drop fields. It runs alongside the parity tests
    but does not require a master golden.) `TEST(BranchSnapshot, dirty_bits_cover_all_fields)`.

## Files inspected

.plan/goal.md
.plan/plan.md
include/openglad/gameplay/game_world.h
src/gameplay/walker.cpp
src/gameplay/walker_combat.cpp
src/gameplay/walker_movement.cpp
src/gameplay/walker_pathing.cpp
src/gameplay/walker_specials.cpp
src/gameplay/living.cpp
src/gameplay/stats.cpp
src/gameplay/effect.cpp
src/gameplay/game_world.cpp
src/gameplay/game_client.cpp
src/gameplay/game_server.cpp
src/gameplay/world_snapshot.cpp
src/gameplay/families/family_archer.cpp
src/gameplay/families/family_archmage.cpp
src/gameplay/families/family_barbarian.cpp
src/gameplay/families/family_cleric.cpp
src/gameplay/families/family_druid.cpp
src/gameplay/families/family_elf.cpp
src/gameplay/families/family_fire_elemental.cpp
src/gameplay/families/family_ghost.cpp
src/gameplay/families/family_mage.cpp
src/gameplay/families/family_orc.cpp
src/gameplay/families/family_skeleton.cpp
src/gameplay/families/family_slime.cpp
src/gameplay/families/family_soldier.cpp
src/gameplay/families/family_thief.cpp
src/gameplay/families/effect_family_bomb.cpp
src/gameplay/families/effect_family_chain.cpp
src/gameplay/families/effect_family_cloud.cpp
src/gameplay/families/effect_family_door_open.cpp
src/gameplay/families/effect_family_ghost_scare.cpp
src/gameplay/families/effect_family_knife_back.cpp
src/gameplay/families/effect_family_shield.cpp
src/gameplay/families/weapon_family_animate.cpp
src/gameplay/families/weapon_family_door.cpp
src/gameplay/families/weapon_family_knife.cpp
src/gameplay/families/weapon_family_projectiles.cpp
src/gameplay/families/weapon_family_rock.cpp
src/gameplay/families/weapon_family_wave.cpp
src/gameplay/families/treasure_family_consumables.cpp
src/gameplay/families/treasure_family_navigation.cpp
src/gameplay/families/treasure_family_valuables.cpp
src/platform/sdl/glad.cpp
src/platform/sdl/game_loop.cpp
src/platform/sdl/game_session.cpp
src/platform/sdl/local_transport_shadow.cpp
src/resources/save_data.cpp
src/resources/gloader.cpp
src/resources/gparser.cpp
src/resources/level_file_io.cpp
include/openglad/gameplay/walker.h
include/openglad/gameplay/sim_entity.h
include/openglad/gameplay/statistics.h
include/openglad/gameplay/dirty_field_bits.h
include/openglad/gameplay/world_snapshot.h
include/openglad/gameplay/sim_event_log.h
include/openglad/gameplay/summon.h
include/openglad/gameplay/effect.h
include/openglad/server/headless_server_runtime.h
include/openglad/interface/screen.h
include/openglad/resources/save_data.h
include/openglad/platform/local_transport_shadow.h
include/openglad/core/sim_cadence.h
scen/scen9301.fss
scen/scen9302.fss
scen/scen9303.fss
scen/scen9304.fss
scen/scen9401.fss
scen/scen9402.fss
scen/scen9403.fss
temp/scen/scen99.fss
temp/scen/scen123.fss
temp/scen/scen124.fss
temp/scen/scen126.fss
temp/scen/scen789.fss
temp/scen/scen950.fss
temp/scen/scen9403.fss
temp/scen/scen9410.fss
temp/scen/scen9421.fss
temp/scen/scen9450.fss

## Out of scope

- **WebSocket transports** (`src/platform/sdl/net_transport_websocket_client.cpp`,
  `net_transport_websocket_server.cpp`, `net_transport_relay_ws.cpp`,
  `include/openglad/platform/net_transport_websocket_*.h`) — pure additions on
  the branch, no master equivalent; gameplay parity is concerned with what
  happens *inside* `GameWorld::tick()` regardless of transport, and the
  single-player parity probes already pin the in-process transport.
- **Lobby UI and networked-picker flows**
  (`src/platform/sdl/picker_lobby_network_client.cpp`,
  `include/openglad/interface/ui/picker_lobby_*.h`, `src/gameplay/lobby_server.cpp`,
  `include/openglad/gameplay/lobby_*.h`) — networked lobby is a new feature
  with no master analog; gameplay parity does not cover it.
- **Multiplex / inprocess transport plumbing internals**
  (`src/gameplay/net_transport.cpp`, `net_transport_inprocess.cpp`,
  `net_transport_multiplex.cpp`) — covered indirectly by subsystem #11; we test
  the *observable result* of the transport via fixed-input scripts rather than
  byte-comparing wire frames.
- **Emscripten-only paths**
  (`include/openglad/platform/emscripten/web_runtime_diagnostics.h`,
  `net_transport_emscripten_ws.h`) — web build is out of the `ci-test` parity
  scope; manual smoke is handled separately.
- **Replay subsystem** (`src/gameplay/replay.cpp`, `include/openglad/gameplay/replay.h`,
  `include/openglad/interface/replay_runtime.h`) — branch-only feature; we
  re-use its determinism guarantees inside parity tests but do not parity-test
  replay itself.
- **FPS overlay and render-side instrumentation**
  (`include/openglad/interface/fps_overlay.h`,
  `src/platform/sdl/game_loop.cpp` render-cadence portions,
  `include/openglad/core/runtime_trace.h`) — render-only, observable diff is
  cosmetic; gameplay parity is sim-only.
- **Local stale build artifacts** at repo root (`./openglad_demo`,
  `./openglad_server`, `./libog_*.a`, root `CMakeFiles/`, root `Makefile`,
  `compile_commands.json`) — residue from earlier sessions per plan.md §1; the
  parity harness rebuilds via the documented presets only.
- **`.plan/` residue from prior tasks** (`.plan/findings.md`,
  `.plan/fps60-*.md`, `.plan/perf-findings.md`, `.plan/verification-notes.md`,
  `.plan/plan-before-cleanup.md`, `.plan/show-fps-*.md`) — not part of parity
  work; read-only.
