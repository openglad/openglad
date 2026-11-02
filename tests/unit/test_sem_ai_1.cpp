/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch ai-1: the rules the review found in astar.cpp, gameplay_context.cpp, guy.cpp, living.cpp.
// Each test is named after the rule it asserts.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/astar.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// The A* solver on an explicit graph: states are small integers, every edge
// and every estimate is spelled out, and the graph records the order in which
// the solver expands states (adjacent_cost is called once per expansion).
// ---------------------------------------------------------------------------

using og::pathfinding::AStar;
using og::pathfinding::SolveResult;
using og::pathfinding::State;
using og::pathfinding::StateCost;

State st(int id) { return reinterpret_cast<State>(static_cast<std::intptr_t>(id)); }
int id_of(State s) { return static_cast<int>(reinterpret_cast<std::intptr_t>(s)); }

class ListGraph final : public og::pathfinding::Graph
{
public:
    void edge(int from, int to, float cost) { adj_[from].push_back({to, cost}); }
    void estimate(int id, float h) { h_[id] = h; }

    float least_cost_estimate(State a, State) override
    {
        const auto it = h_.find(id_of(a));
        return it == h_.end() ? 0.0f : it->second;
    }

    void adjacent_cost(State s, std::vector<StateCost>& out) override
    {
        expanded.push_back(id_of(s));
        const auto it = adj_.find(id_of(s));
        if (it == adj_.end())
            return;
        for (const auto& [to, cost] : it->second)
            out.push_back({st(to), cost});
    }

    std::vector<int> expanded;

private:
    std::map<int, std::vector<std::pair<int, float>>> adj_;
    std::map<int, float> h_;
};

std::vector<int> ids(const std::vector<State>& path)
{
    std::vector<int> out;
    for (State s : path)
        out.push_back(id_of(s));
    return out;
}

} // namespace

// astar.cpp:85 (OpenQueue::push). Two frontier cells at the same cost are
// expanded in the order they were found.
TEST(SemAStar, equally_cheap_frontier_cells_expand_in_the_order_they_were_found)
{
    constexpr int S = 1, A = 2, B = 3, E = 9; // E is unreachable: every state expands
    {
        ListGraph g;
        g.edge(S, A, 1.0f);
        g.edge(S, B, 1.0f);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        EXPECT_EQ(SolveResult::NoSolution, pather.solve(st(S), st(E), path, cost));
        EXPECT_EQ((std::vector<int>{S, A, B}), g.expanded)
            << "tie with an existing run: B, found after A at the same cost, "
               "must queue behind A";
    }
    {
        ListGraph g;
        g.edge(S, B, 1.0f);
        g.edge(S, A, 1.0f);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        EXPECT_EQ(SolveResult::NoSolution, pather.solve(st(S), st(E), path, cost));
        EXPECT_EQ((std::vector<int>{S, B, A}), g.expanded)
            << "tie with an existing run: listed first, B is found first and "
               "expands first";
    }
}

// astar.cpp:108/116 (OpenQueue::update). A frontier cell a relaxation makes
// cheaper moves up the queue: to the front of the run of cells at its new
// cost (ahead of the cells already there), past every strictly cheaper one.
TEST(SemAStar, a_relaxed_frontier_cell_moves_to_the_front_of_its_new_cost_run)
{
    // S reaches M (1), P (2), R (3) and X (5); M then reaches X again for w.
    constexpr int S = 1, M = 2, P = 3, R = 4, X = 5, E = 9;
    auto order_for = [&](float w) {
        ListGraph g;
        g.edge(S, M, 1.0f);
        g.edge(S, P, 2.0f);
        g.edge(S, R, 3.0f);
        g.edge(S, X, 5.0f);
        g.edge(M, X, w);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        EXPECT_EQ(SolveResult::NoSolution, pather.solve(st(S), st(E), path, cost));
        return g.expanded;
    };
    EXPECT_EQ((std::vector<int>{S, M, X, P, R}), order_for(0.5f))
        << "relaxed to 1.5, below every queued cell: X goes to the head";
    EXPECT_EQ((std::vector<int>{S, M, X, P, R}), order_for(1.0f))
        << "relaxed to 2, P's cost: X goes AHEAD of P, to the front of the "
           "cost-2 run";
    EXPECT_EQ((std::vector<int>{S, M, P, X, R}), order_for(1.5f))
        << "relaxed to 2.5: X hops to the head, then slides right past the "
           "cheaper P and stops before R";
}

// astar.cpp:271/276/285. A frontier cell is re-parented only by a strictly
// cheaper route; an expanded cell takes the cheaper cost and parent but is
// never expanded again; a route that is merely as cheap changes nothing.
TEST(SemAStar, a_cell_is_reparented_only_by_a_strictly_cheaper_route_and_never_reopened)
{
    {
        // Open, strictly cheaper: X is queued at 10 from S, then reached at 2
        // through Y.
        constexpr int S = 1, X = 2, Y = 3, E = 4;
        ListGraph g;
        g.edge(S, X, 10.0f);
        g.edge(S, Y, 1.0f);
        g.edge(Y, X, 1.0f);
        g.edge(X, E, 1.0f);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        ASSERT_EQ(SolveResult::Solved, pather.solve(st(S), st(E), path, cost));
        EXPECT_EQ((std::vector<int>{S, Y, X, E}), ids(path))
            << "open, strictly cheaper: X must be re-parented to Y";
        EXPECT_FLOAT_EQ(3.0f, cost) << "open, strictly cheaper: X's cost drops to 2";
    }
    {
        // Closed, strictly cheaper: A (estimate 0) is expanded at 5 before B
        // (estimate 10) pops and reaches it at 2.
        constexpr int S = 1, A = 2, B = 3, E = 4;
        ListGraph g;
        g.estimate(B, 10.0f);
        g.edge(S, A, 5.0f);
        g.edge(S, B, 1.0f);
        g.edge(B, A, 1.0f);
        g.edge(A, E, 10.0f);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        ASSERT_EQ(SolveResult::Solved, pather.solve(st(S), st(E), path, cost));
        EXPECT_EQ((std::vector<int>{S, B, A, E}), ids(path))
            << "closed, strictly cheaper: the expanded A still takes B as its parent";
        EXPECT_FLOAT_EQ(15.0f, cost)
            << "closed, strictly cheaper: A is NOT reopened, so E keeps the cost "
               "it got from A at 5 (a reopen would give 12)";
        EXPECT_EQ((std::vector<int>{S, A, B}), g.expanded)
            << "closed, strictly cheaper: A is expanded once";
    }
    {
        // Not cheaper: E is reached at 2 through A, then at 2 through B.
        constexpr int S = 1, A = 2, B = 3, E = 4;
        ListGraph g;
        g.edge(S, A, 1.0f);
        g.edge(S, B, 1.0f);
        g.edge(A, E, 1.0f);
        g.edge(B, E, 1.0f);
        AStar pather(g);
        std::vector<State> path;
        float cost = -1.0f;
        ASSERT_EQ(SolveResult::Solved, pather.solve(st(S), st(E), path, cost));
        EXPECT_EQ((std::vector<int>{S, A, E}), ids(path))
            << "not cheaper: an equally cheap route through B leaves E's parent A";
        EXPECT_FLOAT_EQ(2.0f, cost);
    }
}

