/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials: the small engine seams — fearlessness and the banner's
// rally aura, solid scenery (blocks_placement) and the on_kill hook. The
// kit verbs' Lua bindings are pinned in test_script_bindings_props.cpp.
//
// No shipped family uses these yet, so each seam is driven by a test-only
// pack declaration (a weapon with rally_radius or blocks_placement, a hook
// table registered on core:soldier) installed for the test and put back by
// the pack-store guard.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "kit_probe_family.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/gameplay/families/classpack_data.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>
#include <openglad/gameplay/fearless.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <string>
#include <utility>
#include <vector>

namespace {

using og::data::ClasspackData;
using og::test::declare;

// Installs `decl` (`families` og.family('weapon', ...) calls) and answers
// the id of `string_id`, or -1.
int install_weapon(const std::string& decl, const char* string_id,
                   int families = 1)
{
    ClasspackData data;
    const og::script::DeclareResult r = declare(decl, data);
    EXPECT_TRUE(r.ok) << r.error;
    if (!r.ok)
        return -1;
    EXPECT_EQ(families,
              og::resources::install_classpack_data(std::move(data)));
    return og::families::resolve_family_string_id(Order::Weapon, string_id);
}

// A pack weapon with no art of its own: the world's loader only knows the
// families that were installed when it was built, so the walker is made as a
// knife and re-tagged, which is all the seams read.
walker* add_pack_weapon(TestGameWorld& tw, int family, unsigned char team,
                        short x, short y)
{
    walker* w = tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    if (w == nullptr)
        return nullptr;
    w->set_order_family(Order::Weapon, static_cast<char>(family));
    w->set_team_num(team);
    w->setxy(x, y);
    return w;
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    return w;
}

bool queues_a_flee_walk(const walker& w)
{
    return !w.stats()->commands.empty() &&
           w.stats()->commands.front().commandtype == COMMAND_WALK;
}

// yell_for_help on `hero` against `foe`, from an empty queue: did the
// yeller queue its run-away walk?
bool yell_runs_away(walker* hero, walker* foe)
{
    hero->stats()->commands.clear();
    hero->stats()->yell_for_help(foe);
    const bool ran = queues_a_flee_walk(*hero);
    hero->stats()->commands.clear();
    return ran;
}

void register_script(const std::string& lua)
{
    og::script::clear_pack_scripts();
    og::script::register_pack_script({"test.seams", "seams_probe.lua", lua});
}

const std::vector<std::string>& script_log()
{
    return og::script::active_world_scripts().host().log();
}

std::string last_script_error()
{
    const auto& errors = og::script::active_world_scripts().host().errors();
    return errors.empty() ? std::string("(no script error recorded)")
                          : errors.back().message;
}

class KitSeams : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override
    {
        og::script::clear_pack_scripts();
        reset_all_registry_mod_slots();
    }
};

}  // namespace

