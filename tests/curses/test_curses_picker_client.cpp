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

#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/platform/curses/curses_picker_client.h>
#include <openglad/platform/curses/headless_terminal.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/cloud_save_client.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/company.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace og::curses;
using og::ui::PickerMenuCommand;
using og::ui::PickerMenuId;
using og::ui::TextPickerConfig;

namespace {

// Bundles the terminal/clock/config + client so each test reads compactly.
// A generous 40x100 grid leaves room for every menu the picker draws; the
// dimensions are a parameter so a test can put a screen on the stock 24x80
// terminal a real player has (the camp composes more lines than that holds).
struct PickerFixture {
    HeadlessTerminal term;
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client;

    explicit PickerFixture(CursesPickerOptions initial_options = {},
                           int rows = 40, int cols = 100)
        : term(rows, cols), options(std::move(initial_options)),
          client(term, clock, config, options)
    {
    }

    HeadlessTerminal& t() { return term; }
    SaveData& save() { return client.save_data(); }

    // §3.8: hire/train/deploy mutations AUTOSAVE the active slot now, so a
    // fixture test can leave a company file behind — which would pollute
    // the company-list tests' row ordering under any test order. Reap the
    // fixture slot's file (the active slot may have been repointed by an
    // open flow, so reap both).
    ~PickerFixture()
    {
        (void)remove_user_file("save/" + config.save_name + ".gtl");
        (void)remove_user_file(
            "save/" + og::data::active_company_slot() + ".gtl");
        og::data::set_active_company_slot("save0");
    }
};

// Select the item at 0-based selectable index `n` from the current menu:
// jump to it with a digit (1..9) and confirm with Enter.
void pick(HeadlessTerminal& term, int n)
{
    ASSERT_GE(n, 0);
    ASSERT_LE(n, 8) << "digit-jump only addresses the first 9 selectable items";
    term.push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(n)));
    term.push_special(KeyCode::Enter);
}

// Select the item at 0-based selectable index `n` by walking the cursor down
// from the top of the list and confirming. Unlike pick(), this reaches rows
// past the digit-jump ceiling — the #206 Camp door pushed Team Build's
// Scenario/CTF rows out of digit range.
void step_to(HeadlessTerminal& term, int n)
{
    ASSERT_GE(n, 0);
    for (int i = 0; i < n; ++i)
        term.push_special(KeyCode::Down);
    term.push_special(KeyCode::Enter);
}

// Dismiss a "press any key" text screen.
void dismiss(HeadlessTerminal& term)
{
    term.push_special(KeyCode::Enter);
}

class ScopedCursesPickerMountRestore
{
public:
    ScopedCursesPickerMountRestore()
        : mounted_before_(get_mounted_campaign())
    {
    }

    ~ScopedCursesPickerMountRestore()
    {
        (void)restore();
    }

    bool restore()
    {
        if (restored_)
            return restore_ok_;

        const std::string mounted_after =
            get_mounted_campaign();
        if (mounted_after != mounted_before_) {
            const CampaignPackageIoError result =
                mounted_before_.empty()
                    ? unmount_campaign_package_with_error(
                          mounted_after)
                    : mount_campaign_package_with_error(
                          mounted_before_);
            restore_ok_ =
                result == CampaignPackageIoError::None;
        }
        restore_ok_ =
            get_mounted_campaign() == mounted_before_ &&
            restore_ok_;
        restored_ = true;
        return restore_ok_;
    }

    const std::string& mounted_before() const
    {
        return mounted_before_;
    }

private:
    std::string mounted_before_;
    bool restored_ = false;
    bool restore_ok_ = true;
};

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

// The same lookup for a Team Build item. Team Build prints context header
// rows above the list, but the digit jump and the arrow walk both count
// selectable entries only, so this is the index both helpers take.
int team_build_item_index(PickerMenuCommand command, int arg = 0)
{
    const auto& def = og::ui::picker_menu_definition(PickerMenuId::TeamBuild);
    for (int i = 0; i < static_cast<int>(def.items.size()); ++i)
        if (def.items[static_cast<size_t>(i)].command == command &&
            def.items[static_cast<size_t>(i)].arg == arg)
            return i;
    return -1;
}

std::optional<int> dynamically_free_tcp_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return std::nullopt;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }
    const int port = ntohs(address.sin_port);
    (void)::close(fd);
    return port > 0 ? std::optional<int>(port) : std::nullopt;
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
    // Jump to the LAST selectable item (the Cloud door) and select it: with
    // the difficulty door gone to Team Build, Main is 8 rows and every one
    // of them is digit-addressable.
    const int cloud_idx = main_menu_item_index(PickerMenuCommand::OpenCloudMenu);
    ASSERT_GE(cloud_idx, 0);
    ASSERT_LE(cloud_idx, 8) << "digit-jump addresses the first 9 items";
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(cloud_idx)));
    f.t().push_special(KeyCode::Enter);
    const auto* item = f.client.present_menu(PickerMenuId::Main);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->command, PickerMenuCommand::OpenCloudMenu);

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

    // §2.2: accept the generated company name at the name-entry prompt.
    f.t().push_special(KeyCode::Enter);
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
    f.config.campaign = "modes";
    f.save().current_campaign = "modes";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"))
        << "the modes package ships with the game and should mount";

    // §2.2: accept the generated company name at the name-entry prompt.
    f.t().push_special(KeyCode::Enter);
    ASSERT_TRUE(f.client.prepare_new_game());

    EXPECT_EQ(f.config.campaign, "gladiator")
        << "a stale config campaign would be copied back onto the save at GO";
    EXPECT_EQ(f.save().current_campaign, "gladiator");
    EXPECT_EQ(get_mounted_campaign(), "gladiator")
        << "the in-picker scenario viewer reads the mounted package";
}

// §2.2: a typed name at the name-entry prompt becomes the company display
// name (save_name). Clear the pre-filled suggestion, then type the new name.
TEST(CursesPickerClient, name_entry_typed_name_becomes_company)
{
    PickerFixture f;
    // Clear the pre-filled suggestion (generous backspaces), type a name.
    for (int i = 0; i < 30; ++i)
        f.t().push_special(KeyCode::Backspace);
    f.t().push_string("MY BAND");
    f.t().push_special(KeyCode::Enter);

    ASSERT_TRUE(f.client.prepare_new_game());
    EXPECT_EQ(f.save().save_name, "MY BAND")
        << "the typed name should land in the 40-byte save_name";
}

// §2.2: an empty entry rerolls a fresh suggestion; accepting it founds a
// company with a non-empty generated name.
TEST(CursesPickerClient, name_entry_empty_rerolls_then_accepts)
{
    PickerFixture f;
    // Clear the field and Enter -> reroll; then accept the new suggestion.
    for (int i = 0; i < 30; ++i)
        f.t().push_special(KeyCode::Backspace);
    f.t().push_special(KeyCode::Enter);  // empty -> reroll
    f.t().push_special(KeyCode::Enter);  // accept the fresh suggestion

    ASSERT_TRUE(f.client.prepare_new_game());
    EXPECT_FALSE(f.save().save_name.empty())
        << "reroll then accept must found a company with a generated name";
}

// §2.2: Esc at the name-entry prompt cancels — nothing is created, so the
// loaded game (its gold) survives untouched.
TEST(CursesPickerClient, name_entry_escape_cancels_without_founding)
{
    PickerFixture f;
    f.save().m_totalcash[0] = 4242u;
    f.save().save_name = "PRIOR CO";

    f.t().push_special(KeyCode::Escape);
    EXPECT_FALSE(f.client.prepare_new_game())
        << "Esc at the name prompt cancels the new game";
    EXPECT_EQ(f.save().m_totalcash[0], 4242u)
        << "cancel must not reset the loaded game";
    EXPECT_EQ(f.save().save_name, "PRIOR CO")
        << "cancel must not overwrite the loaded company name";
}

TEST(CursesPickerClient, name_entry_clamps_the_company_display_name)
{
    PickerFixture f;
    for (int i = 0; i < 80; ++i)
        f.t().push_special(KeyCode::Backspace);
    const std::string long_name(og::ui::kCompanyNameMaxLen + 12u, 'A');
    f.t().push_string(long_name);
    f.t().push_special(KeyCode::Enter);

    ASSERT_TRUE(f.client.prepare_new_game());
    EXPECT_EQ(og::ui::kCompanyNameMaxLen, f.save().save_name.size());
    EXPECT_EQ(std::string(og::ui::kCompanyNameMaxLen, 'A'),
              f.save().save_name);
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

// --- player count compatibility -----------------------------------------

// Seat lifecycle moved off Main. Keep the legacy command dispatch safe for
// old callers, but never advertise any of the old 1-4 player rows.
TEST(CursesPickerClient,
     main_menu_omits_player_count_and_legacy_dispatch_stays_single_player)
{
    PickerFixture f;
    for (int players = 1; players <= 4; ++players) {
        EXPECT_EQ(nullptr, og::ui::find_picker_menu_item(
            PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, players));
    }

    const og::ui::PickerMenuItem legacy{
        "legacy-player-count", "Legacy Player Count",
        PickerMenuCommand::SetPlayerMode, 4};
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Main, legacy);
    EXPECT_EQ(static_cast<int>(f.save().numplayers), 1);
    EXPECT_NE(f.t().dump().find("supports one local player"),
              std::string::npos) << f.t().dump();

    const og::ui::PickerMenuItem one_player{
        "legacy-one-player", "One Player",
        PickerMenuCommand::SetPlayerMode, 1};
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Main, one_player);
    EXPECT_EQ(static_cast<int>(f.save().numplayers), 1);
    EXPECT_NE(f.t().dump().find("Player mode set to 1"), std::string::npos);

    const short allied_before = f.save().allied_mode;
    const og::ui::PickerMenuItem legacy_allied{
        "legacy-allied", "Legacy Allied",
        PickerMenuCommand::ToggleAlliedMode, 0};
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Main, legacy_allied);
    EXPECT_EQ(allied_before, f.save().allied_mode);
    EXPECT_NE(f.t().dump().find("Choose this player's team from Matchup"),
              std::string::npos);
}

// --- seat assignment -----------------------------------------------------

// Seat teams are chosen per level from Base Camp now; Player Settings no
// longer exposes the legacy Together/Split toggle.
TEST(CursesPickerClient, main_menu_does_not_expose_seat_mode)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Main, PickerMenuCommand::ToggleAlliedMode);
    EXPECT_EQ(item, nullptr);

    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::Main);
    EXPECT_EQ(f.t().dump().find("Seat Mode"), std::string::npos);
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

// The flat match-rule rows (match teams, target score) left Team Build for
// the camp's MATCH SETUP page (docs/camp-controls-design.md): one place, in
// plain words, on every client. Nothing in the curses surface carries them
// any more — see team_build_lists_the_appended_doors_last below for the
// list. The third of that trio, TROOPS, retired outright with amendment B5,
// so the shared versus guard no longer names it either. The curses
// guard-rendering branch stays covered by the READY row's networked-only
// guard.

// The Team Build list renders the two appended doors — DIFFICULTY and then
// LINEUP (docs/lineup-design.md §8) — with their fixed labels, past the digit
// ceiling and reachable by the arrow walk.
TEST(CursesPickerClient, team_build_lists_the_appended_doors_last)
{
    PickerFixture f;
    const int items = static_cast<int>(
        og::ui::picker_menu_definition(PickerMenuId::TeamBuild).items.size());
    const int difficulty_idx =
        team_build_item_index(PickerMenuCommand::OpenDifficultyMenu);
    const int lineup_idx = team_build_item_index(PickerMenuCommand::Lineup);
    ASSERT_EQ(items - 2, difficulty_idx)
        << "LINEUP appended BELOW difficulty, so difficulty kept its ordinal";
    ASSERT_EQ(items - 1, lineup_idx)
        << "lineup is appended last, so nothing above it moved";

    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::TeamBuild);
    const std::string dump = f.t().dump();
    // "Difficulty" must be the LAST row the list draws: the curses screen is
    // where an appended row silently falls off the bottom, so the rendered
    // tail is the thing worth pinning, not just membership.
    std::vector<std::string> rows;
    for (std::size_t start = 0; start < dump.size();) {
        std::size_t end = dump.find('\n', start);
        if (end == std::string::npos)
            end = dump.size();
        std::string row = dump.substr(start, end - start);
        while (!row.empty() && row.back() == ' ')
            row.pop_back();
        if (!row.empty())
            rows.push_back(row);
        start = end + 1;
    }
    // The trailing key-hint line is chrome, not a row.
    ASSERT_FALSE(rows.empty()) << dump;
    ASSERT_NE(std::string::npos, rows.back().find("Esc/q back"))
        << "expected the key-hint footer as the last line:\n" << dump;
    rows.pop_back();
    ASSERT_FALSE(rows.empty()) << dump;
    EXPECT_EQ("  Lineup", rows.back())
        << "the last appended door must be the last rendered row:\n" << dump;
    ASSERT_GE(rows.size(), 2u) << dump;
    EXPECT_EQ("  Difficulty", rows[rows.size() - 2])
        << "difficulty sits directly above the LINEUP door:\n" << dump;
    EXPECT_EQ(dump.find("Match Teams"), std::string::npos)
        << "the flat match-rule rows are gone from Team Build:\n" << dump;
    EXPECT_EQ(dump.find("Score Limit"), std::string::npos) << dump;
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
    // §2.5: the roster is an interactive list now (Enter=train, d=deploy,
    // r=ready) — Escape exits it; the drawn list stays on the terminal.
    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    bool found = false;
    for (int r = 0; r < f.t().rows(); ++r)
        if (f.t().text_row(r).find(name) != std::string::npos)
            found = true;
    EXPECT_TRUE(found) << "roster should render the member's name; got:\n" << f.t().dump();
}

