#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/replay.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/game_loop_state.h>

struct InputHardwareState;
struct PickerState;
struct LevelEditorState;

class screen;
class options;
class cfg_store;
class vbutton;
class guy;

namespace og::runtime {

struct VButtonDeleter {
    void operator()(::vbutton* button) const;
};

// Interface-visible subset of session globals.
// Platform's GameSession derives from this and owns additional lifecycle state.
struct SessionState {
    SessionState();
    virtual ~SessionState();
    [[nodiscard]] virtual bool has_local_transport_runtime() const noexcept;

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
    std::array<std::unique_ptr<::vbutton, VButtonDeleter>, kMaxButtons> owned_buttons_{};

    // Render/palette state (Batch 6) — moved from pal32.cpp/video.cpp.
    std::array<unsigned char, 768> curpal_ = {};
    std::array<unsigned char, 768> temppal_ = {};
    unsigned char* videoptr_ = nullptr;

    // Game speed + debug state (Batch 7) — moved from util.cpp/walker.cpp/obmap.cpp.
    float g_game_speed_factor_ = 1.0f;
    // Render-rate target (not sim cadence). Sim cadence comes from
    // world.timer_wait (master semantics); target_fps_ controls only how
    // often the render path runs. Keep the literal in sync with
    // og::core::kDefaultTargetFps via static_assert in frame_rate_config.cpp.
    int target_fps_ = 60;
    std::int8_t pending_timer_wait_request_ = kNoTimerWaitRequest;
    bool relay_transport_active_ = false;
    bool relay_speed_warning_shown_ = false;
    // True for a genuine networked multiplayer session (host with clients, or a
    // join client). Gates save isolation: the live combined roster is written to
    // a transient slot instead of the player's real save0, and each player
    // persists only its own characters (owner_player_index == own_player_index_).
    bool networked_session_ = false;
    // This peer's own player slot for the active networked session (0xff if none).
    std::uint8_t own_player_index_ = 0xff;
    bool debug_draw_paths_ = false;
    bool debug_draw_obmap_ = false;
    // Developer overlay: when true, score_panel draws measured render FPS in the top-right corner.
    bool show_fps_ = false;

    // Entity-layer state (Phase 4) — moved from guy.cpp and cheat_handler.cpp.
    int guy_id_counter_ = 0;
    std::array<short, 6> changedteam_ = {};

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

    // Help UI state (Phase 12) — moved from help.cpp globals.
    short help_end_of_file_ = 0;

    // Test-only context override snapshot used by push_test_context/pop_test_context.
    bool test_context_active_ = false;
    IRandom* test_context_rng_snapshot_ = nullptr;
    InputState test_context_input_snapshot_ = {};

    // Runtime replay recording state.
    std::optional<og::sim::ReplayRecorder> replay_recorder_ = std::nullopt;
    std::filesystem::path replay_output_path_;

    GameContext ctx_;
    GameplayContext game_;
    GameLoopFrameState frame_state_;
    // True only while mission gameplay is actively running.
    // Window autosave uses this to choose the authoritative state source.
    bool gameplay_active_ = false;
    std::unique_ptr<InputHardwareState> input_hw_;
};

// The currently-active session state. Set by GameSession ctor / SessionScope.
extern thread_local SessionState* current_session;
bool local_transport_active(const SessionState& session) noexcept;

// Non-thread-local atomic pointer to the most recently installed session.
// Used by child threads (e.g. test injector threads) to inherit the session
// when their thread_local current_session is still nullptr.
extern std::atomic<SessionState*> primary_session;
extern std::atomic<GameplayContext*> primary_game;

// If current_session is nullptr on this thread, inherit from primary_session.
// Call at the start of any child thread that needs session access.
inline void ensure_thread_session() {
    if (!current_session) {
        current_session = primary_session.load(std::memory_order_acquire);
    }
}

// If current_game is nullptr on this thread, inherit from primary_game.
// Call at the start of any child thread that needs gameplay access.
inline void ensure_thread_game() {
    if (!current_game) {
        current_game = primary_game.load(std::memory_order_acquire);
    }
}

} // namespace og::runtime
