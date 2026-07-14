/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Menu-flow tests for CursesPickerClient (the TUI picker front-end).
 *
 * Every flow is driven headlessly: a HeadlessTerminal supplies a scripted key
 * sequence and records what was drawn, while a FakeClock removes all real time.
 * The picker's menus block on ITerminal::poll_key, so each test scripts the
 * exact keys for the flow under test, then asserts on the resulting SaveData
 * (and, where useful, on what landed on the terminal). No SDL, no TTY, no game
 * loop (run_game / the level runtime are covered by the runtime tests).
 *
 * Menu navigation contract exercised here (see curses_picker_client.cpp):
 *   - Up/'k' and Down/'j' move the highlight; Enter/Space select.
 *   - Digit 'N' jumps the highlight to the N-th selectable entry.
 *   - Esc/'q'/Backspace cancel (Quit on Main, Back elsewhere).
 *   - show_text / "press a key" screens consume exactly one key.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_picker_client.h>
#include <openglad/platform/curses/headless_terminal.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <format>
#include <memory>
#include <string>

using namespace og::curses;
using og::ui::PickerMenuCommand;
using og::ui::PickerMenuId;
using og::ui::TextPickerConfig;

namespace {

// Bundles the terminal/clock/config + client so each test reads compactly.
// A generous 40x100 grid leaves room for every menu the picker draws.
struct PickerFixture {
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client{term, clock, config, options};

    HeadlessTerminal& t() { return term; }
    SaveData& save() { return client.save_data(); }
};

// Select the item at 0-based selectable index `n` from the current menu:
// jump to it with a digit (1..9) and confirm with Enter.
void pick(HeadlessTerminal& term, int n)
{
    ASSERT_GE(n, 0);
    ASSERT_LE(n, 8) << "digit-jump only addresses the first 9 selectable items";
    term.push_char(static_cast<char32_t>(U'1' + n));
    term.push_special(KeyCode::Enter);
}

// Dismiss a "press any key" text screen.
void dismiss(HeadlessTerminal& term)
{
    term.push_special(KeyCode::Enter);
}

// Count non-null team members.
int team_count(const SaveData& save)
{
    int n = 0;
    og::ui::for_each_team_member(save, [&](int, const guy&) { ++n; });
    return n;
}

// Find a Main-menu item's 0-based index among the menu's selectable items.
// (The Main menu has no header rows, so this equals the digit-jump position.)
int main_menu_item_index(PickerMenuCommand command, int arg = 0)
{
    const auto& def = og::ui::picker_menu_definition(PickerMenuId::Main);
    for (int i = 0; i < static_cast<int>(def.items.size()); ++i)
        if (def.items[static_cast<size_t>(i)].command == command &&
            def.items[static_cast<size_t>(i)].arg == arg)
            return i;
    return -1;
}

} // namespace

// --- construction --------------------------------------------------------

// The constructor must guarantee a starting team and starting gold, exactly
// like TextPickerClient::ensure_team_initialized.
TEST(CursesPickerClient, ctor_initializes_team_and_gold)
{
    PickerFixture f;
    EXPECT_EQ(team_count(f.save()), 1);
    EXPECT_EQ(static_cast<unsigned>(f.save().team_size), 1u);
    EXPECT_EQ(f.save().m_totalcash[0], 5000u);
    // The default family is SOLDIER.
    EXPECT_EQ(f.save().team_list[0]->family, FAMILY_SOLDIER);
}

// --- present_menu --------------------------------------------------------

// present_menu returns the highlighted item on Enter and renders the title.
TEST(CursesPickerClient, present_menu_returns_selected_item)
{
    PickerFixture f;
    // Highlight starts on item 0 (Begin New Game); confirm immediately.
    f.t().push_special(KeyCode::Enter);
    const auto* item = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::BeginNewGame);
    EXPECT_NE(f.t().text_row(0).find("OpenGlad"), std::string::npos)
        << "menu title should render on the top row";
}

// Esc on the Main menu cancels to Quit; on other menus it cancels to Back.
TEST(CursesPickerClient, present_menu_cancel_maps_to_quit_or_back)
{
    PickerFixture f;
    f.t().push_special(KeyCode::Escape);
    const auto* main_item = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(main_item, nullptr);
    EXPECT_EQ(main_item->command, PickerMenuCommand::Quit);

    f.t().push_char(U'q');
    const auto* tb_item = f.client.present_menu(PickerMenuId::TeamBuild);
    ASSERT_NE(tb_item, nullptr);
    EXPECT_EQ(tb_item->command, PickerMenuCommand::Back);
}

// Digit jump + Enter selects the addressed item; arrow keys also move.
TEST(CursesPickerClient, present_menu_digit_and_arrow_navigation)
{
    PickerFixture f;
    // Jump to the 7th selectable item (the Difficulty door) and select it.
    const int diff_idx = main_menu_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(diff_idx, 0);
    f.t().push_char(static_cast<char32_t>(U'1' + diff_idx));
    f.t().push_special(KeyCode::Enter);
    const auto* item = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::OpenDifficultyMenu);

    // From the top item, Down once then Enter selects the 2nd item.
    f.t().push_special(KeyCode::Down);
    f.t().push_special(KeyCode::Enter);
    const auto* second = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->command, PickerMenuCommand::ContinueGame);
}

