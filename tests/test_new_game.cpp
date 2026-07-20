#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
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
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("hire_troops") && has_interactable("networking"))
            return true;

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n",
            timeout_ms);
    return false;
}

// Test: Click "BEGIN NEW GAME" from the main menu, which should reset save data
// and land directly on the team-build screen with Networking available.
// Then click BACK to return to the main menu.
//
// This verifies:
//   1. The begin_new_game button works
//   2. Save data gets reset on new game
//   3. The team-build menu appears immediately after the intro
//   4. Networking is available from that menu
//   5. Navigation back to main menu works

struct NewGameState {
    bool started;
    bool finished;
    bool saw_team_menu;
    bool saw_networking_button;
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

    // §2.2: BEGIN NEW GAME now opens the name-entry screen first. Accept the
    // generated company name to found the company and proceed to the intro.
    wait_for_interactable("company_name_accept", 5000);
    SDL_Delay(750);  // FadeAroundEntry settle
    fprintf(stderr, "  [test] accepting generated company name\n");
    interact("company_name_accept");

    // picker_prepare_new_game_setup then calls read_campaign_intro() which
    // blocks until input_continue is set (SDLK_ESCAPE keydown triggers this).
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro with Escape\n");
    inject_key_press(SDLK_ESCAPE);

    // Now we should be on the team-build menu immediately.
    SDL_Delay(500);
    if (wait_for_team_menu()) {
        state->saw_team_menu = true;
        state->saw_networking_button = has_interactable("networking");
        SDL_Delay(750);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

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
    // §2.1: BEGIN NEW GAME's "There is already a game loaded. Do you want to
    // restart?" prompt is RETIRED — founding a company never destroys the
    // loaded game. Seed save0 with a team member (the exact team_size > 0
    // condition that used to raise the prompt) so this flow proves BEGIN NEW
    // GAME now goes straight to the intro. Were the prompt still present, the
    // injector — which never answers it — would hang until timeout.
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[i].reset(nullptr);
    }
    og::runtime::current_session->myscreen_->save_data.team_list[0] =
        std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
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
    ASSERT_TRUE(state.saw_team_menu) << "should have landed on the team menu after new game";
    ASSERT_TRUE(state.saw_networking_button) << "networking should be available immediately after new game";

    // beginmenu calls save_data.reset(), so cash should be the default (starting cash)
    // rather than our 99999
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.totalcash != 99999) << "totalcash should have been reset by new game";

    // §2.2: the name-entry ACCEPT founded the company (traced with the chosen
    // display name), and that name landed in the 40-byte save_name field.
    ASSERT_TRUE(trace_contains("name_entry", "accept")) << "name-entry ACCEPT should have fired";
    ASSERT_FALSE(og::runtime::current_session->myscreen_->save_data.save_name.empty())
        << "the founded company's display name should be stamped into save_name";
}

// §2.2: BACK from the name-entry screen founds NOTHING — the previously loaded
// game survives (its cash is not reset), and REROLL is reachable/clickable
// before cancelling. Proves the "nothing is destroyed" contract for cancel.
static int name_entry_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Name-entry appears. Reroll the suggestion, then BACK out (cancel).
    if (wait_for_interactable("company_name_reroll", 5000)) {
        SDL_Delay(750);  // FadeAroundEntry settle
        fprintf(stderr, "  [test] clicking REROLL\n");
        interact("company_name_reroll");
        SDL_Delay(300);  // let the click release before the next press
        fprintf(stderr, "  [test] clicking BACK (cancel)\n");
        interact("back");
        state->saw_team_menu = true;  // reused flag: reached & left name-entry
    }

    state->finished = true;
    return 0;
}

TEST(NewGame, name_entry_back_cancels_without_founding) {
    trace_clear();

    // Seed a distinctly-valued loaded game and persist it: picker_main reloads
    // the active company (save0) at startup, so the sentinels must be on disk
    // to survive into the run. BACK must then leave them untouched.
    og::runtime::current_session->myscreen_->save_data.totalcash = 424242;
    og::runtime::current_session->myscreen_->save_data.save_name = "PRIOR COMPANY";
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    NewGameState state = { false, false, false, false };
    SDL_Thread* thread =
        SDL_CreateThread(name_entry_cancel_injector, "name_entry_cancel", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu) << "should have reached the name-entry screen";
    ASSERT_TRUE(trace_contains("name_entry", "reroll")) << "REROLL should have fired";
    ASSERT_TRUE(trace_contains("name_entry", "cancel")) << "BACK should have cancelled";
    // The loaded game survived: cancel founded nothing, so no reset ran.
    ASSERT_EQ(424242u, og::runtime::current_session->myscreen_->save_data.totalcash)
        << "cancel must not reset the loaded game";
    ASSERT_EQ("PRIOR COMPANY", og::runtime::current_session->myscreen_->save_data.save_name)
        << "cancel must not overwrite the loaded company name";
}

// §2.2: clicking the name strip opens an in-place editor; the typed name
// becomes the founded company's display name. Exercises the SDL edit path
// (input_string_value under the engine's MenuSpecRow dispatch).
static int name_entry_edit_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    if (wait_for_interactable("company_name_value", 5000)) {
        SDL_Delay(750);  // FadeAroundEntry settle
        fprintf(stderr, "  [test] clicking the name strip to edit\n");
        interact("company_name_value");  // opens input_string_value (blocks)
        SDL_Delay(400);  // let the engine dispatch + the editor start + clear
        // The first text input replaces the pre-filled suggestion entirely.
        inject_text_input("MY GUILD");
        SDL_Delay(50);
        inject_key_press(SDLK_RETURN);  // commit the edit
        SDL_Delay(400);
        fprintf(stderr, "  [test] accepting the edited name\n");
        interact("company_name_accept");
    }

    // Dismiss the campaign intro so the flow reaches team build.
    SDL_Delay(1000);
    inject_key_press(SDLK_ESCAPE);

    // Unwind back to the main menu so picker_main can hit its Quit gate.
    SDL_Delay(500);
    if (wait_for_team_menu()) {
        state->saw_team_menu = true;
        SDL_Delay(750);
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

TEST(NewGame, name_entry_edit_strip_sets_company_name) {
    trace_clear();

    NewGameState state = { false, false, false, false };
    SDL_Thread* thread =
        SDL_CreateThread(name_entry_edit_injector, "name_entry_edit", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu) << "should have reached the name-entry strip";
    ASSERT_TRUE(trace_contains("name_entry", "edit MY GUILD"))
        << "the strip edit should capture the typed name";
    ASSERT_EQ("MY GUILD", og::runtime::current_session->myscreen_->save_data.save_name)
        << "the edited name should become the founded company's display name";
}
