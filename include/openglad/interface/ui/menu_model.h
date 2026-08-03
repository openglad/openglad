/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace og::ui {

enum class PickerMenuId : std::int32_t
{
    Main,
    TeamBuild,
    Scenario,
    Difficulty,
    // Company & Base Camp screens (design §2.2-§2.4), append-only:
    LoadCompany, // §2.3 Company List (LOAD)
    Backups,     // §2.4 per-company Backups sub-view
    NameEntry,   // §2.2 new-company name entry
};

enum class PickerMenuCommand : std::int32_t
{
    BeginNewGame,
    ContinueGame,
    Networking,
    HostGame,
    JoinGame,
    SetPlayerMode, // Legacy direct-dispatch compatibility; no menu exposes it.
    SetDifficulty,
    ToggleAlliedMode,
    LevelEdit,
    Options,
    Help,
    Quit,
    ViewTeam,
    TrainTeam,
    HireTroops,
    LoadTeam,
    SaveTeam,
    ShowProgress,
    SetLevel,
    SetCampaign,
    StartGame,
    Back,
    CycleCtfTeamCount,
    CycleCtfCaptureLimit,
    ToggleCtfScenarioTroops,
    ViewScenario,
    Teams,
    Scenario,
    OpenDifficultyMenu,
    CycleRespawnMode,
    CycleRespawnDelay,
    TogglePermadeath,
    CycleGeneratorRate,
    // Company & Base Camp commands (design §2.2-§2.4), append-only:
    OpenCompany,        // §2.3 open the selected company (row click / "open N")
    OpenCompanyBackups, // §2.3 BK — enter the Backups sub-view
    DeleteCompany,      // §2.3 X — delete company (+ backups) after confirm
    RestoreBackup,      // §2.4 rewind to the selected snapshot after confirm
    DeleteBackup,       // §2.4 delete one snapshot
    EditCompanyName,    // §2.2 edit the name strip in place
    RerollCompanyName,  // §2.2 REROLL — regenerate the fantasy default
    AcceptCompanyName,  // §2.2 ACCEPT — create the company file
    // §2.1 main-menu LOAD door. Temporarily opens the legacy load flow until
    // WP3's Company List screen lands; then this routes to LoadCompany.
    LoadGame,
    // §2.5 base camp (append-only):
    ToggleDeploy, // flip a roster member's mission-deploy flag
    ToggleReady,  // networked lobbies only: flip this machine's ready state
    // DIFFICULTY screen, host-only: flip free hire/train purchases.
    ToggleInfiniteGold,
};

struct PickerMenuItem
{
    std::string_view id;
    std::string_view label;
    PickerMenuCommand command;
    std::int32_t arg = 0;
};

struct PickerMenuDefinition
{
    PickerMenuId id;
    std::string_view title;
    std::span<const PickerMenuItem> items;
};

const PickerMenuDefinition& picker_menu_definition(PickerMenuId menu);
const PickerMenuItem* find_picker_menu_item(PickerMenuId menu, std::string_view item_id);
const PickerMenuItem* find_picker_menu_item(PickerMenuId menu, PickerMenuCommand command, std::int32_t arg = 0);

} // namespace og::ui
