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

#include <openglad/gameplay/astar.h>

#include <cfloat>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace og::pathfinding
{

namespace
{

// One entry per visited state for a single solve. Nodes live in a node-stable
// container (std::unordered_map), so the raw pointers stored in the open list
// and in `parent` chains remain valid as new nodes are inserted.
struct Node
{
    State state = nullptr;
    float cost_from_start = FLT_MAX; // g: exact cost from start to this node
    float est_to_goal = FLT_MAX;     // h: heuristic estimate to the goal
    float total_cost = FLT_MAX;      // f = g + h (cached)
    Node* parent = nullptr;          // back-pointer for path reconstruction
    bool in_open = false;
    bool in_closed = false;
    Node* next = nullptr; // open-list neighbor toward higher cost
    Node* prev = nullptr; // open-list neighbor toward lower cost
};

// f = g + h, but an unreachable component keeps the node at +inf so it sorts to
// the very back of the open list (where the sentinel also lives).
float calc_total_cost(const Node& node)
{
    if (node.cost_from_start < FLT_MAX && node.est_to_goal < FLT_MAX)
        return node.cost_from_start + node.est_to_goal;
    return FLT_MAX;
}

// Cost-sorted intrusive doubly-linked open set with a fixed sentinel tail.
//
// The ordering rules reproduce MicroPather's queue exactly so the pop order --
// and therefore every resulting path -- is identical:
//   * push() inserts before the first node of strictly greater cost, so a node
//     tying an existing run is appended after it (insertion-order ties).
//   * update() (only ever called after a relaxation lowers a node's cost) first
//     promotes the node to the head if it now undercuts its predecessor, then
//     slides it right to the front of its new equal-cost run.
class OpenQueue
{
public:
    OpenQueue()
    {
        sentinel_.total_cost = FLT_MAX;
        sentinel_.next = &sentinel_;
        sentinel_.prev = &sentinel_;
    }

    OpenQueue(const OpenQueue&) = delete;
    OpenQueue& operator=(const OpenQueue&) = delete;

    bool empty() const { return sentinel_.next == &sentinel_; }

    void push(Node* node)
    {
        // The sentinel's FLT_MAX guarantees this terminates; callers never push
        // a node whose total_cost is FLT_MAX (impassable edges are filtered).
        Node* iter = sentinel_.next;
        while (true)
        {
            if (node->total_cost < iter->total_cost)
            {
                link_before(iter, node);
                node->in_open = true;
                break;
            }
            iter = iter->next;
        }
    }

    Node* pop()
    {
        Node* node = sentinel_.next;
        unlink(node);
        node->in_open = false;
        return node;
    }

    void update(Node* node)
    {
        // Lowered below its predecessor: hop to the front of the list.
        if (node->prev != &sentinel_ && node->total_cost < node->prev->total_cost)
        {
            unlink(node);
            link_before(sentinel_.next, node);
        }

        // Now too cheap to sit where it is no longer; slide right to the front
        // of the run of nodes that share its (or a lower) cost.
        if (node->total_cost > node->next->total_cost)
        {
            Node* iter = node->next;
            unlink(node);
            while (node->total_cost > iter->total_cost)
                iter = iter->next;
            link_before(iter, node);
        }
    }

private:
    static void unlink(Node* node)
    {
        node->next->prev = node->prev;
        node->prev->next = node->next;
        node->next = nullptr;
        node->prev = nullptr;
    }

    // Insert `node` immediately before `at`.
    static void link_before(Node* at, Node* node)
    {
        node->next = at;
        node->prev = at->prev;
        at->prev->next = node;
        at->prev = node;
    }

    Node sentinel_;
};

} // namespace

struct AStar::Impl
{
    explicit Impl(Graph& g)
        : graph(g)
    {
        nodes.reserve(256);
    }

    // Look up the node for `state`, creating a fresh (uninitialized-cost) node
    // on first encounter. Returned pointers stay valid across later inserts
    // because std::unordered_map keeps elements node-stable.
    Node* node_for(State state)
    {
        auto [it, inserted] = nodes.try_emplace(state);
        if (inserted)
            it->second.state = state;
        return &it->second;
    }

    // Reconstruct [start, ..., end] into `path` by walking the parent chain.
    // Mirrors MicroPather's GoalReached: the endpoints are always the literal
    // start/end states, and a path of fewer than three nodes collapses to the
    // two endpoints.
    static void reconstruct(Node* goal,
                            State start,
                            State end,
                            std::vector<State>& path)
    {
        int count = 1;
        for (Node* it = goal; it->parent != nullptr; it = it->parent)
            ++count;

        if (count < 3)
        {
            path.resize(2);
            path[0] = start;
            path[1] = end;
            return;
        }

        path.resize(static_cast<std::size_t>(count));
        path[0] = start;
        path[static_cast<std::size_t>(count) - 1] = end;

        int index = count - 2;
        for (Node* it = goal->parent; it->parent != nullptr; it = it->parent)
        {
            path[static_cast<std::size_t>(index)] = it->state;
            --index;
        }
    }

    Graph& graph;
    std::unordered_map<State, Node> nodes;
    std::vector<StateCost> neighbors;
};

AStar::AStar(Graph& graph)
    : impl_(std::make_unique<Impl>(graph))
{
}

AStar::~AStar() = default;
AStar::AStar(AStar&&) noexcept = default;
AStar& AStar::operator=(AStar&&) noexcept = default;

void AStar::reset()
{
    if (!impl_)
        return;
    // Drop the buckets entirely (not just clear()) so reset() truly returns the
    // retained memory, then restore the working reserve for the next solve.
    std::unordered_map<State, Node>().swap(impl_->nodes);
    impl_->nodes.reserve(256);
    std::vector<StateCost>().swap(impl_->neighbors);
}

SolveResult AStar::solve(State start,
                         State end,
                         std::vector<State>& path,
                         float& cost)
{
    path.clear();
    cost = 0.0f;

    if (start == end)
        return SolveResult::StartEndSame;

    // Fresh per-solve bookkeeping; keep the bucket capacity for speed.
    impl_->nodes.clear();

    OpenQueue open;

    Node* start_node = impl_->node_for(start);
    start_node->cost_from_start = 0.0f;
    start_node->est_to_goal = impl_->graph.least_cost_estimate(start, end);
    start_node->total_cost = calc_total_cost(*start_node);
    start_node->parent = nullptr;
    open.push(start_node);

    while (!open.empty())
    {
        Node* node = open.pop();

        if (node->state == end)
        {
            Impl::reconstruct(node, start, end, path);
            cost = node->cost_from_start;
            return SolveResult::Solved;
        }

        node->in_closed = true;

        impl_->neighbors.clear();
        impl_->graph.adjacent_cost(node->state, impl_->neighbors);

        for (const StateCost& edge : impl_->neighbors)
        {
            if (edge.cost == kImpassableCost)
                continue;

            Node* child = impl_->node_for(edge.state);
            const float new_cost = node->cost_from_start + edge.cost;

            if (child->in_open || child->in_closed)
            {
                // Relax only on a strict improvement. A closed node that
                // improves has its cost/parent corrected but is intentionally
                // not re-opened -- matching the algorithm being replaced.
                if (new_cost < child->cost_from_start)
                {
                    child->parent = node;
                    child->cost_from_start = new_cost;
                    // est_to_goal was already computed when the node was first
                    // reached; the Graph contract (astar.h) makes the heuristic
                    // a pure function of (state, end) within one solve, so the
                    // stored value is bit-identical to a recomputation.
                    child->total_cost = calc_total_cost(*child);
                    if (child->in_open)
                        open.update(child);
                }
            }
            else
            {
                child->parent = node;
                child->cost_from_start = new_cost;
                child->est_to_goal =
                    impl_->graph.least_cost_estimate(child->state, end);
                child->total_cost = calc_total_cost(*child);
                open.push(child);
            }
        }
    }

    path.clear();
    return SolveResult::NoSolution;
}

} // namespace og::pathfinding
