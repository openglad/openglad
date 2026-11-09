/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch walker-2: the rules the review found in walker.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
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

// The first sim RNG state from which the draws come out as `draws` says.
[[maybe_unused]] std::uint32_t state_where(std::initializer_list<Draw> draws)
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
[[maybe_unused]] std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// walker's act routines are protected; a member pointer formed through a
// derived class reaches them on any walker (never instantiated).
struct WalkerActs : walker
{
    static bool guard(walker& w) { return (w.*(&WalkerActs::act_guard))(); }
    static bool random(walker& w) { return (w.*(&WalkerActs::act_random))(); }
    static bool generate(walker& w) { return (w.*(&WalkerActs::act_generate))(); }
    static bool fire(walker& w) { return (w.*(&WalkerActs::act_fire))(); }
    static bool query_reach(walker& w, short x, short y, std::int32_t& r)
    {
        return (w.*(&WalkerActs::query_prospective_weapon_reach))(x, y, r);
    }
};

using Denial = walker::FireCheckDenial;

class SemWalker2 : public ::testing::Test {
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
    void fresh_world()
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int tx, int ty, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(tx + world().grid.w * ty)] = pix;
    }

    // Paints the cells [tx0..tx1] x [ty0..ty1] with water.
    void water(int tx0, int ty0, int tx1, int ty1)
    {
        for (int ty = ty0; ty <= ty1; ++ty)
            for (int tx = tx0; tx <= tx1; ++tx)
                tile(tx, ty, PIX_WATER1);
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

    // A level-4 bot (ACT_SIT, no seat), 400 hp, 500 MP, no regeneration.
    walker* living_at(int family, unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->set_act_type(ACT_SIT);
        w->stats()->set_level(4);
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(500.0f);
        w->stats()->set_magicpoints(500.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_bit_flags(BIT_NO_RANGED, 0);
        w->set_busy(0.0f);
        return place(w, x, y);
    }

    walker* generator_at(int family, unsigned char team, short x, short y)
    {
        walker* w = world().add_ob(Order::Generator, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->stats()->set_level(3);
        return place(w, x, y, w->sizex(), w->sizey());
    }

    // The reach of the shot `shooter` would really fire along (hx, hy):
    // a live create_weapon's stepsize * lineofsight, the weapon then dropped.
    std::int32_t real_reach(walker* shooter, float hx, float hy)
    {
        const float ox = shooter->lastx();
        const float oy = shooter->lasty();
        shooter->set_lastx(hx);
        shooter->set_lasty(hy);
        walker* shot = shooter->create_weapon();
        shooter->set_lastx(ox);
        shooter->set_lasty(oy);
        if (shot == nullptr)
            return -1;
        const std::int32_t reach = static_cast<std::int32_t>(shot->stepsize()) *
                                   static_cast<std::int32_t>(shot->lineofsight());
        shot->set_dead(1);
        return reach;
    }

    static void face(walker* w, int dir)
    {
        w->set_curdir(static_cast<signed char>(dir));
        w->set_enddir(static_cast<char>(dir));
    }

    int count_events(og::sim::EventKind kind, std::uint32_t a)
    {
        int n = 0;
        for (const auto& ev : tw_->events.events())
            if (ev.kind == kind && ev.a == a)
                ++n;
        return n;
    }

    int count_live(const decltype(GameWorld::oblist)& list, Order order, int family)
    {
        int n = 0;
        for (const auto& uptr : list)
        {
            const walker* ob = uptr.get();
            if (ob != nullptr && !ob->dead() && ob->query_order() == order &&
                ob->family() == family)
                ++n;
        }
        return n;
    }

    bool registered(walker* w)
    {
        return world().myobmap->walker_to_pos.find(w) !=
               world().myobmap->walker_to_pos.end();
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

[[maybe_unused]] Front front_of(const walker* w)
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

// Runs `body` with the world pointer of the active gameplay context taken
// away, then puts it back.
template <typename F>
void without_world(F&& body)
{
    GameWorld* const saved = current_game->world;
    current_game->world = nullptr;
    body();
    current_game->world = saved;
}

}  // namespace

// ---------------------------------------------------------------------------
// The reach a bot predicts for itself (walker.cpp:1218-1347)
// ---------------------------------------------------------------------------

// walker.cpp:1218 weapon_profile_requires_hook. A living whose family has
// no descriptor at all is Lua-only by construction: its reach cannot be
// predicted, reported as 0, with no draw and no scratch projectile.
TEST_F(SemWalker2, a_family_with_no_descriptor_has_no_predictable_reach)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_KNIFE);
    const std::int32_t plain = archer->prospective_weapon_reach(1, 0);
    ASSERT_EQ(real_reach(archer, 1.0f, 0.0f), plain)
        << "control: a plain family predicts its real knife reach";
    ASSERT_NE(0, plain);

    constexpr int kNoFamily = 100;
    ASSERT_EQ(nullptr, get_family_descriptor(kNoFamily));
    archer->set_family(static_cast<std::uint8_t>(kNoFamily));
    const std::uint32_t rng_before = world().rng_.state_;
    const std::size_t weapons_before = world().weaplist.size();
    std::int32_t reach = -7;
    EXPECT_FALSE(WalkerActs::query_reach(*archer, 1, 0, reach))
        << "no descriptor: the reach is not predictable";
    EXPECT_EQ(0, reach) << "no descriptor: reach reported as 0";
    EXPECT_EQ(rng_before, world().rng_.state_) << "no descriptor: no draw";
    EXPECT_EQ(weapons_before, world().weaplist.size())
        << "no descriptor: no scratch projectile";
}

// walker.cpp:1257 water_blocks_mover. Water stops a walking bot's planned
// approach; a family that flies, or swims, plans straight across it.
TEST_F(SemWalker2, water_stops_a_planned_approach_except_for_fliers_and_swimmers)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 480);
    walker* target = living_at(FAMILY_ORC, 1, 640, 480);
    ASSERT_TRUE(archer && target);
    archer->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_bit_flags(BIT_FLYING, 0);
    archer->stats()->set_bit_flags(BIT_SWIMMING, 0);
    archer->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    ASSERT_EQ(0, archer->flight_left());
    water(16, 0, 30, 63);  // x 256..495, every row: a river between them
    ASSERT_GT(640 - 160, archer->prospective_weapon_reach(1, 0))
        << "the target starts out of reach: the walk decides";

    EXPECT_FALSE(archer->can_approach_weapon_range(target))
        << "otherwise: the river stops the walker";

    archer->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_TRUE(archer->can_approach_weapon_range(target))
        << "BIT_FLYING family flag: the flier is not blocked";
    archer->stats()->set_bit_flags(BIT_FLYING, 0);

    archer->stats()->set_bit_flags(BIT_SWIMMING, 1);
    EXPECT_TRUE(archer->can_approach_weapon_range(target))
        << "swimming: the swimmer is not blocked";
    archer->stats()->set_bit_flags(BIT_SWIMMING, 0);

    EXPECT_FALSE(archer->can_approach_weapon_range(target))
        << "otherwise again: both flags cleared";
}

