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
    if (w == nullptr || w->myguy != nullptr)
        return false;
    const Order order = w->query_order();
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
    if (ob == nullptr)
        return;
    const auto is_doomed = [&doomed](const walker* target) {
        return target != nullptr &&
               std::find(doomed.begin(), doomed.end(), target) != doomed.end();
    };

    if (is_doomed(ob->foe()))
        ob->set_foe(nullptr);
    if (is_doomed(ob->leader()))
        ob->set_leader(nullptr);
    if (is_doomed(ob->owner()))
        ob->set_owner(nullptr);
    if (is_doomed(ob->collide_ob()))
        ob->set_collide_ob(nullptr);
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
    for (const auto& uptr : world.oblist)
    {
        walker* const w = uptr.get();
        if (is_authored_troop(w))
            doomed.push_back(w);
    }
    if (doomed.empty())
        return 0;

    for (const auto& uptr : world.oblist)
        clear_refs_into(doomed, uptr.get());
    for (const auto& uptr : world.fxlist)
        clear_refs_into(doomed, uptr.get());
    for (const auto& uptr : world.weaplist)
        clear_refs_into(doomed, uptr.get());
    for (const auto& uptr : world.dead_list)
        clear_refs_into(doomed, uptr.get());

    int removed = 0;
    for (walker* const victim : doomed)
    {
        if (world.myobmap != nullptr)
            world.myobmap->remove(victim);
        removed += world.remove_ob(victim);
    }
    return removed;
}

} // namespace og::sim
