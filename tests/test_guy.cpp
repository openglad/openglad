#include <openglad/core/test_trace.h>
#include "test_framework.h"
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/resources/save_data.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

// Test: Guy constructors create characters with correct family defaults
TEST(Guy, creation) {
    guy soldier(FAMILY_SOLDIER);
    ASSERT_EQ(FAMILY_SOLDIER, soldier.family) << "soldier should have soldier family";
    ASSERT_STREQ("SOLDIER", soldier.name.c_str()) << "soldier should be named SOLDIER";
    ASSERT_TRUE(soldier.strength > 0) << "soldier should have positive strength";

    guy archer(FAMILY_ARCHER);
    ASSERT_EQ(FAMILY_ARCHER, archer.family) << "archer should have archer family";
    ASSERT_STREQ("ARCHER", archer.name.c_str()) << "archer should be named ARCHER";

    guy mage(FAMILY_MAGE);
    ASSERT_EQ(FAMILY_MAGE, mage.family) << "mage should have mage family";
    ASSERT_STREQ("MAGE", mage.name.c_str()) << "mage should be named MAGE";

    guy cleric(FAMILY_CLERIC);
    ASSERT_EQ(FAMILY_CLERIC, cleric.family) << "cleric should have cleric family";
    ASSERT_STREQ("CLERIC", cleric.name.c_str()) << "cleric should be named CLERIC";

    guy elf(FAMILY_ELF);
    ASSERT_EQ(FAMILY_ELF, elf.family) << "elf should have elf family";
    ASSERT_STREQ("ELF", elf.name.c_str()) << "elf should be named ELF";

    guy thief(FAMILY_THIEF);
    ASSERT_EQ(FAMILY_THIEF, thief.family) << "thief should have thief family";
    ASSERT_STREQ("THIEF", thief.name.c_str()) << "thief should be named THIEF";

    guy druid(FAMILY_DRUID);
    ASSERT_EQ(FAMILY_DRUID, druid.family) << "druid should have druid family";
    ASSERT_STREQ("DRUID", druid.name.c_str()) << "druid should be named DRUID";
}



// Test: Guy copy constructor preserves all fields
TEST(Guy, copy) {
    guy original(FAMILY_ARCHER);
    original.name = "LEGOLAS";
    original.strength = 30;
    original.dexterity = 50;
    original.constitution = 20;
    original.intelligence = 15;
    original.armor = 10;
    original.kills = 42;

    guy copy(original);

    ASSERT_STREQ("LEGOLAS", copy.name.c_str()) << "copy name should match";
    ASSERT_EQ(FAMILY_ARCHER, copy.family) << "copy family should match";
    ASSERT_EQ(30, copy.strength) << "copy strength should match";
    ASSERT_EQ(50, copy.dexterity) << "copy dexterity should match";
    ASSERT_EQ(20, copy.constitution) << "copy constitution should match";
    ASSERT_EQ(15, copy.intelligence) << "copy intelligence should match";
    ASSERT_EQ(10, copy.armor) << "copy armor should match";
    ASSERT_EQ(42, copy.kills) << "copy kills should match";
}



// Test: Guy upgrade_to_level increases stats
TEST(Guy, level_up) {
    guy soldier(FAMILY_SOLDIER);
    short initial_str = soldier.strength;

    soldier.upgrade_to_level(5);

    ASSERT_EQ(5, soldier.level) << "level should be 5 after upgrade";
    ASSERT_TRUE(soldier.strength >= initial_str) << "strength should not decrease after leveling up";
}



