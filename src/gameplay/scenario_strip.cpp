/* Classic-map scenario-troops strip. See scenario_strip.h. */

#include <openglad/gameplay/scenario_strip.h>

#include <openglad/core/order.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <vector>

namespace og::sim {

namespace {

bool is_authored_troop(const walker* w)
{
    // rule: With TROOPS: OWN (any strip request >= 1) on a classic (non-scripted) map, every
    //   map-authored living and generator is removed at the level's first tick; roster heroes
    //   (those with a guy record) and protected named NPCs stay; weapons, effects and treasure are
    //   untouched. A map with no authored troops changes nothing.
    // tests: ScenarioStripClassic.removes_authored_livings_and_generators
    // tests: ScenarioStripClassic.protected_npcs_survive
    // tests: ScenarioStripClassic.no_troops_removes_nothing
    // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
    // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
    // tests: SemScenarioStrip.the_troop_strip_leaves_weapons_effects_and_treasure_alone
    if (w == nullptr || w->myguy != nullptr)
        return false;
    const Order order = w->query_order();
    // rule: Part of the rule above that begins "With TROOPS: OWN (any strip request >= 1) on ..."
    // tests: ScenarioStripClassic.removes_authored_livings_and_generators
    // tests: ScenarioStripClassic.protected_npcs_survive
    // tests: ScenarioStripClassic.no_troops_removes_nothing
    // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
    // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
    // tests: SemScenarioStrip.the_troop_strip_leaves_weapons_effects_and_treasure_alone
    if (order != Order::Living && order != Order::Generator)
        return false;
    // Protected named NPCs survive every strip setting.
    return !w->save_all_protected();
}

// Null any cross-reference into `doomed` before the walkers are freed. The
// engine's own stale-pointer pass only clears pointers to walkers that are
// DEAD but still allocated; a pointer to a freed walker would be read as a
// use-after-free on the next tick.
void clear_refs_into(const std::vector<walker*>& doomed, walker* ob)
{
    const auto is_doomed = [&doomed](const walker* target) {
        return target != nullptr &&
               std::find(doomed.begin(), doomed.end(), target) != doomed.end();
    };

    // rule: When troops are stripped, every surviving walker, effect, weapon and corpse forgets any
    //   foe, leader, owner, collision partner or controller that pointed at a removed walker (no
    //   dangling target); references to survivors are kept.
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    if (is_doomed(ob->foe()))
        ob->set_foe(nullptr);
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    if (is_doomed(ob->leader()))
        ob->set_leader(nullptr);
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    if (is_doomed(ob->owner()))
        ob->set_owner(nullptr);
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    if (is_doomed(ob->collide_ob()))
        ob->set_collide_ob(nullptr);
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    if (statistics* stats = ob->stats();
        stats != nullptr && is_doomed(stats->controller()))
    {
        stats->set_controller(nullptr);
    }
}

} // namespace

int classic_strip_authored_troops(GameWorld& world)
{
    std::vector<walker*> doomed;
    // rule: Part of the rule above that begins "With TROOPS: OWN (any strip request >= 1) on ..."
    // tests: ScenarioStripClassic.removes_authored_livings_and_generators
    // tests: ScenarioStripClassic.protected_npcs_survive
    // tests: ScenarioStripClassic.no_troops_removes_nothing
    // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
    // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
    // tests: SemScenarioStrip.the_troop_strip_leaves_weapons_effects_and_treasure_alone
    for (const auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        // rule: Part of the rule above that begins "With TROOPS: OWN (any strip request >= 1) on
        //   ..."
        // tests: ScenarioStripClassic.removes_authored_livings_and_generators
        // tests: ScenarioStripClassic.protected_npcs_survive
        // tests: ScenarioStripClassic.no_troops_removes_nothing
        // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
        // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
        // tests: SemScenarioStrip.the_troop_strip_leaves_weapons_effects_and_treasure_alone
        if (is_authored_troop(w))
            doomed.push_back(w);
    }
    // rule: Part of the rule above that begins "With TROOPS: OWN (any strip request >= 1) on ..."
    // tests: ScenarioStripClassic.removes_authored_livings_and_generators
    // tests: ScenarioStripClassic.protected_npcs_survive
    // tests: ScenarioStripClassic.no_troops_removes_nothing
    // tests: ScenarioStripClassic.tick_latches_the_sweep_once_on_a_classic_map
    // tests: ScenarioStripClassic.scripted_maps_are_left_to_their_mode_script
    // tests: SemScenarioStrip.the_troop_strip_leaves_weapons_effects_and_treasure_alone
    if (doomed.empty())
        return 0;

    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    for (const auto& uptr : world.oblist)
        clear_refs_into(doomed, uptr.get());
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    for (const auto& uptr : world.fxlist)
        clear_refs_into(doomed, uptr.get());
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    for (const auto& uptr : world.weaplist)
        clear_refs_into(doomed, uptr.get());
    // rule: Part of the rule above that begins "When troops are stripped, every surviving walker,
    //   effect, weapon ..."
    // tests: ScenarioStripClassic.cross_references_into_removed_walkers_are_cleared
    // tests: ScenarioStripClassic.strip_clears_references_held_by_effects_weapons_and_corpses
    for (const auto& uptr : world.dead_list)
        clear_refs_into(doomed, uptr.get());

    int removed = 0;
    // rule: A stripped troop leaves no corpse (not moved to dead_list, so classic respawn cannot
    //   revive it) and leaves the collision table.
    // tests: SemScenarioStrip.a_stripped_troop_leaves_the_collision_table_and_no_corpse
    for (walker* const victim : doomed)
    {
        // rule: Part of the rule above that begins "A stripped troop leaves no corpse (not moved to
        //   ..."
        // tests: SemScenarioStrip.a_stripped_troop_leaves_the_collision_table_and_no_corpse
        if (world.myobmap != nullptr)
            world.myobmap->remove(victim);
        removed += world.remove_ob(victim);
    }
    return removed;
}

} // namespace og::sim
