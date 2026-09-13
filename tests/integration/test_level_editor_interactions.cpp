#include <openglad/interface/screen.h>
#include <openglad/interface/input.h>
#include <openglad/interface/ui/level_editor_state.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/platform/game_session.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/terrain_types.h>
#include "test_input_helpers.h"

#include <atomic>
#include <string>
#include <vector>

// myscreen is now a macro defined in base.h (via game_session.h)

static inline LevelEditorState& eds() { return *og::runtime::current_session->editor_; }

// From level_editor.cpp
Sint32 level_editor();
// From level_editor.cpp (TESTING): object-brush team-range regression seam.
int level_editor_test_object_brush_team_range();

// From picker_dialogs.cpp (TESTING): queue answers for yes_or_no_prompt().
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

// From level_editor_ui.cpp (TESTING): queue answers for prompt_for_string().
void level_editor_testing_prompt_queue_clear();
void level_editor_testing_prompt_queue_push(const char* s);

// From level_editor.cpp (TESTING): the editor's LevelEditorData is a
// function-local static, so these are the only handles a test driving the real
// event loop has on the level and brushes it just edited. The scalars may be
// polled from an injector thread (each is written only in response to input
// that injector itself sent); the level pointer is read after the loop returns.
LevelRuntimeData* level_editor_testing_level();
int level_editor_testing_level_type();
int level_editor_testing_level_par_value();
int level_editor_testing_mode();
int level_editor_testing_terrain_brush();
int level_editor_testing_object_brush_order();
int level_editor_testing_object_brush_family();


struct EditorThreadState {
    bool started;
    bool finished;
};

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

// Wait-on-condition helpers with generous ceilings. Each returns false if the
// condition never arrives, so a broken editor fails the test instead of
// hanging it.
bool wait_for_trace_line(const char* category, const char* needle, Uint32 ceiling_ms)
{
    const Uint64 deadline = SDL_GetTicks() + ceiling_ms;
    while (!trace_contains(category, needle))
    {
        if (SDL_GetTicks() >= deadline)
            return false;
        SDL_Delay(1);
    }
    return true;
}

// The editor has consumed everything we queued once SDL's queue is empty.
bool wait_for_drained_event_queue(Uint32 ceiling_ms)
{
    const Uint64 deadline = SDL_GetTicks() + ceiling_ms;
    while (SDL_HasEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP) ||
           SDL_HasEvents(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP) ||
           SDL_HasEvent(SDL_EVENT_MOUSE_WHEEL))
    {
        if (SDL_GetTicks() >= deadline)
            return false;
        SDL_Delay(1);
    }
    return true;
}

constexpr Uint32 kEditorEntryCeilingMs = 10000u;
constexpr Uint32 kEditorDrainCeilingMs = 10000u;
// How long one authored edit may take to appear once the pump has drained the
// events that carry it. Bounds a dead editor, not a slow one.
constexpr Uint32 kEditorEditCeilingMs = 2000u;

template <typename Pred>
bool wait_until(Pred pred, Uint32 ceiling_ms)
{
    const Uint64 deadline = SDL_GetTicks() + ceiling_ms;
    while (!pred())
    {
        if (SDL_GetTicks() >= deadline)
            return false;
        SDL_Delay(1);
    }
    return true;
}

// Queue one click / key press and wait for the editor's own pump to consume it.
bool click_settled(int gx, int gy)
{
    inject_click_game(gx, gy, 20);
    return wait_for_drained_event_queue(kEditorDrainCeilingMs);
}

bool key_settled(SDL_Keycode key)
{
    inject_key_press(static_cast<int>(key), 10);
    return wait_for_drained_event_queue(kEditorDrainCeilingMs);
}

// `send` must be an IDEMPOTENT chain (a menu walk, a brush pick, a paint
// stroke): it is repeated until `arrived` reports the value the editor writes
// when it consumes it. Never wrap a toggle in this — a second pass undoes it.
template <typename Send, typename Arrived>
bool retry_until(Send send, Arrived arrived)
{
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        if (!send())
            return false;
        if (wait_until(arrived, kEditorEditCeilingMs))
            return true;
    }
    return false;
}

// Editor menu geometry in GAME coordinates. LevelEditorData's constructor
// stacks every one of these from S_RIGHT / OVERSCAN_PADDING and a 20px row
// height; the pane cells come from S_RIGHT + col*GRID_SIZE, PIX_TOP +
// row*GRID_SIZE.
constexpr int kFileX = 15, kFileY = 10;                    // File
constexpr int kFileLevelX = 32, kFileLevelY = 50;          // File > Level >
constexpr int kFileLevelNewX = 97, kFileLevelNewY = 50;    // File > Level > New
constexpr int kLevelX = 105, kLevelY = 10;                 // Level
constexpr int kLevelGoalsX = 105, kLevelGoalsY = 90;       // Level > Goals >
constexpr int kGoalEnemiesX = 255, kGoalEnemiesY = 90;     // ... Defeat enemies
constexpr int kGoalGeneratorsX = 255, kGoalGeneratorsY = 110;
constexpr int kGoalNpcsX = 255, kGoalNpcsY = 130;
constexpr int kLevelDetailsX = 105, kLevelDetailsY = 70;   // Level > Details >
constexpr int kParValueX = 255, kParValueY = 90;           // ... Par value...
// Tile pane cell (col 3, row 2) with rowsdown 0 = kDefaultBackgrounds[11],
// PIX_WATER1 — a genre the all-grass grid of a new level never holds.
constexpr int kTilePaneWaterX = 296, kTilePaneWaterY = 113;
// Object pane cell (0, 0) = Living / family 0 (FAMILY_SOLDIER).
constexpr int kObjectPaneFirstX = 246, kObjectPaneFirstY = 81;
// A map cell clear of every panel button, the menu bar and the minimap.
constexpr int kMapCellX = 160, kMapCellY = 120;

