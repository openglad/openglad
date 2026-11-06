/* Pins for scen 3 of the shipped Imaginations campaign, "The Asteroid
 * Castle" (campaigns/imaginations/, the asteroid.lua dream script):
 * the asteroid floating in space with its spiked castle, the spike band
 * that bites raiders only, the Big One lured off the Balloon Rock and
 * the raider waves. Split from test_imaginations_levels.cpp so each
 * half keeps its own time budget under the instrumented lanes; the
 * shared fixture is imaginations_levels_fixture.h.
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
