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
// some in played scenes; and Gameplay Fixes BK, a bot that holds the key to a door on its way walks
// its route into the door: og::reach::opens_way, its one site in the re-plan block of walk_to_foe,
// and played scenes of a key handed out; and, in suite SemBotBreak, Gameplay Fixes BL, a bot with no
// enemy it can get at breaks down the locked door in its way and lets it go the moment it can get at
// one: og::reach::breaks (the door walk) and keeps_door, the tick's hand-out, the idle decision and
// the keeps, on painted maps and in played scenes. Each test is named after the rule it asserts, asserts both
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
#include <cmath>
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
    // The room of the key holder's tests: walls round the interior cells x 16..26, y 8..15, an enemy
    // door (team 3, key level 1) in the bottom wall at cell (21,16), a soldier (team 2) inside at cell
    // (19,14). The door returned, the soldier into `soldier`.
    walker* keyed_room(walker*& soldier)
    {
        room(16, 8, 26, 15);
        tile(0, 21, 16, PIX_GRASS1);
        soldier = living_at(FAMILY_SOLDIER, 2, 19 * GRID_SIZE, 14 * GRID_SIZE, 16, 16);
        return door_at(21 * GRID_SIZE, 16 * GRID_SIZE);
    }
    // The room of the played door scenes: interior cells x 16..29, y 8..15, walls round it; a door of
    // a third side (team 2, level 3) in its bottom wall at cell (25,16), 16 x 16 at (400,256); twelve
    // orcs (team 1) inside at x 368, 400, 432, 464 by y 160, 192, 224; four crew bots (team 0: elf,
    // archer, cleric, barbarian) 160 px below the door on row 432, at x 352, 384, 416, 448, or, when
    // `elf_alone`, the elf alone.
    struct DoorRoom
    {
        walker* door = nullptr;
        std::vector<walker*> orcs;
        std::vector<walker*> crew;
        std::vector<walker*> all() const
        {
            std::vector<walker*> out = orcs;
            out.insert(out.end(), crew.begin(), crew.end());
            return out;
        }
    };
    DoorRoom door_room(bool elf_alone = false)
    {
        DoorRoom r;
        room(16, 8, 29, 15);
        tile(0, 25, 16, PIX_GRASS1);
        r.door = door_at(400, 256, 2, 3);
        for (short y : {short{160}, short{192}, short{224}})
            for (short x : {short{368}, short{400}, short{432}, short{464}})
                r.orcs.push_back(scene_bot(FAMILY_ORC, 1, x, y));
        const int crew_family[4] = {FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN};
        for (int i = 0; i < (elf_alone ? 1 : 4); ++i)
            r.crew.push_back(scene_bot(crew_family[i], 0, static_cast<short>(352 + 32 * i), 432));
        return r;
    }
    // A door's hit points that no blow can lower: a blow takes a whole number of at most 32,767
    // (walker::do_combat_damage takes a short), and at 10^15 a float's step is 2^26, so the
    // subtraction rounds back to the same float (asserted where it is used).
    static constexpr float kUnbreakable = 1.0e15f;
    static void unbreakable(walker* door)
    {
        door->stats()->set_max_hitpoints(kUnbreakable);
        door->stats()->set_hitpoints(kUnbreakable);
    }
    // Where a blow on a body is shown to its striker (walker::do_hit_effects: the body's centre
    // column and top row, the attacker's colour 235), read once so it is known after the body died.
    struct Spot
    {
        float x = 0.0f;
        float y = 0.0f;
    };
    static Spot spot_of(const walker* body)
    {
        return Spot{static_cast<float>(body->xpos() + body->sizex() / 2), static_cast<float>(body->ypos())};
    }
    // The blows shown to `w` at `s` among the damage numbers of its list after the first `from`.
    static int blows_at(const walker* w, Spot s, std::size_t from = 0)
    {
        int n = 0;
        std::size_t i = 0;
        for (const auto& d : w->damage_numbers) {
            if (i++ < from)
                continue;
            if (d.color == 235 && d.x == s.x && d.y == s.y)
                ++n;
        }
        return n;
    }
    // Is a COMMAND_ATTACK among w's orders?
    static bool attack_queued(walker* w)
    {
        for (const auto& c : w->stats()->commands)
            if (c.commandtype == COMMAND_ATTACK)
                return true;
        return false;
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
// barbarian) 160 px below the door at y 432. The door is killed at tick 200 by its own death path;
// its hit points are a float no blow can lower, so nothing else opens it before. Setting on: after
// tick 1 every bot of both sides holds the door (each is shut out and gets at nobody, so Gameplay
// Fixes BL hands it the door; the full scan passed over every bot of the other side), and after
// every tick to 199 the door stands, its hit points whole. On the door's death the maps join the two
// sides (every orc walks to every crew bot, none is door-shut). After tick 201 every live bot with no
// leader holds a live bot of the other side (the tick's hand-out). Setting off: after tick 1 every
// bot holds a foe already.
// Fails when broken: the full scan offering a door-shut foe (with the setting on a bot holds a bot of
// the other side after tick 1).
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
        for (walker* w : all) {
            ASSERT_NE(nullptr, w);
        }
        if (!fixes) {
            world().tick();
            for (walker* w : all) {
                EXPECT_TRUE(w->foe() != nullptr && !w->is_friendly(w->foe())) << "classic: every bot holds a foe after tick 1";
            }
            continue;
        }
        ASSERT_EQ(kUnbreakable, kUnbreakable - 32767.0f) << "the staging: no blow lowers the door's hit points";
        unbreakable(door);
        for (int tick = 1; tick <= 199; ++tick) {
            world().tick();
            if (tick == 1) {
                for (std::size_t i = 0; i < all.size(); ++i)
                    ASSERT_EQ(door, all[i]->foe()) << "bot " << i << " holds the door after tick 1";
            }
            ASSERT_FALSE(door->dead());
            ASSERT_EQ(kUnbreakable, door->stats()->hitpoints()) << "tick " << tick;
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
            EXPECT_TRUE(w->foe() != nullptr && !w->foe()->dead() && w->foe()->query_order() == Order::Living &&
                        !w->is_friendly(w->foe()))
                << "bot " << i << " holds a bot of the other side after tick 201";
        }
        EXPECT_EQ(all.size(), judged) << "every bot is judged: nobody died and nobody gives a leader";
    }
}

// A chase to a door. The orc's room: interior cells x 20..27, y 6..11, walls
// round it, its own side's door (team 1) at cell (23,12). A hurt orc (team 1, 100 hit points) at
// (368,288) flees straight up through its door (a 54-round walk); a crew orc (team 0, melee only) at
// (368,424) chases it; each holds the other. P is the first tick after which only the door keeps the
// crew orc from the orc (door_shut, asked after the tick). Before P the crew orc holds the orc after
// every tick. Setting on: after tick P it holds the orc's door, not the orc (it let the orc go at the
// top of that tick's round and took no step; the tick's hand-out after its act found no foe it gets
// at and handed it the door in its way, Gameplay Fixes BL), and it stands where it stood after tick
// P - 1. Setting off: it still holds the orc.
// Fails when broken: keeps_foe never letting a foe go (the crew orc holds the orc after tick P).
TEST_F(SemBotReach, a_chaser_lets_its_foe_go_on_the_round_after_the_foe_passes_its_own_door)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, seed " : "Gameplay Fixes off, seed ") + std::to_string(seed));
            fresh_world(fixes);
            room(20, 6, 27, 11);
            tile(0, 23, 12, PIX_GRASS1);
            walker* own_door = door_at(368, 192, 1, 3);
            ASSERT_NE(nullptr, own_door);
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
                EXPECT_EQ(own_door, chaser->foe()) << "the orc let go and the door handed on tick " << passed;
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
// bot is door-shut (asserted for the two pairs of archers in line), so with the setting on every bot
// is shut out and gets at nobody, and is handed the door (Gameplay Fixes BL): after tick 1 every bot
// holds the door, and each of the four archers shoots at it (an arrow of its own flies on a tick at
// whose end it holds the door; by the draws, within the 600 ticks). With the wall cell (20,14) an
// arrow slit (PIX_WALL_ARROW_GRASS) between the two archers at x 320, each of those two holds the
// other only by its shot (asserted: Exit::Shot both ways), so it gets at an enemy and is handed no
// door: for 600 ticks each stands where it was put (a bot kept only by its shot takes no step) and
// holds no door, and each shoots (a shot of its own is in flight after some tick); after tick 1 every
// other bot holds the door. Setting off, the plain door: the archers shoot (a shot in flight).
// Fails when broken: the quick search, the full scan or the keep taking a door-shut foe (with the
// plain wall a bot holds a bot of the other side after tick 1); og::reach's shot exit left out (the
// slit archers let each other go, are handed the door and walk to it).
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
            const std::vector<walker*> archers = {in1, in2, ca1, ca2};
            const std::vector<walker*> slit_pair = {in2, ca2};
            std::vector<std::pair<short, short>> spot;
            for (walker* w : all)
                spot.emplace_back(w->xpos(), w->ypos());
            if (fixes) {
                ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*in1, *ca1)) << "the staging: the door's column";
                EXPECT_EQ(slit ? Exit::Shot : Exit::DoorShut, og::reach::door_shut_exit(*in2, *ca2)) << "the staging";
                EXPECT_EQ(slit ? Exit::Shot : Exit::DoorShut, og::reach::door_shut_exit(*ca2, *in2)) << "the staging";
            }
            const std::uint32_t door_id = door->entity_id();
            bool in2_shot = false, ca2_shot = false, any_shot = false;
            std::vector<bool> shot_at_door(archers.size(), false);
            std::vector<std::vector<std::uint32_t>> arrows(archers.size());
            for (int tick = 1; tick <= 600; ++tick) {
                world().tick();
                any_shot = any_shot || weapons_owned_by(all) > 0;
                in2_shot = in2_shot || weapons_owned_by({in2}) > 0;
                ca2_shot = ca2_shot || weapons_owned_by({ca2}) > 0;
                if (!fixes)
                    continue;
                for (std::size_t i = 0; i < all.size(); ++i) {
                    const bool paired = slit && std::find(slit_pair.begin(), slit_pair.end(), all[i]) != slit_pair.end();
                    if (paired) {
                        ASSERT_EQ(spot[i].first, all[i]->xpos()) << "tick " << tick << ", bot " << i;
                        ASSERT_EQ(spot[i].second, all[i]->ypos()) << "tick " << tick << ", bot " << i;
                        ASSERT_FALSE(all[i]->foe() != nullptr && all[i]->foe()->is_door()) << "tick " << tick << ", bot " << i;
                    } else if (tick == 1) {
                        EXPECT_EQ(door, all[i]->foe()) << "bot " << i << " holds the door after tick 1";
                    }
                }
                if (slit)
                    continue;
                const bool door_stands = world().find_by_id(door_id) != nullptr;
                for (std::size_t i = 0; i < archers.size(); ++i)
                    for (const auto& u : world().weaplist)
                        if (u && !u->dead() && u->owner() == archers[i] && u->family() == FAMILY_ARROW &&
                            std::find(arrows[i].begin(), arrows[i].end(), u->entity_id()) == arrows[i].end()) {
                            arrows[i].push_back(u->entity_id());
                            shot_at_door[i] = shot_at_door[i] || (door_stands && archers[i]->foe() == door);
                        }
            }
            if (!fixes) {
                EXPECT_TRUE(any_shot) << "classic: the archers shoot through the door";
            } else if (slit) {
                EXPECT_TRUE(in2_shot) << "the archer inside shoots through the slit";
                EXPECT_TRUE(ca2_shot) << "the crew's archer shoots through the slit";
            } else {
                for (std::size_t i = 0; i < archers.size(); ++i)
                    EXPECT_TRUE(shot_at_door[i]) << "archer " << i << " shoots at the door";
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

// The idle decision of a bot with no foe (statistics::idle_walk). An orc bot in the pen (an enemy door
// in its gap), a soldier of team 2 outside. Setting on: the bot is shut out, so it queues no wander and
// the stream is not drawn, and it gets at nobody, so it is handed the pen's door (Gameplay Fixes BL);
// with the soldier dead it queues the classic walk after the two direction draws (rng(3), rng(3)) and
// holds no foe. Setting off: the walk and its draws as always. Each case three times: the orc's stats
// owned by the orc, by nobody, and by another orc of its side in the open (not shut out, asserted);
// the stats' owner changes nothing, because the orc asks about itself. A further case, setting on: a
// second soldier of team 2 inside the pen (the orc walks to it, asserted) and the idle decision asked
// before any hand-out gave it that soldier: breaks answers nothing while the orc is shut out, so it
// stays where it is: no foe, no wander, no draw. The same through the orc's own act
// (living::act_random's call): setting on, the door handed and the orc on its cell. Then, setting on,
// the world plays: after tick 1 the orc holds the pen's door.
// Fails when broken: idle_walk without its shut_out test (the shut-out orc queues its wander);
// idle_walk asking about the stats' owner (the orc whose stats name the other orc wanders, and the
// one whose stats name nobody binds a null reference); idle_walk handing no door (the shut-out orc
// holds no foe).
TEST_F(SemBotReach, a_bot_with_only_an_enemy_behind_a_door_queues_no_wander_and_is_handed_the_door)
{
    enum class Owner { Itself, Another, Nobody };
    for (const Owner owner : {Owner::Itself, Owner::Another, Owner::Nobody}) {
        for (int foe_dead = 0; foe_dead < 2; ++foe_dead) {
            for (short fixes : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") + (foe_dead ? ", the soldier dead" : ", the soldier alive") +
                             (owner == Owner::Itself ? ", its stats its own" : owner == Owner::Another ? ", its stats another orc's" : ", its stats nobody's"));
                fresh_world(fixes);
                walker* const pen_door = pen_with_a_door();
                ASSERT_NE(nullptr, pen_door);
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
                EXPECT_EQ(fixes && !foe_dead ? pen_door : nullptr, orc->foe()) << "the door handed only to the shut-out orc";
                orc->stats()->set_controller(orc);
            }
        }
    }
    {
        SCOPED_TRACE("Gameplay Fixes on, a second soldier inside the pen");
        fresh_world(1);
        ASSERT_NE(nullptr, pen_with_a_door());
        walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* outside = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        walker* inside = living_at(FAMILY_SOLDIER, 2, 11 * GRID_SIZE, 11 * GRID_SIZE, 16, 16);
        ASSERT_TRUE(orc && outside && inside);
        ASSERT_TRUE(og::reach::shut_out(*orc)) << "the staging: the soldier outside is door-shut";
        ASSERT_TRUE(og::reach::walks_to(*orc, *inside, Mode::Shut)) << "the staging: the orc walks to the one inside";
        EXPECT_EQ(nullptr, og::reach::breaks(*orc));
        orc->stats()->clear_command();
        const std::uint32_t s0 = world().rng_.state_;
        (void)orc->stats()->idle_walk(*orc, 20);
        EXPECT_EQ(nullptr, orc->foe()) << "no door handed";
        EXPECT_TRUE(orc->stats()->commands.empty()) << "no wander: it stays";
        EXPECT_EQ(s0, world().rng_.state_) << "no draw";
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
            walker* const pen_door = pen_with_a_door();
            ASSERT_NE(nullptr, pen_door);
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
            EXPECT_EQ(fixes ? pen_door : nullptr, orc->foe());
            EXPECT_EQ(12 * GRID_SIZE, orc->xpos());
            EXPECT_EQ(12 * GRID_SIZE, orc->ypos());
            orc->stats()->set_controller(orc);
        }
    }
    fresh_world(1);
    walker* const pen_door = pen_with_a_door();
    ASSERT_NE(nullptr, pen_door);
    walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE);
    walker* foe = scene_bot(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE);
    ASSERT_TRUE(orc && foe);
    world().tick();
    EXPECT_EQ(pen_door, orc->foe()) << "after tick 1 the orc holds the pen's door";
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BK, a bot that holds the key to a door on its way walks its route into the door
// (og::reach::opens_way), asked in the re-plan block of statistics::walk_to_foe only when the near
// test would hold (the foe within 100 px, or more than 200 bodies on the map). The keyed room
// (keyed_room) unless a test says otherwise.
// ---------------------------------------------------------------------------------------------

// The re-plan block of statistics::walk_to_foe (stats.cpp, its near test). An orc bot (team 1)
// holding the door's key (key level 1) stands outside, below the room, at cell (18,18), 80 px from
// the soldier (Manhattan), so the near test holds; its straight line to the soldier meets the wall
// left of the door. Setting on: only its key opens its way (opens_way), so the near branch is not
// taken: no ATTACK is queued, its SEARCH order keeps its count, and its route (A*) runs through the
// door's cell. Setting off, and for a seat's fighter with the setting on: the classic near branch, an
// ATTACK queued and no route. On a crowded map (200 more bodies of its side) with the orc at cell
// (18,21), 128 px off: setting on, the route through the door; setting off, the classic crowded
// branch (no foe within 100 px: its order ended, no route, no ATTACK).
// Fails when broken: the near test without its opens_way term (with the setting on the orc queues an
// ATTACK, and on the crowded map ends its order with no route); opens_way without its asks test (the
// seat's fighter is routed).
TEST_F(SemBotReach, a_key_holder_whose_way_runs_through_its_door_plans_its_route_instead_of_the_near_branch)
{
    struct Case
    {
        const char* name;
        short fixes;
        bool seat;
        bool crowded;
    };
    const Case cases[] = {
        {"Gameplay Fixes on", 1, false, false},
        {"Gameplay Fixes off", 0, false, false},
        {"Gameplay Fixes on, a seat's fighter", 1, true, false},
        {"Gameplay Fixes on, a crowded map", 1, false, true},
        {"Gameplay Fixes off, a crowded map", 0, false, true},
    };
    const std::uint32_t s = a_state_whose_first_draw_of(300);
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world(c.fixes);
        walker* soldier = nullptr;
        ASSERT_NE(nullptr, keyed_room(soldier));
        ASSERT_NE(nullptr, soldier);
        walker* orc = bot_at(18 * GRID_SIZE, static_cast<short>((c.crowded ? 21 : 18) * GRID_SIZE));
        ASSERT_NE(nullptr, orc);
        orc->set_keys(static_cast<std::uint32_t>(key_level_mask(1)));
        if (c.seat)
            orc->set_user(0);
        if (c.crowded) {
            ASSERT_NO_FATAL_FAILURE(crowd(200));
        }
        ASSERT_EQ(c.crowded ? 128 : 80, orc->distance_to_ob(soldier));
        const bool rule = c.fixes != 0 && !c.seat;
        EXPECT_EQ(rule, og::reach::opens_way(*orc, *soldier)) << "the staging";
        orc->set_foe(soldier);
        orc->stats()->clear_command();
        orc->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        orc->stats()->set_last_distance(15000);
        orc->set_path_check_counter(0);
        world().rng_.state_ = s;
        (void)orc->stats()->walk_to_foe();
        const std::vector<Cell> route = route_of(orc);
        ASSERT_FALSE(orc->stats()->commands.empty());
        if (rule) {
            EXPECT_FALSE(attack_queued(orc)) << "the near branch is not taken";
            EXPECT_EQ(COMMAND_SEARCH, orc->stats()->commands.front().commandtype);
            EXPECT_EQ(9, orc->stats()->commands.front().commandcount) << "its order stands";
            EXPECT_TRUE(holds(route, Cell{21, 16, 0})) << "its route runs through the door: " << text(route);
        } else if (!c.crowded) {
            EXPECT_TRUE(attack_queued(orc)) << "classic: the near branch";
            EXPECT_TRUE(route.empty()) << text(route);
        } else {
            EXPECT_FALSE(attack_queued(orc));
            EXPECT_EQ(0, orc->stats()->commands.front().commandcount) << "classic: no foe within 100 px, its order ended";
            EXPECT_TRUE(route.empty()) << text(route);
        }
        EXPECT_EQ(soldier, orc->foe());
    }
}

