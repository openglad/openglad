/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-4: the rules the review found in stats.cpp.
// Each test is named after the rule it asserts.
//
// Every scene is headless: an open field of grass 1024 px a side (GRID_SIZE
// 16, so a 16x16 body at (320,320) covers exactly tile (20,20)), bodies
// placed in pixels, the sim stream (GameWorld::rng_) pinned to a state whose
// draws are known. The probes move the walker's own box one pixel
// (CHECK_STEP_SIZE): for a body at (320,320) the right probe facing up reads
// tiles (20..21, 20), the right-front one (20..21, 19..20), the front one
// (20, 19..20) and the right-back one (20..21, 20..21).
//
// Two engine facts several tests lean on (the old game at 7ff1654a has both):
//  * walk() only TURNS when the step's direction is not the walker's facing
//    (living::walk's "changed direction" arm: set_enddir(dir) and one turn()
//    step), so a step is a move only when the facing already matches;
//    walkstep(0,0) asks facing(0,0), which is FACE_UP.
//  * turn() moves curdir one step and rewrites lastx/lasty, but leaves
//    enddir; init_fire() then turns toward enddir. With the setting off,
//    turn(d) when curdir is already d steps one counter-clockwise (distance
//    0 takes the +7 arm); with it on (Gameplay Fixes AT) it does nothing.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace {

// A draw and the value a pinned state must give (kAny: any value, kNonZero:
// anything but 0).
constexpr std::uint32_t kAny = 0xFFFFFFFFu;
constexpr std::uint32_t kNonZero = 0xFFFFFFFEu;
struct Draw
{
    std::uint32_t bound;
    std::uint32_t want;
};

// The first RNG state from which the draws come out as `draws` says.
std::uint32_t state_where(std::initializer_list<Draw> draws)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        bool all = true;
        for (const Draw& d : draws) {
            const std::uint32_t got = probe.next(d.bound);
            if (d.want == kNonZero)
                all = all && got != 0;
            else
                all = all && (d.want == kAny || got == d.want);
        }
        if (all)
            return seed;
    }
}

// The state after the draws `bounds` from `state`.
std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// The front (and the back) of a walker's command queue.
struct Cmd
{
    std::size_t size = 0;
    int type = -1;
    int count = -1;
    int com1 = 0;
    int com2 = 0;
};

Cmd front_of(const walker* w)
{
    Cmd f;
    const auto& c = w->stats()->commands;
    f.size = c.size();
    if (!c.empty()) {
        f.type = c.front().commandtype;
        f.count = c.front().commandcount;
        f.com1 = c.front().com1;
        f.com2 = c.front().com2;
    }
    return f;
}

Cmd back_of(const walker* w)
{
    Cmd f;
    const auto& c = w->stats()->commands;
    f.size = c.size();
    if (!c.empty()) {
        f.type = c.back().commandtype;
        f.count = c.back().commandcount;
        f.com1 = c.back().com1;
        f.com2 = c.back().com2;
    }
    return f;
}

// A path node's cell, in pixels, and its floor.
struct Cell
{
    int x = -1;
    int y = -1;
    int floor = -1;
    bool operator==(const Cell&) const = default;
};

Cell cell_of(PathState s)
{
    return Cell{GET_STATE_X(s), GET_STATE_Y(s), GET_STATE_FLOOR(s)};
}

std::string describe(const Cell& c)
{
    return "(" + std::to_string(c.x) + "," + std::to_string(c.y) + " floor " + std::to_string(c.floor) + ")";
}

// The unit step of each facing (UP, UR, R, DR, D, DL, L, UL).
constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

class SemAi4 : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world();
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it.
    void fresh_world(short fixes = 0)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.rng_.state_ = 12345u;
    }

    // Two floors: floor 1 a second field of grass of the same size.
    unsigned char* two_floors()
    {
        GameWorld& w = world();
        w.set_floor_count(2);
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
        std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
                  static_cast<unsigned char>(PIX_GRASS1));
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw), static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
        return w.grid_for_floor(1).data.get();
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
    walker* place(walker* w, short x, short y, short sx = 16, short sy = 16)
    {
        if (w == nullptr)
            return nullptr;
        w->set_sizex(sx);
        w->set_sizey(sy);
        w->setxy(static_cast<short>(x + 1), y);  // force a real move
        w->setxy(x, y);
        return w;
    }

    // A level-4 bot (ACT_RANDOM, no seat), 400 hp, 500 MP, no regeneration,
    // walking, facing right, on `floor`.
    walker* living_at(int family, unsigned char team, short x, short y, short floor = 0)
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
        w->set_busy(0.0f);
        w->set_ani_type(ANI_WALK);
        w->set_floor(floor);
        face(w, FACE_RIGHT);
        return place(w, x, y);
    }

    walker* treasure_at(int family, short x, short y, short sx, short sy)
    {
        return place(world().add_fx_ob(Order::Treasure, family), x, y, sx, sy);
    }

    walker* weapon_at(int family, unsigned char team, short x, short y, short sx, short sy)
    {
        walker* w = world().add_weap_ob(Order::Weapon, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        return place(w, x, y, sx, sy);
    }

    // `n` team-0 soldiers stacked in the far corner: the obmap counts them
    // (obmap::size), nobody within 100 px of the scene is hostile to them.
    void crowd(int n)
    {
        for (int i = 0; i < n; ++i)
            ASSERT_NE(nullptr, living_at(FAMILY_SOLDIER, 0, 980, 980));
    }

    // curdir, enddir and the last move (what turn() leaves: the facing's
    // unit step times stepsize; fire_check aims along it).
    static void face(walker* w, int dir)
    {
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
        w->set_lastx(static_cast<float>(kStep[dir][0]) * w->stepsize());
        w->set_lasty(static_cast<float>(kStep[dir][1]) * w->stepsize());
    }

    static short at(float base, float delta) { return static_cast<short>(base + delta); }
};

}  // namespace

// ---------------------------------------------------------------------------
// The four probes' out-of-range facing (stats.cpp:849, 882, 916)
// ---------------------------------------------------------------------------

// stats.cpp:849 and :882, the default arm. A team-1 soldier at (320,320)
// with curdir 127 (no facing), 10 MP; a team-2 orc whose box overlaps the
// soldier's own spot (322,320) and a magic potion on that spot (324,324).
// A probe of the walker's own spot would collide with the orc (a probe that
// does not separate an overlapped pair is blocked) and eat the potion.
// The right-front and right-back probes have no facing to turn: they answer
// false and query nothing, loud or quiet. Control: facing down, the same
// loud probes (one pixel down-left / up-left, away from the orc, so the
// overlapped pair separates) eat the potion: the scene shows a probe that
// ran.
TEST_F(SemAi4, an_out_of_range_facing_leaves_the_right_front_and_right_back_probes_unasked)
{
    enum class Probe { RightFront, RightBack };
    for (Probe p : {Probe::RightFront, Probe::RightBack}) {
        for (bool quiet : {false, true}) {
            SCOPED_TRACE(std::string(p == Probe::RightFront ? "right-front" : "right-back") +
                         (quiet ? ", quiet" : ", loud"));
            fresh_world();
            walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
            walker* orc = living_at(FAMILY_ORC, 2, 322, 320);
            walker* potion = treasure_at(FAMILY_MAGIC_POTION, 324, 324, 8, 8);
            ASSERT_TRUE(w && orc && potion);
            w->set_curdir(static_cast<signed char>(127));
            w->stats()->set_magicpoints(10.0f);
            w->set_collide_ob(nullptr);
            const std::uint32_t s = world().rng_.state_;
            const auto ask = [&]() {
                return p == Probe::RightFront ? w->stats()->right_forward_blocked(quiet)
                                              : w->stats()->right_back_blocked(quiet);
            };

            EXPECT_FALSE(ask()) << "no facing: the probe answers 'not blocked'";
            EXPECT_EQ(nullptr, w->collide_ob()) << "no probe ran: nothing collided";
            EXPECT_FALSE(potion->dead()) << "no probe ran: the potion stands";
            EXPECT_FLOAT_EQ(10.0f, w->stats()->magicpoints());
            EXPECT_FLOAT_EQ(0.0f, w->busy()) << "no bump swing at the orc";
            EXPECT_EQ(ANI_WALK, w->ani_type());
            EXPECT_EQ(s, world().rng_.state_);

            if (!quiet) {
                w->set_curdir(static_cast<signed char>(FACE_DOWN));
                EXPECT_FALSE(ask()) << "control: the orc separates, the potion never blocks";
                EXPECT_TRUE(potion->dead()) << "control: a loud probe with a facing eats the potion";
                EXPECT_FLOAT_EQ(550.0f, w->stats()->magicpoints());
            }
        }
    }
}

// stats.cpp:916, the default arm. forward_blocked has no early exit: an
// out-of-range facing probes the walker's own spot (delta 0,0). A team-2
// orc overlapping that spot (322,320) blocks it (a zero move does not
// separate the pair): loud, true and collide_ob names the orc; quiet, true
// and collide_ob untouched. Control: the orc moved away (400,320), false.
TEST_F(SemAi4, an_out_of_range_facing_makes_the_front_probe_ask_the_walkers_own_spot)
{
    for (bool quiet : {false, true}) {
        SCOPED_TRACE(quiet ? "quiet" : "loud");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* orc = living_at(FAMILY_ORC, 2, 322, 320);
        ASSERT_TRUE(w && orc);
        w->set_curdir(static_cast<signed char>(127));
        w->set_collide_ob(nullptr);

        EXPECT_TRUE(w->stats()->forward_blocked(quiet)) << "the orc on the walker's own spot blocks";
        if (quiet)
            EXPECT_EQ(nullptr, w->collide_ob()) << "quiet: nothing collided";
        else
            EXPECT_EQ(orc, w->collide_ob()) << "loud: the own-spot probe collided with the orc";

        orc->setxy(400, 320);
        w->set_collide_ob(nullptr);
        EXPECT_FALSE(w->stats()->forward_blocked(quiet)) << "control: an empty own spot is open";
        EXPECT_EQ(nullptr, w->collide_ob());
    }
}

// ---------------------------------------------------------------------------
// right_walk (stats.cpp:972, 978, 1048)
// ---------------------------------------------------------------------------