TEST(CursesPickerClient, view_roster_handles_empty_team)
{
    PickerFixture f;
    f.save().team_size = 0;
    for (auto& member : f.save().team_list)
        member.reset();
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    ASSERT_NE(item, nullptr);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_NE(f.t().dump().find("(empty)"), std::string::npos);
    EXPECT_TRUE(f.t().input_exhausted());
}

TEST(CursesPickerClient, roster_keys_wrap_toggle_deploy_and_show_ready_notice)
{
    PickerFixture f;
    ASSERT_TRUE(f.save().team_list[0]);
    ASSERT_TRUE(f.save().team_list[0]->deployed);
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Up);      // wrap through selectable rows
    f.t().push_char(U'd');                // roster extra-key: bench row
    f.t().push_char(U'r');                // roster extra-key: ready notice
    dismiss(f.t());                       // dismiss that notice
    f.t().push_special(KeyCode::Escape);  // leave the redrawn roster
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_FALSE(f.save().team_list[0]->deployed);
    EXPECT_TRUE(f.t().input_exhausted());
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

TEST(CursesPickerClient, hire_rejects_an_unaffordable_recruit)
{
    PickerFixture f;
    const int before_count = team_count(f.save());
    f.save().m_totalcash[0] = 0;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter); // attempt highlighted Hire
    dismiss(f.t());                     // dismiss Can't hire
    f.t().push_char(U'q');              // leave hire screen
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(before_count, team_count(f.save()));
    EXPECT_EQ(0u, f.save().m_totalcash[0]);
    EXPECT_TRUE(f.t().input_exhausted());
}

TEST(CursesPickerClient, hiring_the_last_open_slot_reports_a_full_team)
{
    PickerFixture f;
    f.save().m_totalcash[0] = 1'000'000u;
    for (auto& member : f.save().team_list)
        member.reset();
    f.save().team_size = 0;
    for (int slot = 0; slot < MAX_TEAM_SIZE - 1; ++slot) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->teamnum = 0;
        f.save().team_list[static_cast<std::size_t>(slot)] = std::move(member);
        ++f.save().team_size;
    }
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter); // hire final member
    dismiss(f.t());                     // Hired
    dismiss(f.t());                     // Team is now full
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(MAX_TEAM_SIZE, team_count(f.save()));
    EXPECT_TRUE(f.t().input_exhausted());
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

// §2.5 per-row TRAIN (curses shape): Enter on a roster row opens the train
// flow SEEDED on that member — the +1/accept lands on the second member,
// never the first (the old enter-then-cycle default).
TEST(CursesPickerClient, roster_enter_opens_train_seeded_on_that_row)
{
    PickerFixture f;
    {
        og::ui::HireSession session(f.save(), 0);
        ASSERT_GE(session.hire(), 0);  // second member at slot 1
    }
    const short member0_before = f.save().team_list[0]->strength;
    const short member1_before = f.save().team_list[1]->strength;
    const std::uint32_t before_gold = f.save().m_totalcash[0];

    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ViewTeam);
    ASSERT_NE(item, nullptr);

    // Roster list: Down to row 1, Enter trains it (seeded). Train list:
    // Enter raises STR of the SEEDED member, digit '7' jumps to Accept,
    // Enter accepts, dismiss the notice, 'q' backs out of the train loop,
    // Escape exits the roster.
    f.t().push_special(KeyCode::Down);
    f.t().push_special(KeyCode::Enter);  // train roster row 1
    f.t().push_special(KeyCode::Enter);  // +1 STR (highlight starts on STR)
    f.t().push_char(U'7');               // jump to "Accept training"
    f.t().push_special(KeyCode::Enter);  // accept
    dismiss(f.t());                      // "Training accepted." notice
    f.t().push_char(U'q');               // back out of the train loop
    f.t().push_special(KeyCode::Escape); // exit the roster list
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(f.save().team_list[1]->strength, member1_before + 1)
        << "Enter on roster row 1 must seed the train session on member 1";
    EXPECT_EQ(f.save().team_list[0]->strength, member0_before)
        << "the first member must be untouched by the seeded train";
    EXPECT_LT(f.save().m_totalcash[0], before_gold)
        << "the seeded accept still charges gold";
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

TEST(CursesPickerClient, train_rejects_changes_when_gold_is_insufficient)
{
    PickerFixture f;
    ASSERT_TRUE(f.save().team_list[0]);
    const short strength_before = f.save().team_list[0]->strength;
    f.save().m_totalcash[0] = 0;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::TrainTeam);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter); // increase working STR
    f.t().push_char(U'7');              // jump to Accept training
    f.t().push_special(KeyCode::Enter); // reject for insufficient gold
    dismiss(f.t());                     // dismiss Can't afford
    f.t().push_char(U'q');              // leave training
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(strength_before, f.save().team_list[0]->strength);
    EXPECT_EQ(0u, f.save().m_totalcash[0]);
    EXPECT_TRUE(f.t().input_exhausted());
}

// --- set level -----------------------------------------------------------

// Set Level updates both the config and the save's scenario number — for a
// road the company has earned (the earned-roads gate closes the rest).
// Staged VIEW LEVEL (#218, C10): the solo picker stages the level locally
// through the one launch pipeline and presents the staged world as a glyph
// band (rows 2..13 on a 40-row terminal) above the census lines.
TEST(CursesPickerClient, view_scenario_renders_the_staged_glyph_band)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter);  // dismiss the preview screen
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_NE(std::string::npos, f.t().text_row(0).find("View Scenario"));
    // The band: min(12, rows - 6) = 12 glyph rows starting at row 2, drawn
    // from the STAGED world (terrain glyphs at the level-1 map center).
    bool any_glyph = false;
    for (int row = 2; row < 14 && !any_glyph; ++row)
        any_glyph =
            f.t().text_row(row).find_first_not_of(' ') != std::string::npos;
    EXPECT_TRUE(any_glyph)
        << "the staged world must render glyphs in the band:\n"
        << f.t().dump();
    // The census lines start right below the band.
    EXPECT_EQ(0u, f.t().text_row(14).find("SCEN 1:")) << f.t().dump();
    // Amendment 3 C5 (W6-C): a staged CLASSIC world gets the per-team
    // census fold too — headerless (no mode to name, no match to count
    // teams for) and unclamped (C4: classic levels never refuse), so the
    // block starts at its first team line directly under the title.
    EXPECT_EQ(0u, f.t().text_row(15).find("  RED TEAM  ACTIVE - COMPANY (1)"))
        << f.t().dump();
    EXPECT_EQ(0u,
              f.t().text_row(16).find("  GREEN TEAM  ACTIVE - MAP TROOPS (12)"))
        << f.t().dump();
    // Seat block (#218): the curses viewer stages locally, so the seat
    // lines are the save-derived synthesis — one all-local seat, now below
    // the classic census block.
    EXPECT_EQ(0u, f.t().text_row(17).find("SEATS: CO-OP")) << f.t().dump();
    EXPECT_EQ(0u, f.t().text_row(18).find("  P1 YOU - RED TEAM"))
        << f.t().dump();
}

// Small-terminal degradation: under 16 rows the preview screen is pure
// show_text — no band, census lines right below the title.
TEST(CursesPickerClient, view_scenario_degrades_to_text_on_small_terminals)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f({}, /*rows=*/12, /*cols=*/60);
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter);
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_NE(std::string::npos, f.t().text_row(0).find("View Scenario"));
    EXPECT_EQ(0u, f.t().text_row(2).find("SCEN 1:"))
        << "no band under 16 rows — the census starts at row 2:\n"
        << f.t().dump();
}

// The 16-row band squeeze: only three census rows fit under the band, so
// the overflow census lines (and their wrapped continuations on a narrow
// terminal) stop at the footer, which keeps its dismissal prompt. The
// held-key rule holds for show_preview too: two key RELEASES must not
// dismiss the screen — only the fresh press does.
TEST(CursesPickerClient, view_scenario_band_overflow_stops_at_the_footer)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    // 18 columns: wide enough for the footer prompt, narrow enough that a
    // census line WRAPS across the footer (the wrapped-continuation break
    // beside the whole-line break).
    PickerFixture f({}, /*rows=*/16, /*cols=*/18);
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(item, nullptr);

    f.t().push_key(Key::character(U'x', KeyEvent::Release));
    f.t().push_key(Key::character(U'x', KeyEvent::Release));
    f.t().push_special(KeyCode::Enter);
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_TRUE(f.t().input_exhausted())
        << "both releases and the fresh press must be consumed";
    EXPECT_NE(std::string::npos, f.t().text_row(0).find("View Scenario"));
    EXPECT_EQ(0u, f.t().text_row(15).find("[ press any key ]"))
        << "the footer keeps its prompt when the census overflows:\n"
        << f.t().dump();
    EXPECT_NE(std::string::npos, f.t().text_row(12).find_first_not_of(' '))
        << "the first census row under the band must be filled:\n"
        << f.t().dump();
}

// Solo staged VIEW LEVEL degradation: a local stage that cannot fit the
// wire cap (an oversize completed-levels ledger inflates the staged
// InitialSetup) degrades to the fallback census with the honest STAGING
// FAILED line — never a crash, never a stale band presented as the stage.
TEST(CursesPickerClient, view_scenario_staging_failure_degrades_to_fallback)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    std::set<int>& ledger = f.save().completed_levels["gladiator"];
    for (int level = 100'000; level < 117'000; ++level)
        ledger.insert(level);
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Enter);
    testing::internal::CaptureStderr();
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);
    const std::string diagnostics = testing::internal::GetCapturedStderr();

    EXPECT_NE(std::string::npos, f.t().text_row(0).find("View Scenario"));
    EXPECT_NE(std::string::npos, diagnostics.find("match_stage_failed"));
    bool found_failed_line = false;
    bool found_scen_line = false;
    for (int row = 0; row < 16; ++row)
    {
        if (f.t().text_row(row).find("STAGING FAILED") != std::string::npos)
            found_failed_line = true;
        if (f.t().text_row(row).find("SCEN 1:") != std::string::npos)
            found_scen_line = true;
    }
    EXPECT_TRUE(found_failed_line)
        << "the fallback census must lead with the honest STAGING FAILED "
           "line:\n"
        << f.t().dump();
    EXPECT_TRUE(found_scen_line)
        << "the fallback census still names the level:\n"
        << f.t().dump();
}

TEST(CursesPickerClient, set_level_updates_config_and_save)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().add_level_completed("gladiator", 4);
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

// The same prompt refuses an unearned forward id in the campaign's voice —
// and stays free on a versus campaign, whose arena picking is the point.
TEST(CursesPickerClient, set_level_rides_the_earned_roads_gate)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    const auto* item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::SetLevel);
    ASSERT_NE(item, nullptr);

    f.t().push_special(KeyCode::Backspace); // erase the prefilled "1"
    f.t().push_char(U'1');
    f.t().push_char(U'5');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_EQ(f.config.level, 1)
        << "an unearned forward id must not move the cursor";
    EXPECT_EQ(static_cast<int>(f.save().scen_num), 1);
    EXPECT_NE(f.t().dump().find("That road is not open yet."),
              std::string::npos);

    // Versus exemption: the modes campaign sets any shipped arena freely.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    f.save().current_campaign = "modes";
    f.config.campaign = "modes";
    f.t().push_special(KeyCode::Backspace); // erase the prefilled "1"
    f.t().push_char(U'3');
    f.t().push_char(U'0');
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    EXPECT_EQ(f.config.level, 301)
        << "a versus campaign's prompt stays freely selectable";
    EXPECT_EQ(static_cast<int>(f.save().scen_num), 301);
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

// --- replay level (#207) -------------------------------------------------

// The SCENARIO submenu's Replay Level prompt: Set Level's gates plus the
// cleared check, and the write is the ARM — cursor onto the level, origin
// remembered for the fold/re-entry restore.
TEST(CursesPickerClient, replay_level_arms_only_cleared_levels)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 3;
    f.config.level = 3;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ReplayLevel);
    ASSERT_NE(item, nullptr);

    // An uncleared (but in-frontier) id refuses with the cleared check.
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);
    EXPECT_EQ(3, static_cast<int>(f.save().scen_num))
        << "an uncleared level must not arm or move the cursor";
    EXPECT_EQ(0, static_cast<int>(f.save().replay_level));
    EXPECT_NE(f.t().dump().find("That level is not cleared yet."),
              std::string::npos);

    // An unearned forward id refuses at the earned-roads gate first.
    f.t().push_char(U'1');
    f.t().push_char(U'5');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);
    EXPECT_EQ(0, static_cast<int>(f.save().replay_level));
    EXPECT_NE(f.t().dump().find("That road is not open yet."),
              std::string::npos);

    // A cleared level arms: cursor moves, origin remembered, the click
    // answers in the replay voice.
    f.save().add_level_completed("gladiator", 1);
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);
    EXPECT_EQ(1, f.config.level);
    EXPECT_EQ(1, static_cast<int>(f.save().scen_num));
    EXPECT_EQ(1, static_cast<int>(f.save().replay_level));
    EXPECT_EQ(3, static_cast<int>(f.save().replay_origin));
    EXPECT_NE(f.t().dump().find("Replaying"), std::string::npos);
    f.save().clear_replay_arm();
}

