/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>

using namespace og::script;

namespace {

class ScriptHooksTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        clear_pack_scripts();
    }
    void TearDown() override { clear_pack_scripts(); }
};

}  // namespace

TEST_F(ScriptHooksTest, lua_hook_overrides_cpp_callback)
{
    register_pack_script(
        {"test.pack", "hooks.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"
         "    og.log('special fired', self == nil)\n"
         "    return true\n"
         "  end,\n"
         "})\n"});

    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_NE(nullptr, fd->do_special)
        << "test premise: soldier has a C++ do_special to be overridden";

    // No world context → dispatch goes through the shared UI instance.
    auto result = hooks::do_special(fd, nullptr);
    ASSERT_TRUE(result.has_value()) << "script hook should have run";
    EXPECT_TRUE(*result);

    WorldScripts& ws = active_world_scripts();
    ASSERT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;
    ASSERT_FALSE(ws.host().log().empty());
    EXPECT_EQ("special fired\ttrue", ws.host().log().back());
    EXPECT_TRUE(ws.has_hook(Order::Living, FAMILY_SOLDIER,
                            FamilyHook::DoSpecial));
    EXPECT_FALSE(ws.has_hook(Order::Living, FAMILY_ELF,
                             FamilyHook::DoSpecial));
}

TEST_F(ScriptHooksTest, unknown_family_is_a_load_error)
{
    register_pack_script(
        {"test.pack", "bad_family.lua",
         "og.register_hooks('living', 'core:nosuchclass', "
         "{ do_special = function() return true end })\n"});
    WorldScripts& ws = active_world_scripts();
    ASSERT_FALSE(ws.host().errors().empty());
    EXPECT_NE(std::string::npos,
              ws.host().errors().front().message.find("unknown living family"));
}

TEST_F(ScriptHooksTest, unknown_hook_name_is_a_load_error)
{
    register_pack_script(
        {"test.pack", "bad_hook.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ do_speshul = function() return true end })\n"});
    WorldScripts& ws = active_world_scripts();
    ASSERT_FALSE(ws.host().errors().empty());
    EXPECT_NE(std::string::npos,
              ws.host().errors().front().message.find("no valid hooks"));
}

TEST_F(ScriptHooksTest, erroring_hook_falls_back_deterministically)
{
    // R9: a hook that errors behaves as absent → C++ fallback would run.
    // Soldier's C++ do_special would crash on nullptr self, so use a hook
    // that errors on a family whose C++ callback slot we can observe via
    // return: pick tower1 only if its do_special is null; otherwise skip.
    register_pack_script(
        {"test.pack", "erroring.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function(self) error('boom') return true end })\n"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_EQ(nullptr, fd->on_death)
        << "test premise: soldier has no C++ on_death fallback";
    auto result = hooks::on_death(fd, nullptr);
    EXPECT_FALSE(result.has_value())
        << "erroring hook with no C++ fallback = nothing ran";
    WorldScripts& ws = active_world_scripts();
    ASSERT_FALSE(ws.host().errors().empty());
    EXPECT_NE(std::string::npos,
              ws.host().errors().back().message.find("boom"));
}

TEST_F(ScriptHooksTest, no_scripts_means_cpp_dispatch_untouched)
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    // No pack scripts registered: on_death (no C++ callback) yields nullopt
    // and no VM errors accumulate anywhere.
    auto result = hooks::on_death(fd, nullptr);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ScriptHooksTest, order_alias_fx_and_effect_both_resolve)
{
    register_pack_script(
        {"test.pack", "fx.lua",
         "og.register_hooks('fx', 'core:boomerang', "
         "{ on_act = function(self) return true end })\n"
         "og.register_hooks('effect', 'core:chain', "
         "{ on_death = function(self) return true end })\n"});
    WorldScripts& ws = active_world_scripts();
    ASSERT_TRUE(ws.host().errors().empty())
        << ws.host().errors().front().message;
    EXPECT_TRUE(ws.has_hook(Order::FX, 7 /*FAMILY_BOOMERANG*/,
                            FamilyHook::EffectOnAct));
    EXPECT_TRUE(ws.has_hook(Order::FX, 10 /*FAMILY_CHAIN*/,
                            FamilyHook::EffectOnDeath));
}
