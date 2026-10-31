/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Gameplay Fixes X, Y and Z: the walk, its side probes and the rush.
//
// X (#403): living::walk shoves collide_ob when its step fails. That field
//    names the last body a loud probe met, and it can be stale: a probe that
//    eats a teleporter moves the walker before it meets the body it left
//    behind, and a step the grid stops never reaches the object half of the
//    probe, so the last body touched is shoved again from wherever the
//    walker now stands. With the setting on the walk shoves only a body its
//    own step's boxes meet (og::sim::step_meets, the obmap's collide() rule).
// Y (#405): stats::right_walk asks four probes one pixel to the walker's
//    side, front and back. Loud, they ate a potion the bot never stood on
//    and swung at a body they grazed. With the setting on they ask quietly.
// Z (#406): COMMAND_RUSH strikes collide_ob after its three walksteps,
//    whatever its team and wherever it stands. With the setting on the rush
//    strikes only a body that is not friendly and that its last step ran
//    into.
//
// Each case runs on both values of the setting and pins the sim stream
// (GameWorld::rng_.state_) exactly: the classic draws with it off, the
// fixed ones with it on.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

namespace {

// A draw and the value a pinned state must give (kAny: any value).
constexpr std::uint32_t kAny = 0xFFFFFFFFu;
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

class GameplayFixesWalk : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        fresh_world(0);
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it.
    void fresh_world(short fixes)
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

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Sizes first, then the position: the obmap registers a body in every
    // cell its box covers at the moment it is placed.
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

    // A level-4 bot (ACT_RANDOM, no seat), 400 hp, 500 MP, no regeneration.
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
        w->set_busy(0.0f);
        return place(w, x, y, 16, 16);
    }

    walker* treasure_at(int family, short x, short y, short sx, short sy)
    {
        return place(world().add_fx_ob(Order::Treasure, family), x, y, sx, sy);
    }

    static void face(walker* w, int dir)
    {
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
    }
};

// The front of a walker's command queue.
struct Front
{
    std::size_t size = 0;
    int type = -1;
    int count = -1;
    int com1 = 0;
    int com2 = 0;
};

Front front_of(const walker* w)
{
    Front f;
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

}  // namespace

// ---------------------------------------------------------------------------
// Fix X: the walk's shove
// ---------------------------------------------------------------------------

