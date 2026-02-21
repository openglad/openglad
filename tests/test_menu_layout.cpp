#include <openglad/input/button.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"

// Button arrays and their sizes defined in picker.cpp
extern button main_options_buttons[];
extern button control_options_buttons[];

// Counts derived from the arrays (we know exact sizes from the source)
static constexpr int NUM_MAIN_OPTIONS = 16;
static constexpr int NUM_CONTROL_OPTIONS = 10;

// Screen dimensions for the game
static constexpr int SCREEN_W = 320;
static constexpr int SCREEN_H = 200;

static bool buttons_overlap(const button& a, const button& b)
{
    if (a.hidden || b.hidden)
        return false;
    const int ax1 = a.x, ay1 = a.y, ax2 = a.x + a.sizex, ay2 = a.y + a.sizey;
    const int bx1 = b.x, by1 = b.y, bx2 = b.x + b.sizex, by2 = b.y + b.sizey;
    return ax1 < bx2 && ax2 > bx1 && ay1 < by2 && ay2 > by1;
}

static bool button_in_bounds(const button& b)
{
    if (b.hidden)
        return true;
    return b.x >= 0 && b.y >= 0
        && b.x + b.sizex <= SCREEN_W
        && b.y + b.sizey <= SCREEN_H;
}

static void check_no_overlaps(button* buttons, int count, const char* menu_name)
{
    for (int i = 0; i < count; ++i)
    {
        for (int j = i + 1; j < count; ++j)
        {
            if (buttons_overlap(buttons[i], buttons[j]))
            {
                fprintf(stderr, "  OVERLAP in %s: [%d] '%s' (%d,%d %dx%d) vs [%d] '%s' (%d,%d %dx%d)\n",
                    menu_name,
                    i, buttons[i].id.c_str(), buttons[i].x, buttons[i].y, buttons[i].sizex, buttons[i].sizey,
                    j, buttons[j].id.c_str(), buttons[j].x, buttons[j].y, buttons[j].sizex, buttons[j].sizey);
                TEST_ASSERT(false, "buttons overlap in menu layout");
            }
        }
    }
}

static void check_bounds(button* buttons, int count, const char* menu_name)
{
    for (int i = 0; i < count; ++i)
    {
        if (!button_in_bounds(buttons[i]))
        {
            fprintf(stderr, "  OUT OF BOUNDS in %s: [%d] '%s' (%d,%d %dx%d)\n",
                menu_name,
                i, buttons[i].id.c_str(), buttons[i].x, buttons[i].y, buttons[i].sizex, buttons[i].sizey);
            TEST_ASSERT(false, "button out of screen bounds");
        }
    }
}

static void check_nav_in_range(button* buttons, int count, const char* menu_name)
{
    for (int i = 0; i < count; ++i)
    {
        const auto& n = buttons[i].nav;
        if (n.up >= 0)
        {
            if (n.up >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.up=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.up, count);
                TEST_ASSERT(false, "nav.up out of range");
            }
        }
        if (n.down >= 0)
        {
            if (n.down >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.down=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.down, count);
                TEST_ASSERT(false, "nav.down out of range");
            }
        }
        if (n.left >= 0)
        {
            if (n.left >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.left=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.left, count);
                TEST_ASSERT(false, "nav.left out of range");
            }
        }
        if (n.right >= 0)
        {
            if (n.right >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.right=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.right, count);
                TEST_ASSERT(false, "nav.right out of range");
            }
        }
    }
}

void test_main_options_buttons_no_overlap()
{
    check_no_overlaps(main_options_buttons, NUM_MAIN_OPTIONS, "main_options");
    check_bounds(main_options_buttons, NUM_MAIN_OPTIONS, "main_options");
}
REGISTER_TEST(test_main_options_buttons_no_overlap);

void test_control_options_buttons_no_overlap()
{
    check_no_overlaps(control_options_buttons, NUM_CONTROL_OPTIONS, "control_options");
    check_bounds(control_options_buttons, NUM_CONTROL_OPTIONS, "control_options");
}
REGISTER_TEST(test_control_options_buttons_no_overlap);

void test_main_options_nav_indices_in_range()
{
    check_nav_in_range(main_options_buttons, NUM_MAIN_OPTIONS, "main_options");
}
REGISTER_TEST(test_main_options_nav_indices_in_range);

void test_control_options_nav_indices_in_range()
{
    check_nav_in_range(control_options_buttons, NUM_CONTROL_OPTIONS, "control_options");
}
REGISTER_TEST(test_control_options_nav_indices_in_range);
