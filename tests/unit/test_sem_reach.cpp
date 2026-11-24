/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests of og::reach (include/openglad/gameplay/reach.h): what a bot can get at. The maps
// of where each kind of fighter stands with the doors that bar it shut or with every door open, the
// door predicate and the doors-only question beside the door arm of ob_pass_check, the writers that
// make a map stale, the start cell, the approach cells, the shot term and the exits of door_shut.
// Nothing in play asks yet: each test asks the questions directly. Each test is named after the
// rule it asserts, asserts both settings where the setting decides, and names in its comment the
// game line whose breaking turns it red.
//
// Every scene is headless: a field of grass 40 cells a side (GRID_SIZE 16, 640 px), bodies placed
// in pixels, doors of a third side (team 3) unless a row says otherwise.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/key_mask.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/reach.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/weap.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using og::reach::Cell;
using og::reach::CellAnswer;
using og::reach::Exit;
using og::reach::Mode;

constexpr int kW = 40;  // the field, in cells
constexpr unsigned char kDoorTeam = 3;

std::string cell_name(const Cell& c)
{
    return "(" + std::to_string(c.x) + "," + std::to_string(c.y) + "," + std::to_string(c.floor) + ")";
}

class SemReach : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world(1);
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // A field of grass kW cells a side, nobody on it, the setting as given.
    void fresh_world(short fixes)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(kW, kW);
        std::fill_n(w.grid.data.get(), static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    // A grid byte the test paints. Painting a live level is no writer the game has, so the test
    // tells the maps itself (the game's writers are asserted one by one below).
    void tile(int f, int cx, int cy, unsigned char pix)
    {
        PixieData& g = world().grid_for_floor(f);
        g.data[static_cast<std::size_t>(cx + g.w * cy)] = pix;
        world().reach_maps.note_terrain();
    }

    // Two floors; floor 1 a field of `fill`.
    void two_floors(unsigned char fill = PIX_GRASS1)
    {
        GameWorld& w = world();
        w.set_floor_count(2);
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        const std::size_t n = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
        auto* buf = new unsigned char[n];
        std::fill(buf, buf + n, fill);
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw), static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
        w.reach_maps.note_terrain();
    }

    // A decor plane on floor 0, of the grid's size, empty.
    void decor_plane()
    {
        GameWorld& w = world();
        const std::size_t n = static_cast<std::size_t>(w.grid.w) * static_cast<std::size_t>(w.grid.h);
        auto* buf = new unsigned char[n];
        std::fill(buf, buf + n, DECOR_NONE);
        w.decor = PixieData(1, w.grid.w, w.grid.h, buf);
        w.reach_maps.note_terrain();
    }

    // Sizes first, then the position: the body map files a body in every cell its box covers at
    // the moment it is placed.
    walker* place(walker* w, short x, short y, short sx, short sy)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // A level-4 bot (ACT_RANDOM, no seat), its family's box unless (sx, sy) is given.
    walker* living_at(int family, unsigned char team, short x, short y, short sx = -1, short sy = -1, short floor = 0)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_heal_per_round(0.0f);
        w->set_floor(floor);
        return place(w, x, y, sx < 0 ? w->sizex() : sx, sy < 0 ? w->sizey() : sy);
    }

    // A door of `team` and key level `level`, 16 x 16, at (x, y) on `floor`.
    walker* door_at(short x, short y, unsigned char team = kDoorTeam, int level = 1, short floor = 0)
    {
        walker* d = world().add_ob(Order::Weapon, FAMILY_DOOR);
        if (d == nullptr)
            return nullptr;
        d->set_team_num(team);
        d->set_act_type(ACT_SIT);
        d->stats()->set_level(level);
        d->set_floor(floor);
        return place(d, x, y, 16, 16);
    }

    // A wall of `pix` along a row of cells, x0..x1, on floor 0.
    void wall_row(int y, int x0, int x1, unsigned char pix = PIX_H_WALL1)
    {
        for (int x = x0; x <= x1; ++x)
            tile(0, x, y, pix);
    }
    void wall_column(int x, int y0, int y1, unsigned char pix = PIX_H_WALL1)
    {
        for (int y = y0; y <= y1; ++y)
            tile(0, x, y, pix);
    }
    // A pen of walls whose border runs x0..x1, y0..y1, its top middle cell left open for a door.
    Cell pen(int x0, int y0, int x1, int y1)
    {
        const int gap = (x0 + x1) / 2;
        for (int x = x0; x <= x1; ++x) {
            if (x != gap)
                tile(0, x, y0, PIX_H_WALL1);
            tile(0, x, y1, PIX_H_WALL1);
        }
        wall_column(x0, y0, y1);
        wall_column(x1, y0, y1);
        return Cell{gap, y0, 0};
    }

    // The route finder's own answer: does solve_for_point find a route (or stand on the goal)?
    bool route_exists(walker* w, Cell from, Cell to)
    {
        std::vector<PathState> path;
        float cost = 0.0f;
        const auto state = [](Cell c) {
            return reinterpret_cast<PathState>(static_cast<intptr_t>(
                static_cast<intptr_t>(c.floor) * FLOOR_STRIDE + c.y * MAP_WIDTH + c.x));
        };
        ensure_pathfinding_state(*current_game)
            ->solve_for_point(w, static_cast<short>(to.x * GRID_SIZE), static_cast<short>(to.y * GRID_SIZE),
                              state(from), state(to), path, cost);
        return from == to || !path.empty();
    }

    // Everything a door question could change, read at one moment.
    struct Seen
    {
        std::uint32_t rng = 0;
        std::size_t obmap = 0;
        std::size_t events = 0;
        std::uint64_t door_writes = 0;
        std::uint64_t door_count = 0;
        const walker* collide_ob = nullptr;
        std::vector<short> dead;
        bool operator==(const Seen&) const = default;
    };
    Seen seen(const walker* asker, const std::vector<const walker*>& bodies)
    {
        Seen s;
        s.rng = world().rng_.state_;
        s.obmap = world().myobmap->size();
        s.events = tw_->events.size();
        s.door_writes = world().myobmap->door_writes();
        s.door_count = world().reach_maps.door_count();
        s.collide_ob = asker->collide_ob();
        for (const walker* b : bodies)
            s.dead.push_back(b->dead());
        return s;
    }
};

}  // namespace

// og::reach::door_bars (obmap.cpp) against the loud step, row by row of the door table of the body
// map: the box question (GameWorld::door_bars_at) answers "a door bars the box" exactly where the
// loud step is refused by a door that stays. Each row is built twice: the quiet questions on one
// world, the loud step on its twin. A soldier bot (team 1, 16 x 16 at 320,320) steps one pixel
// right; the body stands at 335,320, so the step meets it by one pixel. Both settings.
// Fails when broken: door_bars without its friendly test ("own-side door" bars); without its key test
// ("key held" bars).
TEST_F(SemReach, a_door_bars_a_walker_as_the_loud_step_meets_it_row_by_row)
{
    struct Row
    {
        const char* name;
        bool bars;         // door_bars(mover, body)
        bool box_bars;     // door_bars_at at the step's box, on the mover's floor
        bool loud_passes;  // the loud step's answer
        bool body_dies;    // the loud step opened the door
    };
    const Row rows[] = {
        {"an enemy door, no key", true, true, false, false},
        {"an enemy door, its key held", false, false, false, true},
        {"a door of the mover's side", false, false, true, false},
        {"a dead door", false, false, true, false},
        {"a door on another floor", true, false, true, false},
        {"a no-collide mover, no key", true, true, false, false},
        {"a no-collide mover, the key held", false, false, true, true},
        {"a golem (a living whose family number is a door's)", false, false, false, false},
    };
    for (short fixes : {short{0}, short{1}}) {
        for (const Row& row : rows) {
            SCOPED_TRACE(std::string(row.name) + (fixes ? ", Gameplay Fixes on" : ", Gameplay Fixes off"));
            const std::string name = row.name;
            const auto build = [&](walker*& mover, walker*& body) {
                fresh_world(fixes);
                two_floors();
                mover = living_at(FAMILY_SOLDIER, 1, 320, 320, 16, 16);
                ASSERT_NE(nullptr, mover);
                if (name == "a golem (a living whose family number is a door's)") {
                    body = living_at(FAMILY_GOLEM, kDoorTeam, 335, 320, 16, 16);
                    ASSERT_NE(nullptr, body);
                    return;
                }
                body = door_at(335, 320, name == "a door of the mover's side" ? 1 : kDoorTeam, 2,
                               name == "a door on another floor" ? 1 : 0);
                ASSERT_NE(nullptr, body);
                if (name == "a dead door")
                    body->set_dead(1);
                if (name.find("key held") != std::string::npos)
                    mover->set_keys(static_cast<std::uint32_t>(key_level_mask(2)));
                if (name.find("no-collide") != std::string::npos)
                    mover->stats()->set_bit_flags(BIT_NO_COLLIDE, 1);
            };
            walker* mover = nullptr;
            walker* body = nullptr;
            ASSERT_NO_FATAL_FAILURE(build(mover, body));
            EXPECT_EQ(row.bars, og::reach::door_bars(mover, body)) << "door_bars";
            EXPECT_EQ(row.box_bars, world().door_bars_at(321, 320, mover, mover->floor())) << "door_bars_at";
            ASSERT_NO_FATAL_FAILURE(build(mover, body));
            const bool was_dead = body->dead() != 0;
            const bool passes = world().query_passable(321.0f, 320.0f, mover);
            EXPECT_EQ(row.loud_passes, passes) << "the loud step";
            EXPECT_EQ(row.body_dies, !was_dead && body->dead() != 0) << "the loud step opens the door";
            EXPECT_EQ(row.box_bars, !passes && body->dead() == 0 && body->is_door())
                << "the box question bars exactly where a door that stays refuses the loud step";
        }
    }
}

