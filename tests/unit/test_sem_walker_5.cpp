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

using Denial = walker::FireCheckDenial;

// The first RNG state whose first draw (of `first_bound`) answers
// `first_want` and whose next draws, all of `bound`, answer as `keep` says.
template <typename Keep>
std::uint32_t state_where_then(std::uint32_t first_bound, std::uint32_t first_want, std::uint32_t bound,
                               int n, Keep keep)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        bool all = probe.next(first_bound) == first_want;
        for (int i = 0; all && i < n; ++i)
            all = keep(probe.next(bound));
        if (all)
            return seed;
    }
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
    // walking, facing right. Its box is the family's unless (sx, sy) is given.
    walker* living_at(int family, unsigned char team, short x, short y, short sx = -1, short sy = -1)
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
        return place(w, x, y, sx < 0 ? w->sizex() : sx, sy < 0 ? w->sizey() : sy);
    }

    // The game's own snap-face (walker::face_delta): curdir, enddir and the
    // heading fire_check aims along (the facing's unit step times stepsize,
    // or the unit step itself for a tower, whose stepsize is 0).
    static void face(walker* w, int dir)
    {
        w->face_delta(static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]));
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

    // A box of (sx, sy) at `b` meets `body` where it stands (the obmap's test).
    struct Box
    {
        int x = 0;
        int y = 0;
    };
    static bool box_meets(Box b, short sx, short sy, const walker* body)
    {
        return ::collide(static_cast<short>(b.x), static_cast<short>(b.y), sx, sy,
                         body->xpos(), body->ypos(), body->sizex(), body->sizey()) != 0;
    }

    // The shot a shooter would take the way it faces, read from a weapon the
    // game builds for it and then drops (no draw): its step and line of sight
    // after the shooter's profile, its box, the pad fire() and the line check
    // put it on (walker::weapon_pad), its heading with a waver of 0, the
    // waver draw (walker::set_weapon_heading: next(h + 1) - h / 2, h the
    // step / 2 clamped to 0..127) and the reach gate (step x line of sight).
    struct ShotPlan
    {
        float step = 0.0f;
        int los = 0;
        short sx = 0;
        short sy = 0;
        short pad_x = 0;
        short pad_y = 0;
        float lx = 0.0f;
        float ly = 0.0f;
        std::uint32_t waver_bound = 0;
        std::uint32_t waver_zero = 0;
        std::int32_t reach = 0;
    };
    ShotPlan plan_of(walker* shooter, int dir)
    {
        ShotPlan p;
        walker* w = shooter->create_weapon();
        if (w == nullptr)
            return p;
        p.step = w->stepsize();
        p.los = w->lineofsight();
        p.sx = w->sizex();
        p.sy = w->sizey();
        shooter->weapon_pad(*w, p.pad_x, p.pad_y);
        const auto h = static_cast<signed char>(std::clamp(p.step / 2, 0.0f, 127.0f));
        p.waver_bound = static_cast<std::uint32_t>(h + 1);
        p.waver_zero = static_cast<std::uint32_t>(h / 2);
        p.lx = static_cast<float>(kStep[dir][0]) * p.step;
        p.ly = static_cast<float>(kStep[dir][1]) * p.step;
        p.reach = static_cast<std::int32_t>(p.step) * static_cast<std::int32_t>(p.los);
        world().remove_ob(w);
        return p;
    }

    // The spots fire_check's classic loop tests: the pad, then each spot i
    // steps on from the last, cut to a whole pixel (setxy(xpos + i * lastx)):
    // 0, 1, 3, 6, 10 ... steps out.
    static std::vector<Box> classic_spots(const ShotPlan& p)
    {
        std::vector<Box> spots;
        std::int32_t x = p.pad_x;
        std::int32_t y = p.pad_y;
        for (int i = 0; i < p.los; ++i) {
            x = static_cast<short>(static_cast<std::int32_t>(static_cast<float>(x) + static_cast<float>(i) * p.lx));
            y = static_cast<short>(static_cast<std::int32_t>(static_cast<float>(y) + static_cast<float>(i) * p.ly));
            spots.push_back({x, y});
        }
        return spots;
    }

    // The boxes the shot's own steps test (walker::walk: the box at the
    // whole-pixel position plus one step), steps 1 .. lineofsight - 1, when
    // nothing stops them; the shot keeps its fraction between steps.
    static std::vector<Box> flown_boxes(const ShotPlan& p)
    {
        std::vector<Box> boxes;
        float wx = p.pad_x;
        float wy = p.pad_y;
        for (int k = 1; k < p.los; ++k) {
            const short xp = static_cast<short>(wx);
            const short yp = static_cast<short>(wy);
            boxes.push_back({static_cast<std::int32_t>(static_cast<float>(xp) + p.lx),
                             static_cast<std::int32_t>(static_cast<float>(yp) + p.ly)});
            wx += p.lx;
            wy += p.ly;
        }
        return boxes;
    }

    // Where the shot stands when each of those steps is asked (its whole-pixel
    // position: what the arrow slit's gamble measures from its shooter).
    static std::vector<Box> flown_from(const ShotPlan& p)
    {
        std::vector<Box> from;
        float wx = p.pad_x;
        float wy = p.pad_y;
        for (int k = 1; k < p.los; ++k) {
            from.push_back({static_cast<short>(wx), static_cast<short>(wy)});
            wx += p.lx;
            wy += p.ly;
        }
        return from;
    }

    // Every weapon and effect leaves the world (the probes fire_check left
    // dead, a shot that ended, what its death made).
    void drop_weapons()
    {
        std::vector<walker*> gone;
        for (const auto& u : world().weaplist)
            gone.push_back(u.get());
        for (const auto& u : world().fxlist)
            gone.push_back(u.get());
        for (walker* w : gone)
            world().remove_ob(w);
    }

    // The live weapons in the world.
    int live_weapons()
    {
        int n = 0;
        for (const auto& u : world().weaplist)
            if (u && !u->dead())
                ++n;
        return n;
    }

    // fire() from the pinned stream, then the shot acts until it ends (as
    // weap::act does each tick). True when the shot ended.
    bool fire_and_fly(walker* shooter, int max_acts)
    {
        walker* shot = shooter->fire();
        for (int a = 0; shot != nullptr && !shot->dead() && a < max_acts; ++a)
            shot->act();
        return shot == nullptr || shot->dead();
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

// ---------------------------------------------------------------------------
// The line check flies the shot (walker.cpp fire_check, walker_movement.cpp
// walker::line_check_flight: Gameplay Fixes BD, #538)
// ---------------------------------------------------------------------------

// Gameplay Fixes BD, setting on: the check answers whether the shot lands.
// For each shooter a bot can be, each of the facings UP, RIGHT and
// DOWN_RIGHT, each sideways offset 0, 6, 12 and 18 px and every gap from 0
// px to the weapon's flight plus 16 px, one hostile soldier with a 16 px box
// stands at that gap from the shooter's face. fire_check is asked along the
// facing, then from the same staging and the same stream fire() is called
// and the shot acts until it ends; the soldier was struck when it lost hit
// points. For every gap the reach gate does not refuse (OutOfRange), the check
// passes if and only if the soldier was struck. Per shooter and facing the
// test counts what it compared and requires both outcomes (the sideways
// offsets give the misses). One world per shooter and facing serves every gap.
//
// The staging is open grass on one floor, and nothing else: no tree or shrub
// (a shot loses range there and the check does not count it), no arrow slit
// (the check takes a slit as open and the shot gambles), no air cell (a rock
// or a knife would drop a floor), and a soldier shooter is handed its knives
// before each throw (its family can refuse a throw with none left, which the
// check does not ask). The waver is pinned to the draw that gives 0, for the
// check and for the shot. A tower's own blow does nothing (its melee damage
// is 0), so every shooter is given a blow of at least 10, or a body struck on
// the pad would not show in its hit points; the check never reads it. The
// soldier target wears no armour, so every strike shows.
//
// What this test cannot tell: a check whose test weapon moves by whole pixels
// (setxy(xpos() + lastx(), ...), #82's form) answers like the shot's own
// sub-pixel flight at every gap for one body in open ground, inside the reach
// gate. That the check takes the shot's own step is kept by the one function
// they share (walker::flight_step) and by
// one_flight_step_tells_a_strike_from_a_stop_and_a_free_step. With the setting
// off this test compares nothing: the classic check's rule is
// classic_the_check_steps_over_a_foe_between_two_of_its_spots.
TEST_F(SemLineCheck, the_check_passes_exactly_where_the_shot_lands_with_the_setting_on)
{
    struct Shooter
    {
        int family;
        const char* name;
    };
    const Shooter kShooters[] = {{FAMILY_TOWER1, "tower"},      {FAMILY_ARCHER, "archer"},
                                 {FAMILY_DRUID, "druid"},       {FAMILY_GOLEM, "golem"},
                                 {FAMILY_FIREELEMENTAL, "fire elemental"},
                                 {FAMILY_FAERIE, "faerie"},     {FAMILY_MAGE, "mage"},
                                 {FAMILY_SKELETON, "skeleton"}, {FAMILY_SOLDIER, "soldier"},
                                 {FAMILY_ELF, "elf"}};
    const int kFacings[] = {FACE_UP, FACE_RIGHT, FACE_DOWN_RIGHT};
    const int kOffsets[] = {0, 6, 12, 18};
    constexpr short kX = 480;
    constexpr short kY = 480;
    for (const Shooter& sh : kShooters) {
        for (int dir : kFacings) {
            SCOPED_TRACE(std::string(sh.name) + " facing " + dir_name(dir));
            fresh_world(1);
            walker* shooter = living_at(sh.family, 0, kX, kY);
            ASSERT_NE(nullptr, shooter);
            face(shooter, dir);
            ASSERT_EQ(dir, shooter->curdir()) << "the staging: the shooter faces " << dir_name(dir);
            ASSERT_FALSE(shooter->stats()->query_bit_flags(BIT_NO_RANGED)) << "the staging: a shooter";
            if (shooter->damage() < 10.0f)
                shooter->set_damage(10.0f);
            walker* target = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
            ASSERT_NE(nullptr, target);
            target->set_act_type(ACT_SIT);
            target->stats()->set_armor(0.0f);
            shooter->set_foe(target);
            const ShotPlan p = plan_of(shooter, dir);
            ASSERT_GT(p.los, 1) << "the staging: a weapon the shooter builds";
            const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
            const int last_gap = static_cast<int>(p.step * static_cast<float>(p.los - 1)) + 16;
            const int w = shooter->sizex();
            const int h = shooter->sizey();
            int compared = 0;
            int hits = 0;
            int misses = 0;
            int wrong = 0;
            for (int off : kOffsets) {
                for (int gap = 0; gap <= last_gap; ++gap) {
                    int tx = 0;
                    int ty = 0;
                    if (dir == FACE_UP) {
                        tx = kX + (w - 16) / 2 + off;
                        ty = kY - gap - 16;
                    } else if (dir == FACE_RIGHT) {
                        tx = kX + w + gap;
                        ty = kY + (h - 16) / 2 + off;
                    } else {
                        const int d = off * 7 / 10;  // off px across the diagonal
                        tx = kX + w + gap + d;
                        ty = kY + h + gap - d;
                    }
                    place(target, static_cast<short>(tx), static_cast<short>(ty), 16, 16);
                    target->stats()->set_hitpoints(400.0f);
                    shooter->stats()->set_magicpoints(500.0f);
                    shooter->set_weapons_left(4);
                    world().rng_.state_ = straight;
                    Denial why = Denial::NoFoe;
                    const bool passes =
                        shooter->fire_check(static_cast<short>(kStep[dir][0]), static_cast<short>(kStep[dir][1]), &why);
                    drop_weapons();
                    if (why == Denial::OutOfRange)
                        continue;
                    world().rng_.state_ = straight;
                    shooter->stats()->set_magicpoints(500.0f);
                    const bool ended = fire_and_fly(shooter, 4 * p.los);
                    const bool struck = target->stats()->hitpoints() < 400.0f;
                    drop_weapons();
                    ASSERT_TRUE(ended) << "the staging: the shot ends inside its range";
                    ++compared;
                    (struck ? hits : misses) += 1;
                    if (struck != passes && ++wrong <= 5)
                        ADD_FAILURE() << "gap " << gap << " px, offset " << off << " px: the check "
                                      << (passes ? "passes" : "refuses") << " (denial "
                                      << static_cast<int>(why) << ") but the shot "
                                      << (struck ? "strikes" : "misses");
                }
            }
            EXPECT_EQ(0, wrong) << "gaps where the check and the shot disagree, of " << compared;
            EXPECT_GT(hits, 0) << "the staging: some gaps where the shot strikes";
            EXPECT_GT(misses, 0) << "the staging: some gaps where the shot misses";
        }
    }
}

// fire_check, the classic loop (walker.cpp), and line_check_flight's gate.
// A tower facing up on a field 160 tiles tall (every spot of the classic loop
// stays on the map, asserted spot by spot) and a 16 px soldier on the shot's
// line. The test computes the classic spots itself, from a probe's pad and
// step (p[0] the pad, p[i] = trunc(p[i-1] + i * step)), and picks the first
// gap inside the reach gate at which no spot's box meets the soldier; a fired
// arrow (waver 0) strikes it there. Setting off: refused, RayMiss. Setting
// on: passes. Control, the first gap at which a classic spot's box meets the
// soldier: passes on both settings.
TEST_F(SemLineCheck, classic_the_check_steps_over_a_foe_between_two_of_its_spots)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes, 64, 160);
        walker* tower = living_at(FAMILY_TOWER1, 0, 496, 2304);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        walker* body = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        tower->set_foe(body);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> spots = classic_spots(p);
        const int map_h = world().grid.h * GRID_SIZE;
        for (const Box& b : spots)
            ASSERT_TRUE(b.y >= 0 && b.y + p.sy < map_h) << "the staging: every classic spot is on the map (y " << b.y << ")";
        const short body_x = static_cast<short>(p.pad_x + p.sx / 2 - 8);
        const auto at_gap = [&](int gap) {
            place(body, body_x, static_cast<short>(tower->ypos() - gap - 16), 16, 16);
        };
        const auto classic_meets = [&] {
            for (const Box& b : spots)
                if (box_meets(b, p.sx, p.sy, body))
                    return true;
            return false;
        };
        // The first gap (from the tower's face to the soldier's near edge) inside the reach gate
        // at which no classic spot's box meets the soldier, and the first at which one does.
        struct
        {
            int blind = -1;
            int seen = -1;
        } g;
        for (int gap = 0; g.blind < 0 || g.seen < 0; ++gap) {
            at_gap(gap);
            if (tower->distance_to_ob(body) > p.reach)
                break;
            const bool met = classic_meets();
            if (!met && g.blind < 0)
                g.blind = gap;
            if (met && g.seen < 0)
                g.seen = gap;
        }
        ASSERT_GE(g.blind, 0) << "the staging: a gap inside the reach that no classic spot meets";
        ASSERT_GE(g.seen, 0) << "the staging: a gap a classic spot meets";
        const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});

        at_gap(g.blind);
        ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: inside the reach gate";
        ASSERT_FALSE(classic_meets()) << "the staging: between two classic spots";
        world().rng_.state_ = straight;
        Denial why = Denial::None;
        const bool passes = tower->fire_check(0, -1, &why);
        drop_weapons();
        if (fixes) {
            EXPECT_TRUE(passes) << "gap " << g.blind << ": the flown check meets the soldier";
            EXPECT_EQ(Denial::None, why);
        } else {
            EXPECT_FALSE(passes) << "gap " << g.blind << ": the classic check steps over the soldier";
            EXPECT_EQ(Denial::RayMiss, why);
        }
        world().rng_.state_ = straight;
        ASSERT_TRUE(fire_and_fly(tower, 4 * p.los));
        drop_weapons();
        EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "the staging: a fired arrow strikes the soldier there";

        at_gap(g.seen);
        body->stats()->set_hitpoints(400.0f);
        world().rng_.state_ = straight;
        why = Denial::NoFoe;
        EXPECT_TRUE(tower->fire_check(0, -1, &why)) << "control, gap " << g.seen << ": a classic spot meets it";
        EXPECT_EQ(Denial::None, why);
        drop_weapons();
    }
}

