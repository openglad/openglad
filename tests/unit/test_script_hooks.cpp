/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/script/family_decl.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace og::script;

namespace {

class ScriptHooksTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        init_all_registries();
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
    }
    void TearDown() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
    }
};

}  // namespace

TEST_F(ScriptHooksTest, lua_hook_is_the_only_family_behavior)
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
    ASSERT_EQ(nullptr, fd->do_special)
        << "pack-installed family behavior must have no C++ callback";

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
    const std::string& message = ws.host().errors().front().message;
    EXPECT_NE(std::string::npos, message.find("has no hook 'do_speshul'"));
    // The other half of a strict-key rule: the name the author meant.
    EXPECT_NE(std::string::npos, message.find("did you mean 'do_special'"));
}

TEST_F(ScriptHooksTest, erroring_hook_is_latched_loudly)
{
    // R9: a hook that errors behaves as absent for that dispatch. Pack
    // descriptors have no C++ fallback, so "absent" means NOTHING runs and
    // the failure must be traced, logged, and latched for tests to assert.
    register_pack_script(
        {"test.pack", "erroring.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function(self) error('boom') return true end })\n"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_EQ(nullptr, fd->on_death)
        << "pack-installed family behavior must have no C++ fallback";

    hooks::reset_hook_failures();
    ASSERT_EQ(0u, hooks::hook_failures().count);

    auto result = hooks::on_death(fd, nullptr);
    EXPECT_FALSE(result.has_value()) << "erroring hook = nothing ran";

    WorldScripts& ws = active_world_scripts();
    ASSERT_FALSE(ws.host().errors().empty());
    EXPECT_NE(std::string::npos,
              ws.host().errors().back().message.find("boom"));

    const auto& failure = hooks::hook_failures();
    EXPECT_EQ(1u, failure.count) << "the dispatch failure must be latched";
    EXPECT_EQ("hook:on_death", failure.where);
    EXPECT_NE(std::string::npos, failure.message.find("boom"));

    // Repeats keep counting even though the host folds the error record.
    (void)hooks::on_death(fd, nullptr);
    EXPECT_EQ(2u, hooks::hook_failures().count);

    hooks::reset_hook_failures();
    EXPECT_EQ(0u, hooks::hook_failures().count);
    EXPECT_TRUE(hooks::hook_failures().where.empty());
}

TEST_F(ScriptHooksTest, healthy_hook_leaves_the_failure_latch_clear)
{
    register_pack_script(
        {"test.pack", "healthy.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function(self) return true end })\n"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    hooks::reset_hook_failures();
    auto result = hooks::on_death(fd, nullptr);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    EXPECT_EQ(0u, hooks::hook_failures().count);
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

// ---------------------------------------------------------------------------
// specials = { charge=fn, ..., default=fn } — the do_special table form.
// Keys are the ids the family declares (core:soldier: charge,
// boomerang, whirlwind, disarm at slots 1..4).
// Contract: current_special() selects,
// a missing index falls to `default`, a table with neither is a successful
// no-op, and the selected function's result converts exactly like a plain
// do_special return.
// ---------------------------------------------------------------------------

namespace {

class SpecialsDispatchTest : public ::testing::Test {
protected:
    SpecialsDispatchTest() : world(11)
    {
        init_all_registries();
        world.id = 92;
        world.entity_factory =
            [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            entity->ani = nullptr;
            entity->ani_count = 0;
            return entity;
        };
        context.world = &world;
        context.sim_events = &events;
        previous_ = current_game;
        current_game = &context;
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
    }

    ~SpecialsDispatchTest() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
        current_game = previous_;
    }

    void SetUp() override
    {
        self = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, self);
    }

    static void register_chunk(const std::string& source)
    {
        register_pack_script({"test.pack", "specials.lua", source});
    }

    // Sets the walker's special slot and dispatches through the same funnel
    // walker::special() uses.
    std::optional<bool> dispatch(int sp)
    {
        self->set_current_special(static_cast<char>(sp));
        const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
        EXPECT_NE(nullptr, fd);
        if (fd == nullptr)
            return std::nullopt;
        return hooks::do_special(fd, self);
    }

    const std::vector<std::string>& vm_log()
    {
        return world.scripts().host().log();
    }

    const std::vector<ScriptError>& vm_errors()
    {
        return world.scripts().host().errors();
    }

    GameWorld world;
    GameplayContext context{};
    og::sim::SimEventLog events;
    GameplayContext* previous_ = nullptr;
    walker* self = nullptr;
};

}  // namespace

TEST_F(SpecialsDispatchTest, table_selects_by_current_special)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('charge') return true end,\n"
        "    boomerang = function(self) og.log('boomerang') return false end,\n"
        "  },\n"
        "})\n");
    auto r1 = dispatch(1);
    ASSERT_TRUE(r1.has_value()) << "slot 1 must dispatch";
    EXPECT_TRUE(*r1);
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("charge", vm_log().back());

    // Result plumbing is the branch's own return, boolean-coerced exactly
    // like a plain do_special return (false stays false).
    auto r2 = dispatch(2);
    ASSERT_TRUE(r2.has_value()) << "slot 2 must dispatch";
    EXPECT_FALSE(*r2);
    EXPECT_EQ("boomerang", vm_log().back());
    ASSERT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(SpecialsDispatchTest, missing_index_falls_to_default)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('one') return true end,\n"
        "    default = function(self) og.log('default') return false end,\n"
        "  },\n"
        "})\n");
    auto r = dispatch(5);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r) << "default's own return value must come through";
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("default", vm_log().back());
}

