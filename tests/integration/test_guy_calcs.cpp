#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>

Uint32 calculate_exp(Sint32 level);
Sint32 calculate_level(Uint32 experience);

// ---------------------------------------------------------------------------
// calculate_exp tests
// ---------------------------------------------------------------------------

TEST(GuyCalcs, calculate_exp_level_1)
{
    ASSERT_EQ(0, (int)calculate_exp(1)) << "level 1 requires 0 XP";
}


TEST(GuyCalcs, calculate_exp_level_0)
{
    ASSERT_EQ(0, (int)calculate_exp(0)) << "level 0 requires 0 XP";
}


TEST(GuyCalcs, calculate_exp_level_2)
{
    // level 2: 8000 + 2000*1 + 4000*0 + 0 = 10000
    ASSERT_EQ(10000, (int)calculate_exp(2)) << "level 2 requires 10000 XP";
}


TEST(GuyCalcs, calculate_exp_level_3)
{
    // level 3: 8000 + 2000*2 + 4000*1 + calculate_exp(2) = 8000 + 4000 + 4000 + 10000 = 26000
    ASSERT_EQ(26000, (int)calculate_exp(3))
        << "the level-3 XP threshold is exactly 26000 (8000 + 2000*2 + 4000*1 + 10000)";
}


TEST(GuyCalcs, calculate_exp_monotonic_extended)
{
    for (int i = 1; i < 20; i++) {
        ASSERT_TRUE(calculate_exp(i+1) > calculate_exp(i)) << "XP should be monotonically increasing";
    }
}


TEST(GuyCalcs, calculate_exp_level_10)
{
    // sum over k = 2..10 of 8000 + 2000*(k-1) + 4000*(k-2)
    ASSERT_EQ(306000, (int)calculate_exp(10))
        << "the level-10 XP threshold is exactly 306000";
}


// ---------------------------------------------------------------------------
// calculate_level tests
// ---------------------------------------------------------------------------

TEST(GuyCalcs, calculate_level_zero_xp)
{
    ASSERT_EQ(1, (int)calculate_level(0)) << "0 XP should be level 1";
}


TEST(GuyCalcs, calculate_level_exact_boundary)
{
    Uint32 xp5 = calculate_exp(5);
    ASSERT_EQ(5, (int)calculate_level(xp5)) << "exact XP boundary should give that level";
}


TEST(GuyCalcs, calculate_level_just_below)
{
    Uint32 xp5 = calculate_exp(5);
    ASSERT_EQ(4, (int)calculate_level(xp5 - 1)) << "1 below boundary should give level - 1";
}


TEST(GuyCalcs, calculate_level_roundtrip_extended)
{
    for (int i = 1; i <= 15; i++) {
        Uint32 xp = calculate_exp(i);
        ASSERT_EQ(i, (int)calculate_level(xp)) << "calculate_level(calculate_exp(n)) should == n";
    }
}


// ---------------------------------------------------------------------------
// guy constructor tests
// ---------------------------------------------------------------------------

TEST(GuyCalcs, guy_default_constructor)
{
    guy g;
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)g.family) << "default guy should be soldier";
    ASSERT_EQ(1, (int)g.level) << "default level should be 1";
    ASSERT_EQ(0, (int)g.exp) << "default exp should be 0";
    ASSERT_EQ(0, (int)g.kills) << "default kills should be 0";
}


TEST(GuyCalcs, guy_family_constructor_soldier)
{
    guy g(FAMILY_SOLDIER);
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)g.family) << "family should be soldier";
    ASSERT_EQ(12, (int)g.strength) << "soldier STR should be 12";
    ASSERT_EQ(6, (int)g.dexterity) << "soldier DEX should be 6";
    ASSERT_EQ(12, (int)g.constitution) << "soldier CON should be 12";
    ASSERT_EQ(8, (int)g.intelligence) << "soldier INT should be 8";
    ASSERT_EQ(9, (int)g.armor) << "soldier ARMOR should be 9";
    ASSERT_EQ(1, (int)g.level) << "soldier level should be 1";
}


TEST(GuyCalcs, guy_family_constructor_mage)
{
    guy g(FAMILY_MAGE);
    ASSERT_EQ((int)FAMILY_MAGE, (int)g.family) << "family should be mage";
    ASSERT_EQ(4, (int)g.strength) << "mage STR should be 4";
    ASSERT_EQ(16, (int)g.intelligence) << "mage INT should be 16";
}