// ---------------------------------------------------------------------------
// The game's pathing graph (PathingMap in gameplay_context.cpp), solved
// directly through GameplayPathfindingState on a painted world.
// ---------------------------------------------------------------------------

namespace {

constexpr float kDiag = 1.41421354f;

PathState cell_state(int cx, int cy, int floor)
{
    return reinterpret_cast<PathState>(
        static_cast<std::intptr_t>(floor) * FLOOR_STRIDE +
        static_cast<std::intptr_t>(cy) * MAP_WIDTH + cx);
}

struct Cell
{
    int x;
    int y;
    int f;
    bool operator==(const Cell& o) const { return x == o.x && y == o.y && f == o.f; }
};

std::ostream& operator<<(std::ostream& os, const Cell& c)
{
    return os << "(" << c.x << "," << c.y << ",f" << c.f << ")";
}

std::vector<Cell> cells(const std::vector<PathState>& path)
{
    std::vector<Cell> out;
    for (PathState s : path)
        out.push_back({GET_STATE_X(s) / GRID_SIZE, GET_STATE_Y(s) / GRID_SIZE,
                       GET_STATE_FLOOR(s)});
    return out;
}

std::vector<Cell> row(int y, int x0, int x1, int f)
{
    std::vector<Cell> out;
    const int step = x0 <= x1 ? 1 : -1;
    for (int x = x0;; x += step) {
        out.push_back({x, y, f});
        if (x == x1)
            break;
    }
    return out;
}

std::vector<Cell> column(int x, int y0, int y1, int f)
{
    std::vector<Cell> out;
    const int step = y0 <= y1 ? 1 : -1;
    for (int y = y0;; y += step) {
        out.push_back({x, y, f});
        if (y == y1)
            break;
    }
    return out;
}

std::vector<Cell> join(std::vector<Cell> a, const std::vector<Cell>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

class SemPathing : public ::testing::Test
{
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        tw_.reset(); // the old world's context guard must unwind first
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(40, 40);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * static_cast<std::size_t>(w.grid.h),
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.rng_.state_ = 12345u;
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int f, int cx, int cy, unsigned char pix)
    {
        PixieData& g = world().grid_for_floor(f);
        g.data[static_cast<std::size_t>(cx + g.w * cy)] = pix;
    }

    // Two floors; floor 1 a copy-sized field of `fill`.
    void two_floors(unsigned char fill = PIX_GRASS1)
    {
        GameWorld& w = world();
        w.set_floor_count(2);
        const int gw = w.grid.w;
        const int gh = w.grid.h;
        const std::size_t n = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh);
        auto* buf = new unsigned char[n];
        std::fill(buf, buf + n, fill);
        w.grid_for_floor(1) = PixieData(1, static_cast<unsigned char>(gw),
                                        static_cast<unsigned char>(gh), buf);
        w.smoother_for_floor(1).set_target(w.grid_for_floor(1));
    }

    // A soldier standing at cell (cx,cy) of floor f, NOT in the obmap (so its
    // own body never prices a cell of its path).
    walker* actor_at(int cx, int cy, int f, int team = 0)
    {
        walker* a = world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (a == nullptr)
            return nullptr;
        a->set_team_num(static_cast<unsigned char>(team));
        a->set_real_team_num(255);
        a->set_floor(static_cast<short>(f));
        a->setxy(static_cast<short>(cx * GRID_SIZE), static_cast<short>(cy * GRID_SIZE));
        (void)world().myobmap->remove(a);
        return a;
    }

    GameplayPathfindingState& pathing()
    {
        return *ensure_pathfinding_state(*current_game);
    }

    // solve_for_point from the actor's cell to (gx,gy,gf), goal = that cell.
    std::vector<Cell> solve_to(walker* a, int gx, int gy, int gf, float& cost,
                               int start_floor = -1)
    {
        std::vector<PathState> path;
        cost = -1.0f;
        const int sf = start_floor >= 0 ? start_floor : a->floor();
        pathing().solve_for_point(a, static_cast<short>(gx * GRID_SIZE),
                                  static_cast<short>(gy * GRID_SIZE),
                                  cell_state(a->xpos() / GRID_SIZE, a->ypos() / GRID_SIZE, sf),
                                  cell_state(gx, gy, gf), path, cost);
        return cells(path);
    }
};

} // namespace

