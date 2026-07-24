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
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/util.h>
#include <atomic>
#include <cstdint>
#include <format>
#include <functional>
#include <list>
#include <string_view>
#include <utility>

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
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
extern std::atomic<int> g_test_game_frame_ticks;
namespace og::sim { extern std::int32_t g_test_level_tick_limit_override; }
namespace og::ui {
void picker_testing_draw_menu_highlight(const MenuScreenSpec& spec,
                                        const button* buttons,
                                        int highlighted_button);
}
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
            // UI-canvas-pinned map — raw viewport_*/320 math ignores the
            // aspect-fit letterbox and mismaps in non-16:10 windows (the
            // seed-23 og_test_menu_ui wedge class; see test_interact.h).
            const auto [mapped_x, mapped_y] =
                ui_canvas_to_window(static_cast<float>(game_x),
                                    static_cast<float>(game_y));
            const int win_x = static_cast<int>(mapped_x);
            const int win_y = static_cast<int>(mapped_y);
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
            // UI-canvas-pinned map (see interact_match above).
            const auto [mapped_x, mapped_y] =
                ui_canvas_to_window(static_cast<float>(game_x),
                                    static_cast<float>(game_y));
            inject_click(static_cast<int>(mapped_x),
                         static_cast<int>(mapped_y));
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
            && has_interactable("roster_row_0")
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


// §2.5 flow 4 via the §9.11 row-body affordance: tapping a roster row's
// visible NAME (inside the name/class/level body — the TRAIN column is
// deleted) opens the train screen seeded ON THAT CHARACTER (no more
// enter-then-cycle), and
// backing out returns to the base camp with the roster intact.
struct BaseCampRowTrainState {
    bool finished;
    bool saw_train_menu;
    int seeded_slot;
};

static int base_camp_row_train_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BaseCampRowTrainState*>(data);

    if (!wait_for_interactable("roster_row_1", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);  // FadeAroundEntry eats early clicks
    // Tap the rendered name itself, not the center of the wide row hit zone.
    // Round 6 places NAME at x=88; row 1 remains y=59..68.
    const auto [mapped_x, mapped_y] =
        ui_canvas_to_window(90.0f, 64.0f);
    inject_click(static_cast<int>(mapped_x), static_cast<int>(mapped_y), 100);

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

TEST(ViewTeam, base_camp_name_tap_opens_train_seeded_on_that_character)
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
        << "tapping a roster name should open the train screen";
    EXPECT_EQ(1, state.seeded_slot)
        << "row 1's body must seed the session on team_list slot 1 (§9.11)";
    ASSERT_TRUE(ret & 1) << "base camp BACK should propagate EXIT";
}

// The visible TEAM-color square is an independent column before NAME.
// Coordinate-level coverage keeps it from regressing into the train zone.
struct BaseCampTeamChipTapState {
    bool finished;
    bool saw_team_change;
};

static int base_camp_team_chip_tap_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BaseCampTeamChipTapState*>(data);

    if (!wait_for_interactable("roster_team_0", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);
    const auto [mapped_x, mapped_y] =
        ui_canvas_to_window(66.0f, 49.0f);
    inject_click(static_cast<int>(mapped_x), static_cast<int>(mapped_y), 100);
    SDL_Delay(500);

    const SaveData& save = og::runtime::current_session->myscreen_->save_data;
    state->saw_team_change = save.team_list[0] != nullptr &&
        save.team_list[0]->teamnum == 1;
    interact("back");
    state->finished = true;
    return 0;
}

