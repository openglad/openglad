#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include <openglad/interface/ui/pause_menu.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

// Seat-card labels are derived from each local profile's live movement keys
// (design §2.3), so any test that pins one needs a known starting mapping —
// and must hand the whole hardware block back, since claiming a factory NAME
// also permutes the RESET identities.
struct FactoryMappingGuard
{
    InputHardwareState hardware = input_hardware_state();
    int active[4][NUM_KEYS]{};

    FactoryMappingGuard()
    {
        for (int player = 0; player < 4; ++player)
        {
            for (int key = 0; key < NUM_KEYS; ++key)
            {
                active[player][key] =
                    og::runtime::current_session->player_keys_[player][key];
            }
            clear_player_joystick(player);
            og::input::assign_mapping_to_player(
                player, og::input::factory_mapping(player));
        }
    }

    ~FactoryMappingGuard()
    {
        input_hardware_state() = hardware;
        for (int player = 0; player < 4; ++player)
        {
            for (int key = 0; key < NUM_KEYS; ++key)
            {
                og::runtime::current_session->player_keys_[player][key] =
                    active[player][key];
            }
        }
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

static bool rects_overlap(
    int ax,
    int ay,
    int aw,
    int ah,
    int bx,
    int by,
    int bw,
    int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
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
                ASSERT_TRUE(false) << "buttons overlap in menu layout";
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
            ASSERT_TRUE(false) << "button out of screen bounds";
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
                ASSERT_TRUE(false) << "nav.up out of range";
            }
        }
        if (n.down >= 0)
        {
            if (n.down >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.down=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.down, count);
                ASSERT_TRUE(false) << "nav.down out of range";
            }
        }
        if (n.left >= 0)
        {
            if (n.left >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.left=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.left, count);
                ASSERT_TRUE(false) << "nav.left out of range";
            }
        }
        if (n.right >= 0)
        {
            if (n.right >= count)
            {
                fprintf(stderr, "  NAV ERROR in %s: [%d] '%s' nav.right=%d out of range [0,%d)\n",
                    menu_name, i, buttons[i].id.c_str(), n.right, count);
                ASSERT_TRUE(false) << "nav.right out of range";
            }
        }
    }
}

namespace
{
void check_nav_closed_and_reachable(button* buttons, int count,
                                    int start_index, const char* variant);
}

TEST(MenuLayout, main_options_buttons_no_overlap)
{
    button* buttons = picker_main_options_buttons();
    const int count = picker_main_options_button_count();
    check_no_overlaps(buttons, count, "main_options");
    check_bounds(buttons, count, "main_options");
}

TEST(MenuLayout, display_settings_buttons_no_overlap)
{
    button* buttons = picker_display_settings_buttons();
    const int count = picker_display_settings_button_count();
    check_no_overlaps(buttons, count, "display_settings");
    check_bounds(buttons, count, "display_settings");
}

TEST(MenuLayout, mainmenu_buttons_no_overlap)
{
    button* buttons = picker_mainmenu_buttons();
    const int count = picker_mainmenu_button_count();
    check_no_overlaps(buttons, count, "mainmenu");
    check_bounds(buttons, count, "mainmenu");
    check_nav_in_range(buttons, count, "mainmenu");
    ASSERT_GE(count, 9);

    // The primary action group retains its classic 4px vertical gutter.
    constexpr int kWithinGroupGutter = 4;
    EXPECT_EQ(kWithinGroupGutter,
              buttons[1].y - (buttons[0].y + buttons[0].sizey));

    // LEVEL EDITOR completes the primary-action group at the same 4px gap.
    EXPECT_EQ("level_edit", buttons[2].id);
    EXPECT_EQ(kWithinGroupGutter,
              buttons[2].y - (buttons[1].y + buttons[1].sizey));

    // SETTINGS: the GAME | CLOUD 68px pair leads the group directly under
    // the grey SETTINGS heading (which owns the y=119..134 band), with
    // full-width DIFFICULTY on the row below — narrow-pair-over-full-width,
    // mirrored by the HELP | QUIT footer.
    EXPECT_EQ("difficulty", buttons[3].id);
    EXPECT_EQ("options", buttons[4].id);
    EXPECT_EQ(buttons[2].x, buttons[4].x);
    EXPECT_EQ(68, buttons[4].sizex);
    EXPECT_EQ("GAME", buttons[4].label);
    EXPECT_EQ(buttons[4].x, buttons[3].x);
    EXPECT_EQ(140, buttons[3].sizex);
    EXPECT_EQ(kWithinGroupGutter,
              buttons[3].y - (buttons[4].y + buttons[4].sizey));

    // Tightening within groups does not collapse the category breaks.
    EXPECT_EQ(17, buttons[4].y - (buttons[2].y + buttons[2].sizey));

    // HELP and QUIT are a stable, aligned footer pair.
    EXPECT_EQ("help", buttons[5].id);
    EXPECT_EQ("quit", buttons[6].id);
    EXPECT_EQ(buttons[5].y, buttons[6].y);
    EXPECT_EQ(buttons[5].sizex, buttons[6].sizex);
    EXPECT_EQ(4, buttons[6].x - (buttons[5].x + buttons[5].sizex));
    // Footer break measured from DIFFICULTY, the settings group's last row.
    EXPECT_EQ(9, buttons[5].y - (buttons[3].y + buttons[3].sizey));

    // #155: the CLOUD door shares the GAME row as an aligned 68px pair
    // (always visible — reachable with zero companies), first row of the
    // settings group. Both build variants share the geometry (the tables
    // differ only in the QUIT fork). No button may sit in the y=119..134
    // band — main_menu_draw_content paints the grey SETTINGS heading
    // there, over any button face.
    ASSERT_EQ(10, count);
    EXPECT_EQ("cloud", buttons[9].id);
    EXPECT_EQ(152, buttons[9].x);
    EXPECT_EQ(buttons[4].y, buttons[9].y);
    EXPECT_EQ(buttons[4].sizex, buttons[9].sizex);
    EXPECT_EQ(15, buttons[9].sizey);
    EXPECT_EQ(4, buttons[9].x - (buttons[4].x + buttons[4].sizex));
    EXPECT_EQ(2, buttons[9].nav.up);
    EXPECT_EQ(3, buttons[9].nav.down) << "cloud links down to DIFFICULTY";
    EXPECT_EQ(4, buttons[9].nav.left) << "cloud links left to GAME";
    EXPECT_EQ(9, buttons[4].nav.right) << "GAME links right to CLOUD";
    EXPECT_EQ(3, buttons[6].nav.up) << "QUIT links up to DIFFICULTY";
    EXPECT_EQ(4, buttons[2].nav.down) << "level_edit links down to GAME";
    EXPECT_EQ(4, buttons[3].nav.up) << "difficulty links up to GAME";
    // 5-char label within the 68px face's 11-char budget.
    EXPECT_EQ("CLOUD", buttons[9].label);
    EXPECT_LE(buttons[9].label.size() * 6,
              static_cast<std::size_t>(buttons[9].sizex));
}

// #155 CLOUD SAVE screen: geometry table, label budgets, the static nav
// cycle, and the Disabled shapes the installed state drives (the engine's
// inert-box grammar keeps every row visible, so one nav graph covers all
// shapes).
TEST(MenuLayout, cloud_save_screen_layout_states_and_nav)
{
    button* buttons = picker_cloud_save_buttons();
    const int count = picker_cloud_save_button_count();
    ASSERT_EQ(4, count);
    check_no_overlaps(buttons, count, "cloud_save");
    check_bounds(buttons, count, "cloud_save");
    check_nav_closed_and_reachable(buttons, count, 0, "cloud_save");

    const struct
    {
        const char* id;
        const char* label;
        int x, y, w, h;
        int up, down;
    } kExpected[] = {
        {"cloud_passphrase", "PASSPHRASE", 80, 60, 140, 15, 3, 1},
        {"cloud_upload", "UPLOAD", 80, 84, 140, 15, 0, 2},
        {"cloud_download", "DOWNLOAD", 80, 108, 140, 15, 1, 3},
        {"back", "BACK", 80, 150, 60, 15, 2, 0},
    };
    const Sint32 spec_row = button_action_id(ButtonAction::MenuSpecRow);
    for (int i = 0; i < count; ++i)
    {
        const auto& want = kExpected[i];
        EXPECT_EQ(want.id, buttons[i].id) << "cloud_save index " << i;
        EXPECT_EQ(want.label, buttons[i].label) << want.id;
        EXPECT_EQ(want.x, buttons[i].x) << want.id;
        EXPECT_EQ(want.y, buttons[i].y) << want.id;
        EXPECT_EQ(want.w, buttons[i].sizex) << want.id;
        EXPECT_EQ(want.h, buttons[i].sizey) << want.id;
        EXPECT_EQ(spec_row, buttons[i].myfun) << want.id;
        EXPECT_EQ(i, buttons[i].arg1)
            << want.id << " (MenuSpecRow arg == ordinal)";
        EXPECT_EQ(want.up, buttons[i].nav.up) << want.id;
        EXPECT_EQ(want.down, buttons[i].nav.down) << want.id;
        EXPECT_FALSE(buttons[i].hidden) << want.id;
        // Label budget: 6px/char on the face width.
        EXPECT_LE(buttons[i].label.size() * 6,
                  static_cast<std::size_t>(buttons[i].sizex)) << want.id;
    }
    EXPECT_EQ(KEYSTATE_ESCAPE, buttons[3].hotkey) << "cloud_save back";

    // Row-state shapes through the installed-state seam: UPLOAD needs
    // key+company, DOWNLOAD needs the key; the null state is all-enabled
    // (what a bare engine sweep sees).
    const og::ui::MenuScreenSpec& spec = og::ui::cloud_save_menu_screen_spec();
    ASSERT_NE(nullptr, spec.rows[1].state_override);
    ASSERT_NE(nullptr, spec.rows[2].state_override);
    ASSERT_EQ(nullptr, spec.rows[0].state_override);
    ASSERT_EQ(nullptr, spec.rows[3].state_override);
    og::ui::MenuLabelContext context;

    og::ui::CloudSaveScreenState state;
    const struct
    {
        bool key_set;
        bool company_present;
        og::ui::RowState upload;
        og::ui::RowState download;
    } kShapes[] = {
        {false, false, og::ui::RowState::Disabled, og::ui::RowState::Disabled},
        {true, false, og::ui::RowState::Disabled, og::ui::RowState::Visible},
        {true, true, og::ui::RowState::Visible, og::ui::RowState::Visible},
    };
    for (const auto& shape : kShapes)
    {
        state.key_set = shape.key_set;
        state.company_present = shape.company_present;
        og::ui::install_cloud_save_state_for_screen(&state);
        EXPECT_EQ(static_cast<int>(shape.upload),
                  static_cast<int>(spec.rows[1].state_override(context)))
            << "upload key=" << shape.key_set
            << " company=" << shape.company_present;
        EXPECT_EQ(static_cast<int>(shape.download),
                  static_cast<int>(spec.rows[2].state_override(context)))
            << "download key=" << shape.key_set;
    }
    og::ui::install_cloud_save_state_for_screen(nullptr);
    EXPECT_EQ(static_cast<int>(og::ui::RowState::Visible),
              static_cast<int>(spec.rows[1].state_override(context)))
        << "null state renders the all-enabled sweep shape";
    EXPECT_EQ(static_cast<int>(og::ui::RowState::Visible),
              static_cast<int>(spec.rows[2].state_override(context)));

    // Spec obligations pinned: no lobby poll (D14), spec-row dispatch only.
    EXPECT_FALSE(spec.polls_lobby);
    EXPECT_EQ(static_cast<int>(og::ui::RemoteStartScope::None),
              static_cast<int>(spec.remote_start));
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_STREQ("cloud_save", spec.name);
}

TEST(MenuLayout, createmenu_buttons_no_overlap)
{
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    check_no_overlaps(buttons, count, "createmenu");
    check_bounds(buttons, count, "createmenu");
    check_nav_in_range(buttons, count, "createmenu");
}

