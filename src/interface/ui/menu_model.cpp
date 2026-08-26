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

// The DIFFICULTY door left this menu for Team Build
// (docs/camp-controls-design.md): difficulty, respawns and permadeath are
// settings about the company's next fight, so they now sit where the company
// is. Every row below it moved up one 1-based position — re-pinned in the
// same commit across the positional drivers (the headless drives, the
// interactive script, the curses route tests).
constexpr std::array<PickerMenuItem, 8> kMainMenuItems = {{
    {"begin_new_game", "Begin New Game", PickerMenuCommand::BeginNewGame},
    {"continue_game", "Continue Game", PickerMenuCommand::ContinueGame},
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
    // #155: the CLOUD door — last, matching the SDL surface's CLOUD SAVES.
    {"cloud", "Cloud Saves", PickerMenuCommand::OpenCloudMenu},
}};

// §2.5 base camp: the TeamBuild model stayed 12 items by IN-PLACE
// substitution (minimizes 1-based churn for the text/curses index contract):
// 1 roster (was view_team), 4 deploy (was load_team), 5 ready (was
// save_team). Save/Load left the base camp entirely — saving is automatic
// (§3.8). The 13th row is the zone's Camp door
// (docs/basecamp-zones-design.md "Terminals"), inserted before Back rather
// than appended: it has to stay inside the curses digit-jump budget (the
// first 9 selectable rows), and a door only the arrow keys reach is a door
// most players never open. That costs back/networking/scenario/the CTF trio
// one ordinal each — re-pinned in the same commit across the three
// positional drivers (the headless drives, the interactive script, the
// curses route tests).
// The flat CTF trio then LEFT (docs/camp-controls-design.md): teams, target
// score and troops are the modes camp's MATCH SETUP page now, one place that
// speaks them in plain words on every client. DIFFICULTY arrived in their
// place, appended so nothing above it moved. It is past the digit-jump
// budget, like Scenario before it — the arrow keys reach it, and unlike a
// match rule it is not something a player retunes every round.
// LINEUP (docs/lineup-design.md §8) then appended after difficulty, by the
// same rule difficulty itself followed: nothing above it moves, so the two
// 1-based position consumers keep every ordinal they already pin and only
// gain one. It sits past the digit-jump budget for the same reason
// difficulty does — a match-composition page is not a per-round retune.
constexpr std::array<PickerMenuItem, 12> kTeamBuildItems = {{
    {"roster", "Roster", PickerMenuCommand::ViewTeam},
    {"train_team", "Train Team", PickerMenuCommand::TrainTeam},
    {"hire_troops", "Hire Troops", PickerMenuCommand::HireTroops},
    {"deploy", "Deploy", PickerMenuCommand::ToggleDeploy},
    {"ready", "Ready", PickerMenuCommand::ToggleReady},
    {"go", "GO!", PickerMenuCommand::StartGame},
    // The Lua-composable gameplay zone's terminal face. Not gated at the
    // model layer — terminals list every row; opening it on a campaign that
    // composed no camp prints the guard line (the SDL surface renders the
    // default zone in place instead, from its own spec table).
    {"camp", "Camp", PickerMenuCommand::CampaignCamp},
    {"back", "Back", PickerMenuCommand::Back},
    {"networking", "Networking", PickerMenuCommand::Networking},
    // The scenario-shaped commands (campaign/level/viewer/matchup/progress)
    // live in the SCENARIO submenu now (kScenarioItems below).
    {"scenario", "Scenario", PickerMenuCommand::Scenario},
    // The door into the DIFFICULTY submenu (kDifficultyMenuItems below).
    {"difficulty", "Difficulty", PickerMenuCommand::OpenDifficultyMenu},
    // The LINEUP door (kTeamBuildItems' growth rule above).
    {"lineup", "Lineup", PickerMenuCommand::Lineup},
}};

// The SCENARIO submenu: everything that chooses or inspects the scenario.
// SET CAMPAIGN / SET LEVEL stay host-gated on the SDL surface; the terminal
// clients present the submenu as a nested flat list.
constexpr std::array<PickerMenuItem, 8> kScenarioItems = {{
    {"set_campaign", "Set Campaign", PickerMenuCommand::SetCampaign},
    {"set_level", "Set Level", PickerMenuCommand::SetLevel},
    {"view_scenario", "View Scenario", PickerMenuCommand::ViewScenario},
    {"matchup", "Matchup", PickerMenuCommand::Teams},
    {"progress", "Progress", PickerMenuCommand::ShowProgress},
    // Appended before Back: the two 1-based TEXT position consumers
    // (scripts/test_text_picker_interactive.sh, tests/unit/
    // test_platform_headless.cpp) select by ordinal, so growth stays
    // append-only and Back keeps its "last item" reading.
    {"troops", "Scenario Troops", PickerMenuCommand::ToggleCtfScenarioTroops},
    // #207: the terminal replay prompt — a CLEARED level re-fought with its
    // authored census, the cursor coming home on the win. Appended before
    // Back like troops above (the 1-based position consumers again); the
    // SDL surface carries the arm on PROGRESS's per-row REPLAY instead.
    {"replay_level", "Replay Level", PickerMenuCommand::ReplayLevel},
    // The v1 MISSIONS row is gone, not moved: the scripted book is a room
    // inside the Base Camp now, reached from a camp page row, so a fourth
    // level-selection door in SCENARIO would restructure nothing
    // (docs/basecamp-zones-design.md "Retirement ledger"). Back returns to
    // its "last item" reading, at 8 now.
    {"back", "Back", PickerMenuCommand::Back},
}};

// The DIFFICULTY submenu: session difficulty plus the match rules that ride
// SaveData (respawns, respawn delay, permadeath, generator rate, infinite
// gold). Defaults keep classic behavior; the terminal clients present it as a
// nested list. Append new rows before "back" — Back stays last.
constexpr std::array<PickerMenuItem, 7> kDifficultyMenuItems = {{
    {"difficulty", "Difficulty", PickerMenuCommand::SetDifficulty},
    {"respawn_mode", "Respawns", PickerMenuCommand::CycleRespawnMode},
    {"respawn_delay", "Respawn Delay", PickerMenuCommand::CycleRespawnDelay},
    {"permadeath", "Permadeath", PickerMenuCommand::TogglePermadeath},
    {"generator_rate", "Generators", PickerMenuCommand::CycleGeneratorRate},
    {"infinite_gold", "Infinite Gold", PickerMenuCommand::ToggleInfiniteGold},
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

// #155 cloud saves: passphrase entry + the two manual sync actions. One
// passphrase = one cloud slot holding one company file.
constexpr std::array<PickerMenuItem, 4> kCloudSaveItems = {{
    {"cloud_passphrase", "Passphrase", PickerMenuCommand::CloudSetPassphrase},
    {"cloud_upload", "Upload", PickerMenuCommand::CloudUpload},
    {"cloud_download", "Download", PickerMenuCommand::CloudDownload},
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

constexpr PickerMenuDefinition kCloudSaveMenu{
    PickerMenuId::CloudSave,
    "Cloud Save",
    std::span<const PickerMenuItem>(kCloudSaveItems),
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
    case PickerMenuId::CloudSave:
        return kCloudSaveMenu;
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
