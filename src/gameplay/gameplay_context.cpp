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
#include <openglad/gameplay/sim_emit.h>
#include <openglad/gameplay/sound_source.h>
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

    // plumbing: integer square root for a libm-free deterministic path distance; no game rule
    while (bit > value)
        bit >>= 2;

    // plumbing: integer square root for a libm-free deterministic path distance; no game rule
    while (bit != 0)
    {
        // plumbing: integer square root for a libm-free deterministic path distance; no game rule
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
        const og::reach::RouteCells* cells = nullptr;  // a bot's route (Gameplay Fixes BI), else none
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
        // rule: The A* estimate adds one cell's width (16) per floor of difference, so a
        //   cross-floor target looks farther than a same-floor one at the same 2D spot;
        //   single-floor levels add nothing.
        // tests: SemPathing.classic_near_greedy_estimate_adds_a_cell_per_floor_between_floors_and_none_within_one
        if (f1 != f2)
            h += static_cast<float>(std::abs(f1 - f2)) *
                 static_cast<float>(GRID_SIZE);
        return h;
    }

    void adjacent_cost(void* state,
                       std::vector<og::pathfinding::StateCost>& adjacent) override
    {
        // plumbing: no walker / no target (no foe and no goal) / no world or obmap: no neighbours,
        //   so the solve finds no path
        if (!owner_ || !owner_->active_walker ||
            (owner_->active_walker->foe() == nullptr && !owner_->has_goal) ||
            current_game == nullptr || current_game->world == nullptr ||
            current_game->world->myobmap == nullptr)
        {
            return;
        }

        // Straightness-bias target: the explicit goal point when one is set,
        // otherwise the owner's foe (the classic chase path). The
        // cross-product below penalizes moving away from a straight line to
        // this target.
        // rule: Path steps that stray from the straight line between the walker and its target (a
        //   goal point when one is set, else its foe) cost a little more, so the AI prefers
        //   straight routes; the classic game biases toward the foe only.
        // tests: SemPathing.path_steps_are_biased_toward_the_goal_point_when_set_else_the_foe
        const int target_x = owner_->has_goal
            ? owner_->goal_x
            : owner_->active_walker->foe()->xpos();
        const int target_y = owner_->has_goal
            ? owner_->goal_y
            : owner_->active_walker->foe()->ypos();

        const int x1 = GET_STATE_X(state);
        const int y1 = GET_STATE_Y(state);

        walker* const aw = owner_->active_walker;
        GameWorld& world = *current_game->world;
        const bool multifloor = world.floor_count() > 1;
        // rule: Pathing on a single-floor level always plans on floor 0; on stacked levels it plans
        //   on the state's floor.
        // tests: SemPathing.pathing_plans_on_the_states_floor_when_stacked_and_on_floor_0_otherwise
        const int f = multifloor ? GET_STATE_FLOOR(state) : 0;
        const bool nonflyer =
            !aw->flies();

        // The passability half of the neighbour rule is written once, in reach.h
        // (og::reach::for_each_step: the eight directions, the flank rule, the air test and the
        // stair hop), and the maps of og::reach number their regions with it; asked here with the
        // terrain's answers. This half prices each step it answers.
        const auto terrain = [&](int x, int y, int cf, og::reach::StepTest test) {
            return og::reach::terrain_passes(world, *aw, x, y, cf, test, multifloor, nonflyer);
        };
        const auto price = [&](int adj_x, int adj_y, int af, og::reach::StepKind kind) {
            // rule: A stair's hop costs 1 and pays no straightness penalty (its two cells are the
            //   same 2D spot on two floors).
            // tests: SemPathing.a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor
            if (kind == og::reach::StepKind::Stair)
            {
                og::pathfinding::StateCost up;
                up.state = MAKE_STATE(adj_x, adj_y, af);
                up.cost = 1.0f;
                adjacent.push_back(up);
                return;
            }

            og::pathfinding::StateCost cost;
            cost.state = MAKE_STATE(adj_x, adj_y, af);
            cost.cost = 0;

            // Any moving objects in the way?
            const std::list<walker*>* pile =
                world.myobmap->find_list(
                    static_cast<short>(adj_x), static_cast<short>(adj_y), af);
            // rule: A cell holding any body costs 10 to path through (the AI prefers going
            //   around crowds but will push through); an empty cell costs 1 straight, 1.414
            //   diagonal.
            // tests: GameContext.pathfinding_state_supports_move_construction_and_assignment
            // tests: WalkerPathing.direct_solver_returns_expected_route_and_cost
            // tests: SemPathing.a_cell_holding_a_body_costs_ten_to_path_through
            if (pile != nullptr && !pile->empty())
            {
                // TODO: Make doors impassable without a key.
                // TODO: Add an adjacent edge to a teleporter's far side.
                cost.cost = 10;
            }
            else
            {
                // Nothing in the way, cost is 1 for adjacent, sqrt(2) for diagonal
                // rule: A cell holding any body costs 10 to path through (the AI prefers going
                //   around crowds but will push through); an empty cell costs 1 straight, 1.414
                //   diagonal.
                // tests: GameContext.pathfinding_state_supports_move_construction_and_assignment
                // tests: WalkerPathing.direct_solver_returns_expected_route_and_cost
                // tests: SemPathing.a_cell_holding_a_body_costs_ten_to_path_through
                if (kind == og::reach::StepKind::Straight)
                    cost.cost = 1.0f;
                else
                    cost.cost = kDiagonalStepCost;
            }

            const int dx1 = adj_x - ALIGN_TO_GRID(target_x);
            const int dy1 = adj_y - ALIGN_TO_GRID(target_y);
            const int dx2 = owner_->active_walker->xpos() - ALIGN_TO_GRID(target_x);
            const int dy2 = owner_->active_walker->ypos() - ALIGN_TO_GRID(target_y);
            const int cross = dx1 * dy2 - dx2 * dy1;
            cost.cost += static_cast<float>(std::abs(cross)) * 0.01f;

            adjacent.push_back(cost);
        };
        // rule: Gameplay Fixes BI, a door a bot cannot open is a wall to it: a bot's route (the
        //   setting on) reads its steps from its kind's shut layer (og::reach::RouteCells), so a door
        //   that bars it is a wall to the route; every other solve reads the terrain's answers. The
        //   costs are the same: a door the walker can open still costs ten, as any body does.
        // tests: SemBotReach.the_route_goes_round_a_door_fence_and_never_through_it
        // tests: SemPathing.a_door_the_walker_can_open_costs_ten_and_is_walked
        if (owner_->cells != nullptr)
            og::reach::for_each_step(world, x1, y1, f, multifloor, nonflyer, *owner_->cells, price);
        else
            og::reach::for_each_step(world, x1, y1, f, multifloor, nonflyer, terrain, price);
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
                                         float& total_cost,
                                         const og::reach::RouteCells* cells)
{
    path_out.clear();
    total_cost = 0.0f;

    // plumbing: solve_for with no foe: empty path, cost 0 (chase pathing needs a foe)
    if (!impl_ || owner == nullptr || owner->foe() == nullptr)
        return;

    // Set the walker that the path is being generated for
    impl_->owner.active_walker = owner;
    impl_->owner.has_goal = false;
    impl_->owner.cells = cells;
    // solve() resets its own per-search state while retaining scratch capacity.
    (void)impl_->pather.solve(start_state, end_state, path_out, total_cost);
    impl_->owner.cells = nullptr;
}

