/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// ABILITIES (New Ability Select, docs/ability-select-design.md): the TRAIN
// screen's door to which ability sits on which SPEC key. The page's words
// and edit rules are og::ui::ability_page and its edits (picker_common.h);
// this screen is their SDL projection.
//
// Today this is the registry's spec only: a BACK row, so the engine sweeps
// (remote start, shape, nav) cover the screen id from the commit that adds
// it. The belt and bench rows arrive with the screen itself.

#include <openglad/interface/ui/menu_screen_spec.h>

struct button;

namespace og::ui {

const MenuScreenSpec& abilities_menu_screen_spec();

}  // namespace og::ui

// The D3 accessor pair (buttons() fills what count() reads).
button* picker_abilities_buttons();
int picker_abilities_button_count();
