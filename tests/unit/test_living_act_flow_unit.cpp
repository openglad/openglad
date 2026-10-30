/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// A bot's act when its special roll casts nothing (Gameplay Fixes W2).
//
// One act in five a bot with the magic for its first slot rolls a special
// (living::act, ACT_RANDOM: the coin, the slot roll, then
// living::check_special's Shift coin and the family's gate). In the classic
// game that roll WAS the act, whatever came of it: a gate that said no, or a
// cast that refused, left the bot standing for the tick. With the setting on
// a roll that cast nothing is not the act: the bot takes the act the coin
// would have given it (a second coin, then act_random() or the four-in-five
// branch), so it walks on. A cast that ran is still the act on both values.
// The hit response is another path: there is no act there to fall into, and
// it draws nothing more than it did.
//
// Each case pins the world's RNG state (the sim stream, GameWorld::rng_:
// living::act draws from it inline) to a state whose draws are known, and
// asserts the exact state after the act: the classic draws with the setting
// off, those plus the ordinary act's with it on.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace {

// A draw the act makes: next(bound), and the value a pinned state must give
// (or kAny when the case does not care).
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

class LivingActFlow : public ::testing::Test {
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
    }

    GameWorld& world() { return tw_->world(); }

    // A bot (ACT_RANDOM, no seat) with `mp` mana and no regeneration.
    living* bot(int family, unsigned char team, short x, short y, int level, float mp)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_sizex(16);
        w->set_sizey(16);
        w->setxy(x, y);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(static_cast<short>(level));
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(std::max(mp, 1.0f));
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->set_busy(0.0f);
        return dynamic_cast<living*>(w);
    }

    // The bound of the bot's slot roll, as living::act computes it.
    std::uint32_t slot_bound(const living* w)
    {
        return static_cast<std::uint32_t>(og::sim::bot_slot_roll_bound(
            get_family_descriptor(w->family()), w->stats()->level(),
            world().new_specials, world().gameplay_fixes));
    }

    // Replaces the soldier's gate and cast with `gate` and `cast` (Lua
    // bodies) in this world; the stub before it goes first. The pack-store
    // guard puts the shipped scripts back after the test.
    void stub_soldier(const std::string& gate, const std::string& cast)
    {
        og::script::unregister_pack_scripts("test.act_flow");
        og::script::register_pack_script(
            {"test.act_flow", "act_flow.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  check_special_ai = function(self) " + gate + " end,\n"
             "  do_special = function(self) " + cast + " end,\n"
             "})\n"});
    }
};

// What one act left behind.
struct Act
{
    bool returned = false;
    std::uint32_t rng = 0;
    std::size_t commands = 0;
    int command = -1;
    int count = -1;
    float mp = 0.0f;
};

Act act_of(GameWorld& world, living* w, std::uint32_t state)
{
    world.rng_.state_ = state;
    Act a;
    a.returned = w->act();
    a.rng = world.rng_.state_;
    a.commands = w->stats()->commands.size();
    if (!w->stats()->commands.empty()) {
        a.command = w->stats()->commands.front().commandtype;
        a.count = w->stats()->commands.front().commandcount;
    }
    a.mp = w->stats()->magicpoints();
    return a;
}

}  // namespace

// A lone level-1 soldier bot with 100 magic: no foe in its window, so the
// gate says no to every slot. The coin comes up (next(5) == 0), the slot
// roll and the Shift coin are drawn, the gate refuses. With the setting off
// that is the act: nothing queued, nothing more drawn, and act() answers 0.
// With it on the bot draws the second coin (not 0: the four-in-five branch),
// finds no foe, and the next(2) of 1 sends it on a random walk of 20 steps
// (two rng(3)).
//
// RED (the setting's return kept on both values: `if (true)`): Expected
// equality of these values: state_after(s, {5, bound, 2, 5, 2, 3, 3}) ...
// "setting on: the second coin and the walk"; (the return dropped) the
// setting-off act draws the second coin too.
TEST_F(LivingActFlow, a_roll_the_gate_refuses_takes_the_ordinary_act_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        living* soldier = bot(FAMILY_SOLDIER, 1, 120, 120, 1, 100.0f);
        ASSERT_NE(nullptr, soldier);
        const std::uint32_t bound = slot_bound(soldier);
        ASSERT_EQ(1u, bound) << "a level-1 soldier rolls one slot";
        const std::uint32_t s =
            state_where({{5, 0}, {bound, kAny}, {2, kAny}, {5, 1}, {2, 1}});
        const Act a = act_of(world(), soldier, s);
        EXPECT_FLOAT_EQ(100.0f, a.mp) << "nothing was cast";
        if (fixes == 0) {
            EXPECT_FALSE(a.returned);
            EXPECT_EQ(state_after(s, {5, bound, 2}), a.rng)
                << "setting off: the coin, the slot roll and the Shift coin, then nothing";
            EXPECT_EQ(0u, a.commands) << "setting off: the bot stands";
        } else {
            EXPECT_TRUE(a.returned);
            EXPECT_EQ(state_after(s, {5, bound, 2, 5, 2, 3, 3}), a.rng)
                << "setting on: the second coin and the walk";
            ASSERT_EQ(1u, a.commands) << "setting on: the walk is queued";
            EXPECT_EQ(COMMAND_WALK, a.command);
            EXPECT_EQ(20, a.count);
        }
    }
}

