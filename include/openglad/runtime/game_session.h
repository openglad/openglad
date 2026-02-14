#pragma once

#include <cstdint>
#include <memory>

#include <openglad/runtime/game_context.h>

class screen;
class options;
class cfg_store;

namespace og::runtime {

// GameSession: RAII root object for runtime state.
//
// Transitional notes:
// - Many subsystems still reference legacy globals (myscreen, theprefs, cfg).
//   A session can install non-owning shims for those globals, but ownership
//   lives in the session and teardown is explicit.
class GameSession final {
public:
    struct Config {
        short numviews = 1;
        bool install_legacy_globals = true;
        bool install_global_context = true;
        bool allocate_prefs = true;
        bool allocate_seeded_rng = false;
        Uint32 rng_seed = 0;
    };

    explicit GameSession(const Config& cfg);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;

    ::screen* screen_ptr() const { return ctx_.game_screen; }
    options* prefs_ptr() const { return ctx_.prefs.get(); }
    cfg_store* config() const { return ctx_.config; }
    GameContext& context() { return ctx_; }
    const GameContext& context() const { return ctx_; }

private:
    Config cfg_;
    GameContext ctx_;

    // Default RNG for sessions that install a global context but don't opt into a seeded RNG.
    ProductionRandom production_rng_;

    // Optional session-owned RNG (used when allocate_seeded_rng=true).
    std::unique_ptr<SeededRandom> seeded_rng_;

    // Legacy global shims (non-owning).
    ::screen* prev_myscreen_ = nullptr;
    options* prev_theprefs_ = nullptr;

    // Owned runtime state.
    std::unique_ptr<::screen> screen_owner_;
};

} // namespace og::runtime
