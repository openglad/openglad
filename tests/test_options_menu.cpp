#include <memory>
#include <array>
#include "graph.h"
#include "input/button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "data/save_data.h"

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
    OptionsState* state = static_cast<OptionsState*>(data);
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

        fprintf(stderr, "  [test] toggling sound/render/fullscreen\n");
        interact("toggle_sound");
        SDL_Delay(200);
        interact("toggle_rendering");
        SDL_Delay(200);
        interact("toggle_fullscreen");
        SDL_Delay(200);

        fprintf(stderr, "  [test] adjusting overscan\n");
        interact("overscan_plus");
        SDL_Delay(200);
        interact("overscan_minus");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling additional effects\n");
        interact("toggle_mini_hp_bar");
        SDL_Delay(200);
        interact("toggle_hit_recoil");
        SDL_Delay(200);
        interact("toggle_attack_lunge");
        SDL_Delay(200);
        interact("toggle_hit_sparks");
        SDL_Delay(200);
        interact("toggle_heal_numbers");
        SDL_Delay(200);

        fprintf(stderr, "  [test] restoring defaults\n");
        interact("restore_defaults");
        SDL_Delay(200);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking options_back\n");
        interact("options_back");
    }

    // Ensure mainmenu() returns so picker_main() can complete.
    // In test mode, QUIT does not exit the process; it just returns EXIT_VALUE.
    if (wait_for_interactable("quit", 10000)) {
        SDL_Delay(200);
        fprintf(stderr, "  [test] clicking quit\n");
        interact("quit");
    }

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
