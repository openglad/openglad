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
// make a map stale, the start cell, the approach cells, the shot term and the exits of door_shut
// (suite SemReach, each question asked directly); and, in suite SemBotReach, Gameplay Fixes BI, a
// door a bot cannot open is a wall to it: the bot's route, its straight walk and the push an ally
// gives it, which ask the maps in play; and Gameplay Fixes BJ, a bot treats an enemy that only a
// locked door keeps it from as if it were not there: the foe searches, the top of its rounds, the
// near branch, a follower, the wander, the hold of a foe kept only by the shot and the cast rules,
// some in played scenes. Each test is named after the rule it asserts, asserts both
// settings where the setting decides, and names in its comment the game line whose breaking turns
// it red.
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
#include <openglad/gameplay/living.h>
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
#include <list>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
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
// asker's, that floor filed as the body map files a body's (obmap::filed_floor: below 0 as floor 0,
// past 120 as floor 120); and it changes nothing: no draw, no pile, no notice, the doors alive, the
// asker's collide_ob kept, no door count moved. A seat's soldier (it would be told "Key N needed!")
// at 320,320 on floor 0; enemy locked doors at 335,320 on floor 1, at 335,400 on floor 0 and at
// 335,480 on floor 120. Both settings.
// Fails when broken: query_list ignoring doors_only->floor (the floor-1 door is not met on floor 1,
// the floor-120 door not on floor 300); obmap::filed_floor without its clamp (floor -5 and floor
// 300 meet no door).
TEST_F(SemReach, the_door_question_asks_the_floor_it_is_given_and_changes_nothing)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        two_floors();
        walker* mover = living_at(FAMILY_SOLDIER, 1, 320, 320, 16, 16);
        walker* door = door_at(335, 320, kDoorTeam, 2, 1);
        walker* ground_door = door_at(335, 400, kDoorTeam, 2, 0);
        walker* high_door = door_at(335, 480, kDoorTeam, 2, 120);
        ASSERT_NE(nullptr, mover);
        ASSERT_NE(nullptr, door);
        ASSERT_NE(nullptr, ground_door);
        ASSERT_NE(nullptr, high_door);
        mover->set_user(0);
        const Seen before = seen(mover, {door, ground_door, high_door});
        EXPECT_FALSE(world().door_bars_at(321, 320, mover, 0)) << "floor 0: the floor-1 door is not in its piles";
        EXPECT_TRUE(world().door_bars_at(321, 320, mover, 1)) << "floor 1: the door bars the box";
        EXPECT_TRUE(world().door_bars_at(321, 400, mover, -5)) << "a floor below 0 is asked as floor 0";
        EXPECT_FALSE(world().door_bars_at(321, 400, mover, 1)) << "control: no door there on floor 1";
        EXPECT_TRUE(world().door_bars_at(321, 480, mover, 300)) << "a floor past 120 is asked as floor 120";
        EXPECT_TRUE(world().door_bars_at(321, 480, mover, 120));
        EXPECT_FALSE(world().door_bars_at(321, 480, mover, 119)) << "control: no door there on floor 119";
        EXPECT_TRUE(before == seen(mover, {door, ground_door, high_door})) << "the questions changed nothing";
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
// passability half, og::reach::for_each_step, with the terrain's answers, terrain_passes). Six kinds
// on a painted two-floor field holding a wall, water, tree crowns, a boulder of the decor plane, an
// air row, three stairs (one landing on air, one landing in a walled pocket of floor 1), a pocket
// of floor 0 whose only way in is a diagonal between two walls that meet at a corner, and a door
// off the grid: (1) every cell's two answers in the open mode are the terrain's, and in the shut
// mode the same except where a barring door meets the box (door_bars_at); a cell's stair-landing
// answer is the terrain's alone in the open mode (a landing on air is one) and none where a barring
// door meets the box in the shut mode (Gameplay Fixes BI, a door a bot cannot open is a wall to
// it); the kind's view
// (Asker::cells) answers as the cells do, and for_each_step over the view's open layer emits from
// every cell exactly the steps it emits over the terrain's answers (adjacent_cost's), over its shut
// layer the steps of the shut answers; (2) "the goal's region is one the start gets to" equals
// "the solve finds a route or stands on its goal" for a sample naming a start on air, inside
// terrain, beside the door, start equal to goal, a stair landing on air, the corner pocket and the
// up-stair's link walked backwards; (3) a door's death and a key open the shut route, a snapshot
// applied puts it back; a snapshot's bytes are equal with and without maps built.
// Why no pair for a diagonal past an air flank on a one-floor field: the flank test refuses only
// what the step test refuses or air (terrain_passes), and an air cell a step may end on is itself a
// way round the diagonal (two straight steps through it), so it never changes a region; only a
// flank the step test also refuses (the corner pocket's walls) can.
// Fails when broken: number_regions without the stair link (the stair pairs differ from the solver);
// the regions' walk answering every flank test yes (the corner pocket is reached); a stair link
// walked both ways (the pocket of floor 1 reaches floor 0); the door half without clearing the
// closed cell (shut answers differ); a stored layer read with the step bit for a flank test (the
// view of the one-floor air cell); the view's open layer read from the shut one (the door's cells);
// a stored landing test read with the flank bit (the air landing's view); terrain_passes without
// its landing arm (the stair landing on air has no route); a closed cell keeping its landing bit
// (the view's shut landing).
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
        // The corner pocket: (16,35) walled on seven sides; its eighth neighbour (17,34) is open,
        // and the diagonal from it passes between the walls at (16,34) and (17,35).
        const int pocket[][2] = {{15, 34}, {16, 34}, {15, 35}, {17, 35}, {15, 36}, {16, 36}, {17, 36}};
        for (const auto& c : pocket)
            tile(0, c[0], c[1], PIX_H_WALL1);
        // An up-stair at (25,36) whose landing on floor 1 is walled on all eight sides.
        tile(0, 25, 36, PIX_ZSTAIR_UP);
        for (int x = 24; x <= 26; ++x)
            for (int y = 35; y <= 37; ++y)
                if (x != 25 || y != 36)
                    tile(1, x, y, PIX_H_WALL1);
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
        {"into the corner pocket", {19, 33, 0}, {16, 35, 0}},
        {"down the up-stair's link", {25, 36, 1}, {25, 38, 0}},
    };
    // The plain walker's answers, worked out from the paint: every pair has a route but the box,
    // the corner pocket and the walled landing.
    const bool plain[] = {true, true, true, true, true, true, true, true, true, false, false, false};
    for (const Kind& k : kinds) {
        SCOPED_TRACE(k.name);
        w->stats()->set_bit_flags(BIT_FLYING | BIT_FORESTWALK | BIT_SWIMMING | BIT_ETHEREAL, 0);
        if (k.flag != 0)
            w->stats()->set_bit_flags(k.flag, 1);
        w->set_sizex(k.size);
        w->set_sizey(k.size);
        const bool nonflyer = !w->flies();
        const og::reach::CellsView view = og::reach::Asker(*w).cells();
        std::vector<CellAnswer> shut_answers(static_cast<std::size_t>(2 * kW * kW));
        std::vector<char> shut_landings(static_cast<std::size_t>(2 * kW * kW), 0);
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
                    shut_answers[static_cast<std::size_t>((f * kW + y) * kW + x)] = want_shut;
                    closed += (terrain.step && door_meets) ? 1 : 0;
                    ASSERT_EQ(shut.step, view(x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Step))
                        << cell_name(c) << " the view, shut";
                    ASSERT_EQ(shut.flank, view(x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Flank))
                        << cell_name(c) << " the view, shut";
                    ASSERT_EQ(terrain.step, view.passes(Mode::Open, x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Step))
                        << cell_name(c) << " the view, open";
                    ASSERT_EQ(terrain.flank, view.passes(Mode::Open, x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Flank))
                        << cell_name(c) << " the view, open";
                    // A stair's landing test: the terrain's alone (a landing on air is a link); a
                    // cell a barring door closes is no landing in the shut layer.
                    const bool landing = og::reach::terrain_passes(world(), *w, x * GRID_SIZE, y * GRID_SIZE, f,
                                                                   og::reach::StepTest::Landing, multifloor, nonflyer);
                    const bool want_landing = landing && !(terrain.step && door_meets);
                    shut_landings[static_cast<std::size_t>((f * kW + y) * kW + x)] = want_landing ? 1 : 0;
                    ASSERT_EQ(landing, view.passes(Mode::Open, x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Landing))
                        << cell_name(c) << " the view's landing, open";
                    ASSERT_EQ(want_landing, view(x * GRID_SIZE, y * GRID_SIZE, f, og::reach::StepTest::Landing))
                        << cell_name(c) << " the view's landing, shut";
                }
            }
        }
        EXPECT_EQ(k.size == 16 ? 4 : 6, closed)
            << "the door at 178,150 closes the four grass cells (11..12, 9..10) of a 16 px kind; of a "
               "32 px kind the six of (10..12, 9..10), row 8 under the 32 px box being wall";
        // The steps for_each_step emits from every cell: over the terrain's answers (adjacent_cost's
        // own Cells) and over the view's open layer; over the shut answers and the view itself.
        using Step = std::tuple<int, int, int, og::reach::StepKind>;
        const auto steps = [&](int x, int y, int f, const auto& cells) {
            std::vector<Step> out;
            og::reach::for_each_step(world(), x * GRID_SIZE, y * GRID_SIZE, f, multifloor, nonflyer, cells,
                                     [&out](int ax, int ay, int af, og::reach::StepKind kind) {
                                         out.emplace_back(ax, ay, af, kind);
                                     });
            return out;
        };
        const auto terrain_cells = [&](int px, int py, int pf, og::reach::StepTest test) {
            return og::reach::terrain_passes(world(), *w, px, py, pf, test, multifloor, nonflyer);
        };
        const auto open_view = [&view](int px, int py, int pf, og::reach::StepTest test) {
            return view.passes(Mode::Open, px, py, pf, test);
        };
        const auto shut_cells = [&shut_answers, &shut_landings](int px, int py, int pf, og::reach::StepTest test) {
            if (px < 0 || py < 0 || pf < 0 || px / GRID_SIZE >= kW || py / GRID_SIZE >= kW || pf >= 2)
                return false;
            const std::size_t i = static_cast<std::size_t>((pf * kW + py / GRID_SIZE) * kW + px / GRID_SIZE);
            const CellAnswer& a = shut_answers[i];
            if (test == og::reach::StepTest::Landing)
                return shut_landings[i] != 0;
            return test == og::reach::StepTest::Step ? a.step : a.flank;
        };
        std::size_t emitted = 0;
        for (int f = 0; f < 2; ++f) {
            for (int y = 0; y < kW; ++y) {
                for (int x = 0; x < kW; ++x) {
                    const std::vector<Step> by_terrain = steps(x, y, f, terrain_cells);
                    ASSERT_EQ(by_terrain, steps(x, y, f, open_view)) << cell_name(Cell{x, y, f}) << " open steps";
                    ASSERT_EQ(steps(x, y, f, shut_cells), steps(x, y, f, view)) << cell_name(Cell{x, y, f}) << " shut steps";
                    emitted += by_terrain.size();
                }
            }
        }
        EXPECT_GT(emitted, static_cast<std::size_t>(kW * kW)) << "the staging: steps to compare";
        for (std::size_t i = 0; i < std::size(pairs); ++i) {
            const Pair& p = pairs[i];
            SCOPED_TRACE(p.name);
            const bool solver = route_exists(w, p.from, p.to);
            EXPECT_EQ(solver, og::reach::reaches(*w, p.from, p.to, Mode::Open));
            if (k.flag == 0 && k.size == 16) {
                EXPECT_EQ(plain[i], solver) << "the route finder's own answer, worked out from the paint";
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
    const og::reach::CellsView air_view = og::reach::Asker(*walker1).cells();
    EXPECT_TRUE(air_view(8 * GRID_SIZE, 8 * GRID_SIZE, 0, og::reach::StepTest::Step)) << "the view reads the step bit";
    EXPECT_FALSE(air_view(8 * GRID_SIZE, 8 * GRID_SIZE, 0, og::reach::StepTest::Flank)) << "and the flank bit";
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
    EXPECT_TRUE(om.files_a_door());
    ASSERT_EQ(1, om.remove(door));
    EXPECT_EQ(writes + 2, om.door_writes()) << "taken out";
    EXPECT_FALSE(om.files_a_door()) << "the only door taken out";
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
// setting on the same first question builds a terrain half and a door half (2), and in the battle
// the bots' routes ask the maps (Gameplay Fixes BI, a door a bot cannot open is a wall to it). An
// orc bot in a pen whose gap holds an enemy door, a second door just outside the gap (the two
// touch), its foe outside; an archer bot far off, the foe outside its firing box.
// Fails when broken: judged() without its setting test (the counter moves with the setting off);
// the shot term without its not-judged exit (the archer's far foe reads "no shot" with the setting
// off); cell_answer's not-judged arm answering no terrain (the door's cell reads closed, or the
// wall's open); plan_route never judging (the battle builds nothing with the setting on).
TEST_F(SemReach, nothing_is_asked_or_built_with_the_setting_off)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        const Cell gap = pen(10, 10, 14, 14);
        walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
        walker* door2 = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>((gap.y - 1) * GRID_SIZE));
        walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        walker* archer = living_at(FAMILY_ARCHER, 1, 30 * GRID_SIZE, 30 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(door && door2 && orc && foe && archer);
        archer->set_lineofsight(5);  // a box of 80 px; the foe is 288 px off on each axis
        const og::reach::Maps& maps = world().reach_maps;
        const bool on = fixes != 0;
        EXPECT_EQ(on ? Exit::DoorShut : Exit::NotAsked, og::reach::door_shut_exit(*orc, *foe));
        EXPECT_EQ(on, og::reach::shut_out(*orc));
        EXPECT_EQ(!on, og::reach::walks_to(*orc, *foe, Mode::Shut));
        EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Open));
        EXPECT_EQ(!on, og::reach::gets_at(*orc, *foe));
        EXPECT_EQ(!on, og::reach::shoots_at(*archer, *foe)) << "off: not judged (yes); on: outside the firing box";
        EXPECT_EQ((Cell{12, 12, 0}), og::reach::start_cell(*orc));
        EXPECT_EQ(on ? 1u : 0u, og::reach::closed_cells(*orc, *door).size());
        EXPECT_EQ(!on, og::reach::reaches(*orc, {12, 12, 0}, {12, 5, 0}, Mode::Shut));
        EXPECT_EQ(on, og::reach::region_of(*orc, {12, 5, 0}, Mode::Shut) >= 0);
        EXPECT_EQ(on, !og::reach::approach_cells(*orc, *foe, Mode::Open).empty());
        Cell near{};
        EXPECT_EQ(on, og::reach::nearest_approach(*orc, *foe, Mode::Open, near));
        EXPECT_EQ((CellAnswer{}), og::reach::cell_answer(*orc, {10, 12, 0}, Mode::Shut)) << "the pen's wall, either way";
        EXPECT_EQ((on ? CellAnswer{} : CellAnswer{true, true}), og::reach::cell_answer(*orc, gap, Mode::Shut))
            << "the door's cell: closed with the setting on, the terrain's grass with it off";
        og::reach::Asker asker(*orc);
        const og::reach::CellsView view = asker.cells();
        EXPECT_EQ(on, view.shut != nullptr);
        EXPECT_EQ(on, view.passes(Mode::Open, 12 * GRID_SIZE, 5 * GRID_SIZE, 0, og::reach::StepTest::Step))
            << "off: the empty view holds no standing cell";
        EXPECT_EQ(on ? (std::vector<const walker*>{door, door2}) : std::vector<const walker*>{}, asker.barring_doors(0));
        EXPECT_EQ(on, !asker.regions_touched(*door).empty());
        EXPECT_EQ(on ? (std::vector<const walker*>{door2}) : std::vector<const walker*>{}, asker.doors_touched(*door));
        const int outside = og::reach::region_of(*orc, {12, 5, 0}, Mode::Shut);
        EXPECT_EQ(on ? (std::vector<const walker*>{door2}) : std::vector<const walker*>{},
                  asker.doors_beside(on ? outside : 0));
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
        if (on) {
            EXPECT_NE(0u, world().reach_maps.builds()) << "the bots' routes read their kinds' maps";
        } else {
            EXPECT_EQ(0u, world().reach_maps.builds()) << "with the setting off nothing ever asks";
        }
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