// og::sim::fearless is the walker's KIT_FEARLESS mark, or (setting on) a
// friendly live rally weapon on its floor within the family's rally_radius.
// Its two consumers: the flee walk at the end of yell_for_help (the yeller
// still rallies its friends, it just does not run) and the s_force_fright
// binding (the ghost's scare). Every arm of the banner rule is flipped once:
// distance, team, floor, the banner's death and the setting itself.
TEST_F(KitSeams, fearless_bit_and_rally_radius_block_the_flee_walk)
{
    og::test::ScopedHookFailureGuard guard;
    const int banner_id = install_weapon(
        "og.family('weapon', { id = 'kitprobe:banner', wire_id = 'auto',\n"
        "  rally_radius = 64 })\n",
        "kitprobe:banner");
    ASSERT_GE(banner_id, 22);
    TestGameWorld tw;
    GameWorld& world = tw.world();
    world.set_floor_count(2);
    world.new_specials = 1;

    walker* hero = add_living(tw, FAMILY_SOLDIER, 0, 160, 160);
    walker* foe = add_living(tw, FAMILY_ORC, 1, 200, 160);
    walker* ally = add_living(tw, FAMILY_SOLDIER, 0, 130, 160);
    ASSERT_NE(nullptr, hero);
    ASSERT_NE(nullptr, foe);
    ASSERT_NE(nullptr, ally);

    // Classic: the yeller runs.
    EXPECT_FALSE(og::sim::fearless(*hero));
    EXPECT_TRUE(yell_runs_away(hero, foe)) << "today's flee walk";

    // The kit mark: no run, but the friends are still rallied onto the foe.
    hero->set_kit_state(KIT_FEARLESS);
    EXPECT_TRUE(og::sim::fearless(*hero));
    ally->set_foe(nullptr);
    ally->set_leader(nullptr);
    EXPECT_FALSE(yell_runs_away(hero, foe)) << "a fearless walker holds";
    EXPECT_EQ(foe, ally->foe()) << "its friends still answer the yell";
    EXPECT_EQ(hero, ally->leader());
    EXPECT_TRUE(ally->stats()->commands.empty());
    hero->set_kit_state(0);
    EXPECT_FALSE(og::sim::fearless(*hero));

    // The banner's aura: a friendly live rally weapon within 64 px.
    walker* banner = add_pack_weapon(tw, banner_id, 0, 180, 170);
    ASSERT_NE(nullptr, banner);
    EXPECT_TRUE(og::sim::fearless(*hero)) << "inside the rally radius";
    EXPECT_FALSE(yell_runs_away(hero, foe));
    EXPECT_FALSE(og::sim::fearless(*foe)) << "an enemy banner rallies nobody";

    banner->setxy(600, 600);
    EXPECT_FALSE(og::sim::fearless(*hero)) << "moved out of the radius";
    EXPECT_TRUE(yell_runs_away(hero, foe));
    banner->setxy(180, 170);
    ASSERT_TRUE(og::sim::fearless(*hero));

    banner->set_team_num(1);
    EXPECT_FALSE(og::sim::fearless(*hero)) << "a banner of another team";
    EXPECT_TRUE(og::sim::fearless(*foe));
    banner->set_team_num(0);

    banner->set_floor(1);
    EXPECT_FALSE(og::sim::fearless(*hero)) << "a banner on another floor";
    banner->set_floor(0);

    world.new_specials = 0;
    EXPECT_FALSE(og::sim::fearless(*hero))
        << "with the setting off a rally_radius weapon changes nothing";
    EXPECT_TRUE(yell_runs_away(hero, foe));
    world.new_specials = 1;
    ASSERT_TRUE(og::sim::fearless(*hero));

    // The fright binding: a rallied walker ignores the scare, and a walker
    // outside the aura takes it as today.
    register_script(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  do_special = function(self)\n"
        "    self:s_force_fright(10, 1, 0)\n"
        "    return true\n"
        "  end,\n"
        "})\n");
    hero->stats()->commands.clear();
    ASSERT_TRUE(og::script::hooks::do_special(
                    get_family_descriptor(FAMILY_SOLDIER), hero)
                    .has_value())
        << last_script_error();
    EXPECT_TRUE(hero->stats()->commands.empty())
        << "s_force_fright skips a fearless walker";
    banner->set_dead(1);
    EXPECT_FALSE(og::sim::fearless(*hero)) << "a fallen banner rallies nobody";
    ASSERT_TRUE(og::script::hooks::do_special(
                    get_family_descriptor(FAMILY_SOLDIER), hero)
                    .has_value())
        << last_script_error();
    EXPECT_TRUE(queues_a_flee_walk(*hero)) << "the scare lands as today";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A weapon whose family declares blocks_placement is solid for the three
// landing probes, exactly like a door, tree or boulder; a pack weapon that
// does not declare it stays as passable as a knife. The teleport case uses
// range 1, so every one of teleport_ranged's candidate spots overlaps the
// scenery: with the flag every try is refused and the hop fails.
TEST_F(KitSeams, blocks_placement_weapon_stops_teleport_spawn_and_floor_landing)
{
    const int wall_id = install_weapon(
        "og.family('weapon', { id = 'kitprobe:wall', wire_id = 'auto',\n"
        "  blocks_placement = true })\n"
        "og.family('weapon', { id = 'kitprobe:post', wire_id = 'auto' })\n",
        "kitprobe:wall", 2);
    const int post_id =
        og::families::resolve_family_string_id(Order::Weapon, "kitprobe:post");
    ASSERT_GE(wall_id, 22);
    ASSERT_GE(post_id, 22);
    ASSERT_TRUE(get_weapon_family_descriptor(wall_id)->blocks_placement);
    ASSERT_FALSE(get_weapon_family_descriptor(post_id)->blocks_placement);

    for (const bool solid : {true, false}) {
        SCOPED_TRACE(solid ? "blocks_placement = true" : "undeclared");
        TestGameWorld tw;
        GameWorld& world = tw.world();
        constexpr short kX = 160;
        constexpr short kY = 160;
        walker* hopper = add_living(tw, FAMILY_SOLDIER, 0, kX, kY);
        ASSERT_NE(nullptr, hopper);
        walker* scenery =
            add_pack_weapon(tw, solid ? wall_id : post_id, 1, kX, kY);
        ASSERT_NE(nullptr, scenery);
        ASSERT_EQ(1u, world.myobmap->walker_to_pos.count(scenery));

        EXPECT_EQ(!solid, og::sim::respawn_spot_clear(world, hopper, kX, kY, 0))
            << "respawn / spawn probe";
        EXPECT_EQ(!solid, world.floor_landing_clear(hopper, kX, kY, 0))
            << "floor landing probe";
        EXPECT_EQ(!solid, hopper->teleport_ranged(1))
            << "teleport_ranged: every candidate overlaps the scenery";
        if (solid) {
            EXPECT_EQ(kX, hopper->xpos()) << "a failed hop moves nobody";
            EXPECT_EQ(kY, hopper->ypos());
        }
    }
}

// on_kill(self, victim) runs on the KILLER's family (the head of the
// weapon/summon chain) for every living it kills: in melee, with a thrown
// weapon and with an explosion it owns. It runs after the victim's death()
// (the hook sees death_called), never for a non-living victim, never for a
// family that does not declare it, and never when the victim got back up
// inside death() (a warded skeleton's shape, staged here by a victim
// on_death that clears dead), and never when the head of the chain is not a
// living (a knife left owning itself after its thrower died).
TEST_F(KitSeams, on_kill_fires_for_melee_projectile_and_explosion_kills_of_a_declaring_family)
{
    og::test::ScopedHookFailureGuard guard;
    register_script(
        "og.register_hooks('living', 'core:soldier', {\n"
        "  on_kill = function(self, victim)\n"
        "    local after = 0\n"
        "    if victim:death_called() ~= 0 then\n"
        "      after = 1\n"
        "    end\n"
        "    og.log('kill', self:family(), victim:family(), victim:xpos(), after)\n"
        "  end,\n"
        "})\n"
        "og.register_hooks('living', 'core:golem', {\n"
        "  on_death = function(self)\n"
        "    self:set_dead(0)\n"
        "    return true\n"
        "  end,\n"
        "})\n");
    TestGameWorld tw;
    GameWorld& world = tw.world();

    walker* killer = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    ASSERT_NE(nullptr, killer);
    killer->set_damage(50.0f);

    const auto victim_at = [&](short x, int family = FAMILY_ORC) {
        walker* v = add_living(tw, family, 1, x, 200);
        if (v != nullptr) {
            v->stats()->set_hitpoints(1.0f);
            v->stats()->set_armor(0);
        }
        return v;
    };

    // Melee.
    walker* melee_victim = victim_at(100);
    ASSERT_NE(nullptr, melee_victim);
    EXPECT_TRUE(killer->attack(melee_victim));
    ASSERT_TRUE(melee_victim->dead());

    // A thrown weapon the soldier owns.
    walker* knife = world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_owner(killer);
    knife->set_team_num(0);
    knife->set_damage(50.0f);
    walker* thrown_victim = victim_at(140);
    ASSERT_NE(nullptr, thrown_victim);
    EXPECT_TRUE(knife->attack(thrown_victim));
    ASSERT_TRUE(thrown_victim->dead());

    // An explosion the soldier owns.
    walker* blast = world.add_ob(Order::FX, FAMILY_EXPLOSION);
    ASSERT_NE(nullptr, blast);
    blast->set_owner(killer);
    blast->set_team_num(0);
    blast->set_damage(50.0f);
    walker* blast_victim = victim_at(180);
    ASSERT_NE(nullptr, blast_victim);
    EXPECT_TRUE(blast->attack(blast_victim));
    ASSERT_TRUE(blast_victim->dead());

    // A knife whose thrower died owns itself (weap.cpp's "parent died" fix),
    // so the head of its chain is the knife, not a living: no kill is
    // reported. The knife family shares the soldier's family number, so
    // without the living-killer check the soldier's hook would run here
    // with the knife as self.
    walker* orphan = world.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, orphan);
    orphan->set_owner(orphan);
    orphan->set_team_num(0);
    orphan->set_damage(50.0f);
    walker* orphan_victim = victim_at(340);
    ASSERT_NE(nullptr, orphan_victim);
    EXPECT_TRUE(orphan->attack(orphan_victim));
    ASSERT_TRUE(orphan_victim->dead());

    // Not a living: a door chopped down is no kill.
    walker* door = world.add_weap_ob(Order::Weapon, FAMILY_DOOR);
    ASSERT_NE(nullptr, door);
    door->set_team_num(1);
    door->setxy(220, 200);
    door->stats()->set_hitpoints(1.0f);
    door->stats()->set_armor(0);
    EXPECT_TRUE(killer->attack(door));
    ASSERT_TRUE(door->dead());

    // A victim that gets back up inside death() was not killed.
    walker* riser = victim_at(260, FAMILY_GOLEM);
    ASSERT_NE(nullptr, riser);
    EXPECT_TRUE(killer->attack(riser));
    EXPECT_FALSE(riser->dead()) << "the on_death stand-in for a ward ran";

    // A family that does not declare on_kill dispatches nothing.
    walker* orc = add_living(tw, FAMILY_ORC, 1, 300, 32);
    ASSERT_NE(nullptr, orc);
    orc->set_damage(50.0f);
    walker* soldier_victim = add_living(tw, FAMILY_SOLDIER, 0, 300, 200);
    ASSERT_NE(nullptr, soldier_victim);
    soldier_victim->stats()->set_hitpoints(1.0f);
    soldier_victim->stats()->set_armor(0);
    EXPECT_TRUE(orc->attack(soldier_victim));
    ASSERT_TRUE(soldier_victim->dead());

    const std::string soldier = std::to_string(FAMILY_SOLDIER);
    const std::string orc_family = std::to_string(FAMILY_ORC);
    const std::vector<std::string> expected = {
        "kill\t" + soldier + "\t" + orc_family + "\t100\t1",
        "kill\t" + soldier + "\t" + orc_family + "\t140\t1",
        "kill\t" + soldier + "\t" + orc_family + "\t180\t1",
    };
    EXPECT_EQ(expected, script_log()) << last_script_error();
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// walker::attack refuses a hidden target outright (a dug-in skeleton, a
// ghost riding a body): no damage, no kill. Revealed, the same blow lands.
TEST_F(KitSeams, hidden_walker_cannot_be_attacked)
{
    TestGameWorld tw;
    walker* attacker = add_living(tw, FAMILY_SOLDIER, 0, 32, 32);
    walker* target = add_living(tw, FAMILY_ORC, 1, 64, 32);
    ASSERT_NE(nullptr, attacker);
    ASSERT_NE(nullptr, target);
    attacker->set_damage(50.0f);
    target->stats()->set_hitpoints(20.0f);
    target->stats()->set_armor(0);

    target->set_hidden(true);
    EXPECT_FALSE(attacker->attack(target)) << "a hidden walker cannot be hit";
    EXPECT_FLOAT_EQ(20.0f, target->stats()->hitpoints());
    EXPECT_FALSE(target->dead());

    target->set_hidden(false);
    EXPECT_TRUE(attacker->attack(target));
    EXPECT_LT(target->stats()->hitpoints(), 20.0f) << "revealed, it is hit";
}