TEST(ViewTeam, base_camp_color_tap_cycles_team_without_opening_train)
{
    trace_clear();
    og::data::set_active_company_slot("save0");

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->name = "COLOR TAP";
    soldier->teamnum = 0;
    save.team_list[0] = std::move(soldier);
    save.team_size = 1;
    save.save("save0");

    BaseCampTeamChipTapState state = {false, false};
    SDL_Thread* thread = SDL_CreateThread(
        base_camp_team_chip_tap_injector, "base_camp_team_chip", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    pks().selected_menu_item = nullptr;
    const Sint32 ret = create_team_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "team-chip injector should complete";
    ASSERT_TRUE(state.saw_team_change)
        << "tapping the team color should advance that character's team";
    EXPECT_TRUE(trace_contains("basecamp", "team slot=0 team=1"));
    EXPECT_FALSE(trace_contains("basecamp", "train slot="));
    ASSERT_TRUE(ret & 1) << "base camp BACK should propagate EXIT";
}

// §9.14: the rendered solo SCEN status line is itself a click target for
// the Scenario menu. Use an ink coordinate rather than interact(id), so the
// test pins the player-facing affordance and not merely its hidden geometry.
struct BaseCampScenarioLineState {
    bool finished;
    bool saw_scenario_menu;
};

static int base_camp_scenario_line_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<BaseCampScenarioLineState*>(data);

    if (!wait_for_interactable("scenario_line", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);  // FadeAroundEntry eats early clicks
    const auto [mapped_x, mapped_y] =
        ui_canvas_to_window(30.0f, 19.0f);
    inject_click(static_cast<int>(mapped_x), static_cast<int>(mapped_y), 100);

    if (!wait_for_interactable("set_level", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    state->saw_scenario_menu = true;
    SDL_Delay(300);
    interact("back");  // Scenario menu -> Base Camp

    if (!wait_for_interactable("scenario_line", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(300);
    interact("back");  // Base Camp -> main menu caller
    state->finished = true;
    return 0;
}

TEST(ViewTeam, base_camp_scenario_line_tap_opens_scenario_menu)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->name = "SCOUT";
    save.team_list[0] = std::move(soldier);
    save.team_size = 1;

    BaseCampScenarioLineState state = {false, false};
    SDL_Thread* thread = SDL_CreateThread(
        base_camp_scenario_line_injector, "base_camp_scenario_line", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    pks().selected_menu_item = nullptr;
    const Sint32 ret = create_team_menu(0);

    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    cleanup_picker_state();

    ASSERT_TRUE(state.finished) << "scenario-line injector should complete";
    ASSERT_TRUE(state.saw_scenario_menu)
        << "tapping the visible SCEN line should open the Scenario menu";
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
                    // UI-canvas-pinned map (see interact_match above).
                    const auto [mapped_x, mapped_y] =
                        ui_canvas_to_window(static_cast<float>(game_x),
                                            static_cast<float>(game_y));
                    const int win_x = static_cast<int>(mapped_x);
                    const int win_y = static_cast<int>(mapped_y);
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
// VIEW LEVEL / MATCHUP / PROGRESS stay visible; BACK returns MENU_REDRAW.
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

    // 13 members so a second page exists (8 rows/page, §9.14); M03 is
    // held back.
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
    ASSERT_EQ(8, state.page.first_index());

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
    // Both the deploy toggle and the §9.11 row-body train zone must no-op
    // (the vacated slot guard), never crash or open a session on a
    // dangling slot.
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    ASSERT_NE(nullptr, host.spec);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    ASSERT_NE(nullptr, spec.on_spec_row);
    EXPECT_EQ(0, spec.on_spec_row(0, &state))
        << "stale deploy click on a vacated slot must be guarded";
    EXPECT_EQ(0, spec.on_spec_row(kBaseCampRowBodyBase + 0, &state))
        << "stale row-body click on a vacated slot must be guarded";
    EXPECT_FALSE(trace_contains("basecamp", "train slot="))
        << "a stale row-body click must not seed a session";

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
// §2.0 U6: roster-row tap debounce. A second ACCEPTED deploy toggle of the
// SAME display row within 250 ms is silently ignored (a touch mistap
// double-toggle would be a spurious §4.3 MP ready-clear); a different row
// is never debounced, and the window expires (the test rewinds the public
// stamp instead of sleeping).
// ---------------------------------------------------------------------------
TEST(ViewTeam, base_camp_deploy_toggle_debounces_same_row_taps)
{
    trace_clear();
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    for (int i = 0; i < 2; ++i) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = std::format("TAP{:02}", i);
        save.team_list[i] = std::move(member);
    }
    save.team_size = 2;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(2u, state.slots.size());

    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    ASSERT_NE(nullptr, spec.on_spec_row);

    // First tap toggles row 1 off and stamps the debounce state.
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=1 off"));
    EXPECT_FALSE(save.team_list[1]->deployed);

    // Immediate second tap of the SAME row: silently ignored (U6).
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy_debounced slot=1"))
        << "the mistap must be traced as debounced";
    EXPECT_FALSE(trace_contains("basecamp", "deploy slot=1"))
        << "the mistap must not re-toggle";
    EXPECT_FALSE(save.team_list[1]->deployed)
        << "a double-tap within 250 ms must not flip the flag back";

    // Rewinding the stamp past the window re-arms the same row.
    state.last_deploy_toggle_ms -= 251;
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(1, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=1 on"));
    EXPECT_TRUE(save.team_list[1]->deployed);

    // A DIFFERENT row right afterwards is never debounced.
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(0, &state));
    EXPECT_TRUE(trace_contains("basecamp", "deploy slot=0 off"));
    EXPECT_FALSE(save.team_list[0]->deployed);

    save.reset();
}

// ---------------------------------------------------------------------------
// §3.8 hook inventory row "team cycle": the former TEAMS per-character cycler
// mutates the persisted roster (teamnum), so it runs the shared mutation
// tail — the new team round-trips from the active company file with no
// manual save. Solo-only surface (networked lobbies hide the button).
// ---------------------------------------------------------------------------
TEST(ViewTeam, teams_cycle_guy_team_autosaves_solo_team_change)
{
    trace_clear();
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;
    og::data::set_active_company_slot("save0");

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "CYCLER";
    member->teamnum = 0;
    save.team_list[0] = std::move(member);
    save.team_size = 1;
    pks().teams_menu_guy_slot = 0;

    ASSERT_EQ(MENU_OK, teams_cycle_guy_team(1));
    ASSERT_TRUE(save.team_list[0] != nullptr);
    EXPECT_EQ(1, (int)save.team_list[0]->teamnum);

    // §3.8: the cycle AUTOSAVED — the team change must be on disk without
    // any manual save.
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"))
        << "the team-cycle autosave must have written the active slot";
    ASSERT_TRUE(reloaded.team_list[0] != nullptr);
    EXPECT_EQ(1, (int)reloaded.team_list[0]->teamnum)
        << "the cycled team must persist via the mutation autosave";

    save.reset();
}

// The Base Camp exposes that same mutation directly on each rendered team
// color chip. Its MenuSpecRow ordinal must target the visible character,
// cycle the team, and run the shared autosave tail without opening TRAIN.
TEST(ViewTeam, base_camp_team_chip_cycles_and_autosaves_solo_team_change)
{
    trace_clear();
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;
    og::data::set_active_company_slot("save0");

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    auto member = std::make_unique<guy>(FAMILY_SOLDIER);
    member->name = "CHIPPER";
    member->teamnum = 0;
    save.team_list[0] = std::move(member);
    save.team_size = 1;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;

    ASSERT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampTeamChipBase, &state));
    ASSERT_TRUE(save.team_list[0] != nullptr);
    EXPECT_EQ(1, static_cast<int>(save.team_list[0]->teamnum));
    EXPECT_TRUE(trace_contains("basecamp", "team slot=0 team=1"));
    EXPECT_FALSE(trace_contains("basecamp", "train slot="));

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_TRUE(reloaded.team_list[0] != nullptr);
    EXPECT_EQ(1, static_cast<int>(reloaded.team_list[0]->teamnum));

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// A team click must mutate only the selected guy's teamnum. The local lobby
// echo addresses characters by their private save slot; compacting the one
// newly active team ahead of the preserved slots used to rotate the roster
// when a middle row changed teams.
TEST(ViewTeam, base_camp_team_chip_keeps_private_roster_order)
{
    trace_clear();
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;
    picker_lobby_shutdown();
    og::data::set_active_company_slot("save0");

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    constexpr std::array<std::string_view, 4> names = {
        "ALPHA", "BRAVO", "CHARLIE", "DELTA"};
    for (int slot = 0; slot < static_cast<int>(names.size()); ++slot) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = names[static_cast<std::size_t>(slot)];
        member->teamnum = 0;
        save.team_list[slot] = std::move(member);
    }
    save.team_size = static_cast<unsigned char>(names.size());

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;

    ASSERT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampTeamChipBase + 2, &state));
    for (int slot = 0; slot < static_cast<int>(names.size()); ++slot) {
        ASSERT_NE(nullptr, save.team_list[slot]) << "slot " << slot;
        EXPECT_EQ(names[static_cast<std::size_t>(slot)],
                  save.team_list[slot]->name)
            << "a team click must not reorder private save slots";
        EXPECT_EQ(slot == 2 ? 1 : 0,
                  static_cast<int>(save.team_list[slot]->teamnum));
    }

    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(names.size(), state.slots.size());
    for (int row = 0; row < static_cast<int>(names.size()); ++row)
        EXPECT_EQ(row, state.slots[static_cast<std::size_t>(row)].save_slot);

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    for (int slot = 0; slot < static_cast<int>(names.size()); ++slot) {
        ASSERT_NE(nullptr, reloaded.team_list[slot]);
        EXPECT_EQ(names[static_cast<std::size_t>(slot)],
                  reloaded.team_list[slot]->name);
    }

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// Team chips retain the four gameplay ramps and overlay their one-based team
// number. Pin every glyph pixel so 1-4 cannot disappear into the colored face
// or regress to the shaded font treatment used elsewhere.
TEST(ViewTeam, base_camp_team_chips_draw_one_based_labels_for_all_teams)
{
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;
    picker_lobby_shutdown();

    screen* const output = og::runtime::current_session->myscreen_;
    SaveData& save = output->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";
    for (int team = 0; team < static_cast<int>(SCORE_TEAM_COUNT); ++team) {
        auto member = std::make_unique<guy>(FAMILY_SOLDIER);
        member->name = std::format("TEAM{}", team + 1);
        member->teamnum = static_cast<short>(team);
        save.team_list[team] = std::move(member);
    }
    save.team_size = SCORE_TEAM_COUNT;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);
    spec.draw_background(&state);
    spec.draw_content(&state);

    text& font = output->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    constexpr std::array<int, SCORE_TEAM_COUNT> label_x = {65, 64, 64, 64};
    constexpr int first_label_y = 48;
    constexpr int row_pitch = 14;
    for (int team = 0; team < static_cast<int>(SCORE_TEAM_COUNT); ++team) {
        const unsigned char label = static_cast<unsigned char>('1' + team);
        const unsigned char* const glyph =
            font.letters->data.get() + static_cast<std::size_t>(label) * stride;
        const int team_color = team * 16 + 40;
        for (Sint32 row = 0; row < font.sizey; ++row) {
            for (Sint32 col = 0; col < font.sizex; ++col) {
                int actual = -1;
                output->get_pixel(label_x[static_cast<std::size_t>(team)] + col,
                                  first_label_y + row_pitch * team + row,
                                  &actual);
                const unsigned char source =
                    glyph[static_cast<std::size_t>(row * font.sizex + col)];
                EXPECT_EQ(source == 0 ? team_color : PURE_BLACK, actual)
                    << "team " << team + 1 << " chip pixel " << col << ","
                    << row;
            }
        }
    }

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

// The seat rail is dense enough that the normal exterior keyboard-focus pulse
// can reach a neighboring card or pager, while an unconstrained interior pulse
// can cross the numbered chip at the selected card's right edge. Render the
// real Base Camp spec and sample a full pulse interval: every rail pixel
// outside card 2's chip-free label face must remain byte-identical.
TEST(ViewTeam, base_camp_seat_card_focus_preserves_neighbors_and_team_chip)
{
    struct ScreenStateGuard {
        ~ScreenStateGuard()
        {
            og::ui::install_base_camp_state_for_screen(nullptr);
            clear_allbuttons();
            pks().menu_nav_enabled = false;
            og::runtime::current_session->myscreen_->save_data.reset();
        }
    } guard;

    screen* const output = og::runtime::current_session->myscreen_;
    SaveData& save = output->save_data;
    save.reset();
    save.save_name = "IRON KETTLE BAND";
    save.current_campaign = "org.openglad.gladiator";

    og::ui::BaseCampScreenState state;
    state.page = og::ui::PageModel::make(0, kBaseCampRosterRowsPerPage);
    for (int index = 0; index < 5; ++index) {
        state.seats.push_back(og::sim::LobbyPlayer{
            .player_index = static_cast<std::uint8_t>(index),
            .name = std::format("net-{}", index),
            .company = index < 2 ? "IRON KETTLE BAND" : "RED LANTERN CREW",
            .team = static_cast<short>(index % SCORE_TEAM_COUNT),
            .character_slots = {},
            .ready = false,
            .is_host = index == 0,
        });
    }
    state.local_seat_indices = {0, 1};
    state.seat_page =
        og::ui::PageModel::make(5, kBaseCampSeatCardsPerPage);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);

    button* const buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    init_buttons(buttons, count);
    int highlighted = kBaseCampSeatCardBase + 1;
    spec.nav.rewire(buttons, count, highlighted);
    ASSERT_EQ(kBaseCampSeatCardBase + 1, highlighted);
    ASSERT_FALSE(buttons[kBaseCampSeatPagePrevIndex].hidden);
    ASSERT_FALSE(buttons[kBaseCampSeatPageNextIndex].hidden);

    constexpr int kCanvasWidth = 320;
    constexpr int kCanvasHeight = 200;
    constexpr int kRailLeft = 42;
    constexpr int kRailTop = 162;
    constexpr int kRailRight = 313;
    constexpr int kRailBottom = 176;
    constexpr int kRailWidth = kRailRight - kRailLeft + 1;
    constexpr int kRailHeight = kRailBottom - kRailTop + 1;
    constexpr int kSelectedCardX = 112;
    constexpr int kSelectedCardY = 164;
    constexpr int kLabelFocusRight = kSelectedCardX + 47;
    constexpr int kFocusBottom = kSelectedCardY + 10;
    constexpr int kChipX = kSelectedCardX + 48;
    constexpr int kChipY = kSelectedCardY + 1;
    constexpr int kChipSize = 8;

    const auto capture = [&]() {
        std::array<Uint32, kRailWidth * kRailHeight> frame{};
        for (int y = kRailTop; y <= kRailBottom; ++y) {
            for (int x = kRailLeft; x <= kRailRight; ++x) {
                Uint8 red = 0;
                Uint8 green = 0;
                Uint8 blue = 0;
                output->get_pixel(x, y, &red, &green, &blue);
                frame[static_cast<std::size_t>(
                    (y - kRailTop) * kRailWidth + x - kRailLeft)] =
                    (static_cast<Uint32>(red) << 16) |
                    (static_cast<Uint32>(green) << 8) |
                    static_cast<Uint32>(blue);
            }
        }
        return frame;
    };
    const auto render_without_focus = [&]() {
        output->fastbox(0, 0, kCanvasWidth, kCanvasHeight, PURE_BLACK);
        spec.draw_background(&state);
        draw_buttons(buttons, count);
        spec.draw_content(&state);
    };

    render_without_focus();
    const auto baseline = capture();
    bool focus_was_visible = false;
    for (int sample = 0; sample < 100; ++sample) {
        render_without_focus();
        pks().menu_nav_enabled = true;
        og::ui::picker_testing_draw_menu_highlight(
            spec, buttons, highlighted);
        const auto focused = capture();

        for (int y = kRailTop; y <= kRailBottom; ++y) {
            for (int x = kRailLeft; x <= kRailRight; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(
                        (y - kRailTop) * kRailWidth + x - kRailLeft);
                const bool in_label_focus =
                    x >= kSelectedCardX && x <= kLabelFocusRight &&
                    y >= kSelectedCardY && y <= kFocusBottom;
                if (in_label_focus) {
                    focus_was_visible =
                        focus_was_visible ||
                        focused[offset] != baseline[offset];
                    continue;
                }
                ASSERT_EQ(baseline[offset], focused[offset])
                    << "focus escaped the seat label at (" << x << "," << y
                    << ")";
            }
        }

        for (int y = kChipY; y < kChipY + kChipSize; ++y) {
            for (int x = kChipX; x < kChipX + kChipSize; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(
                        (y - kRailTop) * kRailWidth + x - kRailLeft);
                ASSERT_EQ(baseline[offset], focused[offset])
                    << "focus overwrote team chip pixel (" << x << "," << y
                    << ")";
            }
        }
        SDL_Delay(10);
    }
    EXPECT_TRUE(focus_was_visible)
        << "test must observe a visible focus pulse inside the seat label";

}

// Identity text stays readable while the old View Team palette survives as
// a real eight-shade family ramp immediately after the class label.
TEST(ViewTeam, base_camp_draws_crisp_identity_text_and_family_ramp_swatches)
{
    struct LobbyShutdownGuard {
        ~LobbyShutdownGuard() { picker_lobby_shutdown(); }
    } lobby_guard;
    picker_lobby_shutdown();

    screen* const output = og::runtime::current_session->myscreen_;
    SaveData& save = output->save_data;
    save.reset();
    save.current_campaign = "org.openglad.gladiator";

    auto deployed = std::make_unique<guy>(FAMILY_ELF);
    deployed->name = "ELENA";
    deployed->deployed = true;
    save.team_list[0] = std::move(deployed);
    auto benched = std::make_unique<guy>(FAMILY_MAGE);
    benched->name = "MIRA";
    benched->deployed = false;
    save.team_list[1] = std::move(benched);
    save.team_size = 2;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);
    spec.draw_background(&state);
    spec.draw_content(&state);

    text& font = output->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    const auto expect_flat_glyph = [&](char label, int x, int y,
                                       int expected_color) {
        const unsigned char* const glyph =
            font.letters->data.get() +
            static_cast<std::size_t>(static_cast<unsigned char>(label)) *
                stride;
        int opaque_pixels = 0;
        for (Sint32 row = 0; row < font.sizey; ++row) {
            for (Sint32 col = 0; col < font.sizex; ++col) {
                const unsigned char source =
                    glyph[static_cast<std::size_t>(row * font.sizex + col)];
                if (source == 0)
                    continue;
                ++opaque_pixels;
                int actual = -1;
                output->get_pixel(x + col, y + row, &actual);
                EXPECT_EQ(expected_color, actual)
                    << label << " pixel " << col << "," << row;
            }
        }
        EXPECT_GT(opaque_pixels, 0);
    };

    constexpr int name_x = 88;
    constexpr int class_x = 164;
    constexpr int first_text_y = 47;
    constexpr int row_pitch = 14;
    expect_flat_glyph('E', name_x, first_text_y, PURE_BLACK);
    expect_flat_glyph('E', class_x, first_text_y, PURE_BLACK);
    expect_flat_glyph('M', name_x, first_text_y + row_pitch, 21);
    expect_flat_glyph('M', class_x, first_text_y + row_pitch, 21);

    const auto expect_swatch = [&](std::string_view cls, int row_y,
                                   int ramp_start) {
        const int swatch_x = class_x + font.query_width(cls) + 1;
        int actual = -1;
        output->get_pixel(swatch_x, row_y + 1, &actual);
        EXPECT_EQ(PURE_BLACK, actual) << "swatch border";
        for (int shade = 0; shade < 8; ++shade) {
            output->get_pixel(swatch_x + 1 + shade, row_y + 2, &actual);
            EXPECT_EQ(ramp_start + shade, actual) << "shade " << shade;
        }
        output->get_pixel(swatch_x + 9, row_y + 8, &actual);
        EXPECT_EQ(PURE_BLACK, actual) << "swatch lower-right border";
    };
    expect_swatch("ELF", 45, 32);
    expect_swatch("MAGE", 45 + row_pitch, 64);

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
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
    bool add_local_seat() override
    {
        ++add_seat_calls;
        if (!add_seat_accept)
            return false;
        if (active_local_count.has_value())
            ++*active_local_count;
        if (seat_to_publish_on_add.has_value()) {
            local_indices.push_back(
                seat_to_publish_on_add->player_index);
            players.push_back(std::move(*seat_to_publish_on_add));
            seat_to_publish_on_add.reset();
        }
        return true;
    }
    bool remove_local_seat(std::uint8_t player_index,
                           og::sim::LobbySeatId seat_id) override
    {
        remove_seat_calls.emplace_back(player_index, seat_id);
        if (!remove_seat_accept)
            return false;
        const auto player = std::find_if(
            players.begin(), players.end(),
            [player_index, seat_id](const og::sim::LobbyPlayer& candidate) {
                return candidate.player_index == player_index &&
                    candidate.seat_id == seat_id;
            });
        const auto local = std::find(
            local_indices.begin(), local_indices.end(), player_index);
        if (player == players.end() || local == local_indices.end())
            return false;
        players.erase(player);
        local_indices.erase(local);
        if (active_local_count.has_value() && *active_local_count > 0)
            --*active_local_count;
        return true;
    }
    [[nodiscard]] std::size_t local_seat_count() const override
    {
        if (active_local_count.has_value())
            return *active_local_count;
        return IPickerLobbyClient::local_seat_count();
    }
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
    bool request_seat_team_change(std::uint8_t player_index,
                                  short team) override
    {
        seat_team_calls.emplace_back(player_index, team);
        if (!seat_team_accept)
            return false;
        const auto found = std::find_if(
            players.begin(), players.end(),
            [player_index](const og::sim::LobbyPlayer& player) {
                return player.player_index == player_index;
            });
        if (found == players.end() ||
            std::find(local_indices.begin(), local_indices.end(),
                      player_index) == local_indices.end())
        {
            return false;
        }
        found->team = team;
        ready_state = false;
        return true;
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
        return networked;
    }

    bool networked = true;
    bool host = false;
    bool ready_state = false;
    bool requested = false;
    og::sim::StartDenialReason denial = og::sim::StartDenialReason::None;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    std::vector<bool> ready_calls;
    std::vector<std::pair<std::uint8_t, short>> seat_team_calls;
    bool seat_team_accept = true;
    bool add_seat_accept = true;
    std::optional<std::size_t> active_local_count;
    std::optional<og::sim::LobbyPlayer> seat_to_publish_on_add;
    int add_seat_calls = 0;
    bool remove_seat_accept = true;
    std::vector<std::pair<std::uint8_t, og::sim::LobbySeatId>>
        remove_seat_calls;
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
    player.seat_id =
        static_cast<og::sim::LobbySeatId>(1000 + player_index);
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

TEST(ViewTeam, stable_seat_token_rejects_reindexed_display_handle)
{
    NetworkedRosterLobbyClient lobby;
    lobby.local_indices = {4};
    og::sim::LobbyPlayer displayed =
        make_foreign_lobby_player(
            4, "same-name", "SAME COMPANY", 0, 0);
    displayed.seat_id = 900;
    lobby.players = {displayed};

    const std::uint8_t stale_player_index = displayed.player_index;
    const og::sim::LobbySeatId stale_seat_id = displayed.seat_id;

    // A later state reuses P5 for a different owned seat with indistinguishable
    // display metadata. The index-only compatibility call could address it,
    // but the live UI's (index, token) pair must fail closed.
    lobby.players.front().seat_id = 901;
    og::ui::IPickerLobbyClient& client = lobby;
    EXPECT_FALSE(client.request_seat_team_change(
        stale_player_index, stale_seat_id, 2));
    EXPECT_TRUE(lobby.seat_team_calls.empty());

    EXPECT_TRUE(client.request_seat_team_change(
        stale_player_index, lobby.players.front().seat_id, 2));
    ASSERT_EQ(1u, lobby.seat_team_calls.size());
    EXPECT_EQ(std::make_pair(stale_player_index, short{2}),
              lobby.seat_team_calls.front());
}

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
    // widen into no_draw hit zones and their §9.11 row-body zones hide.
    og::ui::install_base_camp_state_for_screen(&state);
    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    int highlighted = 0;
    ASSERT_NE(nullptr, spec.nav.rewire);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[0].no_draw);
    EXPECT_EQ(14, buttons[0].sizex);
    EXPECT_FALSE(buttons[kBaseCampRowBodyBase + 0].hidden);
    EXPECT_TRUE(buttons[kBaseCampTeamChipBase + 0].hidden)
        << "network team assignment makes the color chip read-only";
    EXPECT_TRUE(buttons[2].no_draw) << "foreign row = no_draw hit zone";
    EXPECT_EQ(12, buttons[2].x);
    EXPECT_EQ(300, buttons[2].sizex)
        << "foreign rows use the full padded roster width";
    EXPECT_TRUE(buttons[kBaseCampRowBodyBase + 2].hidden)
        << "foreign rows have no row-body train zone";
    EXPECT_TRUE(buttons[kCreateMenuGoIndex].hidden)
        << "joiner machine: GO hidden by the production rewire";

    const short assigned_team = save.team_list[0]->teamnum;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampTeamChipBase + 0, &state));
    EXPECT_EQ(assigned_team, save.team_list[0]->teamnum)
        << "even a stale network chip dispatch must not change team";

    // Foreign clicks are read-only: OWNED BY <full company name> (U9).
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(2, &state));
    EXPECT_TRUE(trace_contains("popup", "OWNED BY: IRON HOST BAND"))
        << "foreign deploy hit zone names the owning company";
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kBaseCampRowBodyBase + 3, &state));
    EXPECT_FALSE(trace_contains("basecamp", "train slot="))
        << "a foreign row-body ordinal must not seed a session";
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
    EXPECT_EQ(4, state.page.page_count())
        << "the page window grows defensively past 3 pages";
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

    // Networked draw pass: §9.12 session-status header, COMPANY column,
    // foreign X/- glyphs (smoke + coverage; the strings are unit-pinned in
    // test_picker_common).
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);
    spec.draw_background(&state);
    spec.draw_content(&state);

    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