// #207 arm lifecycle: a plain Set Level after Replay Level abandons the
// excursion — even for the very level the arm names. A surviving arm would
// skip the purge the plain set promises and a later win would restore an
// abandoned origin.
TEST(CursesPickerClient, plain_set_level_abandons_the_replay_arm)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().add_level_completed("gladiator", 1);
    f.save().scen_num = 3;
    f.config.level = 3;

    const auto* replay_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ReplayLevel);
    ASSERT_NE(replay_item, nullptr);
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *replay_item);
    ASSERT_TRUE(f.save().replay_armed_for(1));

    const auto* set_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::SetLevel);
    ASSERT_NE(set_item, nullptr);
    f.t().push_special(KeyCode::Backspace); // erase the prefilled "1"
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *set_item);
    EXPECT_EQ(1, static_cast<int>(f.save().scen_num));
    EXPECT_EQ(0, static_cast<int>(f.save().replay_level))
        << "the plain Set Level must abandon the excursion";
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

// #207 arm lifecycle: a campaign SWITCH abandons the replay excursion (the
// arm's origin is a cursor in the previous campaign — restoring it into the
// new one could plant an unearned cursor there). Re-selecting the CURRENT
// campaign changes nothing and keeps the arm.
TEST(CursesPickerClient, campaign_switch_clears_the_replay_arm)
{
    ScopedCursesPickerMountRestore mount_restore;
    PickerFixture f;
    const std::string original = f.save().current_campaign;
    f.save().add_level_completed(original, 1);
    f.save().scen_num = 3;
    f.save().arm_replay(1);

    // Confirm the highlighted (current) campaign: no switch, arm kept.
    f.t().push_special(KeyCode::Enter);
    EXPECT_EQ(f.client.show_campaign_select(), original);
    EXPECT_TRUE(f.save().replay_armed_for(1))
        << "re-selecting the current campaign must keep the excursion";

    // A real switch clears it.
    f.t().push_special(KeyCode::Down);
    f.t().push_special(KeyCode::Enter);
    const std::string switched = f.client.show_campaign_select();
    ASSERT_NE(switched, original) << "the list must offer a second campaign";
    EXPECT_EQ(0, static_cast<int>(f.save().replay_level))
        << "the campaign switch must abandon the excursion";
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

TEST(CursesPickerClient, save_reports_an_unsafe_slot_without_writing)
{
    PickerFixture f;
    f.config.save_name = "unsafe/slot";
    f.config.team_families.clear();
    dismiss(f.t());

    EXPECT_FALSE(f.client.save_game());
    EXPECT_NE(f.t().dump().find("Save failed"), std::string::npos);
    EXPECT_FALSE(user_file_exists("save/unsafe/slot.gtl"));
}

TEST(CursesPickerClient, team_build_dispatches_deploy_ready_progress_network_and_campaign)
{
    PickerFixture f;
    f.config.save_name = "curses_dispatch_slot";

    // §2.5 substitution: deploy (was Load Team) and ready (was Save Team).
    const auto* deploy_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::ToggleDeploy);
    const auto* ready_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::ToggleReady);
    const auto* progress_item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::ShowProgress);
    const auto* network_item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::Networking);
    const auto* campaign_item =
        og::ui::find_picker_menu_item(PickerMenuId::Scenario, PickerMenuCommand::SetCampaign);
    ASSERT_NE(deploy_item, nullptr);
    ASSERT_NE(ready_item, nullptr);
    ASSERT_NE(progress_item, nullptr);
    ASSERT_NE(network_item, nullptr);
    ASSERT_NE(campaign_item, nullptr);

    // Deploy: the prompt's default "1" toggles roster row 1 (slot 0);
    // Enter accepts, one dismiss for the confirmation. §3.8: the toggle
    // AUTOSAVES; §2.6 state 1: ready is guarded outside networked lobbies.
    ASSERT_TRUE(f.save().team_list[0] != nullptr);
    ASSERT_TRUE(f.save().team_list[0]->deployed);
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *deploy_item);
    EXPECT_FALSE(f.save().team_list[0]->deployed)
        << "the deploy prompt should bench roster row 1";

    // §3.8 disk round trip (§2.9 flow 5 on the curses client): the toggle's
    // autosave banked the benched flag in the ACTIVE company file.
    {
        SaveData reloaded;
        ASSERT_EQ(SaveDataIoError::None,
                  reloaded.load_with_error(og::data::active_company_slot()));
        ASSERT_TRUE(reloaded.team_list[0] != nullptr);
        EXPECT_FALSE(reloaded.team_list[0]->deployed)
            << "the deploy-toggle autosave must persist the benched flag";
    }

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *ready_item);
    {
        const std::string dump = f.t().dump();
        EXPECT_NE(dump.find("Ready applies to networked lobbies only."),
                  std::string::npos)
            << dump;
    }

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *progress_item);
    pick(f.t(), 2);
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *network_item);
    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *campaign_item);

    EXPECT_EQ(f.save().current_campaign, f.config.campaign);
}

TEST(CursesPickerClient, deploy_handles_empty_and_invalid_roster_selections)
{
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ToggleDeploy);
    ASSERT_NE(item, nullptr);

    {
        PickerFixture empty;
        empty.save().team_size = 0;
        for (auto& member : empty.save().team_list)
            member.reset();
        dismiss(empty.t());
        empty.client.handle_menu_item(PickerMenuId::TeamBuild, *item);
        EXPECT_NE(empty.t().dump().find("empty roster"), std::string::npos);
    }

    PickerFixture invalid;
    ASSERT_TRUE(invalid.save().team_list[0]);
    ASSERT_TRUE(invalid.save().team_list[0]->deployed);
    invalid.t().push_special(KeyCode::Backspace);
    invalid.t().push_char(U'9');
    invalid.t().push_special(KeyCode::Enter);
    dismiss(invalid.t());
    invalid.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_TRUE(invalid.save().team_list[0]->deployed);
    EXPECT_NE(invalid.t().dump().find("Invalid roster row"),
              std::string::npos);
    EXPECT_TRUE(invalid.t().input_exhausted());

    HeadlessTerminal cancelled_term{40, 100};
    FakeClock cancelled_clock;
    TextPickerConfig cancelled_config;
    CursesPickerOptions cancelled_options;
    CursesPickerClient cancelled(
        cancelled_term, cancelled_clock, cancelled_config,
        cancelled_options);
    ASSERT_TRUE(cancelled.save_data().team_list[0]);
    const bool deployed_before =
        cancelled.save_data().team_list[0]->deployed;
    cancelled_term.push_special(KeyCode::Escape);
    cancelled.handle_menu_item(
        PickerMenuId::TeamBuild, *item);
    EXPECT_EQ(deployed_before,
              cancelled.save_data().team_list[0]->deployed)
        << "cancelling the deploy prompt must not mutate the roster";
    EXPECT_TRUE(cancelled_term.input_exhausted());
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
              mount_campaign_package_with_error("gladiator"));
    f.config.campaign = "modes"; // configured != mounted
    f.config.level = 4;

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Campaign: Multiplayer Game Modes"), std::string::npos) << dump;
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

// --- §2.3 company list ----------------------------------------------------

namespace {

// Seeds a loadable company with a pinned last-played timestamp. High
// timestamps keep the seeded rows at the top of the most-recent-first list
// even when other tests' quicksave slots (last_played 0) share the binary's
// config dir under --gtest_shuffle.
bool seed_curses_company(const std::string& slot, const std::string& name,
                         std::int64_t last_played)
{
    SaveData sd;
    sd.reset();
    sd.save_name = name;
    sd.current_campaign = "gladiator";
    sd.last_played_unix_s = last_played;
    return sd.save_with_error(slot) == SaveDataIoError::None;
}

struct CursesSlotCleanup {
    std::vector<std::string> slots;
    ~CursesSlotCleanup()
    {
        for (const std::string& slot : slots)
            (void)remove_user_file("save/" + slot + ".gtl");
    }
};

std::string unique_curses_company_slot(std::string_view stem)
{
    static std::uint64_t sequence = 0;
    const auto nonce =
        std::chrono::steady_clock::now().time_since_epoch().count();
    return std::format("{}-{}-{}-{}", stem,
        static_cast<long long>(::getpid()), nonce, sequence++);
}

int company_row_number(const std::string& slot)
{
    const std::vector<og::data::CompanyInfo> companies =
        og::data::list_companies();
    const auto found = std::find_if(
        companies.begin(), companies.end(),
        [&slot](const og::data::CompanyInfo& company) {
            return company.slot == slot;
        });
    if (found == companies.end())
        return 0;
    return static_cast<int>(
        std::distance(companies.begin(), found)) + 1;
}

void enter_prompt_number(HeadlessTerminal& term, int number)
{
    if (number != 1) {
        term.push_special(KeyCode::Backspace);
        term.push_string(std::to_string(number));
    }
    term.push_special(KeyCode::Enter);
}

// Own only one collision-proof company slot. Cleanup removes that slot's
// documented artifacts and backups, never the surrounding save shelf.
class CursesCompanyArtifactCleanup
{
public:
    explicit CursesCompanyArtifactCleanup(std::string slot)
        : slot_(std::move(slot))
    {
        std::error_code ec;
        const std::filesystem::path backup_directory =
            std::filesystem::path(get_user_path()) /
            "save" / "backups";
        backup_directory_existed_ =
            std::filesystem::exists(backup_directory, ec);
        if (ec)
            return;

        const std::filesystem::path save_directory =
            std::filesystem::path(get_user_path()) / "save";
        for (const std::string_view suffix : artifact_suffixes()) {
            const bool exists = std::filesystem::exists(
                save_directory /
                    (slot_ + std::string(suffix)),
                ec);
            if (ec || exists)
                return;
        }

        if (backup_directory_existed_) {
            std::filesystem::directory_iterator entries(
                backup_directory, ec);
            if (ec)
                return;
            const std::string prefix = slot_ + ".";
            for (const std::filesystem::directory_entry& entry :
                 entries) {
                if (entry.path().filename().string().starts_with(
                        prefix))
                    return;
            }
        }
        owned_ = true;
    }

    ~CursesCompanyArtifactCleanup()
    {
        if (owned_)
            (void)cleanup();
    }

    bool ready() const { return owned_; }

    bool cleanup()
    {
        if (!owned_)
            return false;
        if (cleaned_)
            return true;

        for (const og::data::CompanyBackupInfo& backup :
             og::data::list_company_backups(slot_)) {
            (void)og::data::delete_company_backup(
                slot_, backup.seq);
        }
        const auto& suffixes = artifact_suffixes();
        for (const std::string_view suffix : suffixes) {
            (void)remove_user_file(
                "save/" + slot_ + std::string(suffix));
        }

        const std::filesystem::path backup_directory =
            std::filesystem::path(get_user_path()) /
            "save" / "backups";
        bool backup_cleanup_ok = true;
        std::error_code iteration_error;
        if (std::filesystem::exists(
                backup_directory, iteration_error) &&
            !iteration_error) {
            std::vector<std::filesystem::path> matching_backups;
            std::filesystem::directory_iterator entries(
                backup_directory, iteration_error);
            if (!iteration_error) {
                const std::string prefix = slot_ + ".";
                for (const std::filesystem::directory_entry& entry :
                     entries) {
                    if (entry.path().filename().string().starts_with(
                            prefix)) {
                        matching_backups.push_back(entry.path());
                    }
                }
            }
            backup_cleanup_ok = !iteration_error;
            for (const std::filesystem::path& path :
                 matching_backups) {
                std::error_code artifact_error;
                (void)std::filesystem::remove(
                    path, artifact_error);
                backup_cleanup_ok =
                    !artifact_error && backup_cleanup_ok;
            }
        } else if (iteration_error) {
            backup_cleanup_ok = false;
        }

        std::error_code remove_error;
        if (!backup_directory_existed_)
            (void)std::filesystem::remove(
                backup_directory, remove_error);
        std::error_code exists_error;
        const bool company_artifact_remains =
            std::any_of(
                suffixes.begin(), suffixes.end(),
                [this](std::string_view suffix) {
                    return user_file_exists(
                        "save/" + slot_ +
                        std::string(suffix));
                });
        bool backup_artifact_remains = false;
        std::error_code verify_error;
        if (std::filesystem::exists(
                backup_directory, verify_error) &&
            !verify_error) {
            std::filesystem::directory_iterator entries(
                backup_directory, verify_error);
            if (!verify_error) {
                const std::string prefix = slot_ + ".";
                for (const std::filesystem::directory_entry& entry :
                     entries) {
                    backup_artifact_remains =
                        backup_artifact_remains ||
                        entry.path().filename().string().starts_with(
                            prefix);
                }
            }
        }
        const bool unexpected_directory =
            !backup_directory_existed_ &&
            std::filesystem::exists(
                backup_directory, exists_error);
        const bool cleanup_ok =
            backup_cleanup_ok && !remove_error &&
            !exists_error && !verify_error &&
            !company_artifact_remains &&
            !backup_artifact_remains &&
            !unexpected_directory;
        cleaned_ = cleanup_ok;
        return cleanup_ok;
    }

private:
    static const std::vector<std::string_view>& artifact_suffixes()
    {
        static const std::vector<std::string_view> suffixes{
            ".gtl", ".tmp.gtl", ".gtl.restoretmp",
            ".gtl.restoretmp.tmp", ".gtl.tmp"};
        return suffixes;
    }

    std::string slot_;
    bool backup_directory_existed_ = false;
    bool owned_ = false;
    bool cleaned_ = false;
};

} // namespace