// gameplay_context.cpp:116. The straightness bias of a path step aims at the
// goal point when the solve has one, and at the walker's foe otherwise.
TEST_F(SemPathing, path_steps_are_biased_toward_the_goal_point_when_set_else_the_foe)
{
    walker* a = actor_at(2, 2, 0);
    walker* foe = world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, foe);
    foe->set_team_num(1);
    foe->setxy(12 * GRID_SIZE, 2 * GRID_SIZE);
    (void)world().myobmap->remove(foe);
    a->set_foe(foe);

    // Goal arm first, so a foe solve that wrongly read the goal would see a
    // stale one.
    float cost = 0.0f;
    const std::vector<Cell> south = solve_to(a, 2, 12, 0, cost);
    EXPECT_EQ(column(2, 2, 12, 0), south)
        << "goal point set: the path runs straight down to the goal";
    EXPECT_FLOAT_EQ(10.0f, cost)
        << "goal point set: every step lies on the goal line, so no step pays "
           "a straightness penalty (aimed at the foe, each would)";

    std::vector<PathState> path;
    cost = -1.0f;
    pathing().solve_for(a, cell_state(2, 2, 0), cell_state(12, 2, 0), path, cost);
    EXPECT_EQ(row(2, 2, 12, 0), cells(path))
        << "no goal: the path runs straight east to the foe";
    EXPECT_FLOAT_EQ(10.0f, cost)
        << "no goal: every step lies on the foe line, so no step pays a "
           "straightness penalty (aimed at the old goal, each would)";
}

// gameplay_context.cpp:95. The A* estimate adds one cell (16) per floor of
// difference and nothing between states of the same floor. With the start on
// an up-stair and the near cell east of it crowded (cost 10), the floor term
// makes climbing at once (cost 11) look better than walking floor 0 to the far
// stair (cost 20); without it the near-greedy search takes the long way.
TEST_F(SemPathing, the_path_estimate_adds_a_cell_per_floor_between_floors_and_nothing_within_one)
{
    two_floors();
    tile(0, 5, 8, PIX_ZSTAIR_UP);
    tile(0, 14, 8, PIX_ZSTAIR_UP);
    walker* a = actor_at(5, 8, 0);
    walker* crowd = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, crowd);
    crowd->set_floor(0);
    crowd->setxy(static_cast<short>(6 * GRID_SIZE), static_cast<short>(8 * GRID_SIZE));

    float cost = 0.0f;
    const std::vector<Cell> path = solve_to(a, 15, 8, 1, cost);
    EXPECT_EQ(join({{5, 8, 0}}, row(8, 5, 15, 1)), path)
        << "floor-0 states carry +16 toward a floor-1 goal and floor-1 states "
           "carry nothing: the search climbs at the start";
    EXPECT_FLOAT_EQ(11.0f, cost) << "one climb and ten steps on floor 1";
}

// gameplay_context.cpp:128. On stacked levels the graph plans on the state's
// floor; on a single-floor level it always plans on floor 0.
TEST_F(SemPathing, pathing_plans_on_the_states_floor_when_stacked_and_on_floor_0_otherwise)
{
    {
        two_floors();
        std::fill_n(world().grid.data.get(),
                    static_cast<std::size_t>(world().grid.w) * static_cast<std::size_t>(world().grid.h),
                    static_cast<std::uint8_t>(PIX_H_WALL1));
        walker* a = actor_at(2, 2, 1);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        EXPECT_EQ(row(2, 2, 8, 1), solve_to(a, 8, 2, 1, cost))
            << "floor_count>1: the floor-1 grass is planned on, not floor 0's walls";
        EXPECT_FLOAT_EQ(6.0f, cost);
    }
    {
        SetUp(); // a fresh single-floor field
        walker* a = actor_at(2, 2, 0);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        // A start state carrying floor-1 bits: every neighbour is planned on
        // floor 0, so the floor-0 goal is reached.
        const std::vector<Cell> path = solve_to(a, 8, 2, 0, cost, 1);
        EXPECT_EQ(join({{2, 2, 1}}, row(2, 3, 8, 0)), path)
            << "single floor: every planned step is on floor 0 whatever the "
               "state's floor bits";
        EXPECT_FLOAT_EQ(6.0f, cost);
    }
}

// gameplay_context.cpp:157. On stacked levels a walker that cannot fly never
// plans a step onto air; a flyer may; single-floor levels have no such rule.
TEST_F(SemPathing, a_non_flyer_never_plans_a_step_onto_air_on_stacked_levels)
{
    auto paint_air_row = [&](int f) {
        for (int x = 0; x < 40; ++x)
            if (x != 18)
                tile(f, x, 6, PIX_AIR);
    };
    {
        two_floors();
        paint_air_row(1);
        walker* a = actor_at(4, 4, 1);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        const std::vector<Cell> path = solve_to(a, 4, 10, 1, cost);
        ASSERT_FALSE(path.empty()) << "the gap at x=18 leads round";
        int on_air = 0;
        int through_gap = 0;
        for (const Cell& c : path) {
            on_air += (c.y == 6 && c.x != 18) ? 1 : 0;
            through_gap += (c == Cell{18, 6, 1}) ? 1 : 0;
        }
        EXPECT_EQ(0, on_air) << "non-flyer, air cell: no planned step is on the air row";
        EXPECT_EQ(1, through_gap) << "non-flyer: the path crosses row 6 at the gap";
    }
    {
        SetUp();
        two_floors();
        paint_air_row(1);
        walker* a = actor_at(4, 4, 1);
        ASSERT_NE(nullptr, a);
        a->stats()->set_bit_flags(BIT_FLYING, 1);
        float cost = 0.0f;
        EXPECT_EQ(column(4, 4, 10, 1), solve_to(a, 4, 10, 1, cost))
            << "flyer: the path crosses the air row straight down";
        EXPECT_FLOAT_EQ(6.0f, cost);
    }
    {
        SetUp();
        paint_air_row(0);
        walker* a = actor_at(4, 4, 0);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        EXPECT_EQ(column(4, 4, 10, 0), solve_to(a, 4, 10, 0, cost))
            << "single floor: the rule is off, a non-flyer crosses the air row";
        EXPECT_FLOAT_EQ(6.0f, cost);
    }
}

