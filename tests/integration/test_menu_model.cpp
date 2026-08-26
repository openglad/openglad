#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/picker_lobby_network_client.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace og::ui {

std::vector<std::string> build_host_picker_status_lines(
    const std::string& direct_address,
    bool has_direct_transport,
    int port,
    const std::string& direct_status_message,
    const std::string& relay_room_code,
    const std::string& relay_status_message,
    std::optional<std::size_t> player_count);

} // namespace og::ui

TEST(MenuModel, main_definition_and_lookup)
{
    using namespace og::ui;
    const PickerMenuDefinition& def = picker_menu_definition(PickerMenuId::Main);

    ASSERT_EQ(static_cast<int>(PickerMenuId::Main), static_cast<int>(def.id)) << "main menu definition should report main id";
    ASSERT_EQ(8u, def.items.size())
        << "terminal main menu has no player-count rows and no difficulty "
           "door (7 classic items + the #155 cloud door)";

    const PickerMenuItem* cloud_row =
        find_picker_menu_item(PickerMenuId::Main, "cloud");
    ASSERT_TRUE(cloud_row != nullptr);
    EXPECT_EQ("Cloud Saves", cloud_row->label)
        << "the terminal label matches the SDL CLOUD SAVES face";

    const PickerMenuItem* begin = find_picker_menu_item(PickerMenuId::Main, "begin_new_game");
    ASSERT_TRUE(begin != nullptr) << "begin_new_game id should resolve";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::BeginNewGame), static_cast<int>(begin->command)) << "begin_new_game should map to BeginNewGame command";

    const PickerMenuItem* level_editor =
        find_picker_menu_item(PickerMenuId::Main, "level_edit");
    ASSERT_NE(nullptr, level_editor);
    EXPECT_EQ("Level Editor", level_editor->label);

    const PickerMenuItem* networking = find_picker_menu_item(PickerMenuId::Main, "networking");
    ASSERT_TRUE(networking == nullptr) << "networking should now live in the team build menu";

    const PickerMenuItem* host = find_picker_menu_item(PickerMenuId::Main, "host_game");
    ASSERT_TRUE(host == nullptr) << "host_game id should no longer appear in the main menu";

    const PickerMenuItem* join = find_picker_menu_item(PickerMenuId::Main, "join_game");
    ASSERT_TRUE(join == nullptr) << "join_game id should no longer appear in the main menu";

    for (int players = 1; players <= 4; ++players) {
        EXPECT_EQ(nullptr, find_picker_menu_item(
            PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, players))
            << "player-count choices belong to Base Camp, not Main";
    }

    const PickerMenuItem* missing = find_picker_menu_item(PickerMenuId::Main, "missing-item");
    ASSERT_TRUE(missing == nullptr) << "missing id should return nullptr";
}


