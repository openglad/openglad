// WP1 step 0 — G2 "pin-then-migrate" exact-table pins
// (docs/company-basecamp-design.md §1.2 G2, §1.8 step 0).
//
// Before the menu engine re-hosts ANY screen whose test_menu_layout coverage
// is only overlap/bounds today, the CURRENT static k_* table is transcribed
// here as a hand-owned kExpected pin: every field of every row (id, label,
// hotkey, rect, action, arg, nav, hidden). These tables are the differential
// oracle every later migration step is judged against — when the engine
// materializes the same screen from a spec, a transcription typo fails one of
// these pins, not a new self-referential spec test (G11).
//
// Pinned screens (the "thinly-pinned" set named by G2): main menu (the
// compiled build variant — all four k_mainmenu_buttons variants are
// transcribed under the same preprocessor selection picker.cpp uses, so
// whichever variant a build compiles is the one pinned), hire, train, the
// save/load slot menus, view-team, and the progress screen (whose table is
// function-local in create_progress_menu; it is pinned through the live
// vbutton surface instead of an accessor).
//
// DO NOT relax or re-pin these tables during WP1 (Layer E). They change only
// in the Layer-F commits that intentionally reshape a screen, atomically with
// that screen's re-pin (§7).

#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/resources/save_data.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include "test_interact.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <vector>

namespace
{

struct ExpectedButton
{
    const char* id;
    const char* label;
    int hotkey;
    int x, y, w, h;
    Sint32 myfun;
    Sint32 arg;
    MenuNav nav;
    bool hidden = false;
};

// Field-for-field comparison of a picker button table against its
// transcription. Every mismatch names the screen, index, and field.
void check_exact_table(button* buttons, int count,
                       const ExpectedButton* expected, int expected_count,
                       const char* screen_name)
{
    ASSERT_EQ(expected_count, count) << screen_name << " row count";
    for (int i = 0; i < count; ++i)
    {
        const ExpectedButton& want = expected[i];
        const button& got = buttons[i];
        EXPECT_EQ(want.id, got.id) << screen_name << " index " << i;
        EXPECT_EQ(want.label, got.label) << screen_name << " " << got.id;
        EXPECT_EQ(want.hotkey, got.hotkey) << screen_name << " " << got.id;
        EXPECT_EQ(want.x, got.x) << screen_name << " " << got.id;
        EXPECT_EQ(want.y, got.y) << screen_name << " " << got.id;
        EXPECT_EQ(want.w, got.sizex) << screen_name << " " << got.id;
        EXPECT_EQ(want.h, got.sizey) << screen_name << " " << got.id;
        EXPECT_EQ(want.myfun, got.myfun) << screen_name << " " << got.id;
        EXPECT_EQ(want.arg, got.arg1) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.up, got.nav.up) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.down, got.nav.down) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.left, got.nav.left) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.right, got.nav.right)
            << screen_name << " " << got.id;
        EXPECT_EQ(want.hidden, got.hidden) << screen_name << " " << got.id;
        EXPECT_FALSE(got.no_draw) << screen_name << " " << got.id;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Main menu: the four k_mainmenu_buttons build variants (picker.cpp). The
// SAME preprocessor selection picker.cpp uses (including its
// USE_TOUCH_INPUT => DISABLE_MULTIPLAYER mapping) picks the table below, so
// the pin always matches the variant the build actually compiled. Native CI
// pins the native+MP table; a touch or web build of this test pins its own.
// ---------------------------------------------------------------------------

#ifdef USE_TOUCH_INPUT
#ifndef DISABLE_MULTIPLAYER
#define DISABLE_MULTIPLAYER
#endif
#endif

