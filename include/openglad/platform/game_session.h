#pragma once

#include <cstdint>
#include <memory>

#include <openglad/interface/session_state.h>

struct SDL_Surface;

class screen;
class options;
class cfg_store;
struct InputState;

namespace og::runtime {

struct LocalTransportRuntime;
struct LocalTransportRuntimeAccess;

// GameSession: RAII root object for runtime state.
//
// Owns the screen/prefs objects and installs this session as the active
// runtime context. Also sets up GameContext for RNG / sim events.
class GameSession final : public SessionState {
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
    const cfg_store& config() const;

    // Activate this session: install its globals as current.
    // Returns an RAII guard that restores the previous session on destruction.
    class SessionScope;
    [[nodiscard]] SessionScope activate();

    // Per-session render surface (320x200 32-bit).
    // Non-null only for sessions that don't own the display (create_display=false).
    SDL_Surface* session_surface_ = nullptr;

private:
    friend struct LocalTransportRuntimeAccess;
    friend bool local_transport_active(const SessionState& session) noexcept;
    friend bool local_transport_shadow_is_paused(
        const SessionState& session) noexcept;
    friend void reset_local_transport_shadow(SessionState& session,
                                             screen& gameplay_screen);
    friend void clear_local_transport_shadow(SessionState& session) noexcept;
    friend bool local_transport_shadow_toggle_pause(SessionState& session);
    friend void local_transport_shadow_send_input(SessionState& session,
                                                  const InputState& input,
                                                  std::uint32_t tick);
    friend void local_transport_shadow_finish_tick(SessionState& session);

    Config cfg_;

    // Default RNG for sessions that install a global context but don't opt into a seeded RNG.
    ProductionRandom production_rng_;

    // Optional session-owned RNG (used when allocate_seeded_rng=true).
    std::unique_ptr<SeededRandom> seeded_rng_;

    // Saved previous session pointer for constructor/destructor restore.
    SessionState* prev_session_ = nullptr;
    GameplayContext* prev_game_ = nullptr;

    // Owned runtime state.
    GameWorld world_owner_{};
    std::unique_ptr<options> prefs_owner_;
    std::unique_ptr<::screen> screen_owner_;
    // Session-owned local in-process GameServer/GameClient runtime.
    std::shared_ptr<LocalTransportRuntime> local_transport_runtime_;
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
    SessionState* saved_session_ = nullptr;
    GameplayContext* saved_game_ = nullptr;
    SDL_Surface* saved_render_surface_ = nullptr;
    bool did_swap_render_ = false;
};

} // namespace og::runtime