// og::reach::opens_way and each of its exits, asked directly (the orc bot of the test above at cell
// (18,18), its foe the soldier inside, unless a case says otherwise). Yes: the orc holds the door's
// key, walks to the soldier with the doors that bar it shut (none: its key opens the one door), and
// with no key would not (the door closes the room). Yes too for the orc resting against the door from
// below, at (336,271), its corner one pixel inside the door's cell: with the key it starts from the
// door's own cell, which stands for it, and holding no key from the cell below, which its box
// overlaps 15 rows (each start cell asserted); the keyless walk starts from its own start cell, so it
// stays outside, though from the door's cell it would step into the room (asserted). No, each by its
// own exit: the setting off; a seat's fighter and a guard at its post (each would walk to the soldier
// with the key and not without, asserted); the orc holding no key, and the orc holding only the key
// of a door on floor 1 (both answered before any Asker is resolved: Maps::resolutions does not
// move); a second gap in the room's wall at cell (15,12) (the keyless kind walks in by it,
// asserted); the soldier walled in on its cell (no way even with the key); the orc standing inside a
// wall tile at cell (18,19) (its start cell has no region, asserted, though from inside the wall it
// would reach the open ground and, with the key, the room, asserted); and, on two floors, the orc
// inside the room at cell (19,12) and its foe on floor 1 at cell (21,24), beyond an up-stair outside
// the room at cell (21,20) (with the key it would walk to that foe and without it not, asserted).
// Every question draws nothing.
// Fails when broken: opens_way without its asks test (the seat's fighter and the guard: yes); without
// its floor test (the foe upstairs: yes); without its key test (a resolution for the orc with no
// key); holds_door_key counting a door on another floor (a resolution for the orc holding the key
// upstairs); without its start-region test (the orc inside the wall: yes); without its walks_to test
// (the walled-in soldier: yes); without its keyless test (the second gap: yes); the keyless walk
// started from the key holder's own start cell instead of the keyless kind's (the orc against the
// door: no).
TEST_F(SemBotReach, the_key_opens_the_way_only_for_a_bot_that_holds_it_and_has_no_keyless_way)
{
    enum class V { Yes, AgainstDoor, Off, Seat, Guard, NoKey, KeyUpstairs, NeedsNoKey, NoWay, NoRegion, FoeUpstairs };
    const std::pair<V, const char*> cases[] = {
        {V::Yes, "the key opens the only way"},
        {V::AgainstDoor, "resting against the door from below: the keyless walk from its own start cell"},
        {V::Off, "Gameplay Fixes off"},
        {V::Seat, "a seat's fighter"},
        {V::Guard, "a guard at its post"},
        {V::NoKey, "no key"},
        {V::KeyUpstairs, "only the key of a door on floor 1"},
        {V::NeedsNoKey, "a second gap: a way that needs no key"},
        {V::NoWay, "the soldier walled in: no way even with the key"},
        {V::NoRegion, "the orc inside a wall tile: no region"},
        {V::FoeUpstairs, "its foe on floor 1, beyond a stair"},
    };
    for (const auto& [v, name] : cases) {
        SCOPED_TRACE(name);
        fresh_world(v == V::Off ? 0 : 1);
        if (v == V::KeyUpstairs || v == V::FoeUpstairs)
            two_floors();
        walker* soldier = nullptr;
        ASSERT_NE(nullptr, keyed_room(soldier));
        ASSERT_NE(nullptr, soldier);
        walker* foe = soldier;
        short ox = 18 * GRID_SIZE;
        short oy = 18 * GRID_SIZE;
        std::uint32_t keys = static_cast<std::uint32_t>(key_level_mask(1));
        switch (v) {
        case V::AgainstDoor:
            ox = 21 * GRID_SIZE;
            oy = 16 * GRID_SIZE + 15;
            break;
        case V::NoKey:
            keys = 0;
            break;
        case V::KeyUpstairs:
            keys = static_cast<std::uint32_t>(key_level_mask(2));
            ASSERT_NE(nullptr, door_at(5 * GRID_SIZE, 5 * GRID_SIZE, kDoorTeam, 2, 1));
            break;
        case V::NeedsNoKey:
            tile(0, 15, 12, PIX_GRASS1);
            break;
        case V::NoWay:
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx != 0 || dy != 0)
                        tile(0, 19 + dx, 14 + dy, PIX_H_WALL1);
            break;
        case V::NoRegion:
            oy = 19 * GRID_SIZE;
            tile(0, 18, 19, PIX_H_WALL1);
            break;
        case V::FoeUpstairs:
            ox = 19 * GRID_SIZE;
            oy = 12 * GRID_SIZE;
            tile(0, 21, 20, PIX_ZSTAIR_UP);
            tile(1, 21, 20, PIX_ZSTAIR_DOWN);
            foe = living_at(FAMILY_SOLDIER, 2, 21 * GRID_SIZE, 24 * GRID_SIZE, 16, 16, 1);
            ASSERT_NE(nullptr, foe);
            break;
        default:
            break;
        }
        walker* orc = bot_at(ox, oy);
        ASSERT_NE(nullptr, orc);
        orc->set_keys(keys);
        if (v == V::Seat)
            orc->set_user(0);
        if (v == V::Guard)
            orc->set_act_type(ACT_GUARD);
        const std::uint64_t r0 = world().reach_maps.resolutions();
        const std::uint32_t s0 = world().rng_.state_;
        EXPECT_EQ(v == V::Yes || v == V::AgainstDoor, og::reach::opens_way(*orc, *foe));
        EXPECT_EQ(s0, world().rng_.state_) << "the question draws nothing";
        if (v == V::Off || v == V::Seat || v == V::Guard || v == V::NoKey || v == V::KeyUpstairs) {
            EXPECT_EQ(r0, world().reach_maps.resolutions()) << "answered before any Asker is resolved";
        }
        if (v == V::Off || v == V::NoKey || v == V::KeyUpstairs)
            continue;
        // The staging: what the other exits would answer, so the case's own exit is the one that
        // says no.
        og::reach::Asker keyless(*orc, 0);
        switch (v) {
        case V::NeedsNoKey:
            EXPECT_TRUE(keyless.walks_to(*foe, Mode::Shut)) << "the keyless kind walks in by the gap";
            break;
        case V::NoWay:
            EXPECT_FALSE(og::reach::walks_to(*orc, *foe, Mode::Shut));
            break;
        case V::NoRegion:
            EXPECT_GT(0, og::reach::region_of(*orc, og::reach::start_cell(*orc), Mode::Shut)) << "no region";
            EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Shut)) << "from inside the wall, with the key";
            EXPECT_FALSE(keyless.walks_to(*foe, Mode::Shut));
            break;
        case V::AgainstDoor: {
            EXPECT_EQ((Cell{21, 16, 0}), og::reach::start_cell(*orc)) << "with the key: the door's own cell, which stands for it";
            EXPECT_EQ((Cell{21, 17, 0}), keyless.start_cell()) << "holding no key: the cell below, which its box overlaps 15 rows";
            EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Shut)) << "with the key";
            EXPECT_FALSE(keyless.walks_to(*foe, Mode::Shut)) << "with no key, from its own start cell";
            bool from_the_door = false;
            for (const Cell& c : keyless.approach_cells(*foe, Mode::Shut))
                from_the_door = from_the_door || keyless.reaches(Cell{21, 16, 0}, c, Mode::Shut);
            EXPECT_TRUE(from_the_door) << "started from the door's cell, the keyless walk would step into the room";
            break;
        }
        default:  // Yes, Seat, Guard, FoeUpstairs
            EXPECT_TRUE(og::reach::walks_to(*orc, *foe, Mode::Shut)) << "with the key";
            EXPECT_FALSE(keyless.walks_to(*foe, Mode::Shut)) << "with no key";
            break;
        }
    }
}

// A re-plan whose near test does not hold does not ask opens_way (stats.cpp: the question is the
// near test's last term). The orc bot holding the key stands at cell (18,24), 176 px from the
// soldier, on an uncrowded map. The maps of the orc's own kind are built first (a walks_to
// question); opens_way's keyless kind is built by opens_way alone, so Maps::builds tells whether it
// was asked. Setting on: the far re-plan plans the route through the door (A*, as for any far foe)
// and builds nothing; asked directly afterwards, opens_way answers yes and builds the keyless kind's
// door half (one build). The same orc at cell (18,18), 80 px off: its near re-plan builds that one
// door half (it was asked) and routes through the door. Setting off: nothing is built, and the far
// route runs through the door as the classic route does.
// Fails when broken: opens_way asked before the near test (the far re-plan builds the keyless door
// half); the near test without its opens_way term (the near re-plan builds nothing).
TEST_F(SemBotReach, a_far_replan_does_not_ask_whether_a_key_opens_the_way)
{
    const std::uint32_t s = a_state_whose_first_draw_of(300);
    for (short fixes : {short{0}, short{1}}) {
        for (int near = 0; near < 2; ++near) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (near ? ", its foe 80 px off" : ", its foe 176 px off"));
            fresh_world(fixes);
            walker* soldier = nullptr;
            ASSERT_NE(nullptr, keyed_room(soldier));
            ASSERT_NE(nullptr, soldier);
            walker* orc = bot_at(18 * GRID_SIZE, static_cast<short>((near ? 18 : 24) * GRID_SIZE));
            ASSERT_NE(nullptr, orc);
            orc->set_keys(static_cast<std::uint32_t>(key_level_mask(1)));
            ASSERT_EQ(near ? 80 : 176, orc->distance_to_ob(soldier));
            ASSERT_TRUE(og::reach::walks_to(*orc, *soldier, Mode::Shut));
            const std::uint64_t b0 = world().reach_maps.builds();
            orc->set_foe(soldier);
            orc->stats()->clear_command();
            orc->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
            orc->stats()->set_last_distance(15000);
            orc->set_path_check_counter(0);
            world().rng_.state_ = s;
            (void)orc->stats()->walk_to_foe();
            if (!fixes) {
                EXPECT_EQ(0u, world().reach_maps.builds()) << "nothing is built with the setting off";
                if (!near) {
                    EXPECT_TRUE(holds(route_of(orc), Cell{21, 16, 0})) << "classic: " << text(route_of(orc));
                }
                continue;
            }
            EXPECT_EQ(b0 + (near ? 1u : 0u), world().reach_maps.builds())
                << (near ? "the near re-plan asked: the keyless kind's door half" : "the far re-plan asked nothing");
            EXPECT_TRUE(holds(route_of(orc), Cell{21, 16, 0})) << text(route_of(orc));
            EXPECT_FALSE(attack_queued(orc));
            if (!near) {
                EXPECT_TRUE(og::reach::opens_way(*orc, *soldier));
                EXPECT_EQ(b0 + 1u, world().reach_maps.builds()) << "asked, opens_way builds the keyless kind's door half";
            }
        }
    }
}

// Two scenes, a crew bot handed the key of the room's door: door_room with the elf alone outside.
// The three other crew bots are left out because, handed the door by Gameplay Fixes BL, they would go
// to it and could stand in the elf's way; the orcs inside cannot stand in its way until the door is
// open (the door and the walls are between them), so a step of the elf's that is not taken whole is
// the terrain's refusal. The door's hit points are a float no blow can lower (asserted, and asserted
// whole on every tick it stands), so only the key opens it. (1) The key handed before the first
// tick: the door bars the elf no more, so the elf walks to the orcs and the tick's hand-out gives it
// one. (2) The key handed while the elf holds the door: after tick 1 the elf holds the door (it is
// shut out and gets at nobody); the key is handed after the first tick from 200 at whose end the elf
// holds the door, has nothing of its own in flight and is not in a swing; after the next tick it no
// longer holds the door and holds an orc, which it now walks to with the doors that bar it shut (the
// door bars it no more, so its keep let the door go and the hand-out gave it the orc). Then, in both,
// setting on, three seeds, ticks until the door dies (up to tick 600 in (1); in (2) up to 400 ticks
// after the key, the 400 the scene keyed at tick 200 had): on every tick at whose start opens_way
// held for the elf and its foe (asked after the tick before), the elf queues no ATTACK (the near
// branch is not taken) and every step it takes is the whole step it headed (its move equals its
// heading, lastx and lasty: no step of its was refused by the terrain and turned into the baby step,
// the sidestep or the corner slide); opens_way holds on some tick; the door dies, the elf's box
// within one step of it on that tick (the elf's step opened it); on the next tick every live bot
// without a leader holds a foe. Setting off, the four crew bots outside as before: after tick 1
// every bot holds a foe (the classic hand-out), and opens_way says no for the elf.
// Fails when broken: the near test without its opens_way term (in (1), on every seed, near the door
// the elf queues an ATTACK and steps at the wall beside the door: the stand-in for "the terrain
// refuses none of its steps" has its teeth in (1); in (2) the elf already stands at the door when
// it gets the key, so its straight step meets the door with or without opens_way, and (2) shows the
// keep's let-go instead); the keep asked only of a door that bars the bot (in (2) the elf keeps the
// door after the key); the tick's hand-out handing no fighter (on the tick after the door opened some
// bot holds no foe).
TEST_F(SemBotReach, a_crew_bot_handed_the_key_walks_its_route_into_the_door_and_opens_it)
{
    const std::uint32_t key3 = static_cast<std::uint32_t>(key_level_mask(3));
    ASSERT_EQ(kUnbreakable, kUnbreakable - 32767.0f) << "the staging: no blow lowers the door's hit points";
    for (int while_holding = 0; while_holding < 2; ++while_holding) {
        const char* scene = while_holding ? "the key handed while the elf holds the door" : "the key handed before the first tick";
        {
            SCOPED_TRACE(std::string("Gameplay Fixes off, ") + scene);
            fresh_world(0);
            const DoorRoom r = door_room();
            ASSERT_NE(nullptr, r.door);
            for (walker* w : r.all()) {
                ASSERT_NE(nullptr, w);
            }
            walker* elf = r.crew[0];
            elf->set_keys(elf->keys() | key3);
            world().tick();
            for (walker* w : r.all()) {
                EXPECT_TRUE(w->foe() != nullptr && !w->is_friendly(w->foe())) << "classic: every bot holds a foe after tick 1";
            }
            ASSERT_NE(nullptr, elf->foe());
            EXPECT_FALSE(og::reach::opens_way(*elf, *elf->foe()));
        }
        for (std::uint32_t seed : {42u, 1337u, 2025u}) {
            SCOPED_TRACE(std::string("Gameplay Fixes on, ") + scene + ", seed " + std::to_string(seed));
            fresh_world(1);
            const DoorRoom r = door_room(true);
            ASSERT_NE(nullptr, r.door);
            for (walker* w : r.all()) {
                ASSERT_NE(nullptr, w);
            }
            ASSERT_EQ(1u, r.crew.size()) << "the staging: the elf alone outside";
            walker* elf = r.crew[0];
            unbreakable(r.door);
            // The door is a weapon: the tick it dies it is destroyed, so it is known by its id and box.
            const std::uint32_t door_id = r.door->entity_id();
            const short dx = r.door->xpos(), dy = r.door->ypos(), dsx = r.door->sizex(), dsy = r.door->sizey();
            world().rng_.state_ = seed;
            int tick = 0;
            int last_tick = 600;  // (1) the key before tick 1: 600 ticks, as before
            if (!while_holding) {
                elf->set_keys(elf->keys() | key3);
            } else {
                int handed = -1;
                for (tick = 1; tick <= 600 && handed < 0; ++tick) {
                    world().tick();
                    ASSERT_NE(nullptr, world().find_by_id(door_id)) << "tick " << tick;
                    ASSERT_EQ(kUnbreakable, r.door->stats()->hitpoints()) << "tick " << tick;
                    if (tick == 1) {
                        ASSERT_EQ(r.door, elf->foe()) << "after tick 1 the elf holds the door";
                    }
                    if (tick >= 200 && elf->foe() == r.door && weapons_owned_by({elf}) == 0 && elf->ani_type() == ANI_WALK)
                        handed = tick;
                }
                ASSERT_GT(handed, 0) << "the staging: the elf holds the door, nothing in flight, within 600 ticks";
                elf->set_keys(elf->keys() | key3);
                world().tick();
                tick = handed + 1;
                last_tick = handed + 400;  // (2) the 400 ticks after its key that the scene keyed at tick 200 had
                ASSERT_NE(nullptr, world().find_by_id(door_id));
                EXPECT_NE(r.door, elf->foe()) << "the elf let the door go at its keep";
                ASSERT_TRUE(elf->foe() != nullptr && elf->foe()->query_order() == Order::Living && !elf->is_friendly(elf->foe()))
                    << "and holds an orc";
                EXPECT_TRUE(og::reach::walks_to(*elf, *elf->foe(), Mode::Shut)) << "which it now walks to";
            }
            bool held = elf->foe() != nullptr && og::reach::opens_way(*elf, *elf->foe());
            int held_ticks = 0;
            int attacks = 0, first_attack = -1;
            int slips = 0, first_slip = -1;
            int died = -1;
            for (++tick; tick <= last_tick && died < 0; ++tick) {
                const float x0 = elf->worldx();
                const float y0 = elf->worldy();
                world().tick();
                ASSERT_FALSE(elf->dead()) << "tick " << tick;
                if (world().find_by_id(door_id) == nullptr) {
                    died = tick;
                    break;
                }
                ASSERT_EQ(kUnbreakable, r.door->stats()->hitpoints()) << "tick " << tick;
                if (held) {
                    ++held_ticks;
                    if (attack_queued(elf) && attacks++ == 0)
                        first_attack = tick;
                    const bool moved = elf->worldx() != x0 || elf->worldy() != y0;
                    if (moved && (elf->worldx() - x0 != elf->lastx() || elf->worldy() - y0 != elf->lasty()) &&
                        slips++ == 0)
                        first_slip = tick;
                }
                held = elf->foe() != nullptr && og::reach::opens_way(*elf, *elf->foe());
            }
            EXPECT_GT(died, 0) << "the door opens by tick " << last_tick;
            EXPECT_GT(held_ticks, 0) << "the staging: the elf's way only its key opens";
            EXPECT_EQ(0, attacks) << "first on tick " << first_attack;
            EXPECT_EQ(0, slips) << "a step not taken whole, first on tick " << first_slip;
            if (died < 0)
                continue;
            const short reach = static_cast<short>(std::ceil(elf->stepsize()));
            EXPECT_NE(0, ::collide(static_cast<short>(elf->xpos() - reach), static_cast<short>(elf->ypos() - reach),
                                   static_cast<short>(elf->sizex() + 2 * reach), static_cast<short>(elf->sizey() + 2 * reach),
                                   dx, dy, dsx, dsy))
                << "the elf's step met the door on tick " << died;
            world().tick();
            std::size_t judged = 0;
            const std::vector<walker*> all = r.all();
            for (std::size_t i = 0; i < all.size(); ++i) {
                walker* w = all[i];
                if (w->dead() || w->leader() != nullptr)
                    continue;
                ++judged;
                EXPECT_TRUE(w->foe() != nullptr && !w->foe()->dead() && !w->is_friendly(w->foe()))
                    << "bot " << i << " holds a foe the tick after the door opened (tick " << died + 1 << ")";
            }
            EXPECT_LT(0u, judged);
        }
    }
}

// A scene, an orc handed its own room's key (door_room; the door's hit points a float no blow can
// lower, so only the key opens it). Setting on, three seeds: after tick 1 every bot of both sides
// holds the door (each is shut out and gets at nobody: Gameplay Fixes BL), and every bot walks into
// the door, the orcs from above and the crew from below. The key goes to an orc that does not stand
// at the door when it is handed: after tick 1, before the twelve orcs reach the doorway (where one
// presses against the door and holds the others off it), the orc of the bottom row in the door's
// column, at (400,224) at the start, is given the door's key; the staging asserts that at that
// moment its box grown by its step on every side does not meet the door's box, so its next step
// cannot meet the door and it has a way to walk with the key in hand. The door then dies within the
// 600 ticks, the keyed orc's box within one step of it on that tick (its step opened it: the door's
// hit points stood whole until then), and on the tick after the one it died on every live bot
// without a leader holds a live bot of the other side (the door is gone, so nobody is shut out, and
// the hand-out gives each the nearest enemy it now gets at). opens_way answers yes for the keyed orc
// and its foe on some tick after the key, before the door dies (asserted). The key is no longer
// handed after tick 200, as it was while a bot near its door did not walk into it: by then one orc
// presses against the door (a melee bot handed a door walks into it, Gameplay Fixes BL), so a key
// handed to it is used by its very next step and the question decides nothing, and a key handed to
// another orc is held off the door by the one pressing on it. Setting off: after tick 1 every bot
// holds a foe (the classic hand-out).
// Fails when broken: the maps reading a key holder as holding no key (barring_of asking door_bars
// with no key: the keyed orc stays shut out by its own door and the door stands); the tick's
// hand-out handing no fighter (find_nearest_foe's answer not set: on the tick after the door's death
// some bot holds no foe).
TEST_F(SemBotReach, an_orc_handed_its_rooms_key_walks_out_through_its_door)
{
    const std::uint32_t key3 = static_cast<std::uint32_t>(key_level_mask(3));
    ASSERT_EQ(kUnbreakable, kUnbreakable - 32767.0f) << "the staging: no blow lowers the door's hit points";
    {
        SCOPED_TRACE("Gameplay Fixes off");
        fresh_world(0);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        for (walker* w : r.all()) {
            ASSERT_NE(nullptr, w);
        }
        world().tick();
        for (walker* w : r.all()) {
            EXPECT_TRUE(w->foe() != nullptr && !w->is_friendly(w->foe())) << "classic: every bot holds a foe after tick 1";
        }
    }
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        for (walker* w : r.all()) {
            ASSERT_NE(nullptr, w);
        }
        unbreakable(r.door);
        // The door is a weapon: the tick it dies it is destroyed, so it is known by its id and box.
        const std::uint32_t door_id = r.door->entity_id();
        const short dx = r.door->xpos(), dy = r.door->ypos(), dsx = r.door->sizex(), dsy = r.door->sizey();
        // The orc of the bottom row in the door's column, keyed after tick 1 (the key's tick, K).
        walker* keyed = r.orcs[9];
        ASSERT_EQ(dx, keyed->xpos()) << "the staging: the orc in the door's column";
        ASSERT_EQ(224, keyed->ypos()) << "the staging: of the bottom row";
        constexpr int K = 1;
        world().rng_.state_ = seed;
        int died = -1;
        int yes = 0;  // ticks after the key, before the door died, at whose end opens_way answered yes
        for (int tick = 1; tick <= 600 && died < 0; ++tick) {
            world().tick();
            ASSERT_FALSE(keyed->dead()) << "the staging: the keyed orc lives, tick " << tick;
            if (tick > K && world().find_by_id(door_id) != nullptr && keyed->foe() != nullptr &&
                og::reach::opens_way(*keyed, *keyed->foe()))
                ++yes;
            if (tick == 1) {
                const std::vector<walker*> all = r.all();
                for (std::size_t i = 0; i < all.size(); ++i)
                    ASSERT_EQ(r.door, all[i]->foe()) << "bot " << i << " holds the door after tick 1";
            }
            if (tick == K) {
                const short reach = static_cast<short>(std::ceil(keyed->stepsize()));
                ASSERT_EQ(0, ::collide(static_cast<short>(keyed->xpos() - reach), static_cast<short>(keyed->ypos() - reach),
                                       static_cast<short>(keyed->sizex() + 2 * reach), static_cast<short>(keyed->sizey() + 2 * reach),
                                       dx, dy, dsx, dsy))
                    << "the staging: the keyed orc does not stand at the door when it is handed the key";
                keyed->set_keys(keyed->keys() | key3);
            }
            if (world().find_by_id(door_id) == nullptr) {
                died = tick;
                break;
            }
            ASSERT_EQ(kUnbreakable, r.door->stats()->hitpoints()) << "tick " << tick;
        }
        ASSERT_GT(died, K) << "the door opens after the key, within 600 ticks";
        EXPECT_GT(yes, 0) << "opens_way answers yes for the keyed orc after the key";
        const short reach = static_cast<short>(std::ceil(keyed->stepsize()));
        EXPECT_NE(0, ::collide(static_cast<short>(keyed->xpos() - reach), static_cast<short>(keyed->ypos() - reach),
                               static_cast<short>(keyed->sizex() + 2 * reach), static_cast<short>(keyed->sizey() + 2 * reach),
                               dx, dy, dsx, dsy))
            << "the keyed orc's step met the door on tick " << died;
        world().tick();
        std::size_t judged = 0;
        const std::vector<walker*> all = r.all();
        for (std::size_t i = 0; i < all.size(); ++i) {
            walker* w = all[i];
            if (w->dead() || w->leader() != nullptr)
                continue;
            ++judged;
            EXPECT_TRUE(w->foe() != nullptr && !w->foe()->dead() && w->foe()->query_order() == Order::Living &&
                        !w->is_friendly(w->foe()))
                << "bot " << i << " holds a bot of the other side on the tick after the door opened (tick " << died + 1 << ")";
        }
        EXPECT_LT(0u, judged);
    }
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BL, a bot with no enemy it can get at breaks down the locked door in its way, and
// lets it go the moment it can get at one: og::reach::breaks (the door walk) and keeps_door, the
// tick's hand-out and the idle decision that hand the door, and the keep at the top of a round and
// after every act (statistics::keeps_door).
// ---------------------------------------------------------------------------------------------

