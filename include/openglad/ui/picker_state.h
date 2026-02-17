/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Platform-agnostic picker menu state machine.
// Describes the flow of the team/game picker independently of the
// rendering backend (SDL vs text vs web).

namespace og::ui {

// High-level picker states. Each corresponds to a screen/menu the user sees.
enum class PickerScreen : std::int32_t
{
    MainMenu,           // Top-level menu (Continue, New Game, Load, etc.)
    TeamBuild,          // Hire / train / view team
    CampaignSelect,     // Choose campaign
    LevelSelect,        // Choose starting level
    Options,            // Options/settings
    Help,               // Help screen
    Playing,            // Game is running (picker hands off control)
    Quit,               // Exit the application
};

// Actions the user can take from the main menu.
enum class MainMenuAction : std::int32_t
{
    ContinueGame,       // Resume with current team
    NewGame,            // Start fresh
    LoadGame,           // Load a save file
    SaveGame,           // Save current team
    ViewTeam,           // View team roster
    HireTeam,           // Hire new team members
    TrainTeam,          // Train existing team
    Options,            // Open options menu
    Help,               // Open help
    Multiplayer,        // Change player count
    Quit,               // Exit
};

// Result of a single picker frame/interaction.
struct PickerTransition
{
    PickerScreen next_screen;
    bool redraw = false;   // Hint to re-render current screen
};

// Abstract interface that a picker client implements.
// The platform (SDL/text) provides concrete implementations.
class IPickerClient
{
public:
    virtual ~IPickerClient() = default;

    // Display the main menu and block until the user makes a choice.
    virtual MainMenuAction show_main_menu() = 0;

    // Display team building menu. Returns when user exits back to main menu.
    virtual void show_team_build() = 0;

    // Display campaign selection. Returns selected campaign ID or empty on cancel.
    virtual std::string show_campaign_select() = 0;

    // Display options menu. Returns when user exits back to main menu.
    virtual void show_options() = 0;

    // Show help. Returns when user exits back.
    virtual void show_help() = 0;

    // Run the game for the current team/level. Returns when game ends.
    virtual void run_game() = 0;

    // Load a saved game. Returns true if load succeeded.
    virtual bool load_game() = 0;

    // Save the current game. Returns true if save succeeded.
    virtual bool save_game() = 0;

protected:
    IPickerClient() = default;
};

// Run the picker state machine with the given client implementation.
// This is the platform-agnostic main loop.
inline void run_picker(IPickerClient& client)
{
    PickerScreen screen = PickerScreen::MainMenu;

    while (screen != PickerScreen::Quit) {
        switch (screen) {
        case PickerScreen::MainMenu: {
            MainMenuAction action = client.show_main_menu();
            switch (action) {
            case MainMenuAction::ContinueGame:
                client.run_game();
                screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::NewGame:
                screen = PickerScreen::TeamBuild;
                break;
            case MainMenuAction::LoadGame:
                if (client.load_game())
                    screen = PickerScreen::MainMenu; // Redraw with loaded data
                else
                    screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::SaveGame:
                client.save_game();
                screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::ViewTeam:
            case MainMenuAction::HireTeam:
            case MainMenuAction::TrainTeam:
                client.show_team_build();
                screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::Options:
                client.show_options();
                screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::Help:
                client.show_help();
                screen = PickerScreen::MainMenu;
                break;
            case MainMenuAction::Multiplayer:
                screen = PickerScreen::MainMenu; // Handled in show_main_menu
                break;
            case MainMenuAction::Quit:
                screen = PickerScreen::Quit;
                break;
            }
            break;
        }
        case PickerScreen::TeamBuild:
            client.show_team_build();
            screen = PickerScreen::MainMenu;
            break;
        case PickerScreen::Options:
            client.show_options();
            screen = PickerScreen::MainMenu;
            break;
        case PickerScreen::Help:
            client.show_help();
            screen = PickerScreen::MainMenu;
            break;
        case PickerScreen::Playing:
            client.run_game();
            screen = PickerScreen::MainMenu;
            break;
        default:
            screen = PickerScreen::Quit;
            break;
        }
    }
}

} // namespace og::ui