#ifdef __EMSCRIPTEN__
// Web: QUIT is replaced by HELP (same rect, no hotkey).
static const ExpectedButton kExpectedMainMenu[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 52, 140, 20,
     button_action_id(ButtonAction::BeginMenu), 1, MenuNav{.down = 1}, false},
    {"continue_game", "CONTINUE", KEYSTATE_UNKNOWN, 80, 78, 68, 20,
     button_action_id(ButtonAction::CreateTeamMenu), -1,
     MenuNav{.up = 0, .down = 2, .right = 7}},
    {"player_settings", "PLAYER SETTINGS", KEYSTATE_UNKNOWN, 80, 104, 140, 15,
     button_action_id(ButtonAction::MenuSpecRow), 2,
     MenuNav{.up = 1, .down = 3}},
    {"difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 125, 140, 15,
     button_action_id(ButtonAction::OpenDifficultyMenu), -1,
     MenuNav{.up = 2, .down = 4}},
    {"level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 146, 140, 15,
     button_action_id(ButtonAction::DoLevelEdit), -1,
     MenuNav{.up = 3, .down = 6}},
    {"help", "HELP", KEYSTATE_UNKNOWN, 120, 181, 60, 15,
     button_action_id(ButtonAction::ShowHelp), -1,
     MenuNav{.up = 4, .left = 6}},
    {"options", "", KEYSTATE_UNKNOWN, 90, 181, 20, 15,
     button_action_id(ButtonAction::MainOptions), -1,
     MenuNav{.up = 4, .right = 5}},
    {"load_company", "LOAD", KEYSTATE_UNKNOWN, 152, 78, 68, 20,
     button_action_id(ButtonAction::CreateLoadMenu), 0,
     MenuNav{.up = 0, .down = 2, .left = 1}},
    {"no_company_note", "NO COMPANY YET", KEYSTATE_UNKNOWN, 80, 78, 140, 20,
     button_action_id(ButtonAction::MenuSpecRow), 8, MenuNav{}, true},
};
#else
// Native: the compact footer carries QUIT.
static const ExpectedButton kExpectedMainMenu[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 52, 140, 20,
     button_action_id(ButtonAction::BeginMenu), 1, MenuNav{.down = 1}, false},
    {"continue_game", "CONTINUE", KEYSTATE_UNKNOWN, 80, 78, 68, 20,
     button_action_id(ButtonAction::CreateTeamMenu), -1,
     MenuNav{.up = 0, .down = 2, .right = 7}},
    {"player_settings", "PLAYER SETTINGS", KEYSTATE_UNKNOWN, 80, 104, 140, 15,
     button_action_id(ButtonAction::MenuSpecRow), 2,
     MenuNav{.up = 1, .down = 3}},
    {"difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 125, 140, 15,
     button_action_id(ButtonAction::OpenDifficultyMenu), -1,
     MenuNav{.up = 2, .down = 4}},
    {"level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 146, 140, 15,
     button_action_id(ButtonAction::DoLevelEdit), -1,
     MenuNav{.up = 3, .down = 6}},
    {"quit", "QUIT ", KEYSTATE_ESCAPE, 120, 181, 60, 15,
     button_action_id(ButtonAction::QuitMenu), 0, MenuNav{.up = 4, .left = 6}},
    {"options", "", KEYSTATE_UNKNOWN, 90, 181, 20, 15,
     button_action_id(ButtonAction::MainOptions), -1,
     MenuNav{.up = 4, .right = 5}},
    {"load_company", "LOAD", KEYSTATE_UNKNOWN, 152, 78, 68, 20,
     button_action_id(ButtonAction::CreateLoadMenu), 0,
     MenuNav{.up = 0, .down = 2, .left = 1}},
    {"no_company_note", "NO COMPANY YET", KEYSTATE_UNKNOWN, 80, 78, 140, 20,
     button_action_id(ButtonAction::MenuSpecRow), 8, MenuNav{}, true},
};
#endif // __EMSCRIPTEN__