// walker.cpp:1275 query_prospective_weapon_reach. Only a living's reach is
// predicted: a generator, or a world that is gone, answers false with 0.
TEST_F(SemWalker2, only_a_living_in_a_world_has_a_predictable_reach)
{
    walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
    ASSERT_NE(nullptr, tower);
    ASSERT_EQ(Order::Generator, tower->query_order());
    std::int32_t reach = -7;
    EXPECT_FALSE(WalkerActs::query_reach(*tower, 1, 0, reach))
        << "non-living: not predictable";
    EXPECT_EQ(0, reach) << "non-living: reach 0";

    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_KNIFE);
    std::int32_t live_reach = -7;
    ASSERT_TRUE(WalkerActs::query_reach(*archer, 1, 0, live_reach));
    ASSERT_EQ(real_reach(archer, 1.0f, 0.0f), live_reach) << "control";

    bool answered = true;
    reach = -7;
    without_world([&] { answered = WalkerActs::query_reach(*archer, 1, 0, reach); });
    EXPECT_FALSE(answered) << "no world: not predictable";
    EXPECT_EQ(0, reach) << "no world: reach 0";
}

// walker.cpp:1304 can_approach_weapon_range. A bot plans a shot only at an
// objective that is there to be shot: one that exists, is awake and not
// hidden; and only a living in a world plans at all.
TEST_F(SemWalker2, a_bot_plans_no_shot_at_an_absent_dormant_or_hidden_objective)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    walker* target = living_at(FAMILY_ORC, 1, 400, 160);
    walker* tower = generator_at(FAMILY_TOWER, 0, 160, 400);
    ASSERT_TRUE(archer && target && tower);
    archer->set_current_weapon(FAMILY_KNIFE);
    ASSERT_TRUE(archer->can_approach_weapon_range(target))
        << "otherwise: open grass, the walk reaches knife range";

    EXPECT_FALSE(archer->can_approach_weapon_range(nullptr)) << "objective null";

    target->set_dormant(true);
    EXPECT_FALSE(archer->can_approach_weapon_range(target)) << "objective dormant";
    target->set_dormant(false);
    ASSERT_TRUE(archer->can_approach_weapon_range(target)) << "woken again";

    target->set_hidden(true);
    ASSERT_TRUE(target->hidden());
    EXPECT_FALSE(archer->can_approach_weapon_range(target)) << "objective hidden";
    target->set_hidden(false);
    ASSERT_TRUE(archer->can_approach_weapon_range(target)) << "revealed again";

    target->setxy(tower->xpos(), tower->ypos());
    EXPECT_FALSE(tower->can_approach_weapon_range(target))
        << "self not living: a generator plans no approach, even on top of it";

    bool answered = true;
    without_world([&] { answered = archer->can_approach_weapon_range(target); });
    EXPECT_FALSE(answered) << "no world (defensive)";
}

// walker.cpp:1335/1337. The planned approach walks the way a bot walks at
// a target: more than 3:1 along one axis it goes straight along that axis,
// otherwise diagonally. Water placed where the wrong path would go tells
// the three apart: the right path stays dry and reaches range.
TEST_F(SemWalker2, a_planned_approach_goes_straight_past_three_to_one_and_diagonal_otherwise)
{
    // |dx| > 3|dy|: target 400 right, 64 down. Straight east along row 30
    // until the 3:1 cone; water in row 31 under the first 176 px of it.
    {
        fresh_world();
        walker* archer = living_at(FAMILY_ARCHER, 0, 160, 480);
        walker* target = living_at(FAMILY_ORC, 1, 560, 544);
        ASSERT_TRUE(archer && target);
        archer->set_current_weapon(FAMILY_KNIFE);
        water(10, 31, 20, 31);
        EXPECT_TRUE(archer->can_approach_weapon_range(target))
            << "|dx| > 3|dy|: horizontal steps stay on the dry row";
    }
    // |dy| > 3|dx|: target 64 right, 400 up. Straight north up column 10;
    // water in column 11 beside the first 208 px of it.
    {
        fresh_world();
        walker* archer = living_at(FAMILY_ARCHER, 0, 160, 480);
        walker* target = living_at(FAMILY_ORC, 1, 224, 80);
        ASSERT_TRUE(archer && target);
        archer->set_current_weapon(FAMILY_KNIFE);
        water(11, 18, 11, 30);
        EXPECT_TRUE(archer->can_approach_weapon_range(target))
            << "|dy| > 3|dx|: vertical steps stay in the dry column";
    }
    // else: target 300 right, 300 up. Diagonal; water on row 30 east of
    // x 208, where a straight east walk would go.
    {
        fresh_world();
        walker* archer = living_at(FAMILY_ARCHER, 0, 160, 480);
        walker* target = living_at(FAMILY_ORC, 1, 460, 180);
        ASSERT_TRUE(archer && target);
        archer->set_current_weapon(FAMILY_KNIFE);
        water(13, 30, 40, 31);
        EXPECT_TRUE(archer->can_approach_weapon_range(target))
            << "else: diagonal steps climb clear of the row-30 water";
        // control: the same water across the diagonal path refuses it.
        water(13, 20, 40, 31);
        EXPECT_FALSE(archer->can_approach_weapon_range(target))
            << "control: water on the diagonal path is a shoreline";
    }
}

// walker.cpp:1342-1347. Range is judged with the shot the bot would take
// along each step's heading: a cardinal heading with the cardinal profile
// (longer: the circular scaling), a diagonal one with the diagonal profile.
TEST_F(SemWalker2, a_planned_shot_uses_the_cardinal_or_diagonal_profile_of_its_heading)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 480, 480);
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_ARROW);
    const std::int32_t cardinal = archer->prospective_weapon_reach(1, 0);
    const std::int32_t diagonal = archer->prospective_weapon_reach(1, 1);
    ASSERT_EQ(real_reach(archer, 1.0f, 0.0f), cardinal);
    ASSERT_EQ(real_reach(archer, 1.0f, 1.0f), diagonal);
    ASSERT_LT(diagonal, cardinal);
    const std::int32_t half = cardinal / 2;
    ASSERT_LT(diagonal, 2 * half) << "a diagonal target at Manhattan 2*half sits between the reaches";
    // Water everywhere but the archer's own cell: it cannot take a step,
    // so only the range at its feet decides.
    water(0, 0, 63, 63);
    tile(30, 30, PIX_GRASS1);

    walker* target = living_at(FAMILY_ORC, 1, static_cast<short>(480 + half),
                               static_cast<short>(480 + half));
    ASSERT_NE(nullptr, target);
    EXPECT_FALSE(archer->can_approach_weapon_range(target))
        << "diagonal heading: Manhattan " << 2 * half
        << " is beyond the diagonal reach " << diagonal;

    target->setxy(static_cast<short>(480 + cardinal), short{480});
    EXPECT_TRUE(archer->can_approach_weapon_range(target))
        << "cardinal heading: the same distance is within the cardinal reach";
    target->setxy(static_cast<short>(480 + cardinal + 1), short{480});
    EXPECT_FALSE(archer->can_approach_weapon_range(target))
        << "cardinal heading: one pixel past the cardinal reach";
}

