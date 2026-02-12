#include "graph.h"
#include "entities/guy.h"
#include "data/gloader.h"
#include "test_framework.h"

extern screen* myscreen;

static walker* make_living(char family, short level = 3)
{
    guy g(family);
    g.upgrade_to_level(level, true);
    walker* w = g.create_walker(myscreen);
    if (w) w->setxy(100, 100);
    return w;
}

// ---------------------------------------------------------------------------
// set_difficulty for all families - exercises the big switch (living.cpp)
// ---------------------------------------------------------------------------

void test_living_set_difficulty_levels()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        for (int level = 1; level <= 5; level++) {
            loader* l = myscreen->level_data.myloader.get();
            if (!l) continue;
            walker* w = l->create_walker(Order::Living, families[i], myscreen);
            if (w) {
                static_cast<living*>(w)->set_difficulty(level);
                TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP positive for all families at all levels");
                delete w;
            }
        }
    }
}
REGISTER_TEST(test_living_set_difficulty_levels);

// ---------------------------------------------------------------------------
// check_special for all families - exercises the big switch (~143 lines)
// ---------------------------------------------------------------------------

void test_living_check_special_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };

    for (int i = 0; i < 14; i++) {
        walker* w = make_living(families[i]);
        if (w) {
            w->stats()->magicpoints = 100;
            w->stats()->max_magicpoints = 100;
            bool result = static_cast<living*>(w)->check_special();
            (void)result; // just exercise the code path
            delete w;
        }
    }
}
REGISTER_TEST(test_living_check_special_all_families);

// ---------------------------------------------------------------------------
// living::act smoke test
// ---------------------------------------------------------------------------

void test_living_act_control()
{
    walker* w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_CONTROL);
    bool result = w->act();
    TEST_ASSERT(result, "ACT_CONTROL should return true");
    delete w;
}
REGISTER_TEST(test_living_act_control);

void test_living_act_random()
{
    walker* w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->act();
    delete w;
}
REGISTER_TEST(test_living_act_random);

void test_living_act_guard()
{
    walker* w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_GUARD);
    w->act();
    delete w;
}
REGISTER_TEST(test_living_act_guard);

// ---------------------------------------------------------------------------
// living::facing for all 8 directions
// ---------------------------------------------------------------------------

void test_living_facing_all_directions()
{
    walker* w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    // right
    TEST_ASSERT_EQ(FACE_RIGHT, (int)static_cast<living*>(w)->facing(10, 0), "right");
    // left
    TEST_ASSERT_EQ(FACE_LEFT, (int)static_cast<living*>(w)->facing(-10, 0), "left");
    // up
    TEST_ASSERT_EQ(FACE_UP, (int)static_cast<living*>(w)->facing(0, -10), "up");
    // down
    TEST_ASSERT_EQ(FACE_DOWN, (int)static_cast<living*>(w)->facing(0, 10), "down");
    // diagonals
    static_cast<living*>(w)->facing(10, -10);
    static_cast<living*>(w)->facing(-10, -10);
    static_cast<living*>(w)->facing(10, 10);
    static_cast<living*>(w)->facing(-10, 10);

    delete w;
}
REGISTER_TEST(test_living_facing_all_directions);

// ---------------------------------------------------------------------------
// shove between allies
// ---------------------------------------------------------------------------

void test_living_shove_movement()
{
    walker* a = make_living(FAMILY_SOLDIER);
    walker* b = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->team_num = 0;
    b->team_num = 0;
    a->setxy(100, 100);
    b->setxy(105, 100);

    // Shove in all cardinal directions
    static_cast<living*>(a)->shove(b, 1, 0);
    static_cast<living*>(a)->shove(b, -1, 0);
    static_cast<living*>(a)->shove(b, 0, 1);
    static_cast<living*>(a)->shove(b, 0, -1);

    delete a;
    delete b;
}
REGISTER_TEST(test_living_shove_movement);

// ---------------------------------------------------------------------------
// living walk with multiple families
// ---------------------------------------------------------------------------

void test_living_walk_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC };

    for (int i = 0; i < 6; i++) {
        walker* w = make_living(families[i]);
        if (w) {
            w->setxy(100, 100);
            static_cast<living*>(w)->walk(1, 0);
            static_cast<living*>(w)->walk(-1, 0);
            static_cast<living*>(w)->walk(0, 1);
            static_cast<living*>(w)->walk(0, -1);
            delete w;
        }
    }
}
REGISTER_TEST(test_living_walk_all_families);
