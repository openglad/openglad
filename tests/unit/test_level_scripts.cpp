/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/walker.h>

using namespace og::script;

namespace {

class LevelScriptsTest : public ::testing::Test {
protected:
    LevelScriptsTest() : world(7)
    {
        world.id = 42;
        world.entity_factory = [](Order order,
                                  std::int32_t family) -> std::unique_ptr<walker> {
            auto entity = std::make_unique<walker>();
            entity->set_order_family(order, static_cast<char>(family));
            entity->set_sizex(16);
            entity->set_sizey(16);
            return entity;
        };
        context.world = &world;
        context.sim_events = &events;
        previous_ = current_game;
        current_game = &context;
        clear_pack_scripts();
    }

    ~LevelScriptsTest() override
    {
        clear_pack_scripts();
        current_game = previous_;
    }

    const std::vector<std::string>& vm_log()
    {
        return world.scripts().host().log();
    }

    GameWorld world;
    GameplayContext context{};
    og::sim::SimEventLog events;
    GameplayContext* previous_ = nullptr;
};

}  // namespace

TEST_F(LevelScriptsTest, load_and_tick_hooks_fire_in_order)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_load = function(level) og.log('load', level) end,\n"
         "  on_tick = function(level, tick) og.log('tick', level, tick) end,\n"
         "})\n"});
    world.tick();
    world.tick();
    ASSERT_GE(vm_log().size(), 3u);
    EXPECT_EQ("load\t42", vm_log()[0]);
    EXPECT_EQ("tick\t42\t1", vm_log()[1]);
    EXPECT_EQ("tick\t42\t2", vm_log()[2]);
}

// Staged-lobby seam (#218): run_pending_level_on_load is the tick-side
// on_load dispatch factored behind its latch, so a staged world can run it
// before the first tick. One dispatch path, two call sites.
TEST_F(LevelScriptsTest, run_pending_level_on_load_dispatches_once_untensed)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_load = function(level) og.log('load', level) end,\n"
         "  on_tick = function(level, tick) og.log('tick', level, tick) end,\n"
         "})\n"});
    world.run_pending_level_on_load();
    EXPECT_EQ(0u, world.tick_count_) << "staged on_load must not tick";
    EXPECT_EQ(0u, world.level_tick_count());
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("load\t42", vm_log()[0]);

    // Latch-guarded: a second staged call is a no-op.
    world.run_pending_level_on_load();
    ASSERT_EQ(1u, vm_log().size());

    // The first tick after staging dispatches on_tick only — the latch
    // keeps on_load once-only across the seam's two call sites.
    world.tick();
    ASSERT_EQ(2u, vm_log().size());
    EXPECT_EQ("tick\t42\t1", vm_log()[1]);
    EXPECT_EQ(1u, world.level_tick_count());
}

// Adoption seam (#218): claim_level_load_latch marks on_load as already
// delivered WITHOUT running it — the authoritative adopter of a staged
// world transfers the latch truthfully instead of re-dispatching.
TEST_F(LevelScriptsTest, claim_level_load_latch_suppresses_the_tick_dispatch)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_load = function(level) og.log('load', level) end,\n"
         "  on_tick = function(level, tick) og.log('tick', level, tick) end,\n"
         "})\n"});
    register_pack_script(
        {"test.level2", "lvl2.lua",
         "og.register_level_hooks(43, {\n"
         "  on_load = function(level) og.log('load', level) end,\n"
         "})\n"});
    world.claim_level_load_latch();
    EXPECT_TRUE(vm_log().empty()) << "claiming must not dispatch";

    world.tick();
    ASSERT_EQ(1u, vm_log().size());
    EXPECT_EQ("tick\t42\t1", vm_log()[0])
        << "the claimed latch suppresses the tick-side on_load";

    // A LEVEL CHANGE re-opens the latch: on_load is owed to the new level.
    world.id = 43;
    world.run_pending_level_on_load();
    ASSERT_EQ(2u, vm_log().size());
    EXPECT_EQ("load\t43", vm_log()[1]);
}

TEST_F(LevelScriptsTest, wildcard_level_hooks_fire_for_any_level)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(-1, {\n"
         "  on_load = function(level) og.log('wild', level) end,\n"
         "})\n"});
    world.tick();
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("wild\t42", vm_log()[0]);
}

TEST_F(LevelScriptsTest, other_level_hooks_do_not_fire)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(43, {\n"
         "  on_load = function(level) og.log('wrong') end,\n"
         "})\n"});
    world.tick();
    EXPECT_TRUE(vm_log().empty());
}

TEST_F(LevelScriptsTest, spawn_and_death_hooks_with_per_entity_override)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_entity_spawn = function(ent)\n"
         "    og.log('spawn', og.entity_id(ent))\n"
         "    og.set_entity_hooks(ent, { on_death = function(e)\n"
         "      og.log('custom death', og.entity_id(e))\n"
         "    end })\n"
         "  end,\n"
         "  on_entity_death = function(ent)\n"
         "    og.log('level death', og.entity_id(ent))\n"
         "  end,\n"
         "})\n"});
    // Prime the level (on_load transition) so hooks are live.
    world.tick();
    walker* soldier = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, soldier);
    const std::uint32_t id = soldier->entity_id();
    ASSERT_NE(0u, id);
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("spawn\t" + std::to_string(id), vm_log().back());

    soldier->set_dead(1);
    soldier->death();
    ASSERT_GE(vm_log().size(), 3u);
    // Per-entity hook first, then the level-wide hook.
    EXPECT_EQ("custom death\t" + std::to_string(id),
              vm_log()[vm_log().size() - 2]);
    EXPECT_EQ("level death\t" + std::to_string(id), vm_log().back());

    // The per-entity entry was consumed: a second death() call (guarded by
    // death_called in real flows) must not re-fire the custom hook.
    const size_t count_after = vm_log().size();
    hooks::level_entity_death(soldier);
    EXPECT_EQ(count_after + 1, vm_log().size())
        << "only the level-wide hook may fire again";
    EXPECT_EQ("level death\t" + std::to_string(id), vm_log().back());
}