TEST(MenuEnginePins, mainmenu_exact_table)
{
    button* buttons = picker_mainmenu_buttons();
    const int count = picker_mainmenu_button_count();
    check_exact_table(buttons, count, kExpectedMainMenu,
                      static_cast<int>(std::size(kExpectedMainMenu)),
                      "mainmenu");
    // §2.1/§9.2 index contract: load_company then the no_company_note are
    // appended at the table END, so the options gear is now the third-from-
    // last row (picker_mainmenu_options_index() finds it by id).
    ASSERT_EQ("no_company_note", buttons[count - 1].id);
    ASSERT_EQ("load_company", buttons[count - 2].id);
    ASSERT_EQ("options", buttons[count - 3].id);
}

TEST(MenuEnginePins, player_settings_exact_table)
{
#ifndef DISABLE_MULTIPLAYER
    static const ExpectedButton kExpected[] = {
        {"player_settings_back", "BACK", KEYSTATE_ESCAPE, 10, 10, 50, 15,
         button_action_id(ButtonAction::ReturnMenu), MENU_EXIT,
         MenuNav{.up = 6, .down = 1}},
        {"1_player", "1 PLAYER", KEYSTATE_1, 27, 70, 62, 20,
         button_action_id(ButtonAction::SetPlayerMode), 1,
         MenuNav{.up = 0, .down = 5, .right = 2}},
        {"2_player", "2 PLAYER", KEYSTATE_2, 95, 70, 62, 20,
         button_action_id(ButtonAction::SetPlayerMode), 2,
         MenuNav{.up = 0, .down = 5, .left = 1, .right = 3}},
        {"3_player", "3 PLAYER", KEYSTATE_3, 163, 70, 62, 20,
         button_action_id(ButtonAction::SetPlayerMode), 3,
         MenuNav{.up = 0, .down = 5, .left = 2, .right = 4}},
        {"4_player", "4 PLAYER", KEYSTATE_4, 231, 70, 62, 20,
         button_action_id(ButtonAction::SetPlayerMode), 4,
         MenuNav{.up = 0, .down = 5, .left = 3}},
        {"pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 90, 105, 140, 18,
         button_action_id(ButtonAction::AlliedMode), -1,
         MenuNav{.up = 1, .down = 6}},
        {"player_controls", "CONTROLS", KEYSTATE_UNKNOWN, 90, 132, 140, 18,
         button_action_id(ButtonAction::OpenControlSettings), -1,
         MenuNav{.up = 5, .down = 0}},
    };
#else
    static const ExpectedButton kExpected[] = {
        {"player_settings_back", "BACK", KEYSTATE_ESCAPE, 10, 10, 50, 15,
         button_action_id(ButtonAction::ReturnMenu), MENU_EXIT,
         MenuNav{.up = 1, .down = 1}},
        {"player_controls", "CONTROLS", KEYSTATE_UNKNOWN, 90, 88, 140, 20,
         button_action_id(ButtonAction::OpenControlSettings), -1,
         MenuNav{.up = 0, .down = 0}},
    };
#endif
    button* buttons = picker_player_settings_buttons();
    const int count = picker_player_settings_button_count();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)),
                      "player_settings");
}

// The VIEW TEAM screen and the SAVE/LOAD slot menus are RETIRED (design
// §2.5/§3.8) — their exact-table pins retired with them; the base-camp
// roster's table is pinned in test_menu_layout (createmenu_basecamp_*).

// ---------------------------------------------------------------------------
// §2.2 new-company name entry: a Layer-F engine screen. All four rows dispatch
// through the single generic ButtonAction::MenuSpecRow (arg = row ordinal).
// BACK carries the Escape hotkey; the name-box label is empty (its prompt and
// value are drawn in the content pass); nav is the §2.2 reference graph.
// ---------------------------------------------------------------------------

