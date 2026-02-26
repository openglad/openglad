#pragma once

#include <memory>
#include <vector>

#include <openglad/gameplay/game_world.h>

class walker;

namespace og::sim {
class SimEventLog;
}

namespace og::gameplay {

namespace detail {
class PathfindingStateImpl;
}

class PathfindingState {
public:
    PathfindingState();
    ~PathfindingState();

    PathfindingState(const PathfindingState&) = delete;
    PathfindingState& operator=(const PathfindingState&) = delete;
    PathfindingState(PathfindingState&&) = delete;
    PathfindingState& operator=(PathfindingState&&) = delete;

    void reset();
    void solve_path(walker* actor, std::vector<void*>& path_out, float& total_cost);

private:
    std::unique_ptr<detail::PathfindingStateImpl> impl_;
};

struct GameplayContext {
    GameWorld* world = nullptr;
    og::sim::SimEventLog* sim_events = nullptr;
    PathfindingState* pathfinding = nullptr;
};

extern thread_local GameplayContext* current_game;

} // namespace og::gameplay
