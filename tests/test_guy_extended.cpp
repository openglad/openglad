#include <openglad/entities/guy.h>
#include <openglad/runtime/guy_create.h>
#include <openglad/core/stats.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"

extern screen* myscreen;
extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];

// ---------------------------------------------------------------------------
// upgrade_to_level - exercises the big family switch (lines 323-456)
// ---------------------------------------------------------------------------

void test_guy_upgrade_soldier()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.strength > statlist[FAMILY_SOLDIER][0], "soldier str should increase");
    TEST_ASSERT(g.level == 5, "level should be 5");
    TEST_ASSERT(g.exp > 0, "exp should be set when set_xp=true");
}
REGISTER_TEST(test_guy_upgrade_soldier);

void test_guy_upgrade_elf()
{
    guy g(FAMILY_ELF);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.dexterity > statlist[FAMILY_ELF][1], "elf dex should increase significantly");
}
REGISTER_TEST(test_guy_upgrade_elf);

void test_guy_upgrade_archer()
{
    guy g(FAMILY_ARCHER);
    g.upgrade_to_level(5, false);
    TEST_ASSERT(g.dexterity > statlist[FAMILY_ARCHER][1], "archer dex should increase");
    TEST_ASSERT_EQ(0, (int)g.exp, "exp should be 0 when set_xp=false");
}
REGISTER_TEST(test_guy_upgrade_archer);

void test_guy_upgrade_mage()
{
    guy g(FAMILY_MAGE);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.intelligence > statlist[FAMILY_MAGE][3], "mage int should increase most");
}
REGISTER_TEST(test_guy_upgrade_mage);

void test_guy_upgrade_skeleton()
{
    guy g(FAMILY_SKELETON);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.dexterity > statlist[FAMILY_SKELETON][1], "skeleton dex should increase");
}
REGISTER_TEST(test_guy_upgrade_skeleton);

void test_guy_upgrade_cleric()
{
    guy g(FAMILY_CLERIC);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.level == 5, "cleric level should be 5");
}
REGISTER_TEST(test_guy_upgrade_cleric);

void test_guy_upgrade_fireelemental()
{
    guy g(FAMILY_FIREELEMENTAL);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.strength > statlist[FAMILY_FIREELEMENTAL][0], "fire elem str should increase");
}
REGISTER_TEST(test_guy_upgrade_fireelemental);

void test_guy_upgrade_faerie()
{
    guy g(FAMILY_FAERIE);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.dexterity > statlist[FAMILY_FAERIE][1], "faerie dex should increase");
}
REGISTER_TEST(test_guy_upgrade_faerie);

void test_guy_upgrade_slime()
{
    guy g(FAMILY_SMALL_SLIME);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.level == 5, "slime level should be 5");
}
REGISTER_TEST(test_guy_upgrade_slime);

void test_guy_upgrade_thief()
{
    guy g(FAMILY_THIEF);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.dexterity > statlist[FAMILY_THIEF][1], "thief dex should increase");
}
REGISTER_TEST(test_guy_upgrade_thief);

void test_guy_upgrade_ghost()
{
    guy g(FAMILY_GHOST);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.level == 5, "ghost level should be 5");
}
REGISTER_TEST(test_guy_upgrade_ghost);

void test_guy_upgrade_druid()
{
    guy g(FAMILY_DRUID);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.intelligence > statlist[FAMILY_DRUID][3], "druid int should increase");
}
REGISTER_TEST(test_guy_upgrade_druid);

void test_guy_upgrade_orc()
{
    guy g(FAMILY_ORC);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.strength > statlist[FAMILY_ORC][0], "orc str should increase");
}
REGISTER_TEST(test_guy_upgrade_orc);

void test_guy_upgrade_barbarian()
{
    guy g(FAMILY_BARBARIAN);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.strength > statlist[FAMILY_BARBARIAN][0], "barbarian str should increase");
}
REGISTER_TEST(test_guy_upgrade_barbarian);

void test_guy_upgrade_archmage()
{
    guy g(FAMILY_ARCHMAGE);
    g.upgrade_to_level(5, true);
    TEST_ASSERT(g.intelligence > statlist[FAMILY_ARCHMAGE][3], "archmage int should increase");
}
REGISTER_TEST(test_guy_upgrade_archmage);

// ---------------------------------------------------------------------------
// update_derived_stats (lines 492-571) - exercises HP/MP/speed/armor calc
// ---------------------------------------------------------------------------

void test_guy_update_derived_stats_soldier()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, myscreen);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP should be positive");
    TEST_ASSERT(w->stats()->max_magicpoints >= 0, "MP should be non-negative");
    TEST_ASSERT(w->stats()->heal_per_round >= 0, "heal_per_round should be non-negative");
    TEST_ASSERT(w->stats()->magic_per_round >= 0, "magic_per_round should be non-negative");
}
REGISTER_TEST(test_guy_update_derived_stats_soldier);

void test_guy_update_derived_stats_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        g.upgrade_to_level(3, true);
        auto w = guy_create_walker_owned(g, myscreen);
        if (w) {
            TEST_ASSERT(w->stats()->max_hitpoints > 0, "HP should be positive for all families");
        }
    }
}
REGISTER_TEST(test_guy_update_derived_stats_all_families);

// ---------------------------------------------------------------------------
// query_heart_value (lines 134-179)
// ---------------------------------------------------------------------------

void test_guy_query_heart_value_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        Sint32 val = g.query_heart_value();
        TEST_ASSERT(val > 0, "heart value should be positive for base stats");
    }
}
REGISTER_TEST(test_guy_query_heart_value_all_families);

