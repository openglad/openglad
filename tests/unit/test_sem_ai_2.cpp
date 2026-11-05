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
#include "../test_sim_random_scope.h"
#include "kit_probe_family.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/specials_view.h>
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
#include <list>
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

// The sim stream, steered through ScopedSimRandom: the glibc LCG SimRandom
// runs (an installed override would recurse through SimRandom::next, so the
// step is inlined), recording every bound asked.
class BoundRecorder : public IRandom
{
public:
    explicit BoundRecorder(std::uint32_t seed) : state(seed) {}
    std::uint32_t next(std::uint32_t bound) override
    {
        bounds.push_back(bound);
        state = state * 1103515245u + 12345u;
        return (state >> 16) % bound;
    }
    std::uint32_t state;
    std::vector<std::uint32_t> bounds;
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

    // A fresh world with the Gameplay Fixes setting at `fixes` and one
    // forestwalker in it, seated (ACT_CONTROL, so its act is the per-act
    // clocks and nothing else) and in the trees at tile (10, 10), on its
    // normal 4 px step with no haste. A dexterity below 0 makes an NPC (the
    // level decides its penalty); any other makes a hero with that dexterity.
    living* forestwalker_in_the_trees(short fixes, short level, short dexterity)
    {
        fresh_world(fixes);
        tile(10, 10, PIX_TREE_B1);  // GRID_SIZE 16: the walker's own cell
        living* elf = living_at(FAMILY_SOLDIER, 0, 165, 165);
        if (elf == nullptr)
            return nullptr;
        elf->stats()->set_bit_flags(BIT_FORESTWALK, 1);
        elf->set_act_type(ACT_CONTROL);
        elf->stats()->set_level(level);
        if (dexterity >= 0) {
            elf->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
            elf->myguy->dexterity = dexterity;
        }
        elf->set_normal_stepsize(4.0f);
        elf->set_stepsize(4.0f);
        elf->set_speed_bonus(0.0f);
        elf->set_speed_bonus_left(0);
        return elf;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// living::act: the per-act clocks
// ---------------------------------------------------------------------------

// living.cpp:352. In the trees a forestwalker's step shrinks, from its normal
// step, by 4 - dexterity/10 (a player character) or 4 - level/2 (an NPC),
// never by a negative amount and never below 1 px. Each case is ONE act from
// the normal step, where both values of the Gameplay Fixes setting agree;
// over later acts they part (fix AG, #462: the four tests below). Old game
// (7ff1654a living.cpp:230-246): the same arithmetic.
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

// living.cpp:352 (fix AG, #462). With the Gameplay Fixes setting on, every
// act in the trees takes the forest penalty off the forestwalker's NORMAL
// step, so its step stays at normal - penalty (never below 1 px) for as long
// as it stays in the trees: a dex-30 hero walks 3 px, not the classic crawl.
TEST_F(SemAi2, a_forestwalker_in_the_trees_keeps_its_forest_step_with_the_setting_on)
{
    struct Case
    {
        const char* name;
        short level;
        short dexterity;  // below 0: an NPC
        float want;
    };
    const Case cases[] = {
        {"NPC level 4: 4 - 2 = 2", 4, -1, 2.0f},
        {"NPC level 1: 4 - 3.5 = 0.5, floored at 1", 1, -1, 1.0f},
        {"NPC level 10: nothing off", 10, -1, 4.0f},
        {"hero dex 20: 4 - 2 = 2", 4, 20, 2.0f},
        {"hero dex 30: 4 - 1 = 3", 4, 30, 3.0f},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        living* elf = forestwalker_in_the_trees(1, c.level, c.dexterity);
        ASSERT_NE(nullptr, elf);
        for (int act = 1; act <= 10; ++act) {
            (void)elf->act();
            EXPECT_FLOAT_EQ(c.want, elf->stepsize()) << "after act " << act;
        }
    }
}

// living.cpp:352 (fix AG, #462). With the setting off (the classic game, and
// the old game's src/living.cpp `stepsize -= temp`), every act in the trees
// takes the penalty off the step the walker already carries, so a
// forestwalker with any penalty slows act by act to 1 px and stays there.
TEST_F(SemAi2, a_forestwalker_in_the_trees_slows_to_one_pixel_with_the_setting_off)
{
    struct Case
    {
        const char* name;
        short level;
        short dexterity;  // below 0: an NPC
        float want[4];    // after acts 1 to 4
    };
    const Case cases[] = {
        {"NPC level 4: 2 off each act", 4, -1, {2.0f, 1.0f, 1.0f, 1.0f}},
        {"hero dex 30: 1 off each act", 4, 30, {3.0f, 2.0f, 1.0f, 1.0f}},
        {"NPC level 10: no penalty, no change", 10, -1, {4.0f, 4.0f, 4.0f, 4.0f}},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        living* elf = forestwalker_in_the_trees(0, c.level, c.dexterity);
        ASSERT_NE(nullptr, elf);
        for (int act = 1; act <= 4; ++act) {
            (void)elf->act();
            EXPECT_FLOAT_EQ(c.want[act - 1], elf->stepsize()) << "after act " << act;
        }
    }
}

// living.cpp:352/377 (fix AG, #462). With the setting on, a hastened
// forestwalker in the trees steps normal - penalty + haste every act the
// haste lasts, then normal - penalty: the haste is added once, on top of the
// forest step, and never compounds. Entering the trees on a hastened step
// (normal + haste, as an act on grass leaves it) changes nothing.
TEST_F(SemAi2, a_hastened_forestwalker_in_the_trees_steps_its_forest_step_plus_the_haste_with_the_setting_on)
{
    {
        SCOPED_TRACE("level 10 (no penalty), haste 2 for 100 acts");
        living* elf = forestwalker_in_the_trees(1, 10, -1);
        ASSERT_NE(nullptr, elf);
        elf->set_speed_bonus(2.0f);
        elf->set_speed_bonus_left(100);
        for (int act = 1; act <= 100; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(6.0f, elf->stepsize()) << "after act " << act;
        }
        ASSERT_EQ(0, elf->speed_bonus_left());
        (void)elf->act();
        EXPECT_FLOAT_EQ(4.0f, elf->stepsize()) << "the haste spent: the normal step";
    }
    {
        SCOPED_TRACE("level 6 (1 off), haste 2 for 100 acts");
        living* elf = forestwalker_in_the_trees(1, 6, -1);
        ASSERT_NE(nullptr, elf);
        elf->set_speed_bonus(2.0f);
        elf->set_speed_bonus_left(100);
        for (int act = 1; act <= 100; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(5.0f, elf->stepsize()) << "after act " << act;
        }
        (void)elf->act();
        EXPECT_FLOAT_EQ(3.0f, elf->stepsize()) << "the haste spent: 4 - 1";
    }
    {
        SCOPED_TRACE("level 10, entering the trees on a hastened 6 px step");
        living* elf = forestwalker_in_the_trees(1, 10, -1);
        ASSERT_NE(nullptr, elf);
        elf->set_stepsize(6.0f);
        elf->set_speed_bonus(2.0f);
        elf->set_speed_bonus_left(10);
        (void)elf->act();
        EXPECT_FLOAT_EQ(6.0f, elf->stepsize()) << "4 + 2, not the carried 6 + 2";
    }
}

// living.cpp:352/377 (fix AG, #462). With the setting off (the classic game;
// #462's repro), the haste is added to the step the walker carries, so in the
// trees the step grows by (haste - penalty) every act the haste lasts and
// keeps its size until the walker leaves the trees: a level-10 NPC elf (no
// penalty) with a 2 px haste for 100 acts steps 204 px. A smaller penalty
// grows it while the haste lasts and then crawls it back to 1 px.
TEST_F(SemAi2, a_hastened_forestwalker_in_the_trees_compounds_its_step_with_the_setting_off)
{
    {
        SCOPED_TRACE("level 10 (no penalty), haste 2 for 100 acts");
        living* elf = forestwalker_in_the_trees(0, 10, -1);
        ASSERT_NE(nullptr, elf);
        elf->set_speed_bonus(2.0f);
        elf->set_speed_bonus_left(100);
        for (int act = 1; act <= 100; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(static_cast<float>(4 + 2 * act), elf->stepsize()) << "after act " << act;
        }
        EXPECT_FLOAT_EQ(204.0f, elf->stepsize());
        for (int act = 1; act <= 10; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(204.0f, elf->stepsize()) << "haste spent, still in the trees: act " << act;
        }
        elf->setxy(400, 400);  // out of the trees, onto grass
        (void)elf->act();
        EXPECT_FLOAT_EQ(4.0f, elf->stepsize()) << "on grass: the normal step";
    }
    {
        SCOPED_TRACE("level 6 (1 off), haste 2 for 10 acts");
        living* elf = forestwalker_in_the_trees(0, 6, -1);
        ASSERT_NE(nullptr, elf);
        elf->set_speed_bonus(2.0f);
        elf->set_speed_bonus_left(10);
        for (int act = 1; act <= 10; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(static_cast<float>(4 + act), elf->stepsize()) << "after act " << act;
        }
        for (int act = 1; act <= 13; ++act) {
            (void)elf->act();
            ASSERT_FLOAT_EQ(static_cast<float>(14 - act), elf->stepsize()) << "haste spent: act " << act;
        }
        (void)elf->act();
        EXPECT_FLOAT_EQ(1.0f, elf->stepsize()) << "floored at 1 px";
    }
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

// ---------------------------------------------------------------------------
// living::act: the ACT_RANDOM bot
// ---------------------------------------------------------------------------

// living.cpp:369. A rolled slot whose name is NONE folds to slot 1 on both
// values of Gameplay Fixes. The probe family declares slot 2 by the name
// NONE with a price the bot can pay (5 MP), so check_special's own
// unaffordable-slot fallback (living.cpp:608) cannot do the fold for it: only
// the NONE rule does. A slot out of the table's range (a level-400 bot's roll
// of 130 wraps the char to -126, reachable only with the setting off, whose
// bound is (level + 2) / 3) folds to slot 1 too. The control: a named slot
// rolled (slot 3, HEAL) stays. Old game (7ff1654a living.cpp:350-367): the
// same fold on the NONE name only. The range guard (current_special() < 1 or
// >= NUM_SPECIALS) is the current game's: the old game has none, so its
// level-400 roll indexes special_name[family][-126] out of bounds (undefined
// behaviour), and the out-of-range fold has no classic oracle. The third
// sub-arm, a family with no descriptor, is not staged: every mounted family
// has one.
inline constexpr const char* kNoneSlotDecl =
    "og.family('living', { id = 'kitprobe:golem', wire_id = 18,\n"
    "  specials = {\n"
    "    { id = 'bomb', name = 'DROP BOMB', mp_cost = 35 },\n"
    "    { id = 'none', name = 'NONE', mp_cost = 5 },\n"
    "    { id = 'heal', name = 'HEAL', mp_cost = 2 },\n"
    "  } })\n";

TEST_F(SemAi2, a_bot_that_rolls_an_unnamed_or_out_of_range_slot_casts_slot_one)
{
    og::test::ProbeFamily probe(kNoneSlotDecl);
    ASSERT_EQ(1, probe.installed());
    const FamilyDescriptor* fd = og::test::ProbeFamily::fd();
    ASSERT_STREQ("NONE", og::sim::special_name(fd, 2, 0));
    ASSERT_STREQ("HEAL", og::sim::special_name(fd, 3, 0));
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("gameplay_fixes " + std::to_string(fixes));
        ASSERT_EQ(3, og::sim::bot_slot_roll_bound(fd, 7, 0, fixes));
        for (std::uint32_t roll : {1u, 2u}) {
            SCOPED_TRACE("roll " + std::to_string(roll));
            fresh_world(fixes);
            world().new_specials = 0;
            living* bot = living_at(FAMILY_GOLEM, 1, 320, 320);
            ASSERT_NE(nullptr, bot);
            bot->stats()->set_level(7);
            ASSERT_GE(bot->stats()->magicpoints(),
                      static_cast<float>(og::sim::cast_cost(*bot, 2, false)))
                << "slot 2 is affordable: check_special keeps it";
            world().rng_.state_ = state_where({{5, 0}, {3, roll}});
            (void)bot->act();
            if (roll == 1)
                EXPECT_EQ(1, bot->current_special()) << "NONE: folded to slot 1";
            else
                EXPECT_EQ(3, bot->current_special()) << "control: a named slot stays";
        }
    }
    // Out of range: setting off, level 400, bound 134, roll 129 -> slot 130.
    fresh_world(0);
    world().new_specials = 0;
    living* bot = living_at(FAMILY_GOLEM, 1, 320, 320);
    ASSERT_NE(nullptr, bot);
    bot->stats()->set_level(400);
    ASSERT_EQ(134, og::sim::bot_slot_roll_bound(fd, 400, 0, 0));
    world().rng_.state_ = state_where({{5, 0}, {134, 129}});
    (void)bot->act();
    EXPECT_EQ(1, bot->current_special()) << "out of range: folded to slot 1";
}

// living.cpp:401. Seeking, with no foe near and none held: half the time
// (the coin's 0) the bot takes the nearest foe anywhere and queues nothing
// this act. The near search must miss for this arm to exist: it falls back
// to the nearest-anywhere scan itself, so the foe here is cloaked
// (invisibility 40: each look is a next(2) and a nonzero hides it). The
// soldier stands at the map corner, so the near spiral leaves the map after
// three empty piles and asks the full scan, which the cloak hides (1); then
// the coin (0) and the second full scan sees it (0).
// Old game (7ff1654a living.cpp:388-401, screen.cpp:992-1100): the same
// spiral, its fallback, and find_far_foe, the same scan.
TEST_F(SemAi2, a_bot_with_no_foe_near_takes_the_nearest_foe_anywhere_on_the_coins_zero)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 16, 16);
    living* orc = living_at(FAMILY_ORC, 1, 900, 900);
    ASSERT_TRUE(s && orc);
    orc->set_invisibility_left(40);
    const std::uint32_t start =
        state_where({{5, kNonZero}, {5, kNonZero}, {2, 1}, {2, 0}, {2, 0}});
    world().rng_.state_ = start;

