#include "graph.h"
#include "test_trace.h"
#include "test_framework.h"
#include "guy.h"
#include "save_data.h"
#include "base.h"

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
    short initial_level = soldier.get_level();
    short initial_str = soldier.strength;

    soldier.upgrade_to_level(5);

    TEST_ASSERT_EQ(5, soldier.get_level(), "level should be 5 after upgrade");
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
    guy* g = new guy(FAMILY_SOLDIER);
    myscreen->save_data.team_list[0] = g;
    myscreen->save_data.team_size = 1;

    myscreen->save_data.add_level_completed("org.openglad.gladiator", 1);

    // Now reset
    myscreen->save_data.reset();

    TEST_ASSERT_EQ(0, myscreen->save_data.team_size, "team_size should be 0 after reset");
    TEST_ASSERT_EQ(0, static_cast<int>(myscreen->save_data.totalcash), "totalcash should be 0 after reset");
    TEST_ASSERT_EQ(0, static_cast<int>(myscreen->save_data.totalscore), "totalscore should be 0 after reset");
}
REGISTER_TEST(test_save_data_reset);
