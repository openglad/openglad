#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <openglad/resources/save_data.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>
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

// §2.5 base camp (the reimagined team build): 12 deploy/TRAIN row pairs at
// 12px pitch from y=32, the page cluster top-right, and the bottom command
// strip BACK | HIRE | SCENARIO | NETWORK | GO at y=178. Spec ordinals group
// by kind (dep 0-11, train 12-23, pagers 24/25, strip 26-30) so MenuSpecRow
// args decode positionally; the layout is identical for classic and CTF
// campaigns. Static shape = full page (12 rows), pagers hidden, GO visible.
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
    };
    static const ExpectedButton kExpected[] = {
        {"roster_dep_0", "", 8, 32, 14, 10, MenuNav{.down = 1, .right = 12}, false},
        {"roster_dep_1", "", 8, 44, 14, 10, MenuNav{.up = 0, .down = 2, .right = 13}, false},
        {"roster_dep_2", "", 8, 56, 14, 10, MenuNav{.up = 1, .down = 3, .right = 14}, false},
        {"roster_dep_3", "", 8, 68, 14, 10, MenuNav{.up = 2, .down = 4, .right = 15}, false},
        {"roster_dep_4", "", 8, 80, 14, 10, MenuNav{.up = 3, .down = 5, .right = 16}, false},
        {"roster_dep_5", "", 8, 92, 14, 10, MenuNav{.up = 4, .down = 6, .right = 17}, false},
        {"roster_dep_6", "", 8, 104, 14, 10, MenuNav{.up = 5, .down = 7, .right = 18}, false},
        {"roster_dep_7", "", 8, 116, 14, 10, MenuNav{.up = 6, .down = 8, .right = 19}, false},
        {"roster_dep_8", "", 8, 128, 14, 10, MenuNav{.up = 7, .down = 9, .right = 20}, false},
        {"roster_dep_9", "", 8, 140, 14, 10, MenuNav{.up = 8, .down = 10, .right = 21}, false},
        {"roster_dep_10", "", 8, 152, 14, 10, MenuNav{.up = 9, .down = 11, .right = 22}, false},
        {"roster_dep_11", "", 8, 164, 14, 10, MenuNav{.up = 10, .down = 26, .right = 23}, false},
        {"roster_train_0", "TRAIN", 272, 32, 40, 10, MenuNav{.down = 13, .left = 0}, false},
        {"roster_train_1", "TRAIN", 272, 44, 40, 10, MenuNav{.up = 12, .down = 14, .left = 1}, false},
        {"roster_train_2", "TRAIN", 272, 56, 40, 10, MenuNav{.up = 13, .down = 15, .left = 2}, false},
        {"roster_train_3", "TRAIN", 272, 68, 40, 10, MenuNav{.up = 14, .down = 16, .left = 3}, false},
        {"roster_train_4", "TRAIN", 272, 80, 40, 10, MenuNav{.up = 15, .down = 17, .left = 4}, false},
        {"roster_train_5", "TRAIN", 272, 92, 40, 10, MenuNav{.up = 16, .down = 18, .left = 5}, false},
        {"roster_train_6", "TRAIN", 272, 104, 40, 10, MenuNav{.up = 17, .down = 19, .left = 6}, false},
        {"roster_train_7", "TRAIN", 272, 116, 40, 10, MenuNav{.up = 18, .down = 20, .left = 7}, false},
        {"roster_train_8", "TRAIN", 272, 128, 40, 10, MenuNav{.up = 19, .down = 21, .left = 8}, false},
        {"roster_train_9", "TRAIN", 272, 140, 40, 10, MenuNav{.up = 20, .down = 22, .left = 9}, false},
        {"roster_train_10", "TRAIN", 272, 152, 40, 10, MenuNav{.up = 21, .down = 23, .left = 10}, false},
        {"roster_train_11", "TRAIN", 272, 164, 40, 10, MenuNav{.up = 22, .down = 30, .left = 11}, false},
        {"roster_page_prev", "<", 263, 11, 14, 10, MenuNav{.down = 12, .right = 25}, true},
        {"roster_page_next", ">", 302, 11, 14, 10, MenuNav{.down = 12, .left = 24}, true},
        {"back", "BACK", 8, 178, 44, 18, MenuNav{.up = 11, .right = 27}, false},
        {"hire_troops", "HIRE", 58, 178, 50, 18, MenuNav{.up = 11, .left = 26, .right = 28}, false},
        {"scenario", "SCENARIO", 114, 178, 62, 18, MenuNav{.up = 23, .left = 27, .right = 29}, false},
        {"networking", "NETWORK", 182, 178, 56, 18, MenuNav{.up = 23, .left = 28, .right = 30}, false},
        {"go", "GO", 244, 178, 68, 18, MenuNav{.up = 23, .left = 29}, false},
    };

    for (const char* campaign :
         {"org.openglad.gladiator", "org.openglad.ctf"})
    {
        save.current_campaign = campaign;
        button* buttons = picker_createmenu_buttons();
        const int count = picker_createmenu_button_count();
        ASSERT_EQ(kCreateMenuButtonCount, count)
            << "base camp: 24 roster cells + 2 pagers + 5 strip buttons";
        ASSERT_EQ(31, count);

        for (int i = 0; i < count; ++i)
        {
            const ExpectedButton& want = kExpected[i];
            const button& got = buttons[i];
            EXPECT_EQ(want.id, got.id) << campaign << " index " << i;
            EXPECT_EQ(want.label, got.label) << got.id;
            EXPECT_EQ(want.hidden, got.hidden) << got.id;
            EXPECT_EQ(want.x, got.x) << got.id;
            EXPECT_EQ(want.y, got.y) << got.id;
            EXPECT_EQ(want.w, got.sizex) << got.id;
            EXPECT_EQ(want.h, got.sizey) << got.id;
            EXPECT_EQ(want.nav.up, got.nav.up) << got.id;
            EXPECT_EQ(want.nav.down, got.nav.down) << got.id;
            EXPECT_EQ(want.nav.left, got.nav.left) << got.id;
            EXPECT_EQ(want.nav.right, got.nav.right) << got.id;
            // The 6px/char face budget: floor((w - 8) / 6) chars.
            EXPECT_LE(static_cast<int>(got.label.size()),
                      (got.sizex - 8) / 6)
                << got.id << " '" << got.label << "'";
        }

        // G13 drift pins: spec ordinals == picker_sdl_defs.h constants.
        EXPECT_EQ(kBaseCampTrainBase, 12);
        EXPECT_EQ(kBaseCampPagePrevIndex, 24);
        EXPECT_EQ(kBaseCampPageNextIndex, 25);
        EXPECT_EQ(kCreateMenuBackIndex, 26);
        EXPECT_EQ(kCreateMenuHireIndex, 27);
        EXPECT_EQ(kCreateMenuScenarioIndex, 28);
        EXPECT_EQ(kCreateMenuNetworkingIndex, 29);
        EXPECT_EQ(kCreateMenuGoIndex, 30);

        check_no_overlaps(buttons, count, "createmenu_basecamp");
        check_bounds(buttons, count, "createmenu_basecamp");
        check_nav_closed_and_reachable(buttons, count, kCreateMenuBackIndex,
                                       "createmenu_basecamp");
    }

    save.current_campaign = old_campaign;
    (void)picker_createmenu_buttons();
}