TEST(MenuEnginePins, name_entry_exact_table)
{
    static const ExpectedButton kExpected[] = {
        {"back", "BACK", KEYSTATE_ESCAPE, 10, 170, 44, 20,
         button_action_id(ButtonAction::MenuSpecRow), 0, MenuNav{.up = 2}},
        {"company_name_value", "", KEYSTATE_UNKNOWN, 86, 70, 148, 22,
         button_action_id(ButtonAction::MenuSpecRow), 1, MenuNav{.down = 2}},
        {"company_name_reroll", "REROLL", KEYSTATE_UNKNOWN, 86, 102, 68, 14,
         button_action_id(ButtonAction::MenuSpecRow), 2,
         MenuNav{.up = 1, .down = 0, .right = 3}},
        {"company_name_accept", "ACCEPT", KEYSTATE_UNKNOWN, 166, 102, 68, 14,
         button_action_id(ButtonAction::MenuSpecRow), 3,
         MenuNav{.up = 1, .down = 0, .left = 2}},
    };
    button* buttons = picker_name_entry_buttons();
    const int count = picker_name_entry_button_count();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)), "name_entry");

    // §2.0 U5 soft-keyboard lint: an in-place-editable field must sit with
    // y+h <= 100 so a web soft keyboard never covers it. The box (row 1) is
    // the editable field; the in-place editor opens at y=82 inside it.
    EXPECT_LE(buttons[1].y + buttons[1].sizey, 100)
        << "name box must satisfy the soft-keyboard lint (y+h <= 100)";
}

// ---------------------------------------------------------------------------
// §2.3 Company List (Load): a Layer-F engine screen. 8 visual rows, each a
// (row, BK, X) triple inside the centered 220px slot column — rects transcribed
// from the design amendment INDEPENDENTLY of the spec's macro: company
// (50,35+17i,164,10), BK (218,35+17i,24,10), and X
// (246,35+17i,24,10), followed by the centered footer controls. Every row
// dispatches through MenuSpecRow with arg == spec ordinal (rows 0-7,
// BK 8-15, X 16-23, back 24, prev 25, next 26).
// Static nav is the full-page multi-page shape.
// ---------------------------------------------------------------------------

TEST(MenuEnginePins, company_list_exact_table)
{
    button* buttons = picker_company_list_buttons();
    const int count = picker_company_list_button_count();
    ASSERT_EQ(27, count) << "8 row triples + back + prev + next";
    const Sint32 spec_row = button_action_id(ButtonAction::MenuSpecRow);

    const auto check_row = [&](int index, const std::string& id,
                               const char* label, int x, int y, int w, int h,
                               MenuNav nav, bool hidden) {
        const button& got = buttons[index];
        EXPECT_EQ(id, got.id) << "company_list index " << index;
        EXPECT_EQ(label, got.label) << "company_list " << id;
        EXPECT_EQ(x, got.x) << "company_list " << id;
        EXPECT_EQ(y, got.y) << "company_list " << id;
        EXPECT_EQ(w, got.sizex) << "company_list " << id;
        EXPECT_EQ(h, got.sizey) << "company_list " << id;
        EXPECT_EQ(spec_row, got.myfun) << "company_list " << id;
        EXPECT_EQ(index, got.arg1)
            << "company_list " << id << " (MenuSpecRow arg == ordinal)";
        EXPECT_EQ(nav.up, got.nav.up) << "company_list " << id;
        EXPECT_EQ(nav.down, got.nav.down) << "company_list " << id;
        EXPECT_EQ(nav.left, got.nav.left) << "company_list " << id;
        EXPECT_EQ(nav.right, got.nav.right) << "company_list " << id;
        EXPECT_EQ(hidden, got.hidden) << "company_list " << id;
        EXPECT_FALSE(got.no_draw) << "company_list " << id;
    };

    for (int i = 0; i < 8; ++i) {
        const int y = 35 + 17 * i;
        check_row(i, "company_row_" + std::to_string(i), "", 50, y, 164, 10,
                  MenuNav{.up = i > 0 ? i - 1 : 24,
                          .down = i < 7 ? i + 1 : 24,
                          .left = -1, .right = 8 + i},
                  false);
        check_row(8 + i, "company_bak_" + std::to_string(i), "BK", 218, y,
                  24, 10,
                  MenuNav{.up = i > 0 ? 8 + i - 1 : -1,
                          .down = i < 7 ? 8 + i + 1 : 24,
                          .left = i, .right = 16 + i},
                  false);
        check_row(16 + i, "company_del_" + std::to_string(i), "X", 246, y,
                  24, 10,
                  MenuNav{.up = i > 0 ? 16 + i - 1 : -1,
                          .down = i < 7 ? 16 + i + 1 : 25,
                          .left = 8 + i, .right = -1},
                  false);
    }
    check_row(24, "back", "BACK", 50, 169, 40, 20,
              MenuNav{.up = 7, .down = 0, .left = -1, .right = 25}, false);
    EXPECT_EQ(KEYSTATE_ESCAPE, buttons[24].hotkey) << "company_list back";
    check_row(25, "company_page_prev", "PREV", 185, 169, 40, 20,
              MenuNav{.up = 23, .down = -1, .left = 24, .right = 26}, true);
    check_row(26, "company_page_next", "NEXT", 230, 169, 40, 20,
              MenuNav{.up = 23, .down = -1, .left = 25, .right = -1}, true);
}

