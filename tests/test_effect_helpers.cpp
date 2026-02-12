#include "entities/effect.h"
#include "render/view.h"
#include "test_framework.h"

// ---------------------------------------------------------------------------
// orbit_offset tests
// ---------------------------------------------------------------------------

void test_orbit_offset_north()
{
    float xd = 999, yd = 999;
    orbit_offset(0, xd, yd);
    TEST_ASSERT_EQ(0, static_cast<int>(xd), "drawcycle 0 should be due north (x=0)");
    TEST_ASSERT_EQ(-24, static_cast<int>(yd), "drawcycle 0 should be due north (y=-24)");
}
REGISTER_TEST(test_orbit_offset_north);

void test_orbit_offset_south()
{
    float xd = 999, yd = 999;
    orbit_offset(8, xd, yd);
    TEST_ASSERT_EQ(0, static_cast<int>(xd), "drawcycle 8 should be due south (x=0)");
    TEST_ASSERT_EQ(24, static_cast<int>(yd), "drawcycle 8 should be due south (y=24)");
}
REGISTER_TEST(test_orbit_offset_south);

void test_orbit_offset_wraps_at_16()
{
    // drawcycle 16 should be the same as drawcycle 0 (modulo 16)
    float xd0 = 0, yd0 = 0, xd16 = 0, yd16 = 0;
    orbit_offset(0, xd0, yd0);
    orbit_offset(16, xd16, yd16);
    TEST_ASSERT_EQ(static_cast<int>(xd0), static_cast<int>(xd16), "drawcycle 16 should wrap to match drawcycle 0 (x)");
    TEST_ASSERT_EQ(static_cast<int>(yd0), static_cast<int>(yd16), "drawcycle 16 should wrap to match drawcycle 0 (y)");
}
REGISTER_TEST(test_orbit_offset_wraps_at_16);

void test_orbit_offset_west_and_east()
{
    float xd = 0, yd = 0;
    orbit_offset(4, xd, yd);
    TEST_ASSERT_EQ(-24, static_cast<int>(xd), "drawcycle 4 should be due west (x=-24)");
    TEST_ASSERT_EQ(0, static_cast<int>(yd), "drawcycle 4 should be due west (y=0)");

    orbit_offset(12, xd, yd);
    TEST_ASSERT_EQ(24, static_cast<int>(xd), "drawcycle 12 should be due east (x=24)");
    TEST_ASSERT_EQ(0, static_cast<int>(yd), "drawcycle 12 should be due east (y=0)");
}
REGISTER_TEST(test_orbit_offset_west_and_east);

// ---------------------------------------------------------------------------
// compute_explosion_range tests
// ---------------------------------------------------------------------------

void test_explosion_range_basic()
{
    // level * 4 = range, for moderate levels
    TEST_ASSERT_EQ(20, static_cast<int>(compute_explosion_range(5, 0)), "level 5 should give range 20");
    TEST_ASSERT_EQ(40, static_cast<int>(compute_explosion_range(10, 0)), "level 10 should give range 40");
}
REGISTER_TEST(test_explosion_range_basic);

void test_explosion_range_caps_at_96()
{
    TEST_ASSERT_EQ(96, static_cast<int>(compute_explosion_range(30, 0)), "level 30 should cap at 96");
    TEST_ASSERT_EQ(96, static_cast<int>(compute_explosion_range(100, 0)), "level 100 should cap at 96");
}
REGISTER_TEST(test_explosion_range_caps_at_96);

void test_explosion_range_minimum_16()
{
    TEST_ASSERT_EQ(16, static_cast<int>(compute_explosion_range(1, 0)), "level 1 should give minimum range 16");
    TEST_ASSERT_EQ(16, static_cast<int>(compute_explosion_range(0, 0)), "level 0 should give minimum range 16");
}
REGISTER_TEST(test_explosion_range_minimum_16);

