#include <openglad/interface/ui/picker_state.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

class ScriptedPickerClient final : public og::ui::IPickerClient
{
public:
    std::vector<og::ui::MainMenuAction> main_menu_actions;
    size_t main_menu_index = 0;

    bool prepare_new_game_result = true;
    bool configure_networking_result = true;
    bool host_game_result = true;
    bool join_game_result = true;
    std::string campaign_result = "org.openglad.gladiator";
    og::ui::TeamBuildAction team_build_result = og::ui::TeamBuildAction::BackToMainMenu;
    bool load_result = true;
    bool save_result = true;
    og::ui::PickerScreen screen_after_game_result = og::ui::PickerScreen::MainMenu;

    int show_main_menu_calls = 0;
    int prepare_new_game_calls = 0;
    int configure_networking_calls = 0;
    int host_game_calls = 0;
    int join_game_calls = 0;
    int show_campaign_select_calls = 0;
    int show_team_build_calls = 0;
    int load_game_calls = 0;
    int save_game_calls = 0;
    int run_game_calls = 0;
    int show_help_calls = 0;
    int show_options_calls = 0;

    og::ui::MainMenuAction show_main_menu() override
    {
        ++show_main_menu_calls;
        if (main_menu_index < main_menu_actions.size())
            return main_menu_actions[main_menu_index++];
        return og::ui::MainMenuAction::Quit;
    }

    bool prepare_new_game() override
    {
        ++prepare_new_game_calls;
        return prepare_new_game_result;
    }

    bool configure_networking() override
    {
        ++configure_networking_calls;
        return configure_networking_result;
    }

    bool host_game() override
    {
        ++host_game_calls;
        return host_game_result;
    }

    bool join_game() override
    {
        ++join_game_calls;
        return join_game_result;
    }

    og::ui::TeamBuildAction show_team_build() override
    {
        ++show_team_build_calls;
        return team_build_result;
    }

    std::string show_campaign_select() override
    {
        ++show_campaign_select_calls;
        return campaign_result;
    }

    void show_options() override
    {
        ++show_options_calls;
    }

    void show_help() override
    {
        ++show_help_calls;
    }

    void run_game() override
    {
        ++run_game_calls;
    }

    bool load_game() override
    {
        ++load_game_calls;
        return load_result;
    }

    bool save_game() override
    {
        ++save_game_calls;
        return save_result;
    }

    og::ui::PickerScreen screen_after_game() const override
    {
        return screen_after_game_result;
    }
};

} // namespace

TEST(PickerStateMachine, picker_state_campaign_cancel_returns_to_main_menu)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::Quit
    };
    client.campaign_result.clear();

    og::ui::run_picker(client);

    ASSERT_EQ(2, client.show_main_menu_calls) << "cancelled campaign select should return to main menu";
    ASSERT_EQ(1, client.show_campaign_select_calls) << "campaign select should run once";
    ASSERT_EQ(0, client.show_team_build_calls) << "campaign cancel should not enter team build";
}


TEST(PickerStateMachine, picker_state_load_fail_routes_to_team_build)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::Quit
    };
    client.load_result = false;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.load_game_calls) << "load should be attempted once";
    ASSERT_EQ(1, client.show_team_build_calls) << "failed load should route to team build";
    ASSERT_EQ(2, client.show_main_menu_calls) << "flow should return to main menu after team build";
}


TEST(PickerStateMachine, picker_state_save_fail_routes_to_team_build)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::Quit
    };
    client.save_result = false;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.save_game_calls) << "save should be attempted once";
    ASSERT_EQ(1, client.show_team_build_calls) << "failed save should route to team build";
    ASSERT_EQ(2, client.show_main_menu_calls) << "flow should return to main menu after team build";
}

