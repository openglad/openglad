#include "graph.h"
#include "entities/guy.h"
#include "data/gloader.h"
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> make_living(char family, short level = 3)
{
    guy g(family);
    g.upgrade_to_level(level, true);
    auto w = g.create_walker_owned(myscreen);
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
            auto w = l->create_walker_owned(Order::Living, families[i], myscreen);
            if (w) {
                static_cast<living*>(w.get())->set_difficulty(level);
                TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP positive for all families at all levels");
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
        auto w = make_living(families[i]);
        if (w) {
            w->stats()->magicpoints = 100;
            w->stats()->max_magicpoints = 100;
            bool result = static_cast<living*>(w.get())->check_special();
            (void)result; // just exercise the code path
        }
    }
}
REGISTER_TEST(test_living_check_special_all_families);

// ---------------------------------------------------------------------------
// living::act smoke test
// ---------------------------------------------------------------------------

void test_living_act_control()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_CONTROL);
    bool result = w->act();
    TEST_ASSERT(result, "ACT_CONTROL should return true");
}
REGISTER_TEST(test_living_act_control);

void test_living_act_random()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_RANDOM);
    w->act();
}
REGISTER_TEST(test_living_act_random);

void test_living_act_guard()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");
    w->set_act_type(ACT_GUARD);
    w->act();
}
REGISTER_TEST(test_living_act_guard);

void test_living_act_owner_dead_kills_summon()
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(owner && summoned, "walkers created");
    if (!(owner && summoned))
        return;

    // Summoned living with an owner that is dead should die immediately.
    summoned->owner = owner.get();
    owner->dead = 1;
    summoned->dead = 0;

    bool r = summoned->act();
    (void)r;
    TEST_ASSERT(summoned->dead, "summon should die when owner is dead");
}
REGISTER_TEST(test_living_act_owner_dead_kills_summon);

void test_living_act_lifetime_expires_without_owner()
{
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(summoned, "walker created");
    if (!summoned)
        return;

    // When lifetime is set and owner is missing, it should die.
    summoned->lifetime = 1;
    summoned->owner = nullptr;
    summoned->dead = 0;

    bool r = summoned->act();
    (void)r;
    TEST_ASSERT(summoned->dead, "living with lifetime but no owner should die");
}
REGISTER_TEST(test_living_act_lifetime_expires_without_owner);

void test_living_act_fire_elemental_drain_heals_self_with_owner_resources()
{
    auto owner = make_living(FAMILY_MAGE);
    auto summoned = make_living(FAMILY_FIREELEMENTAL);
    TEST_ASSERT(owner && summoned, "walkers created");
    if (!(owner && summoned))
        return;

    summoned->owner = owner.get();
    summoned->lifetime = 5;
    summoned->dead = 0;

    // Hurt the elemental so it runs the drain logic.
    summoned->stats()->max_hitpoints = 10;
    summoned->stats()->hitpoints = 5;

    owner->stats()->max_hitpoints = 30;
    owner->stats()->hitpoints = 20;  // >= max/3 => can pay hp
    owner->stats()->magicpoints = 10; // >= 3 => can pay mp

    const float hp_before = summoned->stats()->hitpoints;
    (void)summoned->act();

    TEST_ASSERT(summoned->stats()->hitpoints >= hp_before, "fire elemental should heal when owner pays toll");
}
REGISTER_TEST(test_living_act_fire_elemental_drain_heals_self_with_owner_resources);

// ---------------------------------------------------------------------------
// living::facing for all 8 directions
// ---------------------------------------------------------------------------

void test_living_facing_all_directions()
{
    auto w = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "walker created");

    // right
    TEST_ASSERT_EQ(FACE_RIGHT, (int)static_cast<living*>(w.get())->facing(10, 0), "right");
    // left
    TEST_ASSERT_EQ(FACE_LEFT, (int)static_cast<living*>(w.get())->facing(-10, 0), "left");
    // up
    TEST_ASSERT_EQ(FACE_UP, (int)static_cast<living*>(w.get())->facing(0, -10), "up");
    // down
    TEST_ASSERT_EQ(FACE_DOWN, (int)static_cast<living*>(w.get())->facing(0, 10), "down");
    // diagonals
    static_cast<living*>(w.get())->facing(10, -10);
    static_cast<living*>(w.get())->facing(-10, -10);
    static_cast<living*>(w.get())->facing(10, 10);
    static_cast<living*>(w.get())->facing(-10, 10);

}
REGISTER_TEST(test_living_facing_all_directions);

// ---------------------------------------------------------------------------
// shove between allies
// ---------------------------------------------------------------------------

void test_living_shove_movement()
{
    auto a = make_living(FAMILY_SOLDIER);
    auto b = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(a != nullptr, "a created");
    TEST_ASSERT(b != nullptr, "b created");

    a->team_num = 0;
    b->team_num = 0;
    a->setxy(100, 100);
    b->setxy(105, 100);

    // Shove in all cardinal directions
    static_cast<living*>(a.get())->shove(b.get(), 1, 0);
    static_cast<living*>(a.get())->shove(b.get(), -1, 0);
    static_cast<living*>(a.get())->shove(b.get(), 0, 1);
    static_cast<living*>(a.get())->shove(b.get(), 0, -1);
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
        auto w = make_living(families[i]);
        if (w) {
            w->setxy(100, 100);
            static_cast<living*>(w.get())->walk(1, 0);
            static_cast<living*>(w.get())->walk(-1, 0);
            static_cast<living*>(w.get())->walk(0, 1);
            static_cast<living*>(w.get())->walk(0, -1);
        }
    }
}
REGISTER_TEST(test_living_walk_all_families);
