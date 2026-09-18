#include <openglad/gameplay/guy.h>
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include "test_sim_random_scope.h"

#include <unordered_set>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

// Which stream this file pins, and why the old guard could not pin it:
// every og.rand in the mage/archmage hooks -- and the C++ `!rng(3)` on the
// generic hit path -- draws from current_game->world->rng_ (og_rand,
// src/gameplay/script/world_scripts.cpp:1032-1044), the SIM stream reached
// only through og::sim::set_sim_random_override() / ScopedSimRandom.
// GameContext::rng (what the deleted local RngGuard swapped) is never
// consulted here. The old oracle was vacuous for a second reason:
// current_special() == 1 is the living constructor's default
// (src/gameplay/living.cpp:52/59), so it held on the teleport arm AND on the
// retarget arm. The oracles below are the animation and targeting state that
// actually differ between the two arms.

static std::unordered_set<walker*> snapshot_ptrs(const std::list<std::unique_ptr<walker>>& lst)
{
    std::unordered_set<walker*> out;
    out.reserve(lst.size());
    for (auto& up : lst)
        out.insert(up.get());
    return out;
}

static void remove_new_leveldata_objects(LevelRuntimeData& level,
                                        const std::unordered_set<walker*>& ob_before,
                                        const std::unordered_set<walker*>& fx_before,
                                        const std::unordered_set<walker*>& weap_before)
{
    std::vector<walker*> to_remove;
    to_remove.reserve(level.world().oblist.size() + level.world().fxlist.size() + level.world().weaplist.size());

    for (auto& up : level.world().oblist)
        if (up && !ob_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().fxlist)
        if (up && !fx_before.contains(up.get()))
            to_remove.push_back(up.get());
    for (auto& up : level.world().weaplist)
        if (up && !weap_before.contains(up.get()))
            to_remove.push_back(up.get());

    for (walker* w : to_remove)
        level.remove_ob(w);
}

// Give a caster the shape both hooks branch on: a player-owned mage/archmage
// at level 3 (so `possible` covers slots 0..1) with slot 1 free to cast.
static void arm_caster(walker* w, int x, int y, float hitpoints)
{
    w->set_team_num(0);
    w->setxy(x, y);
    w->set_owned_myguy(std::make_unique<guy>(static_cast<int>(w->family())));
    w->stats()->set_level(3);
    w->stats()->set_max_hitpoints(100.0f);
    w->stats()->set_hitpoints(hitpoints);
    w->stats()->set_magicpoints(100.0f);
    w->stats()->set_special_cost(1, 0);
}

