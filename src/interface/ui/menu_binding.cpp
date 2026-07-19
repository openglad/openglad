/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Shared menu binding layer implementation (design: menu-engine §1.3).
 *
 * One label switch and one gate evaluator for all three picker clients.
 * Every dynamic string routes through the picker_common format_* helpers so
 * the SDL, text, and curses surfaces stay byte-identical by construction.
 */

#include <openglad/interface/ui/menu_binding.h>

#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/campaign_metadata.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <format>
#include <string>

namespace og::ui {

std::string level_display_guarded(std::string_view campaign, int level)
{
    if (get_mounted_campaign() == campaign)
        return og::data::scenario_display_name(level);
    return std::to_string(level);
}

std::string menu_item_label(const PickerMenuItem& item,
                            const MenuLabelContext& context)
{
    // Session-backed bindings (no SaveData needed).
    if (item.command == PickerMenuCommand::SetDifficulty)
        return format_difficulty_label(context.session_difficulty);
    if (item.command == PickerMenuCommand::SetLevel)
        return std::format("{} ({})", item.label,
            level_display_guarded(context.campaign, context.level));
    if (item.command == PickerMenuCommand::SetCampaign)
        return std::format("{} ({})", item.label,
            og::data::campaign_display_title(context.campaign));

    // Save-backed bindings fall through to the fixed label when the caller
    // provided no save to read.
    if (context.save == nullptr)
        return std::string(item.label);
    const SaveData& save = *context.save;

    switch (item.command) {
    case PickerMenuCommand::ToggleAlliedMode:
        return format_allied_mode_label(save);
    case PickerMenuCommand::CycleCtfTeamCount:
        return format_ctf_teams_label(save);
    case PickerMenuCommand::CycleCtfCaptureLimit:
        return format_ctf_caps_label(save);
    case PickerMenuCommand::ToggleCtfScenarioTroops:
        return format_ctf_troops_label(save);
    case PickerMenuCommand::CycleRespawnMode:
        return format_respawn_mode_label(save);
    case PickerMenuCommand::CycleRespawnDelay:
        return format_respawn_delay_label(save);
    case PickerMenuCommand::TogglePermadeath:
        return format_permadeath_label(save);
    case PickerMenuCommand::CycleGeneratorRate:
        return format_generator_rate_label(save);
    default:
        return std::string(item.label);
    }
}

RowState gate_state(GateBinding binding, const MenuLabelContext& context)
{
    switch (binding.gate) {
    case MenuGate::Always:
        return RowState::Visible;
    case MenuGate::HostOnly:
        return context.is_host ? RowState::Visible : RowState::Hidden;
    case MenuGate::NetworkedOnly:
        return context.is_networked ? RowState::Visible : RowState::Hidden;
    case MenuGate::LocalOnly:
        return context.is_networked ? RowState::Hidden : RowState::Visible;
    case MenuGate::CtfCampaignOnly:
        return (context.save != nullptr && is_ctf_campaign(*context.save))
            ? RowState::Visible
            : RowState::Hidden;
    case MenuGate::Custom:
        return (binding.custom == nullptr || binding.custom(context))
            ? RowState::Visible
            : RowState::Hidden;
    }
    return RowState::Visible;
}

const PickerMenuItem* menu_cancel_item(PickerMenuId menu_id)
{
    if (menu_id == PickerMenuId::Main)
        return find_picker_menu_item(menu_id, PickerMenuCommand::Quit);
    return find_picker_menu_item(menu_id, PickerMenuCommand::Back);
}

} // namespace og::ui