namespace
{
// Shared graph checks: every nav link in range and landing on a VISIBLE
// button (nav does not skip hidden buttons), and every visible button
// keyboard-reachable from `start_index` over the nav edges.
void check_nav_closed_and_reachable(button* buttons, int count,
                                    int start_index, const char* variant)
{
    check_nav_in_range(buttons, count, variant);

    for (int i = 0; i < count; ++i)
    {
        if (buttons[i].hidden)
            continue;
        for (const int target : {buttons[i].nav.up, buttons[i].nav.down,
                                 buttons[i].nav.left, buttons[i].nav.right})
        {
            if (target < 0)
                continue;
            EXPECT_FALSE(buttons[target].hidden)
                << variant << ": '" << buttons[i].id
                << "' links to hidden '" << buttons[target].id << "'";
        }
    }

    std::vector<bool> reached(static_cast<std::size_t>(count), false);
    std::vector<int> frontier{start_index};
    reached[static_cast<std::size_t>(start_index)] = true;
    while (!frontier.empty())
    {
        const int current = frontier.back();
        frontier.pop_back();
        for (const int target :
             {buttons[current].nav.up, buttons[current].nav.down,
              buttons[current].nav.left, buttons[current].nav.right})
        {
            if (target >= 0 && !reached[static_cast<std::size_t>(target)])
            {
                reached[static_cast<std::size_t>(target)] = true;
                frontier.push_back(target);
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        if (!buttons[i].hidden)
        {
            EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                << variant << ": '" << buttons[i].id
                << "' is not keyboard-reachable";
        }
    }
}
} // namespace

// §2.5 base camp (the reimagined team build) at the §9.14 round-4 grid
// with the §9.11 (G4) row-click-train shape: 8 deploy/team/row-body trios at
// 14px pitch from y=45 (padded grey roster panel (6,28)..(313,160)), the page
// cluster top-right at y=15 beside the relocated line B, and the bottom
// command strip BACK | HIRE | SCENARIO | NETWORK | GO at y=178. The TRAIN
// column is DELETED: the TEAM chip (61,y,10,10) cycles team, the row body
// (84,y,214,10) opens training, and ^ at x=303 moves a member up. Spec
// ordinals group by kind (dep
// 0-7, row body 8-15, team chip 16-23, pagers 24/25, scenario-line 26,
// strip 27-31, ready twin 32) so MenuSpecRow args decode positionally. The
// seat-assignment rail is appended at 33-40, followed by move-up controls
// 41-48, preserving every old ordinal. The layout is identical for classic
// and CTF campaigns.
TEST(MenuLayout, createmenu_basecamp_geometry_and_nav)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;

    struct ExpectedButton
    {
        const char* id;
        const char* label;
        int x, y, w, h;
        MenuNav nav;
        bool hidden;
        // §9.11: the row-body train zones are no_draw hit zones (the row
        // text is the affordance; the keyboard highlight still draws).
        bool no_draw = false;
    };
    static const ExpectedButton kExpected[] = {
        {"roster_dep_0", "", 23, 45, 14, 10, MenuNav{.down = 1, .right = 16}, false},
        {"roster_dep_1", "", 23, 59, 14, 10, MenuNav{.up = 0, .down = 2, .right = 17}, false},
        {"roster_dep_2", "", 23, 73, 14, 10, MenuNav{.up = 1, .down = 3, .right = 18}, false},
        {"roster_dep_3", "", 23, 87, 14, 10, MenuNav{.up = 2, .down = 4, .right = 19}, false},
        {"roster_dep_4", "", 23, 101, 14, 10, MenuNav{.up = 3, .down = 5, .right = 20}, false},
        {"roster_dep_5", "", 23, 115, 14, 10, MenuNav{.up = 4, .down = 6, .right = 21}, false},
        {"roster_dep_6", "", 23, 129, 14, 10, MenuNav{.up = 5, .down = 7, .right = 22}, false},
        {"roster_dep_7", "", 23, 143, 14, 10, MenuNav{.up = 6, .down = 27, .right = 23}, false},
        {"roster_row_0", "", 84, 45, 214, 10, MenuNav{.down = 9, .left = 16}, false, true},
        {"roster_row_1", "", 84, 59, 214, 10, MenuNav{.up = 8, .down = 10, .left = 17}, false, true},
        {"roster_row_2", "", 84, 73, 214, 10, MenuNav{.up = 9, .down = 11, .left = 18}, false, true},
        {"roster_row_3", "", 84, 87, 214, 10, MenuNav{.up = 10, .down = 12, .left = 19}, false, true},
        {"roster_row_4", "", 84, 101, 214, 10, MenuNav{.up = 11, .down = 13, .left = 20}, false, true},
        {"roster_row_5", "", 84, 115, 214, 10, MenuNav{.up = 12, .down = 14, .left = 21}, false, true},
        {"roster_row_6", "", 84, 129, 214, 10, MenuNav{.up = 13, .down = 15, .left = 22}, false, true},
        {"roster_row_7", "", 84, 143, 214, 10, MenuNav{.up = 14, .down = 31, .left = 23}, false, true},
        {"roster_team_0", "", 61, 45, 10, 10, MenuNav{.down = 17, .left = 0, .right = 8}, false, true},
        {"roster_team_1", "", 61, 59, 10, 10, MenuNav{.up = 16, .down = 18, .left = 1, .right = 9}, false, true},
        {"roster_team_2", "", 61, 73, 10, 10, MenuNav{.up = 17, .down = 19, .left = 2, .right = 10}, false, true},
        {"roster_team_3", "", 61, 87, 10, 10, MenuNav{.up = 18, .down = 20, .left = 3, .right = 11}, false, true},
        {"roster_team_4", "", 61, 101, 10, 10, MenuNav{.up = 19, .down = 21, .left = 4, .right = 12}, false, true},
        {"roster_team_5", "", 61, 115, 10, 10, MenuNav{.up = 20, .down = 22, .left = 5, .right = 13}, false, true},
        {"roster_team_6", "", 61, 129, 10, 10, MenuNav{.up = 21, .down = 23, .left = 6, .right = 14}, false, true},
        {"roster_team_7", "", 61, 143, 10, 10, MenuNav{.up = 22, .down = 31, .left = 7, .right = 15}, false, true},
        {"roster_page_prev", "<", 258, 15, 14, 10, MenuNav{.down = 8, .right = 25}, true},
        {"roster_page_next", ">", 298, 15, 14, 10, MenuNav{.down = 8, .left = 24}, true},
        {"scenario_line", "", 6, 14, 208, 12, MenuNav{.down = 29}, false, true},
        {"back", "BACK", 8, 178, 44, 18, MenuNav{.up = 7, .right = 28}, false},
        {"hire_troops", "HIRE", 58, 178, 50, 18, MenuNav{.up = 7, .left = 27, .right = 29}, false},
        {"scenario", "SCENARIO", 114, 178, 62, 18, MenuNav{.up = 26, .left = 28, .right = 30}, false},
        {"networking", "NETWORK", 182, 178, 56, 18, MenuNav{.up = 15, .left = 29, .right = 31}, false},
        {"go", "GO", 244, 178, 68, 18, MenuNav{.up = 15, .left = 30}, false},
        // §2.6: the READY twin shares GO's exact rect; statically hidden
        // (the rewire shows exactly one of the pair — GO for hosts, READY
        // for networked joiners).
        {"ready", "READY", 244, 178, 68, 18, MenuNav{.up = 15, .left = 30},
         true},
        {"seats", "SEATS", 8, 164, 34, 10,
         MenuNav{.down = 27, .right = 34}, false},
        {"seat_page_prev", "<", 44, 164, 8, 10,
         MenuNav{.left = 33, .right = 35}, true},
        {"seat_card_0", "", 54, 164, 57, 10,
         MenuNav{.left = 34, .right = 36}, false},
        {"seat_card_1", "", 112, 164, 57, 10,
         MenuNav{.left = 35, .right = 37}, false},
        {"seat_card_2", "", 170, 164, 57, 10,
         MenuNav{.left = 36, .right = 38}, false},
        {"seat_card_3", "", 228, 164, 57, 10,
         MenuNav{.left = 37, .right = 39}, false},
        {"seat_page_next", ">", 287, 164, 8, 10,
         MenuNav{.down = 31, .left = 38}, true},
        {"add_seat", "+", 298, 164, 14, 10,
         MenuNav{.down = 31, .left = 39}, false},
        {"roster_up_0", "^", 303, 45, 9, 10,
         MenuNav{.left = 8}, true},
        {"roster_up_1", "^", 303, 59, 9, 10,
         MenuNav{.left = 9}, true},
        {"roster_up_2", "^", 303, 73, 9, 10,
         MenuNav{.left = 10}, true},
        {"roster_up_3", "^", 303, 87, 9, 10,
         MenuNav{.left = 11}, true},
        {"roster_up_4", "^", 303, 101, 9, 10,
         MenuNav{.left = 12}, true},
        {"roster_up_5", "^", 303, 115, 9, 10,
         MenuNav{.left = 13}, true},
        {"roster_up_6", "^", 303, 129, 9, 10,
         MenuNav{.left = 14}, true},
        {"roster_up_7", "^", 303, 143, 9, 10,
         MenuNav{.left = 15}, true},
    };

    for (const char* campaign :
         {"gladiator", "modes"})
    {
        save.current_campaign = campaign;
        button* buttons = picker_createmenu_buttons();
        const int count = picker_createmenu_button_count();
        ASSERT_EQ(kCreateMenuButtonCount, count)
            << "base camp: 24 roster controls + 2 pagers + the SCEN line "
               "hit zone + 5 strip buttons + the hidden READY twin + "
               "8 seat-rail controls + 8 move-up controls";
        ASSERT_EQ(49, count);

        for (int i = 0; i < count; ++i)
        {
            const ExpectedButton& want = kExpected[i];
            const button& got = buttons[i];
            EXPECT_EQ(want.id, got.id) << campaign << " index " << i;
            EXPECT_EQ(want.label, got.label) << got.id;
            EXPECT_EQ(want.hidden, got.hidden) << got.id;
            EXPECT_EQ(want.no_draw, got.no_draw) << got.id;
            EXPECT_EQ(want.x, got.x) << got.id;
            EXPECT_EQ(want.y, got.y) << got.id;
            EXPECT_EQ(want.w, got.sizex) << got.id;
            EXPECT_EQ(want.h, got.sizey) << got.id;
            EXPECT_EQ(want.nav.up, got.nav.up) << got.id;
            EXPECT_EQ(want.nav.down, got.nav.down) << got.id;
            EXPECT_EQ(want.nav.left, got.nav.left) << got.id;
            EXPECT_EQ(want.nav.right, got.nav.right) << got.id;
            // The compact rail uses the full face; established beveled
            // controls retain their eight-pixel inset budget.
            const int label_budget =
                i < kBaseCampSeatsLabelIndex
                ? (got.sizex - 8) / 6
                : got.sizex / 6;
            EXPECT_LE(static_cast<int>(got.label.size()), label_budget)
                << got.id << " '" << got.label << "'";
        }

        // G13 drift pins: spec ordinals == picker_sdl_defs.h constants.
        EXPECT_EQ(kBaseCampRowBodyBase, 8);
        EXPECT_EQ(kBaseCampTeamChipBase, 16);
        EXPECT_EQ(kBaseCampPagePrevIndex, 24);
        EXPECT_EQ(kBaseCampPageNextIndex, 25);
        EXPECT_EQ(kBaseCampScenarioLineIndex, 26);
        EXPECT_EQ(kCreateMenuBackIndex, 27);
        EXPECT_EQ(kCreateMenuHireIndex, 28);
        EXPECT_EQ(kCreateMenuScenarioIndex, 29);
        EXPECT_EQ(kCreateMenuNetworkingIndex, 30);
        EXPECT_EQ(kCreateMenuGoIndex, 31);
        EXPECT_EQ(kCreateMenuReadyIndex, 32);
        EXPECT_EQ(kBaseCampSeatsLabelIndex, 33);
        EXPECT_EQ(kBaseCampSeatPagePrevIndex, 34);
        EXPECT_EQ(kBaseCampSeatCardBase, 35);
        EXPECT_EQ(kBaseCampSeatPageNextIndex, 39);
        EXPECT_EQ(kBaseCampAddSeatIndex, 40);
        EXPECT_EQ(kBaseCampMoveUpBase, 41);
        EXPECT_EQ(kCreateMenuButtonCount, 49);
        // §2.6 same-geometry pair: the two rects are IDENTICAL by design
        // (the mutually-exclusive-gate allowance the gate-lattice sweep
        // validates structurally).
        EXPECT_EQ(buttons[kCreateMenuGoIndex].x,
                  buttons[kCreateMenuReadyIndex].x);
        EXPECT_EQ(buttons[kCreateMenuGoIndex].y,
                  buttons[kCreateMenuReadyIndex].y);
        EXPECT_EQ(buttons[kCreateMenuGoIndex].sizex,
                  buttons[kCreateMenuReadyIndex].sizex);
        EXPECT_EQ(buttons[kCreateMenuGoIndex].sizey,
                  buttons[kCreateMenuReadyIndex].sizey);
        EXPECT_EQ(buttons[kCreateMenuBackIndex].x,
                  buttons[kBaseCampSeatsLabelIndex].x)
            << "SEATS and BACK share the base-camp left alignment line";
        // Each row's three actions have deliberate non-overlapping gutters:
        // deploy, TEAM color, then name/train.
        for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
        {
            EXPECT_LE(buttons[r].x + buttons[r].sizex,
                      buttons[kBaseCampTeamChipBase + r].x)
                << "deploy toggle overlaps the team chip on row " << r;
            EXPECT_LE(buttons[kBaseCampTeamChipBase + r].x +
                          buttons[kBaseCampTeamChipBase + r].sizex,
                      buttons[kBaseCampRowBodyBase + r].x)
                << "team chip overlaps the training zone on row " << r;
            EXPECT_LT(buttons[kBaseCampRowBodyBase + r].x +
                          buttons[kBaseCampRowBodyBase + r].sizex,
                      buttons[kBaseCampMoveUpBase + r].x)
                << "training zone needs a gutter before move-up on row " << r;
        }

        // The compact rail uses an interior keyboard-focus ring; its one- and
        // two-pixel horizontal gutters therefore remain clear between every
        // adjacent control, including the new far-right +.
        for (int left = kBaseCampSeatsLabelIndex;
             left < kBaseCampAddSeatIndex; ++left)
        {
            EXPECT_LT(buttons[left].x + buttons[left].sizex,
                      buttons[left + 1].x)
                << "seat rail needs a visible focus-safe gutter";
        }

        check_no_overlaps(buttons, count, "createmenu_basecamp");
        check_bounds(buttons, count, "createmenu_basecamp");
        check_nav_in_range(buttons, count, "createmenu_basecamp_static");
    }

    save.current_campaign = old_campaign;
    (void)picker_createmenu_buttons();
}

// The menus skill's layout-discipline rule applied to Base Camp: the exact
// table above happily pinned a crooked right rail for months (>, ^, + and GO
// each ended on a different column), so the grid is asserted here as
// RELATIONS — one right edge, one card pitch, one pager cluster.
TEST(MenuLayout, createmenu_basecamp_grid_relations)
{
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    ASSERT_EQ(kCreateMenuButtonCount, count);

    // (a) The right rail is ONE column: every control that reaches it ends on
    // GO's right edge, which is the panel's inner face edge (8..311).
    const button& go = buttons[kCreateMenuGoIndex];
    const int rail_right = go.x + go.sizex;
    EXPECT_EQ(312, rail_right) << "GO closes the panel's inner grey face";
    std::vector<int> rail{kBaseCampPageNextIndex, kBaseCampAddSeatIndex,
                          kCreateMenuReadyIndex};
    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
        rail.push_back(kBaseCampMoveUpBase + r);
    for (const int index : rail)
    {
        const button& b = buttons[index];
        EXPECT_EQ(rail_right, b.x + b.sizex)
            << b.id << " must co-terminate with GO on the right rail";
    }

    // (b) The seat cards are one uniform run: equal widths, equal pitch, one
    // baseline.
    for (int card = 0; card + 1 < kBaseCampSeatCardsPerPage; ++card)
    {
        const button& left = buttons[kBaseCampSeatCardBase + card];
        const button& right = buttons[kBaseCampSeatCardBase + card + 1];
        EXPECT_EQ(58, right.x - left.x) << "seat card pitch at card " << card;
        EXPECT_EQ(left.sizex, right.sizex)
            << "seat card width at card " << card;
        EXPECT_EQ(left.y, right.y) << "seat card baseline at card " << card;
    }

    // (c) The roster pagers are twins straddling the "p/N" strip: the space
    // between their faces is exactly the strip's reserved slot (3 glyphs plus
    // the 2px backing pad on each side) plus one 2px gutter per side.
    const button& prev = buttons[kBaseCampPagePrevIndex];
    const button& next = buttons[kBaseCampPageNextIndex];
    EXPECT_EQ(prev.sizex, next.sizex) << "roster pager faces are one size";
    EXPECT_EQ(prev.y, next.y) << "roster pagers share the header-B baseline";
    EXPECT_EQ((3 * 6 + 4) + 2 * 2, next.x - (prev.x + prev.sizex))
        << "pager cluster: strip slot + a 2px gutter on each side";
}

// The seat rail has its own four-card pager, independent of the eight-row
// company-roster pager. Pin every boundary shape through the production
// rewire: empty, partial/full single page, first overflowing page, exact
// two-page fill, and the lobby-wide 16-seat ceiling.
TEST(MenuLayout, createmenu_basecamp_seat_rail_paging_labels_and_nav)
{
#if defined(DISABLE_MULTIPLAYER) || defined(USE_TOUCH_INPUT)
    constexpr bool kAddSeatCompiledIn = false;
#else
    constexpr bool kAddSeatCompiledIn = true;
#endif
    FactoryMappingGuard mapping_guard;
    EXPECT_EQ(9, kBaseCampSeatCardLabelBudget)
        << "57px card face / 6px per character";

    struct LocalSeatRailLobby final : og::ui::IPickerLobbyClient
    {
        void initialize_from_save() override {}
        void shutdown() override {}
        void sync_from_save() override {}
        void sync_roster_from_save() override {}
        void sync_settings_from_save() override {}
        void poll_and_apply() override {}
        void set_player_mode(int) override {}
        bool request_start_game() override { return false; }
        [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
        build_game_start_config() const override { return std::nullopt; }
        [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
        consume_game_start_config() override { return std::nullopt; }
        [[nodiscard]] bool start_request_pending() const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool is_networked_session() const noexcept override
        {
            return true;
        }
        [[nodiscard]] bool host_controls_visible() const noexcept override
        {
            return host;
        }
        [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
            const override
        {
            return players;
        }
        [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
            const override
        {
            return local_indices;
        }

        bool host = true;
        std::vector<og::sim::LobbyPlayer> players;
        std::vector<std::uint8_t> local_indices;
    } lobby;
    struct LobbyRestore
    {
        og::ui::IPickerLobbyClient* saved =
            og::ui::active_picker_lobby_client();
        ~LobbyRestore()
        {
            og::ui::install_base_camp_state_for_screen(nullptr);
            og::ui::install_active_picker_lobby_client(saved);
        }
    } restore;
    og::ui::install_active_picker_lobby_client(&lobby);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);
    ASSERT_NE(nullptr, spec.on_spec_row);

    for (const int seat_count : {0, 1, 4, 5, 8, 16})
    {
        og::ui::BaseCampScreenState state;
        state.page = og::ui::PageModel::make(
            0, kBaseCampRosterRowsPerPage);
        for (int i = 0; i < seat_count; ++i)
        {
            state.seats.push_back(og::sim::LobbyPlayer{
                .player_index = static_cast<std::uint8_t>(i),
                .name = std::format("net-{}", i),
                .company = "Iron Keep",
                .team = static_cast<short>(i % SCORE_TEAM_COUNT),
                .character_slots = {},
                .ready = false,
                .is_host = i == 0,
            });
        }
        if (seat_count > 0)
        {
            state.local_seat_indices.push_back(0);
            if (seat_count > 1)
                state.local_seat_indices.push_back(
                    static_cast<std::uint8_t>(seat_count - 1));
        }
        state.seat_page = og::ui::PageModel::make(
            seat_count, kBaseCampSeatCardsPerPage);
        lobby.players = state.seats;
        lobby.local_indices = state.local_seat_indices;
        og::ui::install_base_camp_state_for_screen(&state);

        if (seat_count > kBaseCampSeatCardsPerPage)
        {
            EXPECT_EQ(MENU_OK,
                      spec.on_spec_row(kBaseCampSeatPageNextIndex, &state));
            EXPECT_EQ(1, state.seat_page.page);
            EXPECT_EQ(MENU_OK,
                      spec.on_spec_row(kBaseCampSeatPagePrevIndex, &state));
            EXPECT_EQ(0, state.seat_page.page);
        }

        for (int page = 0; page < state.seat_page.page_count(); ++page)
        {
            state.seat_page.page = page;
            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = kBaseCampSeatsLabelIndex;
            spec.nav.rewire(buttons, count, highlighted);

            const std::string variant = std::format(
                "basecamp seats={} page={}", seat_count, page);
            const bool paged =
                seat_count > kBaseCampSeatCardsPerPage;
            EXPECT_EQ(!paged,
                      buttons[kBaseCampSeatPagePrevIndex].hidden)
                << variant;
            EXPECT_EQ(!paged,
                      buttons[kBaseCampSeatPageNextIndex].hidden)
                << variant;
            EXPECT_EQ(!kAddSeatCompiledIn,
                      buttons[kBaseCampAddSeatIndex].hidden)
                << variant;
            ASSERT_NE(nullptr,
                      spec.rows[kBaseCampAddSeatIndex].state_override);
            const og::ui::RowState add_state =
                spec.rows[kBaseCampAddSeatIndex].state_override(
                    og::ui::MenuLabelContext{});
            if (!kAddSeatCompiledIn)
            {
                EXPECT_EQ(og::ui::RowState::Hidden, add_state)
                    << variant;
            }
            else
            {
                EXPECT_EQ(seat_count == og::sim::kMaxGlobalPlayers
                              ? og::ui::RowState::Disabled
                              : og::ui::RowState::Visible,
                          add_state)
                    << variant;
            }

            const int first =
                page * kBaseCampSeatCardsPerPage;
            const int visible =
                std::min(kBaseCampSeatCardsPerPage,
                         seat_count - first);
            std::vector<int> rail{kBaseCampSeatsLabelIndex};
            if (paged)
                rail.push_back(kBaseCampSeatPagePrevIndex);
            for (int card = 0; card < kBaseCampSeatCardsPerPage; ++card)
            {
                const button& card_button =
                    buttons[kBaseCampSeatCardBase + card];
                EXPECT_EQ(card >= visible, card_button.hidden)
                    << variant << " card " << card;
                if (card >= visible)
                    continue;

                const int player_index = first + card;
                // Local seat zero is this machine's first controller
                // profile; the trailing seat is its second (the fixture
                // hands the lobby exactly those two local indices).
                const bool local =
                    player_index == 0 ||
                    (seat_count > 1 && player_index == seat_count - 1);
                const char* const owner =
                    !local ? "IRO"
                           : (player_index == 0 ? "WASD"
                                                : og::input::kArrowGlyphs);
                // Design §2.3: a local card names its INPUT mapping. The
                // 57px face is exactly nine characters INCLUDING the
                // load-bearing trailing pad, so a two-digit global P#
                // shortens the name rather than overflowing the bevel.
                std::string expected =
                    std::format("P{} {} ", player_index + 1, owner);
                if (expected.size() >
                    static_cast<std::size_t>(kBaseCampSeatCardLabelBudget))
                {
                    expected = std::format(
                        "P{} {} ", player_index + 1,
                        std::string(owner).substr(
                            0, std::strlen(owner) -
                                   (expected.size() -
                                    static_cast<std::size_t>(
                                        kBaseCampSeatCardLabelBudget))));
                }
                EXPECT_EQ(expected, card_button.label)
                    << variant << " card " << card;
                EXPECT_LE(card_button.label.size(),
                          static_cast<std::size_t>(
                              kBaseCampSeatCardLabelBudget))
                    << variant << " card " << card << " '"
                    << card_button.label << "'";
                EXPECT_EQ(' ', card_button.label.back())
                    << "the team-chip clearance pad is load-bearing";
                rail.push_back(kBaseCampSeatCardBase + card);
            }
            if (paged)
                rail.push_back(kBaseCampSeatPageNextIndex);
            if (kAddSeatCompiledIn)
                rail.push_back(kBaseCampAddSeatIndex);

            for (std::size_t i = 0; i < rail.size(); ++i)
            {
                const button& control = buttons[rail[i]];
                EXPECT_EQ(i > 0 ? rail[i - 1] : -1, control.nav.left)
                    << variant << " " << control.id;
                EXPECT_EQ(i + 1 < rail.size() ? rail[i + 1] : -1,
                          control.nav.right)
                    << variant << " " << control.id;
            }

            const auto visible_card_or_label = [visible](int card) {
                return card < visible
                    ? kBaseCampSeatCardBase + card
                    : kBaseCampSeatsLabelIndex;
            };
            EXPECT_EQ(visible_card_or_label(0),
                      buttons[kCreateMenuHireIndex].nav.up)
                << variant;
            EXPECT_EQ(visible_card_or_label(1),
                      buttons[kCreateMenuScenarioIndex].nav.up)
                << variant;
            EXPECT_EQ(visible_card_or_label(2),
                      buttons[kCreateMenuNetworkingIndex].nav.up)
                << variant;
            EXPECT_EQ(kAddSeatCompiledIn
                          ? kBaseCampAddSeatIndex
                          : (paged
                                 ? kBaseCampSeatPageNextIndex
                                 : visible_card_or_label(3)),
                      buttons[kCreateMenuGoIndex].nav.up)
                << variant;

            check_no_overlaps(buttons, count, variant.c_str());
            check_bounds(buttons, count, variant.c_str());
            check_nav_closed_and_reachable(
                buttons, count, kCreateMenuBackIndex, variant.c_str());

            // The same rail page must close over READY for a joiner with an
            // active seat: GO hides, the fourth card, next arrow, and + point
            // down to READY, and READY points back up to the far-right +.
            // A zero-seat spectator has no READY target; + remains the route
            // back down to NETWORK until it activates a seat.
            lobby.host = false;
            buttons = picker_createmenu_buttons();
            highlighted = kBaseCampSeatsLabelIndex;
            spec.nav.rewire(buttons, count, highlighted);
            EXPECT_TRUE(buttons[kCreateMenuGoIndex].hidden) << variant;
            EXPECT_EQ(seat_count == 0,
                      buttons[kCreateMenuReadyIndex].hidden)
                << variant;
            EXPECT_EQ(seat_count > 0 ? kCreateMenuReadyIndex : -1,
                      buttons[kCreateMenuNetworkingIndex].nav.right)
                << variant;
            if (seat_count > 0 &&
                visible == kBaseCampSeatCardsPerPage)
            {
                EXPECT_EQ(kCreateMenuReadyIndex,
                          buttons[kBaseCampSeatCardBase + 3].nav.down)
                    << variant;
            }
            if (seat_count > 0 && paged)
            {
                EXPECT_EQ(kCreateMenuReadyIndex,
                          buttons[kBaseCampSeatPageNextIndex].nav.down)
                    << variant;
            }
            if (kAddSeatCompiledIn)
            {
                EXPECT_EQ(seat_count > 0
                              ? kCreateMenuReadyIndex
                              : kCreateMenuNetworkingIndex,
                          buttons[kBaseCampAddSeatIndex].nav.down)
                    << variant;
            }
            if (seat_count > 0)
            {
                EXPECT_EQ(kAddSeatCompiledIn
                              ? kBaseCampAddSeatIndex
                              : (paged
                                     ? kBaseCampSeatPageNextIndex
                                     : visible_card_or_label(3)),
                          buttons[kCreateMenuReadyIndex].nav.up)
                    << variant;
            }
            const std::string joiner_variant = variant + " joiner";
            check_nav_closed_and_reachable(
                buttons, count, kCreateMenuBackIndex,
                joiner_variant.c_str());
            lobby.host = true;
        }
    }

    // A network peer may remain connected after removing its final local
    // seat while one to four remote seats still fill the rail. Hosts retain
    // GO; guests have neither GO nor READY until + activates a seat. Exercise
    // every single-page width so card four can never fall through to a hidden
    // GO/READY twin.
    for (const int remote_count : {1, 2, 3, 4})
    {
        og::ui::BaseCampScreenState spectator_state;
        spectator_state.page = og::ui::PageModel::make(
            0, kBaseCampRosterRowsPerPage);
        for (int i = 0; i < remote_count; ++i)
        {
            spectator_state.seats.push_back(og::sim::LobbyPlayer{
                .player_index = static_cast<std::uint8_t>(i),
                .name = std::format("remote-{}", i),
                .company = "Iron Keep",
                .team = static_cast<short>(i % SCORE_TEAM_COUNT),
                .character_slots = {},
                .ready = false,
                .is_host = i == 0,
            });
        }
        spectator_state.seat_page = og::ui::PageModel::make(
            remote_count, kBaseCampSeatCardsPerPage);
        lobby.players = spectator_state.seats;
        lobby.local_indices.clear();
        og::ui::install_base_camp_state_for_screen(&spectator_state);

        for (const bool host : {false, true})
        {
            lobby.host = host;
            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = kBaseCampSeatsLabelIndex;
            spec.nav.rewire(buttons, count, highlighted);
            const std::string variant = std::format(
                "zero-seat {} remote-cards={}",
                host ? "host" : "guest", remote_count);

            EXPECT_EQ(!host, buttons[kCreateMenuGoIndex].hidden)
                << variant;
            EXPECT_TRUE(buttons[kCreateMenuReadyIndex].hidden)
                << variant;
            constexpr std::array<int, kBaseCampSeatCardsPerPage>
                kCardStripTargets{
                    kCreateMenuHireIndex,
                    kCreateMenuScenarioIndex,
                    kCreateMenuNetworkingIndex,
                    kCreateMenuNetworkingIndex,
                };
            for (int card = 0; card < remote_count; ++card)
            {
                const int expected_down =
                    card == kBaseCampSeatCardsPerPage - 1 && host
                    ? kCreateMenuGoIndex
                    : kCardStripTargets[static_cast<std::size_t>(card)];
                EXPECT_EQ(expected_down,
                          buttons[kBaseCampSeatCardBase + card].nav.down)
                    << variant << " card " << card;
            }
            if (kAddSeatCompiledIn)
            {
                EXPECT_EQ(host ? kCreateMenuGoIndex
                               : kCreateMenuNetworkingIndex,
                          buttons[kBaseCampAddSeatIndex].nav.down)
                    << variant;
            }
            check_nav_closed_and_reachable(
                buttons, count, kCreateMenuBackIndex, variant.c_str());
        }
    }

    // The row stays in the native rail but becomes visibly inert when this
    // machine already owns four active seats. Touch/no-MP builds hide the
    // same final slot altogether; both variants keep the authored cap rule.
    lobby.players.resize(static_cast<std::size_t>(MAX_PLAYERS));
    lobby.local_indices = {0, 1, 2, 3};
    const og::ui::RowState local_cap_state =
        spec.rows[kBaseCampAddSeatIndex].state_override(
            og::ui::MenuLabelContext{});
    EXPECT_EQ(kAddSeatCompiledIn ? og::ui::RowState::Disabled
                                : og::ui::RowState::Hidden,
              local_cap_state);
}

// Design §2.2: the seat editor's INPUT cycler needed a band of its own —
// the y=30 band is exactly full (three columns x=12/116/214, widths
// 98/92/90, 6px gutters, shared right edge 304), so the cycler rides the
// y=54 band. Pin both build variants: geometry, no overlaps,
// closed+reachable nav, and the face budget for the widest short mapping
// name.
TEST(MenuLayout, seat_settings_input_row_layout_and_nav)
{
    for (const og::ui::MenuScreenSpec* spec :
         {&og::ui::seat_settings_menu_screen_spec_mp(),
          &og::ui::seat_settings_menu_screen_spec_nomp()})
    {
        const bool mp = spec->row_count == kSeatSettingsButtonCountMP;
        const char* const variant =
            mp ? "seat_settings_mp" : "seat_settings_nomp";
        const int input_row =
            mp ? kSeatSettingsInputRowMP : kSeatSettingsInputRowNoMP;

        std::vector<button> rows;
        og::ui::materialize_menu_buttons(*spec, rows);
        ASSERT_EQ(spec->row_count, static_cast<int>(rows.size())) << variant;

        check_no_overlaps(rows.data(), spec->row_count, variant);
        check_bounds(rows.data(), spec->row_count, variant);
        check_nav_closed_and_reachable(rows.data(), spec->row_count,
                                       spec->default_highlight, variant);

        const button& input = rows[static_cast<std::size_t>(input_row)];
        const button& mode =
            rows[static_cast<std::size_t>(kSeatSettingsModeIndex)];
        const button& team =
            rows[static_cast<std::size_t>(kSeatSettingsTeamIndex)];
        EXPECT_EQ("seat_input", input.id) << variant;
        EXPECT_EQ(12, input.x) << variant;
        EXPECT_EQ(54, input.y) << variant;
        EXPECT_EQ(98, input.sizex) << variant;
        EXPECT_EQ(18, input.sizey) << variant;
        EXPECT_EQ(mode.x, input.x)
            << variant << ": the cycler shares the MODE column";
        EXPECT_LT(mode.y + mode.sizey, input.y)
            << variant << ": the cycler clears the whole y=30 band";
        EXPECT_LT(input.y + input.sizey, team.y)
            << variant << ": the cycler clears the bottom command band";

        // "INPUT: " + a five-character short name on a beveled 98px face.
        const std::string widest = std::format(
            "INPUT: {}",
            std::string(static_cast<std::size_t>(
                            og::input::kMappingShortNameMaxLength),
                        'W'));
        EXPECT_LE(static_cast<int>(widest.size()), (input.sizex - 8) / 6)
            << variant << " '" << widest << "'";
        EXPECT_LE(static_cast<int>(std::strlen(input.label.c_str())),
                  (input.sizex - 8) / 6)
            << variant << " '" << input.label << "'";

        // §7.1 unified player screen: ZOOM shares the y=54 band in the
        // middle column (x=116); the four HUD toggles stack right of the
        // binding panel (x=12..208) at x=214 on a 22px pitch, top-aligned
        // with the panel at y=78 and clearing the y=169 command band.
        const int zoom_row =
            mp ? kSeatSettingsZoomRowMP : kSeatSettingsZoomRowNoMP;
        const int radar_row =
            mp ? kSeatSettingsHudRadarRowMP : kSeatSettingsHudRadarRowNoMP;
        const button& zoom = rows[static_cast<std::size_t>(zoom_row)];
        EXPECT_EQ("seat_zoom", zoom.id) << variant;
        EXPECT_EQ(116, zoom.x) << variant;
        EXPECT_EQ(54, zoom.y) << variant;
        EXPECT_EQ(92, zoom.sizex) << variant;
        EXPECT_EQ(18, zoom.sizey) << variant;
        EXPECT_GT(zoom.x, input.x + input.sizex)
            << variant << ": ZOOM clears the INPUT face";
        const char* const hud_ids[4] = {"seat_hud_radar", "seat_hud_life",
                                        "seat_hud_foes", "seat_hud_score"};
        const char* const hud_labels[4] = {"RADAR: OFF", "HP: OFF",
                                           "FOES: OFF", "SCORE: OFF"};
        for (int k = 0; k < 4; ++k)
        {
            const button& hud =
                rows[static_cast<std::size_t>(radar_row + k)];
            EXPECT_EQ(hud_ids[k], hud.id) << variant;
            EXPECT_EQ(214, hud.x) << variant << " " << hud.id;
            EXPECT_EQ(78 + 22 * k, hud.y) << variant << " " << hud.id;
            EXPECT_EQ(90, hud.sizex) << variant << " " << hud.id;
            EXPECT_EQ(18, hud.sizey) << variant << " " << hud.id;
            EXPECT_GT(hud.x, 208)
                << variant << ": the stack clears the binding panel";
            // Both label states fit the 90px face budget.
            EXPECT_LE(static_cast<int>(std::strlen(hud_labels[k])),
                      (hud.sizex - 8) / 6)
                << variant << " '" << hud_labels[k] << "'";
            EXPECT_LE(static_cast<int>(std::strlen(hud.label.c_str())),
                      (hud.sizex - 8) / 6)
                << variant << " '" << hud.label << "'";
        }
        const button& score =
            rows[static_cast<std::size_t>(radar_row + 3)];
        EXPECT_LT(score.y + score.sizey, team.y)
            << variant << ": the stack clears the bottom command band";
        // Widest zoom label fits the 88px face.
        EXPECT_LE(static_cast<int>(std::strlen("ZOOM: GAME")),
                  (zoom.sizex - 8) / 6)
            << variant;

        // The two binding-panel columns stay inside their budgets with the
        // widest possible binding value (get_key_display_name_short caps at
        // 9 chars; joystick forms are at most 4): movement must clear the
        // ACTIONS column at x=104, actions must clear the panel edge x=208.
        const std::string widest_value(9, 'W');
        for (const char* const label :
             {"UP", "UP-R", "RIGHT", "DN-R", "DOWN", "DN-L", "LEFT", "UP-L"})
        {
            const std::string line = og::ui::format_binding_panel_line(
                label, widest_value, og::ui::kBindingPanelMovementChars);
            EXPECT_LE(20 + static_cast<int>(line.size()) * 6, 104)
                << variant << " movement '" << line << "'";
        }
        for (const char* const label :
             {"FIRE", "SPECIAL", "YELL", "SHIFTER", "LOOK UP", "CHAR SW",
              "SPEC SW"})
        {
            const std::string line = og::ui::format_binding_panel_line(
                label, widest_value, og::ui::kBindingPanelActionsChars);
            EXPECT_LE(104 + static_cast<int>(line.size()) * 6, 208)
                << variant << " actions '" << line << "'";
        }
    }
}

// §2.5 keyboard-nav BFS matrix (pattern b): the per-frame full-graph rewire
// over {page shapes: empty, partial, one full page, two pages, three pages
// — at the §9.14 8-row grid a 12-roster PAGES and cap-24 spans 3}
// x {host, joiner (GO hidden)}. Every visible button reachable, no link at
// a hidden one, empty roster seeds the highlight on HIRE.
TEST(MenuLayout, createmenu_basecamp_nav_matrix_keyboard_reachable)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Snapshot + rebuild the roster per shape.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;

    const og::ui::MenuScreenSpec* spec_ptr =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec_ptr);
    const og::ui::MenuScreenSpec& spec = *spec_ptr;
    ASSERT_NE(nullptr, spec.nav.rewire);

    // The local lobby client reports host_controls_visible()==true; the
    // joiner variant hides GO directly and closes the links into it (the
    // production rewire does the same from the lobby host flag).
    for (const int roster_size : {0, 5, 12, 15, 24})
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)].reset();
        for (int i = 0; i < roster_size; ++i)
        {
            save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[static_cast<std::size_t>(i)]->name = std::format("G{}", i);
        }
        save.team_size = static_cast<unsigned char>(roster_size);

        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        ASSERT_EQ(roster_size, static_cast<int>(state.slots.size()));
        og::ui::install_base_camp_state_for_screen(&state);

