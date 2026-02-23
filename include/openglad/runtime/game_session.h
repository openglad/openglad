#pragma once

#include <cstdint>
#include <memory>

#include <openglad/runtime/game_context.h>
#include <openglad/runtime/game_loop.h>

struct SDL_Surface;

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
        // When false, the session's screen shares the existing display
        // instead of creating its own SDL window. Used for sub-sessions
        // in the multi-instance demo.
        bool create_display = true;
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

    GameLoopFrameState& frame_state() { return frame_state_; }

    // Per-session render surface (320x200 32-bit).
    // Non-null only for sessions that don't own the display (create_display=false).
    SDL_Surface* render_surface() const { return session_surface_; }

    // Activate this session: install its globals as current.
    // Returns an RAII guard that restores the previous session on destruction.
    class SessionScope;
    [[nodiscard]] SessionScope activate();

private:
    Config cfg_;
    GameContext ctx_;
    GameLoopFrameState frame_state_;

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

    // Per-session 320x200 render target for sub-sessions sharing a display.
    SDL_Surface* session_surface_ = nullptr;
};

// RAII guard: while alive, the associated session's globals are installed.
// On destruction, previous globals are restored.
class GameSession::SessionScope {
public:
    ~SessionScope();
    SessionScope(const SessionScope&) = delete;
    SessionScope& operator=(const SessionScope&) = delete;
    SessionScope(SessionScope&& other) noexcept;
    SessionScope& operator=(SessionScope&&) = delete;

private:
    friend class GameSession;
    explicit SessionScope(GameSession& session);

    GameSession* session_ = nullptr;
    ::screen* saved_myscreen_ = nullptr;
    options* saved_theprefs_ = nullptr;
    GameContext* saved_context_ = nullptr;
    SDL_Surface* saved_render_surface_ = nullptr;
};

} // namespace og::runtime
