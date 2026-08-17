/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <openglad/interface/ui/level_picker.h>
#include <openglad/interface/base.h>
#include <vector>

#include <openglad/interface/screen.h>
#include <openglad/resources/level_selection.h>

// Get list of accessible levels (cleared levels + their exits)
std::vector<int> get_accessible_levels()
{
    // The computation lives in og::data::accessible_levels (the earned-roads
    // gate consults it from the SDL-free clients too); this wrapper only
    // resolves the session's save.
    screen* game = og::runtime::current_session->myscreen_;
    if (!game) {
        return {1};
    }
    return og::data::accessible_levels(game->save_data);
}
