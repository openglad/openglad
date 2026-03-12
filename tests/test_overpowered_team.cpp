#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/core/util.h>

#include <atomic>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

// Number of hireable character types in allowable_guys[]
#define NUM_HIRE_TYPES 14

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
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
    og::runtime::ensure_thread_session();
    OpState* state = static_cast<OpState*>(data);
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
    state->num_hired = og::runtime::current_session->myscreen_->save_data.team_size;
    fprintf(stderr, "  [test] hired %d characters, cheating stats\n", state->num_hired);
    for (int i = 0; i < og::runtime::current_session->myscreen_->save_data.team_size; i++) {
        guy* g = og::runtime::current_session->myscreen_->save_data.team_list[i].get();
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
        while (g_test_in_game.load(std::memory_order_acquire) && waited_ms < 60000) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire)) {
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

TEST(OverpoweredTeam, overpowered_team) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OpState state = { false, false, og::runtime::current_session->g_game_speed_factor_, 0 };
    SDL_Thread* thread = SDL_CreateThread(op_injector, "op_injector", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // picker_main blocks — the injector thread drives all navigation
    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.num_hired >= 5) << "should have hired at least 5 characters via UI";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be marked completed (team should have won)";

    fprintf(stderr, "  [test] Team of %d won level 1 via UI hire flow\n",
            state.num_hired);
}