// walker.cpp:1368. A walking bot's full step that would wet its feet falls
// back to the one-pixel step walkstep takes; the bot creeps to the very
// shoreline, so an objective within reach of the bank is approachable.
TEST_F(SemWalker2, a_planned_approach_creeps_one_pixel_to_the_shoreline)
{
    for (int gap : {2, 0}) {
        SCOPED_TRACE("dry pixels before the water: " + std::to_string(gap));
        fresh_world();
        water(20, 0, 40, 63);  // the river starts at x 320
        const short x0 = static_cast<short>(320 - 16 - gap);
        walker* archer = living_at(FAMILY_ARCHER, 0, x0, 480);
        ASSERT_NE(nullptr, archer);
        archer->set_current_weapon(FAMILY_KNIFE);
        archer->set_stepsize(4.0f);
        const std::int32_t reach = archer->prospective_weapon_reach(1, 0);
        ASSERT_GT(reach, 0);
        // Out of reach by exactly the gap: only the creep can close it.
        walker* target = living_at(FAMILY_ORC, 1, static_cast<short>(x0 + reach + 2), 480);
        ASSERT_NE(nullptr, target);
        if (gap == 2) {
            EXPECT_TRUE(archer->can_approach_weapon_range(target))
                << "full step wet, one-pixel step dry: the bot creeps 2 px into range";
        } else {
            EXPECT_FALSE(archer->can_approach_weapon_range(target))
                << "control: one pixel is wet too, the shoreline vetoes";
        }
    }
}

// walker.cpp:1386 create_weapon. A living's shot is an Order::Weapon of its
// current_weapon family; the shooter's heading picks the profile: cardinal
// headings get the circular scaling (stepsize x 362/256, line of sight x
// 309/256 in integer math, as the old game's create_weapon), diagonals do not.
TEST_F(SemWalker2, a_living_shoots_its_current_weapon_scaled_by_its_heading)
{
    walker* archer = living_at(FAMILY_ARCHER, 2, 160, 160);
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_ARROW);

    archer->set_lastx(1.0f);
    archer->set_lasty(0.0f);
    walker* east = archer->create_weapon();
    ASSERT_NE(nullptr, east);
    EXPECT_EQ(Order::Weapon, east->query_order()) << "order: a weapon";
    EXPECT_EQ(FAMILY_ARROW, east->family()) << "family: current_weapon";
    EXPECT_EQ(archer, east->owner());
    EXPECT_EQ(2, east->team_num());

    archer->set_lastx(1.0f);
    archer->set_lasty(1.0f);
    walker* diag = archer->create_weapon();
    ASSERT_NE(nullptr, diag);
    EXPECT_EQ(Order::Weapon, diag->query_order());
    EXPECT_EQ(FAMILY_ARROW, diag->family());

    archer->set_current_weapon(FAMILY_KNIFE);
    walker* knife = archer->create_weapon();
    ASSERT_NE(nullptr, knife);
    EXPECT_EQ(FAMILY_KNIFE, knife->family()) << "family follows current_weapon";

    EXPECT_FLOAT_EQ(diag->stepsize() * 362.0f / 256.0f, east->stepsize())
        << "heading: a cardinal heading scales the stepsize by 362/256";
    EXPECT_EQ(diag->lineofsight() * 309 / 256, east->lineofsight())
        << "heading: and stretches the line of sight by 309/256 (integer)";
    east->set_dead(1);
    diag->set_dead(1);
    knife->set_dead(1);
}

// ---------------------------------------------------------------------------
// fire_check: the gates a shot passes before it flies (walker.cpp:1452-1535)
// ---------------------------------------------------------------------------

// walker.cpp:1452. A generator's fire_check always passes, and it builds no
// probe: no weapon and no troop is created, and the denial is None.
TEST_F(SemWalker2, a_generator_always_passes_fire_check_without_building_a_probe)
{
    walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
    ASSERT_NE(nullptr, tower);
    ASSERT_EQ(nullptr, tower->foe()) << "no foe anywhere";
    const std::size_t obs_before = world().oblist.size();
    const std::size_t weapons_before = world().weaplist.size();
    const std::size_t fx_before = world().fxlist.size();
    Denial why = Denial::NoFoe;
    EXPECT_TRUE(tower->fire_check(0, 0, &why)) << "generator: always passes";
    EXPECT_EQ(Denial::None, why) << "generator: denial None";
    EXPECT_EQ(obs_before, world().oblist.size()) << "generator: no troop built";
    EXPECT_EQ(weapons_before, world().weaplist.size()) << "generator: no probe weapon";
    EXPECT_EQ(fx_before, world().fxlist.size());
}

// walker.cpp:1482. The reach gate runs first: a foe beyond reach is
// OutOfRange even for a shooter that may not shoot at all or cannot pay.
TEST_F(SemWalker2, a_foe_beyond_reach_is_out_of_range_before_any_other_denial)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    ASSERT_NE(nullptr, archer);
    archer->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_weapon_cost(0);
    archer->face_delta(1, 0);
    const std::int32_t reach = real_reach(archer, 1.0f, 0.0f);
    walker* orc = living_at(FAMILY_ORC, 1, static_cast<short>(160 + reach + 40), 160);
    ASSERT_NE(nullptr, orc);
    archer->set_foe(orc);

    Denial why = Denial::None;
    archer->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    EXPECT_FALSE(archer->fire_check(1, 0, &why));
    EXPECT_EQ(Denial::OutOfRange, why)
        << "foe beyond reach, shooter NO_RANGED: OutOfRange wins";
    archer->stats()->set_bit_flags(BIT_NO_RANGED, 0);

    archer->stats()->set_weapon_cost(9999);
    archer->stats()->set_magicpoints(0.0f);
    why = Denial::None;
    EXPECT_FALSE(archer->fire_check(1, 0, &why));
    EXPECT_EQ(Denial::OutOfRange, why)
        << "foe beyond reach, shooter mana-short: OutOfRange wins";
}

// walker.cpp:1489. A NO_RANGED shooter with its foe in reach and faced is
// denied with NoRanged.
TEST_F(SemWalker2, a_no_ranged_shooter_is_denied_no_ranged_with_its_foe_in_reach)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    walker* orc = living_at(FAMILY_ORC, 1, 184, 160);
    ASSERT_TRUE(archer && orc);
    archer->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_weapon_cost(0);
    archer->face_delta(1, 0);
    archer->set_foe(orc);
    Denial why = Denial::NoFoe;
    ASSERT_TRUE(archer->fire_check(1, 0, &why)) << "control: the shot is good";
    ASSERT_EQ(Denial::None, why);

    archer->stats()->set_bit_flags(BIT_NO_RANGED, 1);
    why = Denial::None;
    EXPECT_FALSE(archer->fire_check(1, 0, &why));
    EXPECT_EQ(Denial::NoRanged, why) << "NO_RANGED set, foe in reach: denial NoRanged";
}

// walker.cpp:1496. A shooter whose weapon costs more than its magic is
// denied with NoMagic; exactly enough magic pays.
TEST_F(SemWalker2, a_shot_costing_more_than_the_magic_left_is_denied_no_magic)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    walker* orc = living_at(FAMILY_ORC, 1, 184, 160);
    ASSERT_TRUE(archer && orc);
    archer->set_current_weapon(FAMILY_KNIFE);
    archer->face_delta(1, 0);
    archer->set_foe(orc);
    archer->stats()->set_weapon_cost(7);

    archer->stats()->set_magicpoints(6.0f);
    Denial why = Denial::None;
    EXPECT_FALSE(archer->fire_check(1, 0, &why));
    EXPECT_EQ(Denial::NoMagic, why) << "weapon_cost > mp: denial NoMagic";

    archer->stats()->set_magicpoints(7.0f);
    why = Denial::NoFoe;
    EXPECT_TRUE(archer->fire_check(1, 0, &why)) << "weapon_cost == mp: affordable";
    EXPECT_EQ(Denial::None, why);
}

