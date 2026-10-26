/* Structural pins for the shipped Imaginations campaign
 * (campaigns/imaginations/, composed into builtin/imaginations.glad; the
 * generator is tools/imaginations_mapgen): scen 1 "The Raspberry Isle"
 * (an island in open sea, the crew landing spread around the shore, a
 * moated castle in the middle) and scen 2 "The Castle of Gold" (our own
 * castle in a faerie war, gold that wakes allied ghosts, a gate only
 * twelve faerie-held keys can drop). These pins hold the packaged
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
    int team1_livings;
    int team1_generators;
    int exits;
    int exit_destination;
};

constexpr ShippedLevel kLevels[] = {
    {1, "The Raspberry Isle", 1, 64, 64, 16, 19, 3, 1, 2},
    // Exit loops home to 1 until scen 3 ships (the newest-level rule).
    {2, "The Castle of Gold", 1, 64, 64, 12, 27, 0, 1, 1},
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

} // namespace