TEST(CursesPickerClient, present_menu_ignores_releases_and_accepts_space)
{
    PickerFixture f;
    f.t().push_char_release(U'x');
    f.t().push_char(U' ');
    const auto* item = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::BeginNewGame);
}

TEST(CursesPickerClient, present_menu_team_build_repopulates_empty_config)
{
    PickerFixture f;
    f.config.team_families.clear();
    f.save().team_size = 0;
    for (auto& slot : f.save().team_list)
        slot.reset();

    f.t().push_char(U'q');
    const auto* item = f.client.present_menu(PickerMenuId::TeamBuild);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::Back);
    EXPECT_FALSE(f.config.team_families.empty());
    EXPECT_GE(team_count(f.save()), 1);
}

// --- new game ------------------------------------------------------------

// prepare_new_game resets the team to a fresh single member and restores gold.
TEST(CursesPickerClient, prepare_new_game_populates_team_and_resets_gold)
{
    PickerFixture f;
    // Spend gold and grow the team first so the reset is observable.
    f.save().m_totalcash[0] = 17u;
    f.save().team_size = 0;
    for (auto& slot : f.save().team_list)
        slot.reset();

    ASSERT_TRUE(f.client.prepare_new_game());

    EXPECT_GE(team_count(f.save()), 1) << "new game must populate a team";
    EXPECT_EQ(f.save().m_totalcash[0], 5000u) << "new game resets gold to 5000";
    EXPECT_EQ(f.config.team_families, og::ui::collect_team_families(f.save()))
        << "config team_families must be synced from the save";
}

// A new game must drop a previously selected campaign back to the default —
// in the save, in the session config (run_game copies config_.campaign over
// the freshly reset save), and in the mounted package.
TEST(CursesPickerClient, prepare_new_game_resets_campaign_to_default)
{
    PickerFixture f;
    f.config.campaign = "org.openglad.ctf";
    f.save().current_campaign = "org.openglad.ctf";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.ctf"))
        << "the ctf package ships with the game and should mount";

    ASSERT_TRUE(f.client.prepare_new_game());

    EXPECT_EQ(f.config.campaign, "org.openglad.gladiator")
        << "a stale config campaign would be copied back onto the save at GO";
    EXPECT_EQ(f.save().current_campaign, "org.openglad.gladiator");
    EXPECT_EQ(get_mounted_campaign(), "org.openglad.gladiator")
        << "the in-picker scenario viewer reads the mounted package";
}

// --- difficulty ----------------------------------------------------------

// Handling the Difficulty item (now in the Difficulty submenu) cycles
// options_.difficulty and shows a notice.
TEST(CursesPickerClient, difficulty_cycles_on_select)
{
    PickerFixture f;
    const int before = f.client.difficulty();
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::SetDifficulty);
    ASSERT_NE(item, nullptr);

    dismiss(f.t()); // the post-cycle "press a key" notice
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);

    EXPECT_EQ(f.client.difficulty(), og::ui::cycle_difficulty(before));
    // The notice should name the new difficulty somewhere on screen.
    bool found = false;
    for (int r = 0; r < f.t().rows(); ++r)
        if (f.t().text_row(r).find(og::ui::kDifficultyNames[f.client.difficulty()]) !=
            std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "difficulty notice should render the new difficulty name";
}

// --- player count --------------------------------------------------------

// The "N Player" items set numplayers via the shared set_player_count.
TEST(CursesPickerClient, player_count_cycles)
{
    PickerFixture f;
    for (int n : {4, 2, 1}) {
        const auto* item = og::ui::find_picker_menu_item(
            PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, n);
        ASSERT_NE(item, nullptr) << "missing player-mode item for " << n;
        dismiss(f.t());
        f.client.handle_menu_item(PickerMenuId::Main, *item);
        EXPECT_EQ(static_cast<int>(f.save().numplayers), n);
    }
}

// --- allied mode ---------------------------------------------------------

TEST(CursesPickerClient, allied_mode_toggles)
{
    PickerFixture f;
    const bool before = og::ui::is_allied_mode(f.save());
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode);
    ASSERT_NE(item, nullptr);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Main, *item);
    EXPECT_NE(og::ui::is_allied_mode(f.save()), before);
}

TEST(CursesPickerClient, level_edit_notice_renders)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Main, PickerMenuCommand::LevelEdit);
    ASSERT_NE(item, nullptr);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Main, *item);
    EXPECT_NE(f.t().dump().find("openscen"), std::string::npos);
}

// CTF settings cycle only inside the CTF campaign; elsewhere they notify.
TEST(CursesPickerClient, ctf_settings_cycle_on_ctf_campaign_only)
{
    PickerFixture f;
    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfTeamCount);
    const auto* caps_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfCaptureLimit);
    ASSERT_NE(teams_item, nullptr);
    ASSERT_NE(caps_item, nullptr);

    // Classic campaign: settings stay put and the notice renders.
    f.save().current_campaign = "org.openglad.gladiator";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *teams_item);
    EXPECT_EQ(0, (int)f.save().ctf_team_count);
    EXPECT_NE(f.t().dump().find("CTF maps only"), std::string::npos);

    // CTF campaign: both settings cycle (Auto -> 2).
    f.save().current_campaign = "org.openglad.ctf";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *teams_item);
    EXPECT_EQ(2, (int)f.save().ctf_team_count);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *caps_item);
    EXPECT_EQ(1, (int)f.save().ctf_capture_limit);
}