    EXPECT_TRUE(s->act());

    EXPECT_EQ(orc, s->foe()) << "coin 0: the nearest foe anywhere is taken";
    EXPECT_EQ(0u, s->stats()->commands.size()) << "coin 0: no command this act";
    EXPECT_EQ(state_after(start, {5, 5, 2, 2, 2}), world().rng_.state_)
        << "the special roll, the seek roll, the hidden look, the coin, the seen look";
}

// living.cpp:437. A walker shoves only a living ally, and only two times in
// three (rng(3) != 0), never a seated hero (ACT_CONTROL: the draw is still
// made). A foe or a dead ally is refused before any draw.
TEST_F(SemAi2, a_shove_needs_a_living_ally_that_is_not_a_seat_and_a_nonzero_roll)
{
    living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
    living* ally = living_at(FAMILY_SOLDIER, 0, 336, 320);
    living* orc = living_at(FAMILY_ORC, 1, 320, 400);
    ASSERT_TRUE(s && ally && orc);
    const auto ready = [](walker* t) {
        t->stats()->clear_command();
        t->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
    };
    const auto search_kept = [](const walker* t) {
        const Front f = front_of(t);
        return f.size == 1 && f.type == COMMAND_SEARCH && f.count == 10;
    };

    // The control: an ally, a nonzero roll: the shove proceeds.
    ready(ally);
    std::uint32_t st = state_where({{3, kNonZero}});
    world().rng_.state_ = st;
    EXPECT_EQ(1, s->shove(ally, 1, 0)) << "ally, roll 1-2: shoved";
    EXPECT_EQ(COMMAND_WALK, front_of(ally).type);

    // Roll 0: refused, one draw, the ally's queue untouched.
    ready(ally);
    st = state_where({{3, 0}});
    world().rng_.state_ = st;
    EXPECT_EQ(0, s->shove(ally, 1, 0)) << "roll 0: refused";
    EXPECT_TRUE(search_kept(ally)) << "roll 0: the ally keeps its search";
    EXPECT_EQ(state_after(st, {3}), world().rng_.state_) << "roll 0: one draw";

    // A seat: refused on a nonzero roll, the draw still made.
    ready(ally);
    ally->set_act_type(ACT_CONTROL);
    st = state_where({{3, kNonZero}});
    world().rng_.state_ = st;
    EXPECT_EQ(0, s->shove(ally, 1, 0)) << "a seat: refused";
    EXPECT_TRUE(search_kept(ally)) << "a seat: its queue untouched";
    EXPECT_EQ(state_after(st, {3}), world().rng_.state_) << "a seat: one draw";
    ally->set_act_type(ACT_RANDOM);

    // A foe: refused, no draw.
    ready(orc);
    world().rng_.state_ = st;
    EXPECT_EQ(0, s->shove(orc, 1, 0)) << "a foe: refused";
    EXPECT_TRUE(search_kept(orc)) << "a foe: its queue untouched";
    EXPECT_EQ(st, world().rng_.state_) << "a foe: no draw";

    // A dead ally: refused, no draw.
    ready(ally);
    ally->set_dead(1);
    EXPECT_EQ(0, s->shove(ally, 1, 0)) << "dead: refused";
    EXPECT_TRUE(search_kept(ally)) << "dead: its queue untouched";
    EXPECT_EQ(st, world().rng_.state_) << "dead: no draw";
}

