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
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <memory>
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
    }
    void TearDown() override { clear_pack_scripts(); }
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
        << "stage A premise: the C++ callback is retired, Lua carries it all";

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

TEST_F(ScriptHooksTest, erroring_hook_is_latched_loudly)
{
    // R9: a hook that errors behaves as absent for that dispatch. With the
    // C++ callbacks retired (§9a) "absent" means NOTHING runs, so the
    // failure has to be impossible to miss: traced, logged, and latched
    // where a test can assert on it.
    register_pack_script(
        {"test.pack", "erroring.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function(self) error('boom') return true end })\n"});
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);
    ASSERT_EQ(nullptr, fd->on_death) << "stage A: no C++ fallback exists";

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
    }

    ~ScriptBindingTest() override
    {
        clear_pack_scripts();
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

TEST_F(ScriptBindingTest, ctf_flag_touch_forwards_to_the_ctf_engine)
{
    // TYPE_CTF without an initialized match: the engine's own gate returns
    // true and touches nothing. Both spellings are the same wrapper.
    world.type = GameWorld::TYPE_CTF;
    ASSERT_FALSE(world.ctf.active);
    run("og.log('ctf', og.ctf_on_flag_touch(gen, spawn), "
        "og.ctf_flag_touch == og.ctf_on_flag_touch, "
        "og.world_can_exit_whenever())\n");
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("ctf\ttrue\ttrue\tfalse", vm_log()[0]);
    EXPECT_EQ(0u, world.m_score[0]);
}