// The team-build labels surface the live CTF settings.
TEST(CursesPickerClient, ctf_menu_labels_format_from_save)
{
    PickerFixture f;
    f.save().ctf_team_count = 4;
    f.save().ctf_capture_limit = 0;
    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfTeamCount);
    const auto* caps_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfCaptureLimit);
    ASSERT_NE(teams_item, nullptr);
    ASSERT_NE(caps_item, nullptr);

    // Drive present_menu so the dynamic labels render in the list.
    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::TeamBuild);
    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Teams: 4"), std::string::npos) << dump;
    EXPECT_NE(dump.find("Limit: Map"), std::string::npos) << dump;
}

// --- view roster ---------------------------------------------------------

// Viewing the roster renders each member's name to the terminal.
TEST(CursesPickerClient, view_roster_renders_names)
{
    PickerFixture f;
    const std::string name = f.save().team_list[0]->name;
    ASSERT_FALSE(name.empty());

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    ASSERT_NE(item, nullptr);
    // View Team shows a scrollable roster screen that consumes one key.
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    bool found = false;
    for (int r = 0; r < f.t().rows(); ++r)
        if (f.t().text_row(r).find(name) != std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "roster should render the member's name; got:\n" << f.t().dump();
}

// --- hire ----------------------------------------------------------------

// Hiring a recruit adds a team member and deducts gold.
TEST(CursesPickerClient, hire_adds_member_and_deducts_gold)
{
    PickerFixture f;
    const int before_count = team_count(f.save());
    const std::uint32_t before_gold = f.save().m_totalcash[0];
    ASSERT_GT(before_gold, 0u);

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);

    // Hire screen entries: 3 stat headers, then [Hire(0) Next(1) Prev(2) Back(3)].
    // The first selectable is "Hire this recruit"; choose it (Enter, since it is
    // the initial highlight), dismiss the "Hired!" notice, then Back out.
    f.t().push_special(KeyCode::Enter); // Hire
    dismiss(f.t());                     // "Hired X!" notice
    f.t().push_char(U'q');              // Back out of the hire loop
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(team_count(f.save()), before_count + 1) << "hire should add a member";
    EXPECT_LT(f.save().m_totalcash[0], before_gold) << "hire should deduct gold";
    EXPECT_EQ(f.config.team_families, og::ui::collect_team_families(f.save()))
        << "team_families should resync after hiring";
}

// Cancelling the hire screen leaves the team and gold unchanged.
TEST(CursesPickerClient, hire_cancel_is_noop)
{
    PickerFixture f;
    const int before_count = team_count(f.save());
    const std::uint32_t before_gold = f.save().m_totalcash[0];

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);
    f.t().push_special(KeyCode::Escape); // cancel immediately
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(team_count(f.save()), before_count);
    EXPECT_EQ(f.save().m_totalcash[0], before_gold);
}

TEST(CursesPickerClient, hire_full_team_reports_and_returns)
{
    PickerFixture f;
    f.save().m_totalcash[0] = 50000u;
    for (auto& slot : f.save().team_list)
        slot.reset();
    f.save().team_size = 0;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        f.save().team_list[static_cast<size_t>(i)] = std::make_unique<guy>(FAMILY_SOLDIER);
        f.save().team_list[static_cast<size_t>(i)]->teamnum = 0;
        f.save().team_size++;
    }

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);
    EXPECT_NE(f.t().dump().find("max size"), std::string::npos);
}

TEST(CursesPickerClient, hire_navigation_next_prev_and_back)
{
    PickerFixture f;
    const int before_count = team_count(f.save());

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);
    pick(f.t(), 1); // Next family
    pick(f.t(), 2); // Previous family
    pick(f.t(), 3); // Back
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(team_count(f.save()), before_count);
}

// --- train ---------------------------------------------------------------

// Training raises a stat and charges gold on accept.
TEST(CursesPickerClient, train_raises_stat_at_a_cost)
{
    PickerFixture f;
    const short before_str = f.save().team_list[0]->strength;
    const std::uint32_t before_gold = f.save().m_totalcash[0];

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::TrainTeam);
    ASSERT_NE(item, nullptr);

    // Train entries: stats STR(0)..LVL(5), a cost header, then
    // [Accept(7) Next(8) Prev(9) Back(10)] as selectable indices.
    // STR is the initial highlight: Enter raises it by 1. Then jump to Accept
    // (selectable index 6) with digit '7', dismiss the notice, and Back out.
    f.t().push_special(KeyCode::Enter); // +1 STR (highlight starts on STR)
    f.t().push_char(U'7');              // jump to "Accept training"
    f.t().push_special(KeyCode::Enter); // accept
    dismiss(f.t());                     // "Training accepted." notice
    f.t().push_char(U'q');              // Back
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(f.save().team_list[0]->strength, before_str + 1)
        << "training should raise the chosen stat by 1";
    EXPECT_LT(f.save().m_totalcash[0], before_gold)
        << "accepting training should charge gold";
}

