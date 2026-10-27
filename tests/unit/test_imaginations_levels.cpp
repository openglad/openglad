/* Structural pins for the shipped Imaginations campaign
 * (campaigns/imaginations/, composed into builtin/imaginations.glad; the
 * generator is tools/imaginations_mapgen): scen 1 "The Raspberry Isle"
 * (an island in open sea, the crew landing spread around the shore, a
 * moated castle in the middle) and scen 2 "The Castle of Gold" (our own
 * castle in a faerie war, gold that wakes allied ghosts, a gate only
 * twelve faerie-held keys can drop) and scen 3 "The Asteroid Castle"
 * (our castle on an asteroid in space, a spike band that bites raiders,
 * the Big One lured off the floating Balloon Rock). These pins hold the packaged
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
        level.set_sim_context(&save, &events, &cfg);
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
    int team0_livings; // placed allies (the crew's own markers excluded)
    int team0_generators; // the Castle of Gold's Ghost Tent
    int team1_livings;
    int team1_generators;
    int exits;
    int exit_destination;
};

constexpr ShippedLevel kLevels[] = {
    {1, "The Raspberry Isle", 1, 64, 64, 16, 0, 0, 19, 3, 1, 2},
    {2, "The Castle of Gold", 1, 64, 64, 12, 0, 1, 27, 0, 1, 3},
    // The newest level's exit loops home to 1 until scen 4 ships.
    {3, "The Asteroid Castle", 2, 64, 64, 16, 1, 0, 22, 0, 1, 1},
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
        EXPECT_EQ(expected.team0_livings, livings[0])
            << "placed allies (the asteroid's Purple Man)";
        EXPECT_EQ(expected.team0_generators, generators[0]);
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
                    << "each dream exits to the next; the newest loops "
                       "home to 1";
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
    // The moat is the submitted "sea in the middle": the square band
    // north of the bailey wall, off the paved causeways, sits water.
    EXPECT_TRUE(is_water_class(g.data[static_cast<std::size_t>(24 + 18 * g.w)]));
    // And the crew's landing ring is dry ground where the lead deploys.
    EXPECT_TRUE(og::mapgen::ground_cell_standable(
        g.data[static_cast<std::size_t>(31 + 55 * g.w)]));
}

TEST_F(ImaginationsCampaignTest, watchtower_ward_arc_plays_out)
{
    LoadedImaginationsLevel fx(1);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick(); // on_load: the ward stamps

    walker* wizard = nullptr;
    std::vector<walker*> towers;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Living ||
            ob->team_num() != 1)
            continue;
        if (ob->family() == FAMILY_MAGE)
            wizard = ob;
        else if (ob->family() == FAMILY_TOWER1)
            towers.push_back(ob);
    }
    ASSERT_NE(nullptr, wizard);
    ASSERT_EQ(2u, towers.size());
    EXPECT_TRUE(wizard->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "the ward holds while the watchtowers stand";

    // The ward must block a LIVE attack, not just wear the flag: a raider
    // whose blow demonstrably lands once the ward drops deals nothing now.
    const float warded_hp = wizard->stats()->hitpoints();
    walker* raider = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, raider);
    raider->set_team_num(0);
    raider->set_damage(50);
    raider->attack(wizard);
    EXPECT_EQ(warded_hp, wizard->stats()->hitpoints())
        << "the warded wizard shrugs off a landed blow";

    // Fell both towers through the sim's death path so on_entity_death
    // dispatches for each (first: one remains; second: the ward fails).
    for (walker* tower : towers)
    {
        tower->stats()->set_hitpoints(0);
        tower->set_dead(1);  // the sim flags dead before the death path runs
        tower->death();      // — the level hook observes that order
        world.tick();
    }
    EXPECT_FALSE(wizard->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "both towers down must drop the ward";
    raider->attack(wizard);
    EXPECT_LT(wizard->stats()->hitpoints(), warded_hp)
        << "the same blow lands once the ward is down";

    // The throne falls: the dream-won fanfare fires via the per-entity
    // death hook.
    wizard->stats()->set_hitpoints(0);
    wizard->set_dead(1);
    wizard->death();
    world.tick();

    bool dream_won = false;
    for (const og::sim::Event& ev : fx.events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find("Dream won") != std::string::npos)
            dream_won = true;
    EXPECT_TRUE(dream_won) << "the wizard's death narrates the win";

    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << err.where << ": " << err.message;
}

// Real play seeds the authoritative world from a level-START keyframe
// (the transport-shadow installs: reset_level_progress, apply_snapshot,
// then the first tick). apply_snapshot used to claim the level-change
// latch even for a tick-0 seed, so isle.lua's on_load never ran in the
// SDL flow and the Sea Wizard spawned unwarded — while the unlatched
// on_tick / on_entity_death hooks still fired, masking the breakage.
TEST_F(ImaginationsCampaignTest, ward_survives_the_level_start_snapshot_seed)
{
    LoadedImaginationsLevel fx(1);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const og::sim::WorldSnapshot seed =
        og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(0u, seed.level_tick_count) << "a level-start keyframe";
    world.reset_level_progress();
    ASSERT_TRUE(og::sim::apply_snapshot(world, seed));
    world.tick();

    walker* wizard = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            ob->team_num() == 1 && ob->family() == FAMILY_MAGE)
            wizard = ob;
    }
    ASSERT_NE(nullptr, wizard);
    EXPECT_TRUE(wizard->stats()->query_bit_flags(BIT_INVINCIBLE))
        << "a level-start seed must leave on_load armed";

    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << err.where << ": " << err.message;
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
        for (const std::string& err : og::mapgen::audit_stairs(world))
            ADD_FAILURE() << err;
        for (const std::string& err : og::mapgen::audit_fall_lines(world))
            ADD_FAILURE() << err;
        for (const std::string& err :
             og::mapgen::audit_generator_spawn_exits(world))
            ADD_FAILURE() << err;
    }
}

// ---------------------------------------------------------------------------
// Scen 2, The Castle of Gold: the dream script's beats (gold_castle.lua).
// ---------------------------------------------------------------------------
constexpr int kCastleLevel = 2;
constexpr int kGateRow = 43;           // the three gate doors, x 30..32
constexpr std::uint32_t kShoreKeys = 0x1FFEu; // key bits 1..12

walker* find_treasure(GameWorld& world, int family, int tx, int ty)
{
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            !ob->dead() && ob->family() == family &&
            ob->xpos() == tx * GRID_SIZE && ob->ypos() == ty * GRID_SIZE)
            return ob;
    }
    return nullptr;
}

int live_gate_doors(GameWorld& world)
{
    int doors = 0;
    for (const auto& uptr : world.weaplist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->family() == FAMILY_DOOR && !ob->dead() &&
            ob->ypos() == kGateRow * GRID_SIZE)
            ++doors;
    }
    return doors;
}

int named_allies(GameWorld& world, const char* name)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->family() == FAMILY_GHOST &&
            ob->team_num() == 0 && ob->stats()->name == name)
            ++count;
    }
    return count;
}

std::vector<walker*> live_faeries(GameWorld& world, bool awake_only)
{
    std::vector<walker*> out;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->family() == FAMILY_FAERIE &&
            ob->team_num() == 1 && (!awake_only || !ob->dormant()))
            out.push_back(ob);
    }
    return out;
}

// A crew member driving the keeper's role: player-controlled (the beat
// keys on control or a saved character, never on a plain team-0 ally).
walker* spawn_keeper(GameWorld& world)
{
    walker* k = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (k == nullptr)
        return nullptr;
    k->set_team_num(0);
    k->set_real_team_num(0);
    k->set_act_type(ACT_CONTROL);
    return k;
}

bool saw_notification(og::sim::SimEventLog& events, const char* needle)
{
    bool seen = false;
    for (const og::sim::Event& ev : events.drain())
        if (ev.kind == og::sim::EventKind::Notification &&
            ev.text.find(needle) != std::string::npos)
            seen = true;
    return seen;
}

void expect_no_script_errors(GameWorld& world)
{
    for (const auto& err : world.scripts().host().errors())
        ADD_FAILURE() << err.where << ": " << err.message;
}

constexpr std::pair<int, int> kCastleBars[] = {
    {22, 19}, {25, 22}, {38, 19}, {41, 22},
    {22, 38}, {25, 35}, {38, 38}, {41, 35}};

TEST_F(ImaginationsCampaignTest, castle_rooms_lie_on_inert_floors)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    const PixieData& g = fx.world().grid;
    auto at = [&](int x, int y) {
        return g.data[static_cast<std::size_t>(x + y * g.w)];
    };
    // Every strongroom and the counting house floored wall to wall in
    // purple carpet: no autotiled grass fringe inside a room.
    for (const auto& room : {std::pair{20, 17}, {36, 17}, {20, 33}, {36, 33}})
        for (int y = room.second + 1; y <= room.second + 6; ++y)
            for (int x = room.first + 1; x <= room.first + 6; ++x)
                EXPECT_EQ(PIX_CARPET_M, at(x, y))
                    << "strongroom floor at " << x << "," << y;
    for (int y = 9; y <= 12; ++y)
        for (int x = 55; x <= 59; ++x)
            EXPECT_EQ(PIX_CARPET_M, at(x, y))
                << "counting house floor at " << x << "," << y;
    // The field camp (southwest) in red-brown brick.
    for (int y = 48; y <= 53; ++y)
        for (int x = 11; x <= 16; ++x)
            EXPECT_EQ(PIX_FLOOR1, at(x, y))
                << "field camp floor at " << x << "," << y;
    // The pad square's pool rim: water (not standable) west, east and north
    // but under the counting house's mouth; the south side open ground.
    auto stand = [&](int x, int y) {
        return og::mapgen::ground_cell_standable(at(x, y));
    };
    for (int y = 14; y <= 18; ++y)
    {
        EXPECT_FALSE(stand(55, y)) << "west rim at y " << y;
        EXPECT_FALSE(stand(61, y)) << "east rim at y " << y;
    }
    for (int x : {59, 60})
        EXPECT_FALSE(stand(x, 14)) << "north rim at x " << x;
    for (int x = 56; x <= 58; ++x)
        EXPECT_TRUE(stand(x, 14)) << "the house mouth at x " << x;
    for (int x = 56; x <= 60; ++x)
    {
        EXPECT_TRUE(stand(x, 15)) << "the pad square at x " << x;
        EXPECT_TRUE(stand(x, 19)) << "the open south at x " << x;
    }
    // The gate tunnel behind the three gate doors (row 43) is purple, like
    // every door in the dream; the doors' own row stays stone.
    for (int y = 41; y <= 42; ++y)
        for (int x = 30; x <= 32; ++x)
            EXPECT_EQ(PIX_CARPET_M, at(x, y))
                << "the gate tunnel at " << x << "," << y;
    for (int x = 30; x <= 32; ++x)
        EXPECT_NE(PIX_CARPET_M, at(x, 43)) << "the gate door row at " << x;
}

// The autotiler's cracked wall base (one in ten) has a black crack that
// reads at 1x as a dark figure standing in the wall: the castles' walls
// are mended whole, every base tile plain, and the walls keep their torches.
TEST_F(ImaginationsCampaignTest, castle_walls_stand_whole)
{
    for (int level : {kCastleLevel, 3})
    {
        LoadedImaginationsLevel fx(level);
        ASSERT_TRUE(fx.loaded) << "level " << level;
        const PixieData& g = fx.world().grid;
        int bases = 0;
        for (int y = 0; y < g.h; ++y)
            for (int x = 0; x < g.w; ++x)
            {
                const unsigned char t =
                    g.data[static_cast<std::size_t>(x + y * g.w)];
                EXPECT_NE(PIX_WALLSIDE_CRACK_C1, t)
                    << "a cracked wall base at " << x << "," << y
                    << " on level " << level;
                if (t == PIX_WALLSIDE1)
                    ++bases;
            }
        EXPECT_GT(bases, 20) << "level " << level << " has its wall bases";
        const PixieData& dec = fx.world().decor_for_floor(0);
        ASSERT_EQ(g.w, dec.w);
        int torches = 0;
        for (std::size_t c = 0;
             c < static_cast<std::size_t>(dec.w) * dec.h; ++c)
            if (dec.data[c] == DECOR_TORCH1)
                ++torches;
        EXPECT_GE(torches, 10) << "level " << level << " keeps its torches";
    }
}

TEST_F(ImaginationsCampaignTest, castle_keys_announce_the_crew_not_ghosts)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    (void)fx.events.drain();
    // An allied ghost sweeping the shore takes the key quietly but still
    // holds it, so no faerie can carry that key to the gate.
    walker* ghost = world.add_ob(Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, ghost);
    ghost->set_team_num(0);
    ghost->set_real_team_num(0);
    walker* key1 = find_treasure(world, FAMILY_KEY, 12, 4);
    ASSERT_NE(nullptr, key1);
    ASSERT_EQ(1, key1->stats()->level());
    key1->eat_me(ghost);
    EXPECT_EQ(1u << 1, ghost->keys() & kShoreKeys);
    EXPECT_FALSE(saw_notification(fx.events, "picks up key"))
        << "no key broadcast for an allied ghost";
    // One of the crew picking up a key is still announced.
    walker* keeper = spawn_keeper(world);
    ASSERT_NE(nullptr, keeper);
    walker* key2 = find_treasure(world, FAMILY_KEY, 50, 4);
    ASSERT_NE(nullptr, key2);
    key2->eat_me(keeper);
    EXPECT_EQ(1u << 2, keeper->keys() & kShoreKeys);
    EXPECT_TRUE(saw_notification(fx.events, "picks up key 2"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, castle_gold_wakes_six_ghosts_then_seven)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick(); // on_load
    (void)fx.events.drain();
    walker* keeper = spawn_keeper(world);
    ASSERT_NE(nullptr, keeper);

    // An allied ghost (team 0, no saved character, not driven) floats over
    // a castle bar and leaves it: ghosts never wake more ghosts.
    walker* drifter = world.add_ob(Order::Living, FAMILY_GHOST);
    ASSERT_NE(nullptr, drifter);
    drifter->set_team_num(0);
    drifter->set_real_team_num(0);
    walker* first = find_treasure(world, FAMILY_GOLD_BAR, 22, 19);
    ASSERT_NE(nullptr, first);
    first->eat_me(drifter);
    EXPECT_FALSE(first->dead()) << "an allied ghost must not take castle gold";
    EXPECT_EQ(0, named_allies(world, "Gold Ghost"));

    const std::uint32_t score0 = world.m_score[0];
    first->eat_me(keeper);
    EXPECT_TRUE(first->dead()) << "the keeper takes the bar";
    EXPECT_GT(world.m_score[0], score0) << "castle gold still banks";
    EXPECT_EQ(6, named_allies(world, "Gold Ghost"));
    EXPECT_TRUE(saw_notification(fx.events, "7 gold left"));

    // The ghosts are born at the Ghost Tent (pad at tile 52,33), on our side.
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->family() != FAMILY_GHOST || ob->dead() ||
            ob == drifter)
            continue;
        EXPECT_EQ(0, ob->team_num());
        EXPECT_LE(std::abs(ob->xpos() / GRID_SIZE - 52), 2);
        EXPECT_LE(std::abs(ob->ypos() / GRID_SIZE - 33), 2);
    }

    for (std::size_t i = 1; i + 1 < std::size(kCastleBars); ++i)
    {
        walker* bar = find_treasure(world, FAMILY_GOLD_BAR,
                                    kCastleBars[i].first,
                                    kCastleBars[i].second);
        ASSERT_NE(nullptr, bar) << "castle bar " << i;
        bar->eat_me(keeper);
    }
    EXPECT_EQ(6 * 7, named_allies(world, "Gold Ghost"));
    EXPECT_EQ(0, named_allies(world, "Crown Ghost"));
    walker* last = find_treasure(world, FAMILY_GOLD_BAR, kCastleBars[7].first,
                                 kCastleBars[7].second);
    ASSERT_NE(nullptr, last);
    (void)fx.events.drain();
    last->eat_me(keeper);
    EXPECT_EQ(7, named_allies(world, "Crown Ghost")) << "the last bar: seven";
    EXPECT_TRUE(saw_notification(fx.events, "SEVEN ghosts"));

    // The counting house bar (outside the castle) banks the classic way.
    walker* house = find_treasure(world, FAMILY_GOLD_BAR, 57, 10);
    ASSERT_NE(nullptr, house);
    house->eat_me(keeper);
    EXPECT_TRUE(house->dead());
    EXPECT_EQ(7, named_allies(world, "Crown Ghost"));
    EXPECT_EQ(42, named_allies(world, "Gold Ghost"));
    expect_no_script_errors(world);
}

// The strongrooms read full of treasure: four silver bars heaped round
// each room's gold. Silver banks the classic way and wakes no ghosts.
TEST_F(ImaginationsCampaignTest, castle_strongroom_silver_banks_without_ghosts)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick(); // on_load
    (void)fx.events.drain();
    // Strongroom interiors (6x6) by their top-left interior tile.
    constexpr std::pair<int, int> kRooms[] = {
        {21, 18}, {37, 18}, {21, 34}, {37, 34}};
    for (const auto& room : kRooms)
    {
        int silver = 0;
        for (const auto& uptr : world.fxlist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->family() != FAMILY_SILVER_BAR ||
                ob->query_order() != Order::Treasure)
                continue;
            const int tx = ob->xpos() / GRID_SIZE;
            const int ty = ob->ypos() / GRID_SIZE;
            if (tx >= room.first && tx <= room.first + 5 &&
                ty >= room.second && ty <= room.second + 5)
                ++silver;
        }
        EXPECT_EQ(4, silver) << "strongroom at " << room.first << ","
                             << room.second;
    }
    walker* keeper = spawn_keeper(world);
    ASSERT_NE(nullptr, keeper);
    walker* silver = find_treasure(world, FAMILY_SILVER_BAR, 24, 18);
    ASSERT_NE(nullptr, silver);
    const std::uint32_t score0 = world.m_score[0];
    silver->eat_me(keeper);
    EXPECT_TRUE(silver->dead()) << "the keeper takes the silver";
    EXPECT_GT(world.m_score[0], score0) << "silver banks";
    EXPECT_EQ(0, named_allies(world, "Gold Ghost"));
    EXPECT_EQ(0, named_allies(world, "Crown Ghost"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, castle_ghosts_respect_the_living_cap)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* keeper = spawn_keeper(world);
    ASSERT_NE(nullptr, keeper);
    walker* bar = find_treasure(world, FAMILY_GOLD_BAR, 22, 19);
    ASSERT_NE(nullptr, bar);
    // Three below the script's 110-living ceiling: three ghosts, not six.
    world.living_count = 107;
    bar->eat_me(keeper);
    EXPECT_TRUE(bar->dead());
    EXPECT_EQ(3, named_allies(world, "Gold Ghost"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, castle_gate_falls_to_twelve_faerie_keys)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    ASSERT_EQ(3, live_gate_doors(world));
    std::vector<walker*> faeries = live_faeries(world, /*awake_only=*/true);
    ASSERT_GE(faeries.size(), 2u);

    // Eleven keys between the faeries: the tick safety net leaves the gate.
    faeries[0]->set_keys(kShoreKeys & ~(1u << 12));
    while (world.level_tick_count() < 201)
        world.tick();
    EXPECT_EQ(3, live_gate_doors(world)) << "eleven keys open nothing";

    // The twelfth on a second bearer, then that bearer falls before the
    // next beat: its keys leave the count and the gate holds.
    faeries[1]->set_keys(1u << 12);
    faeries[1]->stats()->set_hitpoints(0);
    faeries[1]->set_dead(1);
    faeries[1]->death();
    while (world.level_tick_count() < 301)
        world.tick();
    EXPECT_EQ(3, live_gate_doors(world)) << "a dead bearer's key is lost";

    // A living faerie picks up key 12 on the shore (the core:key override):
    // all twelve are carried and the gate falls on the spot.
    walker* key12 = find_treasure(world, FAMILY_KEY, 59, 20);
    ASSERT_NE(nullptr, key12);
    ASSERT_EQ(12, key12->stats()->level());
    (void)fx.events.drain();
    key12->eat_me(faeries[0]);
    EXPECT_EQ(kShoreKeys, faeries[0]->keys() & kShoreKeys);
    EXPECT_EQ(0, live_gate_doors(world)) << "twelve keys drop the gate";
    EXPECT_TRUE(saw_notification(fx.events, "The gate falls"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, castle_gate_safety_net_drops_on_the_beat)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    std::vector<walker*> faeries = live_faeries(world, true);
    ASSERT_FALSE(faeries.empty());
    // Lift the shore keys off the map: any faerie touching one runs the
    // count on the spot (castle_gate_falls_to_twelve_faerie_keys), and
    // this test is about the tick net alone.
    int lifted = 0;
    for (const auto& uptr : world.fxlist)
        if (walker* ob = uptr.get(); ob != nullptr &&
            ob->query_order() == Order::Treasure && !ob->dead() &&
            ob->family() == FAMILY_KEY &&
            ((kShoreKeys >> ob->stats()->level()) & 1u) != 0)
        {
            ob->set_dead(1);
            ++lifted;
        }
    ASSERT_EQ(12, lifted);
    faeries[0]->set_keys(kShoreKeys);
    while (world.level_tick_count() < 99)
        world.tick();
    EXPECT_EQ(3, live_gate_doors(world)) << "the net runs on the 100 beat";
    while (world.level_tick_count() < 102)
        world.tick();
    EXPECT_EQ(0, live_gate_doors(world));
    // The 600-tick status line reads the war: the gate is down and
    // faeries remain, so the ghosts are holding the hall.
    (void)fx.events.drain();
    while (world.level_tick_count() < 602)
        world.tick();
    EXPECT_TRUE(saw_notification(fx.events, "The gate is down!"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, castle_deaths_narrate_bearers_and_the_queen)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    (void)fx.events.drain();
    auto fell = [&](walker* ob) {
        ob->stats()->set_hitpoints(0);
        ob->set_dead(1);
        ob->death();
        world.tick();
    };

    // A plain faerie dies quietly; a key-bearer's keys are lost aloud.
    std::vector<walker*> faeries = live_faeries(world, true);
    ASSERT_GE(faeries.size(), 2u);
    fell(faeries[0]);
    EXPECT_FALSE(saw_notification(fx.events, "key-bearer"));
    faeries[1]->set_keys(1u << 5);
    fell(faeries[1]);
    EXPECT_TRUE(saw_notification(fx.events, "A key-bearer falls!"));

    walker* queen = nullptr;
    for (walker* f : live_faeries(world, false))
        if (f->stats()->name == "Fae Queen")
            queen = f;
    ASSERT_NE(nullptr, queen) << "the finale wave carries the Fae Queen";
    fell(queen);
    EXPECT_TRUE(saw_notification(fx.events, "Dream won."));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, nobody_teleports_into_the_castle)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    auto inside = [](const walker* ob) {
        const int tx = ob->xpos() / GRID_SIZE;
        const int ty = ob->ypos() / GRID_SIZE;
        return tx >= 20 && tx <= 43 && ty >= 17 && ty <= 42;
    };

    // A faerie that blinks into the court is thrown out onto the causeway.
    std::vector<walker*> faeries = live_faeries(world, true);
    ASSERT_FALSE(faeries.empty());
    walker* blinker = faeries[0];
    blinker->setxy(static_cast<short>(31 * GRID_SIZE),
                   static_cast<short>(32 * GRID_SIZE));
    ASSERT_TRUE(inside(blinker));
    world.tick();
    EXPECT_FALSE(inside(blinker)) << "the castle throws a faerie out";
    EXPECT_GE(blinker->ypos() / GRID_SIZE, 44) << "onto the causeway";

    // Ours: walking in is fine; landing a teleport inside is not.
    walker* walker_in = spawn_keeper(world);
    ASSERT_NE(nullptr, walker_in);
    walker_in->set_act_type(ACT_SIT);
    walker_in->setxy(static_cast<short>(26 * GRID_SIZE),
                     static_cast<short>(29 * GRID_SIZE));
    walker* mage = world.add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_NE(nullptr, mage);
    mage->set_team_num(0);
    mage->set_real_team_num(0);
    mage->set_act_type(ACT_SIT);
    mage->setxy(static_cast<short>(36 * GRID_SIZE),
                static_cast<short>(29 * GRID_SIZE));
    mage->set_ani_type(static_cast<char>(ANI_TELE_IN));
    world.tick();
    EXPECT_TRUE(inside(walker_in)) << "the crew walks its own castle";
    EXPECT_FALSE(inside(mage)) << "no spell-doors into the castle";

    // Once the gate has fallen, the castle stops throwing anyone out.
    faeries = live_faeries(world, true);
    ASSERT_FALSE(faeries.empty());
    faeries[0]->set_keys(kShoreKeys);
    while (live_gate_doors(world) > 0 && world.level_tick_count() < 400)
        world.tick();
    ASSERT_EQ(0, live_gate_doors(world));
    faeries[0]->setxy(static_cast<short>(31 * GRID_SIZE),
                      static_cast<short>(32 * GRID_SIZE));
    world.tick();
    EXPECT_TRUE(inside(faeries[0])) << "the gate is down: the ghosts hold";
    expect_no_script_errors(world);
}