namespace {

class SemBotBreak : public SemBotReach {
protected:
    // The cells of a room of the door walk's tests: walls round the interior x 10..14, y 10..14.
    void walk_room() { room(10, 10, 14, 14); }
    // A door in a wall cell of the painted map (the wall cell made grass under it).
    walker* door_in(int cx, int cy, unsigned char team = kDoorTeam, int level = 1)
    {
        tile(0, cx, cy, PIX_GRASS1);
        return door_at(static_cast<short>(cx * GRID_SIZE), static_cast<short>(cy * GRID_SIZE), team, level);
    }
    // A soldier of team 2 sitting at the cell (cx, cy): the enemy of the orc bots (team 1).
    walker* sitter_at(int cx, int cy)
    {
        walker* s = living_at(FAMILY_SOLDIER, 2, static_cast<short>(cx * GRID_SIZE), static_cast<short>(cy * GRID_SIZE), 16, 16);
        if (s != nullptr)
            s->set_act_type(ACT_SIT);
        return s;
    }
    static int manhattan(Cell a, Cell b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }
    // A soldier of team 2 sitting in the pen of pen_with_a_door at cell (12,13): the enemy only the
    // pen's door keeps the bots outside from, 112 px below a bot at cell (12,6).
    walker* pen_enemy() { return sitter_at(12, 13); }
    // Puts `w` on the re-plan tick of a SEARCH round of nine rounds, its foe `foe`, the best
    // distance it has measured far beyond any on the field, its facing `dir`.
    static void on_a_replan(walker* w, walker* foe, int dir)
    {
        w->set_foe(foe);
        w->stats()->clear_command();
        w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        w->stats()->set_last_distance(15000);
        w->set_path_check_counter(0);
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
    }
    // The world stream after a chase's two draws on a re-plan tick from `s`: walk_to_foe's
    // rng(300) (a 0 resets its distances and ends the chase) and the re-plan cadence rng(10).
    static std::uint32_t after_a_replans_draws(std::uint32_t s)
    {
        og::sim::SimRandom r(s);
        (void)r.next(300);
        (void)r.next(10);
        return r.state_;
    }
    // Does `o`'s box, grown by one pixel on every side, meet the box of `d` (the two touch)?
    static bool touches(const walker* o, const walker* d)
    {
        return o->xpos() - 1 < d->xpos() + d->sizex() && d->xpos() < o->xpos() + o->sizex() + 1 &&
               o->ypos() - 1 < d->ypos() + d->sizey() && d->ypos() < o->ypos() + o->sizey() + 1;
    }
    // The plain route follower's move, derived from its rule (walker::follow_path_to_foe): every
    // front node whose cell holds the walker's corner is dropped; toward the first node left it
    // takes one step of the sign of the cell delta on each axis. `any` is false when no node is
    // left (no step).
    struct PlainMove
    {
        bool any = false;
        int sx = 0;
        int sy = 0;
        std::vector<Cell> left;
    };
    static PlainMove plain_move(const walker* w)
    {
        PlainMove m;
        m.left = route_of(w);
        const Cell corner{w->xpos() / GRID_SIZE, w->ypos() / GRID_SIZE, w->floor()};
        while (!m.left.empty() && m.left.front() == corner)
            m.left.erase(m.left.begin());
        if (m.left.empty())
            return m;
        m.any = true;
        const int dx = m.left.front().x - corner.x;
        const int dy = m.left.front().y - corner.y;
        m.sx = (dx > 0) - (dx < 0);
        m.sy = (dy > 0) - (dy < 0);
        return m;
    }
    // A draw of the world stream and what a chosen state must answer to it (kAnyDraw: anything,
    // kNotZero: anything but 0, else exactly that value).
    static constexpr std::int64_t kAnyDraw = -1;
    static constexpr std::int64_t kNotZero = -2;
    struct Draw
    {
        std::uint32_t bound;
        std::int64_t want;
    };
    // The first state of the world stream whose draws under the bounds of `draws`, in order, answer
    // as asked.
    static std::uint32_t state_for(std::initializer_list<Draw> draws)
    {
        for (std::uint32_t st = 1;; ++st) {
            og::sim::SimRandom r(st);
            bool ok = true;
            for (const Draw& d : draws) {
                const std::uint32_t v = r.next(d.bound);
                ok = ok && (d.want == kAnyDraw || (d.want == kNotZero ? v != 0 : v == static_cast<std::uint32_t>(d.want)));
            }
            if (ok)
                return st;
        }
    }
    // The world stream after the draws `bounds`, in order, from the state `s`.
    static std::uint32_t after(std::uint32_t s, std::initializer_list<std::uint32_t> bounds)
    {
        og::sim::SimRandom r(s);
        for (std::uint32_t b : bounds)
            (void)r.next(b);
        return r.state_;
    }
    // The staging of the random act's tests (living::act_random). Made in this order, so that the bot
    // is the first walker of the object list and acts first in a tick: a bot of `family` (team 1, its
    // own box, 16 x 16 asserted) at pixel (288,304), its start cell (18,19), diagonal to the ring's
    // corner door (19,19), its sight one cell (no door and no body of the staging is inside its firing
    // box, so the random act's tail is the 200-step search, which draws nothing); the ring of doors round
    // cell (20,20) (ring_of_doors); a soldier of team 2 sitting in the ring, cloaked (invisibility 400:
    // every search that judges it draws its cloak roll, rng(20)). From (18,19) the doors of the ring
    // that lead to the soldier through one door and lie nearest are the left door (19,20) and the top
    // door (20,19), two cells each: afresh og::reach::breaks hands the top one (the smaller row).
    struct RandomActRing
    {
        walker* bot = nullptr;
        std::vector<walker*> ring;
        walker* top = nullptr;   // (20,19)
        walker* left = nullptr;  // (19,20)
        walker* soldier = nullptr;
    };
    // Does `w` have exactly one order, of `type` and (unless `count` is -1) of `count` rounds?
    static bool one_order(walker* w, int type, int count)
    {
        const auto& q = w->stats()->commands;
        return q.size() == 1 && q.front().commandtype == type && (count < 0 || q.front().commandcount == count);
    }
    RandomActRing random_act_ring(int family)
    {
        RandomActRing r;
        r.bot = scene_bot(family, 1, 288, 304);
        if (r.bot == nullptr)
            return r;
        r.bot->set_lineofsight(1);
        r.ring = ring_of_doors(20, 20);
        r.top = r.ring[1];
        r.left = r.ring[3];
        r.soldier = sitter_at(20, 20);
        if (r.soldier != nullptr)
            r.soldier->set_invisibility_left(400);
        return r;
    }
};

}  // namespace

// The tick's hand-out and the idle decision hand the room's door (door_room, nobody holding a key:
// twelve orcs inside, four crew bots 160 px below the door). Every bot of both sides is shut out and
// gets at nobody, so og::reach::breaks answers the room's door for each (asserted). The idle
// decision on its own: the first orc's statistics::idle_walk hands it the door, queues no wander and
// draws nothing. The tick's hand-out on its own: the last orc carries a walk order of ten rounds (no
// step: both deltas 0), so its act runs the order and never reaches its idle decision; after tick 1
// it holds the door with nine rounds of the order left. After tick 1 every bot of both sides holds
// the door. The route of each (find_path_to_foe, after tick 1 of a world of its own) ends in the
// door's cell (25,16). Then, three seeds, up to 600 ticks: an orc's swing takes hit points off the
// door (a blow shown to that orc at the door's spot) and the archer's shot at it is taken (an arrow
// of its own flies on a tick at whose end it holds the door): both by the draws (a blow at a door
// meets its clash roll). Setting off: after tick 1 every bot holds a bot of the other side.
// Fails when broken: the tick's hand-out handing no door (the orc with the walk order holds none
// after tick 1); idle_walk handing no door (the first orc has no foe after its idle decision).
TEST_F(SemBotBreak, on_the_first_tick_every_shut_out_bot_of_both_sides_is_handed_the_rooms_door)
{
    {
        SCOPED_TRACE("Gameplay Fixes off");
        fresh_world(0);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        world().tick();
        for (walker* w : r.all()) {
            ASSERT_NE(nullptr, w);
            EXPECT_TRUE(w->foe() != nullptr && w->foe()->query_order() == Order::Living && !w->is_friendly(w->foe()))
                << "classic: every bot holds a bot of the other side after tick 1";
        }
    }
    {
        SCOPED_TRACE("Gameplay Fixes on, the routes");
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        world().tick();
        const std::vector<walker*> all = r.all();
        for (std::size_t i = 0; i < all.size(); ++i) {
            walker* w = all[i];
            ASSERT_EQ(r.door, w->foe()) << "bot " << i;
            ASSERT_TRUE(og::reach::walks_to(*w, *r.door, Mode::Shut)) << "the staging: bot " << i << " walks to the door";
            w->find_path_to_foe();
            const std::vector<Cell> route = route_of(w);
            ASSERT_FALSE(route.empty()) << "bot " << i;
            EXPECT_EQ((Cell{25, 16, 0}), route.back()) << "bot " << i << "'s route ends in the door: " << text(route);
        }
    }
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        const std::vector<walker*> all = r.all();
        for (std::size_t i = 0; i < all.size(); ++i) {
            ASSERT_NE(nullptr, all[i]);
            ASSERT_EQ(0u, all[i]->keys()) << "the staging: nobody holds a key";
            ASSERT_TRUE(og::reach::shut_out(*all[i])) << "the staging: bot " << i << " is shut out";
            EXPECT_EQ(r.door, og::reach::breaks(*all[i])) << "bot " << i;
        }
        world().rng_.state_ = seed;
        walker* first = r.orcs.front();
        first->stats()->clear_command();
        (void)first->stats()->idle_walk(*first, 20);
        EXPECT_EQ(r.door, first->foe()) << "the idle decision hands the first orc the door";
        EXPECT_TRUE(first->stats()->commands.empty()) << "and queues no wander";
        EXPECT_EQ(seed, world().rng_.state_) << "and draws nothing";
        walker* last = r.orcs.back();
        last->stats()->clear_command();
        last->stats()->force_command(COMMAND_WALK, 10, 0, 0);
        world().tick();
        for (std::size_t i = 0; i < all.size(); ++i)
            EXPECT_EQ(r.door, all[i]->foe()) << "bot " << i << " holds the door after tick 1";
        ASSERT_FALSE(last->stats()->commands.empty());
        EXPECT_EQ(COMMAND_WALK, last->stats()->commands.front().commandtype) << "the last orc's act ran its walk order";
        EXPECT_EQ(9, last->stats()->commands.front().commandcount) << "and the tick's hand-out gave it the door";
        const Spot door_spot = spot_of(r.door);
        const std::uint32_t door_id = r.door->entity_id();
        walker* archer = r.crew[1];
        ASSERT_EQ(FAMILY_ARCHER, archer->family());
        bool orc_blow = false;
        bool archer_shot = false;
        std::vector<std::uint32_t> arrows;
        for (int tick = 2; tick <= 600 && !(orc_blow && archer_shot); ++tick) {
            world().tick();
            for (walker* o : r.orcs)
                orc_blow = orc_blow || (!o->dead() && blows_at(o, door_spot) > 0);
            const bool holds = world().find_by_id(door_id) != nullptr && archer->foe() != nullptr &&
                               archer->foe()->entity_id() == door_id;
            for (const auto& u : world().weaplist)
                if (u && !u->dead() && u->owner() == archer && u->family() == FAMILY_ARROW &&
                    std::find(arrows.begin(), arrows.end(), u->entity_id()) == arrows.end()) {
                    arrows.push_back(u->entity_id());
                    archer_shot = archer_shot || holds;
                }
        }
        EXPECT_TRUE(orc_blow) << "an orc's swing takes hit points off the door";
        EXPECT_TRUE(archer_shot) << "the archer's shot at the door is taken";
    }
}

// A closet whose door leads to no enemy (door_room, and a closet: walls round the interior x 17..19,
// y 25..27, its door of the room door's side, team 2, level 3, in the closet's right wall at cell
// (20,26), 16 px from the elf). The closet's door bars every crew bot and is nearer to the elf and
// to the archer than the room's door (asserted), but no hostile stands behind it, so it is no door
// the walk leads to: breaks answers the room's door for every crew bot. Three seeds, 600 ticks:
// after every tick no bot holds the closet's door and its hit points are whole; after tick 1 every
// bot holds the room's door.
// Fails when broken: the door walk ending at a region that holds no hostile's approach cell (every
// region the crew does not get to taken as an end: the closet's door gets a count equal to the room
// door's, and the elf and the archer are handed the nearer closet's door). The door with no count
// kept out of the choice (`c >= kNever`) is not this test's: the least count already beats a door
// with none here; a_held_door_that_still_leads_to_an_enemy_is_kept_when_a_better_one_appears holds
// that line.
TEST_F(SemBotBreak, a_closets_door_is_handed_to_nobody_and_takes_no_blow)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        room(17, 25, 19, 27);
        walker* closet = door_in(20, 26, 2, 3);
        ASSERT_NE(nullptr, closet);
        const std::vector<walker*> all = r.all();
        for (walker* c : r.crew) {
            ASSERT_TRUE(og::reach::door_bars(c, closet)) << "the staging: the closet's door bars the crew";
            EXPECT_EQ(r.door, og::reach::breaks(*c));
        }
        for (walker* c : {r.crew[0], r.crew[1]}) {
            const Cell start = og::reach::start_cell(*c);
            ASSERT_LT(manhattan(start, Cell{20, 26, 0}), manhattan(start, Cell{25, 16, 0}))
                << "the staging: the closet's door is the nearer";
        }
        const std::uint32_t closet_id = closet->entity_id();
        const float whole = closet->stats()->hitpoints();
        world().rng_.state_ = seed;
        for (int tick = 1; tick <= 600; ++tick) {
            world().tick();
            const walker* d = world().find_by_id(closet_id);
            ASSERT_NE(nullptr, d) << "tick " << tick;
            ASSERT_EQ(whole, d->stats()->hitpoints()) << "the closet's door takes no blow, tick " << tick;
            for (std::size_t i = 0; i < all.size(); ++i) {
                ASSERT_FALSE(all[i]->foe() != nullptr && all[i]->foe()->entity_id() == closet_id)
                    << "tick " << tick << ", bot " << i << " holds the closet's door";
                if (tick == 1) {
                    EXPECT_EQ(r.door, all[i]->foe()) << "bot " << i << " holds the room's door after tick 1";
                }
            }
        }
    }
}

// An enemy outside the room (door_room, and an orc of team 1 at (560,352), 192 px from the
// barbarian). While it lives every crew bot gets at it (asserted) and is handed no door: after every
// tick until it dies no crew bot holds a door and none was shown a blow at the door's spot. The
// orcs inside get at nobody and hold the door from tick 1. The tick after the one after which the
// outside orc is dead, every live crew bot holds the room's door. Three seeds; the crew kills the
// orc within 600 ticks (the staging).
// Fails when broken: breaks without its "gets at nobody" condition (a crew bot is handed the door
// while the outside orc lives).
TEST_F(SemBotBreak, while_an_enemy_outside_lives_the_crew_holds_no_door_and_strikes_none)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        walker* outside = scene_bot(FAMILY_ORC, 1, 560, 352);
        ASSERT_NE(nullptr, outside);
        for (walker* c : r.crew) {
            ASSERT_TRUE(og::reach::walks_to(*c, *outside, Mode::Shut)) << "the staging: the crew walks to the outside orc";
            EXPECT_EQ(nullptr, og::reach::breaks(*c));
        }
        for (walker* o : r.orcs)
            EXPECT_EQ(r.door, og::reach::breaks(*o));
        const Spot door_spot = spot_of(r.door);
        world().rng_.state_ = seed;
        int died = -1;
        for (int tick = 1; tick <= 600 && died < 0; ++tick) {
            world().tick();
            if (outside->dead()) {
                died = tick;
                break;
            }
            for (std::size_t i = 0; i < r.crew.size(); ++i) {
                const walker* c = r.crew[i];
                ASSERT_FALSE(c->foe() != nullptr && c->foe()->is_door()) << "tick " << tick << ", crew bot " << i;
                ASSERT_EQ(0, blows_at(c, door_spot)) << "tick " << tick << ", crew bot " << i;
            }
            if (tick == 1) {
                for (walker* o : r.orcs)
                    EXPECT_EQ(r.door, o->foe()) << "the orcs hold the door from tick 1";
            }
        }
        ASSERT_GT(died, 0) << "the staging: the crew kills the outside orc within 600 ticks";
        world().tick();
        for (std::size_t i = 0; i < r.crew.size(); ++i) {
            if (r.crew[i]->dead())
                continue;
            EXPECT_EQ(r.door, r.crew[i]->foe()) << "crew bot " << i << " holds the door the tick after (tick " << died + 1 << ")";
        }
    }
}

// An enemy appears while the crew hits the door (door_room). Three seeds: the first tick T, up to
// 600, after which every crew bot holds the door and one of them is in the middle of a swing (its
// attack animation, so its next act only plays the animation and never reaches the top of a round).
// Then an orc of team 1 is placed outside at (560,352), where every crew bot walks to it
// (asserted). After tick T + 1 no crew bot holds the door, the one in its swing included (the keep
// after its act let it go), and by the end of tick T + 3 every live crew bot has held the new orc.
// Fails when broken: the keep after every act not asked (the bot in its swing still holds the door
// after tick T + 1).
TEST_F(SemBotBreak, an_enemy_that_appears_takes_every_door_holder_off_its_door)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        world().rng_.state_ = seed;
        walker* swinging = nullptr;
        int t = -1;
        for (int tick = 1; tick <= 600 && t < 0; ++tick) {
            world().tick();
            bool all_hold = true;
            walker* in_swing = nullptr;
            for (walker* c : r.crew) {
                all_hold = all_hold && !c->dead() && c->foe() == r.door;
                if (c->ani_type() == ANI_ATTACK && in_swing == nullptr)
                    in_swing = c;
            }
            if (all_hold && in_swing != nullptr) {
                t = tick;
                swinging = in_swing;
            }
        }
        ASSERT_GT(t, 0) << "the staging: the crew holds the door with a bot in its swing within 600 ticks";
        walker* orc = scene_bot(FAMILY_ORC, 1, 560, 352);
        ASSERT_NE(nullptr, orc);
        for (walker* c : r.crew)
            ASSERT_TRUE(og::reach::walks_to(*c, *orc, Mode::Shut)) << "the staging";
        std::vector<bool> held(r.crew.size(), false);
        for (int k = 1; k <= 3; ++k) {
            world().tick();
            for (std::size_t i = 0; i < r.crew.size(); ++i) {
                walker* c = r.crew[i];
                if (k == 1) {
                    EXPECT_NE(r.door, c->foe()) << "crew bot " << i << " let the door go one tick after the orc appeared"
                                                << (c == swinging ? " (it was in its swing)" : "");
                }
                held[i] = held[i] || c->foe() == orc;
            }
        }
        for (std::size_t i = 0; i < r.crew.size(); ++i) {
            if (!r.crew[i]->dead()) {
                EXPECT_TRUE(held[i]) << "crew bot " << i << " held the orc within three ticks";
            }
        }
    }
}

