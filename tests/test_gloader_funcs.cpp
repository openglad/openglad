#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// create_walker for all order+family combos
// exercises gloader's create_walker (the biggest function)
// ---------------------------------------------------------------------------

void test_gloader_create_living_all()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = l->create_walker(Order::Living, families[i], myscreen);
        TEST_ASSERT(w != nullptr, "create_walker should succeed for all living families");
        TEST_ASSERT(w->stats() != nullptr, "stats should exist");
        TEST_ASSERT(w->query_order() == Order::Living, "order should be Living");
        TEST_ASSERT_EQ((int)families[i], (int)w->query_family(), "family should match");
        delete w;
    }
}
REGISTER_TEST(test_gloader_create_living_all);

void test_gloader_create_weapon_families()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short weap_families[] = { FAMILY_KNIFE, FAMILY_ROCK, FAMILY_ARROW,
                              FAMILY_FIREBALL, FAMILY_LIGHTNING, FAMILY_METEOR };
    for (int i = 0; i < 6; i++) {
        walker* w = l->create_walker(Order::Weapon, weap_families[i], myscreen);
        if (w) {
            TEST_ASSERT(w->query_order() == Order::Weapon, "order should be Weapon");
            delete w;
        }
    }
}
REGISTER_TEST(test_gloader_create_weapon_families);

void test_gloader_create_treasure()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    walker* w = l->create_walker(Order::Treasure, FAMILY_STAIN, myscreen);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Treasure, "order should be Treasure");
        delete w;
    }
}
REGISTER_TEST(test_gloader_create_treasure);

void test_gloader_create_effect()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    walker* w = l->create_walker(Order::FX, FAMILY_EXPLOSION, myscreen);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::FX, "order should be FX");
        delete w;
    }
}
REGISTER_TEST(test_gloader_create_effect);

void test_gloader_create_generator()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    walker* w = l->create_walker(Order::Generator, FAMILY_TENT, myscreen);
    if (w) {
        TEST_ASSERT(w->query_order() == Order::Generator, "order should be Generator");
        delete w;
    }
}
REGISTER_TEST(test_gloader_create_generator);

// ---------------------------------------------------------------------------
// set_derived_stats
// ---------------------------------------------------------------------------

void test_gloader_set_derived_stats_all()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = l->create_walker(Order::Living, families[i], myscreen);
        if (w) {
            l->set_derived_stats(w, Order::Living, families[i]);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP should be set");
            delete w;
        }
    }
}
REGISTER_TEST(test_gloader_set_derived_stats_all);

// ---------------------------------------------------------------------------
// set_walker (sets order/family on existing walker)
// ---------------------------------------------------------------------------

void test_gloader_set_walker()
{
    loader* l = myscreen->level_data.myloader.get();
    TEST_ASSERT(l != nullptr, "loader exists");

    walker* w = l->create_walker(Order::Living, FAMILY_SOLDIER, myscreen);
    TEST_ASSERT(w != nullptr, "walker created");

    myscreen->set_walker(w, Order::Living, FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)w->query_family(), "family should change to mage");

    delete w;
}
REGISTER_TEST(test_gloader_set_walker);