// stats.cpp:972, the open arm with a straight route. A team-0 soldier at
// (96,96) facing up on open grass, its foe a team-1 orc 304 px east (out of
// the knife's reach, so fire_check refuses and direct_walk takes its step).
// No probe is blocked: right_walk runs direct_walk, which steps east, not
// along the facing (north). The step east is a turn first (the soldier faces
// up): enddir RIGHT, one turn to UP_RIGHT, no move. Control: no foe, so
// direct_walk fails and the curdir fallback steps north (ypos 96 - step).
TEST_F(SemAi4, a_right_walker_in_open_space_steps_straight_at_its_foe)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
        walker* orc = living_at(FAMILY_ORC, 1, 400, 96);
        ASSERT_TRUE(w && orc);
        face(w, FACE_UP);
        w->set_foe(orc);

        EXPECT_TRUE(w->stats()->right_walk());

        EXPECT_EQ(FACE_RIGHT, w->enddir()) << "direct_walk's step east set the heading east";
        EXPECT_EQ(FACE_UP_RIGHT, w->curdir()) << "and turned one step toward it";
        EXPECT_EQ(96, w->xpos()) << "a turn, not a move";
        EXPECT_EQ(96, w->ypos()) << "not the curdir step north";
        EXPECT_EQ(0u, w->stats()->commands.size()) << "out of reach: no ATTACK";

        fresh_world(fixes);
        walker* lone = living_at(FAMILY_SOLDIER, 0, 96, 96);
        ASSERT_NE(nullptr, lone);
        face(lone, FACE_UP);
        EXPECT_TRUE(lone->stats()->right_walk());
        EXPECT_EQ(96, lone->xpos()) << "control: no foe, the curdir step";
        EXPECT_EQ(at(96, -lone->stepsize()), lone->ypos());
        EXPECT_EQ(FACE_UP, lone->curdir());
    }
}

// stats.cpp:972, the probe side effects on a door. A team-1 soldier at
// (320,320) heading up (last move (0,-step)) holding key 2; a level-2 door
// at (335,320), one pixel into the soldier's box (collide() forgives one),
// so only the side probes (one pixel right) meet it. Off: the loud right
// probe opens the door (it dies) and blocks this round; the front is open
// so the walk steps forward. On: the quiet probe answers 'blocked' for the
// keyed door without opening it; the walk steps forward the same.
TEST_F(SemAi4, a_right_walk_side_probe_opens_a_door_only_with_the_setting_off)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* door = weapon_at(FAMILY_DOOR, 3, 335, 320, 16, 16);
        ASSERT_TRUE(w && door);
        door->stats()->set_level(2);
        w->set_keys(static_cast<std::uint32_t>(1u << 2));
        face(w, FACE_UP);
        w->set_lastx(0.0f);
        w->set_lasty(-w->stepsize());

        EXPECT_TRUE(w->stats()->right_walk());

        EXPECT_EQ(320, w->xpos()) << "right blocked, front open: the walk steps forward";
        EXPECT_EQ(at(320, -w->stepsize()), w->ypos());
        if (fixes == 0)
            EXPECT_TRUE(door->dead()) << "setting off: the loud side probe opened the door";
        else
            EXPECT_FALSE(door->dead()) << "setting on: the quiet probe leaves the door shut";
    }
}

// stats.cpp:978, the zero last move (triaged a classic quirk). A soldier
// that never stepped (lastx = lasty = 0) facing up with a wall at its right
// (tile (21,20): right and right-front blocked, front open) takes the
// walk-forward arm with the delta (0,0): walkstep(0,0) is walk(0,0) along
// facing(0,0) = FACE_UP, the walker's own spot, so it stands still this
// tick, facing up, its last move still zero. The old game's right_walk and
// living::walk do the same.
TEST_F(SemAi4, classic_a_wall_hugger_that_never_moved_stands_still_for_a_tick)
{
    fresh_world(0);
    tile(21, 20, PIX_H_WALL1);
    walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(nullptr, w);
    face(w, FACE_UP);
    w->set_lastx(0.0f);
    w->set_lasty(0.0f);

    EXPECT_TRUE(w->stats()->right_walk());

    EXPECT_EQ(320, w->xpos()) << "walkstep(0,0) moves nothing";
    EXPECT_EQ(320, w->ypos());
    EXPECT_FLOAT_EQ(0.0f, w->lastx()) << "the last move stays zero";
    EXPECT_FLOAT_EQ(0.0f, w->lasty());
    EXPECT_EQ(FACE_UP, w->curdir());
}

// stats.cpp:1048, the out-of-range curdir. A soldier on open grass with no
// foe and curdir 127: every probe is open (the right-front and right-back
// ones answer false, the right and front ones ask the empty own spot),
// direct_walk fails (no foe), and the curdir switch's default is
// walkstep(0,0): no move, only walk(0,0)'s turn toward facing(0,0) = FACE_UP
// (enddir UP; turn from 127 lands on (127+1)%8 = UP). A step along any real
// facing would set that facing as enddir instead.
TEST_F(SemAi4, a_right_walker_with_no_facing_ambles_nowhere)
{
    fresh_world(0);
    walker* w = living_at(FAMILY_SOLDIER, 1, 512, 512);
    ASSERT_NE(nullptr, w);
    w->set_curdir(static_cast<signed char>(127));
    w->set_enddir(static_cast<char>(127));
    w->set_lastx(5.0f);
    w->set_lasty(5.0f);

    EXPECT_TRUE(w->stats()->right_walk());

    EXPECT_EQ(512, w->xpos()) << "walkstep(0,0): no move";
    EXPECT_EQ(512, w->ypos());
    EXPECT_EQ(FACE_UP, w->enddir()) << "the (0,0) step's facing is FACE_UP";
    EXPECT_EQ(FACE_UP, w->curdir());
}

// ---------------------------------------------------------------------------
// path_probe's gates (stats.cpp:1113)
// ---------------------------------------------------------------------------

// Gameplay Fixes Y's bump at the bot's own foe, the gates that refuse it.
// A team-1 orc bot (BIT_NO_RANGED) at (320,320) facing up, walls at (20,19)
// and (21,19) so the front and right-front probes are blocked by the grid
// whatever stands there; the bump candidate a team-2 orc at (320,305), its
// box inside both probes' boxes. With every gate passing (the control) the
// quiet probe bumps it: collide_ob the foe, busy 7 (the orc's fire delay),
// the attack row. Each failing gate leaves it alone: (a) no foe, (b) the
// foe dead, (c) the foe on another floor, (d) the foe friendly. In every
// case the walk turns left (enddir LEFT, curdir UP_LEFT), stands at
// (320,320), draws nothing, and the foe keeps its 400 hp.
TEST_F(SemAi4, a_quiet_probe_bumps_only_a_live_hostile_foe_on_the_bots_floor)
{
    enum class Gate { Passes, NoFoe, Dead, OtherFloor, Friendly };
    const auto name = [](Gate g) {
        switch (g) {
            case Gate::Passes: return "every gate passes";
            case Gate::NoFoe: return "(a) no foe";
            case Gate::Dead: return "(b) the foe is dead";
            case Gate::OtherFloor: return "(c) the foe is on another floor";
            case Gate::Friendly: return "(d) the foe is friendly";
        }
        return "?";
    };
    for (Gate g : {Gate::Passes, Gate::NoFoe, Gate::Dead, Gate::OtherFloor, Gate::Friendly}) {
        SCOPED_TRACE(name(g));
        fresh_world(1);
        if (g == Gate::OtherFloor)
            two_floors();
        tile(20, 19, PIX_H_WALL1);
        tile(21, 19, PIX_H_WALL1);
        walker* bot = living_at(FAMILY_ORC, 1, 320, 320);
        walker* foe = living_at(FAMILY_ORC, g == Gate::Friendly ? 1 : 2, 320, 305,
                                g == Gate::OtherFloor ? 1 : 0);
        ASSERT_TRUE(bot && foe);
        ASSERT_TRUE(bot->stats()->query_bit_flags(BIT_NO_RANGED));
        face(bot, FACE_UP);
        bot->set_lastx(0.0f);
        bot->set_lasty(-bot->stepsize());
        bot->set_collide_ob(nullptr);
        if (g != Gate::NoFoe)
            bot->set_foe(foe);
        if (g == Gate::Dead)
            foe->set_dead(1);
        const std::uint32_t s = world().rng_.state_;

        EXPECT_TRUE(bot->stats()->right_walk());

        EXPECT_EQ(320, bot->xpos());
        EXPECT_EQ(320, bot->ypos());
        EXPECT_EQ(FACE_LEFT, bot->enddir()) << "front blocked: the walk turns left";
        EXPECT_EQ(FACE_UP_LEFT, bot->curdir());
        EXPECT_EQ(s, world().rng_.state_) << "nothing drawn";
        EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints());
        if (g == Gate::Passes) {
            EXPECT_EQ(foe, bot->collide_ob()) << "control: the bump at the bot's own foe";
            EXPECT_FLOAT_EQ(7.0f, bot->busy());
            EXPECT_EQ(ANI_ATTACK, bot->ani_type());
        } else {
            EXPECT_EQ(nullptr, bot->collide_ob()) << "a failing gate: no bump";
            EXPECT_FLOAT_EQ(0.0f, bot->busy());
            EXPECT_EQ(ANI_WALK, bot->ani_type());
        }
    }
}

// ---------------------------------------------------------------------------
// direct_walk (stats.cpp:1154, 1174)
// ---------------------------------------------------------------------------

// stats.cpp:1154. A team-0 soldier at (96,96) facing right, a SEARCH(9)
// queued; its foe a team-1 orc 10 px east, in the knife's reach. fire_check
// passes (its one draw: the shot's waver, bound 4): the orders are wiped,
// the walker turns (it already faces the foe, so turn(RIGHT) from RIGHT
// steps one counter-clockwise, to UP_RIGHT: the old game's turn() does the
// same), and an ATTACK of 30+rng(25) rounds is queued (rng(25) pinned to 1:
// 31). It does not move. Control: the orc 200 px east, out of reach: no
// ATTACK, the SEARCH stands and the walker steps.
TEST_F(SemAi4, a_chaser_that_can_land_a_shot_stops_and_attacks)
{
    const std::uint32_t s = state_where({{4, kAny}, {25, 1}});
    walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* orc = living_at(FAMILY_ORC, 1, 106, 96);
    ASSERT_TRUE(w && orc);
    w->set_foe(orc);
    w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
    world().rng_.state_ = s;

    EXPECT_TRUE(w->stats()->direct_walk());

    const Cmd f = front_of(w);
    EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
    EXPECT_EQ(COMMAND_ATTACK, f.type);
    EXPECT_EQ(31, f.count) << "30 + rng(25), the draw pinned to 1";
    EXPECT_EQ(0, f.com1);
    EXPECT_EQ(0, f.com2);
    EXPECT_EQ(state_after(s, {4, 25}), world().rng_.state_) << "the waver, then the duration";
    EXPECT_EQ(FACE_UP_RIGHT, w->curdir()) << "turn(RIGHT) from RIGHT steps counter-clockwise";
    EXPECT_EQ(96, w->xpos());
    EXPECT_EQ(96, w->ypos());

    fresh_world();
    walker* c = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* far = living_at(FAMILY_ORC, 1, 296, 96);
    ASSERT_TRUE(c && far);
    c->set_foe(far);
    c->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
    EXPECT_TRUE(c->stats()->direct_walk());
    const Cmd g = front_of(c);
    EXPECT_EQ(1u, g.size);
    EXPECT_EQ(COMMAND_SEARCH, g.type) << "control: out of reach, the order stands";
    EXPECT_EQ(9, g.count);
    EXPECT_EQ(at(96, c->stepsize()), c->xpos()) << "control: the chase steps east";
}

