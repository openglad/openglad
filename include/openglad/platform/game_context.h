/*
 * GameContext: dependency-injection point for non-global game subsystems.
 *
 * Holds state that doesn't have a legacy global equivalent: RNG interface,
 * input snapshot, simulation event log, and mounted-campaign tracking.
 * For screen, prefs, and config, use the globals directly (og::runtime::current_session->myscreen_,
 * og::runtime::current_session->theprefs_, cfg).
 *
 * ctx() returns current_session->ctx_.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <openglad/interface/input/input_state.h>
#include <openglad/gameplay/irandom.h>

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

    std::string mounted_campaign;
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
// Test-only override for ctx(). Caller owns `context` and must keep it alive
// until set_global_context(nullptr) clears the override on the same thread.
void set_global_context(GameContext* context);