// §2.5 keyboard-nav BFS matrix (pattern b): the per-frame full-graph rewire
// over {page shapes: empty, partial, one full page, two pages (both pages)}
// x {host, joiner (GO hidden)}. Every visible button reachable, no link at
// a hidden one, empty roster seeds the highlight on HIRE.
TEST(MenuLayout, createmenu_basecamp_nav_matrix_keyboard_reachable)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;

    // Snapshot + rebuild the roster per shape.
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[i] = std::move(save.team_list[i]);
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
            save.team_list[i].reset();
        for (int i = 0; i < roster_size; ++i)
        {
            save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
            save.team_list[i]->name = std::format("G{}", i);
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
                        button& train = buttons[kBaseCampTrainBase + r];
                        if (train.nav.down == kCreateMenuGoIndex)
                            train.nav.down = kCreateMenuNetworkingIndex;
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
                              buttons[kBaseCampTrainBase + r].hidden)
                        << variant << " row " << r;
                }
                EXPECT_EQ(expected_visible, visible_rows) << variant;
                // Pagers show exactly when the roster spans pages.
                EXPECT_EQ(roster_size <= kBaseCampRosterRowsPerPage,
                          buttons[kBaseCampPagePrevIndex].hidden)
                    << variant;
            }
        }

        // Empty roster: the rewire seeds the highlight on HIRE (§2.5).
        if (roster_size == 0)
        {
            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = 0;  // roster_dep_0, hidden when empty
            spec.nav.rewire(buttons, count, highlighted);
            EXPECT_EQ(kCreateMenuHireIndex, highlighted)
                << "empty roster seeds the highlight on HIRE";
        }

        og::ui::install_base_camp_state_for_screen(nullptr);
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[i] = std::move(saved_team[i]);
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// SCENARIO subscreen static table: the x=30 column stacks the host-gated
// SET CAMPAIGN / SET LEVEL (their name strips draw alongside) over the
// always-visible VIEW LEVEL | TEAMS | PROGRESS row; BACK sits at (30,170)
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
        {"teams", "TEAMS", 120, 100, 80, 15, MenuNav{.up = 2, .down = 0, .left = 3, .right = 5}},
        {"progress", "PROGRESS", 210, 100, 80, 15, MenuNav{.up = 2, .down = 0, .left = 4}},
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

    // The campaign-name / level-title strips draw from x=116 (32-char clip,
    // 6px/char): they must clear the x=30 button column's right edge.
    EXPECT_GE(116, buttons[kScenarioMenuSetCampaignIndex].x +
                       buttons[kScenarioMenuSetCampaignIndex].sizex);
    EXPECT_LE(116 + 32 * 6, SCREEN_W);

    check_no_overlaps(buttons, count, "scenariomenu");
    check_bounds(buttons, count, "scenariomenu");
    check_nav_closed_and_reachable(buttons, count, kScenarioMenuBackIndex,
                                   "scenariomenu_static");
}