TEST(CursesPickerClient,
     company_list_corrupt_active_and_explicit_back_states)
{
    {
        const std::string corrupt_slot =
            unique_curses_company_slot("curses-corrupt-company");
        CursesCompanyArtifactCleanup cleanup(corrupt_slot);
        ASSERT_TRUE(cleanup.ready());
        const std::filesystem::path corrupt_path =
            std::filesystem::path(get_user_path()) /
            "save" / (corrupt_slot + ".gtl");
        std::ofstream corrupt(
            corrupt_path, std::ios::binary | std::ios::trunc);
        corrupt << "not a save";
        ASSERT_TRUE(corrupt.good());
        corrupt.close();

        const int corrupt_row =
            company_row_number(corrupt_slot);
        ASSERT_GT(corrupt_row, 0);

        HeadlessTerminal term{40, 100};
        FakeClock clock;
        TextPickerConfig config;
        CursesPickerOptions options;
        CursesPickerClient client(term, clock, config, options);
        const std::string slot_before = config.save_name;
        pick(term, 0);                  // Open Company
        enter_prompt_number(term, corrupt_row);
        dismiss(term);                  // damaged notice
        pick(term, 3);                  // explicit Back command

        EXPECT_FALSE(client.show_company_list());
        EXPECT_EQ(slot_before, config.save_name);
        EXPECT_NE(term.dump().find("CORRUPT"),
                  std::string::npos);
        EXPECT_TRUE(std::filesystem::exists(corrupt_path));
        EXPECT_TRUE(term.input_exhausted());
        EXPECT_TRUE(cleanup.cleanup());
    }

    {
        const std::string active_slot =
            unique_curses_company_slot("curses-active-company");
        CursesCompanyArtifactCleanup cleanup(active_slot);
        ASSERT_TRUE(cleanup.ready());
        ASSERT_TRUE(seed_curses_company(
            active_slot, "ACTIVE COMPANY", 9000));
        const int active_row =
            company_row_number(active_slot);
        ASSERT_GT(active_row, 0);

        HeadlessTerminal term{40, 100};
        FakeClock clock;
        TextPickerConfig config;
        config.save_name = active_slot;
        CursesPickerOptions options;
        CursesPickerClient client(term, clock, config, options);

        {
            og::data::ScopedActiveCompany active_company(
                active_slot);
            ASSERT_TRUE(active_company.applied());
            pick(term, 2);                    // Delete Company
            enter_prompt_number(term, active_row);
            dismiss(term);                    // switch-first notice
            pick(term, 3);                    // explicit Back command

            EXPECT_FALSE(client.show_company_list());
            EXPECT_EQ(active_slot,
                      og::data::active_company_slot());
            EXPECT_TRUE(user_file_exists(
                "save/" + active_slot + ".gtl"));
            EXPECT_NE(term.dump().find("ACTIVE COMPANY"),
                      std::string::npos);
            EXPECT_TRUE(term.input_exhausted());
        }
        EXPECT_TRUE(cleanup.cleanup());
    }
}

// Opening row 1 (the most recent company) repoints the terminal slot
// ([SAVE-R2]) and loads that company's save; show_company_list reports true
// so the state machine proceeds to team build.
TEST(CursesPickerClient, company_list_open_repoints_slot)
{
    CursesSlotCleanup cleanup{{"wp3curb", "wp3cura"}};
    ASSERT_TRUE(seed_curses_company("wp3curb", "BRAVO BAND", 6000));
    ASSERT_TRUE(seed_curses_company("wp3cura", "ALPHA BAND", 5000));

    PickerFixture f;
    pick(f.t(), 0);                       // chrome: Open Company
    f.t().push_special(KeyCode::Enter);   // accept the pre-filled "1"
    f.t().push_special(KeyCode::Enter);   // dismiss the "Loaded" notice

    EXPECT_TRUE(f.client.show_company_list())
        << "an opened company reports true (-> team build)";
    EXPECT_EQ("wp3curb", f.config.save_name)
        << "[SAVE-R2] opening repoints the terminal slot";
    EXPECT_EQ("BRAVO BAND", f.save().save_name);
}

TEST(CursesPickerClient, company_list_rejects_an_out_of_range_row)
{
    CursesSlotCleanup cleanup{{"wp3cur-invalid"}};
    ASSERT_TRUE(seed_curses_company(
        "wp3cur-invalid", "INVALID ROW TEST", 9000));

    PickerFixture f;
    const std::string slot_before = f.config.save_name;
    pick(f.t(), 0);                       // Open Company
    f.t().push_special(KeyCode::Backspace);
    f.t().push_string("99");
    f.t().push_special(KeyCode::Enter);   // invalid row
    dismiss(f.t());                       // Invalid company selection
    f.t().push_special(KeyCode::Escape);  // leave company list

    EXPECT_FALSE(f.client.show_company_list());
    EXPECT_EQ(slot_before, f.config.save_name);
    EXPECT_TRUE(f.t().input_exhausted());
}

// Esc backs out (nothing opened, slot untouched), and the §2.4 backups
// chrome backs out of a company with no snapshots yet (backups are
// level-win products).
TEST(CursesPickerClient, company_list_backups_empty_and_escape_back)
{
    CursesSlotCleanup cleanup{{"wp3cures"}};
    ASSERT_TRUE(seed_curses_company("wp3cures", "SOLO BAND", 6000));

    PickerFixture f;
    const std::string slot_before = f.config.save_name;
    pick(f.t(), 1);                       // chrome: Backups...
    f.t().push_special(KeyCode::Enter);   // accept the pre-filled company "1"
    f.t().push_special(KeyCode::Enter);   // dismiss the "No backups yet" notice
    f.t().push_special(KeyCode::Escape);  // back out of the list

    EXPECT_FALSE(f.client.show_company_list());
    EXPECT_EQ(slot_before, f.config.save_name)
        << "backing out must not repoint the slot";
}

// §2.4 restore round trip (curses projection): the NO-first confirm keeps
// the current state; the explicit Yes rewinds the company, repoints the
// terminal slot ([SAVE-R2]), snapshots the pre-restore state first, and
// reports true so the state machine proceeds to team build.
TEST(CursesPickerClient, company_backups_restore_no_first_then_yes)
{
    CursesSlotCleanup cleanup{{"wp3curr"}};
    ASSERT_TRUE(seed_curses_company("wp3curr", "OLD BAND", 7000));
    ASSERT_TRUE(og::data::backup_company_now("wp3curr"));
    ASSERT_TRUE(seed_curses_company("wp3curr", "NEW BAND", 8000));

    PickerFixture f;
    pick(f.t(), 1);                       // chrome: Backups...
    f.t().push_special(KeyCode::Enter);   // accept the pre-filled company "1"
    // Restore, answering the NO-first confirm with the default No.
    pick(f.t(), 0);                       // backups chrome: Restore Backup
    f.t().push_special(KeyCode::Enter);   // accept the pre-filled backup "1"
    f.t().push_special(KeyCode::Enter);   // confirm: highlight starts on No
    // Restore again, this time selecting Yes.
    pick(f.t(), 0);
    f.t().push_special(KeyCode::Enter);   // backup "1"
    f.t().push_char(U'2');                // digit-jump to Yes
    f.t().push_special(KeyCode::Enter);
    f.t().push_special(KeyCode::Enter);   // dismiss the "Loaded" notice

    EXPECT_TRUE(f.client.show_company_list())
        << "a rewound company reports true (-> team build / base camp)";
    EXPECT_EQ("wp3curr", f.config.save_name)
        << "[SAVE-R2] a restore repoints the terminal slot";
    EXPECT_EQ("OLD BAND", f.save().save_name)
        << "the in-memory save must hold the rewound state";
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("wp3curr");
    ASSERT_EQ(2u, backups.size());
    EXPECT_EQ("NEW BAND", backups.front().header.display_name)
        << "the pre-restore state must be snapshotted first (§3.7 step 1)";
    for (const og::data::CompanyBackupInfo& backup : backups)
        (void)og::data::delete_company_backup("wp3curr", backup.seq);
}

// §2.4 delete-backup round trip (curses projection): NO-first keeps the
// snapshot, the explicit Yes deletes it, and the emptied list backs out.
TEST(CursesPickerClient, company_backups_delete_no_first_then_yes)
{
    CursesSlotCleanup cleanup{{"wp3curd"}};
    ASSERT_TRUE(seed_curses_company("wp3curd", "KEEP BAND", 7000));
    ASSERT_TRUE(og::data::backup_company_now("wp3curd"));

    PickerFixture f;
    pick(f.t(), 1);                       // chrome: Backups...
    f.t().push_special(KeyCode::Enter);   // company "1"
    // Delete, answering the NO-first confirm with the default No.
    pick(f.t(), 1);                       // backups chrome: Delete Backup
    f.t().push_special(KeyCode::Enter);   // backup "1"
    f.t().push_special(KeyCode::Enter);   // confirm: highlight starts on No
    // Delete again, this time selecting Yes.
    pick(f.t(), 1);
    f.t().push_special(KeyCode::Enter);   // backup "1"
    f.t().push_char(U'2');                // digit-jump to Yes
    f.t().push_special(KeyCode::Enter);
    f.t().push_special(KeyCode::Enter);   // dismiss the "Deleted" notice
    // The emptied snapshot list backs out with a notice; leave the list.
    f.t().push_special(KeyCode::Enter);   // dismiss "No backups yet"
    f.t().push_special(KeyCode::Escape);  // back out of the list

    EXPECT_FALSE(f.client.show_company_list());
    EXPECT_TRUE(og::data::list_company_backups("wp3curd").empty())
        << "the explicit Yes must delete the snapshot";
    EXPECT_TRUE(user_file_exists("save/wp3curd.gtl"))
        << "deleting a snapshot never touches the company file";
}

// §2.4 corrupt snapshots refuse restore up front (the §3.7 step-0 API
// validation stays the real guard; no confirm is ever reached).
TEST(CursesPickerClient, company_backups_corrupt_snapshot_refuses)
{
    CursesSlotCleanup cleanup{{"wp3curc"}};
    ASSERT_TRUE(seed_curses_company("wp3curc", "INTACT BAND", 7000));
    {
        const std::filesystem::path backups_dir =
            std::filesystem::path(get_user_path()) / "save" / "backups";
        std::error_code ec;
        std::filesystem::create_directories(backups_dir, ec);
        std::ofstream corrupt(backups_dir / "wp3curc.001.gtl",
                              std::ios::binary | std::ios::trunc);
        corrupt << "not a backup";
        ASSERT_TRUE(corrupt.good());
    }

    PickerFixture f;
    const std::string slot_before = f.config.save_name;
    pick(f.t(), 1);                       // chrome: Backups...
    f.t().push_special(KeyCode::Enter);   // company "1"
    pick(f.t(), 0);                       // backups chrome: Restore Backup
    f.t().push_special(KeyCode::Enter);   // backup "1" (the corrupt one)
    f.t().push_special(KeyCode::Enter);   // dismiss the "damaged" notice
    f.t().push_special(KeyCode::Escape);  // back out of the backups view
    f.t().push_special(KeyCode::Escape);  // back out of the list

    EXPECT_FALSE(f.client.show_company_list());
    EXPECT_EQ(slot_before, f.config.save_name)
        << "a refused restore must not repoint the slot";
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups("wp3curc");
    ASSERT_EQ(1u, backups.size());
    EXPECT_FALSE(backups.front().header.valid);
    (void)og::data::delete_company_backup("wp3curc", 1);
}

TEST(CursesPickerClient, company_backups_reject_invalid_row_and_explicitly_back)
{
    const std::string company_slot =
        unique_curses_company_slot("curses-invalid-backup");
    CursesCompanyArtifactCleanup cleanup(company_slot);
    ASSERT_TRUE(cleanup.ready());
    ASSERT_TRUE(seed_curses_company(
        company_slot, "INVALID BACKUP ROW", 9100));
    ASSERT_TRUE(og::data::backup_company_now(
        company_slot));
    const int company_row =
        company_row_number(company_slot);
    ASSERT_GT(company_row, 0);

    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client(term, clock, config, options);
    const std::string slot_before = config.save_name;
    pick(term, 1);                         // Backups...
    enter_prompt_number(term, company_row);
    pick(term, 0);                         // Restore Backup
    term.push_special(KeyCode::Backspace); // clear default "1"
    term.push_string("99");
    term.push_special(KeyCode::Enter);     // invalid backup row
    dismiss(term);                         // validation notice
    pick(term, 2);                         // explicit backup-view Back
    pick(term, 3);                         // explicit company-list Back

    EXPECT_FALSE(client.show_company_list());
    EXPECT_EQ(slot_before, config.save_name);
    EXPECT_NE(term.dump().find("INVALID BACKUP ROW"),
              std::string::npos);
    EXPECT_TRUE(term.input_exhausted());
    const std::vector<og::data::CompanyBackupInfo> backups =
        og::data::list_company_backups(company_slot);
    ASSERT_EQ(1u, backups.size());
    EXPECT_TRUE(backups.front().header.valid);
    EXPECT_TRUE(cleanup.cleanup());
}

