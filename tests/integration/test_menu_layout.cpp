#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input.h>
#include <openglad/interface/ui/campaign_picker_session.h>
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
    ASSERT_GE(count, 8);

    // The primary action group retains its classic 4px vertical gutter.
    constexpr int kWithinGroupGutter = 4;
    EXPECT_EQ(kWithinGroupGutter,
              buttons[1].y - (buttons[0].y + buttons[0].sizey));

    // LEVEL EDITOR completes the primary-action group at the same 4px gap.
    EXPECT_EQ("level_edit", buttons[2].id);
    EXPECT_EQ(kWithinGroupGutter,
              buttons[2].y - (buttons[1].y + buttons[1].sizey));

    // SETTINGS: two full-width doors named in full, GAME SETTINGS over
    // CLOUD SAVES. DIFFICULTY left this screen for the Base Camp command
    // strip (docs/camp-controls-design.md) and the grey SETTINGS caption
    // that captioned the old narrow pair went with it — so the band it
    // reserved is ordinary canvas again, and the settings group is a plain
    // stack sharing the primary group's column.
    EXPECT_EQ("options", buttons[3].id);
    EXPECT_EQ(buttons[2].x, buttons[3].x);
    EXPECT_EQ(140, buttons[3].sizex);
    EXPECT_EQ("GAME SETTINGS", buttons[3].label);

    // Tightening within groups does not collapse the category breaks. The
    // settings pair is centered in the canvas the caption left behind: the
    // break above it equals the footer break below it (checked at the CLOUD
    // row), so the group reads as floating between the two neighbours rather
    // than clinging to either.
    constexpr int kCategoryBreak = 13;
    EXPECT_EQ(kCategoryBreak, buttons[3].y - (buttons[2].y + buttons[2].sizey));

    // HELP and QUIT are a stable, aligned footer pair.
    EXPECT_EQ("help", buttons[4].id);
    EXPECT_EQ("quit", buttons[5].id);
    EXPECT_EQ(buttons[4].y, buttons[5].y);
    EXPECT_EQ(buttons[4].sizex, buttons[5].sizex);
    EXPECT_EQ(4, buttons[5].x - (buttons[4].x + buttons[4].sizex));

    // #155: the CLOUD door (always visible — reachable with zero companies)
    // is the settings group's second row, the same full width and column as
    // GAME SETTINGS at the group's 4px gutter. Both build variants share the
    // geometry (the tables differ only in the QUIT fork).
    ASSERT_EQ(9, count);
    EXPECT_EQ("cloud", buttons[8].id);
    EXPECT_EQ(buttons[3].x, buttons[8].x);
    EXPECT_EQ(buttons[3].sizex, buttons[8].sizex);
    EXPECT_EQ(15, buttons[8].sizey);
    EXPECT_EQ(kWithinGroupGutter,
              buttons[8].y - (buttons[3].y + buttons[3].sizey));
    // Footer break measured from CLOUD SAVES, the settings group's last row,
    // and equal to the break above GAME SETTINGS — that equality is what
    // centers the pair.
    EXPECT_EQ(kCategoryBreak, buttons[4].y - (buttons[8].y + buttons[8].sizey));
    EXPECT_EQ(3, buttons[8].nav.up) << "cloud links up to GAME SETTINGS";
    EXPECT_EQ(4, buttons[8].nav.down) << "cloud links down to HELP";
    EXPECT_EQ(8, buttons[3].nav.down) << "GAME SETTINGS links down to CLOUD";
    EXPECT_EQ(8, buttons[5].nav.up) << "QUIT links up to CLOUD SAVES";
    EXPECT_EQ(8, buttons[4].nav.up) << "HELP links up to CLOUD SAVES";
    // The wasm E6 contract: two down-steps from the default highlight
    // (continue_game, ordinal 1) must still land on the GAME door.
    EXPECT_EQ(3, buttons[2].nav.down) << "level_edit links down to GAME";
    EXPECT_EQ(2, buttons[1].nav.down) << "continue links down to level_edit";
    // 11 and 13 characters within the 140px faces' 23-char budget.
    EXPECT_EQ("CLOUD SAVES", buttons[8].label);
    EXPECT_LE(buttons[8].label.size() * 6,
              static_cast<std::size_t>(buttons[8].sizex));
    EXPECT_LE(buttons[3].label.size() * 6,
              static_cast<std::size_t>(buttons[3].sizex));
    // No DIFFICULTY door survives on this screen.
    for (int i = 0; i < count; ++i)
        EXPECT_NE("difficulty", buttons[i].id);
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

