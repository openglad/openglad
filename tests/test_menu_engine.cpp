// WP1 step 2 — menu engine runner tests (docs/menu-engine.md, design §1.9
// Layer E). This file covers the engine obligations that the migrated
// DIFFICULTY screen (the first engine-hosted spec) and the runner itself
// must honor:
//
//   - G5 remote-start generator: preemption fires from EVERY engine screen
//     in the registry, plus a synthetic-spec check of the TeamBuildScope
//     runner branch (structurally killing the strand-the-joiners bug class).
//   - Dual label surfaces under lobby-rewrites-save: the runner's per-frame
//     label sync writes BOTH the live vbutton and the mutable descriptor row.
//   - G3 retvalue-zero discipline: ButtonAction::MenuSpecRow returns 101,
//     and 101 & MENU_EXIT != 0 — the runner must consume the stash and
//     rewrite retvalue so the screen does NOT exit on an engine-row click.
//   - Disabled-row no-op: RowState::Disabled rows stay visible but inert,
//     and a click landing on one leaves a TRACE.
//
// Screen-shape fidelity is NOT re-tested here: test_menu_layout's
// hand-owned difficulty kExpected table remains the independent oracle over
// the spec transcription (G11).

#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/save_data.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include "test_interact.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <string>
#include <vector>

extern bool g_start_game_requested;

namespace
{

inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

// Minimal lobby client: host-visible, with a consumable game-start config so
// the remote-start checkers see a launchable start.
struct FakeLobbyClient final : og::ui::IPickerLobbyClient
{
    bool host = true;
    bool has_config = true;

    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return true; }
    std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override
    {
        return has_config
            ? std::optional<og::ui::PickerLobbyGameStartConfig>{
                  og::ui::PickerLobbyGameStartConfig{}}
            : std::nullopt;
    }
    std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override
    {
        return build_game_start_config();
    }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool has_game_start_config() const noexcept override
    {
        return has_config;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return host;
    }
};

// Restores every global a runner invocation can touch (shuffle hygiene).
struct EngineTestGuard
{
    og::ui::IPickerLobbyClient* saved_client =
        og::ui::active_picker_lobby_client();
    ~EngineTestGuard()
    {
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
        pks().menu_spec_clicked_row = -1;
        og::ui::install_active_picker_lobby_client(saved_client);
        clear_allbuttons();
        og::runtime::current_session->localbuttons_ = nullptr;
    }
};

// --- Synthetic spec plumbing (function pointers need file statics) ---------

std::vector<button> g_synth_buttons;
const og::ui::MenuScreenSpec* g_synth_spec = nullptr;

button* synth_buttons_accessor()
{
    og::ui::materialize_menu_buttons(*g_synth_spec, g_synth_buttons);
    return g_synth_buttons.data();
}

int synth_count_accessor()
{
    return static_cast<int>(g_synth_buttons.size());
}

void synth_draw_background(void* /*state*/)
{
    og::runtime::current_session->myscreen_->clear_window();
}

int g_spec_row_hits = 0;
int g_spec_row_last = -1;

Sint32 synth_on_spec_row(int row, void* /*state*/)
{
    ++g_spec_row_hits;
    g_spec_row_last = row;
    return MENU_REDRAW;
}

og::ui::RowState disabled_row_state(const og::ui::MenuLabelContext&)
{
    return og::ui::RowState::Disabled;
}

og::ui::MenuScreenSpec make_synth_spec(const og::ui::MenuButtonSpec* rows,
                                       int row_count, const char* name)
{
    og::ui::MenuScreenSpec spec{};
    spec.name = name;
    spec.rows = rows;
    spec.row_count = row_count;
    spec.buttons_accessor = &synth_buttons_accessor;
    spec.count_accessor = &synth_count_accessor;
    spec.on_spec_row = &synth_on_spec_row;
    spec.draw_background = &synth_draw_background;
    spec.exit_value = MENU_REDRAW;
    return spec;
}

} // namespace

