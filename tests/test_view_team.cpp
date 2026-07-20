#include <array>
#include <algorithm>
#include <memory>
#include <openglad/resources/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/util.h>
#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <list>

namespace {
constexpr int kViewMenuTransitionTimeoutMs = 15000;
constexpr int kGameStartTimeoutMs = 20000;
constexpr int kGameFinishTimeoutMs = 90000;
constexpr int kTeamBuildInterceptScope = 2;
}

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
Sint32 create_team_menu(Sint32 arg1);
Sint32 create_train_menu(Sint32 arg1);
extern bool g_start_game_requested;
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
extern std::atomic<int> g_test_game_frame_ticks;
namespace og::sim { extern std::int32_t g_test_level_tick_limit_override; }
#endif

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {

class AutoStartPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override
    {
        ++poll_calls;
        g_start_game_requested = true;
    }
    void set_player_mode(int) override {}
    bool request_start_game() override
    {
        return false;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return true;
    }

    int poll_calls = 0;
};

class HostVisibilityPickerLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    explicit HostVisibilityPickerLobbyClient(bool host_controls_visible)
        : host_controls_visible_(host_controls_visible)
    {
    }

    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override
    {
        return false;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return std::nullopt;
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host_controls_visible_;
    }

private:
    bool host_controls_visible_ = true;
};

struct ActivePickerLobbyClientGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;

    explicit ActivePickerLobbyClientGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }

    ~ActivePickerLobbyClientGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

} // namespace


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

static bool interact_silent(const std::string& id)
{
    const auto interactables = get_interactables();
    for (const auto& item : interactables) {
        if (item.id == id && !item.hidden) {
            const int game_x = item.x + item.width / 2;
            const int game_y = item.y + item.height / 2;
            const int win_x = static_cast<int>(static_cast<float>(game_x)
                * (og::runtime::current_session->viewport_w_ / 320.0f)
                + og::runtime::current_session->viewport_offset_x_);
            const int win_y = static_cast<int>(static_cast<float>(game_y)
                * (og::runtime::current_session->viewport_h_ / 200.0f)
                + og::runtime::current_session->viewport_offset_y_);
            inject_click(win_x, win_y);
            return true;
        }
    }
    return false;
}

static bool has_interactable_match(
    const std::string& id,
    const std::function<bool(const Interactable&)>& predicate)
{
    const auto interactables = get_interactables();
    for (const auto& item : interactables) {
        if (item.id == id && !item.hidden && predicate(item))
            return true;
    }
    return false;
}

static bool unwind_to_main_menu(int timeout_ms = 7000)
{
    int elapsed = 0;
    const int poll_interval = 100;
    while (elapsed < timeout_ms) {
        if (has_interactable("continue_game"))
            return true;
        if (!interact_silent("back"))
            inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }
    return has_interactable("continue_game");
}

// §2.5 base camp: the roster IS the default view — wait for a roster row
// pair plus the command strip (GO/BACK at y=178).
static bool wait_for_base_camp_roster(int timeout_ms = 6000)
{
    const auto is_strip_button = [](const Interactable& item) { return item.y >= 160; };
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("roster_dep_0")
            && has_interactable("roster_train_0")
            && has_interactable_match("go", is_strip_button)
            && has_interactable_match("back", is_strip_button))
            return true;

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT waiting for the base-camp roster (%d ms)\n", timeout_ms);
    return false;
}

static bool start_game_from_view_menu(
    int epoch_before,
    int timeout_ms,
    const std::function<bool(const Interactable&)>& is_view_menu_go)
{
    int elapsed = 0;
    int since_last_click = 250;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (g_test_game_epoch.load(std::memory_order_acquire) > epoch_before)
            return true;

        if (since_last_click >= 250 &&
            has_interactable_match("go", is_view_menu_go)) {
            interact_match("go", is_view_menu_go);
            since_last_click = 0;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
        since_last_click += poll_interval;
    }

    return g_test_game_epoch.load(std::memory_order_acquire) > epoch_before;
}

static bool click_intercepted_start_from_view_menu(
    int timeout_ms,
    const std::function<bool(const Interactable&)>& is_view_menu_go)
{
    int elapsed = 0;
    int since_last_click = 250;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (pks().selected_menu_item != nullptr
            && pks().selected_menu_item->command ==
                og::ui::PickerMenuCommand::StartGame) {
            return true;
        }

        if (since_last_click >= 250
            && has_interactable_match("go", is_view_menu_go)) {
            interact_match("go", is_view_menu_go);
            since_last_click = 0;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
        since_last_click += poll_interval;
    }

    return pks().selected_menu_item != nullptr
        && pks().selected_menu_item->command ==
            og::ui::PickerMenuCommand::StartGame;
}

static bool enter_team_menu_from_main_menu(int timeout_ms = 15000)
{
    if (!wait_for_interactable("continue_game", 5000))
        return false;

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    int elapsed = 0;
    while (elapsed < timeout_ms && !has_interactable("hire_troops")) {
        if (has_interactable("continue_game")) {
            fprintf(stderr, "  [test] retry clicking continue_game\n");
            interact("continue_game");
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    return has_interactable("hire_troops");
}

// Test: Continue -> base camp roster (the default view, §2.5) -> Back
//
// Verifies:
//   1. The base camp opens straight onto the roster rows
//   2. The command strip has GO and BACK
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
        unwind_to_main_menu();
        state->finished = true;
        return 0;
    }

    const auto is_strip_back = [](const Interactable& item) { return item.y >= 160; };
    if (wait_for_base_camp_roster(kViewMenuTransitionTimeoutMs)) {
        state->saw_view_menu = true;
        fprintf(stderr, "  [test] clicking back from the base camp\n");
        interact_match("back", is_strip_back);
    }

    // Always try to unwind so picker_main cannot deadlock on failed interactions.
    unwind_to_main_menu();

    state->finished = true;
    return 0;
}