// ---------------------------------------------------------------------------
// §2.4 Backups sub-view (per company): the Company List chassis minus the
// BK/X columns — 10 full-width snapshot rows transcribed from the design
// table INDEPENDENTLY of the spec's macro (backup_row (25,25+15i,220,10);
// click = restore behind the NO-first confirm), then BACK and the PageModel
// pagers (statically hidden; retention 20 => at most 2 pages). Every row
// dispatches through MenuSpecRow with arg == spec ordinal (rows 0-9,
// back 10, prev 11, next 12).
// ---------------------------------------------------------------------------

TEST(MenuEnginePins, company_backups_exact_table)
{
    button* buttons = picker_company_backups_buttons();
    const int count = picker_company_backups_button_count();
    ASSERT_EQ(13, count) << "10 snapshot rows + back + prev + next";
    const Sint32 spec_row = button_action_id(ButtonAction::MenuSpecRow);

    const auto check_row = [&](int index, const std::string& id,
                               const char* label, int x, int y, int w, int h,
                               MenuNav nav, bool hidden) {
        const button& got = buttons[index];
        EXPECT_EQ(id, got.id) << "company_backups index " << index;
        EXPECT_EQ(label, got.label) << "company_backups " << id;
        EXPECT_EQ(x, got.x) << "company_backups " << id;
        EXPECT_EQ(y, got.y) << "company_backups " << id;
        EXPECT_EQ(w, got.sizex) << "company_backups " << id;
        EXPECT_EQ(h, got.sizey) << "company_backups " << id;
        EXPECT_EQ(spec_row, got.myfun) << "company_backups " << id;
        EXPECT_EQ(index, got.arg1)
            << "company_backups " << id << " (MenuSpecRow arg == ordinal)";
        EXPECT_EQ(nav.up, got.nav.up) << "company_backups " << id;
        EXPECT_EQ(nav.down, got.nav.down) << "company_backups " << id;
        EXPECT_EQ(nav.left, got.nav.left) << "company_backups " << id;
        EXPECT_EQ(nav.right, got.nav.right) << "company_backups " << id;
        EXPECT_EQ(hidden, got.hidden) << "company_backups " << id;
        EXPECT_FALSE(got.no_draw) << "company_backups " << id;
    };

    for (int i = 0; i < 10; ++i) {
        check_row(i, "backup_row_" + std::to_string(i), "", 25, 25 + 15 * i,
                  220, 10,
                  MenuNav{.up = i > 0 ? i - 1 : -1,
                          .down = i < 9 ? i + 1 : 10,
                          .left = -1, .right = -1},
                  false);
    }
    check_row(10, "back", "BACK", 10, 170, 44, 20,
              MenuNav{.up = 9, .down = -1, .left = -1, .right = 11}, false);
    EXPECT_EQ(KEYSTATE_ESCAPE, buttons[10].hotkey) << "company_backups back";
    check_row(11, "backup_page_prev", "PREV", 220, 170, 40, 20,
              MenuNav{.up = 9, .down = -1, .left = 10, .right = 12}, true);
    check_row(12, "backup_page_next", "NEXT", 270, 170, 40, 20,
              MenuNav{.up = 9, .down = -1, .left = 11, .right = -1}, true);
}