constexpr int kGoalsAll = GameWorld::TYPE_CAN_EXIT_WHENEVER |
                          GameWorld::TYPE_MUST_DESTROY_GENERATORS |
                          GameWorld::TYPE_MUST_PROTECT_NAMED_NPCS;

// eds().levelchanged is the editor's dirty flag and the only thing a paint
// stroke or an object placement publishes outside the editor's own level. The
// injector zeroes it just before a stroke so the flip back to 1 acknowledges
// THAT stroke; the write is safe because it happens only after the previous
// step's queue drained and its own acknowledgement arrived, so the editor is
// idle (same discipline as the eds().rowsdown writes the older injectors make).
bool stroke_dirties_the_level(int gx, int gy)
{
    eds().levelchanged = 0;
    return retry_until([gx, gy]() { return click_settled(gx, gy); },
                       []() { return eds().levelchanged == 1; });
}

// File > Level > New: LevelRuntimeData::clear() retitles the level "New Level",
// zeroes the goal bits and resets par to 1, and the handler dirties the level.
bool author_new_level()
{
    return retry_until(
        []() {
            // Armed for the "Discard unsaved changes?" prompt a retry can meet.
            picker_testing_yes_or_no_queue_push(true);
            return click_settled(kFileX, kFileY) &&
                   click_settled(kFileLevelX, kFileLevelY) &&
                   click_settled(kFileLevelNewX, kFileLevelNewY);
        },
        []() {
            return eds().levelchanged == 1 &&
                   level_editor_testing_level_par_value() == 1 &&
                   level_editor_testing_level_type() == 0;
        });
}

int editor_menu_authoring_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    bool ok = wait_for_trace_line("canvas", "editor_pin_classic",
                                  kEditorEntryCeilingMs);

    if (ok)
        ok = author_new_level();

    // Level > Goals: each row XORs one goal bit into world().type. A toggle
    // cannot be retried, so every click is acknowledged by its own bit.
    if (ok)
        ok = click_settled(kLevelX, kLevelY) &&
             click_settled(kLevelGoalsX, kLevelGoalsY) &&
             click_settled(kGoalEnemiesX, kGoalEnemiesY) &&
             wait_until([]() {
                 return level_editor_testing_level_type() ==
                        GameWorld::TYPE_CAN_EXIT_WHENEVER;
             }, kEditorEditCeilingMs) &&
             click_settled(kGoalGeneratorsX, kGoalGeneratorsY) &&
             wait_until([]() {
                 return level_editor_testing_level_type() ==
                        (GameWorld::TYPE_CAN_EXIT_WHENEVER |
                         GameWorld::TYPE_MUST_DESTROY_GENERATORS);
             }, kEditorEditCeilingMs) &&
             click_settled(kGoalNpcsX, kGoalNpcsY) &&
             wait_until([]() {
                 return level_editor_testing_level_type() == kGoalsAll;
             }, kEditorEditCeilingMs);

    // Level > Details > Par value...: the prompted integer lands in par_value.
    if (ok)
        ok = retry_until(
            []() {
                level_editor_testing_prompt_queue_clear();
                level_editor_testing_prompt_queue_push("42");
                return click_settled(kLevelX, kLevelY) &&
                       click_settled(kLevelDetailsX, kLevelDetailsY) &&
                       click_settled(kParValueX, kParValueY);
            },
            []() { return level_editor_testing_level_par_value() == 42; });

    // Mode keys: T always reaches Terrain; O from Terrain reaches Object (from
    // Object it would go on to Select, so this one is never retried).
    if (ok)
        ok = retry_until([]() { return key_settled(SDLK_T); },
                         []() { return level_editor_testing_mode() == 0; });
    if (ok)
        ok = key_settled(SDLK_O) &&
             wait_until([]() { return level_editor_testing_mode() == 1; },
                        kEditorEditCeilingMs);

    og::runtime::current_session->myscreen_->world().end = 1;
    return ok ? 0 : 1;
}