TEST_F(SpecialsDispatchTest, unmatched_index_without_default_is_a_noop_true)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('one') return false end,\n"
        "  },\n"
        "})\n");
    hooks::reset_hook_failures();
    auto r = dispatch(4);
    ASSERT_TRUE(r.has_value())
        << "the ladders fell through their if-chain and returned true; the "
           "dispatcher must consume the dispatch the same way";
    EXPECT_TRUE(*r);
    EXPECT_TRUE(vm_log().empty()) << "nothing may run: " << vm_log().back();
    EXPECT_TRUE(vm_errors().empty());
    EXPECT_EQ(0u, hooks::hook_failures().count);
}

TEST_F(SpecialsDispatchTest, selection_reads_the_slot_at_each_dispatch)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('first') return true end,\n"
        "    whirlwind = function(self) og.log('third') return true end,\n"
        "  },\n"
        "})\n");
    ASSERT_TRUE(dispatch(3).has_value());
    EXPECT_EQ("third", vm_log().back());
    ASSERT_TRUE(dispatch(1).has_value());
    EXPECT_EQ("first", vm_log().back());
}

TEST_F(SpecialsDispatchTest, plain_do_special_function_form_still_works)
{
    // The table form is sugar, not a replacement: a sibling family keeps
    // registering the classic function under the same dispatch funnel.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('table form') return true end,\n"
        "  },\n"
        "})\n"
        "og.register_hooks('living', 'core:elf', {\n"
        "  do_special = function(self) og.log('function form') return false "
        "end,\n"
        "})\n");
    ASSERT_TRUE(dispatch(1).has_value());
    EXPECT_EQ("table form", vm_log().back());

    const FamilyDescriptor* elf = get_family_descriptor(FAMILY_ELF);
    ASSERT_NE(nullptr, elf);
    auto r = hooks::do_special(elf, nullptr);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ("function form", vm_log().back());
}

TEST_F(SpecialsDispatchTest, null_self_selects_the_default_entry)
{
    // Test-style dispatches (no walker) select slot 0, which no table maps:
    // a default runs, a default-less table no-ops.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('one') return true end,\n"
        "    default = function(self) og.log('default ran') return true "
        "end,\n"
        "  },\n"
        "})\n");
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    auto r = hooks::do_special(fd, nullptr);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ("default ran", vm_log().back());
}

TEST_F(SpecialsDispatchTest, registered_table_is_immune_to_later_mutation)
{
    // The registered contract is a private copy (R6 at the registration
    // boundary): post-registration edits of the caller's table must not
    // change dispatch.
    register_chunk(
        "local t = {\n"
        "  charge = function(self) og.log('original') return true end,\n"
        "}\n"
        "og.register_hooks('living', 'core:soldier', { specials = t })\n"
        "t.charge = function(self) og.log('mutated') return true end\n"
        "t.default = function(self) og.log('sneaked default') return true "
        "end\n");
    ASSERT_TRUE(dispatch(1).has_value());
    EXPECT_EQ("original", vm_log().back());
    auto r = dispatch(9);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(*r);
    EXPECT_EQ("original", vm_log().back())
        << "the sneaked default must not dispatch";
}

TEST_F(SpecialsDispatchTest, branch_falling_off_the_end_reads_as_false)
{
    // Documented sharp edge: the branch's return converts exactly like a
    // plain do_special return, so falling off the end (nil) is false. The
    // ladder rewrites append the ladder's shared `return true` explicitly.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('no return') end,\n"
        "  },\n"
        "})\n");
    auto r = dispatch(1);
    ASSERT_TRUE(r.has_value());
    EXPECT_FALSE(*r);
    EXPECT_EQ("no return", vm_log().back());
}

TEST_F(SpecialsDispatchTest, erroring_branch_latches_like_any_hook_error)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) error('branch boom') end,\n"
        "  },\n"
        "})\n");
    hooks::reset_hook_failures();
    auto r = dispatch(1);
    EXPECT_FALSE(r.has_value()) << "erroring branch = nothing ran";
    EXPECT_EQ(1u, hooks::hook_failures().count);
    EXPECT_EQ("hook:do_special", hooks::hook_failures().where);
    EXPECT_NE(std::string::npos,
              hooks::hook_failures().message.find("branch boom"));
}

TEST_F(SpecialsDispatchTest, cross_chunk_collision_reports_and_last_wins)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = {\n"
        "    charge = function(self) og.log('table form') return true end,\n"
        "  },\n"
        "})\n"
        "og.register_hooks('living', 'core:soldier', {\n"
        "  do_special = function(self) og.log('function form') return true "
        "end,\n"
        "})\n");
    ASSERT_TRUE(dispatch(1).has_value());
    EXPECT_EQ("function form", vm_log().back())
        << "last registration must win the shared do_special slot";
    ASSERT_FALSE(vm_errors().empty())
        << "the collision must be reported, not silent";
    EXPECT_NE(std::string::npos,
              vm_errors().back().message.find("duplicate hook registration"));
}

