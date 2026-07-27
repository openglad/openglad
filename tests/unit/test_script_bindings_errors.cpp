/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// What a MISBEHAVING pack does to the og.* binding surface.
//
// Class packs are the mod format and they also arrive over the network from
// another player. A pack's Lua therefore reaches the bindings with whatever
// arguments its author (or a corrupted transfer) supplies. The promise the
// sandbox makes is narrow and absolute:
//
//   a bad call raises a Lua error, the error is RECORDED against the pack
//   and the chunk it came from, the hook returns "not handled", and the
//   process — including the deterministic sim state — is untouched.
//
// Every case below drives one binding argument validator through a real
// hook dispatch (og::script::hooks::*, the door the sim uses), then asserts
// the error was recorded and the world is unchanged. These are the branches
// that keep a typo in a mod from being a crash or, worse, a desync.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/walker.h>

#include <optional>
#include <string>
#include <vector>

namespace {

class ScriptBindingErrorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }

    // Install `body` as core:soldier's do_special and dispatch it. Returns
    // the hook's answer: std::nullopt means the hook errored (which is what
    // every case here expects).
    static std::optional<bool> run_do_special(const std::string& body,
                                              walker* self)
    {
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.pack", "probe.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
        EXPECT_NE(nullptr, fd);
        if (fd == nullptr)
            return std::nullopt;
        return og::script::hooks::do_special(fd, self);
    }

    static const std::vector<og::script::ScriptError>& errors()
    {
        return og::script::active_world_scripts().host().errors();
    }

    // The hook must have errored, and the record must be actionable: the
    // message names the problem AND the chunk/line it came from, and `where`
    // names the hook that was being dispatched.
    static void expect_error_naming(const std::string& fragment)
    {
        ASSERT_FALSE(errors().empty()) << "expected a recorded script error";
        const og::script::ScriptError& e = errors().back();
        EXPECT_NE(std::string::npos, e.message.find(fragment))
            << "message was: " << e.message;
        EXPECT_NE(std::string::npos, e.message.find("probe.lua"))
            << "the failing chunk must be named: " << e.message;
        EXPECT_EQ("hook:do_special", e.where) << "where was: " << e.where;
    }
};

// Installs a chosen gameplay context (including none at all) and restores
// whatever was there. Tests must not depend on what ran before them.
class ScopedContextOverride {
public:
    explicit ScopedContextOverride(GameplayContext* ctx)
        : previous_(current_game)
    {
        current_game = ctx;
    }
    ~ScopedContextOverride() { current_game = previous_; }
    ScopedContextOverride(const ScopedContextOverride&) = delete;
    ScopedContextOverride& operator=(const ScopedContextOverride&) = delete;

private:
    GameplayContext* previous_;
};

}  // namespace

// A binding that needs the world, called with a context that has none. This
// is the shape a level script takes when it runs at the wrong moment; it
// must be a script error, never a null dereference.
TEST_F(ScriptBindingErrorTest, world_bindings_refuse_when_no_world_is_active)
{
    GameplayContext worldless;  // .world stays nullptr
    ScopedContextOverride scope(&worldless);

    EXPECT_FALSE(run_do_special("    local l = og.oblist()", nullptr)
                     .has_value());
    expect_error_naming("no active world");

    EXPECT_FALSE(run_do_special("    og.living_count()", nullptr).has_value());
    expect_error_naming("no active world");
}

// og.emit_* are the sim's event channel. With no context they must refuse
// rather than write through a null pointer.
TEST_F(ScriptBindingErrorTest, event_bindings_refuse_when_no_context_is_active)
{
    ScopedContextOverride scope(nullptr);

    EXPECT_FALSE(run_do_special("    og.emit_withdraw_to_level(3)", nullptr)
                     .has_value());
    expect_error_naming("no active context");

    EXPECT_FALSE(
        run_do_special("    og.emit_exit_confirmation('go?', 2, 0)", nullptr)
            .has_value());
    expect_error_naming("no active context");
}

// The string-keyed selectors (order names, list names, flag names, exp
// action names) are the most typo-prone part of the API. Each must name the
// bad value back at the author.
TEST_F(ScriptBindingErrorTest, unknown_string_selectors_are_named_in_the_error)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    EXPECT_FALSE(
        run_do_special("    og.add_ob('vehicle', 0)", self).has_value());
    expect_error_naming("unknown order 'vehicle'");

    EXPECT_FALSE(
        run_do_special("    og.find_in_range('corpses', 50, self)", self)
            .has_value());
    expect_error_naming("unknown list selector 'corpses'");

    EXPECT_FALSE(
        run_do_special("    og.family_flag('living', 0, 'is_purple')", self)
            .has_value());
    expect_error_naming("unknown flag 'is_purple'");

    EXPECT_FALSE(
        run_do_special("    og.family_flag('weapon', 0, 'is_undead')", self)
            .has_value());
    expect_error_naming("only 'living' supported");

    EXPECT_FALSE(
        run_do_special(
            "    og.exp_from_action(self, self, 'befriend', 0)", self)
            .has_value());
    expect_error_naming("unknown action 'befriend'");
}

