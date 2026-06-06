#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w)
        return nullptr;
    // Place at a valid position so obmap operations are safe.
    w->setxy(50, 50);
    return w;
}

TEST(LoaderAndWalker, loader_sets_soldier_defaults)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker(soldier) should succeed";

    ASSERT_EQ((int)FAMILY_KNIFE, (int)w->default_weapon()) << "soldier default weapon should be knife";
    ASSERT_EQ(2, (int)w->stats()->weapon_cost()) << "soldier weapon_cost should be set";
    ASSERT_EQ(25, (int)w->stats()->special_cost(1)) << "soldier charge cost";
    ASSERT_EQ(100, (int)w->stats()->special_cost(2)) << "soldier boomerang cost";
    ASSERT_EQ(120, (int)w->stats()->special_cost(3)) << "soldier whirlwind cost";
    ASSERT_EQ(150, (int)w->stats()->special_cost(4)) << "soldier disarm cost";

}


TEST(LoaderAndWalker, loader_sets_faerie_flags)
{
    auto w = create_living(FAMILY_FAERIE);
    ASSERT_TRUE(w != nullptr) << "create_walker(faerie) should succeed";

    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ANIMATE)) << "faerie should have BIT_ANIMATE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING)) << "faerie should have BIT_FLYING";
    ASSERT_EQ((int)FAMILY_SPRINKLE, (int)w->default_weapon()) << "faerie default weapon should be sprinkle";
    ASSERT_EQ(2, (int)w->stats()->weapon_cost()) << "faerie weapon_cost should be set";

}


TEST(LoaderAndWalker, loader_sets_ghost_flags)
{
    auto w = create_living(FAMILY_GHOST);
    ASSERT_TRUE(w != nullptr) << "create_walker(ghost) should succeed";

    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ANIMATE)) << "ghost should have BIT_ANIMATE";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_FLYING)) << "ghost should have BIT_FLYING";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_ETHEREAL)) << "ghost should have BIT_ETHEREAL";
    ASSERT_TRUE(w->stats()->query_bit_flags(BIT_NO_RANGED)) << "ghost should have BIT_NO_RANGED";
    ASSERT_EQ(0, (int)w->stats()->weapon_cost()) << "ghost melee should be free";

}


TEST(LoaderAndWalker, walker_attack_deals_damage_and_awards_score)
{
    auto attacker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(attacker != nullptr) << "create_walker(attacker) should succeed";
    ASSERT_TRUE(target != nullptr) << "create_walker(target) should succeed";

    attacker->set_team_num(0);
    target->set_team_num(1);

    attacker->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    attacker->myguy->teamnum = 0;
    attacker->myguy->exp = 0;
    attacker->myguy->total_hits = 0;
    attacker->myguy->total_shots = 0;

    target->stats()->set_armor(0);
    target->stats()->set_hitpoints(100);
    target->stats()->set_max_hitpoints(100);

    og::runtime::current_session->myscreen_->world_.m_score[0] = 0;

    bool ok = attacker->attack(target.get());
    ASSERT_TRUE(ok) << "attack should succeed against enemy living target";
    ASSERT_TRUE(target->stats()->hitpoints() < 100) << "attack should reduce target HP";
    ASSERT_TRUE(attacker->myguy->total_hits >= 1) << "attack should increment attacker hits";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->world_.m_score[0] > 0) << "attack should award score for team 0";

}