// obmap.cpp, query_list: the doors-only question asks the piles of the floor it is given, not the
// asker's; and it changes nothing: no draw, no pile, no notice, the door alive, the asker's
// collide_ob kept, no door count moved. A seat's soldier (it would be told "Key N needed!") at
// 320,320 on floor 0, an enemy locked door at 335,320 on floor 1. Both settings.
// Fails when broken: query_list ignoring doors_only->floor (the floor-1 door is not met on floor 1).
TEST_F(SemReach, the_door_question_asks_the_floor_it_is_given_and_changes_nothing)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        two_floors();
        walker* mover = living_at(FAMILY_SOLDIER, 1, 320, 320, 16, 16);
        walker* door = door_at(335, 320, kDoorTeam, 2, 1);
        ASSERT_NE(nullptr, mover);
        ASSERT_NE(nullptr, door);
        mover->set_user(0);
        const Seen before = seen(mover, {door});
        EXPECT_FALSE(world().door_bars_at(321, 320, mover, 0)) << "floor 0: the floor-1 door is not in its piles";
        EXPECT_TRUE(world().door_bars_at(321, 320, mover, 1)) << "floor 1: the door bars the box";
        EXPECT_FALSE(world().door_bars_at(321, 320, mover, 300)) << "a floor past 120 is asked as floor 120";
        EXPECT_FALSE(world().door_bars_at(321, 320, mover, -5)) << "a floor below 0 is asked as floor 0";
        EXPECT_TRUE(before == seen(mover, {door})) << "the questions changed nothing";
        EXPECT_EQ(0u, tw_->events.size()) << "no 'Key N needed!' for the seat";
    }
}

// obmap.cpp, ob_pass_check's doors-only arm: the box question leaves the walker's own foe out (a
// door a script names as a bot's foe does not stop the bot's step question); door_bars and the maps
// have no such exception. Setting on.
// Fails when broken: the doors-only arm without `w != ob->foe()`.
TEST_F(SemReach, the_door_question_leaves_the_walkers_own_foe_out)
{
    walker* bot = living_at(FAMILY_SOLDIER, 1, 320, 320, 16, 16);
    walker* door = door_at(335, 320);
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, door);
    const std::vector<Cell> closed = og::reach::closed_cells(*bot, *door);
    EXPECT_TRUE(world().door_bars_at(321, 320, bot, 0));
    bot->set_foe(door);
    EXPECT_FALSE(world().door_bars_at(321, 320, bot, 0)) << "the bot's own foe is left out of the box question";
    EXPECT_TRUE(og::reach::door_bars(bot, door)) << "door_bars has no foe exception";
    EXPECT_EQ(closed, og::reach::closed_cells(*bot, *door)) << "nor do the maps";
    EXPECT_EQ((std::vector<Cell>{{21, 20, 0}}), closed)
        << "the staging: the door at 335,320 meets a 16 px box only on cell (21,20)";
}