// ---------------------------------------------------------------------------
// living::walk
// ---------------------------------------------------------------------------

// living.cpp:488. A walk in a direction the walker does not face records the
// new facing and turns one notch, without moving; a seated hero (ACT_CONTROL)
// with no commands only records it (its act turns it), and with a command
// queued it turns here like a bot.
TEST_F(SemAi2, a_walk_off_the_facing_turns_one_notch_but_a_seated_hero_only_records_it)
{
    struct Case
    {
        const char* name;
        int act_type;
        bool command;
        int want_curdir;
    };
    const Case cases[] = {
        {"AI", ACT_RANDOM, false, FACE_UP_RIGHT},
        {"seated, no commands", ACT_CONTROL, false, FACE_UP},
        {"seated, a command queued", ACT_CONTROL, true, FACE_UP_RIGHT},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        living* s = living_at(FAMILY_SOLDIER, 0, 320, 320);
        ASSERT_NE(nullptr, s);
        s->set_act_type(static_cast<char>(c.act_type));
        face(s, FACE_UP);
        if (c.command)
            s->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        EXPECT_TRUE(s->walk(1.0f, 0.0f));
        EXPECT_EQ(FACE_RIGHT, s->enddir()) << "the new facing is recorded";
        EXPECT_EQ(c.want_curdir, s->curdir());
        EXPECT_EQ(320, s->xpos()) << "no step";
    }
}

