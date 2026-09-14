/*
 * GameContext: dependency-injection point for non-global game subsystems.
 *
 * Holds state that doesn't have a legacy global equivalent: RNG interface,
 * input snapshot, and simulation event log. For screen, prefs, and config,
 * use the session globals directly (og::runtime::current_session->...).
 *
 * ctx() is strictly session-backed (no fallback/override path).
 */
#pragma once

#include <cstdint>
#include <memory>

#include <openglad/core/irandom.h>
#include <openglad/gameplay/input_state.h>

namespace og::sim { class SimEventLog; }

// Production RNG: wraps the existing global random() function
class ProductionRandom : public IRandom {
public:
    std::uint32_t next(std::uint32_t max_exclusive) override;
};

// Populate an InputState from the current SDL keyboard/joystick state.
// Called once per frame before game logic runs.
// Defined in game_context.cpp (SDL build) or stubbed by text client.
void input_state_from_sdl(InputState& out);

// ---------------------------------------------------------------------------
// GameContext
// ---------------------------------------------------------------------------

struct GameContext {
    GameContext();
    ~GameContext();
    GameContext(const GameContext&) = delete;
    GameContext& operator=(const GameContext&) = delete;
    GameContext(GameContext&&) noexcept = default;
    GameContext& operator=(GameContext&&) noexcept = default;

    IRandom*    rng         = nullptr;
    InputState  input       = {};

    // Simulation event log: accumulates events during a simulation tick.
    // Owned by GameContext. Simulation code pushes events here; the runtime
    // layer drains and dispatches them after each tick.
    std::unique_ptr<og::sim::SimEventLog> sim_events;

    void poll_input();
};

// ---------------------------------------------------------------------------
// Global context accessor
// ---------------------------------------------------------------------------

GameContext& ctx();

#ifdef TESTING
// Test-only context override. push_test_context snapshots ctx().rng and
// ctx().input ONCE per session (a second push before the matching pop does not
// re-snapshot), then assigns ctx().rng from context->rng -- only when that is
// non-null -- and ctx().input from context->input, and installs
// &context->rng as the GAMEPLAY rng override: walker construction
// (walker_rng()), combat rolls (combat_rng()) and autotiling.
//
// It does NOT touch the SIMULATION stream. living::act, act_random,
// act_guard, walker::death and statistics::try_command draw from
// current_game->world->rng_ (og::sim::SimRandom); script that with
// ScopedSimRandom (tests/test_sim_random_scope.h), which is the only way in.
//
// pop_test_context restores the snapshot and clears the gameplay override.
void push_test_context(GameContext* context);
void pop_test_context();
#endif
