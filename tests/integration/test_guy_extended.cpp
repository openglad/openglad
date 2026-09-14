#include <openglad/gameplay/guy.h>
#include <openglad/interface/guy_create.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>

// myscreen is now a macro defined in base.h (via game_session.h)
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
int MAX(int a, int b);
Uint32 calculate_exp(Sint32 level);

namespace {

screen* guy_test_screen()
{
    return og::runtime::current_session->myscreen_;
}

// Families that register no `level_up` hook in packs/core take
// guy::upgrade_to_level's DEFAULT branch (src/gameplay/guy.cpp:421-429):
// apply_level_up() adds kDefaultLevelUpGains{8,6,8,8,1} * level_diff to the
// family's own base_stats, and only then is level/exp written. Pinning the
// level alone cannot tell that branch from a no-op, so every axis is pinned.
void expect_default_level_up_to_5(short family, const char* who)
{
    const FamilyDescriptor* fd = get_family_descriptor(family);
    ASSERT_NE(nullptr, fd) << who << " must be a registered family";
    const int base_str = fd->base_stats[StatAxis::Strength];
    const int base_dex = fd->base_stats[StatAxis::Dexterity];
    const int base_con = fd->base_stats[StatAxis::Constitution];
    const int base_int = fd->base_stats[StatAxis::Intelligence];
    const int base_armor = fd->base_stats[StatAxis::Armor];

    guy g(family);
    const int level_diff = 5 - static_cast<int>(g.level);
    ASSERT_EQ(4, level_diff) << who << " starts at level 1, so upgrading to 5 is a 4-level diff";

    g.upgrade_to_level(5, true);

    EXPECT_EQ(base_str + 8 * level_diff, (int)g.strength)
        << who << ": default gains add 8 STR per level to the family base";
    EXPECT_EQ(base_dex + 6 * level_diff, (int)g.dexterity)
        << who << ": default gains add 6 DEX per level to the family base";
    EXPECT_EQ(base_con + 8 * level_diff, (int)g.constitution)
        << who << ": default gains add 8 CON per level to the family base";
    EXPECT_EQ(base_int + 8 * level_diff, (int)g.intelligence)
        << who << ": default gains add 8 INT per level to the family base";
    EXPECT_EQ(base_armor + 1 * level_diff, (int)g.armor)
        << who << ": default gains add 1 armor per level to the family base";
    ASSERT_EQ(5, (int)g.level) << who << ": upgrade_to_level writes the requested level";
    ASSERT_EQ((int)calculate_exp(5), (int)g.exp)
        << who << ": set_xp=true stamps the level-5 point on the exp curve";
}

}  // namespace

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


