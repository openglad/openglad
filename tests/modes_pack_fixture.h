#pragma once
/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Shared fixture for the multiplayer-modes campaign-pack tests
// (og_unit_modes now; the TDM/Mutant/Soccer/Onslaught/Basketball waves
// reuse it).
//
// Builds a temporary .glad embedding the CURRENT modes pack sources
// (campaigns/modes/packs/modes.core/**)
// sources (byte-copied, so the tests always exercise the shipped Lua) plus
// one test-only registration script that binds the CTF impl to programmatic
// level ids and registers a mode_core probe level. Mounting the campaign
// registers the pack chunks and installs the family descriptors
// (refresh_pack_scripts runs inside mount_campaign_package_with_error).
//
// Teardown EXACT-restores the campaign mount to the state SetUp found —
// never a forced default mount (the PhysFS-roundtrip landmine described in
// test_campaign_sprite_reload.cpp).

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>

#include "test_game_world_fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

std::string get_user_path();
std::string get_asset_path();

namespace og::script {
extern std::int64_t g_test_world_instruction_budget;
}

namespace og::modes_test {

// RAII override of the per-world Lua instruction budget. The restore runs
// on every exit path, so an early fatal ASSERT inside a budget-probed test
// cannot leak a reduced budget into every later test in the binary.
struct BudgetOverride
{
    explicit BudgetOverride(std::int64_t v)
    {
        og::script::g_test_world_instruction_budget = v;
    }
    ~BudgetOverride() { og::script::g_test_world_instruction_budget = 0; }
    BudgetOverride(const BudgetOverride&) = delete;
    BudgetOverride& operator=(const BudgetOverride&) = delete;
};

inline constexpr const char* kCampaignId = "org.test.modespack";
inline constexpr const char* kRulesPackId = "modes.core";

// Level ids the test registration script binds (all authored 0x20 by the
// world fixture below).
inline constexpr int kCtfLevelA = 9001;
inline constexpr int kCtfLevelB = 9002;
inline constexpr int kCtfLevelC = 9003;
inline constexpr int kCtfLevelD = 9004;
// The one CTF level with a MANIFEST row (registered into mode_levels by the
// registration script below): time_limit = 120, the short-clock probe that
// proves CTF reads the row instead of its own T.time_limit_ticks.
inline constexpr int kCtfLevelShortClock = 9005;
inline constexpr int kOtherModeLevel = 9050;  // manifest row for ANOTHER mode
inline constexpr int kProbeLevel = 9090;      // mode_core helper probes
inline constexpr int kTdmLevelA = 9101;       // TDM behavior levels
inline constexpr int kTdmLevelB = 9102;
inline constexpr int kMutantLevelA = 9201;    // Mutant behavior levels
inline constexpr int kMutantLevelB = 9202;

// Soccer test levels (9301+): two-team pitch, four-team pitch, short time
// limit, capped ally generators. Onslaught test levels (9401+): capped
// two-team, three-team, short time limit. Rows live in
// kTestRegistrationLua below; the default 40x60 test grid is 640x960 px.
inline constexpr int kSoccerLevelA = 9301;
inline constexpr int kSoccerLevelB = 9302;
inline constexpr int kSoccerLevelC = 9303;
inline constexpr int kSoccerLevelD = 9304;
inline constexpr int kOnsLevelA = 9401;
inline constexpr int kOnsLevelB = 9402;
inline constexpr int kOnsLevelC = 9403;

// lib/mode_strip.lua probes (9500+): the shared scenario-troops strip the
// six mode impls call from on_mode_init. One level per opts shape, so the
// keep_generators arm is exercised on its own.
inline constexpr int kStripLevelDefault = 9500;      // opts = nil
inline constexpr int kStripLevelKeepGens = 9501;     // keep_generators = true
inline constexpr int kStripLevelDropGens = 9502;     // keep_generators = false

// lib/mode_items.lua probes (9600+): synthetic item-pad rows driven
// straight through items.run with test-owned mode-var slots 40/41.
// Level A: five spawnable pads (2 food + the 3 potions) plus one inert
// "gold_bar" pad, row interval absent (the call's default 60 applies).
// Level B: three food pads with row item_interval = 90 overriding the
// call's 60, plus a gold-only zero-interval row probing the fallback arm.
inline constexpr int kItemsLevelA = 9601;
inline constexpr int kItemsLevelB = 9602;
inline constexpr int kItemsCursorSlot = 40;
inline constexpr int kItemsLastSlot = 41;
inline constexpr int kItemsDefaultInterval = 60;
inline constexpr int kItemsRowIntervalB = 90;

// Basketball test courts (9700+, D13). Rows live in kTestRegistrationLua
// below and register through bball.make_hooks, laid out on the default
// 40x60 test grid (640x960 px). 9701 is the reference two-team court: the
// hoops face each other across the middle row, 512 px apart, with the jump
// spot exactly between them; 9702 hangs one hoop on each of the four
// sides. 9703 and 9706-9709 are the manifest-error arms, one missing field
// each, so every on_mode_init refusal has its own row.
inline constexpr int kBballLevelA = 9701;         // two-team reference court
inline constexpr int kBballLevelB = 9702;         // four-hoop court
inline constexpr int kBballLevelNoHoops = 9703;   // hoops field absent
inline constexpr int kBballLevelCaps = 9704;      // reference court + caps
inline constexpr int kBballLevelShort = 9705;     // 120-tick time limit
inline constexpr int kBballLevelNoArc = 9706;     // arc_radius absent
inline constexpr int kBballLevelNoJump = 9707;    // jump_ball absent
inline constexpr int kBballLevelHalfHoops = 9708; // hoop for team 0 only
inline constexpr int kBballLevelNoRow = 9709;     // make_hooks(nil)

// TROOPS: FAIR power-model probes (9095/9096, matched-teams WP-E).
// 9095 logs the pure-function arms (census, walker_power, predicted_power,
// solver, bot_level_for) plus the model-pin ladder over whatever world the
// test authored. 9096 drives match.spawn_bots directly at team 1, cursor
// slot 12, parameterized through input mode vars the C++ test sets before
// the first tick (mode vars are NOT cleared by lazy init): target, plan,
// mid-match flag, and the MATCHED.SIZE headcount latch (D34/D40).
inline constexpr int kMatchProbeLevel = 9095;
inline constexpr int kMatchSpawnProbeLevel = 9096;

// The shared matched-teams header slots of lib/mode_match.lua (table
// MATCHED — packed PLAN codes are base-100 per team, code = L * 10 + k;
// SIZE is the census headcount H the spawn seam truncates squads to,
// matched-teams D34/D40).
inline constexpr int kSlotMatchedTarget = 2;
inline constexpr int kSlotMatchedPlan = 3;
inline constexpr int kSlotMatchedAnnounced = 4;
inline constexpr int kSlotMatchedSize = 5;

// 9096 probe inputs (test-owned scratch slots — mode-private on the probe
// level, which registers its own hooks; relocated 5/6/7 -> 8/9/10 when
// MATCHED.SIZE claimed header slot 5, matched-teams D40).
inline constexpr int kMatchProbeInTarget = 8;
inline constexpr int kMatchProbeInPlan = 9;
inline constexpr int kMatchProbeInMidMatch = 10;
inline constexpr int kMatchProbeInSize = 11;

// Decode one team's packed matched-plan code.
inline int matched_plan_code(std::int32_t plan, int team)
{
    static constexpr std::int32_t kBase[4] = {1, 100, 10000, 1000000};
    return static_cast<int>((plan / kBase[team]) % 100);
}

// Arm matched power for a world — the ONE place tests name the trigger, so
// a future control move is a one-line change here (the amendment moved it
// once already: the team_count sentinel 5 -> the scenario-troops sentinel,
// matched-teams D25/D33(c)). NOTE the bundle (D26): arming means
// TROOPS: FAIR, so the world ALSO strips authored non-guy troops and
// roster-activates exactly like TROOPS: OWN — a fixture world holding
// authored non-guy livings, or asserting fills beyond the roster-activation
// mask, changes meaning when armed (D33(b)).
inline void arm_matched(GameWorld& world)
{
    world.ctf_requested_strip_scenario_troops = og::sim::kTroopsMatched;
}

// Reference-court geometry (rows 9701, 9704, 9705; 9702 reuses the radius
// and the jump spot). Pixel centers, matching the Lua rows byte for byte.
inline constexpr int kBballArcRadius = 128;
inline constexpr int kBballHoop0X = 64;   // the hoop team 0 defends
inline constexpr int kBballHoop0Y = 480;
inline constexpr int kBballHoop1X = 576;  // the hoop team 1 defends
inline constexpr int kBballHoop1Y = 480;
inline constexpr int kBballJumpX = 320;
inline constexpr int kBballJumpY = 480;

// The mode-var slot map of lib/mode_ctf_impl.lua (table S). The behavior
// tests read match state straight from GameWorld::mode.vars, so a silent
// re-map in the Lua breaks them loudly.
enum CtfSlot : int {
    kSlotModeId = 0,
    kSlotPhase = 1,
    kSlotCaptureLimit = 8,
    kSlotRespawnTicks = 9,
    kSlotTeamCount = 10,
    kSlotTeamMask = 11,
    kSlotCpCount = 12,
    kSlotAnchorCursor = 13,
    kSlotCaptures = 14,     // +team
    kSlotFlagEntity = 18,   // +team
    kSlotFlagCarrier = 22,  // +team
    kSlotFlagReturn = 26,   // +team
    kSlotFlagHome = 30,     // +team, packed x*4096+y
    kSlotFlagPos = 34,      // +team, packed
    kSlotCpEntity = 38,     // +cp
    kSlotCpOwner1 = 42,     // +cp, owner+1
    kSlotCpProgress = 46,   // +cp
    kSlotCpProgTeam1 = 50,  // +cp, team+1
    kSlotCpPulseAt = 54,    // +cp
    kSlotCpPos = 58,        // +cp, packed
};

inline constexpr int kModeIdCtf = 2;  // mode_core.MODE.CTF

inline int pos_pack(int x, int y) { return x * 4096 + y; }
inline int pos_x(int v) { return v / 4096; }
inline int pos_y(int v) { return v % 4096; }

// The test-only registration script, embedded as
// packs/modes.core/scripts/zz_modes_test.lua so og.use resolves
// inside the rules pack. It goes through the SAME registration path the
// shipped mode_ctf.lua uses (mode_core.register_mode over manifest rows,
// including a non-matching row), and adds the mode_core probe level.
// EDITING THIS LITERAL? The coverage gate pins its sha256 in
// scripts/coverage/runtime_only_lua.txt (the "kTestRegistrationLua" line).
// Any byte change here MUST update that digest in the same commit, or
// Coverage CI fails with "recorded pack script is not repository content".
inline constexpr const char* kTestRegistrationLua =
    "-- zz_modes_test -- test-only level bindings for og_unit_modes.\n"
    "local core = og.use(\"mode_core\")\n"
    "local match_lib = og.use(\"mode_match\")\n"
    "local levels_lib = og.use(\"mode_levels\")\n"
    "local ctf = og.use(\"mode_ctf_impl\")\n"
    "-- CTF reads its manifest row at tick time (its mode-var band is\n"
    "-- full), so the short-clock probe needs a row the module answers\n"
    "-- with -- the id-keyed map is what a shipped row IS.\n"
    "levels_lib.levels[9005] = { mode = \"ctf\", teams = 2,\n"
    "                            time_limit = 120 }\n"
    "local rows = {\n"
    "  { id = 9001, mode = \"ctf\" },\n"
    "  { id = 9002, mode = \"ctf\" },\n"
    "  { id = 9003, mode = \"ctf\" },\n"
    "  { id = 9004, mode = \"ctf\" },\n"
    "  { id = 9005, mode = \"ctf\" },\n"
    "  { id = 9050, mode = \"tdm\" },\n"
    "}\n"
    "core.register_mode(rows, \"ctf\", {\n"
    "  on_mode_init = ctf.on_mode_init,\n"
    "  on_mode_tick = ctf.on_mode_tick,\n"
    "  on_respawn = ctf.on_respawn,\n"
    "})\n"
    "local tdm = og.use(\"mode_tdm_impl\")\n"
    "local mutant = og.use(\"mode_mutant_impl\")\n"
    "core.register_mode({\n"
    "  { id = 9101, mode = \"tdm\" },\n"
    "  { id = 9102, mode = \"tdm\" },\n"
    "  { id = 9250, mode = \"mutant\" },\n"
    "}, \"tdm\", {\n"
    "  on_mode_init = tdm.on_mode_init,\n"
    "  on_mode_tick = tdm.on_mode_tick,\n"
    "  on_entity_death = tdm.on_entity_death,\n"
    "  on_respawn = tdm.on_respawn,\n"
    "})\n"
    "core.register_mode({\n"
    "  { id = 9201, mode = \"mutant\" },\n"
    "  { id = 9202, mode = \"mutant\" },\n"
    "}, \"mutant\", {\n"
    "  on_mode_init = mutant.on_mode_init,\n"
    "  on_mode_tick = mutant.on_mode_tick,\n"
    "  on_damage = mutant.on_damage,\n"
    "  on_entity_death = mutant.on_entity_death,\n"
    "  on_respawn = mutant.on_respawn,\n"
    "})\n"
    "og.register_level_hooks(9090, {\n"
    "  on_mode_init = function(level)\n"
    "    og.log(\"difficulty\", og.match_setting(\"difficulty\"))\n"
    "    for _, w in ipairs(og.oblist()) do\n"
    "      if w:dead() == 0 then\n"
    "        if w:s_front_command() == 0 then\n"
    "          local ok = pcall(w.s_refresh_front, w, 1, 2, 3)\n"
    "          og.log(\"refresh_empty\", ok and 1 or 0)\n"
    "        end\n"
    "      end\n"
    "    end\n"
    "    og.log(\"pp\", core.pos_pack(4080, 4095))\n"
    "    og.log(\"pxy\", core.pos_x(core.pos_pack(123, 456)),\n"
    "           core.pos_y(core.pos_pack(123, 456)))\n"
    "    og.log(\"mask\", core.mask_count(11),\n"
    "           core.mask_has(11, 2) and 1 or 0,\n"
    "           core.mask_has(11, 1) and 1 or 0)\n"
    "    og.log(\"madd\", core.mask_add(1, 0), core.mask_add(1, 3))\n"
    "    og.log(\"act\", core.activate_teams(13, 0),\n"
    "           core.activate_teams(13, 2), core.activate_teams(13, 9))\n"
    "    for _, w in ipairs(og.oblist()) do\n"
    "      if w:dead() ~= 0 then\n"
    "        core.scrub_corpse(w)\n"
    "      end\n"
    "    end\n"
    "    core.hud_score_line(0, 3, 7, 9)\n"
    "    og.log(\"lim_full\",\n"
    "           match_lib.resolve_limit({ score_limit = 5 }, \"score_limit\", 0, 20),\n"
    "           match_lib.resolve_limit({ score_limit = 5 }, \"score_limit\", 7, 20))\n"
    "    og.log(\"lim_sparse\",\n"
    "           match_lib.resolve_limit({ mode = \"tdm\" }, \"score_limit\", 0, 20),\n"
    "           match_lib.resolve_limit({ mode = \"tdm\" }, \"time_limit\", 0, 7200))\n"
    "    og.log(\"lim_nil_zero\",\n"
    "           match_lib.resolve_limit(nil, \"score_limit\", 0, 20),\n"
    "           match_lib.resolve_limit({ time_limit = 0 }, \"time_limit\", 0, 7200))\n"
    "  end,\n"
    "})\n"
    "local soccer = og.use(\"mode_soccer_impl\")\n"
    "local soccer_rows = {\n"
    // 9301 is the item-pad probe row: two drumstick pads on the tiles the
    // 9601 timeline already proves spawnable, far up-field of the goals
    // and the kickoff spot, on a 30-tick interval so a short run fires.
    "  { id = 9301, mode = \"soccer\", teams = 2, time_limit = 10800,\n"
    "    score_limit = 3,\n"
    "    item_pads = { { x = 168, y = 168, family = \"drumstick\" },\n"
    "                  { x = 232, y = 168, family = \"drumstick\" } },\n"
    "    item_interval = 30,\n"
    "    goal_rects = { [0] = { x = 16, y = 400, w = 32, h = 128 },\n"
    "                   [1] = { x = 592, y = 400, w = 32, h = 128 } },\n"
    "    kickoff = { x = 320, y = 464 } },\n"
    "  { id = 9302, mode = \"soccer\", teams = 4, time_limit = 10800,\n"
    "    score_limit = 3,\n"
    "    goal_rects = { [0] = { x = 256, y = 16, w = 128, h = 32 },\n"
    "                   [1] = { x = 592, y = 256, w = 32, h = 128 },\n"
    "                   [2] = { x = 256, y = 912, w = 128, h = 32 },\n"
    "                   [3] = { x = 16, y = 256, w = 32, h = 128 } },\n"
    "    kickoff = { x = 320, y = 480 } },\n"
    "  { id = 9303, mode = \"soccer\", teams = 2, time_limit = 120,\n"
    "    score_limit = 3,\n"
    "    goal_rects = { [0] = { x = 16, y = 400, w = 32, h = 128 },\n"
    "                   [1] = { x = 592, y = 400, w = 32, h = 128 } },\n"
    "    kickoff = { x = 320, y = 464 } },\n"
    "  { id = 9304, mode = \"soccer\", teams = 2, time_limit = 10800,\n"
    "    score_limit = 3, spawn_caps = { [0] = 2, [1] = 2 },\n"
    "    goal_rects = { [0] = { x = 16, y = 400, w = 32, h = 128 },\n"
    "                   [1] = { x = 592, y = 400, w = 32, h = 128 } },\n"
    "    kickoff = { x = 320, y = 464 } },\n"
    "}\n"
    "for i = 1, #soccer_rows do\n"
    "  og.register_level_hooks(soccer_rows[i].id,\n"
    "                          soccer.make_hooks(soccer_rows[i]))\n"
    "end\n"
    "og.register_level_hooks(9308, soccer.make_hooks({\n"
    "  id = 9308, mode = \"soccer\", teams = 2, time_limit = 10800,\n"
    "  score_limit = 3,\n"
    "  goal_rects = { [0] = { x = 16, y = 400, w = 32, h = 128 } },\n"
    "  kickoff = { x = 320, y = 464 } }))\n"
    "og.register_level_hooks(9309, soccer.make_hooks(nil))\n"
    "local onslaught = og.use(\"mode_onslaught_impl\")\n"
    "local ons_rows = {\n"
    "  { id = 9401, mode = \"onslaught\", teams = 2, time_limit = 14400,\n"
    "    spawn_caps = { [0] = 3, [1] = 3, [7] = 2 } },\n"
    "  { id = 9402, mode = \"onslaught\", teams = 3, time_limit = 14400 },\n"
    "  { id = 9403, mode = \"onslaught\", teams = 2, time_limit = 120 },\n"
    "}\n"
    "for i = 1, #ons_rows do\n"
    "  og.register_level_hooks(ons_rows[i].id,\n"
    "                          onslaught.make_hooks(ons_rows[i]))\n"
    "end\n"
    "og.register_level_hooks(9409, onslaught.make_hooks(nil))\n"
    "local strip_lib = og.use(\"mode_strip\")\n"
    "local function strip_hooks(opts)\n"
    "  return {\n"
    "    on_mode_init = function(level)\n"
    "      og.log(\"stripped\", strip_lib.strip_authored_troops(opts))\n"
    "      og.log(\"is_troop\",\n"
    "             strip_lib.is_troop(og.C.ORDER_LIVING, true) and 1 or 0,\n"
    "             strip_lib.is_troop(og.C.ORDER_GENERATOR, true) and 1 or 0,\n"
    "             strip_lib.is_troop(og.C.ORDER_GENERATOR, false) and 1 or 0,\n"
    "             strip_lib.is_troop(og.C.ORDER_TREASURE, false) and 1 or 0)\n"
    "      og.log(\"states\", strip_lib.KEEP, strip_lib.OWN)\n"
    "    end,\n"
    "  }\n"
    "end\n"
    "og.register_level_hooks(9500, strip_hooks(nil))\n"
    "og.register_level_hooks(9501, strip_hooks({ keep_generators = true }))\n"
    "og.register_level_hooks(9502, strip_hooks({ keep_generators = false }))\n"
    "local items = og.use(\"mode_items\")\n"
    "local items_row_a = {\n"
    "  item_pads = {\n"
    "    { x = 168, y = 168, family = \"drumstick\" },\n"
    "    { x = 232, y = 168, family = \"drumstick\" },\n"
    "    { x = 296, y = 168, family = \"gold_bar\" },\n"
    "    { x = 168, y = 232, family = \"magic_potion\" },\n"
    "    { x = 232, y = 232, family = \"invis_potion\" },\n"
    "    { x = 296, y = 232, family = \"speed_potion\" },\n"
    "  },\n"
    "}\n"
    "og.register_level_hooks(9601, {\n"
    "  on_mode_init = function(level)\n"
    "    og.mode_set(41, og.world_tick())\n"
    "    og.log(\"items_nil\", items.run(nil, 40, 41, 60) and 1 or 0,\n"
    "           items.run({}, 40, 41, 60) and 1 or 0,\n"
    "           items.run({ item_pads = {} }, 40, 41, 60) and 1 or 0)\n"
    "  end,\n"
    "  on_mode_tick = function(level, tick)\n"
    "    items.run(items_row_a, 40, 41, 60)\n"
    "  end,\n"
    "})\n"
    "local items_row_b = {\n"
    "  item_pads = {\n"
    "    { x = 168, y = 168, family = \"drumstick\" },\n"
    "    { x = 232, y = 168, family = \"drumstick\" },\n"
    "    { x = 296, y = 168, family = \"drumstick\" },\n"
    "  },\n"
    "  item_interval = 90,\n"
    "}\n"
    "local items_row_b0 = {\n"
    "  item_pads = {\n"
    "    { x = 360, y = 168, family = \"gold_bar\" },\n"
    "  },\n"
    "  item_interval = 0,\n"
    "}\n"
    "og.register_level_hooks(9602, {\n"
    "  on_mode_init = function(level)\n"
    "    og.mode_set(41, og.world_tick())\n"
    "  end,\n"
    "  on_mode_tick = function(level, tick)\n"
    "    items.run(items_row_b, 40, 41, 60)\n"
    "    items.run(items_row_b0, 40, 41, 60)\n"
    "  end,\n"
    "})\n"
    "local bball = og.use(\"mode_basketball_impl\")\n"
    "local bball_rows = {\n"
    // 9701 is the item-pad probe row (soccer 9301's twin): two drumstick
    // pads well clear of the hoops and the jump tile, interval 30.
    "  { id = 9701, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    item_pads = { { x = 168, y = 168, family = \"drumstick\" },\n"
    "                  { x = 232, y = 168, family = \"drumstick\" } },\n"
    "    item_interval = 30,\n"
    "    hoops = { [0] = { x = 64, y = 480 },\n"
    "              [1] = { x = 576, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9702, mode = \"basketball\", teams = 4, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    hoops = { [0] = { x = 320, y = 64 },\n"
    "              [1] = { x = 576, y = 480 },\n"
    "              [2] = { x = 320, y = 896 },\n"
    "              [3] = { x = 64, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9703, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9704, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    spawn_caps = { [0] = 2, [1] = 2 },\n"
    "    hoops = { [0] = { x = 64, y = 480 },\n"
    "              [1] = { x = 576, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9705, mode = \"basketball\", teams = 2, time_limit = 120,\n"
    "    score_limit = 21, arc_radius = 128,\n"
    "    hoops = { [0] = { x = 64, y = 480 },\n"
    "              [1] = { x = 576, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9706, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6,\n"
    "    hoops = { [0] = { x = 64, y = 480 },\n"
    "              [1] = { x = 576, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "  { id = 9707, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    hoops = { [0] = { x = 64, y = 480 },\n"
    "              [1] = { x = 576, y = 480 } } },\n"
    "  { id = 9708, mode = \"basketball\", teams = 2, time_limit = 7200,\n"
    "    score_limit = 6, arc_radius = 128,\n"
    "    hoops = { [0] = { x = 64, y = 480 } },\n"
    "    jump_ball = { x = 320, y = 480 } },\n"
    "}\n"
    "for i = 1, #bball_rows do\n"
    "  og.register_level_hooks(bball_rows[i].id,\n"
    "                          bball.make_hooks(bball_rows[i]))\n"
    "end\n"
    "og.register_level_hooks(9709, bball.make_hooks(nil))\n"
    "local match_squad = { \"core:soldier\", \"core:archer\", \"core:elf\",\n"
    "                      \"core:mage\", \"core:thief\" }\n"
    "-- Every TUPLE row of mode_match.lua: the 5 squad families plus the\n"
    "-- other hook-declaring core families the table carries for future\n"
    "-- rosters (beast's family string id is core:#18).\n"
    "local match_pin_families = { \"core:soldier\", \"core:archer\",\n"
    "                             \"core:elf\", \"core:mage\", \"core:thief\",\n"
    "                             \"core:orc\", \"core:#18\", \"core:cleric\",\n"
    "                             \"core:druid\" }\n"
    "og.register_level_hooks(9095, {\n"
    "  on_mode_init = function(level)\n"
    "    local t, sums = match_lib.census_power(og.oblist())\n"
    "    og.log(\"census\", t, sums[1], sums[2], sums[3], sums[4])\n"
    "    for _, w in ipairs(og.oblist()) do\n"
    "      if w:dead() == 0 and w:order() == og.C.ORDER_LIVING then\n"
    "        og.log(\"wp\", w:team_num(), match_lib.walker_power(w))\n"
    "      end\n"
    "    end\n"
    "    local sfam = og.family_id(\"living\", \"core:soldier\")\n"
    "    local floor_probe = og.add_ob(\"living\", sfam)\n"
    "    floor_probe:set_damage(0)\n"
    "    floor_probe:set_stepsize(0)\n"
    "    og.log(\"wp_floor\", match_lib.walker_power(floor_probe),\n"
    "           og.trunc(floor_probe:s_max_hitpoints()),\n"
    "           og.trunc(floor_probe:s_max_magicpoints()))\n"
    "    local fresh = og.add_ob(\"living\", sfam)\n"
    "    local base = match_lib.measured_base(fresh)\n"
    "    og.log(\"base\", base.hp, base.mp, base.armor, base.dmg,\n"
    "           base.sp, base.ff)\n"
    "    og.log(\"pred\", match_lib.predicted_power(\"core:soldier\", base, 2),\n"
    "           match_lib.predicted_power(\"core:notatuple\", base, 2),\n"
    "           match_lib.predicted_power(\"core:mage\", base, 2),\n"
    "           match_lib.predicted_power(\"core:mage\", base, 3))\n"
    "    local sb = { hp = 100, mp = 0, armor = 0, dmg = 20, sp = 0, ff = 6 }\n"
    "    local bases = {}\n"
    "    for i = 1, 5 do\n"
    "      bases[i] = { family = match_squad[i], stats = sb }\n"
    "    end\n"
    "    local b1, b3, b4, b9 = 0, 0, 0, 0\n"
    "    for i = 1, 5 do\n"
    "      b1 = b1 + match_lib.predicted_power(match_squad[i], sb, 1)\n"
    "      b3 = b3 + match_lib.predicted_power(match_squad[i], sb, 3)\n"
    "      b4 = b4 + match_lib.predicted_power(match_squad[i], sb, 4)\n"
    "      b9 = b9 + match_lib.predicted_power(match_squad[i], sb, 9)\n"
    "    end\n"
    "    local inc = match_lib.predicted_power(match_squad[1], sb, 4)\n"
    "                - match_lib.predicted_power(match_squad[1], sb, 3)\n"
    "    og.log(\"solve_tie_parity\", og.mod(inc, 2))\n"
    "    local l, k, c = match_lib.solve_matched_levels(b3 + og.div(inc, 2),\n"
    "                                                   bases)\n"
    "    og.log(\"solve_tie\", l, k, c and 1 or 0)\n"
    "    local p42 = b4\n"
    "    for i = 1, 2 do\n"
    "      p42 = p42 + match_lib.predicted_power(match_squad[i], sb, 5)\n"
    "                - match_lib.predicted_power(match_squad[i], sb, 4)\n"
    "    end\n"
    "    l, k, c = match_lib.solve_matched_levels(p42, bases)\n"
    "    og.log(\"solve_exact\", l, k, c and 1 or 0)\n"
    "    l, k, c = match_lib.solve_matched_levels(og.div(b1, 2), bases)\n"
    "    og.log(\"solve_low\", l, k, c and 1 or 0)\n"
    "    l, k, c = match_lib.solve_matched_levels(b9 * 2, bases)\n"
    "    og.log(\"solve_high\", l, k, c and 1 or 0)\n"
    "    l, k, c = match_lib.solve_matched_levels(\n"
    "        match_lib.predicted_power(\"core:archer\", sb, 4),\n"
    "        { { family = \"core:archer\", stats = sb } })\n"
    "    og.log(\"solve_n1\", l, k, c and 1 or 0)\n"
    "    og.mode_set(3, 430000)\n"
    "    og.log(\"blf\", match_lib.bot_level_for(2, 1),\n"
    "           match_lib.bot_level_for(2, 3), match_lib.bot_level_for(2, 4),\n"
    "           match_lib.bot_level_for(0, 1))\n"
    "    og.mode_set(3, 0)\n"
    "    og.mode_set(2, 4242)\n"
    "    match_lib.bank_match_target({ matched = true, matched_size = 3 },\n"
    "                                og.oblist())\n"
    "    og.log(\"census_latch\", og.mode_get(2), og.mode_get(5))\n"
    "    og.mode_set(2, 0)\n"
    "    for i = 1, #match_pin_families do\n"
    "      local fam = og.family_id(\"living\", match_pin_families[i])\n"
    "      local fam_base = match_lib.measured_base(og.add_ob(\"living\", fam))\n"
    "      for _, lv in ipairs({ 1, 3, 5 }) do\n"
    "        local w = og.add_ob(\"living\", fam)\n"
    "        w:set_team_num(1)\n"
    "        w:s_set_level(lv)\n"
    "        w:set_difficulty(lv)\n"
    "        og.log(\"modelpin\", match_pin_families[i], lv,\n"
    "               match_lib.walker_power(w),\n"
    "               match_lib.predicted_power(match_pin_families[i],\n"
    "                                         fam_base, lv))\n"
    "      end\n"
    "    end\n"
    "  end,\n"
    "})\n"
    "og.register_level_hooks(9096, {\n"
    "  on_mode_init = function(level)\n"
    "    if og.mode_get(10) == 1 then\n"
    "      og.mode_set(0, 99)\n"
    "    end\n"
    "    og.mode_set(2, og.mode_get(8))\n"
    "    og.mode_set(3, og.mode_get(9))\n"
    "    og.mode_set(5, og.mode_get(11))\n"
    "    match_lib.spawn_bots(2, {}, 12)\n"
    "    match_lib.spawn_bots(1, match_squad, 12)\n"
    "    og.log(\"spawned\", og.mode_get(2), og.mode_get(3), og.mode_get(4))\n"
    "  end,\n"
    "})\n";

inline bool write_text(const std::filesystem::path& path,
                       const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out << text;
    return out.good();
}

// Stages campaign.yaml + the byte-copied pack + the test script, zips the
// package into <user>/campaigns/<id>.glad. Returns false on any IO error.
inline bool install_modes_test_campaign()
{
    namespace fs = std::filesystem;
    const fs::path staging =
        fs::path(get_user_path()) / "modes_test_staging" / kCampaignId;
    std::error_code ec;
    fs::remove_all(staging, ec);
    fs::create_directories(staging, ec);
    if (ec)
        return false;
    if (!write_text(staging / "campaign.yaml",
                    "format: 1\ntitle: Modes Pack Test\nfirst_level: 9001\n"))
        return false;

    const fs::path pack_src{OG_MODES_PACK_SOURCE_DIR};
    const fs::path pack_dst = staging / "packs" / kRulesPackId;
    fs::create_directories(pack_dst, ec);
    if (ec)
        return false;
    fs::copy(pack_src, pack_dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec)
        return false;
    if (!write_text(pack_dst / "scripts" / "zz_modes_test.lua",
                    kTestRegistrationLua))
        return false;

    const fs::path archive = fs::path(get_user_path()) / "campaigns" /
                             (std::string(kCampaignId) + ".glad");
    fs::create_directories(archive.parent_path(), ec);
    fs::remove(archive, ec);
    return zip_contents_with_error(staging.string(), archive.string()) ==
           ArchiveIoError::None;
}

inline void remove_modes_test_campaign()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove(fs::path(get_user_path()) / "campaigns" /
                   (std::string(kCampaignId) + ".glad"),
               ec);
    fs::remove_all(fs::path(get_user_path()) / "modes_test_staging", ec);
}

// Mounts the modes test campaign for one test and exact-restores the
// tracked campaign mount afterwards.
class ModesPackTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        previous_mount_ = get_mounted_campaign();
        ASSERT_TRUE(install_modes_test_campaign());
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(kCampaignId));
        flag_family_ = og::families::resolve_family_string_id(
            Order::Treasure, "modes:flag");
        point_family_ = og::families::resolve_family_string_id(
            Order::Treasure, "modes:waypoint");
        ASSERT_GE(flag_family_, 0) << "modes:flag must install on mount";
        ASSERT_GE(point_family_, 0) << "modes:waypoint must install on mount";
    }

    void TearDown() override
    {
        const std::string mounted = get_mounted_campaign();
        if (mounted == kCampaignId)
            (void)unmount_campaign_package_with_error(mounted);
        remove_modes_test_campaign();
        // EXACT restore, never a forced default mount (the PhysFS-roundtrip
        // landmine; see test_campaign_sprite_reload.cpp).
        const std::string now = get_mounted_campaign();
        if (previous_mount_.empty())
        {
            if (!now.empty())
                (void)unmount_campaign_package_with_error(now);
        }
        else if (now != previous_mount_)
        {
            (void)mount_campaign_package_with_error(previous_mount_);
        }
    }

    int flag_family_ = -1;
    int point_family_ = -1;

