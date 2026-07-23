#include <memory>
#include <array>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
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

// Test: Navigate to hire troops, browse characters with NEXT/PREV, then exit.
//
// Note: We can't click HIRE ME because add_guy() calls prompt_for_string()
// to name the character, which blocks on text input. Instead we test that
// the hire menu loads, character cycling works, and we can exit cleanly.
//
// Flow: Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//       Team Build -> Hire Troops -> NEXT -> NEXT -> PREV -> Back -> Back

struct HireState {
    bool started;
    bool finished;
    bool saw_hire_menu;
    int cycles_completed;
};

static int hire_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HireState* state = static_cast<HireState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // §2.2: accept the generated company name at the name-entry screen.
    accept_generated_company_name();

    // Dismiss campaign intro screen
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro with Escape\n");
    inject_key_press(SDLK_ESCAPE);

    // New games now land on team build first, then enter hire explicitly.
    SDL_Delay(500);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(300);
    fprintf(stderr, "  [test] clicking hire_troops\n");
    interact("hire_troops");

    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);

        // Cycle through characters with NEXT
        fprintf(stderr, "  [test] clicking next\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        fprintf(stderr, "  [test] clicking next again\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        // And back with PREV
        fprintf(stderr, "  [test] clicking prev\n");
        interact("prev");
        state->cycles_completed++;
        SDL_Delay(300);
    }

    // Go back to team menu
    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // Back to main menu
    SDL_Delay(500);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_menu_browsing) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireState state = { false, false, false, 0 };
    SDL_Thread* thread = SDL_CreateThread(hire_injector, "hire_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "should have seen the hire menu";
    ASSERT_TRUE(state.cycles_completed >= 3) << "should have cycled through characters 3 times";
}

// §2.9 flow 7 + §3.8: HIRE re-enters from the base-camp command strip, a
// successful hire lands as a roster row that is DEPLOYED BY DEFAULT, and the
// hire mutation autosaves the company (the new member is on disk without any
// manual save). Under TESTING the hire name prompt accepts the generated
// name without blocking.
//
// Flow: Main Menu -> Continue -> base camp -> HIRE -> hire_me -> Back ->
//       base camp shows the new row -> Back

struct HireDeployState {
    bool started;
    bool finished;
    bool saw_hire_menu;
    bool hired;
    bool saw_new_row;
};

static int hire_deployed_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<HireDeployState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    SDL_Delay(500);
    if (!wait_for_interactable("hire_troops", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);
    interact("hire_troops");

    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);
        fprintf(stderr, "  [test] clicking hire_me\n");
        interact("hire_me");
        state->hired = true;
        SDL_Delay(500);
    }

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // Re-entry: the base camp shows the hired member as roster row 1.
    SDL_Delay(500);
    if (wait_for_interactable("roster_dep_1", 10000)) {
        state->saw_new_row = true;
    }
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking back from base camp\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_from_base_camp_lands_deployed_and_autosaves) {
    trace_clear();

    // One existing member + gold to hire with; CONTINUE needs the file on
    // disk (it also serves as the pre-hire disk baseline).
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    {
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "VETERAN";
        og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
        og::runtime::current_session->myscreen_->save_data.team_size = 1;
        og::runtime::current_session->myscreen_->save_data.m_totalcash[0] = 100000;
        og::runtime::current_session->myscreen_->save_data.totalcash = 100000;
    }
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireDeployState state = { false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(hire_deployed_injector, "hire_deploy_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "HIRE strip button should open the hire screen";
    ASSERT_TRUE(state.hired) << "hire_me should be clickable";
    ASSERT_TRUE(state.saw_new_row)
        << "the hired member must appear as a base-camp roster row on re-entry";

    // In memory: the hire landed and defaults to deployed (§2.5).
    ASSERT_EQ(2, static_cast<int>(og::runtime::current_session->myscreen_->save_data.team_size));
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1] != nullptr);
    EXPECT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1]->deployed)
        << "new hires default to deployed=true";

    // §3.8: the hire mutation AUTOSAVED — the pre-hire disk baseline had one
    // member; the file must now hold the hired member, deployed.
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size));
    ASSERT_TRUE(reloaded.team_list[1] != nullptr);
    EXPECT_TRUE(reloaded.team_list[1]->deployed)
        << "the hired member must persist deployed via the mutation autosave";
    EXPECT_EQ("VETERAN", reloaded.team_list[0]->name) << "slot 0 untouched";
}