// Host-gating variants for the SCENARIO subscreen: SET CAMPAIGN / SET LEVEL
// hide for joiners and the row's up-links rewire around them.
TEST(MenuLayout, scenariomenu_nav_variants_keyboard_reachable)
{
    for (const bool host_visible : {true, false})
    {
        button* buttons = picker_scenariomenu_buttons();
        const int count = picker_scenariomenu_button_count();
        buttons[kScenarioMenuSetCampaignIndex].hidden = !host_visible;
        buttons[kScenarioMenuSetLevelIndex].hidden = !host_visible;
        picker_wire_scenario_menu_nav(buttons, count, host_visible);
        check_nav_closed_and_reachable(
            buttons, count, kScenarioMenuBackIndex,
            host_visible ? "scenariomenu_host" : "scenariomenu_joiner");
    }
}

// TEAMS subscreen static table: geometry, ids, label budgets, and the
// local-classic default nav encoded in the table.
TEST(MenuLayout, teamsmenu_static_layout)
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

    // All three CTF match settings (host-gated) plus READY (networked-only)
    // start hidden; the local-classic surface is the static default.
    EXPECT_TRUE(buttons[kTeamsMenuCtfTeamsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuCtfCapsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuCtfTroopsIndex].hidden);
    EXPECT_TRUE(buttons[kTeamsMenuReadyIndex].hidden);

    // CTF settings row at the top; troops completes the trio bottom-right.
    EXPECT_EQ(120, buttons[kTeamsMenuCtfTeamsIndex].x);
    EXPECT_EQ(8, buttons[kTeamsMenuCtfTeamsIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCtfTeamsIndex].sizex);
    EXPECT_EQ(210, buttons[kTeamsMenuCtfCapsIndex].x);
    EXPECT_EQ(8, buttons[kTeamsMenuCtfCapsIndex].y);
    EXPECT_EQ(210, buttons[kTeamsMenuCtfTroopsIndex].x);
    EXPECT_EQ(170, buttons[kTeamsMenuCtfTroopsIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCtfTroopsIndex].sizex);

    // JOIN column beside the team rows (y = 32 + 30*t).
    for (int t = 0; t < 4; ++t)
    {
        const button& join = buttons[kTeamsMenuJoinFirstIndex + t];
        EXPECT_EQ(240, join.x);
        EXPECT_EQ(32 + 30 * t, join.y);
        EXPECT_EQ(50, join.sizex);
        EXPECT_EQ(12, join.sizey);
        // "JOIN"/"YOU" within the 50px face budget (8 chars).
        EXPECT_LE(join.label.size(), 8u) << join.label;
    }

    // Per-team member pagers: hidden by default, '>' inside the row band's
    // right edge (the readability bar spans x=8..234), left of the JOIN
    // column at x=240 and below the row's label line (row_y..row_y+6).
    for (int t = 0; t < 4; ++t)
    {
        const button& pager = buttons[kTeamsMenuPageFirstIndex + t];
        EXPECT_TRUE(pager.hidden) << pager.id;
        EXPECT_EQ(219, pager.x) << pager.id;
        EXPECT_EQ(39 + 30 * t, pager.y) << pager.id;
        EXPECT_EQ(14, pager.sizex) << pager.id;
        EXPECT_EQ(12, pager.sizey) << pager.id;
        EXPECT_EQ(">", pager.label) << pager.id;
        // Fully inside the row band: bar bottom is row_y+20 = 39+30t+13.
        EXPECT_LE(pager.x + pager.sizex, 234) << pager.id;
        EXPECT_LE(pager.y + pager.sizey, (32 + 30 * t) + 20) << pager.id;
        // The detail slice (26 chars from x=24) and the p/N indicator
        // (ending at x=217) stay left of the pager face.
        EXPECT_LE(24 + 26 * 6, 217) << "paged detail slice budget";
        EXPECT_LT(217, pager.x) << pager.id;
    }

    // 6px/char budgets: 12 chars on the 80px settings faces, 6 on TEAM >.
    EXPECT_LE(buttons[kTeamsMenuCtfTeamsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuCtfCapsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuCtfTroopsIndex].label.size(), 12u);
    EXPECT_LE(buttons[kTeamsMenuGuyTeamIndex].label.size(), 11u);
    EXPECT_LE(buttons[kTeamsMenuReadyIndex].label.size(), 12u);

    check_no_overlaps(buttons, count, "teamsmenu");
    check_bounds(buttons, count, "teamsmenu");
    check_nav_in_range(buttons, count, "teamsmenu");
}