int editor_paint_and_place_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    bool ok = wait_for_trace_line("canvas", "editor_pin_classic",
                                  kEditorEntryCeilingMs);

    // Start from a new level: empty object list, all-grass grid, draw position
    // 0,0 (so the placed object's xpos/ypos are the clicked cell).
    if (ok)
        ok = author_new_level();

    if (ok)
        ok = retry_until([]() { return key_settled(SDLK_T); },
                         []() { return level_editor_testing_mode() == 0; });

    // One warm-up stroke: a "Pick" toggle left armed by an earlier editor
    // session is consumed by the first map click, and a pick would otherwise
    // silently replace the brush chosen below.
    if (ok)
        ok = stroke_dirties_the_level(kMapCellX, kMapCellY);

    // Pick water out of the tile pane, then paint the probe cell with it.
    if (ok)
    {
        eds().rowsdown = 0;
        ok = retry_until(
            []() { return click_settled(kTilePaneWaterX, kTilePaneWaterY); },
            []() { return level_editor_testing_terrain_brush() == PIX_WATER1; });
    }
    if (ok)
        ok = stroke_dirties_the_level(kMapCellX, kMapCellY);

    // Object mode, pane cell (0,0) as the brush, then place one on that cell.
    if (ok)
        ok = key_settled(SDLK_O) &&
             wait_until([]() { return level_editor_testing_mode() == 1; },
                        kEditorEditCeilingMs);
    if (ok)
    {
        eds().rowsdown = 0;
        ok = retry_until(
            []() { return click_settled(kObjectPaneFirstX, kObjectPaneFirstY); },
            []() {
                return level_editor_testing_object_brush_family() == FAMILY_SOLDIER &&
                       level_editor_testing_object_brush_order() ==
                           static_cast<int>(Order::Living);
            });
    }
    if (ok)
        ok = stroke_dirties_the_level(kMapCellX, kMapCellY);

    og::runtime::current_session->myscreen_->world().end = 1;
    return ok ? 0 : 1;
}
} // namespace


