/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
/* Terminal menu projection implementation (design: menu-engine G7).
 *
 * The strings assembled here were moved VERBATIM from the text/curses
 * clients' private helpers so both terminal surfaces stay byte-identical.
 */

#include <openglad/interface/ui/terminal_menu_model.h>

#include <openglad/gameplay/guy.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <format>
#include <string>
#include <vector>

namespace og::ui {
namespace {

// Team + gold header lines shown above the Team Build menu (moved verbatim
// from the curses client; the text client printed the same content inline).
std::vector<std::string> team_build_context_lines(const SaveData& save)
{
    std::vector<std::string> lines;
    if (save.team_size == 0) {
        lines.push_back("Team: (empty)");
    } else {
        std::string team = "Team: ";
        bool first = true;
        for_each_team_member(save, [&](int, const guy& member) {
            if (!first)
                team += ", ";
            team += std::format("{} ({})", member.name,
                                family_display_name(member.family));
            first = false;
        });
        lines.push_back(team);
    }
    lines.push_back(std::format("Gold: {}", format_wallet_amount(save, 0)));
    return lines;
}

// Terminal guard: the §2.5 READY item outside a networked lobby
// (solo/local sessions have no ready machinery — §2.6 state 1). The match
// knobs are the camp's MATCH SETUP page now (docs/camp-controls-design.md),
// so no terminal menu carries them or their old versus guard. Scenario
// troops stays ungated on the SCENARIO screen: "strip everything authored"
// is meaningful on classic campaigns too.
constexpr std::string_view kReadyGuardMessage =
    "Ready applies to networked lobbies only.";

GateBinding terminal_item_gate(PickerMenuCommand command)
{
    switch (command) {
    case PickerMenuCommand::ToggleReady:
        return GateBinding{MenuGate::NetworkedOnly, nullptr,
                           kReadyGuardMessage};
    default:
        return GateBinding{};
    }
}

} // namespace

TerminalMenuModel build_terminal_menu_model(PickerMenuId menu_id,
                                            const MenuLabelContext& context)
{
    const PickerMenuDefinition& definition = picker_menu_definition(menu_id);

    TerminalMenuModel model;
    model.title = std::string(definition.title);
    if (menu_id == PickerMenuId::TeamBuild && context.save != nullptr)
        model.context_lines = team_build_context_lines(*context.save);
    model.entries.reserve(definition.items.size());
    for (const PickerMenuItem& item : definition.items) {
        model.entries.push_back(
            TerminalMenuEntry{&item, menu_item_label(item, context), true});
    }
    model.cancel_item = menu_cancel_item(menu_id);
    return model;
}

std::string_view terminal_gate_message(const PickerMenuItem& item,
                                       const MenuLabelContext& context)
{
    const GateBinding binding = terminal_item_gate(item.command);
    if (binding.guard_message.empty())
        return {};
    if (gate_state(binding, context) == RowState::Visible)
        return {};
    return binding.guard_message;
}

} // namespace og::ui