// Guards far off (door_room, the four crew bots moved to row 560 and made guards that hold their
// post: og::reach::asks names no guard at its post). Setting on, three seeds, 600 ticks: breaks
// answers nothing for a guard and the door for every orc (asserted before tick 1); after every tick
// no guard holds the door, and after tick 1 every orc holds it. Last, a guard whose foe a level script
// named the door (set_foe) is not asked the keep: it keeps the door (statistics::keeps_door answers
// Kept and leaves it the foe).
// Fails when broken: og::reach::asks without its guard test (the guards are handed the door);
// statistics::keeps_door asking a walker og::reach::asks does not name (the guard lets the door go).
TEST_F(SemBotBreak, guards_far_off_are_handed_nothing_and_the_orcs_inside_are)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        for (walker* c : r.crew) {
            c->setxy(c->xpos(), static_cast<short>(560));
            c->set_act_type(ACT_GUARD);
            c->set_guard_hold_post(true);
            EXPECT_EQ(nullptr, og::reach::breaks(*c)) << "a guard at its post is handed nothing";
        }
        for (walker* o : r.orcs)
            EXPECT_EQ(r.door, og::reach::breaks(*o));
        const std::uint32_t door_id = r.door->entity_id();
        world().rng_.state_ = seed;
        for (int tick = 1; tick <= 600; ++tick) {
            world().tick();
            for (std::size_t i = 0; i < r.crew.size(); ++i)
                ASSERT_FALSE(r.crew[i]->foe() != nullptr && r.crew[i]->foe()->is_door()) << "tick " << tick << ", guard " << i;
            if (tick == 1) {
                for (walker* o : r.orcs)
                    EXPECT_EQ(r.door, o->foe()) << "every orc holds the door after tick 1";
            }
        }
        ASSERT_NE(nullptr, world().find_by_id(door_id)) << "the staging: the door stands";
        walker* guard = r.crew[0];
        guard->set_foe(r.door);
        EXPECT_EQ(statistics::FoeKeep::Kept, guard->stats()->keeps_door(*guard));
        EXPECT_EQ(r.door, guard->foe()) << "a guard keeps the door a script named its foe";
    }
}

// The door walk counts every door entered (og::reach::breaks; painted maps, walls round the room's
// interior x 10..14, y 10..14, a soldier of team 2 sitting far off in the field at (30,30), the orc
// bot in the room at cell (12,10)). (1) Two doors in a row with no floor between: door T1 in the top
// wall at (12,9), door T2 at (12,8) between walls (T1 touches T2, asserted); door B in the bottom
// wall at (12,15). T1 is nearer (1 cell against 5), but through it two doors are entered and
// through B one: B is handed. Without B: T1, the first of the two (T2 touches no region the orc gets
// to, so it is no edge door). (2) The same with floor between: T1 at (12,9), a vestibule cell
// (12,8) between walls, T2 at (12,7) between walls; door L in the left wall at (9,12), 5 cells off:
// L is handed (one door against two). Without L: T1. Setting off: breaks answers nothing.
// Fails when broken: the walk counting nothing for a door entered from a door (the pair counts one:
// with no floor between, T1 is handed against B); the walk entering a door from a region at no cost
// (the vestibule's pair counts one: T1 against L); a door that touches no region the orc gets to
// taken for an edge door (without the single door, T2 is handed).
TEST_F(SemBotBreak, the_door_walk_counts_every_door_entered_and_takes_the_fewest_before_the_nearest)
{
    for (int between = 0; between < 2; ++between) {
        for (int single = 1; single >= 0; --single) {
            for (short fixes : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(between ? "floor between, " : "no floor between, ") +
                             (single ? "with the single door, " : "the pair alone, ") + (fixes ? "Gameplay Fixes on" : "Gameplay Fixes off"));
                fresh_world(fixes);
                walk_room();
                walker* t1 = door_in(12, 9);
                walker* t2 = nullptr;
                if (between) {
                    wall_row(8, 11, 13);
                    tile(0, 12, 8, PIX_GRASS1);
                    wall_row(7, 11, 13);
                    t2 = door_in(12, 7);
                } else {
                    wall_row(8, 11, 13);
                    t2 = door_in(12, 8);
                }
                walker* one = nullptr;
                if (single)
                    one = between ? door_in(9, 12) : door_in(12, 15);
                ASSERT_NE(nullptr, sitter_at(30, 30));
                walker* orc = bot_at(12 * GRID_SIZE, 10 * GRID_SIZE);
                ASSERT_TRUE(t1 && t2 && orc && (!single || one));
                if (!fixes) {
                    EXPECT_EQ(nullptr, og::reach::breaks(*orc));
                    continue;
                }
                const Cell start = og::reach::start_cell(*orc);
                if (single) {
                    ASSERT_LT(manhattan(start, Cell{12, 9, 0}),
                              manhattan(start, Cell{one->xpos() / GRID_SIZE, one->ypos() / GRID_SIZE, 0}))
                        << "the staging: the pair's first door is the nearer";
                }
                og::reach::Asker a(*orc);
                if (!between) {
                    ASSERT_EQ((std::vector<const walker*>{t2}), a.doors_touched(*t1)) << "the staging: T1 touches T2";
                }
                EXPECT_EQ(single ? one : t1, og::reach::breaks(*orc));
            }
        }
    }
}

// Equal door counts go to the nearest door, then the row, then the column (og::reach::breaks; the
// room of the door walk, a soldier of team 2 sitting in the field at (30,30), every door leading to
// it through one door). (1) Doors at (12,9) and (12,15): the orc at (12,10) is handed (12,9), at
// (12,14) handed (12,15) (the nearer). (2) Doors at (12,9) and (9,12), the orc at (12,12): 3 cells
// to each, (12,9) is handed (its row is the smaller). (3) Doors at (11,9) and (13,9), the orc at
// (12,12): 4 cells to each, the same row, (11,9) is handed (its column is the smaller).
// Fails when broken: the tie by distance left out (the orc at (12,14) is handed (12,9)); the tie by
// row left out ((9,12) is handed: its column is the smaller); the tie by column reversed.
TEST_F(SemBotBreak, equal_door_counts_go_to_the_nearest_then_the_row_then_the_column)
{
    struct Case
    {
        const char* name;
        int a[2];
        int b[2];
        int orc[2];
        int want;  // 0: a, 1: b
    };
    const Case cases[] = {
        {"the nearer, from the top", {12, 9}, {12, 15}, {12, 10}, 0},
        {"the nearer, from the bottom", {12, 9}, {12, 15}, {12, 14}, 1},
        {"equal distance, the smaller row", {9, 12}, {12, 9}, {12, 12}, 1},
        {"equal distance and row, the smaller column", {13, 9}, {11, 9}, {12, 12}, 1},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world(1);
        walk_room();
        walker* da = door_in(c.a[0], c.a[1]);
        walker* db = door_in(c.b[0], c.b[1]);
        ASSERT_NE(nullptr, sitter_at(30, 30));
        walker* orc = bot_at(static_cast<short>(c.orc[0] * GRID_SIZE), static_cast<short>(c.orc[1] * GRID_SIZE));
        ASSERT_TRUE(da && db && orc);
        EXPECT_EQ(c.want == 0 ? da : db, og::reach::breaks(*orc));
    }
}

// A bot shut out only from a tent is handed the door to it (og::reach::breaks; the room of the door
// walk, door B in its bottom wall at (12,15), an enemy tent of team 2 inside at (176,176), the orc
// bot outside at cell (12,20), nothing else hostile on the field). Setting on: the orc is shut out
// (the tent is door-shut for it: a generator is judged) and is handed B. Setting off: nothing.
// Fails when broken: the door walk ending only at a living's approach cells (no door is handed).
TEST_F(SemBotBreak, a_bot_shut_out_only_from_a_tent_is_handed_the_door_to_it)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walk_room();
        walker* b = door_in(12, 15);
        walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
        ASSERT_NE(nullptr, tent);
        tent->set_team_num(2);
        place(tent, 176, 176, tent->sizex(), tent->sizey());
        walker* orc = bot_at(12 * GRID_SIZE, 20 * GRID_SIZE);
        ASSERT_TRUE(b && orc);
        if (!fixes) {
            EXPECT_EQ(nullptr, og::reach::breaks(*orc));
            continue;
        }
        ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc, *tent)) << "the staging: only B keeps the orc from the tent";
        EXPECT_EQ(b, og::reach::breaks(*orc));
    }
}

// A bot that gets at an enemy is handed nothing, even with no foe (og::reach::breaks and
// statistics::idle_walk; the room of the door walk, door B in its bottom wall at (12,15), a soldier
// of team 2 sitting outside at cell (12,20); the orc bot inside at cell (12,12); a cloaked soldier of
// team 2 inside at cell (11,11), its cloak at 30000, so a search would seldom see it). The orc is
// shut out (the soldier outside is door-shut for it) and walks to the cloaked one: breaks answers
// nothing and its idle decision hands it nothing: no foe, no wander, no draw (it stays). With the
// cloaked soldier dead: B.
// Fails when broken: breaks without its "gets at nobody" condition (B is handed).
TEST_F(SemBotBreak, a_bot_that_gets_at_an_enemy_is_handed_nothing_even_with_no_foe)
{
    fresh_world(1);
    walk_room();
    walker* b = door_in(12, 15);
    ASSERT_NE(nullptr, sitter_at(12, 20));
    walker* cloaked = sitter_at(11, 11);
    walker* orc = bot_at(12 * GRID_SIZE, 12 * GRID_SIZE);
    ASSERT_TRUE(b && cloaked && orc);
    cloaked->set_invisibility_left(30000);
    ASSERT_TRUE(og::reach::shut_out(*orc)) << "the staging";
    ASSERT_TRUE(og::reach::walks_to(*orc, *cloaked, Mode::Shut)) << "the staging";
    EXPECT_EQ(nullptr, og::reach::breaks(*orc));
    const std::uint32_t s0 = world().rng_.state_;
    (void)orc->stats()->idle_walk(*orc, 20);
    EXPECT_EQ(nullptr, orc->foe()) << "the idle decision hands it nothing";
    EXPECT_TRUE(orc->stats()->commands.empty()) << "it stays: no wander";
    EXPECT_EQ(s0, world().rng_.state_) << "and draws nothing";
    cloaked->set_dead(1);
    EXPECT_EQ(b, og::reach::breaks(*orc)) << "with the cloaked soldier dead";
    // (2) Its shot lands on an enemy it has no way to: a wall of water across row 17 with no
    // crossing; an archer bot of team 1 at (160,320), its line of sight 12; a soldier of team 2
    // across the water at (160,224), no way to it even with every door open, but the archer's shot
    // lands on it (asserted); a pen (walls round the interior x 5..7, y 23..25, its gap at (6,22)
    // holding an enemy door) below the water with a soldier of team 2 inside, door-shut for the
    // archer. The archer gets at the soldier across the water by its shot: nothing is handed. With
    // that soldier dead: the pen's door.
    fresh_world(1);
    wall_row(17, 0, 39, PIX_WATER1);
    const Cell gap = pen(4, 22, 8, 26);
    walker* pen_door = door_at(static_cast<short>(gap.x * GRID_SIZE), static_cast<short>(gap.y * GRID_SIZE));
    walker* penned = sitter_at(6, 24);
    walker* archer = living_at(FAMILY_ARCHER, 1, 160, 320, 16, 16);
    walker* across = sitter_at(10, 14);
    ASSERT_TRUE(pen_door && penned && archer && across);
    archer->set_lineofsight(12);
    ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*archer, *penned)) << "the staging";
    ASSERT_EQ(Exit::NoWayOpen, og::reach::door_shut_exit(*archer, *across)) << "the staging: no way across";
    ASSERT_TRUE(og::reach::shoots_at(*archer, *across)) << "the staging: its shot lands across the water";
    EXPECT_EQ(nullptr, og::reach::breaks(*archer)) << "(2) it gets at the soldier across the water by its shot";
    across->set_dead(1);
    EXPECT_EQ(pen_door, og::reach::breaks(*archer)) << "(2) with that soldier dead";
}

// A bot shut out by no door, or standing where it has no region, is handed nothing
// (og::reach::breaks; the room of the door walk, door B in its bottom wall at (12,15), a soldier of
// team 2 sitting inside at cell (12,12)). (1) An orc bot of team 1 outside at cell (12,20): B is
// handed (the control). (2) The same orc inside a wall tile of the room, at cell (16,12) painted as
// wall (its start cell has no region): nothing. (3) The soldier gone: the orc is shut out by nothing
// (no hostile only a door keeps it from): nothing. (4) B of the orc's own side (team 1) with the
// soldier back: no door bars the orc (it walks to the soldier): nothing. Setting off: nothing.
// Fails when broken: door_bars without its own-side test ((4): its own side's door bars the orc, the
// soldier is door-shut for it and B is handed). Cases (2) and (3) are each held by two lines, so no
// one broken line turns them red: (2) by door_shut_exit's NoRegion exit (every hostile answers it,
// so the orc is shut out by nothing) and by the door walk's own guard against a start cell with no
// region; (3) by the walk reading the orc shut out by nothing and by its having no hostile's region
// to end at.
// SemReach.each_exit_of_door_shut_gives_todays_answer holds door_shut_exit's exits.
TEST_F(SemBotBreak, a_bot_standing_inside_a_wall_or_not_shut_out_is_handed_nothing)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walk_room();
        walker* b = door_in(12, 15);
        walker* soldier = sitter_at(12, 12);
        walker* orc = bot_at(12 * GRID_SIZE, 20 * GRID_SIZE);
        ASSERT_TRUE(b && soldier && orc);
        EXPECT_EQ(fixes ? b : nullptr, og::reach::breaks(*orc)) << "(1) the control";
        if (!fixes)
            continue;
        tile(0, 16, 12, PIX_H_WALL1);
        orc->setxy(static_cast<short>(16 * GRID_SIZE), static_cast<short>(12 * GRID_SIZE));
        ASSERT_LT(og::reach::region_of(*orc, og::reach::start_cell(*orc), Mode::Shut), 0) << "the staging: no region";
        EXPECT_EQ(nullptr, og::reach::breaks(*orc)) << "(2) inside a wall";
        orc->setxy(static_cast<short>(12 * GRID_SIZE), static_cast<short>(20 * GRID_SIZE));
        ASSERT_EQ(b, og::reach::breaks(*orc)) << "the staging: back outside";
        soldier->set_dead(1);
        EXPECT_FALSE(og::reach::shut_out(*orc)) << "the staging";
        EXPECT_EQ(nullptr, og::reach::breaks(*orc)) << "(3) shut out by nothing";
    }
    fresh_world(1);
    walk_room();
    walker* own = door_in(12, 15, 1);
    ASSERT_NE(nullptr, sitter_at(12, 12));
    walker* orc = bot_at(12 * GRID_SIZE, 20 * GRID_SIZE);
    ASSERT_TRUE(own && orc);
    EXPECT_EQ(nullptr, og::reach::breaks(*orc)) << "(4) no door bars the orc";
}

// The door walk follows stair links at no cost and one way (og::reach::breaks; two floors, floor 1 a
// field of grass). On floor 0: room A (walls round the interior x 4..8, y 4..8) with an up-stair at
// (5,5); room C (walls round the interior x 14..18, y 4..8), into which floor 1's down-stair at
// (16,5) lands; C's bottom wall holds door D at (16,9) and, below it between walls, door D2 at
// (16,10): two doors in a row to the field, where a soldier of team 2 sits at (16,20). (1) An orc bot
// in A at cell (6,6) gets to C only by the stairs (up from A, down into C) and is handed D. (2) With
// a door Db in A's bottom wall at (6,9), one door to the field: the orc in A is handed Db (one door
// against two), and an orc in C at cell (16,6), whom no stair takes back up, is handed D: A, and Db
// with it, is no region it gets to.
// Fails when broken: the regions the bot gets to read without the stair links ((1): nothing is
// handed); the stair links read both ways ((2): the orc in C is handed Db).
TEST_F(SemBotBreak, the_door_walk_follows_stair_links_at_no_cost_and_one_way)
{
    for (int with_db = 0; with_db < 2; ++with_db) {
        SCOPED_TRACE(with_db ? "(2) with Db" : "(1) without Db");
        fresh_world(1);
        two_floors();
        room(4, 4, 8, 8);
        room(14, 4, 18, 8);
        tile(0, 5, 5, PIX_ZSTAIR_UP);
        tile(1, 16, 5, PIX_ZSTAIR_DOWN);
        walker* d = door_in(16, 9);
        tile(0, 15, 10, PIX_H_WALL1);
        tile(0, 17, 10, PIX_H_WALL1);
        walker* d2 = door_in(16, 10);
        walker* db = with_db ? door_in(6, 9) : nullptr;
        ASSERT_NE(nullptr, sitter_at(16, 20));
        walker* in_a = bot_at(6 * GRID_SIZE, 6 * GRID_SIZE);
        ASSERT_TRUE(d && d2 && in_a && (!with_db || db));
        if (!with_db) {
            ASSERT_TRUE(og::reach::shut_out(*in_a)) << "the staging";
            EXPECT_EQ(d, og::reach::breaks(*in_a)) << "(1) C is got to by the stairs";
            continue;
        }
        walker* in_c = bot_at(16 * GRID_SIZE, 6 * GRID_SIZE);
        ASSERT_NE(nullptr, in_c);
        EXPECT_EQ(db, og::reach::breaks(*in_a)) << "(2) the orc in A";
        EXPECT_EQ(d, og::reach::breaks(*in_c)) << "(2) the orc in C";
    }
}

// The keep (og::reach::keeps_door and statistics::keeps_door). The room of the door walk with the
// pair T1 (12,9), T2 (12,8) (no floor between) in its top wall and door B in its bottom wall at
// (12,15) opening into a pocket (walls round the interior x 10..14, y 16..18); soldier S1 of team 2
// sitting in the field at (12,4); the orc bot at cell (12,12). The pocket empty: B leads to no
// hostile, so T1 is handed (breaks) and held. (1) A soldier S2 appears in the pocket at (12,17):
// breaks now answers B (one door against two), but the held T1 still leads to S1 and is kept
// (keeps_door answers T1; the orc's keep leaves its foe T1). (2) S1 dies: T1 leads to no hostile, the
// conditions still hold, so B is taken and kept (the orc's foe is B after its keep). (3) S2 dies:
// the orc is shut out no more, so the keep lets B go: no foe, no orders, no route. (4) The same, the
// keep reached through the top of a SEARCH round (statistics::do_command; keeps_foe asks a held door
// this rule's keep): handed B back and given a SEARCH order, the orc lets B go in that round, its
// orders cleared.
// Fails when broken: the keep taking the best door whenever there is one (T1 is dropped for B in
// (1)); the keep letting a door go when only another door qualifies (no foe in (2)); a held door
// taken through the question a bot asks of any foe (statistics::keeps_foe without its door arm: in
// (4) the round keeps the door).
TEST_F(SemBotBreak, a_held_door_that_still_leads_to_an_enemy_is_kept_when_a_better_one_appears)
{
    fresh_world(1);
    walk_room();
    room(10, 16, 14, 18);
    walker* t1 = door_in(12, 9);
    wall_row(8, 11, 13);
    walker* t2 = door_in(12, 8);
    walker* b = door_in(12, 15);
    walker* s1 = sitter_at(12, 4);
    walker* orc = bot_at(12 * GRID_SIZE, 12 * GRID_SIZE);
    ASSERT_TRUE(t1 && t2 && b && s1 && orc);
    ASSERT_EQ(t1, og::reach::breaks(*orc)) << "the staging: the pocket is empty";
    orc->set_foe(og::reach::breaks(*orc));
    orc->stats()->clear_command();
    orc->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
    walker* s2 = sitter_at(12, 17);
    ASSERT_NE(nullptr, s2);
    EXPECT_EQ(b, og::reach::breaks(*orc)) << "(1) B is now the best door";
    EXPECT_EQ(t1, og::reach::keeps_door(*orc, *t1)) << "(1) the held door still leads to S1";
    EXPECT_EQ(statistics::FoeKeep::Kept, orc->stats()->keeps_door(*orc));
    EXPECT_EQ(t1, orc->foe()) << "(1) no flip between doors";
    s1->set_dead(1);
    EXPECT_EQ(b, og::reach::keeps_door(*orc, *t1)) << "(2) only B qualifies";
    EXPECT_EQ(statistics::FoeKeep::Kept, orc->stats()->keeps_door(*orc));
    EXPECT_EQ(b, orc->foe()) << "(2) B is taken and kept";
    s2->set_dead(1);
    EXPECT_EQ(nullptr, og::reach::keeps_door(*orc, *b)) << "(3) shut out no more";
    EXPECT_EQ(statistics::FoeKeep::LetGo, orc->stats()->keeps_door(*orc));
    EXPECT_EQ(nullptr, orc->foe()) << "(3) let go";
    EXPECT_TRUE(orc->stats()->commands.empty()) << "(3) its orders cleared";
    EXPECT_TRUE(orc->path_to_foe.empty()) << "(3) its route cleared";
    orc->set_foe(b);
    orc->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
    orc->set_ani_type(ANI_WALK);
    (void)orc->stats()->do_command();
    EXPECT_EQ(nullptr, orc->foe()) << "(4) the SEARCH round let B go";
    EXPECT_TRUE(orc->stats()->commands.empty()) << "(4) its orders cleared";
}

