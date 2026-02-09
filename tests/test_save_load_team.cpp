#include "graph.h"
#include "button.h"
#include "test_trace.h"
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "save_data.h"
#include "guy.h"

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

// Globals defined in picker.cpp that we need for cleanup
extern PixieData main_title_logo_data, main_columns_data;
extern pixieN *main_title_logo_pix, *main_columns_pix;
extern pixieN *backdrops[5];
extern PixieData backpics[5];
extern vbutton *localbuttons;

static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        if (backdrops[i]) { delete backdrops[i]; backdrops[i] = nullptr; }
        backpics[i].free();
    }
    for (int i = 0; i < MAX_BUTTONS; i++) {
        if (allbuttons[i]) { delete allbuttons[i]; allbuttons[i] = nullptr; }
    }
    localbuttons = nullptr;
    if (main_columns_pix) { delete main_columns_pix; main_columns_pix = nullptr; }
    main_columns_data.free();
    if (main_title_logo_pix) { delete main_title_logo_pix; main_title_logo_pix = nullptr; }
    main_title_logo_data.free();
}

// Test: Save a team to a slot, start a new game (resetting team), then load
// the old team back from the slot.
//
// This tests the save/load roundtrip at the data layer -- the UI save/load
// menus use prompt_for_string which is hard to drive from tests, so we test
// the underlying SaveData::save/load directly with team members.
//
// Verifies:
//   1. Saving a team with guys preserves their attributes
//   2. Resetting save data clears the team
//   3. Loading restores the team and their stats

void test_save_team_then_load() {
    trace_clear();

    // Build a team with specific guys
    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 3;
    myscreen->save_data.totalcash = 77777;
    myscreen->save_data.totalscore = 42000;

    guy* soldier = new guy(FAMILY_SOLDIER);
    strcpy(soldier->name, "TESTGUY1");
    soldier->strength = 25;
    soldier->dexterity = 15;

    guy* archer = new guy(FAMILY_ARCHER);
    strcpy(archer->name, "TESTGUY2");
    archer->strength = 10;
    archer->intelligence = 20;

    guy* mage = new guy(FAMILY_MAGE);
    strcpy(mage->name, "TESTGUY3");

    myscreen->save_data.team_list[0] = soldier;
    myscreen->save_data.team_list[1] = archer;
    myscreen->save_data.team_list[2] = mage;
    myscreen->save_data.team_size = 3;

    // Save to a non-default slot
    bool saved = myscreen->save_data.save("save5");
    TEST_ASSERT(saved, "save should succeed");

    // Now reset everything -- simulating starting a new game
    myscreen->save_data.reset();
    TEST_ASSERT_EQ(0, myscreen->save_data.team_size, "team_size should be 0 after reset");
    TEST_ASSERT_EQ(0, (int)myscreen->save_data.totalcash, "totalcash should be 0 after reset");

    // Load the saved team back
    trace_clear();
    bool loaded = myscreen->save_data.load("save5");
    TEST_ASSERT(loaded, "load should succeed");

    // Verify team data was restored
    TEST_ASSERT_EQ(3, myscreen->save_data.team_size, "team should have 3 members");
    TEST_ASSERT_EQ(3, myscreen->save_data.scen_num, "scen_num should be restored");
    TEST_ASSERT_EQ(77777, (int)myscreen->save_data.totalcash, "totalcash should be restored");
    TEST_ASSERT_EQ(42000, (int)myscreen->save_data.totalscore, "totalscore should be restored");

    // Verify individual guy data was restored
    TEST_ASSERT(myscreen->save_data.team_list[0] != nullptr, "first guy should exist");
    TEST_ASSERT_STR_EQ("TESTGUY1", myscreen->save_data.team_list[0]->name,
        "first guy name should be restored");
    TEST_ASSERT_EQ(25, myscreen->save_data.team_list[0]->strength,
        "first guy strength should be restored");
    TEST_ASSERT_EQ(15, myscreen->save_data.team_list[0]->dexterity,
        "first guy dexterity should be restored");

    TEST_ASSERT(myscreen->save_data.team_list[1] != nullptr, "second guy should exist");
    TEST_ASSERT_STR_EQ("TESTGUY2", myscreen->save_data.team_list[1]->name,
        "second guy name should be restored");
    TEST_ASSERT_EQ(FAMILY_ARCHER, myscreen->save_data.team_list[1]->family,
        "second guy should be an archer");
    TEST_ASSERT_EQ(20, myscreen->save_data.team_list[1]->intelligence,
        "second guy intelligence should be restored");

    TEST_ASSERT(myscreen->save_data.team_list[2] != nullptr, "third guy should exist");
    TEST_ASSERT_STR_EQ("TESTGUY3", myscreen->save_data.team_list[2]->name,
        "third guy name should be restored");
    TEST_ASSERT_EQ(FAMILY_MAGE, myscreen->save_data.team_list[2]->family,
        "third guy should be a mage");
}
REGISTER_TEST(test_save_team_then_load);


// Test: Navigate to Load Team menu via UI, see the load slots, and exit
//
// Flow: Main Menu -> Continue -> Load Team -> Back -> Back

struct LoadMenuState {
    bool started;
    bool finished;
    bool saw_load_menu;
};

static int load_menu_injector(void* data)
{
    LoadMenuState* state = (LoadMenuState*)data;
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("load_team", 10000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking load_team\n");
    interact("load_team");

    // Load menu has load_slot_1 through load_slot_10 and back
    SDL_Delay(500);
    if (wait_for_interactable("load_slot_1", 10000)) {
        state->saw_load_menu = true;
        SDL_Delay(500);

        fprintf(stderr, "  [test] clicking back from load menu\n");
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

void test_load_team_menu() {
    trace_clear();

    // Need a save so continue_game works
    myscreen->save_data.scen_num = 1;
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.save("save0");

    LoadMenuState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(load_menu_injector, "load_menu_test", &state);
    TEST_ASSERT(thread != nullptr, "failed to create injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should have completed");
    TEST_ASSERT(state.saw_load_menu, "should have seen the load team menu");
}
REGISTER_TEST(test_load_team_menu);