// living.cpp:503. A step moves only when the destination is passable at its
// exact, rounded-up and rounded-down offsets: a wall the step meets refuses
// it (returns 0, no move, the walk cycle stands), and so does a wall only
// the rounded-up offset meets (a fractional step right against it). The
// control: on open grass the step moves and the cycle advances.
TEST_F(SemAi2, a_step_moves_only_when_its_exact_and_rounded_offsets_are_passable)
{
    // GRID_SIZE 16: a 16 px soldier at x 304 fills cell 19 exactly; x 305
    // reaches into cell 20.
    living* s = living_at(FAMILY_SOLDIER, 0, 304, 96);
    ASSERT_NE(nullptr, s);
    s->set_cycle(0);
    EXPECT_TRUE(s->walk(1.0f, 0.0f)) << "control: open grass";
    EXPECT_EQ(305, s->xpos());
    EXPECT_EQ(1, s->cycle()) << "control: the walk cycle advances";

    fresh_world();
    tile(20, 6, PIX_H_WALL1);
    tile(20, 7, PIX_H_WALL1);
    s = living_at(FAMILY_SOLDIER, 0, 304, 96);
    ASSERT_NE(nullptr, s);
    s->set_cycle(0);
    EXPECT_FALSE(s->walk(1.0f, 0.0f)) << "blocked: refused";
    EXPECT_FLOAT_EQ(304.0f, s->worldx()) << "blocked: no move";
    EXPECT_EQ(0, s->cycle()) << "blocked: the cycle stands";

    // A 1.5 px step from x 303: the exact offset (304.5, read as 304) and the
    // rounded-down one (304) stay in cell 19; only the rounded-up one (305)
    // reaches the wall. (A step under 1 px faces up or down, facing() reads
    // whole pixels, so it would only turn the walker.)
    s->setxy(303, 96);
    s->set_cycle(0);
    ASSERT_EQ(FACE_RIGHT, s->facing(1, 0));
    EXPECT_FALSE(s->walk(1.5f, 0.0f)) << "only the rounded-up offset meets the wall: refused";
    EXPECT_FLOAT_EQ(303.0f, s->worldx()) << "1.5 px step: no move";
    EXPECT_EQ(0, s->cycle());
}