// gameplay_context.cpp:191. A diagonal step past an air flank is never
// planned for a walker that cannot fly; a flyer takes it.
TEST_F(SemPathing, a_non_flyer_never_plans_a_diagonal_past_an_air_flank)
{
    two_floors();
    tile(1, 11, 10, PIX_AIR);
    walker* a = actor_at(10, 10, 1);
    ASSERT_NE(nullptr, a);
    float cost = 0.0f;
    EXPECT_EQ((std::vector<Cell>{{10, 10, 1}, {10, 11, 1}, {11, 11, 1}}),
              solve_to(a, 11, 11, 1, cost))
        << "non-flyer, flank is air: the diagonal (10,10)->(11,11) is not an edge";
    // (10,11) is off the goal line: its step pays |cross| = 16*16 = 256 x 0.01.
    const float first = 1.0f + static_cast<float>(256) * 0.01f;
    EXPECT_FLOAT_EQ(first + 1.0f, cost);

    a->stats()->set_bit_flags(BIT_FLYING, 1);
    EXPECT_EQ((std::vector<Cell>{{10, 10, 1}, {11, 11, 1}}), solve_to(a, 11, 11, 1, cost))
        << "flyer: the diagonal past the air flank is taken";
    EXPECT_FLOAT_EQ(kDiag, cost);
}

// gameplay_context.cpp:208. A cell holding any body (any body in its 32-px
// obmap bucket) costs 10 to step into; an empty one costs 1 straight.
TEST_F(SemPathing, a_cell_holding_a_body_costs_ten_to_path_through)
{
    for (int x = 0; x <= 10; ++x) {
        tile(0, x, 1, PIX_H_WALL1);
        tile(0, x, 3, PIX_H_WALL1);
    }
    walker* a = actor_at(2, 2, 0);
    walker* body = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, a);
    ASSERT_NE(nullptr, body);
    body->setxy(static_cast<short>(4 * GRID_SIZE), static_cast<short>(2 * GRID_SIZE));

    // The obmap files bodies in 32-px buckets, so the body at x 64..80 is
    // found in both cells of its bucket, (4,2) and (5,2).
    float cost = 0.0f;
    EXPECT_EQ(row(2, 2, 6, 0), solve_to(a, 6, 2, 0, cost));
    EXPECT_FLOAT_EQ(22.0f, cost)
        << "cell occupied: (3,2) and (6,2) at 1, the two cells of the body's "
           "obmap bucket at 10 each";

    (void)world().myobmap->remove(body);
    EXPECT_EQ(row(2, 2, 6, 0), solve_to(a, 6, 2, 0, cost));
    EXPECT_FLOAT_EQ(4.0f, cost) << "the same corridor empty: four steps at 1";
}

// gameplay_context.cpp:235. On stacked levels a walker that cannot fly plans
// floor changes only through stairs: an up-stair to the floor above, a
// down-stair to the floor below, when that floor exists and its cell is
// passable. Flyers plan no floor change at all.
TEST_F(SemPathing, a_non_flyer_plans_floor_changes_only_through_stairs_to_a_real_open_floor)
{
    const std::vector<Cell> climb =
        join(join(row(8, 5, 10, 0), row(8, 10, 14, 1)), {});
    {
        two_floors();
        tile(0, 10, 8, PIX_ZSTAIR_UP);
        tile(1, 10, 8, PIX_ZSTAIR_DOWN);
        walker* a = actor_at(5, 8, 0);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        EXPECT_EQ(climb, solve_to(a, 14, 8, 1, cost))
            << "UP stair, floor above exists and is open: (10,8,f0) links to (10,8,f1)";
        EXPECT_FLOAT_EQ(10.0f, cost) << "five steps, the climb at 1, four steps";

        walker* b = actor_at(14, 8, 1);
        ASSERT_NE(nullptr, b);
        EXPECT_EQ(join(row(8, 14, 10, 1), row(8, 10, 5, 0)), solve_to(b, 5, 8, 0, cost))
            << "DOWN stair, f>0: (10,8,f1) links to (10,8,f0)";
        EXPECT_FLOAT_EQ(10.0f, cost);

        a->stats()->set_bit_flags(BIT_FLYING, 1);
        EXPECT_EQ(std::vector<Cell>{}, solve_to(a, 14, 8, 1, cost))
            << "flyer: no vertical edge, the floor-1 goal is unreachable";
    }
    {
        SetUp();
        two_floors();
        tile(1, 10, 8, PIX_ZSTAIR_UP); // an up-stair on the top floor
        walker* b = actor_at(5, 8, 1);
        ASSERT_NE(nullptr, b);
        float cost = 0.0f;
        EXPECT_EQ(std::vector<Cell>{}, solve_to(b, 12, 8, 2, cost))
            << "top-floor UP: no edge to a floor that does not exist";
    }
    {
        SetUp();
        two_floors();
        tile(0, 10, 8, PIX_ZSTAIR_DOWN); // a down-stair on the ground floor
        walker* a = actor_at(5, 8, 0);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        EXPECT_EQ(std::vector<Cell>{}, solve_to(a, 14, 8, 1, cost))
            << "ground DOWN: no edge, and never one upward";
    }
    {
        SetUp();
        two_floors();
        tile(0, 10, 8, PIX_ZSTAIR_UP);
        tile(1, 10, 8, PIX_H_WALL1); // the cell above is a wall
        walker* a = actor_at(5, 8, 0);
        ASSERT_NE(nullptr, a);
        float cost = 0.0f;
        EXPECT_EQ(std::vector<Cell>{}, solve_to(a, 14, 8, 1, cost))
            << "blocked destination: no edge into the wall above";
    }
}

// ---------------------------------------------------------------------------
// guy.cpp: a hero's worth and the stats derived from its guy.
// ---------------------------------------------------------------------------

namespace {

class SemGuy : public ::testing::Test
{
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        tw_ = std::make_unique<TestGameWorld>();
        world().resize_grid(40, 40);
        std::fill_n(world().grid.data.get(), 40 * 40, static_cast<std::uint8_t>(PIX_GRASS1));
    }

    GameWorld& world() { return tw_->world(); }

    // A walker of the guy's family carrying (owning) the guy, its derived
    // stats computed the way a hero entering a level gets them.
    walker* hero_for(std::unique_ptr<guy> g)
    {
        walker* w = world().add_ob(Order::Living, g->family);
        if (w == nullptr)
            return nullptr;
        w->set_owned_myguy(std::move(g));
        w->myguy->update_derived_stats(w);
        return w;
    }
};

} // namespace

