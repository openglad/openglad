/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/ui/picker_state.h>

namespace og::ui {

void run_picker(IPickerClient& client)
{
    PickerScreen screen = PickerScreen::MainMenu;

    while (screen != PickerScreen::Quit) {
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