TEST(PickerStateMachine, picker_state_networking_success_routes_to_team_build)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::Networking,
        og::ui::MainMenuAction::Quit
    };

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.configure_networking_calls) << "networking submenu should be attempted once";
    ASSERT_EQ(1, client.show_team_build_calls) << "successful networking flow should open team build";
    ASSERT_EQ(2, client.show_main_menu_calls) << "flow should return to main menu after team build";
}

TEST(PickerStateMachine, picker_state_networking_cancel_returns_to_main_menu)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::Networking,
        og::ui::MainMenuAction::Quit
    };
    client.configure_networking_result = false;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.configure_networking_calls) << "networking submenu should be attempted once";
    ASSERT_EQ(0, client.show_team_build_calls) << "cancelled networking should stay on the main menu";
    ASSERT_EQ(2, client.show_main_menu_calls) << "flow should return to the main menu and then quit";
}

TEST(PickerStateMachine, picker_state_join_game_cancel_returns_to_main_menu)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::JoinGame,
        og::ui::MainMenuAction::Quit
    };
    client.join_game_result = false;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.join_game_calls) << "join game should be attempted once";
    ASSERT_EQ(0, client.show_team_build_calls) << "failed join should stay on main menu";
    ASSERT_EQ(2, client.show_main_menu_calls) << "state machine should continue at main menu";
}


TEST(PickerStateMachine, picker_state_screen_after_game_routes_through_help)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::ContinueGame,
        og::ui::MainMenuAction::Quit
    };
    client.screen_after_game_result = og::ui::PickerScreen::Help;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.run_game_calls) << "continue game should run one game";
    ASSERT_EQ(1, client.show_help_calls) << "screen_after_game should route to help";
    ASSERT_EQ(2, client.show_main_menu_calls) << "help should return back to main menu";
}


class MenuOnlyPickerClient final : public og::ui::IPickerClient
{
public:
    std::vector<const og::ui::PickerMenuItem*> scripted_results;
    int present_calls = 0;
    int handle_calls = 0;

    const og::ui::PickerMenuItem* present_menu(og::ui::PickerMenuId) override
    {
        if (present_calls >= static_cast<int>(scripted_results.size()))
            return nullptr;
        return scripted_results[static_cast<size_t>(present_calls++)];
    }

    void handle_menu_item(og::ui::PickerMenuId, const og::ui::PickerMenuItem&) override
    {
        ++handle_calls;
    }

    std::string show_campaign_select() override { return {}; }
    void show_options() override {}
    void show_help() override {}
    void run_game() override {}
    bool load_game() override { return false; }
    bool save_game() override { return false; }
};

TEST(PickerStateMachine, picker_state_show_main_menu_handles_unknown_then_quit)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem unknown{
        "noop", "noop", og::ui::PickerMenuCommand::SetDifficulty, 0
    };
    client.scripted_results = {&unknown, nullptr};

    const og::ui::MainMenuAction action = client.show_main_menu();
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Quit), static_cast<int>(action)) << "null menu selection should map to Quit";
    ASSERT_EQ(1, client.handle_calls) << "unknown command should be handled and looped";
}


TEST(PickerStateMachine, picker_state_show_team_build_play_and_back)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem go{
        "go", "GO!", og::ui::PickerMenuCommand::StartGame, 0
    };
    static const og::ui::PickerMenuItem back{
        "back", "Back", og::ui::PickerMenuCommand::Back, 0
    };

    client.scripted_results = {&go};
    ASSERT_EQ(static_cast<int>(og::ui::TeamBuildAction::PlayGame), static_cast<int>(client.show_team_build())) << "start game command should return PlayGame";

    client.present_calls = 0;
    client.scripted_results = {&back};
    ASSERT_EQ(static_cast<int>(og::ui::TeamBuildAction::BackToMainMenu), static_cast<int>(client.show_team_build())) << "back command should return BackToMainMenu";
}