        const int page_count = state.page.page_count();
        for (int page = 0; page < page_count; ++page)
        {
            state.page.page = page;
            for (const bool host_visible : {true, false})
            {
                button* buttons = picker_createmenu_buttons();
                const int count = picker_createmenu_button_count();
                int highlighted = 0;
                spec.nav.rewire(buttons, count, highlighted);
                if (!host_visible)
                {
                    // Joiner shape: GO hides (the production rewire reads
                    // the lobby host flag; the matrix forces the variant).
                    buttons[kCreateMenuGoIndex].hidden = true;
                    buttons[kCreateMenuNetworkingIndex].nav.right = -1;
                    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
                    {
                        button& body = buttons[kBaseCampRowBodyBase + r];
                        if (body.nav.down == kCreateMenuGoIndex)
                            body.nav.down = kCreateMenuNetworkingIndex;
                        button& chip = buttons[kBaseCampTeamChipBase + r];
                        if (chip.nav.down == kCreateMenuGoIndex)
                            chip.nav.down = kCreateMenuNetworkingIndex;
                    }
                    buttons[kBaseCampPagePrevIndex].nav.down =
                        buttons[kBaseCampPagePrevIndex].nav.down ==
                                kCreateMenuGoIndex
                            ? kCreateMenuNetworkingIndex
                            : buttons[kBaseCampPagePrevIndex].nav.down;
                    buttons[kBaseCampPageNextIndex].nav.down =
                        buttons[kBaseCampPageNextIndex].nav.down ==
                                kCreateMenuGoIndex
                            ? kCreateMenuNetworkingIndex
                            : buttons[kBaseCampPageNextIndex].nav.down;
                    // The far-right + inherited GO as its native host down
                    // target. This matrix's synthetic joiner hides GO by
                    // hand, so close that final rail link as well.
                    buttons[kBaseCampAddSeatIndex].nav.down =
                        buttons[kBaseCampAddSeatIndex].nav.down ==
                                kCreateMenuGoIndex
                            ? kCreateMenuNetworkingIndex
                            : buttons[kBaseCampAddSeatIndex].nav.down;
                }
                const std::string variant = std::format(
                    "basecamp roster={} page={} {}", roster_size, page,
                    host_visible ? "host" : "joiner");
                check_no_overlaps(buttons, count, variant.c_str());
                check_bounds(buttons, count, variant.c_str());
                check_nav_closed_and_reachable(buttons, count,
                                               kCreateMenuBackIndex,
                                               variant.c_str());

                // Visible-row count matches the page window.
                const int expected_visible =
                    state.page.end_index() - state.page.first_index();
                int visible_rows = 0;
                for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
                {
                    if (!buttons[r].hidden)
                        ++visible_rows;
                    EXPECT_EQ(buttons[r].hidden,
                              buttons[kBaseCampRowBodyBase + r].hidden)
                        << variant << " row " << r;
                    EXPECT_EQ(buttons[r].hidden,
                              buttons[kBaseCampTeamChipBase + r].hidden)
                        << variant << " team chip row " << r;
                }
                EXPECT_EQ(expected_visible, visible_rows) << variant;
                // Pagers show exactly when the roster spans pages.
                EXPECT_EQ(roster_size <= kBaseCampRosterRowsPerPage,
                          buttons[kBaseCampPagePrevIndex].hidden)
                    << variant;
            }
        }