// The gold_bar override is campaign-wide while the pack is mounted: on the
// isle, gold banks exactly the classic way and wakes nothing.
TEST_F(ImaginationsCampaignTest, isle_gold_still_banks_the_classic_way)
{
    LoadedImaginationsLevel fx(1);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* keeper = spawn_keeper(world);
    ASSERT_NE(nullptr, keeper);
    walker* bar = find_treasure(world, FAMILY_GOLD_BAR, 28, 28);
    ASSERT_NE(nullptr, bar);
    const std::uint32_t score0 = world.m_score[0];
    bar->eat_me(keeper);
    EXPECT_TRUE(bar->dead());
    EXPECT_EQ(score0 + 200u * static_cast<std::uint32_t>(bar->stats()->level()),
              world.m_score[0]);
    EXPECT_EQ(0, named_allies(world, "Gold Ghost"));
    // A hostile walker never banks.
    walker* other = find_treasure(world, FAMILY_GOLD_BAR, 35, 35);
    ASSERT_NE(nullptr, other);
    walker* foe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe);
    foe->set_team_num(1);
    foe->set_real_team_num(1);
    other->eat_me(foe);
    EXPECT_FALSE(other->dead());
    expect_no_script_errors(world);
}

// The Ghost Tent is a real tent of ours on the island, one row north of
// its pad. Generators always fire, but on this level the campaign's
// generator dispatcher makes our tent's own spawns stillborn: ghosts are
// bought with gold. A stillborn spawn never goes through death() (no
// corpse, no death sound, no death hooks) and is gone a tick later. The
// same tent turned to the faeries' side spawns its skeletons, so the quiet
// is the rule, not a tent that never fires.
TEST_F(ImaginationsCampaignTest, castle_ghost_tent_spawns_are_stillborn)
{
    auto find_tent = [](GameWorld& world) -> walker* {
        for (const auto& uptr : world.oblist)
            if (uptr && uptr->query_order() == Order::Generator &&
                uptr->family() == FAMILY_TENT)
                return uptr.get();
        return nullptr;
    };
    auto live_skeletons = [](GameWorld& world) {
        int n = 0;
        for (const auto& uptr : world.oblist)
            if (uptr && uptr->query_order() == Order::Living &&
                uptr->family() == FAMILY_SKELETON && !uptr->dead())
                ++n;
        return n;
    };
    auto spawns_over = [&](int team, int ticks) {
        LoadedImaginationsLevel fx(kCastleLevel);
        EXPECT_TRUE(fx.loaded);
        if (!fx.loaded)
            return -1;
        GameWorld& world = fx.world();
        walker* tent = find_tent(world);
        EXPECT_NE(nullptr, tent);
        if (tent == nullptr)
            return -1;
        tent->set_team_num(static_cast<unsigned char>(team));
        tent->set_real_team_num(static_cast<unsigned char>(team));
        tent->stats()->set_max_hitpoints(1.0e6f); // it outlives the span
        tent->stats()->set_hitpoints(1.0e6f);
        int seen = 0;
        for (int t = 0; t < ticks; ++t)
        {
            world.tick();
            seen += live_skeletons(world);
        }
        expect_no_script_errors(world);
        return seen;
    };

    {
        LoadedImaginationsLevel fx(kCastleLevel);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        world.tick(); // on_load
        walker* tent = find_tent(world);
        ASSERT_NE(nullptr, tent);
        EXPECT_EQ(0, tent->team_num()) << "the Ghost Tent is ours";
        EXPECT_EQ(52, tent->xpos() / GRID_SIZE);
        EXPECT_EQ(31, tent->ypos() / GRID_SIZE);
        auto death_fx = [&]() {
            int n = 0;
            for (const auto& uptr : world.fxlist)
                if (uptr && uptr->family() == FAMILY_EXPLOSION)
                    ++n;
            return n;
        };
        const int fx0 = death_fx();
        tent->set_lastx(0.0f); // down, onto the island by the pad
        tent->set_lasty(1.0f);
        walker* spawn = tent->fire();
        ASSERT_NE(nullptr, spawn) << "the tent fires";
        EXPECT_EQ(FAMILY_SKELETON, spawn->family());
        EXPECT_TRUE(spawn->dead()) << "stillborn";
        EXPECT_FALSE(spawn->death_called()) << "no death, so no corpse";
        const std::uint32_t id = spawn->entity_id();
        world.tick();
        EXPECT_EQ(nullptr, world.find_by_id(id)) << "gone a tick later";
        EXPECT_EQ(fx0, death_fx()) << "no death FX at the tent";
        EXPECT_EQ(0, live_skeletons(world));
        expect_no_script_errors(world);
    }
    EXPECT_EQ(0, spawns_over(0, 3000)) << "our tent's own spawns never live";
    EXPECT_GT(spawns_over(1, 3000), 0)
        << "a tent of theirs spawns in the same span";
}

