/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The Gameplay Fixes setting as pack Lua reads it.
//
// og.match_setting("gameplay_fixes") answers the world's flag (0 = the
// classic game, bugs included), and the read draws nothing: a fixed branch
// of a classic special asks it first, so with the setting off the classic
// draw sequence must not move by one step. packs/core/lib/gameplay_fixes.lua
// (`gf`) is the small helper module those fixed branches share.
//
// Every case dispatches a pack script through og::script::hooks, the door
// the sim uses, and checks no hook errored.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <optional>
#include <string>

namespace {

// Registers `body` as core:soldier's do_special (a pack script whose hook
// runs the Lua under test) and dispatches it once for `self`.
class GameplayFixesLua : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }

    static std::optional<SpecialResult> run(const std::string& prelude,
                                            const std::string& body,
                                            walker* self)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.gameplay_fixes", "gameplay_fixes_probe.lua",
             prelude +
                 "\nog.register_hooks('living', 'core:soldier', {\n"
                 "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        return og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
    }

    static std::string last_error()
    {
        const auto& errors = og::script::active_world_scripts().host().errors();
        return errors.empty() ? std::string("(no script error recorded)")
                              : errors.back().message;
    }
};

}  // namespace

// og.match_setting("gameplay_fixes") reads the world's flag on both values,
// independently of New Specials, and leaves the world's random stream where
// it was.
TEST_F(GameplayFixesLua, match_setting_reads_the_world_flag_and_draws_nothing)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, self->stats());

    const std::string body =
        "    self.level = og.match_setting('gameplay_fixes') + 7";
    for (const short flag : {short{0}, short{1}}) {
        tw.world().gameplay_fixes = flag;
        tw.world().new_specials = static_cast<short>(1 - flag);
        tw.world().rng_.state_ = 12345u;
        const auto handled = run("", body, self);
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_TRUE(*handled);
        EXPECT_EQ(7 + flag, self->stats()->level())
            << "og.match_setting('gameplay_fixes') with the flag at " << flag;
        EXPECT_EQ(12345u, tw.world().rng_.state_)
            << "the read draws nothing";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// gf.on() is the setting as a boolean: false on the classic game, true with
// the fixes on, and like the read under it, it draws nothing.
TEST_F(GameplayFixesLua, gf_on_follows_the_setting_and_draws_nothing)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    ASSERT_NE(nullptr, self->stats());

    const std::string prelude = "local gf = og.use('core:gameplay_fixes')\n";
    const std::string body =
        "    if gf.on() then\n"
        "      self.level = 21\n"
        "    else\n"
        "      self.level = 20\n"
        "    end";
    for (const short flag : {short{0}, short{1}}) {
        tw.world().gameplay_fixes = flag;
        tw.world().new_specials = static_cast<short>(1 - flag);
        tw.world().rng_.state_ = 777u;
        const auto handled = run(prelude, body, self);
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_TRUE(*handled);
        EXPECT_EQ(20 + flag, self->stats()->level())
            << "gf.on() with the setting at " << flag;
        EXPECT_EQ(777u, tw.world().rng_.state_) << "gf.on() draws nothing";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// gf.turn_range: 4 px per level on the classic game; with the fixes on never
// under turn_undead_range_min (a level-5 cleric reaches 60, not 20), and a
// reach already past the floor is kept (level 20: 80 on both values).
TEST_F(GameplayFixesLua, gf_turn_range_has_a_floor_only_when_on)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const std::string prelude = "local gf = og.use('core:gameplay_fixes')\n";
    const std::string body =
        "    local t = { turn_undead_range_per_level = 4,\n"
        "                turn_undead_range_min = 60 }\n"
        "    self.level = gf.turn_range(self, t)";
    struct Row {
        short fixes;
        short level;
        short reach;
    };
    for (const Row row : {Row{0, 5, 20}, Row{1, 5, 60}, Row{0, 20, 80},
                          Row{1, 20, 80}}) {
        tw.world().gameplay_fixes = row.fixes;
        self->stats()->set_level(row.level);
        const auto handled = run(prelude, body, self);
        ASSERT_TRUE(handled.has_value()) << last_error();
        EXPECT_EQ(row.reach, self->stats()->level())
            << "level " << row.level << ", setting " << row.fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// gf.take_back: the rocks that never flew are charged back at the weapon
// cost each, as a float.
TEST_F(GameplayFixesLua, gf_take_back_charges_the_rocks_that_never_flew)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->stats()->set_weapon_cost(4);
    const float cost = static_cast<float>(self->stats()->weapon_cost());
    self->stats()->set_max_magicpoints(500.0f);
    self->stats()->set_magicpoints(100.5f);

    const auto handled = run("local gf = og.use('core:gameplay_fixes')\n",
                             "    gf.take_back(self, 3)", self);
    ASSERT_TRUE(handled.has_value()) << last_error();
    EXPECT_FLOAT_EQ(100.5f - 3.0f * cost, self->stats()->magicpoints());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// gf.charmable: a bot foe can be charmed on both values; a charmed walker
// (its real team kept) and a generator never; a hero a seat drives (a user
// tag, or ACT_CONTROL) only on the classic game.
TEST_F(GameplayFixesLua, gf_charmable_skips_a_seated_hero_only_with_the_fixes)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->set_team_num(0);
    self->setxy(100, 100);
    walker* foe = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, foe);
    foe->set_team_num(1);
    foe->set_real_team_num(255);
    foe->setxy(120, 100);

    // The first foe in reach, asked on both values of `fixes`: 1 + 2 * the
    // classic answer + 4 * the fixed one.
    const std::string body =
        "    local foes = og.find_foes_in_range('ob', 400, self)\n"
        "    local n = 1\n"
        "    if gf.charmable(foes[1], false) then\n"
        "      n = n + 2\n"
        "    end\n"
        "    if gf.charmable(foes[1], true) then\n"
        "      n = n + 4\n"
        "    end\n"
        "    self.level = n";
    const std::string prelude = "local gf = og.use('core:gameplay_fixes')\n";
    const auto ask = [&]() {
        const auto handled = run(prelude, body, self);
        EXPECT_TRUE(handled.has_value()) << last_error();
        return static_cast<int>(self->stats()->level());
    };

    EXPECT_EQ(7, ask()) << "a bot foe: charmable on both";
    foe->set_user(0);
    EXPECT_EQ(3, ask()) << "a user tag: a hero, charmable only classically";
    foe->set_user(-1);
    foe->set_act_type(ACT_CONTROL);
    EXPECT_EQ(3, ask()) << "ACT_CONTROL: a hero too";
    foe->set_act_type(ACT_RANDOM);
    foe->set_real_team_num(2);
    EXPECT_EQ(1, ask()) << "already charmed: never";
    foe->set_dead(1);

    walker* tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    ASSERT_NE(nullptr, tent);
    tent->set_team_num(1);
    tent->setxy(130, 100);
    tent->set_real_team_num(255);
    EXPECT_EQ(1, ask()) << "a generator: never";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// gf.nearest_charmable: the nearest charmable foe (a tie goes to the first
// in the list), only at or below max_level when one is given, nil on an
// empty list or when nobody qualifies. A seated hero is skipped.
TEST_F(GameplayFixesLua, gf_nearest_charmable_takes_the_nearest_eligible_foe)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    self->set_team_num(0);
    self->setxy(100, 100);
    self->stats()->set_level(7);

    // Each foe's level names it: 11 far, 12 and 13 tied nearer, 14 the
    // nearest but a seated hero.
    const auto add_foe = [&](short level, int x) {
        walker* foe = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
        EXPECT_NE(nullptr, foe);
        foe->set_team_num(1);
        foe->set_real_team_num(255);
        foe->setxy(static_cast<short>(x), static_cast<short>(100));
        foe->stats()->set_level(level);
        return foe;
    };
    add_foe(11, 160);
    add_foe(12, 130);
    add_foe(13, 70);
    walker* hero = add_foe(14, 110);
    hero->set_user(0);

    const std::string prelude = "local gf = og.use('core:gameplay_fixes')\n";
    const auto pick = [&](const std::string& call) {
        const auto handled = run(
            prelude,
            "    local foes = og.find_foes_in_range('ob', 400, self)\n"
            "    local foe = " + call + "\n"
            "    if foe == nil then\n"
            "      self.level = 1\n"
            "    else\n"
            "      self.level = foe.level\n"
            "    end",
            self);
        EXPECT_TRUE(handled.has_value()) << last_error();
        return static_cast<int>(self->stats()->level());
    };

    EXPECT_EQ(12, pick("gf.nearest_charmable(self, foes, nil)"))
        << "30 px each way: the tie goes to the first in the list; the "
           "hero at 10 px is skipped";
    EXPECT_EQ(11, pick("gf.nearest_charmable(self, foes, 11)"))
        << "only a foe at or below the level";
    EXPECT_EQ(1, pick("gf.nearest_charmable(self, foes, 10)"))
        << "nobody low enough: nil";
    EXPECT_EQ(1, pick("gf.nearest_charmable(self, {}, nil)"))
        << "an empty list: nil";
    hero->set_user(-1);
    EXPECT_EQ(14, pick("gf.nearest_charmable(self, foes, nil)"))
        << "the same foe off its seat is the nearest";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