// walker.cpp:1518/1535. An ignored foe (out of the obmap) can still be named
// as the objective of a shot, but not while it is hidden: the ray then
// misses it.
TEST_F(SemWalker2, a_hidden_ignored_foe_is_not_hit_by_the_fire_check_ray)
{
    walker* archer = living_at(FAMILY_ARCHER, 0, 160, 160);
    walker* orc = living_at(FAMILY_ORC, 1, 184, 160);
    ASSERT_TRUE(archer && orc);
    archer->set_current_weapon(FAMILY_KNIFE);
    archer->stats()->set_weapon_cost(0);
    archer->face_delta(1, 0);
    orc->set_ignore(1);
    world().myobmap->remove(orc);
    ASSERT_FALSE(registered(orc));
    archer->set_foe(orc);
    Denial why = Denial::NoFoe;
    ASSERT_TRUE(archer->fire_check(1, 0, &why))
        << "control: the ignored, visible, hostile foe is hit by its box";
    ASSERT_EQ(Denial::None, why);

    orc->set_hidden(true);
    ASSERT_TRUE(orc->hidden());
    ASSERT_FALSE(registered(orc));
    why = Denial::None;
    EXPECT_FALSE(archer->fire_check(1, 0, &why)) << "foe ignored but hidden: no hit";
    EXPECT_EQ(Denial::RayMiss, why) << "foe ignored but hidden: RayMiss";
}

// ---------------------------------------------------------------------------
// act_generate (walker.cpp:1578-1584)
// ---------------------------------------------------------------------------

// walker.cpp:1578. A generator's emission roll draws twice even when it
// fails, and emits nothing; at the object cap it draws nothing at all.
TEST_F(SemWalker2, a_failed_emission_roll_still_draws_twice_and_a_full_world_draws_none)
{
    walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
    ASSERT_NE(nullptr, tower);
    tower->stats()->set_max_hitpoints(300.0f);
    tower->stats()->set_hitpoints(100.0f);
    tower->set_busy(0.0f);
    ASSERT_EQ(ANI_WALK, tower->ani_type());
    const auto n = static_cast<std::uint32_t>(world().living_count);
    ASSERT_LT(world().living_count, MAXOBS);

    // Roll fails: next(9) == 0 can never beat next(300 + 8n).
    const std::uint32_t fail = state_where({{9, 0}, {300 + 8 * n, kAny}});
    world().rng_.state_ = fail;
    WalkerActs::generate(*tower);
    EXPECT_EQ(state_after(fail, {9, 300 + 8 * n}), world().rng_.state_)
        << "roll fails: RNG: exactly the two draws are consumed";
    EXPECT_EQ(ANI_WALK, tower->ani_type()) << "roll fails: nothing emitted";
    EXPECT_FLOAT_EQ(100.0f, tower->stats()->hitpoints()) << "roll fails: no regen";

    // Control: a roll that succeeds emits (init_fire) and regenerates.
    const std::uint32_t win =
        state_where({{9, 8}, {300 + 8 * n, 0}, {3, 0}, {3, 1}});
    world().rng_.state_ = win;
    WalkerActs::generate(*tower);
    EXPECT_EQ(ANI_ATTACK, tower->ani_type()) << "control: the roll succeeds and fires";
    EXPECT_FLOAT_EQ(101.0f, tower->stats()->hitpoints()) << "control: +1 hp";

    // living_count >= MAXOBS: no draws, nothing emitted.
    tower->set_ani_type(ANI_WALK);
    tower->set_busy(0.0f);
    const int saved = world().living_count;
    world().living_count = MAXOBS;
    world().rng_.state_ = win;
    for (int i = 0; i < 20; ++i)
        WalkerActs::generate(*tower);
    world().living_count = saved;
    EXPECT_EQ(win, world().rng_.state_) << "living_count >= MAXOBS: no draws at all";
    EXPECT_EQ(ANI_WALK, tower->ani_type()) << "living_count >= MAXOBS: nothing emitted";
    EXPECT_FLOAT_EQ(101.0f, tower->stats()->hitpoints());
}

// walker.cpp:1584. A generator emits in the direction it rolled; only the
// (0,0) roll falls back to facing right.
TEST_F(SemWalker2, a_generator_emits_in_the_direction_it_rolled)
{
    struct Case { std::uint32_t dx_draw, dy_draw; float lastx, lasty; const char* what; };
    const Case cases[] = {
        {0, 2, 1.0f, -1.0f, "any non-zero axis: rolled (1,-1)"},
        {2, 1, -1.0f, 0.0f, "any non-zero axis: rolled (-1,0)"},
        {1, 0, 0.0f, 1.0f, "any non-zero axis: rolled (0,1)"},
        {1, 1, 1.0f, 0.0f, "both axes rolled 0: falls back to (1,0)"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.what);
        fresh_world();
        walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
        ASSERT_NE(nullptr, tower);
        tower->set_busy(0.0f);
        const auto n = static_cast<std::uint32_t>(world().living_count);
        world().rng_.state_ =
            state_where({{9, 8}, {300 + 8 * n, 0}, {3, c.dx_draw}, {3, c.dy_draw}});
        WalkerActs::generate(*tower);
        EXPECT_FLOAT_EQ(c.lastx, tower->lastx()) << c.what;
        EXPECT_FLOAT_EQ(c.lasty, tower->lasty()) << c.what;
        EXPECT_EQ(ANI_ATTACK, tower->ani_type());
    }
}

// ---------------------------------------------------------------------------
// act_fire: a projectile's flight (walker.cpp:1604-1650)
// ---------------------------------------------------------------------------

namespace {

// Floor 1 of a two-floor world: grass of floor 0's size, `pix` at (tx,ty).
void floor1_grid(GameWorld& w, int tx, int ty, unsigned char pix)
{
    const int gw = w.grid.w;
    const int gh = w.grid.h;
    auto* buf = new unsigned char[static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)];
    std::fill(buf, buf + static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh),
              static_cast<unsigned char>(PIX_GRASS1));
    buf[tx + ty * gw] = pix;
    w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                    static_cast<unsigned char>(gh), buf);
    w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
}

}  // namespace

// A projectile of `family` at (x,y) on `floor`, flying east one step a tick.
walker* SemWalker2_shot(GameWorld& w, int family, short x, short y, short floor)
{
    walker* shot = w.add_ob(Order::Weapon, family);
    if (shot == nullptr)
        return nullptr;
    shot->set_team_num(1);
    shot->set_floor(floor);
    shot->setxy(x, y);
    shot->set_act_type(ACT_FIRE);
    shot->set_curdir(static_cast<signed char>(FACE_RIGHT));
    shot->set_lastx(4.0f);
    shot->set_lasty(0.0f);
    shot->set_lineofsight(40);
    shot->set_collide_ob(nullptr);
    return shot;
}