TEST(ViewTeam, base_camp_seat_rail_targets_owned_global_seat_and_clamps_page)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.save_name = "MY COMPANY";
    auto hero = std::make_unique<guy>(FAMILY_SOLDIER);
    hero->name = "UNCHANGED HERO";
    hero->teamnum = 2;
    save.team_list[0] = std::move(hero);
    save.team_size = 1;

    NetworkedRosterLobbyClient lobby;
    lobby.host = false;
    lobby.local_indices = {4};
    // Deliberately reverse/mix the wire order: Base Camp must present the
    // authoritative global player indexes as P1..P5.
    for (const int player_index : {4, 2, 0, 3, 1})
    {
        og::sim::LobbyPlayer player = make_foreign_lobby_player(
            static_cast<std::uint8_t>(player_index),
            player_index == 4 ? "net-me" : "net-remote",
            player_index == 4 ? "MY COMPANY" : "Iron Host",
            0, 0);
        player.team = static_cast<short>(
            player_index == 4 ? SCORE_TEAM_COUNT - 1
                              : player_index % SCORE_TEAM_COUNT);
        lobby.players.push_back(std::move(player));
    }
    ActivePickerLobbyClientGuard client_guard(&lobby);

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(5u, state.seats.size());
    ASSERT_EQ(2, state.seat_page.page_count());
    for (int i = 0; i < 5; ++i)
        EXPECT_EQ(i, state.seats[static_cast<std::size_t>(i)].player_index);

    const og::ui::MenuScreenSpec& base_spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, base_spec.on_spec_row);
    ASSERT_NE(nullptr, base_spec.nav.rewire);
    og::ui::install_base_camp_state_for_screen(&state);

    button* buttons = picker_createmenu_buttons();
    const int count = picker_createmenu_button_count();
    int highlighted = kBaseCampSeatCardBase;
    base_spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[kBaseCampSeatPagePrevIndex].hidden);
    EXPECT_FALSE(buttons[kBaseCampSeatPageNextIndex].hidden);
    EXPECT_EQ("P1 IRO ", buttons[kBaseCampSeatCardBase].label);
    EXPECT_EQ("P4 IRO ", buttons[kBaseCampSeatCardBase + 3].label);

    // A foreign card is informational only and names the full company.
    trace_clear();
    EXPECT_EQ(MENU_OK,
              base_spec.on_spec_row(kBaseCampSeatCardBase, &state));
    EXPECT_TRUE(trace_contains("popup", "Iron Host"));
    EXPECT_TRUE(lobby.seat_team_calls.empty());

    // Page 2 contains only P5, which this client owns.
    EXPECT_EQ(MENU_OK,
              base_spec.on_spec_row(kBaseCampSeatPageNextIndex, &state));
    EXPECT_EQ(1, state.seat_page.page);
    // Stepping beyond the final page is a no-op.
    EXPECT_EQ(MENU_OK,
              base_spec.on_spec_row(kBaseCampSeatPageNextIndex, &state));
    EXPECT_EQ(1, state.seat_page.page);
    buttons = picker_createmenu_buttons();
    base_spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("P5 YOU ", buttons[kBaseCampSeatCardBase].label);
    for (int card = 1; card < kBaseCampSeatCardsPerPage; ++card)
        EXPECT_TRUE(buttons[kBaseCampSeatCardBase + card].hidden);

    // Clicking that card opens the stable-token editor in production. Drive
    // its TEAM row directly here so the test never depends on a blocking
    // nested menu, and pin that P5/local-profile-1 identity all the way
    // through the action.
    const auto local = std::find_if(
        lobby.players.begin(), lobby.players.end(),
        [](const og::sim::LobbyPlayer& player) {
            return player.player_index == 4;
        });
    ASSERT_NE(lobby.players.end(), local);
    og::ui::SeatSettingsScreenState editor_state{
        .seat_id = local->seat_id,
        .player_index = local->player_index,
        .local_slot = 0,
    };
    og::ui::install_seat_settings_state_for_screen(&editor_state);
    const og::ui::MenuScreenSpec& editor_spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    ASSERT_NE(nullptr, editor_spec.on_spec_row);
    ASSERT_NE(nullptr, editor_spec.nav.rewire);

    button* editor_buttons = editor_spec.buttons_accessor();
    const int editor_count = editor_spec.count_accessor();
    int editor_highlight = kSeatSettingsTeamIndex;
    editor_spec.nav.rewire(
        editor_buttons, editor_count, editor_highlight);
    EXPECT_EQ("TEAM 4", editor_buttons[kSeatSettingsTeamIndex].label);
    EXPECT_EQ("SPECTATE",
              editor_buttons[kSeatSettingsRemoveIndex].label);

    lobby.ready_state = true;
    EXPECT_EQ(MENU_OK,
              editor_spec.on_spec_row(
                  kSeatSettingsTeamIndex, &editor_state));
    ASSERT_EQ(1u, lobby.seat_team_calls.size());
    EXPECT_EQ(4, lobby.seat_team_calls[0].first)
        << "P5 must target global player_index 4, never local seat zero";
    EXPECT_EQ(0, lobby.seat_team_calls[0].second)
        << "classic team 4 wraps to team 1";
    EXPECT_FALSE(lobby.ready_state)
        << "an accepted assignment clears the owning machine's ready";
    ASSERT_NE(nullptr, save.team_list[0]);
    EXPECT_EQ(2, save.team_list[0]->teamnum)
        << "seat assignment must not recolor a saved hero";
    EXPECT_TRUE(trace_contains("basecamp", "seat_team player=5 team=1"));

    // A denied request reports the failure and leaves hero allegiance alone.
    lobby.seat_team_accept = false;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              editor_spec.on_spec_row(
                  kSeatSettingsTeamIndex, &editor_state));
    ASSERT_EQ(2u, lobby.seat_team_calls.size());
    EXPECT_TRUE(trace_contains("popup", "CHANGE DENIED"));
    EXPECT_EQ(2, save.team_list[0]->teamnum);

    // Explicit two-team CTF wraps the same exact P5 target within {1,2}.
    lobby.seat_team_accept = true;
    save.current_campaign = "org.openglad.ctf";
    save.ctf_team_count = 2;
    local->team = 1;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(1, state.seat_page.page)
        << "refresh preserves a still-valid page";
    EXPECT_EQ(MENU_OK,
              editor_spec.on_spec_row(
                  kSeatSettingsTeamIndex, &editor_state));
    ASSERT_EQ(3u, lobby.seat_team_calls.size());
    const std::pair<std::uint8_t, short> expected_ctf_request{4, 0};
    EXPECT_EQ(expected_ctf_request, lobby.seat_team_calls.back());
    EXPECT_EQ(2, save.team_list[0]->teamnum);

    // Auto follows authored flag teams rather than exposing NOT ON MAP
    // choices. Stamp flags for teams 1 and 3 onto the mounted picker world:
    // a P5 seat on team 1 must advance directly to team 3.
    const std::string old_mounted_campaign = get_mounted_campaign();
    set_mounted_campaign_for_testing("org.openglad.ctf");
    GameWorld& world = og::runtime::current_session->myscreen_->world();
    const int old_world_id = world.id;
    const auto old_world_type = world.type;
    const std::size_t old_fx_size = world.fxlist.size();
    std::vector<std::pair<walker*, unsigned char>> old_flag_teams;
    for (const auto& effect : world.fxlist)
    {
        if (effect && effect->query_order() == Order::Treasure &&
            effect->family() == og::FAMILY_FLAG)
        {
            old_flag_teams.emplace_back(effect.get(), effect->team_num());
            effect->set_team_num(0);
        }
    }
    world.id = save.scen_num;
    world.type |= GameWorld::TYPE_CTF;
    walker* flag0 = world.add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
    walker* flag2 = world.add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
    EXPECT_NE(nullptr, flag0);
    EXPECT_NE(nullptr, flag2);
    if (flag0 != nullptr)
        flag0->set_team_num(0);
    if (flag2 != nullptr)
        flag2->set_team_num(2);
    save.ctf_team_count = 0;
    local->team = 0;
    og::ui::base_camp_refresh_rows(state);
    EXPECT_EQ(MENU_OK,
              editor_spec.on_spec_row(
                  kSeatSettingsTeamIndex, &editor_state));
    EXPECT_EQ(4u, lobby.seat_team_calls.size());
    const std::pair<std::uint8_t, short> expected_auto_request{4, 2};
    if (!lobby.seat_team_calls.empty()) {
        EXPECT_EQ(expected_auto_request, lobby.seat_team_calls.back());
    }
    EXPECT_EQ(2, save.team_list[0]->teamnum);

    // Explicit N is also authored-order, not a numeric [0,N) range. With
    // sparse flags {0,2}, explicit 2 keeps both and advances 0 directly to 2.
    save.ctf_team_count = 2;
    local->team = 0;
    og::ui::base_camp_refresh_rows(state);
    EXPECT_EQ(MENU_OK,
              editor_spec.on_spec_row(
                  kSeatSettingsTeamIndex, &editor_state));
    ASSERT_EQ(5u, lobby.seat_team_calls.size());
    const std::pair<std::uint8_t, short> expected_sparse_explicit{4, 2};
    EXPECT_EQ(expected_sparse_explicit, lobby.seat_team_calls.back());
    EXPECT_EQ(2, save.team_list[0]->teamnum);

    for (const auto& [flag, team] : old_flag_teams)
        flag->set_team_num(team);
    while (world.fxlist.size() > old_fx_size)
        world.fxlist.pop_back();
    world.id = old_world_id;
    world.type = old_world_type;
    set_mounted_campaign_for_testing(old_mounted_campaign);

    // Disconnect/shrink while parked on page two clamps to page one and
    // removes both pager arrows.
    lobby.players.erase(
        std::remove_if(
            lobby.players.begin(), lobby.players.end(),
            [](const og::sim::LobbyPlayer& player) {
                return player.player_index == 4;
            }),
        lobby.players.end());
    og::ui::base_camp_refresh_rows(state);
    EXPECT_EQ(0, state.seat_page.page);
    EXPECT_EQ(1, state.seat_page.page_count());
    buttons = picker_createmenu_buttons();
    base_spec.nav.rewire(buttons, count, highlighted);
    EXPECT_TRUE(buttons[kBaseCampSeatPagePrevIndex].hidden);
    EXPECT_TRUE(buttons[kBaseCampSeatPageNextIndex].hidden);

    og::ui::install_seat_settings_state_for_screen(nullptr);
    og::ui::install_base_camp_state_for_screen(nullptr);
    save.reset();
}