// line_check_flight's pad questions, on a DIAGONAL pad, where the pad
// question decides alone: for a cardinal facing the first flight step meets
// the same tile or body as the pad. An archer facing up and right; the pad
// fire() uses and the box of the shot's first step are computed from the
// arrow's numbers, and the tile and body below are placed under the pad's box
// and clear of the first step's box (asserted). (i) A wall tile under the pad:
// WallBlocked on both settings, and a fired shot is wasted (no weapon flies,
// its magic is spent). (ii) A hostile body under the pad: passes on both
// settings, and fire() lands the blow. The foe of (i) stands in reach but off
// the flight's line.
TEST_F(SemLineCheck, the_pad_passes_on_a_body_and_refuses_on_a_wall_on_both_settings)
{
    constexpr short kX = 320;
    constexpr short kY = 329;
    for (short fixes : {short{0}, short{1}}) {
        for (bool wall : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (wall ? "(i) a wall under the pad" : "(ii) a body under the pad"));
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, kX, kY);
            ASSERT_NE(nullptr, archer);
            face(archer, FACE_UP_RIGHT);
            const ShotPlan p = plan_of(archer, FACE_UP_RIGHT);
            ASSERT_GT(p.los, 1);
            const Box pad{p.pad_x, p.pad_y};
            const Box first = flown_boxes(p).front();
            walker* foe = nullptr;
            if (wall) {
                // The tile under the pad box's bottom-left pixel; the first step's box lies a
                // whole box up and right of the pad's.
                const int ttx = pad.x / GRID_SIZE;
                const int tty = (pad.y + p.sy - 1) / GRID_SIZE;
                tile(ttx, tty, PIX_H_WALL1);
                walker* probe = archer->create_weapon();
                ASSERT_NE(nullptr, probe);
                ASSERT_FALSE(world().query_grid_passable(static_cast<float>(pad.x), static_cast<float>(pad.y), probe)) << "the staging: the wall is under the pad";
                ASSERT_TRUE(world().query_grid_passable(static_cast<float>(first.x), static_cast<float>(first.y), probe))
                    << "the staging: the first step's box is clear of it";
                world().remove_ob(probe);
                foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(kX + 40), static_cast<short>(kY + 40), 16, 16);
                ASSERT_NE(nullptr, foe);
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the flight's line";
            } else {
                foe = living_at(FAMILY_SOLDIER, 1, static_cast<short>(pad.x + 3), static_cast<short>(pad.y + 3), 16, 16);
                ASSERT_NE(nullptr, foe);
                ASSERT_TRUE(box_meets(pad, p.sx, p.sy, foe)) << "the staging: the body is under the pad's box";
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: and under no step's box";
                ASSERT_FALSE(meets(archer, archer->xpos(), archer->ypos(), foe)) << "the staging: clear of the archer";
            }
            foe->set_act_type(ACT_SIT);
            archer->set_foe(foe);
            ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
            world().rng_.state_ = straight;
            Denial why = Denial::NoFoe;
            const bool passes = archer->fire_check(1, -1, &why);
            drop_weapons();
            if (wall) {
                EXPECT_FALSE(passes) << "a wall on the pad: the shot would be wasted";
                EXPECT_EQ(Denial::WallBlocked, why);
            } else {
                EXPECT_TRUE(passes) << "a body on the pad: the blow lands";
                EXPECT_EQ(Denial::None, why);
            }
            world().rng_.state_ = straight;
            const float mp = archer->stats()->magicpoints();
            walker* shot = archer->fire();
            EXPECT_EQ(nullptr, shot) << "no shot leaves a pad that is held";
            EXPECT_EQ(0, live_weapons()) << "nothing flies";
            EXPECT_FLOAT_EQ(mp - archer->stats()->weapon_cost(), archer->stats()->magicpoints()) << "the cost is paid";
            if (wall)
                EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "a wall on the pad: the shot is wasted";
            else
                EXPECT_LT(foe->stats()->hitpoints(), 400.0f) << "a body on the pad: fire() lands the blow";
            drop_weapons();
        }
    }
}