// walker.cpp:1604/1609. A lobbed projectile climbs by vz and loses gravity
// each tick; when it would sink below the ground it lands (height 0, no
// fall); a projectile at rest on the ground stays there.
TEST_F(SemWalker2, a_lobbed_rock_arcs_lands_and_then_stays_on_the_ground)
{
    walker* rock = SemWalker2_shot(world(), FAMILY_ROCK, 160, 160, 0);
    ASSERT_NE(nullptr, rock);
    const WeaponFamilyDescriptor* wfd = get_weapon_family_descriptor(FAMILY_ROCK);
    ASSERT_NE(nullptr, wfd);
    ASSERT_FLOAT_EQ(0.09f, wfd->gravity) << "the core rock's gravity";
    rock->set_worldz(0.0f);
    rock->set_vz(0.7f);  // the rock's launch speed (weapon-01-rock.lua vz)
    float z = 0.0f;
    float v = 0.7f;
    int landed_at = -1;
    for (int tick = 1; tick <= 20; ++tick) {
        WalkerActs::fire(*rock);
        ASSERT_FALSE(rock->dead()) << "tick " << tick;
        if (landed_at < 0) {
            z = z + v;
            v = v - wfd->gravity;
            if (z < 0.0f) {
                z = 0.0f;
                v = 0.0f;
                landed_at = tick;
                EXPECT_EQ(0.0f, rock->worldz())
                    << "worldz would go below 0: landed at height 0, tick " << tick;
                EXPECT_EQ(0.0f, rock->vz())
                    << "worldz would go below 0: vz zeroed, tick " << tick;
            } else {
                EXPECT_EQ(z, rock->worldz())
                    << "vz or worldz non-zero: worldz += vz, tick " << tick;
                EXPECT_EQ(v, rock->vz())
                    << "vz or worldz non-zero: vz -= gravity, tick " << tick;
            }
        } else {
            EXPECT_EQ(0.0f, rock->worldz()) << "flat weapon: stays on the ground, tick " << tick;
            EXPECT_EQ(0.0f, rock->vz()) << "flat weapon: no gravity at rest, tick " << tick;
        }
    }
    EXPECT_EQ(17, landed_at) << "the 0.7 / 0.09 arc lands on its 17th tick";
}

// walker.cpp:1615-1630. Only a projectile that can drop floors falls through
// an air hole, landing at height 0 on the floor below; one that cannot
// flies on; a single-floor level never drops anything.
TEST_F(SemWalker2, only_a_droppable_shot_falls_through_air_and_lands_at_height_0)
{
    world().set_floor_count(2);
    // A knife (can_drop_floors) on floor 1 centred over an air cell.
    {
        walker* knife = SemWalker2_shot(world(), FAMILY_KNIFE, 160, 160, 1);
        ASSERT_NE(nullptr, knife);
        ASSERT_TRUE(get_weapon_family_descriptor(FAMILY_KNIFE)->can_drop_floors);
        floor1_grid(world(), (160 + knife->sizex() / 2) / GRID_SIZE,
                    (160 + knife->sizey() / 2) / GRID_SIZE, PIX_AIR);
        knife->set_vz(0.35f);
        knife->set_worldz(0.0f);
        WalkerActs::fire(*knife);
        EXPECT_EQ(0, knife->floor()) << "droppable over air, floor 1: drops to floor 0";
        EXPECT_FALSE(knife->dead());
        EXPECT_EQ(0.0f, knife->worldz())
            << "droppable over air, floor > 0: worldz reset to 0 (was 0.35 after the climb)";
        knife->set_dead(1);
    }
    // An arrow (cannot drop floors) over the same kind of hole.
    {
        walker* arrow = SemWalker2_shot(world(), FAMILY_ARROW, 320, 320, 1);
        ASSERT_NE(nullptr, arrow);
        ASSERT_FALSE(get_weapon_family_descriptor(FAMILY_ARROW)->can_drop_floors);
        floor1_grid(world(), (320 + arrow->sizex() / 2) / GRID_SIZE,
                    (320 + arrow->sizey() / 2) / GRID_SIZE, PIX_AIR);
        WalkerActs::fire(*arrow);
        EXPECT_EQ(1, arrow->floor()) << "non-droppable family over air: stays on floor 1";
        EXPECT_FALSE(arrow->dead()) << "non-droppable family over air: flies on";
    }
}

TEST_F(SemWalker2, a_single_floor_level_drops_no_shot_through_an_air_tile)
{
    ASSERT_EQ(1, world().floor_count());
    walker* knife = SemWalker2_shot(world(), FAMILY_KNIFE, 160, 160, 0);
    ASSERT_NE(nullptr, knife);
    tile((160 + knife->sizex() / 2) / GRID_SIZE, (160 + knife->sizey() / 2) / GRID_SIZE, PIX_AIR);
    WalkerActs::fire(*knife);
    EXPECT_FALSE(knife->dead()) << "single-floor level: a knife over air on floor 0 lives";
    EXPECT_EQ(0, knife->floor()) << "single-floor level: no floor change";
}

// walker.cpp:1642. A projectile whose range has run out dies where it is:
// its last act does not walk.
TEST_F(SemWalker2, a_spent_projectile_dies_without_taking_its_step)
{
    walker* knife = SemWalker2_shot(world(), FAMILY_KNIFE, 160, 160, 0);
    ASSERT_NE(nullptr, knife);
    knife->set_lineofsight(1);
    WalkerActs::fire(*knife);
    ASSERT_FALSE(knife->dead()) << "control: range 1 left, the knife flies";
    ASSERT_EQ(164, knife->xpos()) << "control: it walked its 4 px";
    ASSERT_EQ(0, knife->lineofsight());

    WalkerActs::fire(*knife);
    EXPECT_TRUE(knife->dead()) << "lineofsight was 0: dead";
    EXPECT_EQ(164, knife->xpos()) << "lineofsight was 0: no walk";
    EXPECT_EQ(160, knife->ypos());
}

// walker.cpp:1647/1650. A shot stopped by a wall hurts nobody, not even a
// body standing in the wall's own step; a corpse in the path neither stops
// the shot nor is struck by it (a walk never meets a dead body, so the
// attack's own dead-target guard is a second line behind the obmap's).
TEST_F(SemWalker2, a_shot_stopped_by_a_wall_or_a_corpse_strikes_nothing)
{
    {
        walker* orc = living_at(FAMILY_ORC, 0, 176, 160);
        ASSERT_NE(nullptr, orc);
        tile(11, 10, PIX_H_WALL1);  // x 176..191: the orc stands in the wall cell
        walker* arrow = SemWalker2_shot(world(), FAMILY_ARROW,
                                        0, 160, 0);
        ASSERT_NE(nullptr, arrow);
        arrow->setxy(static_cast<short>(176 - arrow->sizex()), short{160});
        WalkerActs::fire(*arrow);
        EXPECT_TRUE(arrow->dead()) << "walk blocked by a wall: the arrow breaks";
        EXPECT_FLOAT_EQ(400.0f, orc->stats()->hitpoints())
            << "walk blocked by a wall: no attack";
        EXPECT_EQ(nullptr, arrow->collide_ob());
    }
    {
        fresh_world();
        walker* corpse = living_at(FAMILY_ORC, 0, 176, 160);
        ASSERT_NE(nullptr, corpse);
        corpse->stats()->set_hitpoints(30.0f);
        corpse->set_dead(1);
        ASSERT_TRUE(registered(corpse)) << "the body still stands in the obmap";
        walker* arrow = SemWalker2_shot(world(), FAMILY_ARROW, 0, 160, 0);
        ASSERT_NE(nullptr, arrow);
        arrow->setxy(static_cast<short>(176 - arrow->sizex()), short{160});
        const short x_before = arrow->xpos();
        WalkerActs::fire(*arrow);
        EXPECT_FLOAT_EQ(30.0f, corpse->stats()->hitpoints())
            << "a dead body in the path: no attack";
        EXPECT_FALSE(arrow->dead()) << "a dead body in the path: it does not stop the shot";
        EXPECT_EQ(x_before + 4, arrow->xpos()) << "a dead body in the path: the shot steps through";
        EXPECT_EQ(nullptr, arrow->collide_ob()) << "a dead body in the path: nothing met";
    }
}