// og::reach's maps against the route finder (PathingMap::adjacent_cost, which walks the same
// passability half, og::reach::for_each_step). Six kinds on a painted two-floor field holding a
// wall, water, tree crowns, a boulder of the decor plane, an air row, two stairs (one landing on
// air) and a door off the grid: (1) every cell's two answers in the open mode are the terrain's
// (terrain_passes), and in the shut mode the same except where a barring door meets the box
// (door_bars_at); (2) "the goal's region is one the start gets to" equals "the solve finds a route
// or stands on its goal" for a sample naming a start on air, inside terrain, beside the door, start
// equal to goal, a stair landing on air; (3) a door's death and a key open the shut route, a
// snapshot applied puts it back; a snapshot's bytes are equal with and without maps built.
// Fails when broken: number_regions without the stair link (P5, P7 differ from the solver); the door
// half without clearing the closed cell (shut answers differ).
TEST_F(SemReach, the_maps_steps_and_regions_are_the_route_finders_for_six_kinds)
{
    const auto paint = [&]() {
        two_floors();
        decor_plane();
        wall_row(8, 5, 15);
        for (int x = 20; x <= 24; ++x)
            for (int y = 5; y <= 8; ++y)
                tile(0, x, y, PIX_WATER1);
        for (int x = 28; x <= 30; ++x)
            for (int y = 3; y <= 5; ++y)
                tile(0, x, y, PIX_TREE_M1);
        world().decor.data[static_cast<std::size_t>(12 + world().decor.w * 15)] = DECOR_BOULDER_1;
        world().reach_maps.note_terrain();
        tile(0, 10, 20, PIX_ZSTAIR_UP);
        tile(1, 10, 20, PIX_ZSTAIR_DOWN);
        tile(0, 30, 30, PIX_ZSTAIR_UP);
        tile(1, 30, 30, PIX_AIR);  // the landing of this stair is air
        for (int x = 0; x < kW; ++x)
            if (x != 18)
                tile(1, x, 25, PIX_AIR);
        tile(1, 5, 5, PIX_AIR);
        for (int x = 33; x <= 37; ++x) {
            tile(0, x, 12, PIX_H_WALL1);
            tile(0, x, 16, PIX_H_WALL1);
        }
        wall_column(33, 12, 16);
        wall_column(37, 12, 16);
    };
    paint();
    walker* door = door_at(178, 150);
    walker* w = living_at(FAMILY_SOLDIER, 1, 48, 48, 16, 16);
    ASSERT_NE(nullptr, door);
    ASSERT_NE(nullptr, w);
    (void)world().myobmap->remove(w);  // its own body prices no cell of a route
    struct Kind
    {
        const char* name;
        std::int32_t flag;
        short size;
    };
    const Kind kinds[] = {{"a plain walker", 0, 16},       {"a flyer", BIT_FLYING, 16},
                          {"a forest-walker", BIT_FORESTWALK, 16}, {"a swimmer", BIT_SWIMMING, 16},
                          {"an ethereal walker", BIT_ETHEREAL, 16}, {"a 32 px walker", 0, 32}};
    const bool multifloor = true;
    struct Pair
    {
        const char* name;
        Cell from;
        Cell to;
    };
    const Pair pairs[] = {
        {"a start on air", {5, 5, 1}, {8, 5, 1}},
        {"a start inside terrain", {10, 8, 0}, {10, 12, 0}},
        {"a start beside the door off the grid", {10, 10, 0}, {14, 12, 0}},
        {"start equal to goal", {3, 3, 0}, {3, 3, 0}},
        {"a stair landing on air", {25, 30, 0}, {30, 30, 1}},
        {"across water", {18, 6, 0}, {26, 6, 0}},
        {"up a stair", {5, 20, 0}, {15, 20, 1}},
        {"across the air row", {5, 23, 1}, {5, 28, 1}},
        {"round the trees", {27, 4, 0}, {32, 4, 0}},
        {"into a walled box", {31, 14, 0}, {35, 14, 0}},
    };
    // The plain walker's answers, worked out from the paint: every pair has a route but the box.
    const bool plain[] = {true, true, true, true, true, true, true, true, true, false};
    for (const Kind& k : kinds) {
        SCOPED_TRACE(k.name);
        w->stats()->set_bit_flags(BIT_FLYING | BIT_FORESTWALK | BIT_SWIMMING | BIT_ETHEREAL, 0);
        if (k.flag != 0)
            w->stats()->set_bit_flags(k.flag, 1);
        w->set_sizex(k.size);
        w->set_sizey(k.size);
        const bool nonflyer = !w->flies();
        int closed = 0;
        for (int f = 0; f < 2; ++f) {
            for (int y = 0; y < kW; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const Cell c{x, y, f};
                    CellAnswer terrain;
                    terrain.step = og::reach::terrain_passes(world(), *w, x * GRID_SIZE, y * GRID_SIZE, f,
                                                             og::reach::StepTest::Step, multifloor, nonflyer);
                    terrain.flank = og::reach::terrain_passes(world(), *w, x * GRID_SIZE, y * GRID_SIZE, f,
                                                              og::reach::StepTest::Flank, multifloor, nonflyer);
                    ASSERT_EQ(terrain, og::reach::cell_answer(*w, c, Mode::Open)) << cell_name(c);
                    const bool door_meets = world().door_bars_at(static_cast<short>(x * GRID_SIZE),
                                                                 static_cast<short>(y * GRID_SIZE), w, f);
                    const CellAnswer shut = og::reach::cell_answer(*w, c, Mode::Shut);
                    const CellAnswer want_shut = (terrain.step && door_meets) ? CellAnswer{} : terrain;
                    ASSERT_EQ(want_shut, shut) << cell_name(c) << " shut";
                    closed += (terrain.step && door_meets) ? 1 : 0;
                }
            }
        }
        EXPECT_EQ(k.size == 16 ? 4 : 6, closed)
            << "the door at 178,150 closes the four grass cells (11..12, 9..10) of a 16 px kind; of a "
               "32 px kind the six of (10..12, 9..10), row 8 under the 32 px box being wall";
        for (std::size_t i = 0; i < std::size(pairs); ++i) {
            const Pair& p = pairs[i];
            SCOPED_TRACE(p.name);
            const bool solver = route_exists(w, p.from, p.to);
            EXPECT_EQ(solver, og::reach::reaches(*w, p.from, p.to, Mode::Open));
            if (k.flag == 0 && k.size == 16) {
                EXPECT_EQ(plain[i], solver) << "the staging";
            }
        }
    }

    // (3) A pen whose one gap holds the door: the shut route opens with the door's death and with
    // its key; a snapshot taken with the door alive, applied after its death, closes it again.
    w->stats()->set_bit_flags(BIT_FLYING | BIT_FORESTWALK | BIT_SWIMMING | BIT_ETHEREAL, 0);
    w->set_sizex(16);
    w->set_sizey(16);
    const Cell gap = pen(2, 30, 6, 34);
    walker* pen_door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    ASSERT_NE(nullptr, pen_door);
    const Cell outside{4, 27, 0};
    const Cell inside{4, 32, 0};
    EXPECT_FALSE(og::reach::reaches(*w, outside, inside, Mode::Shut));
    EXPECT_TRUE(og::reach::reaches(*w, outside, inside, Mode::Open));
    const og::sim::WorldSnapshot alive = og::sim::capture_keyframe_snapshot(world());
    pen_door->set_dead(1);
    EXPECT_TRUE(og::reach::reaches(*w, outside, inside, Mode::Shut)) << "the door's death opens the route";
    ASSERT_TRUE(og::sim::apply_snapshot(world(), alive));
    EXPECT_EQ(0, pen_door->dead()) << "the staging: the snapshot brings the door back";
    EXPECT_FALSE(og::reach::reaches(*w, outside, inside, Mode::Shut)) << "the snapshot applied closes it again";
    w->set_keys(static_cast<std::uint32_t>(key_level_mask(pen_door->stats()->level())));
    EXPECT_TRUE(og::reach::reaches(*w, outside, inside, Mode::Shut)) << "its key opens the route";
    w->set_keys(0);
    EXPECT_FALSE(og::reach::reaches(*w, outside, inside, Mode::Shut));

    // A snapshot's bytes with and without maps built: the same scene twice, the questions asked
    // on the second only.
    std::vector<std::vector<std::uint8_t>> bytes;
    for (bool ask : {false, true}) {
        fresh_world(1);
        paint();
        walker* d2 = door_at(178, 150);
        walker* w2 = living_at(FAMILY_SOLDIER, 1, 48, 48, 16, 16);
        ASSERT_NE(nullptr, d2);
        ASSERT_NE(nullptr, w2);
        if (ask) {
            EXPECT_TRUE(og::reach::reaches(*w2, {3, 3, 0}, {14, 12, 0}, Mode::Shut));
            EXPECT_EQ(2u, world().reach_maps.builds()) << "the staging: a terrain half and a door half built";
        }
        bytes.push_back(og::sim::serialize_snapshot(og::sim::peek_keyframe_snapshot(world())));
    }
    EXPECT_EQ(bytes[0], bytes[1]) << "no map is part of a snapshot";

    // On a level of one floor an air tile answers the two tests apart for a walker that cannot
    // fly: a step may end on it (the air test of a step is for stacked levels), no diagonal passes
    // beside it (the flank's air test is on every level); a flyer passes both.
    fresh_world(1);
    tile(0, 8, 8, PIX_AIR);
    walker* walker1 = living_at(FAMILY_SOLDIER, 1, 48, 48, 16, 16);
    ASSERT_NE(nullptr, walker1);
    EXPECT_EQ((CellAnswer{true, false}), og::reach::cell_answer(*walker1, {8, 8, 0}, Mode::Open));
    EXPECT_EQ((CellAnswer{true, false}), og::reach::cell_answer(*walker1, {8, 8, 0}, Mode::Shut));
    walker1->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_EQ((CellAnswer{true, true}), og::reach::cell_answer(*walker1, {8, 8, 0}, Mode::Open));
}