TEST(PickerStateMachine, picker_state_show_main_menu_maps_host_and_join_commands)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem networking{
        "networking", "Networking", og::ui::PickerMenuCommand::Networking, 0
    };
    static const og::ui::PickerMenuItem host{
        "host_game", "Host Game", og::ui::PickerMenuCommand::HostGame, 0
    };
    static const og::ui::PickerMenuItem join{
        "join_game", "Join Game", og::ui::PickerMenuCommand::JoinGame, 0
    };

    client.scripted_results = {&networking};
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Networking),
              static_cast<int>(client.show_main_menu()))
        << "networking command should map to Networking";

    client.present_calls = 0;
    client.scripted_results = {&host};
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::HostGame),
              static_cast<int>(client.show_main_menu()))
        << "host command should map to HostGame";

    client.present_calls = 0;
    client.scripted_results = {&join};
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::JoinGame),
              static_cast<int>(client.show_main_menu()))
        << "join command should map to JoinGame";
}


TEST(PickerStateMachine, picker_state_new_game_cancel_stays_in_main_menu)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::Quit
    };
    client.prepare_new_game_result = false;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.prepare_new_game_calls) << "new game should invoke preparation";
    ASSERT_EQ(0, client.show_campaign_select_calls) << "failed prepare should skip campaign select";
    ASSERT_EQ(2, client.show_main_menu_calls) << "flow should return to main menu and then quit";
}


TEST(PickerStateMachine, picker_state_multiplayer_noop_returns_to_menu)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::Multiplayer,
        og::ui::MainMenuAction::Quit
    };

    og::ui::run_picker(client);

    ASSERT_EQ(2, client.show_main_menu_calls) << "multiplayer action should keep picker in main menu";
    ASSERT_EQ(0, client.run_game_calls) << "multiplayer action should not run game";
}


TEST(PickerStateMachine, picker_state_show_main_menu_command_mappings)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem begin_new_game{
        "begin_new_game", "Begin New Game", og::ui::PickerMenuCommand::BeginNewGame, 0
    };
    static const og::ui::PickerMenuItem continue_game{
        "continue_game", "Continue Game", og::ui::PickerMenuCommand::ContinueGame, 0
    };
    static const og::ui::PickerMenuItem options{
        "options", "Options", og::ui::PickerMenuCommand::Options, 0
    };
    static const og::ui::PickerMenuItem help{
        "help", "Help", og::ui::PickerMenuCommand::Help, 0
    };
    static const og::ui::PickerMenuItem quit{
        "quit", "Quit", og::ui::PickerMenuCommand::Quit, 0
    };

    client.scripted_results = {&begin_new_game};
    client.present_calls = 0;
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::NewGame), static_cast<int>(client.show_main_menu())) << "BeginNewGame command should map to NewGame action";

    client.scripted_results = {&continue_game};
    client.present_calls = 0;
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::ViewTeam), static_cast<int>(client.show_main_menu())) << "ContinueGame command should map to ViewTeam action";

    client.scripted_results = {&options};
    client.present_calls = 0;
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Options), static_cast<int>(client.show_main_menu())) << "Options command should map to Options action";

    client.scripted_results = {&help};
    client.present_calls = 0;
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Help), static_cast<int>(client.show_main_menu())) << "Help command should map to Help action";

    client.scripted_results = {&quit};
    client.present_calls = 0;
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Quit), static_cast<int>(client.show_main_menu())) << "Quit command should map to Quit action";
}


TEST(PickerStateMachine, picker_state_show_team_build_unknown_and_null_paths)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem unknown{
        "noop", "noop", og::ui::PickerMenuCommand::SetCampaign, 0
    };

    client.scripted_results = {&unknown, nullptr};
    const og::ui::TeamBuildAction action = client.show_team_build();
    ASSERT_EQ(static_cast<int>(og::ui::TeamBuildAction::BackToMainMenu), static_cast<int>(action)) << "unknown team-build command then null should return BackToMainMenu";
    ASSERT_EQ(1, client.handle_calls) << "unknown team-build command should call handle_menu_item";
}