// A family byte no pack declares is a legitimate query (the slot is empty),
// not an error: og.family_flag answers nil so a script can branch on it.
TEST_F(ScriptBindingErrorTest, an_undeclared_family_flag_query_answers_nil)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    const std::optional<bool> answered = run_do_special(
        "    if og.family_flag('living', 250, 'is_undead') ~= nil then\n"
        "      error('an empty slot must answer nil')\n"
        "    end",
        self);
    ASSERT_TRUE(answered.has_value())
        << (errors().empty() ? std::string("no error") : errors().back().message);
    EXPECT_TRUE(*answered);
}

// Type confusion: a binding that needs a living or a weapon, handed some
// other entity. dynamic_cast failures must surface as script errors.
TEST_F(ScriptBindingErrorTest, entity_type_mismatches_are_refused)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* soldier = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    walker* treasure_ob = w.add_ob(Order::Treasure, FAMILY_GOLD_BAR);
    ASSERT_NE(nullptr, treasure_ob);

    // do_bounce is a weapon field; a living has none.
    EXPECT_FALSE(run_do_special("    local b = self:do_bounce()", soldier)
                     .has_value());
    expect_error_naming("not a weapon");

    EXPECT_FALSE(run_do_special("    self:set_do_bounce(1)", soldier)
                     .has_value());
    expect_error_naming("not a weapon");

    // find_teleport_target is a treasure field; a living has none.
    EXPECT_FALSE(
        run_do_special("    local t = self:find_teleport_target()", soldier)
            .has_value());
    expect_error_naming("not a treasure");
}

// A hired man's stats live on the guy record. Asking for one where there is
// none must be an error, not a read through null.
TEST_F(ScriptBindingErrorTest, guy_accessors_refuse_an_entity_without_a_guy)
{
    TestGameWorld tw;
    walker* npc = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, npc);
    ASSERT_EQ(nullptr, npc->myguy) << "an NPC has no character record";

    EXPECT_FALSE(run_do_special("    local s = self:g_strength()", npc)
                     .has_value());
    expect_error_naming("no guy record");
}

// Range bindings take a positive bound. Zero and negative must be refused
// rather than folded into a modulo by zero.
TEST_F(ScriptBindingErrorTest, a_nonpositive_cosmetic_rand_bound_is_refused)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    EXPECT_FALSE(run_do_special("    og.cosmetic_rand(0)", self).has_value());
    expect_error_naming("must be positive");

    EXPECT_FALSE(run_do_special("    og.cosmetic_rand(-4)", self).has_value());
    expect_error_naming("must be positive");
}

// The headline invariant: an erroring hook leaves the world exactly as it
// found it up to the point of the error, and — critically — reports "not
// handled" so the caller does not treat a half-executed hook as a success.
TEST_F(ScriptBindingErrorTest, an_erroring_hook_does_not_report_success)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* self = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    const std::size_t obs_before = w.oblist.size();

    const std::optional<bool> handled = run_do_special(
        "    self:set_busy(7)\n"
        "    og.add_ob('not_an_order', 0)", self);

    EXPECT_FALSE(handled.has_value())
        << "an errored hook must not answer true or false";
    EXPECT_EQ(obs_before, w.oblist.size())
        << "the failed add_ob must not have created anything";
    // Side effects BEFORE the error stand — Lua has no transaction — and
    // that is the documented contract; assert it so a future change to it
    // is deliberate.
    EXPECT_EQ(7.0f, self->busy())
        << "writes that already happened are not rolled back";
    expect_error_naming("unknown order");
}

// Errors are stored per (where, message) and deduplicated, so a hook that
// fails every tick cannot flood the store and hide the other packs' errors.
TEST_F(ScriptBindingErrorTest, a_hook_failing_every_tick_records_one_error)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);

    og::script::clear_pack_scripts();
    og::script::register_pack_script(
        {"test.pack", "probe.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self) return og.add_ob('nope', 0) end,\n"
         "})\n"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);

    for (int i = 0; i < 50; i++)
        EXPECT_FALSE(og::script::hooks::do_special(fd, self).has_value());

    ASSERT_EQ(1u, errors().size())
        << "50 identical failures must collapse to one record";
    EXPECT_GE(errors()[0].count, 50u);
}