TEST(CursesPickerClient, train_empty_team_reports_and_returns)
{
    PickerFixture f;
    f.save().team_size = 0;
    for (auto& slot : f.save().team_list)
        slot.reset();

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::TrainTeam);
    ASSERT_NE(item, nullptr);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_NE(f.t().dump().find("No team members"), std::string::npos);
}

TEST(CursesPickerClient, train_navigation_next_prev_and_back)
{
    PickerFixture f;
    {
        og::ui::HireSession session(f.save(), 0);
        ASSERT_GE(session.hire(), 0);
    }
    const short member0_before = f.save().team_list[0]->strength;
    const short member1_before = f.save().team_list[1]->strength;

    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::TrainTeam);
    ASSERT_NE(item, nullptr);
    pick(f.t(), 7); // Next member
    pick(f.t(), 8); // Previous member
    f.t().push_char(U'q'); // Back
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(f.save().team_list[0]->strength, member0_before);
    EXPECT_EQ(f.save().team_list[1]->strength, member1_before);
}

// --- set level -----------------------------------------------------------

// Set Level updates both the config and the save's scenario number.
TEST(CursesPickerClient, set_level_updates_config_and_save)
{
    PickerFixture f;
    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::SetLevel);
    ASSERT_NE(item, nullptr);

    // The prompt is pre-filled with the current level; clear it and type 4.
    f.t().push_special(KeyCode::Backspace); // erase the prefilled "1"
    f.t().push_char(U'4');
    f.t().push_special(KeyCode::Enter);
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_EQ(f.config.level, 4);
    EXPECT_EQ(static_cast<int>(f.save().scen_num), 4);
}

TEST(CursesPickerClient, set_level_rejects_invalid_value)
{
    PickerFixture f;
    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::SetLevel);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Backspace); // erase the prefilled "1"
    f.t().push_char(U'0');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_EQ(f.config.level, 1);
    EXPECT_NE(f.t().dump().find("Invalid level"), std::string::npos);
}

// --- campaign select -----------------------------------------------------

// Selecting a campaign updates config + save; cancelling keeps the current one.
TEST(CursesPickerClient, campaign_select_updates_and_cancel_keeps_current)
{
    PickerFixture f;

    // Cancel: Esc keeps the current campaign unchanged.
    const std::string original = f.config.campaign;
    f.t().push_special(KeyCode::Escape);
    EXPECT_EQ(f.client.show_campaign_select(), original);
    EXPECT_EQ(f.config.campaign, original);

    // Select: confirm the highlighted (current) campaign; config + save match.
    f.t().push_special(KeyCode::Enter);
    const std::string chosen = f.client.show_campaign_select();
    EXPECT_FALSE(chosen.empty());
    EXPECT_EQ(f.config.campaign, chosen);
    EXPECT_EQ(f.save().current_campaign, chosen);
}

// --- save / load round-trip ----------------------------------------------

// Saving then loading restores the same team (size, names, families).
TEST(CursesPickerClient, save_then_load_round_trips_team)
{
    // Use a unique save slot under the test's isolated config dir.
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    config.save_name = "curses_picker_roundtrip";
    config.level = 3;
    CursesPickerOptions options;
    CursesPickerClient client{term, clock, config, options};

    // Grow the roster so the round-trip is meaningful (hire a second soldier).
    {
        og::ui::HireSession session(client.save_data(), 0);
        ASSERT_GE(session.hire(), 0);
    }
    const int saved_count = team_count(client.save_data());
    ASSERT_GE(saved_count, 2);
    const std::string member0 = client.save_data().team_list[0]->name;
    const std::string member1 = client.save_data().team_list[1]->name;

    term.push_special(KeyCode::Enter); // dismiss "Saved" notice
    ASSERT_TRUE(client.save_game());

    // Wipe the in-memory team, then load it back.
    client.save_data().team_size = 0;
    for (auto& slot : client.save_data().team_list)
        slot.reset();
    ASSERT_EQ(team_count(client.save_data()), 0);

    term.push_special(KeyCode::Enter); // dismiss "Loaded" notice
    ASSERT_TRUE(client.load_game());

    EXPECT_EQ(team_count(client.save_data()), saved_count);
    EXPECT_EQ(client.save_data().team_list[0]->name, member0);
    EXPECT_EQ(client.save_data().team_list[1]->name, member1);
    EXPECT_EQ(config.level, 3) << "level should restore from the save's scen_num";
}

TEST(CursesPickerClient, team_build_dispatches_save_load_progress_network_and_campaign)
{
    PickerFixture f;
    f.config.save_name = "curses_dispatch_slot";

    const auto* save_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::SaveTeam);
    const auto* load_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::LoadTeam);
    const auto* progress_item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::ShowProgress);
    const auto* network_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::Networking);
    const auto* campaign_item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::SetCampaign);
    ASSERT_NE(save_item, nullptr);
    ASSERT_NE(load_item, nullptr);
    ASSERT_NE(progress_item, nullptr);
    ASSERT_NE(network_item, nullptr);
    ASSERT_NE(campaign_item, nullptr);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *save_item);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *load_item);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *progress_item);
    pick(f.t(), 2);
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *network_item);
    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *campaign_item);

    EXPECT_EQ(f.save().current_campaign, f.config.campaign);
}