// The reporter's scenario (#403). A team-1 skeleton stands on teleporter A
// at (100,100), an allied cleric beside it at (117,100) with its own search
// queued; teleporter B is at (592,592) in a one-cell pocket walled above,
// below and to both sides. One NPC walkstep right: the full step's first
// probe eats A, which centres the skeleton on B, and the same probe's pile
// loop then meets the cleric at the OLD coordinates, so collide_ob names
// it. The step fails and the baby step and the perpendicular fallback (up)
// are stopped by the pocket's walls in the grid half, so collide_ob still
// names the cleric 967 px away. Off: three stale shoves (three rng(3)), the
// last of them (the fallback's, up) steals the cleric's queue. On: no step
// box meets the cleric from B, so nothing is drawn and its search stands.
//
// RED at bb5e13d7 on the setting-on arm (no fix: the three draws and the
// stolen queue). Planted break on the fixed tree: living::walk's
// `og::sim::step_meets(*this, x, y, *collide_ob())` -> `true`.
TEST_F(GameplayFixesWalk, a_walk_that_eats_a_teleporter_does_not_shove_the_ally_it_left)
{
    const std::uint32_t s = state_where({{3, 1}, {3, 1}, {3, 1}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* pad_a = treasure_at(FAMILY_TELEPORTER, 100, 100, 16, 16);
        walker* pad_b = treasure_at(FAMILY_TELEPORTER, 592, 592, 16, 16);
        tile(36, 37, PIX_H_WALL1);
        tile(38, 37, PIX_H_WALL1);
        tile(37, 36, PIX_H_WALL1);
        tile(37, 38, PIX_H_WALL1);
        walker* cleric = living_at(FAMILY_CLERIC, 1, 117, 100);
        walker* skel = living_at(FAMILY_SKELETON, 1, 100, 100);
        ASSERT_TRUE(pad_a && pad_b && cleric && skel);
        cleric->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        face(skel, FACE_RIGHT);
        ASSERT_EQ(-1, skel->user()) << "an NPC: walkstep takes the fallback step";
        world().rng_.state_ = s;

        (void)skel->walkstep(1.0f, 0.0f);

        EXPECT_EQ(592, skel->xpos()) << "the first probe ate pad A and centred the skeleton on B";
        EXPECT_EQ(592, skel->ypos());
        EXPECT_EQ(cleric, skel->collide_ob()) << "collide_ob itself is left as the probe wrote it";
        EXPECT_EQ(967, skel->distance_to_ob(cleric)) << "the body collide_ob names is far behind";
        const Front f = front_of(cleric);
        ASSERT_EQ(1u, f.size);
        if (fixes == 0) {
            EXPECT_EQ(state_after(s, {3, 3, 3}), world().rng_.state_)
                << "setting off: the full step, the baby step and the fallback each shove the cleric";
            EXPECT_EQ(COMMAND_WALK, f.type) << "setting off: the cleric's search is stolen";
            EXPECT_EQ(4, f.count);
            EXPECT_EQ(0, f.com1) << "the last shove was the fallback's, up";
            EXPECT_EQ(-1, f.com2);
        } else {
            EXPECT_EQ(s, world().rng_.state_) << "setting on: no stale shove, nothing drawn";
            EXPECT_EQ(COMMAND_SEARCH, f.type) << "setting on: the cleric keeps its own order";
            EXPECT_EQ(10, f.count);
        }
    }
}

// The near shape (#403's second kind). A team-1 orc in a corridor walled
// above and below, an allied orc ahead whose box the orc's overlaps by one
// pixel (collide() needs two, so they stand apart). One NPC walkstep right:
// the full step and the baby step both run into the ally (two real shoves),
// then the perpendicular fallback (up) is stopped by the wall in the grid
// half and leaves collide_ob naming the ally. Off: a third shove of the ally
// for a step that never touched it (its own grid probe up refuses, so the
// queue the baby step's shove set stands). On: two draws, not three.
//
// RED at bb5e13d7 on the setting-on arm (three draws).
TEST_F(GameplayFixesWalk, a_grid_stopped_fallback_does_not_shove_the_ally_the_full_step_met)
{
    const std::uint32_t s = state_where({{3, 1}, {3, 1}, {3, 1}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        for (int tx = 16; tx <= 26; ++tx) {
            tile(tx, 19, PIX_H_WALL1);
            tile(tx, 21, PIX_H_WALL1);
        }
        walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
        walker* ally = living_at(FAMILY_ORC, 1, 335, 320);
        ASSERT_TRUE(orc && ally);
        ally->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        face(orc, FACE_RIGHT);
        ASSERT_GE(orc->stepsize(), 1.0f) << "the full step meets the ally";
        world().rng_.state_ = s;

        (void)orc->walkstep(1.0f, 0.0f);

        EXPECT_EQ(320, orc->xpos()) << "every step failed";
        EXPECT_EQ(320, orc->ypos());
        const Front f = front_of(ally);
        ASSERT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_WALK, f.type) << "the real shoves stole the ally's search on both values";
        EXPECT_EQ(4, f.count);
        EXPECT_EQ(1, f.com1) << "the last stealing shove was the baby step's, right";
        EXPECT_EQ(0, f.com2);
        if (fixes == 0)
            EXPECT_EQ(state_after(s, {3, 3, 3}), world().rng_.state_)
                << "setting off: the fallback shoves the ally its step never met";
        else
            EXPECT_EQ(state_after(s, {3, 3}), world().rng_.state_)
                << "setting on: only the two steps that ran into the ally shove it";
    }
}

// The control: on open ground the fallback step up succeeds, so only the
// two steps that met the ally shove it, the same on both values. Fix X
// does not take a shove away from a step that ran into its body.
TEST_F(GameplayFixesWalk, a_body_the_step_meets_is_still_shoved)
{
    const std::uint32_t s = state_where({{3, 1}, {3, 1}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* orc = living_at(FAMILY_ORC, 1, 320, 320);
        walker* ally = living_at(FAMILY_ORC, 1, 335, 320);
        ASSERT_TRUE(orc && ally);
        ally->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        face(orc, FACE_RIGHT);
        world().rng_.state_ = s;

        (void)orc->walkstep(1.0f, 0.0f);

        EXPECT_EQ(state_after(s, {3, 3}), world().rng_.state_)
            << "the full step and the baby step shove the ally on both values";
        EXPECT_EQ(320, orc->xpos());
        EXPECT_EQ(static_cast<short>(320 - orc->stepsize()), orc->ypos()) << "the fallback stepped up";
        const Front f = front_of(ally);
        ASSERT_EQ(1u, f.size);
        EXPECT_EQ(COMMAND_WALK, f.type);
        EXPECT_EQ(4, f.count);
        EXPECT_EQ(1, f.com1);
        EXPECT_EQ(0, f.com2);
    }
}

// ---------------------------------------------------------------------------
// The seams the fixes share
// ---------------------------------------------------------------------------

// og::sim::step_meets agrees with collide() on the box the probe tests.
// A 16x16 mover at (320,320) steps 3 px along each of the eight headings
// toward a detached 16x16 body placed at every position of a 49x49 square
// around it: with a whole-pixel step the exact, ceil and floor boxes are one
// box, so step_meets answers what collide() answers on it, everywhere. Along
// the right heading the boxes meet on exactly a 29x29 block (collide()
// needs an overlap of two pixels on each axis: 16 + 16 - 3). A fractional
// step (2.5 px) is tested on the box living::walk's ceil probe tests: a body
// at x = 337 is met by the ceil box (323) and not by the truncated exact one
// (322). It asks nothing else (no z skip, no separating-move pass), draws
// nothing and writes nothing.
TEST_F(GameplayFixesWalk, step_meets_answers_what_collide_answers_on_the_three_boxes)
{
    walker* mover = living_at(FAMILY_ORC, 1, 320, 320);
    ASSERT_NE(nullptr, mover);
    walker body(walker::WeaponProfileProbeTag{});
    body.set_sizex(16);
    body.set_sizey(16);
    const std::size_t obmap = world().myobmap->size();
    const std::uint32_t s = world().rng_.state_;

    const int headings[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    for (const auto& h : headings) {
        const float dx = 3.0f * static_cast<float>(h[0]);
        const float dy = 3.0f * static_cast<float>(h[1]);
        int met = 0;
        for (short bx = 296; bx <= 344; ++bx) {
            for (short by = 296; by <= 344; ++by) {
                body.set_xpos(bx);
                body.set_ypos(by);
                const bool want = collide(static_cast<short>(320 + h[0] * 3), static_cast<short>(320 + h[1] * 3),
                                          16, 16, bx, by, 16, 16) != 0;
                const bool got = og::sim::step_meets(*mover, dx, dy, body);
                if (want != got) {
                    ADD_FAILURE() << "heading " << h[0] << "," << h[1] << " body at " << bx << "," << by
                                  << ": collide " << want << ", step_meets " << got;
                }
                met += got ? 1 : 0;
            }
        }
        if (h[0] == 1 && h[1] == 0) {
            EXPECT_EQ(29 * 29, met) << "right: x 309..337 by y 306..334";
        }
    }

    body.set_xpos(337);
    body.set_ypos(320);
    EXPECT_TRUE(og::sim::step_meets(*mover, 3.0f, 0.0f, body)) << "a whole step of 3 meets a body at 337";
    EXPECT_FALSE(collide(static_cast<short>(320 + 2.5f), 320, 16, 16, 337, 320, 16, 16))
        << "the truncated exact box of a 2.5 px step (322) does not";
    EXPECT_TRUE(og::sim::step_meets(*mover, 2.5f, 0.0f, body)) << "its ceil box (323) does";
    body.set_xpos(338);
    EXPECT_FALSE(og::sim::step_meets(*mover, 2.5f, 0.0f, body)) << "a pixel further, no box meets";
    body.set_xpos(332);
    EXPECT_TRUE(og::sim::step_meets(*mover, -2.5f, 0.0f, body))
        << "stepping left, the ceil box (318) still meets a body at 332 behind";
    EXPECT_FALSE(collide(static_cast<short>(320 - 2.5f), 320, 16, 16, 332, 320, 16, 16))
        << "the exact box (317) does not";

    EXPECT_EQ(s, world().rng_.state_) << "nothing drawn";
    EXPECT_EQ(obmap, world().myobmap->size()) << "nothing filed";
    EXPECT_EQ(nullptr, mover->collide_ob()) << "nothing written";
    EXPECT_EQ(320, mover->xpos());
}
