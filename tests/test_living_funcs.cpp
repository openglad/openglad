#include <openglad/core/stats.h>
#include <openglad/entities/living.h>
#include <openglad/entities/walker.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/guy.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

bool walkerIsAutoAttackable(walker* ob);

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// ---------------------------------------------------------------------------
// walkerIsAutoAttackable tests
// ---------------------------------------------------------------------------

void test_walkerIsAutoAttackable_soldier()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    bool result = walkerIsAutoAttackable(w.get());
    TEST_ASSERT(result, "soldier should be auto-attackable");

}
REGISTER_TEST(test_walkerIsAutoAttackable_soldier);

void test_walkerIsAutoAttackable_multiple_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = create_living(families[i]);
        if (w) {
            bool result = walkerIsAutoAttackable(w.get());
            (void)result; // Just verify no crash
        }
    }
}
REGISTER_TEST(test_walkerIsAutoAttackable_multiple_families);

// ---------------------------------------------------------------------------
// set_difficulty tests
// ---------------------------------------------------------------------------

void test_living_set_difficulty_basic()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    static_cast<living*>(w.get())->set_difficulty(5);
    // After set_difficulty, stats should have changed
    TEST_ASSERT(w->stats()->max_hitpoints > 0, "max HP should be positive after set_difficulty");

}
REGISTER_TEST(test_living_set_difficulty_basic);

void test_living_set_difficulty_level_10()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    float hp_low, hp_high;
    static_cast<living*>(w.get())->set_difficulty(1);
    hp_low = w->stats()->max_hitpoints;

    auto w2 = create_living(FAMILY_SOLDIER);
    static_cast<living*>(w2.get())->set_difficulty(10);
    hp_high = w2->stats()->max_hitpoints;

    TEST_ASSERT(hp_high > hp_low, "higher difficulty level should give more HP");

}
REGISTER_TEST(test_living_set_difficulty_level_10);

void test_living_set_difficulty_all_families()
{
    char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        auto w = create_living(families[i]);
        if (w) {
            static_cast<living*>(w.get())->set_difficulty(3);
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "every family should have positive HP after set_difficulty");
        }
    }
}
REGISTER_TEST(test_living_set_difficulty_all_families);

// ---------------------------------------------------------------------------
// check_special smoke tests
// ---------------------------------------------------------------------------

void test_living_check_special_soldier()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
    w->stats()->magicpoints = 100;
    w->stats()->max_magicpoints = 100;

    bool result = static_cast<living*>(w.get())->check_special();
    (void)result; // May return true or false

}
REGISTER_TEST(test_living_check_special_soldier);

void test_living_check_special_mage()
{
    auto w = create_living(FAMILY_MAGE);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->set_owned_myguy(std::make_unique<guy>(FAMILY_MAGE));
    w->stats()->magicpoints = 100;
    w->stats()->max_magicpoints = 100;

    bool result = static_cast<living*>(w.get())->check_special();
    (void)result;

}
REGISTER_TEST(test_living_check_special_mage);

// ---------------------------------------------------------------------------
// living::facing tests
// ---------------------------------------------------------------------------

void test_living_facing()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    short dir = static_cast<living*>(w.get())->facing(10, 0);
    TEST_ASSERT_EQ(FACE_RIGHT, (int)dir, "facing right");

    dir = static_cast<living*>(w.get())->facing(0, -10);
    TEST_ASSERT_EQ(FACE_UP, (int)dir, "facing up");

    dir = static_cast<living*>(w.get())->facing(-10, 10);
    TEST_ASSERT_EQ(FACE_DOWN_LEFT, (int)dir, "facing down-left");

}
REGISTER_TEST(test_living_facing);

// ---------------------------------------------------------------------------
// shove test
// ---------------------------------------------------------------------------

void test_living_shove_smoke()
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(a != nullptr, "create a should succeed");
    TEST_ASSERT(b != nullptr, "create b should succeed");

    a->team_num = 0;
    b->team_num = 0;
    a->setxy(100, 100);
    b->setxy(105, 100);

    static_cast<living*>(a.get())->shove(b.get(), 1, 0);

}
REGISTER_TEST(test_living_shove_smoke);

// ---------------------------------------------------------------------------
// living walk smoke
// ---------------------------------------------------------------------------

void test_living_walk_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->setxy(100, 100);

    static_cast<living*>(w.get())->walk(1, 0);
    static_cast<living*>(w.get())->walk(0, 1);
    static_cast<living*>(w.get())->walk(-1, -1);

}
REGISTER_TEST(test_living_walk_smoke);