private:
    std::string previous_mount_;
};

// The shared test loader: a plain headless loader. The authored flag/
// waypoint bytes (13/14 — what the shipped maps carry) are claimed by the
// modes pack's own families, whose sprites install through the generic
// pack-entity path, so the loader must be built AFTER the fixture mounts
// the pack (the lazy first use below guarantees that).
inline loader& modes_test_loader()
{
    static loader* instance = [] {
        auto* l = new loader{EntityFactory{}};
        return l;
    }();
    return *instance;
}

// LevelDataHooks for loading REAL shipped levels (scenNNN.fss from a
// mounted campaign) inside a unit binary: wires the world entity services
// to the shared loader (og_unit binaries have no SDL platform hooks).
inline void wire_modes_test_entity_services(GameWorld* world,
                                            LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &modes_test_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(),
                                         entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

inline const LevelDataHooks& modes_test_level_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_modes_test_entity_services;
        return h;
    }();
    return hooks;
}

// Scripted-mode world over the mounted rules pack: TYPE_SCRIPTED plus the
// spawn helpers the C++ CTF tests used, retargeted at the pack families.
struct ModesCtfWorld : TestGameWorld
{
    explicit ModesCtfWorld(int level_id = kCtfLevelA)
        : TestGameWorld(level_id)
    {
        loader* game_loader = &modes_test_loader();
        world().entity_factory =
            [game_loader](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [game_loader](walker& entity, Order order,
                          std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [game_loader](walker* entity, Order order, std::int32_t family) {
                if (entity != nullptr)
                    game_loader->set_derived_stats(entity, order, family);
            };
        world().type = GameWorld::TYPE_SCRIPTED;
    }

    walker* spawn_flag(int family, int team, int x, int y, int flag_level = 0)
    {
        walker* flag = world().add_fx_ob(Order::Treasure, family);
        if (flag == nullptr)
            return nullptr;
        flag->setxy(static_cast<short>(x), static_cast<short>(y));
        flag->set_team_num(static_cast<unsigned char>(team));
        if (flag_level > 0 && flag->stats() != nullptr)
            flag->stats()->set_level(flag_level);
        return flag;
    }

    walker* spawn_point(int family, int x, int y)
    {
        walker* point = world().add_fx_ob(Order::Treasure, family);
        if (point == nullptr)
            return nullptr;
        point->setxy(static_cast<short>(x), static_cast<short>(y));
        return point;
    }

    walker* spawn_anchor(int team, int x, int y)
    {
        walker* marker = world().add_ob(Order::Special, FAMILY_RESERVED_TEAM);
        if (marker == nullptr)
            return nullptr;
        marker->setxy(static_cast<short>(x), static_cast<short>(y));
        marker->set_team_num(static_cast<unsigned char>(team));
        return marker;
    }

    // A map teleporter pad (pads pair in fxlist order at matching level).
    walker* spawn_teleporter(int x, int y, int pad_level = 1)
    {
        walker* pad = world().add_fx_ob(Order::Treasure, FAMILY_TELEPORTER);
        if (pad == nullptr)
            return nullptr;
        pad->setxy(static_cast<short>(x), static_cast<short>(y));
        if (pad->stats() != nullptr)
            pad->stats()->set_level(pad_level);
        return pad;
    }

    // Stages a self-teleport for the upcoming world tick (production blink
    // paths stamp inside the act, after tick_count_ already incremented).
    void stage_self_teleport(walker* w)
    {
        w->set_last_self_teleport_tick(world().tick_count_ + 1);
    }

    // An authored generator (soccer/onslaught waves): ACT_GENERATE so the
    // engine spawn machinery runs, difficulty-stamped for full HP.
    walker* spawn_generator(int family, int team, int x, int y,
                            int gen_level = 1)
    {
        walker* gen = world().add_ob(Order::Generator, family);
        if (gen == nullptr)
            return nullptr;
        gen->setxy(static_cast<short>(x), static_cast<short>(y));
        gen->set_team_num(static_cast<unsigned char>(team));
        if (gen->stats() != nullptr)
            gen->stats()->set_level(gen_level);
        gen->set_difficulty(static_cast<std::uint32_t>(gen_level));
        gen->set_act_type(ACT_GENERATE);
        return gen;
    }

    walker* spawn_living(int family, int team, int x, int y,
                         int act_type = ACT_CONTROL)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(static_cast<short>(act_type));
        return w;
    }

    walker* spawn_hero(int family, int team, int x, int y, int guy_id,
                       int act_type = ACT_CONTROL)
    {
        walker* w = spawn_living(family, team, x, y, act_type);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->myguy->id = guy_id;
        return w;
    }

    // A LEVELED roster hero, the authority-path production shape
    // (headless_server_runtime.cpp create_team_walker): level the guy
    // through the family level_up hook, mirror the level into walker
    // stats, then derive combat stats from the trained guy.
    walker* spawn_leveled_hero(int family, int team, int x, int y, int guy_id,
                               short guy_level, int act_type = ACT_CONTROL)
    {
        walker* w = spawn_hero(family, team, x, y, guy_id, act_type);
        if (w == nullptr)
            return nullptr;
        w->myguy->upgrade_to_level(guy_level, true);
        if (w->stats() != nullptr)
            w->stats()->set_level(w->myguy->level);
        w->myguy->update_derived_stats(w);
        return w;
    }

    // Arm matched power for this world (see the free arm_matched above).
    void arm_matched() { og::modes_test::arm_matched(world()); }

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }

