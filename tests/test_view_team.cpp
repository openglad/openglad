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
#include <openglad/entities/guy.h>
#include <openglad/core/util.h>
#include <atomic>
#include <functional>

extern screen* myscreen;

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
Sint32 create_view_menu(Sint32 arg1);
Sint32 create_team_menu(Sint32 arg1);
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

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

static bool wait_for_interactable_match(
    const std::string& id,
    const std::function<bool(const Interactable&)>& predicate,
    int timeout_ms = 5000)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        const auto interactables = get_interactables();
        for (const auto& item : interactables) {
            if (item.id == id && !item.hidden && predicate(item))
                return true;
        }
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for matched '%s' (%d ms)\n", id.c_str(), timeout_ms);
    return false;
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
            const int win_x = static_cast<int>(static_cast<float>(game_x) * (viewport_w / 320.0f) + viewport_offset_x);
            const int win_y = static_cast<int>(static_cast<float>(game_y) * (viewport_h / 200.0f) + viewport_offset_y);
            fprintf(stderr, "  [interact] clicking matched '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            inject_click(win_x, win_y);
            return true;
        }
    }
    fprintf(stderr, "  [interact] WARNING: matched '%s' not found\n", id.c_str());
    return false;
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
    const auto is_view_menu_go = [](const Interactable& item) { return item.y >= 160; };
    const auto is_view_menu_back = [](const Interactable& item) { return item.y >= 160; };
    if (wait_for_interactable_match("go", is_view_menu_go, 5000)
        && wait_for_interactable_match("back", is_view_menu_back, 5000)) {
        state->saw_view_menu = true;
        fprintf(stderr, "  [test] clicking back from view menu\n");
        interact_match("back", is_view_menu_back);
    }

    // BACK from view menu returns to team menu (REDRAW), not main menu.
    // Wait for the team menu to redraw, then click its back button.
    SDL_Delay(500);
    for (int i = 0; i < 8; i++) {
        if (has_interactable("continue_game"))
            break; // main menu
        if (has_interactable("back")) {
            fprintf(stderr, "  [test] clicking back\n");
            interact("back");
        } else {
            inject_key_press(SDLK_ESCAPE, 10);
        }
        SDL_Delay(300);
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

struct ViewTeamGoState {
    bool started;
    bool finished;
    bool saw_view_menu;
    bool game_started;
    bool game_finished;
    float original_speed;
};

static int view_team_go_injector(void* data)
{
    ViewTeamGoState* state = static_cast<ViewTeamGoState*>(data);
    state->started = true;

    if (!wait_for_interactable("continue_game", 5000)) {
        state->finished = true;
        return 0;
    }
    interact("continue_game");

    if (!wait_for_interactable("view_team", 8000)) {
        state->finished = true;
        return 0;
    }
    interact("view_team");

    const auto is_view_menu_go = [](const Interactable& item) { return item.y >= 160; };
    if (!wait_for_interactable_match("go", is_view_menu_go, 5000)) {
        state->finished = true;
        return 0;
    }
    state->saw_view_menu = true;

    g_test_remove_exits = true;
    set_game_speed(0.0f);

    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    interact_match("go", is_view_menu_go);

    int waited_ms = 0;
    const int poll_ms = 50;
    while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before && waited_ms < 10000) {
        SDL_Delay(poll_ms);
        waited_ms += poll_ms;
    }
    state->game_started = g_test_game_epoch.load(std::memory_order_acquire) > epoch_before;

    waited_ms = 0;
    while (g_test_in_game.load(std::memory_order_acquire) && waited_ms < 60000) {
        SDL_Delay(poll_ms);
        waited_ms += poll_ms;
    }
    state->game_finished = !g_test_in_game.load(std::memory_order_acquire);

    set_game_speed(state->original_speed);
    g_test_remove_exits = false;

    if (wait_for_interactable("back", 10000))
        interact("back");

    state->finished = true;
    return 0;
}