// Delete is NO-first: the default confirm keeps the company; an explicit
// Yes deletes it (file gone), and the list re-scans.
TEST(CursesPickerClient, company_list_delete_no_first_then_yes)
{
    CursesSlotCleanup cleanup{{"wp3curdb", "wp3curda"}};
    ASSERT_TRUE(seed_curses_company("wp3curdb", "BRAVO BAND", 6000));
    ASSERT_TRUE(seed_curses_company("wp3curda", "ALPHA BAND", 5000));

    PickerFixture f;
    // Delete row 2 (ALPHA), answer the NO-first confirm with the default No.
    pick(f.t(), 2);                          // chrome: Delete Company
    f.t().push_special(KeyCode::Backspace);  // clear the pre-filled "1"
    f.t().push_char(U'2');
    f.t().push_special(KeyCode::Enter);      // row 2
    f.t().push_special(KeyCode::Enter);      // confirm: highlight starts on No
    // Delete row 2 again, this time selecting Yes.
    pick(f.t(), 2);
    f.t().push_special(KeyCode::Backspace);
    f.t().push_char(U'2');
    f.t().push_special(KeyCode::Enter);
    f.t().push_char(U'2');                   // digit-jump to Yes
    f.t().push_special(KeyCode::Enter);
    f.t().push_special(KeyCode::Enter);      // dismiss the "Deleted" notice
    f.t().push_special(KeyCode::Escape);     // leave the list

    EXPECT_FALSE(f.client.show_company_list());
    EXPECT_FALSE(user_file_exists("save/wp3curda.gtl"))
        << "the explicit Yes must delete the company";
    EXPECT_TRUE(user_file_exists("save/wp3curdb.gtl"))
        << "the NO-first default must keep the company";
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

// [SAVE-R2] Terminal slot authority: the client repoints the process-wide
// active-company slot at construction, on the Options slot command, and on
// prepare_new_game, so company-level writes always target the user's chosen
// slot, never save0. (The curses_test_main [SAVE-R8] listener restores
// "save0" after each test.)
TEST(CursesPickerClient, asserts_company_slot_authority)
{
    {
        PickerFixture f; // default TextPickerConfig slot: "text_quicksave"
        EXPECT_EQ("text_quicksave", og::data::active_company_slot())
            << "construction must assert the configured slot";

        // The Options slot command repoints the active company. The prompt is
        // prefilled with the current slot; erase it and type the new one.
        const std::string old_slot = f.config.save_name;
        for (size_t i = 0; i < old_slot.size(); ++i)
            f.t().push_special(KeyCode::Backspace);
        f.t().push_string("curses-authority-slot");
        f.t().push_special(KeyCode::Enter);
        f.t().push_special(KeyCode::Escape); // cancel the seed prompt
        f.client.show_options();
        EXPECT_EQ(f.config.save_name, "curses-authority-slot");
        EXPECT_EQ("curses-authority-slot", og::data::active_company_slot());
    }

    // The CursesApp launch flow passes its --save option (default
    // "curses_quicksave") through the config; construction applies it.
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    config.save_name = "curses_quicksave";
    CursesPickerOptions options;
    CursesPickerClient client{term, clock, config, options};
    EXPECT_EQ("curses_quicksave", og::data::active_company_slot());

    // A stale process-wide slot must not survive a new game.
    ASSERT_TRUE(og::data::set_active_company_slot("save0"));
    // §2.2: accept the generated company name at the name-entry prompt.
    term.push_special(KeyCode::Enter);
    ASSERT_TRUE(client.prepare_new_game());
    EXPECT_EQ("curses_quicksave", og::data::active_company_slot());
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

// #152: show_text wraps a line wider than the terminal instead of letting
// put_str cut it off at the right edge.
TEST(CursesPickerClient, help_wraps_lines_wider_than_terminal)
{
    PickerFixture f;
    f.t().resize(40, 30);
    f.t().push_special(KeyCode::Enter);
    f.client.show_help();
    // "Continue Game opens Team Build; use GO! there to start playing." is
    // 64 chars; at 30 columns the tail survives only if the line wrapped.
    EXPECT_NE(f.t().dump().find("playing."), std::string::npos)
        << "overflow tail must land on a wrapped row:\n"
        << f.t().dump();
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

// --- LINEUP (docs/lineup-design.md §8) -----------------------------------

namespace {

// Two seated teams: three fighters on team 0 (my_team, so the derivation
// seats it) and one on team 1, levels descending by slot so the power-less
// SPLIT FAIR order is fully determined.
void seed_lineup_roster(SaveData& save)
{
    for (auto& member : save.team_list)
        member.reset();
    for (int i = 0; i < 4; ++i) {
        save.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        guy& member = *save.team_list[static_cast<std::size_t>(i)];
        member.name = std::string("F") + static_cast<char>('1' + i);
        member.teamnum = i == 3 ? 1 : 0;
        member.deployed = true;
        member.level = static_cast<short>(4 - i);
    }
    save.team_size = 4;
    save.my_team = 0;
}

const og::ui::PickerMenuItem& lineup_item()
{
    const og::ui::PickerMenuItem* const item =
        og::ui::find_picker_menu_item(PickerMenuId::TeamBuild,
                                      PickerMenuCommand::Lineup);
    EXPECT_TRUE(item != nullptr);
    return *item;
}

} // namespace

// The page is the shared model rendered as a Menu with dynamic rows: four
// bands as NON-selectable context above twelve host rows (B6 took the
// FIGHTERS row out). Selecting the first row steps TEAM 1's FILL wheel and
// the second flips its MAP UNITS box, and the redraw proves both writes
// landed — the labels are re-read from the save, never remembered.
TEST(CursesPickerClient, lineup_page_lists_the_bands_and_works_both_knobs)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    // A versus campaign, kept deliberately after C5 made the knobs live
    // everywhere: this is the modes half of the pair, and the classic twin
    // below now asserts the same answers on gladiator.
    f.save().current_campaign = "modes";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    // B8: nothing on either control is refused any more, so one press is one
    // step and no toast interrupts it. Since E1 the stored default IS NONE,
    // and the DISPLAY order (NONE, WEAK, FAIR, STRONG, BRUTAL) puts WEAK one
    // step past it.
    pick(f.t(), 0);                      // row 1: TEAM 1 FILL
    pick(f.t(), 1);                      // row 2: TEAM 1 MAP UNITS
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Lineup"), std::string::npos) << dump;
    EXPECT_NE(dump.find("TEAM 1 RED  POWER"), std::string::npos)
        << "the band names the colour and the price:\n" << dump;
    EXPECT_NE(dump.find("3 FIGHTERS"), std::string::npos) << dump;
    EXPECT_NE(dump.find("NO SEAT"), std::string::npos)
        << "the two empty teams still get a band:\n" << dump;
    EXPECT_NE(dump.find("TEAM 1  FILL: WEAK"), std::string::npos)
        << "the redraw re-reads the wheel out of the save:\n" << dump;
    EXPECT_NE(dump.find("TEAM 1  MAP UNITS: OFF"), std::string::npos)
        << "and the box with it:\n" << dump;
    EXPECT_EQ(og::sim::kFillWeak, f.save().fill[0])
        << "NONE -> WEAK landed in the save: the row shows the stored code, "
           "so the wheel leaves from NONE's slot";
    EXPECT_EQ(og::sim::kMapUnitsOff, f.save().map_units[0])
        << "ON -> OFF landed in the save";
    EXPECT_EQ(og::sim::kFillNone, f.save().fill[1])
        << "only the cycled team moved";
    EXPECT_EQ(og::sim::kMapUnitsOn, f.save().map_units[1]);
    EXPECT_TRUE(f.t().input_exhausted());
    (void)unmount_campaign_package_with_error("modes");
    (void)mount_campaign_package_with_error("gladiator");
}

// ...and on a CLASSIC campaign the same row does the same thing. Amendment 3
// C5 moved the match machinery into packs/core and gave a mode-less level its
// own stage step, so gladiator reads FILL and MAP UNITS too: no mark, no
// refusal, and the write lands. This test asserted the opposite until C5.
TEST(CursesPickerClient, lineup_knob_cycles_on_a_classic_campaign)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    f.save().current_campaign = "gladiator";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    pick(f.t(), 0);                      // row 1: TEAM 1 FILL -> WEAK
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    const std::string dump = f.t().dump();
    EXPECT_EQ(og::sim::kFillWeak, f.save().fill[0])
        << "a classic campaign's knob write lands like any other:\n" << dump;
    EXPECT_EQ(dump.find("MAP RULES"), std::string::npos)
        << "C5 retired the classic mark and its census:\n" << dump;
    EXPECT_NE(dump.find("TEAM 1  FILL: WEAK"), std::string::npos)
        << "the redraw re-reads the wheel out of the save:\n" << dump;
    EXPECT_TRUE(f.t().input_exhausted());
}

// E1/E2: at rest every band reads its STORED code, and a fresh company
// stores 0 on all four — so the page opens on four NONEs whatever the map
// authors. Gladiator scen 1 ships twelve elves onto team 2 and nothing onto
// teams 3 and 4, and since the per-team resolver retired that difference no
// longer shows up in the word. (W7-G had the terminals census the staged
// world to resolve a default here; amendment 4 removed the thing being
// resolved, and the staged walk survives only for MAP UNITS, below.)
TEST(CursesPickerClient, lineup_bands_read_the_stored_code_at_rest)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 1;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    f.t().push_special(KeyCode::Escape); // look, touch nothing, back out
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("TEAM 1  FILL: NONE"), std::string::npos)
        << "the seated team is no better off than any other at rest:\n"
        << dump;
    EXPECT_NE(dump.find("TEAM 2  FILL: NONE"), std::string::npos)
        << "the elf team is authored, and that fields nothing now:\n" << dump;
    EXPECT_NE(dump.find("TEAM 3  FILL: NONE"), std::string::npos)
        << "an unauthored team reads the same word:\n" << dump;
    EXPECT_NE(dump.find("TEAM 4  FILL: NONE"), std::string::npos)
        << "...and so does its twin:\n" << dump;
    EXPECT_EQ(og::sim::kFillNone, f.save().fill[2])
        << "looking is not a write — nothing was touched";
    EXPECT_TRUE(f.t().input_exhausted());
}

// B4/F3: the curses page censuses the map's own units off the world it
// stages (the SDL box state's own walk), so the unauthored band says
// NO MAP UNITS and the toggle on it is REFUSED with that hint (a show_text
// screen), while the elves' box still flips. Before this the terminals fed
// no count: the hint was dead and the toggle wrote where SDL refuses.
TEST(CursesPickerClient, lineup_map_units_refuses_where_the_map_ships_none)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 1;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    pick(f.t(), 7);                      // row 8: TEAM 4 MAP UNITS -> refused
    dismiss(f.t());                      // the refusal is a show_text screen
    pick(f.t(), 3);                      // row 4: TEAM 2 MAP UNITS -> OFF
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("NO MAP UNITS"), std::string::npos)
        << "B4: a censused page says NO MAP UNITS where the map ships "
           "none:\n" << dump;
    EXPECT_EQ(og::sim::kMapUnitsOn, f.save().map_units[3])
        << "the refused toggle must not touch the save:\n" << dump;
    EXPECT_EQ(og::sim::kMapUnitsOff, f.save().map_units[1])
        << "the authored team's toggle lands like any other:\n" << dump;
    EXPECT_NE(dump.find("TEAM 2  MAP UNITS: OFF"), std::string::npos)
        << "the redraw re-reads the box out of the save:\n" << dump;
    EXPECT_TRUE(f.t().input_exhausted());
}

// D1/D4 as amended by E1: the full-cycle label pin, curses half. Six entries
// per band, one press each, so the redraw after every step is its own frame.
// The two sequences are now IDENTICAL — one wheel, one entry point, because
// the word a band shows at rest is its stored 0 whatever the map authors.
TEST(CursesPickerClient, lineup_wheel_cycles_both_bands_fully)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 1;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));

    // One press on `row`, then read that row's label back off the redraw.
    const auto step = [&f](int row, std::string_view team) {
        pick(f.t(), row);
        f.t().push_special(KeyCode::Escape);
        f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());
        const std::string dump = f.t().dump();
        const std::string needle = std::string(team) + "  FILL: ";
        const std::size_t at = dump.find(needle);
        if (at == std::string::npos)
            return std::string("<no ") + needle + " row>\n" + dump;
        const std::size_t start = at + needle.size();
        const std::size_t end = dump.find_first_not_of(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ", start);
        return dump.substr(start, end - start);
    };

    // Team 0: seated and standing on the map's own units. Since E1 that buys
    // it no head start — it shows the stored 0 and the wheel leaves NONE.
    const std::vector<std::string> expect_wheel = {
        "WEAK", "FAIR", "STRONG", "BRUTAL", "NONE", "WEAK"};
    std::vector<std::string> authored;
    for (int i = 0; i < 6; ++i)
        authored.push_back(step(0, "TEAM 1"));
    EXPECT_EQ(expect_wheel, authored)
        << "an authored band enters the wheel at NONE:\n" << f.t().dump();

    // Team 2: gladiator authors nothing onto it — the same entry point and
    // therefore the same six words. There is one wheel now, not two.
    std::vector<std::string> unauthored;
    for (int i = 0; i < 6; ++i)
        unauthored.push_back(step(4, "TEAM 3"));
    EXPECT_EQ(expect_wheel, unauthored)
        << "an unauthored band walks the identical wheel — E1 left a single "
           "entry point and every word its own stop:\n" << f.t().dump();

    EXPECT_EQ(og::sim::kFillWeak, f.save().fill[0]);
    EXPECT_EQ(og::sim::kFillWeak, f.save().fill[2]);
    EXPECT_EQ(og::sim::kFillNone, f.save().fill[1])
        << "the untouched bands keep the stored default";
    EXPECT_EQ(og::sim::kFillNone, f.save().fill[3])
        << "the untouched bands keep the stored default";
    EXPECT_TRUE(f.t().input_exhausted());
}

// M3 + §5: a curses client is a ONE-SEAT machine (numplayers stays 1), and
// the seat picture is the launch's own — so there is exactly one seated team
// and split_company's documented single-seat rule makes every mode ALL TO 1.
// The terminals used to derive a second seat from a second deployed COLOUR,
// which dealt the company across a seat the launch would never create.
TEST(CursesPickerClient, lineup_split_fair_on_one_seat_is_all_to_one)
{
    PickerFixture f;
    seed_lineup_roster(f.save());

    step_to(f.t(), 9);                   // row 10: Split fair
    dismiss(f.t());                      //   the "Moved n fighters." report
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    for (int slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(0, f.save()
                         .team_list[static_cast<std::size_t>(slot)]
                         ->teamnum)
            << "slot " << slot;
    }
    EXPECT_TRUE(f.t().input_exhausted());
}