// The issue's case (#538): a tower whose top row is y 560 facing up, a wall
// tile on rows 448 to 463 across the shot's line, a 16 px soldier on rows 423
// to 438. The soldier is inside the reach gate, the classic spots (computed
// as above) jump the wall and one of them meets the soldier, and a flown step
// meets the wall before any flown step meets the soldier (all asserted).
// Setting on: WallBlocked. Setting off: passes. Both: a fired arrow (waver 0)
// stops on the wall and leaves the soldier's hit points as they were.
TEST_F(SemLineCheck, a_wall_the_shot_stops_on_refuses_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes);
        walker* tower = living_at(FAMILY_TOWER1, 0, 500, 560);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        for (int tx = p.pad_x / GRID_SIZE; tx <= (p.pad_x + p.sx - 1) / GRID_SIZE; ++tx)
            tile(tx, 448 / GRID_SIZE, PIX_H_WALL1);
        walker* body = living_at(FAMILY_SOLDIER, 1, static_cast<short>(p.pad_x + p.sx / 2 - 8), 423, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        tower->set_foe(body);
        ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: inside the reach gate";
        walker* probe = tower->create_weapon();
        ASSERT_NE(nullptr, probe);
        bool classic_meets = false;
        for (const Box& b : classic_spots(p)) {
            if (box_meets(b, p.sx, p.sy, body)) {
                classic_meets = true;
                break;
            }
            ASSERT_TRUE(world().query_grid_passable(static_cast<float>(b.x), static_cast<float>(b.y), probe))
                << "the staging: the classic spot at y " << b.y << " is clear of the wall";
        }
        ASSERT_TRUE(classic_meets) << "the staging: a classic spot past the wall meets the soldier";
        bool walled_first = false;
        for (const Box& b : flown_boxes(p)) {
            ASSERT_FALSE(box_meets(b, p.sx, p.sy, body)) << "the staging: no flown step reaches the soldier first";
            if (!world().query_grid_passable(static_cast<float>(b.x), static_cast<float>(b.y), probe)) {
                walled_first = true;
                break;
            }
        }
        world().remove_ob(probe);
        ASSERT_TRUE(walled_first) << "the staging: a flown step meets the wall";
        const std::uint32_t straight = state_where({{p.waver_bound, p.waver_zero}});
        world().rng_.state_ = straight;
        Denial why = Denial::NoFoe;
        const bool passes = tower->fire_check(0, -1, &why);
        drop_weapons();
        if (fixes) {
            EXPECT_FALSE(passes) << "the shot would stop on the wall";
            EXPECT_EQ(Denial::WallBlocked, why);
        } else {
            EXPECT_TRUE(passes) << "the classic check steps over the wall";
            EXPECT_EQ(Denial::None, why);
        }
        world().rng_.state_ = straight;
        ASSERT_TRUE(fire_and_fly(tower, 4 * p.los));
        drop_weapons();
        EXPECT_FLOAT_EQ(400.0f, body->stats()->hitpoints()) << "the arrow stops on the wall";
    }
}

