/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// CHANGELOG!: see changelog_screen.h.

#include <openglad/interface/ui/changelog_screen.h>

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

// The OK row at its place on the finished page (centred under the picture).
constexpr MenuButtonSpec kChangelogRows[] = {
    {.id = "changelog_ok", .label = "OK", .hotkey = KEYSTATE_RETURN,
     .x = 135, .y = 178, .w = 50, .h = 16,
     .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT},
};

std::vector<button>& changelog_buttons()
{
    static std::vector<button> buttons;
    return buttons;
}

void changelog_draw_background(void* /*screen_state*/)
{
    og::runtime::current_session->myscreen_->clearbuffer();
}

}  // namespace

const MenuScreenSpec& changelog_menu_screen_spec()
{
    static const MenuScreenSpec spec{
        .name = "changelog",
        .rows = kChangelogRows,
        .row_count = static_cast<int>(std::size(kChangelogRows)),
        .buttons_accessor = &picker_changelog_buttons,
        .count_accessor = &picker_changelog_button_count,
        // No lobby exists yet when the page shows, so there is no remote
        // start to follow (the engine test's allowlist names it).
        .remote_start = RemoteStartScope::None,
        .draw_background = &changelog_draw_background,
        .exit_value = MENU_REDRAW,
    };
    return spec;
}

}  // namespace og::ui

button* picker_changelog_buttons()
{
    og::ui::materialize_menu_buttons(og::ui::changelog_menu_screen_spec(),
                                     og::ui::changelog_buttons());
    return og::ui::changelog_buttons().data();
}

int picker_changelog_button_count()
{
    return static_cast<int>(og::ui::changelog_buttons().size());
}
