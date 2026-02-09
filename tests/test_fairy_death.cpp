#include "graph.h"
#include "button.h"
#include "guy.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"
#include "util.h"

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#ifdef TESTING
extern bool g_test_in_game;
#endif

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern pixieN *main_title_logo_pix, *main_columns_pix;
extern pixieN *backdrops[5];
extern PixieData backpics[5];
extern vbutton *localbuttons;

// FAERIE is at index 12 in allowable_guys[]
#define FAERIE_INDEX 12

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

// Test: hire a lone fairy via the UI, start level 4 at max speed, stand there,
// and confirm we lose.
//
// Flow:
//   Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//   Hire Menu -> NEXT x12 to reach FAERIE -> HIRE ME -> BACK ->
//   Team Menu -> (set scen_num=4) -> GO -> game runs -> fairy dies -> BACK -> exits
//
// Uses level 4 because levels 1-3 have team0 NPC allies that keep the
// game alive after the fairy dies (view.cpp hands control to any team0
// entity). Level 4 has no team0 NPCs, so endgame(1) fires immediately
// when the fairy dies.
//
// The fairy has the lowest HP of all characters. With no input, enemies
// swarm and kill it quickly.

struct FairyState {
    bool started;
    bool finished;
    float original_speed;
};

static int fairy_injector(void* data)
{
    FairyState* state = (FairyState*)data;
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

    // popup_dialog("HIRE TROOPS") returns immediately under TESTING
    SDL_Delay(500);
    wait_for_interactable("hire_me", 10000);
    SDL_Delay(1500);

    // -- Hire Menu: cycle to FAERIE and hire --
    // Starts at allowable_guys[0] (SOLDIER). Click NEXT 12 times for FAERIE.
    fprintf(stderr, "  [test] cycling to fairy (NEXT x%d)\n", FAERIE_INDEX);
    for (int i = 0; i < FAERIE_INDEX; i++) {
        interact("next");
        SDL_Delay(200);
    }

    fprintf(stderr, "  [test] hiring fairy\n");
    interact("hire_me");
    SDL_Delay(300);

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // -- Team Menu: GO at max speed --
    SDL_Delay(500);
    wait_for_interactable("go", 10000);
    SDL_Delay(500);

    myscreen->save_data.scen_num = 4;
    set_game_speed(0.0f);

    fprintf(stderr, "  [test] clicking go\n");
    interact("go");

    // -- Wait for glad_main to finish --
    // Old buttons from create_team_menu persist through glad_main, so
    // wait_for_interactable("back") would return immediately (stale buttons).
    // Instead, poll g_test_in_game which is set/cleared around glad_main.
    fprintf(stderr, "  [test] waiting for game to finish...\n");
    while (!g_test_in_game) SDL_Delay(50);   // wait for game to start
    while (g_test_in_game) SDL_Delay(50);     // wait for game to end

    // Restore test settings
    set_game_speed(state->original_speed);

    // Now we're truly back in create_team_menu with fresh buttons
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);

    // Exit team menu -> main menu -> picker exits
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_fairy_death() {
    trace_clear();

    // Start with empty team
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    FairyState state = { false, false, g_game_speed_factor };
    SDL_Thread* thread = SDL_CreateThread(fairy_injector, "fairy_injector", &state);
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

    // We lost — level 4 should NOT be marked completed
    TEST_ASSERT(!myscreen->save_data.is_level_completed(4),
                "level 4 should NOT be completed (fairy should have died)");

    fprintf(stderr, "  [test] Fairy died as expected via UI hire flow\n");
}
REGISTER_TEST(test_fairy_death);