// #206 zone submenu: static table pins, the empty (null-state) shape,
// and the pattern-b rewire's visibility variants — a partial page and a
// full 24-entry page whose PageModel window shows the pagers.
TEST(MenuLayout, zone_submenu_screen_layout_states_and_nav)
{
    button* buttons = picker_zone_submenu_buttons();
    const int count = picker_zone_submenu_button_count();
    ASSERT_EQ(kZoneSubmenuButtonCount, count);
    check_no_overlaps(buttons, count, "campaign_zone_submenu");
    check_bounds(buttons, count, "campaign_zone_submenu");

    const Sint32 spec_row = button_action_id(ButtonAction::MenuSpecRow);
    for (int r = 0; r < kZoneSubmenuRowsPerPage; ++r)
    {
        EXPECT_EQ(std::format("zone_row_{}", r), buttons[r].id);
        // Rows sit inside the Base Camp panel (the submenu is a room in the
        // camp, not a screen of its own); the rewire re-bands them under
        // the page's narrative lines.
        EXPECT_EQ(kZoneSubmenuRowX, buttons[r].x) << r;
        EXPECT_EQ(kZoneSubmenuRowY0 + kZoneSubmenuRowPitch * r, buttons[r].y)
            << r;
        EXPECT_EQ(kZoneSubmenuRowWidth, buttons[r].sizex) << r;
        EXPECT_EQ(10, buttons[r].sizey) << r;
        EXPECT_EQ(spec_row, buttons[r].myfun) << r;
        EXPECT_EQ(r, buttons[r].arg1) << "MenuSpecRow arg == ordinal";
    }
    EXPECT_EQ("back", buttons[kZoneSubmenuBackIndex].id);
    EXPECT_EQ(10, buttons[kZoneSubmenuBackIndex].x);
    EXPECT_EQ(169, buttons[kZoneSubmenuBackIndex].y);
    EXPECT_EQ(KEYSTATE_ESCAPE, buttons[kZoneSubmenuBackIndex].hotkey);
    EXPECT_EQ("zone_page_prev", buttons[kZoneSubmenuPrevIndex].id);
    EXPECT_EQ("zone_page_next", buttons[kZoneSubmenuNextIndex].id);
    EXPECT_TRUE(buttons[kZoneSubmenuPrevIndex].hidden)
        << "pagers start hidden; the rewire shows them on a multi-page";
    EXPECT_TRUE(buttons[kZoneSubmenuNextIndex].hidden);

    const og::ui::MenuScreenSpec& spec =
        og::ui::zone_submenu_menu_screen_spec();
    ASSERT_NE(nullptr, spec.nav.rewire);
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_TRUE(spec.polls_lobby);
    EXPECT_EQ(static_cast<int>(og::ui::RemoteStartScope::TeamBuildScope),
              static_cast<int>(spec.remote_start));
    EXPECT_STREQ("campaign_zone_submenu", spec.name);

    // Null state: the empty shape — every row and pager hidden, BACK alone.
    og::ui::install_zone_submenu_state_for_screen(nullptr);
    int highlighted = kZoneSubmenuBackIndex;
    spec.nav.rewire(buttons, count, highlighted);
    for (int r = 0; r < kZoneSubmenuRowsPerPage; ++r)
        EXPECT_TRUE(buttons[r].hidden) << "null state hides row " << r;
    EXPECT_TRUE(buttons[kZoneSubmenuPrevIndex].hidden);
    EXPECT_TRUE(buttons[kZoneSubmenuNextIndex].hidden);
    check_nav_closed_and_reachable(buttons, count, kZoneSubmenuBackIndex,
                                   "zone_submenu_empty");

    // Scripted-session variants: a 2-row page (no pagers) and a 24-entry
    // page (the cap) whose 8-row window shows the pagers on every page.
    const std::vector<og::script::PackScript> saved_scripts =
        og::script::pack_scripts();
    og::script::register_pack_script(
        {"test.zonelayout", "zonelayout/scripts/c.lua",
         R"LUA(og.register_campaign_hooks({
  picker_menu = function(page_id)
    if page_id == "big" then
      local entries = {}
      for i = 1, 24 do
        entries[i] = { id = "e" .. i, label = "ENTRY " .. i, kind = "page" }
      end
      return { title = "BIG", entries = entries }
    end
    return { title = "SMALL",
             entries = {
               { id = "a", label = "ALPHA", kind = "page" },
               { id = "big", label = "BIG", kind = "page" },
             } }
  end,
}))LUA"});

    {
        SaveData save;
        og::ui::CampaignPickerSession session(save);
        ASSERT_TRUE(session.open());
        og::ui::ZoneSubmenuScreenState state;
        state.session = &session;
        state.page = og::ui::PageModel::make(
            static_cast<int>(session.page().rows.size()),
            kZoneSubmenuRowsPerPage);
        og::ui::install_zone_submenu_state_for_screen(&state);

        // Partial page: two visible rows, labels composed, no pagers.
        spec.nav.rewire(buttons, count, highlighted);
        EXPECT_FALSE(buttons[0].hidden);
        EXPECT_FALSE(buttons[1].hidden);
        EXPECT_TRUE(buttons[2].hidden);
        // Page rows wear the door marker (the repo's "TEAM >" grammar):
        // a row that opens a page must not look like a row that acts.
        EXPECT_EQ("ALPHA  >", buttons[0].label);
        EXPECT_EQ("BIG  >", buttons[1].label);
        EXPECT_TRUE(buttons[kZoneSubmenuPrevIndex].hidden);
        EXPECT_TRUE(buttons[kZoneSubmenuNextIndex].hidden);
        check_nav_closed_and_reachable(buttons, count,
                                       kZoneSubmenuBackIndex,
                                       "zone_submenu_partial");

        // The 24-entry cap page: full window + pagers on both pages.
        ASSERT_EQ(og::ui::CampaignPickerSession::OutcomeKind::OpenedPage,
                  session.choose(1).kind);
        state.page = og::ui::PageModel::make(
            static_cast<int>(session.page().rows.size()),
            kZoneSubmenuRowsPerPage);
        ASSERT_EQ(24, state.page.item_count);
        spec.nav.rewire(buttons, count, highlighted);
        for (int r = 0; r < kZoneSubmenuRowsPerPage; ++r)
            EXPECT_FALSE(buttons[r].hidden) << "full window row " << r;
        EXPECT_FALSE(buttons[kZoneSubmenuPrevIndex].hidden);
        EXPECT_FALSE(buttons[kZoneSubmenuNextIndex].hidden);
        EXPECT_EQ("ENTRY 1  >", buttons[0].label);
        check_nav_closed_and_reachable(buttons, count,
                                       kZoneSubmenuBackIndex,
                                       "zone_submenu_paged");

        ASSERT_TRUE(state.page.step(2));
        EXPECT_EQ(2, state.page.page) << "24 entries page to exactly 3 windows";
        spec.nav.rewire(buttons, count, highlighted);
        EXPECT_EQ("ENTRY 17  >", buttons[0].label);
        EXPECT_EQ("ENTRY 24  >", buttons[7].label)
            << "the cap page's last window is exactly full";
        check_nav_closed_and_reachable(buttons, count,
                                       kZoneSubmenuBackIndex,
                                       "zone_submenu_lastpage");

        og::ui::install_zone_submenu_state_for_screen(nullptr);
    }

    og::script::clear_pack_scripts();
    for (const og::script::PackScript& script : saved_scripts)
        og::script::register_pack_script(script);
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
// 14px pitch from y=45 (padded grey roster panel (8,28)..(311,160) —
// outside-to-outside with the command strip), the page cluster top-right at
// y=15 beside the relocated line B, and the bottom
// command strip BACK | DIFFICULTY | SCENARIO | NETWORK | GO at y=178 (HIRE
// moved to the roster band header; DIFFICULTY took the slot it left and the
// strip re-gridded so the full word inks — its ordinal is 72, appended past
// the zone band so no established ordinal moved). The TRAIN
// column is DELETED: the TEAM chip (61,y,10,10) cycles team, the row body
// (84,y,214,10) opens training, and ^ at x=301 moves a member up. Spec
// ordinals group by kind (dep
// 0-7, row body 8-15, team chip 16-23, pagers 24/25, scenario-line 26,
// strip 27-31, ready twin 32) so MenuSpecRow args decode positionally. The
// seat-assignment rail is appended at 33-40, followed by move-up controls
// 41-48, preserving every old ordinal. #236 repurposed the rail's ordinals
// in place rather than shifting the table: 33 (the retired SEATS door) is
// the '+' at the rail's left edge and 40 (the '+' it replaced) is a parked
// spare. The layout is identical for classic and CTF campaigns.
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
        {"roster_dep_0", "", 23, 45, 14, 10, MenuNav{.up = 28, .down = 1, .right = 16}, false},
        {"roster_dep_1", "", 23, 59, 14, 10, MenuNav{.up = 0, .down = 2, .right = 17}, false},
        {"roster_dep_2", "", 23, 73, 14, 10, MenuNav{.up = 1, .down = 3, .right = 18}, false},
        {"roster_dep_3", "", 23, 87, 14, 10, MenuNav{.up = 2, .down = 4, .right = 19}, false},
        {"roster_dep_4", "", 23, 101, 14, 10, MenuNav{.up = 3, .down = 5, .right = 20}, false},
        {"roster_dep_5", "", 23, 115, 14, 10, MenuNav{.up = 4, .down = 6, .right = 21}, false},
        {"roster_dep_6", "", 23, 129, 14, 10, MenuNav{.up = 5, .down = 7, .right = 22}, false},
        {"roster_dep_7", "", 23, 143, 14, 10, MenuNav{.up = 6, .down = 27, .right = 23}, false},
        {"roster_row_0", "", 84, 45, 214, 10, MenuNav{.up = 28, .down = 9, .left = 16}, false, true},
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
        {"scenario_line", "", 8, 14, 206, 12, MenuNav{.down = 29, .right = 28}, false, true},
        {"back", "BACK", 8, 178, 44, 18, MenuNav{.up = 7, .right = 72}, false},
        // The roster band header's HIRE (docs/basecamp-zones-design.md):
        // same id/ordinal, relocated beside the pager cluster; the slot it
        // left on the strip is the DIFFICULTY door at ordinal 72.
        {"hire_troops", "HIRE", 220, 14, 34, 12, MenuNav{.down = 0, .left = 26}, false},
        {"scenario", "SCENARIO", 132, 178, 62, 18, MenuNav{.up = 26, .left = 72, .right = 30}, false},
        {"networking", "NETWORK", 200, 178, 56, 18, MenuNav{.up = 15, .left = 29, .right = 31}, false},
        {"go", "GO", 262, 178, 50, 18, MenuNav{.up = 15, .left = 30}, false},
        // §2.6: the READY twin shares GO's exact rect; statically hidden
        // (the rewire shows exactly one of the pair — GO for hosts, READY
        // for networked joiners).
        {"ready", "READY", 262, 178, 50, 18, MenuNav{.up = 15, .left = 30},
         true},
        // The rail is this machine's four seat slots on a fixed grid: four
        // 70px faces at 8/86/164/242, gutter 8, closing on the panel's right
        // rail. Slots carry the static ADD PLAYER label — a slot with no seat
        // in it IS the add button — and the rewire overwrites it with the
        // seat's P#/mapping name once one lands. Ordinals 33/34/39 carried
        // the retired [+] and the two seat pagers; ordinals are append-only,
        // so all four spares park like a zone spare.
        {"seat_rail_spare_0", "", 0, 0, 0, 0, MenuNav{}, true},
        {"seat_rail_spare_1", "", 0, 0, 0, 0, MenuNav{}, true},
        {"seat_card_0", "ADD PLAYER", 8, 164, 70, 10,
         MenuNav{.down = 72, .right = 36}, false},
        {"seat_card_1", "ADD PLAYER", 86, 164, 70, 10,
         MenuNav{.down = 29, .left = 35, .right = 37}, false},
        {"seat_card_2", "ADD PLAYER", 164, 164, 70, 10,
         MenuNav{.down = 30, .left = 36, .right = 38}, false},
        {"seat_card_3", "ADD PLAYER", 242, 164, 70, 10,
         MenuNav{.down = 31, .left = 37}, false},
        {"seat_rail_spare_2", "", 0, 0, 0, 0, MenuNav{}, true},
        {"seat_rail_spare", "", 0, 0, 0, 0, MenuNav{}, true},
        {"roster_up_0", "^", 301, 45, 9, 10,
         MenuNav{.left = 8}, true},
        {"roster_up_1", "^", 301, 59, 9, 10,
         MenuNav{.left = 9}, true},
        {"roster_up_2", "^", 301, 73, 9, 10,
         MenuNav{.left = 10}, true},
        {"roster_up_3", "^", 301, 87, 9, 10,
         MenuNav{.left = 11}, true},
        {"roster_up_4", "^", 301, 101, 9, 10,
         MenuNav{.left = 12}, true},
        {"roster_up_5", "^", 301, 115, 9, 10,
         MenuNav{.left = 13}, true},
        {"roster_up_6", "^", 301, 129, 9, 10,
         MenuNav{.left = 14}, true},
        {"roster_up_7", "^", 301, 143, 9, 10,
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
               "hit zone + HIRE + 4 strip buttons + the hidden READY twin + "
               "4 seat slots + 4 parked rail spares + 8 move-up controls + the 23-row "
               "parked zone band + the appended DIFFICULTY strip door";
        ASSERT_EQ(73, count);
        ASSERT_EQ(static_cast<int>(std::size(kExpected)),
                  kBaseCampZoneActionBase)
            << "the exact table covers the classic ordinals 0..48";

        for (int i = 0; i < static_cast<int>(std::size(kExpected)); ++i)
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
            // The compact rail/move-up band uses the full face; established
            // beveled controls retain their eight-pixel inset budget. The
            // band opens at the first parked rail ordinal (33) and runs to
            // the interior-ring bound, which is what the focus ring uses.
            const int label_budget =
                i < kBaseCampInteriorRingFirstIndex
                ? (got.sizex - 8) / 6
                : got.sizex / 6;
            EXPECT_LE(static_cast<int>(got.label.size()), label_budget)
                << got.id << " '" << got.label << "'";
        }

        // The appended zone band 49..71 (docs/basecamp-zones-design.md
        // "Bounds arithmetic"): statically PARKED — zero-size rects, empty
        // labels, hidden, keyboard-live MenuSpecRow args == ordinals, no
        // static nav — until a scripted composition re-bands them.
        const Sint32 spec_row_action =
            button_action_id(ButtonAction::MenuSpecRow);
        for (int i = kBaseCampZoneActionBase;
             i < kCreateMenuDifficultyIndex; ++i)
        {
            const button& got = buttons[i];
            std::string want_id;
            if (i < kBaseCampZonePagerBase)
            {
                want_id = std::format("zone_action_{}",
                                      i - kBaseCampZoneActionBase);
            }
            else if (i < kBaseCampZoneSpareBase)
            {
                const int pager = i - kBaseCampZonePagerBase;
                want_id = std::format("zone_pager_{}_{}",
                                      pager % 2 == 0 ? "prev" : "next",
                                      pager / 2);
            }
            else
            {
                want_id = std::format("zone_spare_{}",
                                      i - kBaseCampZoneSpareBase);
            }
            EXPECT_EQ(want_id, got.id) << campaign << " index " << i;
            EXPECT_TRUE(got.hidden) << got.id;
            EXPECT_TRUE(got.label.empty()) << got.id;
            EXPECT_EQ(0, got.x) << got.id;
            EXPECT_EQ(0, got.y) << got.id;
            EXPECT_EQ(0, got.sizex) << got.id;
            EXPECT_EQ(0, got.sizey) << got.id;
            EXPECT_EQ(spec_row_action, got.myfun) << got.id;
            EXPECT_EQ(i, got.arg1) << "MenuSpecRow arg == ordinal " << got.id;
            EXPECT_EQ(-1, got.nav.up) << got.id;
            EXPECT_EQ(-1, got.nav.down) << got.id;
            EXPECT_EQ(-1, got.nav.left) << got.id;
            EXPECT_EQ(-1, got.nav.right) << got.id;
        }

        // The appended DIFFICULTY strip door at ordinal 72: a real
        // ButtonAction like its strip peers, always visible, drawn, on the
        // strip's y and height, and inking its full word at the beveled
        // budget ((sizex - 8) / 6 == 10 glyphs for exactly "DIFFICULTY").
        {
            const button& diff = buttons[kCreateMenuDifficultyIndex];
            EXPECT_EQ("difficulty", diff.id);
            EXPECT_EQ("DIFFICULTY", diff.label);
            EXPECT_FALSE(diff.hidden);
            EXPECT_FALSE(diff.no_draw);
            EXPECT_EQ(58, diff.x);
            EXPECT_EQ(178, diff.y);
            EXPECT_EQ(68, diff.sizex);
            EXPECT_EQ(18, diff.sizey);
            EXPECT_EQ(button_action_id(ButtonAction::OpenDifficultyMenu),
                      diff.myfun);
            EXPECT_EQ(-1, diff.arg1);
            EXPECT_EQ(7, diff.nav.up);
            EXPECT_EQ(-1, diff.nav.down);
            EXPECT_EQ(kCreateMenuBackIndex, diff.nav.left);
            EXPECT_EQ(kCreateMenuScenarioIndex, diff.nav.right);
            EXPECT_EQ(10, static_cast<int>(diff.label.size()));
            EXPECT_LE(static_cast<int>(diff.label.size()),
                      (diff.sizex - 8) / 6)
                << "the full word must ink inside the bevel";
        }

        // The re-gridded strip: five doors, one 6px gutter, closing flush on
        // the panel's right rail. Read as relations so a future width edit
        // cannot quietly reopen a hole.
        {
            const int strip[] = {kCreateMenuBackIndex,
                                 kCreateMenuDifficultyIndex,
                                 kCreateMenuScenarioIndex,
                                 kCreateMenuNetworkingIndex,
                                 kCreateMenuGoIndex};
            for (const int i : strip) {
                EXPECT_EQ(178, buttons[i].y) << buttons[i].id;
                EXPECT_EQ(18, buttons[i].sizey) << buttons[i].id;
            }
            for (std::size_t s = 1; s < std::size(strip); ++s) {
                const button& left = buttons[strip[s - 1]];
                const button& right = buttons[strip[s]];
                EXPECT_EQ(6, right.x - (left.x + left.sizex))
                    << "strip gutter before " << right.id;
            }
            EXPECT_EQ(312, buttons[kCreateMenuGoIndex].x +
                               buttons[kCreateMenuGoIndex].sizex);
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
        EXPECT_EQ(kBaseCampAddSeatIndex, 33);
        EXPECT_EQ(kBaseCampSeatPagePrevIndex, 34);
        EXPECT_EQ(kBaseCampSeatCardBase, 35);
        EXPECT_EQ(kBaseCampSeatPageNextIndex, 39);
        EXPECT_EQ(kBaseCampSeatRailSpareIndex, 40);
        EXPECT_EQ(kBaseCampInteriorRingFirstIndex, 33);
        EXPECT_EQ(kBaseCampMoveUpBase, 41);
        EXPECT_EQ(kBaseCampInteriorRingLastIndex, 48);
        EXPECT_EQ(kBaseCampZoneActionBase, 49);
        EXPECT_EQ(kBaseCampZoneActionsPerWidget, 8);
        EXPECT_EQ(kBaseCampZoneActionRows, 16);
        EXPECT_EQ(kBaseCampZonePagerBase, 65);
        EXPECT_EQ(kBaseCampZonePagerCount, 4);
        EXPECT_EQ(kBaseCampZoneSpareBase, 69);
        EXPECT_EQ(kBaseCampZoneSpareCount, 3);
        EXPECT_EQ(kCreateMenuDifficultyIndex, 72);
        EXPECT_EQ(kCreateMenuButtonCount, 73);
        EXPECT_EQ(MAX_BUTTONS, 73);
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
                  buttons[kBaseCampSeatCardBase].x)
            << "the rail's first slot and BACK share the base-camp left "
               "alignment line";
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

        // One gutter across the whole rail, from the first slot on BACK's
        // left edge to the last closing the right rail. Every slot is the
        // same width: the face IS the label budget, so a wider slot would
        // silently mean a longer label on one card than on its neighbour.
        for (int slot = 0; slot < kBaseCampSeatCardsPerPage; ++slot)
        {
            const button& face = buttons[kBaseCampSeatCardBase + slot];
            EXPECT_EQ(8 + 78 * slot, face.x) << "seat slot " << slot;
            EXPECT_EQ(70, face.sizex) << "seat slot " << slot;
            EXPECT_EQ(buttons[kBaseCampSeatCardBase].y, face.y)
                << "seat rail baseline at " << face.id;
            EXPECT_EQ(buttons[kBaseCampSeatCardBase].sizey, face.sizey)
                << "seat rail height at " << face.id;
            if (slot > 0)
            {
                const button& left = buttons[kBaseCampSeatCardBase + slot - 1];
                EXPECT_EQ(8, face.x - (left.x + left.sizex))
                    << "seat rail gutter after " << left.id;
            }
        }
        EXPECT_EQ(312,
                  buttons[kBaseCampSeatCardBase + kBaseCampSeatCardsPerPage - 1]
                          .x +
                      buttons[kBaseCampSeatCardBase +
                              kBaseCampSeatCardsPerPage - 1]
                          .sizex)
            << "the seat rail closes on the panel's right rail";
        // The four retired ordinals keep their slots in the 73-button table
        // and nothing else: a parked spare cannot be clicked or navigated to.
        for (const int spare : kBaseCampSeatRailSpares)
        {
            EXPECT_TRUE(buttons[spare].hidden) << "spare " << spare;
            EXPECT_EQ(0, buttons[spare].sizex) << "spare " << spare;
            EXPECT_EQ(0, buttons[spare].sizey) << "spare " << spare;
            EXPECT_EQ(-1, buttons[spare].nav.up) << "spare " << spare;
            EXPECT_EQ(-1, buttons[spare].nav.down) << "spare " << spare;
            EXPECT_EQ(-1, buttons[spare].nav.left) << "spare " << spare;
            EXPECT_EQ(-1, buttons[spare].nav.right) << "spare " << spare;
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

    // (a) The right rail is ONE column: every control OUTSIDE the roster
    // panel ends on GO's right edge, which is the panel's OUTER right edge
    // (outer bevel 8..311, outside-to-outside with the command strip).
    const button& go = buttons[kCreateMenuGoIndex];
    const int rail_right = go.x + go.sizex;
    EXPECT_EQ(312, rail_right) << "GO closes the panel's outer right edge";
    const std::vector<int> rail{
        kBaseCampPageNextIndex,
        kBaseCampSeatCardBase + kBaseCampSeatCardsPerPage - 1,
        kCreateMenuReadyIndex};
    for (const int index : rail)
    {
        const button& b = buttons[index];
        EXPECT_EQ(rail_right, b.x + b.sizex)
            << b.id << " must co-terminate with GO on the right rail";
    }
    // The per-row '^' lives INSIDE the panel: it co-terminates with the
    // panel's inner grey face — GO's right edge minus the panel's 2px bevel
    // inset.
    for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
    {
        const button& b = buttons[kBaseCampMoveUpBase + r];
        EXPECT_EQ(rail_right - 2, b.x + b.sizex)
            << b.id << " must hug the panel's inner face (outer edge minus "
               "the 2px bevel)";
    }

    // (b) The seat slots are one uniform run: equal widths, equal pitch, one
    // baseline. Retiring the [+] and the two pagers gave the four slots the
    // whole row, widening the pitch from 64 (a 57px card plus a 7px gutter)
    // to 78 — a 70px face plus 8.
    const button& first_slot = buttons[kBaseCampSeatCardBase];
    EXPECT_EQ(buttons[kCreateMenuBackIndex].x, first_slot.x)
        << "the rail opens on BACK's left edge";
    for (int card = 0; card + 1 < kBaseCampSeatCardsPerPage; ++card)
    {
        const button& left = buttons[kBaseCampSeatCardBase + card];
        const button& right = buttons[kBaseCampSeatCardBase + card + 1];
        EXPECT_EQ(78, right.x - left.x) << "seat slot pitch at slot " << card;
        EXPECT_EQ(left.sizex + 8, right.x - left.x)
            << "the slot pitch is the slot face plus the rail gutter";
        EXPECT_EQ(left.sizex, right.sizex)
            << "seat slot width at slot " << card;
        EXPECT_EQ(left.y, right.y) << "seat slot baseline at slot " << card;
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
    // HIRE rides the roster header band: right-aligned before the pager
    // cluster with a 4px gutter, clear of the scenario_line hit zone.
    const button& hire = buttons[kCreateMenuHireIndex];
    EXPECT_EQ(prev.x - 4, hire.x + hire.sizex)
        << "HIRE co-terminates 4px before the pager cluster";
    const button& scen_line = buttons[kBaseCampScenarioLineIndex];
    EXPECT_LT(scen_line.x + scen_line.sizex, hire.x)
        << "HIRE clears the scenario_line hit zone";

    // HIRE's face (y=14..25) overlaps header line B's strip (y=16..23), so
    // its left edge IS the line-B wall while a composition shows it. Tie
    // the composer's character budget to the LIVE button rect: relocating
    // HIRE without re-deriving the budget must fail here, not silently
    // amputate a networked status line under the button.
    EXPECT_LT(hire.y, 17 + 7);
    EXPECT_GT(hire.y + hire.sizey, 17 - 1)
        << "HIRE and line B share the header band";
    EXPECT_EQ(hire.x, og::ui::kBaseCampLineBHireWallX);
    EXPECT_EQ(prev.x, og::ui::kBaseCampLineBPagerWallX);
    const auto ink_right_edge = [](int chars) {
        return og::ui::kBaseCampLineBTextX +
            og::ui::kBaseCampLineBGlyphAdvance * chars +
            og::ui::kBaseCampLineBStripPad;
    };
    EXPECT_LE(ink_right_edge(og::ui::kBaseCampLineBCharsHireVisible), hire.x)
        << "the line-B budget beside HIRE must clear the button face";
    EXPECT_GT(ink_right_edge(og::ui::kBaseCampLineBCharsHireVisible + 1),
              hire.x)
        << "the budget must be the widest that clears HIRE, not a "
           "conservative guess";
    EXPECT_LE(ink_right_edge(og::ui::kBaseCampLineBCharsHireHidden), prev.x)
        << "the HIRE-hidden budget must clear the roster pager cluster";
    EXPECT_GT(ink_right_edge(og::ui::kBaseCampLineBCharsHireHidden + 1),
              prev.x);
}

// The scripted gameplay zone (docs/basecamp-zones-design.md): a synthetic
// composition adopted straight into the session (no Lua) drives the
// production rewire — the parked action rows re-band into their widgets'
// whole-row-unit bands, the roster's controls re-band below them, the
// capability gates hide the classic sub-rows, the widget pagers window an
// overflowing actions widget, and the whole graph stays closed + reachable.
TEST(MenuLayout, createmenu_basecamp_scripted_zone_bands_and_nav)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    for (int i = 0; i < 4; ++i)
    {
        save.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(i)]->name =
            std::format("Z{}", i);
    }
    save.team_size = 4;

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);

    // readout (hoisted into the panel's header band, 0 units) + text
    // (1 line -> 1) + actions (weight 2, 5 entries -> paged) + roster
    // (header 1 + rows 4) = 8 units.
    og::script::hooks::CampaignZone raw;
    {
        og::script::hooks::CampaignZoneWidget readout;
        readout.kind = og::script::hooks::CampaignZoneWidget::Kind::Readout;
        readout.items.push_back({"COIN", "1000"});
        raw.widgets.push_back(readout);
        og::script::hooks::CampaignZoneWidget text;
        text.kind = og::script::hooks::CampaignZoneWidget::Kind::Text;
        text.lines = {"The camp fire crackles."};
        raw.widgets.push_back(text);
        og::script::hooks::CampaignZoneWidget actions;
        actions.kind = og::script::hooks::CampaignZoneWidget::Kind::Actions;
        actions.weight = 2;
        for (int i = 0; i < 5; ++i)
        {
            og::script::hooks::CampaignPageEntry entry;
            entry.id = std::format("act{}", i);
            entry.label = std::format("ACT {}", i);
            entry.kind = og::script::hooks::CampaignPageEntry::Kind::Action;
            actions.entries.push_back(std::move(entry));
        }
        raw.widgets.push_back(actions);
        og::script::hooks::CampaignZoneWidget roster;
        roster.kind = og::script::hooks::CampaignZoneWidget::Kind::Roster;
        roster.can_reorder = false;
        roster.can_team = false;
        raw.widgets.push_back(roster);
    }
    og::ui::CampaignZoneSession zone(save);
    ASSERT_TRUE(zone.adopt(raw));
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_NE(nullptr, zone.readout());
    EXPECT_TRUE(zone.readout()->in_header_band);
    EXPECT_EQ(1, zone.actions()[0].start_unit);
    EXPECT_EQ(3, zone.roster().start_unit);
    EXPECT_EQ(4, zone.roster().row_start_unit);
    EXPECT_EQ(4, zone.roster().rows_per_page);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(4, state.page.rows_per_page)
        << "the roster window derives from the zone band";
    og::ui::install_base_camp_state_for_screen(&state);

    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    int highlighted = kBaseCampRowBodyBase;
    spec.nav.rewire(buttons, count, highlighted);

    // The actions band: 2 visible windows of the 5 entries at the band's
    // units, labels composed, pagers shown on the band's first row.
    EXPECT_FALSE(buttons[kBaseCampZoneActionBase].hidden);
    EXPECT_FALSE(buttons[kBaseCampZoneActionBase + 1].hidden);
    EXPECT_TRUE(buttons[kBaseCampZoneActionBase + 2].hidden)
        << "a 2-unit band shows 2 window rows";
    EXPECT_EQ("ACT 0", buttons[kBaseCampZoneActionBase].label);
    EXPECT_EQ("ACT 1", buttons[kBaseCampZoneActionBase + 1].label);
    EXPECT_EQ(12, buttons[kBaseCampZoneActionBase].x);
    EXPECT_EQ(45 + 14 * 1, buttons[kBaseCampZoneActionBase].y)
        << "the band anchors on its start unit";
    EXPECT_EQ(45 + 14 * 2, buttons[kBaseCampZoneActionBase + 1].y);
    EXPECT_FALSE(buttons[kBaseCampZonePagerBase].hidden)
        << "5 entries over 2 rows page in place";
    EXPECT_FALSE(buttons[kBaseCampZonePagerBase + 1].hidden);
    EXPECT_EQ("<", buttons[kBaseCampZonePagerBase].label);
    EXPECT_EQ(">", buttons[kBaseCampZonePagerBase + 1].label);
    EXPECT_EQ(310, buttons[kBaseCampZonePagerBase + 1].x +
                       buttons[kBaseCampZonePagerBase + 1].sizex)
        << "the widget pagers close the panel's inner right rail";
    // The second widget's pager pair stays parked.
    EXPECT_TRUE(buttons[kBaseCampZonePagerBase + 2].hidden);
    EXPECT_TRUE(buttons[kBaseCampZonePagerBase + 3].hidden);

    // The roster re-bands below the actions: rows at the band's units, the
    // capability gates hide the chip/move-up columns.
    for (int r = 0; r < 4; ++r)
    {
        EXPECT_FALSE(buttons[r].hidden) << "roster row " << r;
        EXPECT_EQ(45 + 14 * (4 + r), buttons[r].y) << "roster row " << r;
        EXPECT_EQ(45 + 14 * (4 + r),
                  buttons[kBaseCampRowBodyBase + r].y);
        EXPECT_TRUE(buttons[kBaseCampTeamChipBase + r].hidden)
            << "can_team=false hides the chip column";
        EXPECT_TRUE(buttons[kBaseCampMoveUpBase + r].hidden)
            << "can_reorder=false hides the move-up column";
    }
    for (int r = 4; r < kBaseCampRosterRowsPerPage; ++r)
    {
        EXPECT_TRUE(buttons[r].hidden)
            << "rows past the band stay hidden " << r;
    }

    check_no_overlaps(buttons, count, "basecamp_scripted_zone");
    check_bounds(buttons, count, "basecamp_scripted_zone");
    check_nav_closed_and_reachable(buttons, count, kCreateMenuBackIndex,
                                   "basecamp_scripted_zone");

    // The spine: HIRE drops into the topmost interactive band (the actions
    // rows), the actions band chains into the roster, and the roster
    // bottoms out on the rail.
    EXPECT_EQ(kBaseCampZoneActionBase,
              buttons[kCreateMenuHireIndex].nav.down);
    EXPECT_EQ(kCreateMenuHireIndex,
              buttons[kBaseCampZoneActionBase].nav.up);
    EXPECT_EQ(0, buttons[kBaseCampZoneActionBase + 1].nav.down)
        << "the band's last row drops onto the roster's dep column";
    EXPECT_EQ(kBaseCampZoneActionBase + 1, buttons[0].nav.up);
    EXPECT_EQ(kBaseCampSeatCardBase, buttons[3].nav.down)
        << "the roster's last row bottoms out on the seat rail";

    // Pager stepping through the production dispatch windows the band.
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampZonePagerBase + 1, &state));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("ACT 2", buttons[kBaseCampZoneActionBase].label)
        << "the widget's own pager steps its window";

    // A capability-gated composition with a frozen assign shows chips on
    // own rows even networked (the visibility fork is pinned by the zone
    // UI flows; here: assign-active solo keeps the chip column visible
    // while can_team=false would have hidden it).
    raw.widgets.back().can_team = false;
    raw.widgets.back().assign.active = true;
    raw.widgets.back().assign.key = "road";
    raw.widgets.back().assign.labels = {"WAR", "BURDEN"};
    ASSERT_TRUE(zone.adopt(raw));
    buttons = picker_createmenu_buttons();
    spec.nav.rewire(buttons, count, highlighted);
    for (int r = 0; r < 3; ++r)
    {
        EXPECT_FALSE(buttons[kBaseCampTeamChipBase + r].hidden)
            << "an active assign shows the chip column on own rows " << r;
    }
    check_nav_closed_and_reachable(buttons, count, kCreateMenuBackIndex,
                                   "basecamp_scripted_zone_assign");

    og::ui::install_base_camp_state_for_screen(nullptr);
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// The spine with BOTH actions widgets on one side of the roster, and with
// no roster rows at all (an empty company is a real camp — you hire your
// first hero here): the bands sort by start unit and chain top-to-bottom,
// and every roster-less shape still lands its exits on HIRE, the other
// band, or the seat rail.
TEST(MenuLayout, createmenu_basecamp_zone_two_bands_and_empty_company_spine)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    const auto set_team = [&save](int size) {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        {
            if (i < size)
            {
                save.team_list[static_cast<std::size_t>(i)] =
                    std::make_unique<guy>(FAMILY_SOLDIER);
                save.team_list[static_cast<std::size_t>(i)]->name =
                    std::format("S{}", i);
            }
            else
            {
                save.team_list[static_cast<std::size_t>(i)].reset();
            }
        }
        save.team_size = static_cast<unsigned char>(size);
    };

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);

    // Three widget orders: both actions widgets above the roster, both
    // below, and one on each side. Each actions widget is one unit / one
    // entry, so its band is a single row (top == bottom).
    const auto one_row_actions = [](const char* id, const char* label) {
        og::script::hooks::CampaignZoneWidget actions;
        actions.kind = og::script::hooks::CampaignZoneWidget::Kind::Actions;
        actions.weight = 1;
        og::script::hooks::CampaignPageEntry entry;
        entry.id = id;
        entry.label = label;
        entry.kind = og::script::hooks::CampaignPageEntry::Kind::Action;
        actions.entries.push_back(std::move(entry));
        return actions;
    };
    const auto roster_widget = [] {
        og::script::hooks::CampaignZoneWidget roster;
        roster.kind = og::script::hooks::CampaignZoneWidget::Kind::Roster;
        return roster;
    };
    og::script::hooks::CampaignZone both_above;
    both_above.widgets.push_back(one_row_actions("first", "FIRST"));
    both_above.widgets.push_back(one_row_actions("second", "SECOND"));
    both_above.widgets.push_back(roster_widget());
    og::script::hooks::CampaignZone both_below;
    both_below.widgets.push_back(roster_widget());
    both_below.widgets.push_back(one_row_actions("first", "FIRST"));
    both_below.widgets.push_back(one_row_actions("second", "SECOND"));
    og::script::hooks::CampaignZone sandwich;
    sandwich.widgets.push_back(one_row_actions("first", "FIRST"));
    sandwich.widgets.push_back(roster_widget());
    sandwich.widgets.push_back(one_row_actions("second", "SECOND"));

    og::ui::CampaignZoneSession zone(save);
    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::install_base_camp_state_for_screen(&state);
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    const auto rewire = [&](const og::script::hooks::CampaignZone& raw,
                            const char* variant) {
        ASSERT_TRUE(zone.adopt(raw)) << variant;
        ASSERT_EQ(2u, zone.actions().size()) << variant;
        og::ui::base_camp_refresh_rows(state);
        buttons = picker_createmenu_buttons();
        int highlighted = kCreateMenuBackIndex;
        spec.nav.rewire(buttons, count, highlighted);
        check_nav_closed_and_reachable(buttons, count, kCreateMenuBackIndex,
                                       variant);
    };
    const int band0 = kBaseCampZoneActionBase;
    const int band1 = kBaseCampZoneActionBase + kBaseCampZoneActionsPerWidget;

    // Both bands above a manned roster: HIRE -> first -> second -> roster.
    set_team(4);
    rewire(both_above, "zone_two_bands_above");
    EXPECT_EQ(band0, buttons[kCreateMenuHireIndex].nav.down);
    EXPECT_EQ(kCreateMenuHireIndex, buttons[band0].nav.up);
    EXPECT_EQ(band1, buttons[band0].nav.down)
        << "stacked bands chain in start-unit order";
    EXPECT_EQ(band0, buttons[band1].nav.up);
    EXPECT_EQ(0, buttons[band1].nav.down)
        << "the lower band drops onto the roster's dep column";

    // Both bands below: roster -> first -> second -> seat rail.
    rewire(both_below, "zone_two_bands_below");
    EXPECT_EQ(band0, buttons[3].nav.down)
        << "the roster's last row drops into the upper band";
    EXPECT_EQ(3, buttons[band0].nav.up);
    EXPECT_EQ(band1, buttons[band0].nav.down);
    EXPECT_EQ(band0, buttons[band1].nav.up);
    EXPECT_EQ(kBaseCampSeatCardBase, buttons[band1].nav.down)
        << "the bottom band lands on the seat rail";

    // An empty company between two bands: the bands bridge straight across
    // the rowless roster.
    set_team(0);
    rewire(sandwich, "zone_empty_company_sandwich");
    EXPECT_EQ(band0, buttons[kCreateMenuHireIndex].nav.down);
    EXPECT_EQ(band1, buttons[band0].nav.down)
        << "the band above bridges over an empty roster";
    EXPECT_EQ(band0, buttons[band1].nav.up)
        << "the band below climbs back over the empty roster";
    EXPECT_EQ(kBaseCampSeatCardBase, buttons[band1].nav.down);

    // Empty company, both bands below: the spine opens on the first band
    // and its top row climbs to HIRE.
    rewire(both_below, "zone_empty_company_below");
    EXPECT_EQ(band0, buttons[kCreateMenuHireIndex].nav.down);
    EXPECT_EQ(kCreateMenuHireIndex, buttons[band0].nav.up)
        << "with no roster rows the first band's up-exit is HIRE";
    EXPECT_EQ(band1, buttons[band0].nav.down);
    EXPECT_EQ(kBaseCampSeatCardBase, buttons[band1].nav.down);

    // Empty company, both bands above: the last band bottoms out on the
    // seat rail.
    rewire(both_above, "zone_empty_company_above");
    EXPECT_EQ(band0, buttons[kCreateMenuHireIndex].nav.down);
    EXPECT_EQ(band1, buttons[band0].nav.down);
    EXPECT_EQ(kBaseCampSeatCardBase, buttons[band1].nav.down)
        << "no roster rows and nothing below: the band exits to the rail";

    og::ui::install_base_camp_state_for_screen(nullptr);
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// A text widget's band is a HARD boundary. Its explicit weight may be
// SMALLER than ceil(lines*8/14) — the parser refuses over-weight, never
// under-weight, so an under-weighted stanza is a legal composition — and the
// draw must then CLIP rather than ink over the rows the next widget owns.
// This pins the arithmetic the draw runs against the real 14px grid, plus
// the teeth: the same fixture unclipped would have painted into the roster.
TEST(MenuLayout, basecamp_zone_text_ink_clips_to_its_own_band)
{
    namespace hooks = og::script::hooks;
    using og::ui::CampaignZoneSession;
    // The Base Camp row grid the zone lays out on (menu_screen_specs.cpp
    // kBaseCampRowY0; its pitch is static_asserted against kZoneRowPitch).
    constexpr int kRowY0 = 45;

    // A DEFAULT share never clips: whatever the line count, the units the
    // layout hands an unweighted widget hold every one of its lines. The
    // clip therefore only ever fires on a deliberate under-weight.
    for (int lines = 1; lines <= hooks::kCampaignZoneMaxTextLines; ++lines)
    {
        const int units =
            (lines * CampaignZoneSession::kTextLinePitch +
             CampaignZoneSession::kZoneRowPitch - 1) /
            CampaignZoneSession::kZoneRowPitch;
        EXPECT_GE(CampaignZoneSession::text_lines_in_band(units), lines)
            << lines << " lines must all fit their default share";
    }

    SaveData save;
    save.current_campaign = "gladiator";
    save.my_team = 0;

    // One unit for six lines: the widget asks for 14px and wants 48.
    hooks::CampaignZone raw;
    {
        hooks::CampaignZoneWidget text;
        text.kind = hooks::CampaignZoneWidget::Kind::Text;
        text.weight = 1;
        for (int i = 0; i < hooks::kCampaignZoneMaxTextLines; ++i)
            text.lines.push_back(std::format("stanza line {}", i));
        raw.widgets.push_back(std::move(text));
        raw.widgets.emplace_back();  // roster: takes the remaining band
    }
    og::ui::CampaignZoneSession zone(save);
    ASSERT_TRUE(zone.adopt(raw));
    ASSERT_EQ(1u, zone.texts().size());
    const og::ui::CampaignZoneSession::TextLayout& band = zone.texts()[0];
    EXPECT_EQ(0, band.start_unit);
    EXPECT_EQ(1, band.units) << "the explicit weight is honored as-is";

    const int drawn = CampaignZoneSession::text_lines_in_band(band.units);
    ASSERT_GT(drawn, 0) << "a one-unit band still shows its first line";
    ASSERT_LT(drawn, static_cast<int>(band.lines.size()))
        << "the fixture must actually engage the clip";

    const int band_y = kRowY0 + CampaignZoneSession::kZoneRowPitch *
                                    band.start_unit;
    const int band_bottom =
        kRowY0 + CampaignZoneSession::kZoneRowPitch *
                     (band.start_unit + band.units);
    const int ink_bottom =
        band_y + drawn * CampaignZoneSession::kTextLinePitch;
    EXPECT_LE(ink_bottom, band_bottom)
        << "the clipped ink escapes its own band";

    // The band below belongs to the roster (its header unit, then its rows):
    // the clipped ink must stop at or before it, and the UNCLIPPED draw
    // would not have.
    const int roster_y =
        kRowY0 + CampaignZoneSession::kZoneRowPitch * zone.roster().start_unit;
    EXPECT_LE(ink_bottom, roster_y) << "text ink overpaints the roster band";
    const int unclipped_bottom =
        band_y + static_cast<int>(band.lines.size()) *
                     CampaignZoneSession::kTextLinePitch;
    EXPECT_GT(unclipped_bottom, roster_y)
        << "the fixture no longer proves the clip does anything";
}

