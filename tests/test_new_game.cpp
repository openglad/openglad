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

// Test: Click "BEGIN NEW GAME" from the main menu, which should reset save data
// and enter the hire troops screen. Then click BACK to return to the team menu,
// and BACK again to return to the main menu.
//
// This verifies:
//   1. The begin_new_game button works
//   2. Save data gets reset on new game
//   3. The hire menu appears (with popup dialog)
//   4. Navigation back to main menu works

struct NewGameState {
    bool started;
    bool finished;
    bool saw_hire_menu;
    bool saw_team_menu;
};

static int new_game_injector(void* data)
{
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // beginmenu() calls read_campaign_intro() which blocks until
    // input_continue is set (SDLK_ESCAPE keydown triggers this).
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro with Escape\n");
    inject_key_press(SDLK_ESCAPE);

    // After campaign intro, beginmenu resets save data and calls
    // create_hire_menu(1). create_hire_menu(1) shows a popup_dialog
    // first ("HIRE TROOPS", ...). The popup has an "ok" button.
    SDL_Delay(500);
    if (wait_for_interactable("ok", 10000)) {
        SDL_Delay(500);
        fprintf(stderr, "  [test] dismissing hire troops popup\n");
        interact("ok");
    }

    // Now we should be in the hire menu with hire_me, prev, next, back buttons
    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);

        // Click BACK to return to create_team_menu
        fprintf(stderr, "  [test] clicking back from hire menu\n");
        interact("back");
    }

    // create_hire_menu returns REDRAW, which puts us back in create_team_menu
    SDL_Delay(500);
    if (wait_for_interactable("view_team", 10000)) {
        state->saw_team_menu = true;
        SDL_Delay(1500);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

void test_begin_new_game() {
    trace_clear();

    // Pre-populate save data so we can verify it gets reset
    myscreen->save_data.totalcash = 99999;
    myscreen->save_data.totalscore = 55555;
    myscreen->save_data.scen_num = 5;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    // Make sure team_size is 0 so beginmenu doesn't prompt "restart?"
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (myscreen->save_data.team_list[i]) {
            myscreen->save_data.team_list[i].reset();
            myscreen->save_data.team_list[i].reset(nullptr);
        }
    }
    myscreen->save_data.team_size = 0;
    myscreen->save_data.save("save0");

    NewGameState state = { false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(new_game_injector, "new_game_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_hire_menu, "should have seen the hire menu after new game");
    TEST_ASSERT(state.saw_team_menu, "should have returned to team menu from hire");

    // beginmenu calls save_data.reset(), so cash should be the default (starting cash)
    // rather than our 99999
    TEST_ASSERT(myscreen->save_data.totalcash != 99999,
        "totalcash should have been reset by new game");
}
REGISTER_TEST(test_begin_new_game);