TEST(MenuModel, team_build_lookup)
{
    using namespace og::ui;
    const PickerMenuDefinition& def = picker_menu_definition(PickerMenuId::TeamBuild);

    ASSERT_EQ(static_cast<int>(PickerMenuId::TeamBuild), static_cast<int>(def.id)) << "team build definition should report team build id";
    ASSERT_TRUE(def.items.size() >= 11) << "team build should expose expected item count";

    const PickerMenuItem* start = find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::StartGame);
    ASSERT_TRUE(start != nullptr) << "start game command should resolve in team build";
    ASSERT_TRUE(start->id == "go") << "start game item id should be go";

    const PickerMenuItem* back = find_picker_menu_item(PickerMenuId::TeamBuild, "back");
    ASSERT_TRUE(back != nullptr) << "back id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::Back), static_cast<int>(back->command)) << "back item should map to Back command";

    const PickerMenuItem* networking =
        find_picker_menu_item(PickerMenuId::TeamBuild, "networking");
    ASSERT_TRUE(networking != nullptr) << "networking id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::Networking), static_cast<int>(networking->command))
        << "team build networking item should map to Networking command";

    const PickerMenuItem* wrong_arg = find_picker_menu_item(
        PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 99);
    ASSERT_TRUE(wrong_arg == nullptr) << "unknown arg variant should return nullptr";

    // The flat CTF trio left the team build menu for the modes camp's MATCH
    // SETUP page (docs/camp-controls-design.md); the DIFFICULTY door took
    // their place, appended so nothing above it moved.
    for (const char* retired_id : {"ctf_teams", "ctf_caps", "ctf_troops"}) {
        ASSERT_TRUE(find_picker_menu_item(PickerMenuId::TeamBuild,
                                          retired_id) == nullptr)
            << retired_id << " belongs to the camp's MATCH SETUP page now";
        ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, retired_id) ==
                    nullptr)
            << retired_id << " never lived in the main menu";
    }

    ASSERT_EQ(12u, def.items.size())
        << "team build is the core team items + the zone's Camp door + "
           "networking + scenario + the DIFFICULTY door + the LINEUP door";

    const PickerMenuItem* difficulty =
        find_picker_menu_item(PickerMenuId::TeamBuild, "difficulty");
    ASSERT_TRUE(difficulty != nullptr)
        << "difficulty id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::OpenDifficultyMenu),
              static_cast<int>(difficulty->command))
        << "difficulty should map to the OpenDifficultyMenu door";
    ASSERT_TRUE(&def.items[10] == difficulty)
        << "difficulty keeps 1-based position 11: LINEUP appended BELOW it";

    // LINEUP (docs/lineup-design.md §8): appended after difficulty, so every
    // ordinal above it — and both 1-based position consumers — are untouched.
    const PickerMenuItem* lineup =
        find_picker_menu_item(PickerMenuId::TeamBuild, "lineup");
    ASSERT_TRUE(lineup != nullptr) << "lineup id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::Lineup),
              static_cast<int>(lineup->command))
        << "lineup should map to the Lineup door command";
    ASSERT_EQ("Lineup", std::string(lineup->label));
    ASSERT_TRUE(&def.items[11] == lineup)
        << "lineup is appended last, at 1-based position 12";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, "lineup") == nullptr)
        << "the lineup door belongs to team build only";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Scenario, "lineup") ==
                nullptr)
        << "the SDL surface reaches LINEUP from SCENARIO by BUTTON, never by "
           "a terminal menu row";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, "difficulty") ==
                nullptr)
        << "the difficulty door left the main menu";

    const PickerMenuItem* scenario =
        find_picker_menu_item(PickerMenuId::TeamBuild, "scenario");
    ASSERT_TRUE(scenario != nullptr) << "scenario id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::Scenario),
              static_cast<int>(scenario->command))
        << "scenario should map to the Scenario submenu command";

    // §2.5 base camp: the 12-item IN-PLACE substitution — roster (was
    // view_team) leads, deploy (was load_team) and ready (was save_team)
    // hold the 1-based positions 4 and 5, and the retired ids are gone.
    const PickerMenuItem* roster =
        find_picker_menu_item(PickerMenuId::TeamBuild, "roster");
    ASSERT_TRUE(roster != nullptr) << "roster id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::ViewTeam),
              static_cast<int>(roster->command))
        << "roster keeps the ViewTeam command (the roster IS the team view)";
    ASSERT_TRUE(&def.items[0] == roster)
        << "roster leads the base camp (1-based position 1)";

    const PickerMenuItem* deploy =
        find_picker_menu_item(PickerMenuId::TeamBuild, "deploy");
    ASSERT_TRUE(deploy != nullptr) << "deploy id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::ToggleDeploy),
              static_cast<int>(deploy->command))
        << "deploy should map to ToggleDeploy";
    ASSERT_TRUE(&def.items[3] == deploy)
        << "deploy holds load_team's old 1-based position 4";

    const PickerMenuItem* ready =
        find_picker_menu_item(PickerMenuId::TeamBuild, "ready");
    ASSERT_TRUE(ready != nullptr) << "ready id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::ToggleReady),
              static_cast<int>(ready->command))
        << "ready should map to ToggleReady";
    ASSERT_TRUE(&def.items[4] == ready)
        << "ready holds save_team's old 1-based position 5";

    // docs/basecamp-zones-design.md "Terminals": the zone's Camp door sits
    // between GO! and Back — 1-based position 7, the last ordinal inside the
    // curses digit-jump budget that leaves the gameplay rows where they were.
    const PickerMenuItem* camp =
        find_picker_menu_item(PickerMenuId::TeamBuild, "camp");
    ASSERT_TRUE(camp != nullptr) << "camp id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::CampaignCamp),
              static_cast<int>(camp->command))
        << "camp should map to the CampaignCamp command";
    ASSERT_TRUE(&def.items[6] == camp)
        << "camp takes 1-based position 7, before Back";
    ASSERT_TRUE(&def.items[7] == back)
        << "Back shifted one ordinal down for the Camp door";
    ASSERT_TRUE(&def.items[8] == networking)
        << "networking follows Back at 1-based position 9 — the last row a "
           "curses digit can address";

    // The scenario-shaped commands moved OUT of the team-build menu, and
    // the §2.5 substitution retired the view/slot ids entirely.
    for (const char* moved_id :
         {"matchup", "teams", "view_scenario", "set_level", "set_campaign",
          "progress", "view_team", "load_team", "save_team"})
    {
        ASSERT_TRUE(find_picker_menu_item(PickerMenuId::TeamBuild, moved_id) ==
                    nullptr)
            << moved_id << " must not resolve in the base camp";
    }

    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, "matchup") == nullptr)
        << "matchup never lives in the main menu";
}


