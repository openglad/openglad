/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-2: the rules the review found in living.cpp, obmap.cpp.
// Each test is named after the rule it asserts.
//
// Every scene is headless: an open field of grass, bodies placed in pixels,
// the sim stream (GameWorld::rng_) pinned to a state whose draws are known.
// living::act's order of business is probed with ACT_DIE where the question
// is "did the act reach the act-type switch": that arm marks the walker dead
// and nothing else in the act does, so dead() answers it exactly.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "kit_probe_family.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
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

class SemAi2 : public ::testing::Test {
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
    // walking, facing right.
    living* living_at(int family, unsigned char team, short x, short y)
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
        return dynamic_cast<living*>(place(w, x, y));
    }

    walker* treasure_at(int family, short x, short y, short sx, short sy)
    {
        return place(world().add_fx_ob(Order::Treasure, family), x, y, sx, sy);
    }

    walker* weapon_at(int family, unsigned char team, short x, short y,
                      short sx = 6, short sy = 6)
    {
        walker* w = world().add_weap_ob(Order::Weapon, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        return place(w, x, y, sx, sy);
    }

    static void face(walker* w, int dir)
    {
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// living::act: the per-act clocks
// ---------------------------------------------------------------------------

// living.cpp:243. In the trees a forestwalker's step shrinks, from its normal
// step, by 4 - dexterity/10 (a player character) or 4 - level/2 (an NPC),
// never by a negative amount and never below 1 px. Each case is ONE act from
// the normal step (the carried value compounds over later acts: #462, not
// pinned here). Old game (7ff1654a living.cpp:230-246): the same arithmetic.
TEST_F(SemAi2, a_forestwalker_in_the_trees_steps_shorter_by_its_dexterity_or_level)
{
    struct Case
    {
        const char* name;
        bool player;
        short level;
        short dexterity;
        float normal;
        float want;
    };
    const Case cases[] = {
        {"NPC level 4: 4 - 2 = 2 off a 4 px step", false, 4, 0, 4.0f, 2.0f},
        {"NPC level 2: 4 - 1 = 3 off a 4 px step", false, 2, 0, 4.0f, 1.0f},
        {"NPC level 10: 4 - 5 < 0, nothing off", false, 10, 0, 4.0f, 4.0f},
        {"NPC level 0: 4 off a 3 px step, floored at 1", false, 0, 0, 3.0f, 1.0f},
        {"player dex 20: 4 - 2 = 2 off a 4 px step", true, 4, 20, 4.0f, 2.0f},
        {"player dex 15: 4 - 1.5 = 2.5 off a 4 px step", true, 4, 15, 4.0f, 1.5f},
        {"player dex 50: 4 - 5 < 0, nothing off", true, 4, 50, 4.0f, 4.0f},
        {"player dex 0: 4 off a 3 px step, floored at 1", true, 4, 0, 3.0f, 1.0f},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        tile(10, 10, PIX_TREE_B1);  // GRID_SIZE 16: the walker's own cell
        living* elf = living_at(FAMILY_SOLDIER, 0, 165, 165);
        ASSERT_NE(nullptr, elf);
        elf->stats()->set_bit_flags(BIT_FORESTWALK, 1);
        elf->set_act_type(ACT_CONTROL);
        elf->stats()->set_level(c.level);
        if (c.player) {
            elf->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
            ASSERT_NE(nullptr, elf->myguy);
            elf->myguy->dexterity = c.dexterity;
        }
        elf->set_normal_stepsize(c.normal);
        elf->set_stepsize(c.normal);
        elf->set_speed_bonus_left(0);

        (void)elf->act();

        EXPECT_FLOAT_EQ(c.want, elf->stepsize());
    }
    // The control: off the trees the step is the normal step.
    fresh_world();
    living* elf = living_at(FAMILY_SOLDIER, 0, 165, 165);
    ASSERT_NE(nullptr, elf);
    elf->stats()->set_bit_flags(BIT_FORESTWALK, 1);
    elf->set_act_type(ACT_CONTROL);
    elf->set_normal_stepsize(4.0f);
    elf->set_stepsize(1.0f);
    (void)elf->act();
    EXPECT_FLOAT_EQ(4.0f, elf->stepsize()) << "on grass: the normal step";
}

// living.cpp:264. A special's rest counts down one per act and stays at 0.
TEST_F(SemAi2, a_specials_rest_counts_down_one_per_act_and_stops_at_zero)
{
    living* mage = living_at(FAMILY_MAGE, 0, 320, 320);
    ASSERT_NE(nullptr, mage);
    mage->set_act_type(ACT_CONTROL);
    mage->set_special_rest_left(2);
    (void)mage->act();
    EXPECT_EQ(1, mage->special_rest_left()) << "> 0: one off";
    (void)mage->act();
    EXPECT_EQ(0, mage->special_rest_left()) << "> 0: one off, to 0";
    for (int i = 0; i < 5; ++i)
        (void)mage->act();
    EXPECT_EQ(0, mage->special_rest_left()) << "0: stays 0";
}

// living.cpp:265. An attack lunge eases back by 0.4 per act and stops at 0.
// Old game: `attack_lunge -= 0.4f; if (< 0) = 0`, the same.
TEST_F(SemAi2, an_attack_lunge_eases_back_by_four_tenths_per_act_and_stops_at_zero)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, s);
    s->set_act_type(ACT_CONTROL);
    s->set_attack_lunge(1.0f);
    (void)s->act();
    EXPECT_FLOAT_EQ(1.0f - 0.4f, s->attack_lunge()) << "> 0.4: exactly 0.4 off";
    s->set_attack_lunge(0.2f);
    (void)s->act();
    EXPECT_FLOAT_EQ(0.0f, s->attack_lunge()) << "<= 0.4: clamped to 0";
    (void)s->act();
    EXPECT_FLOAT_EQ(0.0f, s->attack_lunge()) << "0: stays 0";
}

