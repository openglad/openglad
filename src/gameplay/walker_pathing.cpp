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

#define MAKE_STATE(x, y, f) reinterpret_cast<PathState>(static_cast<intptr_t>(static_cast<intptr_t>(f) * FLOOR_STRIDE + ((y) / GRID_SIZE) * MAP_WIDTH + ((x) / GRID_SIZE)))
#define ALIGN_TO_GRID(x) ((x) / GRID_SIZE * GRID_SIZE)

void walker::find_path_to_foe()
{
    if (foe() == nullptr || current_game == nullptr)
        return;

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    if (pathing == nullptr)
        return;

    float total_cost = 0.0f;
    const PathState start_state = MAKE_STATE(xpos(), ypos(), floor());
    const PathState end_state = MAKE_STATE(foe()->xpos(), foe()->ypos(), foe()->floor());

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
    const PathState start_state = MAKE_STATE(xpos(), ypos(), floor());
    const PathState end_state = MAKE_STATE(x, y, floor());

    pathing->solve_for_point(this, x, y, start_state, end_state, path_to_foe,
                             total_cost);
}

void walker::follow_path_to_foe()
{
    const bool multifloor = current_game != nullptr &&
        current_game->world != nullptr &&
        current_game->world->floor_count() > 1;

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

            // Convex-corner alignment assist (2026-07; see
            // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). The node-reached test
            // below is cell-quantized (ALIGN_TO_GRID), so a node counts as
            // reached while the body still spills up to GRID_SIZE-1 px into
            // the next row/column. When the following hop is CARDINAL and
            // hugs an impassable cell on the spill side (a convex wall
            // corner — the A* diagonal bias produces these constantly), the
            // direct step stays pixel-blocked at EVERY misaligned offset, and
            // walkstep's fixed NPC fallback (e.g. LEFT-blocked -> step DOWN)
            // marches the walker back OUT of alignment: a deterministic
            // 2-tick oscillation that pinned chasers at corridor corners
            // forever (the Westlands L24 terrace wedges). When that exact
            // shape occurs — cardinal hop, perpendicular misalignment, direct
            // step pixel-blocked — slide toward exact alignment instead, one
            // checked pixel at a time. The swept band stays inside
            // rows/columns the body already occupies (the remainder is
            // strictly positive toward +axis), so the slide is grid-safe by
            // construction; per-pixel query_passable keeps entity collision
            // semantics identical to a normal walk. No RNG; a pure function
            // of position. GATED on multifloor (like the graph's
            // no-corner-cut rule): un-stalling the classic single-floor
            // approach moved first-hit timing in 18 parity goldens and would
            // shift calibrated campaign balance, so single-floor levels KEEP
            // the classic wedge behavior deliberately.
            if (multifloor && (dx == 0) != (dy == 0) &&
                current_game->world != nullptr)
            {
                const int mis = (dx != 0)
                    ? ypos() - ALIGN_TO_GRID(ypos())
                    : xpos() - ALIGN_TO_GRID(xpos());
                if (mis > 0 &&
                    !current_game->world->query_passable(
                        xpos() + static_cast<float>(dx) * stepsize(),
                        ypos() + static_cast<float>(dy) * stepsize(), this))
                {
                    int budget = static_cast<int>(stepsize());
                    if (budget > mis)
                        budget = mis;
                    bool slid = false;
                    const float sx = (dx != 0) ? 0.0f : -1.0f;
                    const float sy = (dx != 0) ? -1.0f : 0.0f;
                    for (int i = 0; i < budget; i++)
                    {
                        if (!current_game->world->query_passable(
                                xpos() + sx, ypos() + sy, this))
                            break;
                        worldmove(sx, sy);
                        slid = true;
                    }
                    if (slid)
                        break; // this tick's move went to aligning
                }
            }

            // Move toward this node and stop.
            walkstep(dx, dy);
            break;
        }

        // Reached this node's (x,y) by our top-left corner. On a multi-floor
        // path, a node on a different floor means this cell is the Z-stair we
        // must take. apply_z_motion (which performs the floor change) probes our
        // CENTER cell, not our corner — so for a sized walker the corner can sit
        // on the stair cell while the center is one cell off, and the lift never
        // fires (the walker deadlocks at the stair edge). Nudge until our center
        // is over the stair cell, then wait a tick for apply_z_motion to change
        // our floor before consuming the node. Gated on multifloor so
        // single-floor following stays byte-identical.
        if (multifloor && GET_STATE_FLOOR(state) != floor())
        {
            const int center_cx = (xpos() + sizex() / 2) / GRID_SIZE;
            const int center_cy = (ypos() + sizey() / 2) / GRID_SIZE;
            int sdx = (GET_STATE_X(state) / GRID_SIZE) - center_cx;
            int sdy = (GET_STATE_Y(state) / GRID_SIZE) - center_cy;
            if (sdx != 0 || sdy != 0)
            {
                if (sdx != 0)
                    sdx /= abs(sdx);
                if (sdy != 0)
                    sdy /= abs(sdy);
                walkstep(sdx, sdy);
            }
            break;
        }

        // We already made it to this node, so remove it.
        path_to_foe.erase(node);
    }
}

// draw_path() has been moved to src/render/walker_draw.cpp as draw_walker_path()