// File > Level > New, the three Level > Goals rows and Level > Details > Par
// value are the editor's level-authoring menu. Each click is acknowledged by
// the exact value it writes into the editor's own level, and the level is read
// back after level_editor() returns.
TEST(LevelEditorInteractions, level_menu_authors_new_level_goal_bits_and_par_value)
{
    EditorDecorStateGuard state_guard;   // enters with both dirty flags 0
    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();

    SDL_Thread* thread = SDL_CreateThread(
        editor_menu_authoring_injector, "editor_menu_authoring", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int injector_result = 1;
    SDL_WaitThread(thread, &injector_result);

    LevelRuntimeData* lvl = level_editor_testing_level();
    ASSERT_NE(nullptr, lvl) << "the editor must have published its level";
    const int type_after = static_cast<int>(lvl->world().type);
    const std::string title_after = lvl->world().title;
    const int par_after = static_cast<int>(lvl->world().par_value);
    const int levelchanged_after = eds().levelchanged;
    const int mode_after = level_editor_testing_mode();

    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();

    ASSERT_EQ(0, injector_result)
        << "every scripted click must be consumed and acknowledged by the editor";
    EXPECT_EQ(GameWorld::TYPE_CAN_EXIT_WHENEVER |
                  GameWorld::TYPE_MUST_DESTROY_GENERATORS |
                  GameWorld::TYPE_MUST_PROTECT_NAMED_NPCS,
              type_after)
        << "the three Goals rows each XOR one goal bit into world().type";
    EXPECT_EQ(std::string("New Level"), title_after)
        << "File > Level > New clears the level, which retitles it 'New Level'";
    EXPECT_EQ(42, par_after)
        << "Level > Details > Par value... stores the prompted integer";
    EXPECT_EQ(1, levelchanged_after)
        << "authoring a level through the menus dirties it";
    EXPECT_EQ(1, mode_after)
        << "T then O leaves the editor in Object mode";
}


// Terrain mode paints the clicked cell with the tile-pane brush; Object mode
// places the object-pane brush there. Both are read back off the editor's own
// level after the loop returns — the placed walker's snapped position is also
// how the painted cell is identified.
TEST(LevelEditorInteractions, terrain_brush_paints_and_object_brush_places_on_the_clicked_cell)
{
    EditorDecorStateGuard state_guard;
    picker_testing_yes_or_no_queue_clear();
    level_editor_testing_prompt_queue_clear();

    SDL_Thread* thread = SDL_CreateThread(
        editor_paint_and_place_injector, "editor_paint_and_place", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int injector_result = 1;
    SDL_WaitThread(thread, &injector_result);

    LevelRuntimeData* lvl = level_editor_testing_level();
    ASSERT_NE(nullptr, lvl) << "the editor must have published its level";
    const int levelchanged_after = eds().levelchanged;
    const int current_floor_after = eds().current_floor;

    picker_testing_yes_or_no_queue_clear();

    ASSERT_EQ(0, injector_result)
        << "every scripted click must be consumed and acknowledged by the editor";
    ASSERT_EQ(1u, lvl->world().oblist.size())
        << "one Object-mode click on the map places exactly one object";
    const walker* placed = lvl->world().oblist.front().get();
    ASSERT_NE(nullptr, placed) << "the placed entry must be a live walker";
    EXPECT_EQ(FAMILY_SOLDIER, placed->family())
        << "the object pane's first cell is the Living/soldier brush";
    EXPECT_EQ(static_cast<int>(Order::Living), static_cast<int>(placed->query_order()))
        << "the placed object should carry the brush's order";
    EXPECT_EQ(current_floor_after, static_cast<int>(placed->floor()))
        << "placement stamps the floor the editor is painting";
    EXPECT_EQ(1, levelchanged_after) << "placing an object dirties the level";

    // The paint stroke and the placement clicked the same map cell, so the
    // walker's snapped position names the cell the terrain brush wrote.
    const int cell_x = static_cast<int>(placed->xpos()) / GRID_SIZE;
    const int cell_y = static_cast<int>(placed->ypos()) / GRID_SIZE;
    smoother& sm = lvl->world().smoother_for_floor(0);
    EXPECT_EQ(TYPE_WATER, sm.query_genre_x_y(cell_x, cell_y))
        << "the tile pane's water brush must be written into the clicked cell "
           "(a new level's grid is all grass)";
    EXPECT_EQ(TYPE_GRASS, sm.query_genre_x_y(cell_x + 2, cell_y + 2))
        << "only the clicked cell is painted; the grid two cells over stays grass";
}


namespace
{
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


namespace
{
static int editor_spawn_delay_injector(void* data)
{
    og::runtime::ensure_thread_session();
    EditorThreadState* st = static_cast<EditorThreadState*>(data);
    st->started = true;

    SDL_Delay(300);

    // Clear the stock level objects so the rect-select below grabs exactly
    // the soldier this test places.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);
    inject_click_game(90, 10, 20);   // Level (top menu)
    SDL_Delay(30);
    inject_click_game(90, 145, 20);  // Clear all objects
    SDL_Delay(30);

    // Object mode with a known brush: the object pane's first cell is the
    // Living/soldier brush.
    inject_key_press(SDLK_T, 10);   // -> Terrain
    inject_key_press(SDLK_O, 10);   // Terrain -> Object
    eds().rowsdown = 0;
    SDL_Delay(30);
    inject_click_game(246, 81, 20); // object pane cell (0,0): soldier brush
    SDL_Delay(30);

    // "Delay" sits right of "AI >" on the facing row and is shown in Object
    // mode too, where it presets the brush.
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("240");
    inject_click_game(100, 112, 20);
    SDL_Delay(100);

    // Place the soldier. The first click either places a decoy far from the
    // select rect or disarms a leftover pick toggle; the second click always
    // places a fresh soldier near game coords (160,120) — both inherit the
    // brush's 240-tick delay.
    inject_click_game(120, 60, 20);
    SDL_Delay(30);
    inject_click_game(160, 120, 20);
    SDL_Delay(30);

    // Select mode: rect-select the placed soldier.
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

    // Empty queue: the TESTING prompt accepts the seeded value, which is the
    // selected walker's current delay. Re-applying it proves placement
    // stamped the brush's 240 onto the object.
    level_editor_testing_prompt_queue_clear();
    inject_click_game(100, 112, 20);
    SDL_Delay(150);

    // Author a new delay on the selection.
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("900");
    inject_click_game(100, 112, 20);
    SDL_Delay(150);

    // ... and clear it back to 0.
    level_editor_testing_prompt_queue_clear();
    level_editor_testing_prompt_queue_push("0");
    inject_click_game(100, 112, 20);
    SDL_Delay(150);

    SDL_Delay(200);
    og::runtime::current_session->myscreen_->world().end = 1;

    st->finished = true;
    return 0;
}
} // namespace

// The delayed-spawn field (.fss v10 spawn_delay) already drives the sim and
// the "NEXT WAVE" HUD; this pins the editor control that authors it.
TEST(LevelEditorInteractions, level_editor_delay_button_authors_spawn_delay)
{
    og::runtime::current_session->myscreen_->world().end = 0;
    trace_clear();

    EditorThreadState st{false, false};
    SDL_Thread* thread = SDL_CreateThread(editor_spawn_delay_injector, "editor_spawn_delay", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)level_editor();

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    og::runtime::current_session->myscreen_->world().end = 0;
    level_editor_testing_prompt_queue_clear();

    ASSERT_TRUE(st.started) << "injector thread should have started";
    ASSERT_TRUE(st.finished) << "injector thread should have finished";

    ASSERT_TRUE(trace_contains("editor", "spawn_delay brush=240 order=0"))
        << "Object-mode Delay should preset the brush";
    ASSERT_TRUE(trace_contains("editor", "spawn_delay set=240 order=0 dormant=0"))
        << "the placed soldier should carry the brush's delay, seeded back into the prompt";
    ASSERT_TRUE(trace_contains("editor", "spawn_delay set=900 order=0 dormant=0"))
        << "Select-mode Delay should author the walker's spawn_delay and leave it awake";
    ASSERT_TRUE(trace_contains("editor", "spawn_delay set=0 order=0 dormant=0"))
        << "0 should clear the delay again";
}

// The object brush's team is picked straight off a walker, whose team_num() is
// an unsigned byte. While the brush stored it in a `char`, a team above 127
// arrived negative and the team buttons — which compare against 0 and MAX_TEAM
// — cycled it further out of the authorable range instead of wrapping inside
// it. Legal teams (0..MAX_TEAM) must be completely unaffected.
TEST(LevelEditorInteractions, object_brush_team_stays_in_the_authorable_range)
{
    ASSERT_EQ(0, level_editor_test_object_brush_team_range())
        << "object-brush team range failed at the negated check index";
}

namespace
{
int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
            ++fades;
    }
    return fades;
}

// Ends the editor as soon as the door's two way-in fades have landed — or
// after a bounded wait, so a regression that drops one fails the pin instead
// of hanging the group.
int editor_door_fade_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    constexpr int kTimeoutMs = 8000;
    for (int waited = 0; waited < kTimeoutMs && count_fade_between_traces() < 2;
         waited += 10)
        SDL_Delay(10);
    og::runtime::current_session->myscreen_->world().end = 1;
    return 0;
}
} // namespace