// The barrage arena of the parity rows (#538's worked case): a tower at
// (173, 60) facing up at an archer at (176, 2) on the map's top row. Setting
// on: passes, the flight meets the archer before the edge. Setting off:
// WallBlocked, a classic spot is off the map before any spot meets the archer
// (asserted). And the case the name promises: the archer moved sideways out
// of every box the flight tests, still inside the reach gate (both asserted).
// Setting on: WallBlocked, because the flight leaves the map before its steps
// run out (the step at which it does is computed and asserted to be under
// the arrow's line of sight).
TEST_F(SemLineCheck, the_map_edge_refuses_only_when_the_shot_would_leave_the_map)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool moved : {false, true}) {
            if (moved && !fixes)
                continue;
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off") +
                         (moved ? ", the archer moved off the line" : ", the barrage arena"));
            fresh_world(fixes);
            walker* tower = living_at(FAMILY_TOWER1, 0, 173, 60);
            ASSERT_NE(nullptr, tower);
            face(tower, FACE_UP);
            const ShotPlan p = plan_of(tower, FACE_UP);
            ASSERT_GT(p.los, 1);
            const short archer_x = moved ? static_cast<short>(p.pad_x + p.sx + 12) : short{176};
            walker* archer = living_at(FAMILY_ARCHER, 1, archer_x, 2, 16, 16);
            ASSERT_NE(nullptr, archer);
            archer->set_act_type(ACT_SIT);
            tower->set_foe(archer);
            ASSERT_LE(tower->distance_to_ob(archer), p.reach) << "the staging: inside the reach gate";
            // The step at which the flight's box would leave the map (walker::walk: a step whose
            // top-left would go below 0 is not taken).
            int leaves_at = -1;
            float wy = p.pad_y;
            bool met_first = false;
            for (int k = 1; k < p.los; ++k) {
                const short yp = static_cast<short>(wy);
                if (static_cast<float>(yp) + p.ly < 0.0f) {
                    leaves_at = k;
                    break;
                }
                const Box b{static_cast<std::int32_t>(static_cast<float>(p.pad_x) + p.lx),
                            static_cast<std::int32_t>(static_cast<float>(yp) + p.ly)};
                if (box_meets(b, p.sx, p.sy, archer)) {
                    met_first = true;
                    break;
                }
                wy += p.ly;
            }
            if (moved) {
                ASSERT_FALSE(met_first) << "the staging: no box the flight tests meets the archer";
                ASSERT_GT(leaves_at, 0) << "the staging: the flight leaves the map";
                ASSERT_LT(leaves_at, p.los) << "the staging: before its steps run out";
            } else {
                ASSERT_TRUE(met_first) << "the staging: the flight meets the archer before the edge";
                bool off_first = false;
                for (const Box& b : classic_spots(p)) {
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, archer)) << "the staging: no classic spot meets the archer";
                    if (b.y < 0) {
                        off_first = true;
                        break;
                    }
                }
                ASSERT_TRUE(off_first) << "the staging: a classic spot leaves the map";
            }
            world().rng_.state_ = state_where({{p.waver_bound, p.waver_zero}});
            Denial why = Denial::NoFoe;
            const bool passes = tower->fire_check(0, -1, &why);
            if (fixes && !moved) {
                EXPECT_TRUE(passes) << "the flight meets the archer before the edge";
                EXPECT_EQ(Denial::None, why);
            } else {
                EXPECT_FALSE(passes) << (moved ? "the flight leaves the map at step " + std::to_string(leaves_at)
                                               : std::string("a classic spot is off the map"));
                EXPECT_EQ(Denial::WallBlocked, why);
            }
        }
    }
}

