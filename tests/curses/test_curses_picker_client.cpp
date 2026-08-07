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
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/platform_bridge.h>
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
// A generous 40x100 grid leaves room for every menu the picker draws.
struct PickerFixture {
    HeadlessTerminal term{40, 100};
    FakeClock clock;
    TextPickerConfig config;
    CursesPickerOptions options;
    CursesPickerClient client;

    explicit PickerFixture(CursesPickerOptions initial_options = {})
        : options(std::move(initial_options)),
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
    // Jump to the 7th selectable item (the Difficulty door) and select it.
    const int diff_idx = main_menu_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(diff_idx, 0);
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(diff_idx)));
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
    f.save().current_campaign = "gladiator";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::TeamBuild, *teams_item);
    EXPECT_EQ(0, (int)f.save().ctf_team_count);
    EXPECT_NE(f.t().dump().find("versus maps only"), std::string::npos);

    // Versus campaign: both settings cycle (Auto -> 2).
    f.save().current_campaign = "modes";
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

// Matched teams (design D3): the sentinel value 5 renders the shared
// formatter's "Teams: Match" label in the terminal list too.
TEST(CursesPickerClient, ctf_menu_labels_render_matched_sentinel)
{
    PickerFixture f;
    f.save().ctf_team_count = og::sim::kTeamCountMatched;
    const auto* teams_item = og::ui::find_picker_menu_item(
        PickerMenuId::TeamBuild, PickerMenuCommand::CycleCtfTeamCount);
    ASSERT_NE(teams_item, nullptr);

    // Drive present_menu so the dynamic label renders in the list.
    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::TeamBuild);
    const std::string dump = f.t().dump();
    EXPECT_NE(dump.find("Teams: Match"), std::string::npos) << dump;
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

// A richer run_picker: cycle difficulty through the Main menu's Difficulty
// door, then quit. Asserts the side effect persisted and the loop unwound.
TEST(CursesPickerClient, run_picker_main_action_then_quit)
{
    PickerFixture f;
    const int before = f.client.difficulty();

    const int diff_idx = main_menu_item_index(PickerMenuCommand::OpenDifficultyMenu);
    ASSERT_GE(diff_idx, 0);
    // First Main menu pass: open the Difficulty submenu (digit+Enter).
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(diff_idx)));
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

// --- CTF scenario-troops toggle -------------------------------------------

// The troops control lives in the SCENARIO submenu now and is NOT
// versus-gated: "strip everything authored" applies to classic campaigns
// too, and both states cycle the same way on either campaign kind.
TEST(CursesPickerClient, ctf_troops_toggle_runs_on_every_campaign)
{
    PickerFixture f;
    const auto* troops_item = og::ui::find_picker_menu_item(
        PickerMenuId::Scenario, PickerMenuCommand::ToggleCtfScenarioTroops);
    ASSERT_NE(troops_item, nullptr);

    // Classic campaign: no refusal notice, ALL -> OWN -> ALL.
    f.save().current_campaign = "gladiator";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *troops_item);
    EXPECT_EQ(2, (int)f.save().ctf_strip_scenario_troops);
    EXPECT_EQ(f.t().dump().find("versus maps only"), std::string::npos);
    EXPECT_NE(f.t().dump().find("TROOPS: OWN"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *troops_item);
    EXPECT_EQ(0, (int)f.save().ctf_strip_scenario_troops);

    // Versus campaign: the same two states.
    f.save().current_campaign = "modes";
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *troops_item);
    EXPECT_EQ(2, (int)f.save().ctf_strip_scenario_troops);
    EXPECT_NE(f.t().dump().find("TROOPS: OWN"), std::string::npos);

    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *troops_item);
    EXPECT_EQ(0, (int)f.save().ctf_strip_scenario_troops);

    // A save carrying the retired middle state cycles back to ALL.
    f.save().ctf_strip_scenario_troops = 1;
    dismiss(f.t());
    f.client.handle_menu_item(PickerMenuId::Scenario, *troops_item);
    EXPECT_EQ(0, (int)f.save().ctf_strip_scenario_troops);
}

// The scenario menu label surfaces the live troops setting.
TEST(CursesPickerClient, ctf_troops_label_formats_from_save)
{
    PickerFixture f;
    f.save().ctf_strip_scenario_troops = 0;
    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::Scenario);
    EXPECT_NE(f.t().dump().find("TROOPS: ALL"), std::string::npos);

    f.save().ctf_strip_scenario_troops = 2;
    f.t().push_special(KeyCode::Escape);
    (void)f.client.present_menu(PickerMenuId::Scenario);
    EXPECT_NE(f.t().dump().find("TROOPS: OWN"), std::string::npos);
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
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(scenario_idx)));
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
    f.t().push_char(static_cast<char32_t>(U'1' + static_cast<char32_t>(door_idx)));
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
