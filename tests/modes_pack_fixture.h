#pragma once
/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Shared fixture for the multiplayer-modes campaign-pack tests
// (og_unit_modes now; the TDM/Mutant/Soccer/Onslaught waves reuse it).
//
// Builds a temporary .glad embedding the CURRENT tools/modes_mapgen/pack/**
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
#include <openglad/gameplay/mode/mode_state.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/io_common.h>

#include "test_game_world_fixture.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

std::string get_user_path();
std::string get_asset_path();

namespace og::modes_test {

inline constexpr const char* kCampaignId = "org.test.modespack";
inline constexpr const char* kRulesPackId = "org.openglad.modes.core";

// Level ids the test registration script binds (all authored 0x20 by the
// world fixture below).
inline constexpr int kCtfLevelA = 9001;
inline constexpr int kCtfLevelB = 9002;
inline constexpr int kCtfLevelC = 9003;
inline constexpr int kCtfLevelD = 9004;
inline constexpr int kOtherModeLevel = 9050;  // manifest row for ANOTHER mode
inline constexpr int kProbeLevel = 9090;      // mode_core helper probes

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
// packs/org.openglad.modes.core/scripts/zz_modes_test.lua so og.use resolves
// inside the rules pack. It goes through the SAME registration path the
// shipped mode_ctf.lua uses (mode_core.register_mode over manifest rows,
// including a non-matching row), and adds the mode_core probe level.
inline constexpr const char* kTestRegistrationLua =
    "-- zz_modes_test -- test-only level bindings for og_unit_modes.\n"
    "local core = og.use(\"mode_core\")\n"
    "local ctf = og.use(\"mode_ctf_impl\")\n"
    "local rows = {\n"
    "  { id = 9001, mode = \"ctf\" },\n"
    "  { id = 9002, mode = \"ctf\" },\n"
    "  { id = 9003, mode = \"ctf\" },\n"
    "  { id = 9004, mode = \"ctf\" },\n"
    "  { id = 9050, mode = \"tdm\" },\n"
    "}\n"
    "core.register_mode(rows, \"ctf\", {\n"
    "  on_mode_init = ctf.on_mode_init,\n"
    "  on_mode_tick = ctf.on_mode_tick,\n"
    "  on_respawn = ctf.on_respawn,\n"
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
    "  end,\n"
    "})\n"
    "local soccer = og.use(\"mode_soccer_impl\")\n"
    "local soccer_rows = {\n"
    "  { id = 9301, mode = \"soccer\", teams = 2, time_limit = 10800,\n"
    "    score_limit = 3,\n"
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
    "og.register_level_hooks(9409, onslaught.make_hooks(nil))\n";

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

    // The pack declares sprites/flag.png + sprites/ctfpoint.png; the mapgen
    // wave ships them. Until it lands, inject the shipped CTF art as donors
    // (file-presence coordination: repo copies win once they exist) so the
    // loader can build the treasure slots and create_walker_owned does not
    // fall back to family 0.
    const fs::path sprites = pack_dst / "sprites";
    fs::create_directories(sprites, ec);
    const std::array<const char*, 3> donor_names = {"flag.png", "flag.json",
                                                    "ctfpoint.png"};
    for (const char* name : donor_names)
    {
        if (fs::exists(sprites / name))
            continue;
        fs::copy_file(fs::path(get_asset_path()) / "pix" / name,
                      sprites / name, ec);
        if (ec)
            return false;
    }

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

// Scripted-mode world over the mounted rules pack: TYPE_SCRIPTED plus the
// spawn helpers the C++ CTF tests used, retargeted at the pack families.
struct ModesCtfWorld : TestGameWorld
{
    explicit ModesCtfWorld(int level_id = kCtfLevelA)
        : TestGameWorld(level_id)
    {
        static loader* game_loader = new loader{EntityFactory{}};
        world().entity_factory =
            [](Order order, std::int32_t family) {
                return game_loader->create_walker_owned(order, family);
            };
        world().entity_configurator =
            [](walker& entity, Order order,
               std::int32_t family) -> const PixieData* {
                game_loader->set_walker(&entity, order, family);
                return game_loader->graphics_for(entity.query_order(),
                                                 entity.family());
            };
        world().entity_derived_stats =
            [](walker* entity, Order order, std::int32_t family) {
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
    walker* spawn_generator(int family, int team, int x, int y, int level = 1)
    {
        walker* gen = world().add_ob(Order::Generator, family);
        if (gen == nullptr)
            return nullptr;
        gen->setxy(static_cast<short>(x), static_cast<short>(y));
        gen->set_team_num(static_cast<unsigned char>(team));
        if (gen->stats() != nullptr)
            gen->stats()->set_level(level);
        gen->set_difficulty(static_cast<std::uint32_t>(level));
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

    void tick(int count = 1)
    {
        for (int i = 0; i < count; ++i)
            world().tick();
    }

    // Mode-var readers over the Lua slot map.
    std::int32_t var(int slot) const { return world().mode.vars[slot]; }
    std::int32_t team_var(int base, int team) const
    {
        return world().mode.vars[base + team];
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

}  // namespace og::modes_test
