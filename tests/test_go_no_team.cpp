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

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern std::unique_ptr<pixieN> main_title_logo_pix, main_columns_pix;
extern std::array<std::unique_ptr<pixieN>, 5> backdrops;
extern PixieData backpics[5];

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        backdrops[i].reset();
        backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    main_columns_pix.reset();
    main_columns_data.free();
    main_title_logo_pix.reset();
    main_title_logo_data.free();
}

// Test: Clicking GO with no team should show a popup error, not crash or start.
//
// Flow: Main Menu -> Continue -> GO -> (popup "NEED A TEAM!") -> OK -> Back -> Back
//
// Verifies:
//   1. GO button gracefully handles empty team
//   2. Shows popup dialog
//   3. Can continue navigating after the error

struct GoNoTeamState {
    bool started;
    bool finished;
    bool saw_popup;
};

static int go_no_team_injector(void* data)
{
    og::runtime::ensure_thread_session();
    GoNoTeamState* state = static_cast<GoNoTeamState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("go", 10000);
    SDL_Delay(1500);

    // Click GO with no team
    fprintf(stderr, "  [test] clicking go (with empty team)\n");
    interact("go");

    // popup_dialog returns immediately under TESTING, so just wait a moment
    // for the trace to be written, then verify it was called
    SDL_Delay(500);
    state->saw_popup = trace_contains("popup", "NEED A TEAM");

    // Should be back in team menu (popup already dismissed)
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_go_without_team() {
    trace_clear();

    // Set up save with NO team members
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    GoNoTeamState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(go_no_team_injector, "go_no_team", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_popup, "should have seen 'NEED A TEAM!' popup");
}
REGISTER_TEST(test_go_without_team);


// Test: Clicking TRAIN TEAM with no team should show popup, not crash.
//
// Flow: Main Menu -> Continue -> Train Team -> (popup) -> OK -> Back -> Back

struct TrainNoTeamState {
    bool started;
    bool finished;
    bool saw_popup;
};

static int train_no_team_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TrainNoTeamState* state = static_cast<TrainNoTeamState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("train_team", 10000);
    SDL_Delay(1500);

    // Click TRAIN with no team
    fprintf(stderr, "  [test] clicking train_team (with empty team)\n");
    interact("train_team");

    // popup_dialog returns immediately under TESTING, so just wait a moment
    // for the trace to be written, then verify it was called
    SDL_Delay(500);
    state->saw_popup = trace_contains("popup", "NEED A TEAM");

    // Should be back in team menu (popup already dismissed)
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_train_without_team() {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    TrainNoTeamState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(train_no_team_injector, "train_no_team", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_popup, "should have seen 'NEED A TEAM!' popup");
}
REGISTER_TEST(test_train_without_team);