// Progress shows human titles. The scenario title is read off the MOUNTED
// package, so it only renders while the mount matches the configured
// campaign; a mismatch falls back to the bare level number rather than
// showing another campaign's title.
TEST(CursesPickerClient, progress_level_title_requires_matching_mount)
{
    PickerFixture f;
    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::ShowProgress);
    ASSERT_NE(item, nullptr);

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));
    f.config.campaign = "org.openglad.ctf"; // configured != mounted
    f.config.level = 4;

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Campaign: Capture the Flag"), std::string::npos) << dump;
    EXPECT_NE(dump.find("Level: 4"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("Level: 4."), std::string::npos)
        << "a mismatched mount must not show another campaign's title";
}

// Loading a non-existent slot fails gracefully and returns false.
TEST(CursesPickerClient, load_missing_slot_fails_gracefully)
{
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    config.save_name = "curses_picker_does_not_exist_42";
    CursesPickerOptions options;
    CursesPickerClient client{term, clock, config, options};

    term.push_special(KeyCode::Enter); // dismiss "Load failed" notice
    EXPECT_FALSE(client.load_game());
}

// --- options -------------------------------------------------------------

// Options accepts a new save slot and a new seed via text prompts.
TEST(CursesPickerClient, options_set_save_slot_and_seed)
{
    PickerFixture f;
    f.config.save_name = "old_slot";
    f.config.seed = 7;

    // First prompt: clear "old_slot" and type "new_slot". Backspace count must
    // match the prefilled length.
    const std::string old_slot = "old_slot";
    for (size_t i = 0; i < old_slot.size(); ++i)
        f.t().push_special(KeyCode::Backspace);
    f.t().push_string("new_slot");
    f.t().push_special(KeyCode::Enter);

    // Second prompt: seed prefilled with "7"; erase and type "123".
    f.t().push_special(KeyCode::Backspace);
    f.t().push_string("123");
    f.t().push_special(KeyCode::Enter);

    f.client.show_options();

    EXPECT_EQ(f.config.save_name, "new_slot");
    EXPECT_EQ(f.config.seed, 123u);
}

// Cancelling the options prompts (Esc) leaves config untouched.
TEST(CursesPickerClient, options_cancel_keeps_config)
{
    PickerFixture f;
    f.config.save_name = "keep_me";
    f.config.seed = 99;

    f.t().push_special(KeyCode::Escape); // cancel save-slot prompt
    f.t().push_special(KeyCode::Escape); // cancel seed prompt
    f.client.show_options();

    EXPECT_EQ(f.config.save_name, "keep_me");
    EXPECT_EQ(f.config.seed, 99u);
}

TEST(CursesPickerClient, options_invalid_seed_keeps_current)
{
    PickerFixture f;
    f.config.save_name = "slot";
    f.config.seed = 44;

    f.t().push_special(KeyCode::Enter); // accept slot unchanged
    for (int i = 0; i < 2; ++i)
        f.t().push_special(KeyCode::Backspace);
    f.t().push_string("bad");
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.show_options();

    EXPECT_EQ(f.config.save_name, "slot");
    EXPECT_EQ(f.config.seed, 44u);
    EXPECT_NE(f.t().dump().find("Invalid seed"), std::string::npos);
}

// --- help ----------------------------------------------------------------

TEST(CursesPickerClient, help_renders_and_dismisses)
{
    PickerFixture f;
    f.t().push_special(KeyCode::Enter); // dismiss the help screen
    f.client.show_help();
    bool found = false;
    for (int r = 0; r < f.t().rows(); ++r)
        if (f.t().text_row(r).find("Help") != std::string::npos)
            found = true;
    EXPECT_TRUE(found);
}

// A "press any key" screen must dismiss only on a FRESH press — never on a
// key-up or an auto-repeat. This is the level-end modal bug: when a level ends
// because the player walked into the exit, the movement key is still held, and
// its release + auto-repeat would otherwise dismiss the modal instantly (it would
// never be seen). Here the release and repeat must be ignored, so all three
// scripted events are consumed (only the final Enter press dismisses).
TEST(CursesPickerClient, press_any_key_ignores_release_and_repeat)
{
    PickerFixture f;
    f.t().push_char_release(U'd');                          // key-up: ignored
    f.t().push_key(Key::character(U'd', KeyEvent::Repeat)); // auto-repeat: ignored
    f.t().push_special(KeyCode::Enter);                     // fresh press: dismisses
    f.client.show_help();
    EXPECT_TRUE(f.t().input_exhausted())
        << "the release and repeat must not dismiss the modal; only the press does";
}

// --- networking ----------------------------------------------------------
//
// host_game() builds a real WebSocket lobby and blocks in the lobby loop until a
// game starts or the user cancels, so it is exercised end-to-end by the
// CursesNetwork suite (in-process, no real sockets) rather than here. These
// picker tests cover only the menu navigation around it.

TEST(CursesPickerClient, networking_join_cancel_returns_false)
{
    PickerFixture f;
    // Cancel the URL prompt: join_game should report false without a lobby.
    f.t().push_special(KeyCode::Escape);
    EXPECT_FALSE(f.client.join_game());
}