// A walker holding no key is asked as a kind of its own (og::reach::Asker with keys given,
// door_bars with keys given): an orc bot outside a pen, holding the key of the enemy door in the
// pen's gap, walks to its foe inside; the same orc asked holding no key does not (only the door
// keeps it: door-shut), and the door closes its cell in that kind's map; asking changes no key of
// the orc. Setting on.
// Fails when broken: door_bars reading the mover's own keys instead of the keys given (the keyless
// orc walks through the door).
TEST_F(SemReach, a_walker_holding_no_key_is_asked_as_a_kind_of_its_own)
{
    const Cell gap = pen(10, 10, 14, 14);
    walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
    walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(door && foe && orc);
    const std::uint32_t key = static_cast<std::uint32_t>(key_level_mask(door->stats()->level()));
    orc->set_keys(key);
    EXPECT_FALSE(og::reach::door_bars(orc, door)) << "its own key is held";
    EXPECT_FALSE(og::reach::door_bars(orc, door, key)) << "the same keys given";
    EXPECT_TRUE(og::reach::door_bars(orc, door, 0)) << "no key given: the door bars it";
    EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Shut)) << "its key opens the pen";
    EXPECT_EQ(Exit::NoBarringDoor, og::reach::door_shut_exit(*orc, *foe));
    EXPECT_TRUE(og::reach::closed_cells(*orc, *door).empty());
    og::reach::Asker keyless(*orc, 0);
    EXPECT_FALSE(keyless.walks_to(*foe, Mode::Shut)) << "holding no key it would not walk to its foe";
    EXPECT_TRUE(keyless.walks_to(*foe, Mode::Open));
    EXPECT_EQ(Exit::DoorShut, keyless.door_shut_exit(*foe)) << "only the door keeps the keyless orc from its foe";
    EXPECT_EQ((std::vector<Cell>{{gap.x, gap.y, 0}}), keyless.closed_cells(*door));
    EXPECT_EQ(key, orc->keys()) << "asking changed no key";
}

// The door walk's facts, kept with each door half (reach.cpp, note_touches; Asker::barring_doors,
// regions_touched, doors_touched, doors_beside). A wall band three cells deep (rows 10 to 12) across
// the field; at column 20 a doorway two doors deep with no floor between them (door A in row 10,
// door B in row 11, row 12 open); at column 30 a doorway with floor between (door C in row 10, the
// cell of row 11 open, door D in row 12); two doors on one spot north of the band at (8,5), E and
// F; a door of the bot's side at (25,5), G. A 16 px orc bot north of the band. Each barring door
// closes its own cell. North of the band is region N, south of it S, the cell (30,11) between C
// and D a region of its own, M. A touches N and B; B touches A and S (through row 12); C touches N
// and M, D touches M and S, and C and D do not touch; E and F touch each other (they close the
// same cell) and N; G bars nothing. Setting on.
// Fails when broken: note_touches reading the closed cell's own region instead of its
// 4-neighbours' (no region is touched); only the closed cells themselves compared between doors (A
// and B do not touch); only the 4-neighbours compared (E and F do not touch); the per-region list
// left empty; barring_doors not asking the floor (floor 1 lists the doors of floor 0).
TEST_F(SemReach, the_door_half_keeps_the_regions_and_the_doors_each_barring_door_touches)
{
    for (int y = 10; y <= 12; ++y)
        wall_row(y, 0, kW - 1);
    tile(0, 20, 10, PIX_GRASS1);
    tile(0, 20, 11, PIX_GRASS1);
    tile(0, 20, 12, PIX_GRASS1);
    tile(0, 30, 10, PIX_GRASS1);
    tile(0, 30, 11, PIX_GRASS1);
    tile(0, 30, 12, PIX_GRASS1);
    walker* a = door_at(20 * GRID_SIZE, 10 * GRID_SIZE);
    walker* b = door_at(20 * GRID_SIZE, 11 * GRID_SIZE);
    walker* c = door_at(30 * GRID_SIZE, 10 * GRID_SIZE);
    walker* d = door_at(30 * GRID_SIZE, 12 * GRID_SIZE);
    walker* e = door_at(8 * GRID_SIZE, 5 * GRID_SIZE);
    walker* f = door_at(8 * GRID_SIZE, 5 * GRID_SIZE);
    walker* g = door_at(25 * GRID_SIZE, 5 * GRID_SIZE, 1);
    walker* orc = living_at(FAMILY_ORC, 1, 5 * GRID_SIZE, 3 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(a && b && c && d && e && f && g && orc);
    using Doors = std::vector<const walker*>;
    og::reach::Asker asker(*orc);
    const int n = asker.region_of({5, 5, 0}, Mode::Shut);
    const int s = asker.region_of({5, 15, 0}, Mode::Shut);
    const int m = asker.region_of({30, 11, 0}, Mode::Shut);
    ASSERT_GE(n, 0);
    ASSERT_GE(s, 0);
    ASSERT_GE(m, 0);
    ASSERT_NE(n, s) << "the staging: the doors part the field";
    ASSERT_NE(n, m);
    ASSERT_NE(m, s);
    EXPECT_EQ((Doors{a, b, c, d, e, f}), asker.barring_doors(0)) << "the world's order; G is of the orc's side";
    EXPECT_EQ(Doors{}, asker.barring_doors(1)) << "no door is filed on floor 1";
    const auto sorted = [](std::vector<int> v) {
        std::sort(v.begin(), v.end());
        return v;
    };
    EXPECT_EQ(std::vector<int>{n}, asker.regions_touched(*a));
    EXPECT_EQ(std::vector<int>{s}, asker.regions_touched(*b)) << "through the open cell of row 12";
    EXPECT_EQ(sorted({n, m}), asker.regions_touched(*c));
    EXPECT_EQ(sorted({m, s}), asker.regions_touched(*d));
    EXPECT_EQ(std::vector<int>{n}, asker.regions_touched(*e));
    EXPECT_EQ(std::vector<int>{n}, asker.regions_touched(*f));
    EXPECT_TRUE(asker.regions_touched(*g).empty()) << "a door of its side bars nothing";
    EXPECT_EQ(Doors{b}, asker.doors_touched(*a)) << "two doors in a row, no floor between";
    EXPECT_EQ(Doors{a}, asker.doors_touched(*b));
    EXPECT_EQ(Doors{}, asker.doors_touched(*c)) << "floor between C and D";
    EXPECT_EQ(Doors{}, asker.doors_touched(*d));
    EXPECT_EQ(Doors{f}, asker.doors_touched(*e)) << "two doors closing the same cell";
    EXPECT_EQ(Doors{e}, asker.doors_touched(*f));
    EXPECT_EQ((Doors{a, c, e, f}), asker.doors_beside(n));
    EXPECT_EQ((Doors{c, d}), asker.doors_beside(m));
    EXPECT_EQ((Doors{b, d}), asker.doors_beside(s));
    EXPECT_EQ(Doors{}, asker.doors_beside(-1));
}

// An asker (og::reach::Asker) is resolved to its kind once (Maps::resolutions): twelve door-shut
// questions of one Asker and its shut_out resolve it once, and a free question is one resolution
// (not one per exit it passes); after the walker moves, a door dies or comes back, or the walker
// picks up a key, the next question of the same Asker resolves again and answers the world as it
// now stands. An orc bot in a pen whose gap holds an enemy door, twelve foes outside. Setting on.
// Fails when broken: Asker::fresh resolving at every question (the counts move per question);
// Asker::fresh keeping its resolution when the walker moved (the start cell is the old one).
TEST_F(SemReach, an_asker_is_resolved_once_and_resolved_again_when_what_it_read_changes)
{
    const Cell gap = pen(10, 10, 14, 14);
    walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(door && orc);
    std::vector<walker*> foes;
    for (int i = 0; i < 12; ++i) {
        walker* foe = living_at(FAMILY_SOLDIER, 2, static_cast<short>((2 + 2 * i) * GRID_SIZE), 3 * GRID_SIZE, 16, 16);
        ASSERT_NE(nullptr, foe);
        foes.push_back(foe);
    }
    const og::reach::Maps& maps = world().reach_maps;
    og::reach::Asker asker(*orc);
    const std::uint64_t before = maps.resolutions();
    for (walker* foe : foes)
        EXPECT_EQ(Exit::DoorShut, asker.door_shut_exit(*foe));
    EXPECT_TRUE(asker.shut_out());
    EXPECT_EQ((Cell{12, 12, 0}), asker.start_cell());
    EXPECT_EQ(before + 1, maps.resolutions()) << "fourteen questions of one Asker, one resolution";
    EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc, *foes[0]));
    EXPECT_EQ(before + 2, maps.resolutions()) << "a free question is one resolution";

    orc->setxy(static_cast<short>(12 * GRID_SIZE), static_cast<short>(5 * GRID_SIZE));
    EXPECT_EQ((Cell{12, 5, 0}), asker.start_cell()) << "the walker moved out of the pen";
    EXPECT_EQ(Exit::WalksShut, asker.door_shut_exit(*foes[0]));
    EXPECT_EQ(before + 3, maps.resolutions());
    orc->setxy(static_cast<short>(12 * GRID_SIZE), static_cast<short>(12 * GRID_SIZE));
    EXPECT_EQ(Exit::DoorShut, asker.door_shut_exit(*foes[0])) << "back in the pen";
    door->set_dead(1);
    EXPECT_EQ(Exit::NoBarringDoor, asker.door_shut_exit(*foes[0])) << "the door's death is read at the next question";
    door->set_dead(0);
    EXPECT_EQ(Exit::DoorShut, asker.door_shut_exit(*foes[0])) << "and its coming back";
    orc->set_keys(static_cast<std::uint32_t>(key_level_mask(door->stats()->level())));
    EXPECT_EQ(Exit::NoBarringDoor, asker.door_shut_exit(*foes[0])) << "a key picked up is read at the next question";
    EXPECT_TRUE(asker.walks_to(*foes[0], Mode::Shut));
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BI, a door a bot cannot open is a wall to it: the bot's route (walker_pathing.cpp,
// og::reach::plan_route, the route finder reading og::reach::RouteCells), its straight walk
// (statistics::direct_walk and direct_walk_to_point) and the push an ally gives it (living::shove),
// the last two through GameWorld::step_refused. Doors of a third side (team 3) unless said; the
// bot an orc of team 1 (no seat drives it), 16 x 16.
// ---------------------------------------------------------------------------------------------

namespace {

class SemBotReach : public SemReach {
protected:
    // The cells of w's route, in order.
    static std::vector<Cell> route_of(const walker* w)
    {
        std::vector<Cell> out;
        for (PathState s : w->path_to_foe)
            out.push_back(Cell{GET_STATE_X(s) / GRID_SIZE, GET_STATE_Y(s) / GRID_SIZE, GET_STATE_FLOOR(s)});
        return out;
    }
    static bool holds(const std::vector<Cell>& route, Cell c)
    {
        return std::find(route.begin(), route.end(), c) != route.end();
    }
    static std::string text(const std::vector<Cell>& route)
    {
        std::string out;
        for (const Cell& c : route)
            out += cell_name(c);
        return out.empty() ? std::string("(none)") : out;
    }
    // A wall along row y from x 0 to 35 (the cells 36 to 39 the way round), with doors in the cells
    // listed (grass under each); the doors returned.
    std::vector<walker*> door_fence(int y, std::initializer_list<int> door_cells)
    {
        wall_row(y, 0, 35);
        std::vector<walker*> doors;
        for (int x : door_cells) {
            tile(0, x, y, PIX_GRASS1);
            doors.push_back(door_at(static_cast<short>(x * GRID_SIZE), static_cast<short>(y * GRID_SIZE)));
        }
        return doors;
    }
    // The orc bot of these tests at pixel (x, y).
    walker* bot_at(short x, short y) { return living_at(FAMILY_ORC, 1, x, y, 16, 16); }
    // A state of the world stream whose first rng_.next(3) is not 0 (a shove proceeds).
    static std::uint32_t a_shoving_state()
    {
        for (std::uint32_t st = 1;; ++st) {
            og::sim::SimRandom r(st);
            if (r.next(3) != 0)
                return st;
        }
    }
    // A state of the world stream whose first rng_.next(bound) is not 0.
    static std::uint32_t a_state_whose_first_draw_of(std::uint32_t bound)
    {
        for (std::uint32_t st = 1;; ++st) {
            og::sim::SimRandom r(st);
            if (r.next(bound) != 0)
                return st;
        }
    }
    // Eight doors (team 3, 16 x 16) on the cells around the cell (cx, cy): a ring that closes only
    // its own eight cells (a box touching a door by its edge does not meet it), so a body on (cx, cy)
    // has no approach cell outside the ring.
    std::vector<walker*> ring_of_doors(int cx, int cy)
    {
        std::vector<walker*> ring;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if (dx != 0 || dy != 0)
                    ring.push_back(door_at(static_cast<short>((cx + dx) * GRID_SIZE),
                                           static_cast<short>((cy + dy) * GRID_SIZE)));
        return ring;
    }
    // n orcs of team 1 on the cells x 30..39, y 0..(n/10): a crowd of the orc bots' own side, far
    // from every scene of these tests, that makes the body map hold more than 200 bodies.
    void crowd(int n)
    {
        for (int k = 0; k < n; ++k)
            ASSERT_NE(nullptr, living_at(FAMILY_ORC, 1, static_cast<short>((30 + k % 10) * GRID_SIZE),
                                         static_cast<short>((k / 10) * GRID_SIZE), 16, 16));
    }
    // A wall ring of PIX_H_WALL1 round the interior [x0..x1] x [y0..y1] (cells), on floor 0.
    void room(int x0, int y0, int x1, int y1)
    {
        for (int x = x0 - 1; x <= x1 + 1; ++x) {
            tile(0, x, y0 - 1, PIX_H_WALL1);
            tile(0, x, y1 + 1, PIX_H_WALL1);
        }
        for (int y = y0 - 1; y <= y1 + 1; ++y) {
            tile(0, x0 - 1, y, PIX_H_WALL1);
            tile(0, x1 + 1, y, PIX_H_WALL1);
        }
    }
    // A bot of a real family and its own box for a played scene, ready to act (walking, not busy).
    walker* scene_bot(int family, unsigned char team, short x, short y)
    {
        walker* w = living_at(family, team, x, y);
        if (w != nullptr) {
            w->set_busy(0.0f);
            w->set_ani_type(ANI_WALK);
        }
        return w;
    }
    // How many live walkers of the weapon list a walker of `owners` owns (shots and swings in flight).
    int weapons_owned_by(const std::vector<walker*>& owners)
    {
        int n = 0;
        for (const auto& up : world().weaplist)
            if (up && !up->dead() && std::find(owners.begin(), owners.end(), up->owner()) != owners.end())
                ++n;
        return n;
    }
    // The pen of each_exit_of_door_shut_gives_todays_answer: walls round cells 10..14, the gap at the
    // top (12,10) holding an enemy door (team 3); the door returned.
    walker* pen_with_a_door()
    {
        const Cell gap = pen(10, 10, 14, 14);
        return door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    }
};

}  // namespace