namespace
{
// Every pixel index of a rect, straight off the composed canvas.
std::vector<int> snapshot_canvas_rect(int x0, int y0, int x1, int y1)
{
    screen* const game = og::runtime::current_session->myscreen_;
    std::vector<int> pixels;
    pixels.reserve(static_cast<std::size_t>((x1 - x0) * (y1 - y0)));
    for (int y = y0; y < y1; ++y)
    {
        for (int x = x0; x < x1; ++x)
        {
            int index = 0;
            game->get_pixel(x, y, &index);
            pixels.push_back(index);
        }
    }
    return pixels;
}
} // namespace

// The clip, proved on PIXELS through the production content pass. A text
// widget parked on the band's LAST unit with six lines would ink 48px into a
// 14px band — straight through the panel's bottom bevel and over the seat
// rail. Three renders of the same screen settle it: the widget's first line
// paints, the five that do not fit change nothing, and the rows below the
// band are byte-identical to a composition with no text at all.
TEST(MenuLayout, basecamp_zone_text_ink_never_escapes_below_its_band)
{
    namespace hooks = og::script::hooks;
    screen* const game = og::runtime::current_session->myscreen_;
    SaveData& save = game->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    for (int i = 0; i < 3; ++i)
    {
        save.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(i)]->name =
            std::format("INK{}", i);
    }
    save.team_size = 3;

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.draw_content);

    // Roster FIRST (7 rows, classic header at y=33), text last on the band's
    // final unit: y=143..156, with the panel's inner face ending at 158.
    const auto compose = [](int line_count) {
        hooks::CampaignZone raw;
        hooks::CampaignZoneWidget roster;
        roster.kind = hooks::CampaignZoneWidget::Kind::Roster;
        roster.weight = 7;
        raw.widgets.push_back(std::move(roster));
        hooks::CampaignZoneWidget text;
        text.kind = hooks::CampaignZoneWidget::Kind::Text;
        text.weight = 1;
        for (int i = 0; i < line_count; ++i)
            text.lines.push_back(std::format("ESCAPING STANZA LINE {}", i));
        raw.widgets.push_back(std::move(text));
        return raw;
    };

    const auto render = [&](int line_count) {
        og::ui::CampaignZoneSession zone(save);
        EXPECT_TRUE(zone.adopt(compose(line_count)));
        EXPECT_EQ(7, zone.roster().rows_per_page);
        og::ui::BaseCampScreenState state;
        state.zone = &zone;
        og::ui::base_camp_refresh_rows(state);
        og::ui::install_base_camp_state_for_screen(&state);
        // The zone's narrative ink is PURE_BLACK, which IS palette index 0 —
        // the value clearbuffer leaves behind. Lay a non-zero face over the
        // whole canvas first so text ink reads as a difference anywhere it
        // lands, inside the band or past it.
        game->clearbuffer();
        game->draw_button(0, 0, 319, 199, 2, 1);
        spec.draw_content(&state);
        og::ui::install_base_camp_state_for_screen(nullptr);
    };

    // y=143..157 is the text widget's own band; y=158..199 is everything
    // the panel does not own (bottom bevel, seat rail, command strip).
    constexpr int kBandY0 = 143;
    constexpr int kBandY1 = 157;
    render(0);
    const std::vector<int> band_empty =
        snapshot_canvas_rect(10, kBandY0, 310, kBandY1);
    const std::vector<int> below_empty = snapshot_canvas_rect(0, 158, 320, 200);
    render(1);
    const std::vector<int> band_one =
        snapshot_canvas_rect(10, kBandY0, 310, kBandY1);
    render(hooks::kCampaignZoneMaxTextLines);
    const std::vector<int> band_six =
        snapshot_canvas_rect(10, kBandY0, 310, kBandY1);
    const std::vector<int> below_six = snapshot_canvas_rect(0, 158, 320, 200);

    EXPECT_NE(band_empty, band_one)
        << "the widget's first line must actually ink its band";
    EXPECT_EQ(band_one, band_six)
        << "a one-unit band draws exactly one line, whatever it was handed";
    EXPECT_EQ(below_empty, below_six)
        << "text ink escaped the zone band and painted the rail below it";

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    game->clearbuffer();
}

