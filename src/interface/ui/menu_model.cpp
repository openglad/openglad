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

constexpr std::array<PickerMenuItem, 12> kMainMenuItems = {{
    {"begin_new_game", "Begin New Game", PickerMenuCommand::BeginNewGame},
    {"continue_game", "Continue Game", PickerMenuCommand::ContinueGame},
    {"networking", "Networking", PickerMenuCommand::Networking},
    {"4_player", "4 Player", PickerMenuCommand::SetPlayerMode, 4},
    {"3_player", "3 Player", PickerMenuCommand::SetPlayerMode, 3},
    {"2_player", "2 Player", PickerMenuCommand::SetPlayerMode, 2},
    {"1_player", "1 Player", PickerMenuCommand::SetPlayerMode, 1},
    {"difficulty", "Difficulty", PickerMenuCommand::SetDifficulty},
    {"pvp_allied", "PVP Mode", PickerMenuCommand::ToggleAlliedMode},
    {"level_edit", "Level Edit", PickerMenuCommand::LevelEdit},
    {"options", "Options", PickerMenuCommand::Options},
#ifdef __EMSCRIPTEN__
    {"help", "Help", PickerMenuCommand::Help},
#else
    {"quit", "Quit", PickerMenuCommand::Quit},
#endif
}};

constexpr std::array<PickerMenuItem, 10> kTeamBuildItems = {{
    {"view_team", "View Team", PickerMenuCommand::ViewTeam},
    {"train_team", "Train Team", PickerMenuCommand::TrainTeam},
    {"hire_troops", "Hire Troops", PickerMenuCommand::HireTroops},
    {"load_team", "Load Team", PickerMenuCommand::LoadTeam},
    {"save_team", "Save Team", PickerMenuCommand::SaveTeam},
    {"go", "GO!", PickerMenuCommand::StartGame},
    {"back", "Back", PickerMenuCommand::Back},
    {"progress", "Progress", PickerMenuCommand::ShowProgress},
    {"set_level", "Set Level", PickerMenuCommand::SetLevel},
    {"set_campaign", "Set Campaign", PickerMenuCommand::SetCampaign},
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

} // namespace

const PickerMenuDefinition& picker_menu_definition(PickerMenuId menu)
{
    switch (menu) {
    case PickerMenuId::Main:
        return kMainMenu;
    case PickerMenuId::TeamBuild:
        return kTeamBuildMenu;
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
