#include <openglad/entities/guy.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>

Uint32 calculate_exp(Sint32 level);
Sint32 calculate_level(Uint32 experience);

// ---------------------------------------------------------------------------
// calculate_exp tests
// ---------------------------------------------------------------------------

void test_calculate_exp_level_1()
{
    TEST_ASSERT_EQ(0, (int)calculate_exp(1), "level 1 requires 0 XP");
}
REGISTER_TEST(test_calculate_exp_level_1);

void test_calculate_exp_level_0()
{
    TEST_ASSERT_EQ(0, (int)calculate_exp(0), "level 0 requires 0 XP");
}
REGISTER_TEST(test_calculate_exp_level_0);

void test_calculate_exp_level_2()
{
    // level 2: 8000 + 2000*1 + 4000*0 + 0 = 10000
    TEST_ASSERT_EQ(10000, (int)calculate_exp(2), "level 2 requires 10000 XP");
}
REGISTER_TEST(test_calculate_exp_level_2);

void test_calculate_exp_level_3()
{
    // level 3: 8000 + 2000*2 + 4000*1 + calculate_exp(2) = 8000 + 4000 + 4000 + 10000 = 26000
    Uint32 xp3 = calculate_exp(3);
    TEST_ASSERT(xp3 > 20000, "level 3 should require > 20000 XP");
}
REGISTER_TEST(test_calculate_exp_level_3);

void test_calculate_exp_monotonic_extended()
{
    for (int i = 1; i < 20; i++) {
        TEST_ASSERT(calculate_exp(i+1) > calculate_exp(i), "XP should be monotonically increasing");
    }
}
REGISTER_TEST(test_calculate_exp_monotonic_extended);

void test_calculate_exp_level_10()
{
    Uint32 xp10 = calculate_exp(10);
    TEST_ASSERT(xp10 > 100000, "level 10 should require > 100000 XP");
}
REGISTER_TEST(test_calculate_exp_level_10);

// ---------------------------------------------------------------------------
// calculate_level tests
// ---------------------------------------------------------------------------

void test_calculate_level_zero_xp()
{
    TEST_ASSERT_EQ(1, (int)calculate_level(0), "0 XP should be level 1");
}
REGISTER_TEST(test_calculate_level_zero_xp);

void test_calculate_level_exact_boundary()
{
    Uint32 xp5 = calculate_exp(5);
    TEST_ASSERT_EQ(5, (int)calculate_level(xp5), "exact XP boundary should give that level");
}
REGISTER_TEST(test_calculate_level_exact_boundary);

void test_calculate_level_just_below()
{
    Uint32 xp5 = calculate_exp(5);
    TEST_ASSERT_EQ(4, (int)calculate_level(xp5 - 1), "1 below boundary should give level - 1");
}
REGISTER_TEST(test_calculate_level_just_below);

void test_calculate_level_roundtrip_extended()
{
    for (int i = 1; i <= 15; i++) {
        Uint32 xp = calculate_exp(i);
        TEST_ASSERT_EQ(i, (int)calculate_level(xp), "calculate_level(calculate_exp(n)) should == n");
    }
}
REGISTER_TEST(test_calculate_level_roundtrip_extended);

// ---------------------------------------------------------------------------
// guy constructor tests
// ---------------------------------------------------------------------------

void test_guy_default_constructor()
{
    guy g;
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)g.family, "default guy should be soldier");
    TEST_ASSERT_EQ(1, (int)g.level, "default level should be 1");
    TEST_ASSERT_EQ(0, (int)g.exp, "default exp should be 0");
    TEST_ASSERT_EQ(0, (int)g.kills, "default kills should be 0");
}
REGISTER_TEST(test_guy_default_constructor);

void test_guy_family_constructor_soldier()
{
    guy g(FAMILY_SOLDIER);
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)g.family, "family should be soldier");
    TEST_ASSERT_EQ(12, (int)g.strength, "soldier STR should be 12");
    TEST_ASSERT_EQ(6, (int)g.dexterity, "soldier DEX should be 6");
    TEST_ASSERT_EQ(12, (int)g.constitution, "soldier CON should be 12");
    TEST_ASSERT_EQ(8, (int)g.intelligence, "soldier INT should be 8");
    TEST_ASSERT_EQ(9, (int)g.armor, "soldier ARMOR should be 9");
    TEST_ASSERT_EQ(1, (int)g.level, "soldier level should be 1");
}
REGISTER_TEST(test_guy_family_constructor_soldier);

