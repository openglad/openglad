#include <openglad/legacy/graph.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include <openglad/entities/guy.h>
#include <openglad/input/button.h>
#include <openglad/data/save_data.h>
#include <openglad/legacy/base.h>
#include <memory>

extern screen* myscreen;

// Test: Guy constructors create characters with correct family defaults
void test_guy_creation() {
    guy soldier(FAMILY_SOLDIER);
    TEST_ASSERT_EQ(FAMILY_SOLDIER, soldier.family, "soldier should have soldier family");
    TEST_ASSERT_STR_EQ("SOLDIER", soldier.name.c_str(), "soldier should be named SOLDIER");
    TEST_ASSERT(soldier.strength > 0, "soldier should have positive strength");

    guy archer(FAMILY_ARCHER);
    TEST_ASSERT_EQ(FAMILY_ARCHER, archer.family, "archer should have archer family");
    TEST_ASSERT_STR_EQ("ARCHER", archer.name.c_str(), "archer should be named ARCHER");

    guy mage(FAMILY_MAGE);
    TEST_ASSERT_EQ(FAMILY_MAGE, mage.family, "mage should have mage family");
    TEST_ASSERT_STR_EQ("MAGE", mage.name.c_str(), "mage should be named MAGE");

    guy cleric(FAMILY_CLERIC);
    TEST_ASSERT_EQ(FAMILY_CLERIC, cleric.family, "cleric should have cleric family");
    TEST_ASSERT_STR_EQ("CLERIC", cleric.name.c_str(), "cleric should be named CLERIC");

    guy elf(FAMILY_ELF);
    TEST_ASSERT_EQ(FAMILY_ELF, elf.family, "elf should have elf family");
    TEST_ASSERT_STR_EQ("ELF", elf.name.c_str(), "elf should be named ELF");

    guy thief(FAMILY_THIEF);
    TEST_ASSERT_EQ(FAMILY_THIEF, thief.family, "thief should have thief family");
    TEST_ASSERT_STR_EQ("THIEF", thief.name.c_str(), "thief should be named THIEF");

    guy druid(FAMILY_DRUID);
    TEST_ASSERT_EQ(FAMILY_DRUID, druid.family, "druid should have druid family");
    TEST_ASSERT_STR_EQ("DRUID", druid.name.c_str(), "druid should be named DRUID");
}
REGISTER_TEST(test_guy_creation);


// Test: Guy copy constructor preserves all fields
void test_guy_copy() {
    guy original(FAMILY_ARCHER);
    original.name = "LEGOLAS";
    original.strength = 30;
    original.dexterity = 50;
    original.constitution = 20;
    original.intelligence = 15;
    original.armor = 10;
    original.kills = 42;

    guy copy(original);

    TEST_ASSERT_STR_EQ("LEGOLAS", copy.name.c_str(), "copy name should match");
    TEST_ASSERT_EQ(FAMILY_ARCHER, copy.family, "copy family should match");
    TEST_ASSERT_EQ(30, copy.strength, "copy strength should match");
    TEST_ASSERT_EQ(50, copy.dexterity, "copy dexterity should match");
    TEST_ASSERT_EQ(20, copy.constitution, "copy constitution should match");
    TEST_ASSERT_EQ(15, copy.intelligence, "copy intelligence should match");
    TEST_ASSERT_EQ(10, copy.armor, "copy armor should match");
    TEST_ASSERT_EQ(42, copy.kills, "copy kills should match");
}
REGISTER_TEST(test_guy_copy);


// Test: Guy upgrade_to_level increases stats
void test_guy_level_up() {
    guy soldier(FAMILY_SOLDIER);
    short initial_str = soldier.strength;

    soldier.upgrade_to_level(5);

    TEST_ASSERT_EQ(5, soldier.level, "level should be 5 after upgrade");
    TEST_ASSERT(soldier.strength >= initial_str,
        "strength should not decrease after leveling up");
}
REGISTER_TEST(test_guy_level_up);


