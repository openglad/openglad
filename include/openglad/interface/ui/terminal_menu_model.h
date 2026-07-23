/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Terminal menu projection (SDL-free, design: menu-engine G7).
//
// One prepared model per shared menu screen for the text and curses clients:
// the FULL entry list (never filtered — this preserves the 1-based index
// contract by construction), resolved dynamic labels, the context header
// lines, and the cancel item. The clients keep their own input loops and
// rendering; only the content is shared.

#include <openglad/interface/ui/menu_binding.h>
#include <openglad/interface/ui/menu_model.h>

#include <string>
#include <string_view>
#include <vector>

namespace og::ui {

struct TerminalMenuEntry {
    const PickerMenuItem* item = nullptr;
    std::string label;
    bool selectable = true;
};

struct TerminalMenuModel {
    std::string title;
    std::vector<std::string> context_lines;   // roster/gold header (Team Build)
    std::vector<TerminalMenuEntry> entries;   // FULL list, never filtered
    const PickerMenuItem* cancel_item = nullptr;  // Quit on Main, Back elsewhere
};

// Build the projection for one shared menu: title and items from
// picker_menu_definition, labels via menu_item_label, context lines for the
// Team Build screen, cancel item via menu_cancel_item.
TerminalMenuModel build_terminal_menu_model(PickerMenuId menu_id,
                                            const MenuLabelContext& context);

// Guard message a terminal client shows when the item is activated while its
// gate denies it ("" when the item is not gated / the gate passes). Currently
// carries the CTF match-settings guard.
std::string_view terminal_gate_message(const PickerMenuItem& item,
                                       const MenuLabelContext& context);

} // namespace og::ui