TEST(ViewTeam, seat_settings_draws_selected_identity_and_direction_mode)
{
    NetworkedRosterLobbyClient lobby;
    lobby.local_indices = {6};
    lobby.players.push_back(
        make_foreign_lobby_player(6, "net-me", "WATCHING BAND", 0, 0));
    lobby.players.front().team = 2;
    ActivePickerLobbyClientGuard client_guard(&lobby);

    og::ui::SeatSettingsScreenState state{
        .seat_id = lobby.players.front().seat_id,
        .player_index = lobby.players.front().player_index,
        // Deliberately stale/global-looking: the live seat token must resolve
        // global P7 back to this machine's first controller profile.
        .local_slot = 2,
    };
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    ASSERT_NE(nullptr, spec.draw_content);
    ASSERT_NE(nullptr, spec.nav.rewire);

    screen* const output = og::runtime::current_session->myscreen_;
    text& font = output->text_normal;

    const auto identity_band_hash = [&] {
        std::uint64_t hash = 1469598103934665603ULL;
        for (int y = 13; y < 13 + font.sizey; ++y) {
            for (int x = 0; x < 320; ++x) {
                int pixel = 0;
                output->get_pixel(x, y, &pixel);
                hash ^= static_cast<std::uint8_t>(pixel);
                hash *= 1099511628211ULL;
            }
        }
        return hash;
    };
    const auto expected_identity_hash = [&](const char* label) {
        output->clearbuffer();
        font.write_xy_center(160, 13, DARK_BLUE, "%s", label);
        return identity_band_hash();
    };

    const int old_mode = get_player_control_mode(0);
    const int old_four_yell = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_YELL);
    const int old_eight_yell = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_YELL, SDLK_E);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_YELL, SDLK_S);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    output->clearbuffer();
    spec.draw_content(&state);
    EXPECT_EQ(0, state.local_slot)
        << "global P7 must use local controller profile 1, not profile 3";
    EXPECT_EQ(static_cast<int>(SDLK_E),
              og::runtime::current_session->player_keys_[0][KEY_YELL])
        << "local player 1 uses E to yell in 4-direction mode";
    for (int y = 43; y < 51; ++y) {
        for (int x = 93; x < 101; ++x) {
            int pixel = -1;
            output->get_pixel(x, y, &pixel);
            EXPECT_EQ(PURE_BLACK, pixel)
                << "the TEAM face owns its whole label; no color chip at "
                << x << "," << y;
        }
    }
    const std::uint64_t selected_identity = identity_band_hash();
    EXPECT_EQ(expected_identity_hash("LOCAL PLAYER 1 / P7"),
              selected_identity);

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kSeatSettingsTeamIndex;
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("TEAM 3", buttons[kSeatSettingsTeamIndex].label);
    EXPECT_EQ("4-DIRECTION", buttons[kSeatSettingsModeIndex].label);

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("8-DIRECTION", buttons[kSeatSettingsModeIndex].label);
    EXPECT_EQ(static_cast<int>(SDLK_S),
              og::runtime::current_session->player_keys_[0][KEY_YELL])
        << "local player 1 uses S to yell in 8-direction mode";

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_YELL, old_four_yell);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_YELL, old_eight_yell);
    set_player_control_mode(0, old_mode);
    og::ui::install_seat_settings_state_for_screen(nullptr);
}