// configure_networking opens the Host/Join/Back submenu; Back returns false.
TEST(CursesPickerClient, configure_networking_back_returns_false)
{
    PickerFixture f;
    // Submenu entries: Host(0), Join(1), Back(2). Jump to Back and select.
    pick(f.t(), 2);
    EXPECT_FALSE(f.client.configure_networking());
}

// --- end-to-end run_picker -----------------------------------------------

// A full run_picker that immediately quits from the Main menu unwinds cleanly.
TEST(CursesPickerClient, run_picker_quit_unwinds)
{
    PickerFixture f;
    f.t().push_special(KeyCode::Escape); // Main menu -> Quit
    // run_picker drives the shared state machine; it must consume exactly the
    // scripted Quit key and return — not loop forever (which would drain past the
    // queue) and not leave the Quit key unconsumed.
    og::ui::run_picker(f.client);
    EXPECT_TRUE(f.t().input_exhausted())
        << "run_picker should consume exactly the scripted Quit and then return";
    // The picker returns to the team-build screen after a game (here, after quit).
    EXPECT_EQ(f.client.screen_after_game(), og::ui::PickerScreen::TeamBuild);
}

// A richer run_picker: cycle difficulty through the Main menu's Difficulty
// door, then quit. Asserts the side effect persisted and the loop unwound.
TEST(CursesPickerClient, run_picker_main_action_then_quit)
{
    PickerFixture f;
    const int before = f.client.difficulty();

    const int diff_idx = main_menu_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(diff_idx, 0);
    // First Main menu pass: open the Difficulty submenu (digit+Enter).
    f.t().push_char(static_cast<char32_t>(U'1' + diff_idx));
    f.t().push_special(KeyCode::Enter);
    // Submenu: the Difficulty row starts highlighted; select it, dismiss its
    // notice, then Esc backs out of the submenu (-> Back).
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.t().push_special(KeyCode::Escape);
    // Second Main menu pass (show_main_menu loops on non-terminal commands):
    // Esc to Quit and end the program.
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ(f.client.difficulty(), og::ui::cycle_difficulty(before));
}

// An end-to-end pass that opens Team Build, enters it (GO! is not pressed),
// and backs out to the Main menu, then quits — covering the TeamBuild screen
// transition inside run_picker.
TEST(CursesPickerClient, run_picker_through_team_build_then_quit)
{
    PickerFixture f;

    // Main pass 1: "Continue Game" -> Team Build.
    const int cont_idx = main_menu_item_index(PickerMenuCommand::ContinueGame);
    ASSERT_GE(cont_idx, 0);
    f.t().push_char(static_cast<char32_t>(U'1' + cont_idx));
    f.t().push_special(KeyCode::Enter);
    // Team Build: Back -> Main menu.
    f.t().push_char(U'q');
    // Main pass 2: Quit.
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);
    // The team survived the round trip.
    EXPECT_GE(team_count(f.save()), 1);
}

// --- CTF scenario-troops toggle -------------------------------------------

// The troops toggle is CTF-campaign gated and flips the strip flag.
TEST(CursesPickerClient, ctf_troops_toggle_on_ctf_campaign_only)
{
    PickerFixture f;
    const auto* troops_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ToggleCtfScenarioTroops);
    ASSERT_NE(troops_item, nullptr);

    // Classic campaign: the toggle refuses and the notice renders.
    f.save().current_campaign = "org.openglad.gladiator";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *troops_item);
    EXPECT_EQ(0, (int)f.save().ctf_strip_scenario_troops);
    EXPECT_NE(f.t().dump().find("CTF maps only"), std::string::npos);

    // CTF campaign: Scen -> Own -> Scen.
    f.save().current_campaign = "org.openglad.ctf";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *troops_item);
    EXPECT_EQ(1, (int)f.save().ctf_strip_scenario_troops);
    EXPECT_NE(f.t().dump().find("Troops: Own"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *troops_item);
    EXPECT_EQ(0, (int)f.save().ctf_strip_scenario_troops);
}

// The team-build label surfaces the live troops setting.
TEST(CursesPickerClient, ctf_troops_label_formats_from_save)
{
    PickerFixture f;
    f.save().ctf_strip_scenario_troops = 1;
    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::TeamBuild);
    EXPECT_NE(f.t().dump().find("Troops: Own"), std::string::npos);
}

// --- Teams screen ----------------------------------------------------------

// Enter on a character cycles its team; "Play on {COLOR}" re-seats P1.
// The Teams item lives in the Scenario submenu now.
TEST(CursesPickerClient, teams_screen_cycles_character_and_sets_my_team)
{
    PickerFixture f;
    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(teams_item, nullptr);
    ASSERT_EQ(1, team_count(f.save()));
    ASSERT_EQ(0, (int)f.save().team_list[0]->teamnum);
    ASSERT_EQ(0, (int)f.save().my_team);

    // 1st selectable = "Play on RED", 2nd = the character: cycle the
    // character onto GREEN, then take the GREEN seat, then leave.
    pick(f.t(), 1);                      // character row -> team 0 -> 1
    pick(f.t(), 0);                      // "Play on GREEN" (team 1)
    f.t().push_special(KeyCode::Escape); // leave the teams screen
    f.client.handle_menu_item(PickerMenuId::Scenario, *teams_item);

    EXPECT_EQ(1, (int)f.save().team_list[0]->teamnum);
    EXPECT_EQ(1, (int)f.save().my_team);
    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("GREEN TEAM (P1) 1 HEROES"), std::string::npos) << dump;
}