// Test: Campaign level completion tracking
void test_campaign_progress() {
    myscreen->save_data.reset();
    myscreen->save_data.current_campaign = "org.openglad.gladiator";

    // Initially no levels completed
    TEST_ASSERT(!myscreen->save_data.is_level_completed(1),
        "level 1 should not be completed initially");
    TEST_ASSERT_EQ(0, myscreen->save_data.get_num_levels_completed("org.openglad.gladiator"),
        "no levels should be completed initially");

    // Mark some levels as completed
    myscreen->save_data.add_level_completed("org.openglad.gladiator", 1);
    myscreen->save_data.add_level_completed("org.openglad.gladiator", 2);
    myscreen->save_data.add_level_completed("org.openglad.gladiator", 3);

    TEST_ASSERT(myscreen->save_data.is_level_completed(1),
        "level 1 should be completed");
    TEST_ASSERT(myscreen->save_data.is_level_completed(2),
        "level 2 should be completed");
    TEST_ASSERT(myscreen->save_data.is_level_completed(3),
        "level 3 should be completed");
    TEST_ASSERT(!myscreen->save_data.is_level_completed(4),
        "level 4 should not be completed");

    TEST_ASSERT_EQ(3, myscreen->save_data.get_num_levels_completed("org.openglad.gladiator"),
        "3 levels should be completed");

    // Different campaign should have 0 completions
    TEST_ASSERT_EQ(0, myscreen->save_data.get_num_levels_completed("some.other.campaign"),
        "other campaign should have 0 completions");

    // Reset campaign should clear completions
    myscreen->save_data.reset_campaign("org.openglad.gladiator");
    TEST_ASSERT_EQ(0, myscreen->save_data.get_num_levels_completed("org.openglad.gladiator"),
        "completions should be 0 after reset_campaign");
}
REGISTER_TEST(test_campaign_progress);


// Test: SaveData reset clears everything
void test_save_data_reset() {
    myscreen->save_data.scen_num = 10;
    myscreen->save_data.totalcash = 99999;
    myscreen->save_data.totalscore = 88888;
    myscreen->save_data.numplayers = 4;

    // Add a team member
    myscreen->save_data.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    myscreen->save_data.team_size = 1;

    myscreen->save_data.add_level_completed("org.openglad.gladiator", 1);

    // Now reset
    myscreen->save_data.reset();

    TEST_ASSERT_EQ(0, myscreen->save_data.team_size, "team_size should be 0 after reset");
    TEST_ASSERT_EQ(0, static_cast<int>(myscreen->save_data.totalcash), "totalcash should be 0 after reset");
    TEST_ASSERT_EQ(0, static_cast<int>(myscreen->save_data.totalscore), "totalscore should be 0 after reset");
}
REGISTER_TEST(test_save_data_reset);

// ---------------------------------------------------------------------------
// calculate_exp / calculate_level tests
// ---------------------------------------------------------------------------

void test_calculate_exp_level1()
{
    // Level 1 requires 0 XP
    TEST_ASSERT_EQ(0, (int)calculate_exp(1), "level 1 should require 0 XP");
}
REGISTER_TEST(test_calculate_exp_level1);

void test_calculate_exp_level2()
{
    // Level 2: 8000 + 2000*1 + 4000*0 + calculate_exp(1) = 10000
    Uint32 xp = calculate_exp(2);
    TEST_ASSERT_EQ(10000, (int)xp, "level 2 should require 10000 XP");
}
REGISTER_TEST(test_calculate_exp_level2);

void test_calculate_exp_monotonic()
{
    // Each level should require more XP than the previous
    for (int lvl = 2; lvl <= 20; lvl++) {
        Uint32 prev = calculate_exp(lvl - 1);
        Uint32 curr = calculate_exp(lvl);
        TEST_ASSERT(curr > prev, "XP requirement should increase with level");
    }
}
REGISTER_TEST(test_calculate_exp_monotonic);

void test_calculate_exp_known_values()
{
    // Verify against the actual formula: 8000 + 2000*(level-1) + 4000*(level-2) + calculate_exp(level-1)
    // Level 3: 8000 + 4000 + 4000 + 10000 = 26000
    Uint32 xp3 = calculate_exp(3);
    TEST_ASSERT_EQ(26000, (int)xp3, "level 3 XP threshold");

    // Level 4: 8000 + 6000 + 8000 + 26000 = 48000
    Uint32 xp4 = calculate_exp(4);
    TEST_ASSERT_EQ(48000, (int)xp4, "level 4 XP threshold");

    // Level 5: 8000 + 8000 + 12000 + 48000 = 76000
    Uint32 xp5 = calculate_exp(5);
    TEST_ASSERT_EQ(76000, (int)xp5, "level 5 XP threshold");
}
REGISTER_TEST(test_calculate_exp_known_values);

void test_calculate_level_basic()
{
    // 0 XP -> level 1
    TEST_ASSERT_EQ(1, (int)calculate_level(0), "0 XP should be level 1");

    // Exactly at level 2 threshold
    TEST_ASSERT_EQ(2, (int)calculate_level(10000), "10000 XP should be level 2");

    // Just below level 2
    TEST_ASSERT_EQ(1, (int)calculate_level(9999), "9999 XP should still be level 1");
}
REGISTER_TEST(test_calculate_level_basic);

