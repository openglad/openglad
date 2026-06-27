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

#include <cstdint>
#include <cstdlib>

#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/gameplay_context.h>

#define MAKE_STATE(x, y) reinterpret_cast<PathState>(static_cast<intptr_t>(((y) / GRID_SIZE) * MAP_WIDTH + ((x) / GRID_SIZE)))
#define ALIGN_TO_GRID(x) ((x) / GRID_SIZE * GRID_SIZE)

void walker::find_path_to_foe()
{
    if (foe() == nullptr || current_game == nullptr)
        return;

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    if (pathing == nullptr)
        return;

    float total_cost = 0.0f;
    const PathState start_state = MAKE_STATE(xpos(), ypos());
    const PathState end_state = MAKE_STATE(foe()->xpos(), foe()->ypos());

    pathing->solve_for(this, start_state, end_state, path_to_foe, total_cost);
}

// Position-goal twin of find_path_to_foe: paths toward a fixed map point.
// The result lands in path_to_foe so follow_path_to_foe consumes it as-is.
void walker::find_path_to_point(short x, short y)
{
    if (current_game == nullptr)
        return;

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    if (pathing == nullptr)
        return;

    float total_cost = 0.0f;
    const PathState start_state = MAKE_STATE(xpos(), ypos());
    const PathState end_state = MAKE_STATE(x, y);

    pathing->solve_for_point(this, x, y, start_state, end_state, path_to_foe,
                             total_cost);
}

void walker::follow_path_to_foe()
{
    while (!path_to_foe.empty())
    {
        auto node = path_to_foe.begin();
        const PathState state = *node;
        int dx = GET_STATE_X(state) - ALIGN_TO_GRID(xpos());
        int dy = GET_STATE_Y(state) - ALIGN_TO_GRID(ypos());

        if (dx != 0 || dy != 0)
        {
            // Normalize the deltas so walkstep can use them as stepsize factors.
            if (dx != 0)
                dx /= abs(dx);
            if (dy != 0)
                dy /= abs(dy);

            // Move toward this node and stop.
            walkstep(dx, dy);
            break;
        }

        // We already made it to this node, so remove it.
        path_to_foe.erase(node);
    }
}

// draw_path() has been moved to src/render/walker_draw.cpp as draw_walker_path()