// fire_check's random stream in open ground, both settings: the waver it
// draws for its probe (walker::set_weapon_heading) and nothing else. A
// level-4 soldier's knife (a short weapon: the classic loop's last spot is
// 36 steps of about 7 px past the pad, still on the 1024 px field, so a
// RayMiss is reachable on both settings). A foe straight ahead (passes) and a
// foe in reach but off every box both loops test (RayMiss); the stream after
// each equals the waver's one draw, its bound derived from the knife.
TEST_F(SemLineCheck, the_check_draws_its_waver_and_nothing_else_in_open_ground)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool on_line : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (on_line ? "a foe on the line" : "a foe off the line"));
            fresh_world(fixes);
            walker* soldier = living_at(FAMILY_SOLDIER, 0, 480, 480);
            ASSERT_NE(nullptr, soldier);
            face(soldier, FACE_RIGHT);
            const ShotPlan p = plan_of(soldier, FACE_RIGHT);
            ASSERT_GT(p.los, 1);
            const short fy = on_line ? static_cast<short>(p.pad_y + p.sy / 2 - 8) : static_cast<short>(480 + 24);
            walker* foe = living_at(FAMILY_ORC, 1, static_cast<short>(p.pad_x + 10), fy, 16, 16);
            ASSERT_NE(nullptr, foe);
            foe->set_act_type(ACT_SIT);
            soldier->set_foe(foe);
            ASSERT_LE(soldier->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
            const std::vector<Box> spots = classic_spots(p);
            for (const Box& b : spots)
                ASSERT_LT(b.x + p.sx, world().grid.w * GRID_SIZE) << "the staging: every classic spot is on the map";
            if (!on_line) {
                for (const Box& b : spots)
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: off every classic spot";
                for (const Box& b : flown_boxes(p))
                    ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: off every flown box";
            }
            const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
            world().rng_.state_ = s;
            Denial why = Denial::NoFoe;
            const bool passes = soldier->fire_check(1, 0, &why);
            EXPECT_EQ(on_line, passes);
            EXPECT_EQ(on_line ? Denial::None : Denial::RayMiss, why);
            EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "the waver, and nothing else";
        }
    }
}

