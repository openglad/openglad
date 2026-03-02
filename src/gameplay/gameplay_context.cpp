/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/gameplay/gameplay_context.h>

#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>

#include "micropather.h"

#include <cmath>

namespace
{
#define MAKE_STATE(x, y) reinterpret_cast<void*>(static_cast<intptr_t>(((y) / GRID_SIZE) * MAP_WIDTH + ((x) / GRID_SIZE)))
#define ALIGN_TO_GRID(x) ((x) / GRID_SIZE * GRID_SIZE)

class PathingMap final : public micropather::Graph
{
public:
    struct Owner
    {
        walker* active_walker = nullptr;
    };

    explicit PathingMap(Owner* owner)
        : owner_(owner)
    {
    }

    float LeastCostEstimate(void* state_start, void* state_end) override
    {
        const int x1 = GET_STATE_X(state_start);
        const int y1 = GET_STATE_Y(state_start);
        const int x2 = GET_STATE_X(state_end);
        const int y2 = GET_STATE_Y(state_end);

        const float dx = static_cast<float>(x2 - x1);
        const float dy = static_cast<float>(y2 - y1);
        return sqrtf(dx * dx + dy * dy);
    }

    void AdjacentCost(void* state,
                      std::vector<micropather::StateCost>* adjacent) override
    {
        if (!owner_ || !owner_->active_walker ||
            owner_->active_walker->foe == nullptr ||
            current_game == nullptr || current_game->world == nullptr ||
            current_game->world->myobmap == nullptr)
        {
            return;
        }

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
                cost.cost = 0;

                if (!current_game->world->query_grid_passable(adj_x, adj_y,
                                                              owner_->active_walker))
                {
                    continue;
                }
                else if (current_game->world->myobmap
                             ->obmap_get_list(static_cast<short>(adj_x),
                                              static_cast<short>(adj_y))
                             .size() > 0)
                {
                    cost.cost = 10;
                }
                else
                {
                    cost.cost = sqrtf(static_cast<float>(i * i + j * j));
                }

                const int dx1 = adj_x - ALIGN_TO_GRID(owner_->active_walker->foe->xpos);
                const int dy1 = adj_y - ALIGN_TO_GRID(owner_->active_walker->foe->ypos);
                const int dx2 = owner_->active_walker->xpos - ALIGN_TO_GRID(owner_->active_walker->foe->xpos);
                const int dy2 = owner_->active_walker->ypos - ALIGN_TO_GRID(owner_->active_walker->foe->ypos);
                const float cross = static_cast<float>(dx1 * dy2 - dx2 * dy1);
                cost.cost += fabsf(cross) * 0.01f;

                adjacent->push_back(cost);
            }
        }
    }

    void PrintStateInfo(void* /*state*/) override {}

private:
    Owner* owner_ = nullptr;
};

} // namespace

struct GameplayPathfindingState::Impl
{
    PathingMap::Owner owner;
    PathingMap map{&owner};
    micropather::MicroPather pather{&map};
};

thread_local GameplayContext* current_game = nullptr;

GameplayPathfindingState::GameplayPathfindingState()
    : impl_(std::make_unique<Impl>())
{
}

GameplayPathfindingState::~GameplayPathfindingState() = default;

GameplayPathfindingState::GameplayPathfindingState(GameplayPathfindingState&&) noexcept = default;
GameplayPathfindingState& GameplayPathfindingState::operator=(GameplayPathfindingState&&) noexcept = default;

void GameplayPathfindingState::solve_for(walker* owner,
                                         void* start_state,
                                         void* end_state,
                                         std::vector<void*>& path_out,
                                         float& total_cost)
{
    path_out.clear();
    total_cost = 0.0f;

    if (!impl_ || owner == nullptr || owner->foe == nullptr)
        return;

    impl_->owner.active_walker = owner;
    impl_->pather.Reset();
    (void)impl_->pather.Solve(start_state, end_state, &path_out, &total_cost);
}

GameplayPathfindingState* ensure_pathfinding_state(GameplayContext& context)
{
    if (!context.pathfinding)
        context.pathfinding = std::make_unique<GameplayPathfindingState>();
    return context.pathfinding.get();
}