void test_explosion_range_skip_exit_magical()
{
    // skip_exit > 0 forces range to 0, then clamped to min 16
    TEST_ASSERT_EQ(16, static_cast<int>(compute_explosion_range(50, 1)), "skip_exit=1 should give minimum range 16");
    TEST_ASSERT_EQ(16, static_cast<int>(compute_explosion_range(1, 1)), "skip_exit=1 at low level should give range 16");
}
REGISTER_TEST(test_explosion_range_skip_exit_magical);

// ---------------------------------------------------------------------------
// compute_hp_color tests
// ---------------------------------------------------------------------------

void test_hp_color_low()
{
    // hp * 3 < maxhp => LOW_HP_COLOR (42)
    TEST_ASSERT_EQ(42, static_cast<int>(compute_hp_color(10.0f, 100.0f)), "low hp (10/100) should return LOW_HP_COLOR");
    TEST_ASSERT_EQ(42, static_cast<int>(compute_hp_color(1.0f, 100.0f)), "very low hp (1/100) should return LOW_HP_COLOR");
}
REGISTER_TEST(test_hp_color_low);

void test_hp_color_mid()
{
    // hp * 3 >= maxhp but hp * 3/2 < maxhp => MID_HP_COLOR-3 (234)
    TEST_ASSERT_EQ(237 - 3, static_cast<int>(compute_hp_color(40.0f, 100.0f)), "mid hp (40/100) should return MID_HP_COLOR-3");
}
REGISTER_TEST(test_hp_color_mid);

void test_hp_color_high_not_full()
{
    // hp * 3/2 >= maxhp but hp < maxhp => MAX_HP_COLOR+4 (60)
    TEST_ASSERT_EQ(56 + 4, static_cast<int>(compute_hp_color(90.0f, 100.0f)), "high hp (90/100) should return MAX_HP_COLOR+4");
}
REGISTER_TEST(test_hp_color_high_not_full);

void test_hp_color_full()
{
    // hp == maxhp => HIGH_HP_COLOR+2 (63)
    TEST_ASSERT_EQ(61 + 2, static_cast<int>(compute_hp_color(100.0f, 100.0f)), "full hp should return HIGH_HP_COLOR+2");
}
REGISTER_TEST(test_hp_color_full);

void test_hp_color_over_max()
{
    // hp > maxhp => ORANGE_START (224)
    TEST_ASSERT_EQ(224, static_cast<int>(compute_hp_color(150.0f, 100.0f)), "over-max hp should return ORANGE_START");
}
REGISTER_TEST(test_hp_color_over_max);

// ---------------------------------------------------------------------------
// compute_mp_color tests
// ---------------------------------------------------------------------------

void test_mp_color_low()
{
    TEST_ASSERT_EQ(42, static_cast<int>(compute_mp_color(10.0f, 100.0f)), "low mp should return LOW_MP_COLOR");
}
REGISTER_TEST(test_mp_color_low);

void test_mp_color_mid()
{
    TEST_ASSERT_EQ(108, static_cast<int>(compute_mp_color(40.0f, 100.0f)), "mid mp should return MID_MP_COLOR");
}
REGISTER_TEST(test_mp_color_mid);

void test_mp_color_high_not_full()
{
    TEST_ASSERT_EQ(64, static_cast<int>(compute_mp_color(90.0f, 100.0f)), "high mp (not full) should return MAX_MP_COLOR");
}
REGISTER_TEST(test_mp_color_high_not_full);

void test_mp_color_full()
{
    TEST_ASSERT_EQ(72 + 3, static_cast<int>(compute_mp_color(100.0f, 100.0f)), "full mp should return HIGH_MP_COLOR+3");
}
REGISTER_TEST(test_mp_color_full);

void test_mp_color_over_max()
{
    TEST_ASSERT_EQ(208, static_cast<int>(compute_mp_color(150.0f, 100.0f)), "over-max mp should return WATER_START");
}
REGISTER_TEST(test_mp_color_over_max);
