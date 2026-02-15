#include <memory>
#include <array>
#include <openglad/data/pixie_data.h>
#include <openglad/input/button.h>
#include <openglad/legacy/test_trace.h>
#include <openglad/render/pixien.h>
#include <openglad/runtime/screen.h>
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/data/save_data.h>
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

// Test: Verify key buttons exist on the main menu.
//
// The native build has "quit" where the web build has "help".
// Both builds have "options" and "difficulty".
//
// Flow: Main Menu -> verify buttons exist -> Continue -> Back

struct MainMenuButtonState {
    bool started;
    bool finished;
    bool has_options;
    bool has_difficulty;
    bool has_quit_or_help;
};

static int mainmenu_button_injector(void* data)
{
    MainMenuButtonState* state = static_cast<MainMenuButtonState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    // Check which buttons exist on the main menu
    state->has_options = has_interactable("options");
    state->has_difficulty = has_interactable("difficulty");
    // Native build has "quit", web build has "help"
    state->has_quit_or_help = has_interactable("quit") || has_interactable("help");

    // Navigate normally to exit
    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking back\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_mainmenu_buttons_exist() {
    trace_clear();

    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    MainMenuButtonState state = { false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(mainmenu_button_injector, "btn_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.has_options, "options button should exist on main menu");
    TEST_ASSERT(state.has_difficulty, "difficulty button should exist on main menu");
    TEST_ASSERT(state.has_quit_or_help, "quit or help button should exist on main menu");
}
REGISTER_TEST(test_mainmenu_buttons_exist);