TEST(ViewTeam, seat_settings_offline_p1_uses_profile1_when_roster_is_out_of_order)
{
    NetworkedRosterLobbyClient lobby;
    lobby.networked = false;
    lobby.active_local_count = 3;
    lobby.players.push_back(
        make_foreign_lobby_player(1, "local-two", "MY COMPANY", 0, 0));
    lobby.players.push_back(
        make_foreign_lobby_player(2, "local-three", "MY COMPANY", 0, 0));
    lobby.players.push_back(
        make_foreign_lobby_player(0, "local-one", "MY COMPANY", 0, 0));
    ActivePickerLobbyClientGuard client_guard(&lobby);

    const int old_mode = get_player_control_mode(0);
    const int old_four_yell = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection), KEY_YELL);
    const int old_eight_yell = get_player_key_binding_for_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection), KEY_YELL);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_YELL, SDLK_E);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_YELL, SDLK_S);

    const og::sim::LobbyPlayer& p1 = lobby.players.back();
    og::ui::SeatSettingsScreenState state{
        .seat_id = p1.seat_id,
        .player_index = p1.player_index,
        .local_slot = 2,
    };
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kSeatSettingsModeIndex;

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ(0, state.local_slot)
        << "offline displayed P1 must resolve by dense P# order";
    EXPECT_EQ(static_cast<int>(SDLK_E),
              og::runtime::current_session->player_keys_[0][KEY_YELL]);

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ(0, state.local_slot);
    EXPECT_EQ(static_cast<int>(SDLK_S),
              og::runtime::current_session->player_keys_[0][KEY_YELL]);

    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_YELL, old_four_yell);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    set_player_key_binding(0, KEY_YELL, old_eight_yell);
    set_player_control_mode(0, old_mode);
    og::ui::install_seat_settings_state_for_screen(nullptr);
}