// living.cpp:523. A refused step shoves only a LIVING ALLY: when the body
// the step met is a friendly generator (not living) or a foe, nothing is
// shoved and nothing is drawn, on both values of Gameplay Fixes.
TEST_F(SemAi2, a_refused_step_shoves_no_foe_and_no_non_living_body)
{
    for (short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE("gameplay_fixes " + std::to_string(fixes));
        // A friendly tent in the way.
        fresh_world(fixes);
        walker* tent = place(world().add_ob(Order::Generator, FAMILY_TENT), 319, 96);
        ASSERT_NE(nullptr, tent);
        tent->set_team_num(0);
        living* s = living_at(FAMILY_SOLDIER, 0, 304, 96);
        ASSERT_NE(nullptr, s);
        world().rng_.state_ = 4242u;
        EXPECT_FALSE(s->walk(1.0f, 0.0f)) << "the tent blocks";
        EXPECT_EQ(tent, s->collide_ob()) << "the step met the tent";
        EXPECT_EQ(4242u, world().rng_.state_) << "non-living: no shove, no draw";
        EXPECT_EQ(0u, tent->stats()->commands.size()) << "non-living: no walk injected";

        // A foe in the way.
        fresh_world(fixes);
        living* foe = living_at(FAMILY_SOLDIER, 0, 319, 96);
        living* orc = living_at(FAMILY_ORC, 1, 304, 96);
        ASSERT_TRUE(foe && orc);
        foe->stats()->add_command(COMMAND_SEARCH, 10, 0, 0);
        world().rng_.state_ = 4242u;
        EXPECT_FALSE(orc->walk(1.0f, 0.0f)) << "the foe blocks";
        EXPECT_EQ(foe, orc->collide_ob()) << "the step met the foe";
        EXPECT_EQ(4242u, world().rng_.state_) << "a foe: no shove, no draw";
        const Front f = front_of(foe);
        EXPECT_EQ(COMMAND_SEARCH, f.type) << "a foe: it keeps its search";
        EXPECT_EQ(10, f.count);
    }
}

// living.cpp:531. A walker flagged always-animate cycles its walk frames
// even when its step is refused; an unflagged one does not.
TEST_F(SemAi2, an_always_animate_walker_cycles_its_frames_on_a_refused_step)
{
    for (bool flagged : {true, false}) {
        SCOPED_TRACE(flagged ? "BIT_ANIMATE" : "not flagged");
        fresh_world();
        tile(20, 6, PIX_H_WALL1);
        tile(20, 7, PIX_H_WALL1);
        living* s = living_at(FAMILY_SOLDIER, 0, 304, 96);
        ASSERT_NE(nullptr, s);
        s->stats()->set_bit_flags(BIT_ANIMATE, flagged ? 1 : 0);
        s->set_cycle(0);
        EXPECT_FALSE(s->walk(1.0f, 0.0f));
        EXPECT_EQ(304, s->xpos());
        EXPECT_EQ(flagged ? 1 : 0, s->cycle());
    }
}

// ---------------------------------------------------------------------------
// living::set_difficulty: the regeneration rates (classic, off by one)
// ---------------------------------------------------------------------------

// living.cpp:683. A placed walker's HP regeneration: whole points per tick
// from 4*L^2/REGEN (the loop runs while the remainder EXCEEDS REGEN, so a
// remainder of exactly REGEN is not folded: level 100 gets 9 whole points,
// not 10 -- the classic off-by-one, pinned), the remainder r > 1 as one
// extra point every REGEN/(r+1) ticks, never faster than every 2.
// Old game (7ff1654a living.cpp:1270-1293): the same arithmetic.
TEST_F(SemAi2, hp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one)
{
    static_assert(REGEN == 4000);
    struct Case
    {
        short level;
        float per_round;
        int max_delay;
    };
    const Case cases[] = {
        {0, 0.0f, 4000},    // 0: no fraction, the delay stays REGEN
        {1, 0.0f, 800},     // 4: 4000 / 5
        {23, 0.0f, 2},      // 2116: 4000 / 2117 = 1, clamped to 2
        {32, 1.0f, 41},     // 4096: one whole point, 96 left: 4000 / 97
        {100, 9.0f, 2},     // 40000: nine whole, 4000 left (not folded): 4000 / 4001 = 0 -> 2
    };
    for (const Case& c : cases) {
        SCOPED_TRACE("level " + std::to_string(c.level));
        living* s = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, s);
        s->stats()->set_heal_per_round(0.0f);
        s->set_difficulty(static_cast<std::uint32_t>(c.level));
        EXPECT_FLOAT_EQ(c.per_round, s->stats()->heal_per_round());
        EXPECT_EQ(c.max_delay, s->stats()->max_heal_delay());
        EXPECT_EQ(0, s->stats()->current_heal_delay()) << "regeneration starts with an empty delay";
    }
}

