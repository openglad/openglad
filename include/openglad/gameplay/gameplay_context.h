#pragma once

#include <openglad/core/irandom.h>
#include <openglad/gameplay/game_world.h>

#include <cassert>
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
    using PositionalSoundVisibleFn =
        bool (*)(const walker* source, std::uint32_t sound_id, void* user_data);

    GameWorld* world = nullptr;
    SaveData* save = nullptr;
    og::sim::SimEventLog* sim_events = nullptr;
    cfg_store* config = nullptr;
    IRandom** rng_override_ref = nullptr;
    IRandom** cosmetic_rng_override_ref = nullptr;
    IRandom** session_rng_ref = nullptr;
    bool* gameplay_active_ref = nullptr;
    PositionalSoundVisibleFn positional_sound_visible = nullptr;
    void* positional_sound_visible_user = nullptr;
    std::unique_ptr<GameplayPathfindingState> pathfinding;
};

extern thread_local GameplayContext* current_game;

class GameplayContextGuard
{
public:
    explicit GameplayContextGuard(GameplayContext* ctx)
        : prev_(current_game)
    {
        assert(current_game == nullptr || (current_game == ctx &&
               "Re-entrant GameplayContext installation - context switch bug"));
        current_game = ctx;
    }

    ~GameplayContextGuard()
    {
        current_game = prev_;
    }

    GameplayContextGuard(const GameplayContextGuard&) = delete;
    GameplayContextGuard& operator=(const GameplayContextGuard&) = delete;

private:
    GameplayContext* prev_ = nullptr;
};

GameplayPathfindingState* ensure_pathfinding_state(GameplayContext& context);

// Transitional hook for gameplay code that still needs RNG access outside an
// installed GameplayContext (primarily legacy/test paths).
IRandom* gameplay_rng_override();
void set_gameplay_rng_override(IRandom** rng_ref);

// Cosmetic-RNG stream. Master draws a handful of purely cosmetic / AI-cadence
// values from the C-library rand() rather than the gameplay RNG: walker
// path_check_counter, the FAMILY_HIT FX ani_type, and the elf fireball/rock
// spread jitter. In branch production these draw from the per-world gameplay
// RNG (world.rng_) so they stay deterministic for snapshot/replay/networking.
// The parity harness installs a libc-rand cosmetic override so the captured
// dump matches master's dual-RNG-stream behavior WITHOUT changing production
// determinism. combat math (combat_rng) deliberately does NOT consult this
// override — it stays on world.rng_ on both branch and master.
IRandom* cosmetic_rng_override();
void set_cosmetic_rng_override(IRandom** rng_ref);
// Returns the cosmetic-jitter RNG: the cosmetic override if installed, else
// the gameplay world RNG. Falls back to nullptr only outside any context.
IRandom* cosmetic_rng();

// Session-backed constructors should only consume world RNG while gameplay is
// actively running. UI previews must fall back to the session RNG instead.
bool gameplay_world_rng_active();
IRandom* gameplay_session_rng();
