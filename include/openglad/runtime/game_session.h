#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <openglad/runtime/game_context.h>
#include <openglad/runtime/game_loop_state.h>

struct SDL_Surface;
struct InputHardwareState;
struct PickerState;
struct LevelEditorState;

class screen;
class options;
class cfg_store;
class vbutton;
class guy;

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

    // Activate this session: install its globals as current.
    // Returns an RAII guard that restores the previous session on destruction.
    class SessionScope;
    [[nodiscard]] SessionScope activate();

    // Per-session state — accessed directly via current_session->member_.
    ::screen* myscreen_ = nullptr;
    options* theprefs_ = nullptr;

    // Input state (Batch 3) — moved from input.cpp globals.
    int raw_key_ = 0;
    std::string raw_text_input_;
    short key_press_event_ = 0;
    short text_input_event_ = 0;
    short scroll_amount_ = 0;
    bool input_continue_ = false;
    const std::uint8_t* keystates_ = nullptr;
    static constexpr int kNumKeys = 16;  // == NUM_KEYS from input.h
    int player_keys_[4][kNumKeys] = {};

    // Viewport state (Batch 4) — moved from input.cpp globals.
    float viewport_offset_x_ = 0;
    float viewport_offset_y_ = 0;
    float window_w_ = 320;
    float window_h_ = 200;
    float viewport_w_ = 320;
    float viewport_h_ = 200;
    float overscan_percentage_ = 0.0f;

    // UI button state (Batch 5) — moved from button.cpp global.
    static constexpr int kMaxButtons = 50;  // == MAX_BUTTONS from button.h
    std::array<::vbutton*, kMaxButtons> allbuttons_ = {};

    // Button ownership (Phase 7d) — moved from button.cpp anonymous namespace.
    std::array<std::unique_ptr<::vbutton>, kMaxButtons> owned_buttons_{};

    // Render/palette state (Batch 6) — moved from pal32.cpp/video.cpp.
    unsigned char curpal_[768] = {};
    unsigned char temppal_[768] = {};
    unsigned char* videoptr_ = nullptr;

    // Game speed + debug state (Batch 7) — moved from util.cpp/walker.cpp/obmap.cpp.
    float g_game_speed_factor_ = 1.0f;
    bool debug_draw_paths_ = false;
    bool debug_draw_obmap_ = false;

    // Entity-layer state (Phase 4) — moved from guy.cpp and cheat_handler.cpp.
    int guy_id_counter_ = 0;
    short changedteam_[6] = {};

    // Keybinding storage (Phase 5) — moved from view.cpp allkeys global.
    int allkeys_[4][kNumKeys] = {};

    // Timer anchor (Phase 6) — moved from util.cpp thread_local g_reset_time.
    std::chrono::steady_clock::time_point reset_time_ = std::chrono::steady_clock::now();

    // Picker UI state (Phase 7a) — moved from picker*.cpp globals.
    std::unique_ptr<PickerState> picker_;

    // Level editor state (Phase 7b) — moved from level_editor*.cpp globals.
    std::unique_ptr<LevelEditorState> editor_;

    // Picker state (Batch 8) — moved from picker.cpp globals.
    static constexpr int kNumFamilies = 14;  // == NUM_FAMILIES from constants.h
    std::int32_t current_difficulty_ = 1;
    std::array<std::int32_t, kNumFamilies> numbought_ = {};
    std::unique_ptr<::guy> current_guy_;
    std::int32_t current_type_ = 0;
    short current_team_num_ = 0;
    ::vbutton* localbuttons_ = nullptr;
    std::int32_t editguy_ = 0;
    std::string message_;

    GameContext ctx_;
    GameLoopFrameState frame_state_;
    std::unique_ptr<InputHardwareState> input_hw_;

    // Per-session render surface (320x200 32-bit).
    // Non-null only for sessions that don't own the display (create_display=false).
    SDL_Surface* session_surface_ = nullptr;

private:
    Config cfg_;

    // Default RNG for sessions that install a global context but don't opt into a seeded RNG.
    ProductionRandom production_rng_;

    // Optional session-owned RNG (used when allocate_seeded_rng=true).
    std::unique_ptr<SeededRandom> seeded_rng_;

    // Saved previous session pointer for constructor/destructor restore.
    GameSession* prev_session_ = nullptr;

    // Owned runtime state.
    std::unique_ptr<options> prefs_owner_;
    std::unique_ptr<::screen> screen_owner_;
};

// The currently-active session.  Code accesses members directly, e.g.
// og::runtime::current_session->myscreen_.  Set by GameSession ctor / SessionScope.
// Thread-local: each thread gets its own session pointer for multi-threaded demos.
extern thread_local GameSession* current_session;

// Non-thread-local atomic pointer to the most recently installed session.
// Used by child threads (e.g. test injector threads) to inherit the session
// when their thread_local current_session is still nullptr.
extern std::atomic<GameSession*> primary_session;

// If current_session is nullptr on this thread, inherit from primary_session.
// Call at the start of any child thread that needs session access.
inline void ensure_thread_session() {
    if (!current_session) {
        current_session = primary_session.load(std::memory_order_acquire);
    }
}

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
    GameSession* saved_session_ = nullptr;
    SDL_Surface* saved_render_surface_ = nullptr;
    bool did_swap_render_ = false;
};

} // namespace og::runtime