TEST(GuyExtended, guy_upgrade_cleric_takes_the_default_level_up_gains)
{
    // packs/core/families/living-05-cleric.lua registers no level_up hook.
    expect_default_level_up_to_5(FAMILY_CLERIC, "cleric");
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


TEST(GuyExtended, guy_upgrade_slime_takes_the_default_level_up_gains)
{
    // packs/core/families/living-08-slime.lua registers no level_up hook.
    expect_default_level_up_to_5(FAMILY_SMALL_SLIME, "small slime");
}


TEST(GuyExtended, guy_upgrade_thief)
{
    guy g(FAMILY_THIEF);
    g.upgrade_to_level(5, true);
    ASSERT_TRUE(g.dexterity > get_family_descriptor(FAMILY_THIEF)->base_stats[1]) << "thief dex should increase";
}


TEST(GuyExtended, guy_upgrade_ghost_takes_the_default_level_up_gains)
{
    // packs/core/families/living-12-ghost.lua registers no level_up hook.
    expect_default_level_up_to_5(FAMILY_GHOST, "ghost");
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
    // The soldier's loader row (packs/core/families/living-00-soldier.lua):
    // base stats 12/6/12/8, armor 9, and combat.hp 120 / melee_damage 20.
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd) << "soldier must be a registered family";
    ASSERT_FLOAT_EQ(120.0f, fd->combat.hp) << "loader HP row the bonus is added to";
    ASSERT_FLOAT_EQ(20.0f, fd->combat.melee_damage) << "loader melee damage row";

    guy g3(FAMILY_SOLDIER);
    g3.upgrade_to_level(3, true);
    ASSERT_EQ(28, (int)g3.strength) << "12 + 2 levels * 8";
    ASSERT_EQ(28, (int)g3.constitution) << "12 + 2 levels * 8";
    ASSERT_EQ(24, (int)g3.intelligence) << "8 + 2 levels * 8";
    ASSERT_EQ(18, (int)g3.dexterity) << "6 + 2 levels * 6";

    auto w3 = guy_create_walker_owned(g3, guy_test_screen());
    ASSERT_NE(nullptr, w3) << "create_walker should succeed";

    // guy::update_derived_stats, src/gameplay/guy.cpp:470-554.
    EXPECT_FLOAT_EQ(214.0f, w3->stats()->max_hitpoints())
        << "120 loader HP + get_hp_bonus() (10 + 3*28)";
    EXPECT_FLOAT_EQ(214.0f, w3->stats()->hitpoints())
        << "a fresh guy starts at full health";
    EXPECT_FLOAT_EQ(82.0f, w3->stats()->max_magicpoints())
        << "MP has no loader base: get_mp_bonus() is 10 + 3*24 on its own";
    EXPECT_FLOAT_EQ(82.0f, w3->stats()->magicpoints())
        << "a fresh guy starts at full magic";
    EXPECT_FLOAT_EQ(27.0f, w3->damage())
        << "20 loader damage + get_damage_bonus() (28/4)";
    EXPECT_FLOAT_EQ(11.0f, w3->stats()->armor())
        << "armor has no loader base: it is the guy's own armor (9 + 2 levels * 1)";

    // Regen: (con + str/6 + 1020) = 1052 is below one REGEN(4000) step, so
    // the integer part is 0 and the whole budget lands in the delay divisor.
    EXPECT_FLOAT_EQ(0.0f, w3->stats()->heal_per_round())
        << "1052 < REGEN, so no whole hit point per round";
    EXPECT_EQ(3, w3->stats()->max_heal_delay()) << "REGEN / (1052 + 1)";
    EXPECT_EQ(0, w3->stats()->current_heal_delay()) << "a fresh guy starts without healing";
    // (int*45 + dex*15 + 200) = 1550, likewise below one REGEN step.
    EXPECT_FLOAT_EQ(0.0f, w3->stats()->magic_per_round())
        << "1550 < REGEN, so no whole magic point per round";
    EXPECT_EQ(2, w3->stats()->max_magic_delay()) << "REGEN / (1550 + 1)";
    EXPECT_EQ(0, w3->stats()->current_magic_delay()) << "a fresh guy starts without regen";

    // The same loader row at level 1: only the stat-derived half moved.
    guy g1(FAMILY_SOLDIER);
    auto w1 = guy_create_walker_owned(g1, guy_test_screen());
    ASSERT_NE(nullptr, w1) << "create_walker should succeed for the level-1 baseline";
    EXPECT_FLOAT_EQ(166.0f, w1->stats()->max_hitpoints()) << "120 + (10 + 3*12)";
    EXPECT_FLOAT_EQ(34.0f, w1->stats()->max_magicpoints()) << "10 + 3*8";
    EXPECT_EQ(6, w1->stats()->max_magic_delay()) << "REGEN / (650 + 1): less INT regenerates slower";
    EXPECT_FLOAT_EQ(48.0f,
                    w3->stats()->max_hitpoints() - w1->stats()->max_hitpoints())
        << "the level-3 HP lead is exactly 3 * the constitution lead (16)";
}


