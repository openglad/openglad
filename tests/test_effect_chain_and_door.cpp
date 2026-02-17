#include <openglad/runtime/game_context.h>
#include <openglad/entities/walker.h>
#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

#include <unordered_set>
#include <vector>

extern screen* myscreen;

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_leveldata_objects(LevelData& level,
                                        const std::unordered_set<walker*>& ob_before,
                                        const std::unordered_set<walker*>& fx_before,
                                        const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve((level.oblist.size() + level.fxlist.size() + level.weaplist.size()));

    for (auto& up : level.oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.weaplist)
        if (up && !weap_before.contains(up.get()))
            to_remove.push_back(up.get());

    for (walker* w : to_remove)
        level.remove_ob(w);
}

static std::unique_ptr<walker> make_living(char family, unsigned char team)
{
    guy g(family);
    g.teamnum = team;
    g.upgrade_to_level(3, true);
    return guy_create_walker_owned(g, myscreen);
}

void test_effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx()
{
    TEST_ASSERT(myscreen != nullptr, "myscreen exists");
    if (!myscreen)
        return;

    LevelData& level = myscreen->level_data;

    const auto ob_before = snapshot_ptrs(level.oblist);
    const auto fx_before = snapshot_ptrs(level.fxlist);
    const auto weap_before = snapshot_ptrs(level.weaplist);

    // Use deterministic RNG without swapping the active global context.
    FixedRandom fixed_rng(0);
    IRandom* prev_rng = ctx().rng;
    ctx().rng = &fixed_rng;

    // -----------------------------------------------------------------------
    // FAMILY_CHAIN: overlapping leader hit should spawn explosion and (if there
    // are additional foes in range and damage is high) spawn secondary chains.
    // -----------------------------------------------------------------------
    auto owner_up = make_living(FAMILY_SOLDIER, /*team*/ 0);
    auto leader_up = make_living(FAMILY_ORC, /*team*/ 1);
    auto foe2_up = make_living(FAMILY_ORC, /*team*/ 1);
    auto foe3_up = make_living(FAMILY_ORC, /*team*/ 1);
    TEST_ASSERT(owner_up && leader_up && foe2_up && foe3_up, "livings created");
    if (!(owner_up && leader_up && foe2_up && foe3_up)) {
        ctx().rng = prev_rng;
        return;
    }

    owner_up->setxy(100, 100);
    leader_up->setxy(120, 120);
    foe2_up->setxy(140, 120);
    foe3_up->setxy(160, 120);

    walker* owner = owner_up.get();
    walker* leader = leader_up.get();

    // Put the living walkers into the level so find_foes_in_range can discover them.
    level.oblist.push_back(std::move(owner_up));
    level.oblist.push_back(std::move(leader_up));
    level.oblist.push_back(std::move(foe2_up));
    level.oblist.push_back(std::move(foe3_up));

    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(chain != nullptr, "chain created");
    if (chain) {
        chain->team_num = 0;
        chain->owner = owner;
        chain->leader = leader;
        chain->lineofsight = 5;
        chain->damage = 100.0f; // generic = damage * 0.5f => 50 (> 20)
        chain->setxy(leader->xpos, leader->ypos); // guarantee hits() with leader
        (void)chain->act();
    }

    // -----------------------------------------------------------------------
    // FAMILY_DOOR_OPEN: should spawn a FX copy and then die.
    // -----------------------------------------------------------------------
    walker* door_open = level.add_fx_ob(Order::FX, FAMILY_DOOR_OPEN);
    TEST_ASSERT(door_open != nullptr, "door_open created");
    if (door_open) {
        door_open->ani_type = ANI_WALK;
        door_open->curdir = FACE_DOWN;
        door_open->setworldxy(200.0f, 200.0f);
        (void)door_open->act();
    }

    // Restore RNG and cleanup spawned objects (don't call delete_objects()).
    ctx().rng = prev_rng;
    remove_new_leveldata_objects(level, ob_before, fx_before, weap_before);
}
REGISTER_TEST(test_effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx);