        // Empty roster: the rewire seeds the highlight on HIRE (§2.5).
        // §9.11: the spec default is roster_row_0 (Enter trains); hidden
        // when empty, so the seed must still land on HIRE.
        if (roster_size == 0)
        {
            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = spec.default_highlight;
            EXPECT_EQ(kBaseCampRowBodyBase, highlighted)
                << "§9.11: entry highlights row 0's body";
            spec.nav.rewire(buttons, count, highlighted);
            EXPECT_EQ(kCreateMenuHireIndex, highlighted)
                << "empty roster seeds the highlight on HIRE";
        }

        og::ui::install_base_camp_state_for_screen(nullptr);
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// §2.5 keyboard-nav BFS matrix, networked ownership axes (stage mp-columns):
// the production rewire over {own rows} x {foreign rows} x {host, joiner}
// x every page, driven through an installed networked lobby client (the
// rewire itself hides GO for joiners and shapes foreign rows into no_draw
// hit zones with hidden §9.11 row-body zones — the widened dep zone IS the
// foreign row click). Includes the [NET-R9] spectator machine shape (0 own
// rows, all-foreign roster) and the >24 display-slot defensive paging pin
// (two 20-slot machines => 5 pages at 8/page).
TEST(MenuLayout, createmenu_basecamp_nav_matrix_networked_ownership)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] = std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    const std::string old_save_name = save.save_name;
    save.save_name = "LAYOUT OWN BAND";

    struct NetworkedLayoutLobbyClient final : og::ui::IPickerLobbyClient
    {
        void initialize_from_save() override {}
        void shutdown() override {}
        void sync_from_save() override {}
        void sync_roster_from_save() override {}
        void sync_settings_from_save() override {}
        void poll_and_apply() override {}
        void set_player_mode(int) override {}
        bool request_start_game() override { return false; }
        [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
        build_game_start_config() const override { return std::nullopt; }
        [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
        consume_game_start_config() override { return std::nullopt; }
        [[nodiscard]] bool start_request_pending() const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool host_controls_visible() const noexcept override
        {
            return host;
        }
        [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
            const override
        {
            return players;
        }
        [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
            const override
        {
            return {7};
        }
        [[nodiscard]] bool is_networked_session() const noexcept override
        {
            return true;
        }

        bool host = false;
        std::vector<og::sim::LobbyPlayer> players;
    };
    NetworkedLayoutLobbyClient lobby;
    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    const og::ui::MenuScreenSpec* spec_ptr =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec_ptr);
    const og::ui::MenuScreenSpec& spec = *spec_ptr;
    ASSERT_NE(nullptr, spec.nav.rewire);

    // (own, foreign): mixed single page, spectator machine [NET-R9],
    // mixed multi-page, and the >24 display-slot defensive-paging shape.
    const std::pair<int, int> shapes[] = {{3, 4}, {0, 5}, {12, 20}, {20, 20}};
    for (const auto& [own_size, foreign_size] : shapes)
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)].reset();
        for (int i = 0; i < own_size; ++i)
        {
            save.team_list[static_cast<std::size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[static_cast<std::size_t>(i)]->name = std::format("OWN{}", i);
        }
        save.team_size = static_cast<unsigned char>(own_size);

        lobby.players.clear();
        og::sim::LobbyPlayer foreign;
        foreign.player_index = 3;
        foreign.name = "net-far";
        foreign.company = "FOREIGN LAYOUT BAND";
        if (foreign_size > 0)
        {
            for (int i = 0; i < foreign_size; ++i)
            {
                og::sim::LobbyCharacterSlot slot;
                slot.slot_index = static_cast<std::uint8_t>(i);
                slot.deployed = (i % 2) == 0;
                slot.character.name = std::format("FAR{}", i);
                slot.character.family = FAMILY_ELF;
                foreign.character_slots.push_back(std::move(slot));
            }
            lobby.players.push_back(foreign);
        }
        // The local machine's own replicated seat must be SKIPPED.
        og::sim::LobbyPlayer self;
        self.player_index = 7;
        self.name = "net-self";
        self.company = "LAYOUT OWN BAND";
        lobby.players.push_back(self);

        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        const int total = own_size + foreign_size;
        ASSERT_EQ(total, static_cast<int>(state.slots.size()));
        const int expected_pages =
            std::max(1, (total + kBaseCampRosterRowsPerPage - 1)
                            / kBaseCampRosterRowsPerPage);
        ASSERT_EQ(expected_pages, state.page.page_count())
            << "page window derives from the display size (>24 safe)";
        og::ui::install_base_camp_state_for_screen(&state);

        for (int page = 0; page < state.page.page_count(); ++page)
        {
            state.page.page = page;
            for (const bool host_visible : {true, false})
            {
                lobby.host = host_visible;
                button* buttons = picker_createmenu_buttons();
                const int count = picker_createmenu_button_count();
                int highlighted = 0;
                spec.nav.rewire(buttons, count, highlighted);

                const std::string variant = std::format(
                    "basecamp-mp own={} foreign={} page={} {}", own_size,
                    foreign_size, page,
                    host_visible ? "host" : "joiner");
                EXPECT_TRUE(buttons[kBaseCampScenarioLineIndex].hidden)
                    << variant
                    << ": network status must not retain the solo SCEN action";
                EXPECT_EQ(!host_visible,
                          buttons[kCreateMenuGoIndex].hidden)
                    << variant << ": the rewire host-gates GO itself";
                // §2.6: the same-rect READY twin shows exactly when GO
                // hides — a networked joiner (incl. the [NET-R9] spectator
                // machine) always has the slot occupied by ONE of the pair.
                EXPECT_EQ(host_visible,
                          buttons[kCreateMenuReadyIndex].hidden)
                    << variant << ": READY is GO's mutually exclusive twin";
                if (!host_visible)
                {
                    EXPECT_EQ("READY",
                              buttons[kCreateMenuReadyIndex].label)
                        << variant << ": unready joiner label";
                }
                check_no_overlaps(buttons, count, variant.c_str());
                check_bounds(buttons, count, variant.c_str());
                check_nav_closed_and_reachable(buttons, count,
                                               kCreateMenuBackIndex,
                                               variant.c_str());

                const int first = state.page.first_index();
                const int visible =
                    state.page.end_index() - state.page.first_index();
                for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
                {
                    if (r >= visible)
                    {
                        EXPECT_TRUE(buttons[r].hidden) << variant;
                        EXPECT_TRUE(buttons[kBaseCampRowBodyBase + r].hidden)
                            << variant;
                        EXPECT_TRUE(buttons[kBaseCampTeamChipBase + r].hidden)
                            << variant;
                        continue;
                    }
                    const bool owned = (first + r) < own_size;
                    EXPECT_FALSE(buttons[r].hidden) << variant;
                    EXPECT_EQ(!owned, buttons[r].no_draw)
                        << variant << " row " << r
                        << ": foreign rows are no_draw hit zones";
                    EXPECT_EQ(owned ? 14 : 300, buttons[r].sizex)
                        << variant << " row " << r;
                    EXPECT_EQ(owned,
                              !buttons[kBaseCampRowBodyBase + r].hidden)
                        << variant << " row " << r
                        << ": the §9.11 row-body zone shows on own rows "
                           "only (foreign rows train nowhere — the widened "
                           "dep zone pops OWNED BY)";
                    EXPECT_TRUE(buttons[kBaseCampTeamChipBase + r].hidden)
                        << variant << " row " << r
                        << ": lobby-assigned teams hide the solo cycler";
                }
            }
        }

        // Spectator machine shape [NET-R9]: no own rows, but the foreign
        // roster is visible — the highlight keeps the first visible row
        // (HIRE seeding applies only to a fully empty display).
        if (own_size == 0 && foreign_size > 0)
        {
            state.page.page = 0;
            lobby.host = false;
            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = 0;
            spec.nav.rewire(buttons, count, highlighted);
            EXPECT_EQ(0, highlighted)
                << "spectator machines keep the roster-first highlight";
            // §9.11: the roster_row_0 entry default is hidden on an
            // all-foreign page — the rewire's visibility fall lands on the
            // first visible button (the foreign hit zone), never strands.
            highlighted = spec.default_highlight;
            spec.nav.rewire(buttons, count, highlighted);
            EXPECT_EQ(0, highlighted)
                << "spectator entry falls from the hidden row body to the "
                   "foreign hit zone";
        }

        og::ui::install_base_camp_state_for_screen(nullptr);
    }

    og::ui::install_active_picker_lobby_client(saved_client);
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] = std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    save.save_name = old_save_name;
    (void)picker_createmenu_buttons();
}