// Gameplay Fixes BD, setting on (D6, a deliberate rule): an enemy weapon in
// flight on the line is met as the shot would meet it. No living on the line;
// one enemy arrow parked in a 32 px collision cell the flight crosses beyond
// the pad, where the first flight box that covers the cell also meets it (its
// place is computed from the arrow's step and asserted). A weapon meeting an
// enemy weapon draws rng(10) for each covered cell holding it and is stopped
// by it only on a roll of 3 or less (obmap.cpp). Stream pinned so that roll
// is lost at the first box: the check passes ("we hit an enemy"), and the
// stream after is the waver and that one roll. Pinned so every roll is won:
// RayMiss, and the stream after is the waver and exactly one rng(10) per
// flight box whose cells hold that arrow, a count the test derives. The foe
// stands in reach, off the flight's line.
TEST_F(SemLineCheck, an_enemy_weapon_on_the_line_is_met_on_its_clash_roll_with_the_setting_on)
{
    constexpr int kCell = 32;  // obmap.cpp OBRES: the collision table's cell
    for (bool lost : {true, false}) {
        SCOPED_TRACE(lost ? "the clash roll lost at the first box" : "every clash roll won");
        fresh_world(1);
        walker* archer = living_at(FAMILY_ARCHER, 0, 512, 640);
        ASSERT_NE(nullptr, archer);
        face(archer, FACE_UP);
        const ShotPlan p = plan_of(archer, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> boxes = flown_boxes(p);
        const auto rows = [&](int y) { return std::pair<int, int>{y / kCell, (y + p.sy) / kCell}; };
        const auto cols = [&](int x) { return std::pair<int, int>{x / kCell, (x + p.sx) / kCell}; };
        // The first flight box that enters a cell row no earlier box (nor the pad) covers, with
        // room in that row for the parked arrow's box at the flight box's own spot.
        int k = -1;
        int seen_row = rows(p.pad_y).first;
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
            const int r = rows(boxes[static_cast<std::size_t>(i)].y).first;
            if (r < seen_row && rows(boxes[static_cast<std::size_t>(i)].y).second == r) {
                k = i;
                break;
            }
            seen_row = std::min(seen_row, r);
        }
        ASSERT_GE(k, 0) << "the staging: a flight box that enters a fresh cell row whole";
        const Box at = boxes[static_cast<std::size_t>(k)];
        walker* parked = world().add_ob(Order::Weapon, FAMILY_ARROW);
        ASSERT_NE(nullptr, parked);
        parked->set_team_num(1);
        parked->set_act_type(ACT_SIT);
        place(parked, static_cast<short>(at.x), static_cast<short>(at.y), p.sx, p.sy);
        ASSERT_EQ(cols(at.x).first, cols(at.x).second) << "the staging: the parked arrow is in one cell";
        ASSERT_EQ(rows(at.y).first, rows(at.y).second);
        const int cell_c = cols(at.x).first;
        const int cell_r = rows(at.y).first;
        ASSERT_TRUE(box_meets(at, p.sx, p.sy, parked)) << "the staging: the first box over its cell meets it";
        ASSERT_FALSE(rows(p.pad_y).first <= cell_r && cell_r <= rows(p.pad_y).second) << "the staging: beyond the pad's cells";
        int covering = 0;
        for (const Box& b : boxes)
            if (cols(b.x).first <= cell_c && cell_c <= cols(b.x).second && rows(b.y).first <= cell_r &&
                cell_r <= rows(b.y).second)
                ++covering;
        ASSERT_GE(covering, 1);
        walker* foe = living_at(FAMILY_ORC, 1, static_cast<short>(512 + 60), 640, 16, 16);
        ASSERT_NE(nullptr, foe);
        foe->set_act_type(ACT_SIT);
        archer->set_foe(foe);
        ASSERT_LE(archer->distance_to_ob(foe), p.reach) << "the staging: inside the reach gate";
        for (const Box& b : boxes)
            ASSERT_FALSE(box_meets(b, p.sx, p.sy, foe)) << "the staging: the foe is off the line";
        std::uint32_t s = 0;
        if (lost)
            s = state_where_then(p.waver_bound, p.waver_zero, 10, 1, [](std::uint32_t r) { return r <= 3; });
        else
            s = state_where_then(p.waver_bound, p.waver_zero, 10, covering, [](std::uint32_t r) { return r > 3; });
        world().rng_.state_ = s;
        Denial why = Denial::NoFoe;
        const bool passes = archer->fire_check(0, -1, &why);
        og::sim::SimRandom expect(s);
        (void)expect.next(p.waver_bound);
        for (int i = 0; i < (lost ? 1 : covering); ++i)
            (void)expect.next(10);
        if (lost) {
            EXPECT_TRUE(passes) << "the probe is stopped by the enemy arrow: the shot would strike it";
            EXPECT_EQ(Denial::None, why);
        } else {
            EXPECT_FALSE(passes) << "every roll won: the probe flies on and finds nobody";
            EXPECT_EQ(Denial::RayMiss, why);
        }
        EXPECT_EQ(expect.state_, world().rng_.state_)
            << "the waver and " << (lost ? 1 : covering) << " clash roll(s), one per flight box over the cell";
    }
}

// Gameplay Fixes BD: the mode ball (an ignored, live, hostile foe that is
// not in the collision table) on the line. An archer facing up and an
// ignored orc as its foe, (a) on the pad (its box meets the pad's), (b) three
// steps out (its box meets the flight's position after its third step and
// none before: asserted). Setting on: passes in both. Setting off: passes
// exactly when a classic spot's box meets it (computed from the classic
// spots), else RayMiss.
TEST_F(SemLineCheck, a_mode_ball_on_the_line_passes_the_flown_check)
{
    for (short fixes : {short{0}, short{1}}) {
        for (bool on_pad : {true, false}) {
            SCOPED_TRACE(std::string(fixes ? "Gameplay Fixes on, " : "Gameplay Fixes off, ") +
                         (on_pad ? "(a) the ball on the pad" : "(b) the ball three steps out"));
            fresh_world(fixes);
            walker* archer = living_at(FAMILY_ARCHER, 0, 512, 640);
            ASSERT_NE(nullptr, archer);
            face(archer, FACE_UP);
            const ShotPlan p = plan_of(archer, FACE_UP);
            ASSERT_GT(p.los, 4);
            // Where the flight stands after each step (its whole-pixel position).
            std::vector<Box> at{{p.pad_x, p.pad_y}};
            float wy = p.pad_y;
            for (int k = 1; k < p.los; ++k) {
                wy += p.ly;
                at.push_back({p.pad_x, static_cast<short>(wy)});
            }
            const short bx = static_cast<short>(p.pad_x + p.sx / 2 - 8);
            const short by = on_pad ? static_cast<short>(p.pad_y - 8) : static_cast<short>(at[2].y - 15);
            walker* ball = living_at(FAMILY_ORC, 1, bx, by, 16, 16);
            ASSERT_NE(nullptr, ball);
            ball->set_act_type(ACT_SIT);
            ball->set_ignore(1);
            world().myobmap->remove(ball);
            ASSERT_TRUE(world().myobmap->walker_to_pos.find(ball) == world().myobmap->walker_to_pos.end())
                << "the staging: the ball is not in the collision table";
            if (on_pad) {
                ASSERT_TRUE(box_meets(at[0], p.sx, p.sy, ball)) << "the staging: the ball is on the pad";
            } else {
                for (int k = 0; k <= 2; ++k)
                    ASSERT_FALSE(box_meets(at[static_cast<std::size_t>(k)], p.sx, p.sy, ball))
                        << "the staging: not met after step " << k;
                ASSERT_TRUE(box_meets(at[3], p.sx, p.sy, ball)) << "the staging: met after the third step";
            }
            archer->set_foe(ball);
            ASSERT_LE(archer->distance_to_ob(ball), p.reach);
            bool classic_meets = false;
            for (const Box& b : classic_spots(p))
                classic_meets = classic_meets || box_meets(b, p.sx, p.sy, ball);
            world().rng_.state_ = state_where({{p.waver_bound, p.waver_zero}});
            Denial why = Denial::NoFoe;
            const bool passes = archer->fire_check(0, -1, &why);
            const bool expected = fixes ? true : classic_meets;
            EXPECT_EQ(expected, passes) << (fixes ? "the flown check meets the ball"
                                                  : (classic_meets ? "a classic spot meets the ball"
                                                                   : "no classic spot meets the ball"));
            EXPECT_EQ(expected ? Denial::None : Denial::RayMiss, why);
        }
    }
}

