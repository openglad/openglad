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

// Test: Toggle difficulty from main menu
//
// Flow: Main Menu -> click "difficulty" a few times -> Continue -> Back
//
// The difficulty button cycles through: "Skirmish", "Battle", "Slaughter"
//
// Verifies:
//   1. Difficulty button is clickable
//   2. Doesn't crash when toggled
//   3. Main menu still works after toggling

struct DifficultyState {
    bool started;
    bool finished;
    bool clicked_difficulty;
};

static int difficulty_injector(void* data)
{
    DifficultyState* state = (DifficultyState*)data;
    state->started = true;

    wait_for_interactable("difficulty", 5000);
    SDL_Delay(1500);

    // Toggle difficulty a few times
    fprintf(stderr, "  [test] clicking difficulty (toggle 1)\n");
    interact("difficulty");
    SDL_Delay(300);

    fprintf(stderr, "  [test] clicking difficulty (toggle 2)\n");
    interact("difficulty");
    SDL_Delay(300);

    fprintf(stderr, "  [test] clicking difficulty (toggle 3)\n");
    interact("difficulty");
    state->clicked_difficulty = true;
    SDL_Delay(300);

    // Also toggle player count
    if (has_interactable("1_player")) {
        fprintf(stderr, "  [test] clicking 1_player\n");
        interact("1_player");
        SDL_Delay(200);
    }

    // Exit by going continue -> back
    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");
    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);
    interact("back");

    state->finished = true;
    return 0;
}

void test_difficulty_toggle() {
    trace_clear();

    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    DifficultyState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(difficulty_injector, "difficulty_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.clicked_difficulty, "should have toggled difficulty");
}
REGISTER_TEST(test_difficulty_toggle);
