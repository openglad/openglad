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

#include <openglad/interface/screen.h>
#include <openglad/interface/ui/level_editor_state.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/walker.h>

static inline LevelEditorState& eds() { return *og::runtime::current_session->editor_; }

bool are_objects_outside_area(LevelRuntimeData* level, int x, int y, int w, int h)
{
    x *= GRID_SIZE;
    y *= GRID_SIZE;
    w *= GRID_SIZE;
    h *= GRID_SIZE;

    for (auto& uptr : level->world().oblist)
    {
        walker* ob = uptr.get();
        if(ob && (x > ob->xpos || ob->xpos >= x + w || y > ob->ypos || ob->ypos >= y + h))
            return true;
    }

    for (auto& uptr : level->world().fxlist)
    {
        walker* ob = uptr.get();
        if(ob && (x > ob->xpos || ob->xpos >= x + w || y > ob->ypos || ob->ypos >= y + h))
            return true;
    }

    for (auto& uptr : level->world().weaplist)
    {
        walker* ob = uptr.get();
        if(ob && (x > ob->xpos || ob->xpos >= x + w || y > ob->ypos || ob->ypos >= y + h))
            return true;
    }

    return false;
}

void set_screen_pos(screen *screenp, Sint32 x, Sint32 y)
{
    screenp->level_visuals_.topx = x;
    screenp->level_visuals_.topy = y;
    eds().redraw = 1;
}