// The route (gameplay_context.cpp, adjacent_cost with og::reach::RouteCells). A wall across the
// field at row 20 from x 0 to 35, its cells 18 to 21 holding four doors (a door fence); the way
// round is the gap of cells 36 to 39. The orc at cell (20,14), its foe at (20,26). With the setting
// off the classic route prices each door like any body (ten a cell) and its straightness term makes
// the way round far dearer, so it runs through the fence (asserted). With the setting on the route
// never enters a cell a door closes, crosses row 20 in the gap, and ends on the foe's cell. A
// player's fighter (the orc given seat 0) with the setting on is routed as in the classic game.
// Fails when broken: RouteCells reading the open layer (the route runs through the fence with the
// setting on); plan_route or door_is_a_wall_to taking a seat's fighter for a bot (the seat's route
// goes round).
TEST_F(SemBotReach, the_route_goes_round_a_door_fence_and_never_through_it)
{
    for (int pass = 0; pass < 3; ++pass) {
        const short fixes = pass == 0 ? 0 : 1;
        const bool seat = pass == 2;
        SCOPED_TRACE(pass == 0 ? "Gameplay Fixes off" : seat ? "Gameplay Fixes on, a seat's fighter" : "Gameplay Fixes on");
        fresh_world(fixes);
        const std::vector<walker*> doors = door_fence(20, {18, 19, 20, 21});
        walker* bot = bot_at(20 * GRID_SIZE, 14 * GRID_SIZE);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 20 * GRID_SIZE, 26 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(bot && foe && doors.size() == 4 && doors[0] && doors[1] && doors[2] && doors[3]);
        if (seat)
            bot->set_user(0);
        bot->set_foe(foe);
        bot->find_path_to_foe();
        const std::vector<Cell> route = route_of(bot);
        ASSERT_FALSE(route.empty()) << "the staging: a route on both settings";
        EXPECT_EQ((Cell{20, 14, 0}), route.front());
        EXPECT_EQ((Cell{20, 26, 0}), route.back()) << text(route);
        int through_doors = 0;
        int through_gap = 0;
        for (const Cell& c : route) {
            through_doors += (c.y == 20 && c.x >= 18 && c.x <= 21) ? 1 : 0;
            through_gap += (c.y == 20 && c.x >= 36) ? 1 : 0;
        }
        if (fixes && !seat) {
            EXPECT_EQ(0, through_doors) << "no cell a door closes: " << text(route);
            EXPECT_EQ(1, through_gap) << "row 20 crossed once, in the gap: " << text(route);
        } else {
            EXPECT_EQ(1, through_doors) << "classic: the fence crossed once, through a door: " << text(route);
            EXPECT_EQ(0, through_gap) << text(route);
        }
    }
}

// The flank (for_each_step's diagonal test, read from RouteCells). The orc at cell (10,10), a door
// at (11,10), the foe at (11,11): the door and the foe touch the orc's box and each other without
// meeting them (the body map's box test needs two pixels of overlap), so only the cell (11,10) is
// closed. With the setting off the route is the one diagonal step; with it on the diagonal would
// pass beside the door's cell and is not planned: the route steps down, then right.
// Fails when broken: RouteCells answering the flank test from the open layer (the diagonal is
// planned with the setting on).
TEST_F(SemBotReach, a_diagonal_past_a_doors_corner_is_not_planned)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* door = door_at(11 * GRID_SIZE, 10 * GRID_SIZE);
        walker* bot = bot_at(10 * GRID_SIZE, 10 * GRID_SIZE);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 11 * GRID_SIZE, 11 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(door && bot && foe);
        if (fixes) {
            ASSERT_FALSE(og::reach::cell_answer(*bot, {11, 10, 0}, Mode::Shut).flank) << "the staging: the door's cell is closed";
            ASSERT_TRUE(og::reach::cell_answer(*bot, {10, 11, 0}, Mode::Shut).flank) << "the staging: the other flank stands";
            ASSERT_TRUE(og::reach::cell_answer(*bot, {11, 11, 0}, Mode::Shut).step) << "the staging: the foe's cell stands";
        }
        bot->set_foe(foe);
        bot->find_path_to_foe();
        const std::vector<Cell> route = route_of(bot);
        if (fixes) {
            EXPECT_EQ((std::vector<Cell>{{10, 10, 0}, {10, 11, 0}, {11, 11, 0}}), route) << text(route);
        } else {
            EXPECT_EQ((std::vector<Cell>{{10, 10, 0}, {11, 11, 0}}), route) << text(route);
        }
    }
}

// The start cell (plan_route's start_cell). A wall across the field at row 10 from x 0 to 35, a
// door in its cell 20; the orc stands just under the door, at pixel (320,175): its corner is 1 px
// inside the door's cell and its box meets the door by less than the body map's two pixels, so it
// stands there in play (the followers stuck below a door). Its foe sits at cell (20,4) above the
// wall. With the setting off the route starts in the door's cell and steps straight up, through the
// door. With it on it starts in the cell below, which the orc's box overlaps most, never enters the
// door's cell, and goes round through the gap.
// Fails when broken: Asker::start answering the corner's cell always (the route starts in the door's
// cell and steps up into the door with the setting on).
TEST_F(SemBotReach, a_bot_under_a_door_is_routed_away_from_it_and_not_through_it)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        const std::vector<walker*> doors = door_fence(10, {20});
        walker* bot = bot_at(20 * GRID_SIZE, 175);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 20 * GRID_SIZE, 4 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(bot && foe && doors[0]);
        ASSERT_EQ(0, ::collide(bot->xpos(), bot->ypos(), bot->sizex(), bot->sizey(), doors[0]->xpos(),
                               doors[0]->ypos(), doors[0]->sizex(), doors[0]->sizey()))
            << "the staging: the orc's box does not meet the door";
        bot->set_foe(foe);
        bot->find_path_to_foe();
        const std::vector<Cell> route = route_of(bot);
        ASSERT_GE(route.size(), 2u) << text(route);
        EXPECT_EQ((Cell{20, 4, 0}), route.back()) << text(route);
        if (fixes) {
            EXPECT_EQ((Cell{20, 11, 0}), route.front()) << "the cell its box overlaps most: " << text(route);
            EXPECT_FALSE(holds(route, {20, 10, 0})) << "never the door's cell: " << text(route);
            int through_gap = 0;
            for (const Cell& c : route)
                through_gap += (c.y == 10 && c.x >= 36) ? 1 : 0;
            EXPECT_EQ(1, through_gap) << text(route);
        } else {
            EXPECT_EQ((Cell{20, 10, 0}), route[0]) << "classic: from its corner's cell, the door's";
            EXPECT_EQ((Cell{20, 9, 0}), route[1]) << "classic: straight up through the door";
        }
    }
}

// The route to a point (walker::find_path_to_point, plan_route with no foe). The door fence of
// the_route_goes_round_a_door_fence_and_never_through_it; the orc at (20,14) is routed to the point
// of cell (20,26), nobody there. Setting off: through the fence. Setting on: round, by the gap.
// Then the staging of a_bot_under_a_door_is_routed_away_from_it_and_not_through_it: a door in
// cell 20 of a wall at row 10, the orc at pixel (320,175), its corner 1 px inside the door's cell,
// routed to the point of cell (20,4) above the wall. Setting off: from its corner's cell, the
// door's, straight up through the door. Setting on: from the cell below, which its box overlaps
// most, never through the door's cell, and across row 10 once, in the gap.
// Fails when broken: find_path_to_point not planning a bot's route (the classic solve runs through
// the fence with the setting on); find_path_to_point solving from the corner's cell instead of the
// plan's start cell (the route starts in the door's cell with the setting on).
TEST_F(SemBotReach, a_route_to_a_point_goes_round_a_door)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        const std::vector<walker*> doors = door_fence(20, {18, 19, 20, 21});
        walker* bot = bot_at(20 * GRID_SIZE, 14 * GRID_SIZE);
        ASSERT_NE(nullptr, bot);
        bot->find_path_to_point(20 * GRID_SIZE, 26 * GRID_SIZE);
        const std::vector<Cell> route = route_of(bot);
        ASSERT_FALSE(route.empty()) << "the staging: a route on both settings";
        EXPECT_EQ((Cell{20, 26, 0}), route.back()) << text(route);
        int through_doors = 0;
        int through_gap = 0;
        for (const Cell& c : route) {
            through_doors += (c.y == 20 && c.x >= 18 && c.x <= 21) ? 1 : 0;
            through_gap += (c.y == 20 && c.x >= 36) ? 1 : 0;
        }
        EXPECT_EQ(fixes ? 0 : 1, through_doors) << text(route);
        EXPECT_EQ(fixes ? 1 : 0, through_gap) << text(route);
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, under a door" : "Gameplay Fixes off, under a door");
        fresh_world(fixes);
        const std::vector<walker*> doors = door_fence(10, {20});
        walker* bot = bot_at(20 * GRID_SIZE, 175);
        ASSERT_TRUE(bot && doors[0]);
        ASSERT_EQ(0, ::collide(bot->xpos(), bot->ypos(), bot->sizex(), bot->sizey(), doors[0]->xpos(),
                               doors[0]->ypos(), doors[0]->sizex(), doors[0]->sizey()))
            << "the staging: the orc's box does not meet the door";
        bot->find_path_to_point(20 * GRID_SIZE, 4 * GRID_SIZE);
        const std::vector<Cell> route = route_of(bot);
        ASSERT_GE(route.size(), 2u) << text(route);
        EXPECT_EQ((Cell{20, 4, 0}), route.back()) << text(route);
        if (fixes) {
            EXPECT_EQ((Cell{20, 11, 0}), route.front()) << "the cell its box overlaps most: " << text(route);
            EXPECT_FALSE(holds(route, {20, 10, 0})) << "never the door's cell: " << text(route);
            int through_gap = 0;
            for (const Cell& c : route)
                through_gap += (c.y == 10 && c.x >= 36) ? 1 : 0;
            EXPECT_EQ(1, through_gap) << "row 10 crossed once, in the gap: " << text(route);
        } else {
            EXPECT_EQ((Cell{20, 10, 0}), route[0]) << "classic: from its corner's cell, the door's";
            EXPECT_EQ((Cell{20, 9, 0}), route[1]) << "classic: straight up through the door";
        }
    }
}

// A search that must fail is not run (plan_route). The orc in a pen of walls whose one gap holds a
// door, its foe outside. Setting on: the plan is judged and not run, and the route stays empty; the
// solve on the plan's cells, run anyway, finds nothing either (it could only fail). Setting off: the
// plan is not judged, and the classic route runs through the door. With the orc standing inside a
// wall cell (a start cell with no region, setting on) the search is run, and finds nothing.
// Fails when broken: plan_route marking every plan run (the pen's plan reads run); plan_route
// leaving a start with no region unrun.
TEST_F(SemBotReach, a_search_that_must_fail_is_not_run)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        const Cell gap = pen(10, 10, 14, 14);
        walker* door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
        walker* bot = bot_at(12 * GRID_SIZE, 12 * GRID_SIZE);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(door && bot && foe);
        bot->set_foe(foe);
        const og::reach::RoutePlan plan = og::reach::plan_route(*bot, Cell{12, 5, 0}, foe);
        EXPECT_EQ(fixes != 0, plan.judged);
        bot->find_path_to_foe();
        if (!fixes) {
            EXPECT_FALSE(bot->path_to_foe.empty()) << "classic: through the door";
            continue;
        }
        EXPECT_FALSE(plan.run) << "the foe's cell lies in no region the orc gets to";
        EXPECT_TRUE(bot->path_to_foe.empty());
        std::vector<PathState> path;
        float cost = 0.0f;
        const auto state = [](Cell c) {
            return reinterpret_cast<PathState>(static_cast<intptr_t>(
                static_cast<intptr_t>(c.floor) * FLOOR_STRIDE + c.y * MAP_WIDTH + c.x));
        };
        ensure_pathfinding_state(*current_game)->solve_for(bot, state(plan.start), state(plan.goal), path, cost, &plan.cells);
        EXPECT_TRUE(path.empty()) << "the solve on the plan's cells could only fail";
        tile(0, 12, 12, PIX_H_WALL1);
        ASSERT_LT(og::reach::region_of(*bot, og::reach::start_cell(*bot), Mode::Shut), 0)
            << "the staging: the orc stands inside a wall, its start cell has no region";
        const og::reach::RoutePlan walled = og::reach::plan_route(*bot, Cell{12, 5, 0}, foe);
        EXPECT_TRUE(walled.judged);
        EXPECT_TRUE(walled.run) << "a start cell with no region: the search runs";
        bot->find_path_to_foe();
        EXPECT_TRUE(bot->path_to_foe.empty()) << "and finds no way out of the pen";
    }
}