// walker::act_guard and the FIRE order through fire_check, in a ticking
// world. The geometry of classic_the_check_steps_over_a_foe_between_two_of_its_spots:
// a hold-post tower (a guard that keeps its post asks a FIRE order every act)
// and a soldier that does not act, standing between two classic spots. The
// world ticks; after each tick the weapon list is searched for a live arrow
// whose owner is the tower (found again by its entity id, never by a pointer
// kept across a tick). Setting on: the tower has started a shot within its
// fire delay plus three acts. Setting off: sixty ticks, no shot.
TEST_F(SemLineCheck, a_tower_shoots_a_still_foe_between_two_classic_spots_only_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
        fresh_world(fixes, 64, 160);
        walker* tower = living_at(FAMILY_TOWER1, 0, 496, 2304);
        ASSERT_NE(nullptr, tower);
        face(tower, FACE_UP);
        walker* body = living_at(FAMILY_SOLDIER, 1, 64, 64, 16, 16);
        ASSERT_NE(nullptr, body);
        body->set_act_type(ACT_SIT);
        const ShotPlan p = plan_of(tower, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> spots = classic_spots(p);
        const short body_x = static_cast<short>(p.pad_x + p.sx / 2 - 8);
        int blind = -1;
        for (int gap = 0; blind < 0; ++gap) {
            place(body, body_x, static_cast<short>(tower->ypos() - gap - 16), 16, 16);
            ASSERT_LE(tower->distance_to_ob(body), p.reach) << "the staging: a blind gap inside the reach";
            bool met = false;
            for (const Box& b : spots)
                met = met || box_meets(b, p.sx, p.sy, body);
            if (!met)
                blind = gap;
        }
        tower->set_act_type(ACT_GUARD);
        tower->set_guard_hold_post(true);
        ASSERT_TRUE(tower->guard_hold_post());
        const std::uint32_t tower_id = tower->entity_id();
        const std::uint32_t body_id = body->entity_id();
        const int budget = fixes ? static_cast<int>(std::ceil(tower->fire_frequency())) + 3 : 60;
        const auto tower_shot_alive = [&] {
            const walker* t = world().find_by_id(tower_id);
            if (t == nullptr)
                return false;
            for (const auto& u : world().weaplist)
                if (u && !u->dead() && u->owner() == t && u->family() == FAMILY_ARROW)
                    return true;
            return false;
        };
        int shot_at = -1;
        for (int tick = 1; tick <= budget && shot_at < 0; ++tick) {
            world().tick();
            if (tower_shot_alive())
                shot_at = tick;
        }
        const walker* still = world().find_by_id(body_id);
        ASSERT_NE(nullptr, still) << "the staging: the soldier is still there";
        if (fixes)
            EXPECT_GT(shot_at, 0) << "gap " << blind << ": the tower shoots within " << budget << " ticks";
        else
            EXPECT_EQ(-1, shot_at) << "gap " << blind << ": the classic check never passes (shot on tick " << shot_at << ")";
    }
}