// stats.cpp:1716 with the setting on (#446, fix AT). The same scene as the
// test above: a team-0 soldier at (96,96) facing right, a SEARCH(9) queued,
// its foe a team-1 orc 10 px east in the knife's reach. fire_check passes
// only for a walker that already faces its foe, so the turn toward the foe
// asks for the facing it has: with the setting on that turn does nothing.
// The orders are wiped and an ATTACK of 30+rng(25) rounds is queued (the
// draws: the waver, bound 4, then the duration pinned to 1: 31), and the
// soldier still faces right with its heading one stepsize east, where the
// classic turn (the test above) left it facing UP_RIGHT.
TEST_F(SemAi4, a_chaser_that_can_land_a_shot_keeps_facing_its_foe_with_the_setting_on)
{
    fresh_world(1);
    const std::uint32_t s = state_where({{4, kAny}, {25, 1}});
    walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
    walker* orc = living_at(FAMILY_ORC, 1, 106, 96);
    ASSERT_TRUE(w && orc);
    w->set_foe(orc);
    w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
    world().rng_.state_ = s;

    EXPECT_TRUE(w->stats()->direct_walk());

    const Cmd f = front_of(w);
    EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
    EXPECT_EQ(COMMAND_ATTACK, f.type);
    EXPECT_EQ(31, f.count) << "30 + rng(25), the draw pinned to 1";
    EXPECT_EQ(0, f.com1);
    EXPECT_EQ(0, f.com2);
    EXPECT_EQ(state_after(s, {4, 25}), world().rng_.state_) << "the waver, then the duration";
    EXPECT_EQ(FACE_RIGHT, w->curdir()) << "asked for the facing it has, it stays";
    EXPECT_FLOAT_EQ(w->stepsize(), w->lastx()) << "the heading is unchanged: one stepsize east";
    EXPECT_FLOAT_EQ(0.0f, w->lasty()) << "the heading is unchanged: one stepsize east";
    EXPECT_EQ(96, w->xpos());
    EXPECT_EQ(96, w->ypos());
}

// stats.cpp:1174, the slides. A team-0 soldier at (320,320), its foe 200 px
// down-right (an unreachable-by-knife target: the diagonal step (1,1)).
// Wall tile (21,21) only: the diagonal is walled, the x step is open, so
// the walk slides east (facing right: a real move). Walls (21,21) and
// (21,20): the diagonal and the x step are walled, the y step is open, so
// it slides south (facing down).
TEST_F(SemAi4, a_straight_chase_slides_along_the_open_axis_when_the_diagonal_is_walled)
{
    struct Case
    {
        const char* name;
        std::vector<std::pair<int, int>> walls;
        int facing;
        int dx;
        int dy;
    };
    const Case cases[] = {
        {"diagonal walled: the x slide", {{21, 21}}, FACE_RIGHT, 1, 0},
        {"diagonal and x walled: the y slide", {{21, 21}, {21, 20}}, FACE_DOWN, 0, 1},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        for (const auto& t : c.walls)
            tile(t.first, t.second, PIX_H_WALL1);
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        walker* orc = living_at(FAMILY_ORC, 1, 520, 520);
        ASSERT_TRUE(w && orc);
        w->set_foe(orc);
        face(w, c.facing);

        EXPECT_TRUE(w->stats()->direct_walk());

        EXPECT_EQ(at(320, static_cast<float>(c.dx) * w->stepsize()), w->xpos());
        EXPECT_EQ(at(320, static_cast<float>(c.dy) * w->stepsize()), w->ypos());
        EXPECT_EQ(0u, w->stats()->commands.size());
    }
}

// ---------------------------------------------------------------------------
// path_toward_stair (stats.cpp:1253)
// ---------------------------------------------------------------------------

// A soldier on floor 0 at cell (4,4) whose foe is above. Two up-stairs: one
// at (12,2), first in row-major order but 10 cells away, and one at (7,4),
// 3 cells away. The route goes to the NEAREST: path_toward_stair(1) is true
// and the path ends on cell (7,4) of floor 0.
TEST_F(SemAi4, a_walker_heads_for_the_nearest_up_stair_when_its_foe_is_above)
{
    two_floors();
    tile(12, 2, PIX_ZSTAIR_UP);
    tile(7, 4, PIX_ZSTAIR_UP);
    walker* w = living_at(FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_NE(nullptr, w);

    EXPECT_TRUE(w->stats()->path_toward_stair(1));

    ASSERT_FALSE(w->path_to_foe.empty());
    EXPECT_EQ((Cell{112, 64, 0}), cell_of(w->path_to_foe.back()))
        << "the path ends on the nearest up-stair, got " << describe(cell_of(w->path_to_foe.back()));
}

// The down arm. A soldier on floor 1 at cell (4,4) whose foe is below
// (floor 0): with one down-stair on floor 1 at (9,4) the route ends there;
// with only an UP stair at (9,4) there is no stair leading down: false and
// no path.
TEST_F(SemAi4, a_walker_heads_for_a_down_stair_when_its_foe_is_below)
{
    unsigned char* upper = two_floors();
    upper[9 + 4 * world().grid.w] = static_cast<unsigned char>(PIX_ZSTAIR_DOWN);
    walker* w = living_at(FAMILY_SOLDIER, 0, 64, 64, 1);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(1, w->floor());

    EXPECT_TRUE(w->stats()->path_toward_stair(0));
    ASSERT_FALSE(w->path_to_foe.empty());
    EXPECT_EQ((Cell{144, 64, 1}), cell_of(w->path_to_foe.back()))
        << "the path ends on the down-stair, got " << describe(cell_of(w->path_to_foe.back()));

    w->path_to_foe.clear();
    upper[9 + 4 * world().grid.w] = static_cast<unsigned char>(PIX_ZSTAIR_UP);
    EXPECT_FALSE(w->stats()->path_toward_stair(0)) << "an up-stair does not lead down";
    EXPECT_TRUE(w->path_to_foe.empty());
}

// The stair is there but A* cannot reach it: an up-stair at (9,4) walled in
// on all eight sides. path_toward_stair(1) is false and leaves no path.
TEST_F(SemAi4, a_stair_with_no_route_to_it_gives_no_stair_route)
{
    two_floors();
    tile(9, 4, PIX_ZSTAIR_UP);
    for (int ty = 3; ty <= 5; ++ty)
        for (int tx = 8; tx <= 10; ++tx)
            if (tx != 9 || ty != 4)
                tile(tx, ty, PIX_H_WALL1);
    walker* w = living_at(FAMILY_SOLDIER, 0, 64, 64);
    ASSERT_NE(nullptr, w);

    EXPECT_FALSE(w->stats()->path_toward_stair(1)) << "the only up-stair cannot be reached";
    EXPECT_TRUE(w->path_to_foe.empty());
}

// ---------------------------------------------------------------------------
// walk_to_foe (stats.cpp:1284, 1330, 1351, 1368, 1410, 1426)
// ---------------------------------------------------------------------------

// stats.cpp:1284. Both arms reset the 'closest ever' memory (last and
// current distance 15000) and return 0 before anything moves or the re-plan
// counter ticks: (no foe) with no draw at all (the !foe test short-circuits
// rng(300)); (a foe, rng(300) pinned to 0) after that one draw.
TEST_F(SemAi4, a_chase_with_no_foe_or_the_one_in_300_draw_resets_its_memory_and_stands)
{
    for (bool has_foe : {false, true}) {
        SCOPED_TRACE(has_foe ? "a foe, rng(300) == 0" : "no foe");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
        walker* orc = living_at(FAMILY_ORC, 1, 296, 96);
        ASSERT_TRUE(w && orc);
        if (has_foe)
            w->set_foe(orc);
        w->stats()->set_last_distance(50);
        w->stats()->set_current_distance(50);
        w->set_path_check_counter(7);
        const std::uint32_t s = state_where({{300, 0}});
        world().rng_.state_ = s;

        EXPECT_FALSE(w->stats()->walk_to_foe());

        EXPECT_EQ(15000u, w->stats()->last_distance());
        EXPECT_EQ(15000, w->stats()->current_distance());
        EXPECT_EQ(96, w->xpos()) << "no move";
        EXPECT_EQ(96, w->ypos());
        EXPECT_EQ(7, w->path_check_counter()) << "returns before the re-plan counter ticks";
        if (has_foe)
            EXPECT_EQ(state_after(s, {300}), world().rng_.state_) << "the one draw";
        else
            EXPECT_EQ(s, world().rng_.state_) << "no foe: nothing drawn";
    }
}

// stats.cpp:1330, the cross-floor exception. Two floors joined by a stair
// pair at cell (10,8); a team-1 skeleton on floor 0 at cell (5,5) with its
// re-plan counter at 10 and no path; its foe a team-0 soldier on floor 1 at
// cell (3,3). The counter only falls to 9, but a cross-floor foe with no
// path forces the re-plan now: the counter is redrawn (5 + rng(10), pinned
// to 3: 8) and a stair-aware path is laid. Control: the foe on floor 0, 300
// px away: no re-plan (counter 9, no path).
TEST_F(SemAi4, a_chaser_whose_foe_is_on_another_floor_replans_at_once)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, 3}});
    for (bool cross : {true, false}) {
        SCOPED_TRACE(cross ? "the foe on floor 1" : "control: the foe on floor 0");
        fresh_world();
        unsigned char* upper = two_floors();
        tile(10, 8, PIX_ZSTAIR_UP);
        upper[10 + 8 * world().grid.w] = static_cast<unsigned char>(PIX_ZSTAIR_DOWN);
        walker* skel = living_at(FAMILY_SKELETON, 1, 80, 80);
        walker* foe = cross ? living_at(FAMILY_SOLDIER, 0, 48, 48, 1) : living_at(FAMILY_SOLDIER, 0, 380, 80);
        ASSERT_TRUE(skel && foe);
        skel->set_foe(foe);
        skel->set_path_check_counter(10);
        world().rng_.state_ = s;

        EXPECT_TRUE(skel->stats()->walk_to_foe());

        if (cross) {
            EXPECT_EQ(8, skel->path_check_counter()) << "re-planned now: 5 + rng(10) = 8";
            ASSERT_FALSE(skel->path_to_foe.empty());
            EXPECT_EQ((Cell{48, 48, 1}), cell_of(skel->path_to_foe.back()))
                << "the stair-aware path ends on the foe's cell, got "
                << describe(cell_of(skel->path_to_foe.back()));
        } else {
            EXPECT_EQ(9, skel->path_check_counter()) << "control: throttled";
            EXPECT_TRUE(skel->path_to_foe.empty());
        }
    }
}

