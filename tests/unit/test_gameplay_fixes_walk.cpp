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

// How many LCG steps lead from `from` to `to` (-1: none within 1000).
int lcg_steps(std::uint32_t from, std::uint32_t to)
{
    og::sim::SimRandom probe(from);
    for (int n = 0; n < 1000; ++n) {
        if (probe.state_ == to)
            return n;
        (void)probe.next(2);
    }
    return -1;
}

// The gameplay stream (combat_rng: walker::attack's damage roll) counted:
// answers 0 like the fixture's FixedRandom and counts every call.
class CountingRandom : public IRandom
{
public:
    std::uint32_t next(std::uint32_t) override
    {
        ++calls;
        return 0;
    }
    int calls = 0;
};

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
// Fix Y: the right-hand walk's probes
// ---------------------------------------------------------------------------

namespace {

enum class SideCase { Wall, Foe, Ally };

const char* side_case_name(SideCase c)
{
    switch (c) {
    case SideCase::Wall: return "a wall up-right";
    case SideCase::Foe: return "a foe up-right";
    case SideCase::Ally: return "an ally up-right";
    }
    return "?";
}

}  // namespace

// The reporter's scenario (#405). A team-1 soldier at (320,320) heading up,
// 10 MP; a magic potion at (335,322), one pixel outside its right side, so
// only right_blocked's probe (the box moved one pixel right) meets it, never
// the soldier's own box or its steps. Up-right, where right_forward_blocked
// probes, is (case A) a wall tile, (B) a team-2 orc or (C) a team-1 soldier;
// so the walk takes its first branch on both values and steps forward.
// Off: the side probe eats the potion (MP 10 -> 550) and in case B the
// up-right probe's collide() starts a swing at the orc (busy 6, the attack
// row). On: the potion stands, MP 10, no swing, nothing in collide_ob.
//
// RED at bb5e13d7 on the setting-on arm. Planted break on the fixed tree:
// right_walk's `const bool quiet = og::sim::session_gameplay_fixes() != 0;`
// -> `= false;`.
TEST_F(GameplayFixesWalk, a_right_walk_side_probe_eats_nothing_and_swings_at_nobody)
{
    for (SideCase c : {SideCase::Wall, SideCase::Foe, SideCase::Ally}) {
        for (short fixes : {short{0}, short{1}}) {
            SCOPED_TRACE(std::string(side_case_name(c)) + ", setting " + std::to_string(fixes));
            fresh_world(fixes);
            walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
            walker* potion = treasure_at(FAMILY_MAGIC_POTION, 335, 322, 8, 8);
            walker* body = nullptr;
            if (c == SideCase::Wall)
                tile(21, 19, PIX_H_WALL1);
            else
                body = c == SideCase::Foe ? living_at(FAMILY_ORC, 2, 335, 305)
                                          : living_at(FAMILY_SOLDIER, 1, 335, 305);
            ASSERT_TRUE(w && potion);
            ASSERT_TRUE(c == SideCase::Wall || body != nullptr);
            face(w, FACE_UP);
            w->set_lastx(0.0f);
            w->set_lasty(-w->stepsize());
            w->stats()->set_magicpoints(10.0f);
            const char ani0 = w->ani_type();
            const std::uint32_t s = world().rng_.state_;

            EXPECT_TRUE(w->stats()->right_walk());

            EXPECT_EQ(320, w->xpos()) << "the walk stepped forward";
            EXPECT_EQ(static_cast<short>(320 - w->stepsize()), w->ypos());
            EXPECT_EQ(s, world().rng_.state_) << "no probe and no step draws here";
            if (fixes == 0) {
                EXPECT_TRUE(potion->dead()) << "setting off: the side probe ate the potion";
                EXPECT_FLOAT_EQ(550.0f, w->stats()->magicpoints());
                if (c == SideCase::Foe) {
                    EXPECT_FLOAT_EQ(6.0f, w->busy()) << "setting off: a bump swing at the orc";
                    EXPECT_EQ(ANI_ATTACK, w->ani_type());
                } else {
                    EXPECT_FLOAT_EQ(0.0f, w->busy());
                    EXPECT_EQ(ani0, w->ani_type());
                }
            } else {
                EXPECT_FALSE(potion->dead()) << "setting on: the potion stands";
                EXPECT_FLOAT_EQ(10.0f, w->stats()->magicpoints());
                EXPECT_FLOAT_EQ(0.0f, w->busy()) << "setting on: no swing";
                EXPECT_EQ(ani0, w->ani_type());
                EXPECT_EQ(nullptr, w->collide_ob());
            }
        }
    }
}

