#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// Test: Navigate to hire troops, browse characters with NEXT/PREV, then exit.
//
// Note: We can't click HIRE ME because add_guy() calls prompt_for_string()
// to name the character, which blocks on text input. Instead we test that
// the hire menu loads, character cycling works, and we can exit cleanly.
//
// Flow: Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//       (dismiss popup OK) -> NEXT -> NEXT -> PREV -> Back -> Back

struct HireState {
    bool started;
    bool finished;
    bool saw_hire_menu;
    int cycles_completed;
};

static int hire_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HireState* state = static_cast<HireState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Dismiss campaign intro screen
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro with Escape\n");
    inject_key_press(SDLK_ESCAPE);

    // In TESTING builds, popup_dialog() is a no-op, so no "ok" button exists.

    // Now in hire menu - cycle through characters
    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);

        // Cycle through characters with NEXT
        fprintf(stderr, "  [test] clicking next\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        fprintf(stderr, "  [test] clicking next again\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        // And back with PREV
        fprintf(stderr, "  [test] clicking prev\n");
        interact("prev");
        state->cycles_completed++;
        SDL_Delay(300);
    }

    // Go back to team menu
    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // Back to main menu
    SDL_Delay(500);
    wait_for_interactable("view_team", 10000);
    SDL_Delay(1500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_menu_browsing) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireState state = { false, false, false, 0 };
    SDL_Thread* thread = SDL_CreateThread(hire_injector, "hire_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "should have seen the hire menu";
    ASSERT_TRUE(state.cycles_completed >= 3) << "should have cycled through characters 3 times";
}