// The writers of the two counts (reach.h, Maps). Each bumps its count, and a question asked after
// it answers the changed world. The body map's door count (obmap::door_writes): a door filed,
// moved, taken out (also by its dormant, hidden and ignore states, which file and unfile it), the
// doors an external clear dropped; a non-door files and unfiles nothing it counts. The world's
// door count: a door's dead, team and level setters (sim_entity.cpp, statistics). The terrain
// count: rebind_smoothers, set_floor_count, resize_grid, create_new_grid, delete_grid, a snapshot's
// grid. The blast's charred grass bumps nothing: it answers as grass for every kind (the named rule
// in reach.cpp). Setting on.
// Fails when broken: SimEntity::door_state_written without its bump (the dead/team rows); obmap::add
// without its door test (the filing row); GameWorld::grid_replaced without note_terrain.
TEST_F(SemReach, every_writer_of_the_doors_and_the_terrain_bumps_its_count)
{
    walker* bot = living_at(FAMILY_SOLDIER, 1, 100, 100, 16, 16);
    ASSERT_NE(nullptr, bot);
    obmap& om = *world().myobmap;
    og::reach::Maps& maps = world().reach_maps;

    walker* door = door_at(160, 160);
    ASSERT_NE(nullptr, door);
    ASSERT_EQ(1, om.remove(door)) << "the staging: placed, the door is filed";
    std::uint64_t writes = om.door_writes();
    ASSERT_EQ(1, om.add(door, 160, 160));
    EXPECT_EQ(writes + 1, om.door_writes()) << "filed";
    EXPECT_EQ((std::vector<Cell>{{10, 10, 0}}), og::reach::closed_cells(*bot, *door));
    ASSERT_EQ(1, om.remove(door));
    EXPECT_EQ(writes + 2, om.door_writes()) << "taken out";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty()) << "a door filed nowhere bars nothing";
    ASSERT_EQ(1, om.add(door, 160, 160));

    walker* orc = living_at(FAMILY_ORC, 2, 400, 400, 16, 16);
    ASSERT_NE(nullptr, orc);
    writes = om.door_writes();
    ASSERT_EQ(1, om.remove(orc));
    ASSERT_EQ(1, om.add(orc, 400, 400));
    orc->setxy(static_cast<short>(416), static_cast<short>(400));
    EXPECT_EQ(writes, om.door_writes()) << "a living is no door: nothing counted";

    writes = om.door_writes();
    door->setxy(static_cast<short>(192), static_cast<short>(160));
    EXPECT_EQ(writes + 2, om.door_writes()) << "moved: taken out of its cells and filed in new ones";
    EXPECT_EQ((std::vector<Cell>{{12, 10, 0}}), og::reach::closed_cells(*bot, *door)) << "the move is seen";

    writes = om.door_writes();
    door->set_dormant(true);
    EXPECT_EQ(writes + 1, om.door_writes()) << "dormant: taken out";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty()) << "a door filed nowhere bars nothing";
    door->set_dormant(false);
    EXPECT_EQ(writes + 2, om.door_writes()) << "awake: filed again";
    door->set_hidden(true);
    EXPECT_EQ(writes + 3, om.door_writes()) << "hidden: taken out";
    door->set_hidden(false);
    EXPECT_EQ(writes + 4, om.door_writes()) << "revealed: filed again";
    door->set_ignore(1);
    EXPECT_EQ(writes + 4, om.door_writes()) << "ignored: still filed (and still met) until it is placed again";
    EXPECT_EQ(1u, og::reach::closed_cells(*bot, *door).size());
    door->setxy(door->xpos(), door->ypos());
    EXPECT_EQ(writes + 5, om.door_writes()) << "placed while ignored: taken out";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty());
    door->set_ignore(0);
    place(door, door->xpos(), door->ypos(), 16, 16);
    EXPECT_EQ(writes + 8, om.door_writes())
        << "placed again by a real move (a walker set at its own spot is not filed again, obmap::move): "
           "filed beside (one), then out of those cells and into its own (two)";

    std::uint64_t count = maps.door_count();
    orc->set_dead(1);
    orc->set_team_num(4);
    orc->stats()->set_level(9);
    EXPECT_EQ(count, maps.door_count()) << "a living's setters tell nothing";
    door->set_team_num(1);
    EXPECT_EQ(count + 1, maps.door_count()) << "a door's team";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty()) << "of the bot's side now: it bars nothing";
    door->set_team_num(kDoorTeam);
    door->stats()->set_level(5);
    EXPECT_EQ(count + 3, maps.door_count()) << "a door's level";
    bot->set_keys(static_cast<std::uint32_t>(key_level_mask(1)));
    EXPECT_EQ(1u, og::reach::closed_cells(*bot, *door).size()) << "the key of level 1 does not open a level-5 door";
    bot->set_keys(0);
    door->set_dead(1);
    EXPECT_EQ(count + 4, maps.door_count()) << "a door's death";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty()) << "a dead door closes nothing";
    door->set_dead(0);
    EXPECT_EQ(count + 5, maps.door_count());

    writes = om.door_writes();
    om.pos_to_walker.clear();  // the external clear of level teardown (GameWorld::delete_objects)
    om.walker_to_pos.clear();
    EXPECT_EQ(writes + 1, om.door_writes()) << "the external clear dropped the doors it filed";
    EXPECT_TRUE(og::reach::closed_cells(*bot, *door).empty());
    world().delete_objects();

    // The terrain writers.
    bot = living_at(FAMILY_SOLDIER, 1, 100, 100, 16, 16);
    ASSERT_NE(nullptr, bot);
    EXPECT_TRUE(og::reach::cell_answer(*bot, {7, 7, 0}, Mode::Open).step);
    tile(0, 7, 7, PIX_H_WALL1);  // painted, the count bumped (tile tells the maps)
    EXPECT_FALSE(og::reach::cell_answer(*bot, {7, 7, 0}, Mode::Open).step) << "a terrain count bump is seen";
    tile(0, 7, 7, PIX_GRASS1);
    std::uint64_t terrain = maps.terrain_count();
    world().rebind_smoothers();
    EXPECT_EQ(terrain + 1, maps.terrain_count()) << "grids moved in";
    world().set_floor_count(2);
    EXPECT_EQ(terrain + 2, maps.terrain_count()) << "a floor added";
    world().set_floor_count(1);
    EXPECT_EQ(terrain + 3, maps.terrain_count()) << "a floor dropped";
    const og::sim::WorldSnapshot snap = og::sim::capture_keyframe_snapshot(world());
    world().grid.data[static_cast<std::size_t>(7 + world().grid.w * 7)] = PIX_H_WALL1;  // no writer: the snapshot puts it back
    ASSERT_TRUE(og::sim::apply_snapshot(world(), snap));
    EXPECT_EQ(terrain + 4, maps.terrain_count()) << "a snapshot's grid";
    EXPECT_TRUE(og::reach::cell_answer(*bot, {7, 7, 0}, Mode::Open).step) << "the grass the snapshot wrote back stands";
    world().resize_grid(kW, kW);
    EXPECT_EQ(terrain + 5, maps.terrain_count()) << "resized";
    world().create_new_grid();
    EXPECT_EQ(terrain + 6, maps.terrain_count()) << "a new grid";
    world().delete_grid();
    EXPECT_EQ(terrain + 7, maps.terrain_count()) << "the grid freed";

    // The charred grass: no writer, and it answers as grass for every kind.
    fresh_world(1);
    walker* w = living_at(FAMILY_SOLDIER, 1, 300, 300, 16, 16);
    ASSERT_NE(nullptr, w);
    const std::int32_t flags[] = {0, BIT_FLYING, BIT_FORESTWALK, BIT_SWIMMING, BIT_ETHEREAL};
    std::vector<CellAnswer> before;
    for (const std::int32_t f : flags) {
        w->stats()->set_bit_flags(BIT_FLYING | BIT_FORESTWALK | BIT_SWIMMING | BIT_ETHEREAL, 0);
        if (f != 0)
            w->stats()->set_bit_flags(f, 1);
        for (short size : {short{16}, short{32}}) {
            w->set_sizex(size);
            w->set_sizey(size);
            for (int x = 4; x <= 6; ++x)
                before.push_back(og::reach::cell_answer(*w, {x, 4, 0}, Mode::Open));
        }
    }
    terrain = maps.terrain_count();
    for (int x = 4; x <= 6; ++x)
        ASSERT_EQ(PIX_GRASS1_DAMAGED, static_cast<unsigned char>(world().damage_tile(static_cast<short>(x * GRID_SIZE + 3), 4 * GRID_SIZE + 3)));
    EXPECT_EQ(terrain, maps.terrain_count()) << "the blast's charred grass bumps no terrain count";
    std::vector<CellAnswer> after;
    for (const std::int32_t f : flags) {
        w->stats()->set_bit_flags(BIT_FLYING | BIT_FORESTWALK | BIT_SWIMMING | BIT_ETHEREAL, 0);
        if (f != 0)
            w->stats()->set_bit_flags(f, 1);
        for (short size : {short{16}, short{32}}) {
            w->set_sizex(size);
            w->set_sizey(size);
            for (int x = 4; x <= 6; ++x) {
                const bool nonflyer = !w->flies();
                CellAnswer terrain_now;
                terrain_now.step = og::reach::terrain_passes(world(), *w, x * GRID_SIZE, 4 * GRID_SIZE, 0,
                                                             og::reach::StepTest::Step, false, nonflyer);
                terrain_now.flank = og::reach::terrain_passes(world(), *w, x * GRID_SIZE, 4 * GRID_SIZE, 0,
                                                              og::reach::StepTest::Flank, false, nonflyer);
                after.push_back(terrain_now);
            }
        }
    }
    EXPECT_EQ(before, after) << "charred grass answers every kind as the grass did";
    EXPECT_EQ(30u, before.size()) << "five flag sets, two boxes, three cells";
}

// reach.cpp, current(): in the test build, the first question of a tick checks the doors the maps
// were built from against a fresh list of the doors filed in the body map. A door moved by the
// plain position setter (no writer: the body map and the maps are not told) stops the test build at
// the next tick's first question; moved by setxy (the body map's writer) it does not, and the move
// is seen. Setting on.
// Fails when broken: current() without its check (the death does not happen).
TEST_F(SemReach, a_door_changed_behind_the_writers_back_stops_the_test_build)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    walker* bot = living_at(FAMILY_SOLDIER, 1, 100, 100, 16, 16);
    walker* door = door_at(160, 160);
    ASSERT_NE(nullptr, bot);
    ASSERT_NE(nullptr, door);
    ASSERT_EQ(1u, og::reach::closed_cells(*bot, *door).size());
    door->setxy(static_cast<short>(224), static_cast<short>(160));
    world().tick_count_ += 1;
    EXPECT_EQ((std::vector<Cell>{{14, 10, 0}}), og::reach::closed_cells(*bot, *door))
        << "control: a writer's move survives the check and is seen";
    door->set_xpos(static_cast<short>(288));  // behind the writers' back
    world().tick_count_ += 1;
    EXPECT_DEATH((void)og::reach::closed_cells(*bot, *door), "no writer bumped a door count");
}