TEST(MenuModel, scenario_menu_lookup)
{
    using namespace og::ui;
    const PickerMenuDefinition& def =
        picker_menu_definition(PickerMenuId::Scenario);

    ASSERT_EQ(static_cast<int>(PickerMenuId::Scenario), static_cast<int>(def.id))
        << "scenario definition should report scenario id";
    // (Mechanical W5-C port so the tree compiles: TROOPS retired with
    // amendment B5 — PickerMenuCommand::ToggleCtfScenarioTroops is gone
    // and the terminal SCENARIO list lost its row. The terminals' own
    // position re-pins stay with their wave.)
    ASSERT_EQ(7u, def.items.size())
        << "scenario menu: campaign/level/viewer/matchup/progress/"
           "replay (#207) + back (the missions door retired into the Base "
           "Camp zone; TROOPS retired with B5)";

    const struct
    {
        const char* id;
        PickerMenuCommand command;
    } kExpected[] = {
        {"set_campaign", PickerMenuCommand::SetCampaign},
        {"set_level", PickerMenuCommand::SetLevel},
        {"view_scenario", PickerMenuCommand::ViewScenario},
        {"matchup", PickerMenuCommand::Teams},
        {"progress", PickerMenuCommand::ShowProgress},
        {"replay_level", PickerMenuCommand::ReplayLevel},
        {"back", PickerMenuCommand::Back},
    };
    for (const auto& want : kExpected)
    {
        const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Scenario, want.id);
        ASSERT_TRUE(item != nullptr) << want.id << " should resolve in scenario";
        ASSERT_EQ(static_cast<int>(want.command), static_cast<int>(item->command))
            << want.id;
    }

    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Scenario, "missions") ==
                nullptr)
        << "the missions door retired: the book is a room inside the camp";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::TeamBuild, "missions") ==
                nullptr)
        << "the missions door was excised, not moved to the base camp";

    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Scenario, "go") == nullptr)
        << "GO stays on the team-build screen";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Scenario, "ctf_teams") ==
                nullptr)
        << "the CTF settings trio stays in team build for terminal clients";
}


TEST(MenuModel, invalid_menu_id_falls_back_to_main)
{
    using namespace og::ui;
    const PickerMenuId invalid = static_cast<PickerMenuId>(999);
    const PickerMenuDefinition& def = picker_menu_definition(invalid);

    ASSERT_EQ(static_cast<int>(PickerMenuId::Main), static_cast<int>(def.id)) << "unknown menu id should fall back to main definition";
}