// ---------------------------------------------------------------------------
// G5: remote-start preemption fires from EVERY engine screen in the
// registry. Parameterized over menu_screen_host so each newly migrated
// screen is swept automatically — an engine screen that forgets its
// remote-start scope (or whose loop drops the check) fails here instead of
// stranding joiners.
TEST(MenuEngine, remote_start_preempts_every_engine_screen)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    int engine_screens = 0;
    for (int i = 0; i < static_cast<int>(og::ui::MenuScreenId::Count); ++i) {
        const og::ui::MenuScreenHost& host =
            og::ui::menu_screen_host(static_cast<og::ui::MenuScreenId>(i));
        if (host.kind != og::ui::MenuScreenHost::Kind::Engine)
            continue;
        ++engine_screens;
        ASSERT_NE(nullptr, host.spec) << "engine host without a spec";
        EXPECT_NE(og::ui::RemoteStartScope::None, host.spec->remote_start)
            << host.spec->name
            << ": engine screens must declare a remote-start scope (G5)";

        pks().selected_menu_item = nullptr;
        g_start_game_requested = true;
        const Sint32 result = og::ui::run_menu_screen(*host.spec);
        EXPECT_TRUE(result & MENU_EXIT)
            << host.spec->name << " must propagate the remote MENU_EXIT";
        ASSERT_NE(nullptr, pks().selected_menu_item)
            << host.spec->name << " must select the unwind target";
        if (host.spec->remote_start == og::ui::RemoteStartScope::MainScope) {
            EXPECT_EQ(og::ui::PickerMenuCommand::ContinueGame,
                      pks().selected_menu_item->command)
                << host.spec->name;
        } else {
            EXPECT_EQ(og::ui::PickerMenuCommand::StartGame,
                      pks().selected_menu_item->command)
                << host.spec->name;
        }
        g_start_game_requested = false;
        pks().selected_menu_item = nullptr;
    }
    // The registry must never let this sweep silently pass over nothing.
    EXPECT_GE(engine_screens, 1) << "no engine screens registered";
    // The first migrated screen stays engine-hosted.
    EXPECT_EQ(og::ui::MenuScreenHost::Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Difficulty).kind);
}

// The TeamBuildScope runner branch (no engine screen declares it yet):
// preemption must select TeamBuild START GAME and return MENU_EXIT.
TEST(MenuEngine, remote_start_team_build_scope_returns_exit)
{
    static constexpr og::ui::MenuButtonSpec kRows[] = {
        {.id = "engine_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
         .x = 10, .y = 10, .w = 50, .h = 15,
         .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT, .nav = {}},
    };
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::MenuScreenSpec spec = make_synth_spec(kRows, 1, "synthetic_tb");
    spec.remote_start = og::ui::RemoteStartScope::TeamBuildScope;
    g_synth_spec = &spec;

    pks().selected_menu_item = nullptr;
    g_start_game_requested = true;
    const Sint32 result = og::ui::run_menu_screen(spec);
    EXPECT_EQ(MENU_EXIT, result);
    ASSERT_NE(nullptr, pks().selected_menu_item);
    EXPECT_EQ(og::ui::PickerMenuCommand::StartGame,
              pks().selected_menu_item->command);
}

// ---------------------------------------------------------------------------
// Dual label surfaces under lobby-rewrites-save: with the DIFFICULTY screen
// open, rewrite the save underneath it (what an open lobby does) and require
// the new label on the LIVE vbutton surface while the screen runs, and on
// the mutable descriptor row after it exits (the descriptor backs redraws
// and reset_buttons re-inits).
namespace
{

struct DualSurfaceState
{
    std::atomic<bool> started{false};
    std::atomic<bool> entered{false};
    std::atomic<bool> saw_live_label{false};
    std::atomic<bool> finished{false};
};

int dual_surface_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DualSurfaceState* state = static_cast<DualSurfaceState*>(data);
    state->started = true;

    if (!wait_for_interactable("difficulty_back", 5000))
        return 0;
    state->entered = true;
    SDL_Delay(300);

    // The lobby-rewrites-save moment: no click precedes this write, so only
    // the runner's per-frame label sync can surface it.
    og::runtime::current_session->myscreen_->save_data.respawn_mode = 2;