TEST_F(ImaginationsCampaignTest, castle_on_load_survives_the_level_start_seed)
{
    LoadedImaginationsLevel fx(kCastleLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const og::sim::WorldSnapshot seed =
        og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(0u, seed.level_tick_count);
    world.reset_level_progress();
    ASSERT_TRUE(og::sim::apply_snapshot(world, seed));
    world.tick();
    EXPECT_TRUE(saw_notification(fx.events, "The gate knows us"))
        << "a level-start seed must leave on_load armed";
    expect_no_script_errors(world);
}


// ---------------------------------------------------------------------------
// Scen 3, The Asteroid Castle: the asteroid's shape and the dream script's
// beats (asteroid.lua).
// ---------------------------------------------------------------------------
constexpr int kAsteroidLevel = 3;

walker* find_big_one(GameWorld& world)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->family() == FAMILY_GOLEM &&
            ob->team_num() == 1)
            return ob;
    }
    return nullptr;
}

// A passive, non-regenerating living parked at a tile on floor 0.
walker* park_living(GameWorld& world, int team, int tx, int ty, float hp)
{
    walker* w = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    w->set_act_type(ACT_SIT);
    w->set_floor(0);
    w->setxy(static_cast<short>(tx * GRID_SIZE),
             static_cast<short>(ty * GRID_SIZE));
    w->stats()->set_max_hitpoints(hp);
    w->stats()->set_hitpoints(hp);
    w->stats()->set_heal_per_round(0.0f);
    return w;
}

