/*
 * GameContext: central dependency-injection point for game subsystems.
 *
 * Instead of reaching for globals (myscreen, theprefs, cfg, random()),
 * subsystems can accept a GameContext& and pull what they need from it.
 *
 * Phase 1: the context is a thin wrapper around the existing globals.
 * Call ctx() to get the current global context. Existing code is not
 * yet ported — this module is additive and introduces no behavioral
 * change.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <openglad/input/input_state.h>
#include <openglad/sim/irandom.h>

// Forward declarations — avoid pulling in heavy headers
class screen;
class options;
class cfg_store;
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

    screen*     game_screen = nullptr;
    options*    prefs       = nullptr;  // non-owning; GameSession owns via prefs_owner_
    std::string mounted_campaign;
    cfg_store*  config      = nullptr;
    IRandom*    rng         = nullptr;
    InputState  input       = {};

    // Simulation event log: accumulates events during a simulation tick.
    // Owned by GameContext. Simulation code pushes events here; the runtime
    // layer drains and dispatches them after each tick.
    std::unique_ptr<og::sim::SimEventLog> sim_events;

    // Convenience: is this a valid, initialized context?
    bool valid() const { return game_screen != nullptr; }

    // Direct accessors — no service indirection.
    screen* active_screen() const { return game_screen; }
    options* active_prefs() const { return prefs; }
    cfg_store* active_config() const { return config; }
    InputState* active_input() { return &input; }
    void poll_input();
};

// ---------------------------------------------------------------------------
// Global context accessor
// ---------------------------------------------------------------------------
// Returns the current global GameContext. In production this wraps the
// existing globals. Tests can call set_global_context() to substitute
// their own.

GameContext& ctx();
void set_global_context(GameContext* context);