// SCENARIO subscreen static table: the x=30 column stacks the host-gated
// SET CAMPAIGN / SET LEVEL (their name strips draw alongside) over the
// always-visible VIEW LEVEL | MATCHUP | PROGRESS row; BACK sits at (30,170)
// so no other screen's "back" shares its geometry.
TEST(MenuLayout, scenariomenu_static_layout)
{
    button* buttons = picker_scenariomenu_buttons();
    const int count = picker_scenariomenu_button_count();
    ASSERT_EQ(kScenarioMenuButtonCount, count);

    struct ExpectedButton
    {
        const char* id;
        const char* label;
        int x, y, w, h;
        MenuNav nav;
    };
    static const ExpectedButton kExpected[] = {
        {"back", "BACK", 30, 170, 60, 20, MenuNav{.up = 3}},
        {"set_campaign", "SET CAMPAIGN", 30, 40, 80, 15, MenuNav{.down = 2}},
        {"set_level", "SET LEVEL", 30, 70, 80, 15, MenuNav{.up = 1, .down = 3}},
        {"view_scenario", "VIEW LEVEL", 30, 100, 80, 15, MenuNav{.up = 2, .down = 0, .right = 4}},
        {"matchup", "MATCHUP", 120, 100, 80, 15, MenuNav{.up = 2, .down = 6, .left = 3, .right = 5}},
        {"progress", "PROGRESS", 210, 100, 80, 15, MenuNav{.up = 2, .down = 0, .left = 4}},
        {"troops", "TROOPS: ALL", 120, 140, 80, 15, MenuNav{.up = 4, .down = 0}},
    };

    for (int i = 0; i < count; ++i)
    {
        const ExpectedButton& want = kExpected[i];
        const button& got = buttons[i];
        EXPECT_EQ(want.id, got.id) << "index " << i;
        EXPECT_EQ(want.label, got.label) << got.id;
        EXPECT_FALSE(got.hidden) << got.id;
        EXPECT_EQ(want.x, got.x) << got.id;
        EXPECT_EQ(want.y, got.y) << got.id;
        EXPECT_EQ(want.w, got.sizex) << got.id;
        EXPECT_EQ(want.h, got.sizey) << got.id;
        EXPECT_EQ(want.nav.up, got.nav.up) << got.id;
        EXPECT_EQ(want.nav.down, got.nav.down) << got.id;
        EXPECT_EQ(want.nav.left, got.nav.left) << got.id;
        EXPECT_EQ(want.nav.right, got.nav.right) << got.id;
        EXPECT_LE(got.label.size(), 12u) << got.label;
    }

    EXPECT_EQ(kScenarioMenuBackIndex, 0);
    EXPECT_EQ(kScenarioMenuSetCampaignIndex, 1);
    EXPECT_EQ(kScenarioMenuSetLevelIndex, 2);
    EXPECT_EQ(kScenarioMenuViewScenarioIndex, 3);
    EXPECT_EQ(kScenarioMenuTeamsIndex, 4);
    EXPECT_EQ(kScenarioMenuProgressIndex, 5);
    EXPECT_EQ(kScenarioMenuTroopsIndex, 6);

    // The campaign-name / level-title strips draw from x=116 (32-char clip,
    // 6px/char): they must clear the x=30 button column's right edge.
    EXPECT_GE(116, buttons[kScenarioMenuSetCampaignIndex].x +
                       buttons[kScenarioMenuSetCampaignIndex].sizex);
    EXPECT_LE(116 + 32 * 6, SCREEN_W);

    // ...and so must the TROOPS row, which is why it sits at y=140 instead
    // of the y=70 cell beside SET LEVEL: the strips are drawn AFTER
    // draw_buttons, so anything under them is overprinted. The two strip
    // bands are 8px tall at each host-gated button's y+4-1.
    for (const int strip_index :
         {kScenarioMenuSetCampaignIndex, kScenarioMenuSetLevelIndex})
    {
        const int strip_top = buttons[strip_index].y + 3;
        const button& troops = buttons[kScenarioMenuTroopsIndex];
        const bool vertically_clear = troops.y + troops.sizey <= strip_top ||
                                      troops.y >= strip_top + 8;
        const bool horizontally_clear = troops.x + troops.sizex <= 114;
        EXPECT_TRUE(vertically_clear || horizontally_clear)
            << "TROOPS is drawn under the strip beside button "
            << buttons[strip_index].id;
    }

    check_no_overlaps(buttons, count, "scenariomenu");
    check_bounds(buttons, count, "scenariomenu");
    check_nav_closed_and_reachable(buttons, count, kScenarioMenuBackIndex,
                                   "scenariomenu_static");
}

// Host-gating variants for the SCENARIO subscreen: SET CAMPAIGN / SET LEVEL /
// TROOPS hide for joiners and the row's up-links (plus MATCHUP's down-link)
// rewire around them.
TEST(MenuLayout, scenariomenu_nav_variants_keyboard_reachable)
{
    for (const bool host_visible : {true, false})
    {
        button* buttons = picker_scenariomenu_buttons();
        const int count = picker_scenariomenu_button_count();
        buttons[kScenarioMenuSetCampaignIndex].hidden = !host_visible;
        buttons[kScenarioMenuSetLevelIndex].hidden = !host_visible;
        buttons[kScenarioMenuTroopsIndex].hidden = !host_visible;
        picker_wire_scenario_menu_nav(buttons, count, host_visible);
        check_nav_closed_and_reachable(
            buttons, count, kScenarioMenuBackIndex,
            host_visible ? "scenariomenu_host" : "scenariomenu_joiner");
    }
}

// MATCHUP retains the former TEAMS table's 17 stable ordinals, but JOIN,
// local-guy cycling, and duplicate READY are permanently dormant.
TEST(MenuLayout, matchup_static_layout)
{
    button* buttons = picker_teamsmenu_buttons();
    const int count = picker_teamsmenu_button_count();
    ASSERT_EQ(kTeamsMenuButtonCount, count);

    ASSERT_EQ("back", buttons[kTeamsMenuBackIndex].id);
    ASSERT_EQ("ctf_teams", buttons[kTeamsMenuCtfTeamsIndex].id);
    ASSERT_EQ("ctf_caps", buttons[kTeamsMenuCtfCapsIndex].id);
    ASSERT_EQ("join_team_0", buttons[kTeamsMenuJoinFirstIndex + 0].id);
    ASSERT_EQ("join_team_1", buttons[kTeamsMenuJoinFirstIndex + 1].id);
    ASSERT_EQ("join_team_2", buttons[kTeamsMenuJoinFirstIndex + 2].id);
    ASSERT_EQ("join_team_3", buttons[kTeamsMenuJoinFirstIndex + 3].id);
    ASSERT_EQ("guy_prev", buttons[kTeamsMenuGuyPrevIndex].id);
    ASSERT_EQ("guy_next", buttons[kTeamsMenuGuyNextIndex].id);
    ASSERT_EQ("guy_team", buttons[kTeamsMenuGuyTeamIndex].id);
    ASSERT_EQ("ready", buttons[kTeamsMenuReadyIndex].id);
    ASSERT_EQ("ctf_troops", buttons[kTeamsMenuCtfTroopsIndex].id);
    ASSERT_EQ("team_page_0", buttons[kTeamsMenuPageFirstIndex + 0].id);
    ASSERT_EQ("team_page_1", buttons[kTeamsMenuPageFirstIndex + 1].id);
    ASSERT_EQ("team_page_2", buttons[kTeamsMenuPageFirstIndex + 2].id);
    ASSERT_EQ("team_page_3", buttons[kTeamsMenuPageFirstIndex + 3].id);
    ASSERT_EQ("cross_control", buttons[kTeamsMenuCrossControlIndex].id);

    // Conditional settings, pagers, and cross-control start hidden.
    EXPECT_TRUE(buttons[kTeamsMenuCtfTeamsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuCtfCapsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuCtfTroopsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuReadyIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuCrossControlIndex].hidden);
    for (int t = 0; t < 4; ++t)
        EXPECT_TRUE(buttons[kTeamsMenuJoinFirstIndex + t].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuGuyPrevIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuGuyNextIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuGuyTeamIndex].hidden);

    // Cross-control occupies the bottom-center space released by READY.
    EXPECT_EQ(120, buttons[kTeamsMenuCrossControlIndex].x);
    EXPECT_EQ(170, buttons[kTeamsMenuCrossControlIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCrossControlIndex].sizex);
    EXPECT_EQ(20, buttons[kTeamsMenuCrossControlIndex].sizey);
    EXPECT_EQ("CTRL: OWN", buttons[kTeamsMenuCrossControlIndex].label);
    EXPECT_LE(buttons[kTeamsMenuCrossControlIndex].label.size(), 12u);
    EXPECT_LE(og::ui::format_cross_control_label(true).size(), 12u);
    EXPECT_LE(og::ui::format_cross_control_label(false).size(), 12u);

    // CTF settings row at the top; troops completes the trio bottom-right.
    EXPECT_EQ(120, buttons[kTeamsMenuCtfTeamsIndex].x);
    EXPECT_EQ(8, buttons[kTeamsMenuCtfTeamsIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCtfTeamsIndex].sizex);
    EXPECT_EQ(210, buttons[kTeamsMenuCtfCapsIndex].x);
    EXPECT_EQ(8, buttons[kTeamsMenuCtfCapsIndex].y);
    EXPECT_EQ(210, buttons[kTeamsMenuCtfTroopsIndex].x);
    EXPECT_EQ(170, buttons[kTeamsMenuCtfTroopsIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCtfTroopsIndex].sizex);

    // Retired JOIN rows preserve their old descriptors solely to keep the
    // historic positional table legible; they never reappear.
    for (int t = 0; t < 4; ++t)
    {
        const button& join = buttons[kTeamsMenuJoinFirstIndex + t];
        EXPECT_TRUE(join.hidden);
        EXPECT_EQ(240, join.x);
        EXPECT_EQ(32 + 30 * t, join.y);
        EXPECT_EQ(50, join.sizex);
        EXPECT_EQ(12, join.sizey);
    }

    // Per-team detail pagers sit inside the widened x=8..312 row bars.
    for (int t = 0; t < 4; ++t)
    {
        const button& pager = buttons[kTeamsMenuPageFirstIndex + t];
        EXPECT_TRUE(pager.hidden) << pager.id;
        EXPECT_EQ(297, pager.x) << pager.id;
        EXPECT_EQ(39 + 30 * t, pager.y) << pager.id;
        EXPECT_EQ(14, pager.sizex) << pager.id;
        EXPECT_EQ(12, pager.sizey) << pager.id;
        EXPECT_EQ(">", pager.label) << pager.id;
        // Fully inside the row band: bar bottom is row_y+20 = 39+30t+13.
        EXPECT_LE(pager.x + pager.sizex, 312) << pager.id;
        EXPECT_LE(pager.y + pager.sizey, (32 + 30 * t) + 20) << pager.id;
        // The 39-character detail slice and p/N indicator stop before it.
        EXPECT_LE(24 + 39 * 6, 295) << "paged detail slice budget";
        EXPECT_LT(295, pager.x) << pager.id;
    }

    // 6px/char budgets: 12 chars on the 80px settings faces, 6 on TEAM >.
    EXPECT_LE(buttons[kTeamsMenuCtfTeamsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuCtfCapsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuCtfTroopsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuGuyTeamIndex].label.size(), 11u);
    EXPECT_LE(buttons[kTeamsMenuReadyIndex].label.size(), 12u);

    check_no_overlaps(buttons, count, "matchup");
    check_bounds(buttons, count, "matchup");
    check_nav_in_range(buttons, count, "matchup");
}