TEST(MenuModel, lookup_miss_paths_and_fallback_item)
{
    using namespace og::ui;
    const PickerMenuId invalid = static_cast<PickerMenuId>(-7);

    const PickerMenuItem* begin_from_invalid =
        find_picker_menu_item(invalid, "begin_new_game");
    ASSERT_TRUE(begin_from_invalid != nullptr) << "invalid menu id should fall back to main menu items";

    const PickerMenuItem* command_miss =
        find_picker_menu_item(PickerMenuId::TeamBuild, PickerMenuCommand::SetDifficulty, 0);
    ASSERT_TRUE(command_miss == nullptr) << "nonexistent command lookup should return nullptr";

    const PickerMenuItem* id_miss =
        find_picker_menu_item(PickerMenuId::TeamBuild, "definitely_missing_item");
    ASSERT_TRUE(id_miss == nullptr) << "missing id lookup should return nullptr";
}


TEST(MenuModel, player_mode_is_absent_and_invalid_command_lookup_still_misses)
{
    using namespace og::ui;

    for (int players = 0; players <= 4; ++players) {
        EXPECT_EQ(nullptr, find_picker_menu_item(
            PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, players));
    }

    const PickerMenuItem* invalid_cmd =
        find_picker_menu_item(static_cast<PickerMenuId>(777), PickerMenuCommand::SetCampaign, 0);
    ASSERT_TRUE(invalid_cmd == nullptr) << "invalid menu id plus unmatched command should return nullptr via fallback lookup";
}

TEST(MenuModel, normalize_direct_websocket_url_accepts_plain_endpoints_and_ws_urls)
{
    EXPECT_EQ("ws://192.168.1.5:12345",
              og::ui::normalize_direct_websocket_url("192.168.1.5:12345"));
    EXPECT_EQ("ws://127.0.0.1:12345",
              og::ui::normalize_direct_websocket_url(" 127.0.0.1:12345 "));
    EXPECT_EQ("wss://relay.example/room",
              og::ui::normalize_direct_websocket_url("wss://relay.example/room"));
}

TEST(MenuModel, relay_room_code_and_join_mode_helpers_support_relay_flow)
{
    EXPECT_TRUE(og::ui::picker_join_mode_supported(og::ui::PickerJoinMode::Direct));
    EXPECT_TRUE(og::ui::picker_join_mode_supported(og::ui::PickerJoinMode::Relay));
    EXPECT_EQ("GLAD-XKCD",
              og::ui::normalize_relay_room_code(" glad-xkcd "));
    EXPECT_EQ("https://relay.example",
              og::ui::normalize_relay_base_url("https://relay.example/"));
    EXPECT_FALSE(og::ui::default_relay_base_url().empty());

    const std::vector<og::ui::PickerRelayRoomInfo> rooms = {
        og::ui::PickerRelayRoomInfo{
            .code = "GLAD-XKCD",
            .campaign_hash = "gladiator",
            // Hosts broadcast the human campaign title as display metadata;
            // matching uses campaign_hash of the raw id.
            .campaign_name = "Gladiator",
            .host_name = "Host One",
            .player_count = 2u,
            .created_at_ms = 1000,
        },
        og::ui::PickerRelayRoomInfo{
            .code = "GLAD-ABCD",
            .campaign_hash = "gladiator",
            .campaign_name = "Gladiator",
            .host_name = "",
            .player_count = 1u,
            .created_at_ms = 900,
        },
    };
    const std::string prompt = og::ui::build_relay_room_prompt_message(
        rooms,
        "gladiator");
    EXPECT_NE(std::string::npos, prompt.find("GLAD-XKCD"));
    EXPECT_EQ(std::string::npos, prompt.find("2 players"));
    EXPECT_NE(std::string::npos, prompt.find("Host One"));
    EXPECT_NE(std::string::npos, prompt.find("GLAD-ABCD"));
}