void test_guy_query_heart_value_upgraded()
{
    guy g(FAMILY_SOLDIER);
    Sint32 base_val = g.query_heart_value();
    g.upgrade_to_level(5, true);
    Sint32 upgraded_val = g.query_heart_value();
    TEST_ASSERT(upgraded_val > base_val, "upgraded guy should be worth more");
}
REGISTER_TEST(test_guy_query_heart_value_upgraded);

// ---------------------------------------------------------------------------
// create_walker and create_and_add_walker
// ---------------------------------------------------------------------------

void test_guy_create_walker_various()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC };
    for (int i = 0; i < 6; i++) {
        guy g(families[i]);
        g.upgrade_to_level(2, true);
        auto w = guy_create_walker_owned(g, myscreen);
        TEST_ASSERT(w != nullptr, "create_walker should succeed");
        TEST_ASSERT(w->myguy != nullptr, "walker should have myguy set");
        TEST_ASSERT(w->stats()->level == 2, "walker level should match guy level");
    }
}
REGISTER_TEST(test_guy_create_walker_various);

// ---------------------------------------------------------------------------
// Copy constructor
// ---------------------------------------------------------------------------

void test_guy_copy_constructor_all_fields()
{
    guy original(FAMILY_MAGE);
    original.upgrade_to_level(5, true);
    original.kills = 10;
    original.level_kills = 20;
    original.total_damage = 100;
    original.total_hits = 50;
    original.total_shots = 75;
    original.scen_damage = 30;
    original.scen_kills = 5;

    guy copy(original);
    TEST_ASSERT_EQ((int)original.family, (int)copy.family, "family should match");
    TEST_ASSERT_EQ((int)original.strength, (int)copy.strength, "strength should match");
    TEST_ASSERT_EQ((int)original.intelligence, (int)copy.intelligence, "intelligence should match");
    TEST_ASSERT_EQ((int)original.dexterity, (int)copy.dexterity, "dexterity should match");
    TEST_ASSERT_EQ((int)original.constitution, (int)copy.constitution, "constitution should match");
    TEST_ASSERT_EQ((int)original.armor, (int)copy.armor, "armor should match");
    TEST_ASSERT_EQ((int)original.level, (int)copy.level, "level should match");
    TEST_ASSERT_EQ((int)original.kills, (int)copy.kills, "kills should match");
    TEST_ASSERT_EQ((int)original.total_damage, (int)copy.total_damage, "total_damage should match");
}
REGISTER_TEST(test_guy_copy_constructor_all_fields);

// ---------------------------------------------------------------------------
// Derived stat bonus functions
// ---------------------------------------------------------------------------

void test_guy_derived_bonus_scaling()
{
    guy g(FAMILY_SOLDIER);
    float hp1 = g.get_hp_bonus();
    g.constitution += 10;
    float hp2 = g.get_hp_bonus();
    TEST_ASSERT(hp2 > hp1, "more constitution should give more HP bonus");

    guy g2(FAMILY_MAGE);
    float mp1 = g2.get_mp_bonus();
    g2.intelligence += 10;
    float mp2 = g2.get_mp_bonus();
    TEST_ASSERT(mp2 > mp1, "more intelligence should give more MP bonus");
}
REGISTER_TEST(test_guy_derived_bonus_scaling);

void test_guy_unknown_family_fallback_and_zero_heart_value()
{
    guy unknown(127);
    TEST_ASSERT_EQ(12, (int)unknown.strength, "unknown family should use fallback STR");
    TEST_ASSERT_EQ(6, (int)unknown.dexterity, "unknown family should use fallback DEX");
    TEST_ASSERT_EQ(12, (int)unknown.constitution, "unknown family should use fallback CON");
    TEST_ASSERT_EQ(8, (int)unknown.intelligence, "unknown family should use fallback INT");
    TEST_ASSERT_EQ(6, (int)unknown.armor, "unknown family should use fallback armor");
    TEST_ASSERT_EQ(1, (int)unknown.level, "unknown family should use fallback level");

    unknown.family = 127;
    TEST_ASSERT_EQ(0, (int)unknown.query_heart_value(), "unknown family should have zero heart value");
}
REGISTER_TEST(test_guy_unknown_family_fallback_and_zero_heart_value);

void test_guy_update_derived_stats_clamps_speed_and_regen_delays()
{
    guy g(FAMILY_SOLDIER);
    g.dexterity = 3000;
    g.constitution = 3000;
    g.strength = 3000;
    g.intelligence = 3000;
    g.level = 1;

    auto w = guy_create_walker_owned(g, myscreen);
    TEST_ASSERT(w != nullptr, "walker should be created");
    if (!w)
        return;

    TEST_ASSERT(w->stepsize <= 12.0f, "stepsize should clamp to 12");
    TEST_ASSERT(w->fire_frequency >= 1.0f, "fire_frequency should clamp to minimum 1");
    TEST_ASSERT(w->stats()->heal_per_round > 0, "high stats should increase heal_per_round");
    TEST_ASSERT(w->stats()->magic_per_round > 0, "high stats should increase magic_per_round");
    TEST_ASSERT(w->stats()->max_heal_delay >= 2, "max_heal_delay should respect minimum clamp");
    TEST_ASSERT(w->stats()->max_magic_delay >= 2, "max_magic_delay should respect minimum clamp");
}
REGISTER_TEST(test_guy_update_derived_stats_clamps_speed_and_regen_delays);