// The keep (statistics::keeps_door, which GameWorld::tick asks after every act) leaves alone a
// walker it does not ask. The pen of each_exit_of_door_shut_gives_todays_answer (walls round cells
// 10..14, an enemy door in its gap), an orc of team 1 inside at cell (12,12) holding the pen's door
// (set_foe), its one enemy a soldier of team 2 outside at cell (12,5), killed before the keep is
// asked: the orc is shut out no more, so a keep asked of it lets the door go (the control: its own
// statistics, the setting on: LetGo, no foe). (1) Its statistics name another orc of its side in
// the open, which follows a third orc and carries a weapon other than its default: Kept, the orc
// keeps the door, and the other orc's leader and weapon are as they were (a let-go clears the
// orders through clear_command, which writes to the statistics' owner). (2) Its statistics name
// nobody: Kept, the door kept; then a world tick asks the keep after the orc's act and reads no
// missing owner. (3) The orc is a seat's fighter (seat 0), its statistics its own: Kept, the door
// stays its foe. (4) The setting off, its statistics its own: Kept, the door stays its foe.
// Fails when broken: statistics::keeps_door without its owner test (in (1) the let-go clears the
// other orc's leader and resets its weapon); statistics::keeps_door without its asks test (in (3)
// and (4) the door is let go).
TEST_F(SemBotBreak, the_keep_leaves_alone_a_seat_the_setting_off_and_a_bot_whose_statistics_name_another_or_nobody)
{
    enum class Case { Control, Another, Nobody, Seat, Off };
    for (const Case c : {Case::Control, Case::Another, Case::Nobody, Case::Seat, Case::Off}) {
        const char* name = c == Case::Control ? "the control" : c == Case::Another ? "(1) its statistics another orc's"
                         : c == Case::Nobody ? "(2) its statistics nobody's" : c == Case::Seat ? "(3) a seat's fighter" : "(4) the setting off";
        SCOPED_TRACE(name);
        fresh_world(c == Case::Off ? 0 : 1);
        walker* const pen_door = pen_with_a_door();
        walker* orc = living_at(FAMILY_ORC, 1, 12 * GRID_SIZE, 12 * GRID_SIZE, 16, 16);
        walker* soldier = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 5 * GRID_SIZE, 16, 16);
        walker* other = living_at(FAMILY_ORC, 1, 400, 400, 16, 16);
        walker* third = living_at(FAMILY_ORC, 1, 432, 400, 16, 16);
        ASSERT_TRUE(pen_door && orc && soldier && other && third);
        if (c == Case::Seat)
            orc->set_user(0);
        orc->stats()->clear_command();  // before the owner is changed: it resets the owner's weapon
        orc->set_foe(pen_door);
        soldier->set_dead(1);
        other->set_leader(third);
        const unsigned short weapon = static_cast<unsigned short>(other->default_weapon() + 1);
        other->set_current_weapon(weapon);
        if (c == Case::Another)
            orc->stats()->set_controller(other);
        if (c == Case::Nobody)
            orc->stats()->set_controller(nullptr);
        const statistics::FoeKeep keep = orc->stats()->keeps_door(*orc);
        if (c == Case::Control) {
            EXPECT_EQ(statistics::FoeKeep::LetGo, keep) << "the staging: a keep asked of the orc lets the door go";
            EXPECT_EQ(nullptr, orc->foe());
            continue;
        }
        EXPECT_EQ(statistics::FoeKeep::Kept, keep);
        EXPECT_EQ(pen_door, orc->foe()) << "the door kept";
        if (c == Case::Another) {
            ASSERT_EQ(third, other->leader()) << "the other orc's leader untouched";
            ASSERT_EQ(weapon, other->current_weapon()) << "the other orc's weapon untouched";
        }
        if (c == Case::Nobody) {
            world().tick();
            EXPECT_EQ(pen_door, orc->foe()) << "after a world tick the door is still its foe";
        }
        orc->stats()->set_controller(orc);
    }
}

// A door of the bot's own side, or one it holds the key to, is never handed (og::reach::breaks; the
// room of the door walk with door D in its bottom wall at (12,15) opening into a yard (walls round
// the interior x 10..14, y 16..18) whose door X (team 3) in its bottom wall at (12,19) opens to the
// field; a soldier of team 2 sitting in the field at (12,30); the orc bot at cell (12,10), 144 px
// above X). (1) D of the orc's own side (team 1): D does not bar it, the room and the yard are one
// region for it, and X is handed though D is the nearer. (2) D an enemy door (team 3, key level 2)
// whose key the orc holds: X is handed. Then, three seeds, the orc plays with X as its foe: its
// route runs through D, which its step opens (D dies with the orc's box within one step of it), and
// it holds X on every tick until then. Setting off: nothing is handed.
// Fails when broken: the door walk taking every listed door, not only the ones that bar the orc (D
// is handed).
TEST_F(SemBotBreak, a_door_of_its_own_side_or_one_it_holds_the_key_to_is_never_handed)
{
    for (int keyed = 0; keyed < 2; ++keyed) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(keyed ? "D an enemy door, its key held, " : "D of the orc's side, ") +
                         (fixes ? "Gameplay Fixes on" : "Gameplay Fixes off"));
            fresh_world(fixes);
            walk_room();
            room(10, 16, 14, 18);
            walker* d = keyed ? door_in(12, 15, kDoorTeam, 2) : door_in(12, 15, 1, 2);
            walker* x = door_in(12, 19);
            walker* soldier = sitter_at(12, 30);
            walker* orc = bot_at(12 * GRID_SIZE, 10 * GRID_SIZE);
            ASSERT_TRUE(d && x && soldier && orc);
            if (keyed)
                orc->set_keys(static_cast<std::uint32_t>(key_level_mask(2)));
            if (!fixes) {
                EXPECT_EQ(nullptr, og::reach::breaks(*orc));
                continue;
            }
            ASSERT_FALSE(og::reach::door_bars(orc, d)) << "the staging: D does not bar the orc";
            EXPECT_EQ(x, og::reach::breaks(*orc));
        }
    }
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, the orc plays, seed " + std::to_string(seed));
        fresh_world(1);
        walk_room();
        room(10, 16, 14, 18);
        walker* d = door_in(12, 15, kDoorTeam, 2);
        walker* x = door_in(12, 19);
        ASSERT_NE(nullptr, sitter_at(12, 30));
        walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 10 * GRID_SIZE);
        ASSERT_TRUE(d && x && orc);
        orc->set_keys(static_cast<std::uint32_t>(key_level_mask(2)));
        const std::uint32_t d_id = d->entity_id();
        const short dx = d->xpos(), dy = d->ypos(), dsx = d->sizex(), dsy = d->sizey();
        world().rng_.state_ = seed;
        int opened = -1;
        for (int tick = 1; tick <= 600 && opened < 0; ++tick) {
            world().tick();
            if (world().find_by_id(d_id) == nullptr) {
                opened = tick;
                break;
            }
            ASSERT_EQ(x, orc->foe()) << "the orc holds X, tick " << tick;
        }
        ASSERT_GT(opened, 0) << "D opens within 600 ticks";
        const short reach = static_cast<short>(std::ceil(orc->stepsize()));
        EXPECT_NE(0, ::collide(static_cast<short>(orc->xpos() - reach), static_cast<short>(orc->ypos() - reach),
                               static_cast<short>(orc->sizex() + 2 * reach), static_cast<short>(orc->sizey() + 2 * reach),
                               dx, dy, dsx, dsy))
            << "the orc's step opened D on tick " << opened;
        EXPECT_FALSE(x->dead()) << "X stands";
    }
}

// A bot handed the key of the door it holds lets the door go and takes the enemy behind it (the
// keep is asked of any bot whose foe is a live door, whether that door bars it or not). The room
// of the door walk, door D (team 3, key level 2) in its bottom wall at (12,15), a soldier of team 2
// sitting inside at cell (12,12); the bot outside at cell (12,20), an orc and then an archer. After
// tick 1 it holds D (handed). It is given D's key after the first tick at whose end it holds D, has
// nothing of its own in flight and is not in a swing: D bars it no more, so D is no edge door and it
// is shut out no more. After the next tick it holds the soldier (which it now walks to with the doors
// that bar it shut). Then, up to 600 ticks: it never holds D again; it starts no arrow (an archer's
// line check is refused by D, a door that is not its foe, so it no longer shoots at the door); D
// opens by the bot's own step (D dies with the bot's box within one step of it). Setting off: after
// tick 1 it holds the soldier.
// Fails when broken: the keep asked only of a door that bars the bot (it keeps D after the key: the
// archer goes on shooting at it).
TEST_F(SemBotBreak, a_bot_handed_the_key_of_the_door_it_holds_lets_it_go_and_takes_the_enemy_behind_it)
{
    const std::uint32_t key2 = static_cast<std::uint32_t>(key_level_mask(2));
    for (int family : {FAMILY_ORC, FAMILY_ARCHER}) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(family == FAMILY_ORC ? "an orc, " : "an archer, ") + (fixes ? "Gameplay Fixes on" : "Gameplay Fixes off"));
            fresh_world(fixes);
            walk_room();
            walker* d = door_in(12, 15, kDoorTeam, 2);
            walker* soldier = sitter_at(12, 12);
            walker* bot = scene_bot(family, 1, 12 * GRID_SIZE, 20 * GRID_SIZE);
            ASSERT_TRUE(d && soldier && bot);
            world().tick();
            if (!fixes) {
                EXPECT_EQ(soldier, bot->foe()) << "classic: it holds the soldier after tick 1";
                continue;
            }
            ASSERT_EQ(d, bot->foe()) << "the staging: it holds D after tick 1";
            const std::uint32_t d_id = d->entity_id();
            const short dx = d->xpos(), dy = d->ypos(), dsx = d->sizex(), dsy = d->sizey();
            int keyed = 1;
            while (keyed <= 300 && !(bot->foe() == d && weapons_owned_by({bot}) == 0 && bot->ani_type() == ANI_WALK)) {
                world().tick();
                ++keyed;
            }
            ASSERT_LE(keyed, 300) << "the staging: it holds D with nothing in flight";
            bot->set_keys(bot->keys() | key2);
            world().tick();
            EXPECT_EQ(soldier, bot->foe()) << "after the tick after the key it holds the soldier";
            EXPECT_TRUE(og::reach::walks_to(*bot, *soldier, Mode::Shut)) << "which it now walks to";
            std::vector<std::uint32_t> arrows;
            for (const auto& u : world().weaplist)
                if (u && u->owner() == bot)
                    arrows.push_back(u->entity_id());
            int opened = -1;
            int new_arrows = 0;
            for (int tick = keyed + 2; tick <= 600 && opened < 0; ++tick) {
                world().tick();
                if (world().find_by_id(d_id) == nullptr) {
                    opened = tick;
                    break;
                }
                ASSERT_FALSE(bot->foe() != nullptr && bot->foe()->entity_id() == d_id) << "it holds D again, tick " << tick;
                for (const auto& u : world().weaplist)
                    if (u && u->owner() == bot && u->family() == FAMILY_ARROW &&
                        std::find(arrows.begin(), arrows.end(), u->entity_id()) == arrows.end()) {
                        arrows.push_back(u->entity_id());
                        ++new_arrows;
                    }
            }
            EXPECT_EQ(0, new_arrows) << "no arrow is started after it let D go";
            ASSERT_GT(opened, 0) << "D opens within 600 ticks";
            const short reach = static_cast<short>(std::ceil(bot->stepsize()));
            EXPECT_NE(0, ::collide(static_cast<short>(bot->xpos() - reach), static_cast<short>(bot->ypos() - reach),
                                   static_cast<short>(bot->sizex() + 2 * reach), static_cast<short>(bot->sizey() + 2 * reach),
                                   dx, dy, dsx, dsy))
                << "its step opened D on tick " << opened;
        }
    }
}

// A snapshot taken while a bot holds a door plays on identically: the door it holds is the only
// state the rule reads, and it is its foe, which a snapshot carries (no table of handed doors). The
// room of the door walk, door D in its bottom wall at (12,15), a soldier of team 2 sitting inside at
// cell (12,12), an orc bot outside at cell (12,22). After tick 1 the orc holds D (asserted); the
// orders and routes, which a snapshot does not carry, are cleared in the world, and a keyframe is
// taken. The world plays 30 ticks, the soldier killed after the 10th (the orc is then shut out no
// more and lets D go, asserted), and its keyframe is taken. A fresh world with the first keyframe
// applied plays the same 30 ticks with the same kill: its keyframe equals the first world's byte for
// byte, and differs from the keyframe the two started from.
// Fails when broken: the keep asked only of a door the rule handed in this world (a table the
// snapshot does not carry: the restored orc keeps D after the soldier's death and the keyframes
// differ).
TEST_F(SemBotBreak, a_snapshot_taken_while_a_bot_holds_a_door_plays_on_identically)
{
    const auto clear_orders = [this] {
        for (auto* list : {&world().oblist, &world().weaplist, &world().fxlist})
            for (const auto& u : *list) {
                if (u == nullptr)
                    continue;
                if (u->stats() != nullptr)
                    u->stats()->commands.clear();
                u->path_to_foe.clear();
            }
    };
    fresh_world(1);
    walk_room();
    walker* d = door_in(12, 15);
    walker* soldier = sitter_at(12, 12);
    walker* orc = scene_bot(FAMILY_ORC, 1, 12 * GRID_SIZE, 22 * GRID_SIZE);
    ASSERT_TRUE(d && soldier && orc);
    world().rng_.state_ = 42u;
    world().tick();
    ASSERT_EQ(d, orc->foe()) << "the staging: the orc holds D";
    const std::uint32_t orc_id = orc->entity_id();
    const std::uint32_t soldier_id = soldier->entity_id();
    const std::uint32_t d_id = d->entity_id();
    clear_orders();
    const std::vector<std::uint8_t> start = og::sim::serialize_snapshot(og::sim::capture_keyframe_snapshot(world()));
    const auto play = [&](bool first) {
        for (int tick = 1; tick <= 30; ++tick) {
            world().tick();
            if (tick == 10) {
                walker* s = world().find_by_id(soldier_id);
                ASSERT_NE(nullptr, s);
                s->set_dead(1);
            }
            if (first && tick == 11) {
                const walker* o = world().find_by_id(orc_id);
                ASSERT_NE(nullptr, o);
                ASSERT_FALSE(o->foe() != nullptr && o->foe()->entity_id() == d_id) << "the staging: the orc let D go";
            }
        }
    };
    play(true);
    const std::vector<std::uint8_t> end_first = og::sim::serialize_snapshot(og::sim::capture_keyframe_snapshot(world()));
    fresh_world(1);
    ASSERT_TRUE(og::sim::apply_snapshot(world(), og::sim::deserialize_snapshot(start)));
    const walker* restored = world().find_by_id(orc_id);
    ASSERT_NE(nullptr, restored);
    ASSERT_NE(nullptr, restored->foe());
    EXPECT_EQ(d_id, restored->foe()->entity_id()) << "the snapshot carries the door it holds";
    play(false);
    const std::vector<std::uint8_t> end_restored = og::sim::serialize_snapshot(og::sim::capture_keyframe_snapshot(world()));
    EXPECT_EQ(end_first, end_restored) << "the restored world plays on identically";
    EXPECT_NE(start, end_first) << "the 30 ticks move the world";
}

// A bot holding a door still judges the door shut (og::reach's judgments: door_bars has no
// exception for the bot's own foe, and the judging flight stops at every door). A wall of water
// across row 17, its one crossing at (10,17) holding an enemy door; an archer bot of team 1 at
// (160,320), its line of sight 12, the door its foe; a soldier of team 2 across at (160,224), on the
// line through the door, inside the archer's firing box and reach. With the door its foe the archer
// is still shut out, is still handed the door (breaks), does not walk to the soldier with the doors
// shut, its shot is not credited (shoots_at), and the soldier is door-shut for it.
// Fails when broken: door_bars excepting the mover's own foe (the archer walks to the soldier, which
// is then no door's); the judging flight passing the archer's own foe (Exit::Shot).
TEST_F(SemBotBreak, a_bot_holding_a_door_still_judges_the_enemy_behind_it_door_shut)
{
    fresh_world(1);
    wall_row(17, 0, 39, PIX_WATER1);
    walker* door = door_in(10, 17);
    walker* archer = living_at(FAMILY_ARCHER, 1, 160, 320, 16, 16);
    walker* soldier = living_at(FAMILY_SOLDIER, 2, 160, 224, 16, 16);
    ASSERT_TRUE(door && archer && soldier);
    archer->set_lineofsight(12);
    ASSERT_TRUE(archer->in_firing_box(0, -96)) << "the staging";
    archer->set_foe(door);
    EXPECT_TRUE(og::reach::shut_out(*archer));
    EXPECT_EQ(door, og::reach::breaks(*archer));
    EXPECT_FALSE(og::reach::walks_to(*archer, *soldier, Mode::Shut)) << "on foot";
    EXPECT_FALSE(og::reach::shoots_at(*archer, *soldier)) << "by shot";
    EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*archer, *soldier));
}

// A door that falls to blows sets the two sides on each other on the next tick (door_room, its door
// given 60 hit points, nobody holding a key, so only blows can bring it down). Three seeds: the door
// falls within 600 ticks; on the tick after the one it fell on every live bot without a leader holds
// a live bot of the other side (the door is gone, so nobody is shut out and no door is handed, and
// the tick's hand-out gives each bot whose foe died with the door the nearest enemy it now gets at).
// Fails when broken: the tick's hand-out handing no fighter (find_nearest_foe's answer not set: the
// door still falls to the bots handed it, and on the tick after some bot holds no foe).
TEST_F(SemBotBreak, a_door_that_falls_to_blows_sets_the_two_sides_on_each_other_on_the_next_tick)
{
    for (std::uint32_t seed : {42u, 1337u, 2025u}) {
        SCOPED_TRACE("Gameplay Fixes on, seed " + std::to_string(seed));
        fresh_world(1);
        const DoorRoom r = door_room();
        ASSERT_NE(nullptr, r.door);
        r.door->stats()->set_hitpoints(60.0f);
        const std::vector<walker*> all = r.all();
        for (walker* w : all) {
            ASSERT_NE(nullptr, w);
            ASSERT_EQ(0u, w->keys()) << "the staging: nobody holds a key";
        }
        const std::uint32_t door_id = r.door->entity_id();
        world().rng_.state_ = seed;
        int fell = -1;
        for (int tick = 1; tick <= 600 && fell < 0; ++tick) {
            world().tick();
            if (world().find_by_id(door_id) == nullptr)
                fell = tick;
        }
        ASSERT_GT(fell, 0) << "the door falls within 600 ticks";
        world().tick();
        std::size_t judged = 0;
        for (std::size_t i = 0; i < all.size(); ++i) {
            walker* w = all[i];
            if (w->dead() || w->leader() != nullptr)
                continue;
            ++judged;
            EXPECT_TRUE(w->foe() != nullptr && !w->foe()->dead() && w->foe()->query_order() == Order::Living &&
                        !w->is_friendly(w->foe()))
                << "bot " << i << " holds a bot of the other side on the tick after the fall (tick " << fell + 1 << ")";
        }
        EXPECT_LT(0u, judged);
    }
}

