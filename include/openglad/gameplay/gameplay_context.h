#pragma once

#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>

#include <memory>
#include <vector>

class cfg_store;
class walker;
struct SaveData;

namespace og::sim {
class SimEventLog;
}

class GameplayPathfindingState
{
public:
    GameplayPathfindingState();
    ~GameplayPathfindingState();

    GameplayPathfindingState(const GameplayPathfindingState&) = delete;
    GameplayPathfindingState& operator=(const GameplayPathfindingState&) = delete;
    GameplayPathfindingState(GameplayPathfindingState&&) noexcept;
    GameplayPathfindingState& operator=(GameplayPathfindingState&&) noexcept;

    void solve_for(walker* owner,
                   void* start_state,
                   void* end_state,
                   std::vector<void*>& path_out,
                   float& total_cost);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct GameplayContext
{
    GameWorld* world = nullptr;
    SaveData* save = nullptr;
    og::sim::SimEventLog* sim_events = nullptr;
    cfg_store* config = nullptr;
    IRandom** rng_override_ref = nullptr;
    IRandom** session_rng_ref = nullptr;
    bool* gameplay_active_ref = nullptr;
    std::unique_ptr<GameplayPathfindingState> pathfinding;
};

extern thread_local GameplayContext* current_game;

GameplayPathfindingState* ensure_pathfinding_state(GameplayContext& context);

// Transitional hook for gameplay code that still needs RNG access outside an
// installed GameplayContext (primarily legacy/test paths).
IRandom* gameplay_rng_override();
void set_gameplay_rng_override(IRandom** rng_ref);

// Session-backed constructors should only consume world RNG while gameplay is
// actively running. UI previews must fall back to the session RNG instead.
bool gameplay_world_rng_active();
IRandom* gameplay_session_rng();
