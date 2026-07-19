/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/interface/ui/picker_state.h>

namespace og::ui {

MainMenuAction IPickerClient::show_main_menu()
{
    for (;;) {
        const PickerMenuItem* item = present_menu(PickerMenuId::Main);
        if (!item)
            return MainMenuAction::Quit;

        switch (item->command) {
        case PickerMenuCommand::BeginNewGame:
            return MainMenuAction::NewGame;
        case PickerMenuCommand::ContinueGame:
            return MainMenuAction::ViewTeam;
        case PickerMenuCommand::Networking:
            return MainMenuAction::Networking;
        case PickerMenuCommand::HostGame:
            return MainMenuAction::HostGame;
        case PickerMenuCommand::JoinGame:
            return MainMenuAction::JoinGame;
        case PickerMenuCommand::Options:
            return MainMenuAction::Options;
        case PickerMenuCommand::Help:
            return MainMenuAction::Help;
        case PickerMenuCommand::Quit:
            return MainMenuAction::Quit;
        default:
            handle_menu_item(PickerMenuId::Main, *item);
            break;
        }
    }
}

TeamBuildAction IPickerClient::show_team_build()
{
    for (;;) {
        const PickerMenuItem* item = present_menu(PickerMenuId::TeamBuild);
        if (!item)
            return TeamBuildAction::BackToMainMenu;

        switch (item->command) {
        case PickerMenuCommand::StartGame:
            return TeamBuildAction::PlayGame;
        case PickerMenuCommand::Back:
            return TeamBuildAction::BackToMainMenu;
        case PickerMenuCommand::Networking:
            (void)configure_networking();
            break;
        case PickerMenuCommand::Scenario:
            show_scenario_menu();
            break;
        default:
            handle_menu_item(PickerMenuId::TeamBuild, *item);
            break;
        }
    }
}

void IPickerClient::show_scenario_menu()
{
    for (;;) {
        const PickerMenuItem* item = present_menu(PickerMenuId::Scenario);
        if (!item || item->command == PickerMenuCommand::Back)
            return;
        handle_menu_item(PickerMenuId::Scenario, *item);
    }
}

void run_picker(IPickerClient& client, PickerScreen initial_screen)
{
    PickerScreen screen = initial_screen;

    while (screen != PickerScreen::Quit) {
        client.poll_updates();
        PickerTransition transition{screen, false};
        switch (screen) {
        case PickerScreen::MainMenu: {
            const MainMenuAction action = client.show_main_menu();
            switch (action) {
            case MainMenuAction::ContinueGame:
                transition.next_screen = PickerScreen::Playing;
                break;
            case MainMenuAction::NewGame:
                transition.next_screen = client.prepare_new_game()
                    ? PickerScreen::CampaignSelect
                    : PickerScreen::MainMenu;
                break;
            case MainMenuAction::Networking:
                transition.next_screen = client.configure_networking()
                    ? PickerScreen::TeamBuild
                    : PickerScreen::MainMenu;
                break;
            case MainMenuAction::HostGame:
                transition.next_screen = client.host_game()
                    ? PickerScreen::TeamBuild
                    : PickerScreen::MainMenu;
                break;
            case MainMenuAction::JoinGame:
                transition.next_screen = client.join_game()
                    ? PickerScreen::TeamBuild
                    : PickerScreen::MainMenu;
                break;
            case MainMenuAction::LoadGame:
                transition.next_screen = client.load_game()
                    ? PickerScreen::MainMenu
                    : PickerScreen::TeamBuild;
                break;
            case MainMenuAction::SaveGame:
                transition.next_screen = client.save_game()
                    ? PickerScreen::MainMenu
                    : PickerScreen::TeamBuild;
                break;
            case MainMenuAction::ViewTeam:
            case MainMenuAction::HireTeam:
            case MainMenuAction::TrainTeam:
                transition.next_screen = PickerScreen::TeamBuild;
                break;
            case MainMenuAction::Options:
                transition.next_screen = PickerScreen::Options;
                break;
            case MainMenuAction::Help:
                transition.next_screen = PickerScreen::Help;
                break;
            case MainMenuAction::Multiplayer:
                transition.next_screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::Quit:
                transition.next_screen = PickerScreen::Quit;
                break;
            }
            break;
        }
        case PickerScreen::TeamBuild:
            transition.next_screen =
                (client.show_team_build() == TeamBuildAction::PlayGame)
                    ? PickerScreen::Playing
                    : PickerScreen::MainMenu;
            break;
        case PickerScreen::CampaignSelect:
            transition.next_screen = client.show_campaign_select().empty()
                ? PickerScreen::MainMenu
                : PickerScreen::TeamBuild;
            break;
        case PickerScreen::Options:
            client.show_options();
            transition.next_screen = PickerScreen::MainMenu;
            break;
        case PickerScreen::Help:
            client.show_help();
            transition.next_screen = PickerScreen::MainMenu;
            break;
        case PickerScreen::Playing:
            client.run_game();
            transition.next_screen = client.screen_after_game();
            break;
        default:
            transition.next_screen = PickerScreen::Quit;
            break;
        }
        screen = transition.next_screen;
    }
}

} // namespace og::ui