// ---------------------------------------------------------------------------
// Train (k_trainmenu_buttons): the six +/- stat pairs beside the portrait,
// PREV/NEXT cyclers, RENAME/DETAILS.. header row, team cycler, ACCEPT /
// VIEW TEAM / BACK bottom row.
// ---------------------------------------------------------------------------

TEST(MenuEnginePins, trainmenu_exact_table)
{
    static const ExpectedButton kExpected[] = {
        {"prev", "PREV", KEYSTATE_UNKNOWN, 10, 40, 40, 20,
         button_action_id(ButtonAction::CycleTeamGuy), -1,
         MenuNav{.down = 2, .right = 1}},
        {"next", "NEXT", KEYSTATE_UNKNOWN, 110, 40, 40, 20,
         button_action_id(ButtonAction::CycleTeamGuy), 1,
         MenuNav{.down = 3, .left = 0, .right = 15}},
        {"dec_str", "", KEYSTATE_UNKNOWN, 16, 70, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_STR,
         MenuNav{.up = 0, .down = 4, .right = 3}},
        {"inc_str", "", KEYSTATE_UNKNOWN, 126, 70, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_STR,
         MenuNav{.up = 1, .down = 5, .left = 2}},
        {"dec_dex", "", KEYSTATE_UNKNOWN, 16, 85, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_DEX,
         MenuNav{.up = 2, .down = 6, .right = 5}},
        {"inc_dex", "", KEYSTATE_UNKNOWN, 126, 85, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_DEX,
         MenuNav{.up = 3, .down = 7, .left = 4}},
        {"dec_con", "", KEYSTATE_UNKNOWN, 16, 100, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_CON,
         MenuNav{.up = 4, .down = 8, .right = 7}},
        {"inc_con", "", KEYSTATE_UNKNOWN, 126, 100, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_CON,
         MenuNav{.up = 5, .down = 9, .left = 6}},
        {"dec_int", "", KEYSTATE_UNKNOWN, 16, 115, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_INT,
         MenuNav{.up = 6, .down = 10, .right = 9}},
        {"inc_int", "", KEYSTATE_UNKNOWN, 126, 115, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_INT,
         MenuNav{.up = 7, .down = 11, .left = 8}},
        {"dec_armor", "", KEYSTATE_UNKNOWN, 16, 130, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_ARMOR,
         MenuNav{.up = 8, .down = 12, .right = 11}},
        {"inc_armor", "", KEYSTATE_UNKNOWN, 126, 130, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_ARMOR,
         MenuNav{.up = 9, .down = 13, .left = 10}},
        {"dec_level", "", KEYSTATE_UNKNOWN, 16, 145, 16, 10,
         button_action_id(ButtonAction::DecreaseStat), BUT_LEVEL,
         MenuNav{.up = 10, .down = 18, .right = 13}},
        {"inc_level", "", KEYSTATE_UNKNOWN, 126, 145, 16, 12,
         button_action_id(ButtonAction::IncreaseStat), BUT_LEVEL,
         MenuNav{.up = 11, .down = 14, .left = 12, .right = 17}},
        // §2.5: the VIEW TEAM door retired with its screen (the base-camp
        // roster IS the team view) — rows past the stat pairs shifted down
        // one; kTrainMenuChangeTeamIndex (17) anchors the live label write.
        {"accept", "ACCEPT", KEYSTATE_UNKNOWN, 80, 170, 80, 20,
         button_action_id(ButtonAction::EditGuy), -1,
         MenuNav{.up = 13, .left = 18}},
        {"rename", "RENAME", KEYSTATE_UNKNOWN, 174, 8, 64, 22,
         button_action_id(ButtonAction::NameGuy), 1,
         MenuNav{.down = 17, .left = 1, .right = 16}},
        {"details", "DETAILS..", KEYSTATE_UNKNOWN, 240, 8, 64, 22,
         button_action_id(ButtonAction::CreateDetailMenu), 0,
         MenuNav{.down = 17, .left = 15}},
        {"change_team", "Playing on Team X", KEYSTATE_UNKNOWN, 174, 138, 133,
         22, button_action_id(ButtonAction::ChangeTeam), 1,
         MenuNav{.up = 16, .down = 14, .left = 13}},
        {"back", "BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20,
         button_action_id(ButtonAction::ReturnMenu), MENU_EXIT,
         MenuNav{.up = 12, .right = 14}},
    };
    static_assert(std::size(kExpected) == 19,
                  "train table: change_team at kTrainMenuChangeTeamIndex");
    button* buttons = picker_trainmenu_buttons();
    const int count = picker_trainmenu_button_count();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)), "trainmenu");
}