// stats.cpp:1351, a foe within 100 px. A team-0 soldier at (320,320)
// facing right with its heading (enddir) already swung south, a SEARCH(9)
// queued, its foe a team-1 orc 60 px due south, re-plan due. The short
// circuit wipes the order and queues ATTACK(30+rng(25), 1, 1) (the draws:
// rng(300), the cadence rng(10), then the duration pinned to 7: 37), turns
// toward the foe (RIGHT to DOWN_RIGHT) and runs init_fire, which turns on
// toward enddir (DOWN). It returns 1 and does not move.
TEST_F(SemAi4, a_chaser_within_100_px_of_a_foe_attacks_it_instead_of_pathing)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
    walker* orc = living_at(FAMILY_ORC, 1, 320, 380);
    ASSERT_TRUE(w && orc);
    w->set_enddir(static_cast<char>(FACE_DOWN));
    w->set_foe(orc);
    w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
    w->set_path_check_counter(0);
    world().rng_.state_ = s;

    EXPECT_TRUE(w->stats()->walk_to_foe());

    const Cmd f = front_of(w);
    EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
    EXPECT_EQ(COMMAND_ATTACK, f.type);
    EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
    EXPECT_EQ(1, f.com1);
    EXPECT_EQ(1, f.com2);
    EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_);
    EXPECT_EQ(FACE_DOWN, w->curdir()) << "the aim turn (to DOWN_RIGHT), then init_fire's turn toward enddir";
    EXPECT_EQ(FACE_DOWN, w->enddir());
    EXPECT_FLOAT_EQ(0.0f, w->busy()) << "init_fire turned, it did not fire";
    EXPECT_EQ(orc, w->foe());
    EXPECT_EQ(320, w->xpos());
    EXPECT_EQ(320, w->ypos());
}

// stats.cpp:2050 (#446, fix AT), both settings. A team-1 orc (melee only)
// at (320,320) facing down, heading one stepsize down, enddir down; its foe a
// team-0 soldier due south with a 1 px gap (337), a SEARCH(9) queued,
// re-plan due. The short circuit wipes the order and queues ATTACK(30 +
// rng(25), 1, 1) (the draws: rng(300), the cadence rng(10), the duration
// pinned to 7: 37), turns toward the foe (down: the facing it has) and runs
// init_fire. The orc does not move and its foe stays the soldier.
//  * Setting off: the turn takes the +7 arm to DOWN_RIGHT and heads along
//    it, so init_fire aims DOWN_RIGHT, finds enddir (DOWN) unmet and only
//    turns back: no swing, busy stays 0, the orc keeps walking.
//  * Setting on: the turn does nothing, so init_fire aims down at the foe,
//    passes the busy gate and starts the swing: busy is charged the orc's
//    fire delay and the attack animation runs (its first frame draws
//    nothing, so the random stream is the same three draws on both).
TEST_F(SemAi4, a_chaser_facing_its_foe_within_100_px_swings_only_with_the_setting_on)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
        walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 337);
        ASSERT_TRUE(orc && soldier);
        ASSERT_FLOAT_EQ(7.0f, orc->fire_frequency()) << "the orc's fire delay (living-14-orc.lua fire_delay 7)";
        face(orc, FACE_DOWN);
        orc->set_foe(soldier);
        orc->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        orc->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(orc->stats()->walk_to_foe());

        const Cmd f = front_of(orc);
        EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
        EXPECT_EQ(COMMAND_ATTACK, f.type);
        EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
        EXPECT_EQ(1, f.com1);
        EXPECT_EQ(1, f.com2);
        EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_);
        EXPECT_EQ(soldier, orc->foe());
        EXPECT_EQ(320, orc->xpos());
        EXPECT_EQ(320, orc->ypos());
        EXPECT_EQ(FACE_DOWN, orc->curdir()) << "facing its foe at the end of the call";
        if (fixes) {
            EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "the swing started: the fire delay is charged";
            EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "the swing started: the attack animation runs";
        } else {
            EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "init_fire only turned back: no swing";
            EXPECT_EQ(ANI_WALK, orc->ani_type()) << "init_fire only turned back: no swing";
        }
    }
}

// Gameplay Fixes BB (#497), stats.cpp statistics::near_swing, the melee-only
// arm; both settings. A team-1 orc (melee only) at (320,320) facing down; its
// foe a team-0 soldier due south; a SEARCH(9) queued, re-plan due. On both
// settings the short circuit wipes the order, queues ATTACK(30 + rng(25), 1,
// 1) (the draws: rng(300), the cadence rng(10), the duration pinned to 7:
// 37), the orc keeps its foe, does not move and ends facing down. Three
// scenes, the staging asked first (fire_pad_answer: quiet, no draw):
//  * on the pad: the soldier at (320,337), a 1 px gap: the pad of a 16x16
//    body facing down starts at y 337, so the soldier's body is on it;
//  * a wall on the pad: tile (20,21) walled (x 320-335, y 336-351), the
//    soldier at (320,352) beyond it;
//  * two tiles off: the soldier at (320,368) (the issue's own case).
// Setting off: the classic turn steps the orc a notch to DOWN_RIGHT and
// init_fire only turns it back: no swing in any scene. Setting on: the orc
// already faces its foe, asks its pad and swings (busy charged the orc's fire
// delay, the attack animation runs) only with a body on it; the pad question
// draws nothing, so the stream is the same three draws in every case.
TEST_F(SemAi4, a_melee_only_chaser_swings_from_the_near_branch_only_at_a_body_on_its_pad)
{
    enum class Scene { OnPad, WallOnPad, TwoTilesOff };
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    for (short fixes : {short{0}, short{1}}) {
        for (Scene sc : {Scene::OnPad, Scene::WallOnPad, Scene::TwoTilesOff}) {
            const char* scene = sc == Scene::OnPad       ? "on the pad"
                                : sc == Scene::WallOnPad ? "a wall on the pad"
                                                         : "two tiles off";
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") + scene);
            fresh_world(fixes);
            if (sc == Scene::WallOnPad)
                tile(20, 21, PIX_H_WALL1);
            const short soldier_y = sc == Scene::OnPad ? 337 : sc == Scene::WallOnPad ? 352 : 368;
            walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
            walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, soldier_y);
            ASSERT_TRUE(orc && soldier);
            ASSERT_FLOAT_EQ(7.0f, orc->fire_frequency()) << "the orc's fire delay (living-14-orc.lua fire_delay 7)";
            face(orc, FACE_DOWN);
            const walker::PadAnswer staged = sc == Scene::OnPad       ? walker::PadAnswer::Body
                                             : sc == Scene::WallOnPad ? walker::PadAnswer::Terrain
                                                                      : walker::PadAnswer::Clear;
            ASSERT_TRUE(staged == orc->fire_pad_answer()) << "the staging: what stands on the orc's pad";
            orc->set_foe(soldier);
            orc->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
            orc->set_path_check_counter(0);
            world().rng_.state_ = s;

            EXPECT_TRUE(orc->stats()->walk_to_foe());

            const Cmd f = front_of(orc);
            EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
            EXPECT_EQ(COMMAND_ATTACK, f.type);
            EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
            EXPECT_EQ(1, f.com1);
            EXPECT_EQ(1, f.com2);
            EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_) << "the pad question draws nothing";
            EXPECT_EQ(soldier, orc->foe());
            EXPECT_EQ(320, orc->xpos());
            EXPECT_EQ(320, orc->ypos());
            EXPECT_EQ(FACE_DOWN, orc->curdir()) << "facing its foe at the end of the call";
            EXPECT_EQ(FACE_DOWN, orc->enddir());
            if (fixes && sc == Scene::OnPad) {
                EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "a body on the pad: the swing started";
                EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "a body on the pad: the attack animation runs";
            } else {
                EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "no swing";
                EXPECT_EQ(ANI_WALK, orc->ani_type()) << "no swing";
            }
        }
    }
}

// Gameplay Fixes BB (#497), stats.cpp statistics::near_swing, the shooter's
// arm; both settings. A team-0 soldier (a knife thrower) at (320,320) facing
// down; its foe a team-1 orc due south; a SEARCH(9) queued, re-plan due. On
// both settings the short circuit queues ATTACK(37), the soldier keeps its
// foe, does not move and ends facing down. Three scenes:
//  (a) the line is clear: the orc at (320,337), on the pad, so the ray meets
//      it on its first step whatever the waver;
//  (b) a wall on the pad: tile (20,21) walled, the orc at (320,352);
//  (c) out of reach: open field, the orc at (320,416) (corner distance 96:
//      inside the branch's 100, past a level-4 knife).
// fire_check builds the soldier's weapon and draws its waver once,
// next(h + 1) with h the weapon's stepsize / 2 clamped to 0..127
// (walker::set_weapon_heading); kWaver is that bound, read from a weapon the
// soldier builds in a throwaway world. Setting off: no line check, no swing,
// three draws. Setting on: the line check is asked in every scene (four
// draws) and the shot starts (busy charged its fire delay, the attack
// animation runs) only in (a). Each setting-on scene is also asked directly
// in a twin world built the same way, so the staging cannot rot.
TEST_F(SemAi4, a_shooter_fires_from_the_near_branch_only_when_its_line_check_passes)
{
    enum class Scene { Clear, WallOnPad, OutOfReach };
    std::uint32_t waver_bound = 0;
    {
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        ASSERT_NE(nullptr, w);
        walker* k = w->create_weapon();
        ASSERT_NE(nullptr, k);
        const auto h = static_cast<signed char>(std::clamp(k->stepsize() / 2, 0.0f, 127.0f));
        waver_bound = static_cast<std::uint32_t>(h + 1);
        k->set_dead(1);
    }
    const std::uint32_t kWaver = waver_bound;
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    const auto stage = [this](Scene sc, short fixes, walker*& w, walker*& orc) {
        fresh_world(fixes);
        if (sc == Scene::WallOnPad)
            tile(20, 21, PIX_H_WALL1);
        const short orc_y = sc == Scene::Clear ? 337 : sc == Scene::WallOnPad ? 352 : 416;
        w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        orc = living_at(FAMILY_ORC, 1, 320, orc_y);
        if (w == nullptr || orc == nullptr)
            return;
        face(w, FACE_DOWN);
        w->set_foe(orc);
        w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        w->set_path_check_counter(0);
    };
    for (short fixes : {short{0}, short{1}}) {
        for (Scene sc : {Scene::Clear, Scene::WallOnPad, Scene::OutOfReach}) {
            const char* scene = sc == Scene::Clear       ? "(a) the line is clear"
                                : sc == Scene::WallOnPad ? "(b) a wall on the pad"
                                                         : "(c) out of reach";
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") + scene);
            walker* w = nullptr;
            walker* orc = nullptr;
            if (fixes) {
                stage(sc, fixes, w, orc);
                ASSERT_TRUE(w && orc);
                walker::FireCheckDenial d = walker::FireCheckDenial::None;
                const bool passes = w->fire_check(0, 1, &d);
                if (sc == Scene::Clear) {
                    ASSERT_TRUE(passes) << "the twin: the line check passes";
                } else {
                    ASSERT_FALSE(passes) << "the twin: the line check refuses";
                    ASSERT_TRUE(d == (sc == Scene::WallOnPad ? walker::FireCheckDenial::WallBlocked
                                                             : walker::FireCheckDenial::OutOfRange))
                        << "the twin: refused for the staged reason";
                }
            }
            stage(sc, fixes, w, orc);
            ASSERT_TRUE(w && orc);
            world().rng_.state_ = s;

            EXPECT_TRUE(w->stats()->walk_to_foe());

            const Cmd f = front_of(w);
            EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
            EXPECT_EQ(COMMAND_ATTACK, f.type);
            EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
            EXPECT_EQ(1, f.com1);
            EXPECT_EQ(1, f.com2);
            EXPECT_EQ(orc, w->foe());
            EXPECT_EQ(320, w->xpos());
            EXPECT_EQ(320, w->ypos());
            EXPECT_EQ(FACE_DOWN, w->curdir()) << "facing its foe at the end of the call";
            if (fixes) {
                EXPECT_EQ(state_after(s, {300, 10, 25, kWaver}), world().rng_.state_)
                    << "the line check is asked: its waver is drawn";
            } else {
                EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_) << "no line check with the setting off";
            }
            if (fixes && sc == Scene::Clear) {
                EXPECT_FLOAT_EQ(w->fire_frequency(), w->busy()) << "the line check passed: the shot started";
                EXPECT_EQ(ANI_ATTACK, w->ani_type()) << "the line check passed: the attack animation runs";
            } else {
                EXPECT_FLOAT_EQ(0.0f, w->busy()) << "no shot";
                EXPECT_EQ(ANI_WALK, w->ani_type()) << "no shot";
            }
        }
    }
}