// The gate says yes and the cast refuses (a stub gate and cast in Lua, both
// draw-free, so the draws are the act's own): the same as a gate's no. The
// classic bot stands; with the setting on it walks on. A cast that runs is
// the act on both values: act() answers 1 and nothing more is drawn.
//
// RED (the setting's return kept on both values: `if (true)`): Expected
// equality of these values: state_after(s, {5, bound, 2, 5, 2, 3, 3}) ...
// "setting on: a refused cast takes the walk".
TEST_F(LivingActFlow, a_cast_that_refuses_after_the_gates_yes_takes_the_ordinary_act_with_the_setting_on)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("setting " + std::to_string(fixes));
        fresh_world(fixes);
        stub_soldier("return true", "return false, 'TEST REFUSAL'");
        living* soldier = bot(FAMILY_SOLDIER, 1, 120, 120, 1, 100.0f);
        ASSERT_NE(nullptr, soldier);
        const std::uint32_t bound = slot_bound(soldier);
        const std::uint32_t s =
            state_where({{5, 0}, {bound, kAny}, {2, kAny}, {5, 1}, {2, 1}});
        const Act a = act_of(world(), soldier, s);
        EXPECT_FLOAT_EQ(100.0f, a.mp) << "a refusal spends nothing";
        if (fixes == 0) {
            EXPECT_FALSE(a.returned);
            EXPECT_EQ(state_after(s, {5, bound, 2}), a.rng)
                << "setting off: a refused cast ends the act";
            EXPECT_EQ(0u, a.commands);
        } else {
            EXPECT_TRUE(a.returned);
            EXPECT_EQ(state_after(s, {5, bound, 2, 5, 2, 3, 3}), a.rng)
                << "setting on: a refused cast takes the walk";
            ASSERT_EQ(1u, a.commands);
            EXPECT_EQ(COMMAND_WALK, a.command);
            EXPECT_EQ(20, a.count);
        }

        fresh_world(fixes);
        stub_soldier("return true", "return true");
        soldier = bot(FAMILY_SOLDIER, 1, 120, 120, 1, 100.0f);
        ASSERT_NE(nullptr, soldier);
        const Act cast = act_of(world(), soldier, s);
        EXPECT_TRUE(cast.returned) << "a cast is the act";
        EXPECT_EQ(state_after(s, {5, bound, 2}), cast.rng) << "nothing drawn after the cast";
        EXPECT_EQ(0u, cast.commands);
        EXPECT_LT(cast.mp, 100.0f) << "the cast was paid for";
    }
}

// The hit response asks the gate too (statistics::hit_response: the gate,
// then rng(3) only on its yes, then the special), and has no ordinary act
// to fall into: a no there draws nothing more. A level-4 druid bot whose
// last roll left SUMMON FAERIE (slot 2) is struck by a soldier 100 px away
// (inside the gate's 130 px). Idle, the gate says yes on both values: the
// Shift coin and the rng(3) (not 0: no cast). Busy (the cast's own guard,
// fix W1) the setting-on gate says no, so the rng(3) is not drawn; the
// classic gate does not read busy.
//
// RED (the druid's busy guard taken out of its gate): Expected equality of
// these values: state_after(s, {2}) ... "busy, setting on: the gate's no
// skips the rng(3)".
TEST_F(LivingActFlow, the_hit_response_draws_no_ordinary_act_after_a_gates_no)
{
    const std::uint32_t s = state_where({{2, kAny}, {3, 1}});
    for (short fixes : {short{0}, short{1}}) {
        for (bool busy : {false, true}) {
            SCOPED_TRACE("setting " + std::to_string(fixes) + (busy ? ", busy" : ", idle"));
            fresh_world(fixes);
            living* druid = bot(FAMILY_DRUID, 1, 200, 200, 4, 300.0f);
            living* foe = bot(FAMILY_SOLDIER, 2, 300, 200, 1, 0.0f);
            ASSERT_TRUE(druid && foe);
            druid->set_current_special(2);
            druid->set_busy(busy ? 3.0f : 0.0f);
            world().rng_.state_ = s;
            druid->stats()->hit_response(foe);
            if (fixes != 0 && busy)
                EXPECT_EQ(state_after(s, {2}), world().rng_.state_)
                    << "busy, setting on: the gate's no skips the rng(3)";
            else
                EXPECT_EQ(state_after(s, {2, 3}), world().rng_.state_)
                    << "the gate's yes: the Shift coin and the rng(3)";
            EXPECT_EQ(0u, druid->stats()->commands.size()) << "nothing queued";
            EXPECT_FLOAT_EQ(300.0f, druid->stats()->magicpoints()) << "nothing cast";
        }
    }
}
