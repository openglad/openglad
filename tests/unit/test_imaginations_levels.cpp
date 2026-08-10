/* Structural pins for the shipped Imaginations campaign
 * (campaigns/imaginations/, composed into builtin/imaginations.glad; the
 * generator is tools/imaginations_mapgen). The dream-log campaign ships
 * ONE level today — scen 1 "The Raspberry Isle", the first kid-submitted
 * design: an island in open sea, the crew landing spread around the
 * shore, a moated castle in the middle. These pins hold the packaged
 * bytes to the authored structure (floor/grid identity, army counts,
 * text budgets, the loop-home exit) and re-run the og::mapgen ground
 * audits on the SHIPPED package, so a regeneration that breaks footing
 * or seals the castle goes red here, not in a playtest.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader for every level load (mirrors the
// production headless wiring; the campaign uses only stock families).
// ---------------------------------------------------------------------------
loader& imaginations_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

void wire_imaginations_world_entity_services(GameWorld* world,
                                             LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &imaginations_levels_loader();
    world->entity_factory = [game_loader](Order order, std::int32_t family) {
        return game_loader->create_walker_owned(order, family);
    };
    world->entity_configurator =
        [game_loader](walker& entity, Order order,
                      std::int32_t family) -> const PixieData* {
        game_loader->set_walker(&entity, order, family);
        return game_loader->graphics_for(entity.query_order(), entity.family());
    };
    world->entity_derived_stats =
        [game_loader](walker* entity, Order order, std::int32_t family) {
            if (entity != nullptr)
                game_loader->set_derived_stats(entity, order, family);
        };
}

const LevelDataHooks& imaginations_levels_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_imaginations_world_entity_services;
        return h;
    }();
    return hooks;
}

// Mounts the shipped imaginations campaign for the duration of one test
// and restores the previous mount in teardown.
class ImaginationsCampaignTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error("imaginations"))
            << "builtin/imaginations.glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error("imaginations");
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string previous_;
};

// A campaign level loaded with full sim context.
struct LoadedImaginationsLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    FixedRandom rng{0};
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedImaginationsLevel(int id)
        : level(id, true, &imaginations_levels_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &rng, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedImaginationsLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// One row per shipped level — the tool's ExpectedLevel table and this pin
// table move in LOCKSTEP with the package.
struct ShippedLevel
{
    int id;
    const char* title;
    int floors;
    int grid_w;
    int grid_h;
    int start_markers;
    int team1_livings;
    int team1_generators;
    int exits;
    int exit_destination;
};

constexpr ShippedLevel kLevels[] = {
    {1, "The Raspberry Isle", 1, 42, 42, 12, 9, 1, 1, 1},
};

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line).
constexpr std::size_t kBriefingLineBudget = 33;

TEST_F(ImaginationsCampaignTest, levels_round_trip_the_authored_structure)
{
    for (const ShippedLevel& expected : kLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedImaginationsLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded) << "level should load from the archive";
        ASSERT_TRUE(fx.level.generated)
            << "generated scens carry the provenance bit";
        GameWorld& world = fx.world();
        EXPECT_EQ(expected.title, world.title);
        ASSERT_EQ(expected.floors, world.floor_count());
        ASSERT_EQ(expected.grid_w, world.grid.w);
        ASSERT_EQ(expected.grid_h, world.grid.h);
        ASSERT_FALSE(fx.level.description.empty())
            << "the dream-log briefing must ship";
        for (const std::string& line : fx.level.description)
            EXPECT_LE(line.size(), kBriefingLineBudget) << line;

        int starts = 0;
        int livings[MAX_TEAM + 1] = {};
        int generators[MAX_TEAM + 1] = {};
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr)
                continue;
            const int team = ob->team_num() & 7;
            if (ob->query_order() == Order::Living)
                ++livings[team];
            else if (ob->query_order() == Order::Generator)
                ++generators[team];
            else if (ob->query_order() == Order::Special &&
                     ob->family() == FAMILY_RESERVED_TEAM && team == 0)
                ++starts;
        }
        EXPECT_EQ(expected.start_markers, starts);
        EXPECT_EQ(0, livings[0]) << "the crew has no placed allies yet";
        EXPECT_EQ(expected.team1_livings, livings[1]);
        EXPECT_EQ(expected.team1_generators, generators[1]);

        int exits = 0;
        for (const auto& uptr : world.fxlist)
        {
            walker* ob = uptr.get();
            if (ob != nullptr && ob->query_order() == Order::Treasure &&
                ob->family() == FAMILY_EXIT)
            {
                ++exits;
                EXPECT_EQ(expected.exit_destination, ob->stats()->level())
                    << "the newest level's exit loops home to 1";
            }
        }
        EXPECT_EQ(expected.exits, exits);
    }
}

TEST_F(ImaginationsCampaignTest, isle_is_an_island_with_a_moated_castle)
{
    LoadedImaginationsLevel fx(1);
    ASSERT_TRUE(fx.loaded);
    const PixieData& g = fx.world().grid;

    // Water genre: the sea tiles and every smoothed shore face.
    auto is_water_class = [](unsigned char t) {
        static constexpr unsigned char kWater[] = {
            PIX_WATER1,        PIX_WATER2,        PIX_WATER3,
            PIX_WATERGRASS_LL, PIX_WATERGRASS_LR, PIX_WATERGRASS_UL,
            PIX_WATERGRASS_UR, PIX_WATERGRASS_U,  PIX_WATERGRASS_L,
            PIX_WATERGRASS_R,  PIX_WATERGRASS_D,
        };
        return std::find(std::begin(kWater), std::end(kWater), t) !=
               std::end(kWater);
    };

    // The sea rings the map: all four grid corners are open water.
    EXPECT_TRUE(is_water_class(g.data[0]));
    EXPECT_TRUE(is_water_class(g.data[static_cast<std::size_t>(g.w - 1)]));
    EXPECT_TRUE(is_water_class(
        g.data[static_cast<std::size_t>((g.h - 1) * g.w)]));
    EXPECT_TRUE(is_water_class(
        g.data[static_cast<std::size_t>(g.h * g.w - 1)]));
    // The moat is the submitted "sea in the middle": northwest of the
    // castle wall, off the paved causeways, sits water.
    EXPECT_TRUE(is_water_class(g.data[static_cast<std::size_t>(17 + 12 * g.w)]));
    // And the crew's landing ring is dry ground where the lead deploys.
    EXPECT_TRUE(og::mapgen::ground_cell_standable(
        g.data[static_cast<std::size_t>(20 + 33 * g.w)]));
}

TEST_F(ImaginationsCampaignTest, shipped_package_passes_the_ground_audits)
{
    for (const ShippedLevel& expected : kLevels)
    {
        SCOPED_TRACE("scen" + std::to_string(expected.id));
        LoadedImaginationsLevel fx(expected.id);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        for (const std::string& err : og::mapgen::audit_footing(world))
            ADD_FAILURE() << err;
        for (const std::string& err : og::mapgen::audit_reachability(world))
            ADD_FAILURE() << err;
        for (const std::string& err :
             og::mapgen::audit_generator_spawn_exits(world))
            ADD_FAILURE() << err;
    }
}

} // namespace