// The goal (plan_route): the foe's corner cell, unless only a door closes it. A door off the grid at
// pixel (328,160) closes the cells (20,10) and (21,10) of a 16 px kind; the foe stands beside it at
// (344,160), its box touching the door's, so its corner cell (21,10) is closed though it stands
// free. The orc at cell (21,20). Setting off: the route ends on (21,10). Setting on: on the nearest
// approach cell the orc gets to, (21,11), worked out from the boxes (the cells 20 to 23, rows 9 to
// 11, whose box grown by a cell meets the foe's; (21,11) is nine cells off, the next ten). With the
// orc standing inside a wall cell (its start cell has no region) the goal is not moved: the plan
// keeps the foe's closed cell as its goal and runs, and the search finds no route, since no step
// enters a closed cell. Setting off, from the wall cell too: the classic route to (21,10).
// Fails when broken: plan_route keeping the closed corner cell as the goal (the search is not run
// and the route is empty with the setting on); plan_route moving the goal from a start cell with
// no region (the walled orc's goal becomes (21,11) and a route is found).
TEST_F(SemBotReach, a_foe_whose_cell_a_door_closes_is_routed_to_from_beside_it)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* door = door_at(328, 160);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 344, 160, 16, 16);
        walker* bot = bot_at(21 * GRID_SIZE, 20 * GRID_SIZE);
        ASSERT_TRUE(door && foe && bot);
        if (fixes) {
            ASSERT_LT(og::reach::region_of(*bot, {21, 10, 0}, Mode::Shut), 0) << "the staging: the foe's cell is closed";
            ASSERT_GE(og::reach::region_of(*bot, {21, 10, 0}, Mode::Open), 0) << "the staging: by the door alone";
        }
        bot->set_foe(foe);
        bot->find_path_to_foe();
        const std::vector<Cell> route = route_of(bot);
        ASSERT_FALSE(route.empty()) << "a route on both settings";
        EXPECT_EQ(fixes ? (Cell{21, 11, 0}) : (Cell{21, 10, 0}), route.back()) << text(route);
        EXPECT_FALSE(fixes && (holds(route, {20, 10, 0}) || holds(route, {21, 10, 0}))) << text(route);

        SCOPED_TRACE("the orc inside a wall cell");
        tile(0, 21, 20, PIX_H_WALL1);
        bot->find_path_to_foe();
        if (!fixes) {
            ASSERT_FALSE(bot->path_to_foe.empty()) << "classic: a route from the wall cell";
            EXPECT_EQ((Cell{21, 10, 0}), route_of(bot).back()) << text(route_of(bot));
            continue;
        }
        ASSERT_LT(og::reach::region_of(*bot, og::reach::start_cell(*bot), Mode::Shut), 0)
            << "the staging: the orc stands inside a wall, its start cell has no region";
        const og::reach::RoutePlan walled = og::reach::plan_route(*bot, Cell{21, 10, 0}, foe);
        EXPECT_TRUE(walled.judged);
        EXPECT_EQ((Cell{21, 10, 0}), walled.goal) << "from a start cell with no region the goal is not moved";
        EXPECT_TRUE(walled.run) << "and the search runs";
        EXPECT_TRUE(bot->path_to_foe.empty()) << "and finds no route into the closed cell: " << text(route_of(bot));
    }
}

// A bot whose foe is a door that bars it (a door a level script names its foe): plan_route and
// RouteCells. A wall across the field at row 25, whole, its cell 20 holding door A; a wall at row 15
// from x 0 to 35 (the gap 36 to 39), its cell 20 holding door B. The orc above both at cell (20,8),
// door A its foe. Setting off: the classic route runs through door B and ends in door A's cell.
// Setting on: the route ends in door A's cell (only door A's cells stand for it) and never enters
// door B's cell: it goes round by the gap. Then the orc in a pen of walls whose one gap holds door
// B, door A alone in the field above it, its foe: the orc does not walk to door A with the doors
// that bar it shut, so with the setting on the plan is judged and not run, the route stays empty,
// and the solve on the plan's cells, run anyway, finds nothing either (door B's cells stay closed
// for it). Setting off: the classic route runs through door B into door A.
// Fails when broken: RouteCells opening every barring door's cells (the route runs through door B);
// plan_route without its door branch (the route ends beside door A, at (20,24)); the door branch
// running every search (the pen's plan reads run).
TEST_F(SemBotReach, a_bot_whose_foe_is_a_door_is_routed_into_that_door_and_no_other)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        wall_row(25, 0, 39);
        tile(0, 20, 25, PIX_GRASS1);
        walker* door_a = door_at(20 * GRID_SIZE, 25 * GRID_SIZE);
        const std::vector<walker*> fence = door_fence(15, {20});
        walker* door_b = fence[0];
        walker* bot = bot_at(20 * GRID_SIZE, 8 * GRID_SIZE);
        ASSERT_TRUE(door_a && door_b && bot);
        bot->set_foe(door_a);
        bot->find_path_to_foe();
        const std::vector<Cell> route = route_of(bot);
        ASSERT_FALSE(route.empty()) << "a route on both settings";
        EXPECT_EQ((Cell{20, 25, 0}), route.back()) << "the route ends in door A: " << text(route);
        if (fixes) {
            EXPECT_FALSE(holds(route, {20, 15, 0})) << "never through door B: " << text(route);
            int through_gap = 0;
            for (const Cell& c : route)
                through_gap += (c.y == 15 && c.x >= 36) ? 1 : 0;
            EXPECT_EQ(1, through_gap) << text(route);
        } else {
            EXPECT_TRUE(holds(route, {20, 15, 0})) << "classic: through door B: " << text(route);
        }
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, in a pen" : "Gameplay Fixes off, in a pen");
        fresh_world(fixes);
        const Cell gap = pen(10, 10, 14, 14);
        walker* door_b = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
        walker* door_a = door_at(12 * GRID_SIZE, 4 * GRID_SIZE);
        walker* bot = bot_at(12 * GRID_SIZE, 12 * GRID_SIZE);
        ASSERT_TRUE(door_a && door_b && bot);
        bot->set_foe(door_a);
        const og::reach::RoutePlan plan = og::reach::plan_route(*bot, Cell{12, 4, 0}, door_a);
        EXPECT_EQ(fixes != 0, plan.judged);
        bot->find_path_to_foe();
        if (!fixes) {
            ASSERT_FALSE(bot->path_to_foe.empty()) << "classic: a route through door B";
            EXPECT_TRUE(holds(route_of(bot), {12, 10, 0})) << text(route_of(bot));
            EXPECT_EQ((Cell{12, 4, 0}), route_of(bot).back()) << text(route_of(bot));
            continue;
        }
        ASSERT_GE(og::reach::region_of(*bot, og::reach::start_cell(*bot), Mode::Shut), 0)
            << "the staging: the orc's start cell has a region";
        ASSERT_FALSE(og::reach::walks_to(*bot, *door_a, Mode::Shut)) << "the staging: door B shuts the pen";
        EXPECT_FALSE(plan.run) << "the orc does not walk to door A: the search is not run";
        EXPECT_TRUE(bot->path_to_foe.empty()) << text(route_of(bot));
        std::vector<PathState> path;
        float cost = 0.0f;
        const auto state = [](Cell c) {
            return reinterpret_cast<PathState>(static_cast<intptr_t>(
                static_cast<intptr_t>(c.floor) * FLOOR_STRIDE + c.y * MAP_WIDTH + c.x));
        };
        ensure_pathfinding_state(*current_game)->solve_for(bot, state(plan.start), state(plan.goal), path, cost, &plan.cells);
        EXPECT_TRUE(path.empty()) << "the solve on the plan's cells could only fail";
    }
}

// The stair's landing (for_each_step's landing test, the shut layer). Two floors; an up-stair at
// cell (10,20) of floor 0 whose landing on floor 1 holds an enemy door; the orc on floor 0 at
// (5,20), its foe on floor 1 at (15,20). Setting on: a landing the door closes is no link: the orc
// does not walk to its foe with the doors shut (it does with them open), its route is empty, and the
// solve on the plan's cells, run anyway, plans no hop either. Setting off: the classic route climbs
// the stair.
// Fails when broken: for_each_step asking the stair's landing the terrain's test instead of its
// Cells (the shut layer links the stair: the orc walks to its foe).
TEST_F(SemBotReach, a_stair_whose_landing_a_door_closes_is_no_way_up)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        two_floors();
        tile(0, 10, 20, PIX_ZSTAIR_UP);
        tile(1, 10, 20, PIX_ZSTAIR_DOWN);
        walker* door = door_at(10 * GRID_SIZE, 20 * GRID_SIZE, kDoorTeam, 1, 1);
        walker* bot = bot_at(5 * GRID_SIZE, 20 * GRID_SIZE);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 15 * GRID_SIZE, 20 * GRID_SIZE, 16, 16, 1);
        ASSERT_TRUE(door && bot && foe);
        bot->set_foe(foe);
        bot->find_path_to_foe();
        if (!fixes) {
            EXPECT_TRUE(holds(route_of(bot), {10, 20, 1})) << "classic: up the stair: " << text(route_of(bot));
            continue;
        }
        EXPECT_FALSE(og::reach::walks_to(*bot, *foe, Mode::Shut)) << "the landing the door closes is no link";
        EXPECT_TRUE(og::reach::walks_to(*bot, *foe, Mode::Open)) << "with every door open the stair leads up";
        EXPECT_TRUE(bot->path_to_foe.empty()) << text(route_of(bot));
        const og::reach::RoutePlan plan = og::reach::plan_route(*bot, Cell{15, 20, 1}, foe);
        ASSERT_TRUE(plan.judged);
        std::vector<PathState> path;
        float cost = 0.0f;
        const auto state = [](Cell c) {
            return reinterpret_cast<PathState>(static_cast<intptr_t>(
                static_cast<intptr_t>(c.floor) * FLOOR_STRIDE + c.y * MAP_WIDTH + c.x));
        };
        ensure_pathfinding_state(*current_game)->solve_for(bot, state(plan.start), state(plan.goal), path, cost, &plan.cells);
        EXPECT_TRUE(path.empty()) << "the route plans no hop onto the closed landing";
    }
}

// The straight walk (statistics::direct_walk to the foe, direct_walk_to_point to a point, both
// through GameWorld::step_refused). The orc at (320,320); the foe, and the point, at (420,220), far
// past the orc's reach, so the ladder is diagonal up-right, then right, then up. Doors (16 x 16) at
// (336,304) up-right (only the diagonal step's box meets it, by the stepsize on each axis), (336,320)
// right and (320,304) up. The orc faces down, away from every step the ladder can take, so the step
// walkstep is asked for is not taken but faced: living::walk writes its direction into enddir (and
// turns one notch). Setting on: with the up-right door the walk faces right; with the right one too
// it faces up; with all three it asks for no step (enddir stays down) and reports failure. Setting
// off: the diagonal every time (the classic walk asks the terrain alone).
// Fails when broken: step_refused without its door term (the diagonal step with the setting on).
TEST_F(SemBotReach, the_straight_walk_takes_no_step_into_a_door_it_cannot_open)
{
    const short placed[3][2] = {{336, 304}, {336, 320}, {320, 304}};
    for (short fixes : {short{0}, short{1}}) {
        for (int to_point = 0; to_point < 2; ++to_point) {
            for (int n = 1; n <= 3; ++n) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             (to_point ? "to a point, " : "to the foe, ") + std::to_string(n) + " doors");
                fresh_world(fixes);
                walker* bot = bot_at(320, 320);
                walker* foe = living_at(FAMILY_SOLDIER, 2, 420, 220, 16, 16);
                ASSERT_TRUE(bot && foe);
                const float s = bot->stepsize();
                ASSERT_GE(s, 2.0f) << "the staging: a step meets a touching door by two pixels or more";
                for (int k = 0; k < n; ++k)
                    ASSERT_NE(nullptr, door_at(placed[k][0], placed[k][1]));
                bot->set_foe(foe);
                bot->set_curdir(static_cast<signed char>(FACE_DOWN));
                bot->set_enddir(static_cast<char>(FACE_DOWN));
                const bool walked = to_point ? bot->stats()->direct_walk_to_point(420, 220) : bot->stats()->direct_walk();
                const int asked = bot->enddir();
                if (!fixes) {
                    EXPECT_TRUE(walked);
                    EXPECT_EQ(FACE_UP_RIGHT, asked) << "classic: the diagonal step";
                } else if (n == 1) {
                    EXPECT_TRUE(walked);
                    EXPECT_EQ(FACE_RIGHT, asked) << "the right step";
                } else if (n == 2) {
                    EXPECT_TRUE(walked);
                    EXPECT_EQ(FACE_UP, asked) << "the up step";
                } else {
                    EXPECT_FALSE(walked) << "every straight step meets a door";
                    EXPECT_EQ(FACE_DOWN, asked) << "no step asked for";
                }
            }
        }
    }
}

// The straight walk adds no air test (it keeps its terrain test and adds the door term alone). Two
// floors and no door; the orc's diagonal step from (320,320) toward (420,220) sets its box corner in
// the cell (20,19) of floor 0 and its box's right column over (21,19): both cells are air, so an
// air test of the step's own cell (the route finder's, which reads the cell of the pixel it is
// given) and one of any cell the box covers both see air. On both settings, to the foe and to the
// point, the walk asks for the classic diagonal step onto it (read off enddir as in the test above;
// the route finder's step test refuses the step's cell to a walker that cannot fly, asserted with
// the setting on).
// Fails when broken: step_refused asking the route finder's air test of the step's cell (the air
// refuses the step with the setting on).
TEST_F(SemBotReach, on_a_level_with_floors_and_no_door_the_straight_walk_steps_onto_air_as_before)
{
    for (short fixes : {short{0}, short{1}}) {
        for (int to_point = 0; to_point < 2; ++to_point) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (to_point ? "to a point" : "to the foe"));
            fresh_world(fixes);
            two_floors();
            tile(0, 20, 19, PIX_AIR);
            tile(0, 21, 19, PIX_AIR);
            walker* bot = bot_at(320, 320);
            walker* foe = living_at(FAMILY_SOLDIER, 2, 420, 220, 16, 16);
            ASSERT_TRUE(bot && foe);
            const float s = bot->stepsize();
            const Cell stepped{static_cast<int>(320 + s) / GRID_SIZE, static_cast<int>(320 - s) / GRID_SIZE, 0};
            ASSERT_EQ((Cell{20, 19, 0}), stepped) << "the staging: the diagonal step's corner lies in an air cell";
            ASSERT_TRUE(world().query_grid_passable(320 + s, 320 - s, bot)) << "the staging: the terrain passes air";
            if (fixes) {
                ASSERT_FALSE(og::reach::cell_answer(*bot, stepped, Mode::Shut).step)
                    << "the staging: the route finder's step test refuses the step's air cell";
            }
            bot->set_foe(foe);
            bot->set_curdir(static_cast<signed char>(FACE_DOWN));
            bot->set_enddir(static_cast<char>(FACE_DOWN));
            const bool walked = to_point ? bot->stats()->direct_walk_to_point(420, 220) : bot->stats()->direct_walk();
            EXPECT_TRUE(walked);
            EXPECT_EQ(FACE_UP_RIGHT, static_cast<int>(bot->enddir())) << "the diagonal step";
        }
    }
}

// The push an ally gives (living::shove, through step_refused, asked of the pushed ally). Orc A
// shoves orc B, both of team 1, four pixels up, on a stream whose roll lets the shove proceed. B
// holds a SEARCH order. (a) B stands just under a door (touching it): setting on, B is a bot and the
// door bars it, so the shove is refused and B keeps its order; setting off, B is shoved (its order
// a WALK). (b) B with room (nothing above it): shoved on both settings. (c) B at the door is a seat's
// fighter (seat 0, not seated at the controls): the classic shove on both settings.
// Fails when broken: living::shove asking the terrain alone (B is shoved into the door with the
// setting on).
TEST_F(SemBotReach, a_shoved_bot_at_a_door_keeps_its_orders_and_a_seat_is_shoved_as_before)
{
    enum class B { AtTheDoor, WithRoom, ASeatAtTheDoor };
    for (short fixes : {short{0}, short{1}}) {
        for (const B b : {B::AtTheDoor, B::WithRoom, B::ASeatAtTheDoor}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (b == B::AtTheDoor ? "(a) at the door" : b == B::WithRoom ? "(b) with room" : "(c) a seat at the door"));
            fresh_world(fixes);
            ASSERT_NE(nullptr, door_at(320, 304));
            walker* a = bot_at(320, 400);
            walker* target = bot_at(b == B::WithRoom ? 480 : 320, 320);
            ASSERT_TRUE(a && target);
            if (b == B::ASeatAtTheDoor)
                target->set_user(0);
            ASSERT_NE(ACT_CONTROL, target->act_type()) << "the staging: the shove's own seat test lets it through";
            target->stats()->clear_command();
            target->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
            world().rng_.state_ = a_shoving_state();
            const short shoved = static_cast<living*>(a)->shove(target, short{0}, short{-4});
            const auto& orders = target->stats()->commands;
            ASSERT_FALSE(orders.empty());
            const bool refused = fixes && b == B::AtTheDoor;
            EXPECT_EQ(refused ? 0 : 1, shoved);
            EXPECT_EQ(refused ? COMMAND_SEARCH : COMMAND_WALK, orders.front().commandtype);
        }
    }
}