namespace
{
// Apply a wiring's hidden flags the way MATCHUP's per-frame sync does.
void apply_teams_menu_hidden_flags(button* buttons, const TeamsMenuWiring& w)
{
    buttons[kTeamsMenuCtfTeamsIndex].hidden = !w.show_ctf;
    buttons[kTeamsMenuCtfCapsIndex].hidden = !w.show_ctf;
    // Dormant ordinal: the scenario-troops control moved to the SCENARIO
    // screen, so this row is hidden in every MATCHUP variant.
    buttons[kTeamsMenuCtfTroopsIndex].hidden = true;
    for (int t = 0; t < 4; ++t)
        buttons[kTeamsMenuJoinFirstIndex + t].hidden = true;
    buttons[kTeamsMenuGuyPrevIndex].hidden = true;
    buttons[kTeamsMenuGuyNextIndex].hidden = true;
    buttons[kTeamsMenuGuyTeamIndex].hidden = true;
    buttons[kTeamsMenuReadyIndex].hidden = true;
    buttons[kTeamsMenuCrossControlIndex].hidden = !w.cross_control;
    for (int t = 0; t < 4; ++t)
        buttons[kTeamsMenuPageFirstIndex + t].hidden = !w.pager_visible[static_cast<std::size_t>(t)];
}

// Every nav link must land on a VISIBLE button (nav does not skip hidden
// buttons), and every visible button must be keyboard-reachable from BACK.
void check_teams_menu_wiring(const TeamsMenuWiring& w, const char* variant)
{
    button* buttons = picker_teamsmenu_buttons();
    const int count = picker_teamsmenu_button_count();
    apply_teams_menu_hidden_flags(buttons, w);
    picker_wire_teams_menu_nav(buttons, count, w);

    for (int t = 0; t < 4; ++t)
        EXPECT_TRUE(buttons[kTeamsMenuJoinFirstIndex + t].hidden) << variant;
    EXPECT_TRUE(buttons[kTeamsMenuGuyPrevIndex].hidden) << variant;
    EXPECT_TRUE(buttons[kTeamsMenuGuyNextIndex].hidden) << variant;
    EXPECT_TRUE(buttons[kTeamsMenuGuyTeamIndex].hidden) << variant;
    EXPECT_TRUE(buttons[kTeamsMenuReadyIndex].hidden) << variant;

    check_nav_in_range(buttons, count, variant);

    for (int i = 0; i < count; ++i)
    {
        if (buttons[i].hidden)
            continue;
        for (const int target : {buttons[i].nav.up, buttons[i].nav.down,
                                 buttons[i].nav.left, buttons[i].nav.right})
        {
            if (target < 0)
                continue;
            EXPECT_FALSE(buttons[target].hidden)
                << variant << ": '" << buttons[i].id
                << "' links to hidden '" << buttons[target].id << "'";
        }
    }

    // BFS over the nav edges from BACK (always visible).
    std::vector<bool> reached(static_cast<std::size_t>(count), false);
    std::vector<int> frontier{kTeamsMenuBackIndex};
    reached[kTeamsMenuBackIndex] = true;
    while (!frontier.empty())
    {
        const int current = frontier.back();
        frontier.pop_back();
        for (const int target :
             {buttons[current].nav.up, buttons[current].nav.down,
              buttons[current].nav.left, buttons[current].nav.right})
        {
            if (target >= 0 && !reached[static_cast<std::size_t>(target)])
            {
                reached[static_cast<std::size_t>(target)] = true;
                frontier.push_back(target);
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        if (!buttons[i].hidden)
        {
            EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                << variant << ": '" << buttons[i].id
                << "' is not keyboard-reachable";
        }
    }
}
} // namespace

// MATCHUP's remaining conditional axes are CTF controls, cross-control, and
// the four independent team-detail pagers.
TEST(MenuLayout, matchup_nav_variants_keyboard_reachable)
{
    // Local classic overview: only overflowing detail rows are interactive.
    check_teams_menu_wiring(
        TeamsMenuWiring{.pager_visible = {true, true, false, false}},
        "local_classic");
    // Local CTF host: settings trio and one paged row.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .pager_visible = {true, false, false, false}},
        "local_ctf_host");
    // An isolated lower-team pager remains a reachable row anchor.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .pager_visible = {false, false, true, false}},
        "local_ctf_two_teams");
    // Networked classic: cross-control plus every detail pager; READY stays
    // exclusively in Base Camp.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true,
                        .cross_control = true,
                        .pager_visible = {true, true, true, true}},
        "networked_joiner_classic");
    // Networked CTF host: settings, cross-control, and a lower pager.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .networked = true,
                        .cross_control = true,
                        .pager_visible = {false, false, false, true}},
        "networked_ctf_host");
    // Bare network spectator/joiner still reaches cross-control.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true,
                        .cross_control = true},
        "networked_bare");
    // Degenerate: BACK alone.
    check_teams_menu_wiring(TeamsMenuWiring{}, "back_only");
}


TEST(MenuLayout, main_options_nav_indices_in_range)
{
    button* buttons = picker_main_options_buttons();
    const int count = picker_main_options_button_count();
    check_nav_in_range(buttons, count, "main_options");
}


// The per-effect toggles live in the three FX subscreens; main options keeps
// the sound/graphics settings plus the three stacked FX doors. The global
// CONTROLS door is gone (per-seat player screens own every row it had), and
// SPEED took the row it vacated. Pin the draw-hook index contract, the two
// column edges, and the nav graph.
TEST(MenuLayout, main_options_index_contract_and_nav)
{
    button* buttons = picker_main_options_buttons();
    const int count = picker_main_options_button_count();
    ASSERT_EQ(9, count)
        << "main options is BACK + Sound + DISPLAY + sprite sheet + "
           "3 FX doors + RESTORE SETTINGS + SPEED";

    static const char* kExpectedIds[] = {
        "options_back",       // 0
        "toggle_sound",       // 1
        "display_settings",   // 2: opens the DISPLAY subscreen
        "gameplay_fx",        // 3: opens the GAMEPLAY FX subscreen
        "restore_defaults",   // 4
        "pick_sprite_sheet",  // 5: label synced by index each frame
        "ui_fx",              // 6: opens the UI FX subscreen
        "graphics_fx",        // 7: opens the GRAPHICS FX subscreen
        "game_speed",         // 8: cfg gameplay/timer_wait cycler
    };
    for (int i = 0; i < count; ++i)
    {
        EXPECT_EQ(kExpectedIds[i], buttons[i].id) << "index " << i;
        EXPECT_FALSE(buttons[i].hidden) << buttons[i].id;
        // Centered labels draw with no clipping at 6px/char.
        EXPECT_LE(static_cast<int>(buttons[i].label.size()) * 6,
                  buttons[i].sizex)
            << buttons[i].id << " label '" << buttons[i].label
            << "' escapes its face";
    }
    EXPECT_EQ("RESTORE SETTINGS", buttons[4].label);
    // Column relation, not coordinates: Sound heads the door column, and
    // every door below it shares that one x at a 23px pitch.
    EXPECT_EQ("DISPLAY", buttons[2].label);
    for (const int door : {1, 3, 6, 7})
        EXPECT_EQ(buttons[2].x, buttons[door].x)
            << buttons[door].id << " must sit on the door column edge";
    EXPECT_EQ(buttons[1].y + 23, buttons[2].y);
    EXPECT_EQ(buttons[2].y + 23, buttons[3].y);
    EXPECT_EQ(buttons[3].y + 23, buttons[6].y);
    EXPECT_EQ(buttons[6].y + 23, buttons[7].y);
    EXPECT_EQ(button_action_id(ButtonAction::OpenDisplaySettings), buttons[2].myfun);

    // SPEED closes the right column (RESTORE SETTINGS / Sprite Sheet / SPEED
    // share one x) in the row the retired CONTROLS door vacated: the door
    // column's 90px faces reach x=220, so SPEED must clear their last row.
    EXPECT_EQ(button_action_id(ButtonAction::CycleGameSpeed), buttons[8].myfun);
    EXPECT_EQ(buttons[4].x, buttons[8].x);
    EXPECT_EQ(buttons[5].x, buttons[8].x);
    EXPECT_EQ(buttons[7].y + 23, buttons[8].y)
        << "SPEED keeps the screen's 23px vertical rhythm";
    EXPECT_GT(buttons[8].y, buttons[7].y + buttons[7].sizey)
        << "SPEED must clear the last door's row";
    // Every SPEED the cycler can store fits the 90px face (15 chars).
    {
        std::string value = cfg.get_setting("gameplay", "timer_wait");
        for (int step = 0; step < 11; ++step)
        {
            EXPECT_LE(
                static_cast<int>(og::ui::format_game_speed_label(value).size()) * 6,
                buttons[8].sizex)
                << og::ui::format_game_speed_label(value);
            value = og::ui::cycle_game_speed(value);
        }
    }

    check_nav_closed_and_reachable(buttons, count, 0, "main_options");
}

// DISPLAY subscreen: the mode / resolution / overscan / zoom / smoothing
// stack. Every face is cfg-derived at 6px/char inside 102px (17 chars);
// "Mode: Borderless" and "Res: 2560x1440" are the widest realistic faces.
TEST(MenuLayout, display_settings_index_contract_and_nav)
{
    button* buttons = picker_display_settings_buttons();
    const int count = picker_display_settings_button_count();
    ASSERT_EQ(9, count)
        << "display settings is BACK + mode + resolution + overscan pair + "
           "zoom + smoothing + brightness pair";

    static const char* kExpectedIds[] = {
        "display_back",        // 0
        "display_mode",        // 1: label synced from graphics/fullscreen
        "display_resolution",  // 2: label synced from graphics/width+height
        "overscan_minus",      // 3
        "overscan_plus",       // 4
        "display_zoom",        // 5: label synced from graphics/zoom
        "display_smoothing",   // 6: label synced from graphics/smoothing
        "brightness_minus",    // 7: cfg graphics/brightness, one gamma step
        "brightness_plus",     // 8
    };
    for (int i = 0; i < count; ++i)
    {
        EXPECT_EQ(kExpectedIds[i], buttons[i].id) << "index " << i;
        EXPECT_FALSE(buttons[i].hidden) << buttons[i].id;
        EXPECT_LE(static_cast<int>(buttons[i].label.size()) * 6,
                  buttons[i].sizex)
            << buttons[i].id << " label '" << buttons[i].label
            << "' escapes its face";
    }
    ASSERT_EQ(kDisplayMenuModeIndex, 1);
    ASSERT_EQ(kDisplayMenuResolutionIndex, 2);
    ASSERT_EQ(kDisplayMenuZoomIndex, 5);
    ASSERT_EQ(kDisplayMenuSmoothingIndex, 6);
    // The widest cfg-derivable faces must fit the 102px budget.
    EXPECT_LE(static_cast<int>(std::string("Mode: Borderless").size()) * 6,
              buttons[kDisplayMenuModeIndex].sizex);
    EXPECT_LE(static_cast<int>(std::string("Res: 2560x1440").size()) * 6,
              buttons[kDisplayMenuResolutionIndex].sizex);
    EXPECT_EQ(button_action_id(ButtonAction::CycleDisplayMode), buttons[1].myfun);
    EXPECT_EQ(button_action_id(ButtonAction::CycleResolution), buttons[2].myfun);
    EXPECT_EQ(button_action_id(ButtonAction::CycleZoom), buttons[5].myfun);
    EXPECT_EQ(button_action_id(ButtonAction::CycleSmoothing), buttons[6].myfun);
    // The settings stack at one x at the effects-grid 23px pitch (the
    // overscan pair shares its row).
    EXPECT_EQ(buttons[1].x, buttons[2].x);
    EXPECT_EQ(buttons[1].y + 23, buttons[2].y);
    EXPECT_EQ(buttons[3].y, buttons[4].y);
    EXPECT_EQ(buttons[2].y + 23, buttons[3].y);
    EXPECT_EQ(buttons[3].y + 23, buttons[5].y);
    EXPECT_EQ(buttons[5].y + 23, buttons[6].y);
    // BRIGHTNESS takes the overscan pair's shape one row further down: same
    // 30px faces at the same two x, one adjust action, opposite args.
    EXPECT_EQ(buttons[6].y + 23, buttons[7].y);
    EXPECT_EQ(buttons[7].y, buttons[8].y);
    EXPECT_EQ(buttons[3].x, buttons[7].x);
    EXPECT_EQ(buttons[4].x, buttons[8].x);
    EXPECT_EQ(buttons[3].sizex, buttons[7].sizex);
    EXPECT_EQ(buttons[4].sizex, buttons[8].sizex);
    EXPECT_EQ(button_action_id(ButtonAction::BrightnessAdjust), buttons[7].myfun);
    EXPECT_EQ(button_action_id(ButtonAction::BrightnessAdjust), buttons[8].myfun);
    EXPECT_EQ(-1, buttons[7].arg1);
    EXPECT_EQ(1, buttons[8].arg1);

    check_nav_closed_and_reachable(buttons, count, 0, "display_settings");
}

namespace
{
struct ExpectedFxButton
{
    const char* id;
    const char* label;
    int x;
    int y;
};

// Shared pin for the three FX subscreens: exact count/id/label/geometry,
// label budgets (90px toggle faces fit 15 chars at 6px/char, drawn centered
// with no clipping), faces inside the 4..196 inverted bevel, and — since no
// FX button is visibility-gated — a closed, fully reachable static nav graph.
void check_fx_options_screen(button* buttons, int count,
                             const ExpectedFxButton* expected,
                             int expected_count, const char* screen)
{
    ASSERT_EQ(expected_count, count) << screen;
    for (int i = 0; i < count; ++i)
    {
        const ExpectedFxButton& want = expected[i];
        const button& got = buttons[i];
        EXPECT_EQ(want.id, got.id) << screen << " index " << i;
        EXPECT_EQ(want.label, got.label) << screen << " " << got.id;
        EXPECT_EQ(want.x, got.x) << screen << " " << got.id;
        EXPECT_EQ(want.y, got.y) << screen << " " << got.id;
        EXPECT_FALSE(got.hidden) << screen << " " << got.id;
        EXPECT_LE(static_cast<int>(got.label.size()) * 6, got.sizex)
            << screen << " " << got.id << " label '" << got.label
            << "' escapes its face";
        EXPECT_LE(got.y + got.sizey, 196)
            << screen << " " << got.id << " face exits the bevel";
    }

    check_no_overlaps(buttons, count, screen);
    check_bounds(buttons, count, screen);
    check_nav_closed_and_reachable(buttons, count, 0, screen);
}
} // namespace

// GAMEPLAY FX subscreen: unique BACK id + the two gameplay-feel toggles
// (stable ids — injector flows click these by id) in a centered column.
TEST(MenuLayout, gameplay_fx_options_layout_and_nav)
{
    static const ExpectedFxButton kExpected[] = {
        {"gameplay_fx_back", "BACK", 10, 10},
        {"toggle_hit_recoil", "Hit recoil", 115, 35},
        {"toggle_attack_lunge", "Attack lunge", 115, 58},
    };
    button* buttons = picker_gameplay_fx_options_buttons();
    const int count = picker_gameplay_fx_options_button_count();
    check_fx_options_screen(buttons, count, kExpected, 3, "gameplay_fx_options");
}

// UI FX subscreen: unique BACK id + the three overlay toggles in a centered
// column.
TEST(MenuLayout, ui_fx_options_layout_and_nav)
{
    static const ExpectedFxButton kExpected[] = {
        {"ui_fx_back", "BACK", 10, 10},
        {"toggle_mini_hp_bar", "Mini HP bar", 115, 35},
        {"toggle_damage_numbers", "Damage numbers", 115, 58},
        {"toggle_heal_numbers", "Healing numbers", 115, 81},
    };
    button* buttons = picker_ui_fx_options_buttons();
    const int count = picker_ui_fx_options_button_count();
    check_fx_options_screen(buttons, count, kExpected, 4, "ui_fx_options");
}