namespace
{
// Apply a wiring's hidden flags the way sync_teams_menu_visibility does.
void apply_teams_menu_hidden_flags(button* buttons, const TeamsMenuWiring& w)
{
    buttons[kTeamsMenuCtfTeamsIndex].hidden = !w.show_ctf;
    buttons[kTeamsMenuCtfCapsIndex].hidden = !w.show_ctf;
    buttons[kTeamsMenuCtfTroopsIndex].hidden = !w.show_ctf;
    for (int t = 0; t < 4; ++t)
        buttons[kTeamsMenuJoinFirstIndex + t].hidden = !w.join_visible[t];
    buttons[kTeamsMenuGuyPrevIndex].hidden = !w.guy_row;
    buttons[kTeamsMenuGuyNextIndex].hidden = !w.guy_row;
    buttons[kTeamsMenuGuyTeamIndex].hidden = !w.guy_row;
    buttons[kTeamsMenuReadyIndex].hidden = !w.networked;
    for (int t = 0; t < 4; ++t)
        buttons[kTeamsMenuPageFirstIndex + t].hidden = !w.pager_visible[t];
}

// Every nav link must land on a VISIBLE button (nav does not skip hidden
// buttons), and every visible button must be keyboard-reachable from BACK.
void check_teams_menu_wiring(const TeamsMenuWiring& w, const char* variant)
{
    button* buttons = picker_teamsmenu_buttons();
    const int count = picker_teamsmenu_button_count();
    apply_teams_menu_hidden_flags(buttons, w);
    picker_wire_teams_menu_nav(buttons, count, w);

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

// The conditional-visibility matrix: every variant the subscreen can show
// must keep the keyboard nav graph closed over the visible set. Pager flags
// ('>' member pagers, shown only for overflowing detail lines) ride along:
// beside a visible JOIN, as a row's only anchor (join hidden), and chained
// across rows with no joins at all.
TEST(MenuLayout, teamsmenu_nav_variants_keyboard_reachable)
{
    // Local classic: all joins, guy row; big teams 0/1 carry pagers.
    check_teams_menu_wiring(
        TeamsMenuWiring{.guy_row = true,
                        .join_visible = {true, true, true, true},
                        .pager_visible = {true, true, false, false}},
        "local_classic");
    // Local CTF host: settings trio shown, one paged row.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .guy_row = true,
                        .join_visible = {true, true, true, true},
                        .pager_visible = {true, false, false, false}},
        "local_ctf_host");
    // Local CTF host, 2-team map/clamp: a pager on a join-less row (the
    // pager becomes that row's chain anchor).
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .guy_row = true,
                        .join_visible = {true, true, false, false},
                        .pager_visible = {false, false, true, false}},
        "local_ctf_two_teams");
    // Networked joiner, classic: READY shown, guy row hidden, every lobby
    // row paged.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true,
                        .join_visible = {true, true, true, true},
                        .pager_visible = {true, true, true, true}},
        "networked_joiner_classic");
    // Networked CTF host: settings + READY; pager anchors the join-less row.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .networked = true,
                        .join_visible = {true, true, true, false},
                        .pager_visible = {false, false, false, true}},
        "networked_ctf_host");
    // Allied local: joins hidden, guy row + CTF settings shown; the pagers
    // form the whole row-anchor chain.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true, .guy_row = true,
                        .pager_visible = {true, true, false, false}},
        "allied_local_ctf");
    // Allied networked classic: BACK, READY, and one lone pager.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true,
                        .pager_visible = {true, false, false, false}},
        "allied_networked_classic");
    // Empty local roster, classic: joins but no guy row (and no pagers —
    // an empty roster can never overflow a detail line).
    check_teams_menu_wiring(
        TeamsMenuWiring{.join_visible = {true, true, true, true}},
        "local_empty_roster");
    // Degenerate: BACK alone.
    check_teams_menu_wiring(TeamsMenuWiring{}, "back_only");
}