TEST_F(ImaginationsCampaignTest, asteroid_floats_in_space_with_a_spiked_castle)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const PixieData& g0 = world.grid_for_floor(0);
    const PixieData& g1 = world.grid_for_floor(1);
    auto at = [](const PixieData& g, int x, int y) {
        return g.data[static_cast<std::size_t>(x + y * g.w)];
    };
    auto jagged = [](unsigned char t) {
        return t >= PIX_JAGGED_GROUND_1 && t <= PIX_JAGGED_GROUND_4;
    };

    // Black space all around the rock.
    for (const auto& c : {std::pair{0, 0}, {63, 0}, {0, 63}, {63, 63},
                          {31, 1}, {1, 31}, {62, 31}, {31, 62}})
        EXPECT_EQ(PIX_VOID1, at(g0, c.first, c.second))
            << c.first << "," << c.second;
    // The spike band hugs the outer wall on every side, broken only by
    // the gate apron.
    for (const auto& c : {std::pair{6, 30}, {7, 20}, {24, 20}, {25, 40},
                          {15, 13}, {20, 14}, {15, 47}, {10, 48}})
        EXPECT_TRUE(jagged(at(g0, c.first, c.second)))
            << c.first << "," << c.second;
    for (int y = 29; y <= 31; ++y)
        EXPECT_EQ(PIX_COBBLE_1, at(g0, 24, y)) << "the apron row " << y;
    // The rainbow round the big door's middle (tile 15, row 46): rings 3..7
    // by rounded distance, rows 49 and down, in the tileset's spectrum:
    // outer red lava, white snow, green grass, blue water, inner purple.
    auto ring_of = [](int x, int y) {
        if (y < 49)
            return 0;
        const int d2x4 = 4 * ((x - 15) * (x - 15) + (y - 46) * (y - 46));
        for (int r = 3; r <= 7; ++r)
            if ((2 * r - 1) * (2 * r - 1) <= d2x4 &&
                d2x4 < (2 * r + 1) * (2 * r + 1))
                return r;
        return 0;
    };
    const unsigned char kBands[8] = {0, 0, 0, PIX_CARPET_M, PIX_WATER1,
                                     PIX_GRASS1, PIX_SNOW1, PIX_LAVA1};
    int rainbow_cells[8] = {};
    for (int y = 47; y <= 55; ++y)
        for (int x = 7; x <= 23; ++x)
        {
            const int r = ring_of(x, y);
            const unsigned char t = at(g0, x, y);
            if (r != 0)
            {
                EXPECT_EQ(kBands[r], t) << "rainbow ring " << r << " at " << x
                                        << "," << y;
                ++rainbow_cells[r];
                continue;
            }
            // Its frame stays plain rock (or the rim's space): no stardust,
            // glitter or dust between or round the bands.
            if (y >= 49)
            {
                EXPECT_TRUE(t == PIX_COBBLE_1 || t == PIX_COBBLE_2 ||
                            t == PIX_COBBLE_3 || t == PIX_COBBLE_4 ||
                            t == PIX_VOID1)
                    << "the rainbow's rock frame at " << x << "," << y;
            }
        }
    for (int r = 3; r <= 7; ++r)
        EXPECT_GE(rainbow_cells[r], 3) << "ring " << r << " is an arc";
    // The arc's legs and its foot: the outer ring reaches row 49 at both
    // ends and row 53 under the door.
    EXPECT_EQ(PIX_LAVA1, at(g0, 9, 49));
    EXPECT_EQ(PIX_LAVA1, at(g0, 21, 49));
    EXPECT_EQ(PIX_LAVA1, at(g0, 15, 53));
    EXPECT_EQ(PIX_CARPET_M, at(g0, 15, 49));
    // An asteroid, not a meadow: no grass anywhere on the rock but the
    // rainbow's green band.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            if (ring_of(x, y) == 5 && x >= 7 && x <= 23)
                continue;
            const unsigned char t = at(g0, x, y);
            const bool grass =
                t == PIX_GRASS1 || t == PIX_GRASS2 || t == PIX_GRASS3 ||
                t == PIX_GRASS4 ||
                (t >= PIX_GRASS_DARK_1 && t <= PIX_GRASS_DARK_4) ||
                (t >= PIX_GRASS_DARK_B1 && t <= PIX_GRASS_DARK_R2) ||
                (t >= PIX_GRASS_LIGHT_1 && t <= PIX_GRASS_LIGHT_LEFT_TOP);
            EXPECT_FALSE(grass) << x << "," << y;
        }
    // The Balloon pit: void under the whole deck but the three up-stairs.
    for (int y = 7; y <= 13; ++y)
        for (int x = 46; x <= 52; ++x)
        {
            const bool stair = y == 7 && x >= 48 && x <= 50;
            EXPECT_EQ(stair ? PIX_ZSTAIR_UP : PIX_VOID1, at(g0, x, y))
                << x << "," << y;
            EXPECT_EQ(stair ? PIX_ZSTAIR_DOWN
                            : (y - 7) % 2 == 0 ? PIX_CARPET_M
                                               : PIX_PAVEMENT1,
                      at(g1, x, y))
                << "the purple-striped deck at " << x << "," << y;
        }
    // The crown of spikes in the north hall: a jagged V on the court.
    for (const auto& v : {std::pair{11, 17}, {20, 17}, {12, 18}, {19, 18},
                          {13, 19}, {18, 19}, {14, 20}, {17, 20}, {15, 21},
                          {16, 21}})
        EXPECT_TRUE(jagged(at(g0, v.first, v.second)))
            << "the crown of spikes at " << v.first << "," << v.second;
    // The striped vestibule: a purple row and an ash row.
    for (int x = 14; x <= 16; ++x)
    {
        EXPECT_EQ(PIX_CARPET_M, at(g0, x, 44)) << "the vestibule at " << x;
        EXPECT_EQ(PIX_ASH1, at(g0, x, 45)) << "the vestibule at " << x;
    }
    // The rest of floor 1 is sky.
    int sky = 0;
    for (std::size_t c = 0; c < static_cast<std::size_t>(g1.w * g1.h); ++c)
        if (g1.data[c] == PIX_AIR)
            ++sky;
    EXPECT_EQ(64 * 64 - 7 * 7, sky);
    // The space wind: enough stardust to force the snow weather (40+).
    int snow = 0;
    for (std::size_t c = 0; c < static_cast<std::size_t>(g0.w * g0.h); ++c)
        if (g0.data[c] == PIX_SNOW1 || g0.data[c] == PIX_SNOW2)
            ++snow;
    EXPECT_GE(snow, 40) << "the stardust drifts force snow";

    // The cast: the Big One on the rock, the Purple Man holding the gate
    // ground, the key under the mast, the big door locked at key 3.
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    EXPECT_EQ(1, golem->floor());
    EXPECT_EQ("Big One", golem->stats()->name);
    walker* ally = nullptr;
    for (const auto& uptr : world.oblist)
        if (uptr && uptr->query_order() == Order::Living &&
            uptr->team_num() == 0)
            ally = uptr.get();
    ASSERT_NE(nullptr, ally);
    EXPECT_EQ("Purple Man", ally->stats()->name);
    EXPECT_EQ(FAMILY_BARBARIAN, ally->family());
    EXPECT_TRUE(ally->guard_hold_post());
    walker* key = nullptr;
    for (const auto& uptr : world.fxlist)
        if (uptr && uptr->family() == FAMILY_KEY)
            key = uptr.get();
    ASSERT_NE(nullptr, key);
    EXPECT_EQ(1, key->floor());
    EXPECT_EQ(3, key->stats()->level());
    int doors = 0;
    for (const auto& uptr : world.weaplist)
        if (uptr && uptr->family() == FAMILY_DOOR &&
            uptr->ypos() == 43 * GRID_SIZE)
        {
            ++doors;
            EXPECT_EQ(3, uptr->stats()->level());
        }
    EXPECT_EQ(3, doors) << "the big door is three wide";
}