// living.cpp/walker.cpp: a bot is a living no seat drives (walker::is_bot), and only a bot that is
// not stationary and not a guard at its post is asked (og::reach::asks), only with the setting on.
// Fails when broken: asks() without its ACT_GUARD test; is_bot() answering true for a seat.
TEST_F(SemReach, a_bot_is_a_living_no_seat_drives_and_only_a_moving_bot_is_asked)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* bot = living_at(FAMILY_SOLDIER, 1, 100, 100);
        walker* seat = living_at(FAMILY_SOLDIER, 1, 200, 100);
        walker* tower = living_at(FAMILY_TOWER1, 2, 300, 100);
        walker* guard = living_at(FAMILY_ORC, 2, 400, 100);
        walker* door = door_at(500, 100);
        ASSERT_TRUE(bot && seat && tower && guard && door);
        seat->set_user(0);
        guard->set_act_type(ACT_GUARD);
        EXPECT_TRUE(bot->is_bot());
        EXPECT_FALSE(seat->is_bot());
        EXPECT_TRUE(tower->is_bot()) << "a tower is a bot";
        EXPECT_FALSE(door->is_bot()) << "a door is no living";
        EXPECT_EQ(fixes != 0, og::reach::asks(bot));
        EXPECT_FALSE(og::reach::asks(seat));
        EXPECT_FALSE(og::reach::asks(tower)) << "a tower is never asked";
        EXPECT_FALSE(og::reach::asks(guard)) << "nor a guard at its post";
        guard->set_act_type(ACT_RANDOM);
        EXPECT_EQ(fixes != 0, og::reach::asks(guard)) << "the same orc roaming";
    }
}

// The reach read without the family's weapon hook (walker::unhooked_weapon, walker::weapon_reach)
// equals the line check's reach (the weapon create_weapon builds with the hook, the reach gate of
// fire_check) for the soldier, the cleric and the archmage, in each of the eight facings; a family
// whose weapon the loader cannot make is not judged (the shot term answers yes); a melee-only bot is
// never credited with a shot, the same bot with the flag cleared is (control). Setting on.
// Fails when broken: unhooked_weapon without configure_weapon_profile_base (the reach differs); the
// shot term without its BIT_NO_RANGED test.
TEST_F(SemReach, the_reach_read_without_the_weapon_hook_is_the_line_checks_for_three_families)
{
    constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    for (int family : {FAMILY_SOLDIER, FAMILY_CLERIC, FAMILY_ARCHMAGE}) {
        SCOPED_TRACE("family " + std::to_string(family));
        walker* w = living_at(family, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        for (const auto& d : kStep) {
            w->face_delta(static_cast<short>(d[0]), static_cast<short>(d[1]));
            walker* real = w->create_weapon();
            ASSERT_NE(nullptr, real);
            const std::int32_t line_check_reach = walker::weapon_reach(*real);
            world().remove_ob(real);
            weap test_weapon(walker::WeaponProfileProbeTag{});
            ASSERT_TRUE(w->unhooked_weapon(test_weapon, static_cast<float>(d[0]), static_cast<float>(d[1])));
            EXPECT_EQ(line_check_reach, walker::weapon_reach(test_weapon)) << "facing " << d[0] << "," << d[1];
            // The cleric's glow declares no step (it grows where it stands, packs/core/families/
            // weapon-12-glow.lua): its reach is 0 both ways; the knife and the archmage's bolt fly.
            if (family == FAMILY_CLERIC) {
                EXPECT_EQ(0, line_check_reach);
            } else {
                EXPECT_GT(line_check_reach, 0) << "the staging: a reach to compare";
            }
        }
        world().remove_ob(w);
    }
    walker* orc = living_at(FAMILY_ORC, 1, 320, 320, 16, 16);
    walker* foe = living_at(FAMILY_SOLDIER, 2, 340, 320, 16, 16);
    ASSERT_NE(nullptr, orc);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(0, orc->stats()->query_bit_flags(BIT_NO_RANGED)) << "the staging: the orc is melee-only";
    weap orc_weapon(walker::WeaponProfileProbeTag{});
    ASSERT_TRUE(orc->unhooked_weapon(orc_weapon, 1.0f, 0.0f));
    ASSERT_GE(walker::weapon_reach(orc_weapon), 20) << "the staging: the foe 20 px off is in the orc weapon's reach";
    EXPECT_FALSE(og::reach::shoots_at(*orc, *foe)) << "a melee-only bot is never credited with a shot";
    orc->stats()->set_bit_flags(BIT_NO_RANGED, 0);
    EXPECT_TRUE(og::reach::shoots_at(*orc, *foe)) << "control: the same orc with the flag cleared lands its shot";
    orc->set_current_weapon(100);  // a weapon slot no pack declares
    EXPECT_TRUE(og::reach::shoots_at(*orc, *foe)) << "a weapon the loader cannot make: not judged";
}

// The shot term judges only inside the firing box (walker::in_firing_box, the box act_random fires
// in) and within the weapon's reach (Manhattan): an archer bot facing right at 320,320, its foe
// straight to the right. Just inside both: yes; one pixel past the box (still in reach on a
// diagonal) or past the reach: no. Setting on.
// Fails when broken: in_firing_box answering <= instead of <; the shot term without its reach gate.
TEST_F(SemReach, the_shot_term_judges_only_inside_the_firing_box_and_the_reach)
{
    walker* archer = living_at(FAMILY_ARCHER, 1, 32, 320, 16, 16);
    ASSERT_NE(nullptr, archer);
    archer->set_lineofsight(5);  // a box of 80 px
    weap test_weapon(walker::WeaponProfileProbeTag{});
    ASSERT_TRUE(archer->unhooked_weapon(test_weapon, 1.0f, 0.0f));
    const std::int32_t reach = walker::weapon_reach(test_weapon);
    ASSERT_GT(reach, 79) << "the staging: the reach is longer than the box";
    ASSERT_LT(reach, 560) << "the staging: the reach ends on the field";
    walker* foe = living_at(FAMILY_SOLDIER, 2, static_cast<short>(32 + 79), 320, 16, 16);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(archer->in_firing_box(79, 0));
    EXPECT_FALSE(archer->in_firing_box(80, 0));
    EXPECT_TRUE(archer->in_firing_box(-79, 79));
    EXPECT_FALSE(archer->in_firing_box(-79, -80));
    EXPECT_TRUE(og::reach::shoots_at(*archer, *foe)) << "79 px off: inside the box and the reach";
    foe->setxy(static_cast<short>(32 + 80), static_cast<short>(320));
    EXPECT_FALSE(og::reach::shoots_at(*archer, *foe)) << "80 px off: past the box";
    archer->set_lineofsight(40);  // a box wider than the reach
    foe->setxy(static_cast<short>(32 + reach + 1), static_cast<short>(320));
    EXPECT_FALSE(og::reach::shoots_at(*archer, *foe)) << "one pixel past the reach";
    foe->setxy(static_cast<short>(32 + reach - 16), static_cast<short>(320));
    EXPECT_TRUE(og::reach::shoots_at(*archer, *foe)) << "control: inside the reach";
}

// The judging flight (og::reach::shoots_at): the line check's own flight for a detached test
// weapon. An enemy grown tree on the line is passed without a draw (no clash roll for a detached
// weapon, ob_pass_check's passes_on_the_clash); an enemy door on the line stops it, also when the
// door is the shooter's own foe (line_check_flight's every_door_stops), while the real line check
// keeps its exception for that door. An archer bot facing up at 320,400, its foe straight up. Both
// settings: off, nothing is judged (yes).
// Fails when broken: stopped_by_a_door without `every_door_stops ||` (the foe door is passed);
// passes_on_the_clash without its detached arm (the tree draws a roll).
TEST_F(SemReach, the_judging_flight_passes_a_tree_without_a_draw_and_stops_at_every_door)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* archer = living_at(FAMILY_ARCHER, 1, 320, 400, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 320, 336, 16, 16);
        ASSERT_NE(nullptr, archer);
        ASSERT_NE(nullptr, foe);
        archer->face_delta(0, -1);
        archer->set_lineofsight(10);
        archer->set_foe(foe);
        weap test_weapon(walker::WeaponProfileProbeTag{});
        ASSERT_TRUE(archer->unhooked_weapon(test_weapon, 0.0f, -1.0f));
        ASSERT_GT(walker::weapon_reach(test_weapon), 64) << "the staging: the foe 64 px up is in reach";
        EXPECT_TRUE(og::reach::shoots_at(*archer, *foe)) << "an open line";
        walker* tree = world().add_ob(Order::Weapon, FAMILY_TREE);
        ASSERT_NE(nullptr, tree);
        tree->set_team_num(2);
        place(tree, 320, 368, 16, 16);
        const std::uint32_t rng = world().rng_.state_;
        EXPECT_TRUE(og::reach::shoots_at(*archer, *foe)) << "a grown tree on the line is passed";
        EXPECT_EQ(rng, world().rng_.state_) << "and draws nothing";
        world().remove_ob(tree);
        walker* door = door_at(320, 368);
        ASSERT_NE(nullptr, door);
        EXPECT_EQ(fixes == 0, og::reach::shoots_at(*archer, *foe)) << "an enemy door on the line stops it";
        archer->set_foe(door);
        EXPECT_EQ(fixes == 0, og::reach::shoots_at(*archer, *door)) << "every door stops it, the shooter's foe included";
        EXPECT_EQ(rng, world().rng_.state_) << "no draw at the door";
        if (fixes) {
            world().rng_.state_ = 12345u;
            EXPECT_TRUE(archer->fire_check(0, -32)) << "control: the real line check passes on the door that is its foe";
        }
    }
}