// The roster capability lattice: a composition may clear any subset of
// can_deploy / can_train / can_reorder / can_team, and each cleared bit
// hides a whole roster column. Every surviving column must still be
// keyboard-reachable — a visible control with no incoming nav link is
// mouse-only, and nothing else in the suite would notice. (The chip
// column's own visibility fork is `own && (assign_mode || (can_team &&
// !networked))`, so the {can_team} x {assign} axes below cover every
// networked chip shape too: networked-without-assign is the can_team=false
// shape, networked-with-assign is the assign=true shape.)
TEST(MenuLayout, createmenu_basecamp_roster_capability_lattice_reachable)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    // Three owned members: rows 1 and 2 carry a move-up control (row 0
    // never does — nothing above it to swap with).
    for (int i = 0; i < 3; ++i)
    {
        save.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(i)]->name =
            std::format("L{}", i);
    }
    save.team_size = 3;

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);

    for (int caps = 0; caps < 16; ++caps)
    {
        for (int assign = 0; assign < 2; ++assign)
        {
            og::script::hooks::CampaignZone raw;
            og::script::hooks::CampaignZoneWidget roster;
            roster.kind =
                og::script::hooks::CampaignZoneWidget::Kind::Roster;
            roster.can_deploy = (caps & 1) != 0;
            roster.can_train = (caps & 2) != 0;
            roster.can_reorder = (caps & 4) != 0;
            roster.can_team = (caps & 8) != 0;
            if (assign != 0)
            {
                roster.assign.active = true;
                roster.assign.key = "road";
                roster.assign.labels = {"WAR", "BURDEN"};
            }
            raw.widgets.push_back(roster);

            og::ui::CampaignZoneSession zone(save);
            ASSERT_TRUE(zone.adopt(raw)) << "caps=" << caps;
            og::ui::BaseCampScreenState state;
            state.zone = &zone;
            og::ui::base_camp_refresh_rows(state);
            og::ui::install_base_camp_state_for_screen(&state);

            button* buttons = picker_createmenu_buttons();
            const int count = picker_createmenu_button_count();
            int highlighted = kCreateMenuBackIndex;
            spec.nav.rewire(buttons, count, highlighted);

            const std::string variant =
                std::format("caps={} assign={}", caps, assign);
            check_nav_closed_and_reachable(buttons, count,
                                           kCreateMenuBackIndex,
                                           variant.c_str());

            // The specific shape that stranded the move-up column: the row
            // body gone (can_train cleared) with the chip column still
            // standing between it and the deploy zone.
            for (int r = 0; r < kBaseCampRosterRowsPerPage; ++r)
            {
                if (buttons[kBaseCampMoveUpBase + r].hidden)
                    continue;
                bool linked = false;
                for (int i = 0; i < count && !linked; ++i)
                {
                    if (buttons[i].hidden)
                        continue;
                    const MenuNav& n = buttons[i].nav;
                    linked = n.up == kBaseCampMoveUpBase + r ||
                        n.down == kBaseCampMoveUpBase + r ||
                        n.left == kBaseCampMoveUpBase + r ||
                        n.right == kBaseCampMoveUpBase + r;
                }
                EXPECT_TRUE(linked)
                    << variant << ": roster_up_" << r
                    << " is visible with no incoming nav link";
            }

            og::ui::install_base_camp_state_for_screen(nullptr);
        }
    }

    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// The rail is THIS MACHINE'S four seat slots, and what each slot IS depends
