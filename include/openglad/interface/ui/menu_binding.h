/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Shared menu binding layer (SDL-free).
//
// The single source for dynamic menu labels, gating predicates, and cancel
// semantics consumed by every picker client (SDL, text, curses). Label
// bodies call the picker_common format_* helpers so all three clients emit
// byte-identical strings. Compiled into og_interface AND the text/curses
// targets (the menu_model/picker_common TU-sharing pattern).

#include <openglad/interface/ui/menu_model.h>

#include <cstdint>
#include <string>
#include <string_view>

struct SaveData;

namespace og::ui {

// Per-client borrow bundle: everything a label or gate may read. The SDL
// client fills it from myscreen_->save_data; the terminal clients from their
// member SaveData + TextPickerConfig session values.
struct MenuLabelContext {
    const SaveData* save = nullptr;
    int session_difficulty = 0;
    bool is_networked = false;
    bool is_host = true;
    bool spectator = false;  // numplayers == 0
    // Session campaign/level backing the SET LEVEL / SET CAMPAIGN labels
    // (terminals: TextPickerConfig values; SDL: save-derived).
    std::string campaign;
    int level = 1;
    // Layer F: company name, lobby ready states, cross-control, indexes.
};

using LabelFormatter = std::string (*)(const MenuLabelContext&);

struct LabelBinding {
    std::string_view fixed{};
    LabelFormatter formatter = nullptr;
};

enum class MenuGate : std::uint8_t {
    Always,
    HostOnly,
    NetworkedOnly,
    LocalOnly,
    CtfCampaignOnly,
    Custom,
};

struct GateBinding {
    MenuGate gate = MenuGate::Always;
    bool (*custom)(const MenuLabelContext&) = nullptr;
    std::string_view guard_message{};  // terminals: shown on activate when gated
};

// Row visibility produced by a gate: Hidden rows leave the surface entirely;
// Disabled rows stay visible-but-inert (engine screens draw them dimmed).
enum class RowState : std::uint8_t {
    Visible,
    Hidden,
    Disabled,
};

// THE single label switch replacing the duplicated per-client switches
// (formerly text_picker.cpp / curses_picker_client.cpp private copies).
// Bodies call the picker_common format_* helpers; items without a dynamic
// binding fall through to their fixed label.
std::string menu_item_label(const PickerMenuItem& item,
                            const MenuLabelContext& context);

// Evaluate a gate against the context.
RowState gate_state(GateBinding binding, const MenuLabelContext& context);

// Cancel semantics shared by every client: Quit on the Main menu, Back
// everywhere else (nullptr when the menu has no such item, e.g. the web
// main menu that ships Help instead of Quit).
const PickerMenuItem* menu_cancel_item(PickerMenuId menu_id);

// Mount-guarded level display. Scenario titles are read from the MOUNTED
// package; when the session campaign isn't the mounted one (e.g. a loaded
// save points elsewhere) the title would describe the wrong campaign, so
// show the bare number instead.
std::string level_display_guarded(std::string_view campaign, int level);

} // namespace og::ui