TEST(ViewTeam, view_team) {
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
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
}


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
        unwind_to_main_menu();
        state->finished = true;
        return 0;
    }

    const auto is_view_menu_go = [](const Interactable& item) { return item.y >= 160; };
    if (!wait_for_base_camp_roster(kViewMenuTransitionTimeoutMs)) {
        unwind_to_main_menu();
        state->finished = true;
        return 0;
    }
    state->saw_view_menu = true;
    SDL_Delay(100);

    g_test_remove_exits = true;
    og::sim::g_test_level_tick_limit_override = 15;
    set_game_speed(0.0f);

    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    state->game_started = start_game_from_view_menu(
        epoch_before, kGameStartTimeoutMs, is_view_menu_go);

    int waited_ms = 0;
    const int poll_ms = 50;
    while (g_test_in_game.load(std::memory_order_acquire)
           && waited_ms < kGameFinishTimeoutMs) {
        SDL_Delay(poll_ms);
        waited_ms += poll_ms;
    }
    state->game_finished = !g_test_in_game.load(std::memory_order_acquire);

    set_game_speed(state->original_speed);
    g_test_remove_exits = false;
    og::sim::g_test_level_tick_limit_override = 0;

    unwind_to_main_menu(9000);

    state->finished = true;
    return 0;
}

TEST(ViewTeam, go_starts_level) {
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
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_view_menu) << "should have entered the view team menu";
    ASSERT_TRUE(state.game_started) << "GO from view team should start the game";
    ASSERT_TRUE(state.game_finished) << "started game should return to picker";
}


// §2.5 per-row TRAIN: clicking a roster row's TRAIN button opens the train
// screen seeded ON THAT CHARACTER (no more enter-then-cycle), and backing
// out returns to the base camp with the roster intact.
struct BaseCampRowTrainState {
    bool finished;
    bool saw_train_menu;
    int seeded_slot;
};

static int base_camp_row_train_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BaseCampRowTrainState*>(data);

    if (!wait_for_interactable("roster_train_1", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);  // FadeAroundEntry eats early clicks
    interact("roster_train_1");

    if (!wait_for_interactable("inc_str", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    state->saw_train_menu = true;
    if (pks().train_session != nullptr)
        state->seeded_slot = pks().train_session->current_slot();

    SDL_Delay(300);
    interact("back");  // exit the train screen back to the base camp

    if (!wait_for_interactable("roster_dep_0", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(300);
    interact("back");  // exit the base camp

    state->finished = true;
    return 0;
}

TEST(ViewTeam, base_camp_row_train_opens_seeded_on_that_character)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    soldier->name = "FRONT";
    archer->name = "SECOND";
    og::runtime::current_session->myscreen_->save_data.team_list[0] =
        std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] =
        std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    BaseCampRowTrainState state = { false, false, -1 };
    SDL_Thread* thread = SDL_CreateThread(
        base_camp_row_train_injector, "base_camp_row_train", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    pks().selected_menu_item = nullptr;
    const Sint32 ret = create_team_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_train_menu)
        << "roster TRAIN should open the train screen";
    EXPECT_EQ(1, state.seeded_slot)
        << "row 1's TRAIN must seed the session on team_list slot 1 (§2.5)";
    ASSERT_TRUE(ret & 1) << "base camp BACK should propagate EXIT";
}

struct DirectMenuClickState {
    bool finished;
    bool clicked_target;
    const char* target_id;
    int min_y;
    std::atomic<bool>* menu_done;
};

static int direct_menu_click_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<DirectMenuClickState*>(data);

    int elapsed = 0;
    const int poll_interval = 50;
    int esc_cooldown_ms = 0;
    int click_cooldown_ms = 0;

    while (!state->menu_done->load(std::memory_order_acquire)) {
        if (click_cooldown_ms <= 0) {
            auto interactables = get_interactables();
            for (const auto& item : interactables) {
                if (item.id == state->target_id && !item.hidden && item.y >= state->min_y) {
                    const int game_x = item.x + item.width / 2;
                    const int game_y = item.y + item.height / 2;
                    const int win_x = static_cast<int>(static_cast<float>(game_x)
                        * (og::runtime::current_session->viewport_w_ / 320.0f)
                        + og::runtime::current_session->viewport_offset_x_);
                    const int win_y = static_cast<int>(static_cast<float>(game_y)
                        * (og::runtime::current_session->viewport_h_ / 200.0f)
                        + og::runtime::current_session->viewport_offset_y_);
                    inject_click(win_x, win_y);
                    state->clicked_target = true;
                    click_cooldown_ms = 200;
                    break;
                }
            }
        }

        // Safety valve: if we still haven't found the target button after a few
        // seconds, keep nudging ESC until the menu loop unwinds.
        if (elapsed >= 5000 && esc_cooldown_ms <= 0) {
            inject_key_press(SDLK_ESCAPE, 10);
            esc_cooldown_ms = 200;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
        if (click_cooldown_ms > 0)
            click_cooldown_ms -= poll_interval;
        if (esc_cooldown_ms > 0)
            esc_cooldown_ms -= poll_interval;
    }

    state->finished = true;
    return 0;
}

struct DirectMenuVisibilityState {
    bool finished;
    bool saw_menu;
    bool go_visible;
    bool set_level_visible;
    bool set_campaign_visible;
    bool scenario_visible;
    int min_y;
    std::atomic<bool>* menu_done;
};

static int direct_menu_visibility_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<DirectMenuVisibilityState*>(data);

    const auto in_menu_band = [state](const Interactable& item) {
        return item.y >= state->min_y;
    };

    while (!state->menu_done->load(std::memory_order_acquire)) {
        const auto interactables = get_interactables();
        const auto visible = [&](const char* id) {
            return std::any_of(
                interactables.begin(),
                interactables.end(),
                [id, &in_menu_band](const Interactable& item) {
                    return item.id == id && !item.hidden && in_menu_band(item);
                });
        };

        if (visible("back")) {
            state->saw_menu = true;
            state->go_visible = visible("go");
            state->set_level_visible = visible("set_level");
            state->set_campaign_visible = visible("set_campaign");
            state->scenario_visible = visible("scenario");
            interact_match("back", in_menu_band);
            break;
        }

        SDL_Delay(50);
    }

    state->finished = true;
    return 0;
}

TEST(ViewTeam, create_team_menu_direct_back)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    std::atomic<bool> menu_done{false};
    DirectMenuClickState state = { false, false, "back", 100, &menu_done };
    SDL_Thread* thread = SDL_CreateThread(direct_menu_click_injector, "direct_team_back", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create direct team-menu injector thread";

    const Sint32 ret = create_team_menu(0);
    menu_done.store(true, std::memory_order_release);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "direct team-menu injector should complete";
    ASSERT_TRUE(state.clicked_target) << "direct team-menu injector should click back";
    ASSERT_TRUE(ret & 1) << "create_team_menu(back) should propagate EXIT";
}

TEST(ViewTeam, create_team_menu_remote_start_exits_as_start_game)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    AutoStartPickerLobbyClient client;
    ActivePickerLobbyClientGuard guard(&client);
    g_start_game_requested = false;
    pks().selected_menu_item = nullptr;

    const Sint32 ret = create_team_menu(0);
    const og::ui::PickerMenuItem* const selected = pks().selected_menu_item;
    cleanup_picker_state();

    EXPECT_GT(client.poll_calls, 0);
    ASSERT_TRUE(ret & 1);
    ASSERT_NE(nullptr, selected);
    EXPECT_EQ(og::ui::PickerMenuCommand::StartGame, selected->command);

    g_start_game_requested = false;
}

