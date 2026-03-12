#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)
#include <openglad/gameplay/family_descriptor.h>
#include <openglad/gameplay/family_registry.h>
int MAX(int a, int b);

// ---------------------------------------------------------------------------
// upgrade_to_level - exercises the big family switch (lines 323-456)
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_upgrade_soldier)
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.strength > get_family_descriptor(FAMILY_SOLDIER)->base_stats[0]) << "soldier str should increase";
    ASSERT_TRUE(g.level == 5) << "level should be 5";
    ASSERT_TRUE(g.exp > 0) << "exp should be set when set_xp=true";
}


TEST(GuyExtended, guy_upgrade_elf)
{
    guy g(FAMILY_ELF);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_ELF)->base_stats[1]) << "elf dex should increase significantly";
}


TEST(GuyExtended, guy_upgrade_archer)
{
    guy g(FAMILY_ARCHER);
    g.upgrade_to_level(5, false);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_ARCHER)->base_stats[1]) << "archer dex should increase";
    ASSERT_EQ(0, (int)g.exp) << "exp should be 0 when set_xp=false";
}


TEST(GuyExtended, guy_upgrade_mage)
{
    guy g(FAMILY_MAGE);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.intelligence > get_family_descriptor(FAMILY_MAGE)->base_stats[3]) << "mage int should increase most";
}


TEST(GuyExtended, guy_upgrade_skeleton)
{
    guy g(FAMILY_SKELETON);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_SKELETON)->base_stats[1]) << "skeleton dex should increase";
}


TEST(GuyExtended, guy_upgrade_cleric)
{
    guy g(FAMILY_CLERIC);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.level == 5) << "cleric level should be 5";
}


TEST(GuyExtended, guy_upgrade_fireelemental)
{
    guy g(FAMILY_FIREELEMENTAL);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.strength > get_family_descriptor(FAMILY_FIREELEMENTAL)->base_stats[0]) << "fire elem str should increase";
}


TEST(GuyExtended, guy_upgrade_faerie)
{
    guy g(FAMILY_FAERIE);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_FAERIE)->base_stats[1]) << "faerie dex should increase";
}


TEST(GuyExtended, guy_upgrade_slime)
{
    guy g(FAMILY_SMALL_SLIME);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.level == 5) << "slime level should be 5";
}


TEST(GuyExtended, guy_upgrade_thief)
{
    guy g(FAMILY_THIEF);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_THIEF)->base_stats[1]) << "thief dex should increase";
}


TEST(GuyExtended, guy_upgrade_ghost)
{
    guy g(FAMILY_GHOST);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.level == 5) << "ghost level should be 5";
}


TEST(GuyExtended, guy_upgrade_druid)
{
    guy g(FAMILY_DRUID);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.intelligence > get_family_descriptor(FAMILY_DRUID)->base_stats[3]) << "druid int should increase";
}


TEST(GuyExtended, guy_upgrade_orc)
{
    guy g(FAMILY_ORC);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.strength > get_family_descriptor(FAMILY_ORC)->base_stats[0]) << "orc str should increase";
}


TEST(GuyExtended, guy_upgrade_barbarian)
{
    guy g(FAMILY_BARBARIAN);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.strength > get_family_descriptor(FAMILY_BARBARIAN)->base_stats[0]) << "barbarian str should increase";
}


TEST(GuyExtended, guy_upgrade_archmage)
{
    guy g(FAMILY_ARCHMAGE);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.intelligence > get_family_descriptor(FAMILY_ARCHMAGE)->base_stats[3]) << "archmage int should increase";
}


// ---------------------------------------------------------------------------
// update_derived_stats (lines 492-571) - exercises HP/MP/speed/armor calc
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_update_derived_stats_soldier)
{
    guy g(FAMILY_SOLDIER);
    g.upgrade_to_level(3, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(w->stats()->max_hitpoints > 0) << "HP should be positive";
    ASSERT_TRUE(w->stats()->max_magicpoints >= 0) << "MP should be non-negative";
    ASSERT_TRUE(w->stats()->heal_per_round >= 0) << "heal_per_round should be non-negative";
    ASSERT_TRUE(w->stats()->magic_per_round >= 0) << "magic_per_round should be non-negative";
}


TEST(GuyExtended, guy_update_derived_stats_all_families)
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        g.upgrade_to_level(3, true);
        auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
        if (w) {
            ASSERT_TRUE(w->stats()->max_hitpoints > 0) << "HP should be positive for all families";
        }
    }
}


// ---------------------------------------------------------------------------
// query_heart_value (lines 134-179)
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_query_heart_value_all_families)
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        guy g(families[i]);
        Sint32 val = g.query_heart_value();
        ASSERT_TRUE(val > 0) << "heart value should be positive for base stats";
    }
}


TEST(GuyExtended, guy_query_heart_value_upgraded)
{
    guy g(FAMILY_SOLDIER);
    Sint32 base_val = g.query_heart_value();
    g.upgrade_to_level(5, true);
    Sint32 upgraded_val = g.query_heart_value();
    ASSERT_TRUE(upgraded_val > base_val) << "upgraded guy should be worth more";
}


