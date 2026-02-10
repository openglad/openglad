#include "graph.h"
#include "gloader.h"
#include "guy.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
    if (!w)
        return nullptr;
    // Place at a valid position so obmap operations are safe.
    w->setxy(50, 50);
    return w;
}

void test_loader_sets_soldier_defaults()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker(soldier) should succeed");

    TEST_ASSERT_EQ((int)FAMILY_KNIFE, (int)w->default_weapon, "soldier default weapon should be knife");
    TEST_ASSERT_EQ(2, (int)w->stats()->weapon_cost, "soldier weapon_cost should be set");
    TEST_ASSERT_EQ(25, (int)w->stats()->special_cost[1], "soldier charge cost");
    TEST_ASSERT_EQ(100, (int)w->stats()->special_cost[2], "soldier boomerang cost");
    TEST_ASSERT_EQ(120, (int)w->stats()->special_cost[3], "soldier whirlwind cost");
    TEST_ASSERT_EQ(150, (int)w->stats()->special_cost[4], "soldier disarm cost");

    delete w;
}
REGISTER_TEST(test_loader_sets_soldier_defaults);

void test_loader_sets_faerie_flags()
{
    walker* w = create_living(FAMILY_FAERIE);
    TEST_ASSERT(w != nullptr, "create_walker(faerie) should succeed");

    TEST_ASSERT(w->stats()->query_bit_flags(BIT_ANIMATE), "faerie should have BIT_ANIMATE");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_FLYING), "faerie should have BIT_FLYING");
    TEST_ASSERT_EQ((int)FAMILY_SPRINKLE, (int)w->default_weapon, "faerie default weapon should be sprinkle");
    TEST_ASSERT_EQ(2, (int)w->stats()->weapon_cost, "faerie weapon_cost should be set");

    delete w;
}
REGISTER_TEST(test_loader_sets_faerie_flags);

void test_loader_sets_ghost_flags()
{
    walker* w = create_living(FAMILY_GHOST);
    TEST_ASSERT(w != nullptr, "create_walker(ghost) should succeed");

    TEST_ASSERT(w->stats()->query_bit_flags(BIT_ANIMATE), "ghost should have BIT_ANIMATE");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_FLYING), "ghost should have BIT_FLYING");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_ETHEREAL), "ghost should have BIT_ETHEREAL");
    TEST_ASSERT(w->stats()->query_bit_flags(BIT_NO_RANGED), "ghost should have BIT_NO_RANGED");
    TEST_ASSERT_EQ(0, (int)w->stats()->weapon_cost, "ghost melee should be free");

    delete w;
}
REGISTER_TEST(test_loader_sets_ghost_flags);

void test_walker_attack_deals_damage_and_awards_score()
{
    walker* attacker = create_living(FAMILY_SOLDIER);
    walker* target = create_living(FAMILY_SMALL_SLIME);
    TEST_ASSERT(attacker != nullptr, "create_walker(attacker) should succeed");
    TEST_ASSERT(target != nullptr, "create_walker(target) should succeed");

    attacker->team_num = 0;
    target->team_num = 1;

    attacker->myguy = new guy(FAMILY_SOLDIER);
    attacker->myguy->teamnum = 0;
    attacker->myguy->exp = 0;
    attacker->myguy->total_hits = 0;
    attacker->myguy->total_shots = 0;

    target->stats()->armor = 0;
    target->stats()->hitpoints = 100;
    target->stats()->max_hitpoints = 100;

    myscreen->save_data.m_score[0] = 0;

    bool ok = attacker->attack(target);
    TEST_ASSERT(ok, "attack should succeed against enemy living target");
    TEST_ASSERT(target->stats()->hitpoints < 100, "attack should reduce target HP");
    TEST_ASSERT(attacker->myguy->total_hits >= 1, "attack should increment attacker hits");
    TEST_ASSERT(myscreen->save_data.m_score[0] > 0, "attack should award score for team 0");

    delete attacker; // deletes attacker->myguy as well
    delete target;
}
REGISTER_TEST(test_walker_attack_deals_damage_and_awards_score);
