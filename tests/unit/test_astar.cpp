// Headless unit tests for the first-party A* solver (og::pathfinding::AStar)
// that replaced the vendored MicroPather library.
//
// These cover the generic solver contract directly through a self-contained
// grid graph: optimality, obstacle handling, no-solution / start==end, weighted
// detours, impassable-edge filtering, path reconstruction, exact cost
// accumulation, and determinism. Byte-for-byte equivalence with the historical
// MicroPather output is additionally pinned by the gameplay parity goldens
// (og_test_parity) and an offline differential oracle.

#include <gtest/gtest.h>

#include <openglad/gameplay/astar.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using og::pathfinding::AStar;
using og::pathfinding::Graph;
using og::pathfinding::SolveResult;
using og::pathfinding::State;
using og::pathfinding::StateCost;

namespace
{

// Diagonal step cost; the exact float bits the production cost model uses.
constexpr float kDiag = 1.41421354f;

// A W x H 8-connected grid. Cells may be blocked (no edges into them) or carry
// an extra entry weight added to the base move cost. State encodes y*W + x.
class GridGraph final : public Graph
{
public:
    GridGraph(int w, int h)
        : w_(w), h_(h), blocked_(static_cast<std::size_t>(w * h), false),
          weight_(static_cast<std::size_t>(w * h), 0.0f)
    {
    }

    void block(int x, int y) { blocked_[idx(x, y)] = true; }
    void set_weight(int x, int y, float wgt) { weight_[idx(x, y)] = wgt; }

    State state_of(int x, int y) const
    {
        return reinterpret_cast<State>(static_cast<std::intptr_t>(idx(x, y)));
    }
    int sx(State s) const
    {
        return static_cast<int>(reinterpret_cast<std::intptr_t>(s) % w_);
    }
    int sy(State s) const
    {
        return static_cast<int>(reinterpret_cast<std::intptr_t>(s) / w_);
    }

    float least_cost_estimate(State a, State b) override
    {
        const int dx = sx(a) - sx(b);
        const int dy = sy(a) - sy(b);
        // Euclidean: admissible for an 8-connected grid with diagonal cost ~sqrt2.
        return std::sqrt(static_cast<float>(dx * dx + dy * dy));
    }

    void adjacent_cost(State s, std::vector<StateCost>& out) override
    {
        const int x = sx(s);
        const int y = sy(s);
        for (int i = -1; i <= 1; ++i)
        {
            for (int j = -1; j <= 1; ++j)
            {
                if (i == 0 && j == 0)
                    continue;
                const int nx = x + i;
                const int ny = y + j;
                if (nx < 0 || ny < 0 || nx >= w_ || ny >= h_)
                    continue;
                if (blocked_[idx(nx, ny)])
                    continue;
                const float base = (i == 0 || j == 0) ? 1.0f : kDiag;
                out.push_back({state_of(nx, ny), base + weight_[idx(nx, ny)]});
            }
        }
    }

private:
    std::size_t idx(int x, int y) const
    {
        return static_cast<std::size_t>(y * w_ + x);
    }

    int w_;
    int h_;
    std::vector<bool> blocked_;
    std::vector<float> weight_;
};

// Assert the path is well-formed: correct endpoints and each consecutive pair is
// an 8-neighbor step on the grid. Returns the recomputed total cost (summed in
// path order, which matches the solver's accumulation order bit-for-bit).
float validate_and_cost(GridGraph& g, const std::vector<State>& path,
                        State start, State end)
{
    EXPECT_GE(path.size(), 2u);
    EXPECT_EQ(path.front(), start);
    EXPECT_EQ(path.back(), end);

    float total = 0.0f;
    for (std::size_t i = 0; i + 1 < path.size(); ++i)
    {
        const int dx = std::abs(g.sx(path[i + 1]) - g.sx(path[i]));
        const int dy = std::abs(g.sy(path[i + 1]) - g.sy(path[i]));
        EXPECT_TRUE(dx <= 1 && dy <= 1 && (dx + dy) > 0)
            << "non-adjacent hop at index " << i;

        // Recover the edge cost the solver would have used for this step.
        std::vector<StateCost> nbrs;
        g.adjacent_cost(path[i], nbrs);
        bool found = false;
        for (const StateCost& sc : nbrs)
        {
            if (sc.state == path[i + 1])
            {
                total += sc.cost;
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "path step is not a valid neighbor edge";
    }
    return total;
}

} // namespace

TEST(AStar, start_equals_end_yields_empty_path)
{
    GridGraph g(4, 4);
    AStar solver(g);

    std::vector<State> path{reinterpret_cast<State>(static_cast<std::intptr_t>(7))};
    float cost = 123.0f;
    const SolveResult r =
        solver.solve(g.state_of(1, 1), g.state_of(1, 1), path, cost);

    EXPECT_EQ(r, SolveResult::StartEndSame);
    EXPECT_TRUE(path.empty()) << "path must be cleared for start==end";
    EXPECT_EQ(cost, 0.0f);
}

TEST(AStar, adjacent_step_collapses_to_two_node_path)
{
    GridGraph g(4, 4);
    AStar solver(g);

    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(1, 1), g.state_of(2, 1), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    ASSERT_EQ(path.size(), 2u);
    EXPECT_EQ(path[0], g.state_of(1, 1));
    EXPECT_EQ(path[1], g.state_of(2, 1));
    EXPECT_FLOAT_EQ(cost, 1.0f);
}

TEST(AStar, straight_line_open_grid_is_optimal)
{
    GridGraph g(8, 3);
    AStar solver(g);

    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 1), g.state_of(6, 1), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost, 6.0f) << "six orthogonal steps east";
    const float recomputed = validate_and_cost(g, path, g.state_of(0, 1), g.state_of(6, 1));
    EXPECT_FLOAT_EQ(recomputed, cost);
}