void test_guy_family_constructor_mage()
{
    guy g(FAMILY_MAGE);
    TEST_ASSERT_EQ((int)FAMILY_MAGE, (int)g.family, "family should be mage");
    TEST_ASSERT_EQ(4, (int)g.strength, "mage STR should be 4");
    TEST_ASSERT_EQ(16, (int)g.intelligence, "mage INT should be 16");
}
REGISTER_TEST(test_guy_family_constructor_mage);

void test_guy_family_constructor_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        TEST_ASSERT_EQ((int)families[i], (int)g.family, "family should match constructor arg");
        TEST_ASSERT_EQ(1, (int)g.level, "all families start at level 1");
        TEST_ASSERT(g.strength > 0, "strength should be positive");
    }
}
REGISTER_TEST(test_guy_family_constructor_all_families);

void test_guy_copy_constructor()
{
    guy original(FAMILY_ARCHER);
    original.strength = 50;
    original.kills = 10;
    original.exp = 5000;

    guy copy(original);
    TEST_ASSERT_EQ((int)original.family, (int)copy.family, "copy family should match");
    TEST_ASSERT_EQ((int)original.strength, (int)copy.strength, "copy strength should match");
    TEST_ASSERT_EQ((int)original.kills, (int)copy.kills, "copy kills should match");
    TEST_ASSERT_EQ((int)original.exp, (int)copy.exp, "copy exp should match");
}
REGISTER_TEST(test_guy_copy_constructor);

// ---------------------------------------------------------------------------
// get_*_bonus tests
// ---------------------------------------------------------------------------

void test_guy_get_hp_bonus()
{
    guy g(FAMILY_SOLDIER);
    float hp = g.get_hp_bonus();
    // HP bonus = 10 + constitution*3 = 10 + 12*3 = 46
    TEST_ASSERT(hp > 45.9f && hp < 46.1f, "soldier HP bonus should be ~46");
}
REGISTER_TEST(test_guy_get_hp_bonus);

void test_guy_get_mp_bonus()
{
    guy g(FAMILY_MAGE);
    float mp = g.get_mp_bonus();
    // MP bonus = 10 + intelligence*3 = 10 + 16*3 = 58
    TEST_ASSERT(mp > 57.9f && mp < 58.1f, "mage MP bonus should be ~58");
}
REGISTER_TEST(test_guy_get_mp_bonus);

void test_guy_get_damage_bonus()
{
    guy g(FAMILY_SOLDIER);
    float dmg = g.get_damage_bonus();
    // Damage bonus = strength/4 = 12/4 = 3
    TEST_ASSERT(dmg > 2.9f && dmg < 3.1f, "soldier damage bonus should be ~3");
}
REGISTER_TEST(test_guy_get_damage_bonus);

void test_guy_get_armor_bonus()
{
    guy g(FAMILY_SOLDIER);
    float arm = g.get_armor_bonus();
    // Armor bonus = armor = 9
    TEST_ASSERT(arm > 8.9f && arm < 9.1f, "soldier armor bonus should be ~9");
}
REGISTER_TEST(test_guy_get_armor_bonus);

void test_guy_get_speed_bonus()
{
    guy g(FAMILY_SOLDIER);
    float spd = g.get_speed_bonus();
    // Speed bonus = dexterity/54 = 6/54 ≈ 0.111
    TEST_ASSERT(spd > 0.1f && spd < 0.12f, "soldier speed bonus should be ~0.111");
}
REGISTER_TEST(test_guy_get_speed_bonus);

void test_guy_get_fire_frequency_bonus()
{
    guy g(FAMILY_SOLDIER);
    float freq = g.get_fire_frequency_bonus();
    // Fire freq bonus = dexterity/47 = 6/47 ≈ 0.128
    TEST_ASSERT(freq > 0.12f && freq < 0.14f, "soldier fire freq bonus should be ~0.128");
}
REGISTER_TEST(test_guy_get_fire_frequency_bonus);