// #237 flow pin, the LEVEL EDITOR door through the REAL body: the door notes
// Fade, and level_editor()'s LegacyMenuFade spends it — the still-open menu
// fades out, the editor's first composed frame fades in, and the editor
// fades itself out at its exit (before its post-loop reset draw and clear
// touch the buffer). Dropping the fade-in (as the editor once did) left the
// door at 1 fade in against 2 back, the asymmetry the invariant forbids.
// Counted here: the editor's first-frame fade-in (the test boundary leaves
// the window black, so the door's fade-out of the "menu" is a traceless
// no-op), then the editor's own exit fade-out. Its partner, the parent menu
// loop's fade-in off the black window, has no parent loop in this test —
// the black window is asserted instead, and
// MenuEngine.nested_menu_door_bracket_* pins the loop half.
TEST(LevelEditorInteractions, level_editor_door_fades_symmetrically)
{
    og::ui::menu_transition_testing_reset();
    og::runtime::current_session->myscreen_->world().end = 0;
    trace_clear();

    SDL_Thread* thread = SDL_CreateThread(editor_door_fade_injector,
                                          "editor_door_fade", nullptr);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    (void)og::ui::run_nested_menu_door(&level_editor);

    SDL_WaitThread(thread, nullptr);
    og::runtime::current_session->myscreen_->world().end = 0;

    const int total = count_fade_between_traces();
    EXPECT_EQ(2, total)
        << "over a black window the door fades the editor's first frame in, "
           "and the editor opens the way back with its own exit fade-out";
    EXPECT_TRUE(og::runtime::current_session->myscreen_->window_is_black())
        << "the editor's exit leaves the window black; the still-open parent "
           "menu loop's next present is the matching fade-in";
    og::ui::menu_transition_testing_reset();
}

// ---------------------------------------------------------------------------
// The editor-exit phantom click (the reporter's literal scenario): exit the
// LEVEL EDITOR through its own File -> Exit menu, hover BEGIN NEW GAME during
// the fade back to the main menu — and BEGIN NEW GAME activates itself.
// Mechanism: the editor pumps its own events and reads the mouse only via
// query_mouse_no_poll(), so every click made inside it mints a coordinate-less
// collapsed-tap pending click (input.cpp g_pending_left_clicks); the main
// menu's leftmouse() then pairs one with the LIVE pointer on its first
// post-door frame. The pointer-handoff rule (docs/menu-engine.md, "Pointer
// handoff") forbids exactly this: no screen may consume a click minted on
// another surface.

#include <openglad/interface/ui/picker_ui_state.h>
#include "test_interact.h"

void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

static inline PickerState& pks_phantom()
{
    return *og::runtime::current_session->picker_;
}

namespace
{
struct PhantomClickState {
    std::atomic<bool> started{false};
    std::atomic<bool> editor_opened{false};
    std::atomic<bool> editor_closed{false};
    std::atomic<bool> phantom_fired{false};
    std::atomic<bool> finished{false};
};

// Editor clicks go through the same UI-canvas-pinned transform interact()
// uses: under picker_main the window runs at the configured scale (2x), so
// raw window coordinates would land on the wrong editor pixels. While the
// editor is open both canvases are 320x200, so the UI transform is exact.
void phantom_click_at_canvas(int game_x, int game_y, int delay_ms)
{
    const auto [wx, wy] = ui_canvas_to_window(static_cast<float>(game_x),
                                              static_cast<float>(game_y));
    if (delay_ms > 0) {
        inject_click(static_cast<int>(wx), static_cast<int>(wy), delay_ms);
    } else {
        // A quick tap: press+release land in one editor pump (a collapsed
        // tap — the exact shape the pending-click queue exists for).
        inject_mouse_down(static_cast<int>(wx), static_cast<int>(wy));
        inject_mouse_up(static_cast<int>(wx), static_cast<int>(wy));
    }
}

// Bounded wait on the FadeBetween trace count — the editor-side observable.
// allbuttons[] is NOT one: the editor never rebuilds it, so main-menu ids
// stay visible to has_interactable() while the editor is open.
bool phantom_wait_for_fades(int at_least, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 10) {
        if (count_fade_between_traces() >= at_least)
            return true;
        SDL_Delay(10);
    }
    return count_fade_between_traces() >= at_least;
}

void cleanup_phantom_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks_phantom().backdrops[static_cast<std::size_t>(i)].reset();
        pks_phantom().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks_phantom().main_columns_pix.reset();
    pks_phantom().main_columns_data.free();
    pks_phantom().main_title_logo_pix.reset();
    pks_phantom().main_title_logo_data.free();
}