    // Mode-var readers over the Lua slot map.
    std::int32_t var(int slot) const { return world().mode.vars[static_cast<std::size_t>(slot)]; }
    std::int32_t team_var(int base, int team) const
    {
        return world().mode.vars[static_cast<std::size_t>(base + team)];
    }
    bool ctf_active() const { return var(kSlotModeId) == kModeIdCtf; }
    bool flag_carried(int team) const
    {
        return team_var(kSlotFlagCarrier, team) != 0;
    }
    bool flag_dropped(int team) const
    {
        return !flag_carried(team) && team_var(kSlotFlagReturn, team) != 0;
    }
    bool flag_at_home(int team) const
    {
        return !flag_carried(team) && team_var(kSlotFlagReturn, team) == 0;
    }
    int flag_x(int team) const { return pos_x(team_var(kSlotFlagPos, team)); }
    int flag_y(int team) const { return pos_y(team_var(kSlotFlagPos, team)); }
    std::uint32_t carrier_id(int team) const
    {
        return static_cast<std::uint32_t>(team_var(kSlotFlagCarrier, team));
    }
    int captures(int team) const { return team_var(kSlotCaptures, team); }
};

// ---------------------------------------------------------------------------
// Mirror replication
//
// Every client renders a MIRROR of the authoritative world, never its own
// simulation: the local-transport shadow points the display GameClient at the
// display world and feeds it the server's snapshots, and networked play uses
// the same GameClient. The server checks that mirror every tick by comparing
// the client's re-captured snapshot hash against its own, and disconnects a
// client that misses kMaxConsecutiveSnapshotHashMismatches (120) in a row.
//
// Mode Lua runs server-side only, so everything a mode does — spawned
// entities, mode vars, HUD, beacons — reaches the client through that
// snapshot. A mode that puts anything on the wire the apply path cannot
// reproduce is therefore a shipping desync, not a cosmetic bug. These helpers
// drive that exact loop headlessly.
// ---------------------------------------------------------------------------