// guy::guy(int) copies the family descriptor's `stats` block verbatim (the
// `stats = { ... }` table in packs/core/families/living-*.lua) into
// strength/dexterity/constitution/intelligence/armor/level, and the
// descriptor's `name` into the guy's name. Pin the whole block per family:
// a constructor that reads the wrong StatAxis, that swaps two axes, or that
// falls through to the BEAST default {12,6,12,8,6} fails here instead of
// shipping a character sheet nobody checked.
TEST(GuyCalcs, guy_family_constructor_applies_each_familys_base_stat_block)
{
    struct BaseStats {
        int family;
        const char* name;
        int str, dex, con, intel, armor;
    };
    static const BaseStats kBase[] = {
        {FAMILY_SOLDIER,       "SOLDIER",      12,  6, 12,  8,  9},
        {FAMILY_ELF,           "ELF",           5, 14,  5, 12,  8},
        {FAMILY_ARCHER,        "ARCHER",        6, 12,  6, 10,  5},
        {FAMILY_MAGE,          "MAGE",          4,  6,  4, 16,  5},
        {FAMILY_SKELETON,      "SKELETON",      9, 14,  9,  6,  6},
        {FAMILY_CLERIC,        "CLERIC",        6,  7,  6, 14,  7},
        {FAMILY_FIREELEMENTAL, "ELEMENTAL",    14, 10, 14, 14,  9},
        {FAMILY_FAERIE,        "FAERIE",        3,  8,  3, 14,  2},
        {FAMILY_SMALL_SLIME,   "SLIME",        18,  2, 18,  7,  6},
        {FAMILY_THIEF,         "THIEF",         9, 12, 12, 10,  5},
        {FAMILY_GHOST,         "GHOST",         6, 12, 18, 10, 15},
        {FAMILY_DRUID,         "DRUID",         7,  8, 14, 12,  7},
        {FAMILY_ORC,           "ORC",          18,  8, 16,  5, 11},
        {FAMILY_BIG_ORC,       "ORC CAPTAIN",  18,  8, 16,  5, 11},
        {FAMILY_BARBARIAN,     "BARBARIAN",    14,  5, 14,  8,  8},
        {FAMILY_ARCHMAGE,      "ARCHMAGE",      4,  6,  4, 16,  5},
    };

    for (const BaseStats& row : kBase) {
        guy g(row.family);
        ASSERT_EQ(row.family, (int)g.family) << row.name << ": family matches the constructor arg";
        ASSERT_STREQ(row.name, g.name.c_str()) << row.name << ": the descriptor name is copied in";
        EXPECT_EQ(row.str, (int)g.strength) << row.name << ": base STR";
        EXPECT_EQ(row.dex, (int)g.dexterity) << row.name << ": base DEX";
        EXPECT_EQ(row.con, (int)g.constitution) << row.name << ": base CON";
        EXPECT_EQ(row.intel, (int)g.intelligence) << row.name << ": base INT";
        EXPECT_EQ(row.armor, (int)g.armor) << row.name << ": base ARMOR";
        EXPECT_EQ(1, (int)g.level) << row.name << ": every family starts at level 1";
        EXPECT_EQ(0, (int)g.exp) << row.name << ": a fresh guy has no XP";
        EXPECT_EQ(0, (int)g.kills) << row.name << ": a fresh guy has no kills";
    }
}


TEST(GuyCalcs, guy_copy_constructor)
{
    guy original(FAMILY_ARCHER);
    original.strength = 50;
    original.kills = 10;
    original.exp = 5000;

    guy copy(original);
    ASSERT_EQ((int)original.family, (int)copy.family) << "copy family should match";
    ASSERT_EQ((int)original.strength, (int)copy.strength) << "copy strength should match";
    ASSERT_EQ((int)original.kills, (int)copy.kills) << "copy kills should match";
    ASSERT_EQ((int)original.exp, (int)copy.exp) << "copy exp should match";
}


// ---------------------------------------------------------------------------
// get_*_bonus tests
// ---------------------------------------------------------------------------

TEST(GuyCalcs, guy_get_hp_bonus)
{
    guy g(FAMILY_SOLDIER);
    float hp = g.get_hp_bonus();
    // HP bonus = 10 + constitution*3 = 10 + 12*3 = 46
    ASSERT_TRUE(hp > 45.9f && hp < 46.1f) << "soldier HP bonus should be ~46";
}


TEST(GuyCalcs, guy_get_mp_bonus)
{
    guy g(FAMILY_MAGE);
    float mp = g.get_mp_bonus();
    // MP bonus = 10 + intelligence*3 = 10 + 16*3 = 58
    ASSERT_TRUE(mp > 57.9f && mp < 58.1f) << "mage MP bonus should be ~58";
}


