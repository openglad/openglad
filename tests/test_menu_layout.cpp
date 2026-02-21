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

namespace
{
struct PlayerControlSnapshotGuard
{
    int player;
    int old_mode;
    int old_four[NUM_KEYS];
    int old_eight[NUM_KEYS];

    explicit PlayerControlSnapshotGuard(int player_)
        : player(player_), old_mode(get_player_control_mode(player_))
    {
        for (int k = 0; k < NUM_KEYS; ++k)
        {
            old_four[k] = get_player_key_binding_for_mode(
                player, static_cast<int>(ControlDirectionMode::FourDirection), k);
            old_eight[k] = get_player_key_binding_for_mode(
                player, static_cast<int>(ControlDirectionMode::EightDirection), k);
        }
    }

    ~PlayerControlSnapshotGuard()
    {
        set_player_control_mode(player, static_cast<int>(ControlDirectionMode::FourDirection));
        for (int k = 0; k < NUM_KEYS; ++k)
            set_player_key_binding(player, k, old_four[k]);
        set_player_control_mode(player, static_cast<int>(ControlDirectionMode::EightDirection));
        for (int k = 0; k < NUM_KEYS; ++k)
            set_player_key_binding(player, k, old_eight[k]);
        set_player_control_mode(player, old_mode);
    }
};
} // namespace

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

void test_controls_summary_switches_between_four_and_eight_direction_formats()
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_w);
    set_player_key_binding(0, KEY_LEFT, SDLK_a);
    set_player_key_binding(0, KEY_DOWN, SDLK_s);
    set_player_key_binding(0, KEY_RIGHT, SDLK_d);
    set_player_key_binding(0, KEY_YELL, SDLK_q);
    set_player_key_binding(0, KEY_FIRE, SDLK_1);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_2);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_EQUALS);
    set_player_key_binding(0, KEY_SWITCH, SDLK_BACKQUOTE);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_F8);

    const std::string summary_four = build_player_control_summary(0);
    TEST_ASSERT(summary_four.find("D:WASD") != std::string::npos,
        "4-direction summary should include compact direction order");
    TEST_ASSERT(summary_four.find("Y:Q") != std::string::npos,
        "4-direction summary should include yell label");
    TEST_ASSERT(summary_four.find("F:1") != std::string::npos,
        "4-direction summary should include fire key");
    TEST_ASSERT(summary_four.find("S:2") != std::string::npos,
        "4-direction summary should include special key");
    TEST_ASSERT(summary_four.find("SS:=") != std::string::npos,
        "4-direction summary should include special switch key");
    TEST_ASSERT(summary_four.find("SW:`") != std::string::npos,
        "4-direction summary should display backtick character for switch key");
    TEST_ASSERT(summary_four.find("Sh:F8") != std::string::npos,
        "4-direction summary should include shifter key");
    TEST_ASSERT(summary_four.find("Dir:") == std::string::npos,
        "4-direction summary should not include diagonal keys");

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_e);
    set_player_key_binding(0, KEY_DOWN_RIGHT, SDLK_c);
    set_player_key_binding(0, KEY_DOWN_LEFT, SDLK_z);
    set_player_key_binding(0, KEY_UP_LEFT, SDLK_q);

    const std::string summary_eight = build_player_control_summary(0);
    TEST_ASSERT(summary_eight.find("D:WEDCXZAQ") != std::string::npos,
        "8-direction summary should include compact clockwise direction order");
    TEST_ASSERT(summary_eight.find("Y:") != std::string::npos,
        "8-direction summary should include yell label");
}
REGISTER_TEST(test_controls_summary_switches_between_four_and_eight_direction_formats);

void test_eight_direction_summary_clockwise_order()
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP, SDLK_w);
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_e);
    set_player_key_binding(0, KEY_RIGHT, SDLK_d);
    set_player_key_binding(0, KEY_DOWN_RIGHT, SDLK_c);
    set_player_key_binding(0, KEY_DOWN, SDLK_x);
    set_player_key_binding(0, KEY_DOWN_LEFT, SDLK_z);
    set_player_key_binding(0, KEY_LEFT, SDLK_a);
    set_player_key_binding(0, KEY_UP_LEFT, SDLK_q);
    set_player_key_binding(0, KEY_YELL, SDLK_s);
    set_player_key_binding(0, KEY_FIRE, SDLK_1);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_2);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_EQUALS);
    set_player_key_binding(0, KEY_SWITCH, SDLK_3);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_F8);

    const std::string summary = build_player_control_summary(0);
    TEST_ASSERT(summary.find("D:WEDCXZAQ") != std::string::npos,
        "8-direction summary should list keys clockwise from Up");
    TEST_ASSERT(summary.find("Y:S") != std::string::npos,
        "8-direction summary should include yell key");
    TEST_ASSERT(summary.find("F:1") != std::string::npos,
        "8-direction summary should include fire key");
    TEST_ASSERT(summary.find("S:2") != std::string::npos,
        "8-direction summary should include special key");
    TEST_ASSERT(summary.find("SS:=") != std::string::npos,
        "8-direction summary should include special switch key");
    TEST_ASSERT(summary.find("SW:3") != std::string::npos,
        "8-direction summary should include switch key");
    TEST_ASSERT(summary.find("Sh:F8") != std::string::npos,
        "8-direction summary should include shifter key");
}
REGISTER_TEST(test_eight_direction_summary_clockwise_order);

void test_controls_summary_remap_mode_uses_two_lines()
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_w);
    set_player_key_binding(0, KEY_LEFT, SDLK_a);
    set_player_key_binding(0, KEY_DOWN, SDLK_s);
    set_player_key_binding(0, KEY_RIGHT, SDLK_d);
    set_player_key_binding(0, KEY_YELL, SDLK_e);
    set_player_key_binding(0, KEY_FIRE, SDLK_LCTRL);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_LALT);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_TAB);
    set_player_key_binding(0, KEY_SWITCH, SDLK_BACKQUOTE);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_LSHIFT);

    const std::array<std::string, 2> remap_summary = build_player_control_summary_lines(0, true);
    TEST_ASSERT(remap_summary[0].find("Dir:W/A/S/D") != std::string::npos,
        "remap summary first line should contain directional keys");
    TEST_ASSERT(remap_summary[1].find("Y:E") != std::string::npos,
        "remap summary second line should contain action keys");
    TEST_ASSERT(remap_summary[1].find("SW:`") != std::string::npos,
        "remap summary second line should display backtick character");
}
REGISTER_TEST(test_controls_summary_remap_mode_uses_two_lines);