// ---------------------------------------------------------------------------
// create_walker and create_and_add_walker
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_create_walker_various)
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC };
    for (int i = 0; i < 6; i++) {
        guy g(families[i]);
        g.upgrade_to_level(2, true);
        auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
        ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
        ASSERT_TRUE(w->myguy != nullptr) << "walker should have myguy set";
        ASSERT_TRUE(w->stats()->level == 2) << "walker level should match guy level";
    }
}


// ---------------------------------------------------------------------------
// Copy constructor
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_copy_constructor_all_fields)
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
    ASSERT_EQ((int)original.family, (int)copy.family) << "family should match";
    ASSERT_EQ((int)original.strength, (int)copy.strength) << "strength should match";
    ASSERT_EQ((int)original.intelligence, (int)copy.intelligence) << "intelligence should match";
    ASSERT_EQ((int)original.dexterity, (int)copy.dexterity) << "dexterity should match";
    ASSERT_EQ((int)original.constitution, (int)copy.constitution) << "constitution should match";
    ASSERT_EQ((int)original.armor, (int)copy.armor) << "armor should match";
    ASSERT_EQ((int)original.level, (int)copy.level) << "level should match";
    ASSERT_EQ((int)original.kills, (int)copy.kills) << "kills should match";
    ASSERT_EQ((int)original.total_damage, (int)copy.total_damage) << "total_damage should match";
}


// ---------------------------------------------------------------------------
// Derived stat bonus functions
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_derived_bonus_scaling)
{
    guy g(FAMILY_SOLDIER);
    float hp1 = g.get_hp_bonus();
    g.constitution += 10;
    float hp2 = g.get_hp_bonus();
    ASSERT_TRUE(hp2 > hp1) << "more constitution should give more HP bonus";

    guy g2(FAMILY_MAGE);
    float mp1 = g2.get_mp_bonus();
    g2.intelligence += 10;
    float mp2 = g2.get_mp_bonus();
    ASSERT_TRUE(mp2 > mp1) << "more intelligence should give more MP bonus";
}


TEST(GuyExtended, guy_unknown_family_fallback_and_zero_heart_value)
{
    guy unknown(127);
    ASSERT_EQ(12, (int)unknown.strength) << "unknown family should use fallback STR";
    ASSERT_EQ(6, (int)unknown.dexterity) << "unknown family should use fallback DEX";
    ASSERT_EQ(12, (int)unknown.constitution) << "unknown family should use fallback CON";
    ASSERT_EQ(8, (int)unknown.intelligence) << "unknown family should use fallback INT";
    ASSERT_EQ(6, (int)unknown.armor) << "unknown family should use fallback armor";
    ASSERT_EQ(1, (int)unknown.level) << "unknown family should use fallback level";

    unknown.family = 127;
    ASSERT_EQ(0, (int)unknown.query_heart_value()) << "unknown family should have zero heart value";
}


TEST(GuyExtended, guy_update_derived_stats_clamps_speed_and_regen_delays)
{
    guy g(FAMILY_SOLDIER);
    g.dexterity = 3000;
    g.constitution = 3000;
    g.strength = 3000;
    g.intelligence = 3000;
    g.level = 1;

    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    ASSERT_TRUE(w != nullptr) << "walker should be created";
    if (!w)
        return;

    ASSERT_TRUE(w->stepsize <= 12.0f) << "stepsize should clamp to 12";
    ASSERT_TRUE(w->fire_frequency >= 1.0f) << "fire_frequency should clamp to minimum 1";
    ASSERT_TRUE(w->stats()->heal_per_round > 0) << "high stats should increase heal_per_round";
    ASSERT_TRUE(w->stats()->magic_per_round > 0) << "high stats should increase magic_per_round";
    ASSERT_TRUE(w->stats()->max_heal_delay >= 2) << "max_heal_delay should respect minimum clamp";
    ASSERT_TRUE(w->stats()->max_magic_delay >= 2) << "max_magic_delay should respect minimum clamp";
}


TEST(GuyExtended, guy_batch5_max_helper_and_more_unknown_family_paths)
{
    ASSERT_EQ(5, MAX(3, 5)) << "MAX should return second operand when first is lower";
    ASSERT_EQ(7, MAX(7, 2)) << "MAX should return first operand when first is higher";

    guy unknown_neg(-999);
    ASSERT_STREQ("BEAST", unknown_neg.name.c_str()) << "negative unknown family should use fallback name";
    ASSERT_EQ(1, (int)unknown_neg.level) << "negative unknown family should use fallback level";
    unknown_neg.family = static_cast<char>(-127);
    ASSERT_EQ(0, (int)unknown_neg.query_heart_value()) << "unknown negative family should report zero heart value";
}


TEST(GuyExtended, guy_round10_query_heart_value_clamps_negative_stat_deltas_to_base_cost)
{
    guy g(FAMILY_SOLDIER);
    const Sint32 base = g.query_heart_value();

    // Drop stats below base values; MAX(temp,0) branches should prevent negative contributions.
    g.strength = static_cast<short>(g.strength - 5);
    g.dexterity = static_cast<short>(g.dexterity - 5);
    g.constitution = static_cast<short>(g.constitution - 5);
    g.intelligence = static_cast<short>(g.intelligence - 5);
    g.armor = static_cast<short>(g.armor - 5);

    const Sint32 lowered = g.query_heart_value();
    ASSERT_EQ(base, lowered) << "query_heart_value should clamp negative stat deltas and keep base hiring cost only";
}

