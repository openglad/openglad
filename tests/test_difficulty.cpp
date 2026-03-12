#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


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
    og::runtime::ensure_thread_session();
    DifficultyState* state = static_cast<DifficultyState*>(data);
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

    // Exercise all multiplayer player-count redraw branches.
    if (has_interactable("4_player")) {
        fprintf(stderr, "  [test] clicking 4_player\n");
        interact("4_player");
        SDL_Delay(150);
    }
    if (has_interactable("3_player")) {
        fprintf(stderr, "  [test] clicking 3_player\n");
        interact("3_player");
        SDL_Delay(150);
    }
    if (has_interactable("2_player")) {
        fprintf(stderr, "  [test] clicking 2_player\n");
        interact("2_player");
        SDL_Delay(150);
    }
    if (has_interactable("1_player")) {
        fprintf(stderr, "  [test] clicking 1_player\n");
        interact("1_player");
        SDL_Delay(150);
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

TEST(Difficulty, toggle) {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    DifficultyState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(difficulty_injector, "difficulty_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.clicked_difficulty) << "should have toggled difficulty";
}

