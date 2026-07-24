#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/ui/level_editor_state.h>
#include <openglad/platform/game_session.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"

#include <atomic>

// myscreen is now a macro defined in base.h (via game_session.h)

static inline LevelEditorState& eds() { return *og::runtime::current_session->editor_; }

// From level_editor.cpp
Sint32 level_editor();

// From picker_dialogs.cpp (TESTING): queue answers for yes_or_no_prompt().
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

struct EditorThreadState {
    bool started;
    bool finished;
};

static int editor_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    EditorThreadState* st = static_cast<EditorThreadState*>(data);
    st->started = true;

    // Give the editor time to initialize and enter its main loop.
    SDL_Delay(300);

    // Open Level menu, toggle goal flags (Level > Goals > toggles).
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 85, 20);   // Goals >
    SDL_Delay(30);
    inject_click(200, 85, 20);  // Defeat enemies toggle
    inject_click(200, 105, 20); // Beat generators toggle
    inject_click(200, 125, 20); // Protect NPCs toggle

    // Open Level > Details submenu and exercise prompt_for_string (TESTING fast path).
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 65, 20);   // Details >
    SDL_Delay(30);
    inject_click(200, 85, 20);  // Par value...
    inject_click(200, 105, 20); // Time limit...
    inject_click(200, 65, 20);  // Map size...

    // Open Level > Profile submenu and exercise title/description prompts.
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 45, 20);   // Profile >
    SDL_Delay(30);
    inject_click(200, 45, 20);  // Title...
    inject_click(200, 65, 20);  // Description...

    // Level info / resmooth / clear paths.
    SDL_Delay(30);
    inject_click(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click(90, 25, 20);   // Info...
    SDL_Delay(30);
    inject_click(90, 105, 20);  // Resmooth terrain
    inject_click(90, 125, 20);  // Clear all terrain
    inject_click(90, 145, 20);  // Clear all objects

    // Campaign menu paths (info/profile/details/validate).
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign (top menu)
    SDL_Delay(30);
    inject_click(45, 25, 20);   // Info...
    SDL_Delay(30);
    inject_click(45, 45, 20);   // Profile >
    SDL_Delay(30);
    inject_click(120, 45, 20);  // Title...
    inject_click(120, 65, 20);  // Description...
    inject_click(120, 85, 20);  // Authors...
    inject_click(120, 105, 20); // Contributors...
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign
    SDL_Delay(30);
    inject_click(45, 65, 20);   // Details >
    SDL_Delay(30);
    inject_click(120, 65, 20);  // Version...
    inject_click(120, 85, 20);  // Suggested power...
    inject_click(120, 105, 20); // First level...
    SDL_Delay(30);
    inject_click(45, 10, 20);   // Campaign
    SDL_Delay(30);
    inject_click(45, 85, 20);   // Validate

    // Mode menu selections.
    SDL_Delay(30);
    inject_click(140, 10, 20);  // Edit (top menu)
    SDL_Delay(30);
    inject_click(140, 25, 20);  // Terrain mode
    inject_click(140, 45, 20);  // Object mode
    inject_click(140, 65, 20);  // Select mode

    // Mode toggles and a couple keypaths.
    SDL_Delay(30);
    inject_key_press(SDLK_O, 10); // Terrain -> Object
    inject_key_press(SDLK_T, 10); // Object -> Terrain
    inject_key_press(SDLK_O, 10); // Terrain -> Object
    inject_key_press(SDLK_RIGHTBRACKET, 10);
    inject_key_press(SDLK_LEFTBRACKET, 10);

    inject_key_press(SDLK_O, 10); // Object -> Select
    inject_key_press(SDLK_DELETE, 10);

    // Trigger resmooth (F5) and palette load (F9) paths.
    inject_key_press(SDLK_F5, 10);
    inject_key_press(SDLK_F9, 10);
    inject_key_press(SDLK_G, 10);
    inject_key_press(SDLK_0, 10);
    inject_key_press(SDLK_1, 10);
    inject_key_press(SDLK_2, 10);
    inject_key_press(SDLK_3, 10);
    inject_key_press(SDLK_4, 10);
    inject_key_press(SDLK_5, 10);
    inject_key_press(SDLK_6, 10);
    inject_key_press(SDLK_7, 10);
    inject_key_press(SDLK_W, 10);
    inject_key_press(SDLK_A, 10);
    inject_key_press(SDLK_S, 10);
    inject_key_press(SDLK_D, 10);

    // File menu paths.
    SDL_Delay(30);
    inject_click(15, 10, 20);   // File
    SDL_Delay(30);
    inject_click(15, 25, 20);   // Campaign >
    SDL_Delay(30);
    inject_click(85, 25, 20);   // New
    inject_click(85, 45, 20);   // Import...
    inject_click(85, 65, 20);   // Share...
    inject_click(85, 85, 20);   // Load...
    inject_click(85, 105, 20);  // Save
    inject_click(85, 125, 20);  // Save As...
    SDL_Delay(30);
    inject_click(15, 10, 20);   // File
    SDL_Delay(30);
    inject_click(15, 45, 20);   // Level >
    SDL_Delay(30);
    inject_click(85, 45, 20);   // New
    inject_click(85, 65, 20);   // Load...
    inject_click(85, 85, 20);   // Save
    inject_click(85, 105, 20);  // Save As...

    // Force the ESC quit prompt path (TESTING returns default without blocking).
    eds().levelchanged = 1;
    eds().campaignchanged = 1;

    // Right-click pick path.
    SDL_Event right_down{};
    right_down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    right_down.button.button = SDL_BUTTON_RIGHT;
    right_down.button.down = true;
    right_down.button.x = 120;
    right_down.button.y = 100;
    SDL_PushEvent(&right_down);
    SDL_Delay(10);
    SDL_Event right_up{};
    right_up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    right_up.button.button = SDL_BUTTON_RIGHT;
    right_up.button.down = false;
    right_up.button.x = 120;
    right_up.button.y = 100;
    SDL_PushEvent(&right_up);

    inject_key_press(SDLK_ESCAPE, 10);

    // Push direct SDL event variants to cover handle_basic_editor_event branches.
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_EXPOSED;
    SDL_PushEvent(&event);

    inject_text_input("x");

    event = SDL_Event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = 1;
    event.wheel.integer_y = 1;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_FINGER_DOWN;
    event.tfinger.dx = 0.1f;
    event.tfinger.dy = 0.1f;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_FINGER_MOTION;
    event.tfinger.dx = 0.2f;
    event.tfinger.dy = -0.15f;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_FINGER_UP;
    event.tfinger.dx = 0.0f;
    event.tfinger.dy = 0.0f;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 2;
    event.motion.yrel = -3;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
    event.jaxis.axis = 0;
    event.jaxis.value = 10000;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
    event.jbutton.button = 0;
    SDL_PushEvent(&event);

    event = SDL_Event{};
    event.type = SDL_EVENT_JOYSTICK_BUTTON_UP;
    event.jbutton.button = 0;
    SDL_PushEvent(&event);

    // Exercise a click in the main window.
    inject_click(100, 100, 10);

    // Let it draw a few frames, then request exit.
    SDL_Delay(300);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}