// ...and so does SPLIT EVEN, from the same one seat.
TEST(CursesPickerClient, lineup_split_even_on_one_seat_is_all_to_one)
{
    PickerFixture f;
    seed_lineup_roster(f.save());

    step_to(f.t(), 8);                   // row 9: Split even
    dismiss(f.t());                      //   the "Moved n fighters." report
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    for (int slot = 0; slot < 4; ++slot) {
        EXPECT_EQ(0, f.save()
                         .team_list[static_cast<std::size_t>(slot)]
                         ->teamnum)
            << "slot " << slot;
    }
    EXPECT_TRUE(f.t().input_exhausted());
}

// UNITE puts every deployed character on the lowest-numbered seated team.
TEST(CursesPickerClient, lineup_unite_gathers_the_company_on_one_team)
{
    PickerFixture f;
    seed_lineup_roster(f.save());

    step_to(f.t(), 10);                  // row 11: Unite
    dismiss(f.t());                      //   the "Moved n fighters." report
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    for (int slot = 0; slot < 4; ++slot) {
        ASSERT_TRUE(f.save().team_list[static_cast<std::size_t>(slot)] !=
                    nullptr);
        EXPECT_EQ(0, f.save()
                         .team_list[static_cast<std::size_t>(slot)]
                         ->teamnum)
            << "slot " << slot;
    }
    EXPECT_TRUE(f.t().input_exhausted());
}

// A SPECTATOR machine (no declared players) seats nobody, so a SPLIT has no
// team to deal into and says so instead of silently doing nothing.
TEST(CursesPickerClient, lineup_split_without_a_seat_refuses_in_words)
{
    PickerFixture f;
    seed_lineup_roster(f.save());
    f.save().numplayers = 0;
    for (auto& member : f.save().team_list)
        if (member != nullptr)
            member->deployed = false;

    step_to(f.t(), 9);                   // row 10: Split fair
    dismiss(f.t());                      //   the refusal screen
    f.t().push_special(KeyCode::Escape); // back out of the page
    f.client.handle_menu_item(PickerMenuId::TeamBuild, lineup_item());

    EXPECT_EQ(1, f.save().team_list[3]->teamnum)
        << "a refused split moves nobody";
    EXPECT_TRUE(f.t().input_exhausted());
}

// --- networking ----------------------------------------------------------

TEST(CursesPickerClient, host_game_builds_real_lobby_and_can_cancel)
{
    const std::optional<int> port = dynamically_free_tcp_port();
    ASSERT_TRUE(port.has_value());
    CursesPickerOptions options;
    options.host_port = *port;
    PickerFixture f(options);
    pick(f.t(), 0); // Networking -> Host Game
    f.t().push_char(U'q');

    EXPECT_TRUE(f.client.configure_networking())
        << "the native WebSocket host should bind and enter its lobby";
    EXPECT_TRUE(f.t().input_exhausted());
}

TEST(CursesPickerClient, networking_join_cancel_returns_false)
{
    PickerFixture f;
    // Cancel the URL prompt: join_game should report false without a lobby.
    f.t().push_special(KeyCode::Escape);
    EXPECT_FALSE(f.client.join_game());
}

TEST(CursesPickerClient, configure_networking_routes_join_to_a_real_lobby)
{
    PickerFixture f;
    pick(f.t(), 1); // Networking -> Join Game
    f.t().push_string("ws://127.0.0.1:1");
    f.t().push_special(KeyCode::Enter);
    f.t().push_char(U'q'); // cancel the disconnected lobby

    EXPECT_TRUE(f.client.configure_networking());
    EXPECT_TRUE(f.t().input_exhausted());
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

TEST(CursesPickerClient, convenience_entry_point_quits_cleanly)
{
    HeadlessTerminal term(40, 100);
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    term.push_special(KeyCode::Escape);

    run_curses_picker(term, clock, config, options);

    EXPECT_TRUE(term.input_exhausted());
}

TEST(CursesPickerClient, run_game_starts_real_session_and_withdraws)
{
    PickerFixture f;
    f.config.level = 1;
    f.config.team_families.clear();
    f.t().push_special(KeyCode::Escape);

    f.client.run_game();

    EXPECT_TRUE(f.t().input_exhausted());
    EXPECT_EQ(1, static_cast<int>(f.save().scen_num));
    EXPECT_EQ((std::vector<int>{FAMILY_SOLDIER}), f.config.team_families);
    EXPECT_GT(f.t().present_count(), 0);
}

// #207 design point 5 on the curses client: quitting an armed replay
// restores the campaign cursor at picker re-entry (the run_game tail) and
// clears the arm — the excursion never strands the cursor on the replayed
// level.
TEST(CursesPickerClient, run_game_quit_of_armed_replay_restores_cursor)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 3;
    f.save().add_level_completed("gladiator", 1);
    f.save().arm_replay(1);
    f.config.campaign = "gladiator";
    f.config.level = 1;
    f.config.team_families.clear();
    f.t().push_special(KeyCode::Escape);  // quit the level immediately

    f.client.run_game();

    EXPECT_EQ(3, static_cast<int>(f.save().scen_num))
        << "a quit excursion restores the origin cursor on re-entry";
    EXPECT_EQ(3, f.config.level) << "the session config follows the restore";
    EXPECT_EQ(0, static_cast<int>(f.save().replay_level));
}

// #207 design point 5 on the NETWORKED path (V5 Option A). A networked round
// folds into the session's OWN save copy and never copies back into the
// picker's, so a hosted REPLAY that ends any way but a win comes back with the
// arm still live. Left alone it would seed the next hosted round (V5 seeds the
// authoritative save from this very SaveData) into a second replay, and the
// next base-camp autosave would bank the replayed level as the campaign
// cursor.
TEST(CursesPickerClient, network_round_loss_of_armed_replay_restores_cursor)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 3;
    f.save().add_level_completed("gladiator", 1);
    f.save().arm_replay(1);
    f.config.campaign = "gladiator";
    f.config.level = 1;
    ASSERT_TRUE(f.save().replay_armed_for(1));

    GameRunResult lost;
    lost.ended = true;
    lost.ending = 1;
    dismiss(f.t()); // the "Mission complete" screen eats one key

    f.client.finish_network_round(lost);

    EXPECT_EQ(0, static_cast<int>(f.save().replay_level))
        << "a hosted loss must not leave the arm live for the next round";
    EXPECT_EQ(3, static_cast<int>(f.save().scen_num))
        << "the cursor comes home when the picker takes the save back";
    EXPECT_EQ(3, f.config.level) << "the session config follows the restore";

    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None,
              reloaded.load_with_error(og::data::active_company_slot()))
        << "the heal persists like any other picker mutation (§3.8)";
    EXPECT_EQ(3, static_cast<int>(reloaded.scen_num))
        << "the company file must never keep the replayed level as its cursor";
}

// The win half of the same tail. persist_networked_win already wrote the
// merged company file (restored cursor, banked share, completion mark) while
// this picker's save is a PRE-round copy — so the heal is memory-only there:
// autosaving the stale copy would erase the win it just earned.
TEST(CursesPickerClient, network_round_win_heals_memory_without_clobbering_disk)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 3;
    f.save().add_level_completed("gladiator", 1);
    f.save().m_totalcash[0] = 100;
    f.save().arm_replay(1);
    f.config.campaign = "gladiator";
    f.config.level = 1;

    // What the networked win already banked on disk: the restored cursor, a
    // second cleared level and the fold's gold.
    {
        SaveData persisted;
        persisted.reset();
        persisted.current_campaign = "gladiator";
        persisted.scen_num = 3;
        persisted.add_level_completed("gladiator", 1);
        persisted.add_level_completed("gladiator", 2);
        persisted.m_totalcash[0] = 900;
        ASSERT_EQ(SaveDataIoError::None,
                  persisted.save_with_error(og::data::active_company_slot()));
    }

    GameRunResult won;
    won.ended = true;
    won.ending = 0;
    won.next_level = 4;
    dismiss(f.t());

    f.client.finish_network_round(won);

    EXPECT_EQ(0, static_cast<int>(f.save().replay_level))
        << "the consumed arm clears on the win path too";
    EXPECT_EQ(3, static_cast<int>(f.save().scen_num))
        << "memory agrees with the cursor the fold already persisted";

    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None,
              reloaded.load_with_error(og::data::active_company_slot()));
    EXPECT_EQ(900u, reloaded.m_totalcash[0])
        << "the pre-round picker copy must never be autosaved over the win";
    EXPECT_TRUE(reloaded.is_level_completed(2))
        << "the win's completion mark must survive the picker tail";
    EXPECT_EQ(3, static_cast<int>(reloaded.scen_num));
}

TEST(CursesPickerClient, run_game_reports_real_session_load_failure)
{
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client(term, clock, config, options);
    ScopedCursesPickerMountRestore mount_restore;
    const std::string mounted_before =
        mount_restore.mounted_before();
    config.campaign =
        "missing-curses-run-campaign";
    config.level = 1;
    dismiss(term);

    client.run_game();

    EXPECT_TRUE(term.input_exhausted());
    EXPECT_NE(term.dump().find("Unable to start"),
              std::string::npos);
    EXPECT_NE(term.dump().find(
                  "failed to load level for local game"),
              std::string::npos);
    EXPECT_EQ(config.campaign,
              client.save_data().current_campaign);

    EXPECT_TRUE(mount_restore.restore());
    EXPECT_EQ(mounted_before, get_mounted_campaign());
}

// A richer run_picker: cycle difficulty through Team Build's Difficulty door
// (it left the Main menu — docs/camp-controls-design.md), then quit. Asserts
// the side effect persisted and the loop unwound.
TEST(CursesPickerClient, run_picker_team_build_action_then_quit)
{
    PickerFixture f;
    const int before = f.client.difficulty();

    const int cont_idx = main_menu_item_index(PickerMenuCommand::ContinueGame);
    ASSERT_GE(cont_idx, 0);
    // Main pass 1: "Continue Game" -> Team Build.
    pick(f.t(), cont_idx);
    // Team Build: the appended Difficulty door sits past the digit ceiling,
    // so this route walks the cursor to it.
    const int diff_idx =
        team_build_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(diff_idx, 0);
    step_to(f.t(), diff_idx);
    // Submenu: the Difficulty row starts highlighted; select it, dismiss its
    // notice, then Esc backs out of the submenu (-> Back).
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    f.t().push_special(KeyCode::Escape);
    // Team Build q (-> Back), then Main menu Esc (-> Quit).
    f.t().push_char(U'q');
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
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(cont_idx)));
    f.t().push_special(KeyCode::Enter);
    // Team Build: Back -> Main menu.
    f.t().push_char(U'q');
    // Main pass 2: Quit.
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);
    // The team survived the round trip.
    EXPECT_GE(team_count(f.save()), 1);
}

// --- Matchup screen --------------------------------------------------------

// Enter on a character cycles its team; the leading P# row independently
// cycles that local seat. The Matchup item lives in the Scenario submenu.
TEST(CursesPickerClient, matchup_screen_cycles_character_and_sets_my_team)
{
    PickerFixture f;
    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);
    EXPECT_EQ(matchup_item->label, "Matchup");
    ASSERT_EQ(1, team_count(f.save()));
    ASSERT_EQ(0, (int)f.save().team_list[0]->teamnum);
    ASSERT_EQ(0, (int)f.save().my_team);

    // 1st selectable = "P1 plays RED", 2nd = the character: cycle the
    // character onto GREEN, then cycle P1 onto GREEN, then leave.
    pick(f.t(), 1);                      // character row -> team 0 -> 1
    pick(f.t(), 0);                      // "P1 plays GREEN" (team 1)
    f.t().push_special(KeyCode::Escape); // leave the Matchup screen
    f.client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

    EXPECT_EQ(1, (int)f.save().team_list[0]->teamnum);
    EXPECT_EQ(1, (int)f.save().my_team);
    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Matchup"), std::string::npos) << dump;
    EXPECT_NE(dump.find("GREEN TEAM (P1) 1 HEROES"), std::string::npos) << dump;

    // §3.8 hook inventory row "team cycle": the cycle AUTOSAVED the company
    // — the new team round-trips from the active slot's file with no
    // manual save (my_team is session-only and never serialized).
    SaveData reloaded;
    ASSERT_EQ(SaveDataIoError::None,
              reloaded.load_with_error(og::data::active_company_slot()))
        << "the team-cycle autosave must have written the active slot";
    ASSERT_TRUE(reloaded.team_list[0] != nullptr);
    EXPECT_EQ(1, (int)reloaded.team_list[0]->teamnum)
        << "the cycled team must persist via the mutation autosave";
}

