#include <memory>
#include <array>
#include <openglad/input/button.h>
#include <openglad/runtime/screen.h>
#include <openglad/render/pixien.h>
#include <openglad/legacy/test_trace.h>
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/data/save_data.h>
#include <openglad/entities/guy.h>
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
    localbuttons = nullptr;
    main_columns_pix.reset();
    main_columns_data.free();
    main_title_logo_pix.reset();
    main_title_logo_data.free();
}

// Test: Navigate to Save Team menu, see the save slots, then exit.
//
// Note: We can't test actually saving via menu because do_save() calls
// prompt_for_string() which requires keyboard text input. But we CAN
// verify the save menu opens and has the right buttons.
//
// Flow: Main Menu -> Continue -> Save Team -> Back -> Back

struct SaveMenuState {
    bool started;
    bool finished;
    bool saw_save_menu;
};

static int save_menu_injector(void* data)
{
    SaveMenuState* state = static_cast<SaveMenuState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("save_team", 10000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking save_team\n");
    interact("save_team");

    // Save menu has save_slot_1 through save_slot_10 and back
    SDL_Delay(500);
    if (wait_for_interactable("save_slot_1", 10000)) {
        state->saw_save_menu = true;
        SDL_Delay(500);

        // Just verify the slots exist, don't actually try to save
        // (that would trigger prompt_for_string)
        fprintf(stderr, "  [test] clicking back from save menu\n");
        interact("back");
    }

    // Back in team menu
    SDL_Delay(2000);
    wait_for_interactable("back", 10000);
    SDL_Delay(500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

void test_save_team_menu() {
    trace_clear();

    // Need some team data
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;

    myscreen->save_data.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    myscreen->save_data.team_size = 1;
    myscreen->save_data.save("save0");

    SaveMenuState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(save_menu_injector, "save_menu_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_save_menu, "should have seen the save team menu");
}
REGISTER_TEST(test_save_team_menu);
