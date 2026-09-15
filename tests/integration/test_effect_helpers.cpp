#include <openglad/interface/render/view.h>
#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// compute_hp_color tests
// ---------------------------------------------------------------------------

TEST(EffectHelpers, hp_color_low)
{
    // hp * 3 < maxhp => LOW_HP_COLOR (42)
    ASSERT_EQ(42, static_cast<int>(compute_hp_color(10.0f, 100.0f))) << "low hp (10/100) should return LOW_HP_COLOR";
    ASSERT_EQ(42, static_cast<int>(compute_hp_color(1.0f, 100.0f))) << "very low hp (1/100) should return LOW_HP_COLOR";
}


TEST(EffectHelpers, hp_color_mid)
{
    // hp * 3 >= maxhp but hp * 3/2 < maxhp => MID_HP_COLOR-3 (234)
    ASSERT_EQ(237 - 3, static_cast<int>(compute_hp_color(40.0f, 100.0f))) << "mid hp (40/100) should return MID_HP_COLOR-3";
}


TEST(EffectHelpers, hp_color_high_not_full)
{
    // hp * 3/2 >= maxhp but hp < maxhp => MAX_HP_COLOR+4 (60)
    ASSERT_EQ(56 + 4, static_cast<int>(compute_hp_color(90.0f, 100.0f))) << "high hp (90/100) should return MAX_HP_COLOR+4";
}


TEST(EffectHelpers, hp_color_full)
{
    // hp == maxhp => HIGH_HP_COLOR+2 (63)
    ASSERT_EQ(61 + 2, static_cast<int>(compute_hp_color(100.0f, 100.0f))) << "full hp should return HIGH_HP_COLOR+2";
}


TEST(EffectHelpers, hp_color_over_max)
{
    // hp > maxhp => ORANGE_START (224)
    ASSERT_EQ(224, static_cast<int>(compute_hp_color(150.0f, 100.0f))) << "over-max hp should return ORANGE_START";
}


// ---------------------------------------------------------------------------
// compute_mp_color tests
// ---------------------------------------------------------------------------

TEST(EffectHelpers, mp_color_low)
{
    ASSERT_EQ(42, static_cast<int>(compute_mp_color(10.0f, 100.0f))) << "low mp should return LOW_MP_COLOR";
}


TEST(EffectHelpers, mp_color_mid)
{
    ASSERT_EQ(108, static_cast<int>(compute_mp_color(40.0f, 100.0f))) << "mid mp should return MID_MP_COLOR";
}


TEST(EffectHelpers, mp_color_high_not_full)
{
    ASSERT_EQ(64, static_cast<int>(compute_mp_color(90.0f, 100.0f))) << "high mp (not full) should return MAX_MP_COLOR";
}


TEST(EffectHelpers, mp_color_full)
{
    ASSERT_EQ(72 + 3, static_cast<int>(compute_mp_color(100.0f, 100.0f))) << "full mp should return HIGH_MP_COLOR+3";
}


TEST(EffectHelpers, mp_color_over_max)
{
    ASSERT_EQ(208, static_cast<int>(compute_mp_color(150.0f, 100.0f))) << "over-max mp should return WATER_START";
}

