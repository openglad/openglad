#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <openglad/interface/session_state.h>

struct SDL_Surface;

class screen;
class options;
class cfg_store;
struct InputState;

namespace og::sim {
class ITransport;
class InProcessTransport;
struct LobbyPlayerBinding;
using PeerId = std::uint32_t;
}

namespace og::runtime {

struct LocalTransportRuntime;

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
    [[nodiscard]] bool has_local_transport_runtime() const noexcept override;

    // Activate this session: install its globals as current.
    // Returns an RAII guard that restores the previous session on destruction.
    class SessionScope;
    [[nodiscard]] SessionScope activate();

    // Per-session render surface (320x200 32-bit).
    // Non-null only for sessions that don't own the display (create_display=false).
    SDL_Surface* session_surface_ = nullptr;

private:
    friend std::size_t local_transport_client_count(
        const GameSession& session) noexcept;
    friend bool local_transport_shadow_is_paused(
        const GameSession& session) noexcept;
    friend void reset_local_transport_shadow(GameSession& session,
                                             screen& gameplay_screen);
    friend void reset_network_host_transport_shadow(
        GameSession& session,
        screen& gameplay_screen,
        std::shared_ptr<og::sim::ITransport> server_transport,
        std::shared_ptr<og::sim::InProcessTransport> local_client_transport,
        const std::vector<og::sim::LobbyPlayerBinding>& player_bindings);
    friend void reset_network_client_transport_shadow(
        GameSession& session,
        screen& gameplay_screen,
        std::shared_ptr<og::sim::ITransport> client_transport,
        og::sim::PeerId server_peer_id);
    friend void clear_local_transport_shadow(GameSession& session) noexcept;
    friend bool local_transport_shadow_toggle_pause(GameSession& session);
    friend void local_transport_shadow_send_input(GameSession& session,
                                                  const InputState& input,
                                                  std::uint32_t tick);
    friend void local_transport_shadow_finish_tick(GameSession& session);

    Config cfg_;

    // Default RNG for sessions that install a global context but don't opt into a seeded RNG.
    ProductionRandom production_rng_;

    // Optional session-owned RNG (used when allocate_seeded_rng=true).
    std::unique_ptr<SeededRandom> seeded_rng_;

    // Saved previous session pointer for constructor/destructor restore.
    SessionState* prev_session_ = nullptr;
    GameplayContext* prev_game_ = nullptr;
    GameSession* prev_game_session_ = nullptr;

    // Owned runtime state.
    GameWorld world_owner_{};
    std::unique_ptr<options> prefs_owner_;
    std::unique_ptr<::screen> screen_owner_;
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
    GameSession* saved_game_session_ = nullptr;
    SDL_Surface* saved_render_surface_ = nullptr;
    bool did_swap_render_ = false;
};

extern thread_local GameSession* current_game_session;

} // namespace og::runtime
