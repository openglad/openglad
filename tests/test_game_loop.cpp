#include <array>
#include <thread>
#include <vector>

#include <openglad/runtime/game_loop.h>
#include <openglad/data/save_data.h>
#include <openglad/input/input.h>
#include <openglad/entities/walker.h>
#include <openglad/render/view.h>
#include <openglad/runtime/screen.h>
#include "SDL.h"
#include "test_framework.h"
#include <openglad/core/util.h>

extern screen* myscreen;
extern const Uint8* keystates;
extern int player_keys[4][NUM_KEYS];

short load_saved_game(const char* filename, screen* myscreen);

extern bool debug_draw_paths;
extern bool debug_draw_obmap;

struct EventScript {
    std::vector<SDL_Event> events;
    size_t idx = 0;
};

static int scripted_poll(void* userdata, SDL_Event* out)
{
    EventScript* s = static_cast<EventScript*>(userdata);
    if (s->idx >= s->events.size())
        return 0;
    *out = s->events[s->idx++];
    return 1;
}

// Adapter to match GameLoopDeps signature.
static EventScript* g_script = nullptr;
static int scripted_poll_adapter(SDL_Event* out)
{
    return scripted_poll(g_script, out);
}

void test_game_frame_toggles_debug_hotkeys()
{
    // Load a minimal scenario so screen::act() is safe to call.
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.save("test_game_loop_save");
    short load_result = load_saved_game("test_game_loop_save", myscreen);
    TEST_ASSERT(load_result != 0, "load_saved_game should succeed for scenario 1");

    // Ensure no frame delays.
    float old_speed = g_game_speed_factor;
    set_game_speed(0.0f);

    debug_draw_paths = false;
    debug_draw_obmap = false;

    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = SDLK_F11;
    script.events.push_back(e);
    e.key.keysym.sym = SDLK_F12;
    script.events.push_back(e);

    g_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    (void)game_frame(*myscreen, st, deps);

    TEST_ASSERT(debug_draw_paths, "F11 should toggle debug_draw_paths");
    TEST_ASSERT(debug_draw_obmap, "F12 should toggle debug_draw_obmap");

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_game_frame_toggles_debug_hotkeys);

void test_game_frame_with_result_done_when_end_is_set()
{
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;

    const char old_end = myscreen->end;
    myscreen->end = 1;

    const GameFrameResult result = game_frame_with_result(*myscreen, st, deps);
    TEST_ASSERT_EQ(static_cast<int>(GameFrameResult::Done), static_cast<int>(result),
        "game_frame_with_result should report Done when screen end is set");
    TEST_ASSERT(st.done, "state.done should be set when end is set");

    myscreen->end = old_end;
}
REGISTER_TEST(test_game_frame_with_result_done_when_end_is_set);

void test_game_frame_bool_wrapper_matches_typed_result()
{
    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;

    const char old_end = myscreen->end;
    myscreen->end = 1;

    const GameFrameResult typed = game_frame_with_result(*myscreen, st, deps);
    st.done = false;
    const bool wrapped = game_frame(*myscreen, st, deps);

    TEST_ASSERT_EQ(static_cast<int>(typed != GameFrameResult::Continue), static_cast<int>(wrapped),
        "bool wrapper should map Continue/non-Continue exactly");

    myscreen->end = old_end;
}
REGISTER_TEST(test_game_frame_bool_wrapper_matches_typed_result);

// ---------------------------------------------------------------------------
// Regression test: options_menu via game_frame_with_result call chain.
//
// This exercises the exact path that hangs on Emscripten:
//   game_frame_with_result -> s.input(KEY_PREFS event) -> options_menu()
//
// On Emscripten, options_menu() has a blocking while-loop that must call
// emscripten_sleep() (via YIELD_SLEEP) to yield to the browser.  If the
// ASYNCIFY compile flag is missing, YIELD_SLEEP is a no-op and the browser
// hangs.  Natively, YIELD_SLEEP is always a no-op so options_menu() is a
// tight busy-loop driven by keystates.  A background thread presses ESC to
// let it exit.
// ---------------------------------------------------------------------------

void test_game_frame_options_menu_via_key_prefs_completes()
{
    // Load a minimal scenario so screen::act() is safe.
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.save("test_game_loop_optmenu_save");
    short load_result = load_saved_game("test_game_loop_optmenu_save", myscreen);
    TEST_ASSERT(load_result != 0, "load_saved_game should succeed");

    // Ensure a player-controlled walker exists so options_menu() doesn't
    // early-return via its missing-control guard.
    viewscreen* vs = myscreen->viewob[0].get();
    TEST_ASSERT(vs != nullptr, "viewob[0] should exist");
    if (!vs)
        return;

    walker* saved_control = vs->control;
    if (!vs->control)
    {
        walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
        TEST_ASSERT(w != nullptr, "control walker created");
        if (!w)
            return;
        w->team_num = 0;
        w->user = 0;
        w->set_act_type(ACT_CONTROL);
        vs->control = w;
    }

    // Override keystates so we can inject ESC from a background thread.
    const Uint8* saved_keystates = keystates;
    std::array<Uint8, SDL_NUM_SCANCODES> fake_keystates{};
    fake_keystates.fill(0);
    keystates = fake_keystates.data();

    // Background thread: press ESC after a short delay to exit options_menu().
    std::thread esc_thread([&fake_keystates]() {
        SDL_Delay(50);
        fake_keystates[SDL_SCANCODE_ESCAPE] = 1;
        SDL_Delay(30);
        fake_keystates[SDL_SCANCODE_ESCAPE] = 0;
    });

    // Build a scripted KEY_PREFS event (SDLK_1 for player 0).
    EventScript script;
    SDL_Event e{};
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = player_keys[0][KEY_PREFS];
    e.key.keysym.scancode = SDL_GetScancodeFromKey(e.key.keysym.sym);
    e.key.repeat = 0;
    script.events.push_back(e);

    g_script = &script;

    float old_speed = g_game_speed_factor;
    set_game_speed(0.0f);

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.poll_event = scripted_poll_adapter;

    // This call chain goes through the std::function indirection in
    // game_frame_with_result() and must not hang.
    (void)game_frame(*myscreen, st, deps);

    esc_thread.join();

    // Cleanup.
    g_script = nullptr;
    set_game_speed(old_speed);
    keystates = saved_keystates;
    vs->control = saved_control;
    myscreen->level_data.delete_objects();
}
REGISTER_TEST(test_game_frame_options_menu_via_key_prefs_completes);
