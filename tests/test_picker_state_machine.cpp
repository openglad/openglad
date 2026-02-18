#include <openglad/ui/picker_state.h>
#include "test_framework.h"

#include <string>
#include <vector>

namespace {

class ScriptedPickerClient final : public og::ui::IPickerClient
{
public:
    std::vector<og::ui::MainMenuAction> main_menu_actions;
    size_t main_menu_index = 0;

    bool prepare_new_game_result = true;
    std::string campaign_result = "org.openglad.gladiator";
    og::ui::TeamBuildAction team_build_result = og::ui::TeamBuildAction::BackToMainMenu;
    bool load_result = true;
    bool save_result = true;
    og::ui::PickerScreen screen_after_game_result = og::ui::PickerScreen::MainMenu;

    int show_main_menu_calls = 0;
    int prepare_new_game_calls = 0;
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

void test_picker_state_campaign_cancel_returns_to_main_menu()
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::NewGame,
        og::ui::MainMenuAction::Quit
    };
    client.campaign_result.clear();

    og::ui::run_picker(client);

    TEST_ASSERT_EQ(2, client.show_main_menu_calls, "cancelled campaign select should return to main menu");
    TEST_ASSERT_EQ(1, client.show_campaign_select_calls, "campaign select should run once");
    TEST_ASSERT_EQ(0, client.show_team_build_calls, "campaign cancel should not enter team build");
}
REGISTER_TEST(test_picker_state_campaign_cancel_returns_to_main_menu);

void test_picker_state_load_fail_routes_to_team_build()
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::LoadGame,
        og::ui::MainMenuAction::Quit
    };
    client.load_result = false;

    og::ui::run_picker(client);

    TEST_ASSERT_EQ(1, client.load_game_calls, "load should be attempted once");
    TEST_ASSERT_EQ(1, client.show_team_build_calls, "failed load should route to team build");
    TEST_ASSERT_EQ(2, client.show_main_menu_calls, "flow should return to main menu after team build");
}
REGISTER_TEST(test_picker_state_load_fail_routes_to_team_build);

void test_picker_state_save_fail_routes_to_team_build()
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::SaveGame,
        og::ui::MainMenuAction::Quit
    };
    client.save_result = false;

    og::ui::run_picker(client);

    TEST_ASSERT_EQ(1, client.save_game_calls, "save should be attempted once");
    TEST_ASSERT_EQ(1, client.show_team_build_calls, "failed save should route to team build");
    TEST_ASSERT_EQ(2, client.show_main_menu_calls, "flow should return to main menu after team build");
}
REGISTER_TEST(test_picker_state_save_fail_routes_to_team_build);

void test_picker_state_screen_after_game_routes_through_help()
{
    ScriptedPickerClient client;
    client.main_menu_actions = {
        og::ui::MainMenuAction::ContinueGame,
        og::ui::MainMenuAction::Quit
    };
    client.screen_after_game_result = og::ui::PickerScreen::Help;

    og::ui::run_picker(client);

    TEST_ASSERT_EQ(1, client.run_game_calls, "continue game should run one game");
    TEST_ASSERT_EQ(1, client.show_help_calls, "screen_after_game should route to help");
    TEST_ASSERT_EQ(2, client.show_main_menu_calls, "help should return back to main menu");
}
REGISTER_TEST(test_picker_state_screen_after_game_routes_through_help);
