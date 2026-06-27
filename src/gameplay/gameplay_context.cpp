/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <openglad/gameplay/gameplay_context.h>

#include <openglad/gameplay/astar.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>

#include <cstdlib>

namespace
{
#define MAKE_STATE(x, y) reinterpret_cast<void*>(static_cast<intptr_t>(((y) / GRID_SIZE) * MAP_WIDTH + ((x) / GRID_SIZE)))
#define ALIGN_TO_GRID(x) ((x) / GRID_SIZE * GRID_SIZE)

constexpr float kDiagonalStepCost = 1.41421354f;
constexpr float kSqrtFixedScaleInv = 1.0f / 65536.0f;

std::uint64_t integer_root_u64(std::uint64_t value)
{
    std::uint64_t result = 0;
    std::uint64_t bit = std::uint64_t{1} << 62;

    while (bit > value)
        bit >>= 2;

    while (bit != 0)
    {
        if (value >= result + bit)
        {
            value -= result + bit;
            result = (result >> 1) + bit;
        }
        else
        {
            result >>= 1;
        }
        bit >>= 2;
    }

    return result;
}

float deterministic_path_distance(std::int32_t dx, std::int32_t dy)
{
    // Path costs feed simulation path selection, so avoid runtime libm calls.
    const std::int64_t dx64 = dx;
    const std::int64_t dy64 = dy;
    const std::uint64_t squared_distance = static_cast<std::uint64_t>(
        dx64 * dx64 + dy64 * dy64);
    const std::uint64_t fixed_distance =
        integer_root_u64(squared_distance << 32);
    return static_cast<float>(fixed_distance) * kSqrtFixedScaleInv;
}

class PathingMap final : public og::pathfinding::Graph
{
public:
    struct Owner
    {
        walker* active_walker = nullptr;
        short goal_x = 0;
        short goal_y = 0;
        bool has_goal = false;
    };

    explicit PathingMap(Owner* owner)
        : owner_(owner)
    {
    }

    float least_cost_estimate(void* state_start, void* state_end) override
    {
        const int x1 = GET_STATE_X(state_start);
        const int y1 = GET_STATE_Y(state_start);
        const int x2 = GET_STATE_X(state_end);
        const int y2 = GET_STATE_Y(state_end);

        return deterministic_path_distance(x2 - x1, y2 - y1);
    }

    void adjacent_cost(void* state,
                       std::vector<og::pathfinding::StateCost>& adjacent) override
    {
        if (!owner_ || !owner_->active_walker ||
            (owner_->active_walker->foe() == nullptr && !owner_->has_goal) ||
            current_game == nullptr || current_game->world == nullptr ||
            current_game->world->myobmap == nullptr)
        {
            return;
        }

        // Straightness bias target: the explicit goal point when one is set,
        // otherwise the owner's foe (the classic chase path).
        const int target_x = owner_->has_goal
            ? owner_->goal_x
            : owner_->active_walker->foe()->xpos();
        const int target_y = owner_->has_goal
            ? owner_->goal_y
            : owner_->active_walker->foe()->ypos();

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

                og::pathfinding::StateCost cost;
                cost.state = MAKE_STATE(adj_x, adj_y);
                cost.cost = 0;

                if (!current_game->world->query_grid_passable(
                        static_cast<float>(adj_x),
                        static_cast<float>(adj_y),
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
                    cost.cost = (i == 0 || j == 0) ? 1.0f : kDiagonalStepCost;
                }

                const int dx1 = adj_x - ALIGN_TO_GRID(target_x);
                const int dy1 = adj_y - ALIGN_TO_GRID(target_y);
                const int dx2 = owner_->active_walker->xpos() - ALIGN_TO_GRID(target_x);
                const int dy2 = owner_->active_walker->ypos() - ALIGN_TO_GRID(target_y);
                const int cross = dx1 * dy2 - dx2 * dy1;
                cost.cost += static_cast<float>(std::abs(cross)) * 0.01f;

                adjacent.push_back(cost);
            }
        }
    }

private:
    Owner* owner_ = nullptr;
};

} // namespace

struct GameplayPathfindingState::Impl
{
    PathingMap::Owner owner;
    PathingMap map{&owner};
    og::pathfinding::AStar pather{map};
};

thread_local GameplayContext* current_game = nullptr;
thread_local IRandom** g_rng_override_ref = nullptr;
thread_local IRandom** g_cosmetic_rng_override_ref = nullptr;

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

    if (!impl_ || owner == nullptr || owner->foe() == nullptr)
        return;

    impl_->owner.active_walker = owner;
    impl_->owner.has_goal = false;
    // solve() resets its own per-search state while retaining scratch capacity.
    (void)impl_->pather.solve(start_state, end_state, path_out, total_cost);
}

void GameplayPathfindingState::solve_for_point(walker* owner,
                                               short goal_x,
                                               short goal_y,
                                               void* start_state,
                                               void* end_state,
                                               std::vector<void*>& path_out,
                                               float& total_cost)
{
    path_out.clear();
    total_cost = 0.0f;

    if (!impl_ || owner == nullptr)
        return;

    impl_->owner.active_walker = owner;
    impl_->owner.goal_x = goal_x;
    impl_->owner.goal_y = goal_y;
    impl_->owner.has_goal = true;
    // solve() resets its own per-search state while retaining scratch capacity.
    (void)impl_->pather.solve(start_state, end_state, path_out, total_cost);
    impl_->owner.has_goal = false;
}

GameplayPathfindingState* ensure_pathfinding_state(GameplayContext& context)
{
    if (!context.pathfinding)
        context.pathfinding = std::make_unique<GameplayPathfindingState>();
    return context.pathfinding.get();
}

IRandom* gameplay_rng_override()
{
    if (g_rng_override_ref != nullptr)
        return *g_rng_override_ref;

    return (current_game && current_game->rng_override_ref)
        ? *current_game->rng_override_ref
        : nullptr;
}

void set_gameplay_rng_override(IRandom** rng_ref)
{
    g_rng_override_ref = rng_ref;
    if (current_game)
        current_game->rng_override_ref = rng_ref;
}

IRandom* cosmetic_rng_override()
{
    if (g_cosmetic_rng_override_ref != nullptr)
        return *g_cosmetic_rng_override_ref;

    return (current_game && current_game->cosmetic_rng_override_ref)
        ? *current_game->cosmetic_rng_override_ref
        : nullptr;
}

void set_cosmetic_rng_override(IRandom** rng_ref)
{
    g_cosmetic_rng_override_ref = rng_ref;
    if (current_game)
        current_game->cosmetic_rng_override_ref = rng_ref;
}

IRandom* cosmetic_rng()
{
    // Cosmetic override (parity harness) wins; otherwise the per-world gameplay
    // RNG so production stays deterministic for snapshot/replay/networking.
    if (IRandom* override_rng = cosmetic_rng_override())
        return override_rng;
    if (current_game != nullptr && current_game->world != nullptr)
        return &current_game->world->rng_;
    return nullptr;
}

bool gameplay_world_rng_active()
{
    return current_game == nullptr
        || current_game->gameplay_active_ref == nullptr
        || *current_game->gameplay_active_ref;
}

IRandom* gameplay_session_rng()
{
    return (current_game != nullptr && current_game->session_rng_ref != nullptr)
        ? *current_game->session_rng_ref
        : nullptr;
}
