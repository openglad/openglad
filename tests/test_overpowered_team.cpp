#include "graph.h"
#include "button.h"
#include "guy.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"
#include "util.h"

#include <atomic>

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

#ifdef TESTING
extern bool g_test_remove_exits;
extern bool g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

// Number of hireable character types in allowable_guys[]
#define NUM_HIRE_TYPES 14

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

// Hire one of each character type via the actual UI, crank stats to absurd
// levels (programmatically), run level 1 at max speed, and confirm that we win.
//
// Flow:
//   Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//   Hire Menu -> cycle through all types, clicking HIRE ME for each ->
//   Back -> (cheat stats on hired team) -> GO -> game runs -> win ->
//   Back -> exits to main menu
//
// prompt_for_string() returns immediately under TESTING, accepting the
// default generated name for each hired character.

struct OpState {
    bool started;
    bool finished;
    float original_speed;
    int num_hired;
};

static int op_injector(void* data)
{
    OpState* state = (OpState*)data;
    state->started = true;

    // -- Main Menu --
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Dismiss campaign intro screen (blocks until Escape)
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro\n");
    inject_key_press(SDLK_ESCAPE);

    // popup_dialog("HIRE TROOPS") returns immediately under TESTING,
    // so the hire menu opens right away.
    SDL_Delay(500);
    wait_for_interactable("hire_me", 10000);
    SDL_Delay(1500);

    // -- Hire Menu: hire one of each type --
    // The hire menu starts showing allowable_guys[0] (SOLDIER).
    // For each type: click HIRE ME (succeeds if we can afford it),
    // then click NEXT to cycle to the next type.
    fprintf(stderr, "  [test] hiring characters through UI...\n");
    for (int i = 0; i < NUM_HIRE_TYPES; i++) {
        interact("hire_me");
        SDL_Delay(200);

        if (i < NUM_HIRE_TYPES - 1) {
            interact("next");
            SDL_Delay(200);
        }
    }

    fprintf(stderr, "  [test] done hiring, clicking back\n");
    interact("back");

    // -- Team Menu: cheat stats then GO --
    SDL_Delay(500);
    wait_for_interactable("go", 10000);
    SDL_Delay(500);

    // Programmatically crank every stat to ludicrous levels
    state->num_hired = myscreen->save_data.team_size;
    fprintf(stderr, "  [test] hired %d characters, cheating stats\n", state->num_hired);
    for (int i = 0; i < myscreen->save_data.team_size; i++) {
        guy* g = myscreen->save_data.team_list[i];
        if (g) {
            g->strength = 200;
            g->dexterity = 200;
            g->constitution = 200;
            g->intelligence = 200;
            g->armor = 200;
        }
    }

    // Set up for auto-win: remove exits so level completes when enemies die
    g_test_remove_exits = true;
    set_game_speed(0.0f);

    fprintf(stderr, "  [test] clicking go\n");
    int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    interact("go");

    // -- Wait for glad_main to finish --
    // Old buttons from create_team_menu persist through glad_main, so
    // wait_for_interactable("back") would return immediately (stale buttons).
    // Instead, wait for a monotonic epoch increment and then poll g_test_in_game
    // which is set/cleared around glad_main.
    fprintf(stderr, "  [test] waiting for game to finish...\n");
    {
        int waited_ms = 0;
        const int poll_ms = 50;
        while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before && waited_ms < 10000) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
            fprintf(stderr, "  [test] ERROR: game never started (epoch unchanged)\n");
            set_game_speed(state->original_speed);
            g_test_remove_exits = false;
            return 0;
        }
        waited_ms = 0;
        while (g_test_in_game && waited_ms < 60000) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game) {
            fprintf(stderr, "  [test] ERROR: game did not finish within timeout\n");
            set_game_speed(state->original_speed);
            g_test_remove_exits = false;
            return 0;
        }
    }

    // Now we're truly back in create_team_menu with fresh buttons
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);

    // Restore state
    set_game_speed(state->original_speed);
    g_test_remove_exits = false;

    // Exit team menu -> main menu -> picker exits
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_overpowered_team() {
    trace_clear();

    // Start with empty team
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    OpState state = { false, false, g_game_speed_factor, 0 };
    SDL_Thread* thread = SDL_CreateThread(op_injector, "op_injector", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // picker_main blocks — the injector thread drives all navigation
    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.num_hired >= 5,
                "should have hired at least 5 characters via UI");
    TEST_ASSERT(myscreen->save_data.is_level_completed(1),
                "level 1 should be marked completed (team should have won)");

    fprintf(stderr, "  [test] Team of %d won level 1 via UI hire flow\n",
            state.num_hired);
}
REGISTER_TEST(test_overpowered_team);
