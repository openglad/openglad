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
 * Scen 3's own pins live in test_imaginations_asteroid.cpp (its own unit
 * group, for the instrumented lanes' time budget); the shared fixture is
 * imaginations_levels_fixture.h.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include "imaginations_levels_fixture.h"

namespace {

using namespace imaginations_levels_fixture;

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

constexpr std::pair<int, int> kCastleBars[] = {
    {22, 23}, {24, 23}, {39, 23}, {41, 23},
    {22, 34}, {24, 34}, {39, 34}, {41, 34}};

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
    // The keeper's own castle key (key 13) is named, never counted: the
    // HUD must not say "key 13" while the dream counts twelve.
    (void)fx.events.drain();
    walker* castle_key = find_treasure(world, FAMILY_KEY, 31, 36);
    ASSERT_NE(nullptr, castle_key);
    ASSERT_EQ(13, castle_key->stats()->level());
    castle_key->eat_me(keeper);
    EXPECT_EQ(1u << 13, keeper->keys() & (1u << 13));
    EXPECT_TRUE(saw_notification(fx.events, "takes the castle key."));
    EXPECT_FALSE(saw_notification(fx.events, "picks up key 13"));
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
    walker* first = find_treasure(world, FAMILY_GOLD_BAR, 22, 23);
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

// The strongrooms read full of treasure: two gold bars just inside the
// doors, four silver bars along the back wall. Silver banks the classic
// way and wakes no ghosts.
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
            {
                ++silver;
                // The back wall: the top rooms open down, the bottom
                // rooms open up, so the back row is the far one.
                const int back = room.second < 30 ? room.second
                                                  : room.second + 5;
                EXPECT_EQ(back, ty) << "silver off the back wall at " << tx
                                    << "," << ty;
            }
        }
        EXPECT_EQ(4, silver) << "strongroom at " << room.first << ","
                             << room.second;
    }
    // Each room's two gold bars lie in the row just inside its doors (the
    // door rows are 24 and 33), the first thing seen through them.
    for (const auto& bar : kCastleBars)
        EXPECT_TRUE(bar.second == 23 || bar.second == 34)
            << "castle gold at " << bar.first << "," << bar.second;
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
    walker* bar = find_treasure(world, FAMILY_GOLD_BAR, 22, 23);
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
    // Fire the tent by hand, one shot a tick, down onto the island: the
    // dispatcher sees the same spawn path as the tent's own cadence,
    // without running the whole level for thousands of ticks.
    auto spawns_over = [&](int team, int shots) {
        LoadedImaginationsLevel fx(kCastleLevel);
        EXPECT_TRUE(fx.loaded);
        if (!fx.loaded)
            return -1;
        GameWorld& world = fx.world();
        world.tick(); // on_load
        walker* tent = find_tent(world);
        EXPECT_NE(nullptr, tent);
        if (tent == nullptr)
            return -1;
        tent->set_team_num(static_cast<unsigned char>(team));
        tent->set_real_team_num(static_cast<unsigned char>(team));
        tent->stats()->set_max_hitpoints(1.0e6f); // it outlives the span
        tent->stats()->set_hitpoints(1.0e6f);
        int seen = 0;
        for (int t = 0; t < shots; ++t)
        {
            tent->set_lastx(0.0f);
            tent->set_lasty(1.0f);
            tent->fire();
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
    EXPECT_EQ(0, spawns_over(0, 40)) << "our tent's own spawns never live";
    EXPECT_GT(spawns_over(1, 40), 0)
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

} // namespace
