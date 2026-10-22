/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The CHANGELOG! page (docs/ability-select-design.md): shown once, the
// first time the game starts after an update, from the picker's shared
// start, before any lobby exists.
//
// Today this is the registry's spec only: an OK row, so the engine sweeps
// (shape, nav, the remote-start allowlist) cover the screen id from the
// commit that adds it. The entries, the keyboard picture and the seen-once
// marker arrive with the page itself.

#include <openglad/interface/ui/menu_screen_spec.h>

struct button;

namespace og::ui {

const MenuScreenSpec& changelog_menu_screen_spec();

}  // namespace og::ui

// The D3 accessor pair (buttons() fills what count() reads).
button* picker_changelog_buttons();
int picker_changelog_button_count();