// The jagged band is too sharp to cross (jagged ground blocks walkers and
// flyers), so raiders pile up against it; the spikes bite any hostile
// whose centre tile is on or beside a spike, never one of ours.
TEST_F(ImaginationsCampaignTest, asteroid_spikes_bite_bad_guys_only)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* probe = park_living(world, 1, 31, 50, 500.0f);
    ASSERT_NE(nullptr, probe);
    for (const auto& c : {std::pair{6, 25}, {25, 20}, {15, 13}, {15, 48}})
        EXPECT_FALSE(world.query_grid_passable(
            static_cast<float>(c.first * GRID_SIZE),
            static_cast<float>(c.second * GRID_SIZE), probe))
            << "the band at " << c.first << "," << c.second
            << " is too sharp to cross";
    probe->set_dead(1);

    walker* raider = park_living(world, 1, 5, 25, 500.0f);  // against it
    walker* crewman = park_living(world, 0, 5, 36, 500.0f); // against it
    walker* inside = park_living(world, 1, 13, 32, 500.0f); // in the court
    walker* crowned = park_living(world, 1, 15, 22, 500.0f); // at the crown
    walker* back = park_living(world, 1, 4, 30, 500.0f);    // a step back
    walker* dying = park_living(world, 1, 5, 40, 5.0f);     // against it
    ASSERT_NE(nullptr, raider);
    ASSERT_NE(nullptr, crewman);
    ASSERT_NE(nullptr, inside);
    ASSERT_NE(nullptr, crowned);
    ASSERT_NE(nullptr, back);
    ASSERT_NE(nullptr, dying);
    // Any ten consecutive ticks hold exactly one bite.
    for (int t = 0; t < 10; ++t)
        world.tick();
    // Exactly one bite of twelve (a soldier's slow regen can win back a
    // point or two between bites; a second bite could never hide in it).
    auto expect_bites = [&](int bites) {
        const float hp = raider->stats()->hitpoints();
        EXPECT_LE(hp, 500.0f - 12.0f * static_cast<float>(bites) + 2.0f)
            << bites << " bite(s) of twelve, one per ten ticks";
        EXPECT_GT(hp, 500.0f - 12.0f * static_cast<float>(bites + 1))
            << "no more than " << bites << " bite(s)";
    };
    expect_bites(1);
    EXPECT_FLOAT_EQ(500.0f, crewman->stats()->hitpoints())
        << "the spikes know their own";
    EXPECT_FLOAT_EQ(500.0f, inside->stats()->hitpoints())
        << "no spikes in the open court";
    EXPECT_LE(crowned->stats()->hitpoints(), 500.0f - 12.0f + 2.0f)
        << "the crown of spikes bites a raider that runs up to it";
    EXPECT_FLOAT_EQ(500.0f, back->stats()->hitpoints())
        << "two tiles off the band is out of reach";
    EXPECT_TRUE(dying->dead()) << "the spikes kill";
    for (int t = 0; t < 30; ++t)
        world.tick();
    expect_bites(4);
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_big_one_holds_fire_until_the_crew_comes)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    EXPECT_EQ(0, golem->lineofsight()) << "he never throws from the rock";
    EXPECT_EQ(ACT_GUARD, golem->act_type());
    for (const auto& uptr : world.oblist)
    {
        if (uptr && uptr->query_order() == Order::Living &&
            uptr->team_num() == 1)
        {
            EXPECT_LE(uptr->lineofsight(), 7) << "the mist caps sight at 7";
        }
    }

    // Ten tiles off (his centre tile is 48,11): he sleeps on.
    walker* lure = park_living(world, 0, 38, 11, 30000.0f);
    ASSERT_NE(nullptr, lure);
    (void)fx.events.drain();
    for (int t = 0; t < 30; ++t)
        world.tick();
    EXPECT_EQ(ACT_GUARD, golem->act_type()) << "ten tiles is too far";
    EXPECT_EQ(1, golem->floor());
    // Nine tiles: he wakes.
    lure->setxy(static_cast<short>(39 * GRID_SIZE),
                static_cast<short>(11 * GRID_SIZE));
    world.tick();
    EXPECT_EQ(ACT_RANDOM, golem->act_type());
    EXPECT_EQ(0, golem->lineofsight()) << "awake on the rock, still no throw";
    EXPECT_TRUE(saw_notification(fx.events, "The Big One wakes up!"));
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_big_one_walks_off_the_rock_and_falls)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    const float max_hp = golem->stats()->max_hitpoints();
    // A crew member at the pit's west edge, three tiles from the deck.
    ASSERT_NE(nullptr, park_living(world, 0, 44, 12, 30000.0f));
    (void)fx.events.drain();
    int ticks = 0;
    while (golem->stats()->name == "Big One" && ticks < 60)
    {
        world.tick();
        ++ticks;
        if (golem->floor() == 1)
        {
            EXPECT_FALSE(golem->stats()->has_commands() &&
                         golem->stats()->commands.front().commandtype !=
                             COMMAND_WALK)
                << "on the rock he only walks straight";
        }
    }
    ASSERT_EQ("Fallen One", golem->stats()->name)
        << "he walks off the deck and falls within " << ticks << " ticks";
    EXPECT_EQ(0, golem->floor());
    EXPECT_GE(golem->stats()->hitpoints(), 89.0f);
    EXPECT_LE(golem->stats()->hitpoints(), 92.0f) << "hurt: 90 of " << max_hp;
    EXPECT_FLOAT_EQ(24.0f, golem->damage());
    EXPECT_EQ(7, golem->lineofsight()) << "down here he throws again";
    EXPECT_TRUE(saw_notification(fx.events, "The Big One falls off the rock!"));
    // He stays slow on the asteroid.
    for (int t = 0; t < 120; ++t)
    {
        world.tick();
        if (golem->dead())
            break;
        EXPECT_FLOAT_EQ(4.0f, golem->stepsize()) << "slow at tick " << t;
    }
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_big_one_down_the_stairs_is_the_stomper)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    // Arriving on floor 0 with the deck (not sky) above him.
    golem->change_floor(0);
    golem->setxy(static_cast<short>(48 * GRID_SIZE),
                 static_cast<short>(7 * GRID_SIZE));
    (void)fx.events.drain();
    world.tick();
    EXPECT_EQ("Stomper", golem->stats()->name);
    EXPECT_GE(golem->stats()->hitpoints(), 179.0f);
    EXPECT_LE(golem->stats()->hitpoints(), 182.0f);
    EXPECT_FLOAT_EQ(40.0f, golem->damage());
    EXPECT_FLOAT_EQ(6.0f, golem->stepsize());
    EXPECT_EQ(7, golem->lineofsight());
    EXPECT_TRUE(saw_notification(fx.events, "The Big One stomps down!"));
    expect_no_script_errors(world);
}

