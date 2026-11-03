/* Shared fixture for the Imaginations level pins: the headless entity
 * wiring, the campaign mount and one level loaded with full sim
 * context. Used by test_imaginations_levels.cpp (og_unit_imaginations:
 * the isle and the Castle of Gold) and test_imaginations_asteroid.cpp
 * (og_unit_imaginations_asteroid: the Asteroid Castle).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/mapgen/builders.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/game_context.h>
#include <openglad/resources/gloader.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>

#include "test_gameplay_context_scope.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <string>
#include <vector>

namespace imaginations_levels_fixture {

// ---------------------------------------------------------------------------
// Entity wiring: one shared loader for every level load (mirrors the
// production headless wiring; the campaign uses only stock families).
// ---------------------------------------------------------------------------
inline loader& imaginations_levels_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

inline void wire_imaginations_world_entity_services(GameWorld* world,
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

inline const LevelDataHooks& imaginations_levels_hooks()
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
        level.set_sim_context(&save, &events, &cfg);
        gc.rng = &rng;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedImaginationsLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

inline bool saw_notification(og::sim::SimEventLog& events, const char* needle)
{
    bool seen = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            seen = true;
    return seen;
}

inline void expect_no_script_errors(GameWorld& world)
{
    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << err.where << ": " << err.message;
}

} // namespace imaginations_levels_fixture
