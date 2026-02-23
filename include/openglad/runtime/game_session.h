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
// Owns the screen and prefs objects, installs them into legacy globals
// (myscreen, theprefs), and sets up a GameContext for RNG / sim events.
class GameSession final {
public:
    struct Config {
        short numviews = 1;
        // Headless sessions can skip screen allocation to avoid SDL/video init.
        // Note: myscreen will be nullptr in this mode.
        bool allocate_screen = true;
        bool install_legacy_globals = true;
        bool install_global_context = true;
        bool allocate_prefs = true;
        bool allocate_seeded_rng = false;
        std::uint32_t rng_seed = 0;
    };

    explicit GameSession(const Config& cfg);
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    GameSession(GameSession&&) = delete;
    GameSession& operator=(GameSession&&) = delete;

    ::screen* screen_ptr() const;
    options* prefs_ptr() const;
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
    std::unique_ptr<options> prefs_owner_;
    std::unique_ptr<::screen> screen_owner_;
};

} // namespace og::runtime