// Door bots on all four sides of their doors, routed in and resting, in a pen whose doors sit in
// walls and in a ring of door bodies in open ground. The pen: walls round the interior x 19..21, y
// 19..21, an enemy door (team 3) in the middle cell of each wall, (20,18), (20,22), (18,20),
// (22,20), so that a door's neighbours are walls. The ring: eight doors on the cells round (20,20)
// (ring_of_doors), the four in the middle of its sides, (20,19), (20,21), (19,20), (21,20), the
// orcs' doors, in open ground. A soldier of team 2 sits on (20,20), the only hostile; four orc bots
// of team 1, one outside each of those doors: (a) routed in, each 112 px from its door, so that it
// plans its route into the door; (b) resting against it, its box on the door's outer edge, within
// 100 px. Each orc is handed the door on its side (og::reach::breaks, asserted). Setting on, both
// places, both stagings, three seeds, 600 ticks: after every tick each orc holds its own door, in the
// ring as in the pen (the keep keeps the door it holds while it leads to the soldier, and its random
// act's one-in-80 no longer drops it for a fresh search: before that rule an orc that had stepped
// aside toward a corner door could drop its door there and be handed the next door round, a tie of
// door count and distance broken by the row); on every tick an orc's collision body is a door it
// stands outside on that door's side, and in the pen every blow it lands is on its own door and on
// its side (in the ring a swing can strike the door beside the one it aimed at, the pile's order, an
// older fault: nothing is asserted of the neighbour's door there); from the tick it first meets its
// door (at the tick's end its box, grown by a pixel, touches the door's and the door's cell is the
// only node left on its route, its last hop: the collision body is no oracle in open ground, where
// the sidestep after a refused step passes and clears it, and a route round the ring may brush
// another door's corner on its way), on every later tick its box grown by its step (rounded up) on
// every side meets its door's box. That bound is worked out from the rules, not read off a run: the
// follower keeps the door as its last node (it is never reached while the door stands), so each act
// the orc steps toward the door along the axis it meets it on (its step refused once it meets it,
// the one-pixel baby step closing the last pixels), only turns, or takes walkstep's sidestep, which
// is perpendicular to the step and one step long; the next act the follower's convex-corner assist
// slides it back toward alignment (its direct step is blocked, by the door or by the wall beside
// it), so it is never more than one step off the door's line and never moves away along it. Every
// orc meets its door on its last hop within the 600 ticks (asserted at the end, so the bound is
// asked of each orc in each staging). Each orc swings at its own door (its firing delay rose on a tick at whose end it touches its door: the
// bump swings at the door that is its foe, Gameplay Fixes BF's exception) and lands a blow on it (by
// the draws: a blow at a door meets its clash roll), from below and from the right as from above
// and from the left. The case that
// failed before this rule, a melee bot resting above a door set in a wall (it walked away along the
// wall and never struck), is asserted by name: the pen's top door loses hit points. Setting off, the
// pen, staging (b): after tick 1 every orc holds the soldier (the classic hand-out; no door is
// handed).
// Fails when broken: walk_to_foe's near test without its walks_into_door term (no orc ends a tick
// touching its door with only the door's cell left on its route, so the bound is never asked and
// the first meeting's assertion fails for every orc; some land no blow on their own door, and the
// pen's top door may lose none); the follower without its drop of the start cell's node, or its hop from
// the start cell into the door (an orc below or right of its door is walked off the door it met:
// the bound breaks); the door walk's tie by distance left out (every orc is handed the top door,
// the smallest row); the tick's hand-out handing no door (an orc whose own act handed it nothing
// holds no door after tick 1); the random act's line without its holds_door term (in the ring an
// orc is handed another door).
TEST_F(SemBotBreak, door_bots_on_all_four_sides_strike_their_door_from_their_own_side)
{
    struct Side
    {
        const char* name;
        int pen_door[2];
        short pen_cell[2];  // (a): the cell 112 px out
        short pen_rest[2];  // (b): pixels, the box on the door's outer edge
        int ring_door[2];
        short ring_cell[2];
        short ring_rest[2];
    };
    const Side sides[4] = {
        {"above", {20, 18}, {20, 11}, {320, 272}, {20, 19}, {20, 12}, {320, 288}},
        {"below", {20, 22}, {20, 29}, {320, 368}, {20, 21}, {20, 28}, {320, 352}},
        {"left", {18, 20}, {11, 20}, {272, 320}, {19, 20}, {12, 20}, {288, 320}},
        {"right", {22, 20}, {29, 20}, {368, 320}, {21, 20}, {28, 20}, {352, 320}},
    };
    // Does `o` stand on side i of the 16 x 16 door at `d` (its box's centre beyond that edge)?
    const auto on_side = [](std::size_t i, const walker* o, const short d[2]) {
        const int cx = 2 * o->xpos() + o->sizex();  // twice the centre, in pixels
        const int cy = 2 * o->ypos() + o->sizey();
        return i == 0 ? cy <= 2 * d[1] : i == 1 ? cy >= 2 * (d[1] + 16) : i == 2 ? cx <= 2 * d[0] : cx >= 2 * (d[0] + 16);
    };
    {
        SCOPED_TRACE("Gameplay Fixes off, the pen, (b) resting against their doors");
        fresh_world(0);
        room(19, 19, 21, 21);
        walker* soldier = sitter_at(20, 20);
        ASSERT_NE(nullptr, soldier);
        std::vector<walker*> orcs;
        for (const Side& s : sides) {
            ASSERT_NE(nullptr, door_in(s.pen_door[0], s.pen_door[1]));
            walker* o = scene_bot(FAMILY_ORC, 1, s.pen_rest[0], s.pen_rest[1]);
            ASSERT_NE(nullptr, o);
            orcs.push_back(o);
        }
        world().tick();
        for (std::size_t i = 0; i < 4; ++i)
            EXPECT_EQ(soldier, orcs[i]->foe()) << "classic: the orc " << sides[i].name << " holds the soldier after tick 1";
    }
    for (int ring = 0; ring < 2; ++ring) {
        for (int resting = 0; resting < 2; ++resting) {
            for (std::uint32_t seed : {42u, 1337u, 2025u}) {
                SCOPED_TRACE(std::string(ring ? "the ring, " : "the pen, ") +
                             (resting ? "(b) resting against their doors, seed " : "(a) routed in from 112 px, seed ") + std::to_string(seed));
                fresh_world(1);
                std::vector<walker*> doors;
                std::vector<walker*> all_doors;  // every door of the place
                if (ring) {
                    all_doors = ring_of_doors(20, 20);
                    for (const Side& s : sides)
                        for (walker* d : all_doors)
                            if (d != nullptr && d->xpos() == s.ring_door[0] * GRID_SIZE && d->ypos() == s.ring_door[1] * GRID_SIZE)
                                doors.push_back(d);
                } else {
                    room(19, 19, 21, 21);
                    for (const Side& s : sides)
                        doors.push_back(door_in(s.pen_door[0], s.pen_door[1]));
                    all_doors = doors;
                }
                ASSERT_EQ(4u, doors.size());
                ASSERT_NE(nullptr, sitter_at(20, 20));
                std::vector<walker*> orcs;
                short at[4][2] = {};
                std::vector<Spot> spots;
                for (std::size_t i = 0; i < 4; ++i) {
                    const Side& s = sides[i];
                    ASSERT_NE(nullptr, doors[i]);
                    at[i][0] = doors[i]->xpos();
                    at[i][1] = doors[i]->ypos();
                    spots.push_back(spot_of(doors[i]));
                    const short* cell = ring ? s.ring_cell : s.pen_cell;
                    const short* rest = ring ? s.ring_rest : s.pen_rest;
                    walker* o = resting ? scene_bot(FAMILY_ORC, 1, rest[0], rest[1])
                                        : scene_bot(FAMILY_ORC, 1, static_cast<short>(cell[0] * GRID_SIZE),
                                                    static_cast<short>(cell[1] * GRID_SIZE));
                    ASSERT_NE(nullptr, o);
                    ASSERT_EQ(16, o->sizex()) << "the staging: a 16 px orc";
                    ASSERT_EQ(16, o->sizey()) << "the staging: a 16 px orc";
                    orcs.push_back(o);
                }
                const float whole_top = doors[0]->stats()->hitpoints();
                const std::uint32_t top_id = doors[0]->entity_id();
                for (std::size_t i = 0; i < 4; ++i) {
                    ASSERT_EQ(doors[i], og::reach::breaks(*orcs[i])) << sides[i].name;
                    if (resting) {
                        ASSERT_LT(orcs[i]->distance_to_ob(doors[i]), 100) << "the staging: the orc " << sides[i].name << " is near its door";
                    } else {
                        ASSERT_GE(orcs[i]->distance_to_ob(doors[i]), 100) << "the staging: the orc " << sides[i].name << " is far";
                    }
                }
                std::vector<std::vector<int>> seen(4, std::vector<int>(4, 0));
                std::vector<int> first_met(4, -1);
                std::vector<int> met_own(4, 0);
                std::vector<int> swings(4, 0);
                std::vector<float> busy(4, 0.0f);
                for (std::size_t i = 0; i < 4; ++i)
                    busy[i] = orcs[i]->busy();
                world().rng_.state_ = seed;
                for (int tick = 1; tick <= 600; ++tick) {
                    world().tick();
                    for (std::size_t i = 0; i < 4; ++i) {
                        const walker* o = orcs[i];
                        const walker* now = o->foe();
                        ASSERT_EQ(doors[i], now) << "the orc " << sides[i].name << " holds its own door, tick " << tick;
                        const bool rose = o->busy() > busy[i];
                        busy[i] = o->busy();
                        const std::vector<Cell> last_hop{Cell{now->xpos() / GRID_SIZE, now->ypos() / GRID_SIZE, 0}};
                        if (touches(o, now)) {
                            if (first_met[i] < 0 && route_of(o) == last_hop)
                                first_met[i] = tick;
                            if (now == doors[i]) {
                                ++met_own[i];
                                swings[i] += rose ? 1 : 0;
                            }
                        }
                        if (first_met[i] >= 0) {
                            const short reach = static_cast<short>(std::ceil(o->stepsize()));
                            ASSERT_NE(0, ::collide(static_cast<short>(o->xpos() - reach), static_cast<short>(o->ypos() - reach),
                                                   static_cast<short>(o->sizex() + 2 * reach), static_cast<short>(o->sizey() + 2 * reach),
                                                   now->xpos(), now->ypos(), now->sizex(), now->sizey()))
                                << "the orc " << sides[i].name << " at (" << o->xpos() << "," << o->ypos() << ") stays at the door it holds, tick "
                                << tick << " (it first met it on tick " << first_met[i] << ")";
                        }
                        for (std::size_t k = 0; k < 4; ++k) {
                            if (ring && k != i)
                                continue;
                            if (o->collide_ob() == doors[k]) {
                                EXPECT_TRUE(on_side(k, o, at[k])) << "the orc " << sides[i].name << " at (" << o->xpos() << ","
                                                                  << o->ypos() << ") meets the door " << sides[k].name << ", tick " << tick;
                            }
                            const int n = blows_at(o, spots[k]);
                            if (n == seen[i][k])
                                continue;
                            seen[i][k] = n;
                            EXPECT_EQ(i, k) << "the orc " << sides[i].name << " lands a blow on the door " << sides[k].name << ", tick " << tick;
                            EXPECT_TRUE(on_side(k, o, at[k])) << "the orc " << sides[i].name << " at (" << o->xpos() << ","
                                                              << o->ypos() << ") lands a blow on the door " << sides[k].name << ", tick " << tick;
                        }
                    }
                }
                for (std::size_t i = 0; i < 4; ++i) {
                    EXPECT_GE(first_met[i], 0) << "the orc " << sides[i].name << " met its door on its last hop (the bound above was asked)";
                    EXPECT_GT(met_own[i], 0) << "the orc " << sides[i].name << "'s step meets its door";
                    EXPECT_GT(swings[i], 0) << "the orc " << sides[i].name << " swings at its door from its side";
                    EXPECT_GT(seen[i][i], 0) << "the orc " << sides[i].name << " lands a blow on its own door";
                }
                if (!ring && resting) {
                    const walker* top = world().find_by_id(top_id);
                    ASSERT_NE(nullptr, top);
                    EXPECT_LT(top->stats()->hitpoints(), whole_top) << "the orc resting above a door set in a wall strikes it: its door loses hit points";
                }
            }
        }
    }
}

// A big bot whose foe is one door of a two-door doorway (og::reach::plan_route, its door branch).
// A wall across row 20 from x 0 to 35 (the way round the gap of cells 36 to 39) holds two doors of
// team 3 side by side in its cells 20 and 21. A bot of team 1 with a 32 x 32 box at (320,400), door
// A in cell 20 its foe. Setting on: its box set on A's corner cell meets door B as well, so the
// cells the route reads refuse that cell both as a step's end and as a stair's landing (asserted),
// and although the bot walks to A with the doors shut (asserted) the search is not run (the route
// finder enters a cell only by those two tests, so it would only fail): the plan is judged and not
// run, and the solve on the plan's cells, run anyway, is empty. A 16 px orc in its place: the plan
// runs and its route ends in A's cell. Setting off: not judged.
// Fails when broken: the door branch without its test of the goal cell (the big bot's plan reads
// run).
TEST_F(SemBotBreak, a_big_bot_whose_foe_is_one_door_of_a_two_door_doorway_runs_no_search_that_must_fail)
{
    const auto state = [](Cell c) {
        return reinterpret_cast<PathState>(static_cast<intptr_t>(
            static_cast<intptr_t>(c.floor) * FLOOR_STRIDE + c.y * MAP_WIDTH + c.x));
    };
    for (int big = 1; big >= 0; --big) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(big ? "32 px, " : "16 px, ") + (fixes ? "Gameplay Fixes on" : "Gameplay Fixes off"));
            fresh_world(fixes);
            const std::vector<walker*> fence = door_fence(20, {20, 21});
            walker* a = fence[0];
            const short size = big ? 32 : 16;
            walker* bot = living_at(FAMILY_ORC, 1, 320, 400, size, size);
            ASSERT_TRUE(a && fence[1] && bot);
            bot->set_foe(a);
            const og::reach::RoutePlan plan = og::reach::plan_route(*bot, Cell{20, 20, 0}, a);
            EXPECT_EQ(fixes != 0, plan.judged);
            if (!fixes)
                continue;
            ASSERT_TRUE(og::reach::walks_to(*bot, *a, Mode::Shut)) << "the staging: the bot walks to A";
            ASSERT_EQ(!big, plan.cells(320, 320, 0, og::reach::StepTest::Step)) << "the staging: A's corner cell as a step's end";
            ASSERT_EQ(!big, plan.cells(320, 320, 0, og::reach::StepTest::Landing)) << "the staging: and as a landing";
            EXPECT_EQ(!big, plan.run);
            std::vector<PathState> path;
            float cost = 0.0f;
            ensure_pathfinding_state(*current_game)->solve_for(bot, state(plan.start), state(plan.goal), path, cost, &plan.cells);
            if (big) {
                EXPECT_TRUE(path.empty()) << "the solve on the plan's cells could only fail";
            } else {
                ASSERT_FALSE(path.empty());
                EXPECT_EQ((Cell{20, 20, 0}), (Cell{GET_STATE_X(path.back()) / GRID_SIZE, GET_STATE_Y(path.back()) / GRID_SIZE, 0}));
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BL, a melee bot handed a door walks into it: statistics::walks_into_door (a
// melee-only bot og::reach::asks names whose foe is a live door), its three sites (walk_to_foe's
// near test, the one hand-over statistics::hand_door, and the route follower
// walker::follow_path_to_foe), and played scenes of the walk. The pen of
// each_exit_of_door_shut_gives_todays_answer (walls round cells 10..14, an enemy door, team 3, in
// its top gap at cell (12,10)) unless a test says otherwise.
// ---------------------------------------------------------------------------------------------


// walk_to_foe's near test (stats.cpp, its re-plan block), and statistics::walks_into_door. The pen,
// its door D, the soldier inside at cell (12,13); a bot outside at cell (12,6), 64 px above D and
// 112 px from the soldier (asserted), so the near test holds by distance and no foe is within the
// near branch's 100 px; every bot on the re-plan tick of a SEARCH round of nine rounds, its foe D,
// from a stream state whose first rng(300) is not 0. (1) An orc (melee-only), the setting on: the
// near branch is not taken: no ATTACK, its SEARCH order keeps its nine rounds, its route
// (find_path_to_foe, by the far arm) ends in D's cell, and the round draws exactly walk_to_foe's
// rng(300) and the re-plan cadence rng(10), both of the world stream (the unit world installs no
// cosmetic override), and nothing else. (2) The same orc at cell (12,2), 128 px
// from D, on a map of more than 200 bodies (200 orcs of its side far off): the same. Each of the
// following holds D within 100 px and takes the near branch as before: no route, its order ended
// (front count 0), no ATTACK: (3) an archer (it shoots) facing away from D, the setting on;
// (4) the orc as a seat's fighter (seat 0); (5) the orc as a guard at its post; (6) a tower made
// melee-only by the test; (7) the orc with the setting off (D its foe as a level script would
// name it); (8) the orc with the setting off on the crowded map. (9) An orc whose foe is a soldier
// 48 px off in the open, the setting on: the classic near branch, an ATTACK queued and no route.
// Fails when broken: the near test without its walks_into_door term (in (1) and (2) no route and
// the order ended); walks_into_door without its melee-only test (the archer is routed); without its
// og::reach::asks test (the seat's fighter, the guard, the tower and the setting off are routed);
// without its door test (the orc whose foe is a soldier is routed, no ATTACK).
TEST_F(SemBotBreak, a_melee_door_holder_skips_the_near_branch_and_plans_its_route_into_the_door)
{
    enum class Who { Orc, Archer, Seat, Guard, Tower };
    struct Case
    {
        const char* name;
        short fixes;
        Who who;
        bool crowded;
        bool fighter_foe;
        bool routed;  // what the rule derives: the route into the door
    };
    const Case cases[] = {
        {"(1) an orc, the setting on", 1, Who::Orc, false, false, true},
        {"(2) an orc on a crowded map, the setting on", 1, Who::Orc, true, false, true},
        {"(3) an archer, the setting on", 1, Who::Archer, false, false, false},
        {"(4) a seat's fighter, the setting on", 1, Who::Seat, false, false, false},
        {"(5) a guard at its post, the setting on", 1, Who::Guard, false, false, false},
        {"(6) a tower made melee-only, the setting on", 1, Who::Tower, false, false, false},
        {"(7) an orc, the setting off", 0, Who::Orc, false, false, false},
        {"(8) an orc on a crowded map, the setting off", 0, Who::Orc, true, false, false},
        {"(9) an orc whose foe is a soldier, the setting on", 1, Who::Orc, false, true, false},
    };
    ASSERT_EQ(nullptr, cosmetic_rng_override()) << "the staging: the cadence comes from the world stream";
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world(c.fixes);
        walker* door = pen_with_a_door();
        walker* soldier = pen_enemy();
        ASSERT_TRUE(door && soldier);
        const short y = static_cast<short>((c.crowded ? 2 : 6) * GRID_SIZE);
        walker* bot = nullptr;
        switch (c.who) {
            case Who::Archer: bot = living_at(FAMILY_ARCHER, 1, 12 * GRID_SIZE, y, 16, 16); break;
            case Who::Tower: bot = living_at(FAMILY_TOWER1, 1, 12 * GRID_SIZE, y, 16, 16); break;
            default: bot = bot_at(12 * GRID_SIZE, y); break;
        }
        ASSERT_NE(nullptr, bot);
        if (c.who == Who::Seat)
            bot->set_user(0);
        if (c.who == Who::Guard) {
            bot->set_act_type(ACT_GUARD);
            bot->set_guard_hold_post(true);
        }
        if (c.who == Who::Tower) {
            bot->stats()->set_bit_flags(BIT_NO_RANGED, 1);
            ASSERT_TRUE(bot->stationary()) << "the staging: a tower";
        }
        if (c.crowded) {
            ASSERT_NO_FATAL_FAILURE(crowd(200));
            ASSERT_GT(world().myobmap->size(), static_cast<std::size_t>(200)) << "the staging";
        }
        walker* foe = door;
        if (c.fighter_foe) {
            foe = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE + 48, y, 16, 16);
            ASSERT_NE(nullptr, foe);
            ASSERT_EQ(48, bot->distance_to_ob(foe));
        }
        ASSERT_EQ(c.crowded ? 128 : 64, bot->distance_to_ob(door)) << "the staging";
        ASSERT_GT(bot->distance_to_ob(soldier), 100) << "the staging: no foe in the pen within 100 px";
        EXPECT_EQ(c.routed, statistics::walks_into_door(*bot, foe));
        on_a_replan(bot, foe, c.who == Who::Archer ? FACE_UP : FACE_DOWN);
        const std::uint32_t s0 = a_state_whose_first_draw_of(300);
        world().rng_.state_ = s0;
        (void)bot->stats()->walk_to_foe();
        const std::vector<Cell> route = route_of(bot);
        EXPECT_EQ(foe, bot->foe());
        ASSERT_FALSE(bot->stats()->commands.empty());
        if (c.routed) {
            EXPECT_FALSE(attack_queued(bot)) << "the near branch is not taken";
            EXPECT_EQ(COMMAND_SEARCH, bot->stats()->commands.front().commandtype);
            EXPECT_EQ(9, bot->stats()->commands.front().commandcount) << "its order stands";
            EXPECT_FALSE(route.empty()) << "its route is planned";
            if (!route.empty()) {
                EXPECT_EQ((Cell{12, 10, 0}), route.back()) << "and ends in the door's cell: " << text(route);
            }
            EXPECT_EQ(after_a_replans_draws(s0), world().rng_.state_) << "the round draws its rng(300) and the cadence, nothing else";
        } else if (c.fighter_foe) {
            EXPECT_TRUE(attack_queued(bot)) << "the classic near branch";
            EXPECT_TRUE(route.empty()) << text(route);
        } else {
            EXPECT_FALSE(attack_queued(bot));
            EXPECT_EQ(0, bot->stats()->commands.front().commandcount) << "no foe within 100 px: its order ended";
            EXPECT_TRUE(route.empty()) << "no route: " << text(route);
        }
    }
}

// An enemy the door holder cannot get at even with every door open does not pull a melee bot off
// its door (walk_to_foe's near test). The pen, its door D, the soldier inside at cell (12,13); an
// orc outside at cell (12,6); a second soldier U at cell (14,6), 32 px from the orc, either walled
// in (walls on the eight cells round it) or across water (water on those eight cells). The orc is
// shut out (the soldier inside), gets at nobody (U has no way to it with every door open and the
// orc has no shot), and is handed D (og::reach::breaks, asserted). On the re-plan tick of a SEARCH
// round, its foe D, the setting on: it still holds D, queues no ATTACK, keeps its nine rounds, its
// route ends in D's cell, and the world stream holds exactly walk_to_foe's rng(300) and the re-plan
// cadence rng(10): the near branch's draws (find_near_foe's cloak rolls, the ATTACK's 30+rng(25),
// near_swing's) are not made. Before this rule the same bot took the near branch, which counted U
// (U is not door-shut: it has no way even with the doors open), took U as its foe (strictly
// nearer than D) and started an ATTACK toward it. Setting off (D its foe as a level script would
// name it): the classic near branch takes U and queues an ATTACK.
// Fails when broken: the near test without its walks_into_door term (the orc takes U and queues an
// ATTACK with the setting on).
TEST_F(SemBotBreak, an_enemy_a_melee_door_holder_cannot_get_at_does_not_pull_it_off_its_door)
{
    for (int water = 0; water < 2; ++water) {
        for (short fixes : {short{1}, short{0}}) {
            SCOPED_TRACE(std::string(water ? "across water, " : "walled in, ") + (fixes ? "Gameplay Fixes on" : "Gameplay Fixes off"));
            fresh_world(fixes);
            walker* door = pen_with_a_door();
            walker* inside = pen_enemy();
            for (int cy = 5; cy <= 7; ++cy)
                for (int cx = 13; cx <= 15; ++cx)
                    if (cx != 14 || cy != 6)
                        tile(0, cx, cy, water ? PIX_WATER1 : PIX_H_WALL1);
            walker* u = living_at(FAMILY_SOLDIER, 2, 14 * GRID_SIZE, 6 * GRID_SIZE, 16, 16);
            walker* orc = bot_at(12 * GRID_SIZE, 6 * GRID_SIZE);
            ASSERT_TRUE(door && inside && u && orc);
            ASSERT_EQ(32, orc->distance_to_ob(u));
            ASSERT_EQ(64, orc->distance_to_ob(door));
            if (fixes) {
                ASSERT_EQ(Exit::NoWayOpen, og::reach::door_shut_exit(*orc, *u)) << "the staging: no way to U even with every door open";
                ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*orc, *inside)) << "the staging";
                ASSERT_EQ(door, og::reach::breaks(*orc)) << "the staging: the orc is handed the door";
            }
            on_a_replan(orc, door, FACE_DOWN);
            const std::uint32_t s0 = a_state_whose_first_draw_of(300);
            world().rng_.state_ = s0;
            (void)orc->stats()->walk_to_foe();
            const std::vector<Cell> route = route_of(orc);
            if (fixes) {
                EXPECT_EQ(door, orc->foe()) << "it still holds its door";
                EXPECT_FALSE(attack_queued(orc)) << "no ATTACK";
                ASSERT_FALSE(orc->stats()->commands.empty());
                EXPECT_EQ(9, orc->stats()->commands.front().commandcount) << "its order stands";
                EXPECT_FALSE(route.empty()) << "its route is planned";
                if (!route.empty()) {
                    EXPECT_EQ((Cell{12, 10, 0}), route.back()) << text(route);
                }
                EXPECT_EQ(after_a_replans_draws(s0), world().rng_.state_) << "only walk_to_foe's rng(300) and the cadence are drawn";
            } else {
                EXPECT_EQ(u, orc->foe()) << "classic: the near branch takes U";
                EXPECT_TRUE(attack_queued(orc));
                EXPECT_TRUE(route.empty()) << text(route);
            }
        }
    }
}

