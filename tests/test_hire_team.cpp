#include <memory>
#include <array>
#include "graph.h"
#include "input/button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "data/save_data.h"
#include "entities/guy.h"

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern std::unique_ptr<pixieN> main_title_logo_pix, main_columns_pix;
extern std::array<std::unique_ptr<pixieN>, 5> backdrops;
extern PixieData backpics[5];
extern vbutton *localbuttons;

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        backdrops[i].reset();
        backpics[i].free();
    }
    clear_allbuttons();
    localbuttons = nullptr;
    main_columns_pix.reset();
    main_columns_data.free();
    main_title_logo_pix.reset();
    main_title_logo_data.free();
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

    // Dismiss the hire troops popup
    SDL_Delay(500);
    if (wait_for_interactable("ok", 10000)) {
        SDL_Delay(500);
        fprintf(stderr, "  [test] dismissing popup\n");
        interact("ok");
    }

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

void test_hire_menu_browsing() {
    trace_clear();

    // Start with empty team
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    HireState state = { false, false, false, 0 };
    SDL_Thread* thread = SDL_CreateThread(hire_injector, "hire_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_hire_menu, "should have seen the hire menu");
    TEST_ASSERT(state.cycles_completed >= 3,
        "should have cycled through characters 3 times");
}
REGISTER_TEST(test_hire_menu_browsing);