// living.cpp:709. The same for MP, from 30*L^2: level 20 (12000) gets two
// whole points and a remainder of exactly REGEN (classic, pinned).
TEST_F(SemAi2, mp_regeneration_rate_comes_from_the_level_with_the_classic_off_by_one)
{
    struct Case
    {
        short level;
        float per_round;
        int max_delay;
    };
    const Case cases[] = {
        {0, 0.0f, 4000},    // 0
        {1, 0.0f, 129},     // 30: 4000 / 31
        {9, 0.0f, 2},       // 2430: 4000 / 2431 = 1, clamped to 2
        {12, 1.0f, 12},     // 4320: one whole point, 320 left: 4000 / 321
        {20, 2.0f, 2},      // 12000: two whole, 4000 left (not folded) -> 0 -> 2
    };
    for (const Case& c : cases) {
        SCOPED_TRACE("level " + std::to_string(c.level));
        living* s = living_at(FAMILY_SOLDIER, 1, 320, 320);
        ASSERT_NE(nullptr, s);
        s->stats()->set_magic_per_round(0.0f);
        s->set_difficulty(static_cast<std::uint32_t>(c.level));
        EXPECT_FLOAT_EQ(c.per_round, s->stats()->magic_per_round());
        EXPECT_EQ(c.max_delay, s->stats()->max_magic_delay());
        EXPECT_EQ(0, s->stats()->current_magic_delay()) << "regeneration starts with an empty delay";
    }
}

// ---------------------------------------------------------------------------
// living::act_random (protected: reached through act, whose ACT_RANDOM arm
// calls it when the special roll is not 0 and the second 1-in-5 is)
// ---------------------------------------------------------------------------

class SemAi2Randomer : public SemAi2 {
protected:
    // A soldier bot at (480,480) facing up, sight 10 cells (160 px).
    living* randomer()
    {
        living* s = living_at(FAMILY_SOLDIER, 0, 480, 480);
        if (s == nullptr)
            return nullptr;
        s->set_lineofsight(10);
        face(s, FACE_UP);
        return s;
    }

    // The state from which act() takes act_random() and its 1-in-80 is `roll`.
    static std::uint32_t into_act_random(std::uint32_t roll)
    {
        return state_where({{5, kNonZero}, {5, 0}, {80, roll}});
    }
};

// living.cpp:737. act_random retargets to a near foe one time in 80 (or
// whenever it holds none). With none held it takes the near one; with a far
// foe held, the roll's 0 swaps it for the near one and any other roll keeps
// it. Old game (7ff1654a living.cpp:1370-1374): the same.
TEST_F(SemAi2Randomer, act_random_retargets_to_a_near_foe_one_time_in_eighty_or_when_it_has_none)
{
    struct Case
    {
        const char* name;
        bool hold_far;
        std::uint32_t roll80;
        bool want_near;
    };
    const Case cases[] = {
        {"no foe held, roll 37: the near foe is taken", false, 37, true},
        {"far foe held, roll 0: swapped for the near foe", true, 0, true},
        {"far foe held, roll 37: kept", true, 37, false},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        living* s = randomer();
        living* near_orc = living_at(FAMILY_ORC, 1, 512, 480);
        living* far_orc = living_at(FAMILY_ORC, 1, 900, 480);
        ASSERT_TRUE(s && near_orc && far_orc);
        if (c.hold_far)
            s->set_foe(far_orc);
        world().rng_.state_ = into_act_random(c.roll80);
        (void)s->act();
        EXPECT_EQ(c.want_near ? near_orc : far_orc, s->foe());
    }
}

// living.cpp:746. With a foe inside the sight box and a clear shot the bot
// fires (init_fire's attack row and busy, COMMAND_FIRE with the deltas and a
// 0-23 count); a foe outside the box gets a 200-step search and no turn.
TEST_F(SemAi2Randomer, act_random_fires_at_a_foe_in_sight_and_searches_for_one_out_of_it)
{
    // In sight (32 px east, sight 160 px), the shot clear.
    living* s = randomer();
    living* orc = living_at(FAMILY_ORC, 1, 512, 480);
    ASSERT_TRUE(s && orc);
    face(s, FACE_RIGHT);
    s->set_lastx(1.0f);  // fire_check aims the shot along lastx/lasty
    s->set_lasty(0.0f);
    s->set_foe(orc);
    const std::uint32_t start = into_act_random(kNonZero);
    og::sim::SimRandom replay(start);
    (void)replay.next(5);
    (void)replay.next(5);
    (void)replay.next(80);
    (void)replay.next(4);
    const int count = static_cast<int>(replay.next(24));
    BoundRecorder rec(start);
    {
        ScopedSimRandom steer(&rec);
        (void)s->act();  // act_random's own return is not act's on this path
    }
    // The two 1-in-5s, the 1-in-80, the knife's waver (fire_check's
    // set_weapon_heading: next(knife step / 2 + 1)) and the shot count.
    EXPECT_EQ((std::vector<std::uint32_t>{5, 5, 80, 4, 24}), rec.bounds);
    EXPECT_EQ(orc, s->foe());
    EXPECT_EQ(ANI_ATTACK, s->ani_type()) << "in sight, clear: init_fire starts the attack row";
    EXPECT_FLOAT_EQ(s->fire_frequency(), s->busy()) << "in sight, clear: busy is the fire delay";
    Front f = front_of(s);
    EXPECT_EQ(1u, f.size);
    EXPECT_EQ(COMMAND_FIRE, f.type);
    EXPECT_EQ(count, f.count) << "the count is the next(24) draw";
    EXPECT_EQ(32, f.com1);
    EXPECT_EQ(0, f.com2);
    EXPECT_EQ(replay.state_, rec.state) << "five draws, no more";

    // Out of sight (420 px east, sight 160 px): a search, no turn.
    fresh_world();
    s = randomer();
    orc = living_at(FAMILY_ORC, 1, 900, 480);
    ASSERT_TRUE(s && orc);
    s->set_foe(orc);
    world().rng_.state_ = start;
    (void)s->act();
    EXPECT_EQ(FACE_UP, s->curdir()) << "out of sight: no turn";
    f = front_of(s);
    EXPECT_EQ(1u, f.size);
    EXPECT_EQ(COMMAND_SEARCH, f.type) << "out of sight: a search";
    EXPECT_EQ(200, f.count);
    EXPECT_EQ(state_after(start, {5, 5, 80}), world().rng_.state_) << "out of sight: no fire draw";
}