TEST(ViewTeam, seat_settings_remove_uses_exact_token_and_compacts_profiles)
{
#if defined(DISABLE_MULTIPLAYER) || defined(USE_TOUCH_INPUT)
    GTEST_SKIP() << "remove/spectate is not compiled into single-seat builds";
#else
    picker_testing_yes_or_no_queue_clear();
    reset_default_player_controls();
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_control_mode(
        1, static_cast<int>(ControlDirectionMode::EightDirection));

    NetworkedRosterLobbyClient lobby;
    lobby.local_indices = {1, 4};
    lobby.active_local_count = 2;
    lobby.players.push_back(
        make_foreign_lobby_player(4, "net-second", "MY COMPANY", 0, 0));
    lobby.players.push_back(
        make_foreign_lobby_player(1, "net-first", "MY COMPANY", 0, 0));
    ActivePickerLobbyClientGuard client_guard(&lobby);

    const auto selected = std::find_if(
        lobby.players.begin(), lobby.players.end(),
        [](const og::sim::LobbyPlayer& player) {
            return player.player_index == 1;
        });
    ASSERT_NE(lobby.players.end(), selected);
    const std::uint8_t selected_index = selected->player_index;
    const og::sim::LobbySeatId selected_token = selected->seat_id;
    og::ui::SeatSettingsScreenState state{
        .seat_id = selected_token,
        .player_index = selected_index,
        .local_slot = 0,
    };
    og::ui::install_seat_settings_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    ASSERT_NE(nullptr, spec.on_spec_row);
    ASSERT_NE(nullptr, spec.frame_tick);
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kSeatSettingsRemoveIndex;
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("REMOVE PLAYER",
              buttons[kSeatSettingsRemoveIndex].label);

    // REMOVE is deliberately NO-first; the queued affirmative drives the
    // destructive branch without weakening the live confirmation contract.
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(
        MENU_REDRAW,
        spec.on_spec_row(kSeatSettingsRemoveIndex, &state));
    ASSERT_EQ(1u, lobby.remove_seat_calls.size());
    EXPECT_EQ(
        std::make_pair(selected_index, selected_token),
        lobby.remove_seat_calls.front())
        << "the dense P# and authority token must name the same live seat";
    EXPECT_TRUE(state.removed);
    EXPECT_FALSE(spec.frame_tick(&state, 0))
        << "the editor exits immediately after its selected seat leaves";
    ASSERT_TRUE(lobby.active_local_count.has_value());
    EXPECT_EQ(1u, *lobby.active_local_count);
    EXPECT_EQ(std::vector<std::uint8_t>({4}), lobby.local_indices);

    // Local profile 2 follows its surviving seat down to profile 1; the
    // vacated tail returns to the defaults instead of leaking old controls.
    EXPECT_EQ(
        static_cast<int>(ControlDirectionMode::EightDirection),
        get_player_control_mode(0));
    EXPECT_EQ(
        static_cast<int>(ControlDirectionMode::FourDirection),
        get_player_control_mode(1));

    picker_testing_yes_or_no_queue_clear();
    reset_default_player_controls();
    og::ui::install_seat_settings_state_for_screen(nullptr);
#endif
}

