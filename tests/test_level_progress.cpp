#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
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


// Regression test for segfault: Main Menu -> Continue -> Level Progress
//
// Reproduces the exact crash path by clicking actual UI buttons:
//   1. picker_main() enters the main menu
//   2. We click "CONTINUE GAME" button -> enters create_team_menu
//   3. We click "PROGRESS" button -> enters create_progress_menu
//      (this is where the segfault used to occur in getLevelStats)
//   4. We click "BACK" -> unwinds all menus back to picker_main
//
// A background thread uses the interaction API to drive navigation
// while the main thread runs the blocking menu event loops.

struct EventSequence {
    bool started;
    bool finished;
};

static int event_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    EventSequence* seq = static_cast<EventSequence*>(data);
    seq->started = true;

    // Wait for mainmenu to initialize and complete fadeblack.
    // mainmenu() calls init_buttons() then fadeblack(1) which takes ~500ms
    // and eats SDL events during the fade. We wait for the button to exist
    // then add extra delay so the fade finishes and the event loop is ready.
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);

    // Step 1: Click "CONTINUE GAME"
    fprintf(stderr, "  [test] Step 1: clicking continue_game\n");
    interact("continue_game");

    // Wait for create_team_menu to init after fade + level load.
    // create_team_menu calls fadeblack(0), level_data.load(), fadeblack(1).
    // PROGRESS lives inside the SCENARIO subscreen now.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);

    // Step 2: Open the SCENARIO subscreen, then click "PROGRESS"
    fprintf(stderr, "  [test] Step 2: clicking scenario, then progress\n");
    interact("scenario");
    wait_for_interactable("progress", 10000);
    SDL_Delay(500);
    interact("progress");

    // Wait for create_progress_menu to load level data and enter its loop.
    // "prev" is unique to the progress menu here, so waiting on it (instead
    // of the ambiguous per-screen "back") confirms the right screen is up.
    SDL_Delay(500);
    wait_for_interactable("prev", 10000);
    SDL_Delay(500);

    // Step 3: Click "BACK" in the progress menu
    fprintf(stderr, "  [test] Step 3: clicking progress back\n");
    interact("back");

    // create_progress_menu returns REDRAW, so we're back in the SCENARIO
    // subscreen. Wait for its unique "view_scenario" button, then BACK out.
    SDL_Delay(2000);
    wait_for_interactable("view_scenario", 10000);
    SDL_Delay(500);

    // Step 4: Click "BACK" in the scenario menu
    fprintf(stderr, "  [test] Step 4: clicking scenario back\n");
    interact("back");

    // Step 5: Click "BACK" in the team menu
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(500);
    fprintf(stderr, "  [test] Step 5: clicking team back\n");
    interact("back");

    // Back returns to the main menu. Trip the test-mode max-loop guard so
    // picker_main() does not sit in a fresh mainmenu() waiting for input.
    g_picker_mainmenu_calls = g_picker_max_mainmenu_calls;
    SDL_Delay(500);

    seq->finished = true;
    return 0;
}

static void cleanup_picker_state()
{
    // Clean up picker globals without deleting myscreen
    // (picker_quit() deletes myscreen which other tests still need)
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    // localbuttons == allbuttons[0] (returned by init_buttons), so just
    // delete everything via allbuttons to avoid double-free.
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

TEST(LevelProgress, menu) {
    trace_clear();

    // Set up save data so "CONTINUE GAME" has something to load
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    // Start the event injector thread
    EventSequence seq = { false, false };
    SDL_Thread* thread = SDL_CreateThread(event_injector_thread, "test_injector", &seq);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    // Limit picker_mainmenu_loop to 1 iteration (this test only needs one pass)
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // This blocks until menus unwind -- the injector thread drives navigation
    picker_main(0, nullptr);

    // Wait for thread to finish
    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;  // reset for other tests

    // If we got here without crashing, the regression test passed
    ASSERT_TRUE(seq.finished) << "event injector thread should have completed";

    // Verify the progress menu was actually entered by checking traces
    ASSERT_TRUE(trace_contains("menu", "init_buttons")) << "menu buttons should have been initialized";
}