void test_calculate_level_roundtrip()
{
    // For each level, calculate_level(calculate_exp(lvl)) == lvl
    for (int lvl = 1; lvl <= 15; lvl++) {
        Uint32 xp = calculate_exp(lvl);
        Sint32 back = calculate_level(xp);
        TEST_ASSERT_EQ(lvl, (int)back, "calculate_level(calculate_exp(n)) should return n");
    }
}
REGISTER_TEST(test_calculate_level_roundtrip);

// ---------------------------------------------------------------------------
// Stat bonus function tests
// ---------------------------------------------------------------------------

void test_hp_bonus()
{
    guy g(FAMILY_SOLDIER);
    g.constitution = 0;
    TEST_ASSERT((int)g.get_hp_bonus() == 10, "hp bonus with 0 con should be 10");

    g.constitution = 10;
    TEST_ASSERT((int)g.get_hp_bonus() == 40, "hp bonus with 10 con should be 40");

    g.constitution = 100;
    TEST_ASSERT((int)g.get_hp_bonus() == 310, "hp bonus with 100 con should be 310");
}
REGISTER_TEST(test_hp_bonus);

void test_mp_bonus()
{
    guy g(FAMILY_MAGE);
    g.intelligence = 0;
    TEST_ASSERT((int)g.get_mp_bonus() == 10, "mp bonus with 0 int should be 10");

    g.intelligence = 20;
    TEST_ASSERT((int)g.get_mp_bonus() == 70, "mp bonus with 20 int should be 70");
}
REGISTER_TEST(test_mp_bonus);

void test_damage_bonus()
{
    guy g(FAMILY_SOLDIER);
    g.strength = 0;
    float dmg = g.get_damage_bonus();
    TEST_ASSERT(dmg < 0.01f && dmg > -0.01f, "damage bonus with 0 str should be 0");

    g.strength = 40;
    dmg = g.get_damage_bonus();
    TEST_ASSERT(dmg > 9.9f && dmg < 10.1f, "damage bonus with 40 str should be 10");
}
REGISTER_TEST(test_damage_bonus);

void test_speed_bonus()
{
    guy g(FAMILY_ELF);
    g.dexterity = 54;
    float spd = g.get_speed_bonus();
    TEST_ASSERT(spd > 0.99f && spd < 1.01f, "speed bonus with 54 dex should be ~1.0");

    g.dexterity = 0;
    spd = g.get_speed_bonus();
    TEST_ASSERT(spd < 0.01f && spd > -0.01f, "speed bonus with 0 dex should be 0");
}
REGISTER_TEST(test_speed_bonus);

void test_fire_frequency_bonus()
{
    guy g(FAMILY_ARCHER);
    g.dexterity = 47;
    float freq = g.get_fire_frequency_bonus();
    TEST_ASSERT(freq > 0.99f && freq < 1.01f, "fire freq bonus with 47 dex should be ~1.0");
}
REGISTER_TEST(test_fire_frequency_bonus);

void test_armor_bonus()
{
    guy g(FAMILY_SOLDIER);
    g.armor = 25;
    float arm = g.get_armor_bonus();
    TEST_ASSERT((int)arm == 25, "armor bonus should return armor value");
}
REGISTER_TEST(test_armor_bonus);

// ---------------------------------------------------------------------------
// upgrade_to_level tests
// ---------------------------------------------------------------------------

void test_upgrade_to_level_stats_increase()
{
    guy soldier(FAMILY_SOLDIER);
    short str1 = soldier.strength;
    short dex1 = soldier.dexterity;
    short con1 = soldier.constitution;

    soldier.upgrade_to_level(10);

    TEST_ASSERT(soldier.strength > str1, "strength should increase after leveling to 10");
    TEST_ASSERT(soldier.dexterity > dex1, "dexterity should increase after leveling to 10");
    TEST_ASSERT(soldier.constitution > con1, "constitution should increase after leveling to 10");
    TEST_ASSERT_EQ(10, (int)soldier.level, "level should be 10");
    TEST_ASSERT(soldier.exp == calculate_exp(10), "exp should be set to level 10 threshold");
}
REGISTER_TEST(test_upgrade_to_level_stats_increase);

void test_upgrade_to_level_different_families()
{
    // Different families should get different stat distributions
    guy soldier(FAMILY_SOLDIER);
    guy mage(FAMILY_MAGE);
    soldier.upgrade_to_level(5);
    mage.upgrade_to_level(5);

    // Soldier should have higher strength growth, mage higher intelligence
    // (Relative to their family defaults at level 1)
    TEST_ASSERT(soldier.strength != mage.strength || soldier.intelligence != mage.intelligence,
        "different families should have different stat distributions at same level");
}
REGISTER_TEST(test_upgrade_to_level_different_families);
