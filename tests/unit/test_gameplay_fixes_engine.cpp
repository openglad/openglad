/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The engine pieces the Gameplay Fixes setting stands on.
//
// og::sim::seat_driven: a body a seat drives is a hero (a user tag, or
// ACT_CONTROL). POSSESS refuses one; with the setting on, so do the charms.
//
// walker::undead_foes_in_range and walker::turn_undead share one question,
// "could TURN UNDEAD turn this foe" (undead, not warded). With the setting
// on, a ring with foes but nothing to turn is refused before any roll (-2),
// and the roll keeps its classic strength (40 per px of the classic reach,
// 160 per level) however wide the reach is.
//
// Both seams are bound to Lua (walker:seat_driven, walker:undead_foes_in_range)
// and asked here through a pack script, the door the families use.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

walker* add_living(TestGameWorld& tw, int family, unsigned char team, int x,
                   int y, short level = 4)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(static_cast<short>(x), static_cast<short>(y));
    w->stats()->set_level(level);
    return w;
}

// Runs `body` as core:soldier's do_special for `self` (the pack-script door
// the families use) and returns what the hook answered.
std::optional<SpecialResult> run_lua(const std::string& body, walker* self)
{
    og::script::clear_pack_scripts();
    og::script::register_pack_script(
        {"test.gameplay_fixes_engine", "gameplay_fixes_engine_probe.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n" +
             body +
             "\n    return true\n"
             "  end,\n"
             "})\n"});
    return og::script::hooks::do_special(get_family_descriptor(FAMILY_SOLDIER),
                                         self);
}

std::string last_error()
{
    const auto& errors = og::script::active_world_scripts().host().errors();
    return errors.empty() ? std::string("(no script error recorded)")
                          : errors.back().message;
}

class GameplayFixesEngine : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }
};

// The first world-RNG state from which next(bound_a) and next(bound_b),
// drawn in EITHER order from the same two raw draws, keep the order the
// test wants: `a_wins` means the first operand of turn_undead's compare
// (the roll, bound_a) is greater than the second (the level, bound_b).
// Either order, because C++ leaves the evaluation order of the two
// operands of `>` unspecified.
std::uint32_t state_where_roll(std::uint32_t bound_a, std::uint32_t bound_b,
                               bool a_wins_with_a, std::uint32_t other_bound_a,
                               bool a_wins_with_other)
{
    for (std::uint32_t seed = 1;; ++seed) {
        bool ok = true;
        for (const bool a_first : {true, false}) {
            og::sim::SimRandom r(seed);
            og::sim::SimRandom q(seed);
            std::uint32_t a = 0;
            std::uint32_t b = 0;
            std::uint32_t a2 = 0;
            std::uint32_t b2 = 0;
            if (a_first) {
                a = r.next(bound_a);
                b = r.next(bound_b);
                a2 = q.next(other_bound_a);
                b2 = q.next(bound_b);
            } else {
                b = r.next(bound_b);
                a = r.next(bound_a);
                b2 = q.next(bound_b);
                a2 = q.next(other_bound_a);
            }
            ok = ok && ((a > b) == a_wins_with_a) &&
                 ((a2 > b2) == a_wins_with_other);
        }
        if (ok)
            return seed;
    }
}

}  // namespace

// seat_driven: a user tag or ACT_CONTROL makes a hero; neither is a body
// nobody drives. Through Lua the same answer.
// RED (checked by mutation): seat_driven reading only the user tag fails
// the ACT_CONTROL rows.
TEST_F(GameplayFixesEngine, seat_driven_is_a_user_tag_or_act_control)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* w = add_living(tw, FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, w);
    w->set_user(-1);
    w->set_act_type(ACT_RANDOM);
    EXPECT_FALSE(og::sim::seat_driven(*w)) << "a bot";

    w->set_user(0);
    EXPECT_TRUE(og::sim::seat_driven(*w)) << "a user tag";
    w->set_user(-1);
    w->set_act_type(ACT_CONTROL);
    EXPECT_TRUE(og::sim::seat_driven(*w)) << "ACT_CONTROL alone";

    const std::string body =
        "    if self:seat_driven() then\n"
        "      self.level = 31\n"
        "    else\n"
        "      self.level = 30\n"
        "    end";
    for (const bool seated : {false, true}) {
        w->set_act_type(seated ? ACT_CONTROL : ACT_RANDOM);
        const auto handled = run_lua(body, w);
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_EQ(seated ? 31 : 30, w->stats()->level())
            << "walker:seat_driven() with ACT_CONTROL " << seated;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// undead_foes_in_range counts the foes TURN UNDEAD could turn: an undead
// foe in reach counts; a warded one (BIT_INVINCIBLE, or invulnerable), a
// living foe, a generator, an undead ally and an undead out of reach do
// not. It draws nothing. Through Lua the same count.
// RED (checked by mutation): turnable() without the ward test counts 3.
TEST_F(GameplayFixesEngine, undead_foes_in_range_counts_what_turn_undead_could_turn)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* cleric = add_living(tw, FAMILY_CLERIC, 0, 100, 100, 10);
    ASSERT_NE(nullptr, cleric);

    walker* tower = tw.world().add_ob(Order::Generator, FAMILY_TOWER);
    ASSERT_NE(nullptr, tower);
    tower->set_team_num(1);
    tower->setxy(110, 100);
    ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 1, 100, 110));
    EXPECT_EQ(0, cleric->undead_foes_in_range(40))
        << "a generator and a living foe: nothing to turn";

    walker* skeleton = add_living(tw, FAMILY_SKELETON, 1, 120, 100);
    ASSERT_NE(nullptr, skeleton);
    EXPECT_EQ(1, cleric->undead_foes_in_range(40)) << "a skeleton foe in reach";
    EXPECT_EQ(0, cleric->undead_foes_in_range(19)) << "out of reach";

    walker* invincible = add_living(tw, FAMILY_GHOST, 1, 100, 120);
    ASSERT_NE(nullptr, invincible);
    invincible->stats()->set_bit_flags(BIT_INVINCIBLE, 1);
    walker* potion = add_living(tw, FAMILY_SKELETON, 1, 90, 100);
    ASSERT_NE(nullptr, potion);
    potion->set_invulnerable_left(50);
    ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 0, 100, 90));  // ours
    tw.world().rng_.state_ = 4242u;
    EXPECT_EQ(1, cleric->undead_foes_in_range(40))
        << "warded undead and an undead ally are not counted";
    EXPECT_EQ(4242u, tw.world().rng_.state_) << "the count draws nothing";

    const auto handled =
        run_lua("    self.level = self:undead_foes_in_range(40) + 40", cleric);
    ASSERT_TRUE(handled.has_value()) << last_error();
    EXPECT_EQ(41, cleric->stats()->level()) << "walker:undead_foes_in_range(40)";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With the setting on, a ring with foes but none of them undead is refused