TEST(MenuLayout, control_options_buttons_no_overlap)
{
    button* buttons = picker_control_options_buttons();
    const int count = picker_control_options_button_count();
    check_no_overlaps(buttons, count, "control_options");
    check_bounds(buttons, count, "control_options");
}

// The header text ("Player control modes and key remapping") once started at
// y=24, inside the BACK button's animated highlight box, which overwrote its
// first characters. Pin that it clears the highlight (3px beyond the bevel)
// and still sits above the first player row.
TEST(MenuLayout, control_options_header_clears_back_button_and_player_rows)
{
    button* buttons = picker_control_options_buttons();
    const int count = picker_control_options_button_count();
    ASSERT_GE(count, 2);
    const button& back = buttons[0];
    ASSERT_EQ("controls_back", back.id);
    const button& player1_mode = buttons[1];
    ASSERT_EQ("player1_mode", player1_mode.id);

    constexpr int kHighlightExtent = 3;  // draw_highlight animates 0..3px out
    constexpr int kTextHeight = 8;       // small-font glyph rows + breathing room
    EXPECT_GT(PICKER_CONTROLS_HEADER_Y, back.y + back.sizey + kHighlightExtent)
        << "header must start below the BACK button's highlight box";
    EXPECT_LE(PICKER_CONTROLS_HEADER_Y + kTextHeight, player1_mode.y)
        << "header must finish above the P1 row";
    EXPECT_GE(PICKER_CONTROLS_HEADER_X, 10) << "header stays inside the bevel";
}


TEST(MenuLayout, main_options_nav_indices_in_range)
{
    button* buttons = picker_main_options_buttons();
    const int count = picker_main_options_button_count();
    check_nav_in_range(buttons, count, "main_options");
}


TEST(MenuLayout, control_options_nav_indices_in_range)
{
    button* buttons = picker_control_options_buttons();
    const int count = picker_control_options_button_count();
    check_nav_in_range(buttons, count, "control_options");
}

