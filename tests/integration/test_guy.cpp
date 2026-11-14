#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <openglad/core/combat_math.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/resources/save_data.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/screen.h>
#include <memory>

// myscreen is now a macro defined in base.h (via game_session.h)

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

    guy progressed(FAMILY_ARCHER);
    progressed.strength = 50;
    progressed.kills = 10;
    progressed.exp = 5000;
    guy progressed_copy(progressed);
    EXPECT_EQ(FAMILY_ARCHER, progressed_copy.family);
    EXPECT_EQ(50, progressed_copy.strength);
    EXPECT_EQ(10, progressed_copy.kills);
    EXPECT_EQ(5000u, progressed_copy.exp);
}



// Test: Campaign level completion tracking
TEST(Guy, campaign_progress) {
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";

    // Initially no levels completed
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should not be completed initially";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("gladiator")) << "no levels should be completed initially";

    // Mark some levels as completed
    og::runtime::current_session->myscreen_->save_data.add_level_completed("gladiator", 1);
    og::runtime::current_session->myscreen_->save_data.add_level_completed("gladiator", 2);
    og::runtime::current_session->myscreen_->save_data.add_level_completed("gladiator", 3);

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be completed";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(2)) << "level 2 should be completed";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(3)) << "level 3 should be completed";
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(4)) << "level 4 should not be completed";

    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("gladiator")) << "3 levels should be completed";

    // Different campaign should have 0 completions
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("some.other.campaign")) << "other campaign should have 0 completions";

    // Reset campaign should clear completions
    og::runtime::current_session->myscreen_->save_data.reset_campaign("gladiator");
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.get_num_levels_completed("gladiator")) << "completions should be 0 after reset_campaign";
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

    og::runtime::current_session->myscreen_->save_data.add_level_completed("gladiator", 1);

    // Now reset
    og::runtime::current_session->myscreen_->save_data.reset();

    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.team_size) << "team_size should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore)) << "totalscore should be 0 after reset";
}


// ---------------------------------------------------------------------------
// Stat bonus function tests
// ---------------------------------------------------------------------------

