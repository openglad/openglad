#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include <gtest/gtest.h>
#include <SDL.h>

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

// The y=40 band of the team-build screen is the stable expansion row:
// TEAMS (opens the team-choice & match-settings subscreen) and VIEW LEVEL
// (read-only scenario roster viewer), ALWAYS visible — classic and CTF
// campaigns alike — with static nav links into the VIEW/TRAIN/HIRE row.
TEST(MenuLayout, createmenu_teams_row_geometry_and_nav)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string old_campaign = save.current_campaign;

    for (const char* campaign :
         {"org.openglad.gladiator", "org.openglad.ctf"})
    {
        save.current_campaign = campaign;
        button* buttons = picker_createmenu_buttons();
        const int count = picker_createmenu_button_count();
        ASSERT_EQ(13, count) << "team build is the 11 classic buttons + the "
                                "TEAMS | VIEW LEVEL row";

        const button& teams = buttons[kCreateMenuTeamsIndex];
        const button& viewer = buttons[kCreateMenuViewScenarioIndex];
        ASSERT_EQ("teams", teams.id) << campaign;
        ASSERT_EQ("view_scenario", viewer.id) << campaign;
        EXPECT_FALSE(teams.hidden) << campaign;
        EXPECT_FALSE(viewer.hidden) << campaign;
        EXPECT_EQ("TEAMS", teams.label);
        EXPECT_EQ("VIEW LEVEL", viewer.label);

        EXPECT_EQ(30, teams.x);
        EXPECT_EQ(40, teams.y);
        EXPECT_EQ(80, teams.sizex);
        EXPECT_EQ(15, teams.sizey);
        EXPECT_EQ(120, viewer.x);
        EXPECT_EQ(40, viewer.y);
        EXPECT_EQ(80, viewer.sizex);
        EXPECT_EQ(15, viewer.sizey);

        // Labels respect the menu's classic 12-char/80px face budget.
        EXPECT_LE(teams.label.size(), 12u) << teams.label;
        EXPECT_LE(viewer.label.size(), 12u) << viewer.label;

        // Static nav: down into the VIEW/TRAIN/HIRE row, which links back up
        // (the 210 slot is empty, so HIRE links up to VIEW LEVEL).
        EXPECT_EQ(0, teams.nav.down);
        EXPECT_EQ(12, teams.nav.right);
        EXPECT_EQ(1, viewer.nav.down);
        EXPECT_EQ(11, viewer.nav.left);
        EXPECT_EQ(11, buttons[0].nav.up);
        EXPECT_EQ(12, buttons[1].nav.up);
        EXPECT_EQ(12, buttons[2].nav.up);

        check_no_overlaps(buttons, count, "createmenu_teams_row");
        check_bounds(buttons, count, "createmenu_teams_row");
        check_nav_in_range(buttons, count, "createmenu_teams_row");
    }

    save.current_campaign = old_campaign;
    (void)picker_createmenu_buttons();
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
// must keep the keyboard nav graph closed over the visible set.
TEST(MenuLayout, teamsmenu_nav_variants_keyboard_reachable)
{
    // Local classic: all joins, guy row.
    check_teams_menu_wiring(
        TeamsMenuWiring{.guy_row = true,
                        .join_visible = {true, true, true, true}},
        "local_classic");
    // Local CTF host: settings trio shown.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .guy_row = true,
                        .join_visible = {true, true, true, true}},
        "local_ctf_host");
    // Local CTF host, 2-team map/clamp.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .guy_row = true,
                        .join_visible = {true, true, false, false}},
        "local_ctf_two_teams");
    // Networked joiner, classic: READY shown, guy row hidden.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true,
                        .join_visible = {true, true, true, true}},
        "networked_joiner_classic");
    // Networked CTF host: settings + READY.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true,
                        .networked = true,
                        .join_visible = {true, true, true, false}},
        "networked_ctf_host");
    // Allied local: joins hidden, guy row + CTF settings shown.
    check_teams_menu_wiring(
        TeamsMenuWiring{.show_ctf = true, .guy_row = true},
        "allied_local_ctf");
    // Allied networked classic: only BACK and READY.
    check_teams_menu_wiring(
        TeamsMenuWiring{.networked = true},
        "allied_networked_classic");
    // Empty local roster, classic: joins but no guy row.
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

TEST(MenuLayout, networking_buttons_no_overlap)
{
    button* buttons = picker_networking_buttons();
    const int count = picker_networking_button_count();
    check_no_overlaps(buttons, count, "networking");
    check_bounds(buttons, count, "networking");
    check_nav_in_range(buttons, count, "networking");
}

