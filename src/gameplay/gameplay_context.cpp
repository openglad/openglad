#include <openglad/gameplay/gameplay_context.h>

#include <openglad/entities/pathfinding_grid.h>
#include <openglad/entities/obmap.h>
#include <openglad/entities/walker.h>
#include <openglad/gameplay/game_world.h>

#include "micropather.h"

#include <cmath>
#include <cstdint>

#define MAKE_STATE(x, y) reinterpret_cast<void*>(static_cast<intptr_t>(((y)/GRID_SIZE)*MAP_WIDTH + ((x)/GRID_SIZE)))
#define ALIGN_TO_GRID(x) ((x)/GRID_SIZE * GRID_SIZE)

namespace og::gameplay {

thread_local GameplayContext* current_game = nullptr;

namespace detail {

class PathMap : public micropather::Graph {
public:
    walker* actor = nullptr;

    float LeastCostEstimate(void* stateStart, void* stateEnd) override
    {
        const int x1 = GET_STATE_X(stateStart);
        const int y1 = GET_STATE_Y(stateStart);
        const int x2 = GET_STATE_X(stateEnd);
        const int y2 = GET_STATE_Y(stateEnd);

        const float dx = static_cast<float>(x2 - x1);
        const float dy = static_cast<float>(y2 - y1);
        return sqrtf(dx * dx + dy * dy);
    }

    void AdjacentCost(void* state, std::vector<micropather::StateCost>* adjacent) override
    {
        if (!current_game || !current_game->world || !actor)
            return;

        const int x1 = GET_STATE_X(state);
        const int y1 = GET_STATE_Y(state);

        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                if (i == 0 && j == 0)
                    continue;

                const int adj_x = x1 + i * GRID_SIZE;
                const int adj_y = y1 + j * GRID_SIZE;

                micropather::StateCost cost;
                cost.state = MAKE_STATE(adj_x, adj_y);
                cost.cost = 0.0f;

                if (!current_game->world->query_grid_passable(adj_x, adj_y, actor))
                    continue;

                if (!current_game->world->myobmap->obmap_get_list(static_cast<short>(adj_x), static_cast<short>(adj_y)).empty())
                    cost.cost = 10.0f;
                else
                    cost.cost = sqrtf(static_cast<float>(i * i + j * j));

                const int dx1 = adj_x - ALIGN_TO_GRID(actor->foe->xpos);
                const int dy1 = adj_y - ALIGN_TO_GRID(actor->foe->ypos);
                const int dx2 = actor->xpos - ALIGN_TO_GRID(actor->foe->xpos);
                const int dy2 = actor->ypos - ALIGN_TO_GRID(actor->foe->ypos);
                const float cross = static_cast<float>(dx1 * dy2 - dx2 * dy1);
                cost.cost += fabsf(cross) * 0.01f;

                adjacent->push_back(cost);
            }
        }
    }

    void PrintStateInfo(void* state) override
    {
        (void)state;
    }
};

class PathfindingStateImpl {
public:
    PathMap map;
    micropather::MicroPather pather{&map};
};

} // namespace detail

PathfindingState::PathfindingState()
    : impl_(std::make_unique<detail::PathfindingStateImpl>())
{
}

PathfindingState::~PathfindingState() = default;

void PathfindingState::reset()
{
    impl_->pather.Reset();
}

void PathfindingState::solve_path(walker* actor, std::vector<void*>& path_out, float& total_cost)
{
    path_out.clear();

    if (!actor || !actor->foe || !current_game || !current_game->world)
        return;

    impl_->map.actor = actor;
    impl_->pather.Reset();

    void* start_state = MAKE_STATE(actor->xpos, actor->ypos);
    void* end_state = MAKE_STATE(actor->foe->xpos, actor->foe->ypos);
    impl_->pather.Solve(start_state, end_state, &path_out, &total_cost);
}

} // namespace og::gameplay
