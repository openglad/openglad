#include <array>

#include <openglad/interface/render/pal32.h>
#include "test_framework.h"

void test_palette_set_and_query_reg()
{
    std::array<unsigned char, 768> pal{};
    // Keep values in the classic 0-63 VGA range that this game expects.
    for (size_t i = 0; i < pal.size(); i++)
        pal[i] = static_cast<unsigned char>(i % 64);

    TEST_ASSERT(set_palette(pal) == 1, "set_palette should succeed");

    int r = -1, g = -1, b = -1;
    query_palette_reg(10, &r, &g, &b);
    TEST_ASSERT_EQ(static_cast<int>(pal[30]), r, "palette reg R should match");
    TEST_ASSERT_EQ(static_cast<int>(pal[31]), g, "palette reg G should match");
    TEST_ASSERT_EQ(static_cast<int>(pal[32]), b, "palette reg B should match");

    set_palette_reg(10, 1, 2, 3);
    query_palette_reg(10, &r, &g, &b);
    TEST_ASSERT_EQ(1, r, "set_palette_reg should update R");
    TEST_ASSERT_EQ(2, g, "set_palette_reg should update G");
    TEST_ASSERT_EQ(3, b, "set_palette_reg should update B");
}
REGISTER_TEST(test_palette_set_and_query_reg);

void test_palette_adjust_clamps()
{
    std::array<unsigned char, 768> pal{};
    // A small known palette that will exercise both clamp directions.
    pal[0] = 0;
    pal[1] = 1;
    pal[2] = 63;

    // Lighten: 63 stays clamped, low values increase.
    adjust_palette(pal, 5);
    int r = -1, g = -1, b = -1;
    query_palette_reg(0, &r, &g, &b);
    TEST_ASSERT(r >= 5, "adjust_palette should lighten low channel values");
    TEST_ASSERT(g >= 6, "adjust_palette should lighten low channel values (g)");
    TEST_ASSERT_EQ(63, b, "adjust_palette should clamp at 63");

    // Darken: all channels clamp at 0.
    adjust_palette(pal, -10);
    query_palette_reg(0, &r, &g, &b);
    TEST_ASSERT_EQ(0, r, "adjust_palette should clamp at 0 (r)");
    TEST_ASSERT_EQ(0, g, "adjust_palette should clamp at 0 (g)");
    TEST_ASSERT_EQ(0, b, "adjust_palette should clamp at 0 (b)");
}
REGISTER_TEST(test_palette_adjust_clamps);

void test_palette_cycle_basic()
{
    std::array<unsigned char, 768> pal{};

    // Encode 4 palette entries (0..3) with distinct RGB triplets.
    // Entry i uses (i, i+1, i+2).
    for (int idx = 0; idx <= 3; idx++) {
        pal[idx * 3 + 0] = static_cast<unsigned char>(idx);
        pal[idx * 3 + 1] = static_cast<unsigned char>(idx + 1);
        pal[idx * 3 + 2] = static_cast<unsigned char>(idx + 2);
    }

    // Cycle entries 1..3 by shift=1. Entry 1 should take on old entry 2's color.
    cycle_palette(pal, 1, 3, 1);

    int r = -1, g = -1, b = -1;
    query_palette_reg(1, &r, &g, &b);
    TEST_ASSERT_EQ(2, r, "cycle_palette should rotate entry 1 from entry 2 (r)");
    TEST_ASSERT_EQ(3, g, "cycle_palette should rotate entry 1 from entry 2 (g)");
    TEST_ASSERT_EQ(4, b, "cycle_palette should rotate entry 1 from entry 2 (b)");
}
REGISTER_TEST(test_palette_cycle_basic);