// ---------------------------------------------------------------------------
// obmap: the collision piles (OBRES 32 px cells; one band of 256 columns per
// floor)
// ---------------------------------------------------------------------------

// obmap.cpp:49. A walker's floor picks its pile band: walkers on different
// floors never block each other, floors below 0 collide as floor 0 and
// floors above 120 as floor 120. set_floor is the raw setter (change_floor
// clamps upstream), and the obmap registers a body when it is placed, so
// each floor is set before the position.
TEST_F(SemAi2, collision_piles_clamp_the_floor_to_zero_through_one_hundred_twenty)
{
    world().set_floor_count(2);
    struct Case
    {
        const char* name;
        short prober_floor;
        short body_floor;
        bool passable;
    };
    const Case cases[] = {
        {"same floor: blocked", 0, 0, false},
        {"different floors: passable", 1, 0, true},
        {"floor -2 shares floor 0's pile", -2, 0, false},
        {"floor 121 shares floor 120's pile", 121, 120, false},
        {"floor 125 does not share floor 0's", 125, 0, true},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.name);
        fresh_world();
        world().set_floor_count(2);
        walker* a = world().add_ob(Order::Living, FAMILY_SOLDIER);
        walker* b = world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_TRUE(a && b);
        b->set_team_num(1);
        a->set_floor(c.prober_floor);
        b->set_floor(c.body_floor);
        place(a, 300, 300);
        place(b, 100, 100);
        EXPECT_EQ(c.passable, world().query_object_passable(100, 100, a));
    }
}

// obmap.cpp:134. A probe whose box spans several 32 px cells scans them
// column by column and stops at the first cell that blocks: food in a later
// cell is not eaten on a blocked probe; food in an earlier cell is.
TEST_F(SemAi2, a_blocked_probe_stops_at_the_first_blocking_cell_and_eats_nothing_beyond_it)
{
    // The probe box: x 24..40, y 100..116 (cells x 0 and 1, row 3).
    for (bool food_first : {false, true}) {
        SCOPED_TRACE(food_first ? "food in cell 0, the orc in cell 1"
                                : "the orc in cell 0, food in cell 1");
        fresh_world();
        living* s = living_at(FAMILY_SOLDIER, 0, 400, 400);
        ASSERT_NE(nullptr, s);
        s->stats()->set_hitpoints(100.0f);  // hungry
        walker* orc = living_at(FAMILY_ORC, 1, food_first ? 38 : 10, 100);
        walker* food = treasure_at(FAMILY_DRUMSTICK, food_first ? 20 : 34, 104, 8, 8);
        ASSERT_TRUE(orc && food);
        food->stats()->set_level(1);
        ASSERT_EQ(food_first ? 0 : 1, obmap::hash(food->xpos()));
        ASSERT_EQ(food_first ? 1 : 0, obmap::hash(orc->xpos()));
        ASSERT_EQ(obmap::hash(static_cast<short>(orc->xpos() + orc->sizex())),
                  obmap::hash(orc->xpos())) << "the orc fills one column";
        world().rng_.state_ = 31337u;
        og::sim::SimRandom roll(31337u);
        const float healed = 100.0f + 10.0f + static_cast<float>(roll.next(10));

        EXPECT_FALSE(world().query_object_passable(24, 100, s)) << "the orc blocks";

        if (food_first) {
            EXPECT_EQ(1, food->dead()) << "food in the cell before the block: eaten";
            EXPECT_FLOAT_EQ(healed, s->stats()->hitpoints());
        } else {
            EXPECT_EQ(0, food->dead()) << "food beyond the block: not eaten";
            EXPECT_FLOAT_EQ(100.0f, s->stats()->hitpoints());
            EXPECT_EQ(31337u, world().rng_.state_) << "no eat roll";
        }
    }
}

// obmap.cpp:222. A walker placed at negative coordinates is not registered:
// nothing bumps into it. Moved onto the map it is registered in every cell
// its box covers, and blocks there.
TEST_F(SemAi2, a_walker_at_negative_coordinates_is_not_in_the_collision_map)
{
    walker* a = world().add_ob(Order::Living, FAMILY_SOLDIER);
    living* b = living_at(FAMILY_ORC, 1, 200, 200);
    ASSERT_TRUE(a && b);
    a->set_sizex(16);
    a->set_sizey(16);
    a->setxy(-8, 40);
    EXPECT_EQ(0u, world().myobmap->walker_to_pos.count(a)) << "x < 0: not registered";
    EXPECT_TRUE(world().query_object_passable(0, 40, b)) << "x < 0: it blocks nothing";
    a->setxy(40, -8);
    EXPECT_EQ(0u, world().myobmap->walker_to_pos.count(a)) << "y < 0: not registered";
    EXPECT_TRUE(world().query_object_passable(40, 0, b)) << "y < 0: it blocks nothing";

    // On the map: x 24..40 covers columns 0 and 1, y 40..56 row 1 only.
    a->setxy(24, 40);
    ASSERT_EQ(1u, world().myobmap->walker_to_pos.count(a));
    EXPECT_EQ(2u, world().myobmap->walker_to_pos.at(a).size()) << "on the map: both covered cells";
    EXPECT_FALSE(world().query_object_passable(38, 40, b)) << "it blocks a probe in column 1 alone";
    EXPECT_FALSE(world().query_object_passable(10, 40, b)) << "and one in column 0 alone";
}