void test_view_team_go_starts_level() {
    trace_clear();

    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    soldier->strength = soldier->dexterity = soldier->constitution = soldier->intelligence = soldier->armor = 200;
    archer->strength = archer->dexterity = archer->constitution = archer->intelligence = archer->armor = 200;
    myscreen->save_data.team_list[0] = std::move(soldier);
    myscreen->save_data.team_list[1] = std::move(archer);
    myscreen->save_data.team_size = 2;
    myscreen->save_data.save("save0");

    ViewTeamGoState state = { false, false, false, false, false, g_game_speed_factor };
    SDL_Thread* thread = SDL_CreateThread(view_team_go_injector, "view_team_go", &state);
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
    TEST_ASSERT(state.game_started, "GO from view team should start the game");
    TEST_ASSERT(state.game_finished, "started game should return to picker");
}
REGISTER_TEST(test_view_team_go_starts_level);

struct DirectMenuClickState {
    bool finished;
    bool clicked_target;
    const char* target_id;
    int min_y;
};

static int direct_menu_click_injector(void* data)
{
    auto* state = static_cast<DirectMenuClickState*>(data);

    int elapsed = 0;
    while (elapsed < 5000) {
        auto interactables = get_interactables();
        for (const auto& item : interactables) {
            if (item.id == state->target_id && !item.hidden && item.y >= state->min_y) {
                const int game_x = item.x + item.width / 2;
                const int game_y = item.y + item.height / 2;
                const int win_x = static_cast<int>(static_cast<float>(game_x) * (viewport_w / 320.0f) + viewport_offset_x);
                const int win_y = static_cast<int>(static_cast<float>(game_y) * (viewport_h / 200.0f) + viewport_offset_y);
                inject_click(win_x, win_y);
                state->clicked_target = true;
                state->finished = true;
                return 0;
            }
        }
        SDL_Delay(50);
        elapsed += 50;
    }

    // Safety valve so menu loops don't hang forever in a failed interaction.
    inject_key_press(SDLK_ESCAPE, 10);
    state->finished = true;
    return 0;
}

void test_create_view_menu_direct_back()
{
    trace_clear();

    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    myscreen->save_data.team_list[0] = std::move(soldier);
    myscreen->save_data.team_size = 1;

    DirectMenuClickState state = { false, false, "back", 160 };
    SDL_Thread* thread = SDL_CreateThread(direct_menu_click_injector, "direct_view_back", &state);
    TEST_ASSERT(thread != nullptr, "failed to create direct view-menu injector thread");

    const Sint32 ret = create_view_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    TEST_ASSERT(state.finished, "direct view-menu injector should complete");
    TEST_ASSERT(state.clicked_target, "direct view-menu injector should click back");
    TEST_ASSERT(ret & 2, "create_view_menu(back) should return REDRAW");
}
REGISTER_TEST(test_create_view_menu_direct_back);

void test_create_team_menu_direct_back()
{
    trace_clear();

    myscreen->save_data.reset();
    myscreen->save_data.numplayers = 1;
    myscreen->save_data.current_campaign = "org.openglad.gladiator";
    myscreen->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    myscreen->save_data.team_list[0] = std::move(soldier);
    myscreen->save_data.team_size = 1;

    DirectMenuClickState state = { false, false, "back", 100 };
    SDL_Thread* thread = SDL_CreateThread(direct_menu_click_injector, "direct_team_back", &state);
    TEST_ASSERT(thread != nullptr, "failed to create direct team-menu injector thread");

    const Sint32 ret = create_team_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    TEST_ASSERT(state.finished, "direct team-menu injector should complete");
    TEST_ASSERT(state.clicked_target, "direct team-menu injector should click back");
    TEST_ASSERT(ret & 1, "create_team_menu(back) should propagate EXIT");
}
REGISTER_TEST(test_create_team_menu_direct_back);