// A one-player local setup renders its one real, independently selectable seat.
TEST(CursesPickerClient, matchup_screen_shows_single_real_local_seat)
{
    PickerFixture f;
    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);

    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Matchup"), std::string::npos) << dump;
    EXPECT_NE(dump.find("P1 plays RED"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("P2 plays"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("Play on"), std::string::npos)
        << "the old P1-only action must not remain";
    EXPECT_NE(dump.find("BLUE TEAM 0 HEROES"), std::string::npos) << dump;
}

// A spectator-shaped local save has no seat rows or P# tags.
TEST(CursesPickerClient, matchup_screen_handles_zero_local_seats)
{
    PickerFixture f;
    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);
    f.save().numplayers = 0;

    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

    const std::string dump = f.t().dump();
    EXPECT_EQ(dump.find("P1 plays"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("(P1)"), std::string::npos) << dump;
    EXPECT_NE(dump.find("RED TEAM 1 HEROES"), std::string::npos) << dump;
}

// A loaded SDL save can still say "4 players", but a curses process advertises
// only its single real seat. Never turn the other three into phantom controls.
TEST(CursesPickerClient, matchup_screen_ignores_extra_saved_local_seats)
{
    PickerFixture f;
    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);
    f.save().numplayers = 4;

    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("P1 plays RED"), std::string::npos) << dump;
    EXPECT_NE(dump.find("RED TEAM (P1) 1 HEROES"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("P2 plays"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("(P2"), std::string::npos) << dump;
}

// numplayers and my_team are old fields; damaged/legacy values still yield at
// most one honest terminal seat, with an invalid team normalized safely.
TEST(CursesPickerClient, matchup_screen_handles_damaged_legacy_seat_fields)
{
    PickerFixture f;
    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);
    f.save().numplayers = 255;
    f.save().my_team = 99;

    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

    const std::string dump = f.t().dump();
    EXPECT_EQ(f.save().my_team, 0);
    EXPECT_NE(dump.find("P1 plays RED"), std::string::npos) << dump;
    EXPECT_EQ(dump.find("P2 plays"), std::string::npos) << dump;
}

// The Matchup roster never truncates at the terminal: the list view scrolls
// to follow the cursor, so a full 24-member team stays reachable AND
// visible on a short terminal (the curses analogue of the SDL pager).
TEST(CursesPickerClient, matchup_screen_scrolls_to_keep_cursor_visible)
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

    const auto* matchup_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::Teams);
    ASSERT_NE(matchup_item, nullptr);

    // Cursor starts on "P1 plays RED"; 10 downs land on the 10th member
    // (Grunt09), far past the 12-row terminal's first page.
    for (int i = 0; i < 10; ++i)
        term.push_char(U'j');
    term.push_special(KeyCode::Escape);
    client.handle_menu_item(PickerMenuId::Scenario, *matchup_item);

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
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 1;

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *viewer_item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("SCEN 1:"), std::string::npos) << dump;
    EXPECT_NE(dump.find("TEAM"), std::string::npos) << dump;
}

TEST(CursesPickerClient, view_scenario_reports_mount_and_level_failures)
{
    const auto* viewer_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ViewScenario);
    ASSERT_NE(viewer_item, nullptr);

    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client(term, clock, config, options);
    ScopedCursesPickerMountRestore mount_restore;
    const std::string mounted_before =
        mount_restore.mounted_before();

    client.save_data().current_campaign =
        "missing-curses-view-campaign";
    dismiss(term);
    client.handle_menu_item(
        PickerMenuId::Scenario, *viewer_item);
    EXPECT_NE(term.dump().find("is not mounted"),
              std::string::npos);
    EXPECT_TRUE(term.input_exhausted());

    const CampaignPackageIoError mounted =
        mount_campaign_package_with_error(
            "gladiator");
    EXPECT_EQ(CampaignPackageIoError::None, mounted);
    if (mounted == CampaignPackageIoError::None) {
        client.save_data().current_campaign =
            "gladiator";
        client.save_data().scen_num = 32000;
        dismiss(term);
        client.handle_menu_item(
            PickerMenuId::Scenario, *viewer_item);
        EXPECT_NE(term.dump().find("Could not load level 32000"),
                  std::string::npos);
        EXPECT_TRUE(term.input_exhausted());
    }

    EXPECT_TRUE(mount_restore.restore());
    EXPECT_EQ(mounted_before, get_mounted_campaign());
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
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(cont_idx)));
    f.t().push_special(KeyCode::Enter);
    // Team Build: the "Scenario" item opens the submenu. The #206 Camp door
    // pushed Scenario past the digit-jump ceiling (only the first 9
    // selectable rows carry digits), so this route walks the cursor.
    int scenario_idx = -1;
    {
        const auto& def = og::ui::picker_menu_definition(PickerMenuId::TeamBuild);
        for (int i = 0; i < static_cast<int>(def.items.size()); ++i)
            if (def.items[static_cast<size_t>(i)].command ==
                PickerMenuCommand::Scenario)
                scenario_idx = i;
    }
    ASSERT_GE(scenario_idx, 0);
    step_to(f.t(), scenario_idx);
    // Scenario submenu: leave with Esc (-> Back), then Team Build Esc,
    // then Main menu Esc (-> Quit).
    f.t().push_special(KeyCode::Escape);
    f.t().push_char(U'q');
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);
    EXPECT_TRUE(f.t().input_exhausted())
        << "the scenario submenu round trip should consume the whole script";
}

// --- #206 Camp (the scripted Base Camp gameplay zone) -----------------------

namespace {

// Registers a throwaway scripted campaign for one test and restores the
// pack-script registry (and the gameplay context campaign dispatch
// resolves) afterwards — the test_campaign_picker_session fixture approach.
// The chunk name deliberately does NOT start with `packs/`: that prefix
// declares the bytes to the pack-Lua coverage inventory, and this throwaway
// chunk exists nowhere in the repository.
class ScopedSyntheticCampaignPicker
{
public:
    explicit ScopedSyntheticCampaignPicker(const std::string& source)
        : previous_game_(current_game)
        , scripts_(og::script::pack_scripts())
    {
        current_game = nullptr;  // dispatch resolves the shared UI VM
        og::script::register_pack_script(
            {"test.cursescamp", "cursescamp/scripts/c.lua", source});
    }

    ~ScopedSyntheticCampaignPicker()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : scripts_)
            og::script::register_pack_script(script);
        current_game = previous_game_;
    }

private:
    GameplayContext* previous_game_;
    std::vector<og::script::PackScript> scripts_;
};

} // namespace

// run_picker reaches the scripted camp by ordinal: Team Build -> Camp
// (0-based selectable index 6, ordinal 7). That position is load-bearing —
// the camp is the door into everything a campaign composes, so it has to
// stay inside the digit-jump budget. Choosing a level row runs the curses
// set-level tail; Esc at the camp prompt closes it back to Team Build.
TEST(CursesPickerClient, run_picker_camp_sets_level_from_the_zone)
{
    PickerFixture f;
    // The earned-roads gate closes unearned rows; THE PIT is a replay of a
    // cleared level, the state the camp's set-level tail serves.
    f.save().add_level_completed("gladiator", 9);
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return {
      widgets = {
        { kind = "actions", entries = {
            { id = "300", label = "THE CIRCLE", kind = "level", level = 300 },
            { id = "9", label = "THE PIT", kind = "level", level = 9 },
          } },
        { kind = "roster" },
      },
    }
  end,
}))LUA");

    const int cont_idx = main_menu_item_index(PickerMenuCommand::ContinueGame);
    ASSERT_GE(cont_idx, 0);
    pick(f.t(), cont_idx); // Main: Continue -> Team Build
    // Team Build: the Camp door. Digit jump counts selectable entries only,
    // so the item index maps 1:1 past the context header rows.
    int camp_idx = -1;
    {
        const auto& def =
            og::ui::picker_menu_definition(PickerMenuId::TeamBuild);
        for (int i = 0; i < static_cast<int>(def.items.size()); ++i)
            if (def.items[static_cast<size_t>(i)].command ==
                PickerMenuCommand::CampaignCamp)
                camp_idx = i;
    }
    ASSERT_GE(camp_idx, 0);
    ASSERT_LE(camp_idx, 8)
        << "the Camp door must stay inside the digit-jump budget";
    pick(f.t(), camp_idx); // Team Build -> the scripted camp
    // Camp prompt: row 2 = THE PIT (level 9) -> the set-level tail.
    f.t().push_char(U'2');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t()); // the "Level set to THE PIT." notice screen
    // Camp prompt again: Esc cancels the prompt = back, closing the camp.
    f.t().push_special(KeyCode::Escape);
    // Unwind: Team Build q (Back), Main Esc (Quit).
    f.t().push_char(U'q');
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ(9, (int)f.save().scen_num)
        << "a camp level row must run the curses set-level tail";
    EXPECT_EQ(9, f.config.level)
        << "the tail also updates the session config level";
    EXPECT_TRUE(f.t().input_exhausted())
        << "the camp round trip should consume the whole script";
}

// A camp composes as many lines as the campaign wants — a full company alone
// is 25 roster lines — and every line carries a live ordinal the prompt still
// accepts. On the stock 24x80 terminal the block outruns the screen, so it
// scrolls: the marker says how much is off-screen and the arrows reach the
// tail, where the oath door sits. Truncating it would hide controls the
// player is expected to type.
TEST(CursesPickerClient, camp_prompt_scrolls_a_composition_past_the_screen)
{
    PickerFixture f({}, 24, 80);  // the stock terminal, not the test's 40x100
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    local docket = {}
    for i = 1, 14 do
      docket[i] = { id = "job" .. i, label = "JOB " .. i, kind = "action" }
    end
    return {
      widgets = {
        { kind = "text", weight = 2, lines = {
            "The company waits at the fire.",
            "The road east is open.",
            "The bearer keeps the coin.",
          } },
        { kind = "actions", weight = 2, entries = docket },
        { kind = "roster",
          assign = { key = "muster", labels = { "WAR", "BURDEN" } } },
      },
    }
  end,
}))LUA");

    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CampaignCamp);
    ASSERT_NE(item, nullptr);
    // Page to the bottom, step back up, page home, then back to the bottom:
    // the offset clamps at both ends and the last frame shows the tail. Esc
    // then closes the prompt and the camp.
    f.t().push_special(KeyCode::PageDown);
    f.t().push_special(KeyCode::Up);
    f.t().push_special(KeyCode::PageUp);
    f.t().push_special(KeyCode::PageDown);
    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("SWEAR MUSTER"), std::string::npos)
        << "the oath door is the LAST composed line — a prompt that cannot "
           "scroll hides it while still accepting its ordinal:\n"
        << dump;
    EXPECT_NE(dump.find("Up/Down scroll"), std::string::npos)
        << "an overflowing block must say so on screen:\n" << dump;
    // The prompt line survives the scroll: 14 docket rows + the oath door.
    EXPECT_NE(dump.find("Camp # [1-15]"), std::string::npos) << dump;
}

// #207: a camp docket row marked `replay = true` on a CLEARED level arms
// the excursion through the curses tail — arm_replay (origin remembered,
// cursor onto the level) instead of the plain write, answered in the
// replay voice.
TEST(CursesPickerClient, camp_replay_row_arms_through_the_curses_tail)
{
    ScopedCursesPickerMountRestore mount_restore;
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    PickerFixture f;
    f.save().current_campaign = "gladiator";
    f.save().scen_num = 3;
    f.config.level = 3;
    f.save().add_level_completed("gladiator", 1);
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = {
      { kind = "actions", entries = {
          { id = "1", label = "THE FIRST ROAD", kind = "level", level = 1, replay = true },
        } },
      { kind = "roster" },
    } }
  end,
}))LUA");

    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CampaignCamp);
    ASSERT_NE(item, nullptr);
    f.t().push_char(U'1');
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());                       // the "Replaying ..." notice
    f.t().push_special(KeyCode::Escape);  // close the camp
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    EXPECT_EQ(1, f.config.level);
    EXPECT_EQ(1, static_cast<int>(f.save().scen_num));
    EXPECT_EQ(1, static_cast<int>(f.save().replay_level))
        << "the curses camp tail arms a cleared replay row";
    EXPECT_EQ(3, static_cast<int>(f.save().replay_origin));
    // (The "Replaying <label>. GO when ready." notice itself is pinned in
    // the shared-driver tests — dump() is the FINAL frame only, and the
    // camp re-prompt overpaints the notice after its dismissal.)
    f.save().clear_replay_arm();
}

// A benched, unsworn, LOCKED hero on the stock 24x80 terminal: one readable
// row of columns — the padlock letter, the oath cell and the reason — with
// the oath heading directly over its cell. The reason used to arrive behind
// a " - " separator that collided with the unsworn cell's own "-", printing
// "- - " mid-row; and the heading used to sit at column 18 in the summary
// strip while the values it named sat at 49.
TEST(CursesPickerClient, camp_roster_row_reads_as_columns_at_24x80)
{
    PickerFixture f({}, 24, 80);  // the stock terminal, not the test's 40x100
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster",
      locks = { { unset = true, reason = "Swear at the Falls first." } },
      assign = { key = "muster", labels = { "WAR", "BURDEN" } } } } }
  end,
}))LUA");

    for (std::unique_ptr<guy>& member : f.save().team_list) {
        if (member != nullptr) {
            member->name = "Tom";
            member->deployed = false;  // benched: the padlock shows
            member->campaign_tag = 0;  // unsworn: the oath cell is "-"
        }
    }

    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CampaignCamp);
    ASSERT_NE(item, nullptr);
    f.t().push_special(KeyCode::Escape);
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);

    const std::string dump = f.t().dump();
    EXPECT_EQ(std::string::npos, dump.find("- - "))
        << "the unsworn oath cell must not stutter into the reason:\n"
        << dump;
    EXPECT_NE(std::string::npos, dump.find("[L] Tom"))
        << "the padlock is a letter on a prompt:\n" << dump;
    EXPECT_NE(std::string::npos, dump.find("-  Swear at the Falls first."))
        << "the reason is a trailing column, space-separated:\n" << dump;
    // The heading sits over the cell it names, in the SAME rendered frame.
    {
        std::vector<std::string> rows;
        for (std::size_t start = 0, end = dump.find('\n');
             start < dump.size();
             start = end + 1, end = dump.find('\n', start)) {
            if (end == std::string::npos)
                end = dump.size();
            rows.push_back(dump.substr(start, end - start));
        }
        std::size_t heading_col = std::string::npos;
        std::size_t cell_col = std::string::npos;
        for (const std::string& row : rows) {
            if (heading_col == std::string::npos &&
                row.find("COMPANY") != std::string::npos)
                heading_col = row.find("MUSTER");
            const std::size_t hero = row.find("[L] Tom");
            if (hero != std::string::npos && cell_col == std::string::npos)
                cell_col = row.find('-', row.find("XP=", hero));
        }
        ASSERT_NE(std::string::npos, heading_col) << dump;
        ASSERT_NE(std::string::npos, cell_col) << dump;
        EXPECT_EQ(heading_col, cell_col)
            << "MUSTER must head the oath column, not trail the summary:\n"
            << dump;
    }
    // The purse is on the camp screen, not only back on Team Build.
    EXPECT_NE(std::string::npos, dump.find("GOLD "))
        << "a camp with no gold on it cannot price anything:\n" << dump;
}