// Case B carried on: the swing the classic side probe started ends in a
// throw. Six animate() calls (the commands and foe cleared each time, so
// nothing but the swing can fire): off, one knife flies (weaplist + 1),
// the soldier pays 2 MP and fire() draws its waver once; on, nothing.
TEST_F(GameplayFixesWalk, a_side_probe_does_not_swing_at_a_foe_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* potion = treasure_at(FAMILY_MAGIC_POTION, 335, 322, 8, 8);
        walker* orc = living_at(FAMILY_ORC, 2, 335, 305);
        ASSERT_TRUE(w && potion && orc);
        face(w, FACE_UP);
        w->set_lastx(0.0f);
        w->set_lasty(-w->stepsize());
        w->stats()->set_magicpoints(10.0f);
        EXPECT_TRUE(w->stats()->right_walk());
        const float mp = w->stats()->magicpoints();
        const std::size_t weapons = world().weaplist.size();
        const std::uint32_t s = world().rng_.state_;

        for (int t = 0; t < 6; ++t) {
            w->stats()->clear_command();
            w->set_foe(nullptr);
            (void)w->animate();
        }

        if (fixes == 0) {
            EXPECT_EQ(weapons + 1, world().weaplist.size()) << "setting off: the swing threw a knife";
            EXPECT_FLOAT_EQ(mp - 2.0f, w->stats()->magicpoints());
            EXPECT_EQ(1, lcg_steps(s, world().rng_.state_)) << "setting off: the throw's one draw";
        } else {
            EXPECT_EQ(weapons, world().weaplist.size()) << "setting on: nothing thrown";
            EXPECT_FLOAT_EQ(10.0f, w->stats()->magicpoints());
            EXPECT_EQ(s, world().rng_.state_) << "setting on: nothing drawn";
        }
        EXPECT_FLOAT_EQ(400.0f, orc->stats()->hitpoints()) << "the classic knife flies up, past the orc";
    }
}

// ---------------------------------------------------------------------------
// Fix Z: the rush
// ---------------------------------------------------------------------------

// The reporter's scenario (#406). A seated team-1 soldier (no fallback
// steps) rushes right into an allied orc 20 px ahead with its own search
// queued. The rush's walksteps run into the orc (the walk's shoves: on a
// state whose three rolls come up 0, none steals its queue), and then the
// rush strikes collide_ob. Off: walker::attack draws its damage roll and
// refuses a friend (hp unchanged), and the rush still erases the orc's
// orders and force-walks it four steps right. On: a friendly collide_ob is
// not struck: no damage roll, the orc keeps its search.
//
// RED at bb5e13d7 on the setting-on arm. Planted break on the fixed tree:
// `!controller_->is_friendly(controller_->collide_ob())` -> `true`.
TEST_F(GameplayFixesWalk, a_rush_into_an_ally_erases_no_orders_with_the_setting_on)
{
    const std::uint32_t s = state_where({{3, 0}, {3, 0}, {3, 0}});
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* ally = living_at(FAMILY_ORC, 1, 340, 320);
        ASSERT_TRUE(soldier && ally);
        soldier->set_user(0);
        soldier->set_stepsize(4.0f);
        face(soldier, FACE_RIGHT);
        ally->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        soldier->stats()->add_command(COMMAND_RUSH, 3, 1, 0);
        soldier->set_collide_ob(nullptr);
        CountingRandom combat;
        tw_->gc.rng = &combat;
        world().rng_.state_ = s;

        (void)soldier->stats()->do_command();

        tw_->gc.rng = &tw_->rng;
        EXPECT_EQ(state_after(s, {3, 3, 3}), world().rng_.state_)
            << "the walk's three shoves, on both values";
        EXPECT_FLOAT_EQ(400.0f, ally->stats()->hitpoints()) << "walker::attack never hurts a friend";
        const Front f = front_of(ally);
        ASSERT_EQ(1u, f.size);
        if (fixes == 0) {
            EXPECT_EQ(1, combat.calls) << "setting off: attack's damage roll";
            EXPECT_EQ(COMMAND_WALK, f.type) << "setting off: the rush erased the ally's orders";
            EXPECT_EQ(4, f.count);
            EXPECT_EQ(1, f.com1);
            EXPECT_EQ(0, f.com2);
        } else {
            EXPECT_EQ(0, combat.calls) << "setting on: no strike, no damage roll";
            EXPECT_EQ(COMMAND_SEARCH, f.type) << "setting on: the ally keeps its own order";
            EXPECT_EQ(10, f.count);
        }
    }
}