// The one hand-over of a door (statistics::hand_door), at each of the three places that hand a
// door; an orc (melee-only) and an archer (it shoots), each in its own world. (a) The idle decision
// (statistics::idle_walk): the pen, the soldier inside, the bot outside at cell (12,4). (b) The
// keep's swap (statistics::keeps_door): the room of
// a_held_door_that_still_leads_to_an_enemy_is_kept_when_a_better_one_appears (T1 and T2 in a row in
// its top wall, B in its bottom wall opening into a pocket, soldier S1 in the field above, S2 in the
// pocket), the bot at cell (12,12) holding T1; S1 dies, so B is taken. (c) The tick's hand-out
// (GameWorld::tick): the pen, the bot outside at cell (12,4) with a walk order of ten rounds and no
// step, so its act runs the order and never reaches its idle decision. Each bot's re-plan counter
// is 7 before. After the hand-over the bot holds the door handed (D, B, D); the orc's counter is 0
// and the archer's still 7. Then the orc's first chase (walk_to_foe in a SEARCH round of nine, from a
// stream state s whose first rng(300) is not 0): it re-plans: the world stream holds exactly the
// chase's rng(300) and then one rng(10), the cadence (the unit world installs no cosmetic override,
// so both come from the world stream), its counter is 5 plus that cadence draw, and its route ends
// in the door's cell.
// Fails when broken: hand_door without its counter write (at each place the orc's counter is not
// 0 and its first chase does not re-plan: its counter not 5 plus the cadence, no route).
TEST_F(SemBotBreak, a_melee_bot_handed_a_door_replans_on_its_next_chase_at_each_place_a_door_is_handed)
{
    enum class Place { Idle, Swap, Tick };
    const std::uint32_t s = a_state_whose_first_draw_of(300);
    for (const Place place : {Place::Idle, Place::Swap, Place::Tick}) {
        for (int archer = 0; archer < 2; ++archer) {
            const char* where = place == Place::Idle ? "(a) the idle decision" : place == Place::Swap ? "(b) the keep's swap" : "(c) the tick's hand-out";
            SCOPED_TRACE(std::string(where) + (archer ? ", an archer" : ", an orc"));
            fresh_world(1);
            walker* handed = nullptr;
            walker* bot = nullptr;
            Cell door_cell{};
            const int family = archer ? FAMILY_ARCHER : FAMILY_ORC;
            if (place == Place::Swap) {
                walk_room();
                room(10, 16, 14, 18);
                walker* t1 = door_in(12, 9);
                wall_row(8, 11, 13);
                walker* t2 = door_in(12, 8);
                walker* b = door_in(12, 15);
                walker* s1 = sitter_at(12, 4);
                walker* s2 = sitter_at(12, 17);
                bot = scene_bot(family, 1, 12 * GRID_SIZE, 12 * GRID_SIZE);
                ASSERT_TRUE(t1 && t2 && b && s1 && s2 && bot);
                bot->set_foe(t1);
                s1->set_dead(1);
                ASSERT_EQ(b, og::reach::keeps_door(*bot, *t1)) << "the staging: only B qualifies";
                bot->set_path_check_counter(7);
                EXPECT_EQ(statistics::FoeKeep::Kept, bot->stats()->keeps_door(*bot));
                handed = b;
                door_cell = Cell{12, 15, 0};
            } else {
                walker* door = pen_with_a_door();
                ASSERT_NE(nullptr, pen_enemy());
                bot = scene_bot(family, 1, 12 * GRID_SIZE, 4 * GRID_SIZE);
                ASSERT_TRUE(door && bot);
                ASSERT_EQ(door, og::reach::breaks(*bot)) << "the staging";
                bot->set_path_check_counter(7);
                bot->stats()->clear_command();
                if (place == Place::Idle) {
                    (void)bot->stats()->idle_walk(*bot, 20);
                } else {
                    bot->stats()->force_command(COMMAND_WALK, 10, 0, 0);
                    world().tick();
                    ASSERT_FALSE(bot->stats()->commands.empty());
                    ASSERT_EQ(COMMAND_WALK, bot->stats()->commands.front().commandtype) << "the staging: its act ran the order";
                }
                handed = door;
                door_cell = Cell{12, 10, 0};
            }
            ASSERT_EQ(handed, bot->foe()) << "the door is handed";
            if (archer) {
                EXPECT_EQ(7, bot->path_check_counter()) << "a bot that shoots keeps its counter";
                continue;
            }
            EXPECT_EQ(0, bot->path_check_counter()) << "the orc's counter is set to 0";
            bot->stats()->clear_command();
            bot->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
            bot->stats()->set_last_distance(15000);
            world().rng_.state_ = s;
            (void)bot->stats()->walk_to_foe();
            og::sim::SimRandom expect(s);
            (void)expect.next(300);
            const std::uint32_t cadence = expect.next(10);
            EXPECT_EQ(expect.state_, world().rng_.state_) << "its first chase draws its rng(300) and the cadence, one rng(10)";
            EXPECT_EQ(5 + static_cast<int>(cadence), bot->path_check_counter()) << "and re-plans";
            const std::vector<Cell> route = route_of(bot);
            EXPECT_FALSE(route.empty()) << "its route is planned";
            if (!route.empty()) {
                EXPECT_EQ(door_cell, route.back()) << text(route);
            }
        }
    }
}

// The route follower (walker::follow_path_to_foe) of a melee door holder. The pen of
// door_bots_on_all_four_sides_strike_their_door_from_their_own_side (walls round the interior x
// 19..21, y 19..21, an enemy door in the middle cell of each wall: (20,18), (20,22), (18,20),
// (22,20)), a soldier of team 2 sitting in it at (20,20); four orcs, one resting against each door,
// each holding its own door, facing it, not busy: above at (320,272) and left at (272,320), their
// corners in the cell beside the door; below at (320,367) and right at (367,320), their corners one
// pixel inside the door's cell (the body map lets them rest so: its box test shrinks each box by a
// pixel), so their start cell (og::reach::start_cell) is the cell beyond, (20,23) and (23,20)
// (asserted). Setting on: each route (find_path_to_foe) is its start cell, then the door's cell.
// One call of the follower: for the orcs below and right the node on their start cell is dropped
// (their corner's cell is another cell) and the door's node is kept although their corner stands
// in it; for the orcs above and left the plain rule drops the start node (their corner is on it).
// Each then takes the hop from its start cell into the door's cell (its heading is that hop, one
// step long) and the bump of that step starts a swing (its attack animation, and its firing delay
// is its family's; no other body stands within a step of it); for the orcs below and right the door
// is still their collision body after the call (their sidestep after the refused step meets the
// wall beside the door, which leaves it; the sidestep of the orcs above and left passes in open
// ground and clears it). Setting off, the same four orcs:
// the route and the one step are the plain follower's (the orcs below and right are routed from
// their corner's cell, the door's, and take no step into it). Then, the setting on, step for step
// against the plain follower's rule: an orc routed to a soldier of team 2 in the open, an archer (it
// shoots) routed to its door from seven cells below it, and an orc routed to its door from seven
// cells below it, each turned to face its next step before the call (so no call is spent turning),
// while the full step is free: each call drops the reached nodes and takes the whole step the rule
// derives, as before this fix. A twin of the follower as it was cannot be had in one
// binary, so the oracle is the plain follower's rule worked out here (plain_move).
// Fails when broken: the follower without its start-cell drop (the orcs below and right step away
// from their doors, back to their start cell); the follower without its hop into the door's cell
// (their door's node is taken as reached: no node is left and no step is taken).
TEST_F(SemBotBreak, a_melee_door_holder_below_or_right_of_its_door_steps_from_its_start_cell_into_it)
{
    struct Side
    {
        const char* name;
        int door[2];
        short at[2];
        int start[2];  // the start cell, the setting on
        int dir;       // facing the door
        int hop[2];    // the hop from the start cell into the door's cell
    };
    const Side sides[4] = {
        {"above", {20, 18}, {320, 272}, {20, 17}, FACE_DOWN, {0, 1}},
        {"below", {20, 22}, {320, 367}, {20, 23}, FACE_UP, {0, -1}},
        {"left", {18, 20}, {272, 320}, {17, 20}, FACE_RIGHT, {1, 0}},
        {"right", {22, 20}, {367, 320}, {23, 20}, FACE_LEFT, {-1, 0}},
    };
    for (short fixes : {short{1}, short{0}}) {
        fresh_world(fixes);
        room(19, 19, 21, 21);
        ASSERT_NE(nullptr, sitter_at(20, 20));
        std::vector<walker*> doors;
        std::vector<walker*> orcs;
        for (const Side& s : sides) {
            walker* d = door_in(s.door[0], s.door[1]);
            walker* o = scene_bot(FAMILY_ORC, 1, s.at[0], s.at[1]);
            ASSERT_TRUE(d && o);
            ASSERT_EQ(0, ::collide(o->xpos(), o->ypos(), o->sizex(), o->sizey(), d->xpos(), d->ypos(), d->sizex(), d->sizey()))
                << "the staging: the orc " << s.name << " rests against its door";
            doors.push_back(d);
            orcs.push_back(o);
        }
        for (std::size_t i = 0; i < 4; ++i) {
            const Side& s = sides[i];
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, the orc " : "Gameplay Fixes off, the orc ") + s.name);
            walker* o = orcs[i];
            const Cell door_cell{s.door[0], s.door[1], 0};
            const Cell corner{o->xpos() / GRID_SIZE, o->ypos() / GRID_SIZE, 0};
            const bool inside = i == 1 || i == 3;
            ASSERT_EQ(inside, corner == door_cell) << "the staging: the corner one pixel inside the door's cell";
            o->set_foe(doors[i]);
            o->set_curdir(static_cast<signed char>(s.dir));
            o->set_enddir(static_cast<char>(s.dir));
            o->set_lastx(5.0f);
            o->set_lasty(5.0f);
            o->set_collide_ob(nullptr);
            o->find_path_to_foe();
            const float step = o->stepsize();
            if (fixes) {
                EXPECT_TRUE(statistics::walks_into_door(*o, doors[i]));
                ASSERT_EQ((Cell{s.start[0], s.start[1], 0}), og::reach::start_cell(*o)) << "the staging";
                ASSERT_EQ((std::vector<Cell>{Cell{s.start[0], s.start[1], 0}, door_cell}), route_of(o)) << text(route_of(o));
                o->follow_path_to_foe();
                EXPECT_EQ((std::vector<Cell>{door_cell}), route_of(o)) << "the start node dropped, the door's node kept: " << text(route_of(o));
                EXPECT_EQ(static_cast<float>(s.hop[0]) * step, o->lastx()) << "the hop from its start cell into the door";
                EXPECT_EQ(static_cast<float>(s.hop[1]) * step, o->lasty()) << "the hop from its start cell into the door";
                if (inside) {
                    EXPECT_EQ(doors[i], o->collide_ob()) << "the step meets its door";
                }
                EXPECT_EQ(ANI_ATTACK, o->ani_type()) << "the bump starts a swing";
                EXPECT_EQ(o->fire_frequency(), o->busy()) << "the bump starts a swing";
            } else {
                const PlainMove m = plain_move(o);
                o->follow_path_to_foe();
                EXPECT_EQ(m.left, route_of(o)) << "classic: the plain follower's nodes: " << text(route_of(o));
                EXPECT_EQ(m.any ? static_cast<float>(m.sx) * step : 5.0f, o->lastx()) << "classic: the plain follower's step";
                EXPECT_EQ(m.any ? static_cast<float>(m.sy) * step : 5.0f, o->lasty()) << "classic: the plain follower's step";
                if (inside) {
                    EXPECT_FALSE(m.any) << "classic: no step into the door from inside its cell";
                }
            }
        }
    }
    // Step for step against the plain follower's rule, the setting on.
    enum class Who { OrcToSoldier, ArcherToDoor, OrcToDoor };
    for (const Who who : {Who::OrcToSoldier, Who::ArcherToDoor, Who::OrcToDoor}) {
        SCOPED_TRACE(who == Who::OrcToSoldier ? "an orc routed to a soldier" : who == Who::ArcherToDoor ? "an archer routed to its door" : "an orc routed to its door from afar");
        fresh_world(1);
        room(19, 19, 21, 21);
        ASSERT_NE(nullptr, sitter_at(20, 20));
        walker* door = door_in(20, 22);
        ASSERT_NE(nullptr, door);
        walker* w = nullptr;
        if (who == Who::OrcToSoldier) {
            w = scene_bot(FAMILY_ORC, 1, 5 * GRID_SIZE, 5 * GRID_SIZE);
            walker* s = living_at(FAMILY_SOLDIER, 2, 12 * GRID_SIZE, 11 * GRID_SIZE, 16, 16);
            ASSERT_TRUE(w && s);
            w->set_foe(s);
        } else {
            w = scene_bot(who == Who::ArcherToDoor ? FAMILY_ARCHER : FAMILY_ORC, 1, 20 * GRID_SIZE, 29 * GRID_SIZE);
            ASSERT_NE(nullptr, w);
            w->set_foe(door);
        }
        EXPECT_EQ(who == Who::OrcToDoor, statistics::walks_into_door(*w, w->foe()));
        w->find_path_to_foe();
        const std::size_t nodes = w->path_to_foe.size();
        ASSERT_GE(nodes, 7u) << "the staging: " << text(route_of(w));
        const float step = w->stepsize();
        std::size_t compared = 0;
        for (int call = 0; call < 400; ++call) {
            const PlainMove m = plain_move(w);
            if (!m.any)
                break;
            const float nx = static_cast<float>(w->xpos()) + static_cast<float>(m.sx) * step;
            const float ny = static_cast<float>(w->ypos()) + static_cast<float>(m.sy) * step;
            if (!world().query_passable(nx, ny, w, w->floor(), true))
                break;
            const short dir = w->facing(static_cast<short>(m.sx), static_cast<short>(m.sy));
            w->set_curdir(static_cast<signed char>(dir));
            w->set_enddir(static_cast<char>(dir));
            w->follow_path_to_foe();
            ++compared;
            ASSERT_EQ(m.left, route_of(w)) << "call " << call;
            ASSERT_EQ(static_cast<float>(m.sx) * step, w->lastx()) << "call " << call;
            ASSERT_EQ(static_cast<float>(m.sy) * step, w->lasty()) << "call " << call;
            ASSERT_EQ(static_cast<short>(nx), w->xpos()) << "call " << call << ": the whole step";
            ASSERT_EQ(static_cast<short>(ny), w->ypos()) << "call " << call << ": the whole step";
        }
        EXPECT_GE(compared, nodes - 2) << "every node but the last two was walked to under the comparison";
    }
}

// A melee bot pressed against its door lets it go for an enemy it can get at. The pen of
// door_bots_on_all_four_sides_strike_their_door_from_their_own_side (doors in its four walls, the
// soldier sitting inside), one orc of team 1 routed in from cell (20,11) to the top door. Three
// seeds, two cases: the first tick T, up to 600, after which the orc has met its door (its box
// touched the door's at the end of some tick), holds it and (1) is in the middle of a swing (its
// attack animation, so its next act never reaches the top of a round) or (2) is not. Then a soldier of team 2 is placed in the
// open at cell (20,8), which the orc walks to (asserted). After tick T + 1 the orc holds that
// soldier: whichever keep let the door go in that tick (the keep after its act in (1), the keep at
// the top of its round or after its act in (2)), the tick's hand-out after its act handed it the
// nearest enemy it gets at, the new soldier (the one inside is door-shut and passed over).
// Fails when broken: the keep after every act not asked (in (1) the orc still holds its door after
// tick T + 1).
TEST_F(SemBotBreak, a_melee_bot_pressed_against_its_door_lets_it_go_for_an_enemy_it_can_get_at)
{
    for (int in_swing = 1; in_swing >= 0; --in_swing) {
        for (std::uint32_t seed : {42u, 1337u, 2025u}) {
            SCOPED_TRACE(std::string(in_swing ? "(1) in its swing, seed " : "(2) not in a swing, seed ") + std::to_string(seed));
            fresh_world(1);
            room(19, 19, 21, 21);
            ASSERT_NE(nullptr, sitter_at(20, 20));
            walker* door = door_in(20, 18);
            for (const auto& c : {std::pair<int, int>{20, 22}, {18, 20}, {22, 20}})
                ASSERT_NE(nullptr, door_in(c.first, c.second));
            walker* orc = scene_bot(FAMILY_ORC, 1, 20 * GRID_SIZE, 11 * GRID_SIZE);
            ASSERT_TRUE(door && orc);
            world().rng_.state_ = seed;
            bool met = false;
            int t = -1;
            for (int tick = 1; tick <= 600 && t < 0; ++tick) {
                world().tick();
                met = met || touches(orc, door);
                if (met && orc->foe() == door && (orc->ani_type() == ANI_ATTACK) == (in_swing != 0))
                    t = tick;
            }
            ASSERT_GT(t, 0) << "the staging: the orc pressed against its door within 600 ticks";
            walker* enemy = living_at(FAMILY_SOLDIER, 2, 20 * GRID_SIZE, 8 * GRID_SIZE, 16, 16);
            ASSERT_NE(nullptr, enemy);
            enemy->set_act_type(ACT_SIT);
            ASSERT_TRUE(og::reach::walks_to(*orc, *enemy, Mode::Shut)) << "the staging: the orc walks to the new soldier";
            world().tick();
            EXPECT_EQ(enemy, orc->foe()) << "after tick " << t + 1 << " the orc holds the soldier it can get at";
        }
    }
}