TEST_F(LevelScriptsTest, nested_dispatch_keeps_outer_handles_alive)
{
    // A hook that re-enters dispatch (walker:special(), a death hook that
    // spawns, ...) opens a NEWER dispatch generation while its own frame is
    // still running. Handle validity is therefore "any generation still on
    // the stack", not "the newest one" — otherwise a hook invalidates its
    // own `self` halfway through, which is exactly how fire_elemental's
    // on_death used to fail silently once the C++ fallback was gone.
    // The handle must be one the world id index CANNOT resolve, otherwise
    // find_by_id answers first and the generation check never runs — which
    // is why this only ever bit loose, untracked walkers.
    register_pack_script(
        {"test.pack", "nested.lua",
         "og.register_level_hooks(42, {\n"
         "  on_entity_spawn = function(ent) end,\n"  // inner dispatch
         "})\n"
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n"
         "    -- Opens and closes a NESTED dispatch (add_ob fires the level\n"
         "    -- on_entity_spawn hook above).\n"
         "    og.add_ob('living', og.family_id('living', 'core:elf'))\n"
         "    -- `self` was minted by the OUTER dispatch and is untracked;\n"
         "    -- it must still resolve here.\n"
         "    og.log('outer handle survived', self:family())\n"
         "    return true\n"
         "  end,\n"
         "})\n"});
    world.tick();

    walker loose;  // never added to the world: entity_id stays 0
    loose.set_order_family(Order::Living, static_cast<char>(FAMILY_SOLDIER));
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd);

    const auto result = hooks::do_special(fd, &loose);
    const auto& errs = world.scripts().host().errors();
    ASSERT_TRUE(errs.empty())
        << "nested dispatch must not invalidate the outer frame's handles: "
        << errs.front().message;
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(*result);
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("outer handle survived\t" + std::to_string(FAMILY_SOLDIER),
              vm_log().back());
}

TEST_F(LevelScriptsTest, generator_deaths_dispatch_entity_death)
{
    // Generators are the canonical scripted object; a level script must be
    // able to observe one falling (the showcase's pillar mechanic).
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_entity_death = function(ent)\n"
         "    og.log('died', og.entity_id(ent))\n"
         "  end,\n"
         "})\n"});
    world.tick();
    walker* tent = world.add_ob(Order::Generator, 0 /*FAMILY_TENT*/);
    ASSERT_NE(nullptr, tent);
    const std::uint32_t id = tent->entity_id();
    tent->set_dead(1);
    tent->death();
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("died\t" + std::to_string(id), vm_log().back());
}

TEST_F(LevelScriptsTest, fx_spawns_do_not_fire_entity_spawn)
{
    register_pack_script(
        {"test.level", "lvl.lua",
         "og.register_level_hooks(42, {\n"
         "  on_entity_spawn = function(ent) og.log('spawn') end,\n"
         "})\n"});
    world.tick();
    world.add_ob(Order::FX, 3 /*FAMILY_EXPLOSION*/);
    EXPECT_TRUE(vm_log().empty()) << "FX spawns must not dispatch";
    world.add_ob(Order::Generator, 0 /*FAMILY_TENT*/);
    EXPECT_EQ(1u, vm_log().size()) << "generator spawns dispatch";
}

TEST_F(LevelScriptsTest, generator_customize_spawn_dispatches)
{
    register_pack_script(
        {"test.pack", "gen.lua",
         "og.register_hooks('generator', 'core:tent', {\n"
         "  customize_spawn = function(gen, spawn)\n"
         "    og.log('customized', og.entity_id(gen), og.entity_id(spawn))\n"
         "    spawn:set_lifetime(123)\n"
         "  end,\n"
         "})\n"});
    walker* generator = world.add_ob(Order::Generator, 0 /*FAMILY_TENT*/);
    walker* spawn = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, generator);
    ASSERT_NE(nullptr, spawn);
    const bool ran = hooks::generator_customize_spawn(0, generator, spawn);
    EXPECT_TRUE(ran);
    EXPECT_EQ(123, spawn->lifetime());
    ASSERT_FALSE(vm_log().empty());
    EXPECT_EQ("customized\t" + std::to_string(generator->entity_id()) + "\t" +
                  std::to_string(spawn->entity_id()),
              vm_log().back());
}

TEST_F(LevelScriptsTest, no_level_hooks_means_no_vm_activity)
{
    register_pack_script(
        {"test.pack", "family_only.lua",
         "og.register_hooks('living', 'core:soldier', "
         "{ on_death = function() return true end })\n"});
    world.tick();
    world.tick();
    EXPECT_TRUE(vm_log().empty());
    EXPECT_TRUE(world.scripts().host().errors().empty());
}