// guy.cpp:181. A trained stat adds nothing to a hero's worth when its family
// prices that stat at zero (or below).
TEST_F(SemGuy, a_stat_trained_on_a_family_that_prices_it_at_zero_adds_nothing_to_its_worth)
{
    const FamilyDescriptor* golem = get_family_descriptor(FAMILY_GOLEM);
    ASSERT_NE(nullptr, golem);
    ASSERT_EQ(0, golem->stat_costs[StatAxis::Strength]) << "the golem's pack row prices training at 0";
    ASSERT_EQ(0, golem->hiring_cost);

    guy g(FAMILY_GOLEM);
    g.strength = static_cast<short>(g.strength + 50);
    g.dexterity = static_cast<short>(g.dexterity + 50);
    g.constitution = static_cast<short>(g.constitution + 50);
    g.intelligence = static_cast<short>(g.intelligence + 50);
    g.armor = static_cast<short>(g.armor + 50);
    EXPECT_EQ(0, g.query_heart_value())
        << "stat_cost 0: fifty points over base on every stat are worth nothing";

    // The same family with a NEGATIVE strength price (a mod's row): still nothing.
    FamilyDescriptor patched = *golem;
    patched.stat_costs[StatAxis::Strength] = -5;
    ASSERT_TRUE(set_family_descriptor(FAMILY_GOLEM, patched));
    guy strong(FAMILY_GOLEM);
    strong.strength = static_cast<short>(strong.strength + 50);
    EXPECT_EQ(0, strong.query_heart_value())
        << "stat_cost < 0: a trained stat adds nothing (it never subtracts or wraps)";

    // The control: the soldier prices strength, and eight points are worth 281.
    guy soldier(FAMILY_SOLDIER);
    soldier.strength = static_cast<short>(soldier.strength + 8);
    EXPECT_EQ(531, soldier.query_heart_value()) << "250 to hire + 8^1.85 x 6";
}

// guy.cpp:519. Dexterity speeds a hero up by dex/54 over its family's step,
// and below the cap of 12 the sum is kept as is.
TEST_F(SemGuy, a_heros_step_below_the_cap_is_its_family_step_plus_dex_over_54)
{
    auto g = std::make_unique<guy>(FAMILY_SOLDIER);
    ASSERT_EQ(6, g->dexterity) << "the soldier's base dexterity";
    walker* w = hero_for(std::move(g));
    ASSERT_NE(nullptr, w);
    EXPECT_FLOAT_EQ(4.0f + 6.0f / 54.0f, w->stepsize())
        << "<= 12: stepsize is the loader's 4 plus dex/54";
    EXPECT_FLOAT_EQ(4.0f + 6.0f / 54.0f, w->normal_stepsize())
        << "<= 12: normal_stepsize follows the unclamped value";
}

// guy.cpp:525. Dexterity shortens a hero's fire delay by dex/47; above the
// floor of 1 the difference is kept as is.
TEST_F(SemGuy, a_heros_fire_delay_above_the_floor_is_its_family_delay_minus_dex_over_47)
{
    walker* w = hero_for(std::make_unique<guy>(FAMILY_SOLDIER));
    ASSERT_NE(nullptr, w);
    EXPECT_FLOAT_EQ(6.0f - 6.0f / 47.0f, w->fire_frequency())
        << "else: fire_frequency is the loader's 6 minus dex/47";
}

// guy.cpp:541/547/558. The heal budget con + str/6 + 1020: each whole REGEN
// (4000) above it is a hit point per round, a remainder above 1 sets the
// delay to REGEN/(rem+1), a remainder of 1 leaves REGEN, and the delay is
// never below 2.
TEST_F(SemGuy, a_heros_heal_delay_keeps_regen_for_a_remainder_of_one_and_never_drops_below_two)
{
    {
        auto g = std::make_unique<guy>(FAMILY_SOLDIER);
        g->strength = 0;
        g->constitution = 2981; // budget 4001: one whole step, remainder 1
        walker* w = hero_for(std::move(g));
        ASSERT_NE(nullptr, w);
        EXPECT_FLOAT_EQ(1.0f, w->stats()->heal_per_round());
        EXPECT_EQ(4000, w->stats()->max_heal_delay())
            << "remainder <= 1: the delay stays REGEN";
        EXPECT_EQ(0, w->stats()->current_heal_delay()) << "a new hero starts with an empty counter";
    }
    {
        auto g = std::make_unique<guy>(FAMILY_SOLDIER);
        g->strength = 0;
        g->constitution = 980; // budget 2000: no whole step, 4000/2001 = 1
        walker* w = hero_for(std::move(g));
        ASSERT_NE(nullptr, w);
        EXPECT_FLOAT_EQ(0.0f, w->stats()->heal_per_round());
        EXPECT_EQ(2, w->stats()->max_heal_delay())
            << "computed delay < 2: clamped to 2";
    }
}

// guy.cpp:432. A bot's "foe within N" gate measures to the foe it holds; only
// a walker with no foe acquires the nearest one (and keeps it); with none to
// find the gate is closed; the test is strict. The classic acquire-then-
// measure: a held far foe closes the gate even with a nearer one beside it.
TEST_F(SemGuy, classic_foe_within_gate_measures_only_the_held_foe_and_acquires_only_when_foeless)
{
    walker* self_w = world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self_w);
    self_w->set_team_num(0);
    self_w->setxy(100, 100);
    living* self = static_cast<living*>(self_w);

    // No foe on the map at all.
    EXPECT_FALSE(check_special_ai_distance(self, 100)) << "no foe, none found: false";
    EXPECT_EQ(nullptr, self->foe()) << "no foe, none found: the foe stays empty";

    walker* far_orc = world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, far_orc);
    far_orc->set_team_num(1);
    far_orc->setxy(300, 100);
    const int far_d = self->distance_to_ob(far_orc);
    ASSERT_EQ(200, far_d);

    // No foe held: the nearest is acquired and measured.
    EXPECT_FALSE(check_special_ai_distance(self, 200))
        << "no foe, one found at exactly N: the test is strict";
    EXPECT_EQ(far_orc, self->foe()) << "no foe: the found foe is kept as the foe";
    self->set_foe(nullptr);
    EXPECT_TRUE(check_special_ai_distance(self, 201)) << "no foe, one found within N: true";
    EXPECT_EQ(far_orc, self->foe());

    // A nearer orc arrives; the held far one is still what is measured.
    walker* near_orc = world().add_ob(Order::Living, FAMILY_ORC);
    ASSERT_NE(nullptr, near_orc);
    near_orc->set_team_num(1);
    near_orc->setxy(130, 100);
    ASSERT_EQ(30, self->distance_to_ob(near_orc));
    EXPECT_FALSE(check_special_ai_distance(self, 100))
        << "has foe, distance >= N: false although another foe stands within N";
    EXPECT_EQ(far_orc, self->foe()) << "has foe: the held foe is unchanged";
    EXPECT_TRUE(check_special_ai_distance(self, 250)) << "has foe, distance < N: true";
    EXPECT_EQ(far_orc, self->foe()) << "has foe, distance < N: the held foe is unchanged";
}