TEST(ViewTeam, create_team_menu_hides_host_only_controls_for_non_host_client)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    HostVisibilityPickerLobbyClient client(false);
    ActivePickerLobbyClientGuard guard(&client);
    std::atomic<bool> menu_done{false};
    DirectMenuVisibilityState state{
        .finished = false,
        .saw_menu = false,
        .go_visible = false,
        .set_level_visible = false,
        .set_campaign_visible = false,
        .scenario_visible = false,
        .min_y = 100,
        .menu_done = &menu_done,
    };
    SDL_Thread* thread =
        SDL_CreateThread(direct_menu_visibility_injector, "direct_team_visibility", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create team-menu visibility injector thread";

    const Sint32 ret = create_team_menu(0);
    menu_done.store(true, std::memory_order_release);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "team-menu visibility injector should complete";
    ASSERT_TRUE(state.saw_menu) << "team menu should become visible";
    EXPECT_FALSE(state.go_visible) << "GO is host-gated on team build";
    EXPECT_FALSE(state.set_level_visible)
        << "set_level lives in the SCENARIO subscreen now";
    EXPECT_FALSE(state.set_campaign_visible)
        << "set_campaign lives in the SCENARIO subscreen now";
    EXPECT_TRUE(state.scenario_visible)
        << "the SCENARIO entry stays visible for joiners";
    ASSERT_TRUE(ret & 1) << "create_team_menu(back) should propagate EXIT";
}

// The SCENARIO subscreen keeps SET CAMPAIGN / SET LEVEL host-only while
// VIEW LEVEL / TEAMS / PROGRESS stay visible; BACK returns MENU_REDRAW.
TEST(ViewTeam, create_scenario_menu_hides_host_only_controls_for_non_host_client)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    HostVisibilityPickerLobbyClient client(false);
    ActivePickerLobbyClientGuard guard(&client);
    std::atomic<bool> menu_done{false};
    DirectMenuVisibilityState state{
        .finished = false,
        .saw_menu = false,
        .go_visible = false,
        .set_level_visible = false,
        .set_campaign_visible = false,
        .scenario_visible = false,
        .min_y = 0,
        .menu_done = &menu_done,
    };
    SDL_Thread* thread = SDL_CreateThread(
        direct_menu_visibility_injector, "direct_scenario_visibility", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create scenario visibility injector thread";

    const Sint32 ret = create_scenario_menu(0);
    menu_done.store(true, std::memory_order_release);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "scenario visibility injector should complete";
    ASSERT_TRUE(state.saw_menu) << "scenario menu should become visible";
    EXPECT_FALSE(state.set_level_visible)
        << "set_level is host-only inside the subscreen";
    EXPECT_FALSE(state.set_campaign_visible)
        << "set_campaign is host-only inside the subscreen";
    ASSERT_TRUE(ret & 2) << "create_scenario_menu(back) should return REDRAW";
}

// ... and the host keeps them: the same direct screen with host controls on.
TEST(ViewTeam, create_scenario_menu_shows_host_controls_for_host)
{
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;

    HostVisibilityPickerLobbyClient client(true);
    ActivePickerLobbyClientGuard guard(&client);
    std::atomic<bool> menu_done{false};
    DirectMenuVisibilityState state{
        .finished = false,
        .saw_menu = false,
        .go_visible = false,
        .set_level_visible = false,
        .set_campaign_visible = false,
        .scenario_visible = false,
        .min_y = 0,
        .menu_done = &menu_done,
    };
    SDL_Thread* thread = SDL_CreateThread(
        direct_menu_visibility_injector, "direct_scenario_host", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create scenario host injector thread";

    const Sint32 ret = create_scenario_menu(0);
    menu_done.store(true, std::memory_order_release);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "scenario host injector should complete";
    ASSERT_TRUE(state.saw_menu) << "scenario menu should become visible";
    EXPECT_TRUE(state.set_level_visible)
        << "the host keeps SET LEVEL in the subscreen";
    EXPECT_TRUE(state.set_campaign_visible)
        << "the host keeps SET CAMPAIGN in the subscreen";
    ASSERT_TRUE(ret & 2) << "create_scenario_menu(back) should return REDRAW";
}

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
        unwind_to_main_menu();
        state->finished = true;
        return 0;
    }

    const auto is_view_menu_go = [](const Interactable& item) { return item.y >= 160; };
    if (!wait_for_base_camp_roster(7000)) {
        unwind_to_main_menu();
        state->finished = true;
        return 0;
    }
    state->saw_view_menu = true;

    g_test_remove_exits = true;
    og::sim::g_test_level_tick_limit_override = 15;
    set_game_speed(0.0f);
    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    state->game_started =
        start_game_from_view_menu(epoch_before, 10000, is_view_menu_go);

    int waited_ms = 0;
    int stable_polls = 0;
    const int poll_ms = 100;
    while (g_test_in_game.load(std::memory_order_acquire) && waited_ms < 90000) {
        SDL_Delay(poll_ms);
        waited_ms += poll_ms;
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

    unwind_to_main_menu(9000);

    state->finished = true;
    return 0;
}

