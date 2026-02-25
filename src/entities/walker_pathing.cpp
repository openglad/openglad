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

#include <openglad/entities/walker.h>
#include <openglad/entities/pathfinding_grid.h>
#include <openglad/gameplay/gameplay_context.h>

#include <cstdlib>
#include <cstdint>

#define ALIGN_TO_GRID(x) ((x)/GRID_SIZE * GRID_SIZE)

void walker::find_path_to_foe()
{
    float totalCost = 0.0f;

    path_to_foe.clear();
    if (!foe || !og::gameplay::current_game || !og::gameplay::current_game->pathfinding)
        return;

    og::gameplay::current_game->pathfinding->solve_path(this, path_to_foe, totalCost);
}

void walker::follow_path_to_foe()
{
    while (path_to_foe.size() > 0)
    {
        std::vector<MicroPatherState>::iterator node = path_to_foe.begin();
        MicroPatherState state = *node;
        int dx = GET_STATE_X(state) - ALIGN_TO_GRID(xpos);
        int dy = GET_STATE_Y(state) - ALIGN_TO_GRID(ypos);

        if (dx != 0 || dy != 0)
        {
            // Normalize the deltas so walkstep can use them as stepsize factors.
            if (dx != 0)
                dx /= abs(dx);
            if (dy != 0)
                dy /= abs(dy);

            // Move toward there and we're done.
            walkstep(dx, dy);
            break;
        }

        // We already made it to this node, so remove it
        path_to_foe.erase(node);
    }
}

// draw_path() has been moved to src/render/walker_draw.cpp as draw_walker_path()