// ---------------------------------------------------------------------------
// Hire (k_hiremenu_buttons): PREV/NEXT candidate cyclers, the hire-team
// cycler (label rewritten by change_hire_teamnum via index 2 — the G8 raw
// allbuttons_[2] write), HIRE ME, BACK.
// ---------------------------------------------------------------------------

TEST(MenuEnginePins, hiremenu_exact_table)
{
    static const ExpectedButton kExpected[] = {
        {"prev", "PREV", KEYSTATE_UNKNOWN, 10, 40, 40, 20,
         button_action_id(ButtonAction::CycleGuy), -1,
         MenuNav{.down = 4, .right = 1}},
        {"next", "NEXT", KEYSTATE_UNKNOWN, 110, 40, 40, 20,
         button_action_id(ButtonAction::CycleGuy), 1,
         MenuNav{.down = 3, .left = 0, .right = 3}},
        {"change_hire_team", "hiring for team X", KEYSTATE_UNKNOWN, 190, 170,
         110, 20, button_action_id(ButtonAction::ChangeHireTeam), 1,
         MenuNav{.up = 1, .left = 3}},
        {"hire_me", "HIRE ME", KEYSTATE_UNKNOWN, 82, 166, 88, 28,
         button_action_id(ButtonAction::AddGuy), -1,
         MenuNav{.up = 1, .left = 4, .right = 2}},
        {"back", "BACK", KEYSTATE_ESCAPE, 10, 170, 40, 20,
         button_action_id(ButtonAction::ReturnMenu), MENU_EXIT,
         MenuNav{.up = 0, .right = 3}},
    };
    button* buttons = picker_hiremenu_buttons();
    const int count = picker_hiremenu_button_count();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)), "hiremenu");
}

// ---------------------------------------------------------------------------
// Progress (create_progress_menu, picker_team_build.cpp): the button table
// is FUNCTION-LOCAL, so there is no accessor to transcribe from. Pin the
// live vbutton surface instead: enter the screen for real, snapshot
// allbuttons via the interaction API, and compare against the transcription.
//
// Fields not observable through the live surface, transcribed here for the
// migration-time differential — VERIFIED at migration time against the
// deleted function-local table and now carried by kProgressMenuRows
// (menu_screen_specs.cpp):
//   prev: myfun=0 (keyboard-dead by design at Layer E), arg=-1,
//         nav{.right=1}, hotkey=KEYSTATE_UNKNOWN
//   next: myfun=0, arg=-1, nav{.left=0, .right=2}, hotkey=KEYSTATE_UNKNOWN
//   back: myfun=ReturnMenu, arg=MENU_EXIT, nav{.left=1},
//         hotkey=KEYSTATE_ESCAPE
// (This comment originally transcribed next's arg as 1; the deleted table
// read `0, -1` — the arg was never dispatched for the myfun=0 pagers.)
// PREV/NEXT are dispatched by raw mouse-rect checks in the screen's frame
// hook, never do_call (the legacy loop's retvalue==MENU_OK branches were
// dead code); the screen returns MENU_REDRAW to its parent.
// ---------------------------------------------------------------------------