// ---------------------------------------------------------------------------
// living.cpp: living::act's upkeep, one act at a time.
// ---------------------------------------------------------------------------

namespace {

class SemLiving : public ::testing::Test
{
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    og::test::ScopedHookFailureGuard guard_;
    std::unique_ptr<TestGameWorld> tw_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(40, 40);
        std::fill_n(w.grid.data.get(), 40 * 40, static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.rng_.state_ = 12345u;
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    GameWorld& world() { return tw_->world(); }

    void tile(int cx, int cy, unsigned char pix)
    {
        world().grid.data[static_cast<std::size_t>(cx + world().grid.w * cy)] = pix;
    }

    // A walker standing still under ACT_CONTROL (no AI, no rolls), 100/100
    // HP and MP, no regeneration, no timers running.
    walker* still(int family, int team, short x, short y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(static_cast<unsigned char>(team));
        w->set_real_team_num(255);
        w->set_act_type(ACT_CONTROL);
        w->stats()->set_max_hitpoints(100.0f);
        w->stats()->set_hitpoints(100.0f);
        w->stats()->set_max_magicpoints(100.0f);
        w->stats()->set_magicpoints(100.0f);
        w->stats()->set_heal_per_round(0.0f);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_heal_delay(30000);
        w->stats()->set_max_magic_delay(30000);
        w->stats()->set_current_heal_delay(0);
        w->stats()->set_current_magic_delay(0);
        w->set_regen_delay(0);
        w->setxy(x, y);
        return w;
    }
};

} // namespace

// living.cpp:69. With no banked rounds, or dead, a walker acts once.
TEST_F(SemLiving, a_walker_with_no_banked_rounds_or_dead_takes_a_single_act)
{
    walker* w = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_invulnerable_left(10);
    const unsigned char dc = w->drawcycle();
    w->act();
    EXPECT_EQ(static_cast<unsigned char>(dc + 1), w->drawcycle()) << "no rounds: one act, drawcycle +1";
    EXPECT_EQ(9, w->invulnerable_left()) << "no rounds: the timer ran once";
    EXPECT_EQ(0, w->bonus_rounds());

    walker* corpse = still(FAMILY_SOLDIER, 0, 240, 160);
    ASSERT_NE(nullptr, corpse);
    corpse->set_dead(1);
    corpse->set_bonus_rounds(3);
    const unsigned char cdc = corpse->drawcycle();
    EXPECT_FALSE(corpse->act());
    EXPECT_EQ(3, corpse->bonus_rounds()) << "dead: the bank is never spent";
    EXPECT_EQ(cdc, corpse->drawcycle()) << "dead: no act at all";
}

// living.cpp:74/84. A dead walker does nothing in its act; one that dies
// falling down a pit stops acting that same tick.
TEST_F(SemLiving, a_dead_walker_does_nothing_and_a_pit_death_ends_the_act_at_once)
{
    {
        walker* w = still(FAMILY_SOLDIER, 0, 160, 160);
        ASSERT_NE(nullptr, w);
        w->set_dead(1);
        w->set_view_all(5);
        w->set_invulnerable_left(5);
        const unsigned char dc = w->drawcycle();
        EXPECT_FALSE(w->act());
        EXPECT_EQ(5, w->view_all()) << "dead on entry: no timer runs";
        EXPECT_EQ(5, w->invulnerable_left()) << "dead on entry: no timer runs";
        EXPECT_EQ(dc, w->drawcycle()) << "dead on entry: drawcycle unchanged";
    }
    {
        GameWorld& g = world();
        g.set_floor_count(2);
        tile(10, 10, PIX_AIR); // a hole in floor 0: falling through it is a pit death
        walker* w = still(FAMILY_SOLDIER, 1, 160, 160);
        ASSERT_NE(nullptr, w);
        w->set_act_type(ACT_RANDOM);
        w->set_invulnerable_left(5);
        EXPECT_FALSE(w->act()) << "pit death in apply_z_motion: act returns 0";
        EXPECT_TRUE(w->dead()) << "pit death in apply_z_motion";
        EXPECT_EQ(5, w->invulnerable_left()) << "pit death: nothing after the fall runs";
        EXPECT_FALSE(w->stats()->has_commands()) << "pit death: no AI command queued";
    }
}

// living.cpp:94. A walker forgets a foe that is hidden, before any cloak roll.
TEST_F(SemLiving, a_walker_forgets_a_hidden_foe_without_rolling_for_its_cloak)
{
    walker* hunter = still(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = still(FAMILY_ORC, 1, 200, 160);
    ASSERT_NE(nullptr, hunter);
    ASSERT_NE(nullptr, orc);
    orc->set_invisibility_left(400);
    orc->set_hidden(true);
    hunter->set_foe(orc);
    const std::uint32_t rng_before = world().rng_.state_;
    hunter->act();
    EXPECT_EQ(nullptr, hunter->foe()) << "foe hidden: the foe is cleared";
    EXPECT_EQ(rng_before, world().rng_.state_) << "foe hidden: no cloak roll is drawn";
}

// living.cpp:96. A walker never keeps a friend as its foe.
TEST_F(SemLiving, a_walker_drops_a_foe_that_turned_friendly_and_keeps_a_hostile_one)
{
    walker* hunter = still(FAMILY_SOLDIER, 0, 160, 160);
    walker* orc = still(FAMILY_ORC, 1, 200, 160);
    ASSERT_NE(nullptr, hunter);
    ASSERT_NE(nullptr, orc);
    hunter->set_foe(orc);
    hunter->act();
    EXPECT_EQ(orc, hunter->foe()) << "foe hostile: kept";
    orc->set_team_num(0); // charmed onto our side
    hunter->act();
    EXPECT_EQ(nullptr, hunter->foe()) << "foe now friendly: cleared";
}

// living.cpp:98. A follower forgets a dead leader and keeps a live one.
TEST_F(SemLiving, a_follower_forgets_a_dead_leader_and_keeps_a_live_one)
{
    walker* hero = still(FAMILY_SOLDIER, 0, 160, 160);
    walker* ally = still(FAMILY_SOLDIER, 0, 200, 160);
    ASSERT_NE(nullptr, hero);
    ASSERT_NE(nullptr, ally);
    ally->set_leader(hero);
    ally->act();
    EXPECT_EQ(hero, ally->leader()) << "leader alive: kept";
    hero->set_dead(1);
    ally->act();
    EXPECT_EQ(nullptr, ally->leader()) << "leader dead: cleared";
}

// living.cpp:100. An owned living whose owner lives goes on acting.
TEST_F(SemLiving, an_owned_living_goes_on_acting_while_its_owner_lives)
{
    walker* mage = still(FAMILY_MAGE, 0, 160, 160);
    walker* summon = still(FAMILY_SOLDIER, 0, 200, 160);
    ASSERT_NE(nullptr, mage);
    ASSERT_NE(nullptr, summon);
    summon->set_owner(mage);
    summon->set_lifetime(0);
    const unsigned char dc = summon->drawcycle();
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(summon->act()) << "owner alive: the act continues (act " << i << ")";
    EXPECT_FALSE(summon->dead()) << "owner alive";
    EXPECT_EQ(static_cast<unsigned char>(dc + 5), summon->drawcycle()) << "owner alive: five full acts";
}

// living.cpp:109/117/119. A lifetime counts down one per act and the walker
// dies on the act it reaches 0; a lifetime of 0 never expires.
TEST_F(SemLiving, a_lifetime_counts_down_one_per_act_and_zero_means_permanent)
{
    walker* owner = still(FAMILY_DRUID, 0, 160, 160);
    walker* summon = still(FAMILY_SOLDIER, 0, 200, 160);
    ASSERT_NE(nullptr, owner);
    ASSERT_NE(nullptr, summon);
    summon->set_owner(owner);
    summon->set_lifetime(3);
    summon->act();
    EXPECT_EQ(2, summon->lifetime()) << "lifetime > 1: decreases by 1";
    EXPECT_FALSE(summon->dead());
    summon->act();
    EXPECT_EQ(1, summon->lifetime()) << "lifetime > 1: decreases by 1";
    EXPECT_FALSE(summon->dead());
    summon->act();
    EXPECT_TRUE(summon->dead()) << "lifetime counts to 0: dies on that act";

    walker* permanent = still(FAMILY_SOLDIER, 0, 240, 160);
    ASSERT_NE(nullptr, permanent);
    permanent->set_lifetime(0);
    for (int i = 0; i < 100; ++i)
        permanent->act();
    EXPECT_FALSE(permanent->dead()) << "lifetime 0, no owner: never expires";
    EXPECT_EQ(0, permanent->lifetime());
}

// living.cpp:147/158. Regeneration: +per_round each act and one more each
// max_delay acts, capped at max; nothing while FREEZE TIME runs, on the
// caster's own side too (the classic gate is world-wide).
TEST_F(SemLiving, classic_regeneration_runs_each_act_and_freeze_time_stops_it_for_every_side)
{
    walker* w = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->stats()->set_hitpoints(10.0f);
    w->stats()->set_magicpoints(20.0f);
    w->stats()->set_heal_per_round(1.0f);
    w->stats()->set_magic_per_round(2.0f);
    w->stats()->set_max_heal_delay(3);
    w->stats()->set_max_magic_delay(3);
    w->stats()->set_current_heal_delay(0);
    w->stats()->set_current_magic_delay(0);
    w->set_regen_delay(0);

    world().enemy_freeze = 50;
    w->act();
    EXPECT_FLOAT_EQ(10.0f, w->stats()->hitpoints()) << "enemy_freeze > 0: no HP regen (team 0 included)";
    EXPECT_FLOAT_EQ(20.0f, w->stats()->magicpoints()) << "enemy_freeze > 0: no MP regen (team 0 included)";
    EXPECT_EQ(0, w->stats()->current_heal_delay()) << "enemy_freeze > 0: the heal counter stands";

    world().enemy_freeze = 0;
    w->act();
    EXPECT_FLOAT_EQ(11.0f, w->stats()->hitpoints()) << "normal: +heal_per_round";
    EXPECT_FLOAT_EQ(22.0f, w->stats()->magicpoints()) << "normal: +magic_per_round";
    w->act();
    EXPECT_FLOAT_EQ(12.0f, w->stats()->hitpoints());
    w->act();
    EXPECT_FLOAT_EQ(14.0f, w->stats()->hitpoints()) << "normal: the third act adds the extra point";
    EXPECT_FLOAT_EQ(27.0f, w->stats()->magicpoints()) << "normal: the third act adds the extra point";
    EXPECT_EQ(0, w->stats()->current_heal_delay());

    w->stats()->set_hitpoints(99.5f);
    w->act();
    EXPECT_FLOAT_EQ(100.0f, w->stats()->hitpoints()) << "normal: capped at max";
}

// living.cpp:169/173. View-all and invulnerability at 0 stay 0.
TEST_F(SemLiving, view_all_and_invulnerability_at_zero_stay_zero)
{
    walker* w = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_view_all(0);
    w->set_invulnerable_left(0);
    w->act();
    EXPECT_EQ(0, w->view_all()) << "view_all 0: stays 0";
    EXPECT_EQ(0, w->invulnerable_left()) << "invulnerable_left 0: stays 0";
}

// living.cpp:183. Flight lasts its count of acts and then stays 0.
TEST_F(SemLiving, flight_lasts_its_count_of_acts_and_then_stays_zero)
{
    walker* w = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, w);
    w->set_flight_left(5);
    for (int i = 0; i < 3; ++i)
        w->act();
    EXPECT_EQ(2, w->flight_left()) << "flight > 0: one per act";
    w->act();
    w->act();
    EXPECT_EQ(0, w->flight_left()) << "flight > 0: runs out after its count";
    w->act();
    EXPECT_EQ(0, w->flight_left()) << "flight 0: stays 0";
}

// living.cpp:185/197. A non-flying walker inside impassable terrain loses 1 HP
// per act (a red 1) and is lent one tick of flight; on open ground, or
// flying, nothing happens.
TEST_F(SemLiving, a_walker_stuck_in_a_wall_loses_a_point_per_act_and_gets_a_tick_of_flight)
{
    tile(10, 10, PIX_H_WALL1);
    walker* stuck = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, stuck);
    stuck->stats()->set_hitpoints(10.0f);
    stuck->damage_numbers.clear();
    stuck->act();
    EXPECT_FLOAT_EQ(9.0f, stuck->stats()->hitpoints()) << "inside impassable, no flight: -1 HP";
    EXPECT_EQ(1, stuck->flight_left()) << "inside impassable, no flight: one tick of flight";
    ASSERT_EQ(1u, stuck->damage_numbers.size()) << "inside impassable: one damage number";
    EXPECT_FLOAT_EQ(1.0f, stuck->damage_numbers.front().value);
    EXPECT_EQ(RED, stuck->damage_numbers.front().color);
    EXPECT_FLOAT_EQ(static_cast<float>(160 + stuck->sizex() / 2), stuck->damage_numbers.front().x);
    EXPECT_FLOAT_EQ(160.0f, stuck->damage_numbers.front().y);
    stuck->act();
    EXPECT_FLOAT_EQ(8.0f, stuck->stats()->hitpoints())
        << "the lent tick runs out the next act and the wall bites again";
    EXPECT_EQ(1, stuck->flight_left());