TEST(MenuModel, networking_menu_instructions_match_build_shape)
{
    const auto lines = og::ui::networking_menu_instruction_lines();
#ifdef __EMSCRIPTEN__
    // Web: one line of relay-first guidance under the ACTIVE GAMES list.
    ASSERT_EQ(1u, lines.size());
    EXPECT_NE(std::string_view::npos, lines[0].find("room code"));
    EXPECT_NE(std::string_view::npos, lines[0].find("active game"));
#else
    // Native: the DIRECT (LAN) section header carries the context; no
    // instruction copy is drawn (there is no vertical room for it).
    EXPECT_EQ(0u, lines.size());
#endif
}

TEST(MenuModel, relay_room_button_labels_show_code_and_host)
{
    og::ui::PickerRelayRoomInfo room{
        .code = "GLAD-XKCD",
        .campaign_hash = "gladiator",
        .campaign_name = "Gladiator",
        .host_name = "Host One",
        .player_count = 2u,
        .created_at_ms = 1000,
    };
    EXPECT_EQ("GLAD-XKCD  Host One",
              og::ui::format_relay_room_button_label(room, 39u));

    room.host_name.clear();
    room.player_count = 1u;
    EXPECT_EQ("GLAD-XKCD",
              og::ui::format_relay_room_button_label(room, 39u));

    // Over-budget labels are truncated to the button face's character
    // budget (labels draw centered with no clipping).
    room.host_name = "An Extremely Long Host Name That Overflows";
    const std::string truncated =
        og::ui::format_relay_room_button_label(room, 20u);
    EXPECT_EQ(20u, truncated.size());
    EXPECT_EQ("GLAD-XKCD  An Extrem", truncated);
}

TEST(MenuModel, host_picker_lobby_client_accepts_direct_only_and_relay_options)
{
    og::ui::PickerHostGameOptions direct_only;
    direct_only.port = 12345;
    auto direct_client = og::ui::create_host_picker_lobby_client(direct_only);
    EXPECT_TRUE(direct_client != nullptr);

    og::ui::PickerHostGameOptions with_relay;
    with_relay.port = 23456;
    with_relay.enable_relay = true;
    with_relay.relay_base_url = "https://relay.example";
    auto relay_client = og::ui::create_host_picker_lobby_client(with_relay);
    EXPECT_TRUE(relay_client != nullptr);
}

TEST(MenuModel, host_status_lines_show_lan_only_for_real_direct_transport)
{
    const std::vector<std::string> direct_lines =
        og::ui::build_host_picker_status_lines(
            "192.168.1.5",
            true,
            12345,
            "ignored",
            {},
            {},
            1u);
    ASSERT_EQ(2u, direct_lines.size());
    EXPECT_EQ("LAN: 192.168.1.5:12345", direct_lines[0]);
    EXPECT_EQ("Lobby: 1 player", direct_lines[1]);

    const std::vector<std::string> fallback_lines =
        og::ui::build_host_picker_status_lines(
            "192.168.1.5",
            false,
            12345,
            "Direct hosting is unavailable in browser builds.",
            "GLAD-XKCD",
            {},
            2u);
    ASSERT_EQ(3u, fallback_lines.size());
    EXPECT_EQ("Direct: Direct hosting is unavailable in browser builds.",
              fallback_lines[0]);
    EXPECT_EQ("Room: GLAD-XKCD", fallback_lines[1]);
    EXPECT_EQ("Lobby: 2 players", fallback_lines[2]);
}

