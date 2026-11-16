/* Shared Westlands in-test sim fixture (og_unit_sim).
 *
 * Loads a level from the builtin War of the Westlands package under the
 * headless unit-test wiring and deploys a playtest crew exactly the way
 * src/platform/text/text_protocol.cpp does (derived stats for placed
 * walkers, family-ctor guys leveled to team_level, deployment onto the
 * authored FAMILY_RESERVED_TEAM start markers). Since the text client's
 * context-before-load fix (F4), openglad_text --protocol runs and this
 * fixture produce bit-identical simulations for the same level/seed/crew:
 * a battle measured here is the battle openglad_text plays with that
 * level, seed and crew (the playtest scripts, scripts/westlands_playtest.sh
 * and its siblings, drive it with a crew of their own).
 *
 * Used by test_melee_standoff.cpp (the L2 wedge pin) and the campaign
 * calibration suites (test_westlands_calibration.cpp and its Long Season,
 * Imaginations, Tower and classic-campaign siblings: the curve-level battle
 * sanity, and the measure modes scripts/calibration/seed_table.py runs).
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
#include <cstdio>
#include <cstdlib>
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
        level.set_sim_context(&save, &events, &cfg);
        gc.rng = &level.world().rng_;
        push_test_context(&gc);
        loaded = level.load();
    }

    ~LoadedWestlandsLevel() { pop_test_context(); }

    GameWorld& world() { return level.world(); }
};

// A start marker deploy_crew can put a crew member on: a live
// FAMILY_RESERVED_TEAM special of any team. deploy_crew below and the
// classic campaigns' measure mode, which prints how many markers a level
// holds (test_classic_campaigns_calibration.cpp), both ask this one test.
inline bool is_crew_start_marker(const walker* m)
{
    return m != nullptr && !m->dead() && m->query_order() == Order::Special &&
           m->family() == FAMILY_RESERVED_TEAM;
}

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
            g->family = static_cast<std::uint8_t>(family);
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
            if (!is_crew_start_marker(m))
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

// The calibration suites play the shipped default (Gameplay Fixes on, New
// Specials on). OG_CALIBRATION_<NAME>=0 measures the classic value.
inline short calibration_setting(const char* name)
{
    const char* v = std::getenv(name);
    return (v != nullptr && v[0] == '0') ? 0 : 1;
}

// The seeds of a many-seed measuring run: OG_CALIBRATION_SEEDS holds
// comma-separated numbers ("42,1337,2025,1,2"), and each calibration
// suite's measure mode plays every one of them (the Tower: layout seeds)
// in that order. Unset or empty, the list is empty and the measure modes
// print what they always printed. Anything else (a word, an empty item, a
// number past 32 bits) fails the running test, so a typo can never
// measure some other list in silence. No test sets it; CI never does.
inline std::vector<std::uint32_t> calibration_seeds()
{
    std::vector<std::uint32_t> seeds;
    const char* text = std::getenv("OG_CALIBRATION_SEEDS");
    if (text == nullptr || text[0] == '\0')
        return seeds;
    const std::string list(text);
    std::size_t start = 0;
    while (true)
    {
        const std::size_t comma = list.find(',', start);
        const std::string item = list.substr(
            start, comma == std::string::npos ? std::string::npos
                                              : comma - start);
        bool number = !item.empty() && item.size() <= 10;
        for (const char c : item)
            number = number && c >= '0' && c <= '9';
        if (!number || std::stoull(item) > 0xFFFFFFFFull)
        {
            ADD_FAILURE() << "OG_CALIBRATION_SEEDS=\"" << list
                          << "\" is not a comma-separated list of numbers "
                             "(item \"" << item << "\")";
            return {};
        }
        seeds.push_back(static_cast<std::uint32_t>(std::stoull(item)));
        if (comma == std::string::npos)
            return seeds;
        start = comma + 1;
    }
}

// The two lines a many-seed measuring run prints per row, in the seed
// list's order: the crew members standing at tick 600 on each seed, and
// the hit points the crew lost on each. `prefix` is "" or "TOWER ";
// `keys` names the row ("level=7 cl=4"). scripts/calibration/seed_table.py
// reads exactly these forms.
inline void print_calibn_lines(const char* prefix, const std::string& keys,
                               const std::vector<int>& alive,
                               const std::vector<float>& taken)
{
    std::printf("%sCALIBN %s survivors@600:", prefix, keys.c_str());
    for (const int a : alive)
        std::printf(" %d", a);
    std::printf("\n%sCALIBN-TAKEN %s taken@600:", prefix, keys.c_str());
    for (const float t : taken)
        std::printf(" %.0f", static_cast<double>(t));
    std::printf("\n");
    std::fflush(stdout);
}

// What a curve battle did: the battle-sanity halves the calibration suites
// assert, and the survivor count their measurement modes print.
struct CurveBattle
{
    int alive = -1;          // crew members still standing at the end
    float crew_taken = 0.0f; // hit points the crew lost to the level
    float crew_dealt = 0.0f; // hit points the crew took off the level
    int host_alive = 0;      // non-crew livings the level fields at deploy
};

// Every living that is not on the crew's team: the garrisons and ambush
// posts the level fields against it (asleep wave spawns included).
inline int host_count(GameWorld& world)
{
    int n = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* const w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living && w->team_num() != 0)
            ++n;
    }
    return n;
}

// Loads `level_id` of the mounted campaign on `seed`, deploys the 8-unit
// mixed fresh team (4 soldiers, elf, archer, cleric, barbarian, the
// playtest harness's representative full-lobby roster) at `crew_level` and
// runs `ticks` ticks. alive stays -1 when the level does not load or the
// crew does not deploy whole.
inline CurveBattle mixed_crew_battle(int level_id, int crew_level,
                                     std::uint32_t seed, int ticks)
{
    CurveBattle out;
    LoadedWestlandsLevel fx(level_id, seed);
    if (!fx.loaded)
        return out;
    GameWorld& world = fx.world();
    world.new_specials = calibration_setting("OG_CALIBRATION_NEW_SPECIALS");
    world.gameplay_fixes = calibration_setting("OG_CALIBRATION_GAMEPLAY_FIXES");
    std::vector<walker*> crew = deploy_crew(
        fx.level, world, {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
                          FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER,
                          FAMILY_CLERIC, FAMILY_BARBARIAN},
        crew_level);
    if (crew.size() != 8u)
        return out;
    out.host_alive = host_count(world);
    for (int t = 0; t < ticks; ++t)
        world.tick();
    out.alive = 0;
    for (walker* w : crew)
    {
        if (w == nullptr)
            continue;
        if (!w->dead())
            ++out.alive;
        if (w->myguy != nullptr)
        {
            out.crew_taken += w->myguy->scen_damage_taken;
            out.crew_dealt += w->myguy->scen_damage;
        }
    }
    return out;
}

} // namespace westlands_fixture