// Gameplay Fixes BD and the arrow slit (GameWorld::slit_open_for,
// game_world.cpp). A slit lets an owned weapon through on rng(dist / 16) == 0,
// dist the weapon's distance from its shooter on the long axis minus 8, plus
// 16 when under 16, measured from where the weapon stands when it asks (the
// classic loop moves its probe onto a spot and then asks there; a flying shot
// asks for the box one step ahead of where it stands); the roll is made for
// each slit tile under each box a weapon tests. An archer facing up, one slit tile on its line, a soldier
// beyond the slit inside the reach and under a flight box and a classic spot's
// box. Three stagings, each slit tile chosen from the arrow's numbers and
// asserted: (a) every flight box over the slit has a bound of 0 or 1; (b) no
// classic spot's box covers the slit, and the first flight box over it has a
// bound of 2 or more; (c) exactly one classic spot's box covers it, with a
// bound of 2 or more. Setting on, all three: the check passes and the stream
// after is the waver alone (no slit roll was drawn). The shot, setting on: in
// (a) it strikes the soldier; in (b), the shot's first slit roll lost, the
// arrow dies at the slit and the soldier keeps its hit points; every slit roll
// won, the soldier is struck. Setting off: (b) passes with no slit draw; (c)
// draws exactly the one roll of the computed bound, refused WallBlocked when
// it is lost, passing when it is won.
TEST_F(SemLineCheck, an_arrow_slit_is_open_to_the_check_and_the_shot_takes_its_gamble)
{
    constexpr short kX = 512;
    constexpr short kY = 640;
    enum class Slit { Near, Unspotted, Spotted };
    for (Slit slit : {Slit::Near, Slit::Unspotted, Slit::Spotted}) {
        const char* name = slit == Slit::Near        ? "(a) a near slit"
                           : slit == Slit::Unspotted ? "(b) a slit no classic spot covers"
                                                     : "(c) a slit one classic spot covers";
        SCOPED_TRACE(name);
        // The numbers, from an archer in a world of its own.
        fresh_world(1);
        walker* a0 = living_at(FAMILY_ARCHER, 0, kX, kY);
        ASSERT_NE(nullptr, a0);
        face(a0, FACE_UP);
        const ShotPlan p = plan_of(a0, FACE_UP);
        ASSERT_GT(p.los, 1);
        const std::vector<Box> flown = flown_boxes(p);
        const std::vector<Box> from = flown_from(p);
        const std::vector<Box> spots = classic_spots(p);
        ASSERT_EQ(p.pad_x / GRID_SIZE, (p.pad_x + p.sx - 1) / GRID_SIZE) << "the staging: the line is one tile wide";
        const int col = p.pad_x / GRID_SIZE;
        const auto covers = [&](const Box& b, int row) {
            return b.y / GRID_SIZE <= row && row <= (b.y + p.sy - 1) / GRID_SIZE;
        };
        const auto bound_at = [&](const Box& b) {
            std::int32_t dist = std::abs(b.y - kY) - GRID_SIZE / 2;
            if (dist < GRID_SIZE)
                dist += GRID_SIZE;
            return static_cast<std::uint32_t>(dist / GRID_SIZE);
        };
        // The soldier: under the first classic spot's box past the pad that is also met by a
        // flight box, inside the reach.
        int body_y = -1;
        for (std::size_t i = 1; i < spots.size() && body_y < 0; ++i) {
            const int y = spots[i].y - 7;
            if (std::abs(kY - y) + std::abs(kX - (p.pad_x + p.sx / 2 - 8)) > p.reach)
                break;
            if (kY - y < 100)
                continue;  // room for the three slits in front of it
            body_y = y;
        }
        ASSERT_GE(body_y, 0) << "the staging: a soldier spot";
        const int body_row_top = body_y / GRID_SIZE;
        const int pad_row_bottom = (p.pad_y + p.sy - 1) / GRID_SIZE;
        int row = -1;
        std::vector<std::uint32_t> flown_bounds;
        std::uint32_t classic_bound = 0;
        for (int r = pad_row_bottom - 1; r > body_row_top + 1 && row < 0; --r) {
            std::vector<std::uint32_t> fb;
            for (std::size_t k = 0; k < flown.size(); ++k)
                if (covers(flown[k], r))
                    fb.push_back(bound_at(from[k]));  // measured from where the shot stands
            int nspots = 0;
            std::uint32_t cb = 0;
            for (const Box& b : spots)
                if (covers(b, r)) {
                    ++nspots;
                    cb = bound_at(b);
                }
            if (fb.empty() || covers(Box{p.pad_x, p.pad_y}, r))
                continue;
            const bool near = std::all_of(fb.begin(), fb.end(), [](std::uint32_t v) { return v <= 1; });
            if ((slit == Slit::Near && near) || (slit == Slit::Unspotted && nspots == 0 && fb.front() >= 2) ||
                (slit == Slit::Spotted && nspots == 1 && cb >= 2)) {
                row = r;
                flown_bounds = fb;
                classic_bound = cb;
            }
        }
        ASSERT_GE(row, 0) << "the staging: a slit row for " << name;
        for (short fixes : {short{1}, short{0}}) {
            SCOPED_TRACE(fixes ? "Gameplay Fixes on" : "Gameplay Fixes off");
            if (!fixes && slit == Slit::Near)
                continue;
            const auto stage = [&](walker*& archer, walker*& body) {
                fresh_world(fixes);
                tile(col, row, PIX_WALL_ARROW_GRASS);
                archer = living_at(FAMILY_ARCHER, 0, kX, kY);
                body = living_at(FAMILY_SOLDIER, 1, static_cast<short>(p.pad_x + p.sx / 2 - 8), static_cast<short>(body_y), 16, 16);
                if (archer == nullptr || body == nullptr)
                    return;
                face(archer, FACE_UP);
                body->set_act_type(ACT_SIT);
                body->stats()->set_armor(0.0f);
                archer->set_foe(body);
            };
            walker* archer = nullptr;
            walker* body = nullptr;
            stage(archer, body);
            ASSERT_TRUE(archer && body);
            ASSERT_LE(archer->distance_to_ob(body), p.reach) << "the staging: inside the reach";
            bool flown_meets = false;
            for (const Box& b : flown)
                flown_meets = flown_meets || box_meets(b, p.sx, p.sy, body);
            ASSERT_TRUE(flown_meets) << "the staging: under a flight box";
            if (fixes) {
                const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
                world().rng_.state_ = s;
                Denial why = Denial::NoFoe;
                EXPECT_TRUE(archer->fire_check(0, -1, &why)) << "the check takes the slit as open";
                EXPECT_EQ(Denial::None, why);
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "no slit roll drawn by the check";
                drop_weapons();
                if (slit == Slit::Near) {
                    world().rng_.state_ = s;
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "every slit roll has a bound of 0 or 1: struck";
                } else if (slit == Slit::Unspotted) {
                    world().rng_.state_ =
                        state_where_then(p.waver_bound, p.waver_zero, flown_bounds.front(), 1,
                                         [](std::uint32_t r) { return r != 0; });
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_FLOAT_EQ(400.0f, body->stats()->hitpoints()) << "the first slit roll lost: the arrow breaks";
                    drop_weapons();
                    stage(archer, body);
                    ASSERT_TRUE(archer && body);
                    std::uint32_t won = 0;
                    for (std::uint32_t seed = 1; won == 0; ++seed) {
                        og::sim::SimRandom probe(seed);
                        bool all = probe.next(p.waver_bound) == p.waver_zero;
                        for (std::uint32_t b : flown_bounds)
                            all = all && probe.next(b) == 0;
                        if (all)
                            won = seed;
                    }
                    world().rng_.state_ = won;
                    ASSERT_TRUE(fire_and_fly(archer, 4 * p.los));
                    EXPECT_LT(body->stats()->hitpoints(), 400.0f) << "every slit roll won: struck";
                }
            } else if (slit == Slit::Unspotted) {
                const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}});
                world().rng_.state_ = s;
                Denial why = Denial::NoFoe;
                EXPECT_TRUE(archer->fire_check(0, -1, &why)) << "no classic spot is on the slit";
                EXPECT_EQ(Denial::None, why);
                EXPECT_EQ(state_after(s, {p.waver_bound}), world().rng_.state_) << "no slit roll drawn";
            } else {
                ASSERT_GE(classic_bound, 2u);
                for (bool lost : {true, false}) {
                    SCOPED_TRACE(lost ? "the classic spot's roll lost" : "the classic spot's roll won");
                    if (!lost)
                        stage(archer, body);
                    ASSERT_TRUE(archer && body);
                    const std::uint32_t s = state_where({{p.waver_bound, p.waver_zero}, {classic_bound, lost ? kNonZero : 0u}});
                    world().rng_.state_ = s;
                    Denial why = Denial::NoFoe;
                    const bool passes = archer->fire_check(0, -1, &why);
                    EXPECT_EQ(!lost, passes);
                    EXPECT_EQ(lost ? Denial::WallBlocked : Denial::None, why);
                    EXPECT_EQ(state_after(s, {p.waver_bound, classic_bound}), world().rng_.state_)
                        << "exactly one slit roll, of bound " << classic_bound;
                }
            }
        }
    }
}