// ---------------------------------------------------------------------------
// act_guard and act_random (walker.cpp:1674-1763)
// ---------------------------------------------------------------------------

// walker.cpp:1674/1722. A guard with a foe anywhere queues its fire command
// on exactly one rng(30) draw; with no foe it draws nothing.
TEST_F(SemWalker2, a_guard_draws_once_for_its_fire_command_and_never_without_a_foe)
{
    walker* guard = living_at(FAMILY_ORC, 1, 160, 160);
    ASSERT_NE(nullptr, guard);
    guard->set_act_type(ACT_GUARD);
    guard->set_guard_hold_post(true);
    guard->stats()->clear_command();

    const std::uint32_t s = 4242u;
    world().rng_.state_ = s;
    EXPECT_FALSE(WalkerActs::guard(*guard)) << "no foe: act_guard reports nothing to do";
    EXPECT_EQ(s, world().rng_.state_) << "no foe: RNG: no draw";
    EXPECT_FALSE(guard->stats()->has_commands()) << "no foe: no command queued";
    EXPECT_EQ(nullptr, guard->foe());

    walker* foe = living_at(FAMILY_SOLDIER, 0, 800, 800);
    ASSERT_NE(nullptr, foe);
    world().rng_.state_ = s;
    EXPECT_TRUE(WalkerActs::guard(*guard));
    EXPECT_EQ(state_after(s, {30}), world().rng_.state_)
        << "a foe exists anywhere: RNG: exactly one rng(30) draw";
    EXPECT_EQ(foe, guard->foe());
    const Front f = front_of(guard);
    EXPECT_EQ(COMMAND_FIRE, f.type);
    EXPECT_EQ(640, f.com1);
    EXPECT_EQ(640, f.com2);
}

// walker.cpp:1736. A plain walker's act_random re-picks the nearest foe only
// when its 1-in-70 roll hits 0 (or it has none); otherwise it keeps its foe.
TEST_F(SemWalker2, a_wandering_walker_keeps_its_foe_unless_the_one_in_seventy_roll_hits)
{
    for (std::uint32_t roll : {5u, 0u}) {
        SCOPED_TRACE("rng(70) = " + std::to_string(roll));
        fresh_world();
        walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
        walker* near = living_at(FAMILY_ORC, 0, 368, 320);
        walker* far = living_at(FAMILY_ORC, 0, 416, 320);
        ASSERT_TRUE(tower && near && far);
        tower->set_act_type(ACT_RANDOM);
        tower->set_foe(far);
        world().rng_.state_ = state_where({{70, roll}});
        WalkerActs::random(*tower);
        if (roll != 0)
            EXPECT_EQ(far, tower->foe()) << "otherwise: the preset (far) foe is kept";
        else
            EXPECT_EQ(near, tower->foe()) << "roll hits 0: the nearest foe is picked";
    }
}

// walker.cpp:1738. With no hostile anywhere a plain walker wanders: a
// COMMAND_WALK of 20 in a random direction (two rng(3)), foe stays null;
// with a hostile in sight it fires instead.
TEST_F(SemWalker2, a_wandering_walker_with_no_foe_queues_a_twenty_tick_random_walk)
{
    walker* tower = generator_at(FAMILY_TOWER, 1, 320, 320);
    ASSERT_NE(nullptr, tower);
    tower->set_act_type(ACT_RANDOM);
    tower->stats()->clear_command();
    const std::uint32_t s = state_where({{70, kAny}, {3, 2}, {3, 2}});
    world().rng_.state_ = s;
    WalkerActs::random(*tower);
    const Front f = front_of(tower);
    EXPECT_EQ(nullptr, tower->foe()) << "no hostile exists: foe stays null";
    EXPECT_EQ(1u, f.size) << "no hostile exists: one command queued";
    EXPECT_EQ(COMMAND_WALK, f.type) << "no hostile exists: a walk";
    EXPECT_EQ(20, f.count) << "no hostile exists: of 20 ticks";
    EXPECT_EQ(1, f.com1) << "no hostile exists: direction rng(3)-1";
    EXPECT_EQ(1, f.com2) << "no hostile exists: direction rng(3)-1";
    EXPECT_EQ(state_after(s, {70, 3, 3}), world().rng_.state_);

    walker* orc = living_at(FAMILY_ORC, 0, 352, 320);
    ASSERT_NE(nullptr, orc);
    tower->stats()->clear_command();
    tower->set_busy(0.0f);
    tower->set_lineofsight(4);  // a 64 px firing box: the orc is inside it
    WalkerActs::random(*tower);
    const Front g = front_of(tower);
    EXPECT_EQ(orc, tower->foe()) << "foe exists: acquired";
    EXPECT_EQ(COMMAND_FIRE, g.type) << "foe exists: the approach/fire logic runs, no wander";
    EXPECT_EQ(32, g.com1);
    EXPECT_EQ(0, g.com2);
}

// walker.cpp:1745-1763. A plain walker whose foe is outside its firing box
// does not fire: it steps one unit toward the foe on each axis (+1 / -1 /
// 0 when aligned), scaled by its stepsize.
TEST_F(SemWalker2, a_wandering_walker_steps_toward_a_foe_outside_its_firing_box)
{
    struct Case { short dx, dy; float lastx, lasty; const char* what; };
    const Case cases[] = {
        {200, -200, 2.0f, -2.0f, "foe east and north"},
        {-200, 200, -2.0f, 2.0f, "foe west and south"},
        {0, 200, 0.0f, 2.0f, "foe aligned on x, south"},
        {200, 0, 2.0f, 0.0f, "foe east, aligned on y"},
        {0, -200, 0.0f, -2.0f, "foe aligned on x, north"},
        {-200, 0, -2.0f, 0.0f, "foe west, aligned on y"},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.what);
        fresh_world();
        walker* tower = generator_at(FAMILY_TOWER, 1, 480, 480);
        walker* orc = living_at(FAMILY_ORC, 0, static_cast<short>(480 + c.dx),
                                static_cast<short>(480 + c.dy));
        ASSERT_TRUE(tower && orc);
        tower->set_act_type(ACT_RANDOM);
        tower->set_stepsize(2.0f);
        tower->set_lineofsight(2);  // a 32 px firing box
        tower->set_foe(orc);
        tower->stats()->clear_command();
        world().rng_.state_ = state_where({{70, 5}});
        WalkerActs::random(*tower);
        EXPECT_FALSE(tower->stats()->has_commands())
            << c.what << ": out of the box: no COMMAND_FIRE";
        EXPECT_EQ(ANI_WALK, tower->ani_type()) << c.what << ": out of the box: no init_fire";
        EXPECT_FLOAT_EQ(c.lastx, tower->lastx()) << c.what << ": x step";
        EXPECT_FLOAT_EQ(c.lasty, tower->lasty()) << c.what << ": y step";
    }
}