int editor_exit_phantom_injector(void* data)
{
    og::runtime::ensure_thread_session();
    PhantomClickState* st = static_cast<PhantomClickState*>(data);
    st->started = true;

    if (!wait_for_interactable("level_edit", 10000)) {
        st->finished = true;
        return 1;
    }
    SDL_Delay(750);
    const int fades_before_door = count_fade_between_traces();
    interact("level_edit");

    // Editor entry = the door's two way-in fades (the still-open menu's
    // fade-out plus the editor's first-frame fade-in). Bounded wait so a
    // regression fails instead of hanging the group.
    if (!phantom_wait_for_fades(fades_before_door + 2, 10000)) {
        // Recovery so the group never hangs: end whatever is open, then
        // leave; the editor_opened guard reds the test.
        og::runtime::current_session->myscreen_->world().end = 1;
        SDL_Delay(500);
        interact("quit");
        st->finished = true;
        return 2;
    }
    st->editor_opened = true;
    SDL_Delay(500);  // let the editor's loop settle on its first frames

    // The reporter's exit, at the editor's own menu geometry
    // (level_editor.cpp menu init: File 0..30 x 0..20 opens Campaign >/
    // Level >/Exit rows at x 0..65, 20px pitch — Exit is y 60..80; the
    // save-confirm it can raise is answered instantly from the TESTING
    // queue the test armed, with no event loop run).
    phantom_click_at_canvas(15, 10, 30);   // File (opens on the release)
    SDL_Delay(300);
    phantom_click_at_canvas(32, 70, 0);    // Exit — a quick collapsed tap
    // The hover: ONLY a motion, queued right behind Exit's release so the
    // pointer is over BEGIN NEW GAME (80,55 140x20 -> centre (150,65))
    // before the main menu's first post-door frame. No click follows.
    {
        const auto [mx, my] = ui_canvas_to_window(150.0f, 65.0f);
        inject_mouse_motion(static_cast<int>(mx), static_cast<int>(my));
    }

    // The way back is two more fades: the editor's own exit fade-out plus
    // the parent menu loop's fade-in off the black window.
    if (!phantom_wait_for_fades(fades_before_door + 4, 10000)) {
        og::runtime::current_session->myscreen_->world().end = 1;
        SDL_Delay(500);
        interact("quit");
        st->finished = true;
        return 3;
    }
    st->editor_closed = true;

    // The phantom: pre-fix, one of the File/Exit pending clicks is spent
    // at the hovered pointer on the menu's first post-door frame,
    // activating BEGIN NEW GAME and opening the company-name-entry screen.
    if (wait_for_interactable("company_name_accept", 2500)) {
        st->phantom_fired = true;
        SDL_Delay(750);
        interact("back");  // escape the name entry so the test cannot hang
        SDL_Delay(300);
        wait_for_interactable("level_edit", 10000);
    }

    SDL_Delay(500);
    interact("quit");  // ends mainmenu(); the TESTING call cap does the rest
    st->finished = true;
    return 0;
}
} // namespace

TEST(LevelEditorInteractions, editor_exit_clicks_cannot_activate_the_main_menu)
{
    og::ui::menu_transition_testing_reset();
    og::runtime::current_session->myscreen_->world().end = 0;

    // A save of our own so the flow is deterministic under --gtest_shuffle
    // (picker_main loads save0 and the editor opens that campaign's level).
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    // The editor's entry path can mark the level changed, so File -> Exit
    // raises the "Quit without saving?" confirm. Queue the YES: the TESTING
    // prompt path answers from the queue and returns at once — it runs no
    // event loop, so the collapsed-tap pending queue the phantom rides on
    // is untouched, exactly as in the reporter's clean-exit scenario.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(true);

    trace_clear();
    PhantomClickState st;
    SDL_Thread* thread = SDL_CreateThread(editor_exit_phantom_injector,
                                          "phantom_injector", &st);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_phantom_picker_state();
    g_picker_max_mainmenu_calls = 0;
    og::runtime::current_session->myscreen_->world().end = 0;
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(st.started.load()) << "injector thread never started";
    ASSERT_TRUE(st.finished.load()) << "injector thread never finished";
    ASSERT_TRUE(st.editor_opened.load())
        << "guard: the LEVEL EDITOR door never opened, the pin has no teeth";
    ASSERT_TRUE(st.editor_closed.load())
        << "guard: the File->Exit clicks never closed the editor";
    EXPECT_FALSE(st.phantom_fired.load())
        << "phantom click: the File->Exit clicks made INSIDE the editor were "
           "spent on the main menu — hovering BEGIN NEW GAME during the fade "
           "activated it with no click";
    og::ui::menu_transition_testing_reset();
}


// ---------------------------------------------------------------------------
// Scripted key/wheel sequences through the real editor event loop (#264).
//
// The editor's key handler is only reachable from level_editor()'s own pump,
// so these tests script real SDL events and read the result from the editor's
// function-local static AFTER the loop returns. The injector never writes
// editor state; its one product write is world().end, the loop's documented
// exit flag (same seam as the menu-authoring test above).
// ---------------------------------------------------------------------------

// From picker_dialogs.cpp (TESTING).
void picker_testing_yes_or_no_queue_clear();
// From level_editor.cpp (TESTING).
int level_editor_testing_object_brush_worldz();
int level_editor_testing_default_maxrows();
LevelRuntimeData* level_editor_testing_level();