// living.cpp:273. A hit recoil eases back by 0.6 per act and stops at 0.
TEST_F(SemAi2, a_hit_recoil_eases_back_by_six_tenths_per_act_and_stops_at_zero)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, s);
    s->set_act_type(ACT_CONTROL);
    s->set_hit_recoil(1.0f);
    (void)s->act();
    EXPECT_FLOAT_EQ(1.0f - 0.6f, s->hit_recoil()) << "> 0.6: exactly 0.6 off";
    s->set_hit_recoil(0.3f);
    (void)s->act();
    EXPECT_FLOAT_EQ(0.0f, s->hit_recoil()) << "<= 0.6: clamped to 0";
    (void)s->act();
    EXPECT_FLOAT_EQ(0.0f, s->hit_recoil()) << "0: stays 0";
}

// ---------------------------------------------------------------------------
// living::act: the order of business
// ---------------------------------------------------------------------------

// living.cpp:294. A walker in the middle of an animation finishes it before
// anything else: no frozen drain, busy drain, command or act-type switch.
// Walking, the act goes on (here: the frozen drain, or with nothing frozen
// the ACT_DIE switch arm).
TEST_F(SemAi2, a_walker_mid_animation_finishes_it_before_anything_else)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, s);
    s->set_act_type(ACT_DIE);
    s->set_ani_type(ANI_ATTACK);
    s->set_cycle(0);
    s->set_busy(3.0f);
    s->stats()->set_frozen_delay(2);
    s->stats()->add_command(COMMAND_WALK, 5, 1, 0);

    (void)s->act();

    EXPECT_EQ(ANI_ATTACK, s->ani_type()) << "animating: the attack plays on";
    EXPECT_EQ(1, s->cycle()) << "animating: one frame of the attack played";
    EXPECT_FLOAT_EQ(3.0f, s->busy()) << "animating: busy not drained";
    EXPECT_EQ(2, s->stats()->frozen_delay()) << "animating: frozen not drained";
    const Front f = front_of(s);
    EXPECT_EQ(1u, f.size);
    EXPECT_EQ(5, f.count) << "animating: the walk command did not run";
    EXPECT_EQ(320, s->xpos());
    EXPECT_EQ(0, s->dead()) << "animating: the act-type switch did not run";

    // Walking: the act goes on to the frozen drain.
    s->set_ani_type(ANI_WALK);
    s->set_cycle(0);
    (void)s->act();
    EXPECT_EQ(1, s->stats()->frozen_delay()) << "walking: frozen drained";

    // Walking and not frozen: it reaches the switch.
    s->stats()->set_frozen_delay(0);
    s->stats()->clear_command();
    (void)s->act();
    EXPECT_EQ(1, s->dead()) << "walking: the act-type switch ran (ACT_DIE)";
}