// ---------------------------------------------------------------------------
// spaces_clear, transfer_stats, transform_to (walker.cpp:1783-1876)
// ---------------------------------------------------------------------------

// walker.cpp:1783/1784. A neighbour spot blocked by a wall or by a body is
// not clear.
TEST_F(SemWalker2, a_neighbour_spot_blocked_by_a_wall_or_a_body_is_not_clear)
{
    walker* soldier = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, soldier);
    ASSERT_EQ(8, soldier->spaces_clear()) << "control: open grass";
    tile(11, 10, PIX_H_WALL1);  // the east neighbour (176,160)
    EXPECT_EQ(7, soldier->spaces_clear()) << "neighbour blocked by a wall";
    tile(11, 10, PIX_GRASS1);
    ASSERT_EQ(8, soldier->spaces_clear());
    walker* orc = living_at(FAMILY_ORC, 1, 160, 176);  // the south neighbour
    ASSERT_NE(nullptr, orc);
    EXPECT_EQ(7, soldier->spaces_clear()) << "neighbour blocked by a body";
}

// walker.cpp:1818 transfer_stats. The receiver gets its own copy of the
// character, half the magic regeneration and the raw freeze timer (thaw
// immunity included); a source with no character gives none.
TEST_F(SemWalker2, transferred_stats_copy_the_character_halve_magic_regen_and_keep_the_freeze)
{
    walker* a = living_at(FAMILY_SOLDIER, 0, 160, 160);
    walker* b = living_at(FAMILY_ORC, 0, 320, 160);
    ASSERT_TRUE(a && b);
    auto ann = std::make_unique<guy>(FAMILY_SOLDIER);
    ann->name = "Ann";
    ann->strength = 33;
    a->set_owned_myguy(std::move(ann));
    a->stats()->set_magic_per_round(4.0f);
    a->stats()->set_frozen_delay(-12);
    ASSERT_EQ(nullptr, b->myguy);

    a->transfer_stats(b);
    ASSERT_NE(nullptr, b->myguy) << "source has myguy: present";
    EXPECT_NE(a->myguy, b->myguy) << "source has myguy: a fresh copy, not an alias";
    EXPECT_EQ("Ann", b->myguy->name) << "source has myguy: same name";
    EXPECT_EQ(33, b->myguy->strength) << "source has myguy: same stats";
    EXPECT_FLOAT_EQ(2.0f, b->stats()->magic_per_round()) << "always: magic_per_round halved";
    EXPECT_EQ(-12, b->stats()->frozen_delay_raw())
        << "always: the raw frozen_delay (thaw immunity) transfers";

    walker* c = living_at(FAMILY_SOLDIER, 0, 160, 320);
    walker* d = living_at(FAMILY_ORC, 0, 320, 320);
    ASSERT_TRUE(c && d);
    ASSERT_EQ(nullptr, c->myguy);
    c->transfer_stats(d);
    EXPECT_EQ(nullptr, d->myguy) << "no myguy: newob keeps none";
}

// walker.cpp:1838-1876 transform_to. A transform into another order takes
// that order's act type; every transform keeps the walker's centre and
// clears the flags the old body carried.
TEST_F(SemWalker2, a_transform_keeps_the_centre_resets_the_flags_and_takes_a_new_orders_act)
{
    walker* fresh_knife = world().add_ob(Order::Weapon, FAMILY_KNIFE);
    walker* fresh_archer = world().add_ob(Order::Living, FAMILY_ARCHER);
    ASSERT_TRUE(fresh_knife && fresh_archer);
    const char knife_act = fresh_knife->act_type();
    const std::int32_t archer_flags = fresh_archer->stats()->bit_flags();
    ASSERT_NE(ACT_CONTROL, knife_act);
    ASSERT_EQ(0, archer_flags & BIT_FLYING);
    fresh_knife->set_dead(1);

    walker* soldier = place(living_at(FAMILY_SOLDIER, 0, 160, 160), 160, 160, 10, 10);
    ASSERT_NE(nullptr, soldier);
    soldier->set_act_type(ACT_CONTROL);
    soldier->transform_to(Order::Weapon, FAMILY_KNIFE);
    EXPECT_EQ(knife_act, soldier->act_type())
        << "different order: act_type from the new order's configuration";
    ASSERT_NE(10, soldier->sizex()) << "the knife body is a different size";
    ASSERT_NE(10, soldier->sizey());
    EXPECT_EQ(165, soldier->xpos() + soldier->sizex() / 2) << "always: centre x kept";
    EXPECT_EQ(165, soldier->ypos() + soldier->sizey() / 2) << "always: centre y kept";

    walker* flier = living_at(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, flier);
    flier->stats()->set_bit_flags(BIT_FLYING, 1);
    flier->transform_to(Order::Living, FAMILY_ARCHER);
    EXPECT_EQ(archer_flags, flier->stats()->bit_flags())
        << "always: bit flags are the new family's, the old BIT_FLYING is gone";
}

// ---------------------------------------------------------------------------
// death (walker.cpp:1950)
// ---------------------------------------------------------------------------

// walker.cpp:1936-1950. On a SAVE_ALL level a protected character's death
// ends the mission and leaves no corpse; every other death (unnamed,
// summoned, charmed, another colour, unflagged when flags exist) leaves its
// ordinary corpse; off SAVE_ALL a named team-0 death is just a death.
TEST_F(SemWalker2, a_mission_losing_death_leaves_no_corpse_and_any_other_leaves_one)
{
    auto kill = [](walker* w) {
        w->set_dead(1);
        w->death();
    };
    auto stains = [this] { return count_live(world().fxlist, Order::Treasure, FAMILY_STAIN); };
    auto losses = [this] {
        return count_events(og::sim::EventKind::EndGame,
                            static_cast<std::uint32_t>(SCEN_TYPE_SAVE_ALL));
    };

    world().type = SCEN_TYPE_SAVE_ALL;
    {
        walker* hero = living_at(FAMILY_SOLDIER, 0, 160, 160);
        ASSERT_NE(nullptr, hero);
        hero->stats()->name = "Ranger-King";
        const int before = stains();
        kill(hero);
        ASSERT_EQ(1, losses()) << "protected death: the mission fails";
        EXPECT_EQ(before, stains()) << "protected death on SAVE_ALL level: corpse suppressed";
    }
    struct Case { const char* what; std::string name; unsigned char team; unsigned char real_team; bool summoned; };
    const Case cases[] = {
        {"unnamed", "", 0, 255, false},
        {"summoned", "Phantom", 0, 255, true},
        {"charmed foe", "Grunt", 0, 1, false},
        {"foreign colour", "Yellow", 1, 255, false},
    };
    int x = 200;
    for (const Case& c : cases) {
        SCOPED_TRACE(c.what);
        walker* w = living_at(FAMILY_SOLDIER, c.team, static_cast<short>(x), 400);
        x += 40;
        ASSERT_NE(nullptr, w);
        w->stats()->name = c.name;
        w->set_real_team_num(c.real_team);
        w->set_summoned(c.summoned);
        const int before = stains();
        kill(w);
        EXPECT_EQ(1, losses()) << c.what << ": no EndGame";
        EXPECT_EQ(before + 1, stains()) << c.what << ": corpse";
    }
    {
        walker* flagged = living_at(FAMILY_THIEF, 0, 600, 600);
        walker* named = living_at(FAMILY_SOLDIER, 0, 640, 600);
        ASSERT_TRUE(flagged && named);
        flagged->set_save_all_protected(true);
        ASSERT_TRUE(world().has_save_all_protected());
        named->stats()->name = "Unflagged";
        const int before = stains();
        kill(named);
        EXPECT_EQ(1, losses()) << "unflagged when flags exist: no EndGame";
        EXPECT_EQ(before + 1, stains()) << "unflagged when flags exist: corpse";
    }
}