// statistics::level_written tells the walker its statistics belong to by their owner first, as
// statistics::mark_dirty finds it: a door whose statistics' controller link was cleared and whose
// level is then written still bumps its world's door count, so a map built before reads the change.
// Setting on.
// Fails when broken: level_written telling only the controller (the count stays).
TEST_F(SemBotReach, a_doors_level_written_after_its_controller_link_was_cleared_still_counts)
{
    walker* door = door_at(200, 200);
    ASSERT_NE(nullptr, door);
    door->stats()->set_controller(nullptr);
    const std::uint64_t before = world().reach_maps.door_count();
    door->stats()->set_level(5);
    EXPECT_EQ(before + 1, world().reach_maps.door_count());
    door->stats()->set_controller(door);
}

// Gameplay Fixes BI asks only "the setting is on and the walker is a bot" (door_is_a_wall_to), so a
// tower and a guard at its post are bots to it as any other bot is. (1) A tower (a stationary bot,
// 16 x 16) at cell (20,14) behind the door fence of the_route_goes_round_a_door_fence_and_never_through_it,
// its foe at (20,26): a tower's SEARCH order plans this route and its step only turns it along the
// route's first hop (walker::walkstep's stationary arm). Setting on: the route goes round by the
// gap and never enters a cell a door closes; setting off: through the fence. (2) A guard at its
// post (ACT_GUARD), holding a SEARCH order, standing just under a door, is shoved four pixels up by
// an ally on a stream whose roll lets the shove proceed: setting on, the shove is refused and the
// guard keeps its order; setting off, it is shoved (its order a WALK).
// Fails when broken: door_is_a_wall_to leaving out a stationary walker (the tower's route runs
// through the fence with the setting on); door_is_a_wall_to leaving out a guard at its post (the
// guard is shoved into the door with the setting on).
TEST_F(SemBotReach, a_tower_and_a_guard_at_its_post_are_bots_to_this_rule)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, a tower" : "Gameplay Fixes off, a tower");
        fresh_world(fixes);
        const std::vector<walker*> doors = door_fence(20, {18, 19, 20, 21});
        walker* tower = living_at(FAMILY_TOWER1, 1, 20 * GRID_SIZE, 14 * GRID_SIZE, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 20 * GRID_SIZE, 26 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(tower && foe && doors.size() == 4);
        ASSERT_TRUE(tower->stationary() && tower->is_bot()) << "the staging: a tower no seat drives";
        tower->set_foe(foe);
        tower->find_path_to_foe();
        const std::vector<Cell> route = route_of(tower);
        ASSERT_FALSE(route.empty()) << "the staging: a route on both settings";
        EXPECT_EQ((Cell{20, 26, 0}), route.back()) << text(route);
        int through_doors = 0;
        int through_gap = 0;
        for (const Cell& c : route) {
            through_doors += (c.y == 20 && c.x >= 18 && c.x <= 21) ? 1 : 0;
            through_gap += (c.y == 20 && c.x >= 36) ? 1 : 0;
        }
        EXPECT_EQ(fixes ? 0 : 1, through_doors) << text(route);
        EXPECT_EQ(fixes ? 1 : 0, through_gap) << text(route);
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, a guard at its post" : "Gameplay Fixes off, a guard at its post");
        fresh_world(fixes);
        ASSERT_NE(nullptr, door_at(320, 304));
        walker* a = bot_at(320, 400);
        walker* guard = bot_at(320, 320);
        ASSERT_TRUE(a && guard);
        guard->set_act_type(ACT_GUARD);
        ASSERT_TRUE(guard->is_bot()) << "the staging: a guard no seat drives";
        guard->stats()->clear_command();
        guard->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        world().rng_.state_ = a_shoving_state();
        const short shoved = static_cast<living*>(a)->shove(guard, short{0}, short{-4});
        const auto& orders = guard->stats()->commands;
        ASSERT_FALSE(orders.empty());
        EXPECT_EQ(fixes ? 0 : 1, shoved);
        EXPECT_EQ(fixes ? COMMAND_SEARCH : COMMAND_WALK, orders.front().commandtype);
    }
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BJ: a bot treats an enemy that only a locked door keeps it from as if it were
// not there (the searches, the top of a SEARCH and an ATTACK round, the near branch, a follower,
// the wander, a bot kept only by its shot, the cast rules). The same fixture: bots are level 4 with
// 400 hit points and 500 magic and no regeneration, so a blow, a shot or a cast shows in their
// counts. "Door-shut" is og::reach::door_shut.
// ---------------------------------------------------------------------------------------------

// The quick foe search (GameWorld::find_near_foe). An orc bot at (320,320); a soldier R (team 2) at
// cell (24,20), 64 px right, inside a ring of eight doors; a soldier F at (256,256), 128 px off in
// the open. The spiral reads the body map's 32 px buckets from the orc's outward, its own bucket
// never: R's bucket (12,10) is its 10th, F's (8,8) its 20th. A lone door with a cloak of 20 stands
// in bucket (9,10), the 5th, and another in bucket (8,10), the 18th; R wears the same cloak. The body
// map files a body in every bucket its box covers, its far edge included, so each body stands where
// its box (16 px from a bucket's corner) covers one bucket and is rolled for once. A cloak
// of 20 rolls rng.next(1): always 0, one draw; an uncloaked body rolls rng.next(0), which draws
// nothing (SimRandom), so the draws count the cloaked bodies the search rolled for. Setting off: R
// is answered after two draws (the first door's, R's). Setting on: only the ring keeps the orc from
// R (asserted), so R is passed over after its own roll and the search goes on to F: three draws.
// The same on a crowded map (200 more bodies of the orc's side, beyond the spiral's reach).
// Fails when broken: find_near_foe without its door term (R answered with the setting on).
TEST_F(SemBotReach, the_quick_search_passes_over_a_foe_in_a_ring_of_doors_after_its_cloak_roll)
{
    for (int crowded = 0; crowded < 2; ++crowded) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (crowded ? ", a crowded map" : ""));
            fresh_world(fixes);
            walker* orc = bot_at(320, 320);
            walker* ringed = living_at(FAMILY_SOLDIER, 2, 24 * GRID_SIZE, 20 * GRID_SIZE, 16, 16);
            const std::vector<walker*> ring = ring_of_doors(24, 20);
            walker* far_foe = living_at(FAMILY_SOLDIER, 2, 256, 256, 16, 16);
            walker* before = door_at(288, 320);
            walker* after = door_at(256, 320);
            ASSERT_TRUE(orc && ringed && far_foe && before && after);
            ASSERT_EQ(8u, ring.size());
            for (walker* d : ring) {
                ASSERT_NE(nullptr, d);
            }
            if (crowded) {
                ASSERT_NO_FATAL_FAILURE(crowd(200));
                ASSERT_GT(world().myobmap->size(), 200u);
            }
            ringed->set_invisibility_left(20);
            before->set_invisibility_left(20);
            after->set_invisibility_left(20);
            if (fixes) {
                ASSERT_TRUE(og::reach::door_shut(*orc, *ringed)) << "the staging: only the ring keeps the orc from R";
                ASSERT_FALSE(og::reach::door_shut(*orc, *far_foe)) << "the staging: F stands in the open";
            }
            const std::uint32_t s0 = world().rng_.state_;
            walker* found = world().find_near_foe(orc);
            const int draws = fixes ? 3 : 2;
            og::sim::SimRandom expect(s0);
            for (int k = 0; k < draws; ++k)
                (void)expect.next(1);
            EXPECT_EQ(fixes ? far_foe : ringed, found);
            EXPECT_EQ(expect.state_, world().rng_.state_) << "exactly " << draws << " cloak rolls";
        }
    }
}

// The full scan (GameWorld::find_nearest_foe) and its two picks (NearestFoePick). Two floors; an
// orc bot at (320,320) on floor 0; R inside a ring of doors at (384,320), 64 px; F in the open at
// (256,256), 128 px; G on floor 1 at (320,416), 96 px. G, R and F, in that list order, wear a cloak
// of 20: each rolls one draw that always reveals it, so the scan draws three on both settings (it
// rolls for every candidate before it offers it). A flying orc on a level with floors is answered
// its own floor's pick: off R, on F (R passed over for the own-floor pick). A walking orc is answered
// the any-floor pick: off R, on G (R passed over for that pick too; G is on another floor, which the
// door question does not judge). A third staging moves G to (320,352), 32 px, nearer than R, so R
// would replace only the flyer's own-floor pick: the flyer is still answered off R, on F.
// Fails when broken: NearestFoePick::offer without its door rule (R answered with the setting on);
// its door rule asked only of a candidate that would replace the any-floor pick (the third staging's
// flyer answered R).
TEST_F(SemBotReach, the_full_scan_passes_over_a_foe_in_a_ring_of_doors_for_both_picks)
{
    for (int staging = 0; staging < 3; ++staging) {
        const bool flying = staging > 0;
        const short g_y = staging == 2 ? 352 : 416;
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (!flying ? ", a walking orc" : staging == 1 ? ", a flying orc" : ", a flying orc, G nearer than R"));
            fresh_world(fixes);
            two_floors();
            walker* orc = bot_at(320, 320);
            walker* upstairs = living_at(FAMILY_SOLDIER, 2, 320, g_y, 16, 16, 1);
            walker* ringed = living_at(FAMILY_SOLDIER, 2, 24 * GRID_SIZE, 20 * GRID_SIZE, 16, 16);
            walker* far_foe = living_at(FAMILY_SOLDIER, 2, 256, 256, 16, 16);
            const std::vector<walker*> ring = ring_of_doors(24, 20);
            ASSERT_TRUE(orc && upstairs && ringed && far_foe);
            for (walker* d : ring) {
                ASSERT_NE(nullptr, d);
            }
            if (flying)
                orc->stats()->set_bit_flags(BIT_FLYING, 1);
            ASSERT_EQ(flying, orc->flies());
            for (walker* w : {upstairs, ringed, far_foe})
                w->set_invisibility_left(20);
            if (fixes) {
                ASSERT_TRUE(og::reach::door_shut(*orc, *ringed)) << "the staging: only the ring keeps the orc from R";
            }
            const std::uint32_t s0 = world().rng_.state_;
            walker* found = world().find_nearest_foe(orc);
            og::sim::SimRandom expect(s0);
            for (int k = 0; k < 3; ++k)
                (void)expect.next(1);
            EXPECT_EQ(expect.state_, world().rng_.state_) << "three cloak rolls on both settings";
            if (!fixes) {
                EXPECT_EQ(ringed, found);
            }
            else {
                EXPECT_EQ(flying ? far_foe : upstairs, found);
            }
        }
    }
}

// The top of a SEARCH and of an ATTACK round (statistics::keeps_foe): a foe handed to a bot without
// a search is let go there when only a locked door keeps the bot from it. A bot of team 1 in the pen
// (walls round cells 10..14, an enemy door in the gap at the top); a soldier of team 2 outside, 112
// px above it. Handed by a script (set_foe), or by the bot's own shot landing (an archer's arrow
// strikes the soldier: walker::attack makes the struck body its owner's foe). The bot holds the
// order (10 rounds), a leader of its side and a route of three cells. Setting on: after one round it
// holds no foe, no order, no leader and no route, stands where it stood, and the round drew nothing.
// Setting off: it still holds the soldier.
// Fails when broken: keeps_foe never letting a foe go (the foe kept with the setting on).
TEST_F(SemBotReach, a_handed_foe_only_a_door_keeps_the_bot_from_is_let_go_at_the_top_of_its_next_round)
{
    for (const int order : {COMMAND_SEARCH, COMMAND_ATTACK}) {
        for (int by_shot = 0; by_shot < 2; ++by_shot) {
            for (short fixes : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             (order == COMMAND_SEARCH ? "a SEARCH round, " : "an ATTACK round, ") +
                             (by_shot ? "handed by its own shot" : "handed by a script"));
                fresh_world(fixes);
                ASSERT_NE(nullptr, pen_with_a_door());
                walker* bot = living_at(by_shot ? FAMILY_ARCHER : FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
                walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
                walker* ally = living_at(FAMILY_ORC, 1, 11 * GRID_SIZE, 13 * GRID_SIZE, 16, 16);
                ASSERT_TRUE(bot && foe && ally);
                if (by_shot) {
                    walker* arrow = bot->create_weapon();
                    ASSERT_NE(nullptr, arrow);
                    ASSERT_TRUE(arrow->attack(foe)) << "the staging: the arrow lands";
                } else {
                    bot->set_foe(foe);
                }
                ASSERT_EQ(foe, bot->foe()) << "the staging: the foe was handed over";
                if (fixes) {
                    ASSERT_TRUE(og::reach::door_shut(*bot, *foe)) << "the staging: only the door keeps the bot from it";
                }
                bot->stats()->clear_command();
                bot->stats()->add_command(order, 10, 0, 0);
                bot->set_leader(ally);
                bot->path_to_foe.assign(3, PathState{});
                bot->set_ani_type(ANI_WALK);
                const short x0 = bot->xpos();
                const short y0 = bot->ypos();
                const std::uint32_t s0 = world().rng_.state_;
                bot->stats()->do_command();
                if (fixes) {
                    EXPECT_EQ(nullptr, bot->foe()) << "let go";
                    EXPECT_TRUE(bot->stats()->commands.empty()) << "its orders cleared";
                    EXPECT_EQ(nullptr, bot->leader()) << "its leader cleared";
                    EXPECT_TRUE(bot->path_to_foe.empty()) << "its route cleared";
                    EXPECT_EQ(x0, bot->xpos());
                    EXPECT_EQ(y0, bot->ypos());
                    EXPECT_EQ(s0, world().rng_.state_) << "the round drew nothing";
                } else {
                    EXPECT_EQ(foe, bot->foe()) << "classic: the foe is kept";
                }
            }
        }
    }
}