TEST(MenuModel, difficulty_menu_definition_and_lookup)
{
    using namespace og::ui;

    // The DIFFICULTY door lives in TEAM BUILD now
    // (docs/camp-controls-design.md): the main menu keeps only the settings
    // that are about the program, not about the company's next fight.
    const PickerMenuDefinition& main_def = picker_menu_definition(PickerMenuId::Main);
    ASSERT_EQ(8u, main_def.items.size())
        << "main menu exposes both Help and Quit plus the company loader "
           "and the cloud door, without the retired player-count, seat-mode "
           "or difficulty rows";

    const PickerMenuItem* door =
        find_picker_menu_item(PickerMenuId::TeamBuild, "difficulty");
    ASSERT_TRUE(door != nullptr)
        << "difficulty id should resolve in team build";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::OpenDifficultyMenu),
              static_cast<int>(door->command))
        << "the team-build difficulty entry opens the submenu";
    ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, "difficulty") ==
                nullptr)
        << "no difficulty door survives on the main menu";
    for (const PickerMenuId menu : {PickerMenuId::Main,
                                    PickerMenuId::TeamBuild}) {
        ASSERT_TRUE(find_picker_menu_item(
                        menu, PickerMenuCommand::SetDifficulty) == nullptr)
            << "the in-place difficulty cycle lives in the submenu only";
    }

    const PickerMenuDefinition& def = picker_menu_definition(PickerMenuId::Difficulty);
    ASSERT_EQ(static_cast<int>(PickerMenuId::Difficulty), static_cast<int>(def.id))
        << "difficulty definition should report difficulty id";
    ASSERT_TRUE(def.title == "Difficulty");
    ASSERT_EQ(7u, def.items.size())
        << "difficulty menu: cycle + the five match rules + back";

    const struct
    {
        const char* id;
        PickerMenuCommand command;
    } kExpected[] = {
        {"difficulty", PickerMenuCommand::SetDifficulty},
        {"respawn_mode", PickerMenuCommand::CycleRespawnMode},
        {"respawn_delay", PickerMenuCommand::CycleRespawnDelay},
        {"permadeath", PickerMenuCommand::TogglePermadeath},
        {"generator_rate", PickerMenuCommand::CycleGeneratorRate},
        {"infinite_gold", PickerMenuCommand::ToggleInfiniteGold},
        {"back", PickerMenuCommand::Back},
    };
    for (const auto& want : kExpected)
    {
        const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::Difficulty, want.id);
        ASSERT_TRUE(item != nullptr) << want.id << " should resolve in difficulty";
        ASSERT_EQ(static_cast<int>(want.command), static_cast<int>(item->command))
            << want.id;
        const PickerMenuItem* by_command =
            find_picker_menu_item(PickerMenuId::Difficulty, want.command);
        ASSERT_TRUE(by_command == item)
            << want.id << " should also resolve by command";
    }

    // The match-rule entries live only in the submenu.
    for (const char* submenu_id :
         {"respawn_mode", "respawn_delay", "permadeath", "generator_rate",
          "infinite_gold"})
    {
        ASSERT_TRUE(find_picker_menu_item(PickerMenuId::Main, submenu_id) == nullptr)
            << submenu_id << " should not appear in the main menu";
        ASSERT_TRUE(find_picker_menu_item(PickerMenuId::TeamBuild, submenu_id) == nullptr)
            << submenu_id << " should not appear in team build";
    }
}