TEST(ViewTeam, base_camp_zero_seat_state_activates_only_through_plus)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 0;
    save.current_campaign = "org.openglad.gladiator";

    NetworkedRosterLobbyClient lobby;
    lobby.players.push_back(
        make_foreign_lobby_player(6, "spectator", "WATCHING BAND", 0, 0));
    lobby.players.front().team = 0;
    lobby.active_local_count = 0;
    ActivePickerLobbyClientGuard client_guard(&lobby);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);
    ASSERT_NE(nullptr, spec.on_spec_row);

    // Local sessions own every synthetic seat, including the one retained
    // for a zero-player spectator. They do not need a network ownership list.
    // The old Player Settings right-click painted a separate SPECTATOR status;
    // Base Camp now makes that state actionable in-place as SPEC + [+].
    lobby.networked = false;
    lobby.local_indices.clear();
    {
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        ASSERT_EQ(1u, state.seats.size());
        ASSERT_EQ(1u, state.local_seat_indices.size());
        EXPECT_EQ(6, state.local_seat_indices.front());

        og::ui::install_base_camp_state_for_screen(&state);
        button* buttons = picker_createmenu_buttons();
        int highlighted = kBaseCampSeatCardBase;
        spec.nav.rewire(
            buttons, picker_createmenu_button_count(), highlighted);
        EXPECT_FALSE(buttons[kBaseCampSeatCardBase].hidden);
        EXPECT_EQ("P7 SPEC ", buttons[kBaseCampSeatCardBase].label);
        EXPECT_FALSE(buttons[kBaseCampAddSeatIndex].hidden);
        ASSERT_NE(nullptr,
                  spec.rows[kBaseCampAddSeatIndex].state_override);
        EXPECT_EQ(
            og::ui::RowState::Visible,
            spec.rows[kBaseCampAddSeatIndex].state_override(
                og::ui::MenuLabelContext{}));
        trace_clear();
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(kBaseCampSeatCardBase, &state));
        EXPECT_TRUE(trace_contains("popup", "PRESS + TO ADD A PLAYER"));
        EXPECT_TRUE(lobby.seat_team_calls.empty());
        og::ui::install_base_camp_state_for_screen(nullptr);
    }

    // A current network spectator publishes no owned placeholder at all:
    // the dormant reconnect token is server-private, so the client sees zero
    // cards and cannot READY. [+] asks authority to activate and publish P1.
    lobby.networked = true;
    lobby.players.clear();
    lobby.local_indices.clear();
    lobby.active_local_count = 0;
    lobby.seat_to_publish_on_add =
        make_foreign_lobby_player(0, "net-me", "WATCHING BAND", 0, 0);
    {
        og::ui::BaseCampScreenState state;
        og::ui::base_camp_refresh_rows(state);
        EXPECT_TRUE(state.seats.empty());
        EXPECT_TRUE(state.local_seat_indices.empty());

        og::ui::install_base_camp_state_for_screen(&state);
        button* buttons = picker_createmenu_buttons();
        int highlighted = kBaseCampAddSeatIndex;
        spec.nav.rewire(
            buttons, picker_createmenu_button_count(), highlighted);
        EXPECT_TRUE(buttons[kBaseCampSeatCardBase].hidden);
        EXPECT_FALSE(buttons[kBaseCampAddSeatIndex].hidden);
        EXPECT_TRUE(buttons[kCreateMenuReadyIndex].hidden);
        ASSERT_NE(nullptr,
                  spec.rows[kBaseCampAddSeatIndex].state_override);
        EXPECT_EQ(
            og::ui::RowState::Visible,
            spec.rows[kBaseCampAddSeatIndex].state_override(
                og::ui::MenuLabelContext{}));
        EXPECT_TRUE(lobby.seat_team_calls.empty());

        // [+] activates the authority's dormant token and publishes its new
        // active seat. One touch-collapsed duplicate is debounced.
        trace_clear();
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(kBaseCampAddSeatIndex, &state));
        ASSERT_EQ(1, lobby.add_seat_calls);
        ASSERT_TRUE(lobby.active_local_count.has_value());
        EXPECT_EQ(1u, *lobby.active_local_count);
        buttons = picker_createmenu_buttons();
        spec.nav.rewire(
            buttons, picker_createmenu_button_count(), highlighted);
        EXPECT_EQ("P1 YOU ", buttons[kBaseCampSeatCardBase].label);
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(kBaseCampAddSeatIndex, &state));
        EXPECT_EQ(1, lobby.add_seat_calls);
        EXPECT_TRUE(trace_contains("basecamp", "seat_add_debounced"));
        og::ui::install_base_camp_state_for_screen(nullptr);
    }

    save.reset();
}