// With no registered campaign, the Camp row shows the shared guard line.
// (The literal bytes are pinned once, by the text drive; the exported
// constant proves the same line reaches the curses surface.)
TEST(CursesPickerClient, camp_without_a_zone_shows_the_guard_line)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CampaignCamp);
    ASSERT_NE(item, nullptr);

    dismiss(f.t()); // the guard notice renders as a show_text screen
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *item);
    EXPECT_NE(f.t().dump().find(
                  std::string(og::ui::kCampaignCampNoZoneMessage)),
              std::string::npos)
        << "a campaign that composed no camp must show the guard line";
}

// The camp's rules bind this client's OWN Team Build commands, not just its
// Camp screen: a deploy lock, a cleared can_train and a cleared can_hire each
// refuse in the campaign's words. (The SDL panel hides the retired control
// instead; a prompt cannot hide, so it answers.)
TEST(CursesPickerClient, roster_commands_obey_the_camps_rules)
{
    PickerFixture f;
    ScopedSyntheticCampaignPicker picker(R"LUA(og.register_campaign_hooks({
  base_camp = function()
    return { widgets = { { kind = "roster",
      can_train = false,
      can_hire = false,
      locks = { { unset = true, reason = "Swear first." } } } } }
  end,
}))LUA");

    // Bench the starting soldier: benching is never refused, so the locked
    // path is the deploy back.
    for (std::unique_ptr<guy>& member : f.save().team_list)
        if (member != nullptr)
            member->deployed = false;

    const auto* deploy = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::ToggleDeploy);
    ASSERT_NE(deploy, nullptr);
    f.t().push_special(KeyCode::Enter);  // accept the prompt's default row 1
    dismiss(f.t());  // the refusal renders as a show_text screen
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *deploy);
    EXPECT_NE(f.t().dump().find("Swear first."), std::string::npos)
        << "the lock's own reason answers the deploy prompt";
    EXPECT_EQ(0, og::ui::count_deployed_members(f.save()))
        << "a refused toggle must not deploy";

    const auto* train = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::TrainTeam);
    ASSERT_NE(train, nullptr);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *train);
    EXPECT_NE(f.t().dump().find(
                  std::string(og::ui::kCampaignRosterTrainClosedMessage)),
              std::string::npos)
        << "a camp that retired training says so";

    const auto* hire = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::HireTroops);
    ASSERT_NE(hire, nullptr);
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *hire);
    EXPECT_NE(f.t().dump().find(
                  std::string(og::ui::kCampaignRosterHireClosedMessage)),
              std::string::npos)
        << "can_hire is the flag that hides HIRE on the panel";
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

// Respawns: Off -> Heroes -> Everyone -> Team 1 Heroes -> Off.
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
    EXPECT_EQ(3, (int)f.save().respawn_mode);
    EXPECT_NE(f.t().dump().find("Respawns: Team 1 Heroes"),
              std::string::npos);

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

// Infinite Gold: Off (0, classic economy) <-> On (free hire/train purchases).
// Session-only, so the toggle never autosaves the company.
TEST(CursesPickerClient, infinite_gold_toggles)
{
    PickerFixture f;
    const auto* item = og::ui::find_picker_menu_item(
        PickerMenuId::Difficulty, PickerMenuCommand::ToggleInfiniteGold);
    ASSERT_NE(item, nullptr);
    ASSERT_EQ(0, (int)f.save().infinite_gold) << "default is the classic economy";

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(1, (int)f.save().infinite_gold);
    EXPECT_NE(f.t().dump().find("Infinite Gold: On"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Difficulty, *item);
    EXPECT_EQ(0, (int)f.save().infinite_gold);
    EXPECT_NE(f.t().dump().find("Infinite Gold: Off"), std::string::npos);
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
    f.save().infinite_gold = 1;

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
    EXPECT_NE(dump.find("Infinite Gold: On"), std::string::npos) << dump;
}

// run_picker reaches the nested Difficulty submenu from the Team Build door
// and unwinds cleanly: Difficulty -> cycle Respawns -> Back -> Quit (the
// curses analogue of run_picker_through_scenario_submenu_then_quit above).
TEST(CursesPickerClient, run_picker_through_difficulty_submenu_then_quit)
{
    PickerFixture f;

    const int cont_idx = main_menu_item_index(PickerMenuCommand::ContinueGame);
    ASSERT_GE(cont_idx, 0);
    pick(f.t(), cont_idx); // Main pass 1: Continue -> Team Build
    const int door_idx =
        team_build_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(door_idx, 0);
    ASSERT_GT(door_idx, 8)
        << "the appended door is past the digit ceiling by design — the "
           "arrow walk below is the only way in, so it must be tested";
    ASSERT_EQ(nullptr, og::ui::find_picker_menu_item(
                           PickerMenuId::Main,
                           PickerMenuCommand::OpenDifficultyMenu))
        << "no difficulty door survives on the Main menu";
    step_to(f.t(), door_idx);
    // Difficulty submenu: cycle Respawns (2nd selectable row), dismiss the
    // notice, then leave with Esc (-> Back), Team Build q (-> Back), then
    // Main menu Esc (-> Quit).
    pick(f.t(), 1);
    dismiss(f.t());
    f.t().push_special(KeyCode::Escape);
    f.t().push_char(U'q');
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ(1, (int)f.save().respawn_mode)
        << "the submenu action must land on the save";
    EXPECT_TRUE(f.t().input_exhausted())
        << "the difficulty submenu round trip should consume the whole script";
}

// #155: the CLOUD submenu round trip — set a passphrase (stores the DERIVED
// key, never the raw phrase), then Upload/Download degrade with the D8
// unavailable notice (the curses bridge installs no cloud HTTP callbacks).
TEST(CursesPickerClient, run_picker_through_cloud_submenu_then_quit)
{
    cfg.data.erase("cloud");
    PickerFixture f;

    const int door_idx =
        main_menu_item_index(PickerMenuCommand::OpenCloudMenu);
    ASSERT_GE(door_idx, 0);
    ASSERT_LE(door_idx, 8) << "digit-jump addresses the first 9 items";
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(door_idx)));
    f.t().push_special(KeyCode::Enter);

    // Cloud submenu: PASSPHRASE (row 0) -> type a phrase, accept, dismiss
    // the "Passphrase set." notice.
    pick(f.t(), 0);
    for (const char ch : std::string("correct horse battery"))
        f.t().push_char(static_cast<char32_t>(ch));
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t());
    // UPLOAD (row 1): no HTTP in this client -> unavailable notice.
    pick(f.t(), 1);
    dismiss(f.t());
    // DOWNLOAD (row 2): same degradation.
    pick(f.t(), 2);
    dismiss(f.t());
    // Esc leaves the submenu (Back), Esc on Main quits.
    f.t().push_special(KeyCode::Escape);
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ("73270125791ba273", cfg.get_setting("cloud", "key"))
        << "the DERIVED key is persisted (D9), pinned to the D2 vector";
    EXPECT_EQ("0", cfg.get_setting("cloud", "revision"));
    EXPECT_TRUE(f.t().input_exhausted())
        << "the cloud submenu round trip should consume the whole script";
    cfg.data.erase("cloud");
}

// #155: a too-short passphrase is rejected client-side and nothing persists.
TEST(CursesPickerClient, cloud_passphrase_length_gate_rejects_short_input)
{
    cfg.data.erase("cloud");
    PickerFixture f;

    const int door_idx =
        main_menu_item_index(PickerMenuCommand::OpenCloudMenu);
    ASSERT_GE(door_idx, 0);
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(door_idx)));
    f.t().push_special(KeyCode::Enter);
    pick(f.t(), 0);
    for (const char ch : std::string("short12")) // 7 chars -> reject
        f.t().push_char(static_cast<char32_t>(ch));
    f.t().push_special(KeyCode::Enter);
    dismiss(f.t()); // "Passphrase must be 8-64 characters."
    f.t().push_special(KeyCode::Escape);
    f.t().push_special(KeyCode::Escape);

    og::ui::run_picker(f.client);

    EXPECT_EQ("", cfg.get_setting("cloud", "key"))
        << "a rejected passphrase must not persist a key";
    EXPECT_TRUE(f.t().input_exhausted());
    cfg.data.erase("cloud");
}

// #155: the DOWNLOAD flow all the way down in the curses projection. The
// round trip above stops at the D8 unavailable line (no HTTP on the bridge),
// so this one installs a canned vault reply and walks the rest: the NO-first
// Yes/No list confirm over an existing company, install_company_bytes (which
// snapshots first), and the §2.3 open — whose "Loaded" screen lands before
// the cloud notice.
TEST(CursesPickerClient, cloud_download_confirms_installs_and_opens_company)
{
    cfg.data.erase("cloud");

    // Cloud-side company: real writer bytes (loadable by the open path),
    // staged through a scratch slot that is then removed.
    CursesSlotCleanup staging_cleanup{{"wp3cloudr"}};
    ASSERT_TRUE(seed_curses_company("wp3cloudr", "CLOUD BAND", 9300));
    std::string remote_bytes;
    {
        std::ifstream in(std::filesystem::path(get_user_path()) / "save" /
                             "wp3cloudr.gtl",
                         std::ios::binary);
        remote_bytes.assign((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    }
    ASSERT_FALSE(remote_bytes.empty());
    ASSERT_TRUE(remove_user_file("save/wp3cloudr.gtl"));

    // ...and the DIFFERENT company already occupying the target slot, so the
    // NO-first confirm actually fires.
    const std::string company_slot =
        unique_curses_company_slot("curses-cloud-download");
    CursesCompanyArtifactCleanup cleanup(company_slot);
    ASSERT_TRUE(cleanup.ready());
    ASSERT_TRUE(seed_curses_company(company_slot, "LOCAL BAND", 9200));

    const std::vector<std::uint8_t> remote_raw(remote_bytes.begin(),
                                               remote_bytes.end());
    const std::string get_body =
        std::format(
            R"({{"revision":5,"uploaded_at":1754200000000,"slot":"{}",)"
            R"("save_name":"CLOUD BAND","scen_num":1,"last_played":9300,)"
            R"("data_hex":"{}"}})",
            company_slot, og::ui::cloud::hex_encode(remote_raw));

    std::vector<std::string> get_urls;
    // Restore on every exit path: an ASSERT inside the flow must not leak the
    // faked HTTP into the rest of the binary.
    struct BridgeRestore {
        PlatformBridge saved;
        ~BridgeRestore() { set_platform_bridge(saved); }
    } bridge_restore{platform_bridge()};

    PlatformBridge faked = bridge_restore.saved;
    faked.cloud_http_get = [&](const std::string& url) {
        get_urls.push_back(url);
        og::ui::cloud::CloudHttpResult result;
        result.status = 200;
        result.body = get_body;
        return result;
    };
    faked.cloud_http_post = [](const std::string&, const std::string&) {
        // Present only so hooks_available() passes; this flow never posts.
        og::ui::cloud::CloudHttpResult result;
        result.status = 500;
        return result;
    };
    set_platform_bridge(faked);

    {
        PickerFixture f;

        const int door_idx =
            main_menu_item_index(PickerMenuCommand::OpenCloudMenu);
        ASSERT_GE(door_idx, 0);
        f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(door_idx)));
        f.t().push_special(KeyCode::Enter);

        // PASSPHRASE (row 0) -> derived key stored.
        pick(f.t(), 0);
        for (const char ch : std::string("correct horse battery"))
            f.t().push_char(static_cast<char32_t>(ch));
        f.t().push_special(KeyCode::Enter);
        dismiss(f.t());
        // DOWNLOAD (row 2): the confirm highlights No; digit-jump to Yes.
        pick(f.t(), 2);
        f.t().push_char(U'2');
        f.t().push_special(KeyCode::Enter);
        dismiss(f.t()); // the open path's "Loaded" screen
        dismiss(f.t()); // the "Downloaded 'CLOUD BAND'." notice
        f.t().push_special(KeyCode::Escape); // leave the submenu
        f.t().push_special(KeyCode::Escape); // Main -> quit

        og::ui::run_picker(f.client);

        EXPECT_TRUE(f.t().input_exhausted())
            << "the download round trip should consume the whole script";
        EXPECT_EQ(company_slot, f.config.save_name)
            << "[SAVE-R2] the open repoints the curses slot";
        EXPECT_EQ("CLOUD BAND", f.save().save_name)
            << "the opened company is the downloaded one";
    }

    ASSERT_EQ(1u, get_urls.size());
    EXPECT_NE(std::string::npos,
              get_urls[0].find("/api/save/73270125791ba273"))
        << "the GET addresses the derived-key route";
    EXPECT_GE(og::data::list_company_backups(company_slot).size(), 1u)
        << "install_company_bytes snapshots before the swap";
    EXPECT_EQ("5", cfg.get_setting("cloud", "revision"))
        << "the server revision persists for the next optimistic upload";
    EXPECT_TRUE(cleanup.cleanup());
    cfg.data.erase("cloud");
}