TEST_F(SemWalker2, a_named_team_0_death_off_a_save_all_level_is_an_ordinary_death)
{
    world().type = 0;
    walker* hero = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, hero);
    hero->stats()->name = "Ranger-King";
    const int before = count_live(world().fxlist, Order::Treasure, FAMILY_STAIN);
    hero->set_dead(1);
    hero->death();
    EXPECT_EQ(0, count_events(og::sim::EventKind::EndGame,
                              static_cast<std::uint32_t>(SCEN_TYPE_SAVE_ALL)))
        << "non-SAVE_ALL level: never EndGame";
    EXPECT_EQ(before + 1, count_live(world().fxlist, Order::Treasure, FAMILY_STAIN))
        << "non-SAVE_ALL level: the ordinary corpse";
}

// walker.cpp:1950-1963, the hook half. The mission-losing death returns
// before the level's on_entity_death runs; an ordinary death on the same
// SAVE_ALL level reaches it.
TEST_F(SemWalker2, a_mission_losing_death_skips_the_level_death_hook_and_any_other_runs_it)
{
    constexpr std::int32_t kLevel = 4242;
    og::script::register_pack_script(
        {"test.sem.level", "sem_level.lua",
         "og.register_level_hooks(4242, {\n"
         "  on_entity_death = function(ent) og.log('level death', og.entity_id(ent)) end,\n"
         "})\n"});
    fresh_world();
    world().id = kLevel;
    world().type = SCEN_TYPE_SAVE_ALL;
    world().tick();  // the on_load transition arms the level hooks
    auto deaths = [this](std::uint32_t id) {
        const std::string line = "level death\t" + std::to_string(id);
        const auto& log = world().scripts().host().log();
        return static_cast<int>(std::count(log.begin(), log.end(), line));
    };

    walker* hero = living_at(FAMILY_SOLDIER, 0, 160, 160);
    walker* grunt = living_at(FAMILY_SOLDIER, 0, 400, 400);
    ASSERT_TRUE(hero && grunt);
    hero->stats()->name = "Ranger-King";
    grunt->stats()->name = "";
    const std::uint32_t hero_id = hero->entity_id();
    const std::uint32_t grunt_id = grunt->entity_id();

    grunt->set_dead(1);
    grunt->death();
    EXPECT_EQ(1, deaths(grunt_id)) << "ordinary death on a SAVE_ALL level: level_entity_death runs";

    hero->set_dead(1);
    hero->death();
    ASSERT_EQ(1, count_events(og::sim::EventKind::EndGame,
                              static_cast<std::uint32_t>(SCEN_TYPE_SAVE_ALL)))
        << "protected death: the mission fails";
    EXPECT_EQ(0, deaths(hero_id)) << "protected death: level_entity_death not called";
}

// ---------------------------------------------------------------------------
// set_dormant and change_floor (walker.cpp:2087-2112)
// ---------------------------------------------------------------------------

// walker.cpp:2078-2089. Waking an ignored walker leaves it out of the
// obmap; setting the dormancy it already has changes nothing, so an awake
// hidden walker told to wake stays out of the obmap too.
TEST_F(SemWalker2, waking_an_ignored_walker_keeps_it_out_and_a_repeat_changes_nothing)
{
    walker* probe = living_at(FAMILY_ORC, 1, 400, 400);
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_TRUE(probe && s);
    ASSERT_TRUE(registered(s));
    s->set_dormant(true);
    ASSERT_FALSE(registered(s));
    s->set_ignore(1);
    s->set_dormant(false);
    EXPECT_FALSE(s->dormant());
    EXPECT_FALSE(registered(s)) << "wake, ignored: stays absent from the obmap";
    EXPECT_TRUE(world().query_object_passable(160, 160, probe))
        << "wake, ignored: its spot does not block";

    walker* t = living_at(FAMILY_SOLDIER, 0, 240, 160);
    ASSERT_TRUE(t && registered(t));
    const auto entry = world().myobmap->walker_to_pos.find(t)->second;
    t->set_dormant(false);
    ASSERT_TRUE(registered(t));
    EXPECT_EQ(entry, world().myobmap->walker_to_pos.find(t)->second)
        << "same value (awake): the registration is unchanged";
    t->set_hidden(true);
    ASSERT_FALSE(registered(t));
    t->set_dormant(false);
    EXPECT_FALSE(registered(t)) << "same value (awake, hidden): no re-registration";
    t->set_hidden(false);
    t->set_dormant(true);
    t->set_dormant(true);
    EXPECT_FALSE(registered(t)) << "same value (dormant): still absent";
    EXPECT_TRUE(t->dormant());
}

// walker.cpp:2104/2112 change_floor. A move to another floor lands at height
// 0 and leaves the old floor's collision table; a "move" to the same floor
// changes nothing.
TEST_F(SemWalker2, a_floor_change_lands_at_height_0_leaves_the_old_floor_and_same_floor_is_a_no_op)
{
    world().set_floor_count(2);
    walker* probe0 = living_at(FAMILY_ORC, 1, 400, 400);
    walker* probe1 = living_at(FAMILY_ORC, 1, 400, 400);
    walker* s = living_at(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_TRUE(probe0 && probe1 && s);
    probe1->change_floor(1);
    ASSERT_EQ(1, probe1->floor());
    ASSERT_FALSE(world().query_object_passable(160, 160, probe0)) << "control: blocks floor 0";

    s->set_worldz(12.0f);
    s->change_floor(1);
    EXPECT_EQ(1, s->floor());
    EXPECT_EQ(0.0f, s->worldz()) << "different floor, visible: worldz 0 on arrival";
    EXPECT_TRUE(world().query_object_passable(160, 160, probe0))
        << "different floor, visible: leaves the old floor";
    EXPECT_FALSE(world().query_object_passable(160, 160, probe1))
        << "different floor, visible: collides on the new floor";

    s->set_worldz(12.0f);
    const auto entry = world().myobmap->walker_to_pos.find(s)->second;
    s->change_floor(1);
    EXPECT_EQ(1, s->floor());
    EXPECT_EQ(12.0f, s->worldz()) << "same floor: worldz untouched";
    ASSERT_TRUE(registered(s));
    EXPECT_EQ(entry, world().myobmap->walker_to_pos.find(s)->second)
        << "same floor: registration unchanged";
}
