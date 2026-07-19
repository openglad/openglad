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
#include <openglad/interface/ui/menu_model.h>

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
    Networking,         // Open network configuration submenu
    HostGame,           // Host a networked lobby
    JoinGame,           // Join a networked lobby
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

enum class TeamBuildAction : std::int32_t
{
    BackToMainMenu,
    PlayGame,
};

// Abstract interface that a picker client implements.
// The platform (SDL/text) provides concrete implementations.
class IPickerClient
{
public:
    virtual ~IPickerClient() = default;

    // Display the main menu and block until the user makes a choice.
    virtual MainMenuAction show_main_menu();

    // Prepare local save/team state for starting a new game.
    // Return false to cancel and go back to the main menu.
    virtual bool prepare_new_game() { return true; }

    // Open the networking submenu and return true if it produced a lobby.
    virtual bool configure_networking() { return false; }

    // Prepare a hosted network lobby from the current save.
    virtual bool host_game() { return false; }

    // Join a network lobby from the current save.
    virtual bool join_game() { return false; }

    // Display team building menu.
    virtual TeamBuildAction show_team_build();

    // Display a shared submenu (SCENARIO: campaign/level/viewer/teams/
    // progress; DIFFICULTY: session difficulty + match rules) as a nested
    // presentation loop until Back. Terminal clients reach these from their
    // team-build/main items; the SDL client nests the equivalent blocking
    // subscreens inside its own screens instead (its present_menu answers
    // the Scenario id with a safe no-op Back).
    virtual void show_submenu(PickerMenuId menu_id);

    // Render a shared picker menu and return which shared item was selected.
    // Returning nullptr means "cancel/default back".
    virtual const PickerMenuItem* present_menu(PickerMenuId menu_id) { (void)menu_id; return nullptr; }

    // Handle menu commands that do not leave the current screen.
    virtual void handle_menu_item(PickerMenuId menu_id, const PickerMenuItem& item)
    {
        (void)menu_id;
        (void)item;
    }

    // Allow platform clients to pump asynchronous lobby/runtime updates while
    // the shared state machine is between blocking menu screens.
    virtual void poll_updates() {}

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

    // Decide where the state machine should return after a game run.
    virtual PickerScreen screen_after_game() const { return PickerScreen::MainMenu; }

protected:
    IPickerClient() = default;
};

// Run the picker state machine with the given client implementation.
// This is the platform-agnostic main loop.
// Startup begins at MainMenu; browser gameplay re-entry can resume directly at
// TeamBuild, matching the native blocking game loop's post-level destination.
void run_picker(
    IPickerClient& client,
    PickerScreen initial_screen = PickerScreen::MainMenu);

} // namespace og::ui