// The per-effect toggles live in the three FX subscreens; main options keeps
// the sound/graphics settings plus the CONTROLS door and the three stacked
// FX doors. Pin the index contract (main_options() writes labels by index)
// and the nav graph.
TEST(MenuLayout, main_options_index_contract_and_nav)
{
    button* buttons = picker_main_options_buttons();
    const int count = picker_main_options_button_count();
    ASSERT_EQ(9, count)
        << "main options is BACK + Sound + DISPLAY door + CONTROLS door + "
           "sprite sheet + 3 FX doors + RESTORE DEFAULTS";

    static const char* kExpectedIds[] = {
        "options_back",       // 0
        "toggle_sound",       // 1
        "display_settings",   // 2: opens the DISPLAY subscreen
        "gameplay_fx",        // 3: opens the GAMEPLAY FX subscreen
        "restore_defaults",   // 4
        "player_controls",    // 5
        "pick_sprite_sheet",  // 6: label synced by index each frame
        "ui_fx",              // 7: opens the UI FX subscreen
        "graphics_fx",        // 8: opens the GRAPHICS FX subscreen
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
    // The settings/door column stacks at one x at 23px pitch.
    EXPECT_EQ("DISPLAY", buttons[2].label);
    EXPECT_EQ(buttons[2].x, buttons[3].x);
    EXPECT_EQ(buttons[3].x, buttons[7].x);
    EXPECT_EQ(buttons[3].x, buttons[8].x);
    EXPECT_EQ(buttons[2].y + 23, buttons[3].y);
    EXPECT_EQ(buttons[3].y + 23, buttons[7].y);
    EXPECT_EQ(buttons[7].y + 23, buttons[8].y);
    EXPECT_EQ(button_action_id(ButtonAction::OpenDisplaySettings), buttons[2].myfun);

    check_nav_closed_and_reachable(buttons, count, 0, "main_options");
}

// DISPLAY subscreen: the mode / resolution / overscan / zoom / smoothing
// stack. Every face is cfg-derived at 6px/char inside 102px (17 chars);
// "Mode: Borderless" and "Res: 2560x1440" are the widest realistic faces.
TEST(MenuLayout, display_settings_index_contract_and_nav)
{
    button* buttons = picker_display_settings_buttons();
    const int count = picker_display_settings_button_count();
    ASSERT_EQ(7, count)
        << "display settings is BACK + mode + resolution + overscan pair + "
           "zoom + smoothing";

    static const char* kExpectedIds[] = {
        "display_back",        // 0
        "display_mode",        // 1: label synced from graphics/fullscreen
        "display_resolution",  // 2: label synced from graphics/width+height
        "overscan_minus",      // 3
        "overscan_plus",       // 4
        "display_zoom",        // 5: label synced from graphics/zoom
        "display_smoothing",   // 6: label synced from graphics/smoothing
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

// GRAPHICS FX subscreen: unique BACK id + 13 effects/* visual toggles on the
// three-column x=15/115/215 grid (4 full rows at 23px pitch from y=35, plus
// the lone floor-glide toggle on a fifth row). Weather is the single display
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
    };
    button* buttons = picker_graphics_fx_options_buttons();
    const int count = picker_graphics_fx_options_button_count();
    check_fx_options_screen(buttons, count, kExpected, 14, "graphics_fx_options");

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
// five match-rule rows in one centered 140px column on the FX row pitch.
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

    // Every dynamic label across the full value cycles stays within the
    // 140px face budget.
    const int face_width = buttons[kDifficultyMenuDifficultyIndex].sizex;
    ASSERT_EQ(140, face_width);
    SaveData save;
    for (int step = 0; step < 3; ++step)
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
        og::ui::cycle_respawn_mode(save);
        og::ui::cycle_respawn_delay(save);
        og::ui::toggle_permadeath(save);
        og::ui::cycle_generator_rate(save);
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

    const std::string summary_four = build_player_control_summary(0);
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

    const std::string summary_eight = build_player_control_summary(0);
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

    const std::string summary = build_player_control_summary(0);
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
    const std::string summary = build_player_control_summary(0);
    ASSERT_TRUE(summary.find("L:V") != std::string::npos)
        << "controls summary should show the look-up binding: " << summary;

    // Unbound (the P4 8-direction default) reads as "--", not an empty label.
    set_player_key_binding(0, KEY_LOOKUP, SDLK_UNKNOWN);
    const std::string unbound = build_player_control_summary(0);
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