// The stand-down beat posts every hostile hunter but the Big One on the
// rock (posted up there with sight 0, nothing could ever re-wake him).
TEST_F(ImaginationsCampaignTest, asteroid_stand_down_spares_the_big_one_on_the_rock)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    while (world.level_tick_count() < 48)
        world.tick();
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    walker* orc = nullptr;
    for (const auto& uptr : world.oblist)
        if (uptr && uptr->query_order() == Order::Living &&
            uptr->family() == FAMILY_ORC && !uptr->dormant())
            orc = uptr.get();
    ASSERT_NE(nullptr, orc);
    golem->set_act_type(ACT_RANDOM);
    orc->set_act_type(ACT_RANDOM);
    while (world.level_tick_count() < 52)
        world.tick();
    EXPECT_EQ(1, golem->floor());
    EXPECT_EQ(ACT_RANDOM, golem->act_type()) << "the rock is the script's";
    EXPECT_EQ(ACT_GUARD, orc->act_type()) << "a yard hunter stands down";
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_raider_waves_wake_on_schedule)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    struct Wave
    {
        int delay;
        int size;
        const char* line;
    };
    constexpr Wave kWaves[] = {
        {300, 4, "Raiders ride the space wind!"},
        {800, 3, "Falling stars from the south!"},
        {1400, 4, "Black Dash brings his band!"},
    };
    std::vector<std::vector<walker*>> members(std::size(kWaves));
    int delayed = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob->query_order() != Order::Living ||
            ob->spawn_delay() == 0)
            continue;
        ++delayed;
        for (std::size_t i = 0; i < std::size(kWaves); ++i)
            if (ob->spawn_delay() == kWaves[i].delay)
                members[i].push_back(ob);
    }
    EXPECT_EQ(11, delayed);
    walker* captain = nullptr;
    for (walker* ob : members[2])
        if (ob->family() == FAMILY_BIG_ORC)
            captain = ob;
    ASSERT_NE(nullptr, captain);
    EXPECT_EQ("Black Dash", captain->stats()->name);

    for (std::size_t i = 0; i < std::size(kWaves); ++i)
    {
        SCOPED_TRACE(kWaves[i].line);
        ASSERT_EQ(static_cast<std::size_t>(kWaves[i].size), members[i].size());
        while (world.level_tick_count() <
               static_cast<std::uint32_t>(kWaves[i].delay))
            world.tick();
        for (walker* ob : members[i])
            EXPECT_TRUE(ob->dormant()) << "asleep until the gust";
        (void)fx.events.drain();
        while (world.level_tick_count() <
               static_cast<std::uint32_t>(kWaves[i].delay + 2))
            world.tick();
        for (walker* ob : members[i])
            EXPECT_FALSE(ob->dormant()) << "awake one tick past the delay";
        EXPECT_TRUE(saw_notification(fx.events, kWaves[i].line));
    }
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_last_bad_guy_lands_the_rainbow)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    world.tick();
    (void)fx.events.drain();
    std::vector<walker*> hostiles;
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    for (const auto& uptr : world.oblist)
        if (uptr && uptr->query_order() == Order::Living &&
            uptr->team_num() == 1 && uptr.get() != golem)
            hostiles.push_back(uptr.get());
    ASSERT_EQ(21u, hostiles.size());
    auto fell = [&](walker* ob) {
        ob->stats()->set_hitpoints(0);
        ob->set_dead(1);
        ob->death();
    };
    fell(golem);
    EXPECT_TRUE(saw_notification(fx.events, "The Big One is beaten!"));
    for (std::size_t i = 0; i + 1 < hostiles.size(); ++i)
        fell(hostiles[i]);
    EXPECT_FALSE(saw_notification(fx.events, "Dream won!"))
        << "one bad guy is left (a dormant raider counts)";
    // Rainbow ring (3..7) of a flash, or 0 when it is off the arc.
    auto ring_of = [](const walker& ob) {
        const int tx = ob.xpos() / GRID_SIZE;
        const int ty = ob.ypos() / GRID_SIZE;
        if (ty < 49 || tx < 7 || tx > 23)
            return 0;
        const int d2x4 = 4 * ((tx - 15) * (tx - 15) + (ty - 46) * (ty - 46));
        for (int r = 3; r <= 7; ++r)
            if ((2 * r - 1) * (2 * r - 1) <= d2x4 &&
                d2x4 < (2 * r + 1) * (2 * r + 1))
                return r;
        return 0;
    };
    // Every rainbow flash not seen before, as (tick, ring) per new flash.
    // The flashes alive last tick, keyed by address and tile: a flash is
    // new when no flash at its address stood on its tile a tick ago.
    struct Seen
    {
        const walker* ob;
        int x;
        int y;
    };
    std::vector<Seen> seen;
    struct Spawn
    {
        std::uint32_t tick;
        int ring;
    };
    std::vector<Spawn> spawns;
    std::vector<Seen> now;
    auto collect = [&](std::uint32_t tick) {
        for (const auto& uptr : world.oblist)
        {
            if (!uptr || uptr->query_order() != Order::FX ||
                uptr->family() != FAMILY_FLASH)
                continue;
            const int r = ring_of(*uptr);
            if (r == 0)
                continue;
            const Seen key{uptr.get(), uptr->xpos(), uptr->ypos()};
            now.push_back(key);
            if (std::none_of(seen.begin(), seen.end(), [&](const Seen& o) {
                    return o.ob == key.ob && o.x == key.x && o.y == key.y;
                }))
                spawns.push_back({tick, r});
        }
        seen.swap(now);
        now.clear();
    };
    collect(0);
    ASSERT_TRUE(spawns.empty()) << "no rainbow before the last one falls";
    fell(hostiles.back());
    EXPECT_TRUE(saw_notification(fx.events, "Dream won!"));
    // The show: every ring of the arc sparkles, one ring at a time, outer
    // to inner and round again, for about 180 ticks, so it outlasts the
    // 100-tick win lines; then the arc is quiet.
    collect(0);
    for (std::uint32_t t = 1; t <= 300; ++t)
    {
        world.tick();
        collect(t);
    }
    ASSERT_FALSE(spawns.empty());
    // Group the spawns into sweeps: one tick, one ring.
    std::vector<Spawn> sweeps;
    int lit[8] = {};
    for (const Spawn& sp : spawns)
    {
        ++lit[sp.ring];
        if (sweeps.empty() || sweeps.back().tick != sp.tick)
            sweeps.push_back(sp);
        else
            EXPECT_EQ(sweeps.back().ring, sp.ring)
                << "one ring per sweep, tick " << sp.tick;
    }
    EXPECT_EQ(18u, sweeps.size()) << "eighteen sweeps";
    EXPECT_EQ(0u, sweeps.front().tick) << "the outer ring flashes at once";
    for (std::size_t k = 0; k < sweeps.size(); ++k)
        EXPECT_EQ(7 - static_cast<int>(k % 5), sweeps[k].ring)
            << "sweep " << k << " rolls outer to inner";
    for (int r = 3; r <= 7; ++r)
        EXPECT_GE(lit[r], 3 * 3) << "ring " << r << " sparkles, every pass";
    EXPECT_GE(sweeps.back().tick, 160u) << "the show runs about 3 s";
    EXPECT_LE(sweeps.back().tick, 180u) << "and then it ends";
    expect_no_script_errors(world);
}

TEST_F(ImaginationsCampaignTest, asteroid_on_load_survives_the_level_start_seed)
{
    LoadedImaginationsLevel fx(kAsteroidLevel);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    const og::sim::WorldSnapshot seed =
        og::sim::capture_keyframe_snapshot(world);
    ASSERT_EQ(0u, seed.level_tick_count);
    world.reset_level_progress();
    ASSERT_TRUE(og::sim::apply_snapshot(world, seed));
    world.tick();
    EXPECT_TRUE(saw_notification(fx.events, "The spikes know their own."))
        << "a level-start seed must leave on_load armed";
    for (const auto& uptr : world.oblist)
    {
        if (uptr && uptr->query_order() == Order::Living &&
            uptr->team_num() == 1)
        {
            EXPECT_LE(uptr->lineofsight(), 7);
        }
    }
    walker* golem = find_big_one(world);
    ASSERT_NE(nullptr, golem);
    EXPECT_EQ(0, golem->lineofsight());
    expect_no_script_errors(world);
}

} // namespace
