#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>
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

static bool click_until_interactable(const std::string& click_id, const std::string& next_id, int timeout_ms)
{
    const Uint32 deadline = SDL_GetTicks() + static_cast<Uint32>(timeout_ms);
    while (SDL_GetTicks() < deadline) {
        if (has_interactable(next_id))
            return true;
        if (has_interactable(click_id)) {
            interact(click_id);
            SDL_Delay(250);
            continue;
        }
        SDL_Delay(50);
    }
    return has_interactable(next_id);
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
    bool entered_controls;
    bool exited_controls;
    bool used_options_back;
};

static int options_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OptionsState* state = static_cast<OptionsState*>(data);
    state->started = true;

    // Wait for main menu
    if (!wait_for_interactable("options", 5000)) {
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking options\n");
    bool in_options = click_until_interactable("options", "toggle_hit_flash", 10000);

    // Options menu buttons
    SDL_Delay(150);
    if (in_options || wait_for_interactable("toggle_hit_flash", 10000)) {
        state->saw_options = true;
        SDL_Delay(150);

        fprintf(stderr, "  [test] entering player controls\n");
        bool in_controls = click_until_interactable("player_controls", "player1_mode", 5000);
        SDL_Delay(150);
        if (in_controls || wait_for_interactable("player1_mode", 5000)) {
            state->entered_controls = true;
            interact("player1_mode");
            if (wait_for_interactable("controls_back", 5000)) {
                SDL_Delay(200);
                state->exited_controls = click_until_interactable("controls_back", "toggle_hit_flash", 5000);
            }
            wait_for_interactable("toggle_hit_flash", 10000);
            SDL_Delay(150);
        }

        // Toggle a few settings
        fprintf(stderr, "  [test] toggling hit flash\n");
        interact("toggle_hit_flash");
        SDL_Delay(80);

        fprintf(stderr, "  [test] toggling damage numbers\n");
        interact("toggle_damage_numbers");
        SDL_Delay(80);

        fprintf(stderr, "  [test] toggling gore\n");
        interact("toggle_gore");
        SDL_Delay(80);

        fprintf(stderr, "  [test] toggling sound/render/fullscreen\n");
        interact("toggle_sound");
        SDL_Delay(80);
        interact("toggle_rendering");
        SDL_Delay(80);
        interact("toggle_fullscreen");
        SDL_Delay(80);

        fprintf(stderr, "  [test] adjusting overscan\n");
        interact("overscan_plus");
        SDL_Delay(80);
        interact("overscan_minus");
        SDL_Delay(80);

        fprintf(stderr, "  [test] toggling additional effects\n");
        interact("toggle_mini_hp_bar");
        SDL_Delay(80);
        interact("toggle_hit_recoil");
        SDL_Delay(80);
        interact("toggle_attack_lunge");
        SDL_Delay(80);
        interact("toggle_hit_sparks");
        SDL_Delay(80);
        interact("toggle_heal_numbers");
        SDL_Delay(80);

        fprintf(stderr, "  [test] restoring defaults\n");
        interact("restore_defaults");
        SDL_Delay(80);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking options_back\n");
        if (wait_for_interactable("options_back", 5000)) {
            interact("options_back");
            state->used_options_back = true;
            SDL_Delay(500);
        }
    }

    // Ensure mainmenu() returns so picker_main() can complete. Coverage builds
    // can redraw the main menu slowly after leaving options, so use Escape to
    // unwind whichever menu is currently active.
    const Uint32 quit_deadline = SDL_GetTicks() + 3000;
    while (SDL_GetTicks() < quit_deadline) {
        if (has_interactable("quit")) {
            SDL_Delay(80);
            fprintf(stderr, "  [test] clicking quit\n");
            interact("quit");
            break;
        }

        inject_key_press(SDLK_ESCAPE, 20);
        SDL_Delay(150);
    }

    state->finished = true;
    return 0;
}

TEST(OptionsMenu, options_menu) {
    trace_clear();

    // Need save data for continue_game
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OptionsState state = { false, false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(options_injector, "options_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;  // options REDRAW is handled within same mainmenu() call

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.started) << "injector thread should have started";
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_options) << "should have entered the options menu";
    ASSERT_TRUE(state.entered_controls) << "should have entered controls submenu";
    ASSERT_TRUE(state.exited_controls) << "should have exited via controls_back";
    ASSERT_TRUE(state.used_options_back) << "should have exited options via options_back";
}
