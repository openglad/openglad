#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


// Test: Continue -> Back should return to the main menu, not exit.
//
// Before the fix, picker_main called mainmenu() once with no loop.
// When create_team_menu returned EXIT (from the BACK button), mainmenu
// also returned EXIT, picker_main fell through, and the program exited.
//
// This test drives two full iterations of the main menu loop:
//   Iteration 1: mainmenu -> click CONTINUE -> create_team_menu -> click BACK
//   Iteration 2: mainmenu reappears -> click CONTINUE -> click BACK again
//
// A background thread uses the interaction API (wait_for_interactable,
// interact) to drive navigation while the main thread runs the blocking
// menu event loops -- same pattern as test_level_progress_menu.

struct BackTestState {
    bool started;
    bool finished;
    int times_saw_mainmenu;  // how many times "continue_game" appeared
};

static int back_test_injector(void* data)
{
    og::runtime::ensure_thread_session();
    BackTestState* state = static_cast<BackTestState*>(data);
    state->started = true;

    // === Iteration 1: Click CONTINUE then BACK ===

    // Wait for mainmenu to initialize and complete fadeblack.
    // mainmenu() calls init_buttons() then fadeblack(1) which takes ~500ms
    // and eats SDL events during the fade.
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    state->times_saw_mainmenu++;

    fprintf(stderr, "  [test] Iteration 1: clicking continue_game\n");
    interact("continue_game");

    // Wait for create_team_menu to init (fadeblack + level load)
    SDL_Delay(500);
    wait_for_interactable("go", 10000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] Iteration 1: clicking back\n");
    interact("back");

    // === Iteration 2: mainmenu should reappear (the fix!) ===

    // Before the fix: picker_main returns here, program exits.
    // After the fix: the loop re-enters mainmenu, so continue_game reappears.
    SDL_Delay(500);
    if (wait_for_interactable("continue_game", 10000)) {
        state->times_saw_mainmenu++;
        SDL_Delay(750);

        fprintf(stderr, "  [test] Iteration 2: clicking continue_game\n");
        interact("continue_game");

        SDL_Delay(500);
        wait_for_interactable("go", 10000);
        SDL_Delay(750);

        fprintf(stderr, "  [test] Iteration 2: clicking back\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

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

TEST(BackToMainmenu, continue_then_back_returns_to_mainmenu) {
    trace_clear();

    // Set up save data so CONTINUE GAME has something to load
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    // Start the event injector thread
    BackTestState state = { false, false, 0 };
    SDL_Thread* thread = SDL_CreateThread(back_test_injector, "back_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    // Allow 2 mainmenu iterations then exit the loop
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;

    // This blocks in picker_main's menu loop -- the injector thread drives it
    picker_main(0, nullptr);

    // Wait for injector thread
    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";

    // The real assertion: the injector saw the main menu appear TWICE.
    // Before the fix (no loop): mainmenu only appeared once -> FAIL
    // After the fix (loop): mainmenu appeared twice -> PASS
    ASSERT_TRUE(state.times_saw_mainmenu >= 2) << "main menu should reappear after Continue->Back (not exit the program)";
}