// Gameplay Fixes BB (#497), stats.cpp statistics::turn_to_foe; both settings.
// A team-0 soldier at (320,320), its foe a team-1 orc at (337,320) (due east:
// the aim is RIGHT); a SEARCH(9) queued, re-plan due. Two starts: facing UP
// (two notches off) and UP_RIGHT (one notch off). On both settings the short
// circuit queues ATTACK(37) and draws only its three draws (a bot that does
// not face its foe asks nothing).
//  * Setting off: the turn moves a notch toward RIGHT and points the heading
//    along it, enddir is left as it was, and init_fire, finding the heading
//    on its facing but enddir unmet, turns the soldier back: it ends where it
//    started, enddir unchanged, nothing fired.
//  * Setting on: enddir becomes RIGHT and the notch stays: UP to UP_RIGHT,
//    UP_RIGHT to RIGHT. The turn is the act: nothing starts, even from
//    UP_RIGHT, where the orc now stands on the soldier's pad.
// Then, from UP, one more act (living::act): with the setting on the soldier
// is still turning (curdir != enddir), so the act is one notch to RIGHT and
// the ATTACK order does not run (count 37); with it off the soldier faces its
// enddir, so the order runs: the line check refuses (Facing), the order
// snap-faces it RIGHT, and do_command spends one round of it (count 36).
TEST_F(SemAi4, a_chaser_turning_toward_its_near_foe_keeps_that_aim_with_the_setting_on)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    for (short fixes : {short{0}, short{1}}) {
        for (int start : {FACE_UP, FACE_UP_RIGHT}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (start == FACE_UP ? "facing UP" : "facing UP_RIGHT"));
            fresh_world(fixes);
            walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
            walker* orc = living_at(FAMILY_ORC, 1, 337, 320);
            ASSERT_TRUE(w && orc);
            face(w, start);
            w->set_foe(orc);
            w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
            w->set_path_check_counter(0);
            world().rng_.state_ = s;

            EXPECT_TRUE(w->stats()->walk_to_foe());

            const Cmd f = front_of(w);
            EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
            EXPECT_EQ(COMMAND_ATTACK, f.type);
            EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
            EXPECT_EQ(1, f.com1);
            EXPECT_EQ(1, f.com2);
            EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_) << "not facing its foe: nothing asked";
            EXPECT_EQ(orc, w->foe());
            EXPECT_EQ(320, w->xpos());
            EXPECT_EQ(320, w->ypos());
            EXPECT_FLOAT_EQ(0.0f, w->busy()) << "nothing started";
            EXPECT_EQ(ANI_WALK, w->ani_type()) << "nothing started";
            if (fixes) {
                EXPECT_EQ(start == FACE_UP ? FACE_UP_RIGHT : FACE_RIGHT, w->curdir()) << "the notch toward the foe stays";
                EXPECT_EQ(FACE_RIGHT, w->enddir()) << "the aim is kept as the facing it turns to";
            } else {
                EXPECT_EQ(start, w->curdir()) << "a notch toward the foe, then init_fire turns it back";
                EXPECT_EQ(start, w->enddir()) << "the classic turn leaves enddir as it was";
            }
            if (start != FACE_UP)
                continue;

            (void)w->act();

            EXPECT_EQ(FACE_RIGHT, w->curdir()) << "one act later the soldier faces its foe";
            EXPECT_EQ(COMMAND_ATTACK, front_of(w).type);
            if (fixes) {
                EXPECT_EQ(37, front_of(w).count) << "the act was the turn: the order did not run";
            } else {
                EXPECT_EQ(36, front_of(w).count) << "the order ran (its snap-face turned the soldier): one round spent";
            }
        }
    }
}

// Gameplay Fixes BB (#497), stats.cpp statistics::near_swing, the facing
// bot's heading; both settings. A team-1 orc at (320,320) that faces down
// (curdir and enddir) while its heading, its last step, points east (lastx
// one stepsize, lasty 0); its foe a team-0 soldier at (320,337), on the pad
// below it; a SEARCH(9) queued, re-plan due. Setting off: the classic turn
// steps the orc a notch off and init_fire turns it back: no swing. Setting
// on: the orc faces its foe, so its heading is pointed along the facing
// first (lastx 0, lasty one stepsize) and the pad it asks, and swings at, is
// the one below it: the soldier's body, so the swing starts. Along the stale
// heading (east) the pad would be clear and init_fire would only turn it.
TEST_F(SemAi4, a_chaser_facing_its_near_foe_swings_along_its_facing_not_a_stale_heading)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, 7}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
        walker* soldier = living_at(FAMILY_SOLDIER, 0, 320, 337);
        ASSERT_TRUE(orc && soldier);
        ASSERT_FLOAT_EQ(7.0f, orc->fire_frequency()) << "the orc's fire delay (living-14-orc.lua fire_delay 7)";
        orc->set_curdir(static_cast<signed char>(FACE_DOWN));
        orc->set_enddir(static_cast<char>(FACE_DOWN));
        orc->set_lastx(orc->stepsize());
        orc->set_lasty(0.0f);
        ASSERT_TRUE(walker::PadAnswer::Clear == orc->fire_pad_answer()) << "the staging: the pad along the stale heading is clear";
        orc->set_foe(soldier);
        orc->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        orc->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(orc->stats()->walk_to_foe());

        const Cmd f = front_of(orc);
        EXPECT_EQ(1u, f.size) << "the SEARCH was wiped";
        EXPECT_EQ(COMMAND_ATTACK, f.type);
        EXPECT_EQ(37, f.count) << "30 + rng(25), the draw pinned to 7";
        EXPECT_EQ(state_after(s, {300, 10, 25}), world().rng_.state_);
        EXPECT_EQ(soldier, orc->foe());
        EXPECT_EQ(320, orc->xpos());
        EXPECT_EQ(320, orc->ypos());
        EXPECT_EQ(FACE_DOWN, orc->curdir()) << "facing its foe at the end of the call";
        if (fixes) {
            EXPECT_FLOAT_EQ(orc->fire_frequency(), orc->busy()) << "the swing started along the facing";
            EXPECT_EQ(ANI_ATTACK, orc->ani_type()) << "the swing started along the facing";
            EXPECT_FLOAT_EQ(0.0f, orc->lastx()) << "the heading points along the facing";
            EXPECT_FLOAT_EQ(orc->stepsize(), orc->lasty()) << "the heading points along the facing";
        } else {
            EXPECT_FLOAT_EQ(0.0f, orc->busy()) << "init_fire only turned back: no swing";
            EXPECT_EQ(ANI_WALK, orc->ani_type()) << "init_fire only turned back: no swing";
        }
    }
}