// on three facts: how many seats this machine holds, how many the device can
// seat, and whether the lobby has room for another. Pin the whole matrix
// through the production rewire — a slot is a card, an ADD PLAYER door, a
// dimmed LOBBY FULL face, or absent, and nothing else.
TEST(MenuLayout, createmenu_basecamp_seat_rail_slot_matrix_labels_and_nav)
{
    // A build with no multiplayer has ONE seat and no door to a second, so
    // its rail is one slot wide whatever the device could seat.
#ifdef DISABLE_MULTIPLAYER
    constexpr bool kMultiplayerCompiledIn = false;
#else
    constexpr bool kMultiplayerCompiledIn = true;
#endif
    FactoryMappingGuard mapping_guard;
    EXPECT_EQ(11, kBaseCampSeatCardLabelBudget)
        << "70px slot face / 6px per character";

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
        bool saved_device_class = input_hardware_state().single_seat_device;
        ~LobbyRestore()
        {
            input_hardware_state().single_seat_device = saved_device_class;
            og::ui::install_base_camp_state_for_screen(nullptr);
            og::ui::install_active_picker_lobby_client(saved);
        }
    } restore;
    og::ui::install_active_picker_lobby_client(&lobby);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);
    ASSERT_NE(nullptr, spec.on_spec_row);

    // The rail's own mapping names, in local-slot order (the factory profile
    // pool): slot 0 is WASD, slot 1 the arrow glyphs.
    const std::array<const char*, kBaseCampSeatCardsPerPage> kSlotOwner{
        "WASD", og::input::kArrowGlyphs, "IJKL", "TFGH"};

    int shapes_checked = 0;
    for (const int local_count : {0, 1, 2, 3, 4})
    {
        // A phone with nothing attached seats ONE player; a desktop four.
        for (const bool phone : {false, true})
        {
            // A lobby at the 16-seat global ceiling has no room for another
            // seat even though this machine's slots are still there.
            for (const bool lobby_full : {false, true})
            {
                input_hardware_state().single_seat_device = phone;
                const int slot_cap = (phone || !kMultiplayerCompiledIn)
                    ? 1
                    : kBaseCampSeatCardsPerPage;

                og::ui::BaseCampScreenState state;
                state.page = og::ui::PageModel::make(
                    0, kBaseCampRosterRowsPerPage);
                const int total_seats =
                    lobby_full ? static_cast<int>(og::sim::kMaxGlobalPlayers)
                               : local_count;
                for (int i = 0; i < total_seats; ++i)
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
                for (int i = 0; i < local_count; ++i)
                    state.local_seat_indices.push_back(
                        static_cast<std::uint8_t>(i));
                lobby.players = state.seats;
                lobby.local_indices = state.local_seat_indices;
                og::ui::install_base_camp_state_for_screen(&state);

                button* buttons = picker_createmenu_buttons();
                const int count = picker_createmenu_button_count();
                int highlighted = kBaseCampSeatCardBase;
                spec.nav.rewire(buttons, count, highlighted);

                const std::string variant = std::format(
                    "local={} device={} lobby={}", local_count,
                    phone ? "phone" : "desktop",
                    lobby_full ? "full" : "open");
                ++shapes_checked;

                // The four retired ordinals are re-parked every frame.
                for (const int spare : kBaseCampSeatRailSpares)
                {
                    EXPECT_TRUE(buttons[spare].hidden)
                        << variant << " spare " << spare;
                    EXPECT_EQ(-1, buttons[spare].nav.left)
                        << variant << " spare " << spare;
                    EXPECT_EQ(-1, buttons[spare].nav.right)
                        << variant << " spare " << spare;
                }

                std::vector<int> rail;
                for (int slot = 0; slot < kBaseCampSeatCardsPerPage; ++slot)
                {
                    const int ordinal = kBaseCampSeatCardBase + slot;
                    const button& face = buttons[ordinal];
                    const std::string where =
                        variant + std::format(" slot {}", slot);

                    // THE STATE RULE. Order matters: a seat this machine
                    // already holds keeps its card whatever the caps say.
                    const bool is_card = slot < local_count;
                    const bool is_hidden = !is_card && slot >= slot_cap;
                    const bool is_full =
                        !is_card && !is_hidden &&
                        (lobby_full || !kMultiplayerCompiledIn);

                    EXPECT_EQ(is_hidden, face.hidden) << where;
                    ASSERT_NE(nullptr, spec.rows[ordinal].state_override);
                    const og::ui::RowState row_state =
                        spec.rows[ordinal].state_override(
                            og::ui::MenuLabelContext{});
                    EXPECT_EQ(is_hidden ? og::ui::RowState::Hidden
                                        : (is_full
                                               ? og::ui::RowState::Disabled
                                               : og::ui::RowState::Visible),
                              row_state)
                        << where;

                    // The grid never moves: slot k is at 8 + 78k, 70 wide,
                    // whether it holds a seat, an offer, or nothing.
                    EXPECT_EQ(8 + 78 * slot, face.x) << where;
                    EXPECT_EQ(70, face.sizex) << where;
                    EXPECT_EQ(164, face.y) << where;

                    if (is_card)
                    {
                        // Design §2.3: a local card names its INPUT mapping,
                        // followed by the two load-bearing trailing pads that
                        // center the visible ink over the chip-free zone
                        // rather than over the whole face.
                        // #249: on a single-seat device the touchscreen IS
                        // the controller, so the FIRST seat names the screen
                        // instead of keys the device does not have. A later
                        // seat exists only because a pad opened the cap, so
                        // it keeps its own mapping name.
                        const char* const owner =
                            (phone && slot == 0)
                            ? og::input::kScreenSeatOwnerLabel
                            : kSlotOwner[static_cast<std::size_t>(slot)];
                        EXPECT_EQ(std::format("P{} {}  ", slot + 1, owner),
                                  face.label)
                            << where;
                        ASSERT_GE(face.label.size(), 2u) << where;
                        EXPECT_EQ("  ", face.label.substr(face.label.size() - 2))
                            << where << ": both chip-clearance pads are "
                                        "load-bearing";
                    }
                    else if (is_full)
                    {
                        EXPECT_EQ("LOBBY FULL", face.label) << where;
                    }
                    else if (!is_hidden)
                    {
                        EXPECT_EQ("ADD PLAYER", face.label) << where;
                    }
                    EXPECT_LE(face.label.size(),
                              static_cast<std::size_t>(
                                  kBaseCampSeatCardLabelBudget))
                        << where << " '" << face.label << "'";

                    if (!is_hidden)
                        rail.push_back(ordinal);
                }

                // Visible slots run contiguously from slot zero: a hole in
                // the middle of the rail would strand the chain.
                ASSERT_FALSE(rail.empty()) << variant;
                EXPECT_EQ(kBaseCampSeatCardBase, rail.front()) << variant;
                EXPECT_EQ(static_cast<std::size_t>(rail.back() -
                                                   kBaseCampSeatCardBase + 1),
                          rail.size())
                    << variant << ": the rail has a hole in it";
                EXPECT_EQ(8, buttons[rail.front()].x) << variant;
                EXPECT_EQ(buttons[kCreateMenuBackIndex].x,
                          buttons[rail.front()].x)
                    << variant << " (the rail opens on BACK's edge)";

                for (std::size_t i = 0; i < rail.size(); ++i)
                {
                    const button& control = buttons[rail[i]];
                    EXPECT_EQ(i > 0 ? rail[i - 1] : -1, control.nav.left)
                        << variant << " " << control.id;
                    EXPECT_EQ(i + 1 < rail.size() ? rail[i + 1] : -1,
                              control.nav.right)
                        << variant << " " << control.id;
                }

                const int rail_first = rail.front();
                const int rail_last = rail.back();
                const auto visible_slot_or_rail = [&](int slot) {
                    return slot < static_cast<int>(rail.size())
                        ? kBaseCampSeatCardBase + slot
                        : rail_first;
                };
                EXPECT_EQ(-1, buttons[kCreateMenuHireIndex].nav.up) << variant;
                EXPECT_EQ(rail_first, buttons[kCreateMenuHireIndex].nav.down)
                    << variant << " (empty roster: HIRE drops to the rail)";
                EXPECT_EQ(rail_first, buttons[kCreateMenuBackIndex].nav.up)
                    << variant << " (BACK climbs to the rail's left end)";
                EXPECT_EQ(visible_slot_or_rail(1),
                          buttons[kCreateMenuScenarioIndex].nav.up)
                    << variant;
                EXPECT_EQ(visible_slot_or_rail(2),
                          buttons[kCreateMenuNetworkingIndex].nav.up)
                    << variant;
                EXPECT_EQ(rail_last, buttons[kCreateMenuGoIndex].nav.up)
                    << variant;
                // Each slot drops onto its strip door; the last one falls
                // back a door when the GO/READY slot has no visible half.
                constexpr std::array<int, kBaseCampSeatCardsPerPage>
                    kSlotStripTargets{kCreateMenuDifficultyIndex,
                                      kCreateMenuScenarioIndex,
                                      kCreateMenuNetworkingIndex,
                                      kCreateMenuGoIndex};
                for (const int ordinal : rail)
                {
                    const int slot = ordinal - kBaseCampSeatCardBase;
                    EXPECT_EQ(kSlotStripTargets[static_cast<std::size_t>(slot)],
                              buttons[ordinal].nav.down)
                        << variant << " slot " << slot;
                }
                // The last visible slot closes the rail on the panel's right
                // rail only when all four are up — a device-capped rail stops
                // where the hardware stops.
                if (rail.size() ==
                    static_cast<std::size_t>(kBaseCampSeatCardsPerPage))
                {
                    EXPECT_EQ(312, buttons[rail_last].x +
                                       buttons[rail_last].sizex)
                        << variant;
                }

                check_no_overlaps(buttons, count, variant.c_str());
                check_bounds(buttons, count, variant.c_str());
                check_nav_closed_and_reachable(
                    buttons, count, kCreateMenuBackIndex, variant.c_str());

                // The same rail must close over READY for a joiner with an
                // active seat: GO hides, the last visible slot points down to
                // READY, and READY points back up to the rail's right end.
                lobby.host = false;
                buttons = picker_createmenu_buttons();
                highlighted = kBaseCampSeatCardBase;
                spec.nav.rewire(buttons, count, highlighted);
                EXPECT_TRUE(buttons[kCreateMenuGoIndex].hidden) << variant;
                EXPECT_EQ(local_count == 0,
                          buttons[kCreateMenuReadyIndex].hidden)
                    << variant;
                EXPECT_EQ(local_count > 0 ? kCreateMenuReadyIndex : -1,
                          buttons[kCreateMenuNetworkingIndex].nav.right)
                    << variant;
                if (local_count > 0)
                {
                    EXPECT_EQ(rail_last,
                              buttons[kCreateMenuReadyIndex].nav.up)
                        << variant;
                    if (rail.size() == static_cast<std::size_t>(
                                           kBaseCampSeatCardsPerPage))
                    {
                        EXPECT_EQ(kCreateMenuReadyIndex,
                                  buttons[kBaseCampSeatCardBase + 3].nav.down)
                            << variant;
                    }
                }
                else if (rail.size() == static_cast<std::size_t>(
                                            kBaseCampSeatCardsPerPage))
                {
                    // A zero-seat guest has neither half of the GO/READY
                    // slot: the last slot falls back one door to NETWORK
                    // rather than pointing keyboard focus at a hidden row.
                    EXPECT_EQ(kCreateMenuNetworkingIndex,
                              buttons[rail_last].nav.down)
                        << variant;
                }
                const std::string joiner_variant = variant + " joiner";
                check_nav_closed_and_reachable(
                    buttons, count, kCreateMenuBackIndex,
                    joiner_variant.c_str());
                lobby.host = true;
                og::ui::install_base_camp_state_for_screen(nullptr);
            }
        }
    }
    EXPECT_EQ(20, shapes_checked)
        << "5 local seat counts x 2 device classes x 2 lobby states";
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
                    // Rail controls over the strip's right end inherit GO as
                    // their native host down target. This matrix's synthetic
                    // joiner hides GO by hand, so close those links too.
                    for (int i = kBaseCampSeatCardBase;
                         i < kBaseCampSeatCardBase +
                                 kBaseCampSeatCardsPerPage; ++i)
                    {
                        if (buttons[i].nav.down == kCreateMenuGoIndex)
                            buttons[i].nav.down = kCreateMenuNetworkingIndex;
                    }
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
// always-visible VIEW LEVEL | PROGRESS | LINEUP row and the y=140
// match-settings band TEAMS | TROOPS | LIMIT (#218 — ctf_teams/ctf_caps
// re-homed from MATCHUP; the retired MATCHUP door's ordinal 4 is the
// LINEUP door now — docs/lineup-design.md §2 — so every index below it
// kept its value); BACK sits at (30,170) so no other screen's "back"
// shares its geometry. Static nav encodes the host+versus (all-visible)
// variant.
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
        bool hidden = false;
    };
    static const ExpectedButton kExpected[] = {
        {"back", "BACK", 30, 170, 60, 20, MenuNav{.up = 7}},
        {"set_campaign", "SET CAMPAIGN", 30, 40, 80, 15, MenuNav{.down = 2}},
        {"set_level", "SET LEVEL", 30, 70, 80, 15, MenuNav{.up = 1, .down = 3}},
        {"view_scenario", "VIEW LEVEL", 30, 100, 80, 15, MenuNav{.up = 2, .down = 7, .right = 5}},
        {"lineup", "LINEUP", 210, 100, 80, 15, MenuNav{.up = 2, .down = 8, .left = 5}},
        {"progress", "PROGRESS", 120, 100, 80, 15, MenuNav{.up = 2, .down = 6, .left = 3, .right = 4}},
        {"troops", "TROOPS: ALL", 120, 140, 80, 15, MenuNav{.up = 5, .down = 0, .left = 7, .right = 8}},
        {"ctf_teams", "Teams: Auto", 30, 140, 80, 15, MenuNav{.up = 3, .down = 0, .right = 6}},
        {"ctf_caps", "Limit: Map", 210, 140, 80, 15, MenuNav{.up = 4, .down = 0, .left = 6}},
    };

    for (int i = 0; i < count; ++i)
    {
        const ExpectedButton& want = kExpected[i];
        const button& got = buttons[i];
        EXPECT_EQ(want.id, got.id) << "index " << i;
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
        EXPECT_LE(got.label.size(), 12u) << got.label;
    }

    EXPECT_EQ(kScenarioMenuBackIndex, 0);
    EXPECT_EQ(kScenarioMenuSetCampaignIndex, 1);
    EXPECT_EQ(kScenarioMenuSetLevelIndex, 2);
    EXPECT_EQ(kScenarioMenuViewScenarioIndex, 3);
    EXPECT_EQ(kScenarioMenuLineupIndex, 4);
    EXPECT_EQ(kScenarioMenuProgressIndex, 5);
    EXPECT_EQ(kScenarioMenuTroopsIndex, 6);
    EXPECT_EQ(kScenarioMenuCtfTeamsIndex, 7);
    EXPECT_EQ(kScenarioMenuCtfCapsIndex, 8);
    EXPECT_EQ(kScenarioMenuButtonCount, 9);

    // The LINEUP door completes the y=100 row on the declared grid: same
    // baseline as PROGRESS, same x=210 column as LIMIT below it.
    EXPECT_EQ(buttons[kScenarioMenuLineupIndex].y,
              buttons[kScenarioMenuProgressIndex].y)
        << "LINEUP shares the VIEW LEVEL | PROGRESS baseline";
    EXPECT_EQ(buttons[kScenarioMenuLineupIndex].x,
              buttons[kScenarioMenuCtfCapsIndex].x)
        << "LINEUP sits on the x=210 column over LIMIT";

    // Grid RELATIONS (the menus discipline: exact tables pin
    // self-consistency, relations pin alignment). Declared columns
    // x=30/120/210; the x=30 column stacks five faces; the y=100 row and
    // the y=140 band each share one baseline; all seven grid faces are
    // 80x15.
    for (const int left_col : {kScenarioMenuBackIndex,
                               kScenarioMenuSetCampaignIndex,
                               kScenarioMenuSetLevelIndex,
                               kScenarioMenuViewScenarioIndex,
                               kScenarioMenuCtfTeamsIndex})
        EXPECT_EQ(30, buttons[left_col].x) << buttons[left_col].id;
    EXPECT_EQ(buttons[kScenarioMenuProgressIndex].x,
              buttons[kScenarioMenuTroopsIndex].x)
        << "PROGRESS left-packs into the x=120 column over TROOPS";
    EXPECT_EQ(210, buttons[kScenarioMenuCtfCapsIndex].x);
    EXPECT_EQ(buttons[kScenarioMenuViewScenarioIndex].y,
              buttons[kScenarioMenuProgressIndex].y);
    EXPECT_EQ(buttons[kScenarioMenuCtfTeamsIndex].y,
              buttons[kScenarioMenuTroopsIndex].y);
    EXPECT_EQ(buttons[kScenarioMenuTroopsIndex].y,
              buttons[kScenarioMenuCtfCapsIndex].y);
    for (const int face : {kScenarioMenuViewScenarioIndex,
                           kScenarioMenuLineupIndex,
                           kScenarioMenuProgressIndex,
                           kScenarioMenuTroopsIndex,
                           kScenarioMenuCtfTeamsIndex,
                           kScenarioMenuCtfCapsIndex})
    {
        EXPECT_EQ(80, buttons[face].sizex) << buttons[face].id;
        EXPECT_EQ(15, buttons[face].sizey) << buttons[face].id;
    }

    // The campaign-name / level-title strips draw from x=116 (32-char clip,
    // 6px/char): they must clear the x=30 button column's right edge.
    EXPECT_GE(116, buttons[kScenarioMenuSetCampaignIndex].x +
                       buttons[kScenarioMenuSetCampaignIndex].sizex);
    EXPECT_LE(116 + 32 * 6, SCREEN_W);

    // ...and so must every row that reaches past x=114 (the strips are
    // drawn AFTER draw_buttons, so anything under them is overprinted; the
    // two strip bands are 8px tall at each host-gated button's y+4-1).
    // That is why the whole match-settings band sits at y=140 and PROGRESS
    // at y=100 rather than beside SET LEVEL.
    for (const int strip_index :
         {kScenarioMenuSetCampaignIndex, kScenarioMenuSetLevelIndex})
    {
        const int strip_top = buttons[strip_index].y + 3;
        for (const int row_index : {kScenarioMenuProgressIndex,
                                    kScenarioMenuTroopsIndex,
                                    kScenarioMenuCtfTeamsIndex,
                                    kScenarioMenuCtfCapsIndex})
        {
            const button& row = buttons[row_index];
            const bool vertically_clear = row.y + row.sizey <= strip_top ||
                                          row.y >= strip_top + 8;
            const bool horizontally_clear = row.x + row.sizex <= 114;
            EXPECT_TRUE(vertically_clear || horizontally_clear)
                << row.id << " is drawn under the strip beside button "
                << buttons[strip_index].id;
        }
    }

    check_no_overlaps(buttons, count, "scenariomenu");
    check_bounds(buttons, count, "scenariomenu");
    check_nav_closed_and_reachable(buttons, count, kScenarioMenuBackIndex,
                                   "scenariomenu_static");
}

