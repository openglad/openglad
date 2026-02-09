#include "graph.h"
#include "button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern pixieN *main_title_logo_pix, *main_columns_pix;
extern pixieN *backdrops[5];
extern PixieData backpics[5];
extern vbutton *localbuttons;

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        if (backdrops[i]) { delete backdrops[i]; backdrops[i] = nullptr; }
        backpics[i].free();
    }
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (allbuttons[i]) { delete allbuttons[i]; allbuttons[i] = nullptr; }
    }
    localbuttons = nullptr;
    if (main_columns_pix) { delete main_columns_pix; main_columns_pix = nullptr; }
    main_columns_data.free();
    if (main_title_logo_pix) { delete main_title_logo_pix; main_title_logo_pix = nullptr; }
    main_title_logo_data.free();
}

// Test: Open options menu, toggle some settings, then exit.
//
// Flow: Main Menu -> Options -> toggle some settings -> Back -> (main menu exits)
//
// Verifies:
//   1. Options menu opens
//   2. Can toggle visual effects
//   3. Returns cleanly to main menu

struct OptionsState {
    bool started;
    bool finished;
    bool saw_options;
};

static int options_injector(void* data)
{
    OptionsState* state = (OptionsState*)data;
    state->started = true;

    // Wait for main menu
    wait_for_interactable("options", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking options\n");
    interact("options");

    // Options menu buttons
    SDL_Delay(500);
    if (wait_for_interactable("toggle_hit_flash", 10000)) {
        state->saw_options = true;
        SDL_Delay(500);

        // Toggle a few settings
        fprintf(stderr, "  [test] toggling hit flash\n");
        interact("toggle_hit_flash");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling damage numbers\n");
        interact("toggle_damage_numbers");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling gore\n");
        interact("toggle_gore");
        SDL_Delay(200);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking options_back\n");
        interact("options_back");
    }

    // Main menu reappears -- let the mainmenu_loop iteration limit exit
    SDL_Delay(500);
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);
    // Need to do something so mainmenu returns. Click continue then back.
    interact("continue_game");
    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);
    interact("back");

    state->finished = true;
    return 0;
}

void test_options_menu() {
    trace_clear();

    // Need save data for continue_game
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    OptionsState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(options_injector, "options_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // options REDRAW is handled within same mainmenu() call

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_options, "should have entered the options menu");
}
REGISTER_TEST(test_options_menu);