TEST(AStar, diagonal_line_open_grid_is_optimal)
{
    GridGraph g(6, 6);
    AStar solver(g);

    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 0), g.state_of(4, 4), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost, 4.0f * kDiag) << "four diagonal steps";
    const float recomputed = validate_and_cost(g, path, g.state_of(0, 0), g.state_of(4, 4));
    EXPECT_FLOAT_EQ(recomputed, cost);
}

TEST(AStar, routes_around_wall_through_the_gap)
{
    // Vertical wall at x=3 spanning y=0..3, with a single gap at y=4.
    GridGraph g(7, 6);
    for (int y = 0; y <= 3; ++y)
        g.block(3, y);

    AStar solver(g);
    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 0), g.state_of(6, 0), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    const float recomputed = validate_and_cost(g, path, g.state_of(0, 0), g.state_of(6, 0));
    EXPECT_FLOAT_EQ(recomputed, cost);

    // The route must pass through the only opening in the wall column.
    bool through_gap = false;
    for (State s : path)
        if (g.sx(s) == 3)
        {
            EXPECT_EQ(g.sy(s), 4) << "wall is only passable at the gap";
            through_gap = true;
        }
    EXPECT_TRUE(through_gap);
}

TEST(AStar, no_solution_when_goal_is_walled_off)
{
    GridGraph g(7, 7);
    // Fully enclose the goal cell (3,3) so no edge can reach it.
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            if (!(i == 0 && j == 0))
                g.block(3 + i, 3 + j);

    AStar solver(g);
    std::vector<State> path{g.state_of(0, 0)};
    float cost = 99.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 0), g.state_of(3, 3), path, cost);

    EXPECT_EQ(r, SolveResult::NoSolution);
    EXPECT_TRUE(path.empty()) << "no-solution clears the output path";
}

TEST(AStar, no_solution_when_start_is_boxed_in)
{
    GridGraph g(7, 7);
    for (int i = -1; i <= 1; ++i)
        for (int j = -1; j <= 1; ++j)
            if (!(i == 0 && j == 0))
                g.block(2 + i, 2 + j);

    AStar solver(g);
    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(2, 2), g.state_of(6, 6), path, cost);

    EXPECT_EQ(r, SolveResult::NoSolution);
    EXPECT_TRUE(path.empty());
}

TEST(AStar, prefers_cheaper_detour_over_expensive_direct_cell)
{
    // Make the single direct cell between start and end very expensive so the
    // optimal route detours around it.
    GridGraph g(5, 5);
    g.set_weight(2, 2, 100.0f);

    AStar solver(g);
    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(1, 2), g.state_of(3, 2), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    for (State s : path)
        EXPECT_FALSE(g.sx(s) == 2 && g.sy(s) == 2)
            << "optimal path must avoid the expensive cell";
    const float recomputed = validate_and_cost(g, path, g.state_of(1, 2), g.state_of(3, 2));
    EXPECT_FLOAT_EQ(recomputed, cost);
    EXPECT_LT(cost, 100.0f);
}

TEST(AStar, returned_cost_matches_summed_edge_costs)
{
    GridGraph g(10, 10);
    g.block(5, 0);
    g.block(5, 1);
    g.block(5, 2);
    g.set_weight(6, 3, 2.5f);
    g.set_weight(4, 6, 0.75f);

    AStar solver(g);
    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 0), g.state_of(9, 9), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    const float recomputed = validate_and_cost(g, path, g.state_of(0, 0), g.state_of(9, 9));
    // Accumulation order matches the solver's parent-chain order exactly.
    EXPECT_EQ(std::memcmp(&recomputed, &cost, sizeof(float)), 0)
        << "summed edge costs must equal the reported total bit-for-bit";
}

