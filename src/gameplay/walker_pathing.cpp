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
    // plumbing: No foe / no game / no pathfinding state: no path solved.
    if (foe() == nullptr || current_game == nullptr)
        return;

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    // plumbing: No foe / no game / no pathfinding state: no path solved.
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
    // plumbing: No foe / no game / no pathfinding state: no path solved.
    if (current_game == nullptr)
        return;

    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    // plumbing: No foe / no game / no pathfinding state: no path solved.
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

    // plumbing: Consumes reached nodes; body rules below.
    while (!path_to_foe.empty())
    {
        auto node = path_to_foe.begin();
        const PathState state = *node;
        int dx = GET_STATE_X(state) - ALIGN_TO_GRID(xpos());
        int dy = GET_STATE_Y(state) - ALIGN_TO_GRID(ypos());

        // rule: A path-follower drops every node whose grid cell its top-left corner already
        //   occupies, then takes one walk step (sign of the remaining delta per axis, possibly
        //   diagonal) toward the first unreached node; one step per tick.
        // tests: WalkerPathing.find_path_to_foe_routes_east_and_each_follow_walks_one_stepsize
        // tests: WalkerPathing.round10_follow_path_node_erase_and_normalize_paths
        // tests: SemWalker4.a_path_follower_with_no_node_left_stands_still
        if (dx != 0 || dy != 0)
        {
            // Normalize the deltas so walkstep can use them as stepsize factors.
            // rule: A path-follower drops every node whose grid cell its top-left corner already
            //   occupies, then takes one walk step (sign of the remaining delta per axis, possibly
            //   diagonal) toward the first unreached node.
            // tests: WalkerPathing.find_path_to_foe_routes_east_and_each_follow_walks_one_stepsize
            // tests: WalkerPathing.round10_follow_path_node_erase_and_normalize_paths
            // tests: SemWalker4.a_path_follower_with_no_node_left_stands_still
            if (dx != 0)
                dx /= abs(dx);
            // rule: A path-follower drops every node whose grid cell its top-left corner already
            //   occupies, then takes one walk step (sign of the remaining delta per axis, possibly
            //   diagonal) toward the first unreached node.
            // tests: WalkerPathing.find_path_to_foe_routes_east_and_each_follow_walks_one_stepsize
            // tests: WalkerPathing.round10_follow_path_node_erase_and_normalize_paths
            // tests: SemWalker4.a_path_follower_with_no_node_left_stands_still
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
            // of position. 2026-07-10: the multifloor gate is REMOVED —
            // single-floor corridor mazes (Westlands L2 "The Forest Road")
            // pin chasers in the same deterministic 2-tick corner dance the
            // gate was preserving. Audited single-floor parity rebaseline
            // (first-hit timing moves in the chase-heavy goldens; see the
            // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md row).
            // rule: Convex-corner alignment assist: when the next hop is cardinal, the body is
            //   misaligned perpendicular to it, and the direct step is blocked, the walker slides
            //   back toward exact grid alignment (one checked pixel at a time, at most stepsize and
            //   at most the misalignment) instead of stepping, so it turns one-lane corners instead
            //   of oscillating forever; if not even one pixel of slide is possible it falls back to
            //   the normal step.
            // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
            // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
            // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
            // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
            // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
            if ((dx == 0) != (dy == 0) && current_game != nullptr &&
                current_game->world != nullptr)
            {
                // rule: Convex-corner alignment assist.
                // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                const int mis = (dx != 0)
                    ? ypos() - ALIGN_TO_GRID(ypos())
                    : xpos() - ALIGN_TO_GRID(xpos());
                if (mis > 0 &&
                    !current_game->world->query_passable(
                        xpos() + static_cast<float>(dx) * stepsize(),
                        ypos() + static_cast<float>(dy) * stepsize(), this))
                {
                    int budget = static_cast<int>(stepsize());
                    // rule: Convex-corner alignment assist.
                    // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                    // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                    // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                    // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                    // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                    if (budget > mis)
                        budget = mis;
                    bool slid = false;
                    // rule: Convex-corner alignment assist.
                    // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                    // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                    // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                    // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                    // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                    const float sx = (dx != 0) ? 0.0f : -1.0f;
                    // rule: Convex-corner alignment assist.
                    // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                    // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                    // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                    // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                    // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                    const float sy = (dx != 0) ? -1.0f : 0.0f;
                    // rule: Convex-corner alignment assist.
                    // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                    // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                    // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                    // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                    // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                    for (int i = 0; i < budget; i++)
                    {
                        // rule: Convex-corner alignment assist.
                        // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                        // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                        // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                        // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                        // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
                        if (!current_game->world->query_passable(
                                xpos() + sx, ypos() + sy, this))
                            break;
                        worldmove(sx, sy);
                        slid = true;
                    }
                    // rule: Convex-corner alignment assist.
                    // tests: SingleFloorPathing.misaligned_walker_aligns_into_a_one_lane_tree_gap
                    // tests: ZAxis.misaligned_walker_aligns_and_turns_a_one_lane_corner
                    // tests: CornerAssist.a_misaligned_walker_slides_into_alignment_at_a_blocked_corner
                    // tests: CornerAssist.a_slide_blocked_at_its_first_pixel_falls_back_to_the_normal_step
                    // tests: CornerAssist.an_aligned_or_open_or_diagonal_hop_takes_the_normal_step
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
        // rule: On a multi-floor map, a reached path node on another floor is the Z-stair: the
        //   walker nudges until its CENTER is over the stair cell and then waits (does not consume
        //   the node) for the floor change, so sized walkers do not deadlock at the stair edge;
        //   single-floor maps never do this.
        // tests: ZAxis.cross_floor_ai_chases_foe_through_stair
        // tests: ZAxis.cross_floor_ai_chases_MOVING_foe_through_stair
        // tests: SemWalker4.a_walker_centers_on_a_stair_node_and_waits_on_a_multifloor_map
        if (multifloor && GET_STATE_FLOOR(state) != floor())
        {
            const int center_cx = (xpos() + sizex() / 2) / GRID_SIZE;
            const int center_cy = (ypos() + sizey() / 2) / GRID_SIZE;
            int sdx = (GET_STATE_X(state) / GRID_SIZE) - center_cx;
            int sdy = (GET_STATE_Y(state) / GRID_SIZE) - center_cy;
            // rule: On a multi-floor map, a reached path node on another floor is the Z-stair.
            // tests: ZAxis.cross_floor_ai_chases_foe_through_stair
            // tests: ZAxis.cross_floor_ai_chases_MOVING_foe_through_stair
            // tests: SemWalker4.a_walker_centers_on_a_stair_node_and_waits_on_a_multifloor_map
            if (sdx != 0 || sdy != 0)
            {
                // rule: On a multi-floor map, a reached path node on another floor is the Z-stair.
                // tests: ZAxis.cross_floor_ai_chases_foe_through_stair
                // tests: ZAxis.cross_floor_ai_chases_MOVING_foe_through_stair
                // tests: SemWalker4.a_walker_centers_on_a_stair_node_and_waits_on_a_multifloor_map
                if (sdx != 0)
                    sdx /= abs(sdx);
                // rule: On a multi-floor map, a reached path node on another floor is the Z-stair.
                // tests: ZAxis.cross_floor_ai_chases_foe_through_stair
                // tests: ZAxis.cross_floor_ai_chases_MOVING_foe_through_stair
                // tests: SemWalker4.a_walker_centers_on_a_stair_node_and_waits_on_a_multifloor_map
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