TEST(StatsHitResponseTeleport, stats_hit_response_mage_and_archmage_teleport_branches)
{
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_) << "myscreen exists";

    LevelRuntimeData& level = og::runtime::current_session->myscreen_->level_runtime_data();
    const auto ob_before = snapshot_ptrs(level.world().oblist);
    const auto fx_before = snapshot_ptrs(level.world().fxlist);
    const auto weap_before = snapshot_ptrs(level.world().weaplist);

    walker* foe = level.add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, foe) << "foe created";
    foe->set_team_num(1);
    foe->setxy(160, 100);

    // --- MAGE, teleport arm: hp 10 of 100 is below the 3/5 player threshold
    // and slot 1 is affordable (living-03-mage.lua:45-51).
    walker* mage = level.add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_NE(nullptr, mage) << "mage created";
    arm_caster(mage, 100, 100, 10.0f);
    ASSERT_NE(ANI_TELE_OUT, static_cast<int>(mage->ani_type()))
        << "precondition: the mage is not already mid-teleport";
    ASSERT_EQ(nullptr, mage->foe()) << "precondition: the mage has no foe yet";

    mage->stats()->hit_response(foe);

    EXPECT_EQ(ANI_TELE_OUT, static_cast<int>(mage->ani_type()))
        << "below 3/5 hp with TELEPORT affordable the mage hook casts: "
           "walker::special() -> mage teleport() sets ANI_TELE_OUT "
           "(living-03-mage.lua:45-51, :71-80)";
    EXPECT_EQ(nullptr, mage->foe()) << "the teleport arm never retargets";
    EXPECT_EQ(0.0f, mage->busy()) << "the hook force-allows the cast with busy = 0";
    EXPECT_EQ(1, mage->current_special())
        << "slot 1 is TELEPORT (also the living default: the ani_type line above "
           "is the oracle that separates the two arms)";

    // --- MAGE control, else arm: full hp is above the threshold, so the hook
    // takes the attacker as its foe instead of casting.
    walker* mage2 = level.add_ob(Order::Living, FAMILY_MAGE);
    ASSERT_NE(nullptr, mage2) << "control mage created";
    arm_caster(mage2, 140, 100, 100.0f);
    ASSERT_EQ(nullptr, mage2->foe()) << "precondition: the control mage has no foe yet";

    mage2->stats()->hit_response(foe);

    EXPECT_NE(ANI_TELE_OUT, static_cast<int>(mage2->ani_type()))
        << "above the 3/5 threshold the mage hook does not teleport";
    EXPECT_EQ(foe, mage2->foe()) << "the else arm takes the attacker as foe";
    EXPECT_EQ(mage2, foe->foe()) << "and points the attacker back at the mage";
    EXPECT_EQ(15000u, mage2->stats()->last_distance())
        << "the else arm resets the distance caches to 15000";
    EXPECT_EQ(15000, mage2->stats()->current_distance())
        << "the else arm resets the distance caches to 15000";

    // --- ARCHMAGE, teleport arm. living-17-archmage.lua:73-76 gates the
    // teleport on `og.rand(3) ~= 0` -- a SIM-stream draw; FixedRandom(1)
    // answers 1 % 3 == 1, which takes it.
    walker* arch = level.add_ob(Order::Living, FAMILY_ARCHMAGE);
    ASSERT_NE(nullptr, arch) << "archmage created";
    arm_caster(arch, 120, 100, 10.0f);
    ASSERT_NE(ANI_TELE_OUT, static_cast<int>(arch->ani_type()))
        << "precondition: the archmage is not already mid-teleport";
    ASSERT_EQ(nullptr, arch->foe()) << "precondition: the archmage has no foe yet";

    {
        FixedRandom one(1);
        ScopedSimRandom sim(&one);
        arch->stats()->hit_response(foe);
    }

    EXPECT_EQ(ANI_TELE_OUT, static_cast<int>(arch->ani_type()))
        << "below 3/5 hp with slot 1 affordable and og.rand(3) == 1 the archmage "
           "casts TELEPORT (living-17-archmage.lua:73-79, :131-138)";
    EXPECT_EQ(nullptr, arch->foe()) << "the teleport arm never retargets";
    EXPECT_EQ(0.0f, arch->busy()) << "the hook force-allows the cast with busy = 0";

    // --- ARCHMAGE control, else arm: same low-hp shape, but og.rand(3) == 0
    // closes the gate. At level 3 `possible` holds only slots 0 and 1, so the
    // else arm's possible[3]/possible[2] follow-ups are skipped and the only
    // observable is the retarget.
    walker* arch2 = level.add_ob(Order::Living, FAMILY_ARCHMAGE);
    ASSERT_NE(nullptr, arch2) << "control archmage created";
    arm_caster(arch2, 180, 100, 10.0f);
    ASSERT_EQ(nullptr, arch2->foe()) << "precondition: the control archmage has no foe yet";

    {
        FixedRandom zero(0);
        ScopedSimRandom sim0(&zero);
        arch2->stats()->hit_response(foe);
    }

    EXPECT_NE(ANI_TELE_OUT, static_cast<int>(arch2->ani_type()))
        << "og.rand(3) == 0 closes the teleport gate even below the hp threshold";
    EXPECT_EQ(foe, arch2->foe()) << "the else arm takes the attacker as foe";
    EXPECT_EQ(arch2, foe->foe()) << "and points the attacker back at the archmage";
    EXPECT_EQ(15000u, arch2->stats()->last_distance())
        << "the else arm resets the distance caches to 15000";
    EXPECT_EQ(15000, arch2->stats()->current_distance())
        << "the else arm resets the distance caches to 15000";

    remove_new_leveldata_objects(level, ob_before, fx_before, weap_before);
}