    walker* flyer = still(FAMILY_SOLDIER, 0, 160, 160);
    ASSERT_NE(nullptr, flyer);
    flyer->set_flight_left(5);
    flyer->damage_numbers.clear();
    flyer->act();
    EXPECT_FLOAT_EQ(100.0f, flyer->stats()->hitpoints()) << "flying in the wall: no damage";
    EXPECT_EQ(4, flyer->flight_left());
    EXPECT_TRUE(flyer->damage_numbers.empty()) << "flying in the wall: no damage number";

    walker* open = still(FAMILY_SOLDIER, 0, 320, 320);
    ASSERT_NE(nullptr, open);
    open->damage_numbers.clear();
    open->act();
    EXPECT_FLOAT_EQ(100.0f, open->stats()->hitpoints()) << "passable: no damage";
    EXPECT_EQ(0, open->flight_left()) << "passable: no flight lent";
    EXPECT_TRUE(open->damage_numbers.empty()) << "passable: no damage number";
}

// living.cpp:205/211. Charm counts down while above 1 and leaves the team
// alone; a walker never charmed keeps its team.
TEST_F(SemLiving, charm_counts_down_without_touching_the_team_and_an_uncharmed_walker_keeps_its_team)
{
    walker* orc = still(FAMILY_ORC, 0, 160, 160);
    ASSERT_NE(nullptr, orc);
    orc->set_real_team_num(1); // charmed onto team 0 from team 1
    orc->set_charm_left(3);
    orc->act();
    EXPECT_EQ(2, orc->charm_left()) << "charm_left > 1: -1";
    EXPECT_EQ(0, orc->team_num()) << "charm_left > 1: still on the charmer's team";
    EXPECT_EQ(1, orc->real_team_num());
    orc->act();
    EXPECT_EQ(1, orc->charm_left()) << "charm_left > 1: -1";
    EXPECT_EQ(0, orc->team_num());
    orc->act();
    EXPECT_EQ(0, orc->charm_left());
    EXPECT_EQ(1, orc->team_num()) << "charm runs out: back to the real team";
    EXPECT_EQ(255, orc->real_team_num());

    walker* plain = still(FAMILY_SOLDIER, 2, 240, 160);
    ASSERT_NE(nullptr, plain);
    plain->act();
    EXPECT_EQ(0, plain->charm_left()) << "never charmed: charm stays 0";
    EXPECT_EQ(2, plain->team_num()) << "never charmed: team unchanged";
    EXPECT_EQ(255, plain->real_team_num());
}

