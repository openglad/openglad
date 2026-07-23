/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/interface/ui/menu_model.h>

#include <array>

namespace og::ui {
namespace {

constexpr std::array<PickerMenuItem, 13> kMainMenuItems = {{
    {"begin_new_game", "Begin New Game", PickerMenuCommand::BeginNewGame},
    {"continue_game", "Continue Game", PickerMenuCommand::ContinueGame},
    {"4_player", "4 Player", PickerMenuCommand::SetPlayerMode, 4},
    {"3_player", "3 Player", PickerMenuCommand::SetPlayerMode, 3},
    {"2_player", "2 Player", PickerMenuCommand::SetPlayerMode, 2},
    {"1_player", "1 Player", PickerMenuCommand::SetPlayerMode, 1},
    // The DIFFICULTY entry is a door into the DIFFICULTY submenu
    // (kDifficultyMenuItems below); the in-place cycle moved in there.
    {"difficulty", "Difficulty", PickerMenuCommand::OpenDifficultyMenu},
    {"pvp_allied", "Seat Mode", PickerMenuCommand::ToggleAlliedMode},
    {"level_edit", "Level Editor", PickerMenuCommand::LevelEdit},
    {"options", "Game Settings", PickerMenuCommand::Options},
    // HELP and QUIT are separate, stable footer actions on the graphical
    // menu. Text clients expose both too; the browser's disabled QUIT face
    // is an SDL presentation concern.
    {"help", "Help", PickerMenuCommand::Help},
    {"quit", "Quit", PickerMenuCommand::Quit},
    // §2.1: the LOAD half of the CONTINUE|LOAD split remains at the end of
    // the terminal model; the SDL surface positions it beside CONTINUE.
    {"load_company", "Load Company", PickerMenuCommand::LoadGame},
}};

// §2.5 base camp: the TeamBuild model stays 12 items by IN-PLACE substitution
// (minimizes 1-based churn for the text/curses index contract): 1 roster (was
// view_team), 4 deploy (was load_team), 5 ready (was save_team). Save/Load
// left the base camp entirely — saving is automatic (§3.8).
constexpr std::array<PickerMenuItem, 12> kTeamBuildItems = {{
    {"roster", "Roster", PickerMenuCommand::ViewTeam},
    {"train_team", "Train Team", PickerMenuCommand::TrainTeam},
    {"hire_troops", "Hire Troops", PickerMenuCommand::HireTroops},
    {"deploy", "Deploy", PickerMenuCommand::ToggleDeploy},
    {"ready", "Ready", PickerMenuCommand::ToggleReady},
    {"go", "GO!", PickerMenuCommand::StartGame},
    {"back", "Back", PickerMenuCommand::Back},
    {"networking", "Networking", PickerMenuCommand::Networking},
    // The scenario-shaped commands (campaign/level/viewer/teams/progress)
    // live in the SCENARIO submenu now (kScenarioItems below).
    {"scenario", "Scenario", PickerMenuCommand::Scenario},
    // The SDL picker groups the CTF match settings into the TEAMS subscreen;
    // terminal clients keep them as flat team-build items.
    {"ctf_teams", "CTF Teams", PickerMenuCommand::CycleCtfTeamCount},
    {"ctf_caps", "Capture Limit", PickerMenuCommand::CycleCtfCaptureLimit},
    {"ctf_troops", "Scenario Troops", PickerMenuCommand::ToggleCtfScenarioTroops},
}};

// The SCENARIO submenu: everything that chooses or inspects the scenario.
// SET CAMPAIGN / SET LEVEL stay host-gated on the SDL surface; the terminal
// clients present the submenu as a nested flat list.
constexpr std::array<PickerMenuItem, 6> kScenarioItems = {{
    {"set_campaign", "Set Campaign", PickerMenuCommand::SetCampaign},
    {"set_level", "Set Level", PickerMenuCommand::SetLevel},
    {"view_scenario", "View Scenario", PickerMenuCommand::ViewScenario},
    {"teams", "Teams", PickerMenuCommand::Teams},
    {"progress", "Progress", PickerMenuCommand::ShowProgress},
    {"back", "Back", PickerMenuCommand::Back},
}};

// The DIFFICULTY submenu: session difficulty plus the match rules that ride
// SaveData (respawns, respawn delay, permadeath, generator rate). Defaults
// keep classic behavior; the terminal clients present it as a nested list.
constexpr std::array<PickerMenuItem, 6> kDifficultyMenuItems = {{
    {"difficulty", "Difficulty", PickerMenuCommand::SetDifficulty},
    {"respawn_mode", "Respawns", PickerMenuCommand::CycleRespawnMode},
    {"respawn_delay", "Respawn Delay", PickerMenuCommand::CycleRespawnDelay},
    {"permadeath", "Permadeath", PickerMenuCommand::TogglePermadeath},
    {"generator_rate", "Generators", PickerMenuCommand::CycleGeneratorRate},
    {"back", "Back", PickerMenuCommand::Back},
}};

// The Company & Base Camp screens (design §2.2-§2.4). LoadCompany and
// Backups list dynamic rows (companies on disk / snapshots) at runtime; the
// model carries the fixed command chrome all three clients share. Nothing
// presents these menus yet — the SDL specs and the terminal flows land with
// the WP3 screen reshapes, atomically with their re-pins.
constexpr std::array<PickerMenuItem, 4> kLoadCompanyItems = {{
    {"open_company", "Open Company", PickerMenuCommand::OpenCompany},
    {"company_backups", "Backups", PickerMenuCommand::OpenCompanyBackups},
    {"delete_company", "Delete Company", PickerMenuCommand::DeleteCompany},
    {"back", "Back", PickerMenuCommand::Back},
}};

constexpr std::array<PickerMenuItem, 3> kBackupsItems = {{
    {"restore_backup", "Restore Backup", PickerMenuCommand::RestoreBackup},
    {"delete_backup", "Delete Backup", PickerMenuCommand::DeleteBackup},
    {"back", "Back", PickerMenuCommand::Back},
}};

constexpr std::array<PickerMenuItem, 4> kNameEntryItems = {{
    {"company_name_value", "Company Name", PickerMenuCommand::EditCompanyName},
    {"company_name_reroll", "Reroll", PickerMenuCommand::RerollCompanyName},
    {"company_name_accept", "Accept", PickerMenuCommand::AcceptCompanyName},
    {"back", "Back", PickerMenuCommand::Back},
}};

constexpr PickerMenuDefinition kMainMenu{
    PickerMenuId::Main,
    "OpenGlad Main Menu",
    std::span<const PickerMenuItem>(kMainMenuItems),
};

constexpr PickerMenuDefinition kTeamBuildMenu{
    PickerMenuId::TeamBuild,
    "Team Build",
    std::span<const PickerMenuItem>(kTeamBuildItems),
};

constexpr PickerMenuDefinition kScenarioMenu{
    PickerMenuId::Scenario,
    "Scenario",
    std::span<const PickerMenuItem>(kScenarioItems),
};

constexpr PickerMenuDefinition kDifficultyMenu{
    PickerMenuId::Difficulty,
    "Difficulty",
    std::span<const PickerMenuItem>(kDifficultyMenuItems),
};

constexpr PickerMenuDefinition kLoadCompanyMenu{
    PickerMenuId::LoadCompany,
    "Companies",
    std::span<const PickerMenuItem>(kLoadCompanyItems),
};

constexpr PickerMenuDefinition kBackupsMenu{
    PickerMenuId::Backups,
    "Backups",
    std::span<const PickerMenuItem>(kBackupsItems),
};

constexpr PickerMenuDefinition kNameEntryMenu{
    PickerMenuId::NameEntry,
    "Found Your Company",
    std::span<const PickerMenuItem>(kNameEntryItems),
};

} // namespace

const PickerMenuDefinition& picker_menu_definition(PickerMenuId menu)
{
    switch (menu) {
    case PickerMenuId::Main:
        return kMainMenu;
    case PickerMenuId::TeamBuild:
        return kTeamBuildMenu;
    case PickerMenuId::Scenario:
        return kScenarioMenu;
    case PickerMenuId::Difficulty:
        return kDifficultyMenu;
    case PickerMenuId::LoadCompany:
        return kLoadCompanyMenu;
    case PickerMenuId::Backups:
        return kBackupsMenu;
    case PickerMenuId::NameEntry:
        return kNameEntryMenu;
    }
    return kMainMenu;
}

const PickerMenuItem* find_picker_menu_item(PickerMenuId menu, std::string_view item_id)
{
    const auto& definition = picker_menu_definition(menu);
    for (const PickerMenuItem& item : definition.items) {
        if (item.id == item_id)
            return &item;
    }
    return nullptr;
}

const PickerMenuItem* find_picker_menu_item(PickerMenuId menu, PickerMenuCommand command, std::int32_t arg)
{
    const auto& definition = picker_menu_definition(menu);
    for (const PickerMenuItem& item : definition.items) {
        if (item.command == command && item.arg == arg)
            return &item;
    }
    return nullptr;
}

} // namespace og::ui