// A bot wider than one cell made melee-only by the test: the big slime (32 x 32), the
// BIT_NO_RANGED flag set on it. (1) A one-cell doorway: a whole wall across row 20, one enemy door
// in its cell 20, a soldier of team 2 sitting above at cell (20,12), the slime below at (320,416).
// Its box cannot pass the doorway even with the door open, so the soldier is no enemy only a door
// keeps it from (door_shut_exit answers NoWayOpen): it is not shut out and is handed no door
// (og::reach::breaks answers nothing). Handed the door all the same (statistics::hand_door, as a
// level script would name it), walks_into_door names it and its plan into the door is judged and
// not run (the door's corner cell, set under its box, reaches into the wall: no search could end
// there); after one tick its keep has let the door go (it is not shut out). (2) A two-door doorway:
// the same wall with doors A and B in its cells 20 and 21. It is shut out (DoorShut) and breaks
// answers A (the nearer, by its corner cell); 600 ticks, three seeds, its magic taken away so that
// it never splits: after every tick it is one 32 px body and holds A, and on every tick at whose end
// its start cell has a region the plan into A is judged and not run (A's corner cell under its box
// meets B: the search could only fail); with its corner's cell a wall cell beside the doorway its
// start cell has no region and the search is run, as Gameplay Fixes BI runs every such search. It
// takes the old walk: a player sees it stay at the doorway and strike the door seldom (not
// asserted here: that walk is the older straight and right-hand walk, which this rule leaves as it
// is). (3) A door alone in open ground, handed
// to it as a script would: the plan runs, its route ends in the door's cell, and the follower
// (called on its own, the world not ticked, so no keep lets the door go) walks it into the door: on
// some call the bump of its step starts a swing while its box touches the door, the only body near.
// Fails when broken: og::reach::plan_route's door branch without its test of the goal cell (in (1)
// and (2) the plan reads run); the follower without its hop from the start cell into the door (in
// (3) its step never meets the door).
TEST_F(SemBotBreak, a_bot_wider_than_a_cell_made_melee_only_at_a_one_cell_and_a_two_door_doorway)
{
    for (int stage = 1; stage <= 3; ++stage) {
        for (std::uint32_t seed : {42u, 1337u, 2025u}) {
            SCOPED_TRACE(std::string(stage == 1 ? "(1) a one-cell doorway" : stage == 2 ? "(2) a two-door doorway" : "(3) a door alone") +
                         ", seed " + std::to_string(seed));
            fresh_world(1);
            walker* a = nullptr;
            walker* b = nullptr;
            walker* soldier = nullptr;
            if (stage <= 2) {
                wall_row(20, 0, 39);
                a = door_in(20, 20);
                if (stage == 2) {
                    b = door_in(21, 20);
                    ASSERT_NE(nullptr, b);
                }
                soldier = sitter_at(20, 12);
                ASSERT_NE(nullptr, soldier);
            } else {
                a = door_at(320, 320);
            }
            walker* big = scene_bot(FAMILY_SLIME, 1, 320, 416);
            ASSERT_TRUE(a && big);
            ASSERT_EQ(32, big->sizex()) << "the staging: wider than one cell";
            ASSERT_EQ(32, big->sizey());
            big->stats()->set_bit_flags(BIT_NO_RANGED, 1);
            big->stats()->set_max_magicpoints(0.0f);  // no SPLIT: it stays one 32 px body
            big->stats()->set_magicpoints(0.0f);
            const Cell a_cell{a->xpos() / GRID_SIZE, a->ypos() / GRID_SIZE, 0};
            if (stage == 1) {
                EXPECT_EQ(Exit::NoWayOpen, og::reach::door_shut_exit(*big, *soldier));
                EXPECT_FALSE(og::reach::shut_out(*big));
                EXPECT_EQ(nullptr, og::reach::breaks(*big)) << "no door is handed to it";
            } else if (stage == 2) {
                EXPECT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*big, *soldier));
                EXPECT_EQ(a, og::reach::breaks(*big)) << "the nearer door of the two";
            }
            statistics::hand_door(*big, a);
            ASSERT_EQ(a, big->foe());
            EXPECT_TRUE(statistics::walks_into_door(*big, a));
            const og::reach::RoutePlan plan = og::reach::plan_route(*big, a_cell, a);
            EXPECT_TRUE(plan.judged);
            if (stage == 1) {
                EXPECT_FALSE(plan.run) << "no search into a doorway its box cannot stand in";
                world().rng_.state_ = seed;
                world().tick();
                EXPECT_NE(a, big->foe()) << "after one tick its keep let the door go";
                continue;
            }
            if (stage == 2) {
                EXPECT_FALSE(plan.run) << "no search that must fail";
                const std::uint32_t a_id = a->entity_id();
                world().rng_.state_ = seed;
                int unrun = 0;
                for (int tick = 1; tick <= 600; ++tick) {
                    world().tick();
                    ASSERT_NE(nullptr, world().find_by_id(a_id)) << "the staging: A stands, tick " << tick;
                    ASSERT_EQ(32, big->sizex()) << "the staging: one 32 px body, tick " << tick;
                    ASSERT_EQ(a, big->foe()) << "it holds A, tick " << tick;
                    const og::reach::RoutePlan p = og::reach::plan_route(*big, a_cell, a);
                    ASSERT_TRUE(p.judged) << "tick " << tick;
                    if (og::reach::region_of(*big, p.start, Mode::Shut) < 0)
                        continue;  // a start cell with no region runs its search (Gameplay Fixes BI's start rule)
                    ++unrun;
                    ASSERT_FALSE(p.run) << "no search that must fail from a start with a region, tick " << tick;
                }
                EXPECT_LT(0, unrun) << "the staging: its start cell had a region on some tick";
                continue;
            }
            EXPECT_TRUE(plan.run);
            big->find_path_to_foe();
            ASSERT_FALSE(big->path_to_foe.empty()) << "its route into the door";
            EXPECT_EQ(a_cell, route_of(big).back()) << text(route_of(big));
            bool met = false;
            for (int call = 0; call < 200 && !met; ++call) {
                big->set_busy(0.0f);
                big->set_ani_type(ANI_WALK);
                big->follow_path_to_foe();
                met = big->ani_type() == ANI_ATTACK && touches(big, a);
                if (big->path_to_foe.empty())
                    break;
            }
            EXPECT_TRUE(met) << "its step meets the door and the bump starts a swing, at (" << big->xpos() << "," << big->ypos() << ")";
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Gameplay Fixes BL: a bot keeps the door it holds through its random act (living::act_random):
// a door holder (statistics::holds_door) does not retarget on the act's one-in-80 draw, so among the
// door rules only the keep lets a door go. The ring of random_act_ring unless a test says otherwise.
// ---------------------------------------------------------------------------------------------

// The random act's one-in-80 (living::act_random, the line that retargets). The ring of
// random_act_ring, the bot holding the left door (19,20); asserted first: afresh og::reach::breaks
// would hand it the OTHER door, the top one, and the keep (og::reach::keeps_door) keeps the one it
// holds. Its route into the left door is planned, its last distance set to 4321, no order queued; the
// world stream set to a state whose draws are act's two coins (rng(5) not 0, then rng(5) 0: the
// random act) and then the act's rng(80), and the bot acts once (living::act). An orc (melee-only)
// and an archer (it shoots), the setting on, the 80 drawing 0 (the one-in-80) and, as the twin,
// not 0: on both draws alike it still holds its left door, the stream holds exactly those three
// draws (the fresh search, find_near_foe, is not asked: it would roll the soldier's cloak, a fourth
// draw), its last distance is 4321 (the full scan, which resets it to 10000, is not run), its route
// is the one it had, it stands where it stood, and its one order is the act's own for a held foe
// outside its firing box, a search of 200 rounds. Setting off, the same staging, the door named its
// foe as a level script would: the classic act on the 80's 0: the door is dropped and searched
// afresh, and the quick search's first candidate, the soldier in the ring (its pile the spiral's
// second, the bot's own never asked; its cloak roll drawn 0), is taken: it holds the soldier, the
// stream holds the three draws and the cloak roll, its last distance is 4321 (the spiral answered
// before the full scan), and its order is the same search of 200. Last, the setting on, the 80
// drawing 0: an orc whose statistics name another orc of its side, as only a restored snapshot
// leaves them (its owner's id resolving to another walker). holds_door has no owner test, so it is
// a door holder and its random act keeps the left door as the others' does (exactly the three
// draws, its last distance 4321, its one order the search of 200), while the keep, whose owner test
// it fails, leaves it alone (statistics::keeps_door answers Kept): nothing lets that door go until
// the door falls or opens.
// Fails when broken: the random act's line without its holds_door term (with the setting on the
// orc and the archer drop the left door on the 80's 0, find_near_foe rolls the soldier's cloak in the
// spiral and in the full scan, the last distance reads 10000, and the idle decision hands them the
// top door; the orc whose statistics name another drops it too).
TEST_F(SemBotBreak, a_door_holder_keeps_its_door_through_the_one_in_eighty_of_its_random_act)
{
    for (int archer = 0; archer < 2; ++archer) {
        for (short fixes : {short{1}, short{0}}) {
            for (int zero = 1; zero >= (fixes ? 0 : 1); --zero) {
                SCOPED_TRACE(std::string(archer ? "an archer, " : "an orc, ") + (fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             (zero ? "the 80 draws 0" : "the 80 draws not 0"));
                fresh_world(fixes);
                const RandomActRing r = random_act_ring(archer ? FAMILY_ARCHER : FAMILY_ORC);
                ASSERT_TRUE(r.bot && r.top && r.left && r.soldier);
                ASSERT_EQ(16, r.bot->sizex()) << "the staging: a 16 px bot";
                ASSERT_EQ(16, r.bot->sizey()) << "the staging: a 16 px bot";
                ASSERT_EQ((Cell{18, 19, 0}), og::reach::start_cell(*r.bot)) << "the staging";
                if (fixes) {
                    ASSERT_EQ(r.top, og::reach::breaks(*r.bot)) << "the staging: afresh it would be handed the other door, the top one";
                    ASSERT_EQ(r.left, og::reach::keeps_door(*r.bot, *r.left)) << "the staging: the keep keeps the door it holds";
                }
                r.bot->set_foe(r.left);
                EXPECT_EQ(fixes != 0, statistics::holds_door(*r.bot, r.left));
                r.bot->find_path_to_foe();
                const std::vector<Cell> route = route_of(r.bot);
                ASSERT_FALSE(route.empty()) << "the staging: a route";
                r.bot->stats()->clear_command();
                r.bot->stats()->set_last_distance(4321);
                const signed char dir = r.bot->curdir();
                ASSERT_EQ(dir, static_cast<signed char>(r.bot->enddir())) << "the staging: no turn is owed";
                const std::uint32_t s0 = fixes ? state_for({{5, kNotZero}, {5, 0}, {80, zero ? 0 : kNotZero}})
                                               : state_for({{5, kNotZero}, {5, 0}, {80, 0}, {20, 0}});
                world().rng_.state_ = s0;
                (void)r.bot->act();
                EXPECT_EQ(fixes ? r.left : r.soldier, r.bot->foe()) << (fixes ? "it still holds its own door" : "classic: the soldier in the ring");
                if (fixes) {
                    EXPECT_EQ(after(s0, {5, 5, 80}), world().rng_.state_) << "exactly the two coins and the 80: the fresh search is not asked";
                    EXPECT_EQ(route, route_of(r.bot)) << "its route is the one it had: " << text(route_of(r.bot));
                } else {
                    EXPECT_EQ(after(s0, {5, 5, 80, 20}), world().rng_.state_) << "classic: the two coins, the 80 and the soldier's cloak roll";
                }
                EXPECT_EQ(4321u, r.bot->stats()->last_distance()) << "the full scan is not run";
                EXPECT_EQ(288, r.bot->xpos());
                EXPECT_EQ(304, r.bot->ypos());
                EXPECT_EQ(dir, r.bot->curdir());
                EXPECT_TRUE(one_order(r.bot, COMMAND_SEARCH, 200)) << "the act's own order for a held foe outside its firing box";
            }
        }
    }
    {
        SCOPED_TRACE("an orc whose statistics name another orc, Gameplay Fixes on, the 80 draws 0");
        fresh_world(1);
        const RandomActRing r = random_act_ring(FAMILY_ORC);
        ASSERT_TRUE(r.bot && r.top && r.left && r.soldier);
        walker* other = living_at(FAMILY_ORC, 1, 2 * GRID_SIZE, 2 * GRID_SIZE, 16, 16);
        ASSERT_NE(nullptr, other);
        other->set_act_type(ACT_SIT);
        r.bot->set_foe(r.left);
        r.bot->find_path_to_foe();
        r.bot->stats()->clear_command();  // before the owner is changed: it resets the owner's weapon
        r.bot->stats()->set_last_distance(4321);
        r.bot->stats()->set_controller(other);
        EXPECT_TRUE(statistics::holds_door(*r.bot, r.left)) << "the one condition has no owner test";
        EXPECT_EQ(statistics::FoeKeep::Kept, r.bot->stats()->keeps_door(*r.bot)) << "the keep is not asked of it";
        const std::uint32_t s0 = state_for({{5, kNotZero}, {5, 0}, {80, 0}});
        world().rng_.state_ = s0;
        (void)r.bot->act();
        EXPECT_EQ(r.left, r.bot->foe()) << "it keeps its door through the one-in-80";
        EXPECT_EQ(after(s0, {5, 5, 80}), world().rng_.state_) << "exactly the two coins and the 80";
        EXPECT_EQ(4321u, r.bot->stats()->last_distance()) << "the full scan is not run";
        EXPECT_TRUE(one_order(r.bot, COMMAND_SEARCH, 200));
        r.bot->stats()->set_controller(r.bot);
    }
}

// Everyone else retargets on the random act's one-in-80 as before (living::act_random,
// statistics::holds_door). The ring of random_act_ring, the setting on, the bot acting once
// (living::act) from a state whose draws are act's two coins (not 0, then 0) and the 80's 0, then
// the cloak rolls a case names. (1) An orc whose foe is the soldier in the ring (a fighter, not a
// door; its act first rolls its lock on the cloaked soldier, rng(20), drawn 0, so it still holds the
// soldier when its random act runs): it retargets: the quick search rolls the soldier's cloak in the
// spiral and passes it over
// (door-shut, Gameplay Fixes BJ), the full scan rolls it again and passes it over too and resets the
// last distance to 10000, so it has no foe and its idle decision hands it the door og::reach::breaks
// answers, the top door; the stream holds the lock roll, the three draws and the two cloak rolls;
// no order is queued. (2) An orc with no foe: the same, without the lock roll (the 80 is drawn
// although it holds none). (3) An orc whose
// foe is a soldier F far off at cell (2,36), which it walks to, with a soldier N of team 2 at cell
// (21,18), beside the ring, in the spiral's first pile: it takes N, the near foe find_near_foe answers
// (asserted before the act; not cloaked, so it is judged without a draw), the stream holds the three
// draws, the last distance is 4321, its order the search of 200. Its foe a door as a level script
// would name it, the left door: (4) a guard at its post: holds_door answers no; its random act never
// runs (its act is the guard's, walker::act_guard, which asks find_near_foe on every act): from a
// state whose draws are the soldier's cloak roll 0 and the fire order's rng(30) it holds the soldier,
// with a fire order queued and exactly those two draws; (5) a tower (16 x 16): holds_door answers no;
// the three draws and the cloak roll 0: the classic retarget takes the soldier (nothing asks it about
// doors); (6) a seat's fighter (the orc given seat 0, its act type left ACT_RANDOM, as only a test or
// a restored snapshot makes it: every place that seats a fighter sets ACT_CONTROL, whose act never
// reaches the random act): the same as the tower. (7) An orc whose foe is the left door, dead (as on
// the tick a blow brings it down, before the door is taken off the field; the ring is then open
// there and the orc walks to the soldier, asserted): holds_door answers no, so the keep is not
// asked and leaves it holding the dead door (statistics::keeps_door answers Kept: its next act
// forgets a dead foe, living::act), and on that act the random act retargets as the tower's does
// (the three draws and the cloak roll 0, the soldier taken, the last distance 4321, a search of 200).
// Fails when broken: holds_door answering yes for a bot whose foe is a fighter (in (1) the orc keeps
// the soldier, its stream holds the lock roll and three draws; in (3) it keeps F); holds_door without its
// og::reach::asks test (the tower and the seat's fighter keep the door, and holds_door answers yes
// for the guard); holds_door without its test of a dead door (in (7) holds_door answers yes and the
// keep, asked of the dead door, lets it go: no foe before the act).
TEST_F(SemBotBreak, everyone_else_retargets_on_the_one_in_eighty_of_the_random_act_as_before)
{
    enum class Who { FoeShut, NoFoe, FoeNear, Guard, Tower, Seat, DeadDoor };
    const struct
    {
        const char* name;
        Who who;
    } cases[] = {
        {"(1) an orc whose foe is the soldier in the ring", Who::FoeShut},
        {"(2) an orc with no foe", Who::NoFoe},
        {"(3) an orc whose foe is a soldier far off, a soldier near", Who::FoeNear},
        {"(4) a guard at its post, its foe the left door", Who::Guard},
        {"(5) a tower, its foe the left door", Who::Tower},
        {"(6) a seat's fighter, its foe the left door", Who::Seat},
        {"(7) an orc whose foe is the left door, dead", Who::DeadDoor},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world(1);
        const RandomActRing r = random_act_ring(c.who == Who::Tower ? FAMILY_TOWER1 : FAMILY_ORC);
        ASSERT_TRUE(r.bot && r.top && r.left && r.soldier);
        walker* const bot = r.bot;
        if (c.who == Who::Tower) {
            bot->set_sizex(16);
            bot->set_sizey(16);
            bot->setxy(289, 304);
            bot->setxy(288, 304);
            ASSERT_TRUE(bot->stationary()) << "the staging: a tower";
        }
        ASSERT_EQ(16, bot->sizex()) << "the staging: a 16 px bot";
        ASSERT_EQ(16, bot->sizey()) << "the staging: a 16 px bot";
        walker* near_foe = nullptr;
        walker* far_foe = nullptr;
        switch (c.who) {
            case Who::FoeShut: bot->set_foe(r.soldier); break;
            case Who::NoFoe: bot->set_foe(nullptr); break;
            case Who::FoeNear:
                near_foe = living_at(FAMILY_SOLDIER, 2, 21 * GRID_SIZE, 18 * GRID_SIZE, 16, 16);
                far_foe = living_at(FAMILY_SOLDIER, 2, 2 * GRID_SIZE, 36 * GRID_SIZE, 16, 16);
                ASSERT_TRUE(near_foe && far_foe);
                near_foe->set_act_type(ACT_SIT);
                far_foe->set_act_type(ACT_SIT);
                ASSERT_TRUE(og::reach::walks_to(*bot, *far_foe, Mode::Shut)) << "the staging: it walks to F";
                bot->set_foe(far_foe);
                break;
            case Who::Guard:
                bot->set_act_type(ACT_GUARD);
                bot->set_guard_hold_post(true);
                bot->set_foe(r.left);
                break;
            case Who::Tower: bot->set_foe(r.left); break;
            case Who::Seat:
                bot->set_user(0);
                bot->set_foe(r.left);
                break;
            case Who::DeadDoor:
                r.left->set_dead(1);
                bot->set_foe(r.left);
                ASSERT_TRUE(og::reach::walks_to(*bot, *r.soldier, Mode::Shut)) << "the staging: the ring is open at the dead door";
                break;
        }
        if (c.who == Who::FoeShut || c.who == Who::NoFoe) {
            ASSERT_EQ(Exit::DoorShut, og::reach::door_shut_exit(*bot, *r.soldier)) << "the staging";
            ASSERT_EQ(r.top, og::reach::breaks(*bot)) << "the staging";
        }
        EXPECT_FALSE(statistics::holds_door(*bot, bot->foe())) << "not a door holder";
        if (c.who == Who::DeadDoor) {
            EXPECT_EQ(statistics::FoeKeep::Kept, bot->stats()->keeps_door(*bot)) << "the keep is not asked";
            EXPECT_EQ(r.left, bot->foe()) << "the dead door is left for its next act to forget";
        }
        if (c.who == Who::FoeNear) {
            const std::uint32_t before = world().rng_.state_;
            ASSERT_EQ(near_foe, world().find_near_foe(bot)) << "the staging: find_near_foe answers N";
            ASSERT_EQ(before, world().rng_.state_) << "the staging: and judges it without a draw";
        }
        bot->stats()->clear_command();
        bot->stats()->set_last_distance(4321);
        std::uint32_t s0 = 0;
        std::uint32_t want = 0;
        switch (c.who) {
            case Who::FoeShut:
                s0 = state_for({{20, 0}, {5, kNotZero}, {5, 0}, {80, 0}});
                want = after(s0, {20, 5, 5, 80, 20, 20});
                break;
            case Who::NoFoe:
                s0 = state_for({{5, kNotZero}, {5, 0}, {80, 0}});
                want = after(s0, {5, 5, 80, 20, 20});
                break;
            case Who::FoeNear:
                s0 = state_for({{5, kNotZero}, {5, 0}, {80, 0}});
                want = after(s0, {5, 5, 80});
                break;
            case Who::Guard:
                s0 = state_for({{20, 0}});
                want = after(s0, {20, 30});
                break;
            case Who::Tower:
            case Who::Seat:
            case Who::DeadDoor:
                s0 = state_for({{5, kNotZero}, {5, 0}, {80, 0}, {20, 0}});
                want = after(s0, {5, 5, 80, 20});
                break;
        }
        world().rng_.state_ = s0;
        (void)bot->act();
        EXPECT_EQ(want, world().rng_.state_) << "the draws of the classic act";
        switch (c.who) {
            case Who::FoeShut:
            case Who::NoFoe:
                EXPECT_EQ(r.top, bot->foe()) << "searched afresh, it finds no foe and is handed the door breaks answers";
                EXPECT_EQ(10000u, bot->stats()->last_distance()) << "the full scan ran";
                EXPECT_TRUE(bot->stats()->commands.empty()) << "no order";
                break;
            case Who::FoeNear:
                EXPECT_EQ(near_foe, bot->foe()) << "it takes the near foe";
                EXPECT_EQ(4321u, bot->stats()->last_distance());
                EXPECT_TRUE(one_order(bot, COMMAND_SEARCH, 200));
                break;
            case Who::Guard:
                EXPECT_EQ(r.soldier, bot->foe()) << "the guard's act retargets on every act";
                EXPECT_TRUE(one_order(bot, COMMAND_FIRE, -1));
                break;
            case Who::Tower:
            case Who::Seat:
            case Who::DeadDoor:
                EXPECT_EQ(r.soldier, bot->foe()) << "the classic retarget takes the soldier";
                EXPECT_EQ(4321u, bot->stats()->last_distance());
                EXPECT_TRUE(one_order(bot, COMMAND_SEARCH, 200));
                break;
        }
    }
}

// The keep still lets the door go (statistics::keeps_door after every act, then the tick's
// hand-out in GameWorld::tick). The ring of random_act_ring, the setting on, an orc holding the left
// door; a soldier E of team 2 sitting in the open at cell (20,12), which the orc walks to (asserted),
// placed before the tick, so the keep no longer keeps the left door (og::reach::keeps_door answers
// none, asserted). The orc is the first walker of the object list, so the tick's first draws are its
// act's: the world stream set to a state whose draws are act's two coins (not 0, then 0) and the
// 80's 0: on that tick its random act drew the one-in-80. After the tick it holds E, its orders and
// its route are empty: its act kept the door (it queued its search of 200), the keep after its act
// let the door go (no foe, orders and route cleared, as Gameplay Fixes BJ lets a foe go), and the
// tick's hand-out gave it E (find_nearest_foe: the soldier in the ring is door-shut and passed over).
// Fails when broken: the random act's line without its holds_door term (its act takes E by the fresh
// search on the 80's 0, the keep is not asked of a fighter, and after the tick it still has the
// act's search of 200 queued and its route into the left door).
TEST_F(SemBotBreak, the_keep_still_lets_the_door_go_on_the_tick_its_random_act_drew_the_one_in_eighty)
{
    fresh_world(1);
    const RandomActRing r = random_act_ring(FAMILY_ORC);
    ASSERT_TRUE(r.bot && r.top && r.left && r.soldier);
    walker* const orc = r.bot;
    ASSERT_EQ(orc, world().oblist.begin()->get()) << "the staging: the orc acts first in the tick";
    walker* e = sitter_at(20, 12);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(og::reach::walks_to(*orc, *e, Mode::Shut)) << "the staging: the orc walks to E";
    orc->set_foe(r.left);
    ASSERT_TRUE(statistics::holds_door(*orc, r.left));
    ASSERT_EQ(nullptr, og::reach::keeps_door(*orc, *r.left)) << "the staging: the keep lets the door go";
    orc->find_path_to_foe();
    ASSERT_FALSE(orc->path_to_foe.empty()) << "the staging: its route into the left door";
    orc->stats()->clear_command();
    world().rng_.state_ = state_for({{5, kNotZero}, {5, 0}, {80, 0}});
    world().tick();
    EXPECT_EQ(e, orc->foe()) << "after the tick it holds E, not its door";
    EXPECT_TRUE(orc->stats()->commands.empty()) << "the keep after its act cleared its orders";
    EXPECT_TRUE(orc->path_to_foe.empty()) << "and its route: " << text(route_of(orc));
}