// The control: a foe in the same spot is struck on both values (hp 400 ->
// 382 on the fixture's fixed damage roll) and force-walked.
TEST_F(GameplayFixesWalk, a_rush_into_a_foe_still_strikes_it)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* foe = living_at(FAMILY_ORC, 2, 340, 320);
        ASSERT_TRUE(soldier && foe);
        soldier->set_user(0);
        soldier->set_stepsize(4.0f);
        face(soldier, FACE_RIGHT);
        soldier->stats()->add_command(COMMAND_RUSH, 3, 1, 0);
        soldier->set_collide_ob(nullptr);

        (void)soldier->stats()->do_command();

        EXPECT_FLOAT_EQ(382.0f, foe->stats()->hitpoints()) << "struck on both values";
        const Front f = front_of(foe);
        ASSERT_GE(f.size, 1u);
        EXPECT_EQ(COMMAND_WALK, f.type) << "and force-walked";
        EXPECT_EQ(4, f.count);
        EXPECT_EQ(1, f.com1);
        EXPECT_EQ(0, f.com2);
    }
}

// A stale collide_ob: a team-2 orc 40 px ahead names collide_ob from an
// earlier act and the rush's three steps (12 px) never reach it. Off: the
// rush strikes it from where it stands (hp 400 -> 382) and force-walks it.
// On: no step box meets it: no strike. Then the same orc 4 px ahead of a
// bot in a walled corridor: the steps run into it, the fallback up is
// stopped by the wall, and the last step's box meets it: struck on both.
//
// RED at bb5e13d7 on the setting-on arm of the first half. Planted break on
// the fixed tree: the rush's `og::sim::step_meets(...)` term -> `true`.
TEST_F(GameplayFixesWalk, a_wall_stopped_rush_strikes_no_stale_body)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("stale, setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* foe = living_at(FAMILY_ORC, 2, 376, 320);
        ASSERT_TRUE(soldier && foe);
        soldier->set_stepsize(4.0f);
        face(soldier, FACE_RIGHT);
        foe->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        soldier->stats()->add_command(COMMAND_RUSH, 3, 1, 0);
        soldier->set_collide_ob(foe);

        (void)soldier->stats()->do_command();

        EXPECT_EQ(332, soldier->xpos()) << "three clean steps: none scanned the foe's cell";
        const Front f = front_of(foe);
        ASSERT_GE(f.size, 1u);
        if (fixes == 0) {
            EXPECT_FLOAT_EQ(382.0f, foe->stats()->hitpoints()) << "setting off: struck from 40 px";
            EXPECT_EQ(COMMAND_WALK, f.type);
            EXPECT_EQ(4, f.count);
        } else {
            EXPECT_FLOAT_EQ(400.0f, foe->stats()->hitpoints()) << "setting on: no step met it";
            EXPECT_EQ(COMMAND_SEARCH, f.type);
            EXPECT_EQ(10, f.count);
        }
    }
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("met, setting " + std::to_string(fixes));
        fresh_world(fixes);
        for (int tx = 16; tx <= 26; ++tx) {
            tile(tx, 19, PIX_H_WALL1);
            tile(tx, 21, PIX_H_WALL1);
        }
        walker* soldier = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* foe = living_at(FAMILY_ORC, 2, 340, 320);
        ASSERT_TRUE(soldier && foe);
        soldier->set_stepsize(4.0f);
        face(soldier, FACE_RIGHT);
        ASSERT_EQ(-1, soldier->user());
        soldier->stats()->add_command(COMMAND_RUSH, 3, 1, 0);
        soldier->set_collide_ob(nullptr);

        (void)soldier->stats()->do_command();

        EXPECT_FLOAT_EQ(382.0f, foe->stats()->hitpoints()) << "the last step ran into it: struck on both";
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

