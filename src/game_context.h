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

#include "SDL.h"
#include <cstdint>
#include <functional>

// Forward declarations — avoid pulling in heavy headers
class screen;
class options;
class cfg_store;

// ---------------------------------------------------------------------------
// IRandom — injectable RNG interface
// ---------------------------------------------------------------------------
// Production code uses the global random(). Tests can supply a fixed-seed
// or deterministic implementation.

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual Uint32 next(Uint32 max_exclusive) = 0;
};

// Production RNG: wraps the existing global random() function
class ProductionRandom : public IRandom {
public:
    Uint32 next(Uint32 max_exclusive) override;
};

// Test RNG: returns a fixed value or cycles through a sequence
class FixedRandom : public IRandom {
public:
    explicit FixedRandom(Uint32 value) : value_(value) {}
    Uint32 next(Uint32 max_exclusive) override {
        return (max_exclusive == 0) ? 0 : (value_ % max_exclusive);
    }
private:
    Uint32 value_;
};

// Seeded RNG: uses a simple LCG for reproducible sequences
class SeededRandom : public IRandom {
public:
    explicit SeededRandom(Uint32 seed) : state_(seed) {}
    Uint32 next(Uint32 max_exclusive) override {
        if (max_exclusive == 0) return 0;
        // LCG: same constants as glibc
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }
    void reset(Uint32 seed) { state_ = seed; }
private:
    Uint32 state_;
};

// ---------------------------------------------------------------------------
// InputState — per-frame snapshot of what each player is doing
// ---------------------------------------------------------------------------
// Phase 2 will flesh this out. For now it's a placeholder so the
// GameContext struct is complete.

inline constexpr int MAX_PLAYERS = 4;

struct PlayerInput {
    float move_x = 0.0f;     // -1..1 horizontal
    float move_y = 0.0f;     // -1..1 vertical
    bool fire = false;
    bool special = false;
    bool yell = false;
    bool shift = false;      // alternate action modifier
    bool prefs = false;       // open preferences
};

struct InputState {
    PlayerInput players[MAX_PLAYERS] = {};
    bool quit_requested = false;
};

// ---------------------------------------------------------------------------
// GameContext
// ---------------------------------------------------------------------------

struct GameContext {
    screen*     game_screen = nullptr;
    options*    prefs       = nullptr;
    cfg_store*  config      = nullptr;
    IRandom*    rng         = nullptr;
    InputState  input       = {};

    // Convenience: is this a valid, initialized context?
    bool valid() const { return game_screen != nullptr; }
};

// ---------------------------------------------------------------------------
// Global context accessor
// ---------------------------------------------------------------------------
// Returns the current global GameContext. In production this wraps the
// existing globals. Tests can call set_global_context() to substitute
// their own.

GameContext& ctx();
void set_global_context(GameContext* context);