TEST(AStar, solve_is_deterministic_across_repeats)
{
    GridGraph g(20, 20);
    // A non-trivial obstacle field with several equal-cost choices.
    for (int y = 2; y < 18; ++y)
        if (y != 9)
            g.block(7, y);
    for (int y = 2; y < 18; ++y)
        if (y != 10)
            g.block(13, y);

    AStar solver(g);
    std::vector<State> first;
    float first_cost = 0.0f;
    ASSERT_EQ(solver.solve(g.state_of(0, 0), g.state_of(19, 19), first, first_cost),
              SolveResult::Solved);

    for (int rep = 0; rep < 5; ++rep)
    {
        std::vector<State> again;
        float again_cost = 0.0f;
        ASSERT_EQ(
            solver.solve(g.state_of(0, 0), g.state_of(19, 19), again, again_cost),
            SolveResult::Solved);
        ASSERT_EQ(again.size(), first.size());
        EXPECT_EQ(again, first) << "identical input must give the identical path";
        EXPECT_EQ(std::memcmp(&again_cost, &first_cost, sizeof(float)), 0)
            << "identical input must give identical cost bits";
    }
}

TEST(AStar, reset_then_solve_still_correct)
{
    GridGraph g(8, 8);
    AStar solver(g);

    std::vector<State> path;
    float cost = 0.0f;
    ASSERT_EQ(solver.solve(g.state_of(0, 0), g.state_of(7, 0), path, cost),
              SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost, 7.0f);

    solver.reset();

    std::vector<State> path2;
    float cost2 = 0.0f;
    ASSERT_EQ(solver.solve(g.state_of(0, 0), g.state_of(7, 0), path2, cost2),
              SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost2, 7.0f);
    EXPECT_EQ(path2, path) << "reset must not change subsequent results";
}

// A graph that explicitly emits an impassable edge to verify the solver filters
// edges whose cost equals kImpassableCost.
TEST(AStar, impassable_edges_are_ignored)
{
    using og::pathfinding::kImpassableCost;

    class LineWithDeadEnd final : public Graph
    {
    public:
        // States 0..3 in a line. Node 1 -> 2 is impassable, forcing no path.
        float least_cost_estimate(State a, State b) override
        {
            return static_cast<float>(std::abs(id(a) - id(b)));
        }
        void adjacent_cost(State s, std::vector<StateCost>& out) override
        {
            const int n = id(s);
            if (n == 0)
                out.push_back({make(1), 1.0f});
            else if (n == 1)
            {
                out.push_back({make(0), 1.0f});
                out.push_back({make(2), kImpassableCost}); // must be skipped
            }
            else if (n == 2)
                out.push_back({make(3), 1.0f});
        }
        static State make(int n) { return reinterpret_cast<State>(static_cast<std::intptr_t>(n)); }
        static int id(State s) { return static_cast<int>(reinterpret_cast<std::intptr_t>(s)); }
    } graph;

    AStar solver(graph);
    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(LineWithDeadEnd::make(0), LineWithDeadEnd::make(3), path, cost);

    EXPECT_EQ(r, SolveResult::NoSolution)
        << "the only route crosses an impassable edge, so there is no path";
    EXPECT_TRUE(path.empty());
}

TEST(AStar, large_open_grid_diagonal_is_optimal)
{
    GridGraph g(40, 40);
    AStar solver(g);

    std::vector<State> path;
    float cost = 0.0f;
    const SolveResult r =
        solver.solve(g.state_of(0, 0), g.state_of(39, 39), path, cost);

    ASSERT_EQ(r, SolveResult::Solved);
    // The solver accumulates 39 individual diagonal steps, so the total differs
    // from a single 39*kDiag multiply by float rounding -- compare with a small
    // tolerance; validate_and_cost below pins the accumulated value exactly.
    EXPECT_NEAR(cost, 39.0f * kDiag, 1e-3f);
    EXPECT_EQ(path.size(), 40u) << "40 cells along the main diagonal";
    const float recomputed = validate_and_cost(g, path, g.state_of(0, 0), g.state_of(39, 39));
    EXPECT_FLOAT_EQ(recomputed, cost);
}

TEST(AStar, solver_is_movable)
{
    GridGraph g(5, 5);
    AStar a(g);
    AStar b = std::move(a); // move-construct

    std::vector<State> path;
    float cost = 0.0f;
    ASSERT_EQ(b.solve(g.state_of(0, 0), g.state_of(4, 0), path, cost),
              SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost, 4.0f);

    AStar c(g);
    c = std::move(b); // move-assign
    std::vector<State> path2;
    float cost2 = 0.0f;
    ASSERT_EQ(c.solve(g.state_of(0, 0), g.state_of(4, 0), path2, cost2),
              SolveResult::Solved);
    EXPECT_FLOAT_EQ(cost2, 4.0f);
}