TEST(MenuModel, company_screen_menu_definitions_resolve)
{
    using namespace og::ui;

    // §2.3 Company List (LOAD): dynamic company rows come from disk at
    // runtime; the model carries the fixed command chrome.
    const PickerMenuDefinition& load_def =
        picker_menu_definition(PickerMenuId::LoadCompany);
    ASSERT_EQ(static_cast<int>(PickerMenuId::LoadCompany),
              static_cast<int>(load_def.id))
        << "load-company definition should report its own id";
    ASSERT_TRUE(load_def.title == "Companies");
    ASSERT_EQ(4u, load_def.items.size())
        << "company list chrome: open/backups/delete + back";

    // §2.4 Backups sub-view.
    const PickerMenuDefinition& backups_def =
        picker_menu_definition(PickerMenuId::Backups);
    ASSERT_EQ(static_cast<int>(PickerMenuId::Backups),
              static_cast<int>(backups_def.id))
        << "backups definition should report its own id";
    ASSERT_TRUE(backups_def.title == "Backups");
    ASSERT_EQ(3u, backups_def.items.size())
        << "backups chrome: restore/delete + back";

    // §2.2 new-company name entry.
    const PickerMenuDefinition& name_def =
        picker_menu_definition(PickerMenuId::NameEntry);
    ASSERT_EQ(static_cast<int>(PickerMenuId::NameEntry),
              static_cast<int>(name_def.id))
        << "name-entry definition should report its own id";
    ASSERT_TRUE(name_def.title == "Found Your Company");
    ASSERT_EQ(4u, name_def.items.size())
        << "name entry: value/reroll/accept + back";

    const struct
    {
        PickerMenuId menu;
        const char* id;
        PickerMenuCommand command;
    } kExpected[] = {
        {PickerMenuId::LoadCompany, "open_company", PickerMenuCommand::OpenCompany},
        {PickerMenuId::LoadCompany, "company_backups", PickerMenuCommand::OpenCompanyBackups},
        {PickerMenuId::LoadCompany, "delete_company", PickerMenuCommand::DeleteCompany},
        {PickerMenuId::LoadCompany, "back", PickerMenuCommand::Back},
        {PickerMenuId::Backups, "restore_backup", PickerMenuCommand::RestoreBackup},
        {PickerMenuId::Backups, "delete_backup", PickerMenuCommand::DeleteBackup},
        {PickerMenuId::Backups, "back", PickerMenuCommand::Back},
        {PickerMenuId::NameEntry, "company_name_value", PickerMenuCommand::EditCompanyName},
        {PickerMenuId::NameEntry, "company_name_reroll", PickerMenuCommand::RerollCompanyName},
        {PickerMenuId::NameEntry, "company_name_accept", PickerMenuCommand::AcceptCompanyName},
        {PickerMenuId::NameEntry, "back", PickerMenuCommand::Back},
    };
    for (const auto& want : kExpected)
    {
        const PickerMenuItem* item = find_picker_menu_item(want.menu, want.id);
        ASSERT_TRUE(item != nullptr) << want.id << " should resolve";
        ASSERT_EQ(static_cast<int>(want.command), static_cast<int>(item->command))
            << want.id;
        const PickerMenuItem* by_command =
            find_picker_menu_item(want.menu, want.command);
        ASSERT_TRUE(by_command == item)
            << want.id << " should also resolve by command";
    }
}

TEST(MenuModel, company_screens_cancel_to_back_and_leak_nowhere)
{
    using namespace og::ui;

    // Shared cancel semantics: Back on every non-Main screen.
    for (const PickerMenuId menu :
         {PickerMenuId::LoadCompany, PickerMenuId::Backups, PickerMenuId::NameEntry})
    {
        const PickerMenuItem* cancel = menu_cancel_item(menu);
        ASSERT_TRUE(cancel != nullptr)
            << "company screens must have a cancel item";
        ASSERT_TRUE(cancel->id == "back");
        ASSERT_EQ(static_cast<int>(PickerMenuCommand::Back),
                  static_cast<int>(cancel->command));
    }

    // The new ids never leak into the legacy menus, whose 1-based shapes
    // are pinned by the text-drive consumers.
    for (const char* new_id :
         {"open_company", "company_backups", "delete_company", "restore_backup",
          "delete_backup", "company_name_value", "company_name_reroll",
          "company_name_accept"})
    {
        for (const PickerMenuId legacy_menu :
             {PickerMenuId::Main, PickerMenuId::TeamBuild, PickerMenuId::Scenario,
              PickerMenuId::Difficulty})
        {
            ASSERT_TRUE(find_picker_menu_item(legacy_menu, new_id) == nullptr)
                << new_id << " must not appear in a legacy menu yet";
        }
    }

    // §2.1: load_company remains after the classic items; the #155 cloud
    // door is appended last. Main exposes both stable Help and Quit actions,
    // and lost its difficulty door to Team Build. TeamBuild grew the #206
    // Camp door, that difficulty door and the LINEUP door (§8), and lost the
    // flat CTF trio to the camp's MATCH SETUP page; Scenario grew the
    // #207 replay-level row, gave the missions door back, and lost its
    // troops row (B5); Difficulty grew the appended infinite-gold row.
    ASSERT_EQ(8u, picker_menu_definition(PickerMenuId::Main).items.size());
    ASSERT_EQ(12u, picker_menu_definition(PickerMenuId::TeamBuild).items.size());
    ASSERT_EQ(7u, picker_menu_definition(PickerMenuId::Scenario).items.size());
    ASSERT_EQ(7u, picker_menu_definition(PickerMenuId::Difficulty).items.size());

    // load_company resolves by id and by command and keeps its place in the
    // appended tail (1-based position 7 since difficulty left); the appended
    // cloud door is the last Main item.
    const PickerMenuItem* load_company =
        find_picker_menu_item(PickerMenuId::Main, "load_company");
    ASSERT_TRUE(load_company != nullptr);
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::LoadGame),
              static_cast<int>(load_company->command));
    ASSERT_EQ(load_company,
              &picker_menu_definition(PickerMenuId::Main).items[6]);
    const PickerMenuItem* cloud =
        find_picker_menu_item(PickerMenuId::Main, "cloud");
    ASSERT_TRUE(cloud != nullptr);
    ASSERT_EQ(cloud, &picker_menu_definition(PickerMenuId::Main).items.back());
}