// The near branch of walk_to_foe (statistics::foes_not_door_shut). An orc bot at (320,320) holding a
// soldier H; a soldier R inside a ring of doors 64 px right. The round is a SEARCH round on a re-plan
// tick (the path counter at 0, a stream whose rng(300) is not 0). (1) H 96 px below: both settings
// take the near branch (H counts); the quick search passes over R with the setting on, so the bot
// keeps H and keeps its aim on it (enddir down: it faced up), where the classic bot takes R. (2) A
// crowded map (200 more bodies of its side) and H 200 px below: the near branch is asked for the
// crowd; R is the only foe within 100 px. Setting on: it counts no foe, its order ends and no ATTACK
// is queued, and it keeps H. Setting off: it takes R and queues an ATTACK.
// Fails when broken: foes_not_door_shut counting a door-shut foe (with the setting on the crowded
// orc queues an ATTACK).
TEST_F(SemBotReach, the_near_branch_counts_no_foe_only_a_door_keeps_the_bot_from)
{
    for (int crowded = 0; crowded < 2; ++crowded) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (crowded ? ", a crowded map, its foe 200 px off" : ", its foe 96 px off"));
            fresh_world(fixes);
            walker* orc = bot_at(320, 320);
            walker* held = living_at(FAMILY_SOLDIER, 2, 320, crowded ? 520 : 416, 16, 16);
            walker* ringed = living_at(FAMILY_SOLDIER, 2, 24 * GRID_SIZE, 20 * GRID_SIZE, 16, 16);
            const std::vector<walker*> ring = ring_of_doors(24, 20);
            ASSERT_TRUE(orc && held && ringed);
            for (walker* d : ring) {
                ASSERT_NE(nullptr, d);
            }
            if (crowded) {
                ASSERT_NO_FATAL_FAILURE(crowd(200));
            }
            ASSERT_EQ(crowded ? 200 : 96, orc->distance_to_ob(held));
            ASSERT_EQ(64, orc->distance_to_ob(ringed));
            if (fixes) {
                ASSERT_TRUE(og::reach::door_shut(*orc, *ringed));
                ASSERT_FALSE(og::reach::door_shut(*orc, *held));
            }
            orc->set_foe(held);
            orc->set_curdir(static_cast<signed char>(FACE_UP));
            orc->set_enddir(static_cast<char>(FACE_UP));
            orc->set_path_check_counter(0);
            orc->stats()->clear_command();
            orc->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
            world().rng_.state_ = a_state_whose_first_draw_of(300);
            orc->stats()->do_command();
            bool attack_queued = false;
            for (const auto& c : orc->stats()->commands)
                attack_queued = attack_queued || c.commandtype == COMMAND_ATTACK;
            if (!fixes) {
                EXPECT_EQ(ringed, orc->foe()) << "classic: the ringed soldier, the nearer";
                EXPECT_TRUE(attack_queued);
            } else if (!crowded) {
                EXPECT_EQ(held, orc->foe()) << "the near branch keeps its foe";
                EXPECT_TRUE(attack_queued);
                EXPECT_EQ(FACE_DOWN, static_cast<int>(orc->enddir())) << "its aim on the held soldier";
            } else {
                EXPECT_EQ(held, orc->foe());
                EXPECT_FALSE(attack_queued) << "no foe counted near: no ATTACK";
            }
        }
    }
}

// An ally following the player's fighter takes up its leader's foe (living::do_action, ACTION_FOLLOW)
// only when the foe is not door-shut for it. The player's fighter (team 0, seat 0) at (96,400)
// holds a soldier inside the pen (walls round cells 10..14, an enemy door in the gap); the follower,
// a bot of team 0, stands at (128,400) with no foe. Setting on: it takes no foe and queues its
// FOLLOW (the act is spent: 1). Setting off: it takes the soldier (the act goes on: 0).
// Fails when broken: do_action without its door term (the follower takes the soldier).
TEST_F(SemBotReach, a_follower_follows_instead_of_taking_its_leaders_foe_behind_a_door)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        ASSERT_NE(nullptr, pen_with_a_door());
        walker* inside = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* hero = living_at(FAMILY_SOLDIER, 0, 96, 400, 16, 16);
        walker* follower = living_at(FAMILY_ORC, 0, 128, 400, 16, 16);
        ASSERT_TRUE(inside && hero && follower);
        hero->set_user(0);
        hero->set_foe(inside);
        follower->set_action(ACTION_FOLLOW);
        if (fixes) {
            ASSERT_TRUE(og::reach::door_shut(*follower, *inside)) << "the staging: only the door keeps it from the soldier";
        }
        const bool spent = static_cast<living*>(follower)->do_action();
        EXPECT_EQ(hero, follower->leader());
        if (fixes) {
            EXPECT_TRUE(spent);
            EXPECT_EQ(nullptr, follower->foe());
            ASSERT_FALSE(follower->stats()->commands.empty());
            EXPECT_EQ(COMMAND_FOLLOW, follower->stats()->commands.front().commandtype);
        } else {
            EXPECT_FALSE(spent);
            EXPECT_EQ(inside, follower->foe());
        }
    }
}

// A room that is opened. The room's interior cells x 16..29, y 8..15, walls
// round it; a door of a third side (team 2, level 3) in its bottom wall at cell (25,16); twelve orcs
// (team 1) inside at x 368..464 by y 160..224; four crew bots (team 0: elf, archer, cleric,
// barbarian) 160 px below the door at y 432. The door is killed at tick 200 by its own death path.
// Setting on, after every tick to 199: no bot holds a foe, none lost a hit point or a magic point
// (nobody struck, shot or cast at anybody), the door is whole, and every bot stands where it was put
// (each is shut out: it queues no wander). On the door's death the maps join the two sides (every
// orc walks to every crew bot, none is door-shut). After tick 201 every live bot with no leader
// holds a foe (the tick's hand-out). Setting off: after tick 1 every bot holds a foe already.
// Fails when broken: the full scan offering a door-shut foe (with the setting on the hand-out gives
// every bot a foe on tick 1).
TEST_F(SemBotReach, a_room_opened_by_its_doors_death_joins_both_sides_on_the_next_tick)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        room(16, 8, 29, 15);
        tile(0, 25, 16, PIX_GRASS1);
        walker* door = door_at(400, 256, 2, 3);
        ASSERT_NE(nullptr, door);
        std::vector<walker*> orcs, crew;
        for (short y : {short{160}, short{192}, short{224}})
            for (short x : {short{368}, short{400}, short{432}, short{464}})
                orcs.push_back(scene_bot(FAMILY_ORC, 1, x, y));
        const int crew_family[4] = {FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN};
        for (int i = 0; i < 4; ++i)
            crew.push_back(scene_bot(crew_family[i], 0, static_cast<short>(352 + 32 * i), 432));
        std::vector<walker*> all = orcs;
        all.insert(all.end(), crew.begin(), crew.end());
        std::vector<std::pair<short, short>> spot;
        for (walker* w : all) {
            ASSERT_NE(nullptr, w);
            spot.emplace_back(w->xpos(), w->ypos());
        }
        const float door_hp = door->stats()->hitpoints();
        if (!fixes) {
            world().tick();
            for (walker* w : all) {
                EXPECT_TRUE(w->foe() != nullptr && !w->is_friendly(w->foe())) << "classic: every bot holds a foe after tick 1";
            }
            continue;
        }
        for (int tick = 1; tick <= 199; ++tick) {
            world().tick();
            for (std::size_t i = 0; i < all.size(); ++i) {
                walker* w = all[i];
                ASSERT_EQ(nullptr, w->foe()) << "tick " << tick << ", bot " << i;
                ASSERT_EQ(400.0f, w->stats()->hitpoints()) << "tick " << tick << ", bot " << i;
                ASSERT_EQ(500.0f, w->stats()->magicpoints()) << "tick " << tick << ", bot " << i;
                ASSERT_EQ(spot[i].first, w->xpos()) << "tick " << tick << ", bot " << i;
                ASSERT_EQ(spot[i].second, w->ypos()) << "tick " << tick << ", bot " << i;
            }
            ASSERT_FALSE(door->dead());
            ASSERT_EQ(door_hp, door->stats()->hitpoints()) << "tick " << tick;
        }
        world().tick();  // tick 200
        door->set_dead(1);
        door->death();
        for (walker* o : orcs)
            for (walker* c : crew) {
                EXPECT_TRUE(og::reach::walks_to(*o, *c, Mode::Shut));
                EXPECT_FALSE(og::reach::door_shut(*o, *c));
            }
        world().tick();  // tick 201
        std::size_t judged = 0;
        for (std::size_t i = 0; i < all.size(); ++i) {
            walker* w = all[i];
            if (w->dead() || w->leader() != nullptr)
                continue;
            ++judged;
            EXPECT_TRUE(w->foe() != nullptr && !w->foe()->dead()) << "bot " << i << " holds a foe after tick 201";
        }
        EXPECT_EQ(all.size(), judged) << "every bot is judged: none had lost a hit point by tick 199, the sides stand "
                                         "208 px apart, and nobody gives a leader";
    }
}

// A chase to a door. The orc's room: interior cells x 20..27, y 6..11, walls
// round it, its own side's door (team 1) at cell (23,12). A hurt orc (team 1, 100 hit points) at
// (368,288) flees straight up through its door (a 54-round walk); a crew orc (team 0, melee only) at
// (368,424) chases it; each holds the other. P is the first tick after which only the door keeps the
// crew orc from the orc (door_shut, asked after the tick). Before P the crew orc holds the orc after
// every tick. Setting on: after tick P it holds no foe, and it stands where it stood after tick P - 1
// (it let go at the top of that tick's round, and took no step). Setting off: it still holds the orc.
// Fails when broken: keeps_foe never letting a foe go (the crew orc holds the orc after tick P).
TEST_F(SemBotReach, a_chaser_lets_its_foe_go_on_the_round_after_the_foe_passes_its_own_door)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, seed " : "Gameplay Fixes off, seed ") + std::to_string(seed));
            fresh_world(fixes);
            room(20, 6, 27, 11);
            tile(0, 23, 12, PIX_GRASS1);
            ASSERT_NE(nullptr, door_at(368, 192, 1, 3));
            walker* orc = scene_bot(FAMILY_ORC, 1, 368, 288);
            walker* chaser = scene_bot(FAMILY_ORC, 0, 368, 424);
            ASSERT_TRUE(orc && chaser);
            orc->stats()->set_hitpoints(100.0f);
            orc->set_foe(chaser);
            chaser->set_foe(orc);
            orc->stats()->force_command(COMMAND_WALK, 54, 0, -1);
            world().rng_.state_ = seed;
            int passed = -1;
            short x_before = chaser->xpos();
            short y_before = chaser->ypos();
            for (int tick = 1; tick <= 120 && passed < 0; ++tick) {
                x_before = chaser->xpos();
                y_before = chaser->ypos();
                world().tick();
                world().gameplay_fixes = 1;
                const bool shut = og::reach::door_shut(*chaser, *orc);
                world().gameplay_fixes = fixes;
                if (shut)
                    passed = tick;
                else {
                    ASSERT_EQ(orc, chaser->foe()) << "before the orc passes its door, tick " << tick;
                }
            }
            ASSERT_GT(passed, 0) << "the staging: the orc passes its door within 120 ticks";
            if (fixes) {
                EXPECT_EQ(nullptr, chaser->foe()) << "let go on tick " << passed;
                EXPECT_EQ(x_before, chaser->xpos());
                EXPECT_EQ(y_before, chaser->ypos());
            } else {
                EXPECT_EQ(orc, chaser->foe());
            }
        }
    }
}

// Archers behind a door. The room: interior cells x 18..27, y 8..13,
// walls round it; a door of a third side (team 2) in its bottom wall at cell (23,14). Two archers
// (team 1) inside at (368,144) and (320,144); the crew (team 0) on row 288: archers at x 368 (the
// door's column) and 320, orcs at 400 and 288. 600 ticks. With the plain wall every enemy of every
// bot is door-shut (asserted for the two pairs of archers in line), so with the setting on, after
// every tick, nobody holds a foe, owns a shot in flight, lost a hit point or a magic point, nor
// stands anywhere but where it was put, and the door is whole. With the wall cell (20,14) an arrow
// slit (PIX_WALL_ARROW_GRASS) between the two archers at x 320, each holds the other only by its
// shot (asserted: Exit::Shot both ways): every bot still stands where it was put for 600 ticks (a
// bot kept only by its shot takes no step), and the two slit archers each shoot (a shot of its own
// is in flight after some tick). Setting off, the plain door: the archers shoot (a shot in flight).
// Fails when broken: the quick search, the full scan or the keep taking a door-shut foe (with the
// plain wall a bot holds a foe); the cast rule naming nobody (the plain door's archers fire rings at
// nobody: magic spent); og::reach's shot exit left out (the slit archers let each other go and never
// shoot).
TEST_F(SemBotReach, archers_behind_a_door_shoot_only_through_an_arrow_slit_and_never_step)
{
    for (int slit = 0; slit < 2; ++slit) {
        for (short fixes : {short{0}, short{1}}) {
            if (slit && !fixes)
                continue;
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") + (slit ? ", an arrow slit" : ", a plain wall"));
            fresh_world(fixes);
            world().rng_.state_ = 42u;
            room(18, 8, 27, 13);
            tile(0, 23, 14, PIX_GRASS1);
            if (slit)
                tile(0, 20, 14, PIX_WALL_ARROW_GRASS);
            walker* door = door_at(368, 224, 2, 3);
            walker* in1 = scene_bot(FAMILY_ARCHER, 1, 368, 144);
            walker* in2 = scene_bot(FAMILY_ARCHER, 1, 320, 144);
            walker* ca1 = scene_bot(FAMILY_ARCHER, 0, 368, 288);
            walker* ca2 = scene_bot(FAMILY_ARCHER, 0, 320, 288);
            walker* cm1 = scene_bot(FAMILY_ORC, 0, 400, 288);
            walker* cm2 = scene_bot(FAMILY_ORC, 0, 288, 288);
            ASSERT_TRUE(door && in1 && in2 && ca1 && ca2 && cm1 && cm2);
            const std::vector<walker*> all = {in1, in2, ca1, ca2, cm1, cm2};
            std::vector<std::pair<short, short>> spot;
            for (walker* w : all)
                spot.emplace_back(w->xpos(), w->ypos());
            const float door_hp = door->stats()->hitpoints();
            if (fixes) {
                ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*in1, *ca1)) << "the staging: the door's column";
                EXPECT_EQ(slit ? Exit::Shot : Exit::DoorShut, og::reach::door_shut_exit(*in2, *ca2)) << "the staging";
                EXPECT_EQ(slit ? Exit::Shot : Exit::DoorShut, og::reach::door_shut_exit(*ca2, *in2)) << "the staging";
            }
            bool in2_shot = false, ca2_shot = false, any_shot = false;
            for (int tick = 1; tick <= 600; ++tick) {
                world().tick();
                any_shot = any_shot || weapons_owned_by(all) > 0;
                in2_shot = in2_shot || weapons_owned_by({in2}) > 0;
                ca2_shot = ca2_shot || weapons_owned_by({ca2}) > 0;
                if (!fixes)
                    continue;
                for (std::size_t i = 0; i < all.size(); ++i) {
                    ASSERT_EQ(spot[i].first, all[i]->xpos()) << "tick " << tick << ", bot " << i;
                    ASSERT_EQ(spot[i].second, all[i]->ypos()) << "tick " << tick << ", bot " << i;
                    if (slit)
                        continue;
                    ASSERT_EQ(nullptr, all[i]->foe()) << "tick " << tick << ", bot " << i;
                    ASSERT_EQ(400.0f, all[i]->stats()->hitpoints()) << "tick " << tick << ", bot " << i;
                    ASSERT_EQ(500.0f, all[i]->stats()->magicpoints()) << "tick " << tick << ", bot " << i;
                }
                if (!slit) {
                    ASSERT_EQ(0, weapons_owned_by(all)) << "tick " << tick;
                    ASSERT_EQ(door_hp, door->stats()->hitpoints()) << "tick " << tick;
                }
            }
            if (!fixes) {
                EXPECT_TRUE(any_shot) << "classic: the archers shoot through the door";
            } else if (slit) {
                EXPECT_TRUE(in2_shot) << "the archer inside shoots through the slit";
                EXPECT_TRUE(ca2_shot) << "the crew's archer shoots through the slit";
            }
        }
    }
}