TEST(Guy, hp_bonus)
{
    guy g(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(46.0f, g.get_hp_bonus());
    g.constitution = 0;
    EXPECT_FLOAT_EQ(10.0f, g.get_hp_bonus());
    g.constitution = 10;
    EXPECT_FLOAT_EQ(40.0f, g.get_hp_bonus());
    g.constitution = 100;
    EXPECT_FLOAT_EQ(310.0f, g.get_hp_bonus());
}


TEST(Guy, mp_bonus)
{
    guy g(FAMILY_MAGE);
    EXPECT_FLOAT_EQ(58.0f, g.get_mp_bonus());
    g.intelligence = 0;
    EXPECT_FLOAT_EQ(10.0f, g.get_mp_bonus());
    g.intelligence = 20;
    EXPECT_FLOAT_EQ(70.0f, g.get_mp_bonus());
    guy soldier(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(34.0f, soldier.get_mp_bonus());
    soldier.intelligence = 100;
    EXPECT_FLOAT_EQ(310.0f, soldier.get_mp_bonus());
}


TEST(Guy, damage_bonus)
{
    guy g(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(3.0f, g.get_damage_bonus());
    g.strength = 0;
    EXPECT_FLOAT_EQ(0.0f, g.get_damage_bonus());
    g.strength = 40;
    EXPECT_FLOAT_EQ(10.0f, g.get_damage_bonus());
}


TEST(Guy, speed_bonus)
{
    guy soldier(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(6.0f / 54.0f, soldier.get_speed_bonus());
    guy g(FAMILY_ELF);
    g.dexterity = 54;
    EXPECT_FLOAT_EQ(1.0f, g.get_speed_bonus());
    g.dexterity = 0;
    EXPECT_FLOAT_EQ(0.0f, g.get_speed_bonus());
}


TEST(Guy, fire_frequency_bonus)
{
    guy soldier(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(6.0f / 47.0f, soldier.get_fire_frequency_bonus());
    guy g(FAMILY_ARCHER);
    g.dexterity = 47;
    EXPECT_FLOAT_EQ(1.0f, g.get_fire_frequency_bonus());
    g.dexterity = 0;
    EXPECT_FLOAT_EQ(0.0f, g.get_fire_frequency_bonus());
}


TEST(Guy, armor_bonus)
{
    guy g(FAMILY_SOLDIER);
    EXPECT_FLOAT_EQ(9.0f, g.get_armor_bonus());
    g.armor = 25;
    EXPECT_FLOAT_EQ(25.0f, g.get_armor_bonus());
}


// ---------------------------------------------------------------------------
// upgrade_to_level tests
// ---------------------------------------------------------------------------

TEST(Guy, upgrade_to_level_different_families)
{
    // The GAINS differ, not merely the level-1 bases: the soldier has no Lua
    // level_up hook and takes kDefaultLevelUpGains{8,6,8,8,1}; the mage's hook
    // (packs/core/families/living-03-mage.lua) applies {4,6,4,16,1}. Losing the
    // hook dispatch would give the mage 4+8*4=36 STR / 16+8*4=48 INT here.
    guy soldier(FAMILY_SOLDIER);
    guy mage(FAMILY_MAGE);
    ASSERT_EQ(12, (int)soldier.strength) << "soldier base STR";
    ASSERT_EQ(8, (int)soldier.intelligence) << "soldier base INT";
    ASSERT_EQ(4, (int)mage.strength) << "mage base STR";
    ASSERT_EQ(16, (int)mage.intelligence) << "mage base INT";

    soldier.upgrade_to_level(5);
    mage.upgrade_to_level(5);
    EXPECT_EQ(5, (int)soldier.level);

    EXPECT_EQ(44, (int)soldier.strength) << "soldier STR at level 5 is 12 + 4*8";
    EXPECT_EQ(40, (int)soldier.intelligence) << "soldier INT at level 5 is 8 + 4*8";
    EXPECT_EQ(20, (int)mage.strength) << "mage STR at level 5 is 4 + 4*4 (Lua hook)";
    EXPECT_EQ(80, (int)mage.intelligence) << "mage INT at level 5 is 16 + 4*16 (Lua hook)";
}


// ---------------------------------------------------------------------------
// Combat XP -> leveling, end to end
// ---------------------------------------------------------------------------
// GuyCalcs pins the XP ladder and inverse; combat_math covers
// the award curve, but nothing joined the two: does a realistic run of kills
// produce the level it should? These pin that, and specifically that the XP
// saturation clamp never reaches into ordinary progression.
//
// They assert CONCRETE award values on purpose. Asserting "below the clamp"
// would be vacuous -- any clamp value satisfies it, including one pulled down
// into normal play, which is the regression worth catching.

namespace {

// Exactly what walker::attack does with the award: a short, widened to the
// uint32 exp counter. That cast is the hazard -- a negative short lands as
// ~4.29e9 here.
void award(Uint32& exp, short xp)
{
    exp += static_cast<Uint32>(xp);
}

}  // namespace

TEST(Guy, combat_xp_levels_a_character_at_the_expected_rate)
{
    // A level-1 character killing level-1 foes. 181 is the unclamped award and
    // is pinned here: if saturation ever moved down into normal play this is
    // the assertion that notices.
    const short per_kill = compute_xp_from_kill(0);
    ASSERT_EQ(181, per_kill) << "even-level kill award";

    Uint32 exp = 0;
    int kills = 0;
    while (calculate_level(exp) < 2)
    {
        award(exp, per_kill);
        ++kills;
        ASSERT_LT(kills, 10000) << "leveling must terminate";
    }
    // calculate_exp(2) is 10000, so 56 kills (55 leaves you 5 xp short).
    EXPECT_EQ(56, kills) << "kills to reach level 2";
    EXPECT_EQ(2, calculate_level(exp));
}

TEST(Guy, leveling_over_a_campaign_never_reaches_the_xp_clamp)
{
    // Walk a character up through the levels a campaign covers, killing foes
    // across the whole spread of gaps the game presents -- five levels above
    // down to five below. The clamp is only reachable above 32767, so pinning
    // the exact total proves every award in that range is the unclamped value
    // and progression is bit-identical to the pre-clamp code.
    Uint32 exp = 0;
    for (Sint32 my_level = 1; my_level <= 20; ++my_level)
    {
        for (Sint32 foe_level = my_level - 5; foe_level <= my_level + 5;
             ++foe_level)
        {
            if (foe_level < 1)
                continue;
            const short xp = compute_xp_from_kill(my_level - foe_level);
            ASSERT_GE(xp, 0) << "level " << my_level << " vs " << foe_level;
            award(exp, xp);
        }
    }
    // A measured canary, not a derived constant: there is no closed form for
    // a sum over 200 awards. Its job is to move the moment any award in this
    // range does -- a clamp pulled down into normal play, or a retune of the
    // curve. If you change the curve deliberately, re-measure it.
    EXPECT_EQ(26976u, exp) << "total XP for a 20-level campaign sweep";
    EXPECT_EQ(3, calculate_level(exp));
}