// Gating variants for the SCENARIO subscreen: SET CAMPAIGN / SET LEVEL /
// TROOPS hide for joiners, and the nav graph rewires around every hidden
// combination. (The MISSIONS door retired into the Base Camp zone —
// docs/basecamp-zones-design.md.)
// VIEW LEVEL's staged-preview band (#218, C10): the band and census rows
// are a declared grid, pinned both as exact values and as RELATIONS (the
// menus discipline: exact tables pin self-consistency; the relations pin
// that the band sits inside the frame with the census fitting below it).
TEST(MenuLayout, view_scenario_staged_band_geometry)
{
    // Exact values (the classic UI-canvas grid).
    EXPECT_EQ(5, kViewScenarioFrameX);
    EXPECT_EQ(5, kViewScenarioFrameY);
    EXPECT_EQ(314, kViewScenarioFrameW);
    EXPECT_EQ(160, kViewScenarioFrameH);
    EXPECT_EQ(8, kViewScenarioPreviewBandX);
    EXPECT_EQ(16, kViewScenarioPreviewBandY);
    EXPECT_EQ(303, kViewScenarioPreviewBandW);
    EXPECT_EQ(76, kViewScenarioPreviewBandH);
    EXPECT_EQ(96, kViewScenarioCensusTopY);
    EXPECT_EQ(6, kViewScenarioRowPitch);
    EXPECT_EQ(10, kViewScenarioRowsPerPage);

    // Relations: the band nests strictly inside the frame.
    EXPECT_GT(kViewScenarioPreviewBandX, kViewScenarioFrameX);
    EXPECT_LT(kViewScenarioPreviewBandX + kViewScenarioPreviewBandW,
              kViewScenarioFrameX + kViewScenarioFrameW);
    EXPECT_GT(kViewScenarioPreviewBandY, kViewScenarioFrameY);
    EXPECT_LT(kViewScenarioPreviewBandY + kViewScenarioPreviewBandH,
              kViewScenarioFrameY + kViewScenarioFrameH);
    // Title row (y=8) sits above the band; census rows start 4px under it.
    EXPECT_LT(8, kViewScenarioPreviewBandY);
    EXPECT_EQ(4, kViewScenarioCensusTopY - (kViewScenarioPreviewBandY +
                                            kViewScenarioPreviewBandH));
    // A full census page ends inside the frame, above the y>=170 buttons.
    EXPECT_LE(kViewScenarioCensusTopY +
                  kViewScenarioRowsPerPage * kViewScenarioRowPitch,
              kViewScenarioFrameY + kViewScenarioFrameH);
}

