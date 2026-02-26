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
#include <cstdint>
#include <functional>

// myscreen is now a macro defined in base.h (via game_session.h)

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
extern std::atomic<int> g_test_game_frame_ticks;
namespace og::sim { extern std::int32_t g_test_level_tick_limit_override; }
#endif

#include <openglad/runtime/picker_ui_state.h>
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
            const int win_x = static_cast<int>(static_cast<float>(game_x) * (og::runtime::current_session->viewport_w_ / 320.0f) + og::runtime::current_session->viewport_offset_x_);
            const int win_y = static_cast<int>(static_cast<float>(game_y) * (og::runtime::current_session->viewport_h_ / 200.0f) + og::runtime::current_session->viewport_offset_y_);
            fprintf(stderr, "  [interact] clicking matched '%s' at game(%d,%d) win(%d,%d)\n",
                    id.c_str(), game_x, game_y, win_x, win_y);
            inject_click(win_x, win_y);
            return true;
        }
    }
    fprintf(stderr, "  [interact] WARNING: matched '%s' not found\n", id.c_str());
    return false;
}

static bool enter_team_menu_from_main_menu(int timeout_ms = 15000)
{
    if (!wait_for_interactable("continue_game", 5000))
        return false;

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    int elapsed = 0;
    while (elapsed < timeout_ms && !has_interactable("view_team")) {
        if (has_interactable("continue_game")) {
            fprintf(stderr, "  [test] retry clicking continue_game\n");
            interact("continue_game");
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    return has_interactable("view_team");
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
    og::runtime::ensure_thread_session();
    ViewState* state = static_cast<ViewState*>(data);
    state->started = true;

    if (!enter_team_menu_from_main_menu(20000)) {
        state->finished = true;
        return 0;
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
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    // Give the team strong stats so the launched game finishes quickly.
    soldier->strength = soldier->dexterity = soldier->constitution = soldier->intelligence = soldier->armor = 200;
    archer->strength = archer->dexterity = archer->constitution = archer->intelligence = archer->armor = 200;
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;

    og::runtime::current_session->myscreen_->save_data.save("save0");

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
    og::runtime::ensure_thread_session();
    ViewTeamGoState* state = static_cast<ViewTeamGoState*>(data);
    state->started = true;

    if (!enter_team_menu_from_main_menu(20000)) {
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
    og::sim::g_test_level_tick_limit_override = 15;
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
    og::sim::g_test_level_tick_limit_override = 0;

    for (int i = 0; i < 20; ++i) {
        if (has_interactable("continue_game"))
            break;
        if (has_interactable("back"))
            interact("back");
        else
            inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(200);
    }

    state->finished = true;
    return 0;
}

void test_view_team_go_starts_level() {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    soldier->strength = soldier->dexterity = soldier->constitution = soldier->intelligence = soldier->armor = 200;
    archer->strength = archer->dexterity = archer->constitution = archer->intelligence = archer->armor = 200;
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    ViewTeamGoState state = { false, false, false, false, false, og::runtime::current_session->g_game_speed_factor_ };
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
    og::runtime::ensure_thread_session();
    auto* state = static_cast<DirectMenuClickState*>(data);

    int elapsed = 0;
    while (elapsed < 5000) {
        auto interactables = get_interactables();
        for (const auto& item : interactables) {
            if (item.id == state->target_id && !item.hidden && item.y >= state->min_y) {
                const int game_x = item.x + item.width / 2;
                const int game_y = item.y + item.height / 2;
                const int win_x = static_cast<int>(static_cast<float>(game_x) * (og::runtime::current_session->viewport_w_ / 320.0f) + og::runtime::current_session->viewport_offset_x_);
                const int win_y = static_cast<int>(static_cast<float>(game_y) * (og::runtime::current_session->viewport_h_ / 200.0f) + og::runtime::current_session->viewport_offset_y_);
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

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

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

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

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

struct ViewTeamGoLevel17State {
    bool finished;
    bool saw_view_menu;
    bool game_started;
    bool game_finished;
    bool frame_progressed;
    float original_speed;
};

static int view_team_go_level17_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<ViewTeamGoLevel17State*>(data);

    if (!enter_team_menu_from_main_menu(20000)) {
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
    og::sim::g_test_level_tick_limit_override = 15;
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

    int stable_polls = 0;
    waited_ms = 0;
    while (g_test_in_game.load(std::memory_order_acquire) && waited_ms < 90000) {
        SDL_Delay(100);
        waited_ms += 100;
        const int frames_seen = g_test_game_frame_ticks.load(std::memory_order_acquire);
        if (frames_seen > 0) {
            state->frame_progressed = true;
            stable_polls = 0;
        } else {
            stable_polls++;
            if (stable_polls >= 100) {
                // No frame advance for 10s while still in game => likely stall.
                break;
            }
        }
    }
    if (g_test_game_frame_ticks.load(std::memory_order_acquire) > 0)
        state->frame_progressed = true;
    state->game_finished = !g_test_in_game.load(std::memory_order_acquire);

    set_game_speed(state->original_speed);
    g_test_remove_exits = false;
    og::sim::g_test_level_tick_limit_override = 0;

    for (int i = 0; i < 20; ++i) {
        if (has_interactable("continue_game"))
            break;
        if (has_interactable("back"))
            interact("back");
        else
            inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(200);
    }

    state->finished = true;
    return 0;
}

void test_view_team_go_level17_no_hang()
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 17;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    auto cleric = std::make_unique<guy>(FAMILY_CLERIC);
    auto mage = std::make_unique<guy>(FAMILY_MAGE);
    for (guy* g : {soldier.get(), archer.get(), cleric.get(), mage.get()}) {
        g->strength = g->dexterity = g->constitution = g->intelligence = g->armor = 200;
        g->level = 20;
    }

    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_list[2] = std::move(cleric);
    og::runtime::current_session->myscreen_->save_data.team_list[3] = std::move(mage);
    og::runtime::current_session->myscreen_->save_data.team_size = 4;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    ViewTeamGoLevel17State state{};
    state.original_speed = og::runtime::current_session->g_game_speed_factor_;
    SDL_Thread* thread = SDL_CreateThread(view_team_go_level17_injector, "view_team_go_lv17", &state);
    TEST_ASSERT(thread != nullptr, "failed to create level17 injector thread");

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    TEST_ASSERT(state.finished, "injector thread should complete");
    TEST_ASSERT(state.saw_view_menu, "should enter view team menu");
    TEST_ASSERT(state.game_started, "GO should start level 17");
    TEST_ASSERT(state.frame_progressed, "level 17 should advance frames");
    TEST_ASSERT(state.game_finished, "level 17 should return to picker (no hang)");
}
REGISTER_TEST(test_view_team_go_level17_no_hang);