// #447 (this game only, both settings), stats.cpp statistics::near_adopts.
// Two floors. A team-1 ghost (it flies; melee only) at (320,320) on floor 0
// facing down, holding `above`, a team-0 soldier on floor 1; `beside`, a
// second team-0 soldier; a SEARCH(9) queued, re-plan due. A flyer keeps the
// floor-blind chase (it never changes floors), so the re-plan finds `above`
// within 100 px and enters the near branch, where the quick foe search
// (find_near_foe: floor-keyed, about five map cells round the bot, then the
// floor-blind full scan) answers a foe; the staging asserts that answer first.
//  * the scene: `above` at (320,340) on floor 1 (corner distance 20),
//    `beside` at (320,400) on floor 0 (80: not nearer). The search answers
//    `beside`, on the ghost's own floor: the ghost takes it and queues its
//    ATTACK order;
//  * control, no foe on the ghost's floor: `above` at (320,337) and `beside`
//    at (320,400), both on floor 1. The search finds nobody on floor 0 and
//    the full scan answers `above`: the ghost keeps it, and since its pad (on
//    floor 0) is clear it starts no swing on either setting (busy 0, the walk
//    animation): a flyer swings at no foe on another floor;
//  * control, beyond the quick search: `beside` at (320,620) on floor 0, 300
//    px off. The spiral never reaches it and the full scan answers `above`:
//    the ghost keeps `above`;
//  * control, another foe on the other floor at the same distance: `twin`, a
//    third team-0 soldier at (320,300) on floor 1, made before `above`
//    (320,340); both 20 px off, `beside` at (320,400) on floor 1. The search
//    finds nobody on floor 0 and the full scan, which keeps the earlier
//    walker on a tie, answers `twin`: not nearer than `above` and not on the
//    ghost's floor, so the ghost keeps `above`. Only this scene tells "take
//    the foe on its own floor" from "take whatever the search answers": in
//    the two controls above the search answers the held foe itself;
//  * control, a ground walker: an orc (it does not fly) in the ghost's place,
//    the first scene's soldiers. A ground walker whose foe is on another
//    floor heads for a stair and never enters the near branch: it keeps
//    `above`. The near branch therefore never asks the rule for it, so the
//    scene asks near_adopts itself, with the flyer flag walk_to_foe gives a
//    walker that does not fly: no, though `beside` stands on its floor;
//  * control, the held foe on the ghost's own floor (after the loop): the
//    ghost holds `held` at (288,320) on floor 0, 32 px off, and `first` at
//    (352,352) on floor 0 is 64 px off. The quick search answers the first
//    foe in spiral order, not the nearest: it reaches `first`'s cell on its
//    second step, `held`'s on its fifth. `first` is on the ghost's floor but
//    the held foe is too, so the flyer rule does not apply and #320's alone
//    decides: not nearer, the ghost keeps `held` and queues its ATTACK order
//    (the near branch ran, so the rule was asked). Only this scene tells
//    "the held foe is on another floor" from "the ghost flies".
// The five controls assert the foe (the ground walker may step; that is not
// this rule).
TEST_F(SemAi4, a_flyer_whose_foe_is_on_another_floor_takes_the_foe_on_its_own_floor)
{
    enum class Scene { OwnFloorFoe, NoFoeOnItsFloor, BeyondTheSearch, TieOnTheOtherFloor, GroundWalker };
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, kAny}});
    for (short fixes : {short{0}, short{1}}) {
        for (Scene sc : {Scene::OwnFloorFoe, Scene::NoFoeOnItsFloor, Scene::BeyondTheSearch,
                         Scene::TieOnTheOtherFloor, Scene::GroundWalker}) {
            const char* scene = sc == Scene::OwnFloorFoe          ? "a foe on the ghost's floor"
                                : sc == Scene::NoFoeOnItsFloor    ? "control: no foe on the ghost's floor"
                                : sc == Scene::BeyondTheSearch    ? "control: the foe on its floor beyond the quick search"
                                : sc == Scene::TieOnTheOtherFloor ? "control: another foe on the other floor at the same distance"
                                                                  : "control: a ground walker";
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") + scene);
            fresh_world(fixes);
            two_floors();
            const bool both_above = sc == Scene::NoFoeOnItsFloor || sc == Scene::TieOnTheOtherFloor;
            walker* bot = living_at(sc == Scene::GroundWalker ? FAMILY_ORC : FAMILY_GHOST, 1, 320, 320, 0);
            // Made before `above`, so the full scan meets it first.
            walker* twin = sc == Scene::TieOnTheOtherFloor ? living_at(FAMILY_SOLDIER, 0, 320, 300, 1) : nullptr;
            const short above_y = sc == Scene::NoFoeOnItsFloor ? 337 : 340;
            walker* above = living_at(FAMILY_SOLDIER, 0, 320, above_y, 1);
            walker* beside = living_at(FAMILY_SOLDIER, 0, 320, sc == Scene::BeyondTheSearch ? 620 : 400,
                                       both_above ? 1 : 0);
            ASSERT_TRUE(bot && above && beside);
            ASSERT_EQ(sc == Scene::TieOnTheOtherFloor, twin != nullptr);
            ASSERT_EQ(sc != Scene::GroundWalker, bot->stats()->query_bit_flags(BIT_FLYING) != 0)
                << "the staging: the ghost flies, the orc does not";
            face(bot, FACE_DOWN);
            bot->set_foe(above);
            if (sc != Scene::GroundWalker) {
                walker* const answered = world().find_near_foe(bot);
                walker* const expected = sc == Scene::OwnFloorFoe          ? beside
                                         : sc == Scene::TieOnTheOtherFloor ? twin
                                                                           : above;
                ASSERT_EQ(expected, answered) << "the staging: the quick foe search's answer";
            }
            if (sc == Scene::TieOnTheOtherFloor) {
                ASSERT_EQ(bot->distance_to_ob(above), bot->distance_to_ob(twin))
                    << "the staging: `twin` is exactly as far as `above`, so it is not nearer";
                ASSERT_EQ(twin->floor(), above->floor()) << "the staging: `twin` stands on the other floor too";
            }
            if (sc == Scene::NoFoeOnItsFloor) {
                ASSERT_TRUE(walker::PadAnswer::Clear == bot->fire_pad_answer())
                    << "the staging: `above` stands on floor 1, the ghost's pad on floor 0 is clear";
            }
            if (sc == Scene::GroundWalker) {
                EXPECT_FALSE(bot->stats()->near_adopts(beside, static_cast<std::uint32_t>(bot->distance_to_ob(above)),
                                                       /*flyer=*/false))
                    << "asked by itself, the rule takes no foe for a walker that does not fly";
            }
            bot->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
            bot->set_path_check_counter(0);
            world().rng_.state_ = s;

            EXPECT_TRUE(bot->stats()->walk_to_foe());

            if (sc == Scene::OwnFloorFoe) {
                EXPECT_EQ(beside, bot->foe()) << "the foe on its own floor is taken, though it is farther";
                EXPECT_EQ(COMMAND_ATTACK, front_of(bot).type) << "and attacked";
            } else {
                EXPECT_EQ(above, bot->foe()) << "the held foe is kept";
            }
            if (sc == Scene::NoFoeOnItsFloor) {
                EXPECT_FLOAT_EQ(0.0f, bot->busy()) << "no swing at a foe on another floor";
                EXPECT_EQ(ANI_WALK, bot->ani_type()) << "no swing at a foe on another floor";
            }
        }
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                     "control: the held foe on the ghost's own floor, another the search meets first");
        fresh_world(fixes);
        two_floors();
        walker* ghost = living_at(FAMILY_GHOST, 1, 320, 320, 0);
        walker* held = living_at(FAMILY_SOLDIER, 0, 288, 320, 0);
        walker* first = living_at(FAMILY_SOLDIER, 0, 352, 352, 0);
        ASSERT_TRUE(ghost && held && first);
        ASSERT_NE(0, ghost->stats()->query_bit_flags(BIT_FLYING)) << "the staging: the ghost flies";
        ASSERT_EQ(ghost->floor(), held->floor()) << "the staging: the held foe stands on the ghost's floor";
        ASSERT_EQ(ghost->floor(), first->floor()) << "the staging: `first` stands on the ghost's floor";
        ASSERT_EQ(32, ghost->distance_to_ob(held)) << "the staging: the held foe is within 100 px";
        ASSERT_EQ(64, ghost->distance_to_ob(first)) << "the staging: `first` is not nearer than the held foe";
        face(ghost, FACE_DOWN);
        ghost->set_foe(held);
        ASSERT_EQ(first, world().find_near_foe(ghost))
            << "the staging: the quick foe search meets `first` before the nearer held foe";
        ghost->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        ghost->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(ghost->stats()->walk_to_foe());

        EXPECT_EQ(COMMAND_ATTACK, front_of(ghost).type) << "the near branch ran, so the rule was asked";
        EXPECT_EQ(held, ghost->foe()) << "the held foe on the ghost's own floor is kept: `first` is not nearer";
    }
}

// stats.cpp:1351, the crowded map. 201 team-0 soldiers stacked in the far
// corner put the obmap past 200 objects, so the re-plan skips A* even for a
// foe 400 px away; no foe is within 100 px, so the current order is ended
// (front SEARCH count 0, not popped here) and the fallback walk runs (a new
// best distance: direct_walk steps east, 96 -> 100). Control: no crowd, A*
// lays a path to the foe's cell and the order stands.
TEST_F(SemAi4, a_crowded_map_skips_pathing_and_ends_the_order_when_no_foe_is_near)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}});
    for (bool crowded : {true, false}) {
        SCOPED_TRACE(crowded ? "201 bodies on the map" : "control: no crowd");
        fresh_world();
        if (crowded)
            crowd(201);
        walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
        walker* orc = living_at(FAMILY_ORC, 1, 496, 96);
        ASSERT_TRUE(w && orc);
        w->set_foe(orc);
        w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        w->stats()->set_last_distance(15000);
        w->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        const Cmd f = front_of(w);
        EXPECT_EQ(1u, f.size) << "no ATTACK queued";
        EXPECT_EQ(COMMAND_SEARCH, f.type);
        if (crowded) {
            EXPECT_EQ(0, f.count) << "no foe within 100: the order is ended";
            EXPECT_TRUE(w->path_to_foe.empty()) << "no A* on a crowded map";
            EXPECT_EQ(400u, w->stats()->last_distance()) << "a new best: the direct walk";
            EXPECT_EQ(100, w->xpos());
        } else {
            EXPECT_EQ(9, f.count) << "control: the order stands";
            ASSERT_FALSE(w->path_to_foe.empty()) << "control: A* ran";
            EXPECT_EQ((Cell{496, 96, 0}), cell_of(w->path_to_foe.back()));
        }
    }
}

// stats.cpp:1368, #320's adoption aims the turn. A team-0 soldier at
// (320,320) facing right, its heading (enddir) swung south; its held foe a
// team-1 orc 80 px east, a second team-1 orc 32 px due south. The short
// circuit adopts the nearer orc and aims at it: RIGHT to DOWN_RIGHT, then
// init_fire on to DOWN. Control: the second orc 280 px south (beyond the
// held one): the held foe stays and the aim is east (turn(RIGHT) from RIGHT
// steps to UP_RIGHT; init_fire then turns toward enddir, still DOWN, one
// clockwise step from UP_RIGHT: RIGHT).
TEST_F(SemAi4, a_chaser_switches_to_a_strictly_nearer_foe_and_turns_to_it)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {25, kAny}});
    for (bool nearer : {true, false}) {
        SCOPED_TRACE(nearer ? "the second orc is nearer" : "control: the second orc is farther");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        walker* held = living_at(FAMILY_ORC, 1, 400, 320);
        walker* other = living_at(FAMILY_ORC, 1, 320, nearer ? 352 : 600);
        ASSERT_TRUE(w && held && other);
        w->set_enddir(static_cast<char>(FACE_DOWN));
        w->set_foe(held);
        w->set_path_check_counter(0);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        if (nearer) {
            EXPECT_EQ(other, w->foe()) << "the strictly nearer foe is adopted";
            EXPECT_EQ(FACE_DOWN, w->curdir()) << "aimed south at it";
        } else {
            EXPECT_EQ(held, w->foe()) << "control: the held foe stays";
            EXPECT_EQ(FACE_RIGHT, w->curdir()) << "control: aimed east";
        }
    }
}

// stats.cpp:1410, the path and the new-best arms. (path) A team-0 soldier at
// (96,96) facing right, its foe a team-1 orc 300 px east, re-plan due on an
// uncrowded map: A* lays a path, the walker follows it one step east (100)
// and last_distance is the distance measured after the step, 296. (no path,
// new best) The same with 201 bodies in the far corner (no A*): the re-plan
// measured 300, a new best: last_distance 300, direct_walk steps east.
// (control, a throttled tick) counter 5: no measure (tempdistance keeps its
// 9999999), last_distance stays 15000; the right walk in open space steps
// east too.
TEST_F(SemAi4, a_chaser_follows_its_path_or_charges_when_closer_than_ever)
{
    enum class Case { Path, NewBest, Throttled };
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}});
    for (Case c : {Case::Path, Case::NewBest, Case::Throttled}) {
        SCOPED_TRACE(c == Case::Path ? "a path" : c == Case::NewBest ? "no path, a new best" : "throttled");
        fresh_world();
        if (c != Case::Path)
            crowd(201);
        walker* w = living_at(FAMILY_SOLDIER, 0, 96, 96);
        walker* orc = living_at(FAMILY_ORC, 1, 396, 96);
        ASSERT_TRUE(w && orc);
        w->set_foe(orc);
        w->stats()->set_last_distance(15000);
        w->set_path_check_counter(c == Case::Throttled ? 5 : 0);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        EXPECT_EQ(100, w->xpos());
        EXPECT_EQ(96, w->ypos());
        if (c == Case::Path) {
            EXPECT_FALSE(w->path_to_foe.empty());
            EXPECT_EQ(296u, w->stats()->last_distance()) << "the distance after the path step";
        } else if (c == Case::NewBest) {
            EXPECT_TRUE(w->path_to_foe.empty());
            EXPECT_EQ(300u, w->stats()->last_distance()) << "the re-plan's measure, a new best";
        } else {
            EXPECT_EQ(15000u, w->stats()->last_distance()) << "a throttled tick measures nothing";
        }
    }
}