    const std::string want = "Respawns: Everyone";
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == "respawn_mode" && item.label == want) {
                state->saw_live_label = true;
                break;
            }
        }
        if (state->saw_live_label)
            break;
        SDL_Delay(50);
    }

    SDL_Delay(300);
    interact("difficulty_back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(MenuEngine, difficulty_dual_label_surface_under_save_rewrite)
{
    EngineTestGuard guard;
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const auto saved_respawn_mode = save.respawn_mode;
    save.respawn_mode = 0;
    const Sint32 saved_difficulty =
        og::runtime::current_session->current_difficulty_;
    og::runtime::current_session->current_difficulty_ = 1;

    DualSurfaceState state;
    SDL_Thread* thread =
        SDL_CreateThread(dual_surface_injector, "dual_surface", &state);
    ASSERT_NE(nullptr, thread);

    const Sint32 result = run_difficulty_menu();

    SDL_WaitThread(thread, nullptr);
    EXPECT_EQ(MENU_REDRAW, result) << "local BACK exits with MENU_REDRAW";
    EXPECT_TRUE(state.entered) << "DIFFICULTY screen never came up";
    EXPECT_TRUE(state.saw_live_label)
        << "live vbutton label must re-derive from the rewritten save";
    EXPECT_TRUE(state.finished);

    // Descriptor surface (race-free read: the screen has exited).
    ASSERT_GT(static_cast<int>(pks().difficulty_menu_buttons.size()),
              kDifficultyMenuRespawnModeIndex);
    EXPECT_EQ("Respawns: Everyone",
              pks().difficulty_menu_buttons[kDifficultyMenuRespawnModeIndex]
                  .label)
        << "descriptor row label must carry the re-derived value too";

    save.respawn_mode = saved_respawn_mode;
    og::runtime::current_session->current_difficulty_ = saved_difficulty;
}

// ---------------------------------------------------------------------------
// G3 retvalue-zero discipline: clicking a ButtonAction::MenuSpecRow row
// returns 101 into retvalue, and 101 & MENU_EXIT != 0. The runner must
// consume the stash (dispatching on_spec_row exactly once, with the row's
// arg) and rewrite retvalue, so the screen stays alive until BACK.
namespace
{

std::atomic<bool> g_run_returned{false};

struct SpecRowState
{
    std::atomic<bool> started{false};
    std::atomic<bool> alive_after_click{false};
    std::atomic<bool> finished{false};
};

int spec_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    SpecRowState* state = static_cast<SpecRowState*>(data);
    state->started = true;

    if (!wait_for_interactable("engine_spec_row", 5000))
        return 0;
    SDL_Delay(300);
    interact("engine_spec_row");
    SDL_Delay(500);
    // If the runner leaked retvalue=101 into its loop condition the screen
    // already exited (101 & MENU_EXIT), and this flag stays false.
    state->alive_after_click = !g_run_returned;
    interact("engine_back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(MenuEngine, spec_row_retvalue_zero_discipline)
{
    static constexpr og::ui::MenuButtonSpec kRows[] = {
        {.id = "engine_spec_row", .label = "ENGINE ROW",
         .x = 90, .y = 60, .w = 140, .h = 15,
         .action = ButtonAction::MenuSpecRow, .arg = 7, .nav = {.down = 1}},
        {.id = "engine_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
         .x = 10, .y = 10, .w = 50, .h = 15,
         .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
         .nav = {.up = 0}},
    };
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::MenuScreenSpec spec = make_synth_spec(kRows, 2, "synthetic_row");
    g_synth_spec = &spec;
    g_spec_row_hits = 0;
    g_spec_row_last = -1;
    g_run_returned = false;

    SpecRowState state;
    SDL_Thread* thread =
        SDL_CreateThread(spec_row_injector, "spec_row", &state);
    ASSERT_NE(nullptr, thread);

    const Sint32 result = og::ui::run_menu_screen(spec);
    g_run_returned = true;

    SDL_WaitThread(thread, nullptr);
    EXPECT_EQ(MENU_REDRAW, result);
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.alive_after_click)
        << "screen exited on the MenuSpecRow click: retvalue 101 leaked "
           "into the MENU_EXIT loop condition";
    EXPECT_EQ(1, g_spec_row_hits) << "on_spec_row must dispatch exactly once";
    EXPECT_EQ(7, g_spec_row_last) << "dispatch must carry the row's arg";
    EXPECT_EQ(-1, pks().menu_spec_clicked_row) << "stash must be consumed";
}

// ---------------------------------------------------------------------------
// Disabled rows: visible (nav counts them, interact() finds them) but inert
// on activation, with a TRACE when a click lands on one.
namespace
{

int disabled_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    SpecRowState* state = static_cast<SpecRowState*>(data);
    state->started = true;

    if (!wait_for_interactable("engine_disabled", 5000))
        return 0;
    SDL_Delay(300);
    interact("engine_disabled");
    SDL_Delay(500);
    state->alive_after_click = !g_run_returned;
    interact("engine_back");
    state->finished = true;
    return 0;
}

} // namespace

TEST(MenuEngine, disabled_row_activation_no_op)
{
    static constexpr og::ui::MenuButtonSpec kRows[] = {
        {.id = "engine_disabled", .label = "DISABLED ROW",
         .x = 90, .y = 60, .w = 140, .h = 15,
         .action = ButtonAction::MenuSpecRow, .arg = 3, .nav = {.down = 1},
         .state_override = &disabled_row_state},
        {.id = "engine_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
         .x = 10, .y = 10, .w = 50, .h = 15,
         .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT,
         .nav = {.up = 0}},
    };
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::MenuScreenSpec spec = make_synth_spec(kRows, 2, "synthetic_dis");
    g_synth_spec = &spec;
    g_spec_row_hits = 0;
    g_run_returned = false;
    trace_clear();

    SpecRowState state;
    SDL_Thread* thread =
        SDL_CreateThread(disabled_row_injector, "disabled_row", &state);
    ASSERT_NE(nullptr, thread);

    const Sint32 result = og::ui::run_menu_screen(spec);
    g_run_returned = true;

    SDL_WaitThread(thread, nullptr);
    EXPECT_EQ(MENU_REDRAW, result);
    EXPECT_TRUE(state.finished);
    EXPECT_TRUE(state.alive_after_click);
    EXPECT_EQ(0, g_spec_row_hits)
        << "a Disabled row must never dispatch its action";
    EXPECT_TRUE(trace_contains("menu_engine",
                               "disabled_row_click engine_disabled"))
        << "a click landing on a Disabled row must leave a TRACE";
}