// before any roll: -2, and the stream does not move. The classic game finds
// the foe, rolls nothing and answers 0. An empty ring is -1 on both values,
// and a ring with an undead in it is not refused.
// RED on the classic code: 0 with the setting on.
TEST_F(GameplayFixesEngine, turn_undead_refuses_a_ring_without_undead_when_on)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* cleric = add_living(tw, FAMILY_CLERIC, 0, 100, 100, 10);
    ASSERT_NE(nullptr, cleric);
    ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 1, 128, 100));

    for (const short fixes : {short{0}, short{1}}) {
        tw.world().gameplay_fixes = fixes;
        tw.world().rng_.state_ = 99u;
        EXPECT_EQ(fixes != 0 ? -2 : 0, cleric->turn_undead(40, 10))
            << "a living foe in reach, setting " << fixes;
        EXPECT_EQ(99u, tw.world().rng_.state_) << "no roll, setting " << fixes;
        EXPECT_EQ(-1, cleric->turn_undead(20, 10))
            << "nobody in reach, setting " << fixes;
    }

    walker* skeleton = add_living(tw, FAMILY_SKELETON, 1, 100, 120, 10);
    ASSERT_NE(nullptr, skeleton);
    tw.world().gameplay_fixes = 1;
    const int turned = cleric->turn_undead(40, 10);
    EXPECT_GE(turned, 0) << "an undead in the ring: the cast rolls";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The roll's strength is the classic one on both values: OFF the bound is
// range * 40, so the classic reach (4 px per level) rolls level * 160; ON
// the bound is power * 160, whatever the reach. A level-5 cleric on the
// classic reach (20) and the same cleric on the fixed reach (60) roll
// against one skeleton from the same state and get the same answer, which
// the wide ring's own strength (60 * 40 = 2400) would not give.
// RED on the classic code: the 60 px cast with the setting on uses 2400
// and destroys the skeleton.
TEST_F(GameplayFixesEngine, turn_undead_roll_bound_is_classic_on_both_values)
{
    og::test::ScopedHookFailureGuard guard;
    // The skeleton (level 10, so next(100)) resists a bound of 800 and
    // loses to one of 2400, whichever operand is drawn first.
    const std::uint32_t start = state_where_roll(800, 100, false, 2400, true);

    const auto cast = [&](short fixes, std::int32_t range,
                          std::uint32_t* state_after) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        walker* cleric = add_living(tw, FAMILY_CLERIC, 0, 100, 100, 5);
        walker* skeleton = add_living(tw, FAMILY_SKELETON, 1, 110, 100, 10);
        EXPECT_NE(nullptr, cleric);
        EXPECT_NE(nullptr, skeleton);
        if (cleric == nullptr || skeleton == nullptr)
            return -100;
        tw.world().rng_.state_ = start;
        const int turned = cleric->turn_undead(range, 5);
        *state_after = tw.world().rng_.state_;
        return turned;
    };

    og::sim::SimRandom two_draws(start);
    (void)two_draws.next(800);
    (void)two_draws.next(100);

    std::uint32_t state_off = 0;
    std::uint32_t state_on = 0;
    EXPECT_EQ(0, cast(0, 20, &state_off))
        << "the classic reach of a level-5 cleric: the skeleton resists";
    EXPECT_EQ(0, cast(1, 60, &state_on))
        << "the fixed reach rolls the classic strength: it resists too";
    EXPECT_EQ(two_draws.state_, state_off) << "one roll: two draws";
    EXPECT_EQ(two_draws.state_, state_on) << "the same two draws";

    std::uint32_t state_wide = 0;
    EXPECT_EQ(1, cast(0, 60, &state_wide))
        << "the control: the classic game's 60 px ring rolls 2400 and turns";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
