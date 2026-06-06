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
    ASSERT_TRUE(def.items.size() >= 11) << "main menu should expose expected item count";

    const PickerMenuItem* begin = find_picker_menu_item(PickerMenuId::Main, "begin_new_game");
    ASSERT_TRUE(begin != nullptr) << "begin_new_game id should resolve";
    ASSERT_EQ(static_cast<int>(PickerMenuCommand::BeginNewGame), static_cast<int>(begin->command)) << "begin_new_game should map to BeginNewGame command";

    const PickerMenuItem* networking = find_picker_menu_item(PickerMenuId::Main, "networking");
    ASSERT_TRUE(networking == nullptr) << "networking should now live in the team build menu";

    const PickerMenuItem* host = find_picker_menu_item(PickerMenuId::Main, "host_game");
    ASSERT_TRUE(host == nullptr) << "host_game id should no longer appear in the main menu";

    const PickerMenuItem* join = find_picker_menu_item(PickerMenuId::Main, "join_game");
    ASSERT_TRUE(join == nullptr) << "join_game id should no longer appear in the main menu";

    const PickerMenuItem* p4 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 4);
    ASSERT_TRUE(p4 != nullptr) << "set player mode with arg=4 should resolve";
    ASSERT_TRUE(p4->id == "4_player") << "set player mode arg=4 should resolve to 4_player item";

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


TEST(MenuModel, round10_set_player_mode_arg_variants_and_invalid_command_lookup)
{
    using namespace og::ui;

    const PickerMenuItem* p1 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 1);
    const PickerMenuItem* p2 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 2);
    const PickerMenuItem* p3 = find_picker_menu_item(PickerMenuId::Main, PickerMenuCommand::SetPlayerMode, 3);
    ASSERT_TRUE(p1 && p2 && p3) << "set-player-mode items for args 1/2/3 should resolve";

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
            .campaign_hash = "org.openglad.gladiator",
            .campaign_name = "org.openglad.gladiator",
            .host_name = "Host One",
            .player_count = 2u,
            .created_at_ms = 1000,
        },
        og::ui::PickerRelayRoomInfo{
            .code = "GLAD-ABCD",
            .campaign_hash = "org.openglad.gladiator",
            .campaign_name = "org.openglad.gladiator",
            .host_name = "",
            .player_count = 1u,
            .created_at_ms = 900,
        },
    };
    const std::string prompt = og::ui::build_relay_room_prompt_message(
        rooms,
        "org.openglad.gladiator");
    EXPECT_NE(std::string::npos, prompt.find("GLAD-XKCD"));
    EXPECT_NE(std::string::npos, prompt.find("2 players"));
    EXPECT_NE(std::string::npos, prompt.find("Host One"));
    EXPECT_NE(std::string::npos, prompt.find("GLAD-ABCD"));
}

TEST(MenuModel, networking_menu_instructions_explain_fresh_host_flow)
{
    const auto lines = og::ui::networking_menu_instruction_lines();
    ASSERT_EQ(2u, lines.size());
    EXPECT_NE(std::string_view::npos, lines[0].find("HOST or JOIN"));
    EXPECT_NE(std::string_view::npos, lines[0].find("team setup"));
    EXPECT_NE(std::string_view::npos, lines[1].find("BEGIN NEW GAME"));
    EXPECT_NE(std::string_view::npos, lines[1].find("CONTINUE GAME"));
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
    EXPECT_EQ("Relay: GLAD-XKCD", fallback_lines[1]);
    EXPECT_EQ("Lobby: 2 players", fallback_lines[2]);
}