TEST(ViewTeam, go_level17_no_hang)
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
    ASSERT_TRUE(thread != nullptr) << "failed to create level17 injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should complete";
    ASSERT_TRUE(state.saw_view_menu) << "should enter view team menu";
    ASSERT_TRUE(state.game_started) << "GO should start level 17";
    ASSERT_TRUE(state.frame_progressed) << "level 17 should advance frames";
    ASSERT_TRUE(state.game_finished) << "level 17 should return to picker (no hang)";
}

// ---------------------------------------------------------------------------
// §3.3 positional-index refresh after a WIN: update_guys' fold drops the
// dead and REORDERS the roster (survivors first, held-back appended), so the
// base-camp screen state must re-derive its display rows and a click
// captured against the pre-fold layout must be guarded, never dereference a
// vacated slot. This drives the SCREEN-level state (BaseCampScreenState +
// the spec's on_spec_row dispatch) directly; the pure reorder itself is
// pinned in test_picker_common.
// ---------------------------------------------------------------------------
TEST(ViewTeam, base_camp_win_fold_rederives_rows_and_guards_stale_clicks)
{
    trace_clear();

    // The deploy dispatch below runs the base-camp mutation tail, which
    // lazily creates the local lobby client outside picker_main — pair it
    // with a shutdown or later picker loops read a stale roster cache (the
    // documented promote-wedge class).
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;

    // 13 members so a second page exists (12 rows/page); M03 is held back.
    for (int i = 0; i < 13; ++i) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = std::format("M{:02}", i);
        save.team_list[i] = std::move(member);
    }
    save.team_list[3]->deployed = false;
    save.team_size = 13;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(13u, state.slots.size());
    ASSERT_EQ(2, state.page.page_count());
    ASSERT_TRUE(state.page.step(1)) << "page 2 should be reachable";
    ASSERT_EQ(12, state.page.first_index());

    // The win fold: only one deployed member survived; every other deployed
    // member died. Pass 2 appends the held-back M03 behind the survivor.
    {
        std::list<std::unique_ptr<walker>> oblist;
        auto survivor = std::make_unique<walker>();
        auto survivor_guy = std::make_unique<guy>(FAMILY_SOLDIER);
        survivor_guy->name = "SURVIVOR";
        survivor->set_dead(0);
        survivor->set_owned_myguy(std::move(survivor_guy));
        oblist.push_back(std::move(survivor));
        save.update_guys(oblist);
    }
    ASSERT_EQ(2, static_cast<int>(save.team_size));

    // Stale window: the fold landed this frame; a click captured against
    // the OLD page-2 layout dispatches before the frame tick refreshes.
    // Both the deploy toggle and the per-row TRAIN must no-op (the vacated
    // slot guard), never crash or open a session on a dangling slot.
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    ASSERT_NE(nullptr, host.spec);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_EQ(0, spec.on_spec_row(0, &state))
        << "stale deploy click on a vacated slot must be guarded";
    EXPECT_EQ(0, spec.on_spec_row(kBaseCampTrainBase + 0, &state))
        << "stale TRAIN click on a vacated slot must be guarded";
    EXPECT_FALSE(trace_contains("basecamp", "train slot="))
        << "a stale TRAIN click must not seed a session";

    // §3.3: the refresh re-derives the rows and clamps the page window.
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(2u, state.slots.size());
    EXPECT_EQ(1, state.page.page_count());
    EXPECT_EQ(0, state.page.page);
    EXPECT_TRUE(state.slots[0].owned && state.slots[1].owned)
        << "solo display rows are all owned";
    EXPECT_EQ("SURVIVOR", save.team_list[state.slots[0].save_slot]->name)
        << "pass 1 seats the survivor first";
    EXPECT_EQ("M03", save.team_list[state.slots[1].save_slot]->name)
        << "pass 2 appends the held-back member";
    EXPECT_FALSE(save.team_list[state.slots[1].save_slot]->deployed)
        << "the held-back flag survives the fold";

    // A fresh click maps to the CURRENT occupant: row 1 toggles M03 back on.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(save.team_list[state.slots[1].save_slot]->deployed);
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=1 on"))
        << "the post-refresh toggle should hit the re-derived slot";
}

// ---------------------------------------------------------------------------
// §2.5 MP presentation (stage mp-columns): the merged display list splits
// into own rows (private-save authority, editable) and foreign rows
// (read-only hit zones). Foreign clicks pop OWNED BY <full company name>
// (U9), the client-side 24-cap guard denies a toggle-ON at 24 deployed
// (§4.2 pre-empt), and an accepted own-row toggle runs the §4.3 ready-clear
// + roster-sync mutation tail. Also drives the networked draw pass (READY
// n/m + DEP n/m header, COMPANY column, foreign X/- glyphs).
// ---------------------------------------------------------------------------
namespace {

class NetworkedRosterLobbyClient final : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override { ++roster_syncs; }
    void sync_settings_from_save() override { ++settings_syncs; }
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host;
    }
    bool set_ready(bool ready) override
    {
        ready_calls.push_back(ready);
        // Mimic the production convergence: both SDL network clients block
        // until the echo lands, so local_ready() reflects the send in-call.
        ready_state = ready;
        return true;
    }
    [[nodiscard]] bool local_ready() const noexcept override
    {
        return ready_state;
    }
    bool request_start_game() override
    {
        requested = true;
        return false;
    }
    [[nodiscard]] og::sim::StartDenialReason last_start_denial()
        const noexcept override
    {
        return denial;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
        const override
    {
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        return local_indices;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }

    bool host = false;
    bool ready_state = false;
    bool requested = false;
    og::sim::StartDenialReason denial = og::sim::StartDenialReason::None;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    std::vector<bool> ready_calls;
    int roster_syncs = 0;
    int settings_syncs = 0;
};