// Playing on an empty team is impossible: empty teams offer no Play entry.
TEST(CursesPickerClient, teams_screen_offers_play_only_for_manned_teams)
{
    PickerFixture f;
    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(teams_item, nullptr);

    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *teams_item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Play on RED"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("Play on BLUE"), std::string::npos)
        << "no Play entry for an empty team";
    EXPECT_NE(dump.find("BLUE TEAM 0 HEROES"), std::string::npos) << dump;
}

// The teams roster never truncates at the terminal: the list view scrolls
// to follow the cursor, so a full 24-member team stays reachable AND
// visible on a short terminal (the curses analogue of the SDL pager).
TEST(CursesPickerClient, teams_screen_scrolls_to_keep_cursor_visible)
{
    HeadlessTerminal term{12, 60};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client{term, clock, config, options};

    SaveData& save = client.save_data();
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    for (int i = 0; i < 15; ++i) {
        save.team_list[static_cast<size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<size_t>(i)]->name =
            std::format("Grunt{:02}", i);
        save.team_list[static_cast<size_t>(i)]->teamnum = 0;
        save.team_size++;
    }

    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(teams_item, nullptr);

    // Cursor starts on "Play on RED"; 10 downs land on the 10th member
    // (Grunt09), far past the 12-row terminal's first page.
    for (int i = 0; i < 10; ++i)
        term.push_char(U'j');
    term.push_special(KeyCode::Escape);
    client.handle_menu_item(PickerMenuId::Scenario, *teams_item);

    const std::string dump = term.dump();
    EXPECT_NE(dump.find("Grunt09"), std::string::npos)
        << "the cursor's row must scroll into view; got:\n" << dump;
    EXPECT_EQ(dump.find("Grunt00"), std::string::npos)
        << "rows above the scroll window leave the screen";
}

// --- View Scenario ----------------------------------------------------------

// The viewer renders the shared roster report from a scratch headless load.
// The View Scenario item lives in the Scenario submenu now.
TEST(CursesPickerClient, view_scenario_renders_roster_report)
{
    PickerFixture f;
    const auto* viewer_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(viewer_item, nullptr);
    f.save().current_campaign = "org.openglad.gladiator";
    f.save().scen_num = 1;

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *viewer_item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("SCEN 1:"), std::string::npos) << dump;
    EXPECT_NE(dump.find("TEAM"), std::string::npos) << dump;
}

// --- Scenario submenu --------------------------------------------------------

// run_picker reaches the nested Scenario submenu from Team Build and unwinds
// cleanly: Continue -> Team Build -> Scenario -> Back -> Back -> Quit.
TEST(CursesPickerClient, run_picker_through_scenario_submenu_then_quit)
{
    PickerFixture f;

    const int cont_idx = main_menu_item_index(PickerMenuCommand::ContinueGame);
    ASSERT_GE(cont_idx, 0);
    // Main pass 1: "Continue Game" -> Team Build.
    f.t().push_char(static_cast<char32_t>(U'1' + cont_idx));
    f.t().push_special(KeyCode::Enter);
    // Team Build: the "Scenario" item opens the submenu.
    int scenario_idx = -1;
    {
        const auto& def = og::ui::picker_menu_definition(PickerMenuId::TeamBuild);
        for (int i = 0; i < static_cast<int>(def.items.size()); ++i)
            if (def.items[static_cast<size_t>(i)].command ==
                PickerMenuCommand::Scenario)
                scenario_idx = i;
    }
    ASSERT_GE(scenario_idx, 0);
    // The Team Build list shows non-selectable context headers, but digit
    // jump counts selectable entries only, so the item index maps 1:1.
    ASSERT_LE(scenario_idx, 8) << "digit-jump addresses the first 9 items";
    f.t().push_char(static_cast<char32_t>(U'1' + scenario_idx));
    f.t().push_special(KeyCode::Enter);
    // Scenario submenu: leave with Esc (-> Back), then Team Build Esc,
    // then Main menu Esc (-> Quit).
    f.t().push_special(KeyCode::Escape);
    f.t().push_char(U'q');
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);
    EXPECT_TRUE(f.t().input_exhausted())
        << "the scenario submenu round trip should consume the whole script";
}

// --- Campaign ordering -------------------------------------------------------

// The default campaign lists first; the CTF campaign no longer sorts ahead
// of it alphabetically. Entries render as human titles, not raw ids.
TEST(CursesPickerClient, campaign_select_lists_default_campaign_first)
{
    PickerFixture f;
    f.t().push_special(KeyCode::Escape); // keep the current campaign
    (void)f.client.show_campaign_select();

    const std::string dump = f.t().dump();
    EXPECT_EQ(dump.find("org.openglad"), std::string::npos)
        << "campaign entries must show titles, not raw ids";
    const std::size_t default_pos = dump.find("Gladiator");
    ASSERT_NE(default_pos, std::string::npos) << dump;
    const std::size_t ctf_pos = dump.find("Capture the Flag");
    if (ctf_pos != std::string::npos)
    {
        EXPECT_LT(default_pos, ctf_pos)
            << "the default campaign must list before the CTF campaign";
    }
}