TEST(GuyExtended, guy_update_derived_stats_all_families)
{
    const short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    int checked = 0;
    for (short family : families) {
        const FamilyDescriptor* fd = get_family_descriptor(family);
        ASSERT_NE(nullptr, fd) << "family " << family << " must be registered";
        guy g(family);
        g.upgrade_to_level(3, true);
        auto w = guy_create_walker_owned(g, guy_test_screen());
        ASSERT_NE(nullptr, w) << "create_walker should succeed for family " << family;

        // Every derived value is the family's OWN loader row plus that
        // family's own stats; one row read fourteen times fails here.
        EXPECT_FLOAT_EQ(fd->combat.hp + 10.0f + 3.0f * (float)g.constitution,
                        w->stats()->max_hitpoints())
            << fd->name << ": loader HP + get_hp_bonus()";
        EXPECT_FLOAT_EQ(w->stats()->max_hitpoints(), w->stats()->hitpoints())
            << fd->name << ": a fresh guy starts at full health";
        EXPECT_FLOAT_EQ(10.0f + 3.0f * (float)g.intelligence,
                        w->stats()->max_magicpoints())
            << fd->name << ": MP is get_mp_bonus() alone (no loader base)";
        EXPECT_FLOAT_EQ(fd->combat.melee_damage + (float)g.strength / 4.0f,
                        w->damage())
            << fd->name << ": loader damage + get_damage_bonus()";
        EXPECT_FLOAT_EQ((float)g.armor, w->stats()->armor())
            << fd->name << ": armor is the guy's own armor score";
        ++checked;
    }
    ASSERT_EQ(14, checked) << "every listed family must have been checked";
}


// ---------------------------------------------------------------------------
// query_heart_value (lines 134-179)
// ---------------------------------------------------------------------------

TEST(GuyExtended, guy_query_heart_value_all_families)
{
    const short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    int checked = 0;
    for (short family : families) {
        const FamilyDescriptor* fd = get_family_descriptor(family);
        ASSERT_NE(nullptr, fd) << "family " << family << " must be registered";
        guy g(family);
        // Every stat delta against a base-stat twin is 0, so the cost curves
        // contribute nothing and the value is exactly the hiring cost
        // (src/gameplay/guy.cpp:288-327) -- the identity
        // GuyCalcs.guy_query_heart_value_base pins for the soldier's 250.
        EXPECT_EQ(fd->hiring_cost, (int)g.query_heart_value())
            << fd->name << ": a base-stat recruit is worth exactly its hiring cost";
        ++checked;
    }
    ASSERT_EQ(14, checked) << "every listed family must have been priced";
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
        ASSERT_TRUE(w->stats()->level() == 2) << "walker level should match guy level";
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
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, fd) << "soldier must be a registered family";
    // The clamps only ENGAGE above the loader row, so pin what they start from.
    ASSERT_FLOAT_EQ(4.0f, fd->combat.stepsize) << "loader stepsize row";
    ASSERT_FLOAT_EQ(6.0f, fd->combat.fire_delay) << "loader fire-delay row";

    guy g(FAMILY_SOLDIER);
    g.dexterity = 3000;
    g.constitution = 3000;
    g.strength = 3000;
    g.intelligence = 3000;
    g.level = 1;

    auto w = guy_create_walker_owned(g, guy_test_screen());
    ASSERT_NE(nullptr, w) << "walker should be created";

    // src/gameplay/guy.cpp:490-499: both bonuses are applied, then clamped.
    EXPECT_FLOAT_EQ(12.0f, w->stepsize())
        << "4 + get_speed_bonus() (3000/54) clamps down to exactly 12";
    EXPECT_FLOAT_EQ(12.0f, w->normal_stepsize())
        << "normal_stepsize follows the clamped stepsize";
    EXPECT_FLOAT_EQ(1.0f, w->fire_frequency())
        << "6 - get_fire_frequency_bonus() (3000/47) clamps up to exactly 1";

    // Heal budget (con + str/6 + 1020) = 4520: one whole REGEN(4000) step,
    // leaving 520 as the delay divisor.
    EXPECT_FLOAT_EQ(1.0f, w->stats()->heal_per_round()) << "4520 / REGEN = 1 whole step";
    EXPECT_EQ(7, w->stats()->max_heal_delay()) << "REGEN / (520 + 1)";
    // Magic budget (int*45 + dex*15 + 200) = 180200: 45 whole steps, 200 left.
    EXPECT_FLOAT_EQ(45.0f, w->stats()->magic_per_round()) << "180200 / REGEN = 45 whole steps";
    EXPECT_EQ(19, w->stats()->max_magic_delay()) << "REGEN / (200 + 1)";
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
