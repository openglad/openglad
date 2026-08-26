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

#include <algorithm>
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
    case PickerMenuCommand::CycleRespawnMode:
        return format_respawn_mode_label(save);
    case PickerMenuCommand::CycleRespawnDelay:
        return format_respawn_delay_label(save);
    case PickerMenuCommand::TogglePermadeath:
        return format_permadeath_label(save);
    case PickerMenuCommand::CycleGeneratorRate:
        return format_generator_rate_label(save);
    case PickerMenuCommand::ToggleInfiniteGold:
        return format_infinite_gold_label(save);
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

// --- PageModel (G6) ---------------------------------------------------------
// The arithmetic is a verbatim lift of the VIEW LEVEL pager
// (create_view_scenario_menu): ceiling page count floored at one page,
// clamped stepping, hidden-when-<=1-page, and the 1-based "p/N" indicator.
// The differential oracle in tests/unit/test_menu_spec.cpp pins the
// equivalence against the transcribed legacy arithmetic.

PageModel PageModel::make(int item_count, int rows_per_page)
{
    PageModel model;
    model.item_count = std::max(0, item_count);
    model.rows_per_page = std::max(1, rows_per_page);
    return model;
}

int PageModel::page_count() const
{
    return std::max(1, (item_count + rows_per_page - 1) / rows_per_page);
}

bool PageModel::multi_page() const
{
    return page_count() > 1;
}

int PageModel::first_index() const
{
    return page * rows_per_page;
}

int PageModel::end_index() const
{
    return std::min(item_count, first_index() + rows_per_page);
}

bool PageModel::step(int delta)
{
    const int next_page = std::clamp(page + delta, 0, page_count() - 1);
    if (next_page == page)
        return false;
    page = next_page;
    return true;
}

std::string PageModel::indicator() const
{
    return std::format("{}/{}", page + 1, page_count());
}

} // namespace og::ui