// GRAPHICS FX subscreen: unique BACK id + 14 effects/* visual toggles on the
// three-column x=15/115/215 grid (4 full rows at 23px pitch from y=35, plus
// floor glide and color cycling on a fifth row). Weather is the single display
// opt-out for the per-level sim weather (the old Clouds/Rain pair merged).
TEST(MenuLayout, graphics_fx_options_grid_geometry_and_nav)
{
    static const ExpectedFxButton kExpected[] = {
        {"graphics_fx_back", "BACK", 10, 10},
        {"toggle_hit_flash", "Hit flash", 15, 35},
        {"toggle_hit_sparks", "Hit sparks", 115, 35},
        {"toggle_gore", "Gore", 215, 35},
        {"toggle_shadows", "Shadows", 15, 58},
        {"toggle_reflections", "Reflections", 115, 58},
        {"toggle_weather", "Weather", 215, 58},
        {"toggle_dust", "Dust", 15, 81},
        {"depth_fx", "Depth: Fog", 115, 81},
        {"toggle_trails", "Trails", 215, 81},
        {"toggle_fire_glow", "Fire glow", 15, 104},
        {"toggle_ripples", "Ripples", 115, 104},
        {"toggle_screen_shake", "Screen shake", 215, 104},
        {"toggle_floor_glide", "Floor glide", 15, 127},
        {"toggle_color_cycling", "Color cycling", 115, 127},
    };
    button* buttons = picker_graphics_fx_options_buttons();
    const int count = picker_graphics_fx_options_button_count();
    check_fx_options_screen(buttons, count, kExpected, 15, "graphics_fx_options");

    // The depth row is a five-way CYCLE (id "depth_fx"), addressed by index
    // from change_depth_fx(); pin the index contract and that every label
    // the cycle can produce fits the 90px face (15 chars at 6px/char).
    EXPECT_EQ("depth_fx", buttons[kGraphicsFxDepthFxIndex].id);
    const int face_width = buttons[kGraphicsFxDepthFxIndex].sizex;
    ASSERT_EQ(90, face_width);
    std::string value = "fog";
    for (int step = 0; step < 5; ++step)
    {
        EXPECT_LE(static_cast<int>(og::ui::format_depth_fx_label(value).size()) * 6,
                  face_width)
            << og::ui::format_depth_fx_label(value);
        value = og::ui::cycle_depth_fx(value);
    }
    EXPECT_EQ("fog", value) << "five clicks must restore the selector";
}

// DIFFICULTY subscreen (the main-menu DIFFICULTY door): unique BACK id + the
// six match-rule rows in one centered 140px column on the FX row pitch.
// Static labels are the default-state formatter outputs; the screen re-derives
// every row from session/save each frame, so also pin that every label the
// formatters can produce fits the 140px face (23 chars at 6px/char).
TEST(MenuLayout, difficulty_menu_layout_and_nav)
{
    static const ExpectedFxButton kExpected[] = {
        {"difficulty_back", "BACK", 10, 10},
        {"difficulty", "Difficulty: Battle", 90, 35},
        {"respawn_mode", "Respawns: Off", 90, 58},
        {"respawn_delay", "Spawn Delay: Normal", 90, 81},
        {"permadeath", "Permadeath: On", 90, 104},
        {"generator_rate", "Generators: Normal", 90, 127},
        {"infinite_gold", "Infinite Gold: Off", 90, 150},
    };
    button* buttons = picker_difficulty_menu_buttons();
    const int count = picker_difficulty_menu_button_count();
    ASSERT_EQ(kDifficultyMenuButtonCount, count);
    check_fx_options_screen(buttons, count, kExpected,
                            kDifficultyMenuButtonCount, "difficulty_menu");

    // The index contract the label-writing callbacks depend on.
    EXPECT_EQ("difficulty_back", buttons[kDifficultyMenuBackIndex].id);
    EXPECT_EQ("difficulty", buttons[kDifficultyMenuDifficultyIndex].id);
    EXPECT_EQ("respawn_mode", buttons[kDifficultyMenuRespawnModeIndex].id);
    EXPECT_EQ("respawn_delay", buttons[kDifficultyMenuRespawnDelayIndex].id);
    EXPECT_EQ("permadeath", buttons[kDifficultyMenuPermadeathIndex].id);
    EXPECT_EQ("generator_rate", buttons[kDifficultyMenuGeneratorRateIndex].id);
    EXPECT_EQ("infinite_gold", buttons[kDifficultyMenuInfiniteGoldIndex].id);

    // Every dynamic label across the full value cycles stays within the
    // 140px face budget.
    const int face_width = buttons[kDifficultyMenuDifficultyIndex].sizex;
    ASSERT_EQ(140, face_width);
    SaveData save;
    for (int step = 0; step < 4; ++step)
    {
        EXPECT_LE(static_cast<int>(og::ui::format_difficulty_label(step).size()) * 6,
                  face_width)
            << og::ui::format_difficulty_label(step);
        EXPECT_LE(static_cast<int>(og::ui::format_respawn_mode_label(save).size()) * 6,
                  face_width)
            << og::ui::format_respawn_mode_label(save);
        EXPECT_LE(static_cast<int>(og::ui::format_respawn_delay_label(save).size()) * 6,
                  face_width)
            << og::ui::format_respawn_delay_label(save);
        EXPECT_LE(static_cast<int>(og::ui::format_permadeath_label(save).size()) * 6,
                  face_width)
            << og::ui::format_permadeath_label(save);
        EXPECT_LE(static_cast<int>(og::ui::format_generator_rate_label(save).size()) * 6,
                  face_width)
            << og::ui::format_generator_rate_label(save);
        EXPECT_LE(static_cast<int>(og::ui::format_infinite_gold_label(save).size()) * 6,
                  face_width)
            << og::ui::format_infinite_gold_label(save);
        og::ui::cycle_respawn_mode(save);
        og::ui::cycle_respawn_delay(save);
        og::ui::toggle_permadeath(save);
        og::ui::cycle_generator_rate(save);
        og::ui::toggle_infinite_gold(save);
    }

    // Non-host (networked joiner) variant: every settings row is
    // LobbySettings-backed and hides; BACK's vertical cycle is rewired, the
    // highlight is pulled onto BACK, and the nav graph stays closed and
    // reachable in BOTH variants.
    struct FakeLobbyClient final : og::ui::IPickerLobbyClient
    {
        bool host = false;
        void initialize_from_save() override {}
        void shutdown() override {}
        void sync_from_save() override {}
        void sync_roster_from_save() override {}
        void sync_settings_from_save() override {}
        void poll_and_apply() override {}
        void set_player_mode(int) override {}
        bool request_start_game() override { return false; }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        build_game_start_config() const override { return std::nullopt; }
        std::optional<og::ui::PickerLobbyGameStartConfig>
        consume_game_start_config() override { return std::nullopt; }
        [[nodiscard]] bool start_request_pending() const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool host_controls_visible() const noexcept override
        {
            return host;
        }
    };
    FakeLobbyClient lobby;
    og::ui::IPickerLobbyClient* saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    lobby.host = false;
    int highlighted = kDifficultyMenuGeneratorRateIndex;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    for (int i = kDifficultyMenuDifficultyIndex; i < count; ++i)
        EXPECT_TRUE(buttons[i].hidden) << buttons[i].id;
    EXPECT_FALSE(buttons[kDifficultyMenuBackIndex].hidden);
    EXPECT_EQ(kDifficultyMenuBackIndex, highlighted)
        << "the highlight must be pulled off the hidden rows";
    check_nav_closed_and_reachable(buttons, count, kDifficultyMenuBackIndex,
                                   "difficulty_menu_joiner");

    // Host / local variant restores the full column.
    lobby.host = true;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    og::ui::install_active_picker_lobby_client(saved_client);
    for (int i = 0; i < count; ++i)
        EXPECT_FALSE(buttons[i].hidden) << buttons[i].id;
    check_nav_closed_and_reachable(buttons, count, kDifficultyMenuBackIndex,
                                   "difficulty_menu_host");
}

namespace
{

// The side labels drawn next to the networking fields, per build (web drops
// the direct JOIN IP / PORT rows entirely).
struct NetworkingFieldLabel
{
    int index;
    std::string_view label;
};

#ifdef __EMSCRIPTEN__
constexpr std::array<NetworkingFieldLabel, 1> kNetworkingFieldLabels{{
    {kNetworkingMenuRoomValueIndex, "ROOM CODE"},
}};
#else
constexpr std::array<NetworkingFieldLabel, 3> kNetworkingFieldLabels{{
    {kNetworkingMenuRoomValueIndex, "ROOM CODE"},
    {kNetworkingMenuIpIndex, "JOIN IP / HOST"},
    {kNetworkingMenuPortIndex, "PORT"},
}};
#endif

// The section headers configure_networking centers at x=160.
#ifdef __EMSCRIPTEN__
constexpr std::array<std::pair<std::string_view, int>, 1> kNetworkingHeaders{{
    {"ACTIVE GAMES", PICKER_NETWORKING_ROOMS_HEADER_Y},
}};
#else
constexpr std::array<std::pair<std::string_view, int>, 2> kNetworkingHeaders{{
    {"ACTIVE GAMES", PICKER_NETWORKING_ROOMS_HEADER_Y},
    {"DIRECT (LAN)", PICKER_NETWORKING_DIRECT_HEADER_Y},
}};
#endif

// Empty-list status copy drawn centered in the room-list area (only while
// no room rows are visible).
constexpr std::array<std::string_view, 4> kNetworkingRoomsStatusLines{{
    "Turn ROOM CODE ON to list relay games.",
    "Room list unavailable.",
    "No active games found.",
    "Looking for active games...",
}};

} // namespace

TEST(MenuLayout, networking_buttons_no_overlap)
{
    button* buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    ASSERT_EQ(kNetworkingMenuButtonCount, count);
    check_no_overlaps(buttons, count, "networking");
    check_bounds(buttons, count, "networking");
    check_nav_in_range(buttons, count, "networking");

    // Worst case: every ACTIVE GAMES row visible must still not collide
    // with any other control.
    std::vector<button> visible_rooms(buttons, buttons + count);
    for (int slot = 0; slot < kNetworkingMenuRoomSlots; ++slot)
        visible_rooms[static_cast<std::size_t>(
            kNetworkingMenuRoomFirstIndex + slot)].hidden = false;
    check_no_overlaps(visible_rooms.data(), count, "networking_rooms");
    check_bounds(visible_rooms.data(), count, "networking_rooms");
}

// Keyboard nav contract over every room-list variant: nav never links to a
// hidden room row and every visible button stays reachable from BACK.
TEST(MenuLayout, networking_nav_reachable_for_all_room_variants)
{
    button* buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    ASSERT_EQ(kNetworkingMenuButtonCount, count);

    for (int rooms = 0; rooms <= kNetworkingMenuRoomSlots; ++rooms)
    {
        std::vector<button> variant(buttons, buttons + count);
        for (int slot = 0; slot < kNetworkingMenuRoomSlots; ++slot)
            variant[static_cast<std::size_t>(
                kNetworkingMenuRoomFirstIndex + slot)].hidden = slot >= rooms;
        picker_wire_networking_menu_nav(variant.data(), count, rooms);
        const std::string name =
            std::string("networking_rooms_") + std::to_string(rooms);
        check_nav_closed_and_reachable(variant.data(), count,
                                       kNetworkingMenuBackIndex, name.c_str());
    }
}

// Every networking control AND every piece of copy (title, field labels,
// section headers, room-status copy, centered instruction lines) must fit
// inside the enclosing panel frame, so nothing writes over the frame edge
// (regression for the long instruction line overrunning the frame's right
// border).
TEST(MenuLayout, networking_content_fits_within_panel_frame)
{
    button* buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    const int fx1 = PICKER_NETWORKING_FRAME_X1;
    const int fy1 = PICKER_NETWORKING_FRAME_Y1;
    const int fx2 = PICKER_NETWORKING_FRAME_X2;
    const int fy2 = PICKER_NETWORKING_FRAME_Y2;
    ASSERT_LE(fx2, SCREEN_W);
    ASSERT_LE(fy2, SCREEN_H);

    const auto fits = [&](int x, int y, int w, int h, const char* what) {
        EXPECT_GE(x, fx1) << what << " clips the frame's left edge";
        EXPECT_GE(y, fy1) << what << " clips the frame's top edge";
        EXPECT_LE(x + w, fx2) << what << " clips the frame's right edge";
        EXPECT_LE(y + h, fy2) << what << " clips the frame's bottom edge";
    };

    // Room rows are hidden by default but occupy fixed slots — check every
    // button's rect regardless of visibility.
    for (int i = 0; i < count; ++i)
    {
        fits(buttons[i].x, buttons[i].y, buttons[i].sizex, buttons[i].sizey,
             buttons[i].id.c_str());
    }

    const int title_w = mytext.query_width(std::string_view("NETWORKING"));
    fits(160 - title_w / 2, PICKER_NETWORKING_TITLE_Y, title_w, mytext.sizey,
         "title");

    for (const NetworkingFieldLabel& field_label : kNetworkingFieldLabels)
    {
        const button& field = buttons[field_label.index];
        const int w = mytext.query_width(field_label.label);
        fits(field.x - w - PICKER_NETWORKING_LABEL_GAP,
             field.y + (field.sizey - mytext.sizey) / 2, w, mytext.sizey,
             "field label");
    }

    for (const auto& [header, header_y] : kNetworkingHeaders)
    {
        const int w = mytext.query_width(header);
        fits(160 - w / 2, header_y, w, mytext.sizey, "section header");
    }

    for (const std::string_view status : kNetworkingRoomsStatusLines)
    {
        const int w = mytext.query_width(status);
        fits(160 - w / 2, PICKER_NETWORKING_ROOM_Y + 2, w, mytext.sizey,
             "rooms status line");
    }

    const auto lines = og::ui::networking_menu_instruction_lines();
    const int pitch = mytext.sizey + 1;
    const int height = lines.empty()
        ? 0
        : mytext.sizey + static_cast<int>(lines.size() - 1) * pitch;
    const int iy = buttons[kNetworkingMenuJoinIndex].y -
        PICKER_NETWORKING_INSTRUCTION_GAP - height;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const int w = mytext.query_width(lines[i]);
        fits(160 - w / 2, iy + static_cast<int>(i) * pitch, w, mytext.sizey,
             "instruction line");
    }
}