TEST(GuyCalcs, guy_get_damage_bonus)
{
    guy g(FAMILY_SOLDIER);
    float dmg = g.get_damage_bonus();
    // Damage bonus = strength/4 = 12/4 = 3
    ASSERT_TRUE(dmg > 2.9f && dmg < 3.1f) << "soldier damage bonus should be ~3";
}


TEST(GuyCalcs, guy_get_armor_bonus)
{
    guy g(FAMILY_SOLDIER);
    float arm = g.get_armor_bonus();
    // Armor bonus = armor = 9
    ASSERT_TRUE(arm > 8.9f && arm < 9.1f) << "soldier armor bonus should be ~9";
}


TEST(GuyCalcs, guy_get_speed_bonus)
{
    guy g(FAMILY_SOLDIER);
    float spd = g.get_speed_bonus();
    // Speed bonus = dexterity/54 = 6/54 ≈ 0.111
    ASSERT_TRUE(spd > 0.1f && spd < 0.12f) << "soldier speed bonus should be ~0.111";
}


TEST(GuyCalcs, guy_get_fire_frequency_bonus)
{
    guy g(FAMILY_SOLDIER);
    float freq = g.get_fire_frequency_bonus();
    // Fire freq bonus = dexterity/47 = 6/47 ≈ 0.128
    ASSERT_TRUE(freq > 0.12f && freq < 0.14f) << "soldier fire freq bonus should be ~0.128";
}


TEST(GuyCalcs, guy_bonuses_scale_with_stats)
{
    guy g(FAMILY_SOLDIER);
    float hp1 = g.get_hp_bonus();
    g.constitution = 100;
    float hp2 = g.get_hp_bonus();
    ASSERT_TRUE(hp2 > hp1) << "HP bonus should increase with constitution";

    float mp1 = g.get_mp_bonus();
    g.intelligence = 100;
    float mp2 = g.get_mp_bonus();
    ASSERT_TRUE(mp2 > mp1) << "MP bonus should increase with intelligence";
}


// ---------------------------------------------------------------------------
// query_heart_value tests
// ---------------------------------------------------------------------------

TEST(GuyCalcs, guy_query_heart_value_base)
{
    guy g(FAMILY_SOLDIER);
    Sint32 val = g.query_heart_value();
    // Base cost = costlist[FAMILY_SOLDIER] = 250
    // No stat increases, so cost = 250
    ASSERT_EQ(250, (int)val) << "base soldier should cost 250";
}


TEST(GuyCalcs, guy_query_heart_value_with_stats)
{
    guy g(FAMILY_SOLDIER);
    g.strength = 20;  // 8 above base of 12
    Sint32 val = g.query_heart_value();
    ASSERT_EQ(531, (int)val) << "soldier strength delta should follow the legacy stat cost curve";
}


TEST(GuyCalcs, guy_query_heart_value_large_delta_matches_legacy_curve)
{
    guy g(FAMILY_SOLDIER);
    g.strength = static_cast<short>(g.strength + 3000);
    const Sint32 val = g.query_heart_value();
    ASSERT_EQ(16249210, (int)val) << "large stat deltas should keep the deterministic legacy curve";
}


TEST(GuyCalcs, guy_query_heart_value_different_families)
{
    guy g1(FAMILY_SOLDIER);
    guy g2(FAMILY_FIREELEMENTAL);
    Sint32 v1 = g1.query_heart_value();
    Sint32 v2 = g2.query_heart_value();
    ASSERT_TRUE(v2 > v1) << "fire elemental should cost more than soldier at base stats";
}


// ---------------------------------------------------------------------------
// upgrade_to_level tests
// ---------------------------------------------------------------------------

// guy_upgrade_to_level_basic ("strength went up, exp > 0") and
// guy_upgrade_to_level_mage ("INT gain > STR gain") are folded into
// guy_upgrade_to_level_applies_each_familys_gain_vector below, which pins
// every axis of every family's gain vector exactly -- including the mage row
// {4,6,4,16,1} the inequality was gesturing at -- plus the default set_xp
// arm those two shared.


