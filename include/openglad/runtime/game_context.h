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

// Forward declarations — avoid pulling in heavy headers
class screen;
class options;
class cfg_store;
namespace og::sim { class SimEventLog; }

// ---------------------------------------------------------------------------
// IRandom — injectable RNG interface
// ---------------------------------------------------------------------------
// Production code uses the global random(). Tests can supply a fixed-seed
// or deterministic implementation.

class IRandom {
public:
    virtual ~IRandom() = default;
    virtual std::uint32_t next(std::uint32_t max_exclusive) = 0;
};

// Production RNG: wraps the existing global random() function
class ProductionRandom : public IRandom {
public:
    std::uint32_t next(std::uint32_t max_exclusive) override;
};

// Test RNG: returns a fixed value or cycles through a sequence
class FixedRandom : public IRandom {
public:
    explicit FixedRandom(std::uint32_t value) : value_(value) {}
    std::uint32_t next(std::uint32_t max_exclusive) override {
        return (max_exclusive == 0) ? 0 : (value_ % max_exclusive);
    }
private:
    std::uint32_t value_;
};

// Seeded RNG: uses a simple LCG for reproducible sequences
class SeededRandom : public IRandom {
public:
    explicit SeededRandom(std::uint32_t seed) : state_(seed) {}
    std::uint32_t next(std::uint32_t max_exclusive) override {
        if (max_exclusive == 0) return 0;
        // LCG: same constants as glibc
        state_ = state_ * 1103515245u + 12345u;
        return (state_ >> 16) % max_exclusive;
    }
    void reset(std::uint32_t seed) { state_ = seed; }
private:
    std::uint32_t state_;
};

// ---------------------------------------------------------------------------
// InputState — per-frame snapshot of what each player is doing
// ---------------------------------------------------------------------------
// Maps to the game's 16 abstract key actions per player (KEY_UP through
// KEY_CHEAT). The "held" array tracks continuously-held keys; "pressed"
// tracks one-shot events for the current frame.

inline constexpr int MAX_PLAYERS = 4;
inline constexpr int NUM_INPUT_KEYS = 16;

// Indices match KEY_UP..KEY_CHEAT from input.h
enum class InputKey : int {
    Up = 0, UpRight = 1, Right = 2, DownRight = 3,
    Down = 4, DownLeft = 5, Left = 6, UpLeft = 7,
    Fire = 8, Special = 9, Switch = 10, SpecialSwitch = 11,
    Yell = 12, Shifter = 13, Prefs = 14, Cheat = 15
};

struct PlayerInput {
    // Held state: true while the key is physically down
    bool held[NUM_INPUT_KEYS] = {};

    // Pressed this frame: true only on the frame the key transitions down
    bool pressed[NUM_INPUT_KEYS] = {};

    // Derived movement direction from held directional keys (-1, 0, or 1)
    int move_x() const;
    int move_y() const;
};

struct InputState {
    PlayerInput players[MAX_PLAYERS] = {};
    bool quit_requested = false;

    void clear();
};

// Populate an InputState from the current SDL keyboard/joystick state.
// Called once per frame before game logic runs.
void input_state_from_sdl(InputState& out);

// ---------------------------------------------------------------------------
// Context services for config/input/render access
// ---------------------------------------------------------------------------

class IConfigContextService {
public:
    virtual ~IConfigContextService() = default;
    virtual cfg_store* config() = 0;
};

class IRenderContextService {
public:
    virtual ~IRenderContextService() = default;
    virtual screen* game_screen() = 0;
    virtual options* prefs() = 0;
};

class IInputContextService {
public:
    virtual ~IInputContextService() = default;
    virtual InputState* input_state() = 0;
    virtual void poll_input() = 0;
};

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
    std::unique_ptr<options> prefs;
    std::string mounted_campaign;
    cfg_store*  config      = nullptr;
    IRandom*    rng         = nullptr;
    InputState  input       = {};
    IConfigContextService* config_service = nullptr;
    IRenderContextService* render_service = nullptr;
    IInputContextService* input_service   = nullptr;

    // Simulation event log: accumulates events during a simulation tick.
    // Owned by GameContext. Simulation code pushes events here; the runtime
    // layer drains and dispatches them after each tick.
    std::unique_ptr<og::sim::SimEventLog> sim_events;

    // Convenience: is this a valid, initialized context?
    bool valid() const { return game_screen != nullptr; }

    screen* active_screen() const;
    options* active_prefs() const;
    cfg_store* active_config() const;
    InputState* active_input();
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