// Two visibility axes since the MATCHUP re-home (#218): SET CAMPAIGN /
// SET LEVEL / TROOPS hide on the host axis, TEAMS / LIMIT on the
// versus-campaign axis (visible to joiners as read-only labels). Every
// {host} x {versus} combination must leave the visible graph closed and
// fully keyboard-reachable — TROOPS lost its only static .up when the
// MATCHUP door parked, so the rewire is what keeps the whole y=140 band
// reachable in every variant.
TEST(MenuLayout, scenariomenu_nav_variants_keyboard_reachable)
{
    for (const bool host_visible : {true, false})
    {
        for (const bool match_visible : {true, false})
        {
            button* buttons = picker_scenariomenu_buttons();
            const int count = picker_scenariomenu_button_count();
            buttons[kScenarioMenuSetCampaignIndex].hidden = !host_visible;
            buttons[kScenarioMenuSetLevelIndex].hidden = !host_visible;
            buttons[kScenarioMenuTroopsIndex].hidden = !host_visible;
            buttons[kScenarioMenuCtfTeamsIndex].hidden = !match_visible;
            buttons[kScenarioMenuCtfCapsIndex].hidden = !match_visible;
            picker_wire_scenario_menu_nav(buttons, count, host_visible,
                                          match_visible);
            check_nav_closed_and_reachable(
                buttons, count, kScenarioMenuBackIndex,
                std::format("scenariomenu_{}_{}",
                            host_visible ? "host" : "joiner",
                            match_visible ? "versus" : "classic")
                    .c_str());
            EXPECT_FALSE(buttons[kScenarioMenuLineupIndex].hidden)
                << "the LINEUP door is never gated (§2.3)";
        }
    }
}

// LINEUP grid relations (docs/lineup-design.md §2.4; the exact table is the
// test_menu_pins oracle): four bands of EQUAL pitch, one shared column x per
// knob across all bands, knob rows inside the panel, and the action strip
// closing flush on x=312 with uniform 6px gutters.
TEST(MenuLayout, lineup_band_and_strip_relations)
{
    button* buttons = picker_lineup_buttons();
    const int count = picker_lineup_button_count();
    ASSERT_EQ(kLineupButtonCount, count);

    // Equal band pitch, derived from the knob faces themselves.
    const int pitch = buttons[kLineupBotsBase + 1].y -
        buttons[kLineupBotsBase + 0].y;
    EXPECT_EQ(kLineupBandPitch, pitch);
    for (int t = 0; t < 4; ++t)
    {
        const button& bots = buttons[kLineupBotsBase + t];
        const button& level = buttons[kLineupLevelBase + t];
        // Shared columns across every band.
        EXPECT_EQ(buttons[kLineupBotsBase].x, bots.x) << "band " << t;
        EXPECT_EQ(buttons[kLineupLevelBase].x, level.x) << "band " << t;
        // One baseline per band's knob row.
        EXPECT_EQ(bots.y, level.y) << "band " << t;
        EXPECT_EQ(lineup_band_y(t) + kLineupKnobDy, bots.y) << "band " << t;
        if (t > 0)
        {
            EXPECT_EQ(pitch, bots.y - buttons[kLineupBotsBase + t - 1].y)
                << "unequal band pitch at band " << t;
        }
        // LV opens right after BOTS with one gutter.
        EXPECT_EQ(bots.x + bots.sizex + 6, level.x) << "band " << t;
        // The knob row stays inside the opaque panel.
        EXPECT_GT(bots.y, kLineupPanelY1);
        EXPECT_LT(bots.y + bots.sizey, kLineupPanelY2);
    }

    // Action strip: one baseline, uniform 6px gutters, flush right on 312.
    const int strip[] = {kLineupBackIndex, kLineupFightersIndex,
                         kLineupSplitEvenIndex, kLineupSplitFairIndex,
                         kLineupUniteIndex};
    for (const int index : strip)
    {
        EXPECT_EQ(kLineupStripY, buttons[index].y) << buttons[index].id;
        EXPECT_EQ(kLineupStripH, buttons[index].sizey) << buttons[index].id;
    }
    for (std::size_t i = 1; i < std::size(strip); ++i)
    {
        const button& prev = buttons[strip[i - 1]];
        const button& next = buttons[strip[i]];
        EXPECT_EQ(prev.x + prev.sizex + kLineupStripGap, next.x)
            << next.id << " breaks the strip gutter";
    }
    EXPECT_EQ(kLineupStripRightX,
              buttons[kLineupUniteIndex].x +
                  buttons[kLineupUniteIndex].sizex)
        << "the strip closes flush on the panel rail";

    // Label budgets: centered labels draw with no clipping at 6px/char.
    for (int i = 0; i < count; ++i)
    {
        EXPECT_LE(static_cast<int>(buttons[i].label.size()) * 6,
                  buttons[i].sizex)
            << buttons[i].id << " label '" << buttons[i].label
            << "' escapes its face";
    }

    check_no_overlaps(buttons, count, "lineup");
    check_bounds(buttons, count, "lineup");
    check_nav_closed_and_reachable(buttons, count, kLineupBackIndex,
                                   "lineup_static");
}