void GameplayPathfindingState::solve_for_point(walker* owner,
                                               short goal_x,
                                               short goal_y,
                                               void* start_state,
                                               void* end_state,
                                               std::vector<void*>& path_out,
                                               float& total_cost,
                                               const og::reach::RouteCells* cells)
{
    path_out.clear();
    total_cost = 0.0f;

    // plumbing: solve_for_point null owner guard
    if (!impl_ || owner == nullptr)
        return;

    impl_->owner.active_walker = owner;
    impl_->owner.goal_x = goal_x;
    impl_->owner.goal_y = goal_y;
    impl_->owner.has_goal = true;
    impl_->owner.cells = cells;
    // solve() resets its own per-search state while retaining scratch capacity.
    (void)impl_->pather.solve(start_state, end_state, path_out, total_cost);
    impl_->owner.has_goal = false;
    impl_->owner.cells = nullptr;
}

GameplayPathfindingState* ensure_pathfinding_state(GameplayContext& context)
{
    // plumbing: lazy creation of the per-context pathfinding state
    if (!context.pathfinding)
        context.pathfinding = std::make_unique<GameplayPathfindingState>();
    return context.pathfinding.get();
}

IRandom* gameplay_rng_override()
{
    // plumbing: thread-local RNG override (parity harness / tests) wins over the context override
    if (g_rng_override_ref != nullptr)
        return *g_rng_override_ref;

    // plumbing: the context's RNG override (tests) when one is installed, else none.
    return (current_game && current_game->rng_override_ref)
        ? *current_game->rng_override_ref
        : nullptr;
}

void set_gameplay_rng_override(IRandom** rng_ref)
{
    g_rng_override_ref = rng_ref;
    // plumbing: mirror the override into the current context when one exists
    if (current_game)
        current_game->rng_override_ref = rng_ref;
}

IRandom* cosmetic_rng_override()
{
    // plumbing: thread-local RNG override (parity harness / tests) wins over the context override
    if (g_cosmetic_rng_override_ref != nullptr)
        return *g_cosmetic_rng_override_ref;

    // plumbing: the context's cosmetic RNG override (tests) when one is installed, else none.
    return (current_game && current_game->cosmetic_rng_override_ref)
        ? *current_game->cosmetic_rng_override_ref
        : nullptr;
}

void set_cosmetic_rng_override(IRandom** rng_ref)
{
    g_cosmetic_rng_override_ref = rng_ref;
    // plumbing: mirror the override into the current context when one exists
    if (current_game)
        current_game->cosmetic_rng_override_ref = rng_ref;
}

IRandom* cosmetic_rng()
{
    // Cosmetic override (parity harness) wins; otherwise the per-world gameplay
    // RNG so production stays deterministic for snapshot/replay/networking.
    // plumbing: cosmetic_rng: override, else the world's rng_, else nullptr between levels;
    //   asserted by tests/unit/test_sim_entity.cpp TEST(SimEntity,
    //   cosmetic_rng_follows_the_installed_world).
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
    // plumbing: the session RNG when the context carries one, else none.
    return (current_game != nullptr && current_game->session_rng_ref != nullptr)
        ? *current_game->session_rng_ref
        : nullptr;
}

namespace og::sim {

std::uint32_t sound_source_of(const walker* source)
{
    // plumbing: no walker to place the sound at: it goes out unplaced, heard by every machine
    if (source == nullptr)
        return 0;
    return pack_sound_source(source->xpos(), source->ypos(),
                             source->sizex(), source->sizey());
}

} // namespace og::sim