// Every playable family's level-up gain vector, pinned exactly. The Lua
// level_up hook in packs/core/families/living-*.lua is what makes these
// differ; a family with no hook falls back to kDefaultLevelUpGains{8,6,8,8,1}
// (soldier, cleric, slime, ghost). If the hook dispatch in
// guy::upgrade_to_level is lost, every hooked row below reads the default
// vector instead and fails.
TEST(GuyCalcs, guy_upgrade_to_level_applies_each_familys_gain_vector)
{
    struct FamilyGains {
        int family;
        const char* label;
        int str, dex, con, intel, armor;
    };
    static const FamilyGains kGains[] = {
        {FAMILY_SOLDIER,       "soldier",       8,  6,  8,  8, 1},  // no hook: default
        {FAMILY_ELF,           "elf",           6,  9,  6,  8, 1},
        {FAMILY_ARCHER,        "archer",        4,  9,  8,  8, 1},
        {FAMILY_MAGE,          "mage",          4,  6,  4, 16, 1},
        {FAMILY_SKELETON,      "skeleton",      8, 12,  4,  4, 1},
        {FAMILY_CLERIC,        "cleric",        8,  6,  8,  8, 1},  // no hook: default
        {FAMILY_FIREELEMENTAL, "fire elemental",12,  6,  4,  8, 1},
        {FAMILY_FAERIE,        "faerie",        4, 12,  4,  8, 1},
        {FAMILY_SMALL_SLIME,   "small slime",   8,  6,  8,  8, 1},  // no hook: default
        {FAMILY_THIEF,         "thief",         4, 12,  4,  8, 1},
        {FAMILY_GHOST,         "ghost",         8,  6,  8,  8, 1},  // no hook: default
        {FAMILY_DRUID,         "druid",         8,  3,  8, 12, 1},
        {FAMILY_ORC,           "orc",          12,  3, 12,  4, 1},
        {FAMILY_BIG_ORC,       "orc captain",  12,  3, 12,  4, 1},
        {FAMILY_BARBARIAN,     "barbarian",    12,  3, 12,  4, 1},
        {FAMILY_ARCHMAGE,      "archmage",      4,  6,  4, 16, 1},
    };

    for (const FamilyGains& row : kGains) {
        guy g(row.family);
        ASSERT_EQ(1, (int)g.level) << row.label << " starts at level 1";
        const int orig_str = g.strength;
        const int orig_dex = g.dexterity;
        const int orig_con = g.constitution;
        const int orig_int = g.intelligence;
        const int orig_arm = g.armor;

        g.upgrade_to_level(3);   // level_diff == 2

        ASSERT_EQ(3, (int)g.level) << row.label << ": upgrade_to_level sets the level";
        EXPECT_EQ((int)calculate_exp(3), (int)g.exp)
            << row.label << ": set_xp defaults to true, so XP lands on the level-3 threshold";
        EXPECT_EQ(orig_str + 2 * row.str, (int)g.strength)
            << row.label << ": STR gain is 2 levels * " << row.str;
        EXPECT_EQ(orig_dex + 2 * row.dex, (int)g.dexterity)
            << row.label << ": DEX gain is 2 levels * " << row.dex;
        EXPECT_EQ(orig_con + 2 * row.con, (int)g.constitution)
            << row.label << ": CON gain is 2 levels * " << row.con;
        EXPECT_EQ(orig_int + 2 * row.intel, (int)g.intelligence)
            << row.label << ": INT gain is 2 levels * " << row.intel;
        EXPECT_EQ(orig_arm + 2 * row.armor, (int)g.armor)
            << row.label << ": ARMOR gain is 2 levels * " << row.armor;
    }
}


TEST(GuyCalcs, guy_upgrade_to_level_sets_xp)
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    ASSERT_EQ((int)calculate_exp(5), (int)g.exp) << "XP should be set to level 5 threshold";
}


TEST(GuyCalcs, guy_upgrade_to_level_no_xp)
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, false);
    ASSERT_EQ(0, (int)g.exp) << "XP should remain 0 when set_xp=false";
}


TEST(GuyCalcs, guy_upgrade_level_10)
{
    guy g(FAMILY_SOLDIER);
    // A soldier has no Lua level_up hook, so nine levels of
    // kDefaultLevelUpGains{8,6,8,8,1} land on the base {12,6,12,8,9}.
    ASSERT_EQ(12, (int)g.strength) << "soldier base STR";
    g.upgrade_to_level(10);
    ASSERT_EQ(10, (int)g.level) << "level should be 10";
    ASSERT_EQ(84, (int)g.strength) << "soldier STR at level 10 is 12 + 9*8";
    EXPECT_EQ(60, (int)g.dexterity) << "soldier DEX at level 10 is 6 + 9*6";
    EXPECT_EQ(84, (int)g.constitution) << "soldier CON at level 10 is 12 + 9*8";
    EXPECT_EQ(80, (int)g.intelligence) << "soldier INT at level 10 is 8 + 9*8";
    EXPECT_EQ(18, (int)g.armor) << "soldier ARMOR at level 10 is 9 + 9*1";
}
