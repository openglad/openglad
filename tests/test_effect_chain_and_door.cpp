#include <openglad/platform/game_context.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/guy.h>
#include <openglad/platform/guy_create.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"

#include <unordered_set>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_leveldata_objects(og::gameplay::GameWorld& level,
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
    return guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
}

void test_effect_chain_hits_leader_spawns_explosion_and_secondary_chains_and_door_open_spawns_fx()
{
    TEST_ASSERT(og::runtime::current_session->myscreen_ != nullptr, "myscreen exists");
    if (!og::runtime::current_session->myscreen_)
        return;

    auto& level = og::runtime::current_session->myscreen_->world();

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

void test_effect_chain_early_exit_and_movement_branches()
{
    TEST_ASSERT(og::runtime::current_session->myscreen_ != nullptr, "myscreen exists");
    if (!og::runtime::current_session->myscreen_)
        return;

    auto& level = og::runtime::current_session->myscreen_->world();

    // Missing leader should kill the chain immediately.
    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(chain != nullptr, "chain created");
    if (chain) {
        chain->owner = nullptr;
        chain->leader = nullptr;
        chain->lineofsight = 0;
        chain->dead = 0;
        (void)chain->act();
        TEST_ASSERT(chain->dead == 1, "chain without leader/owner should die");
    }

    // Non-hit movement path: chain should move toward leader and consume LOS.
    walker* owner = level.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader = level.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(owner != nullptr && leader != nullptr, "owner and leader created");
    if (!(owner && leader))
        return;

    owner->setxy(20, 20);
    leader->setxy(260, 180);

    walker* moving_chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(moving_chain != nullptr, "moving chain created");
    if (!moving_chain)
        return;

    moving_chain->owner = owner;
    moving_chain->leader = leader;
    moving_chain->lineofsight = 5;
    moving_chain->setxy(40, 40);
    const short x_before = moving_chain->xpos;
    const short y_before = moving_chain->ypos;
    (void)moving_chain->act();
    TEST_ASSERT(moving_chain->lineofsight == 4, "movement path should decrement lineofsight");
    TEST_ASSERT(moving_chain->xpos != x_before || moving_chain->ypos != y_before,
                "movement path should move toward leader");

    level.delete_objects();
}
REGISTER_TEST(test_effect_chain_early_exit_and_movement_branches);

void test_effect_chain_movement_axis_delta_branches()
{
    TEST_ASSERT(og::runtime::current_session->myscreen_ != nullptr, "myscreen exists");
    if (!og::runtime::current_session->myscreen_)
        return;

    auto& level = og::runtime::current_session->myscreen_->world();
    level.delete_objects();

    walker* owner = level.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* leader = level.add_ob(Order::Living, FAMILY_ORC);
    walker* chain = level.add_fx_ob(Order::FX, FAMILY_CHAIN);
    TEST_ASSERT(owner && leader && chain, "owner/leader/chain created");
    if (!(owner && leader && chain))
        return;

    chain->owner = owner;
    chain->leader = leader;
    chain->lineofsight = 20;
    chain->setxy(200, 200);

    // leader x greater/y less: xd positive, yd negative path
    leader->setxy(240, 120);
    (void)chain->act();

    // leader x less/y greater: xd negative, yd positive path
    leader->setxy(80, 260);
    (void)chain->act();

    // x equal/y less: x branch skipped, y negative path
    leader->setxy(chain->xpos, static_cast<short>(chain->ypos - 20));
    (void)chain->act();

    // x equal/y equal: near-distance center_on path
    leader->setxy(chain->xpos, chain->ypos);
    (void)chain->act();

    TEST_ASSERT(chain->lineofsight < 20, "movement branch should consume lineofsight across acts");
    level.delete_objects();
}
REGISTER_TEST(test_effect_chain_movement_axis_delta_branches);
