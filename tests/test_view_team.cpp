#include <memory>
#include <array>
#include "graph.h"
#include "input/button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "data/save_data.h"
#include "entities/guy.h"
#include <openglad/core/util.h>

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern std::unique_ptr<pixieN> main_title_logo_pix, main_columns_pix;
extern std::array<std::unique_ptr<pixieN>, 5> backdrops;
extern PixieData backpics[5];
extern vbutton *localbuttons;

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        backdrops[i].reset();
        backpics[i].free();
    }
    clear_allbuttons();
    localbuttons = nullptr;
    main_columns_pix.reset();
    main_columns_data.free();
    main_title_logo_pix.reset();
    main_title_logo_data.free();
}

// Test: Continue -> View Team -> Back -> Back
//
// Verifies:
//   1. View Team menu opens with a team
//   2. The view menu has GO and BACK buttons
//   3. Can navigate back cleanly

struct ViewState {
    bool started;
    bool finished;
    bool saw_view_menu;
};

static int view_team_injector(void* data)
{
    ViewState* state = static_cast<ViewState*>(data);
    state->started = true;

    // Wait for main menu and enter the create-team menu.
    if (!wait_for_interactable("continue_game", 5000)) {
        // Don't hang the whole suite if the menu didn't initialize.
        state->finished = true;
        return 0;
    }
    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    // The picker menus are stateful; if our click didn't register (rare under load),
    // re-click continue_game. If we do reach the create-team menu, view_team will be present.
    const int kMenuTimeoutMs = 8000;
    int elapsed = 0;
    while (elapsed < kMenuTimeoutMs && !has_interactable("view_team")) {
        if (has_interactable("continue_game")) {
            fprintf(stderr, "  [test] retry clicking continue_game\n");
            interact("continue_game");
        } else if (has_interactable("begin_new_game")) {
            // Still on main menu but continue_game missing? Give it a moment.
        }
        SDL_Delay(50);
        elapsed += 50;
    }

    fprintf(stderr, "  [test] clicking view_team\n");
    interact("view_team");

    // View team menu has "go" and "back" buttons.
    if (wait_for_interactable("go", 5000) && wait_for_interactable("back", 5000)) {
        state->saw_view_menu = true;
        fprintf(stderr, "  [test] clicking back from view menu\n");
        interact("back");
    }

    // Ensure we return to main menu even if the view menu wasn't reached.
    // Prefer the BACK button when present; otherwise, fall back to Escape.
    for (int i = 0; i < 4; i++) {
        if (has_interactable("continue_game"))
            break; // main menu
        if (has_interactable("back")) {
            fprintf(stderr, "  [test] clicking back\n");
            interact("back");
        } else {
            inject_key_press(SDLK_ESCAPE, 10);
        }
        SDL_Delay(200);
    }

    state->finished = true;
    return 0;
}

void test_view_team() {
    trace_clear();

    // Set up a team so view has something to show
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    // Give the team strong stats so the launched game finishes quickly.
    soldier->strength = soldier->dexterity = soldier->constitution = soldier->intelligence = soldier->armor = 200;
    archer->strength = archer->dexterity = archer->constitution = archer->intelligence = archer->armor = 200;
    myscreen->save_data.team_list[0] = std::move(soldier);
    myscreen->save_data.team_list[1] = std::move(archer);
    myscreen->save_data.team_size = 2;

    myscreen->save_data.save("save0");

    ViewState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(view_team_injector, "view_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_view_menu, "should have entered the view team menu");
}
REGISTER_TEST(test_view_team);