TEST(LevelEditorInteractions, level_editor_runs_and_handles_basic_input)
{
    // Ensure the editor loop runs for a short period.
    og::runtime::current_session->myscreen_->world().end = 0;

    EditorThreadState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(editor_injector_thread, "editor_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    // This blocks until myscreen->end is set by the injector.
    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    // Reset end flag for subsequent tests.
    og::runtime::current_session->myscreen_->world().end = 0;

    ASSERT_TRUE(st.started) << "injector thread should have started";
    ASSERT_TRUE(st.finished) << "injector thread should have finished";
}

namespace
{
struct EditorDecorToggleThreadState
{
    std::atomic<bool> stop{false};
    bool events_queued = false;
};

class EditorDecorStateGuard
{
public:
    EditorDecorStateGuard()
        : saved_editor_state_(eds()),
          saved_input_state_(input_hardware_state()),
          saved_end_(og::runtime::current_session->myscreen_->world().end)
    {
        trace_clear();
        eds().levelchanged = 0;
        eds().campaignchanged = 0;
        eds().decor_mode = false;
        og::runtime::current_session->myscreen_->world().end = 0;
    }

    ~EditorDecorStateGuard()
    {
        SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
        input_hardware_state() = std::move(saved_input_state_);
        eds() = std::move(saved_editor_state_);
        og::runtime::current_session->myscreen_->world().end = saved_end_;
        trace_clear();
    }

private:
    LevelEditorState saved_editor_state_;
    InputHardwareState saved_input_state_;
    char saved_end_;
};

class JoinedSdlThread
{
public:
    JoinedSdlThread(
        SDL_Thread* thread, EditorDecorToggleThreadState& state)
        : thread_(thread), state_(state) {}
    ~JoinedSdlThread() { (void)join(); }

    bool valid() const { return thread_ != nullptr; }

    int join()
    {
        state_.stop.store(true, std::memory_order_release);
        if (thread_ != nullptr)
        {
            SDL_WaitThread(thread_, &result_);
            thread_ = nullptr;
        }
        return result_;
    }

private:
    SDL_Thread* thread_;
    EditorDecorToggleThreadState& state_;
    int result_ = 1;
};

bool push_checked_key_press(SDL_Keycode key)
{
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.key = key;
    down.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    down.key.down = true;
    if (!SDL_PushEvent(&down))
        return false;

    SDL_Delay(10);
    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    return SDL_PushEvent(&up);
}

int editor_decor_toggle_injector(void* opaque)
{
    og::runtime::ensure_thread_session();
    auto& state = *static_cast<EditorDecorToggleThreadState*>(opaque);
    const Uint64 entry_deadline = SDL_GetTicks() + 5000u;
    while (!trace_contains("canvas", "editor_pin_classic"))
    {
        if (SDL_GetTicks() >= entry_deadline)
        {
            (void)push_checked_key_press(SDLK_ESCAPE);
            return 1;
        }
        SDL_Delay(1);
    }

    // O followed by T reaches Terrain mode from every possible current mode.
    bool all_events_queued =
        push_checked_key_press(SDLK_O) &&
        push_checked_key_press(SDLK_T) &&
        push_checked_key_press(SDLK_B);

    // Keep a checked Escape event available until the real editor loop exits.
    // This is event-only: the worker never races a product-state write.
    while (!state.stop.load(std::memory_order_acquire))
    {
        const bool escape_queued = push_checked_key_press(SDLK_ESCAPE);
        all_events_queued = escape_queued && all_events_queued;
        SDL_Delay(10);
    }
    const bool stopped = state.stop.load(std::memory_order_acquire);
    state.events_queued = all_events_queued && stopped;
    return state.events_queued ? 0 : 1;
}
} // namespace

TEST(LevelEditorInteractions, terrain_decor_key_toggles_through_real_event_loop)
{
    EditorDecorStateGuard state_guard;
    EditorDecorToggleThreadState state;
    JoinedSdlThread thread(
        SDL_CreateThread(
            editor_decor_toggle_injector, "editor_decor_toggle", &state),
        state);
    if (!thread.valid())
    {
        FAIL() << "failed to create the editor event injector";
        return;
    }

    (void)level_editor();

    const int thread_result = thread.join();
    const bool decor_mode_after_key = eds().decor_mode;

    EXPECT_EQ(0, thread_result);
    EXPECT_TRUE(state.events_queued);
    EXPECT_TRUE(decor_mode_after_key)
        << "B must switch a freshly entered editor from base to decor painting";
}


namespace
{
static void push_mouse_motion(int x, int y, int xrel = 0, int yrel = 0)
{
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = static_cast<float>(x);
    e.motion.y = static_cast<float>(y);
    e.motion.xrel = static_cast<float>(xrel);
    e.motion.yrel = static_cast<float>(yrel);
    SDL_PushEvent(&e);
}

static void push_mouse_drag(int x0, int y0, int x1, int y1)
{
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = static_cast<float>(x0);
    down.button.y = static_cast<float>(y0);
    SDL_PushEvent(&down);

    SDL_Delay(10);
    push_mouse_motion(x1, y1, x1 - x0, y1 - y0);
    SDL_Delay(10);

    SDL_Event up{};
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.button = SDL_BUTTON_LEFT;
    up.button.down = false;
    up.button.x = static_cast<float>(x1);
    up.button.y = static_cast<float>(y1);
    SDL_PushEvent(&up);
}

static int editor_edit_smoke_injector(void* data)
{
    og::runtime::ensure_thread_session();
    EditorThreadState* st = static_cast<EditorThreadState*>(data);
    st->started = true;

    SDL_Delay(300);

    // Enter terrain mode and paint a couple of tiles.
    inject_key_press(SDLK_T, 10);
    inject_key_press(SDLK_G, 10);  // toggle grid alignment
    inject_key_press(SDLK_1, 10);
    inject_click(160, 120, 10);
    inject_key_press(SDLK_2, 10);
    push_mouse_drag(170, 120, 190, 120);

    // Smooth current map path (F5), then toggle grid again.
    inject_key_press(SDLK_F5, 10);
    inject_key_press(SDLK_G, 10);

    // Switch to object mode, cycle brush, and place an object.
    inject_key_press(SDLK_O, 10);
    inject_key_press(SDLK_RIGHTBRACKET, 10);
    inject_key_press(SDLK_LEFTBRACKET, 10);
    inject_click(200, 130, 10);

    // Select mode: click/drag selection area and delete selection/object.
    inject_key_press(SDLK_O, 10);  // object -> select
    push_mouse_drag(190, 120, 210, 140);
    inject_key_press(SDLK_DELETE, 10);

    // Exit editor.
    SDL_Delay(200);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}
} // namespace

TEST(LevelEditorInteractions, level_editor_edits_terrain_and_places_objects_smoke)
{
    og::runtime::current_session->myscreen_->world().end = 0;

    EditorThreadState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(editor_edit_smoke_injector, "editor_edit_smoke", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    og::runtime::current_session->myscreen_->world().end = 0;

    ASSERT_TRUE(st.started) << "injector thread should have started";
    ASSERT_TRUE(st.finished) << "injector thread should have finished";
}


namespace
{
// Injected SDL mouse events carry *window* coordinates; the input layer maps
// them back to 320x200 game coordinates through the viewport transform. The
// default test window is 640x400, so raw game coordinates would land at half
// position. These helpers apply the same game->window mapping the editor's
// controller-input path uses, so callers can think in game coordinates.
static int game_to_window_x(int gx)
{
    // UI-canvas-pinned map — raw viewport_*/320 math ignores the aspect-fit
    // letterbox and mismaps in non-16:10 windows (see test_interact.h). In
    // the default 640x400 window the two are identical.
    return static_cast<int>(
        ui_canvas_to_window(static_cast<float>(gx), 0.0f).first);
}

static int game_to_window_y(int gy)
{
    return static_cast<int>(
        ui_canvas_to_window(0.0f, static_cast<float>(gy)).second);
}

static void inject_click_game(int gx, int gy, int delay_ms = 20)
{
    inject_click(game_to_window_x(gx), game_to_window_y(gy), delay_ms);
}

static void push_mouse_motion_game(int gx, int gy, int gxrel, int gyrel)
{
    push_mouse_motion(game_to_window_x(gx), game_to_window_y(gy),
                      game_to_window_x(gxrel) - game_to_window_x(0),
                      game_to_window_y(gyrel) - game_to_window_y(0));
}

static int editor_ai_cycle_injector(void* data)
{
    og::runtime::ensure_thread_session();
    EditorThreadState* st = static_cast<EditorThreadState*>(data);
    st->started = true;

    SDL_Delay(300);

    // Clear the stock level objects so the rect-select below grabs exactly
    // the soldier this test places (queued answer accepts the prompt).
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    inject_click_game(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click_game(90, 145, 20);  // Clear all objects
    SDL_Delay(30);

    // Object mode with a known brush: click the picker pane's first cell
    // (Living / soldier), regardless of what earlier tests left behind.
    inject_key_press(SDLK_T, 10);   // -> Terrain
    inject_key_press(SDLK_O, 10);   // Terrain -> Object
    eds().rowsdown = 0;
    SDL_Delay(30);
    inject_click_game(246, 81, 20); // object pane cell (0,0): soldier brush
    SDL_Delay(30);

    // Place the soldier. The first click either places a decoy far from the
    // select rect or disarms a leftover pick toggle; the second click always
    // places a fresh (ROAM) soldier near game coords (160,120).
    inject_click_game(120, 60, 20);
    SDL_Delay(30);
    inject_click_game(160, 120, 20);
    SDL_Delay(30);

    // Select mode: rect-select the placed soldier. The first small motion
    // anchors the rectangle near the press before stretching it.
    inject_key_press(SDLK_O, 10);   // Object -> Select
    SDL_Delay(30);
    inject_mouse_down(game_to_window_x(130), game_to_window_y(95));
    SDL_Delay(20);
    push_mouse_motion_game(135, 100, 5, 5);
    SDL_Delay(20);
    push_mouse_motion_game(200, 155, 65, 55);
    SDL_Delay(20);
    inject_mouse_up(game_to_window_x(200), game_to_window_y(155));
    SDL_Delay(60);

    // The "AI >" cycle button sits right of "Facing >" in the select panel.
    // Three clicks walk ROAM -> GUARD -> HOLD -> ROAM.
    inject_click_game(60, 112, 20);
    SDL_Delay(100);
    inject_click_game(60, 112, 20);
    SDL_Delay(100);
    inject_click_game(60, 112, 20);
    SDL_Delay(100);

    SDL_Delay(200);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}
} // namespace

TEST(LevelEditorInteractions, level_editor_ai_button_cycles_roam_guard_hold)
{
    og::runtime::current_session->myscreen_->world().end = 0;
    trace_clear();

    EditorThreadState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(editor_ai_cycle_injector, "editor_ai_cycle", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    og::runtime::current_session->myscreen_->world().end = 0;

    ASSERT_TRUE(st.started) << "injector thread should have started";
    ASSERT_TRUE(st.finished) << "injector thread should have finished";

    ASSERT_TRUE(trace_contains("editor", "ai_cycle to=GUARD act=3 hold=0"))
        << "first AI click should author GUARD (ACT_GUARD, wake-on-sight)";
    ASSERT_TRUE(trace_contains("editor", "ai_cycle to=HOLD act=3 hold=1"))
        << "second AI click should author HOLD (ACT_GUARD + guard_hold_post)";
    ASSERT_TRUE(trace_contains("editor", "ai_cycle to=ROAM act=0 hold=0"))
        << "third AI click should return to ROAM (ACT_RANDOM)";
}
