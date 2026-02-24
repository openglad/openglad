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
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/runtime/picker_ui_state.h>
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

// Test: Open options menu, toggle some settings, then exit.
//
// Flow: Main Menu -> Options -> toggle some settings -> Back -> (main menu exits)
//
// Verifies:
//   1. Options menu opens
//   2. Can toggle visual effects
//   3. Returns cleanly to main menu

struct OptionsState {
    bool started;
    bool finished;
    bool saw_options;
};

static int options_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OptionsState* state = static_cast<OptionsState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("options", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking options\n");
    interact("options");

    // Options menu buttons
    SDL_Delay(500);
    if (wait_for_interactable("toggle_hit_flash", 10000)) {
        state->saw_options = true;
        SDL_Delay(500);

        fprintf(stderr, "  [test] entering player controls\n");
        interact("player_controls");
        SDL_Delay(400);
        if (wait_for_interactable("player1_mode", 5000)) {
            interact("player1_mode");
            SDL_Delay(150);
            interact("player1_remap");
            SDL_Delay(150);
            interact("controls_back");
            SDL_Delay(250);
        }

        // Toggle a few settings
        fprintf(stderr, "  [test] toggling hit flash\n");
        interact("toggle_hit_flash");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling damage numbers\n");
        interact("toggle_damage_numbers");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling gore\n");
        interact("toggle_gore");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling sound/render/fullscreen\n");
        interact("toggle_sound");
        SDL_Delay(200);
        interact("toggle_rendering");
        SDL_Delay(200);
        interact("toggle_fullscreen");
        SDL_Delay(200);

        fprintf(stderr, "  [test] adjusting overscan\n");
        interact("overscan_plus");
        SDL_Delay(200);
        interact("overscan_minus");
        SDL_Delay(200);

        fprintf(stderr, "  [test] toggling additional effects\n");
        interact("toggle_mini_hp_bar");
        SDL_Delay(200);
        interact("toggle_hit_recoil");
        SDL_Delay(200);
        interact("toggle_attack_lunge");
        SDL_Delay(200);
        interact("toggle_hit_sparks");
        SDL_Delay(200);
        interact("toggle_heal_numbers");
        SDL_Delay(200);

        fprintf(stderr, "  [test] restoring defaults\n");
        interact("restore_defaults");
        SDL_Delay(200);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking options_back\n");
        interact("options_back");
    }

    // Ensure mainmenu() returns so picker_main() can complete.
    // In test mode, QUIT does not exit the process; it just returns EXIT_VALUE.
    if (wait_for_interactable("quit", 10000)) {
        SDL_Delay(200);
        fprintf(stderr, "  [test] clicking quit\n");
        interact("quit");
    }

    state->finished = true;
    return 0;
}

void test_options_menu() {
    trace_clear();

    // Need save data for continue_game
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OptionsState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(options_injector, "options_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // options REDRAW is handled within same mainmenu() call

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_options, "should have entered the options menu");
}
REGISTER_TEST(test_options_menu);