// A door off the 16 px grid closes exactly the cells its box meets (by the body map's own test,
// collide, on a standing cell): a door at 178,150 closes four cells of a 16 px kind, (11..12,
// 9..10), and nine of a 32 px kind, (10..12, 8..10); door_bars_at answers "barred" on exactly those
// cells, and at every pixel of a sweep exactly where collide says the boxes meet; the questions draw
// nothing and change nothing. Setting on.
// Fails when broken: the door half's collide with the door box one pixel narrower (the closed cells
// differ from collide's).
TEST_F(SemReach, a_door_off_the_grid_closes_exactly_the_cells_its_box_meets)
{
    walker* door = door_at(178, 150);
    ASSERT_NE(nullptr, door);
    for (short size : {short{16}, short{32}}) {
        SCOPED_TRACE(size == 16 ? "a 16 px walker" : "a 32 px walker");
        walker* w = living_at(FAMILY_SOLDIER, 1, 400, 400, size, size);
        ASSERT_NE(nullptr, w);
        std::vector<Cell> want;
        for (int y = 0; y < kW; ++y)
            for (int x = 0; x < kW; ++x)
                if (x * GRID_SIZE + size < kW * GRID_SIZE && y * GRID_SIZE + size < kW * GRID_SIZE &&
                    ::collide(static_cast<short>(x * GRID_SIZE), static_cast<short>(y * GRID_SIZE), size, size,
                              door->xpos(), door->ypos(), door->sizex(), door->sizey()))
                    want.push_back(Cell{x, y, 0});
        const std::size_t expect = size == 16 ? 4u : 9u;
        EXPECT_EQ(expect, want.size()) << "the staging, worked out from collide's one-pixel shrink";
        const Seen before = seen(w, {door});
        EXPECT_EQ(want, og::reach::closed_cells(*w, *door));
        for (int y = 5; y <= 13; ++y)
            for (int x = 7; x <= 15; ++x)
                EXPECT_EQ(std::find(want.begin(), want.end(), Cell{x, y, 0}) != want.end(),
                          world().door_bars_at(static_cast<short>(x * GRID_SIZE), static_cast<short>(y * GRID_SIZE), w, 0))
                    << cell_name(Cell{x, y, 0});
        for (int px = 140; px <= 200; ++px)
            EXPECT_EQ(::collide(static_cast<short>(px), 150, size, size, 178, 150, 16, 16) != 0,
                      world().door_bars_at(static_cast<short>(px), 150, w, 0))
                << "x " << px;
        EXPECT_TRUE(before == seen(w, {door})) << "nothing drawn, filed, opened, collided or told";
        world().remove_ob(w);
    }
}

// With the setting off nothing is asked and no map is built: every question answers today's
// ("not judged"), the build counter stays 0, also through a 600-tick battle beside doors. With the
// setting on the same first question builds a terrain half and a door half (2).
// Fails when broken: judged() without its setting test (the counter moves with the setting off).
TEST_F(SemReach, nothing_is_asked_or_built_with_the_setting_off)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        const Cell gap = pen(10, 10, 14, 14);
        walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
        walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(door && orc && foe);
        const og::reach::Maps& maps = world().reach_maps;
        const bool on = fixes != 0;
        EXPECT_EQ(on ? Exit::DoorShut : Exit::NotAsked, og::reach::door_shut_exit(*orc, *foe));
        EXPECT_EQ(on, og::reach::shut_out(*orc));
        EXPECT_EQ(!on, og::reach::walks_to(*orc, *foe, Mode::Shut));
        EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Open));
        EXPECT_EQ(!on, og::reach::gets_at(*orc, *foe));
        EXPECT_EQ((Cell{12, 12, 0}), og::reach::start_cell(*orc));
        EXPECT_EQ(on ? 1u : 0u, og::reach::closed_cells(*orc, *door).size());
        EXPECT_EQ(!on, og::reach::reaches(*orc, {12, 12, 0}, {12, 5, 0}, Mode::Shut));
        EXPECT_EQ(on, og::reach::region_of(*orc, {12, 5, 0}, Mode::Shut) >= 0);
        EXPECT_EQ(on, !og::reach::approach_cells(*orc, *foe, Mode::Open).empty());
        Cell near{};
        EXPECT_EQ(on, og::reach::nearest_approach(*orc, *foe, Mode::Open, near));
        EXPECT_EQ(on ? 2u : 0u, maps.builds()) << "the orc's kind: a terrain half and a door half";
        // A battle beside doors.
        fresh_world(fixes);
        for (int i = 0; i < 4; ++i) {
            ASSERT_NE(nullptr, door_at(static_cast<short>(200 + 16 * i), 300));
            ASSERT_NE(nullptr, living_at(FAMILY_SOLDIER, 1, static_cast<short>(180 + 24 * i), 250));
            ASSERT_NE(nullptr, living_at(FAMILY_ORC, 2, static_cast<short>(180 + 24 * i), 350));
        }
        for (int t = 0; t < 600; ++t)
            world().tick();
        EXPECT_EQ(0u, world().reach_maps.builds()) << "nothing in play asks yet, and with the setting off nothing ever does";
    }
}

// On a level with no door nothing is door-shut (the exit NoBarringDoor), no map is built for the
// question, the start cell is the corner's, and the route finder's route for a bot to its foe round
// a wall is the same with the setting on and off. Setting on and off.
// Fails when broken: door_shut_exit without its no-barring-door exit (maps are built).
TEST_F(SemReach, with_no_door_nothing_is_door_shut_and_every_route_is_the_classic_route)
{
    std::vector<std::vector<PathState>> routes;
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        wall_row(10, 2, 20);
        walker* bot = living_at(FAMILY_SOLDIER, 1, 5 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        walker* foe = living_at(FAMILY_ORC, 2, 5 * GRID_SIZE, 15 * GRID_SIZE, 16, 16);
        walker* ally_door = door_at(30 * GRID_SIZE, 30 * GRID_SIZE, 1);  // of the bot's side: bars nothing
        ASSERT_TRUE(bot && foe && ally_door);
        EXPECT_EQ(fixes ? Exit::NoBarringDoor : Exit::NotAsked, og::reach::door_shut_exit(*bot, *foe));
        EXPECT_FALSE(og::reach::door_shut(*bot, *foe));
        EXPECT_FALSE(og::reach::shut_out(*bot));
        EXPECT_EQ((Cell{5, 5, 0}), og::reach::start_cell(*bot));
        EXPECT_EQ(0u, world().reach_maps.builds()) << "no map built for these questions";
        EXPECT_TRUE(og::reach::cell_answer(*bot, {5, 5, 0}, Mode::Shut).step) << "grass stands either way";
        EXPECT_EQ(fixes ? 1u : 0u, world().reach_maps.builds())
            << "a cell's answer of a kind no door bars: the terrain half alone, no door half";
        bot->set_foe(foe);
        bot->find_path_to_foe();
        routes.push_back(bot->path_to_foe);
        EXPECT_FALSE(bot->path_to_foe.empty()) << "the staging: a route round the wall";
    }
    EXPECT_EQ(routes[0], routes[1]);
}

