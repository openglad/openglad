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
#ifndef OPENGLAD_GAMEPLAY_ASTAR_H
#define OPENGLAD_GAMEPLAY_ASTAR_H

#include <memory>
#include <vector>

namespace og::pathfinding
{

// Opaque pathfinding state. A state must be unique and unchanging for the
// duration of a solve. Clients that represent positions as numbers (OpenGlad
// encodes a grid-cell index in the pointer bits) simply reinterpret_cast the
// value to void*; the solver never dereferences it.
using State = void*;

// The exact cost to reach a neighboring state. Mirrors the classic A* edge:
// emit one StateCost per traversable neighbor from Graph::adjacent_cost. Use a
// huge cost (>= kImpassableCost) to mark an edge the solver must ignore.
struct StateCost
{
    State state = nullptr;
    float cost = 0.0f;
};

// Edges whose cost compares equal to this sentinel are filtered out by the
// solver (an "infinite cost" / impassable edge), matching the historical
// MicroPather contract this solver replaces.
inline constexpr float kImpassableCost = 3.40282347e+38f; // FLT_MAX

// Callback surface implemented by the client "map". The methods must be pure
// with respect to a single solve(): adjacent_cost must return identical results
// for the same state, and least_cost_estimate must be a stable lower bound.
class Graph
{
public:
    virtual ~Graph() = default;

    // Admissible lower bound on the cost from `start` to `end` (the heuristic).
    virtual float least_cost_estimate(State start, State end) = 0;

    // Append every traversable neighbor of `state`, with its edge cost, to
    // `out`. The solver clears `out` before each call. Do not emit `state`
    // itself as its own neighbor.
    virtual void adjacent_cost(State state, std::vector<StateCost>& out) = 0;
};

enum class SolveResult
{
    Solved,       // path holds [start, ..., end]; cost is the total path cost
    NoSolution,   // start and end are disconnected; path is empty
    StartEndSame, // start == end; path is empty and cost is 0
};

// Deterministic A* solver.
//
// The expansion order, tie-breaking, float accumulation, and path
// reconstruction are byte-for-byte compatible with the MicroPather library this
// type replaces, so AI movement (and the gameplay parity goldens that pin it)
// is preserved exactly. The open set is a cost-sorted intrusive list: nodes
// pushed at an equal cost are explored in insertion order, while a node whose
// cost is lowered by relaxation is promoted ahead of its equal-cost peers.
//
// Each solve() is self-contained: it resets per-search bookkeeping internally
// while retaining scratch capacity for speed across calls. The solver does not
// dereference State values and holds only a borrowed reference to the Graph.
class AStar
{
public:
    explicit AStar(Graph& graph);
    ~AStar();

    AStar(const AStar&) = delete;
    AStar& operator=(const AStar&) = delete;
    AStar(AStar&&) noexcept;
    AStar& operator=(AStar&&) noexcept;

    // Release retained scratch memory. Optional: solve() is self-contained and
    // does not require a prior reset().
    void reset();

    // Find a least-cost path from `start` to `end`.
    SolveResult solve(State start,
                      State end,
                      std::vector<State>& path,
                      float& cost);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::pathfinding

#endif // OPENGLAD_GAMEPLAY_ASTAR_H