TEST(PickerStateMachine, picker_state_load_save_success_and_options_flow)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::Options,
        og::ui::MainMenuAction::Help,
        og::ui::MainMenuAction::Quit
    };
    client.load_result = true;
    client.save_result = true;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.load_game_calls) << "successful load should run once";
    ASSERT_EQ(1, client.save_game_calls) << "successful save should run once";
    ASSERT_EQ(1, client.show_options_calls) << "options action should route to show_options";
    ASSERT_EQ(1, client.show_help_calls) << "help action should route to show_help";
    ASSERT_EQ(0, client.show_team_build_calls) << "successful load/save should not route to team build";
}


TEST(PickerStateMachine, picker_state_invalid_screen_after_game_falls_back_to_quit)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::ContinueGame
    };
    client.screen_after_game_result = static_cast<og::ui::PickerScreen>(99);

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.run_game_calls) << "continue game should run exactly once";
    ASSERT_EQ(1, client.show_main_menu_calls) << "invalid next screen should exit without re-entering main menu";
}


TEST(PickerStateMachine, picker_state_batch7_hire_and_train_actions_route_to_team_build)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::HireTeam,
        og::ui::MainMenuAction::TrainTeam,
        og::ui::MainMenuAction::Quit
    };
    client.team_build_result = og::ui::TeamBuildAction::BackToMainMenu;

    og::ui::run_picker(client);

    ASSERT_EQ(2, client.show_team_build_calls) << "hire/train actions should both route through team build";
    ASSERT_EQ(3, client.show_main_menu_calls) << "flow should return to main menu between team-build actions";
}


TEST(PickerStateMachine, picker_state_batch7_team_build_play_game_path)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::ViewTeam,
        og::ui::MainMenuAction::Quit
    };
    client.team_build_result = og::ui::TeamBuildAction::PlayGame;
    client.screen_after_game_result = og::ui::PickerScreen::MainMenu;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.show_team_build_calls) << "view team should enter team build once";
    ASSERT_EQ(1, client.run_game_calls) << "PlayGame from team build should enter playing state";
}


TEST(PickerStateMachine, picker_state_show_main_menu_null_immediate_maps_to_quit)
{
    MenuOnlyPickerClient client;
    client.scripted_results = {nullptr};
    const og::ui::MainMenuAction action = client.show_main_menu();
    ASSERT_EQ(static_cast<int>(og::ui::MainMenuAction::Quit), static_cast<int>(action)) << "null selection should map directly to Quit";
    ASSERT_EQ(0, client.handle_calls) << "null immediate path should not call handle_menu_item";
}


TEST(PickerStateMachine, picker_state_show_team_build_unknown_then_start_game)
{
    MenuOnlyPickerClient client;
    static const og::ui::PickerMenuItem unknown{
        "noop", "noop", og::ui::PickerMenuCommand::SetDifficulty, 0
    };
    static const og::ui::PickerMenuItem go{
        "go", "GO!", og::ui::PickerMenuCommand::StartGame, 0
    };

    client.scripted_results = {&unknown, &go};
    const og::ui::TeamBuildAction action = client.show_team_build();
    ASSERT_EQ(static_cast<int>(og::ui::TeamBuildAction::PlayGame), static_cast<int>(action)) << "unknown command should be handled and loop until StartGame";
    ASSERT_EQ(1, client.handle_calls) << "unknown team-build command should call handle_menu_item once";
}


TEST(PickerStateMachine, picker_state_round10_continue_game_to_quit_transition)
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::ContinueGame
    };
    client.screen_after_game_result = og::ui::PickerScreen::Quit;

    og::ui::run_picker(client);

    ASSERT_EQ(1, client.run_game_calls) << "continue game should execute one game run";
    ASSERT_EQ(1, client.show_main_menu_calls) << "screen_after_game=Quit should exit picker without re-entering main menu";
}