og::sim::LobbyPlayer make_foreign_lobby_player(std::uint8_t player_index,
                                               const char* name,
                                               const char* company,
                                               int deployed_slots,
                                               int benched_slots)
{
    og::sim::LobbyPlayer player;
    player.player_index = player_index;
    player.name = name;
    player.company = company;
    int slot_index = 0;
    for (int i = 0; i < deployed_slots + benched_slots; ++i) {
        og::sim::LobbyCharacterSlot slot;
        slot.slot_index = static_cast<std::uint8_t>(slot_index++);
        slot.deployed = i < deployed_slots;
        slot.character.name = std::format("{}-{}", company, i);
        slot.character.family = FAMILY_ELF;
        slot.character.level = 5;
        slot.character.strength = 10;
        slot.character.dexterity = 10;
        slot.character.constitution = 10;
        slot.character.intelligence = 10;
        player.character_slots.push_back(std::move(slot));
    }
    return player;
}

} // namespace

TEST(ViewTeam, base_camp_mp_columns_gate_foreign_rows_and_cap_deploys)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.save_name = "MY OWN BAND";

    auto own_deployed = std::make_unique<guy>(FAMILY_SOLDIER);
    own_deployed->name = "OWN FRONT";
    auto own_benched = std::make_unique<guy>(FAMILY_ARCHER);
    own_benched->name = "OWN RESERVE";
    own_benched->deployed = false;
    save.team_list[0] = std::move(own_deployed);
    save.team_list[1] = std::move(own_benched);
    save.team_size = 2;

    NetworkedRosterLobbyClient lobby;
    lobby.host = false;  // joiner machine: GO hidden by the production rewire
    lobby.local_indices = {7};
    // The local machine's own replicated seat must be SKIPPED (the private
    // save is the authority for own rows); the host machine's slots are the
    // foreign read-only rows.
    lobby.players.push_back(
        make_foreign_lobby_player(3, "net-host", "IRON HOST BAND", 1, 1));
    {
        og::sim::LobbyPlayer self =
            make_foreign_lobby_player(7, "net-me", "MY OWN BAND", 2, 0);
        lobby.players.push_back(std::move(self));
    }
    ActivePickerLobbyClientGuard client_guard(&lobby);

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(4u, state.slots.size())
        << "2 own rows + the host machine's 2 replicated rows";
    EXPECT_TRUE(state.slots[0].owned);
    EXPECT_EQ(0, state.slots[0].save_slot);
    EXPECT_TRUE(state.slots[1].owned);
    EXPECT_EQ(1, state.slots[1].save_slot);
    EXPECT_FALSE(state.slots[2].owned);
    EXPECT_EQ(3, state.slots[2].owner_player_index);
    EXPECT_EQ("IRON HOST BAND", state.slots[2].company)
        << "the COMPANY column reads LobbyPlayer::company off the wire";
    EXPECT_TRUE(state.slots[2].deployed);
    EXPECT_FALSE(state.slots[3].deployed);

    const og::ui::BaseCampDeployCounts counts =
        og::ui::count_base_camp_display_deploys(state.slots, save);
    EXPECT_EQ(2, counts.deployed);
    EXPECT_EQ(4, counts.total);

    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;

    // The production rewire shapes the ownership row: foreign dep buttons
    // widen into no_draw hit zones and their TRAIN buttons hide.
    og::ui::install_base_camp_state_for_screen(&state);
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    int highlighted = 0;
    ASSERT_NE(nullptr, spec.nav.rewire);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[0].no_draw);
    EXPECT_EQ(14, buttons[0].sizex);
    EXPECT_FALSE(buttons[kBaseCampTrainBase + 0].hidden);
    EXPECT_TRUE(buttons[2].no_draw) << "foreign row = no_draw hit zone";
    EXPECT_EQ(212, buttons[2].sizex) << "the §2.5 (8,y,212,10) hit zone";
    EXPECT_TRUE(buttons[kBaseCampTrainBase + 2].hidden)
        << "foreign rows have no TRAIN button";
    EXPECT_TRUE(buttons[kCreateMenuGoIndex].hidden)
        << "joiner machine: GO hidden by the production rewire";

    // Foreign clicks are read-only: OWNED BY <full company name> (U9).
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(2, &state));
    EXPECT_TRUE(trace_contains("popup", "OWNED BY: IRON HOST BAND"))
        << "foreign deploy hit zone names the owning company";
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampTrainBase + 3, &state));
    EXPECT_FALSE(trace_contains("basecamp", "train slot="))
        << "foreign TRAIN must not seed a session";
    EXPECT_TRUE(save.team_list[0]->deployed);
    EXPECT_FALSE(save.team_list[1]->deployed);
    EXPECT_TRUE(lobby.ready_calls.empty())
        << "a denied foreign click must not touch ready";
    EXPECT_EQ(0, lobby.roster_syncs);

    // Client-side 24-cap guard: 23 foreign deploys + 1 own deployed = 24 —
    // toggling the own benched row ON is denied with the popup.
    lobby.players[0] =
        make_foreign_lobby_player(3, "net-host", "IRON HOST BAND", 23, 0);
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(25u, state.slots.size())
        << ">24 display slots replicate (the §4.2 full-roster rule)";
    EXPECT_EQ(3, state.page.page_count())
        << "the page window grows defensively past 2 pages";
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy_cap_denied deployed=24"));
    EXPECT_TRUE(trace_contains("popup", "DEPLOY LIMIT 24"));
    EXPECT_FALSE(save.team_list[1]->deployed)
        << "a denied toggle must not flip the flag";
    EXPECT_TRUE(lobby.ready_calls.empty());

    // Under the cap the toggle lands and runs the §4.3 mutation tail:
    // roster re-sync (server clears ready on the content change) + the
    // optimistic local ready drop.
    lobby.players[0] =
        make_foreign_lobby_player(3, "net-host", "IRON HOST BAND", 22, 1);
    og::ui::base_camp_refresh_rows(state);
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=1 on"));
    EXPECT_TRUE(save.team_list[1]->deployed);
    ASSERT_EQ(1u, lobby.ready_calls.size())
        << "an own-row mutation clears this machine's ready";
    EXPECT_FALSE(lobby.ready_calls[0]);
    EXPECT_EQ(1, lobby.roster_syncs);

    // Networked draw pass: READY/DEP header, COMPANY column, foreign X/-
    // glyphs (smoke + coverage; the strings are unit-pinned in
    // test_picker_common).
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);
    spec.draw_background(&state);
    spec.draw_content(&state);

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// ---------------------------------------------------------------------------
// §2.6 READY twin (stage ready-go-slot): a networked joiner gets READY in
// GO's exact rect; the click drives the shared lobby flag through the
// production ToggleLobbyReady handler with the client deploy gate
// (cross-control OFF + brought-but-benched => popup; spectator machines
// ready freely [NET-R9]; cross-control ON removes the minimum).
// ---------------------------------------------------------------------------
TEST(ViewTeam, base_camp_ready_twin_toggles_and_gates)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.save_name = "JOINER BAND";

    auto own = std::make_unique<guy>(FAMILY_SOLDIER);
    own->name = "OWN FRONT";
    save.team_list[0] = std::move(own);
    save.team_size = 1;

    NetworkedRosterLobbyClient lobby;
    lobby.host = false;
    lobby.local_indices = {7};
    lobby.players.push_back(
        make_foreign_lobby_player(3, "net-host", "IRON HOST BAND", 1, 0));
    lobby.players.push_back(
        make_foreign_lobby_player(7, "net-me", "JOINER BAND", 1, 0));
    ActivePickerLobbyClientGuard client_guard(&lobby);

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    int highlighted = 0;
    spec.nav.rewire(buttons, count, highlighted);

    // The dual-role slot: READY occupies GO's exact rect on a joiner.
    EXPECT_TRUE(buttons[kCreateMenuGoIndex].hidden);
    ASSERT_FALSE(buttons[kCreateMenuReadyIndex].hidden);
    EXPECT_EQ(buttons[kCreateMenuGoIndex].x,
              buttons[kCreateMenuReadyIndex].x);
    EXPECT_EQ(buttons[kCreateMenuGoIndex].sizex,
              buttons[kCreateMenuReadyIndex].sizex);
    EXPECT_EQ("READY", buttons[kCreateMenuReadyIndex].label);
    EXPECT_EQ(kCreateMenuReadyIndex,
              buttons[kCreateMenuNetworkingIndex].nav.right)
        << "the strip chains into the visible half of the pair";
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::ClientUnready, p.state);
        EXPECT_EQ(og::ui::kReadyGoFaceUnready, p.face_color);
    }

    // Deployed roster: the toggle acts directly (production dispatch arg =
    // the twin's own index — the TEAMS mirror label is NOT index-stamped).
    EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    ASSERT_EQ(1u, lobby.ready_calls.size());
    EXPECT_TRUE(lobby.ready_calls[0]);
    EXPECT_TRUE(lobby.ready_state);
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::ClientReady, p.state);
        EXPECT_EQ("UNREADY", p.label);
        EXPECT_EQ(og::ui::kReadyGoFaceGo, p.face_color);
    }
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("UNREADY", buttons[kCreateMenuReadyIndex].label)
        << "the rewire stamps the presentation label on the descriptor";
    EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    ASSERT_EQ(2u, lobby.ready_calls.size());
    EXPECT_FALSE(lobby.ready_calls[1]);

    // Brought-but-benched + cross-control OFF: the ready click popups and
    // sends nothing (§2.6 client gate).
    save.team_list[0]->deployed = false;
    save.cross_control = 0;
    trace_clear();
    EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    EXPECT_TRUE(trace_contains("basecamp", "ready_gated"));
    EXPECT_TRUE(trace_contains("popup", "DEPLOY AT LEAST ONE"));
    EXPECT_EQ(2u, lobby.ready_calls.size()) << "gated click sends nothing";

    // Cross-control ON removes the per-machine minimum.
    save.cross_control = 1;
    EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    ASSERT_EQ(3u, lobby.ready_calls.size());
    EXPECT_TRUE(lobby.ready_calls[2]);
    lobby.ready_state = false;
    save.cross_control = 0;

    // Spectator machine (empty roster): readies freely [NET-R9]; the slot
    // stays visible and reachable on an all-foreign display.
    save.team_list[0].reset();
    save.team_size = 0;
    og::ui::base_camp_refresh_rows(state);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[kCreateMenuReadyIndex].hidden)
        << "[NET-R9]: the spectator machine keeps the READY button";
    trace_clear();
    EXPECT_EQ(MENU_OK, teams_toggle_ready(kCreateMenuReadyIndex));
    ASSERT_EQ(4u, lobby.ready_calls.size());
    EXPECT_TRUE(lobby.ready_calls[3]);
    EXPECT_FALSE(trace_contains("popup", "DEPLOY AT LEAST ONE"));

    // The engine's per-frame bindings (the runner's label/color pass)
    // re-derive the twin from the same presentation — drive the spec-row
    // formatters directly (both label states, both faces).
    const og::ui::MenuButtonSpec& ready_row =
        spec.rows[kCreateMenuReadyIndex];
    ASSERT_NE(nullptr, ready_row.label_binding.formatter);
    ASSERT_NE(nullptr, ready_row.color);
    og::ui::MenuLabelContext binding_context;
    lobby.ready_state = false;
    EXPECT_EQ("READY", ready_row.label_binding.formatter(binding_context));
    EXPECT_EQ(og::ui::kReadyGoFaceUnready, ready_row.color(binding_context));
    lobby.ready_state = true;
    EXPECT_EQ("UNREADY", ready_row.label_binding.formatter(binding_context));
    EXPECT_EQ(og::ui::kReadyGoFaceGo, ready_row.color(binding_context));
    const og::ui::MenuButtonSpec& go_row = spec.rows[kCreateMenuGoIndex];
    ASSERT_NE(nullptr, go_row.color);
    EXPECT_EQ(og::ui::kReadyGoFaceGrey, go_row.color(binding_context))
        << "GO keeps the default face while hidden on a joiner";

    // The TEAMS mirror (origin -1) index-refreshes its own label surfaces;
    // the base-camp twin never touches them.
    (void)picker_teamsmenu_buttons();
    lobby.ready_state = false;
    EXPECT_EQ(MENU_OK, teams_toggle_ready(-1));
    ASSERT_EQ(5u, lobby.ready_calls.size());
    EXPECT_TRUE(lobby.ready_state);
    EXPECT_EQ("UNREADY",
              og::runtime::current_session->picker_
                  ->teamsmenu_buttons[kTeamsMenuReadyIndex].label)
        << "the TEAMS mirror refreshes its descriptor label";

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// ---------------------------------------------------------------------------
// §2.6 host GO gating (states 3-4): a networked host's GO pre-checks the
// lobby BEFORE sending — machines-not-ready popups WAITING FOR: <companies>
// (rule 3 outranks rule 4), an all-benched lobby popups NO ONE IS DEPLOYED,
// and only a fully-gated GO sends the start request. A denial that arrives
// asynchronously (the [NET-R4] echo) still renders its reason, and the solo
// deploy popup carries the §2.6 state-2 wording.
// ---------------------------------------------------------------------------
TEST(ViewTeam, base_camp_go_host_gate_popups_and_denial_display)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.save_name = "HOST BAND";

    auto own = std::make_unique<guy>(FAMILY_SOLDIER);
    own->name = "HOST FRONT";
    save.team_list[0] = std::move(own);
    save.team_size = 1;

    NetworkedRosterLobbyClient lobby;
    lobby.host = true;
    lobby.local_indices = {0};
    {
        og::sim::LobbyPlayer self =
            make_foreign_lobby_player(0, "net-host", "HOST BAND", 1, 0);
        self.is_host = true;
        lobby.players.push_back(std::move(self));
    }
    lobby.players.push_back(
        make_foreign_lobby_player(3, "net-join", "IRON JOIN BAND", 1, 0));
    ActivePickerLobbyClientGuard client_guard(&lobby);

    const og::ui::MenuScreenSpec& tb_spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    const og::ui::MenuButtonSpec& go_row = tb_spec.rows[kCreateMenuGoIndex];
    ASSERT_NE(nullptr, go_row.color);
    og::ui::MenuLabelContext binding_context;

    // State 3, rule 3: the joiner machine is unready — popup names it, no
    // request is sent. The GO color binding stamps the yellow face.
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::HostGated, p.state);
        EXPECT_EQ(og::ui::kReadyGoFaceGated, p.face_color);
        EXPECT_EQ(og::ui::kReadyGoFaceGated, go_row.color(binding_context));
    }
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains("basecamp", "go_gated ready=0/1"));
    EXPECT_TRUE(trace_contains("popup", "WAITING FOR:"));
    EXPECT_TRUE(trace_contains("popup", "IRON JOIN BAND"))
        << "the blocker popup names the unready company";
    EXPECT_FALSE(lobby.requested) << "state 3 sends NOTHING";

    // Rule 4 after rule 3 clears: everyone ready but every slot benched.
    lobby.players[1].ready = true;
    lobby.players[0].character_slots[0].deployed = false;
    lobby.players[1].character_slots[0].deployed = false;
    save.team_list[0]->deployed = false;
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains("popup", "NO ONE IS DEPLOYED"));
    EXPECT_FALSE(lobby.requested);
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::HostGated, p.state);
    }

    // Gates met => the request goes out (state 4, green face).
    lobby.players[0].character_slots[0].deployed = true;
    lobby.players[1].character_slots[0].deployed = true;
    save.team_list[0]->deployed = true;
    {
        const og::ui::ReadyGoPresentation p =
            picker_compute_ready_go_presentation();
        EXPECT_EQ(og::ui::ReadyGoState::HostGo, p.state);
        EXPECT_EQ(og::ui::kReadyGoFaceGo, p.face_color);
        EXPECT_EQ(og::ui::kReadyGoFaceGo, go_row.color(binding_context))
            << "the color binding stamps the green face on the last ready";
    }
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(lobby.requested);
    EXPECT_FALSE(trace_contains("popup", "WAITING FOR:"));

    // An async server denial that beat the pre-check still renders its
    // reason from the cached echo (best-effort, [NET-R4] client mirror).
    lobby.requested = false;
    lobby.denial = og::sim::StartDenialReason::MachinesNotReady;
    lobby.players[1].ready = false;
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains("popup", "WAITING FOR:"))
        << "the pre-check catches it, or the denial display does — either "
           "way the reason shows";

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// ---------------------------------------------------------------------------
// §2.7 cross-control (TEAMS subscreen): visible to ALL peers when networked
// in the guy-row slot; host-only actionable (non-host click popups HOST
// CONTROLS THIS SETTING); a host toggle sanitizes to {0,1}, TRACEs, and
// syncs settings (the server clears every non-host machine's ready — §4.5).
// ---------------------------------------------------------------------------
TEST(ViewTeam, teams_cross_control_toggle_host_gates_and_syncs)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.cross_control = 0;

    NetworkedRosterLobbyClient lobby;
    lobby.host = false;
    ActivePickerLobbyClientGuard client_guard(&lobby);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).spec;
    ASSERT_NE(nullptr, spec.on_spec_row)
        << "§2.7: cross-control is the TEAMS screen's one MenuSpecRow";

    // Visible to every networked peer (the guy row is local-only, so the
    // §2.7 slot is free), label from the shared formatter.
    button* buttons = picker_teamsmenu_buttons();
    const int count = picker_teamsmenu_button_count();
    int highlighted = 0;
    ASSERT_NE(nullptr, spec.nav.rewire);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[kTeamsMenuCrossControlIndex].hidden)
        << "§2.7: joiners must SEE the mode that changes their rights";
    EXPECT_TRUE(buttons[kTeamsMenuGuyTeamIndex].hidden)
        << "the same-rect guy row is local-only";
    EXPECT_EQ("CTRL: OWN", buttons[kTeamsMenuCrossControlIndex].label);

    // Non-host click: popup, no change, no settings sync.
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kTeamsMenuCrossControlIndex, nullptr));
    EXPECT_TRUE(trace_contains("teams", "cross_control_denied"));
    EXPECT_TRUE(trace_contains("popup", "HOST CONTROLS THIS SETTING"));
    EXPECT_EQ(0, save.cross_control);
    EXPECT_EQ(0, lobby.settings_syncs);

    // Host click: toggles, TRACEs, and syncs settings (server-side that
    // clears every non-host machine's ready — pinned e2e in
    // test_picker_network_client).
    lobby.host = true;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kTeamsMenuCrossControlIndex, nullptr));
    EXPECT_TRUE(trace_contains("teams", "cross_control 1"));
    EXPECT_EQ(1, save.cross_control);
    EXPECT_EQ(1, lobby.settings_syncs);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("CTRL: ALL", buttons[kTeamsMenuCrossControlIndex].label);

    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kTeamsMenuCrossControlIndex, nullptr));
    EXPECT_EQ(0, save.cross_control);

    // Sanitization: junk counts as ON and lands on exactly 0.
    save.cross_control = 7;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kTeamsMenuCrossControlIndex, nullptr));
    EXPECT_EQ(0, save.cross_control);

    // Other rows are not this screen's spec rows.
    EXPECT_EQ(0, spec.on_spec_row(kTeamsMenuBackIndex, nullptr));

    save.cross_control = 0;
    save.reset();
}