TEST_F(SpecialsDispatchTest, both_forms_in_one_call_is_a_load_error)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  do_special = function(self) return true end,\n"
        "  specials = { charge = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    EXPECT_NE(std::string::npos,
              vm_errors().front().message.find("not both in one call"));
}

TEST_F(SpecialsDispatchTest, empty_specials_table_is_a_load_error)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', { specials = {} })\n");
    ASSERT_FALSE(vm_errors().empty());
    EXPECT_NE(std::string::npos,
              vm_errors().front().message.find("'specials' table is empty"));
}

TEST_F(SpecialsDispatchTest, bad_specials_key_is_a_load_error)
{
    // Neither a string nor an integer: nothing to resolve and nothing to
    // suggest, so the error just states the two forms that exist.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = { [1.5] = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    EXPECT_NE(std::string::npos,
              vm_errors().front().message.find(
                  "keys must be a declared special id or 'default'"));
}

TEST_F(SpecialsDispatchTest, a_slot_number_key_is_a_load_error)
{
    // The id form is the only spelling. A slot number bound a handler to a
    // position in the YAML list, so the error names the key and the ids
    // that would have worked.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = { [1] = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    const std::string& msg = vm_errors().front().message;
    EXPECT_NE(std::string::npos, msg.find("key [1] is a slot number")) << msg;
    EXPECT_NE(std::string::npos,
              msg.find("specials keys are the family's declared special ids, "
                       "or 'default'"))
        << msg;
    EXPECT_NE(std::string::npos, msg.find("living family 'core:soldier'"))
        << msg;
    EXPECT_NE(std::string::npos,
              msg.find("declared ids: charge, boomerang, whirlwind, disarm"))
        << "the message must list what would have worked: " << msg;
}

TEST_F(SpecialsDispatchTest, non_function_specials_entry_is_a_load_error)
{
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = { charge = 5 },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    EXPECT_NE(std::string::npos,
              vm_errors().front().message.find("entries must be functions"));
}

TEST_F(SpecialsDispatchTest, specials_on_a_non_living_order_is_a_load_error)
{
    register_chunk(
        "og.register_hooks('weapon', 'core:knife', {\n"
        "  specials = { charge = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    EXPECT_NE(std::string::npos,
              vm_errors().front().message.find("living-order key"));
}

// ---------------------------------------------------------------------------
// Specials keyed by declared id. A declaration gives each special an `id`;
// a script keys its handler by that id and the engine resolves it to the
// slot at registration — so the two reference each other by name and a typo
// on either side is a load error instead of a handler bound to the wrong
// special. The slot number that used to work here is refused.
// ---------------------------------------------------------------------------

namespace {

// A living family with declared special ids, installed into a free wire
// slot so it cannot disturb the core registry the other tests read.
constexpr int kIdFamilyWireId = 64;

class SpecialsByIdTest : public SpecialsDispatchTest {
protected:
    void SetUp() override
    {
        og::data::ClasspackData data;
        og::script::clear_pack_family_chunks();
        // Not `packs/idtest/...`: that prefix would declare these bytes to
        // the pack-Lua coverage inventory (pack_scripts.h) as content of a
        // pack that does not exist.
        og::script::register_pack_family_chunk(
            {"idtest", "idtest/families/warlock.lua",
             "og.family('living', { id = 'idtest:warlock', wire_id = 64,\n"
             "  name = 'WARLOCK',\n"
             "  specials = {\n"
             "    { id = 'flare_burst', name = 'FLARE BURST', mp_cost = 5 },\n"
             "    { id = 'hex', name = 'HEX', mp_cost = 9 } } })\n"});
        const og::script::DeclareResult declared =
            og::script::declare_pack_families("idtest", data);
        og::script::clear_pack_family_chunks();
        ASSERT_TRUE(declared.ok) << declared.error;
        ASSERT_EQ(og::resources::install_classpack_data(std::move(data)), 1);
        self = world.add_ob(Order::Living, kIdFamilyWireId);
        ASSERT_NE(nullptr, self);
    }

    std::optional<bool> dispatch_warlock(int sp)
    {
        self->set_current_special(static_cast<char>(sp));
        const FamilyDescriptor* fd =
            get_family_descriptor(kIdFamilyWireId);
        EXPECT_NE(nullptr, fd);
        if (fd == nullptr)
            return std::nullopt;
        return hooks::do_special(fd, self);
    }
};

}  // namespace

TEST_F(SpecialsByIdTest, a_declared_id_keys_its_own_slot)
{
    register_chunk(
        "og.register_hooks('living', 'v2test:warlock', {\n"
        "  specials = {\n"
        "    hex = function(self) og.log('hex') return true end,\n"
        "    flare_burst = function(self) og.log('flare') return true end,\n"
        "  },\n"
        "})\n");
    auto r1 = dispatch_warlock(1);
    ASSERT_TRUE(r1.has_value()) << "flare_burst is slot 1";
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("flare", vm_log().back());
    auto r2 = dispatch_warlock(2);
    ASSERT_TRUE(r2.has_value()) << "hex is slot 2";
    EXPECT_EQ("hex", vm_log().back());
    // The unmapped slot still falls through to the charging no-op: id keys
    // change how a handler is named, nothing about dispatch.
    auto r3 = dispatch_warlock(3);
    ASSERT_TRUE(r3.has_value());
    EXPECT_TRUE(*r3);
    ASSERT_TRUE(vm_errors().empty()) << vm_errors().front().message;
}

