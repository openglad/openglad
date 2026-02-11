#include "graph.h"
#include "button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"
#include "guy.h"

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

// Test: Continue -> Train Team -> interact with stat buttons -> Back -> Back
//
// Verifies:
//   1. Train menu opens when team has members
//   2. Can click stat increase buttons
//   3. Can cycle through team members
//   4. Navigation back works

struct TrainState {
    bool started;
    bool finished;
    bool saw_train_menu;
};

static int train_injector(void* data)
{
    TrainState* state = (TrainState*)data;
    state->started = true;

    // Wait for main menu
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    // Wait for team menu
    SDL_Delay(500);
    wait_for_interactable("train_team", 10000);
    SDL_Delay(1500);

    // Click TRAIN TEAM
    fprintf(stderr, "  [test] clicking train_team\n");
    interact("train_team");

    // Wait for train menu buttons
    SDL_Delay(500);
    if (wait_for_interactable("inc_str", 10000)) {
        state->saw_train_menu = true;
        SDL_Delay(500);

        // Try increasing strength
        fprintf(stderr, "  [test] clicking inc_str\n");
        interact("inc_str");
        SDL_Delay(300);

        // Try increasing dexterity
        fprintf(stderr, "  [test] clicking inc_dex\n");
        interact("inc_dex");
        SDL_Delay(300);

        // Open details across several classes to exercise detail rendering branches.
        for (int i = 0; i < 5; i++) {
            wait_for_interactable("details", 10000);
            fprintf(stderr, "  [test] clicking details (%d)\n", i + 1);
            interact("details");
            SDL_Delay(300);
            wait_for_interactable("back", 10000);
            fprintf(stderr, "  [test] clicking back from details (%d)\n", i + 1);
            interact("back");
            SDL_Delay(300);

            if (i < 4) {
                fprintf(stderr, "  [test] clicking next (%d)\n", i + 1);
                interact("next");
                SDL_Delay(300);
            }
        }

        // Go back
        fprintf(stderr, "  [test] clicking back from train menu\n");
        interact("back");
    }

    // Back in team menu
    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_train_team() {
    trace_clear();

    // Set up a team with members so train menu doesn't show "NEED A TEAM!" popup
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.totalcash = 50000;  // Enough cash for training

    // Add multiple classes at high level to hit create_detail_menu text branches.
    guy* archmage = new guy(FAMILY_ARCHMAGE);
    guy* cleric = new guy(FAMILY_CLERIC);
    guy* druid = new guy(FAMILY_DRUID);
    guy* thief = new guy(FAMILY_THIEF);
    guy* orc = new guy(FAMILY_ORC);
    archmage->level = 10;
    cleric->level = 10;
    druid->level = 10;
    thief->level = 10;
    orc->level = 10;
    myscreen->save_data.team_list[0] = archmage;
    myscreen->save_data.team_list[1] = cleric;
    myscreen->save_data.team_list[2] = druid;
    myscreen->save_data.team_list[3] = thief;
    myscreen->save_data.team_list[4] = orc;
    myscreen->save_data.team_size = 5;

    myscreen->save_data.save("save0");

    TrainState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(train_injector, "train_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_train_menu, "should have entered the train menu");
}
REGISTER_TEST(test_train_team);