// living.cpp:298. A frozen walker spends its act thawing by one and does
// nothing else (no busy drain, no AI, no draw); at 0, or in the thaw-immunity
// phase (frozen_delay_raw < 0, frozen_delay() == 0), the act goes on.
TEST_F(SemAi2, a_frozen_walker_spends_its_act_thawing_and_nothing_else)
{
    living* orc = living_at(FAMILY_ORC, 1, 320, 320);
    living* foe = living_at(FAMILY_SOLDIER, 0, 352, 320);
    ASSERT_TRUE(orc && foe);
    orc->stats()->set_frozen_delay(3);
    orc->set_busy(2.0f);
    world().rng_.state_ = 777u;

    EXPECT_TRUE(orc->act()) << "frozen: the act is spent";
    EXPECT_EQ(2, orc->stats()->frozen_delay()) << "frozen: one off";
    EXPECT_EQ(777u, world().rng_.state_) << "frozen: the ACT_RANDOM AI drew nothing";
    EXPECT_EQ(0u, orc->stats()->commands.size()) << "frozen: no command queued";
    EXPECT_EQ(nullptr, orc->foe()) << "frozen: no foe sought";
    EXPECT_FLOAT_EQ(2.0f, orc->busy()) << "frozen: busy not drained";

    // At 0 the act reaches the switch.
    orc->set_act_type(ACT_DIE);
    orc->stats()->set_frozen_delay(0);
    (void)orc->act();
    EXPECT_FLOAT_EQ(1.0f, orc->busy()) << "0: busy drained";
    EXPECT_EQ(1, orc->dead()) << "0: the act-type switch ran";

    // In the immunity phase too.
    living* orc2 = living_at(FAMILY_ORC, 1, 600, 600);
    ASSERT_NE(nullptr, orc2);
    orc2->set_act_type(ACT_DIE);
    orc2->stats()->set_frozen_delay(-5);
    ASSERT_EQ(0, orc2->stats()->frozen_delay());
    (void)orc2->act();
    EXPECT_EQ(1, orc2->dead()) << "immune phase: the act-type switch ran";
}

// living.cpp:309. A walker not yet facing where it wants to go spends its act
// turning one notch toward it; aligned, the act goes on.
TEST_F(SemAi2, a_walker_not_facing_its_goal_spends_its_act_turning_one_notch)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, s);
    s->set_act_type(ACT_DIE);
    s->set_curdir(static_cast<signed char>(FACE_UP));
    s->set_enddir(static_cast<char>(FACE_RIGHT));
    s->stats()->add_command(COMMAND_WALK, 5, 1, 0);

    EXPECT_TRUE(s->act());
    EXPECT_EQ(FACE_UP_RIGHT, s->curdir()) << "turning: one notch clockwise";
    EXPECT_EQ(FACE_RIGHT, s->enddir());
    EXPECT_EQ(5, front_of(s).count) << "turning: the command did not run";
    EXPECT_EQ(0, s->dead()) << "turning: the act-type switch did not run";

    (void)s->act();
    EXPECT_EQ(FACE_RIGHT, s->curdir()) << "turning: the second notch";
    EXPECT_EQ(0, s->dead());

    s->stats()->clear_command();
    (void)s->act();
    EXPECT_EQ(1, s->dead()) << "aligned: the act goes on to the switch";
}