// stats.cpp:1426 (triaged a classic quirk: on one floor the line is reached
// only when find_foes_in_range finds no hostile in 100 px). Three arms; (a)
// and (c) match the old game (7ff1654a stats.cpp:1340), (b) is the current
// game's multifloor guard (the old game has no !cross_floor term):
//  (a) same floor, re-plan tick, no orders queued, the held foe an ignored
//      hostile object 20 px east (a mode's attack objective, the soccer
//      ball's shape): find_foes_in_range counts only livings and generators,
//      so the short circuit counts nobody, while fire_check's ray sees the
//      ignored foe's box; direct_walk queues ATTACK(30+rng(25)) and this line
//      ends it at once (front count 0).
//  (b) cross-floor, the foe on floor 1 10 px away in 2D, SEARCH(9) queued,
//      re-plan tick: the order stands (count 9).
//  (c) same floor, throttled tick (counter 5), a friendly held foe 20 px
//      east, SEARCH(9): tempdistance is unmeasured, the order stands.
TEST_F(SemAi4, a_chaser_within_30_px_ends_its_order_only_on_a_replan_tick_on_its_floor)
{
    const std::uint32_t s = state_where({{300, kNonZero}, {10, kAny}, {4, kAny}, {25, 5}});
    {
        SCOPED_TRACE("(a) same floor, re-plan tick");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        walker* ball = treasure_at(FAMILY_STAIN, 340, 320, 16, 16);
        ASSERT_TRUE(w && ball);
        ball->set_team_num(1);
        ball->set_ignore(1);
        w->set_foe(ball);
        w->set_path_check_counter(0);
        w->stats()->set_last_distance(15000);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        const Cmd f = front_of(w);
        EXPECT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_ATTACK, f.type) << "direct_walk's shot check queued an ATTACK";
        EXPECT_EQ(0, f.count) << "within 30 px on a re-plan tick: the order is ended at once";
        EXPECT_EQ(state_after(s, {300, 10, 4, 25}), world().rng_.state_);
    }
    {
        SCOPED_TRACE("(b) cross-floor");
        fresh_world();
        two_floors();
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        walker* orc = living_at(FAMILY_ORC, 1, 330, 320, 1);
        ASSERT_TRUE(w && orc);
        w->set_foe(orc);
        w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        w->set_path_check_counter(0);
        w->stats()->set_last_distance(15000);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        const Cmd f = front_of(w);
        EXPECT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_SEARCH, f.type);
        EXPECT_EQ(9, f.count) << "a cross-floor chase never ends on 2D proximity";
    }
    {
        SCOPED_TRACE("(c) throttled tick");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 0, 320, 320);
        walker* friend_ = living_at(FAMILY_SOLDIER, 0, 340, 320);
        ASSERT_TRUE(w && friend_);
        w->set_foe(friend_);
        w->stats()->add_command(COMMAND_SEARCH, 9, 0, 0);
        w->set_path_check_counter(5);
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_foe());

        const Cmd f = front_of(w);
        EXPECT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_SEARCH, f.type);
        EXPECT_EQ(9, f.count) << "a throttled tick never measures, so never ends the order";
        EXPECT_EQ(4, w->path_check_counter());
    }
}

// ---------------------------------------------------------------------------
// facing_step_delta (stats.cpp:1443) and right_walk_to_point (stats.cpp:1552)
// ---------------------------------------------------------------------------

// stats.cpp:1443 through right_walk_to_point's right-back arm (stats.cpp:
// 1574/1579). A soldier at (320,320) facing up with only tile (21,21)
// walled: right, right-front and front are open, the right-back probe is
// blocked. For each heading e the walker's enddir is set to (e+6)%8; the
// arm turns it right to e and queues WALK(1) along e's unit step, so the
// queued com1/com2 are facing_step_delta's table, one entry per facing.
// Both values of the setting (the probes are geometric here).
TEST_F(SemAi4, a_point_walker_that_passed_a_corner_turns_right_and_queues_one_step_along_its_new_heading)
{
    for (short fixes : {short{0}, short{1}}) {
        for (int e = 0; e < 8; ++e) {
            SCOPED_TRACE("setting " + std::to_string(fixes) + ", new heading " + std::to_string(e));
            fresh_world(fixes);
            tile(21, 21, PIX_H_WALL1);
            walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
            ASSERT_NE(nullptr, w);
            face(w, FACE_UP);
            w->set_enddir(static_cast<char>((e + 6) % 8));

            EXPECT_TRUE(w->stats()->right_walk_to_point(320, 0));

            EXPECT_EQ(e, w->enddir()) << "turned right: (enddir + 2) % 8";
            const Cmd b = back_of(w);
            ASSERT_EQ(1u, b.size);
            EXPECT_EQ(COMMAND_WALK, b.type);
            EXPECT_EQ(1, b.count);
            EXPECT_EQ(kStep[e][0], b.com1) << "facing_step_delta's x for heading " << e;
            EXPECT_EQ(kStep[e][1], b.com2) << "facing_step_delta's y for heading " << e;
            EXPECT_EQ(320, w->xpos());
            EXPECT_EQ(320, w->ypos());
        }
    }
}

// stats.cpp:1443's default arm through right_walk_to_point's open arm. A
// soldier with no facing (curdir 127) on open grass asked for its own spot:
// every probe is open, direct_walk_to_point fails (it is already there), so
// it steps along facing_step_delta(127) = (0,0): no move, only walk(0,0)'s
// turn toward facing(0,0) = FACE_UP.
TEST_F(SemAi4, an_invalid_facing_maps_to_no_step)
{
    walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
    ASSERT_NE(nullptr, w);
    w->set_curdir(static_cast<signed char>(127));
    w->set_enddir(static_cast<char>(127));

    EXPECT_TRUE(w->stats()->right_walk_to_point(320, 320));

    EXPECT_EQ(320, w->xpos());
    EXPECT_EQ(320, w->ypos());
    EXPECT_EQ(FACE_UP, w->enddir()) << "the (0,0) step's facing";
    EXPECT_EQ(FACE_UP, w->curdir());
}

// stats.cpp:1552, the arms right_walk_to_point shares with right_walk, on a
// soldier at (320,320), both values of the setting:
//  (2) walls (21,20) and (20,19), facing up: right and front blocked, turn
//      left: enddir (0+6)%8 = LEFT, one turn to UP_LEFT, no move;
//  (3) wall (20,19), facing up-right: only the front is blocked, turn left:
//      enddir (1+6)%8 = UP_LEFT, one turn to UP, no move;
//  (5) open grass, facing up-right, the point due east: the step goes
//      toward the point (enddir RIGHT, a turn), not along the facing (a
//      diagonal move);
//  (6) open grass, facing down, the point the walker's own spot: the
//      direct step fails, the curdir step goes south (ypos 324).
TEST_F(SemAi4, a_point_walker_follows_the_right_hand_rule_and_steps_at_its_point_in_the_open)
{
    struct Case
    {
        const char* name;
        std::vector<std::pair<int, int>> walls;
        int facing;
        short px;
        short py;
        int enddir;
        int curdir;
        int dx;
        int dy;
    };
    const Case cases[] = {
        {"(2) right and front blocked", {{21, 20}, {20, 19}}, FACE_UP, 320, 0, FACE_LEFT, FACE_UP_LEFT, 0, 0},
        {"(3) front blocked only", {{20, 19}}, FACE_UP_RIGHT, 320, 0, FACE_UP_LEFT, FACE_UP, 0, 0},
        {"(5) open, the point due east", {}, FACE_UP_RIGHT, 600, 320, FACE_RIGHT, FACE_RIGHT, 0, 0},
        {"(6) open, already at the point", {}, FACE_DOWN, 320, 320, FACE_DOWN, FACE_DOWN, 0, 1},
    };
    for (short fixes : {short{0}, short{1}}) {
        for (const Case& c : cases) {
            SCOPED_TRACE(std::string(c.name) + ", setting " + std::to_string(fixes));
            fresh_world(fixes);
            for (const auto& t : c.walls)
                tile(t.first, t.second, PIX_H_WALL1);
            walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
            ASSERT_NE(nullptr, w);
            face(w, c.facing);

            EXPECT_TRUE(w->stats()->right_walk_to_point(c.px, c.py));

            EXPECT_EQ(c.enddir, w->enddir());
            EXPECT_EQ(c.curdir, w->curdir());
            EXPECT_EQ(at(320, static_cast<float>(c.dx) * w->stepsize()), w->xpos());
            EXPECT_EQ(at(320, static_cast<float>(c.dy) * w->stepsize()), w->ypos());
            EXPECT_EQ(0u, w->stats()->commands.size()) << "no WALK queued";
        }
    }
}

// stats.cpp:1558, the wall hug keeps the last move. (x-major) A soldier at
// (320,320) facing right, wall (20,21) below it (its right side), its last
// move (4, 0.8): the y part is snapped away, the step is (1,0): x 324.
// (diagonal) Facing up-left, wall (21,19) (its right side), last move
// (-2,-1): neither axis dominates 3:1, the step is (-1,-1): (316,316).
TEST_F(SemAi4, a_point_walker_hugging_a_wall_keeps_its_last_direction)
{
    struct Case
    {
        const char* name;
        std::pair<int, int> wall;
        int facing;
        float lastx;
        float lasty;
        int dx;
        int dy;
    };
    const Case cases[] = {
        {"x-major", {20, 21}, FACE_RIGHT, 4.0f, 0.8f, 1, 0},
        {"diagonal", {21, 19}, FACE_UP_LEFT, -2.0f, -1.0f, -1, -1},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world(0);
        tile(c.wall.first, c.wall.second, PIX_H_WALL1);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        face(w, c.facing);
        w->set_lastx(c.lastx);
        w->set_lasty(c.lasty);

        EXPECT_TRUE(w->stats()->right_walk_to_point(320, 100));

        EXPECT_EQ(at(320, static_cast<float>(c.dx) * w->stepsize()), w->xpos());
        EXPECT_EQ(at(320, static_cast<float>(c.dy) * w->stepsize()), w->ypos());
        EXPECT_EQ(c.facing, w->curdir()) << "a real step along the facing, no turn";
    }
}

// ---------------------------------------------------------------------------
// direct_walk_to_point (stats.cpp:1496, 1509)
// ---------------------------------------------------------------------------