// Every networking control AND every piece of copy (title, field labels,
// centered instruction lines) must fit inside the enclosing panel frame, so
// nothing writes over the frame edge (regression for the long instruction line
// overrunning the frame's right border).
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

    for (int i = 0; i < count; ++i)
    {
        if (buttons[i].hidden)
            continue;
        fits(buttons[i].x, buttons[i].y, buttons[i].sizex, buttons[i].sizey,
             buttons[i].id.c_str());
    }

    const int title_w = mytext.query_width(std::string_view("NETWORKING"));
    fits(160 - title_w / 2, PICKER_NETWORKING_TITLE_Y, title_w, mytext.sizey,
         "title");

    static constexpr std::array<std::string_view, 4> kFrameFieldLabels{{
        "JOIN IP / HOST", "PORT", "ROOM CODE", "ROOM VALUE",
    }};
    for (std::size_t i = 0; i < kFrameFieldLabels.size(); ++i)
    {
        const button& field = buttons[i + 1];
        const int w = mytext.query_width(kFrameFieldLabels[i]);
        fits(field.x - w - PICKER_NETWORKING_LABEL_GAP,
             field.y + (field.sizey - mytext.sizey) / 2, w, mytext.sizey,
             "field label");
    }

    const auto lines = og::ui::networking_menu_instruction_lines();
    const int pitch = mytext.sizey + 1;
    const int height = lines.empty()
        ? 0
        : mytext.sizey + static_cast<int>(lines.size() - 1) * pitch;
    const int iy = buttons[5].y - PICKER_NETWORKING_INSTRUCTION_GAP - height;
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

    static constexpr std::array<std::string_view, 4> kFieldLabels{{
        "JOIN IP / HOST",
        "PORT",
        "ROOM CODE",
        "ROOM VALUE",
    }};

    for (std::size_t field_index = 0; field_index < kFieldLabels.size(); ++field_index)
    {
        const button& field = buttons[field_index + 1];
        const std::string_view label = kFieldLabels[field_index];
        const int label_w = mytext.query_width(label);
        const int label_h = mytext.sizey;
        const int label_x = field.x - label_w - PICKER_NETWORKING_LABEL_GAP;
        const int label_y = field.y + (field.sizey - mytext.sizey) / 2;

        ASSERT_GE(label_x, 0) << "field label should remain on-screen";
        ASSERT_LE(label_x + label_w, SCREEN_W) << "field label should remain on-screen";

        for (int button_index = 0; button_index < count; ++button_index)
        {
            const button& other = buttons[button_index];
            if (other.hidden)
                continue;
            ASSERT_FALSE(rects_overlap(
                label_x,
                label_y,
                label_w,
                label_h,
                other.x,
                other.y,
                other.sizex,
                other.sizey))
                << "networking field label '" << label
                << "' overlaps button '" << other.id << "'";
        }
    }

    const auto instruction_lines = og::ui::networking_menu_instruction_lines();
    const int instruction_pitch = mytext.sizey + 1;
    const int instruction_height = instruction_lines.empty()
        ? 0
        : mytext.sizey +
            static_cast<int>(instruction_lines.size() - 1) * instruction_pitch;
    const int instruction_y =
        buttons[5].y - PICKER_NETWORKING_INSTRUCTION_GAP - instruction_height;

    ASSERT_GE(instruction_y, 0) << "instruction copy should remain on-screen";

    for (std::size_t line_index = 0; line_index < instruction_lines.size(); ++line_index)
    {
        const std::string_view line = instruction_lines[line_index];
        const int line_w = mytext.query_width(line);
        const int line_h = mytext.sizey;
        const int line_x = 160 - line_w / 2;
        const int line_y = instruction_y + static_cast<int>(line_index) * instruction_pitch;

        ASSERT_LE(line_x + line_w, SCREEN_W) << "instruction copy should remain on-screen";

        for (int button_index = 0; button_index < count; ++button_index)
        {
            const button& other = buttons[button_index];
            if (other.hidden)
                continue;
            ASSERT_FALSE(rects_overlap(
                line_x,
                line_y,
                line_w,
                line_h,
                other.x,
                other.y,
                other.sizex,
                other.sizey))
                << "networking instruction line '" << line
                << "' overlaps button '" << other.id << "'";
        }
    }
}


TEST(MenuLayout, controls_summary_switches_between_four_and_eight_direction_formats)
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
    ASSERT_TRUE(summary_four.find("D:WASD") != std::string::npos) << "4-direction summary should include compact direction order";
    ASSERT_TRUE(summary_four.find("Y:Q") != std::string::npos) << "4-direction summary should include yell label";
    ASSERT_TRUE(summary_four.find("F:1") != std::string::npos) << "4-direction summary should include fire key";
    ASSERT_TRUE(summary_four.find("S:2") != std::string::npos) << "4-direction summary should include special key";
    ASSERT_TRUE(summary_four.find("SS:=") != std::string::npos) << "4-direction summary should include special switch key";
    ASSERT_TRUE(summary_four.find("SW:`") != std::string::npos) << "4-direction summary should display backtick character for switch key";
    ASSERT_TRUE(summary_four.find("Sh:F8") != std::string::npos) << "4-direction summary should include shifter key";
    ASSERT_TRUE(summary_four.find("Dir:") == std::string::npos) << "4-direction summary should not include diagonal keys";

    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_UP_RIGHT, SDLK_e);
    set_player_key_binding(0, KEY_DOWN_RIGHT, SDLK_c);
    set_player_key_binding(0, KEY_DOWN_LEFT, SDLK_z);
    set_player_key_binding(0, KEY_UP_LEFT, SDLK_q);

    const std::string summary_eight = build_player_control_summary(0);
    ASSERT_TRUE(summary_eight.find("D:WEDCXZAQ") != std::string::npos) << "8-direction summary should include compact clockwise direction order";
    ASSERT_TRUE(summary_eight.find("Y:") != std::string::npos) << "8-direction summary should include yell label";
}


TEST(MenuLayout, eight_direction_summary_clockwise_order)
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
    ASSERT_TRUE(remap_summary[0].find("Dir:W/A/S/D") != std::string::npos) << "remap summary first line should contain directional keys";
    ASSERT_TRUE(remap_summary[1].find("Y:E") != std::string::npos) << "remap summary second line should contain action keys";
    ASSERT_TRUE(remap_summary[1].find("SW:`") != std::string::npos) << "remap summary second line should display backtick character";
}