namespace
{
struct ProgressPinState
{
    std::atomic<bool> finished{false};
    bool saw_menu = false;
    std::vector<Interactable> snapshot;
};

int progress_pin_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<ProgressPinState*>(data);

    // "prev" is unique to the progress menu on this path (the ambiguous
    // per-screen "back" is not a safe wait target).
    if (!wait_for_interactable("prev", 10000))
    {
        state->finished.store(true, std::memory_order_relaxed);
        return 0;
    }
    state->saw_menu = true;
    // Let the blocking loop settle before snapshotting/clicking.
    SDL_Delay(500);
    state->snapshot = get_interactables();
    SDL_Delay(300);
    interact("back");
    state->finished.store(true, std::memory_order_relaxed);
    return 0;
}
} // namespace

TEST(MenuEnginePins, progress_menu_exact_layout)
{
    struct ExpectedInteractable
    {
        const char* id;
        const char* label;
        int x, y, w, h;
    };
    static const ExpectedInteractable kExpected[] = {
        {"prev", "PREV", 30, 170, 40, 20},
        {"next", "NEXT", 80, 170, 40, 20},
        {"back", "BACK", 260, 170, 50, 20},
    };

    auto& save = og::runtime::current_session->myscreen_->save_data;
    const short old_scen = save.scen_num;
    const unsigned char old_players = save.numplayers;
    const std::string old_campaign = save.current_campaign;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";

    clear_events();
    auto& input_hw = input_hardware_state();
    input_hw.mouse.left = 0;
    input_hw.mouse.right = 0;
    input_hw.picker_was_left_down = false;
    input_hw.picker_was_right_down = false;

    ProgressPinState state;
    SDL_Thread* thread =
        SDL_CreateThread(progress_pin_injector, "progress_pin", &state);
    ASSERT_TRUE(thread != nullptr) << "injector thread started";

    const Sint32 r = create_progress_menu(0);

    SDL_WaitThread(thread, nullptr);
    clear_events();

    save.scen_num = old_scen;
    save.numplayers = old_players;
    save.current_campaign = old_campaign;

    ASSERT_TRUE(state.finished.load(std::memory_order_relaxed));
    ASSERT_TRUE(state.saw_menu) << "progress menu should have opened";
    ASSERT_EQ(MENU_REDRAW, r)
        << "progress BACK returns REDRAW to the SCENARIO subscreen";

    ASSERT_EQ(std::size(kExpected), state.snapshot.size())
        << "progress screen is exactly PREV | NEXT | BACK";
    for (std::size_t i = 0; i < std::size(kExpected); ++i)
    {
        const ExpectedInteractable& want = kExpected[i];
        const Interactable& got = state.snapshot[i];
        EXPECT_EQ(want.id, got.id) << "progress index " << i;
        EXPECT_EQ(want.label, got.label) << "progress " << got.id;
        EXPECT_EQ(want.x, got.x) << "progress " << got.id;
        EXPECT_EQ(want.y, got.y) << "progress " << got.id;
        EXPECT_EQ(want.w, got.width) << "progress " << got.id;
        EXPECT_EQ(want.h, got.height) << "progress " << got.id;
        EXPECT_FALSE(got.hidden) << "progress " << got.id;
    }
}