// Test: Campaign level completion tracking
TEST(Guy, campaign_progress) {
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";

    // Initially no levels completed
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should not be completed initially";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("org.openglad.gladiator")) << "no levels should be completed initially";

    // Mark some levels as completed
    og::runtime::current_session->myscreen_->save_data.add_level_completed("org.openglad.gladiator", 1);
    og::runtime::current_session->myscreen_->save_data.add_level_completed("org.openglad.gladiator", 2);
    og::runtime::current_session->myscreen_->save_data.add_level_completed("org.openglad.gladiator", 3);

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be completed";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(2)) << "level 2 should be completed";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(3)) << "level 3 should be completed";
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(4)) << "level 4 should not be completed";

    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("org.openglad.gladiator")) << "3 levels should be completed";

    // Different campaign should have 0 completions
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("some.other.campaign")) << "other campaign should have 0 completions";

    // Reset campaign should clear completions
    og::runtime::current_session->myscreen_->save_data.reset_campaign("org.openglad.gladiator");
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("org.openglad.gladiator")) << "completions should be 0 after reset_campaign";
}



// Test: SaveData reset clears everything
TEST(Guy, save_data_reset) {
    og::runtime::current_session->myscreen_->save_data.scen_num = 10;
    og::runtime::current_session->myscreen_->save_data.totalcash = 99999;
    og::runtime::current_session->myscreen_->save_data.totalscore = 88888;
    og::runtime::current_session->myscreen_->save_data.numplayers = 4;

    // Add a team member
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    og::runtime::current_session->myscreen_->save_data.add_level_completed("org.openglad.gladiator", 1);

    // Now reset
    og::runtime::current_session->myscreen_->save_data.reset();

    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.team_size) << "team_size should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore)) << "totalscore should be 0 after reset";
}


// ---------------------------------------------------------------------------
// calculate_exp / calculate_level tests
// ---------------------------------------------------------------------------

TEST(Guy, calculate_exp_level1)
{
    // Level 1 requires 0 XP
    ASSERT_EQ(0, (int)calculate_exp(1)) << "level 1 should require 0 XP";
}


TEST(Guy, calculate_exp_level2)
{
    // Level 2: 8000 + 2000*1 + 4000*0 + calculate_exp(1) = 10000
    Uint32 xp = calculate_exp(2);
    ASSERT_EQ(10000, (int)xp) << "level 2 should require 10000 XP";
}


TEST(Guy, calculate_exp_monotonic)
{
    // Each level should require more XP than the previous
    for (int lvl = 2; lvl <= 20; lvl++) {
        Uint32 prev = calculate_exp(lvl - 1);
        Uint32 curr = calculate_exp(lvl);
        ASSERT_TRUE(curr > prev) << "XP requirement should increase with level";
    }
}


TEST(Guy, calculate_exp_known_values)
{
    // Verify against the actual formula: 8000 + 2000*(level-1) + 4000*(level-2) + calculate_exp(level-1)
    // Level 3: 8000 + 4000 + 4000 + 10000 = 26000
    Uint32 xp3 = calculate_exp(3);
    ASSERT_EQ(26000, (int)xp3) << "level 3 XP threshold";

    // Level 4: 8000 + 6000 + 8000 + 26000 = 48000
    Uint32 xp4 = calculate_exp(4);
    ASSERT_EQ(48000, (int)xp4) << "level 4 XP threshold";

    // Level 5: 8000 + 8000 + 12000 + 48000 = 76000
    Uint32 xp5 = calculate_exp(5);
    ASSERT_EQ(76000, (int)xp5) << "level 5 XP threshold";
}


TEST(Guy, calculate_level_basic)
{
    // 0 XP -> level 1
    ASSERT_EQ(1, (int)calculate_level(0)) << "0 XP should be level 1";

    // Exactly at level 2 threshold
    ASSERT_EQ(2, (int)calculate_level(10000)) << "10000 XP should be level 2";

    // Just below level 2
    ASSERT_EQ(1, (int)calculate_level(9999)) << "9999 XP should still be level 1";
}


TEST(Guy, calculate_level_roundtrip)
{
    // For each level, calculate_level(calculate_exp(lvl)) == lvl
    for (int lvl = 1; lvl <= 15; lvl++) {
        Uint32 xp = calculate_exp(lvl);
        Sint32 back = calculate_level(xp);
        ASSERT_EQ(lvl, (int)back) << "calculate_level(calculate_exp(n)) should return n";
    }
}