// LINEUP nav variants (§2.3): the knobs hide for joiners and dim (visible,
// engine-inert) on classic campaigns; SPLIT EVEN / SPLIT FAIR hide when
// this machine holds fewer than two seats. Every {host, joiner} x
// {versus, classic} x {1, 2 local seats} combination must leave the
// visible graph closed and fully keyboard-reachable.
TEST(MenuLayout, lineup_nav_variants_keyboard_reachable)
{
    struct LineupVariantLobby final : og::ui::IPickerLobbyClient
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
    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const std::string saved_campaign = save.current_campaign;

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::Lineup).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);

    for (const bool host : {true, false})
    {
        for (const bool versus : {true, false})
        {
            for (const int seats : {1, 2})
            {
                lobby.host = host;
                lobby.players.clear();
                lobby.local_indices.clear();
                for (int seat = 0; seat < seats; ++seat)
                {
                    og::sim::LobbyPlayer player;
                    player.player_index = static_cast<std::uint8_t>(seat);
                    player.team = static_cast<short>(seat);
                    lobby.players.push_back(player);
                    lobby.local_indices.push_back(
                        static_cast<std::uint8_t>(seat));
                }
                save.current_campaign = versus ? "modes" : "gladiator";

                button* buttons = picker_lineup_buttons();
                const int count = picker_lineup_button_count();
                // Mirror the runner's gate pass for the knob rows (their
                // state_override), then the production rewire.
                og::ui::MenuLabelContext context;
                context.save = &save;
                context.is_host = host;
                context.is_networked = true;
                const std::vector<const og::ui::MenuButtonSpec*> rows =
                    og::ui::materialized_spec_rows(spec);
                ASSERT_EQ(static_cast<int>(rows.size()), count);
                for (int i = 0; i < count; ++i)
                {
                    if (rows[static_cast<std::size_t>(i)]->state_override ==
                        nullptr)
                        continue;
                    buttons[i].hidden =
                        rows[static_cast<std::size_t>(i)]->state_override(
                            context) == og::ui::RowState::Hidden;
                }
                int highlighted = kLineupBackIndex;
                spec.nav.rewire(buttons, count, highlighted);

                const std::string name = std::format(
                    "lineup_{}_{}_{}seat", host ? "host" : "joiner",
                    versus ? "versus" : "classic", seats);
                check_nav_closed_and_reachable(buttons, count,
                                               kLineupBackIndex,
                                               name.c_str());
                for (int t = 0; t < 4; ++t)
                {
                    EXPECT_EQ(!host,
                              buttons[kLineupBotsBase + t].hidden)
                        << name << " band " << t;
                    EXPECT_EQ(!host,
                              buttons[kLineupLevelBase + t].hidden)
                        << name << " band " << t;
                }
                EXPECT_EQ(seats < 2,
                          buttons[kLineupSplitEvenIndex].hidden) << name;
                EXPECT_EQ(seats < 2,
                          buttons[kLineupSplitFairIndex].hidden) << name;
                EXPECT_FALSE(buttons[kLineupUniteIndex].hidden)
                    << name << ": UNITE is always offered";
            }
        }
    }

    save.current_campaign = saved_campaign;
    og::ui::install_active_picker_lobby_client(saved_client);
}

// FIGHTERS list (§2.2): the Base Camp roster row grid verbatim — 8 rows at
// y=45+14r, deploy box x=23 w=14, row body opening on the chip column x=61
// — with the zone-submenu footer split (BACK 10 / PREV 220 / NEXT 270 at
// y=169). The literals here are the independent oracle for the shared
// constants in menu_screen_specs.cpp.
TEST(MenuLayout, lineup_fighters_layout_matches_base_camp_grid)
{
    button* buttons = picker_lineup_fighters_buttons();
    const int count = picker_lineup_fighters_button_count();
    ASSERT_EQ(kLineupFightersButtonCount, count);

    for (int r = 0; r < kLineupFightersRowsPerPage; ++r)
    {
        const button& dep = buttons[kLineupFightersDeployBase + r];
        const button& body = buttons[kLineupFightersBodyBase + r];
        EXPECT_EQ("fighter_dep_" + std::to_string(r), dep.id);
        EXPECT_EQ("fighter_row_" + std::to_string(r), body.id);
        EXPECT_EQ(23, dep.x) << dep.id;
        EXPECT_EQ(14, dep.sizex) << dep.id;
        EXPECT_EQ(45 + 14 * r, dep.y) << dep.id;
        EXPECT_EQ(10, dep.sizey) << dep.id;
        EXPECT_EQ(61, body.x) << body.id;
        EXPECT_EQ(dep.y, body.y) << body.id;
        EXPECT_EQ(10, body.sizey) << body.id;
        EXPECT_TRUE(body.no_draw)
            << body.id << ": the row text IS the affordance";
        // The body ends on the panel's inner face, like the Base Camp rows'
        // controls.
        EXPECT_EQ(310, body.x + body.sizex) << body.id;
        if (r > 0)
        {
            EXPECT_EQ(14, dep.y - buttons[kLineupFightersDeployBase + r - 1].y)
                << "unequal row pitch at row " << r;
        }
    }

    const button& back = buttons[kLineupFightersBackIndex];
    EXPECT_EQ("back", back.id);
    EXPECT_EQ(10, back.x);
    EXPECT_EQ(169, back.y);
    EXPECT_EQ(44, back.sizex);
    EXPECT_EQ(20, back.sizey);
    const button& prev = buttons[kLineupFightersPrevIndex];
    const button& next = buttons[kLineupFightersNextIndex];
    EXPECT_EQ(220, prev.x);
    EXPECT_EQ(270, next.x);
    EXPECT_EQ(prev.y, back.y);
    EXPECT_EQ(next.y, back.y);
    EXPECT_TRUE(prev.hidden) << "pagers start hidden (single page)";
    EXPECT_TRUE(next.hidden);

    check_no_overlaps(buttons, count, "lineup_fighters");
    check_bounds(buttons, count, "lineup_fighters");
}

// The FIGHTERS rewire over a real paged company: 10 occupied slots make two
// pages (8 + 2); the pagers show, the second page hides the empty rows, and
// both windows leave the graph closed and reachable.
TEST(MenuLayout, lineup_fighters_rewire_pages_and_reachability)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char old_team_size = save.team_size;
    for (int i = 0; i < 10; ++i)
    {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = "F" + std::to_string(i);
        member->deployed = (i % 2) == 0;
        save.team_list[static_cast<std::size_t>(i)] = std::move(member);
    }
    save.team_size = 10;

    og::ui::LineupFightersScreenState state;
    og::ui::lineup_fighters_refresh_rows(state);
    EXPECT_EQ(10, static_cast<int>(state.slots.size()));
    EXPECT_TRUE(state.page.multi_page());
    og::ui::install_lineup_fighters_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::LineupFighters).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);
    for (const int page : {0, 1})
    {
        state.page.page = page;
        button* buttons = picker_lineup_fighters_buttons();
        const int count = picker_lineup_fighters_button_count();
        int highlighted = kLineupFightersBackIndex;
        spec.nav.rewire(buttons, count, highlighted);
        const int visible_rows = page == 0 ? 8 : 2;
        for (int r = 0; r < kLineupFightersRowsPerPage; ++r)
        {
            EXPECT_EQ(r >= visible_rows,
                      buttons[kLineupFightersBodyBase + r].hidden)
                << "page " << page << " row " << r;
        }
        EXPECT_FALSE(buttons[kLineupFightersPrevIndex].hidden);
        EXPECT_FALSE(buttons[kLineupFightersNextIndex].hidden);
        // The deploy glyph rides the button label on the visible window.
        EXPECT_EQ("X", buttons[kLineupFightersDeployBase].label)
            << "page " << page << ": first visible row is deployed";
        check_nav_closed_and_reachable(
            buttons, count, kLineupFightersBackIndex,
            page == 0 ? "lineup_fighters_p1" : "lineup_fighters_p2");
    }

    og::ui::install_lineup_fighters_state_for_screen(nullptr);
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    save.team_size = old_team_size;
    (void)picker_lineup_fighters_buttons();
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

// DIFFICULTY subscreen (the Base Camp DIFFICULTY door): unique BACK id + the
// six match-rule rows and the §2.7 CTRL row (#218, re-homed from MATCHUP)
// in one centered 140px column on the FX row pitch (35 + 23n; the CTRL face
// bottom at 188 clears the 4..196 panel bevel).
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
        {"cross_control", "CTRL: OWN", 90, 173},
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
    EXPECT_EQ("cross_control", buttons[kDifficultyMenuCrossControlIndex].id);

    // Column + band-pitch relations (the menus discipline): every settings
    // row shares the x=90 column and the 23px pitch runs unbroken through
    // the appended CTRL row.
    for (int i = kDifficultyMenuDifficultyIndex;
         i <= kDifficultyMenuCrossControlIndex; ++i)
    {
        EXPECT_EQ(90, buttons[i].x) << buttons[i].id;
        if (i > kDifficultyMenuDifficultyIndex)
        {
            EXPECT_EQ(23, buttons[i].y - buttons[i - 1].y) << buttons[i].id;
        }
    }

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
        for (const bool cross : {false, true})
        {
            EXPECT_LE(static_cast<int>(
                          og::ui::format_cross_control_label(cross).size()) * 6,
                      face_width)
                << og::ui::format_cross_control_label(cross);
        }
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
        bool networked = false;
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
        [[nodiscard]] bool is_networked_session() const noexcept override
        {
            return networked;
        }
    };
    FakeLobbyClient lobby;
    og::ui::IPickerLobbyClient* saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    // Degenerate non-host non-networked variant: everything but BACK hides
    // (the CTRL row needs a networked session too).
    lobby.host = false;
    lobby.networked = false;
    int highlighted = kDifficultyMenuGeneratorRateIndex;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    for (int i = kDifficultyMenuDifficultyIndex; i < count; ++i)
        EXPECT_TRUE(buttons[i].hidden) << buttons[i].id;
    EXPECT_FALSE(buttons[kDifficultyMenuBackIndex].hidden);
    // With every row hidden the panel would be a heading over nothing —
    // and this door is one click off the Base Camp strip, where a joiner
    // spends the whole lobby. The caption takes the rows' place.
    EXPECT_EQ("The host sets these for everyone.", difficulty_panel_caption())
        << "a joiner's empty panel must say whose call it is";
    EXPECT_EQ(kDifficultyMenuBackIndex, highlighted)
        << "the highlight must be pulled off the hidden rows";
    check_nav_closed_and_reachable(buttons, count, kDifficultyMenuBackIndex,
                                   "difficulty_menu_joiner");

    // Networked joiner: the six LobbySettings rows stay hidden, but the
    // §2.7 CTRL row shows read-only — a joiner keeps SIGHT of the mode
    // that changes their rights (host-only actionable via
    // change_cross_control's popup).
    lobby.host = false;
    lobby.networked = true;
    highlighted = kDifficultyMenuGeneratorRateIndex;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    for (int i = kDifficultyMenuDifficultyIndex;
         i < kDifficultyMenuCrossControlIndex; ++i)
        EXPECT_TRUE(buttons[i].hidden) << buttons[i].id;
    EXPECT_FALSE(buttons[kDifficultyMenuCrossControlIndex].hidden)
        << "§2.7: joiners must SEE the mode that changes their rights";
    EXPECT_EQ("The host sets these for everyone.", difficulty_panel_caption());
    EXPECT_EQ(kDifficultyMenuBackIndex, highlighted);
    check_nav_closed_and_reachable(buttons, count, kDifficultyMenuBackIndex,
                                   "difficulty_menu_networked_joiner");

    // Networked host: the full column plus the CTRL row, one closed cycle.
    lobby.host = true;
    lobby.networked = true;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    for (int i = 0; i < count; ++i)
        EXPECT_FALSE(buttons[i].hidden) << buttons[i].id;
    EXPECT_EQ(kDifficultyMenuCrossControlIndex,
              buttons[kDifficultyMenuBackIndex].nav.up);
    EXPECT_EQ(kDifficultyMenuCrossControlIndex,
              buttons[kDifficultyMenuInfiniteGoldIndex].nav.down);
    check_nav_closed_and_reachable(buttons, count, kDifficultyMenuBackIndex,
                                   "difficulty_menu_networked_host");

    // Host / local variant restores the classic column — CTRL hides (it
    // decides nothing in a local session) — and drops the caption: rows
    // that answer to this player need no explanation.
    lobby.host = true;
    lobby.networked = false;
    sync_difficulty_menu_visibility(buttons, count, highlighted);
    EXPECT_EQ("", difficulty_panel_caption())
        << "the host's panel is its own explanation";
    og::ui::install_active_picker_lobby_client(saved_client);
    for (int i = 0; i < kDifficultyMenuCrossControlIndex; ++i)
        EXPECT_FALSE(buttons[i].hidden) << buttons[i].id;
    EXPECT_TRUE(buttons[kDifficultyMenuCrossControlIndex].hidden)
        << "cross-control is meaningless without a networked lobby";
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
