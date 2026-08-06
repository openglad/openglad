#include <memory>
#include <array>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
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
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// Test: Verify key buttons exist on the main menu.
//
// HELP and QUIT occupy separate, stable footer slots. The native test sees
// both as interactable; the spec-level web test pins QUIT as disabled.
//
// Flow: Main Menu -> verify buttons exist -> Continue -> Back

struct MainMenuButtonState {
    bool started;
    bool finished;
    bool has_options;
    bool has_difficulty;
    bool has_help;
    bool has_quit;
};

static int mainmenu_button_injector(void* data)
{
    og::runtime::ensure_thread_session();
    MainMenuButtonState* state = static_cast<MainMenuButtonState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);

    // Check which buttons exist on the main menu
    state->has_options = has_interactable("options");
    state->has_difficulty = has_interactable("difficulty");
    state->has_help = has_interactable("help");
    state->has_quit = has_interactable("quit");

    // Navigate normally to exit
    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking back\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(Help, mainmenu_buttons_exist) {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    MainMenuButtonState state = { false, false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(mainmenu_button_injector, "btn_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.has_options) << "Game Settings should exist on main menu";
    ASSERT_TRUE(state.has_difficulty) << "difficulty button should exist on main menu";
    ASSERT_TRUE(state.has_help) << "help should exist beside quit";
    ASSERT_TRUE(state.has_quit) << "native quit should remain interactable";
}
