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
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>

#include <cstdlib>

namespace
{
#define MAKE_STATE(x, y, f) reinterpret_cast<void*>(static_cast<intptr_t>(static_cast<intptr_t>(f) * FLOOR_STRIDE + ((y) / GRID_SIZE) * MAP_WIDTH + ((x) / GRID_SIZE)))
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

        float h = deterministic_path_distance(x2 - x1, y2 - y1);
        // Admissible cross-floor term: 0 when start/end share a floor (always
        // the case on single-floor levels → byte-identical heuristic).
        const int f1 = GET_STATE_FLOOR(state_start);
        const int f2 = GET_STATE_FLOOR(state_end);
        if (f1 != f2)
            h += static_cast<float>(std::abs(f1 - f2)) *
                 static_cast<float>(GRID_SIZE);
        return h;
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

        walker* const aw = owner_->active_walker;
        const bool multifloor = current_game->world->floor_count() > 1;
        const int f = multifloor ? GET_STATE_FLOOR(state) : 0;
        const bool nonflyer = !(aw->stats()->query_bit_flags(BIT_FLYING) ||
                                aw->flight_left());

        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                if (i == 0 && j == 0)
                    continue;

                const int adj_x = x1 + i * GRID_SIZE;
                const int adj_y = y1 + j * GRID_SIZE;

                og::pathfinding::StateCost cost;
                cost.state = MAKE_STATE(adj_x, adj_y, f);
                cost.cost = 0;

                if (!current_game->world->query_grid_passable(
                        static_cast<float>(adj_x),
                        static_cast<float>(adj_y),
                                                              aw, f))
                {
                    continue;
                }
                // Non-flyers avoid "air" holes in pathing (they would fall), so
                // AI routes around pits and only changes floors via Z-stairs.
                // Gated on multifloor so single-floor expansion is unchanged.
                if (multifloor && nonflyer &&
                    current_game->world->smoother_for_floor(f).query_genre_x_y(
                        adj_x / GRID_SIZE, adj_y / GRID_SIZE) == TYPE_AIR)
                {
                    continue;
                }
                // No corner cutting (2026-07, deliberate fix; see
                // docs/GAMEPLAY_FIXES_FROM_CLASSIC.md). A full-cell body can
                // only make a diagonal cell transition when BOTH flanking
                // orthogonal cells are open: its swept box necessarily
                // overlaps them, so a diagonal past a blocked flank is
                // pixel-impassable at EVERY offset. The classic graph emitted
                // such hops anyway; when the map pinched (lava + scree
                // boulder in an X around the hop) the follower seized on an
                // edge it could never take — permanently (the L24 terrace
                // wedge). Flanks impose the same rules as targets: grid
                // passability plus the non-flyer air skip (a body swept over
                // an air flank can clip its center into the hole and fall).
                // 2026-07-10: the multifloor gate is REMOVED — single-floor
                // corridor mazes (Westlands L2 "The Forest Road") exhibit the
                // same permanent corner seizures the gate was preserving, so
                // corner-cut edges are now pruned on every level. This is an
                // audited single-floor parity rebaseline (see the doc row);
                // the rule consumes no RNG, so drift is position/timing only.
                if (i != 0 && j != 0)
                {
                    bool flanks_open = true;
                    const int flank[2][2] = {{x1 + i * GRID_SIZE, y1},
                                             {x1, y1 + j * GRID_SIZE}};
                    for (const auto& fc : flank)
                    {
                        if (!current_game->world->query_grid_passable(
                                static_cast<float>(fc[0]),
                                static_cast<float>(fc[1]), aw, f) ||
                            (nonflyer &&
                             current_game->world->smoother_for_floor(f)
                                     .query_genre_x_y(fc[0] / GRID_SIZE,
                                                      fc[1] / GRID_SIZE) ==
                                 TYPE_AIR))
                        {
                            flanks_open = false;
                            break;
                        }
                    }
                    if (!flanks_open)
                        continue;
                }
                if (current_game->world->myobmap
                             ->obmap_get_list(static_cast<short>(adj_x),
                                              static_cast<short>(adj_y), f)
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

        // Cross-floor Z-stair edge: if the current cell is a stair tile, add a
        // vertical neighbor on the connected floor (positional: UP→f+1,
        // DOWN→f-1). Non-flyers only; gated on multifloor so single-floor A*
        // expansion is byte-identical.
        if (multifloor && nonflyer)
        {
            const int tile = current_game->world->smoother_for_floor(f)
                .query_x_y(x1 / GRID_SIZE, y1 / GRID_SIZE);
            int tf = -1;
            if (tile == PIX_ZSTAIR_UP &&
                (f + 1) < current_game->world->floor_count())
                tf = f + 1;
            else if (tile == PIX_ZSTAIR_DOWN && f > 0)
                tf = f - 1;
            if (tf >= 0 &&
                current_game->world->query_grid_passable(
                    static_cast<float>(x1), static_cast<float>(y1), aw, tf))
            {
                og::pathfinding::StateCost up;
                up.state = MAKE_STATE(x1, y1, tf);
                up.cost = 1.0f;
                adjacent.push_back(up);
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
