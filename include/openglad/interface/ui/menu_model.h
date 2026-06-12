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
};

enum class PickerMenuCommand : std::int32_t
{
    BeginNewGame,
    ContinueGame,
    Networking,
    HostGame,
    JoinGame,
    SetPlayerMode,
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