// A mirror world for a mode level: same shared pack loader as the authority,
// its own sim context, no Lua of its own.
struct ModeMirror
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};

    explicit ModeMirror(int level_id)
        : level(level_id, true)
    {
        level.create_new_grid();
        wire_modes_test_entity_services(&level.world(), &level);
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
    }

    GameWorld& world() { return level.world(); }
};

struct MirrorReplication
{
    int strikes = 0;
    int first_strike_tick = -1;
};

// Ticks the authority `fx` for `ticks` ticks, replicating each tick onto
// `mirror`, and counts the ticks the server's hash check would have struck.
inline MirrorReplication replicate_to_mirror(ModesCtfWorld& fx,
                                             ModeMirror& mirror,
                                             int ticks)
{
    MirrorReplication result;
    for (int i = 0; i < ticks; ++i)
    {
        fx.tick(1);
        const og::sim::WorldSnapshot snapshot =
            og::sim::capture_keyframe_snapshot(fx.world());
        std::uint32_t mirror_hash = 0;
        {
            ScopedGameplayContext bound(mirror.level, mirror.save,
                                        mirror.events, cfg);
            og::sim::apply_snapshot(mirror.world(), snapshot);
            mirror_hash = og::sim::compute_snapshot_hash(
                og::sim::peek_keyframe_snapshot(mirror.world()));
        }
        if (mirror_hash != og::sim::compute_snapshot_hash(snapshot))
        {
            ++result.strikes;
            if (result.first_strike_tick < 0)
                result.first_strike_tick = i;
        }
    }
    return result;
}

}  // namespace og::modes_test