void test_guy_bonuses_scale_with_stats()
{
    guy g(FAMILY_SOLDIER);
    float hp1 = g.get_hp_bonus();
    g.constitution = 100;
    float hp2 = g.get_hp_bonus();
    TEST_ASSERT(hp2 > hp1, "HP bonus should increase with constitution");

    float mp1 = g.get_mp_bonus();
    g.intelligence = 100;
    float mp2 = g.get_mp_bonus();
    TEST_ASSERT(mp2 > mp1, "MP bonus should increase with intelligence");
}
REGISTER_TEST(test_guy_bonuses_scale_with_stats);

// ---------------------------------------------------------------------------
// query_heart_value tests
// ---------------------------------------------------------------------------

void test_guy_query_heart_value_base()
{
    guy g(FAMILY_SOLDIER);
    Sint32 val = g.query_heart_value();
    // Base cost = costlist[FAMILY_SOLDIER] = 250
    // No stat increases, so cost = 250
    TEST_ASSERT_EQ(250, (int)val, "base soldier should cost 250");
}
REGISTER_TEST(test_guy_query_heart_value_base);

void test_guy_query_heart_value_with_stats()
{
    guy g(FAMILY_SOLDIER);
    g.strength = 20;  // 8 above base of 12
    Sint32 val = g.query_heart_value();
    TEST_ASSERT(val > 250, "soldier with increased strength should cost more than base");
}
REGISTER_TEST(test_guy_query_heart_value_with_stats);

void test_guy_query_heart_value_different_families()
{
    guy g1(FAMILY_SOLDIER);
    guy g2(FAMILY_FIREELEMENTAL);
    Sint32 v1 = g1.query_heart_value();
    Sint32 v2 = g2.query_heart_value();
    TEST_ASSERT(v2 > v1, "fire elemental should cost more than soldier at base stats");
}
REGISTER_TEST(test_guy_query_heart_value_different_families);

// ---------------------------------------------------------------------------
// upgrade_to_level tests
// ---------------------------------------------------------------------------

void test_guy_upgrade_to_level_basic()
{
    guy g(FAMILY_SOLDIER);
    short orig_str = g.strength;
    g.upgrade_to_level(5);
    TEST_ASSERT_EQ(5, (int)g.level, "level should be 5 after upgrade");
    TEST_ASSERT(g.strength > orig_str, "strength should increase after leveling");
    TEST_ASSERT(g.exp > 0, "XP should be set after leveling");
}
REGISTER_TEST(test_guy_upgrade_to_level_basic);

void test_guy_upgrade_to_level_mage()
{
    guy g(FAMILY_MAGE);
    short orig_int = g.intelligence;
    short orig_str = g.strength;
    g.upgrade_to_level(5);
    // Mage gets 2x INT scaling, 0.5x STR scaling
    short int_gain = g.intelligence - orig_int;
    short str_gain = g.strength - orig_str;
    TEST_ASSERT(int_gain > str_gain, "mage INT gain should exceed STR gain");
}
REGISTER_TEST(test_guy_upgrade_to_level_mage);

void test_guy_upgrade_to_level_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN,
                        FAMILY_ARCHMAGE, FAMILY_BIG_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 15; i++) {
        guy g(families[i]);
        short orig_str = g.strength;
        g.upgrade_to_level(3);
        TEST_ASSERT_EQ(3, (int)g.level, "level should be 3");
        TEST_ASSERT(g.strength >= orig_str, "strength should not decrease");
    }
}
REGISTER_TEST(test_guy_upgrade_to_level_all_families);

void test_guy_upgrade_to_level_sets_xp()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    TEST_ASSERT_EQ((int)calculate_exp(5), (int)g.exp, "XP should be set to level 5 threshold");
}
REGISTER_TEST(test_guy_upgrade_to_level_sets_xp);

void test_guy_upgrade_to_level_no_xp()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, false);
    TEST_ASSERT_EQ(0, (int)g.exp, "XP should remain 0 when set_xp=false");
}
REGISTER_TEST(test_guy_upgrade_to_level_no_xp);

void test_guy_upgrade_level_10()
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(10);
    TEST_ASSERT_EQ(10, (int)g.level, "level should be 10");
    // STR gain: 8 * 9 levels * 1.0 = 72 + base 12 = 84
    TEST_ASSERT(g.strength > 50, "soldier STR at level 10 should be > 50");
}
REGISTER_TEST(test_guy_upgrade_level_10);