// ---------------------------------------------------------------------------
// Stat bonus function tests
// ---------------------------------------------------------------------------

TEST(Guy, hp_bonus)
{
    guy g(FAMILY_SOLDIER);
    g.constitution = 0;
    ASSERT_TRUE((int)g.get_hp_bonus() == 10) << "hp bonus with 0 con should be 10";

    g.constitution = 10;
    ASSERT_TRUE((int)g.get_hp_bonus() == 40) << "hp bonus with 10 con should be 40";

    g.constitution = 100;
    ASSERT_TRUE((int)g.get_hp_bonus() == 310) << "hp bonus with 100 con should be 310";
}


TEST(Guy, mp_bonus)
{
    guy g(FAMILY_MAGE);
    g.intelligence = 0;
    ASSERT_TRUE((int)g.get_mp_bonus() == 10) << "mp bonus with 0 int should be 10";

    g.intelligence = 20;
    ASSERT_TRUE((int)g.get_mp_bonus() == 70) << "mp bonus with 20 int should be 70";
}


TEST(Guy, damage_bonus)
{
    guy g(FAMILY_SOLDIER);
    g.strength = 0;
    float dmg = g.get_damage_bonus();
    ASSERT_TRUE(dmg < 0.01f && dmg > -0.01f) << "damage bonus with 0 str should be 0";

    g.strength = 40;
    dmg = g.get_damage_bonus();
    ASSERT_TRUE(dmg > 9.9f && dmg < 10.1f) << "damage bonus with 40 str should be 10";
}


TEST(Guy, speed_bonus)
{
    guy g(FAMILY_ELF);
    g.dexterity = 54;
    float spd = g.get_speed_bonus();
    ASSERT_TRUE(spd > 0.99f && spd < 1.01f) << "speed bonus with 54 dex should be ~1.0";

    g.dexterity = 0;
    spd = g.get_speed_bonus();
    ASSERT_TRUE(spd < 0.01f && spd > -0.01f) << "speed bonus with 0 dex should be 0";
}


TEST(Guy, fire_frequency_bonus)
{
    guy g(FAMILY_ARCHER);
    g.dexterity = 47;
    float freq = g.get_fire_frequency_bonus();
    ASSERT_TRUE(freq > 0.99f && freq < 1.01f) << "fire freq bonus with 47 dex should be ~1.0";
}


TEST(Guy, armor_bonus)
{
    guy g(FAMILY_SOLDIER);
    g.armor = 25;
    float arm = g.get_armor_bonus();
    ASSERT_TRUE((int)arm == 25) << "armor bonus should return armor value";
}


// ---------------------------------------------------------------------------
// upgrade_to_level tests
// ---------------------------------------------------------------------------

TEST(Guy, upgrade_to_level_stats_increase)
{
    guy soldier(FAMILY_SOLDIER);
    short str1 = soldier.strength;
    short dex1 = soldier.dexterity;
    short con1 = soldier.constitution;

    soldier.upgrade_to_level(10);

    ASSERT_TRUE(soldier.strength > str1) << "strength should increase after leveling to 10";
    ASSERT_TRUE(soldier.dexterity > dex1) << "dexterity should increase after leveling to 10";
    ASSERT_TRUE(soldier.constitution > con1) << "constitution should increase after leveling to 10";
    ASSERT_EQ(10, (int)soldier.level) << "level should be 10";
    ASSERT_TRUE(soldier.exp == calculate_exp(10)) << "exp should be set to level 10 threshold";
}


TEST(Guy, upgrade_to_level_different_families)
{
    // Different families should get different stat distributions
    guy soldier(FAMILY_SOLDIER);
    guy mage(FAMILY_MAGE);
    soldier.upgrade_to_level(5);
    mage.upgrade_to_level(5);

    // Soldier should have higher strength growth, mage higher intelligence
    // (Relative to their family defaults at level 1)
    ASSERT_TRUE(soldier.strength != mage.strength || soldier.intelligence != mage.intelligence) << "different families should have different stat distributions at same level";
}

