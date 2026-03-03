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
// Test-only context overrides that snapshot and restore the active session
// context (primarily RNG/input).
void push_test_context(GameContext* context);
void pop_test_context();
#endif