// A bot kept only by its shot (statistics::search_round and attack_step). An island ringed by water
// two cells wide (cells 4..14), its one bridge at (9,4)..(9,5) holding an enemy door; a soldier of
// team 2 on the island at (144,144); an archer bot of team 1 on the shore at (144,256), 112 px below,
// its line of sight 12 (the arrow's reach above 112 and the soldier inside its firing box: asserted).
// Only the door keeps the archer from the soldier on foot, and its arrow lands across the water
// (Exit::Shot, asserted). (a) A SEARCH round, the archer facing down (away): setting on, it faces up
// at once (face_delta, before the shot test), takes no step, passes the shot test and holds an
// ATTACK of 30+rng(25) rounds, one spent (29 to 53); setting off, the classic round neither faces
// it at once nor stands (it is not facing up afterwards). (b) An ATTACK round, the archer facing up
// with no magic for its arrow (cost 5): fire_check refuses for the magic; setting on, it takes no
// step and keeps its facing; setting off, it steps up toward the soldier.
// Fails when broken: keeps_foe answering Kept for a foe held only by the shot (the SEARCH round walks
// instead of facing); attack_step without its shot term (the ATTACK round steps).
TEST_F(SemBotReach, a_bot_kept_only_by_its_shot_faces_its_foe_and_shoots_without_a_step)
{
    for (int attack_round = 0; attack_round < 2; ++attack_round) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (attack_round ? "(b) an ATTACK round" : "(a) a SEARCH round"));
            fresh_world(fixes);
            for (int x = 4; x <= 14; ++x)
                for (int y = 4; y <= 14; ++y)
                    if (x == 4 || x == 14 || y == 4 || y == 14 || x == 5 || x == 13 || y == 5 || y == 13)
                        tile(0, x, y, PIX_WATER1);
            tile(0, 9, 4, PIX_GRASS1);
            tile(0, 9, 5, PIX_GRASS1);
            walker* bridge_door = door_at(9 * GRID_SIZE, 4 * GRID_SIZE);
            walker* islander = living_at(FAMILY_SOLDIER, 2, 144, 144, 16, 16);
            walker* archer = living_at(FAMILY_ARCHER, 1, 144, 256, 16, 16);
            ASSERT_TRUE(bridge_door && islander && archer);
            archer->set_lineofsight(12);
            weap test_weapon(walker::WeaponProfileProbeTag{});
            ASSERT_TRUE(archer->unhooked_weapon(test_weapon, 0.0f, -1.0f));
            ASSERT_GT(walker::weapon_reach(test_weapon), 112) << "the staging: the soldier is in reach";
            ASSERT_TRUE(archer->in_firing_box(0, -112));
            world().gameplay_fixes = 1;
            ASSERT_EQ(Exit::Shot, og::reach::door_shut_exit(*archer, *islander)) << "the staging";
            world().gameplay_fixes = fixes;
            archer->set_foe(islander);
            archer->stats()->clear_command();
            archer->set_ani_type(ANI_WALK);
            if (!attack_round) {
                archer->face_delta(0, 1);
                archer->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
            } else {
                archer->face_delta(0, -1);
                archer->stats()->set_weapon_cost(5);
                archer->stats()->set_magicpoints(0.0f);
                archer->stats()->add_command(COMMAND_ATTACK, 10, 0, 0);
            }
            const short x0 = archer->xpos();
            const short y0 = archer->ypos();
            archer->stats()->do_command();
            const auto& orders = archer->stats()->commands;
            if (!attack_round) {
                if (fixes) {
                    EXPECT_EQ(FACE_UP, static_cast<int>(archer->curdir())) << "faced at once";
                    EXPECT_EQ(x0, archer->xpos());
                    EXPECT_EQ(y0, archer->ypos());
                    ASSERT_FALSE(orders.empty());
                    EXPECT_EQ(COMMAND_ATTACK, orders.front().commandtype) << "the shot test passed";
                    EXPECT_GE(orders.front().commandcount, 29);
                    EXPECT_LE(orders.front().commandcount, 53);
                } else {
                    EXPECT_NE(FACE_UP, static_cast<int>(archer->curdir())) << "classic: not faced at once";
                }
            } else {
                if (fixes) {
                    EXPECT_EQ(x0, archer->xpos());
                    EXPECT_EQ(y0, archer->ypos()) << "no step";
                    EXPECT_EQ(FACE_UP, static_cast<int>(archer->curdir()));
                } else {
                    EXPECT_LT(archer->ypos(), y0) << "classic: the step toward the soldier";
                }
            }
        }
    }
}

// The judging flight of og::reach::shoots_at on a painted map, setting on. An archer bot of team 1
// at (160,320), line of sight 12, a soldier of team 2 straight above it at (160,224), 96 px, across
// the row of cells 17 (y 272), which is painted per case: a wall with an arrow slit at (10,17) on
// the line (the check takes the slit as open: yes) and the same wall plain (no); water with a door
// standing on a grass cell of the water row one cell beside the line, at (11,17) (yes: water passes
// a shot and the door is off the line) and on the line, at (10,17) (no: every door stops it); open
// grass with the soldier exactly at the arrow's reach above the archer (yes) and one pixel past it
// (no). In both water rows the door is the archer's own foe, which the real line check passes, so
// only every_door_stops stops the judging flight at the door on the line.
// Fails when broken: shoots_at flying with every_door_stops false (the door on the line, the
// archer's foe, passes).
TEST_F(SemBotReach, the_shot_term_takes_an_arrow_slit_and_water_and_stops_at_a_door_on_its_line)
{
    enum class Row { Slit, PlainWall, WaterDoorBeside, WaterDoorOnLine };
    for (const Row row : {Row::Slit, Row::PlainWall, Row::WaterDoorBeside, Row::WaterDoorOnLine}) {
        SCOPED_TRACE(row == Row::Slit ? "a slit" : row == Row::PlainWall ? "a plain wall" : row == Row::WaterDoorBeside ? "water, a door beside the line" : "water, a door on the line");
        fresh_world(1);
        const bool water = row == Row::WaterDoorBeside || row == Row::WaterDoorOnLine;
        wall_row(17, 0, 39, water ? PIX_WATER1 : PIX_H_WALL1);
        if (row == Row::Slit)
            tile(0, 10, 17, PIX_WALL_ARROW_GRASS);
        walker* door = nullptr;
        if (water) {
            const int door_cell = row == Row::WaterDoorBeside ? 11 : 10;
            tile(0, door_cell, 17, PIX_GRASS1);
            door = door_at(static_cast<short>(door_cell * GRID_SIZE), 17 * GRID_SIZE);
            ASSERT_NE(nullptr, door);
        }
        walker* archer = living_at(FAMILY_ARCHER, 1, 160, 320, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 160, 224, 16, 16);
        ASSERT_TRUE(archer && foe);
        archer->set_lineofsight(12);
        if (door != nullptr)
            archer->set_foe(door);
        const bool yes = row == Row::Slit || row == Row::WaterDoorBeside;
        EXPECT_EQ(yes, og::reach::shoots_at(*archer, *foe));
    }
    fresh_world(1);
    walker* archer = living_at(FAMILY_ARCHER, 1, 160, 600, 16, 16);
    ASSERT_NE(nullptr, archer);
    archer->set_lineofsight(12);
    weap test_weapon(walker::WeaponProfileProbeTag{});
    ASSERT_TRUE(archer->unhooked_weapon(test_weapon, 0.0f, -1.0f));
    const int reach = walker::weapon_reach(test_weapon);
    ASSERT_GT(reach, 0);
    ASSERT_LT(reach, 600) << "the staging: the reach ends on the field";
    walker* at_reach = living_at(FAMILY_SOLDIER, 2, 160, static_cast<short>(600 - reach), 16, 16);
    ASSERT_NE(nullptr, at_reach);
    ASSERT_TRUE(archer->in_firing_box(0, static_cast<short>(-reach)));
    EXPECT_TRUE(og::reach::shoots_at(*archer, *at_reach)) << "the reach's end";
    at_reach->setxy(static_cast<short>(160), static_cast<short>(600 - reach - 1));
    EXPECT_FALSE(og::reach::shoots_at(*archer, *at_reach)) << "one pixel past it";
}

// The cast rules (living::cast_rule; GameWorld::find_foes_in_range while cast_rule_for names the
// bot). A soldier of team 2, 64 px right of the caster, inside a ring of doors of team 3 (or, as a
// control, of the caster's own team 1: no door bars it). (1) An archer bot facing down, its FIRE
// ARROWS slot: setting on, with the enemy ring its gate (one foe it does not face in the fire arrow's
// flight pays) counts no foe and says no; with its own side's ring, yes; setting off (the classic
// gate: a foe within 130), yes. (2) A mage bot, TELEPORT (yes when no foe within 110 or more than
// three): on, yes; off, no. (3) The range finder asked outside any cast rule counts the ringed
// soldier on both settings; inside the archer's scope it does not (on); inside another walker's
// scope the archer's own question still counts it. (4) The cast made strikes whom it struck before:
// a level-12 cleric bot on its slot 2 (TURN UNDEAD with the Shift, reach 48 px, 60 with the setting
// on), a level-1 skeleton of team 2 48 px right in a ring of enemy doors. Its gate asks the range
// finder for undead to turn (walker::undead_foes_in_range): setting on, it counts none, takes the
// raise, finds no corpse and says no; off, yes (classic). Then the cast asked directly with the Shift
// down, from a stream whose first two draws are the turn's rolls and the cleric's wins (rng(1920)
// above rng(10); the strength is 160 per cleric level on both settings): the range finder of
// walker::turn_undead is asked outside any cast rule, so on both settings the cast is made and the
// ringed skeleton is turned. (5) A seat's archer (seat 0) is asked with nobody named: yes with the
// enemy ring, setting on.
// Fails when broken: cast_rule_leaves_out never leaving a candidate out, or cast_rule naming nobody
// (the archer's gate says yes with the setting on); the bot kept named while its cast runs (the
// cleric's cast finds no foe in range and the skeleton stands).
TEST_F(SemBotReach, a_cast_rule_counts_no_foe_only_a_door_keeps_the_bot_from)
{
    const auto stage = [&](short fixes, int family, unsigned char ring_team, walker*& caster, walker*& foe) {
        fresh_world(fixes);
        caster = living_at(family, 1, 320, 320, 16, 16);
        foe = living_at(FAMILY_SOLDIER, 2, 24 * GRID_SIZE, 20 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(caster && foe);
        for (walker* d : ring_of_doors(24, 20)) {
            ASSERT_NE(nullptr, d);
            d->set_team_num(ring_team);
        }
        caster->face_delta(0, 1);
        caster->set_current_special(1);
        caster->set_ani_type(ANI_WALK);
        caster->set_busy(0.0f);
    };
    walker* caster = nullptr;
    walker* foe = nullptr;
    for (short fixes : {short{0}, short{1}}) {
        for (unsigned char ring_team : {kDoorTeam, static_cast<unsigned char>(1)}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") + (ring_team == 1 ? ", its own side's ring" : ", an enemy ring"));
            ASSERT_NO_FATAL_FAILURE(stage(fixes, FAMILY_ARCHER, ring_team, caster, foe));
            ASSERT_EQ(fixes && ring_team == kDoorTeam, og::reach::door_shut(*caster, *foe)) << "the staging";
            EXPECT_EQ(!(fixes && ring_team == kDoorTeam), caster->check_special()) << "(1) FIRE ARROWS";
            EXPECT_EQ(nullptr, world().cast_rule_for) << "the scope is gone after the call";
        }
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, (2) TELEPORT" : "Gameplay Fixes off, (2) TELEPORT");
        ASSERT_NO_FATAL_FAILURE(stage(fixes, FAMILY_MAGE, kDoorTeam, caster, foe));
        EXPECT_EQ(fixes != 0, caster->check_special());
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, (3) the range finder" : "Gameplay Fixes off, (3) the range finder");
        ASSERT_NO_FATAL_FAILURE(stage(fixes, FAMILY_ARCHER, kDoorTeam, caster, foe));
        walker* other = living_at(FAMILY_ORC, 1, 100, 100, 16, 16);
        ASSERT_NE(nullptr, other);
        std::int32_t n = -1;
        (void)world().find_foes_in_range(world().oblist, 110, &n, caster);
        EXPECT_EQ(1, n) << "outside any cast rule";
        {
            const GameWorld::CastRuleScope named(world(), caster);
            const std::list<walker*> found = world().find_foes_in_range(world().oblist, 110, &n, caster);
            EXPECT_EQ(fixes ? 0 : 1, n) << "inside the archer's own scope";
            EXPECT_EQ(static_cast<std::size_t>(n), found.size());
        }
        {
            const GameWorld::CastRuleScope named(world(), other);
            (void)world().find_foes_in_range(world().oblist, 110, &n, caster);
            EXPECT_EQ(1, n) << "inside another walker's scope";
        }
        EXPECT_EQ(nullptr, world().cast_rule_for);
    }
    const auto turning_state = [] {
        for (std::uint32_t st = 1;; ++st) {
            og::sim::SimRandom r(st);
            const std::uint32_t caster_roll = r.next(1920);
            if (caster_roll > r.next(10))
                return st;
        }
    };
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on, (4) TURN UNDEAD" : "Gameplay Fixes off, (4) TURN UNDEAD");
        fresh_world(fixes);
        walker* cleric = living_at(FAMILY_CLERIC, 1, 320, 320, 16, 16);
        walker* skeleton = living_at(FAMILY_SKELETON, 2, 23 * GRID_SIZE, 20 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(cleric && skeleton);
        for (walker* d : ring_of_doors(23, 20)) {
            ASSERT_NE(nullptr, d);
        }
        cleric->stats()->set_level(12);
        skeleton->stats()->set_level(1);
        cleric->face_delta(0, 1);
        cleric->set_ani_type(ANI_WALK);
        cleric->set_busy(0.0f);
        cleric->set_current_special(2);
        ASSERT_EQ(fixes != 0, og::reach::door_shut(*cleric, *skeleton)) << "the staging";
        EXPECT_EQ(fixes == 0, cleric->check_special()) << "(4) the gate";
        ASSERT_EQ(2, static_cast<int>(cleric->current_special())) << "the staging: slot 2 is paid for";
        cleric->set_shifter_down(1);
        world().rng_.state_ = turning_state();
        EXPECT_TRUE(cleric->special()) << "(4) the cast is made";
        EXPECT_TRUE(skeleton->dead()) << "(4) the ringed skeleton is turned";
    }
    ASSERT_NO_FATAL_FAILURE(stage(1, FAMILY_ARCHER, kDoorTeam, caster, foe));
    caster->set_user(0);
    EXPECT_TRUE(caster->check_special()) << "(5) a seat's archer: nobody is named";
}

// A seat's fighter is never asked (og::reach::asks): an orc of team 1 driven by seat 0 inside the
// pen (an enemy door in its gap), a soldier of team 2 outside, setting on and off alike: the quick
// search and the full scan answer the soldier, a SEARCH round keeps it, and with no foe its wander
// is queued.
// Fails when broken: asks taking a seat's fighter for a bot (the searches pass over the soldier).
TEST_F(SemBotReach, a_seats_fighter_is_never_asked)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        ASSERT_NE(nullptr, pen_with_a_door());
        walker* seat = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(seat && foe);
        seat->set_user(0);
        EXPECT_EQ(Exit::NotAsked, og::reach::door_shut_exit(*seat, *foe));
        EXPECT_EQ(foe, world().find_near_foe(seat));
        EXPECT_EQ(foe, world().find_nearest_foe(seat));
        seat->set_foe(foe);
        seat->stats()->clear_command();
        seat->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        seat->set_ani_type(ANI_WALK);
        seat->stats()->do_command();
        EXPECT_EQ(foe, seat->foe()) << "its SEARCH round keeps the soldier";
        seat->set_foe(nullptr);
        seat->stats()->clear_command();
        (void)seat->stats()->idle_walk(*seat, 20);
        ASSERT_FALSE(seat->stats()->commands.empty());
        EXPECT_EQ(COMMAND_WALK, seat->stats()->commands.front().commandtype) << "its wander is queued";
    }
}