// The quiet right-hand probes answer what the loud ones answer. A soldier at
// (320,320) faces each of the eight headings in turn, with nothing beside it,
// with one wall tile on each of its eight neighbouring tiles, and with an
// enemy orc one pixel into each of its eight sides and corners. Each of the
// three probes is asked on its own fresh world (a loud probe's bump swing
// can turn the soldier, which would change what the next probe asks):
// quietly first (the quiet call changes nothing), then loudly on the same
// world. Two anchors pin what an answer is: open ground is clear, and a
// wall right of a soldier heading up blocks its right.
TEST_F(GameplayFixesWalk, the_right_probes_answer_the_same_on_both_values)
{
    const int dirs[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};
    int asked = 0;
    for (int facing = 0; facing < 8; ++facing) {
        for (int blocker = -1; blocker < 16; ++blocker) {
            for (int probe = 0; probe < 3; ++probe) {
                fresh_world(0);
                walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
                ASSERT_NE(nullptr, w);
                face(w, facing);
                if (blocker >= 0 && blocker < 8) {
                    tile(20 + dirs[blocker][0], 20 + dirs[blocker][1], PIX_H_WALL1);
                } else if (blocker >= 8) {
                    const int d = blocker - 8;
                    ASSERT_NE(nullptr, living_at(FAMILY_ORC, 2, static_cast<short>(320 + 15 * dirs[d][0]),
                                                 static_cast<short>(320 + 15 * dirs[d][1])));
                }
                statistics* st = w->stats();
                const bool quiet = probe == 0 ? st->right_blocked(true)
                                 : probe == 1 ? st->right_forward_blocked(true)
                                              : st->right_back_blocked(true);
                const bool loud = probe == 0 ? st->right_blocked(false)
                                : probe == 1 ? st->right_forward_blocked(false)
                                             : st->right_back_blocked(false);
                EXPECT_EQ(quiet, loud) << "facing " << facing << " blocker " << blocker << " probe " << probe;
                if (blocker == -1) {
                    EXPECT_FALSE(quiet) << "open ground is clear, facing " << facing << " probe " << probe;
                }
                if (facing == FACE_UP && blocker == 2 && probe == 0) {
                    EXPECT_TRUE(quiet) << "a wall right of a soldier heading up blocks its right";
                }
                ++asked;
            }
        }
    }
    EXPECT_EQ(8 * 17 * 3, asked);
}

// COMMAND_GOTO's right-hand walk reads the same flag (one rule): the
// reporter's case A asked through right_walk_to_point. Off, the side probe
// eats the potion; on, it stands.
TEST_F(GameplayFixesWalk, right_walk_to_point_probes_quietly)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        walker* w = living_at(FAMILY_SOLDIER, 1, 320, 320);
        walker* potion = treasure_at(FAMILY_MAGIC_POTION, 335, 322, 8, 8);
        ASSERT_TRUE(w && potion);
        tile(21, 19, PIX_H_WALL1);
        face(w, FACE_UP);
        w->set_lastx(0.0f);
        w->set_lasty(-w->stepsize());
        w->stats()->set_magicpoints(10.0f);

        EXPECT_TRUE(w->stats()->right_walk_to_point(320, 100));

        EXPECT_EQ(static_cast<short>(320 - w->stepsize()), w->ypos()) << "the walk stepped forward";
        EXPECT_EQ(fixes == 0, potion->dead() != 0);
        EXPECT_FLOAT_EQ(fixes == 0 ? 550.0f : 10.0f, w->stats()->magicpoints());
    }
}
