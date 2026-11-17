/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-5: the line check a shooter asks before it
// fires (walker::fire_check), and the one step of a flying weapon it shares
// with the shot (walker::flight_step). Each test is named after the rule it
// asserts.
//
// Every scene is headless: an open field of grass 1024 px a side (GRID_SIZE
// 16), bodies placed in pixels, the sim stream (GameWorld::rng_) pinned to a
// state whose draws are known. A weapon's numbers (its step, its line of
// sight, its box) are read from a weapon the game builds, never typed.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
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
[[maybe_unused]] std::uint32_t state_where(std::initializer_list<Draw> draws)
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
[[maybe_unused]] std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// The unit step of each facing (UP, UR, R, DR, D, DL, L, UL).
constexpr int kStep[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

const char* dir_name(int dir)
{
    static const char* const kNames[8] = {"UP", "UP_RIGHT", "RIGHT", "DOWN_RIGHT",
                                          "DOWN", "DOWN_LEFT", "LEFT", "UP_LEFT"};
    return (dir >= 0 && dir < 8) ? kNames[dir] : "?";
}

class SemLineCheck : public ::testing::Test {
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

    // An open field of grass, `tiles_w` x `tiles_h` tiles (1024 px a side by
    // default), with nobody on it.
    void fresh_world(short fixes = 0, int tiles_w = 64, int tiles_h = 64)
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(tiles_w, tiles_h);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.rng_.state_ = 12345u;
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
    // walking, facing right. Its own box is the family's.
    walker* living_at(int family, unsigned char team, short x, short y)
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
        face(w, FACE_RIGHT);
        return place(w, x, y, w->sizex(), w->sizey());
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

    // The shot `shooter` builds for the way it faces, pointed straight along
    // that facing at its own step (no waver) and already facing it, so its
    // first walk() steps; placed at (x, y), nothing met yet.
    walker* aimed_shot(walker* shooter, int dir, short x, short y)
    {
        walker* shot = shooter->create_weapon();
        if (shot == nullptr)
            return nullptr;
        const float s = shot->stepsize();
        shot->set_lastx(static_cast<float>(kStep[dir][0]) * s);
        shot->set_lasty(static_cast<float>(kStep[dir][1]) * s);
        shot->set_curdir(static_cast<signed char>(dir));
        place(shot, x, y, shot->sizex(), shot->sizey());
        shot->set_collide_ob(nullptr);
        return shot;
    }

    // Do the boxes of `a` at (ax, ay) and `b` where it stands meet, by the
    // obmap's own test (each box shrunk by a pixel)?
    static bool meets(const walker* a, float ax, float ay, const walker* b)
    {
        return ::collide(static_cast<short>(ax), static_cast<short>(ay), a->sizex(), a->sizey(),
                         b->xpos(), b->ypos(), b->sizex(), b->sizey()) != 0;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// One step of a weapon in flight (walker_movement.cpp, walker::flight_step)
// ---------------------------------------------------------------------------

// walker::flight_step, both settings (it is not gated). An archer's arrow
// pointed right along its own step (a cardinal arrow's step, which is not a
// whole number of pixels):
//  (a) open ground: Flew, twice; the arrow moved exactly two steps and kept
//      the fraction (its world position is two steps on, where a move cut to
//      a whole pixel each step would have dropped it);
//  (b) a live hostile orc whose box the next step's box meets: Struck, the
//      arrow did not move and its collision partner is the orc;
//  (c) a wall tile under the next step's box only: Stopped, not moved,
//      nothing met;
//  (d) a wave (NO_COLLIDE and IMMORTAL) whose next box meets an orc: Struck,
//      and it moved its step (a NO_COLLIDE weapon passes the body it strikes).
TEST_F(SemLineCheck, one_flight_step_tells_a_strike_from_a_stop_and_a_free_step)
{
    constexpr short kX = 400;
    constexpr short kY = 320;
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        {
            SCOPED_TRACE("(a) open ground");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            ASSERT_NE(std::floor(2.0f * step), 2.0f * step)
                << "the staging: two steps are not a whole number of pixels";
            ASSERT_NE(static_cast<float>(2 * static_cast<int>(step)), 2.0f * step)
                << "the staging: two moves cut to a whole pixel each would land elsewhere";
            EXPECT_EQ(walker::FlightStep::Flew, arrow->flight_step()) << "first step";
            EXPECT_EQ(walker::FlightStep::Flew, arrow->flight_step()) << "second step";
            EXPECT_FLOAT_EQ(static_cast<float>(kX) + 2.0f * step, arrow->worldx())
                << "two steps on, the fraction kept";
            EXPECT_EQ(static_cast<short>(static_cast<float>(kX) + 2.0f * step), arrow->xpos());
            EXPECT_FLOAT_EQ(static_cast<float>(kY), arrow->worldy());
            EXPECT_EQ(nullptr, arrow->collide_ob()) << "nothing met";
        }
        {
            SCOPED_TRACE("(b) a live hostile body ahead");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(kX + arrow->sizex()),
                                    static_cast<short>(kY + (arrow->sizey() - 16) / 2));
            ASSERT_NE(nullptr, orc);
            ASSERT_FALSE(meets(arrow, kX, kY, orc)) << "the staging: the orc is not on the arrow's box";
            ASSERT_TRUE(meets(arrow, static_cast<float>(kX) + step, kY, orc))
                << "the staging: the next step's box meets the orc";
            EXPECT_EQ(walker::FlightStep::Struck, arrow->flight_step());
            EXPECT_EQ(orc, arrow->collide_ob()) << "the body it struck";
            EXPECT_FLOAT_EQ(static_cast<float>(kX), arrow->worldx()) << "a struck step is not taken";
            EXPECT_FLOAT_EQ(400.0f, orc->stats()->hitpoints()) << "the step only tells; act_fire strikes";
        }
        {
            SCOPED_TRACE("(c) a wall ahead");
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 300, kY);
            ASSERT_NE(nullptr, archer);
            walker* arrow = aimed_shot(archer, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, arrow);
            const float step = arrow->stepsize();
            const int wall_tx = (static_cast<int>(static_cast<float>(kX) + step) + arrow->sizex()) / GRID_SIZE;
            tile(wall_tx, kY / GRID_SIZE, PIX_H_WALL1);
            ASSERT_TRUE(world().query_grid_passable(kX, kY, arrow)) << "the staging: open where it stands";
            ASSERT_FALSE(world().query_grid_passable(static_cast<float>(kX) + step, kY, arrow))
                << "the staging: the wall is under the next step's box";
            EXPECT_EQ(walker::FlightStep::Stopped, arrow->flight_step());
            EXPECT_EQ(nullptr, arrow->collide_ob()) << "a wall is no body";
            EXPECT_FLOAT_EQ(static_cast<float>(kX), arrow->worldx()) << "a stopped step is not taken";
        }
        {
            SCOPED_TRACE("(d) a NO_COLLIDE, IMMORTAL wave over a body");
            fresh_world(fixes);
            walker* mage = living_at(FAMILY_MAGE, 0, 300, kY);
            ASSERT_NE(nullptr, mage);
            mage->set_current_weapon(FAMILY_WAVE);
            walker* wave = aimed_shot(mage, FACE_RIGHT, kX, kY);
            ASSERT_NE(nullptr, wave);
            ASSERT_EQ(FAMILY_WAVE, wave->family());
            ASSERT_TRUE(wave->stats()->query_bit_flags(BIT_NO_COLLIDE)) << "the staging: the wave is NO_COLLIDE";
            ASSERT_TRUE(wave->stats()->query_bit_flags(BIT_IMMORTAL)) << "the staging: the wave is IMMORTAL";
            const float step = wave->stepsize();
            walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(kX + wave->sizex()),
                                    static_cast<short>(kY + (wave->sizey() - 16) / 2));
            ASSERT_NE(nullptr, orc);
            ASSERT_FALSE(meets(wave, kX, kY, orc)) << "the staging: the orc is not on the wave's box";
            ASSERT_TRUE(meets(wave, static_cast<float>(kX) + step, kY, orc))
                << "the staging: the next step's box meets the orc";
            EXPECT_EQ(walker::FlightStep::Struck, wave->flight_step());
            EXPECT_EQ(orc, wave->collide_ob());
            EXPECT_FLOAT_EQ(static_cast<float>(kX) + step, wave->worldx()) << "a NO_COLLIDE step is taken";
        }
    }
}

