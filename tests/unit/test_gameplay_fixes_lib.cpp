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
