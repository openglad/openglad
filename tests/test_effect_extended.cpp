#include <openglad/entities/effect.h>
#include <openglad/data/gloader.h>
#include "test_framework.h"

extern screen* myscreen;

// hits() is a free function
short hits(short x, short y, short xsize, short ysize,
           short x2, short y2, short xsize2, short ysize2);

// ---------------------------------------------------------------------------
// hits() AABB collision tests
// ---------------------------------------------------------------------------

void test_hits_overlap()
{
    short result = hits(10, 10, 20, 20, 15, 15, 20, 20);
    TEST_ASSERT_EQ(1, (int)result, "overlapping boxes should hit");
}
REGISTER_TEST(test_hits_overlap);

void test_hits_no_overlap_right()
{
    short result = hits(10, 10, 20, 20, 50, 10, 20, 20);
    TEST_ASSERT_EQ(0, (int)result, "boxes far apart should not hit");
}
REGISTER_TEST(test_hits_no_overlap_right);

void test_hits_no_overlap_below()
{
    short result = hits(10, 10, 20, 20, 10, 50, 20, 20);
    TEST_ASSERT_EQ(0, (int)result, "box below should not hit");
}
REGISTER_TEST(test_hits_no_overlap_below);

void test_hits_no_overlap_left()
{
    short result = hits(50, 10, 20, 20, 10, 10, 20, 20);
    TEST_ASSERT_EQ(0, (int)result, "box to left should not hit");
}
REGISTER_TEST(test_hits_no_overlap_left);

void test_hits_no_overlap_above()
{
    short result = hits(10, 50, 20, 20, 10, 10, 20, 20);
    TEST_ASSERT_EQ(0, (int)result, "box above should not hit");
}
REGISTER_TEST(test_hits_no_overlap_above);

void test_hits_exact_touching()
{
    // Edge-touching: x1 right edge = x2 left edge
    short result = hits(10, 10, 20, 20, 30, 10, 20, 20);
    TEST_ASSERT_EQ(1, (int)result, "touching edges should hit");
}
REGISTER_TEST(test_hits_exact_touching);

void test_hits_contained()
{
    short result = hits(10, 10, 40, 40, 15, 15, 10, 10);
    TEST_ASSERT_EQ(1, (int)result, "contained box should hit");
}
REGISTER_TEST(test_hits_contained);

void test_hits_same_box()
{
    short result = hits(10, 10, 20, 20, 10, 10, 20, 20);
    TEST_ASSERT_EQ(1, (int)result, "same box should hit");
}
REGISTER_TEST(test_hits_same_box);

void test_hits_zero_size()
{
    short result = hits(10, 10, 0, 0, 10, 10, 0, 0);
    TEST_ASSERT_EQ(1, (int)result, "zero-size at same point should hit");
}
REGISTER_TEST(test_hits_zero_size);

// ---------------------------------------------------------------------------
// orbit_offset extended tests
// ---------------------------------------------------------------------------

void test_orbit_offset_all_directions()
{
    for (int i = 0; i < 16; i++) {
        float xd = 0, yd = 0;
        orbit_offset(i, xd, yd);
        // Each position should have some offset
        TEST_ASSERT(xd != 0 || yd != 0, "orbit offset should be non-zero for all positions");
    }
}
REGISTER_TEST(test_orbit_offset_all_directions);

void test_orbit_offset_symmetry()
{
    // North and South should have opposite y values
    float xn = 0, yn = 0, xs = 0, ys = 0;
    orbit_offset(0, xn, yn);
    orbit_offset(8, xs, ys);
    TEST_ASSERT(yn < 0, "north should have negative y");
    TEST_ASSERT(ys > 0, "south should have positive y");
    TEST_ASSERT_EQ(0, static_cast<int>(xn), "north x should be 0");
    TEST_ASSERT_EQ(0, static_cast<int>(xs), "south x should be 0");
}
REGISTER_TEST(test_orbit_offset_symmetry);

// ---------------------------------------------------------------------------
// compute_explosion_range extended tests
// ---------------------------------------------------------------------------

void test_compute_explosion_range_scaling()
{
    Sint32 r1 = compute_explosion_range(1, 0);
    Sint32 r5 = compute_explosion_range(5, 0);
    Sint32 r10 = compute_explosion_range(10, 0);
    TEST_ASSERT(r5 > r1, "range should increase with level");
    TEST_ASSERT(r10 > r5, "range should increase with level");
}
REGISTER_TEST(test_compute_explosion_range_scaling);

void test_compute_explosion_range_clamp()
{
    Sint32 r50 = compute_explosion_range(50, 0);
    Sint32 r100 = compute_explosion_range(100, 0);
    TEST_ASSERT_EQ(96, (int)r50, "level 50 should cap at 96");
    TEST_ASSERT_EQ(96, (int)r100, "level 100 should cap at 96");
}
REGISTER_TEST(test_compute_explosion_range_clamp);