// obmap.cpp:306. Pile lookups by (x, y, floor) clamp the floor as a walker's
// registration does: floor -3 reads floor 0's pile, floor 200 reads floor
// 120's.
TEST_F(SemAi2, pile_lookups_clamp_the_floor_like_registration_does)
{
    world().set_floor_count(2);
    walker* low = world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* high = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(low && high);
    low->set_floor(0);
    high->set_floor(120);
    place(low, 100, 100);
    place(high, 100, 100);
    const auto holds = [](const std::list<walker*>* pile, const walker* w) {
        return pile != nullptr && std::find(pile->begin(), pile->end(), w) != pile->end();
    };
    const std::list<walker*>* below = world().myobmap->find_list(100, 100, -3);
    EXPECT_TRUE(holds(below, low)) << "floor -3: floor 0's pile";
    EXPECT_FALSE(holds(below, high));
    const std::list<walker*>* above = world().myobmap->find_list(100, 100, 200);
    EXPECT_TRUE(holds(above, high)) << "floor 200: floor 120's pile";
    EXPECT_FALSE(holds(above, low));
    EXPECT_TRUE(holds(world().myobmap->find_list(100, 100, 0), low)) << "in range: its own pile";
    EXPECT_TRUE(holds(&world().myobmap->obmap_get_list(100, 100, -3), low))
        << "obmap_get_list clamps the same way";
}

// obmap.cpp:379. A dead walker never blocks, is never met (no collide()).
TEST_F(SemAi2, a_dead_walker_never_blocks_a_probe)
{
    living* a = living_at(FAMILY_SOLDIER, 0, 100, 100);
    living* b = living_at(FAMILY_ORC, 1, 114, 100);
    ASSERT_TRUE(a && b);
    EXPECT_FALSE(world().query_object_passable(101, 100, a)) << "control: alive, it blocks";
    EXPECT_EQ(b, a->collide_ob());
    a->set_collide_ob(nullptr);
    b->set_dead(1);  // still registered in the obmap
    ASSERT_EQ(1u, world().myobmap->walker_to_pos.count(b));
    EXPECT_TRUE(world().query_object_passable(101, 100, a)) << "dead: passed through";
    EXPECT_EQ(nullptr, a->collide_ob()) << "dead: not met";
}

// obmap.cpp:389. Two hostile projectiles meeting fly through each other when
// the loud probe's roll is 4..9 (one draw); 0..3 they collide (pinned in
// QuietProbe).
TEST_F(SemAi2, hostile_projectiles_pass_each_other_on_a_roll_of_four_to_nine)
{
    living* archer = living_at(FAMILY_ELF, 1, 200, 200);
    ASSERT_NE(nullptr, archer);
    walker* rock = weapon_at(FAMILY_ROCK, 1, 300, 300);
    walker* arrow = weapon_at(FAMILY_ARROW, 2, 302, 300);
    ASSERT_TRUE(rock && arrow);
    rock->set_owner(archer);
    for (std::uint32_t want : {4u, 9u}) {
        SCOPED_TRACE("roll " + std::to_string(want));
        const std::uint32_t st = state_where({{10, want}});
        world().rng_.state_ = st;
        EXPECT_TRUE(world().query_passable(300.0f, 300.0f, rock)) << "a miss: they pass";
        EXPECT_EQ(state_after(st, {10}), world().rng_.state_) << "one roll";
        EXPECT_EQ(0, arrow->dead());
        EXPECT_EQ(0, rock->dead());
    }
}

// obmap.cpp:392. A projectile never hits or eats treasure: a hurt arrow over
// a drumstick passes it and the food stays.
TEST_F(SemAi2, a_projectile_passes_over_treasure_and_eats_none)
{
    walker* arrow = weapon_at(FAMILY_ARROW, 1, 320, 320);
    ASSERT_NE(nullptr, arrow);
    arrow->stats()->set_max_hitpoints(10.0f);
    arrow->stats()->set_hitpoints(5.0f);  // "hungry", should food apply to it
    walker* food = treasure_at(FAMILY_DRUMSTICK, 322, 322, 8, 8);
    ASSERT_NE(nullptr, food);
    food->stats()->set_level(1);
    world().rng_.state_ = 999u;
    EXPECT_TRUE(world().query_passable(321.0f, 320.0f, arrow)) << "the food never blocks";
    EXPECT_EQ(0, food->dead()) << "weapon over treasure: not eaten";
    EXPECT_FLOAT_EQ(5.0f, arrow->stats()->hitpoints());
    EXPECT_EQ(999u, world().rng_.state_) << "no eat roll";
    EXPECT_EQ(nullptr, arrow->collide_ob());
}