namespace
{
// One scripted input. `wheel` != 0 sends a mouse-wheel notch instead of a
// key. `wait_for` (optional) is a substring of a "dialog" trace the driver
// must observe BEFORE sending this step: timed_dialog() eats pending input
// while it is open, so anything queued behind a dialog never reaches the key
// handler.
struct EditorScriptStep
{
    SDL_Keycode key = SDLK_UNKNOWN;
    SDL_Keymod mod = SDL_KMOD_NONE;
    int wheel = 0;
    const char* wait_for = nullptr;
};

EditorScriptStep plain_key(SDL_Keycode k)
{
    return EditorScriptStep{k, SDL_KMOD_NONE, 0, nullptr};
}
EditorScriptStep ctrl_key(SDL_Keycode k)
{
    return EditorScriptStep{k, SDL_KMOD_LCTRL, 0, nullptr};
}
EditorScriptStep wheel_notch(int notches)
{
    return EditorScriptStep{SDLK_UNKNOWN, SDL_KMOD_NONE, notches, nullptr};
}
EditorScriptStep key_after_dialog(SDL_Keycode k, const char* trace_substring)
{
    return EditorScriptStep{k, SDL_KMOD_NONE, 0, trace_substring};
}

bool push_checked_key_press_mod(SDL_Keycode key, SDL_Keymod mod)
{
    SDL_Event down{};
    down.type = SDL_EVENT_KEY_DOWN;
    down.key.key = key;
    down.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
    down.key.mod = mod;
    down.key.down = true;
    if (!SDL_PushEvent(&down))
        return false;

    SDL_Event up = down;
    up.type = SDL_EVENT_KEY_UP;
    up.key.down = false;
    return SDL_PushEvent(&up);
}

bool push_checked_wheel(int notches)
{
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = static_cast<float>(notches);
    wheel.wheel.integer_y = notches;
    return SDL_PushEvent(&wheel);
}

struct EditorScriptState
{
    std::vector<EditorScriptStep> script;
};

int editor_key_script_injector(void* opaque)
{
    og::runtime::ensure_thread_session();
    auto& state = *static_cast<EditorScriptState*>(opaque);
    bool ok = wait_for_trace_line("canvas", "editor_pin_classic", 10000u);

    if (ok)
    {
        for (const EditorScriptStep& step : state.script)
        {
            if (step.wait_for != nullptr &&
                !wait_for_trace_line("dialog", step.wait_for, 10000u))
            {
                ok = false;
                break;
            }
            ok = (step.wheel != 0 ? push_checked_wheel(step.wheel)
                                  : push_checked_key_press_mod(step.key, step.mod)) &&
                 ok;
        }
    }

    // Two drains with an inert fence key between them. The first proves the
    // pump consumed the script; the fence can only be read by the NEXT frame's
    // pump, so the second drain proves the frame that consumed the last
    // scripted event also ran the post-pump work (the tile-selector scroll).
    ok = wait_for_drained_event_queue(10000u) && ok;
    ok = push_checked_key_press_mod(SDLK_F12, SDL_KMOD_NONE) && ok;
    ok = wait_for_drained_event_queue(10000u) && ok;

    og::runtime::current_session->myscreen_->world().end = 1;
    return ok ? 0 : 1;
}

// Runs one real editor session driven by `script`; returns the injector's exit
// code (0 = every event was queued and every awaited dialog appeared).
int run_editor_with_key_script(std::vector<EditorScriptStep> script)
{
    trace_clear();
    og::runtime::current_session->myscreen_->world().end = 0;
    EditorScriptState state;
    state.script = std::move(script);
    SDL_Thread* thread =
        SDL_CreateThread(editor_key_script_injector, "editor_key_script", &state);
    if (thread == nullptr)
        return 1;
    (void)level_editor();
    int result = 1;
    SDL_WaitThread(thread, &result);
    SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP);
    return result;
}
} // namespace


// Ctrl+S on a clean level must say exactly "No changes to save." and touch
// nothing: if that arm were dropped, a stray Ctrl+S would either repack the
// campaign or report a save that never happened. The brush keys in the same
// session are the positive control — they prove the key handler really ran.
TEST(LevelEditorInteractions, ctrl_s_with_nothing_dirty_reports_no_changes_and_saves_nothing)
{
    EditorDecorStateGuard state_guard;   // enters with both dirty flags 0
    picker_testing_yes_or_no_queue_clear();

    std::vector<EditorScriptStep> script;
    script.push_back(ctrl_key(SDLK_S));
    // Close the timed dialog with a key the editor has no handler for, so the
    // three-second product timeout is never paid.
    script.push_back(
        key_after_dialog(SDLK_F12, "timed_dialog_open No changes to save."));
    // O,T,O reaches Object mode from whichever mode an earlier test left.
    script.push_back(
        key_after_dialog(SDLK_O, "timed_dialog_closed No changes to save."));
    script.push_back(plain_key(SDLK_T));
    script.push_back(plain_key(SDLK_O));
    // ',' floors the brush at Z 0 (40 presses is more than any reachable
    // height, so the last ones exercise the refusal), then '.' raises it three
    // half-tiles.
    for (int i = 0; i < 40; ++i)
        script.push_back(plain_key(SDLK_COMMA));
    for (int i = 0; i < 3; ++i)
        script.push_back(plain_key(SDLK_PERIOD));

    const int injector_result = run_editor_with_key_script(std::move(script));

    const int levelchanged_after = eds().levelchanged;
    const int campaignchanged_after = eds().campaignchanged;
    const int worldz_after = level_editor_testing_object_brush_worldz();

    EXPECT_EQ(0, injector_result);
    EXPECT_TRUE(trace_contains("dialog", "timed_dialog_open No changes to save."))
        << "Ctrl+S on a clean level must say 'No changes to save.'";
    EXPECT_FALSE(trace_contains("dialog", "timed_dialog_open Saved."))
        << "nothing was dirty, so nothing may report itself saved";
    EXPECT_FALSE(trace_contains("dialog", "timed_dialog_open Failed to save level."))
        << "a clean level is never handed to saveLevel()";
    EXPECT_FALSE(trace_contains("dialog", "timed_dialog_open Failed to save campaign."))
        << "a clean campaign is never handed to saveCampaign()";
    EXPECT_EQ(0, levelchanged_after) << "a refused save leaves the level clean";
    EXPECT_EQ(0, campaignchanged_after)
        << "a refused save leaves the campaign clean";
    EXPECT_EQ(24, worldz_after)
        << "three '.' presses raise the brush three half-tiles above the floor";
}