// ---------------------------------------------------------------------------
// Empty-state treatment: the fixed View Team-style grey roster panel remains
// visible with zero rows and the content pass centers the ORANGE line inside
// it. The null install renders the empty shape on both hooks; smoke + coverage
// (the panel itself is verified by capture).
// ---------------------------------------------------------------------------
TEST(ViewTeam, base_camp_empty_state_draws_framed_panel)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);

    // Null state == zero visible rows == the empty-roster shape.
    og::ui::install_base_camp_state_for_screen(nullptr);
    spec.draw_background(nullptr);
    spec.draw_content(nullptr);

    // An installed-but-empty state windows zero rows and draws the same
    // shape (the page model clamps to an empty slot list).
    og::ui::BaseCampScreenState empty_state;
    og::ui::install_base_camp_state_for_screen(&empty_state);
    spec.draw_background(&empty_state);
    spec.draw_content(&empty_state);
    og::ui::install_base_camp_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// §2.6 READY twin (stage ready-go-slot): a networked joiner gets READY in
// GO's exact rect; the click drives the shared lobby flag through the
// production ToggleLobbyReady handler with the client deploy gate
// (cross-control OFF + brought-but-benched => popup; active empty-roster
// seats have no deploy minimum [NET-R9]; cross-control ON removes it too).
// True zero-seat clients have no READY twin and are covered separately.
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

    // Deployed roster: the toggle acts directly through the Base Camp
    // twin's own ordinal.
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

    // Active seat with an empty roster: readies freely [NET-R9]; the slot
    // stays visible and reachable on an all-foreign display. A true zero-seat
    // spectator has no READY action.
    save.team_list[0].reset();
    save.team_size = 0;
    og::ui::base_camp_refresh_rows(state);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[kCreateMenuReadyIndex].hidden)
        << "[NET-R9]: the active empty-roster seat keeps the READY button";
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

    // MATCHUP retains the old READY ordinal only as dormant table history;
    // Base Camp is the sole ready affordance.
    button* matchup = picker_teamsmenu_buttons();
    const og::ui::MenuScreenSpec& matchup_spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).spec;
    int matchup_highlight = kTeamsMenuBackIndex;
    matchup_spec.nav.rewire(
        matchup, picker_teamsmenu_button_count(), matchup_highlight);
    EXPECT_TRUE(matchup[kTeamsMenuReadyIndex].hidden);
    EXPECT_EQ(4u, lobby.ready_calls.size())
        << "opening MATCHUP must not toggle readiness";

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

TEST(ViewTeam, base_camp_go_uses_explicit_local_seat_teams_for_control_gate)
{
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 2;
    save.allied_mode = 0; // legacy Split would derive teams 0 and 1
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;

    for (int slot = 0; slot < 2; ++slot)
    {
        save.team_list[slot] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[slot]->teamnum = 0;
        save.team_list[slot]->deployed = true;
    }
    save.team_size = 2;

    NetworkedRosterLobbyClient lobby;
    lobby.networked = false;
    lobby.players.push_back(
        make_foreign_lobby_player(0, "local-1", "LOCAL BAND", 0, 0));
    lobby.players.push_back(
        make_foreign_lobby_player(1, "local-2", "LOCAL BAND", 0, 0));
    lobby.players[0].team = 0;
    lobby.players[1].team = 0;
    ActivePickerLobbyClientGuard client_guard(&lobby);

    EXPECT_EQ(MENU_REDRAW, go_menu(0));
    EXPECT_TRUE(lobby.requested)
        << "the explicit [0,0] seats have two deployed team-0 heroes";
    EXPECT_FALSE(trace_contains("popup", "DEPLOY FOR EVERY PLAYER"));

    save.reset();
}

// ---------------------------------------------------------------------------
// §2.7 cross-control (MATCHUP): visible to ALL peers when networked in the
// bottom command row; host-only actionable (non-host click popups HOST
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
        << "§2.7: cross-control is MATCHUP's one MenuSpecRow";

    // Visible to every networked peer, with retired JOIN/guy/READY controls
    // staying hidden and the label supplied by the shared formatter.
    button* buttons = picker_teamsmenu_buttons();
    const int count = picker_teamsmenu_button_count();
    int highlighted = 0;
    ASSERT_NE(nullptr, spec.nav.rewire);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_FALSE(buttons[kTeamsMenuCrossControlIndex].hidden)
        << "§2.7: joiners must SEE the mode that changes their rights";
    EXPECT_TRUE(buttons[kTeamsMenuGuyTeamIndex].hidden)
        << "the retired guy cycler must remain hidden";
    EXPECT_TRUE(buttons[kTeamsMenuReadyIndex].hidden)
        << "READY belongs only to Base Camp";
    EXPECT_EQ(120, buttons[kTeamsMenuCrossControlIndex].x);
    EXPECT_EQ(170, buttons[kTeamsMenuCrossControlIndex].y);
    EXPECT_EQ(80, buttons[kTeamsMenuCrossControlIndex].sizex);
    EXPECT_EQ(20, buttons[kTeamsMenuCrossControlIndex].sizey);
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
// §9.11 row-body click's seed, renames THAT character (editguy_ follows the
// seeded slot) and the rename accept autosaves the company (§3.8).
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

    if (!wait_for_interactable("roster_row_1", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);  // FadeAroundEntry eats early clicks
    interact("roster_row_1");

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
        << "a roster row-body click should open the train screen";
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