// Each exit of door_shut gives today's answer ("no door keeps it") but the last. Setting on unless
// said. An orc bot (melee-only) in a pen of walls whose one gap holds an enemy door; its foe
// outside. Exits: not asked (the setting off; a seat; a tower; a guard), no barring door (the door
// of its side; its key held), not a fighter (a dead foe; a door), another floor, no region (it
// stands inside a wall), walks shut (a foe inside the pen), no way open (a foe inside a closed
// box of walls), the shot (an archer on the pen's floor shooting across water at a foe on an island
// whose one bridge holds the door), and door-shut.
// Fails when broken: door_shut_exit with its NoWayOpen exit removed (the walled foe reads DoorShut).
TEST_F(SemReach, each_exit_of_door_shut_gives_todays_answer)
{
    const auto scene = [&](short fixes, walker*& orc, walker*& foe, walker*& door) {
        fresh_world(fixes);
        two_floors();
        const Cell gap = pen(10, 10, 14, 14);
        door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
        orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(door && orc && foe);
    };
    walker* orc = nullptr;
    walker* foe = nullptr;
    walker* door = nullptr;
    ASSERT_NO_FATAL_FAILURE(scene(0, orc, foe, door));
    EXPECT_EQ(Exit::NotAsked, og::reach::door_shut_exit(*orc, *foe)) << "the setting off";
    ASSERT_NO_FATAL_FAILURE(scene(1, orc, foe, door));
    EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc, *foe)) << "only the door keeps the orc from its foe";
    EXPECT_TRUE(og::reach::door_shut(*orc, *foe));
    orc->set_user(0);
    EXPECT_EQ(Exit::NotAsked, og::reach::door_shut_exit(*orc, *foe)) << "a seat";
    orc->set_user(-1);
    orc->set_act_type(ACT_GUARD);
    EXPECT_EQ(Exit::NotAsked, og::reach::door_shut_exit(*orc, *foe)) << "a guard at its post";
    orc->set_act_type(ACT_RANDOM);
    walker* tower = living_at(FAMILY_TOWER1, 1, 11 * GRID_SIZE, 13 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, tower);
    EXPECT_EQ(Exit::NotAsked, og::reach::door_shut_exit(*tower, *foe)) << "a tower";
    door->set_team_num(1);
    EXPECT_EQ(Exit::NoBarringDoor, og::reach::door_shut_exit(*orc, *foe)) << "the door of its side";
    door->set_team_num(kDoorTeam);
    orc->set_keys(static_cast<std::uint32_t>(key_level_mask(door->stats()->level())));
    EXPECT_EQ(Exit::NoBarringDoor, og::reach::door_shut_exit(*orc, *foe)) << "its key held";
    orc->set_keys(0);
    foe->set_dead(1);
    EXPECT_EQ(Exit::NotAFighter, og::reach::door_shut_exit(*orc, *foe)) << "a dead foe";
    foe->set_dead(0);
    EXPECT_EQ(Exit::NotAFighter, og::reach::door_shut_exit(*orc, *door)) << "a door is no fighter";
    foe->set_floor(1);
    EXPECT_EQ(Exit::OtherFloor, og::reach::door_shut_exit(*orc, *foe)) << "another floor";
    foe->set_floor(0);
    walker* inside = living_at(FAMILY_SOLDIER, 2, 13 * GRID_SIZE, 13 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, inside);
    EXPECT_EQ(Exit::WalksShut, og::reach::door_shut_exit(*orc, *inside)) << "a foe in the pen";
    world().remove_ob(inside);
    for (int x = 30; x <= 34; ++x) {
        tile(0, x, 30, PIX_H_WALL1);
        tile(0, x, 34, PIX_H_WALL1);
    }
    wall_column(30, 30, 34);
    wall_column(34, 30, 34);
    walker* boxed = living_at(FAMILY_SOLDIER, 2, 32 * GRID_SIZE, 32 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, boxed);
    EXPECT_EQ(Exit::NoWayOpen, og::reach::door_shut_exit(*orc, *boxed)) << "walls all round: no way even with the doors open";
    world().remove_ob(boxed);
    tile(0, 12, 12, PIX_H_WALL1);
    EXPECT_EQ(Exit::NoRegion, og::reach::door_shut_exit(*orc, *foe)) << "it stands inside a wall";
    tile(0, 12, 12, PIX_GRASS1);
    EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc, *foe));

    // The shot: an island ringed by water, its one bridge cell holding the door. An archer on the
    // shore shoots across the water; the orc beside it cannot.
    fresh_world(1);
    for (int x = 4; x <= 14; ++x)
        for (int y = 4; y <= 14; ++y)
            if (x == 4 || x == 14 || y == 4 || y == 14 || x == 5 || x == 13 || y == 5 || y == 13)
                tile(0, x, y, PIX_WATER1);
    tile(0, 9, 4, PIX_GRASS1);
    tile(0, 9, 5, PIX_GRASS1);
    walker* bridge_door = door_at(9 * GRID_SIZE, 4 * GRID_SIZE);
    walker* islander = living_at(FAMILY_SOLDIER, 2, 9 * GRID_SIZE, 9 * GRID_SIZE, 16, 16);
    walker* archer = living_at(FAMILY_ARCHER, 1, 9 * GRID_SIZE, 15 * GRID_SIZE, 16, 16);
    walker* orc2 = living_at(FAMILY_ORC, 1, 11 * GRID_SIZE, 15 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(bridge_door && islander && archer && orc2);
    archer->face_delta(0, -1);
    archer->set_lineofsight(10);
    weap test_weapon(walker::WeaponProfileProbeTag{});
    ASSERT_TRUE(archer->unhooked_weapon(test_weapon, 0.0f, -1.0f));
    ASSERT_GT(walker::weapon_reach(test_weapon), 96) << "the staging: the islander 96 px up is in reach";
    EXPECT_EQ(Exit::Shot, og::reach::door_shut_exit(*archer, *islander)) << "its arrow lands across the water";
    EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc2, *islander)) << "the orc cannot shoot";
    bridge_door->set_dead(1);
    EXPECT_EQ(Exit::NoBarringDoor, og::reach::door_shut_exit(*orc2, *islander)) << "the bridge open: no door is left";
    EXPECT_TRUE(og::reach::walks_to(*orc2, *islander, Mode::Shut));
}

// Approach cells (reach.cpp, approach_in): the standing cells on which the asker's box, grown by
// one cell on every side, meets the body's box. A tent of 32 x 32 at 160,160 (two cells by two):
// for a 16 px asker the 4 x 4 cells 9..12 (16), for a 32 px asker the 5 x 5 cells 8..12 (25), as
// collide's shrink works out; a two-door doorway in a wall with a 20 px foe behind it: the door
// cells are approach cells with the doors open and not shut, so the asker walks to the foe only with
// the doors open; the nearest approach cell is the reached one fewest cells away, the first of a tie
// in row order. Setting on.
// Fails when broken: approach_in growing the box by half a cell (the counts differ).
TEST_F(SemReach, approach_cells_ring_a_tent_a_big_asker_and_a_two_door_doorway)
{
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    place(tent, 160, 160, 32, 32);
    for (short size : {short{16}, short{32}}) {
        SCOPED_TRACE(size == 16 ? "a 16 px asker" : "a 32 px asker");
        walker* w = living_at(FAMILY_SOLDIER, 1, 400, 400, size, size);
        ASSERT_NE(nullptr, w);
        std::vector<Cell> want;
        const int lo = size == 16 ? 9 : 8;
        for (int y = lo; y <= 12; ++y)
            for (int x = lo; x <= 12; ++x)
                want.push_back(Cell{x, y, 0});
        EXPECT_EQ(want, og::reach::approach_cells(*w, *tent, Mode::Open));
        Cell nearest{};
        ASSERT_TRUE(og::reach::nearest_approach(*w, *tent, Mode::Open, nearest));
        EXPECT_EQ((Cell{12, 12, 0}), nearest) << "the corner cell nearest the asker at 25,25";
        world().remove_ob(w);
    }
    // A tie: the asker at (5,10), the approach cell (9,10) walled, so (9,9) and (9,11) are both
    // five cells off; the first in row order wins.
    tile(0, 9, 10, PIX_H_WALL1);
    walker* west = living_at(FAMILY_SOLDIER, 1, 5 * GRID_SIZE, 10 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, west);
    Cell tie{};
    ASSERT_TRUE(og::reach::nearest_approach(*west, *tent, Mode::Open, tie));
    EXPECT_EQ((Cell{9, 9, 0}), tie);
    world().remove_ob(west);
    tile(0, 9, 10, PIX_GRASS1);
    world().remove_ob(tent);

    wall_row(10, 0, kW - 1);
    tile(0, 10, 10, PIX_GRASS1);
    tile(0, 11, 10, PIX_GRASS1);
    walker* left = door_at(160, 160);
    walker* right = door_at(176, 160);
    walker* foe = living_at(FAMILY_SOLDIER, 2, 166, 178, 20, 20);
    walker* bot = living_at(FAMILY_ORC, 1, 168, 64, 16, 16);
    ASSERT_TRUE(left && right && foe && bot);
    const std::vector<Cell> open = og::reach::approach_cells(*bot, *foe, Mode::Open);
    const std::vector<Cell> shut = og::reach::approach_cells(*bot, *foe, Mode::Shut);
    EXPECT_NE(open.end(), std::find(open.begin(), open.end(), Cell{10, 10, 0})) << "a door cell, the doors open";
    EXPECT_NE(open.end(), std::find(open.begin(), open.end(), Cell{11, 10, 0}));
    EXPECT_EQ(shut.end(), std::find(shut.begin(), shut.end(), Cell{10, 10, 0})) << "closed by its door, shut";
    EXPECT_EQ(shut.end(), std::find(shut.begin(), shut.end(), Cell{11, 10, 0}));
    EXPECT_EQ(open.size(), shut.size() + 2u) << "the two door cells and nothing else";
    EXPECT_EQ(14u, open.size())
        << "worked out from collide's shrink: x 9..12 of rows 11..13 (twelve) and the two door cells of row 10";
    EXPECT_TRUE(og::reach::walks_to(*bot, *foe, Mode::Open));
    EXPECT_FALSE(og::reach::walks_to(*bot, *foe, Mode::Shut));
    Cell nearest{};
    ASSERT_TRUE(og::reach::nearest_approach(*bot, *foe, Mode::Open, nearest));
    EXPECT_EQ((Cell{10, 10, 0}), nearest) << "the door cell straight below the bot, six cells off";
    EXPECT_FALSE(og::reach::nearest_approach(*bot, *foe, Mode::Shut, nearest)) << "none reached with the doors shut";
}

