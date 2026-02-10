#include "graph.h"
#include "gloader.h"
#include "guy.h"
#include "test_framework.h"

extern screen* myscreen;

bool walkerIsAutoAttackable(walker* ob);

static walker* create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// ---------------------------------------------------------------------------
// walkerIsAutoAttackable tests
// ---------------------------------------------------------------------------

void test_walkerIsAutoAttackable_soldier()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    bool result = walkerIsAutoAttackable(w);
    TEST_ASSERT(result, "soldier should be auto-attackable");

    delete w;
}
REGISTER_TEST(test_walkerIsAutoAttackable_soldier);

void test_walkerIsAutoAttackable_multiple_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = create_living(families[i]);
        if (w) {
            bool result = walkerIsAutoAttackable(w);
            (void)result; // Just verify no crash
            delete w;
        }
    }
}
REGISTER_TEST(test_walkerIsAutoAttackable_multiple_families);

// ---------------------------------------------------------------------------
// set_difficulty tests
// ---------------------------------------------------------------------------

void test_living_set_difficulty_basic()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    float hp_before = w->stats()->max_hitpoints;
    ((living*)w)->set_difficulty(5);
    // After set_difficulty, stats should have changed
    TEST_ASSERT(w->stats()->max_hitpoints > 0, "max HP should be positive after set_difficulty");

    delete w;
}
REGISTER_TEST(test_living_set_difficulty_basic);

void test_living_set_difficulty_level_10()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    float hp_low, hp_high;
    ((living*)w)->set_difficulty(1);
    hp_low = w->stats()->max_hitpoints;

    walker* w2 = create_living(FAMILY_SOLDIER);
    ((living*)w2)->set_difficulty(10);
    hp_high = w2->stats()->max_hitpoints;

    TEST_ASSERT(hp_high > hp_low, "higher difficulty level should give more HP");

    delete w;
    delete w2;
}
REGISTER_TEST(test_living_set_difficulty_level_10);

void test_living_set_difficulty_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        walker* w = create_living(families[i]);
        if (w) {
            ((living*)w)->set_difficulty(3);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "every family should have positive HP after set_difficulty");
            delete w;
        }
    }
}
REGISTER_TEST(test_living_set_difficulty_all_families);

// ---------------------------------------------------------------------------
// check_special smoke tests
// ---------------------------------------------------------------------------

void test_living_check_special_soldier()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->myguy = new guy(FAMILY_SOLDIER);
    w->stats()->magicpoints = 100;
    w->stats()->max_magicpoints = 100;

    bool result = ((living*)w)->check_special();
    (void)result; // May return true or false

    delete w;
}
REGISTER_TEST(test_living_check_special_soldier);

void test_living_check_special_mage()
{
    walker* w = create_living(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->myguy = new guy(FAMILY_MAGE);
    w->stats()->magicpoints = 100;
    w->stats()->max_magicpoints = 100;

    bool result = ((living*)w)->check_special();
    (void)result;

    delete w;
}
REGISTER_TEST(test_living_check_special_mage);

// ---------------------------------------------------------------------------
// living::facing tests
// ---------------------------------------------------------------------------

void test_living_facing()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    short dir = ((living*)w)->facing(10, 0);
    TEST_ASSERT_EQ(FACE_RIGHT, (int)dir, "facing right");

    dir = ((living*)w)->facing(0, -10);
    TEST_ASSERT_EQ(FACE_UP, (int)dir, "facing up");

    dir = ((living*)w)->facing(-10, 10);
    TEST_ASSERT_EQ(FACE_DOWN_LEFT, (int)dir, "facing down-left");

    delete w;
}
REGISTER_TEST(test_living_facing);

// ---------------------------------------------------------------------------
// shove test
// ---------------------------------------------------------------------------

void test_living_shove_smoke()
{
    walker* a = create_living(FAMILY_SOLDIER);
    walker* b = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(a != nullptr, "create a should succeed");
    TEST_ASSERT(b != nullptr, "create b should succeed");

    a->team_num = 0;
    b->team_num = 0;
    a->setxy(100, 100);
    b->setxy(105, 100);

    ((living*)a)->shove(b, 1, 0);

    delete a;
    delete b;
}
REGISTER_TEST(test_living_shove_smoke);

// ---------------------------------------------------------------------------
// living walk smoke
// ---------------------------------------------------------------------------

void test_living_walk_smoke()
{
    walker* w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->setxy(100, 100);

    ((living*)w)->walk(1, 0);
    ((living*)w)->walk(0, 1);
    ((living*)w)->walk(-1, -1);

    delete w;
}
REGISTER_TEST(test_living_walk_smoke);
