#include <openglad/gameplay/guy.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>

#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>

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
    Uint32 xp3 = calculate_exp(3);
    ASSERT_TRUE(xp3 > 20000) << "level 3 should require > 20000 XP";
}


TEST(GuyCalcs, calculate_exp_monotonic_extended)
{
    for (int i = 1; i < 20; i++) {
        ASSERT_TRUE(calculate_exp(i+1) > calculate_exp(i)) << "XP should be monotonically increasing";
    }
}


TEST(GuyCalcs, calculate_exp_level_10)
{
    Uint32 xp10 = calculate_exp(10);
    ASSERT_TRUE(xp10 > 100000) << "level 10 should require > 100000 XP";
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


TEST(GuyCalcs, guy_family_constructor_all_families)
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        ASSERT_EQ((int)families[i], (int)g.family) << "family should match constructor arg";
        ASSERT_EQ(1, (int)g.level) << "all families start at level 1";
        ASSERT_TRUE(g.strength > 0) << "strength should be positive";
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

TEST(GuyCalcs, guy_upgrade_to_level_basic)
{
    guy g(FAMILY_SOLDIER);
    short orig_str = g.strength;
    g.upgrade_to_level(5);
    ASSERT_EQ(5, (int)g.level) << "level should be 5 after upgrade";
    ASSERT_TRUE(g.strength > orig_str) << "strength should increase after leveling";
    ASSERT_TRUE(g.exp > 0) << "XP should be set after leveling";
}


TEST(GuyCalcs, guy_upgrade_to_level_mage)
{
    guy g(FAMILY_MAGE);
    short orig_int = g.intelligence;
    short orig_str = g.strength;
    g.upgrade_to_level(5);
    // Mage gets 2x INT scaling, 0.5x STR scaling
    short int_gain = g.intelligence - orig_int;
    short str_gain = g.strength - orig_str;
    ASSERT_TRUE(int_gain > str_gain) << "mage INT gain should exceed STR gain";
}


TEST(GuyCalcs, guy_upgrade_to_level_all_families)
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
        ASSERT_EQ(3, (int)g.level) << "level should be 3";
        ASSERT_TRUE(g.strength >= orig_str) << "strength should not decrease";
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
    g.upgrade_to_level(10);
    ASSERT_EQ(10, (int)g.level) << "level should be 10";
    // STR gain: 8 * 9 levels * 1.0 = 72 + base 12 = 84
    ASSERT_TRUE(g.strength > 50) << "soldier STR at level 10 should be > 50";
}
