#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <functional>
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

static bool interact_match(
    const std::string& id,
    const std::function<bool(const Interactable&)>& predicate)
{
    const auto interactables = get_interactables();
    for (const auto& item : interactables) {
        if (item.id == id && !item.hidden && predicate(item)) {
            const int game_x = item.x + item.width / 2;
            const int game_y = item.y + item.height / 2;
            const int win_x = static_cast<int>(static_cast<float>(game_x)
                * (og::runtime::current_session->viewport_w_ / 320.0f)
                + og::runtime::current_session->viewport_offset_x_);
            const int win_y = static_cast<int>(static_cast<float>(game_y)
                * (og::runtime::current_session->viewport_h_ / 200.0f)
                + og::runtime::current_session->viewport_offset_y_);
            fprintf(stderr, "  [interact] clicking matched '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            inject_click(win_x, win_y);
            return true;
        }
    }
    fprintf(stderr, "  [interact] WARNING: matched '%s' not found\n", id.c_str());
    return false;
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

TEST(SaveLoadTeam, save_team_then_load) {
    trace_clear();

    // Build a team with specific guys
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 3;
    og::runtime::current_session->myscreen_->save_data.totalcash = 77777;
    og::runtime::current_session->myscreen_->save_data.totalscore = 42000;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->name = "TESTGUY1";
    soldier->strength = 25;
    soldier->dexterity = 15;

    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    archer->name = "TESTGUY2";
    archer->strength = 10;
    archer->intelligence = 20;

    auto mage = std::make_unique<guy>(FAMILY_MAGE);
    mage->name = "TESTGUY3";

    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_list[2] = std::move(mage);
    og::runtime::current_session->myscreen_->save_data.team_size = 3;

    // Save to a non-default slot
    bool saved = og::runtime::current_session->myscreen_->save_data.save("save5");
    ASSERT_TRUE(saved) << "save should succeed";

    // Now reset everything -- simulating starting a new game
    og::runtime::current_session->myscreen_->save_data.reset();
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.team_size) << "team_size should be 0 after reset";
    ASSERT_EQ(0, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be 0 after reset";

    // Load the saved team back
    trace_clear();
    bool loaded = og::runtime::current_session->myscreen_->save_data.load("save5");
    ASSERT_TRUE(loaded) << "load should succeed";

    // Verify team data was restored
    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.team_size) << "team should have 3 members";
    ASSERT_EQ(3, og::runtime::current_session->myscreen_->save_data.scen_num) << "scen_num should be restored";
    ASSERT_EQ(77777, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalcash)) << "totalcash should be restored";
    ASSERT_EQ(42000, static_cast<int>(og::runtime::current_session->myscreen_->save_data.totalscore)) << "totalscore should be restored";

    // Verify individual guy data was restored
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[0] != nullptr) << "first guy should exist";
    ASSERT_STREQ("TESTGUY1", og::runtime::current_session->myscreen_->save_data.team_list[0]->name.c_str()) << "first guy name should be restored";
    ASSERT_EQ(25, og::runtime::current_session->myscreen_->save_data.team_list[0]->strength) << "first guy strength should be restored";
    ASSERT_EQ(15, og::runtime::current_session->myscreen_->save_data.team_list[0]->dexterity) << "first guy dexterity should be restored";

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1] != nullptr) << "second guy should exist";
    ASSERT_STREQ("TESTGUY2", og::runtime::current_session->myscreen_->save_data.team_list[1]->name.c_str()) << "second guy name should be restored";
    ASSERT_EQ(FAMILY_ARCHER, og::runtime::current_session->myscreen_->save_data.team_list[1]->family) << "second guy should be an archer";
    ASSERT_EQ(20, og::runtime::current_session->myscreen_->save_data.team_list[1]->intelligence) << "second guy intelligence should be restored";

    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[2] != nullptr) << "third guy should exist";
    ASSERT_STREQ("TESTGUY3", og::runtime::current_session->myscreen_->save_data.team_list[2]->name.c_str()) << "third guy name should be restored";
    ASSERT_EQ(FAMILY_MAGE, og::runtime::current_session->myscreen_->save_data.team_list[2]->family) << "third guy should be a mage";
}



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
    og::runtime::ensure_thread_session();
    LoadMenuState* state = static_cast<LoadMenuState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("load_team", 10000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking load_team\n");
    interact("load_team");

    // Load menu has load_slot_1 through load_slot_10 and back
    SDL_Delay(500);
    if (wait_for_interactable("load_slot_1", 10000)) {
        state->saw_load_menu = true;
        SDL_Delay(500);

        fprintf(stderr, "  [test] clicking back from load menu\n");
        interact_match("back", [](const Interactable& item) { return item.y >= 170; });
    }

    const Uint32 deadline = SDL_GetTicks() + 10000;
    while (SDL_GetTicks() < deadline)
    {
        if (wait_for_interactable("continue_game", 150))
            break;

        if (wait_for_interactable("load_team", 150))
        {
            fprintf(stderr, "  [test] clicking back from team menu\n");
            interact_match("back", [](const Interactable& item) { return item.y < 170; });
            SDL_Delay(150);
            continue;
        }

        if (wait_for_interactable("back", 150))
        {
            fprintf(stderr, "  [test] retry clicking back from load menu\n");
            interact_match("back", [](const Interactable& item) { return item.y >= 170; });
            SDL_Delay(150);
            continue;
        }

        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

TEST(SaveLoadTeam, load_team_menu) {
    trace_clear();

    // Need a save so continue_game works
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    LoadMenuState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(load_menu_injector, "load_menu_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_load_menu) << "should have seen the load team menu";
}
