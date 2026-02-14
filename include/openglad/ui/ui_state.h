#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace og::ui {

// Commands produced by UI controllers. Runtime/renderer interprets them.
enum class Command : std::uint32_t {
    None = 0,
    StartGame,        // Begin gameplay with current team/level settings
    QuitApp,          // Exit the application
    NavigateToMenu,   // Navigate to a submenu
    NavigateBack,     // Return to parent menu
    SaveTeam,         // Save current team
    LoadTeam,         // Load a team
    HireUnit,         // Hire a unit to the team
    DismissUnit,      // Remove a unit from the team
    TrainUnit,        // Open training for a unit
    ChangeLevel,      // Change the selected level/scenario
    ChangeCampaign,   // Change the selected campaign
    SetDifficulty,    // Change difficulty setting
    SetPlayerCount,   // Change number of players
    OpenOptions,      // Open options menu
    OpenHelp,         // Open help screen
    OpenLevelEditor,  // Open the level editor
};

// A button in the view model, decoupled from rendering.
struct ButtonViewModel {
    std::string id;
    std::string label;
    bool enabled = true;
    bool visible = true;
    bool focused = false;
};

// Abstract view model that a UI controller produces.
// The renderer consumes this to draw the UI without needing
// to know about UI logic or state transitions.
struct MenuViewModel {
    std::string title;
    std::vector<ButtonViewModel> buttons;
    std::string status_text;
    int focused_button = -1;
};

// Menu states for the picker state machine.
enum class PickerState : std::uint32_t {
    MainMenu = 0,
    TeamMenu,
    HireMenu,
    TrainMenu,
    ViewMenu,
    DetailMenu,
    LoadMenu,
    SaveMenu,
    ProgressMenu,
    CampaignPicker,
    LevelPicker,
    OptionsMenu,
    HelpScreen,
    LevelEditor,
    Playing,        // Handed off to game loop
    Quitting,       // Shutting down
};

} // namespace og::ui

// Centralized picker resource cleanup (does not destroy the screen).
// Call this instead of duplicating cleanup logic in tests.
void picker_cleanup_resources();