// living.cpp:314. Queued commands run before the AI; a command step that does
// something ends the act; a step that returns 0 (here COMMAND_FOLLOW refused
// because the walker has a foe) lets the act go on.
TEST_F(SemAi2, a_command_step_that_does_nothing_lets_the_act_go_on)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    living* orc = living_at(FAMILY_ORC, 1, 600, 600);
    ASSERT_TRUE(s && orc);
    s->set_act_type(ACT_DIE);
    s->set_foe(orc);
    s->stats()->add_command(COMMAND_FOLLOW, 5, 0, 0);

    (void)s->act();

    EXPECT_EQ(0u, s->stats()->commands.size()) << "the refused follow is spent";
    EXPECT_EQ(nullptr, s->leader());
    EXPECT_EQ(1, s->dead()) << "returned 0: the act went on to the switch";

    // The control: a command that ran ends the act.
    living* s2 = living_at(FAMILY_SOLDIER, 0, 400, 400);
    ASSERT_NE(nullptr, s2);
    s2->set_act_type(ACT_DIE);
    s2->stats()->add_command(COMMAND_WALK, 5, 1, 0);
    EXPECT_TRUE(s2->act());
    EXPECT_EQ(4, front_of(s2).count) << "ran: one step of the walk";
    EXPECT_EQ(0, s2->dead()) << "ran: the act ended there";
}

// living.cpp:321. The exit-pad skip counter counts down one per act, and
// stays at 0.
TEST_F(SemAi2, the_exit_skip_counter_counts_down_and_stops_at_zero)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, s);
    s->set_act_type(ACT_CONTROL);
    s->set_skip_exit(2);
    (void)s->act();
    EXPECT_EQ(1, s->skip_exit()) << "> 0: one off";
    (void)s->act();
    EXPECT_EQ(0, s->skip_exit()) << "> 0: to 0";
    (void)s->act();
    (void)s->act();
    EXPECT_EQ(0, s->skip_exit()) << "0: stays 0";
}

// living.cpp:325. An unseated walker with an action order runs it first and,
// when it acted, the AI does not run; a seated walker ignores its action, and
// an order that passes (do_action 0: the follower has a foe) lets the act go
// on to the switch.
TEST_F(SemAi2, a_seated_walker_ignores_its_action_and_a_passing_action_lets_the_act_go_on)
{
    living* hero = living_at(FAMILY_SOLDIER, 0, 200, 64);
    living* ally = living_at(FAMILY_SOLDIER, 0, 64, 64);
    living* orc = living_at(FAMILY_ORC, 1, 800, 800);
    ASSERT_TRUE(hero && ally && orc);
    hero->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    hero->set_user(0);
    hero->set_act_type(ACT_CONTROL);

    // The control: unseated, no foe: the follow order acts and ends the act.
    ally->set_act_type(ACT_DIE);
    ally->set_action(ACTION_FOLLOW);
    ally->set_user(-1);
    EXPECT_TRUE(ally->act());
    EXPECT_EQ(hero, ally->leader()) << "unseated: the follow order ran";
    EXPECT_EQ(COMMAND_FOLLOW, front_of(ally).type);
    EXPECT_EQ(0, ally->dead()) << "unseated: the order was the act";

    // Seated: the action is ignored and the act goes on to the switch.
    ally->stats()->clear_command();
    ally->set_leader(nullptr);
    ally->set_user(1);
    (void)ally->act();
    EXPECT_EQ(nullptr, ally->leader()) << "seated: the follow order did not run";
    EXPECT_EQ(0u, ally->stats()->commands.size()) << "seated: no follow queued";
    EXPECT_EQ(1, ally->dead()) << "seated: the act-type switch ran";

    // Unseated with a foe: do_action passes (0) and the act goes on.
    living* ally2 = living_at(FAMILY_SOLDIER, 0, 64, 300);
    ASSERT_NE(nullptr, ally2);
    ally2->set_act_type(ACT_DIE);
    ally2->set_action(ACTION_FOLLOW);
    ally2->set_user(-1);
    ally2->set_foe(orc);
    (void)ally2->act();
    EXPECT_EQ(nullptr, ally2->leader()) << "passing: no leader taken";
    EXPECT_EQ(0u, ally2->stats()->commands.size()) << "passing: no follow queued";
    EXPECT_EQ(1, ally2->dead()) << "passing: the act-type switch ran";
}