// §2.6 state 2 wording (solo reword pin): a solo hired-but-benched roster
// gets the DEPLOY AT LEAST ONE popup (was NO ONE DEPLOYED).
TEST(ViewTeam, solo_go_benched_roster_popups_deploy_at_least_one)
{
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;

    auto own = std::make_unique<guy>(FAMILY_SOLDIER);
    own->name = "BENCHED";
    own->deployed = false;
    save.team_list[0] = std::move(own);
    save.team_size = 1;

    trace_clear();
    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(trace_contains("popup", "DEPLOY AT LEAST ONE"))
        << "§2.6 state 2 wording";
    save.reset();
}

// ---------------------------------------------------------------------------
// §2.5 flow 4 + rename: the train screen's RENAME button, reached through a
// per-row TRAIN seed, renames THAT character (editguy_ follows the seeded
// slot) and the rename accept autosaves the company (§3.8).
// ---------------------------------------------------------------------------
struct BaseCampRenameState {
    bool finished;
    bool saw_train_menu;
    bool saw_rename_button;
};

static int base_camp_train_rename_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BaseCampRenameState*>(data);

    if (!wait_for_interactable("roster_train_1", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);  // FadeAroundEntry eats early clicks
    interact("roster_train_1");

    if (!wait_for_interactable("rename", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    state->saw_train_menu = true;
    state->saw_rename_button = true;
    SDL_Delay(300);
    interact("rename");

    // name_guy(1)'s modal is live now: the first text event replaces the
    // prefilled name, RETURN accepts it.
    SDL_Delay(400);
    inject_text_input("ZEDNAME");
    SDL_Delay(150);
    inject_key_press(SDLK_RETURN, 10);

    if (!wait_for_interactable("inc_str", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(300);
    interact("back");  // exit the train screen back to the base camp

    if (!wait_for_interactable("roster_dep_0", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(300);
    interact("back");  // exit the base camp

    state->finished = true;
    return 0;
}

TEST(ViewTeam, base_camp_row_train_rename_renames_seeded_character)
{
    trace_clear();

    // The rename accept runs the base-camp mutation tail (lazy local lobby
    // client) — shut it down on every exit path.
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    auto archer = std::make_unique<guy>(FAMILY_ARCHER);
    soldier->name = "FRONT";
    archer->name = "SECOND";
    og::runtime::current_session->myscreen_->save_data.team_list[0] =
        std::move(soldier);
    og::runtime::current_session->myscreen_->save_data.team_list[1] =
        std::move(archer);
    og::runtime::current_session->myscreen_->save_data.team_size = 2;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    BaseCampRenameState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(
        base_camp_train_rename_injector, "base_camp_rename", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    pks().selected_menu_item = nullptr;
    const Sint32 ret = create_team_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_train_menu)
        << "roster TRAIN should open the train screen";
    ASSERT_TRUE(ret & 1) << "base camp BACK should propagate EXIT";

    // The SEEDED character was renamed; its neighbor was not.
    EXPECT_EQ("ZEDNAME",
              og::runtime::current_session->myscreen_->save_data
                  .team_list[1]->name)
        << "RENAME from a seeded train screen must target the seeded slot";
    EXPECT_EQ("FRONT",
              og::runtime::current_session->myscreen_->save_data
                  .team_list[0]->name)
        << "slot 0 must be untouched by the seeded rename";

    // §3.8: the rename accept AUTOSAVED — the new name is on disk without
    // any manual save.
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size));
    ASSERT_TRUE(reloaded.team_list[1] != nullptr);
    EXPECT_EQ("ZEDNAME", reloaded.team_list[1]->name)
        << "the rename mutation must persist via the company autosave";
}