// --- Difficulty submenu ------------------------------------------------------
//
// The Main menu's Difficulty entry is a door into the Difficulty submenu
// (kDifficultyMenuItems): the in-place difficulty cycle plus the match rules
// that ride SaveData (respawns, respawn delay, permadeath, generator rate).
// Each per-command test mirrors the CTF-setting handler tests above: resolve
// the item from the menu model, handle it, assert the save field and the
// shared label the notice renders.

// Respawns: Off -> Heroes -> Everyone -> Off on save.respawn_mode.
TEST(CursesPickerClient, respawn_mode_cycles_through_sequence)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::CycleRespawnMode);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(0, (int)f.save().respawn_mode) << "default is Off (classic)";

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(1, (int)f.save().respawn_mode);
    EXPECT_NE(f.t().dump().find("Respawns: Heroes"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(2, (int)f.save().respawn_mode);
    EXPECT_NE(f.t().dump().find("Respawns: Everyone"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(0, (int)f.save().respawn_mode);
    EXPECT_NE(f.t().dump().find("Respawns: Off"), std::string::npos);
}

// Respawn delay rides the existing ctf_respawn_ticks: 0 -> 60 -> 360 -> 0.
TEST(CursesPickerClient, respawn_delay_cycles_through_sequence)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::CycleRespawnDelay);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(0, (int)f.save().ctf_respawn_ticks) << "default is the map delay";

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(60, (int)f.save().ctf_respawn_ticks);
    EXPECT_NE(f.t().dump().find("Spawn Delay: Fast"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(360, (int)f.save().ctf_respawn_ticks);
    EXPECT_NE(f.t().dump().find("Spawn Delay: Slow"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(0, (int)f.save().ctf_respawn_ticks);
    EXPECT_NE(f.t().dump().find("Spawn Delay: Normal"), std::string::npos);
}

// Permadeath: On (keep_fallen_heroes == 0, classic) <-> Off.
TEST(CursesPickerClient, permadeath_toggles)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::TogglePermadeath);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(0, (int)f.save().keep_fallen_heroes) << "default is permadeath On";

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(1, (int)f.save().keep_fallen_heroes);
    EXPECT_NE(f.t().dump().find("Permadeath: Off"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(0, (int)f.save().keep_fallen_heroes);
    EXPECT_NE(f.t().dump().find("Permadeath: On"), std::string::npos);
}

// Generators: Normal (0) -> Calm (50) -> Frenzy (200) -> Normal.
TEST(CursesPickerClient, generator_rate_cycles_through_sequence)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::CycleGeneratorRate);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(0, (int)f.save().generator_rate) << "default is the classic rate";

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(50, (int)f.save().generator_rate);
    EXPECT_NE(f.t().dump().find("Generators: Calm"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(200, (int)f.save().generator_rate);
    EXPECT_NE(f.t().dump().find("Generators: Frenzy"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(0, (int)f.save().generator_rate);
    EXPECT_NE(f.t().dump().find("Generators: Normal"), std::string::npos);
}

// The submenu rows render the live settings from options_/SaveData, and Esc
// cancels to Back (like every non-Main menu).
TEST(CursesPickerClient, difficulty_submenu_labels_format_from_save)
{
    PickerFixture f;
    f.save().respawn_mode = 1;
    f.save().ctf_respawn_ticks = 60;
    f.save().keep_fallen_heroes = 1;
    f.save().generator_rate = 200;

    f.t().push_special(KeyCode::Escape);
    const auto* item = f.client.present_menu(PickerMenuId::Difficulty);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::Back);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Difficulty: Battle"), std::string::npos)
        << "options_.difficulty defaults to 1 (Battle); got:\n" << dump;
    EXPECT_NE(dump.find("Respawns: Heroes"), std::string::npos) << dump;
    EXPECT_NE(dump.find("Spawn Delay: Fast"), std::string::npos) << dump;
    EXPECT_NE(dump.find("Permadeath: Off"), std::string::npos) << dump;
    EXPECT_NE(dump.find("Generators: Frenzy"), std::string::npos) << dump;
}

// run_picker reaches the nested Difficulty submenu from the Main menu door and
// unwinds cleanly: Difficulty -> cycle Respawns -> Back -> Quit (the curses
// analogue of run_picker_through_scenario_submenu_then_quit above).
TEST(CursesPickerClient, run_picker_through_difficulty_submenu_then_quit)
{
    PickerFixture f;

    const int door_idx = main_menu_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(door_idx, 0);
    ASSERT_LE(door_idx, 8) << "digit-jump addresses the first 9 items";
    // Main pass 1: the Difficulty door opens the submenu.
    f.t().push_char(static_cast<char32_t>(U'1' + door_idx));
    f.t().push_special(KeyCode::Enter);
    // Difficulty submenu: cycle Respawns (2nd selectable row), dismiss the
    // notice, then leave with Esc (-> Back), then Main menu Esc (-> Quit).
    pick(f.t(), 1);
    dismiss(f.t());
    f.t().push_special(KeyCode::Escape);
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ(1, (int)f.save().respawn_mode)
        << "the submenu action must land on the save";
    EXPECT_TRUE(f.t().input_exhausted())
        << "the difficulty submenu round trip should consume the whole script";
}
