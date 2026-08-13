#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    // used to signal text input
    short text_input_event_ = 0;
    // for scrolling up and down text popups
    short scroll_amount_ = 0;
    // Done with text popups, etc.
    bool input_continue_ = false;
    const bool* keystates_ = nullptr;  // SDL3 SDL_GetKeyboardState buffer
    // == NUM_KEYS from input.h (16 wire-visible actions + the client-side
    // KEY_LOOKUP hold; enforced by input_player_keys()'s reference return).
    static constexpr int kNumKeys = 17;
    int player_keys_[4][kNumKeys] = {};

    // Viewport state (Batch 4) — moved from input.cpp globals.
    // In window coords
    float viewport_offset_x_ = 0;
    float viewport_offset_y_ = 0;
    float window_w_ = 320;
    float window_h_ = 200;
    float viewport_w_ = 320;
    float viewport_h_ = 200;
    // Out of 1.0f, percent of total screen dimension that is cut off.  10% (0.10f) is recommended on OUYA.
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
    // BRIGHTNESS (cfg graphics/brightness, DISPLAY subscreen): the gamma
    // steps set_palette() re-applies to curpal_ on EVERY palette set. This
    // used to be viewscreen::gamma, a per-view member nothing re-applied —
    // so every keyframe palette sync, level start and cheat-palette flip
    // silently reset the player's brightness. Seeded from cfg on first use
    // (display_brightness_steps in pal32.cpp).
    int display_brightness_ = 0;
    bool display_brightness_seeded_ = false;

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
    // persists only the characters owned by one of its own seats
    // (owner_player_index in own_player_indices_).
    bool networked_session_ = false;
    // A local lobby launch also assembles a mission-only roster. Keep that
    // transient copy isolated from the active company exactly as we do for a
    // genuine network session, while retaining local gameplay/control
    // semantics. The copy contains the full company; player-seat teams only
    // select controls and never filter who enters the mission.
    bool isolated_company_session_ = false;
    // This machine's own GLOBAL player slots for the active networked session,
    // one entry per local seat in seat order (seat 0 first). Empty if none.
    std::vector<std::uint8_t> own_player_indices_ = {};
    bool debug_draw_paths_ = false;
    bool debug_draw_obmap_ = false;
    // Developer overlay: when true, score_panel draws measured render FPS in the top-right corner.
    bool show_fps_ = false;

    // Entity-layer state (Phase 4) — moved from guy.cpp and cheat_handler.cpp.
    int guy_id_counter_ = 0;
    std::array<short, 6> changedteam_ = {};

    // Timer anchor (Phase 6) — moved from util.cpp thread_local g_reset_time.
    std::chrono::steady_clock::time_point reset_time_ = std::chrono::steady_clock::now();

    // Picker UI state (Phase 7a) — moved from picker*.cpp globals.
    std::unique_ptr<PickerState> picker_;

    // Level editor state (Phase 7b) — moved from level_editor*.cpp globals.
    std::unique_ptr<LevelEditorState> editor_;

    // Picker state (Batch 8) — moved from picker.cpp globals.
    static constexpr int kNumFamilies = 14;  // == NUM_FAMILIES from constants.h
    std::int32_t current_difficulty_ = 1;
    // Used to label new hires, like "SOLDIER5"
    std::array<std::int32_t, kNumFamilies> numbought_ = {};
    std::unique_ptr<::guy> current_guy_;
    // guy type we're looking at
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
    // True while a loaded replay is being played back: the transport-shadow
    // installs must NOT re-roll weather then — the recorded initial snapshot
    // carries the kind the recording actually played under. Set by
    // initialize_replay_screen, cleared when a live session starts.
    bool replay_playback_active_ = false;

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

// §4.5 networked follow camera: cycle view `view_index`'s watched target
// (Shift = reverse) on a SwitchChar press-edge. Declared here so the render
// layer's input path can reach it; implemented by the platform local
// transport shadow (local_transport_shadow.cpp) against the runtime's
// DisplayFollowState. Returns false (a no-op) when no networked runtime is
// installed or the view is not follow-engaged. Never stamps user tags.
bool display_follow_cycle_target(SessionState& session, int view_index,
                                 bool reverse);

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