TEST_F(SpecialsByIdTest, misspelled_id_is_a_load_error_naming_the_real_ids)
{
    register_chunk(
        "og.register_hooks('living', 'v2test:warlock', {\n"
        "  specials = { flare_brust = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    const std::string& msg = vm_errors().front().message;
    EXPECT_NE(std::string::npos, msg.find("flare_brust")) << msg;
    EXPECT_NE(std::string::npos, msg.find("v2test:warlock")) << msg;
    EXPECT_NE(std::string::npos, msg.find("flare_burst, hex"))
        << "the message must list what the family does declare: " << msg;
}

TEST_F(SpecialsByIdTest, a_slot_number_beside_its_id_is_still_a_load_error)
{
    // The table that used to be the ambiguous case — a slot and the id of
    // that same slot — now fails on the slot alone, before anything is
    // stored, so the well-spelled sibling entry does not register either.
    register_chunk(
        "og.register_hooks('living', 'v2test:warlock', {\n"
        "  specials = {\n"
        "    [1] = function(self) return true end,\n"
        "    flare_burst = function(self) return false end,\n"
        "  },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    const std::string& msg = vm_errors().front().message;
    EXPECT_NE(std::string::npos, msg.find("key [1] is a slot number")) << msg;
    EXPECT_NE(std::string::npos,
              msg.find("declared ids: flare_burst, hex"))
        << msg;
    EXPECT_FALSE(dispatch_warlock(1).has_value())
        << "a refused registration registers nothing";
}

TEST_F(SpecialsDispatchTest, an_id_key_on_a_family_that_declares_none_says_so)
{
    // core:faerie has no castable slot and so declares no id; the error has
    // to say that rather than list an empty set.
    register_chunk(
        "og.register_hooks('living', 'core:faerie', {\n"
        "  specials = { flutter = function(self) return true end },\n"
        "})\n");
    ASSERT_FALSE(vm_errors().empty());
    const std::string& msg = vm_errors().front().message;
    EXPECT_NE(std::string::npos, msg.find("declares no special ids")) << msg;
}

TEST_F(SpecialsDispatchTest, the_shipped_core_pack_declares_its_special_ids)
{
    // The join the migrated corpus depends on: soldier.lua keys its four
    // handlers by the ids living-00-soldier.yaml declares.
    register_chunk(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  specials = { charge = function(self) return true end },\n"
        "})\n");
    ASSERT_TRUE(vm_errors().empty()) << vm_errors().front().message;
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    EXPECT_STREQ("charge", fd->special_ids[1]);
    EXPECT_STREQ("boomerang", fd->special_ids[2]);
    EXPECT_STREQ("whirlwind", fd->special_ids[3]);
    EXPECT_STREQ("disarm", fd->special_ids[4]);
    EXPECT_EQ(nullptr, fd->special_ids[0]);
    EXPECT_EQ(nullptr, fd->special_ids[5]);
}

TEST_F(SpecialsByIdTest, castable_slot_with_no_handler_warns_after_load)
{
    // HEX is castable (a name and a cost under 5000) and this table
    // handles neither it nor `default`, so casting it would spend 9 MP and
    // do nothing. That is still what happens — it is just no longer silent.
    register_chunk(
        "og.register_hooks('living', 'v2test:warlock', {\n"
        "  specials = { flare_burst = function(self) return true end },\n"
        "})\n");
    testing::internal::CaptureStderr();
    (void)world.scripts();
    const std::string said = testing::internal::GetCapturedStderr();
    EXPECT_NE(std::string::npos, said.find("HEX")) << said;
    EXPECT_NE(std::string::npos, said.find("spend 9 MP and do nothing"))
        << said;
    EXPECT_EQ(std::string::npos, said.find("FLARE BURST"))
        << "a handled slot is not worth a word: " << said;
}

TEST_F(SpecialsByIdTest, a_default_answers_for_every_castable_slot)
{
    register_chunk(
        "og.register_hooks('living', 'v2test:warlock', {\n"
        "  specials = { default = function(self) return true end },\n"
        "})\n");
    testing::internal::CaptureStderr();
    (void)world.scripts();
    const std::string said = testing::internal::GetCapturedStderr();
    EXPECT_EQ(std::string::npos, said.find("has no handler")) << said;
}

// ---------------------------------------------------------------------------
// og.* binding rows (wave-4 surface: animation tables, statistics commands,
// score/progression/exit flow, CTF flag touch)
// ---------------------------------------------------------------------------

namespace {

// Runs Lua against a real GameWorld. Chunks execute inside a generator
// customize_spawn hook, which hands the script two live walker handles
// (`gen`, `spawn`) without needing a full world tick.
class ScriptBindingTest : public ::testing::Test {
protected:
    ScriptBindingTest() : world(11)
    {
        init_all_registries();
        world.id = 91;
        world.entity_factory =
            [](Order order, std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            entity->ani = nullptr;
            entity->ani_count = 0;
            return entity;
        };
        context.world = &world;
        context.sim_events = &events;
        previous_ = current_game;
        current_game = &context;
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
    }

    ~ScriptBindingTest() override
    {
        clear_pack_scripts();
        clear_pack_family_chunks();
        clear_lua_declared_families();
        current_game = previous_;
    }

    void SetUp() override
    {
        generator = world.add_ob(Order::Generator, 0 /*FAMILY_TENT*/);
        spawn = world.add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, generator);
        ASSERT_NE(nullptr, spawn);
    }

    void run(const std::string& body)
    {
        register_pack_script(
            {"test.pack", "bind.lua",
             "og.register_hooks('generator', 'core:tent', {\n"
             "  customize_spawn = function(gen, spawn)\n" +
                 body + "\n  end,\n})\n"});
        ASSERT_TRUE(hooks::generator_customize_spawn(0, generator, spawn))
            << "customize_spawn hook did not run";
        const auto& errs = world.scripts().host().errors();
        ASSERT_TRUE(errs.empty()) << errs.front().message;
    }

    const std::vector<std::string>& vm_log()
    {
        return world.scripts().host().log();
    }

    const std::vector<og::sim::Event>& sim_events() const
    {
        return events.events();
    }

    GameWorld world;
    GameplayContext context{};
    og::sim::SimEventLog events;
    GameplayContext* previous_ = nullptr;
    walker* generator = nullptr;
    walker* spawn = nullptr;
};

}  // namespace

TEST_F(ScriptBindingTest, ani_frame_and_ani_row_read_the_animation_table)
{
    static const signed char kRow0[] = {5, 6, 7, -1};
    static const signed char kEmptyRow[] = {-1};
    static const signed char kUnterminated[130] = {};  // no -1 within the cap
    static const signed char* const kTable[] = {kRow0, kEmptyRow, nullptr,
                                                kUnterminated};
    spawn->ani = kTable;
    spawn->ani_count = 4;

    run("og.log('frame', og.ani_frame(spawn, 0, 0), og.ani_frame(spawn, 0, 2))\n"
        // An empty row still answers index 0 with the -1 the C++ would have
        // handed set_frame.
        "og.log('sentinel', og.ani_frame(spawn, 1, 0))\n"
        "og.log('nil', og.ani_frame(spawn, 0, 4) == nil,\n"
        "              og.ani_frame(spawn, 2, 0) == nil,\n"
        "              og.ani_frame(spawn, 3, 0) == nil,\n"
        "              og.ani_frame(spawn, 4, 0) == nil,\n"
        "              og.ani_frame(spawn, -1, 0) == nil)\n"
        "local r = og.ani_row(spawn, 0)\n"
        "og.log('row', #r, r[1], r[2], r[3])\n"
        "og.log('empty', #og.ani_row(spawn, 1))\n"
        "og.log('nilrow', og.ani_row(spawn, 2) == nil,\n"
        "                 og.ani_row(spawn, 3) == nil,\n"
        "                 og.ani_row(spawn, 4) == nil)\n");

    ASSERT_EQ(6u, vm_log().size());
    EXPECT_EQ("frame\t5\t7", vm_log()[0]);
    EXPECT_EQ("sentinel\t-1", vm_log()[1]);
    // Past the sentinel, a null row, a malformed row, past ani_count, and a
    // negative row all read back nil.
    EXPECT_EQ("nil\ttrue\ttrue\ttrue\ttrue\ttrue", vm_log()[2]);
    EXPECT_EQ("row\t3\t5\t6\t7", vm_log()[3]);
    EXPECT_EQ("empty\t0", vm_log()[4]);
    EXPECT_EQ("nilrow\ttrue\ttrue\ttrue", vm_log()[5]);
}

TEST_F(ScriptBindingTest, ani_accessors_are_nil_without_a_table)
{
    // Mirrors the C++ `if (self->ani)` guard: no table ⇒ nothing to read.
    ASSERT_EQ(nullptr, spawn->ani);
    run("og.log('none', og.ani_frame(spawn, 0, 0) == nil, "
        "og.ani_row(spawn, 0) == nil)\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("none\ttrue\ttrue", vm_log()[0]);
}

TEST_F(ScriptBindingTest, ani_count_zero_keeps_legacy_direct_indexing)
{
    // ani_count == 0 marks a walker that assigned `ani` directly (the walker
    // ani_count invariant); rows are then indexed without an upper bound.
    static const signed char kRow[] = {3, -1};
    static const signed char* const kTable[] = {kRow, kRow, kRow};
    spawn->ani = kTable;
    spawn->ani_count = 0;
    run("og.log('legacy', og.ani_frame(spawn, 2, 0))\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("legacy\t3", vm_log()[0]);
}

TEST_F(ScriptBindingTest, s_force_fright_merges_into_a_forced_walk)
{
    // statistics::force_fright: a forced COMMAND_WALK already at the front is
    // refreshed (max count, new direction), not prepended to.
    run("spawn:s_force_command(og.C.COMMAND_WALK, 5, 1, 0)\n"
        "spawn:s_force_fright(100, 7, 0)\n");
    auto& cmds = spawn->stats()->commands;
    ASSERT_EQ(1u, cmds.size()) << "fright must merge, not stack";
    EXPECT_EQ(COMMAND_WALK, cmds.front().commandtype);
    EXPECT_EQ(100, cmds.front().commandcount);
    EXPECT_EQ(1, cmds.front().com1) << "delta clamped to the unit step";
    EXPECT_EQ(0, cmds.front().com2);
    EXPECT_TRUE(cmds.front().forced);
}

TEST_F(ScriptBindingTest, s_do_command_runs_and_drains_the_queue)
{
    // COMMAND_UNCHARM is the queue's no-op branch: it exercises the run +
    // decrement + pop path without moving anything.
    run("spawn:s_add_command(og.C.COMMAND_UNCHARM, 2, 0, 0)\n"
        "og.log('step', spawn:s_do_command(), spawn:s_has_commands())\n"
        "og.log('step', spawn:s_do_command(), spawn:s_has_commands())\n");
    ASSERT_EQ(2u, vm_log().size());
    EXPECT_EQ("step\t1\ttrue", vm_log()[0]);
    EXPECT_EQ("step\t1\tfalse", vm_log()[1]);
    EXPECT_TRUE(spawn->stats()->commands.empty());
}

TEST_F(ScriptBindingTest, scare_radius_matches_combat_math)
{
    run("og.log('r', og.scare_radius(1), og.scare_radius(5), "
        "og.scare_radius(30))\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("r\t" + std::to_string(og::combat::scare_radius(1)) + "\t" +
                  std::to_string(og::combat::scare_radius(5)) + "\t" +
                  std::to_string(og::combat::scare_radius(30)),
              vm_log()[0]);
    // Pin the cap so the binding cannot drift from the C++ helper.
    EXPECT_EQ("r\t60\t100\t250", vm_log()[0]);
}

TEST_F(ScriptBindingTest, award_score_credits_the_team_and_emits_score_change)
{
    run("og.award_score(1, 250)\n"
        "og.award_score(1, 50)\n"
        "og.award_score(9, 999)\n");  // out of range: silently ignored
    EXPECT_EQ(300u, world.m_score[1]);
    EXPECT_EQ(0u, world.m_score[0]);

    std::vector<og::sim::Event> scores;
    for (const auto& e : sim_events())
        if (e.kind == og::sim::EventKind::ScoreChange)
            scores.push_back(e);
    ASSERT_EQ(2u, scores.size()) << "one event per accepted credit";
    EXPECT_EQ(1u, scores[0].a);
    EXPECT_EQ(250u, scores[0].b);
    EXPECT_EQ(1u, scores[1].a);
    EXPECT_EQ(50u, scores[1].b);
}

TEST_F(ScriptBindingTest, exit_flow_bindings_read_and_write_progression)
{
    world.type = GameWorld::TYPE_CAN_EXIT_WHENEVER;
    world.current_scenario = 5;
    world.completed_levels.insert(3);

    run("og.log('flags', og.world_can_exit_whenever(), og.level_completed(3), "
        "og.level_completed(4), og.current_scenario())\n"
        "og.set_withdraw_request(3)\n"
        "og.emit_exit_confirmation('Withdraw to Keep?', 3, 1)\n"
        "og.emit_withdraw_to_level(3)\n");

    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("flags\ttrue\ttrue\tfalse\t5", vm_log()[0]);
    EXPECT_TRUE(world.withdraw_requested);
    EXPECT_EQ(3, world.withdraw_level);

    ASSERT_EQ(2u, sim_events().size());
    EXPECT_EQ(og::sim::EventKind::RequestExitConfirmation,
              sim_events()[0].kind);
    EXPECT_EQ("Withdraw to Keep?", sim_events()[0].text);
    EXPECT_EQ(3u, sim_events()[0].a);
    EXPECT_EQ(1u, sim_events()[0].b);
    EXPECT_EQ(og::sim::EventKind::WithdrawToLevel, sim_events()[1].kind);
    EXPECT_EQ(3u, sim_events()[1].a);
    EXPECT_EQ(0u, sim_events()[1].b);
}

// #230: og.emit_notification's optional third argument addresses one seat.
// A walker resolves to whoever controls it; an integer is taken as a global
// player index; nil/omitted/negative stays the broadcast sentinel.
TEST_F(ScriptBindingTest, emit_notification_addresses_a_single_seat)
{
    spawn->set_user(4);

    run("og.emit_notification('broadcast')\n"
        "og.emit_notification('timed', 12)\n"
        "og.emit_notification('to walker', 0, spawn)\n"
        "og.emit_notification('to index', 0, 2)\n"
        "og.emit_notification('negative', 0, -5)\n"
        "og.emit_notification('explicit nil', 0, nil)\n");

    ASSERT_EQ(6u, sim_events().size());
    for (const auto& e : sim_events())
        EXPECT_EQ(og::sim::EventKind::Notification, e.kind);

    EXPECT_EQ("broadcast", sim_events()[0].text);
    EXPECT_EQ(-1, sim_events()[0].target_player);
    EXPECT_EQ(0u, sim_events()[0].a);

    EXPECT_EQ("timed", sim_events()[1].text);
    EXPECT_EQ(12u, sim_events()[1].a);
    EXPECT_EQ(-1, sim_events()[1].target_player);

    EXPECT_EQ("to walker", sim_events()[2].text);
    EXPECT_EQ(4, sim_events()[2].target_player)
        << "a walker argument addresses the player controlling it";

    EXPECT_EQ("to index", sim_events()[3].text);
    EXPECT_EQ(2, sim_events()[3].target_player);

    EXPECT_EQ("negative", sim_events()[4].text);
    EXPECT_EQ(-1, sim_events()[4].target_player)
        << "any negative index collapses to the broadcast sentinel";

    EXPECT_EQ("explicit nil", sim_events()[5].text);
    EXPECT_EQ(-1, sim_events()[5].target_player);
}

// An unowned walker (user() == -1) as the target keeps the line a broadcast,
// which is what the treasure emitters rely on for AI-eaten pickups.
TEST_F(ScriptBindingTest, emit_notification_to_an_unowned_walker_broadcasts)
{
    ASSERT_EQ(-1, static_cast<int>(spawn->user()));
    run("og.emit_notification('unowned', 0, spawn)\n");
    ASSERT_EQ(1u, sim_events().size());
    EXPECT_EQ(-1, sim_events()[0].target_player);
}

TEST_F(ScriptBindingTest, scenario_title_reports_none_without_a_provider)
{
    ASSERT_FALSE(static_cast<bool>(world.scenario_title_provider));
    run("og.log('t', og.scenario_title('scen1'))\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("t\tnone", vm_log()[0]);
}

TEST_F(ScriptBindingTest, scenario_title_uses_the_installed_provider)
{
    world.scenario_title_provider = [](const char* filename) {
        return std::string("Title of ") + filename;
    };
    run("og.log('t', og.scenario_title('scen7'))\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("t\tTitle of scen7", vm_log()[0]);
}

TEST_F(ScriptBindingTest, walker_field_rows_round_trip_through_the_members)
{
    spawn->set_sizez(7);
    spawn->set_worldz(3.0f);

    run("spawn:set_death_called(1)\n"
        "spawn:set_fire_frequency(4)\n"
        "spawn:set_default_weapon(3)\n"
        "spawn:set_speed_bonus(2)\n"
        "spawn:set_cycle(2)\n"
        "spawn:set_summoned(true)\n"
        "spawn:set_save_all_protected(true)\n"
        // og.trunc keeps float-valued rows out of Lua's float formatting so
        // the expectation is exact on every platform.
        "og.log('ints', spawn:sizez(), spawn:death_called(),\n"
        "               spawn:default_weapon(), spawn:cycle())\n"
        "og.log('floats', og.trunc(spawn:worldz()),\n"
        "                 og.trunc(spawn:fire_frequency()),\n"
        "                 og.trunc(spawn:speed_bonus()))\n"
        "og.log('bools', spawn:summoned(), spawn:save_all_protected())\n");

    ASSERT_EQ(3u, vm_log().size());
    EXPECT_EQ("ints\t7\t1\t3\t2", vm_log()[0]);
    EXPECT_EQ("floats\t3\t4\t2", vm_log()[1]);
    EXPECT_EQ("bools\ttrue\ttrue", vm_log()[2]);

    // The setters landed on the real members, not on a Lua-side shadow.
    EXPECT_EQ(1, spawn->death_called());
    EXPECT_FLOAT_EQ(4.0f, spawn->fire_frequency());
    EXPECT_EQ(3, spawn->default_weapon());
    EXPECT_EQ(2, spawn->cycle());
    EXPECT_FLOAT_EQ(2.0f, spawn->speed_bonus());
    EXPECT_TRUE(spawn->summoned());
    EXPECT_TRUE(spawn->save_all_protected());
}

TEST_F(ScriptBindingTest, statistics_rows_round_trip_through_the_members)
{
    ASSERT_NE(nullptr, spawn->stats());
    spawn->stats()->set_controller(generator);

    run("spawn:s_set_max_magicpoints(40)\n"
        "spawn:s_set_armor(6)\n"
        "spawn:s_set_magic_per_round(3)\n"
        "spawn:s_set_heal_per_round(2)\n"
        "spawn:s_set_weapon_cost(9)\n"
        "spawn:s_set_frozen_delay(11)\n"
        "spawn:s_set_current_distance(21)\n"
        "spawn:s_set_last_distance(22)\n"
        "spawn:s_set_max_heal_delay(31)\n"
        "spawn:s_set_current_heal_delay(32)\n"
        "spawn:s_set_max_magic_delay(41)\n"
        "spawn:s_set_current_magic_delay(42)\n"
        "spawn:s_set_special_cost(1, 77)\n"
        "og.log('ints', spawn:s_weapon_cost(), spawn:s_frozen_delay(),\n"
        "               spawn:s_current_distance(), spawn:s_last_distance(),\n"
        "               spawn:s_max_heal_delay(), spawn:s_current_heal_delay(),\n"
        "               spawn:s_max_magic_delay(), spawn:s_current_magic_delay(),\n"
        "               spawn:s_special_cost(1))\n"
        "og.log('floats', og.trunc(spawn:s_max_magicpoints()),\n"
        "                 og.trunc(spawn:s_armor()),\n"
        "                 og.trunc(spawn:s_magic_per_round()),\n"
        "                 og.trunc(spawn:s_heal_per_round()))\n"
        // The controller row hands back a live handle to the controlling
        // walker, not a copy or a stale id.
        "og.log('controller', spawn:s_controller() == gen)\n");

    ASSERT_EQ(3u, vm_log().size());
    EXPECT_EQ("ints\t9\t11\t21\t22\t31\t32\t41\t42\t77", vm_log()[0]);
    EXPECT_EQ("floats\t40\t6\t3\t2", vm_log()[1]);
    EXPECT_EQ("controller\ttrue", vm_log()[2]);

    statistics* st = spawn->stats();
    ASSERT_NE(nullptr, st);
    EXPECT_FLOAT_EQ(40.0f, st->max_magicpoints());
    EXPECT_FLOAT_EQ(6.0f, st->armor());
    EXPECT_EQ(9, st->weapon_cost());
    EXPECT_EQ(77, st->special_cost(1));
}

TEST_F(ScriptBindingTest, special_cost_rows_reject_an_out_of_range_index)
{
    // The bounds check is the whole point of the pair: NUM_SPECIALS-sized
    // array, index straight from a pack script.
    register_pack_script(
        {"test.pack", "bind.lua",
         "og.register_hooks('generator', 'core:tent', {\n"
         "  customize_spawn = function(gen, spawn)\n"
         "    local ok = pcall(function() spawn:s_special_cost(-1) end)\n"
         "    og.log('get', ok)\n"
         "    ok = pcall(function() spawn:s_set_special_cost(99, 1) end)\n"
         "    og.log('set', ok)\n"
         "  end,\n})\n"});
    ASSERT_TRUE(hooks::generator_customize_spawn(0, generator, spawn));
    ASSERT_TRUE(world.scripts().host().errors().empty());
    ASSERT_EQ(2u, vm_log().size());
    EXPECT_EQ("get\tfalse", vm_log()[0]) << "negative index must raise";
    EXPECT_EQ("set\tfalse", vm_log()[1]) << "index past NUM_SPECIALS must raise";
}

TEST_F(ScriptBindingTest, guy_rows_round_trip_and_myguy_moves_between_walkers)
{
    spawn->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    ASSERT_NE(nullptr, spawn->myguy);
    spawn->myguy->level = 4;

    run("spawn:g_set_strength(11)\n"
        "spawn:g_set_dexterity(12)\n"
        "spawn:g_set_constitution(13)\n"
        "spawn:g_set_intelligence(14)\n"
        "spawn:g_set_armor(15)\n"
        "spawn:g_set_total_hits(16)\n"
        "spawn:g_set_scen_hits(17)\n"
        "og.log('guy', spawn:g_strength(), spawn:g_dexterity(),\n"
        "              spawn:g_constitution(), spawn:g_intelligence(),\n"
        "              spawn:g_armor(), spawn:g_level(),\n"
        "              spawn:g_total_hits(), spawn:g_scen_hits())\n"
        // slime.lua's split path: the record follows the new body, and the
        // old one is left without one.
        "spawn:move_myguy_to(gen)\n"
        "og.log('moved', spawn:has_guy(), gen:has_guy())\n"
        "gen:clear_myguy()\n"
        "og.log('cleared', gen:has_guy())\n");

    ASSERT_EQ(3u, vm_log().size());
    EXPECT_EQ("guy\t11\t12\t13\t14\t15\t4\t16\t17", vm_log()[0]);
    EXPECT_EQ("moved\tfalse\ttrue", vm_log()[1]);
    EXPECT_EQ("cleared\tfalse", vm_log()[2]);
    EXPECT_EQ(nullptr, spawn->myguy);
    EXPECT_EQ(nullptr, generator->myguy);
}

TEST_F(ScriptBindingTest, world_query_rows_read_the_live_world)
{
    world.id = 91;
    world.game_ended = 0;

    run("local w = og.add_weap_ob('weapon', 0)\n"
        "og.log('weap', w ~= nil, w:order() == og.C.ORDER_WEAPON)\n"
        "og.log('world', og.level_id(), og.level_tick(), og.game_ended())\n"
        "og.log('foes', og.remaining_foes(spawn))\n"
        // freeze_duration draws the sim RNG; the result must be a real
        // non-negative duration, not nil.
        "og.log('freeze', og.freeze_duration(10, 3) >= 0)\n"
        // walker:collide(other) records the collision partner.
        "og.log('collide', spawn:collide(gen), spawn:collide_ob() == gen)\n"
        "og.log('alive', og.is_alive(spawn), tostring(spawn))\n");

    ASSERT_EQ(6u, vm_log().size());
    EXPECT_EQ("weap\ttrue\ttrue", vm_log()[0]);
    EXPECT_EQ("world\t91\t0\tfalse", vm_log()[1]);
    // The binding must forward to GameWorld::remaining_foes for THIS walker.
    EXPECT_EQ("foes\t" + std::to_string(world.remaining_foes(spawn)),
              vm_log()[2]);
    EXPECT_EQ("freeze\ttrue", vm_log()[3]);
    EXPECT_EQ("collide\ttrue\ttrue", vm_log()[4]);
    EXPECT_EQ("alive\ttrue\tentity#" + std::to_string(spawn->entity_id()),
              vm_log()[5])
        << "__tostring must be address-free (entity id, never a pointer)";
    EXPECT_EQ(generator, spawn->collide_ob());
}

TEST_F(ScriptBindingTest, host_is_reachable_through_a_const_world_scripts)
{
    run("og.log('hello')\n");
    const og::script::WorldScripts& scripts = world.scripts();
    EXPECT_TRUE(scripts.host().errors().empty());
    ASSERT_EQ(1u, scripts.host().log().size());
    EXPECT_EQ("hello", scripts.host().log()[0]);
}