TEST(MenuModel, cloud_save_menu_definition_and_lookup)
{
    using namespace og::ui;

    // #155: the main-menu CLOUD door.
    const PickerMenuItem* door = find_picker_menu_item(PickerMenuId::Main, "cloud");
    ASSERT_TRUE(door != nullptr) << "cloud id should resolve in main";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::OpenCloudMenu),
              static_cast<int>(door->command))
        << "the main cloud entry opens the CloudSave submenu";

    const PickerMenuDefinition& def =
        picker_menu_definition(PickerMenuId::CloudSave);
    ASSERT_EQ(static_cast<int>(PickerMenuId::CloudSave),
              static_cast<int>(def.id))
        << "cloud-save definition should report its own id";
    ASSERT_TRUE(def.title == "Cloud Save");
    ASSERT_EQ(4u, def.items.size())
        << "cloud save: passphrase/upload/download + back";

    const struct
    {
        const char* id;
        PickerMenuCommand command;
    } kExpected[] = {
        {"cloud_passphrase", PickerMenuCommand::CloudSetPassphrase},
        {"cloud_upload", PickerMenuCommand::CloudUpload},
        {"cloud_download", PickerMenuCommand::CloudDownload},
        {"back", PickerMenuCommand::Back},
    };
    for (const auto& want : kExpected)
    {
        const PickerMenuItem* item =
            find_picker_menu_item(PickerMenuId::CloudSave, want.id);
        ASSERT_TRUE(item != nullptr) << want.id << " should resolve";
        ASSERT_EQ(static_cast<int>(want.command),
                  static_cast<int>(item->command)) << want.id;
        const PickerMenuItem* by_command =
            find_picker_menu_item(PickerMenuId::CloudSave, want.command);
        ASSERT_TRUE(by_command == item)
            << want.id << " should also resolve by command";
    }

    // Shared cancel semantics + no leakage into the legacy menus.
    const PickerMenuItem* cancel = menu_cancel_item(PickerMenuId::CloudSave);
    ASSERT_TRUE(cancel != nullptr);
    ASSERT_TRUE(cancel->id == "back");
    for (const char* new_id :
         {"cloud_passphrase", "cloud_upload", "cloud_download"})
    {
        for (const PickerMenuId legacy_menu :
             {PickerMenuId::Main, PickerMenuId::TeamBuild,
              PickerMenuId::Scenario, PickerMenuId::Difficulty})
        {
            ASSERT_TRUE(find_picker_menu_item(legacy_menu, new_id) == nullptr)
                << new_id << " must not appear outside the cloud submenu";
        }
    }
}