TEST(MenuLayout, networking_text_does_not_overlap_buttons)
{
    button* buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    text& mytext = og::runtime::current_session->myscreen_->text_normal;

    // Worst case for the copy: every ACTIVE GAMES row visible. (The rooms
    // status line is exercised separately below because it only draws while
    // the list is empty.)
    std::vector<button> visible(buttons, buttons + count);
    for (int slot = 0; slot < kNetworkingMenuRoomSlots; ++slot)
        visible[static_cast<std::size_t>(
            kNetworkingMenuRoomFirstIndex + slot)].hidden = false;

    const auto check_text_rect = [&](int x, int y, int w, int h,
                                     std::string_view what,
                                     bool against_room_rows) {
        ASSERT_GE(x, 0) << what << " should remain on-screen";
        ASSERT_LE(x + w, SCREEN_W) << what << " should remain on-screen";
        for (int button_index = 0; button_index < count; ++button_index)
        {
            const button& other = visible[static_cast<std::size_t>(button_index)];
            if (!against_room_rows &&
                button_index >= kNetworkingMenuRoomFirstIndex)
                continue;
            ASSERT_FALSE(rects_overlap(x, y, w, h, other.x, other.y,
                                       other.sizex, other.sizey))
                << "networking copy '" << what << "' overlaps button '"
                << other.id << "'";
        }
    };

    for (const NetworkingFieldLabel& field_label : kNetworkingFieldLabels)
    {
        const button& field = buttons[field_label.index];
        const int label_w = mytext.query_width(field_label.label);
        const int label_x = field.x - label_w - PICKER_NETWORKING_LABEL_GAP;
        const int label_y = field.y + (field.sizey - mytext.sizey) / 2;
        check_text_rect(label_x, label_y, label_w, mytext.sizey,
                        field_label.label, true);
    }

    for (const auto& [header, header_y] : kNetworkingHeaders)
    {
        const int w = mytext.query_width(header);
        check_text_rect(160 - w / 2, header_y, w, mytext.sizey, header, true);
    }

    // The rooms status line shares the list area with the (then-hidden)
    // room rows, so it is only checked against the other controls.
    for (const std::string_view status : kNetworkingRoomsStatusLines)
    {
        const int w = mytext.query_width(status);
        check_text_rect(160 - w / 2, PICKER_NETWORKING_ROOM_Y + 2, w,
                        mytext.sizey, status, false);
    }

    const auto instruction_lines = og::ui::networking_menu_instruction_lines();
    const int instruction_pitch = mytext.sizey + 1;
    const int instruction_height = instruction_lines.empty()
        ? 0
        : mytext.sizey +
            static_cast<int>(instruction_lines.size() - 1) * instruction_pitch;
    const int instruction_y = buttons[kNetworkingMenuJoinIndex].y -
        PICKER_NETWORKING_INSTRUCTION_GAP - instruction_height;

    ASSERT_GE(instruction_y, 0) << "instruction copy should remain on-screen";

    for (std::size_t line_index = 0; line_index < instruction_lines.size(); ++line_index)
    {
        const std::string_view line = instruction_lines[line_index];
        const int line_w = mytext.query_width(line);
        const int line_y =
            instruction_y + static_cast<int>(line_index) * instruction_pitch;
        check_text_rect(160 - line_w / 2, line_y, line_w, mytext.sizey, line,
                        true);
    }
}


namespace
{
// The summary the player screens draw, as one string. Production draws the
// two lines separately (the joined one-liner retired with the global
// CONTROLS screen); these format pins read either line the same way.
std::string joined_control_summary(int player_index)
{
    const std::array<std::string, 2> lines =
        build_player_control_summary_lines(player_index, false);
    if (lines[0].empty() && lines[1].empty())
        return {};
    return lines[0] + " " + lines[1];
}
} // namespace

TEST(MenuLayout, controls_summary_switches_between_four_and_eight_direction_formats)
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_W);
    set_player_key_binding(0, KEY_LEFT, SDLK_A);
    set_player_key_binding(0, KEY_DOWN, SDLK_S);
    set_player_key_binding(0, KEY_RIGHT, SDLK_D);
    set_player_key_binding(0, KEY_YELL, SDLK_Q);
    set_player_key_binding(0, KEY_FIRE, SDLK_1);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_2);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_EQUALS);
    set_player_key_binding(0, KEY_SWITCH, SDLK_GRAVE);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_F8);

    const std::string summary_four = joined_control_summary(0);
    ASSERT_TRUE(summary_four.find("D:WASD") != std::string::npos) << "4-direction summary should include compact direction order";
    ASSERT_TRUE(summary_four.find("Y:Q") != std::string::npos) << "4-direction summary should include yell label";
    ASSERT_TRUE(summary_four.find("F:1") != std::string::npos) << "4-direction summary should include fire key";
    ASSERT_TRUE(summary_four.find("S:2") != std::string::npos) << "4-direction summary should include special key";
    ASSERT_TRUE(summary_four.find("SS:=") != std::string::npos) << "4-direction summary should include special switch key";
    ASSERT_TRUE(summary_four.find("SW:`") != std::string::npos) << "4-direction summary should display backtick character for switch key";
    ASSERT_TRUE(summary_four.find("Sh:F8") != std::string::npos) << "4-direction summary should include shifter key";
    ASSERT_TRUE(summary_four.find("Dir:") == std::string::npos) << "4-direction summary should not include diagonal keys";

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_E);
    set_player_key_binding(0, KEY_DOWN_RIGHT, SDLK_C);
    set_player_key_binding(0, KEY_DOWN_LEFT, SDLK_Z);
    set_player_key_binding(0, KEY_UP_LEFT, SDLK_Q);

    const std::string summary_eight = joined_control_summary(0);
    ASSERT_TRUE(summary_eight.find("D:WEDCXZAQ") != std::string::npos) << "8-direction summary should include compact clockwise direction order";
    ASSERT_TRUE(summary_eight.find("Y:") != std::string::npos) << "8-direction summary should include yell label";
}


TEST(MenuLayout, eight_direction_summary_clockwise_order)
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP, SDLK_W);
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_E);
    set_player_key_binding(0, KEY_RIGHT, SDLK_D);
    set_player_key_binding(0, KEY_DOWN_RIGHT, SDLK_C);
    set_player_key_binding(0, KEY_DOWN, SDLK_X);
    set_player_key_binding(0, KEY_DOWN_LEFT, SDLK_Z);
    set_player_key_binding(0, KEY_LEFT, SDLK_A);
    set_player_key_binding(0, KEY_UP_LEFT, SDLK_Q);
    set_player_key_binding(0, KEY_YELL, SDLK_S);
    set_player_key_binding(0, KEY_FIRE, SDLK_1);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_2);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_EQUALS);
    set_player_key_binding(0, KEY_SWITCH, SDLK_3);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_F8);

    const std::string summary = joined_control_summary(0);
    ASSERT_TRUE(summary.find("D:WEDCXZAQ") != std::string::npos) << "8-direction summary should list keys clockwise from Up";
    ASSERT_TRUE(summary.find("Y:S") != std::string::npos) << "8-direction summary should include yell key";
    ASSERT_TRUE(summary.find("F:1") != std::string::npos) << "8-direction summary should include fire key";
    ASSERT_TRUE(summary.find("S:2") != std::string::npos) << "8-direction summary should include special key";
    ASSERT_TRUE(summary.find("SS:=") != std::string::npos) << "8-direction summary should include special switch key";
    ASSERT_TRUE(summary.find("SW:3") != std::string::npos) << "8-direction summary should include switch key";
    ASSERT_TRUE(summary.find("Sh:F8") != std::string::npos) << "8-direction summary should include shifter key";
}


TEST(MenuLayout, controls_summary_remap_mode_uses_two_lines)
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_UP, SDLK_W);
    set_player_key_binding(0, KEY_LEFT, SDLK_A);
    set_player_key_binding(0, KEY_DOWN, SDLK_S);
    set_player_key_binding(0, KEY_RIGHT, SDLK_D);
    set_player_key_binding(0, KEY_YELL, SDLK_E);
    set_player_key_binding(0, KEY_FIRE, SDLK_LCTRL);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_LALT);
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_TAB);
    set_player_key_binding(0, KEY_SWITCH, SDLK_GRAVE);
    set_player_key_binding(0, KEY_SHIFTER, SDLK_LSHIFT);

    const std::array<std::string, 2> remap_summary = build_player_control_summary_lines(0, true);
    ASSERT_TRUE(remap_summary[0].find("Dir:W/A/S/D") != std::string::npos) << "remap summary first line should contain directional keys";
    ASSERT_TRUE(remap_summary[1].find("Y:E") != std::string::npos) << "remap summary second line should contain action keys";
    ASSERT_TRUE(remap_summary[1].find("SW:`") != std::string::npos) << "remap summary second line should display backtick character";
}


TEST(MenuLayout, controls_summary_shows_look_up_binding)
{
    PlayerControlSnapshotGuard guard(0);

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_LOOKUP, SDLK_V);
    const std::string summary = joined_control_summary(0);
    ASSERT_TRUE(summary.find("L:V") != std::string::npos)
        << "controls summary should show the look-up binding: " << summary;

    // Unbound (the P4 8-direction default) reads as "--", not an empty label.
    set_player_key_binding(0, KEY_LOOKUP, SDLK_UNKNOWN);
    const std::string unbound = joined_control_summary(0);
    ASSERT_TRUE(unbound.find("L:--") != std::string::npos)
        << "unbound look-up should display as --: " << unbound;

    // The action line stays inside the 48-char row the controls screen draws
    // it into (x=30, 6px/char, 320px wide) with typical worst-case names.
    set_player_key_binding(0, KEY_YELL, SDLK_BACKSPACE);       // "Bk"
    set_player_key_binding(0, KEY_FIRE, SDLK_LCTRL);           // "LC"
    set_player_key_binding(0, KEY_SPECIAL, SDLK_LALT);         // "LA"
    set_player_key_binding(0, KEY_SPECIAL_SWITCH, SDLK_TAB);   // "Tab"
    set_player_key_binding(0, KEY_SWITCH, SDLK_RETURN);        // "Return"
    set_player_key_binding(0, KEY_SHIFTER, SDLK_RSHIFT);       // "RS"
    set_player_key_binding(0, KEY_LOOKUP, SDLK_CAPSLOCK);      // "Cap"
    const auto lines = build_player_control_summary_lines(0, false);
    EXPECT_LE(lines[1].size(), 48u)
        << "action summary line must fit the controls row: " << lines[1];
}

// The menus skill's layout-discipline rules, as executable relations: exact
// tables pin a crooked layout just as happily as a straight one (RESET at
// x=218 shipped beside a stack at x=214 with every pin green), so the grid
// itself is asserted here — shared column edges, uniform pitches, and the
// §7.1 cross-screen geometry identity — instead of more absolutes.
TEST(MenuLayout, player_screen_grid_relations_and_cross_screen_identity)
{
    std::vector<button> pause_rows;
    og::ui::materialize_menu_buttons(og::ui::pause_player_menu_screen_spec(),
                                     pause_rows);
    std::vector<button> seat_rows;
    og::ui::materialize_menu_buttons(og::ui::seat_settings_menu_screen_spec_mp(),
                                     seat_rows);
    ASSERT_EQ(og::ui::kPausePlayerButtonCount,
              static_cast<int>(pause_rows.size()));
    ASSERT_EQ(kSeatSettingsButtonCountMP, static_cast<int>(seat_rows.size()));

    const auto& p = pause_rows;
    const auto& s = seat_rows;

    // Column A: DIRECTION, INPUT, and (seat) TEAM share one left edge.
    for (const button* b :
         {&p[og::ui::kPausePlayerModeIndex], &p[og::ui::kPausePlayerInputIndex],
          &s[kSeatSettingsModeIndex], &s[kSeatSettingsInputRowMP],
          &s[kSeatSettingsTeamIndex]})
        EXPECT_EQ(kPlayerScreenColAX, b->x) << b->id;
    // Column B: REMAP and ZOOM share a left edge and end at the panel edge.
    for (const button* b :
         {&p[og::ui::kPausePlayerRemapIndex], &p[og::ui::kPausePlayerZoomIndex],
          &s[kSeatSettingsRemapIndex], &s[kSeatSettingsZoomRowMP]})
    {
        EXPECT_EQ(kPlayerScreenColBX, b->x) << b->id;
        EXPECT_EQ(kPlayerScreenPanelRightX, b->x + b->sizex) << b->id;
    }
    // Column C: RESET and the four HUD rows share left AND right edges.
    for (const button* b :
         {&p[og::ui::kPausePlayerResetIndex],
          &p[og::ui::kPausePlayerHudRadarIndex],
          &p[og::ui::kPausePlayerHudLifeIndex],
          &p[og::ui::kPausePlayerHudFoesIndex],
          &p[og::ui::kPausePlayerHudScoreIndex],
          &s[kSeatSettingsResetIndex], &s[kSeatSettingsHudRadarRowMP],
          &s[kSeatSettingsHudScoreRowMP]})
    {
        EXPECT_EQ(kPlayerScreenColCX, b->x) << b->id;
        EXPECT_EQ(kPlayerScreenColCX + kPlayerScreenColCW, b->x + b->sizex)
            << b->id;
    }
    // Uniform HUD pitch, and the stack is co-terminous with the panel.
    const button& radar = p[og::ui::kPausePlayerHudRadarIndex];
    const button& life = p[og::ui::kPausePlayerHudLifeIndex];
    const button& foes = p[og::ui::kPausePlayerHudFoesIndex];
    const button& score = p[og::ui::kPausePlayerHudScoreIndex];
    EXPECT_EQ(kPlayerScreenHudPitch, life.y - radar.y);
    EXPECT_EQ(kPlayerScreenHudPitch, foes.y - life.y);
    EXPECT_EQ(kPlayerScreenHudPitch, score.y - foes.y);
    EXPECT_EQ(kPlayerScreenHudTopY, radar.y);
    // Co-terminous with the panel: the panel constant is a draw_button
    // INCLUSIVE corner row; button extent y+sizey is EXCLUSIVE, so the same
    // bottom ink row differs by exactly one.
    EXPECT_EQ(kPlayerScreenPanelBottomY + 1, score.y + score.sizey);
    // Band rhythm: band1->band2 gap equals band2->panel gap.
    EXPECT_EQ(kPlayerScreenBand2Y - (kPlayerScreenBand1Y + kPlayerScreenBandH),
              kPlayerScreenPanelTopY -
                  (kPlayerScreenBand2Y + kPlayerScreenBandH));

    // §7.1 identity: every row shared by both screens has IDENTICAL geometry.
    const std::pair<int, int> shared[] = {
        {og::ui::kPausePlayerBackIndex, kSeatSettingsBackIndex},
        {og::ui::kPausePlayerModeIndex, kSeatSettingsModeIndex},
        {og::ui::kPausePlayerRemapIndex, kSeatSettingsRemapIndex},
        {og::ui::kPausePlayerResetIndex, kSeatSettingsResetIndex},
        {og::ui::kPausePlayerInputIndex, kSeatSettingsInputRowMP},
        {og::ui::kPausePlayerZoomIndex, kSeatSettingsZoomRowMP},
        {og::ui::kPausePlayerHudRadarIndex, kSeatSettingsHudRadarRowMP},
        {og::ui::kPausePlayerHudLifeIndex, kSeatSettingsHudLifeRowMP},
        {og::ui::kPausePlayerHudFoesIndex, kSeatSettingsHudFoesRowMP},
        {og::ui::kPausePlayerHudScoreIndex, kSeatSettingsHudScoreRowMP},
        {og::ui::kPausePlayerRemoveIndex, kSeatSettingsRemoveIndex},
    };
    for (const auto& [pi, si] : shared)
    {
        const auto pu = static_cast<std::size_t>(pi);
        const auto su = static_cast<std::size_t>(si);
        EXPECT_EQ(s[su].x, p[pu].x) << p[pu].id;
        EXPECT_EQ(s[su].y, p[pu].y) << p[pu].id;
        EXPECT_EQ(s[su].sizex, p[pu].sizex) << p[pu].id;
        EXPECT_EQ(s[su].sizey, p[pu].sizey) << p[pu].id;
    }
}