// A guard at its post keeps the classic search (og::reach::asks leaves it out): a guard bot (team 1,
// ACT_GUARD) inside the pen, a soldier of team 2 outside at (192,64) (its box in one bucket of the
// body map) wearing a cloak of 20 (one draw that always reveals it). On both settings the quick
// search and the full scan answer the soldier, each after that one draw. An ordinary bot on the same
// spot with the setting on is answered nobody (the soldier is door-shut for it): the full scan after
// its one draw, the quick search after two (the soldier's roll in the spiral, which then goes on and
// falls back to the full scan, which rolls for it again).
// Fails when broken: asks taking a guard at its post (the guard is answered nobody).
TEST_F(SemBotReach, a_guard_at_its_post_keeps_the_classic_search)
{
    for (int guard = 0; guard < 2; ++guard) {
        for (short fixes : {short{0}, short{1}}) {
            if (!guard && !fixes)
                continue;
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") + (guard ? ", a guard at its post" : ", an ordinary bot"));
            fresh_world(fixes);
            ASSERT_NE(nullptr, pen_with_a_door());
            walker* bot = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
            walker* foe = living_at(FAMILY_SOLDIER, 2, 192, 64, 16, 16);
            ASSERT_TRUE(bot && foe);
            if (guard)
                bot->set_act_type(ACT_GUARD);
            foe->set_invisibility_left(20);
            for (int search = 0; search < 2; ++search) {
                const std::uint32_t s0 = world().rng_.state_;
                walker* found = search == 0 ? world().find_near_foe(bot) : world().find_nearest_foe(bot);
                og::sim::SimRandom expect(s0);
                const int draws = (search == 0 && !guard) ? 2 : 1;
                for (int k = 0; k < draws; ++k)
                    (void)expect.next(1);
                EXPECT_EQ(guard ? foe : nullptr, found) << (search == 0 ? "the quick search" : "the full scan");
                EXPECT_EQ(expect.state_, world().rng_.state_) << draws << " cloak rolls";
            }
        }
    }
}

// A foe with a way round is kept and routed to: the door fence (a wall across row 20 from x 0 to 35,
// doors in its cells 18 to 21, the way round by cells 36 to 39); an orc bot at cell (20,14), a
// soldier at (20,26). Setting on: the soldier is not door-shut (the orc walks round), the quick
// search answers it, and the orc's SEARCH round on a re-plan tick keeps it and plans its route round
// the fence by the gap.
// Fails when broken: door_shut_exit without its WalksShut exit (the soldier read door-shut).
TEST_F(SemBotReach, a_foe_with_a_way_round_is_kept_and_routed_to)
{
    const std::vector<walker*> doors = door_fence(20, {18, 19, 20, 21});
    walker* orc = bot_at(20 * GRID_SIZE, 14 * GRID_SIZE);
    walker* foe = living_at(FAMILY_SOLDIER, 2, 20 * GRID_SIZE, 26 * GRID_SIZE, 16, 16);
    ASSERT_TRUE(orc && foe);
    EXPECT_EQ(Exit::WalksShut, og::reach::door_shut_exit(*orc, *foe));
    EXPECT_EQ(foe, world().find_near_foe(orc));
    orc->set_foe(foe);
    orc->set_path_check_counter(0);
    orc->stats()->clear_command();
    orc->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
    orc->set_ani_type(ANI_WALK);
    world().rng_.state_ = a_state_whose_first_draw_of(300);
    orc->stats()->do_command();
    EXPECT_EQ(foe, orc->foe());
    const std::vector<Cell> route = route_of(orc);
    ASSERT_FALSE(route.empty()) << "a route was planned";
    int through_gap = 0;
    for (const Cell& c : route)
        through_gap += (c.y == 20 && c.x >= 36) ? 1 : 0;
    EXPECT_EQ(1, through_gap) << "round the fence by the gap: " << text(route);
}

// A foe behind a wall keeps the classic walk step for step: an orc bot of team 1 in a closed box of
// walls (cells 10..14, no gap), a soldier of team 2 outside at (192,80); with every door open there
// is still no way, so the soldier is no door's (NoWayOpen, asserted). The world holds a door of a
// third side far off at (560,560); its twin is the same world without the door. Setting on, 120
// ticks each: after every tick both bots stand where their twins stand and the stream is the twin's.
// Fails when broken: door_shut_exit without its NoWayOpen exit (the orc lets the walled soldier go
// and stops walking).
TEST_F(SemBotReach, a_foe_behind_a_wall_keeps_the_classic_walk_step_for_step)
{
    struct Step
    {
        short ox, oy, fx, fy;
        std::uint32_t rng;
        bool operator==(const Step&) const = default;
    };
    std::vector<Step> run[2];
    for (int with_door = 0; with_door < 2; ++with_door) {
        SCOPED_TRACE(with_door ? "a door far off" : "its twin with no door");
        fresh_world(1);
        world().rng_.state_ = 42u;
        for (int x = 10; x <= 14; ++x) {
            tile(0, x, 10, PIX_H_WALL1);
            tile(0, x, 14, PIX_H_WALL1);
        }
        wall_column(10, 10, 14);
        wall_column(14, 10, 14);
        walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE);
        walker* foe = scene_bot(FAMILY_SOLDIER, 2, 192, 80);
        ASSERT_TRUE(orc && foe);
        if (with_door) {
            ASSERT_NE(nullptr, door_at(560, 560));
            EXPECT_EQ(Exit::NoWayOpen, og::reach::door_shut_exit(*orc, *foe)) << "the staging";
        }
        for (int tick = 1; tick <= 120; ++tick) {
            world().tick();
            run[with_door].push_back(Step{orc->xpos(), orc->ypos(), foe->xpos(), foe->ypos(), world().rng_.state_});
        }
        if (with_door) {
            EXPECT_EQ(foe, orc->foe()) << "the walled soldier is still the orc's foe";
        }
    }
    ASSERT_EQ(run[0].size(), run[1].size());
    for (std::size_t t = 0; t < run[0].size(); ++t) {
        ASSERT_TRUE(run[0][t] == run[1][t]) << "tick " << (t + 1);
    }
    bool moved = false;
    for (const Step& s : run[1])
        moved = moved || s.ox != run[1].front().ox || s.oy != run[1].front().oy;
    EXPECT_TRUE(moved) << "the staging: the orc walks at its walled foe";
}

// A struck bot takes its striker at the blow and lets it go on its next round: an orc bot in the pen
// (an enemy door in its gap) is struck by a soldier outside (statistics::hit_response, which this rule
// leaves as it was). On both settings it holds the soldier at once. Its next round (a SEARCH order):
// setting on, it lets the soldier go; setting off, it keeps it.
// Fails when broken: keeps_foe never letting a foe go (the soldier kept with the setting on).
TEST_F(SemBotReach, a_struck_bot_takes_its_striker_at_the_blow_and_lets_go_on_its_next_round)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        ASSERT_NE(nullptr, pen_with_a_door());
        walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* striker = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(orc && striker);
        orc->stats()->hit_response(striker);
        EXPECT_EQ(striker, orc->foe()) << "it takes its striker at the blow";
        orc->stats()->clear_command();
        orc->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        orc->set_ani_type(ANI_WALK);
        orc->stats()->do_command();
        EXPECT_EQ(fixes ? nullptr : striker, orc->foe());
    }
}

// The wander of a bot with no foe (statistics::idle_walk). An orc bot in the pen (an enemy door in its
// gap), a soldier of team 2 outside. Setting on: the bot is shut out, so it queues no wander and the
// stream is not drawn; with the soldier dead it queues the classic walk after the two direction draws
// (rng(3), rng(3)). Setting off: the walk and its draws as always. Each case three times: the orc's
// stats owned by the orc, by nobody, and by another orc of its side in the open (not shut out,
// asserted); the stats' owner changes nothing, because the orc asks about itself. The same through
// the orc's own act (living::act_random's call). Then, setting on, the world plays 200 ticks: after
// every tick the orc stands on its cell and holds no foe.
// Fails when broken: idle_walk without its shut_out test (the shut-out orc queues its wander);
// idle_walk asking about the stats' owner (the orc whose stats name the other orc wanders, and the
// one whose stats name nobody binds a null reference).
TEST_F(SemBotReach, a_bot_with_only_an_enemy_behind_a_door_queues_no_wander_and_stands_on_its_cell)
{
    enum class Owner { Itself, Another, Nobody };
    for (const Owner owner : {Owner::Itself, Owner::Another, Owner::Nobody}) {
        for (int foe_dead = 0; foe_dead < 2; ++foe_dead) {
            for (short fixes : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") + (foe_dead ? ", the soldier dead" : ", the soldier alive") +
                             (owner == Owner::Itself ? ", its stats its own" : owner == Owner::Another ? ", its stats another orc's" : ", its stats nobody's"));
                fresh_world(fixes);
                ASSERT_NE(nullptr, pen_with_a_door());
                walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
                walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
                walker* other = living_at(FAMILY_ORC, 1, 400, 400, 16, 16);
                ASSERT_TRUE(orc && foe && other);
                if (foe_dead)
                    foe->set_dead(1);
                ASSERT_EQ(fixes && !foe_dead, og::reach::shut_out(*orc)) << "the staging";
                ASSERT_FALSE(og::reach::shut_out(*other)) << "the staging: the other orc walks to the soldier";
                orc->stats()->clear_command();  // before the owner is changed: it resets the owner's weapon
                if (owner != Owner::Itself)
                    orc->stats()->set_controller(owner == Owner::Another ? other : nullptr);
                ASSERT_EQ(owner == Owner::Itself ? orc : owner == Owner::Another ? other : nullptr, orc->stats()->controller())
                    << "the staging";
                const std::uint32_t s0 = world().rng_.state_;
                (void)orc->stats()->idle_walk(*orc, 20);
                og::sim::SimRandom expect(s0);
                if (!(fixes && !foe_dead)) {
                    (void)expect.next(3);
                    (void)expect.next(3);
                    ASSERT_FALSE(orc->stats()->commands.empty());
                    EXPECT_EQ(COMMAND_WALK, orc->stats()->commands.front().commandtype);
                    EXPECT_EQ(20, orc->stats()->commands.front().commandcount);
                } else {
                    EXPECT_TRUE(orc->stats()->commands.empty()) << "no wander";
                }
                EXPECT_EQ(expect.state_, world().rng_.state_);
                orc->stats()->set_controller(orc);
            }
        }
    }
    // The same through the living's own call (living::act_random, reached by act's two coins, the
    // first not 0 and the second 0, and then its 1-in-80, not 0, with no foe), for each owner of its
    // stats: the setting on with the soldier alive, nothing is queued after those three draws; the
    // setting off with the soldier dead, the classic walk of 40 after them and its two draws.
    const auto into_act_random = [] {
        for (std::uint32_t st = 1;; ++st) {
            og::sim::SimRandom r(st);
            if (r.next(5) != 0 && r.next(5) == 0 && r.next(80) != 0)
                return st;
        }
    };
    for (const Owner owner : {Owner::Itself, Owner::Another, Owner::Nobody}) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, the soldier alive, act" : "Gameplay Fixes off, the soldier dead, act") +
                         (owner == Owner::Itself ? ", its stats its own" : owner == Owner::Another ? ", its stats another orc's" : ", its stats nobody's"));
            fresh_world(fixes);
            ASSERT_NE(nullptr, pen_with_a_door());
            walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE);
            walker* foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
            walker* other = living_at(FAMILY_ORC, 1, 400, 400, 16, 16);
            ASSERT_TRUE(orc && foe && other);
            if (!fixes)
                foe->set_dead(1);
            ASSERT_EQ(fixes != 0, og::reach::shut_out(*orc)) << "the staging";
            orc->stats()->clear_command();
            if (owner != Owner::Itself)
                orc->stats()->set_controller(owner == Owner::Another ? other : nullptr);
            const std::uint32_t s0 = into_act_random();
            world().rng_.state_ = s0;
            (void)orc->act();
            og::sim::SimRandom expect(s0);
            (void)expect.next(5);
            (void)expect.next(5);
            (void)expect.next(80);
            if (!fixes) {
                (void)expect.next(3);
                (void)expect.next(3);
                ASSERT_FALSE(orc->stats()->commands.empty());
                EXPECT_EQ(COMMAND_WALK, orc->stats()->commands.front().commandtype);
                EXPECT_EQ(40, orc->stats()->commands.front().commandcount);
            } else {
                EXPECT_TRUE(orc->stats()->commands.empty()) << "no wander";
            }
            EXPECT_EQ(expect.state_, world().rng_.state_);
            EXPECT_EQ(nullptr, orc->foe());
            EXPECT_EQ(12 * GRID_SIZE, orc->xpos());
            EXPECT_EQ(12 * GRID_SIZE, orc->ypos());
            orc->stats()->set_controller(orc);
        }
    }
    fresh_world(1);
    ASSERT_NE(nullptr, pen_with_a_door());
    walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE);
    walker* foe = scene_bot(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE);
    ASSERT_TRUE(orc && foe);
    for (int tick = 1; tick <= 200; ++tick) {
        world().tick();
        ASSERT_EQ(12 * GRID_SIZE, orc->xpos()) << "tick " << tick;
        ASSERT_EQ(12 * GRID_SIZE, orc->ypos()) << "tick " << tick;
        ASSERT_EQ(nullptr, orc->foe()) << "tick " << tick;
    }
}