// stats.cpp:1496, the 3:1 snap toward a point. From (320,320): the point
// (330,420) is y-major (100 > 3*10): a step south; (370,370) is neither: a
// diagonal step; (420,330) is x-major: a step east. Each walker faces its
// step so walkstep moves it.
TEST_F(SemAi4, a_walk_to_a_point_aims_eight_ways_with_the_three_to_one_snap)
{
    struct Case
    {
        const char* name;
        short px;
        short py;
        int facing;
        int dx;
        int dy;
    };
    const Case cases[] = {
        {"y-major", 330, 420, FACE_DOWN, 0, 1},
        {"diagonal", 370, 370, FACE_DOWN_RIGHT, 1, 1},
        {"x-major", 420, 330, FACE_RIGHT, 1, 0},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        face(w, c.facing);

        EXPECT_TRUE(w->stats()->direct_walk_to_point(c.px, c.py));

        EXPECT_EQ(at(320, static_cast<float>(c.dx) * w->stepsize()), w->xpos());
        EXPECT_EQ(at(320, static_cast<float>(c.dy) * w->stepsize()), w->ypos());
    }
}

// stats.cpp:1509, the grid ladder toward a point, from (320,320):
//  x slide: point (520,520), wall (21,21): the diagonal is walled, the x
//      step open: x 324 (facing right);
//  x open, dx 0: point (320,520) straight below, wall (20,21): the step
//      (0,1) is walled, the x probe (the own spot) open but dx is 0: fails;
//  y slide: point (520,520), walls (21,21) and (21,20): y 324 (facing down);
//  y open, dy 0: point (520,320), wall (21,20): the step (1,0) and the x
//      probe are walled, the y probe (own spot) open but dy is 0: fails;
//  all walled: point (520,520), walls (21,21), (21,20), (20,21): fails.
// It never attacks: no command is queued.
TEST_F(SemAi4, a_walk_to_a_point_slides_along_an_open_axis_and_fails_when_walled)
{
    struct Case
    {
        const char* name;
        short px;
        short py;
        std::vector<std::pair<int, int>> walls;
        int facing;
        bool ok;
        int dx;
        int dy;
    };
    const Case cases[] = {
        {"the x slide", 520, 520, {{21, 21}}, FACE_RIGHT, true, 1, 0},
        {"x open, dx 0", 320, 520, {{20, 21}}, FACE_DOWN, false, 0, 0},
        {"the y slide", 520, 520, {{21, 21}, {21, 20}}, FACE_DOWN, true, 0, 1},
        {"y open, dy 0", 520, 320, {{21, 20}}, FACE_RIGHT, false, 0, 0},
        {"all walled", 520, 520, {{21, 21}, {21, 20}, {20, 21}}, FACE_DOWN_RIGHT, false, 0, 0},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        for (const auto& t : c.walls)
            tile(t.first, t.second, PIX_H_WALL1);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        face(w, c.facing);

        EXPECT_EQ(c.ok, w->stats()->direct_walk_to_point(c.px, c.py));

        EXPECT_EQ(at(320, static_cast<float>(c.dx) * w->stepsize()), w->xpos());
        EXPECT_EQ(at(320, static_cast<float>(c.dy) * w->stepsize()), w->ypos());
        EXPECT_EQ(c.facing, w->curdir()) << "no turn either";
        EXPECT_EQ(0u, w->stats()->commands.size());
    }
}

// ---------------------------------------------------------------------------
// walk_to_point (stats.cpp:1606, 1617, 1628, 1648)
// ---------------------------------------------------------------------------

// stats.cpp:1606. rng(300) pinned to 0: the GOTO walk returns 0 with no
// move, resets the 'closest ever' memory to 15000 and does not tick the
// re-plan counter. One draw.
TEST_F(SemAi4, a_point_walk_skips_one_tick_in_300_and_resets_its_memory)
{
    const std::uint32_t s = state_where({{300, 0}});
    walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
    ASSERT_NE(nullptr, w);
    w->stats()->set_last_distance(50);
    w->stats()->set_current_distance(50);
    w->set_path_check_counter(7);
    world().rng_.state_ = s;

    EXPECT_FALSE(w->stats()->walk_to_point(300, 96));

    EXPECT_EQ(96, w->xpos());
    EXPECT_EQ(96, w->ypos());
    EXPECT_EQ(15000u, w->stats()->last_distance());
    EXPECT_EQ(15000, w->stats()->current_distance());
    EXPECT_EQ(7, w->path_check_counter());
    EXPECT_EQ(state_after(s, {300}), world().rng_.state_);
}

// stats.cpp:1617, arrival. A soldier at (100,100) facing right with a path
// laid elsewhere. GOTO(5, 110, 100), 10 px away: arrived; the path is
// cleared, the GOTO's count is zeroed and do_command's tail pops it, no
// step. GOTO(5, 112, 100), 12 px away: not arrived; one step east (104),
// the GOTO stays with count 4.
TEST_F(SemAi4, a_point_goal_is_reached_within_12_px)
{
    const std::uint32_t s = state_where({{300, kNonZero}});
    for (short gx : {short{110}, short{112}}) {
        SCOPED_TRACE("goal x " + std::to_string(gx));
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 1, 100, 100);
        ASSERT_NE(nullptr, w);
        w->find_path_to_point(400, 400);
        ASSERT_FALSE(w->path_to_foe.empty()) << "precondition: a path to clear";
        w->set_path_check_counter(5);
        w->stats()->set_last_distance(15000);
        w->stats()->add_command(COMMAND_GOTO, 5, gx, 100);
        if (gx == 112)
            w->path_to_foe.clear();
        world().rng_.state_ = s;

        (void)w->stats()->do_command();

        EXPECT_EQ(state_after(s, {300}), world().rng_.state_);
        EXPECT_EQ(100, w->ypos());
        if (gx == 110) {
            EXPECT_EQ(0u, w->stats()->commands.size()) << "arrived: the GOTO is done";
            EXPECT_TRUE(w->path_to_foe.empty()) << "arrived: the path is cleared";
            EXPECT_EQ(100, w->xpos()) << "arrived: no step";
        } else {
            const Cmd f = front_of(w);
            ASSERT_EQ(1u, f.size) << "12 px is not arrived";
            EXPECT_EQ(COMMAND_GOTO, f.type);
            EXPECT_EQ(4, f.count);
            EXPECT_EQ(104, w->xpos()) << "one step toward the goal";
        }
    }
}

// stats.cpp:1628, the GOTO re-plan, a soldier at (96,96) facing right:
//  far, uncrowded, re-plan due: counter 5 + rng(10) (pinned to 3: 8), an A*
//      path ending on the goal cell (400,96);
//  near (80 px): the counter is redrawn, no A*;
//  far but crowded (201 bodies): no A*;
//  not due (counter 5): counter 4, the path laid before (to (400,400)) is
//      not cleared or recomputed.
TEST_F(SemAi4, a_point_walk_replans_on_its_cadence_and_paths_only_far_uncrowded_goals)
{
    enum class Case { Far, Near, Crowded, NotDue };
    const std::uint32_t s = state_where({{300, kNonZero}, {10, 3}});
    for (Case c : {Case::Far, Case::Near, Case::Crowded, Case::NotDue}) {
        SCOPED_TRACE(c == Case::Far ? "far" : c == Case::Near ? "near" : c == Case::Crowded ? "crowded" : "not due");
        fresh_world();
        if (c == Case::Crowded)
            crowd(201);
        walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
        ASSERT_NE(nullptr, w);
        w->stats()->set_last_distance(15000);
        w->set_path_check_counter(c == Case::NotDue ? 5 : 0);
        if (c == Case::NotDue) {
            w->find_path_to_point(400, 400);
            ASSERT_FALSE(w->path_to_foe.empty());
        }
        world().rng_.state_ = s;

        EXPECT_TRUE(w->stats()->walk_to_point(c == Case::Near ? 176 : 400, 96));

        if (c == Case::NotDue) {
            EXPECT_EQ(4, w->path_check_counter());
            ASSERT_FALSE(w->path_to_foe.empty());
            EXPECT_EQ((Cell{400, 400, 0}), cell_of(w->path_to_foe.back())) << "the old path stands";
        } else {
            EXPECT_EQ(8, w->path_check_counter()) << "5 + rng(10) = 8";
            if (c == Case::Far) {
                ASSERT_FALSE(w->path_to_foe.empty());
                EXPECT_EQ((Cell{400, 96, 0}), cell_of(w->path_to_foe.back()));
            } else {
                EXPECT_TRUE(w->path_to_foe.empty()) << "no A* for a near goal or on a crowded map";
            }
        }
    }
}

// stats.cpp:1648, the GOTO's ladder (no re-plan this call: counter 5):
//  (1) a path laid to (400,96) from (96,96): followed one step east (100);
//      last_distance is the distance measured before the step, 304;
//  (2) no path, a new best, straight open: goal (156,96), last 15000: one
//      direct step east, last_distance 60;
//  (3) no path, a new best, straight walled: a soldier at (320,320) facing
//      up, wall (21,20) east of it, goal (380,320): the direct step fails
//      and the right walk runs: right blocked, front open, it steps forward
//      (north, 316); last_distance 60;
//  (4) no path, not a new best (last 10), goal (156,96): last_distance stays
//      10 and the right walk's open-space step goes east (100).
TEST_F(SemAi4, a_point_walk_follows_its_path_or_charges_or_hugs_the_wall)
{
    const std::uint32_t s = state_where({{300, kNonZero}});
    {
        SCOPED_TRACE("(1) a path");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
        ASSERT_NE(nullptr, w);
        w->set_path_check_counter(5);
        w->find_path_to_point(400, 96);
        ASSERT_FALSE(w->path_to_foe.empty());
        world().rng_.state_ = s;
        EXPECT_TRUE(w->stats()->walk_to_point(400, 96));
        EXPECT_EQ(100, w->xpos());
        EXPECT_EQ(96, w->ypos());
        EXPECT_EQ(304u, w->stats()->last_distance()) << "the distance measured before the step";
    }
    for (std::uint32_t last : {15000u, 10u}) {
        SCOPED_TRACE(last == 15000u ? "(2) no path, a new best, open" : "(4) no path, not a new best");
        fresh_world();
        walker* w = living_at(FAMILY_SOLDIER, 1, 96, 96);
        ASSERT_NE(nullptr, w);
        w->set_path_check_counter(5);
        w->stats()->set_last_distance(last);
        world().rng_.state_ = s;
        EXPECT_TRUE(w->stats()->walk_to_point(156, 96));
        EXPECT_EQ(100, w->xpos());
        EXPECT_EQ(96, w->ypos());
        EXPECT_EQ(last == 15000u ? 60u : 10u, w->stats()->last_distance());
    }
    {
        SCOPED_TRACE("(3) no path, a new best, walled");
        fresh_world();
        tile(21, 20, PIX_H_WALL1);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, w);
        face(w, FACE_UP);
        w->set_path_check_counter(5);
        w->stats()->set_last_distance(15000);
        world().rng_.state_ = s;
        EXPECT_TRUE(w->stats()->walk_to_point(380, 320));
        EXPECT_EQ(320, w->xpos());
        EXPECT_EQ(316, w->ypos()) << "the right walk's forward step";
        EXPECT_EQ(60u, w->stats()->last_distance());
    }
}
