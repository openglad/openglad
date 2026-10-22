/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// ABILITIES: see abilities_screen.h.

#include <openglad/interface/ui/abilities_screen.h>

#include <openglad/interface/base.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>

#include "picker_sdl_defs.h"

#include <iterator>
#include <vector>

namespace og::ui {

namespace {

// The BACK row at its place on the finished screen's grid (y 176).
constexpr MenuButtonSpec kAbilitiesRows[] = {
    {.id = "abilities_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
     .x = 10, .y = 176, .w = 50, .h = 16,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT},
};

std::vector<button>& abilities_buttons()
{
    static std::vector<button> buttons;
    return buttons;
}

// The classic picker frame: clear, then the tiled backdrop.
void abilities_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
    draw_backdrop();
}

}  // namespace

const MenuScreenSpec& abilities_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "abilities",
        .rows = kAbilitiesRows,
        .row_count = static_cast<int>(std::size(kAbilitiesRows)),
        .buttons_accessor = &picker_abilities_buttons,
        .count_accessor = &picker_abilities_button_count,
        // Opened from TRAIN inside Base Camp: a joiner parked here follows
        // the host's GO like every team-build screen.
        .remote_start = RemoteStartScope::TeamBuildScope,
        .remote_start_exit = RemoteStartExit::ReturnMenuExit,
        .right_click_enabled = true,
        .polls_lobby = true,
        .draw_background = &abilities_draw_background,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

}  // namespace og::ui

button* picker_abilities_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::abilities_menu_screen_spec(),
                                     og::ui::abilities_buttons());
    return og::ui::abilities_buttons().data();
}

int picker_abilities_button_count()
{
    return static_cast<int>(og::ui::abilities_buttons().size());
}