// living.cpp:237. The forest charges a forestwalker a point of magic only
// when it has some and its magic-regen delay is at 0.
TEST_F(SemLiving, the_forest_charges_magic_only_with_magic_in_hand_and_the_delay_at_zero)
{
    for (int x = 0; x < 6; ++x)
        for (int y = 0; y < 6; ++y)
            tile(x, y, PIX_TREE_M1);
    auto elf_in_trees = [&](float mp, float max_mp, int delay) {
        walker* e = still(FAMILY_ELF, 0, 32, 32);
        if (e == nullptr)
            return e;
        e->stats()->set_max_magicpoints(max_mp);
        e->stats()->set_magicpoints(mp);
        e->stats()->set_current_magic_delay(delay);
        e->stats()->set_max_magic_delay(1000);
        return e;
    };
    walker* fed = elf_in_trees(10.0f, 10.0f, 0);
    ASSERT_NE(nullptr, fed);
    ASSERT_EQ(1, fed->stats()->query_bit_flags(BIT_FORESTWALK) ? 1 : 0);
    fed->act();
    EXPECT_FLOAT_EQ(9.0f, fed->stats()->magicpoints()) << "in forest, MP > 0, delay 0: -1";

    walker* waiting = elf_in_trees(10.0f, 10.0f, 3);
    ASSERT_NE(nullptr, waiting);
    waiting->act();
    EXPECT_FLOAT_EQ(10.0f, waiting->stats()->magicpoints()) << "delay running: MP unchanged by the forest";

    walker* empty = elf_in_trees(0.0f, 0.0f, 0);
    ASSERT_NE(nullptr, empty);
    empty->act();
    EXPECT_FLOAT_EQ(0.0f, empty->stats()->magicpoints()) << "MP 0: unchanged (never below 0)";
}