// walker::fire (walker.cpp: a fresh shot's facing is (frame + 1) % 2, "so
// first walk will just be draw") and walker::act_fire's countdown, both
// settings. An archer at the middle of the field fires an arrow along each
// facing and the arrow acts until it dies; the acts on which its position
// changed are counted. A shot whose fresh facing is not its heading spends
// its first act turning, so it moves lineofsight - 1 times; FACE_UP is 0 and
// FACE_UP_RIGHT is 1, so an UP shot from an odd shooter frame and an UP_RIGHT
// shot from an even one already face their heading and move lineofsight
// times. Each facing is fired from an even and an odd frame, and every count
// is asserted exactly. The line check (Gameplay Fixes BD) flies the steps
// every shot moves: lineofsight - 1.
TEST_F(SemLineCheck, a_fresh_shot_moves_one_step_fewer_than_its_line_of_sight)
{
    ASSERT_EQ(0, FACE_UP);
    ASSERT_EQ(1, FACE_UP_RIGHT);
    for (short fixes : {short{0}, short{1}}) {
        for (int dir = 0; dir < 8; ++dir) {
            for (short frame : {short{0}, short{1}}) {
                SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                             dir_name(dir) + ", shooter frame " + std::to_string(frame));
                fresh_world(fixes);
                walker* archer = living_at(FAMILY_ARCHER, 0, 504, 504);
                ASSERT_NE(nullptr, archer);
                face(archer, dir);
                archer->set_frame(frame);
                ASSERT_EQ(frame, archer->frame()) << "the staging: the shooter's frame";
                walker* shot = archer->fire();
                ASSERT_NE(nullptr, shot) << "the staging: the arrow flies";
                const int los = shot->lineofsight();
                ASSERT_GT(los, 1) << "the staging: a line of sight read from the arrow";
                const bool faces_heading = ((frame + 1) % 2) == dir;
                int moves = 0;
                int acts = 0;
                while (!shot->dead() && acts < los + 4) {
                    const float x = shot->worldx();
                    const float y = shot->worldy();
                    shot->act();
                    ++acts;
                    if (shot->worldx() != x || shot->worldy() != y)
                        ++moves;
                }
                ASSERT_TRUE(shot->dead()) << "the staging: the arrow's range ran out on open ground";
                EXPECT_EQ(faces_heading ? los : los - 1, moves)
                    << (faces_heading ? "already facing its heading: lineofsight moves"
                                      : "the first act turns: lineofsight - 1 moves");
            }
        }
    }
}