// The start cell (reach.cpp, start_cell_in): the corner's cell, unless only a barring door closes
// it; then the standing cell (shut) the box overlaps most, a tie to the first of the scan; a corner
// in a wall keeps its cell, with no region; with the door of its side it is the corner's cell. A
// door at 160,160. Setting on.
// Fails when broken: start_cell_in returning the corner's cell always (the bot under the door starts in
// the door's cell).
TEST_F(SemReach, the_start_cell_leaves_a_doors_cell_for_the_cell_the_box_overlaps_most)
{
    walker* door = door_at(160, 160);
    walker* bot = living_at(FAMILY_SOLDIER, 1, 160, 175, 16, 16);
    ASSERT_TRUE(door && bot);
    EXPECT_EQ((Cell{10, 11, 0}), og::reach::start_cell(*bot))
        << "its corner 1 px inside the door's cell: the cell below, which its box overlaps 15 rows";
    EXPECT_LT(og::reach::region_of(*bot, {10, 10, 0}, Mode::Shut), 0) << "the staging: the door's cell is closed";
    bot->setxy(static_cast<short>(168), static_cast<short>(168));
    EXPECT_EQ((Cell{11, 10, 0}), og::reach::start_cell(*bot)) << "three standing cells overlapped alike: the first of the scan";
    bot->setxy(static_cast<short>(200), static_cast<short>(200));
    EXPECT_EQ((Cell{12, 12, 0}), og::reach::start_cell(*bot)) << "away from the door: the corner's cell";
    tile(0, 12, 12, PIX_H_WALL1);
    EXPECT_EQ((Cell{12, 12, 0}), og::reach::start_cell(*bot)) << "in a wall: the corner's cell";
    EXPECT_LT(og::reach::region_of(*bot, og::reach::start_cell(*bot), Mode::Shut), 0) << "with no region";
    tile(0, 12, 12, PIX_GRASS1);
    door->set_team_num(1);
    bot->setxy(static_cast<short>(160), static_cast<short>(175));
    EXPECT_EQ((Cell{10, 10, 0}), og::reach::start_cell(*bot)) << "the door of its side: the corner's cell";
}

// The tick's memo (reach.cpp, walks_to_in): twelve orcs of one kind in one room asking about the
// same foe are answered from the arrays once and from the memo eleven times in a tick; on the next
// tick the first asker reads the arrays again; every answer is the arrays' answer. Setting on.
// Fails when broken: walks_to_in keying its memo without the start region (the orc outside the room
// is answered the room's answer).
TEST_F(SemReach, twelve_askers_of_one_kind_in_one_room_are_answered_once_a_tick)
{
    const Cell gap = pen(10, 10, 20, 20);
    ASSERT_NE(nullptr, door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE)));
    walker* foe = living_at(FAMILY_SOLDIER, 2, 15 * GRID_SIZE, 4 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, foe);
    std::vector<walker*> orcs;
    for (int i = 0; i < 12; ++i) {
        walker* o = living_at(FAMILY_ORC, 1, static_cast<short>((12 + (i % 6)) * GRID_SIZE + 2),
                              static_cast<short>((13 + 2 * (i / 6)) * GRID_SIZE + 2), 16, 16);
        ASSERT_NE(nullptr, o);
        orcs.push_back(o);
    }
    const og::reach::Maps& maps = world().reach_maps;
    for (walker* o : orcs)
        EXPECT_FALSE(og::reach::walks_to(*o, *foe, Mode::Shut));
    EXPECT_EQ(11u, maps.memo_hits()) << "one answer from the arrays, eleven from the memo";
    walker* outside = living_at(FAMILY_ORC, 1, 15 * GRID_SIZE, 6 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, outside);
    EXPECT_TRUE(og::reach::walks_to(*outside, *foe, Mode::Shut)) << "another start region: its own answer";
    EXPECT_EQ(11u, maps.memo_hits());
    world().tick_count_ += 1;
    EXPECT_FALSE(og::reach::walks_to(*orcs[0], *foe, Mode::Shut));
    EXPECT_EQ(11u, maps.memo_hits()) << "a new tick: the arrays again";
    EXPECT_FALSE(og::reach::walks_to(*orcs[1], *foe, Mode::Shut));
    EXPECT_EQ(12u, maps.memo_hits());
}

// shut_out (reach.cpp): a bot is shut out when some live, awake, visible hostile living or
// generator on its floor is door-shut for it; cloaked or far makes no difference; a dormant or
// hidden one, one of its side or on another floor does not count; with no barring door it is not.
// An orc bot in a pen whose gap holds an enemy door. Setting on.
// Fails when broken: shut_out skipping hostiles past 100 px (the far foe does not count).
TEST_F(SemReach, a_bot_is_shut_out_by_an_enemy_only_a_door_keeps_from_it)
{
    two_floors();
    const Cell gap = pen(10, 10, 14, 14);
    walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(door && orc);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "no enemy at all";
    walker* foe = living_at(FAMILY_SOLDIER, 2, 35 * GRID_SIZE, 35 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(og::reach::shut_out(*orc)) << "a far enemy outside";
    orc->set_act_type(ACT_GUARD);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "a guard at its post is not asked";
    orc->set_act_type(ACT_RANDOM);
    foe->set_invisibility_left(50);
    EXPECT_TRUE(og::reach::shut_out(*orc)) << "cloaked";
    foe->set_invisibility_left(0);
    foe->set_hidden(true);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "hidden";
    foe->set_hidden(false);
    foe->set_dormant(true);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "dormant";
    foe->set_dormant(false);
    foe->set_team_num(1);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "of its side";
    foe->set_team_num(2);
    foe->set_floor(1);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "on another floor";
    foe->set_floor(0);
    walker* in_pen = living_at(FAMILY_SOLDIER, 2, 13 * GRID_SIZE, 13 * GRID_SIZE, 16, 16);
    ASSERT_NE(nullptr, in_pen);
    EXPECT_TRUE(og::reach::shut_out(*orc)) << "an enemy it gets at changes nothing: some enemy is door-shut";
    world().remove_ob(in_pen);
    walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(2);
    place(tent, 30 * GRID_SIZE, 5 * GRID_SIZE, 32, 32);
    foe->set_dead(1);
    EXPECT_TRUE(og::reach::shut_out(*orc)) << "a tent outside";
    door->set_team_num(1);
    EXPECT_FALSE(og::reach::shut_out(*orc)) << "no barring door";
}
