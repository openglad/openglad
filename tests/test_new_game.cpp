#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr int kTeamMenuTimeoutMs = 20000;
}

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

static bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs)
{
    int elapsed = 0;
    int since_last_retry = 250;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("view_team"))
            return true;

        if (since_last_retry >= 250 && has_interactable("hire_me")) {
            fprintf(stderr, "  [test] retry clicking back from hire menu\n");
            interact("back");
            since_last_retry = 0;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
        since_last_retry += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n",
            timeout_ms);
    return false;
}

static bool unwind_to_main_menu(int timeout_ms = 7000)
{
    int elapsed = 0;
    const int poll_interval = 100;
    while (elapsed < timeout_ms) {
        if (has_interactable("continue_game") ||
            has_interactable("begin_new_game"))
            return true;
        if (!has_interactable("back"))
            inject_key_press(SDLK_ESCAPE, 10);
        else
            interact("back");
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    return has_interactable("continue_game") ||
           has_interactable("begin_new_game");
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
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // beginmenu() calls read_campaign_intro() which blocks until
    // input_continue is set (SDLK_ESCAPE keydown triggers this).
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro with Escape\n");
    inject_key_press(SDLK_ESCAPE);

    // In TESTING builds, popup_dialog() is a no-op, so no "ok" button exists.

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
    if (wait_for_team_menu()) {
        state->saw_team_menu = true;
        SDL_Delay(750);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    unwind_to_main_menu();
    state->finished = true;
    return 0;
}

TEST(NewGame, begin_new_game) {
    trace_clear();

    // Pre-populate save data so we can verify it gets reset
    og::runtime::current_session->myscreen_->save_data.totalcash = 99999;
    og::runtime::current_session->myscreen_->save_data.totalscore = 55555;
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    // Make sure team_size is 0 so beginmenu doesn't prompt "restart?"
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        if (og::runtime::current_session->myscreen_->save_data.team_list[i]) {
            og::runtime::current_session->myscreen_->save_data.team_list[i].reset();
            og::runtime::current_session->myscreen_->save_data.team_list[i].reset(nullptr);
        }
    }
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    NewGameState state = { false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(new_game_injector, "new_game_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "should have seen the hire menu after new game";
    ASSERT_TRUE(state.saw_team_menu) << "should have returned to team menu from hire";

    // beginmenu calls save_data.reset(), so cash should be the default (starting cash)
    // rather than our 99999
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.totalcash != 99999) << "totalcash should have been reset by new game";
}
