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

#include "graph.h"
#include "walker.h"
#include "micropather.h"

#define MAP_WIDTH 400
#define GRID_SIZE 16  // Should not really be duplicating this from screen.cpp

#define MAKE_STATE(x, y) (MicroPatherState)intptr_t(((y)/GRID_SIZE)*MAP_WIDTH + ((x)/GRID_SIZE))
#define GET_STATE_X(state) (intptr_t(state)%MAP_WIDTH * GRID_SIZE)
#define GET_STATE_Y(state) (intptr_t(state)/MAP_WIDTH * GRID_SIZE)
#define ALIGN_TO_GRID(x) ((x)/GRID_SIZE * GRID_SIZE)

namespace {

walker* path_walker = nullptr;

class Map : public micropather::Graph
{
public:
    float LeastCostEstimate(void* stateStart, void* stateEnd) override;
    void AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacent) override;
    void PrintStateInfo(void* state) override;
};

float Map::LeastCostEstimate(void* stateStart, void* stateEnd)
{
    int x1 = GET_STATE_X(stateStart);
    int y1 = GET_STATE_Y(stateStart);
    int x2 = GET_STATE_X(stateEnd);
    int y2 = GET_STATE_Y(stateEnd);

    return sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

void Map::AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacent)
{
    int x1 = GET_STATE_X(state);
    int y1 = GET_STATE_Y(state);

    for (int i = -1; i <= 1; i++)
    {
        for (int j = -1; j <= 1; j++)
        {
            if (i == 0 && j == 0)
                continue;

            int adj_x = x1 + i * GRID_SIZE;
            int adj_y = y1 + j * GRID_SIZE;

            micropather::StateCost cost;
            cost.state = MAKE_STATE(adj_x, adj_y);
            cost.cost = 0;

            // TODO: Make doors impassable without a key.
            // TODO: Make teleporters add another adjacent space on the other side of the teleporter.

            // Any terrain in the way?  This checks boundaries too.
            if (!myscreen->query_grid_passable(adj_x, adj_y, path_walker))
                continue;
            // Any moving objects in the way?
            else if (myscreen->level_data.myobmap->obmap_get_list(adj_x, adj_y).size() > 0)
                cost.cost = 10;
            else
                // Nothing in the way, cost is 1 for adjacent, sqrt(2) for diagonal
                cost.cost = sqrtf(i * i + j * j);

            // Smoothing heuristic using cross-product.  This penalizes going away from a straight line to the goal.
            int dx1 = adj_x - ALIGN_TO_GRID(path_walker->foe->xpos);
            int dy1 = adj_y - ALIGN_TO_GRID(path_walker->foe->ypos);
            int dx2 = path_walker->xpos - ALIGN_TO_GRID(path_walker->foe->xpos);
            int dy2 = path_walker->ypos - ALIGN_TO_GRID(path_walker->foe->ypos);
            float cross = dx1 * dy2 - dx2 * dy1;
            cost.cost += fabs(cross) * 0.01f;

            adjacent->push_back(cost);
        }
    }
}

void Map::PrintStateInfo(void* state)
{
    int x1 = GET_STATE_X(state);
    int y1 = GET_STATE_Y(state);

    Log("({},{})", x1, y1);
}

Map path_map;
micropather::MicroPather pather(&path_map);

} // namespace

void walker::find_path_to_foe()
{
    float totalCost = 0.0f;

    MicroPatherState startState = MAKE_STATE(xpos, ypos);
    MicroPatherState endState = MAKE_STATE(foe->xpos, foe->ypos);

    path_to_foe.clear();
    pather.Reset();  // Assume that the old paths are invalid
    path_walker = this;  // Set the walker that the path is being generated for
    pather.Solve(startState, endState, &path_to_foe, &totalCost);  // There's a result returned from this, but we don't need it.
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

void walker::draw_path(viewscreen* view_buf)
{
    if (path_to_foe.size() == 0)
        return;

    unsigned char mycolor = query_team_color() + intptr_t(this) % 5;  // Our pointer is a unique integer... but probably aligned to 4 bytes.

    short offsetx = view_buf->topx - view_buf->xloc - 8;
    short offsety = view_buf->topy - view_buf->yloc - 8;

    std::vector<MicroPatherState>::iterator e = path_to_foe.begin();
    int px = GET_STATE_X(*e) - offsetx;
    int py = GET_STATE_Y(*e) - offsety;
    while (e != path_to_foe.end())
    {
        int x1 = GET_STATE_X(*e) - offsetx;
        int y1 = GET_STATE_Y(*e) - offsety;

        myscreen->draw_line(px, py, x1, y1, mycolor);
        myscreen->fastbox_outline(x1 - 1, y1 - 1, 2, 2, mycolor);
        e++;
        px = x1;
        py = y1;
    }
}
