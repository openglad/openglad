#include "graph.h"
#include "button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"
#include "guy.h"
#include "util.h"

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

// Test: Continue -> View Team -> Back -> Back
//
// Verifies:
//   1. View Team menu opens with a team
//   2. The view menu has GO and BACK buttons
//   3. Can navigate back cleanly

struct ViewState {
    bool started;
    bool finished;
    bool saw_view_menu;
};

static int view_team_injector(void* data)
{
    ViewState* state = (ViewState*)data;
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("view_team", 10000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking view_team\n");
    interact("view_team");

    // View team menu has "go" and "back" buttons
    SDL_Delay(500);
    if (wait_for_interactable("go", 10000)) {
        state->saw_view_menu = true;
        wait_for_interactable("back", 10000);
        SDL_Delay(500);
        fprintf(stderr, "  [test] clicking back from view menu\n");
        interact("back");

        // Back at the create-team menu, click BACK again to return to main menu.
        wait_for_interactable("back", 10000);
        SDL_Delay(500);
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

void test_view_team() {
    trace_clear();

    // Set up a team so view has something to show
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;

    guy* soldier = new guy(FAMILY_SOLDIER);
    guy* archer = new guy(FAMILY_ARCHER);
    // Give the team strong stats so the launched game finishes quickly.
    soldier->strength = soldier->dexterity = soldier->constitution = soldier->intelligence = soldier->armor = 200;
    archer->strength = archer->dexterity = archer->constitution = archer->intelligence = archer->armor = 200;
    myscreen->save_data.team_list[0] = soldier;
    myscreen->save_data.team_list[1] = archer;
    myscreen->save_data.team_size = 2;

    myscreen->save_data.save("save0");

    ViewState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(view_team_injector, "view_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_view_menu, "should have entered the view team menu");
}
REGISTER_TEST(test_view_team);
