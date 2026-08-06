/* Shared Westlands in-test sim fixture (og_unit_sim).
 *
 * Loads a level from the builtin War of the Westlands package under the
 * headless unit-test wiring and deploys a playtest crew exactly the way
 * src/platform/text/text_protocol.cpp does (derived stats for placed
 * walkers, family-ctor guys leveled to team_level, deployment onto the
 * authored FAMILY_RESERVED_TEAM start markers). Since the text client's
 * context-before-load fix (F4), openglad_text --protocol runs and this
 * fixture produce bit-identical simulations for the same level/seed/crew,
 * so CI pins measured here hold for the playtest harness too.
 *
 * Used by test_melee_standoff.cpp (the L2 wedge pin) and
 * test_westlands_calibration.cpp (the F4 curve floors).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
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

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace westlands_fixture {

inline loader& sim_loader()
{
    static loader instance{EntityFactory{}};
    return instance;
}

inline void wire_world_entity_services(GameWorld* world,
                                       LevelRuntimeData* level)
{
    (void)level;
    if (world == nullptr)
        return;
    loader* game_loader = &sim_loader();
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

inline const LevelDataHooks& sim_hooks()
{
    static const LevelDataHooks hooks = [] {
        LevelDataHooks h{};
        h.wire_world_entity_services = wire_world_entity_services;
        return h;
    }();
    return hooks;
}

// gtest fixture: mounts a builtin campaign package for the test's lifetime
// and restores the previously mounted campaign afterwards. The sim wiring
// above is campaign-agnostic (stock families only), so any builtin package
// can ride the same fixture; the Long Season calibration test derives from
// this with its own id.
class MountedCampaignTest : public ::testing::Test
{
protected:
    explicit MountedCampaignTest(const char* campaign_id)
        : campaign_id_(campaign_id)
    {
    }

    void SetUp() override
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(campaign_id_))
            << "builtin/" << campaign_id_ << ".glad should restore and mount";
    }

    void TearDown() override
    {
        (void)unmount_campaign_package_with_error(campaign_id_);
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

private:
    std::string campaign_id_;
    std::string previous_;
};

class WestlandsCampaignTest : public MountedCampaignTest
{
protected:
    WestlandsCampaignTest()
        : MountedCampaignTest("westlands")
    {
    }
};

struct LoadedWestlandsLevel
{
    LevelRuntimeData level;
    SaveData save;
    og::sim::SimEventLog events;
    GameContext gc;
    ScopedGameplayContext gameplay;
    bool loaded = false;

    explicit LoadedWestlandsLevel(int id, std::uint32_t seed = 0)
        : level(id, true, &sim_hooks())
        , gameplay(level, save, events, cfg)
    {
        level.world().rng_.state_ = seed;
        level.set_sim_context(&save, &level.world().enemy_freeze, &events,
                              &level.world().rng_, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedWestlandsLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// Mirrors src/platform/text/text_protocol.cpp's playtest crew spawn+deploy:
// derived stats for placed walkers, family-ctor guys leveled to team_level,
// deployed onto the authored FAMILY_RESERVED_TEAM start markers.
inline std::vector<walker*> deploy_crew(LevelRuntimeData& level,
                                        GameWorld& world,
                                        const std::vector<int>& families,
                                        int team_level)
{
    for (auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w != nullptr)
            w->set_difficulty(static_cast<std::uint32_t>(w->stats()->level()));
    }

    std::vector<walker*> crew;
    for (std::size_t i = 0; i < families.size(); i++)
    {
        const int family = families[i];
        walker* w = level.add_ob(Order::Living, family);
        if (w)
        {
            w->set_team_num(0);
            w->set_real_team_num(0);
            w->set_user(static_cast<signed char>(i < 4 ? i : static_cast<std::size_t>(-1)));
            auto g = std::make_unique<guy>(family);
            g->family = static_cast<char>(family);
            g->name = std::format("Player{}", i + 1);
            w->set_owned_myguy(std::move(g));
            w->myguy->upgrade_to_level(static_cast<short>(team_level));
            if (w->stats() != nullptr)
                w->stats()->set_level(w->myguy->level);
            w->myguy->update_derived_stats(w);
            crew.push_back(w);
        }
    }

    auto find_team_marker = [&world](int team) -> walker* {
        for (const auto& entity : world.oblist)
        {
            walker* const m = entity.get();
            if (m == nullptr || m->dead())
                continue;
            if (m->query_order() != Order::Special ||
                m->family() != FAMILY_RESERVED_TEAM)
                continue;
            if (team != -1 && m->team_num() != team)
                continue;
            return m;
        }
        return nullptr;
    };
    auto next_team_marker = [&find_team_marker]() -> walker* {
        if (walker* m = find_team_marker(0))
            return m;
        return find_team_marker(-1);
    };
    for (walker* w : crew)
    {
        if (walker* marker = next_team_marker())
        {
            w->set_floor(marker->floor());
            w->setxy(marker->xpos(), marker->ypos());
            marker->set_dead(1);
        }
        else
        {
            w->teleport();
        }
    }
    while (walker* leftover = next_team_marker())
        leftover->set_dead(1);

    world.enemy_freeze = 0;
    world.end = 0;
    return crew;
}

inline int alive_livings_on_team(GameWorld& world, int team)
{
    int n = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* ob = uptr.get();
        if (ob != nullptr && !ob->dead() && !ob->dormant() &&
            ob->query_order() == Order::Living && ob->team_num() == team)
            ++n;
    }
    return n;
}

} // namespace westlands_fixture