// The authored Z height survives leaving and re-entering the editor, and one
// ',' lowers it by exactly one half-tile from there. Without this second arm
// the 24 above could come from a brush that only ever counts up.
TEST(LevelEditorInteractions, comma_lowers_the_object_brush_z_height_by_one_half_tile)
{
    EditorDecorStateGuard state_guard;
    picker_testing_yes_or_no_queue_clear();

    std::vector<EditorScriptStep> raise{plain_key(SDLK_O), plain_key(SDLK_T),
                                        plain_key(SDLK_O)};
    for (int i = 0; i < 40; ++i)
        raise.push_back(plain_key(SDLK_COMMA));
    for (int i = 0; i < 3; ++i)
        raise.push_back(plain_key(SDLK_PERIOD));
    const int raise_result = run_editor_with_key_script(std::move(raise));
    const int worldz_raised = level_editor_testing_object_brush_worldz();

    std::vector<EditorScriptStep> lower{plain_key(SDLK_O), plain_key(SDLK_T),
                                        plain_key(SDLK_O), plain_key(SDLK_COMMA)};
    const int lower_result = run_editor_with_key_script(std::move(lower));
    const int worldz_lowered = level_editor_testing_object_brush_worldz();

    EXPECT_EQ(0, raise_result);
    EXPECT_EQ(0, lower_result);
    EXPECT_EQ(24, worldz_raised)
        << "40 ',' presses floor the brush at 0 and three '.' raise it to 24";
    EXPECT_EQ(16, worldz_lowered)
        << "one ',' drops the brush exactly GRID_SIZE/2 from 24";
}

// Page Up/Down is the whole Z-axis authoring UI: Ctrl+PageUp stacks a floor
// (and lands the editor on it), plain Page Up/Down walk the stack, and
// Ctrl+PageDown drops the top floor. A level with one floor cannot lose it —
// that is what the twelve leading Ctrl+PageDown presses assert.
TEST(LevelEditorInteractions, page_keys_add_switch_and_drop_editor_floors)
{
    EditorDecorStateGuard state_guard;
    picker_testing_yes_or_no_queue_clear();

    std::vector<EditorScriptStep> script;
    // Normalize: whatever floor count the mounted campaign's first level
    // carries, Ctrl+PageDown becomes a no-op once one floor is left.
    for (int i = 0; i < 12; ++i)
        script.push_back(ctrl_key(SDLK_PAGEDOWN));
    script.push_back(ctrl_key(SDLK_PAGEUP));    // 1 floor -> 2, editor on 1
    script.push_back(ctrl_key(SDLK_PAGEUP));    // 2 floors -> 3, editor on 2
    script.push_back(plain_key(SDLK_PAGEDOWN)); // -> floor 1
    script.push_back(plain_key(SDLK_PAGEUP));   // -> floor 2
    script.push_back(ctrl_key(SDLK_PAGEDOWN));  // drop floor 2 -> 2 floors

    const int injector_result = run_editor_with_key_script(std::move(script));

    LevelRuntimeData* level = level_editor_testing_level();
    ASSERT_NE(nullptr, level) << "the editor must have run at least once";
    const int floor_count_after = level->world().floor_count();
    const int current_floor_after = eds().current_floor;
    const int levelchanged_after = eds().levelchanged;

    EXPECT_EQ(0, injector_result);
    EXPECT_EQ(2, floor_count_after)
        << "twelve refused drops, two adds and one drop leave exactly two floors";
    EXPECT_EQ(1, current_floor_after)
        << "dropping the floor the editor stood on moves it down one";
    EXPECT_EQ(1, levelchanged_after)
        << "changing the floor stack dirties the level";
}

// The tile-selector wheel wraps instead of running off either end of the
// palette: one notch up from the first row lands on the last row, one notch
// down from the last row lands back on the first.
TEST(LevelEditorInteractions, one_wheel_notch_wraps_the_tile_selector_both_ways)
{
    const int maxrows = level_editor_testing_default_maxrows();
    ASSERT_EQ(29, maxrows) << "116 default background tiles, 4 to a row";

    int rowsdown_after_up = -1;
    int rowsdown_after_down = -1;
    {
        EditorDecorStateGuard state_guard;
        picker_testing_yes_or_no_queue_clear();
        eds().rowsdown = 0;
        const int result = run_editor_with_key_script({wheel_notch(1)});
        rowsdown_after_up = eds().rowsdown;
        EXPECT_EQ(0, result);
    }
    {
        EditorDecorStateGuard state_guard;
        picker_testing_yes_or_no_queue_clear();
        eds().rowsdown = maxrows - 1;
        const int result = run_editor_with_key_script({wheel_notch(-1)});
        rowsdown_after_down = eds().rowsdown;
        EXPECT_EQ(0, result);
    }

    EXPECT_EQ(maxrows - 1, rowsdown_after_up)
        << "one notch up from the first row wraps to the last row";
    EXPECT_EQ(0, rowsdown_after_down)
        << "one notch down from the last row wraps to the first";
}
