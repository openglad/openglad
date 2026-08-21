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
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/interface/ui/campaign_picker_session.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/ui/menu_model.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_interact.h"
#include "test_save_state_guard.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

extern bool g_start_game_requested;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
void ensure_highlighted_button_visible(const button* buttons, int num_buttons,
                                       int& highlighted_button);

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
    bool networked = false;
    bool add_seat_result = false;
    bool remove_seat_result = false;
    bool set_ready_result = false;

    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override
    {
        ++settings_syncs;
        const screen* const myscreen =
            og::runtime::current_session->myscreen_;
        synced_save_level = myscreen->save_data.scen_num;
        synced_world_level = myscreen->world().id;
        synced_ctf_team_mask =
            og::ui::ctf_authored_team_mask_for_loaded_level(
                myscreen->save_data,
                myscreen->world(),
            get_mounted_campaign());
    }
    void poll_and_apply() override
    {
        ++poll_count;
        if (start_on_next_poll.exchange(false))
            g_start_game_requested = true;
    }
    void set_player_mode(int) override {}
    bool add_local_seat() override
    {
        ++add_seat_calls;
        return add_seat_result;
    }
    bool remove_local_seat(std::uint8_t player_index,
                           og::sim::LobbySeatId seat_id) override
    {
        removed_seats.emplace_back(player_index, seat_id);
        return remove_seat_result;
    }
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
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return networked;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer>
    lobby_players() const override
    {
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t>
    local_player_indices() const override
    {
        return local_indices;
    }
    bool set_ready(bool ready) override
    {
        ready_requests.push_back(ready);
        return set_ready_result;
    }
    [[nodiscard]] std::size_t local_seat_count() const override
    {
        return local_seats;
    }

    int settings_syncs = 0;
    short synced_save_level = -1;
    int synced_world_level = -1;
    std::uint8_t synced_ctf_team_mask = 0;
    std::size_t local_seats = 1;
    int add_seat_calls = 0;
    std::vector<std::pair<std::uint8_t, og::sim::LobbySeatId>> removed_seats;
    std::vector<bool> ready_requests;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
    std::atomic<int> poll_count{0};
    std::atomic<bool> start_on_next_poll{false};
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
int g_synth_content_draws = 0;

void synth_draw_content(void* /*state*/)
{
    ++g_synth_content_draws;
}

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

// Engine screens transcribed from legacy loops that historically had NO
// remote-start check keep RemoteStartScope::None for 10a fidelity: a joiner
// parked in one of these launches when the screen exits back into a loop
// that DOES check (mainmenu / team build), exactly as before the migration.
// Any engine screen NOT named here must declare a scope — this allowlist is
// the G5 guard against silently forgetting the obligation on new screens.
static bool remote_start_none_is_legacy_faithful(const std::string& name)
{
    static const std::set<std::string> kLegacyNoRemoteStart = {
        "gameplay_fx", "ui_fx", "graphics_fx",
        "display_settings",
        // #168: the legacy blocking help dialog ran no remote-start check
        // either — a joiner parked in HELP launches when the main menu
        // re-enters, exactly as before the migration.
        "help",
    };
    return kLegacyNoRemoteStart.count(name) > 0;
}

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
        if (host.spec->remote_start == og::ui::RemoteStartScope::None) {
            EXPECT_TRUE(remote_start_none_is_legacy_faithful(host.spec->name))
                << host.spec->name
                << ": engine screens must declare a remote-start scope (G5) "
                   "unless their legacy loop provably had none (10a)";
            continue;
        }

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

// main_options() normalizes every ordinary exit to MENU_REDRAW (it is a
// child of the main menu). A remote start must survive that normalization —
// otherwise a joiner parked in GAME SETTINGS when the host presses GO drops
// back to the main menu instead of launching. This used to be pinned through
// the nested CONTROLS subscreen; with that screen deleted the wrapper itself
// is the subject.
TEST(MenuEngine, remote_start_propagates_out_of_main_options)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    lobby.networked = true;
    og::ui::install_active_picker_lobby_client(&lobby);
    pks().selected_menu_item = nullptr;
    clear_events();
    g_start_game_requested = true;

    const Sint32 result = main_options();

    EXPECT_TRUE(result & MENU_EXIT)
        << "the remote exit must not become main_options()'s local redraw";
    ASSERT_NE(nullptr, pks().selected_menu_item);
    EXPECT_EQ(og::ui::PickerMenuCommand::ContinueGame,
              pks().selected_menu_item->command);
    g_start_game_requested = false;
}

TEST(MenuEngine, blocking_control_remap_polls_and_aborts_for_remote_start)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    lobby.networked = true;
    lobby.start_on_next_poll = true;
    og::ui::install_active_picker_lobby_client(&lobby);
    g_start_game_requested = false;

    EXPECT_EQ(MENU_REDRAW, edit_player_keymap(0));
    EXPECT_TRUE(g_start_game_requested);
    EXPECT_EQ(1, lobby.poll_count)
        << "the first prompt should poll once and abort the remaining remap "
           "sequence as soon as the host start is launchable";
}

TEST(MenuEngine, fade_around_entry_composes_content_before_fade_in)
{
    static constexpr og::ui::MenuButtonSpec kRows[] = {
        {.id = "engine_back", .label = "BACK", .hotkey = KEYSTATE_ESCAPE,
         .x = 10, .y = 10, .w = 50, .h = 15,
         .action = ButtonAction::ReturnMenu, .arg = MENU_EXIT, .nav = {}},
    };
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::MenuScreenSpec spec = make_synth_spec(kRows, 1, "fade_content");
    spec.enter = og::ui::EnterTransition::FadeAroundEntry;
    spec.draw_content = &synth_draw_content;
    spec.remote_start = og::ui::RemoteStartScope::TeamBuildScope;
    g_synth_spec = &spec;
    g_synth_content_draws = 0;
    g_start_game_requested = true;

    EXPECT_EQ(MENU_EXIT, og::ui::run_menu_screen(spec));
    EXPECT_EQ(1, g_synth_content_draws)
        << "the cold fade frame must include content before the loop's first "
           "full draw";
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

    // The screen is a Base Camp child now: a joiner parked here must follow
    // a host GO, like every sibling (Teams/Hire/Train).
    const og::ui::MenuScreenSpec& spec = og::ui::difficulty_menu_screen_spec();
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, spec.remote_start);
    EXPECT_EQ(og::ui::RemoteStartExit::ReturnMenuExit, spec.remote_start_exit);
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

// ---------------------------------------------------------------------------
// G13 gate-lattice sweep over EVERY engine screen in the registry (grows
// automatically with each migration): materialize through the D3 accessor
// pair, then for each host-visibility variant mirror the runner's gate/nav
// passes and assert the structural invariants — id uniqueness,
// keyboard-liveness (myfun != 0), static label budget (6px/char, centered,
// unclipped), canvas bounds, no overlap among visible rows, nav closure
// (no visible row links out of range or at a hidden row), and BFS
// reachability of every visible row from the ensured highlight.
namespace
{

bool sweep_rows_overlap(const button& a, const button& b)
{
    return a.x < b.x + b.sizex && b.x < a.x + a.sizex &&
           a.y < b.y + b.sizey && b.y < a.y + a.sizey;
}

// Declared exception to the keyboard-liveness invariant (G13): the PROGRESS
// screen's PREV/NEXT shipped KEYBOARD-DEAD (myfun = 0, raw mouse-rect
// dispatch) and Layer E must reproduce that exactly — making them
// keyboard-live is a visible change deferred past 10a (§1.8 step 6). Every
// other engine row must stay live.
bool sweep_keyboard_dead_is_legacy_faithful(const std::string& screen,
                                            const std::string& id)
{
    return screen == "progress" && (id == "prev" || id == "next");
}

} // namespace

TEST(MenuEngine, engine_screen_gate_lattice_sweep)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);
    // Null out any live vbuttons a prior test left behind: the legacy Rewire
    // hooks write hidden state through allbuttons_ when entries are non-null.
    clear_allbuttons();

    // Give Base Camp a real roster row so its dynamic graph exercises both
    // the roster and the appended seat-assignment rail.
    SaveData& sweep_save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> sweep_saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        sweep_saved_team[static_cast<std::size_t>(i)] = std::move(sweep_save.team_list[static_cast<std::size_t>(i)]);
    const unsigned char sweep_old_team_size = sweep_save.team_size;
    sweep_save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    sweep_save.team_list[0]->name = "SWEEP";
    sweep_save.team_size = 1;

    // G13 lattice axes: {host} x {networked}. host=false without a network
    // session is the degenerate legacy shape; production non-hosts are
    // always networked — that variant drives the Base Camp READY twin and
    // the DIFFICULTY cross-control row.
    struct SweepVariant {
        bool host;
        bool networked;
        const char* name;
    };
    constexpr SweepVariant kVariants[] = {
        {true, false, "host-local"},
        {false, false, "nonhost-degenerate"},
        {true, true, "host-networked"},
        {false, true, "joiner-networked"},
    };

    // §1.2 G13 / design §2.6: two rows may share geometry ONLY with
    // mutually exclusive gates. Any statically-overlapping pair must be
    // declared here, must never be simultaneously visible in any variant
    // (the per-variant overlap check enforces that), and BOTH halves must
    // be visible in at least one variant each (the allowance is exercised).
    // §9.2: the no_company_note fills the exact CONTINUE|LOAD envelope and
    // shows (Disabled) exactly when the pair hides — the company-presence
    // axis below exercises both halves.
    const std::set<std::pair<std::string, std::string>> kSameGeometryAllowed =
        {{"go", "ready"},
         {"continue_game", "no_company_note"},
         {"load_company", "no_company_note"}};

    int engine_screens = 0;
    for (int s = 0; s < static_cast<int>(og::ui::MenuScreenId::Count); ++s) {
        const og::ui::MenuScreenHost& host =
            og::ui::menu_screen_host(static_cast<og::ui::MenuScreenId>(s));
        if (host.kind != og::ui::MenuScreenHost::Kind::Engine)
            continue;
        ++engine_screens;
        const og::ui::MenuScreenSpec& spec = *host.spec;

        // Statically-overlapping row pairs (from the materialized spec
        // geometry, before any rewire mutates it) — the same-geometry set
        // this screen must justify against the allowlist.
        std::set<std::pair<std::string, std::string>> geometry_pairs;
        {
            button* fresh = spec.buttons_accessor();
            const int fresh_count = spec.count_accessor();
            for (int i = 0; i < fresh_count; ++i) {
                for (int j = i + 1; j < fresh_count; ++j) {
                    const button& a = fresh[i];
                    const button& b = fresh[j];
                    const bool overlap = a.x < b.x + b.sizex &&
                        b.x < a.x + a.sizex && a.y < b.y + b.sizey &&
                        b.y < a.y + a.sizey;
                    if (!overlap)
                        continue;
                    auto pair = a.id < b.id
                        ? std::make_pair(a.id, b.id)
                        : std::make_pair(b.id, a.id);
                    EXPECT_TRUE(kSameGeometryAllowed.contains(pair))
                        << spec.name << ": rows '" << pair.first << "' and '"
                        << pair.second
                        << "' share geometry without a declared "
                           "mutually-exclusive-gate allowance";
                    geometry_pairs.insert(std::move(pair));
                }
            }
        }
        std::map<std::string, bool> seen_visible;

        for (const SweepVariant& sweep_variant : kVariants) {
            lobby.host = sweep_variant.host;
            lobby.networked = sweep_variant.networked;
            // §9.2 company-presence axis, ridden on the host flag (only the
            // main-menu gates read it): host variants sweep the with-company
            // shape (CONTINUE|LOAD visible, note Hidden), non-host variants
            // the no-company shape (pair Hidden, note Disabled-visible) —
            // both halves of the declared same-geometry pairs get exercised.
            og::ui::set_main_menu_company_view_for_tests(
                sweep_variant.host, sweep_variant.host ? "SWEEP BAND" : "");
            // Fresh materialization per variant (buttons() fills what
            // count() reads — the D3 sequencing contract).
            button* buttons = spec.buttons_accessor();
            const int count = spec.count_accessor();
            ASSERT_GT(count, 0) << spec.name;
            ASSERT_LE(count, MAX_BUTTONS) << spec.name;
            // Build-gated rows (the main-menu QUIT-state fork) drop at
            // materialization; index spec rows through the same mapping the
            // runner uses so buttons[i] pairs with its materialized spec row.
            const std::vector<const og::ui::MenuButtonSpec*> spec_rows =
                og::ui::materialized_spec_rows(spec);
            ASSERT_EQ(static_cast<int>(spec_rows.size()), count) << spec.name;

            std::set<std::string> ids;
            for (int i = 0; i < count; ++i) {
                const button& b = buttons[i];
                EXPECT_TRUE(ids.insert(b.id).second)
                    << spec.name << " duplicate id " << b.id;
                EXPECT_FALSE(b.id.empty()) << spec.name << " row " << i;
                if (!sweep_keyboard_dead_is_legacy_faithful(spec.name, b.id)) {
                    EXPECT_NE(0, b.myfun)
                        << spec.name << " keyboard-dead row " << b.id;
                }
                EXPECT_LE(static_cast<int>(b.label.size()) * 6, b.sizex)
                    << spec.name << " " << b.id << " label '" << b.label
                    << "' escapes its face";
                EXPECT_GE(b.x, 0) << spec.name << " " << b.id;
                EXPECT_GE(b.y, 0) << spec.name << " " << b.id;
                EXPECT_LE(b.x + b.sizex, 320) << spec.name << " " << b.id;
                EXPECT_LE(b.y + b.sizey, 200) << spec.name << " " << b.id;
            }

            // Mirror the runner's gate pass (descriptor surface only: the
            // live vbuttons are null here and every writer null-guards).
            og::ui::MenuLabelContext context;
            context.save = &og::runtime::current_session->myscreen_->save_data;
            context.session_difficulty =
                og::runtime::current_session->current_difficulty_;
            context.is_host = sweep_variant.host;
            context.is_networked = sweep_variant.networked;
            std::vector<og::ui::RowState> row_states(
                static_cast<std::size_t>(count), og::ui::RowState::Visible);
            for (int i = 0; i < count; ++i) {
                const og::ui::MenuButtonSpec& row =
                    *spec_rows[static_cast<std::size_t>(i)];
                const og::ui::RowState state = row.state_override != nullptr
                    ? row.state_override(context)
                    : og::ui::gate_state(row.gate, context);
                row_states[static_cast<std::size_t>(i)] = state;
                buttons[i].hidden = (state == og::ui::RowState::Hidden);
            }
            int highlighted = spec.default_highlight;
            if (spec.nav.kind == og::ui::NavProgramKind::Rewire &&
                spec.nav.rewire != nullptr) {
                spec.nav.rewire(buttons, count, highlighted);
            }
            ensure_highlighted_button_visible(buttons, count, highlighted);

            // No overlap among simultaneously-visible rows.
            for (int i = 0; i < count; ++i) {
                if (buttons[i].hidden)
                    continue;
                for (int j = i + 1; j < count; ++j) {
                    if (buttons[j].hidden)
                        continue;
                    EXPECT_FALSE(sweep_rows_overlap(buttons[i], buttons[j]))
                        << spec.name << " " << sweep_variant.name << ": "
                        << buttons[i].id << " overlaps " << buttons[j].id;
                }
            }

            // Nav closure + BFS reachability over the visible subgraph.
            std::vector<bool> reached(static_cast<std::size_t>(count), false);
            std::vector<int> frontier;
            ASSERT_GE(highlighted, 0) << spec.name;
            ASSERT_LT(highlighted, count) << spec.name;
            EXPECT_FALSE(buttons[highlighted].hidden)
                << spec.name << " " << sweep_variant.name
                << ": highlight stuck on a hidden row";
            reached[static_cast<std::size_t>(highlighted)] = true;
            frontier.push_back(highlighted);
            while (!frontier.empty()) {
                const int at = frontier.back();
                frontier.pop_back();
                const int links[4] = {
                    buttons[at].nav.up, buttons[at].nav.down,
                    buttons[at].nav.left, buttons[at].nav.right};
                for (const int link : links) {
                    EXPECT_LT(link, count)
                        << spec.name << " " << sweep_variant.name << ": "
                        << buttons[at].id << " nav link out of range";
                    if (link < 0 || link >= count)
                        continue;
                    EXPECT_FALSE(buttons[link].hidden)
                        << spec.name << " " << sweep_variant.name << ": "
                        << buttons[at].id << " nav-links to hidden "
                        << buttons[link].id;
                    if (!buttons[link].hidden &&
                        !reached[static_cast<std::size_t>(link)]) {
                        reached[static_cast<std::size_t>(link)] = true;
                        frontier.push_back(link);
                    }
                }
            }
            for (int i = 0; i < count; ++i) {
                if (!buttons[i].hidden) {
                    // §9.2 Disabled exemption: a Disabled row is inert
                    // chrome (keyboard-dead by the engine's gate pass), so
                    // keyboard reachability is not required of it — the
                    // no_company_note is the declared consumer.
                    if (row_states[static_cast<std::size_t>(i)]
                        == og::ui::RowState::Disabled)
                        continue;
                    EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                        << spec.name << " " << sweep_variant.name << ": "
                        << buttons[i].id << " unreachable by keyboard";
                }
            }

            // Same-geometry bookkeeping: which halves of the declared
            // pairs actually showed in this variant.
            for (int i = 0; i < count; ++i) {
                if (!buttons[i].hidden)
                    seen_visible[buttons[i].id] = true;
            }
        }

        // The allowance case must be EXERCISED: for every declared
        // same-geometry pair present on this screen, each half was visible
        // in at least one lattice variant (the per-variant overlap check
        // above already proved they were never visible TOGETHER).
        for (const auto& [first_id, second_id] : geometry_pairs) {
            EXPECT_TRUE(seen_visible[first_id])
                << spec.name << ": same-geometry row '" << first_id
                << "' never became visible across the lattice";
            EXPECT_TRUE(seen_visible[second_id])
                << spec.name << ": same-geometry row '" << second_id
                << "' never became visible across the lattice";
        }
    }
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        sweep_save.team_list[static_cast<std::size_t>(i)] = std::move(sweep_saved_team[static_cast<std::size_t>(i)]);
    sweep_save.team_size = sweep_old_team_size;
    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
    EXPECT_GE(engine_screens, 16)
        << "difficulty + the FX trio + display + seat settings + main "
           "options + main menu + the team-build cluster (base camp, "
           "SCENARIO) + hire + train + progress + view level + help + the "
           "zone submenu must be engine-hosted (VIEW TEAM, MATCHUP and the "
           "slot menus RETIRED)";
}

// The G13 sweep above materializes Base Camp with NO zone state installed,
// so all four variants see the built-in DEFAULT composition: one
// full-height roster and sixteen parked action ordinals. A SCRIPTED
// composition re-bands the entire panel — the roster shrinks and moves down,
// the appended rows un-park into the freed units, each actions widget grows
// its own pager pair, and the whole nav spine is rewired around them. That
// shape had no lattice coverage at all. Run the same {host} x {networked}
// axes with the same overlap and BFS checks over a synthetic FOUR-widget
// zone (one of every kind — the per-kind caps allow no richer composition).
TEST(MenuEngine, base_camp_scripted_zone_gate_lattice_sweep)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);
    clear_allbuttons();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        saved_team[static_cast<std::size_t>(i)] =
            std::move(save.team_list[static_cast<std::size_t>(i)]);
    }
    const unsigned char old_team_size = save.team_size;
    for (int i = 0; i < 4; ++i) {
        save.team_list[static_cast<std::size_t>(i)] =
            std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[static_cast<std::size_t>(i)]->name =
            std::format("ZONE{}", i);
    }
    save.team_size = 4;

    // readout (hoists into the panel header band, 0 grid units) + text
    // (1 line -> 1 unit) + actions (weight 2 holding 5 entries, so the
    // widget pages in place) + roster (1 header unit + 4 rows) = 8 units.
    // Locks and an assign spec ride along so the padlock/oath cells are in
    // the swept geometry too.
    og::script::hooks::CampaignZone raw;
    {
        og::script::hooks::CampaignZoneWidget readout;
        readout.kind = og::script::hooks::CampaignZoneWidget::Kind::Readout;
        readout.items.push_back({"WAGES", "1400g"});
        readout.items.push_back({"DEBT", "900g"});
        raw.widgets.push_back(std::move(readout));

        og::script::hooks::CampaignZoneWidget text;
        text.kind = og::script::hooks::CampaignZoneWidget::Kind::Text;
        text.lines = {"Collectors at the Toll."};
        raw.widgets.push_back(std::move(text));

        og::script::hooks::CampaignZoneWidget actions;
        actions.kind = og::script::hooks::CampaignZoneWidget::Kind::Actions;
        actions.weight = 2;
        for (int i = 0; i < 5; ++i) {
            og::script::hooks::CampaignPageEntry entry;
            entry.id = std::format("sweep{}", i);
            entry.label = std::format("SWEEP ACT {}", i);
            entry.kind = og::script::hooks::CampaignPageEntry::Kind::Action;
            actions.entries.push_back(std::move(entry));
        }
        raw.widgets.push_back(std::move(actions));

        og::script::hooks::CampaignZoneWidget roster;
        roster.kind = og::script::hooks::CampaignZoneWidget::Kind::Roster;
        roster.locks.push_back({1, false, "ON THE WAR ROAD"});
        roster.assign.active = true;
        roster.assign.key = "road";
        roster.assign.labels = {"WAR", "BURDEN"};
        raw.widgets.push_back(std::move(roster));
    }
    og::ui::CampaignZoneSession zone(save);
    ASSERT_TRUE(zone.adopt(raw)) << "the synthetic composition must lay out";
    ASSERT_EQ(1u, zone.actions().size());
    ASSERT_EQ(1u, zone.texts().size());
    ASSERT_NE(nullptr, zone.readout());
    ASSERT_EQ(4, zone.roster().rows_per_page);

    og::ui::BaseCampScreenState state;
    state.zone = &zone;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);

    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec.nav.rewire);

    struct ZoneVariant {
        bool host;
        bool networked;
        const char* name;
    };
    constexpr ZoneVariant kZoneVariants[] = {
        {true, false, "host-local"},
        {false, false, "nonhost-degenerate"},
        {true, true, "host-networked"},
        {false, true, "joiner-networked"},
    };

    for (const ZoneVariant& variant : kZoneVariants) {
        lobby.host = variant.host;
        lobby.networked = variant.networked;

        button* buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        ASSERT_GT(count, 0) << variant.name;
        ASSERT_LE(count, MAX_BUTTONS) << variant.name;
        const std::vector<const og::ui::MenuButtonSpec*> spec_rows =
            og::ui::materialized_spec_rows(spec);
        ASSERT_EQ(static_cast<int>(spec_rows.size()), count) << variant.name;

        og::ui::MenuLabelContext context;
        context.save = &save;
        context.session_difficulty =
            og::runtime::current_session->current_difficulty_;
        context.is_host = variant.host;
        context.is_networked = variant.networked;
        std::vector<og::ui::RowState> row_states(
            static_cast<std::size_t>(count), og::ui::RowState::Visible);
        for (int i = 0; i < count; ++i) {
            const og::ui::MenuButtonSpec& row =
                *spec_rows[static_cast<std::size_t>(i)];
            const og::ui::RowState row_state = row.state_override != nullptr
                ? row.state_override(context)
                : og::ui::gate_state(row.gate, context);
            row_states[static_cast<std::size_t>(i)] = row_state;
            buttons[i].hidden = (row_state == og::ui::RowState::Hidden);
        }
        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        ensure_highlighted_button_visible(buttons, count, highlighted);

        // Teeth: the scripted composition is what got swept. The appended
        // action band and its pagers must be LIVE in every variant — a
        // regression that parked them would otherwise sail through the
        // checks below by sweeping the default zone again.
        EXPECT_FALSE(buttons[kBaseCampZoneActionBase].hidden)
            << variant.name << ": the scripted action band never appeared";
        EXPECT_FALSE(buttons[kBaseCampZoneActionBase + 1].hidden)
            << variant.name << ": the 2-unit band shows both window rows";
        EXPECT_FALSE(buttons[kBaseCampZonePagerBase].hidden)
            << variant.name << ": 5 entries over 2 rows must page in place";

        // No overlap among simultaneously-visible rows.
        for (int i = 0; i < count; ++i) {
            if (buttons[i].hidden)
                continue;
            for (int j = i + 1; j < count; ++j) {
                if (buttons[j].hidden)
                    continue;
                EXPECT_FALSE(sweep_rows_overlap(buttons[i], buttons[j]))
                    << "scripted zone " << variant.name << ": "
                    << buttons[i].id << " overlaps " << buttons[j].id;
            }
            EXPECT_GE(buttons[i].x, 0) << variant.name << " " << buttons[i].id;
            EXPECT_GE(buttons[i].y, 0) << variant.name << " " << buttons[i].id;
            EXPECT_LE(buttons[i].x + buttons[i].sizex, 320)
                << variant.name << " " << buttons[i].id;
            EXPECT_LE(buttons[i].y + buttons[i].sizey, 200)
                << variant.name << " " << buttons[i].id;
        }

        // Nav closure + BFS reachability over the visible subgraph.
        std::vector<bool> reached(static_cast<std::size_t>(count), false);
        std::vector<int> frontier;
        ASSERT_GE(highlighted, 0) << variant.name;
        ASSERT_LT(highlighted, count) << variant.name;
        EXPECT_FALSE(buttons[highlighted].hidden)
            << "scripted zone " << variant.name
            << ": highlight stuck on a hidden row";
        reached[static_cast<std::size_t>(highlighted)] = true;
        frontier.push_back(highlighted);
        while (!frontier.empty()) {
            const int at = frontier.back();
            frontier.pop_back();
            const int links[4] = {buttons[at].nav.up, buttons[at].nav.down,
                                  buttons[at].nav.left,
                                  buttons[at].nav.right};
            for (const int link : links) {
                EXPECT_LT(link, count)
                    << "scripted zone " << variant.name << ": "
                    << buttons[at].id << " nav link out of range";
                if (link < 0 || link >= count)
                    continue;
                EXPECT_FALSE(buttons[link].hidden)
                    << "scripted zone " << variant.name << ": "
                    << buttons[at].id << " nav-links to hidden "
                    << buttons[link].id;
                if (!buttons[link].hidden &&
                    !reached[static_cast<std::size_t>(link)]) {
                    reached[static_cast<std::size_t>(link)] = true;
                    frontier.push_back(link);
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            if (buttons[i].hidden)
                continue;
            if (row_states[static_cast<std::size_t>(i)] ==
                og::ui::RowState::Disabled)
                continue;
            EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                << "scripted zone " << variant.name << ": " << buttons[i].id
                << " unreachable by keyboard";
        }
    }

    og::ui::install_base_camp_state_for_screen(nullptr);
    for (int i = 0; i < MAX_TEAM_SIZE; ++i) {
        save.team_list[static_cast<std::size_t>(i)] =
            std::move(saved_team[static_cast<std::size_t>(i)]);
    }
    save.team_size = old_team_size;
    (void)picker_createmenu_buttons();
}

// §2.2 new-company name entry: a Layer-F engine screen entered directly from
// the BEGIN NEW GAME flow, NOT a registry (legacy-vs-engine) screen, so the
// gate-lattice sweep above cannot reach it. Pin its materialized shape, the
// generic MenuSpecRow dispatch (every row keyboard-live), the ACCEPT default
// highlight, no overlap, and nav closure + BFS reachability.
TEST(MenuEngine, name_entry_spec_shape_and_nav)
{
    EngineTestGuard guard;
    clear_allbuttons();
    const og::ui::MenuScreenSpec& spec = og::ui::name_entry_menu_screen_spec();
    EXPECT_TRUE(spec.polls_lobby)
        << "name entry must not hide a remote host launch";

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_EQ(4, count);
    ASSERT_LE(count, MAX_BUTTONS);
    const std::vector<const og::ui::MenuButtonSpec*> spec_rows =
        og::ui::materialized_spec_rows(spec);
    ASSERT_EQ(static_cast<int>(spec_rows.size()), count);

    const Sint32 spec_row_action = button_action_id(ButtonAction::MenuSpecRow);
    std::set<std::string> ids;
    for (int i = 0; i < count; ++i) {
        const button& b = buttons[i];
        EXPECT_TRUE(ids.insert(b.id).second) << "duplicate id " << b.id;
        EXPECT_FALSE(b.id.empty()) << "row " << i;
        // Every row routes through the single generic action (G3), so all are
        // keyboard-live and never hidden on this ungated screen.
        EXPECT_EQ(spec_row_action, b.myfun) << b.id << " not MenuSpecRow";
        EXPECT_FALSE(b.hidden) << b.id;
        EXPECT_LE(static_cast<int>(b.label.size()) * 6, b.sizex)
            << b.id << " label '" << b.label << "' escapes its face";
        EXPECT_GE(b.x, 0) << b.id;
        EXPECT_GE(b.y, 0) << b.id;
        EXPECT_LE(b.x + b.sizex, 320) << b.id;
        EXPECT_LE(b.y + b.sizey, 200) << b.id;
    }

    int highlighted = spec.default_highlight;
    ensure_highlighted_button_visible(buttons, count, highlighted);
    ASSERT_GE(highlighted, 0);
    ASSERT_LT(highlighted, count);
    EXPECT_EQ("company_name_accept", buttons[highlighted].id)
        << "initial highlight should be ACCEPT (§2.2)";

    // §9.20: the name box uses the stock grey button face, matching the
    // character naming/renaming modal. No row carries a color override.
    for (int i = 0; i < count; ++i)
        EXPECT_EQ(nullptr, spec_rows[static_cast<std::size_t>(i)]->color)
            << spec_rows[static_cast<std::size_t>(i)]->id;

    // The field frames the action pair below instead of looking pinched
    // between their outer edges.
    EXPECT_EQ(buttons[2].x, buttons[1].x);
    EXPECT_EQ(buttons[3].x + buttons[3].sizex,
              buttons[1].x + buttons[1].sizex);

    for (int i = 0; i < count; ++i)
        for (int j = i + 1; j < count; ++j)
            EXPECT_FALSE(sweep_rows_overlap(buttons[i], buttons[j]))
                << buttons[i].id << " overlaps " << buttons[j].id;

    // Nav closure + BFS reachability from the ensured highlight.
    std::vector<bool> reached(static_cast<std::size_t>(count), false);
    std::vector<int> frontier{highlighted};
    reached[static_cast<std::size_t>(highlighted)] = true;
    while (!frontier.empty()) {
        const int at = frontier.back();
        frontier.pop_back();
        const int links[4] = {buttons[at].nav.up, buttons[at].nav.down,
                              buttons[at].nav.left, buttons[at].nav.right};
        for (const int link : links) {
            EXPECT_LT(link, count) << buttons[at].id << " nav link out of range";
            if (link < 0 || link >= count)
                continue;
            EXPECT_FALSE(buttons[link].hidden)
                << buttons[at].id << " nav-links to hidden " << buttons[link].id;
            if (!reached[static_cast<std::size_t>(link)]) {
                reached[static_cast<std::size_t>(link)] = true;
                frontier.push_back(link);
            }
        }
    }
    for (int i = 0; i < count; ++i)
        EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
            << buttons[i].id << " unreachable by keyboard";
}

// §2.3 Company List: like name entry, entered from the main-menu LOAD door
// rather than the registry, so the gate-lattice sweep cannot reach it. The
// per-frame rewire owns ALL visibility (page window, pagers) and nav; pin the
// spec obligations plus BFS reachability / nav closure / no-overlap over the
// §2.3 visibility variants: null state (bare sweep shape), empty list, one
// partial page, two pages (both), and corrupt rows (BK/X stay available).
namespace {

og::data::CompanyInfo make_company_info(const std::string& slot,
                                        std::int64_t ts, bool valid)
{
    og::data::CompanyInfo info;
    info.slot = slot;
    info.display_name = "COMPANY " + slot;
    info.last_played_unix_s = ts;
    info.roster_size = 3;
    info.valid = valid;
    return info;
}

// RAII: the file-static state pointer the rewire reads must never leak into
// later tests (shuffle safety).
struct ScopedCompanyListState
{
    explicit ScopedCompanyListState(og::ui::CompanyListScreenState* state)
    {
        og::ui::install_company_list_state_for_screen(state);
    }
    ~ScopedCompanyListState()
    {
        og::ui::install_company_list_state_for_screen(nullptr);
    }
};

} // namespace

TEST(MenuEngine, company_list_spec_shape_and_nav_variants)
{
    EngineTestGuard guard;
    clear_allbuttons();
    const og::ui::MenuScreenSpec& spec = og::ui::company_list_menu_screen_spec();

    // Spec obligations (§2.3): main-scope remote start (a host GO launches a
    // peer parked in the list), lobby poll, fade entry, the old load loop's
    // BACK-first highlight, generic MenuSpecRow dispatch.
    EXPECT_EQ(std::string("company_list"), spec.name);
    EXPECT_EQ(og::ui::RemoteStartScope::MainScope, spec.remote_start);
    EXPECT_EQ(og::ui::RemoteStartExit::ReturnMenuExit, spec.remote_start_exit);
    EXPECT_EQ(og::ui::EnterTransition::FadeAroundEntry, spec.enter);
    EXPECT_TRUE(spec.polls_lobby);
    EXPECT_EQ(24, spec.default_highlight);
    EXPECT_TRUE(spec.on_spec_row != nullptr);
    EXPECT_TRUE(spec.nav.kind == og::ui::NavProgramKind::Rewire
                && spec.nav.rewire != nullptr);
    EXPECT_EQ(MENU_REDRAW, spec.exit_value);

    struct Variant {
        const char* label;
        int companies;
        int corrupt_from;  // companies at index >= this are corrupt (-1: none)
        int page;
        int want_visible_rows;
        bool want_pagers;
    };
    const Variant variants[] = {
        {"empty", 0, -1, 0, 0, false},
        {"partial_page", 3, -1, 0, 3, false},
        {"two_pages_first", 15, -1, 0, 8, true},
        {"two_pages_second", 15, -1, 1, 7, true},
        {"corrupt_rows_second_page", 15, 12, 1, 7, true},
    };

    for (const Variant& variant : variants) {
        og::ui::CompanyListScreenState state;
        for (int i = 0; i < variant.companies; ++i) {
            state.companies.push_back(make_company_info(
                "wp3var" + std::to_string(i), 1000 - i,
                variant.corrupt_from < 0 || i < variant.corrupt_from));
        }
        state.page = og::ui::PageModel::make(variant.companies, 8);
        state.page.page = variant.page;
        ScopedCompanyListState installed(&state);

        button* buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        ASSERT_EQ(27, count) << variant.label;

        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        ensure_highlighted_button_visible(buttons, count, highlighted);

        // Visibility: the page window's row triples, BACK always, pagers
        // only when the list spans pages (corrupt rows keep BK/X — the
        // restore/delete doors stay available, §2.3).
        for (int r = 0; r < 8; ++r) {
            const bool want = r < variant.want_visible_rows;
            EXPECT_EQ(!want, buttons[r].hidden)
                << variant.label << " company_row_" << r;
            EXPECT_EQ(!want, buttons[8 + r].hidden)
                << variant.label << " company_bak_" << r;
            EXPECT_EQ(!want, buttons[16 + r].hidden)
                << variant.label << " company_del_" << r;
        }
        EXPECT_FALSE(buttons[24].hidden) << variant.label;
        EXPECT_EQ(!variant.want_pagers, buttons[25].hidden) << variant.label;
        EXPECT_EQ(!variant.want_pagers, buttons[26].hidden) << variant.label;

        // No overlap among the visible rows.
        for (int i = 0; i < count; ++i) {
            if (buttons[i].hidden)
                continue;
            for (int j = i + 1; j < count; ++j) {
                if (buttons[j].hidden)
                    continue;
                EXPECT_FALSE(sweep_rows_overlap(buttons[i], buttons[j]))
                    << variant.label << ": " << buttons[i].id << " overlaps "
                    << buttons[j].id;
            }
        }

        // Nav closure + BFS reachability over the visible subgraph.
        ASSERT_GE(highlighted, 0) << variant.label;
        ASSERT_LT(highlighted, count) << variant.label;
        EXPECT_FALSE(buttons[highlighted].hidden)
            << variant.label << ": highlight stuck on a hidden row";
        std::vector<bool> reached(static_cast<std::size_t>(count), false);
        std::vector<int> frontier{highlighted};
        reached[static_cast<std::size_t>(highlighted)] = true;
        while (!frontier.empty()) {
            const int at = frontier.back();
            frontier.pop_back();
            const int links[4] = {buttons[at].nav.up, buttons[at].nav.down,
                                  buttons[at].nav.left, buttons[at].nav.right};
            for (const int link : links) {
                EXPECT_LT(link, count)
                    << variant.label << ": " << buttons[at].id
                    << " nav link out of range";
                if (link < 0 || link >= count)
                    continue;
                EXPECT_FALSE(buttons[link].hidden)
                    << variant.label << ": " << buttons[at].id
                    << " nav-links to hidden " << buttons[link].id;
                if (!buttons[link].hidden
                    && !reached[static_cast<std::size_t>(link)]) {
                    reached[static_cast<std::size_t>(link)] = true;
                    frontier.push_back(link);
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            if (!buttons[i].hidden) {
                EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                    << variant.label << ": " << buttons[i].id
                    << " unreachable by keyboard";
            }
        }
    }

    // Null state (no install): the bare-sweep shape — every row and pager
    // hidden, BACK alone, its side links written as explicit no-ops.
    {
        button* buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        ensure_highlighted_button_visible(buttons, count, highlighted);
        for (int i = 0; i < count; ++i) {
            if (buttons[i].id == "back") {
                EXPECT_FALSE(buttons[i].hidden);
                EXPECT_EQ(-1, buttons[i].nav.up);
                EXPECT_EQ(-1, buttons[i].nav.right);
            } else {
                EXPECT_TRUE(buttons[i].hidden) << buttons[i].id;
            }
        }
        EXPECT_EQ("back", buttons[highlighted].id)
            << "empty shape highlight must settle on BACK";
    }

    // Draw smoke over the headless screen buffer: the restored classic panel
    // plus its eight visual EMPTY SLOT rows (the corresponding engine rows stay
    // hidden/inert), then a populated two-page state with a corrupt row + the
    // "p/N" indicator. The flows pin behavior; this pins both draw passes.
    ASSERT_TRUE(spec.draw_background != nullptr);
    ASSERT_TRUE(spec.draw_content != nullptr);
    spec.draw_background(nullptr);
    spec.draw_content(nullptr);
    {
        og::ui::CompanyListScreenState state;
        state.page = og::ui::PageModel::make(0, 8);
        spec.draw_background(&state);
        spec.draw_content(&state);
        // The last empty row still has all three classic button faces. Sample
        // their top bevels independently so a future visibility cleanup cannot
        // collapse the old menu back into a short list over a blank panel.
        int pixel = -1;
        og::runtime::current_session->myscreen_->get_pixel(50, 154, &pixel);
        EXPECT_EQ(BUTTON_TOP, pixel) << "empty company face";
        og::runtime::current_session->myscreen_->get_pixel(218, 154, &pixel);
        EXPECT_EQ(BUTTON_TOP, pixel) << "empty BK face";
        og::runtime::current_session->myscreen_->get_pixel(246, 154, &pixel);
        EXPECT_EQ(BUTTON_TOP, pixel) << "empty X face";
        for (int i = 0; i < 15; ++i) {
            state.companies.push_back(make_company_info(
                "wp3draw" + std::to_string(i), 1000 - i, i < 12));
        }
        state.page = og::ui::PageModel::make(15, 8);
        state.page.page = 1;
        spec.draw_background(&state);
        spec.draw_content(&state);
    }
}

// §2.3 U4: the ACTIVE company's row wears the red do_outline on the live
// vbutton surface — stamped by the rewire every frame, so a reset or page
// flip re-derives it. Driven against real init_buttons vbuttons.
TEST(MenuEngine, company_list_active_row_outline)
{
    EngineTestGuard guard;
    clear_allbuttons();
    const og::ui::MenuScreenSpec& spec = og::ui::company_list_menu_screen_spec();

    og::ui::CompanyListScreenState state;
    state.companies.push_back(make_company_info("wp3outlineb", 2000, true));
    state.companies.push_back(make_company_info("wp3outlinea", 1000, true));
    state.page = og::ui::PageModel::make(2, 8);
    ScopedCompanyListState installed(&state);
    og::data::ScopedActiveCompany active("wp3outlineb");
    ASSERT_TRUE(active.applied());

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    og::runtime::current_session->localbuttons_ = init_buttons(buttons, count);
    int highlighted = spec.default_highlight;
    spec.nav.rewire(buttons, count, highlighted);

    ASSERT_TRUE(og::runtime::current_session->allbuttons_[0] != nullptr);
    ASSERT_TRUE(og::runtime::current_session->allbuttons_[1] != nullptr);
    EXPECT_EQ(1, og::runtime::current_session->allbuttons_[0]->do_outline)
        << "row 0 (the active company) wears the marker";
    EXPECT_EQ(0, og::runtime::current_session->allbuttons_[1]->do_outline)
        << "row 1 (not active) must not";

    // Repoint the active slot: the next rewire pass moves the marker.
    {
        og::data::ScopedActiveCompany repointed("wp3outlinea");
        ASSERT_TRUE(repointed.applied());
        spec.nav.rewire(buttons, count, highlighted);
        EXPECT_EQ(0, og::runtime::current_session->allbuttons_[0]->do_outline);
        EXPECT_EQ(1, og::runtime::current_session->allbuttons_[1]->do_outline);
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
}

// §2.4 Backups sub-view: entered from the Company List's BK door, not the
// registry. Pin the spec obligations plus BFS reachability / nav closure /
// no-overlap over the visibility variants: null state (bare sweep shape),
// empty list (a REAL shape — a company with no level wins has no
// snapshots), one partial page, two pages (both sides), and corrupt rows
// (they stay listed and clickable; the restore API's step-0 validation is
// the guard).
namespace {

og::data::CompanyBackupInfo make_backup_info(const std::string& slot, int seq,
                                             std::int64_t ts, bool valid)
{
    og::data::CompanyBackupInfo info;
    info.slot = slot;
    info.seq = seq;
    info.filename = std::format("{}.{:03}.gtl", slot, seq);
    info.header.slot = slot;
    info.header.display_name = "COMPANY " + slot;
    info.header.campaign_id = "gladiator";
    info.header.scen_num = static_cast<short>(seq);
    info.header.last_played_unix_s = ts;
    info.header.valid = valid;
    return info;
}

// RAII: the file-static state pointer the rewire reads must never leak into
// later tests (shuffle safety).
struct ScopedCompanyBackupsState
{
    explicit ScopedCompanyBackupsState(og::ui::CompanyBackupsScreenState* state)
    {
        og::ui::install_company_backups_state_for_screen(state);
    }
    ~ScopedCompanyBackupsState()
    {
        og::ui::install_company_backups_state_for_screen(nullptr);
    }
};

} // namespace

TEST(MenuEngine, company_backups_spec_shape_and_nav_variants)
{
    EngineTestGuard guard;
    clear_allbuttons();
    const og::ui::MenuScreenSpec& spec =
        og::ui::company_backups_menu_screen_spec();

    // Spec obligations (§2.4): main-scope remote start (a host GO launches a
    // peer parked in the sub-view), lobby poll, fade entry, row-0 highlight
    // (the newest snapshot), generic MenuSpecRow dispatch.
    EXPECT_EQ(std::string("company_backups"), spec.name);
    EXPECT_EQ(og::ui::RemoteStartScope::MainScope, spec.remote_start);
    EXPECT_EQ(og::ui::RemoteStartExit::ReturnMenuExit, spec.remote_start_exit);
    EXPECT_EQ(og::ui::EnterTransition::FadeAroundEntry, spec.enter);
    EXPECT_TRUE(spec.polls_lobby);
    EXPECT_EQ(0, spec.default_highlight);
    EXPECT_TRUE(spec.on_spec_row != nullptr);
    EXPECT_TRUE(spec.nav.kind == og::ui::NavProgramKind::Rewire
                && spec.nav.rewire != nullptr);
    EXPECT_EQ(MENU_REDRAW, spec.exit_value);

    struct Variant {
        const char* label;
        int backups;
        int corrupt_from;  // backups at index >= this are corrupt (-1: none)
        int page;
        int want_visible_rows;
        bool want_pagers;
    };
    const Variant variants[] = {
        {"empty", 0, -1, 0, 0, false},
        {"partial_page", 3, -1, 0, 3, false},
        {"two_pages_first", 15, -1, 0, 10, true},
        {"two_pages_second", 15, -1, 1, 5, true},
        {"corrupt_rows_first_page", 15, 2, 0, 10, true},
    };

    for (const Variant& variant : variants) {
        og::ui::CompanyBackupsScreenState state;
        state.slot = "wp3bkvar";
        state.company_name = "BACKUP VARIANT BAND";
        for (int i = 0; i < variant.backups; ++i) {
            state.backups.push_back(make_backup_info(
                "wp3bkvar", variant.backups - i, 1000 - i,
                variant.corrupt_from < 0 || i < variant.corrupt_from));
        }
        state.page = og::ui::PageModel::make(variant.backups, 10);
        state.page.page = variant.page;
        ScopedCompanyBackupsState installed(&state);

        button* buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        ASSERT_EQ(13, count) << variant.label;

        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        ensure_highlighted_button_visible(buttons, count, highlighted);

        // Visibility: the page window's rows, BACK always, pagers only when
        // the snapshots span pages.
        for (int r = 0; r < 10; ++r) {
            EXPECT_EQ(!(r < variant.want_visible_rows), buttons[r].hidden)
                << variant.label << " backup_row_" << r;
        }
        EXPECT_FALSE(buttons[10].hidden) << variant.label;
        EXPECT_EQ(!variant.want_pagers, buttons[11].hidden) << variant.label;
        EXPECT_EQ(!variant.want_pagers, buttons[12].hidden) << variant.label;

        // No overlap among the visible rows.
        for (int i = 0; i < count; ++i) {
            if (buttons[i].hidden)
                continue;
            for (int j = i + 1; j < count; ++j) {
                if (buttons[j].hidden)
                    continue;
                EXPECT_FALSE(sweep_rows_overlap(buttons[i], buttons[j]))
                    << variant.label << ": " << buttons[i].id << " overlaps "
                    << buttons[j].id;
            }
        }

        // Nav closure + BFS reachability over the visible subgraph.
        ASSERT_GE(highlighted, 0) << variant.label;
        ASSERT_LT(highlighted, count) << variant.label;
        EXPECT_FALSE(buttons[highlighted].hidden)
            << variant.label << ": highlight stuck on a hidden row";
        std::vector<bool> reached(static_cast<std::size_t>(count), false);
        std::vector<int> frontier{highlighted};
        reached[static_cast<std::size_t>(highlighted)] = true;
        while (!frontier.empty()) {
            const int at = frontier.back();
            frontier.pop_back();
            const int links[4] = {buttons[at].nav.up, buttons[at].nav.down,
                                  buttons[at].nav.left, buttons[at].nav.right};
            for (const int link : links) {
                EXPECT_LT(link, count)
                    << variant.label << ": " << buttons[at].id
                    << " nav link out of range";
                if (link < 0 || link >= count)
                    continue;
                EXPECT_FALSE(buttons[link].hidden)
                    << variant.label << ": " << buttons[at].id
                    << " nav-links to hidden " << buttons[link].id;
                if (!buttons[link].hidden
                    && !reached[static_cast<std::size_t>(link)]) {
                    reached[static_cast<std::size_t>(link)] = true;
                    frontier.push_back(link);
                }
            }
        }
        for (int i = 0; i < count; ++i) {
            if (!buttons[i].hidden) {
                EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                    << variant.label << ": " << buttons[i].id
                    << " unreachable by keyboard";
            }
        }
    }

    // Null state (no install): the bare-sweep shape — every row and pager
    // hidden, BACK alone, its side links written as explicit no-ops.
    {
        button* buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        ensure_highlighted_button_visible(buttons, count, highlighted);
        for (int i = 0; i < count; ++i) {
            if (buttons[i].id == "back") {
                EXPECT_FALSE(buttons[i].hidden);
                EXPECT_EQ(-1, buttons[i].nav.up);
                EXPECT_EQ(-1, buttons[i].nav.right);
            } else {
                EXPECT_TRUE(buttons[i].hidden) << buttons[i].id;
            }
        }
        EXPECT_EQ("back", buttons[highlighted].id)
            << "empty shape highlight must settle on BACK";
    }

    // Draw smoke over the headless screen buffer: the null-state and
    // zero-snapshot shapes (both draw NO BACKUPS YET + the retention title)
    // and a populated two-page state with a corrupt row + the "p/N"
    // indicator. The flows pin behavior; this pins that every content branch
    // draws.
    ASSERT_TRUE(spec.draw_content != nullptr);
    spec.draw_content(nullptr);
    {
        og::ui::CompanyBackupsScreenState state;
        state.slot = "wp3bkdraw";
        state.company_name = "BACKUP DRAW BAND";
        state.page = og::ui::PageModel::make(0, 10);
        spec.draw_content(&state);
        for (int i = 0; i < 15; ++i) {
            state.backups.push_back(make_backup_info(
                "wp3bkdraw", 15 - i, 1000 - i, i < 12));
        }
        state.page = og::ui::PageModel::make(15, 10);
        state.page.page = 1;
        spec.draw_content(&state);
    }
}

// ---------------------------------------------------------------------------
// §1.8 step 3 registry state: the options family migrates in order (FX trio,
// then display + controls, then main options). Updated in the SAME commit as
// each registry flip.
TEST(MenuEngine, options_family_registry_hosts)
{
    using Kind = og::ui::MenuScreenHost::Kind;
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::GameplayFx).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::UiFx).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::GraphicsFx).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::DisplaySettings).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions).kind);
}

// ---------------------------------------------------------------------------
// §1.8 step 5 registry state: the team-build cluster. VIEW TEAM and the
// SAVE/LOAD slot menus RETIRED with WP4's base camp (§2.5/§3.8), MATCHUP
// with #218 — their MenuScreenId rows are gone entirely.
TEST(MenuEngine, team_build_cluster_registry_hosts)
{
    using Kind = og::ui::MenuScreenHost::Kind;
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Scenario).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::ViewScenario).kind);
}

TEST(MenuEngine, base_camp_reload_publishes_authored_teams_after_level_load)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);

    screen& picker_screen = *og::runtime::current_session->myscreen_;
    SaveData& save = picker_screen.save_data;
    const std::string original_campaign = save.current_campaign;
    const short original_level = save.scen_num;
    const std::string original_mount = get_mounted_campaign();

    struct RestorePickerLevel
    {
        screen& picker_screen;
        SaveData& save;
        std::string campaign;
        short level;
        std::string mount;

        ~RestorePickerLevel()
        {
            save.current_campaign = campaign;
            save.scen_num = level;
            if (!mount.empty())
                save.current_campaign = mount;
            (void)og::ui::sync_campaign_mount_to_save(save);
            save.current_campaign = campaign;
            picker_screen.world().id = level;
            (void)picker_screen.load_level();
        }
    } restore{picker_screen, save, original_campaign, original_level,
              original_mount};

    save.current_campaign = "modes";
    save.scen_num = 500;
    ASSERT_TRUE(og::ui::sync_campaign_mount_to_save(save));

    og::ui::BaseCampScreenState state;
    state.last_level_id = original_level;
    const og::ui::MenuScreenSpec* const spec =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, spec);
    ASSERT_NE(nullptr, spec->frame_tick);
    ASSERT_TRUE(spec->frame_tick(&state, 1));

    EXPECT_EQ(1, lobby.settings_syncs);
    EXPECT_EQ(save.scen_num, lobby.synced_save_level);
    EXPECT_EQ(save.scen_num, lobby.synced_world_level)
        << "the map-derived settings echo must run after load_level";
    EXPECT_NE(0u, lobby.synced_ctf_team_mask)
        << "CTF Auto must publish the selected map's authored flag teams";
}

// The exit_on_redraw contract on the migrated cluster: BACK on these
// subscreens carries MENU_REDRAW and must END the screen (the parent loop
// keeps running), while MENU_EXIT-bearing exits still propagate.
TEST(MenuEngine, team_build_cluster_exit_semantics_pins)
{
    // VIEW LEVEL: BACK carries MENU_REDRAW and ENDS the screen (redraw-exit),
    // while a remote start still propagates its MENU_EXIT through
    // remote_start_exit. Its own exit_value is MENU_REDRAW — the parent
    // team-build loop keeps running. (MATCHUP held the redraw-exit half of
    // this contract until #218 retired it.)
    const og::ui::MenuScreenSpec* viewer =
        og::ui::menu_screen_host(og::ui::MenuScreenId::ViewScenario).spec;
    ASSERT_NE(nullptr, viewer);
    EXPECT_TRUE(viewer->exit_on_redraw);
    EXPECT_EQ(MENU_REDRAW, viewer->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, viewer->remote_start);
    EXPECT_EQ(og::ui::RemoteStartExit::ReturnMenuExit,
              viewer->remote_start_exit);
    EXPECT_NE(nullptr, viewer->frame_tick) << "VIEW LEVEL refresh guard";

    // SCENARIO: nested subscreens return MENU_REDRAW for reset_buttons to
    // consume — exit_on_redraw must stay FALSE or every nested return would
    // close the screen; BACK's MENU_EXIT is folded by the wrapper.
    const og::ui::MenuScreenSpec* scenario =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Scenario).spec;
    ASSERT_NE(nullptr, scenario);
    EXPECT_FALSE(scenario->exit_on_redraw);
    EXPECT_EQ(MENU_EXIT, scenario->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              scenario->remote_start);
    EXPECT_NE(nullptr, scenario->frame_tick) << "SCENARIO level-reload guard";
    EXPECT_NE(nullptr, scenario->on_reset)
        << "a nested screen's reset must trigger the reload guard";

    // TEAM BUILD: fade-bracketed entry, nested MENU_REDRAWs consumed by
    // reset_buttons (exit_on_redraw false), reload guard on both hooks.
    const og::ui::MenuScreenSpec* team_build =
        og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    ASSERT_NE(nullptr, team_build);
    EXPECT_EQ(og::ui::EnterTransition::FadeAroundEntry, team_build->enter);
    EXPECT_FALSE(team_build->exit_on_redraw);
    EXPECT_EQ(MENU_EXIT, team_build->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              team_build->remote_start);
    // §9.11 roster-first: the default highlight is row 0's BODY — entering
    // the base camp highlights the first row and Enter trains, the curses
    // roster grammar (the rewire seeds HIRE when the roster is empty and
    // falls to the first visible button on all-foreign pages).
    EXPECT_EQ(kBaseCampRowBodyBase, team_build->default_highlight);
    EXPECT_NE(nullptr, team_build->frame_tick);
    EXPECT_NE(nullptr, team_build->on_reset);
    EXPECT_NE(nullptr, team_build->on_spec_row)
        << "roster rows dispatch through MenuSpecRow (G3)";
}

// ---------------------------------------------------------------------------
// §1.8 step 6 registry state + exit/entry semantics for the content-hook-
// heavy screens. Updated in the SAME commit as each registry flip.
TEST(MenuEngine, content_screen_registry_hosts_and_semantics)
{
    using Kind = og::ui::MenuScreenHost::Kind;
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Hire).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Train).kind);

    // HIRE: every legacy local exit returned MENU_REDRAW (BACK folds to the
    // exit_value); the remote MENU_EXIT propagates (slot-menu shape). The
    // entry-time PREV/NEXT repositioning is the prepare_buttons hook — the
    // accessor output stays the pinned table shape (test_menu_pins).
    const og::ui::MenuScreenSpec* hire =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Hire).spec;
    ASSERT_NE(nullptr, hire);
    EXPECT_EQ(MENU_REDRAW, hire->exit_value);
    EXPECT_FALSE(hire->exit_on_redraw)
        << "hire callbacks return MENU_REDRAW for reset_buttons to consume";
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, hire->remote_start);
    EXPECT_TRUE(hire->right_click_enabled) << "reverse candidate cycling";
    EXPECT_EQ(1, hire->default_highlight);
    ASSERT_NE(nullptr, hire->prepare_buttons);
    {
        std::vector<button> rows;
        og::ui::materialize_menu_buttons(*hire, rows);
        ASSERT_EQ(5, static_cast<int>(rows.size()));
        hire->prepare_buttons(rows.data(), static_cast<int>(rows.size()),
                              nullptr);
        // The computed portrait-flanking positions the legacy loop wrote
        // over the table shape before init_buttons.
        EXPECT_EQ(27, rows[0].x) << "prev";
        EXPECT_EQ(37, rows[0].y) << "prev";
        EXPECT_EQ(135, rows[1].x) << "next";
        EXPECT_EQ(37, rows[1].y) << "next";
    }
    // The solo-hidden team cycler is a per-frame state override
    // (entry-equivalent: numplayers cannot change under the open screen).
    ASSERT_NE(nullptr, hire->rows[2].state_override);
    {
        SaveData save;
        og::ui::MenuLabelContext context;
        context.save = &save;
        save.numplayers = 1;
        EXPECT_EQ(og::ui::RowState::Hidden,
                  hire->rows[2].state_override(context));
#ifndef DISABLE_MULTIPLAYER
        save.numplayers = 2;
        EXPECT_EQ(og::ui::RowState::Visible,
                  hire->rows[2].state_override(context));
#endif
    }

    // TRAIN: exit-bearing paths carry MENU_EXIT (the wrapper folds unless a
    // start was selected); nested DETAILS/RENAME MENU_REDRAWs are consumed
    // by reset_buttons; the +/- pixie faces are art bindings. (The VIEW
    // TEAM door retired with its screen; SELL is the 20th row.)
    const og::ui::MenuScreenSpec* train =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Train).spec;
    ASSERT_NE(nullptr, train);
    EXPECT_EQ(MENU_EXIT, train->exit_value);
    EXPECT_FALSE(train->exit_on_redraw);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, train->remote_start);
    EXPECT_TRUE(train->right_click_enabled);
    EXPECT_EQ(1, train->default_highlight);
    EXPECT_NE(nullptr, train->on_reset) << "the bug-A9 promotion resync";
    ASSERT_EQ(20, train->row_count);
    for (int i = 2; i < 14; ++i) {
        EXPECT_EQ((i % 2 == 0) ? FAMILY_MINUS : FAMILY_PLUS,
                  static_cast<int>(train->rows[i].art_family))
            << "row " << i
            << ": the legacy set_graphic loop must be the art binding";
    }

    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Progress).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::ViewScenario).kind);

    // PROGRESS: PREV/NEXT stay KEYBOARD-DEAD (myfun 0 — the shipped screen;
    // the raw mouse-rect dispatch lives in frame_tick); every local exit
    // returns MENU_REDRAW.
    const og::ui::MenuScreenSpec* progress =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Progress).spec;
    ASSERT_NE(nullptr, progress);
    EXPECT_EQ(MENU_REDRAW, progress->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              progress->remote_start);
    EXPECT_EQ(2, progress->default_highlight);
    EXPECT_NE(nullptr, progress->frame_tick)
        << "raw PREV/NEXT + per-row GO dispatch";
    {
        std::vector<button> rows;
        og::ui::materialize_menu_buttons(*progress, rows);
        ASSERT_EQ(3, static_cast<int>(rows.size()));
        EXPECT_EQ(0, rows[0].myfun) << "prev keyboard-dead (shipped shape)";
        EXPECT_EQ(0, rows[1].myfun) << "next keyboard-dead (shipped shape)";
        EXPECT_NE(0, rows[2].myfun) << "back stays live";
    }

    // VIEW LEVEL: BACK carries MENU_REDRAW (exit_on_redraw); pager
    // visibility is a per-frame state override over the PageModel, and the
    // page-step stash is consumed by consume_click at the legacy point.
    const og::ui::MenuScreenSpec* view_scenario =
        og::ui::menu_screen_host(og::ui::MenuScreenId::ViewScenario).spec;
    ASSERT_NE(nullptr, view_scenario);
    EXPECT_TRUE(view_scenario->exit_on_redraw);
    EXPECT_EQ(MENU_REDRAW, view_scenario->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              view_scenario->remote_start);
    EXPECT_EQ(kViewScenarioBackIndex, view_scenario->default_highlight);
    EXPECT_NE(nullptr, view_scenario->consume_click);
    ASSERT_EQ(3, view_scenario->row_count);
    EXPECT_NE(nullptr,
              view_scenario->rows[kViewScenarioPrevIndex].state_override);
    EXPECT_NE(nullptr,
              view_scenario->rows[kViewScenarioNextIndex].state_override);
    // No open screen (null wrapper state) presents the single-page shape:
    // pagers hidden — the shape the bare G5/gate-lattice sweeps drive.
    {
        og::ui::MenuLabelContext context;
        EXPECT_EQ(og::ui::RowState::Hidden,
                  view_scenario->rows[kViewScenarioPrevIndex].state_override(
                      context));
    }
}

// #168 HELP: full-screen engine screen. Pin the registry hosting, the spec
// obligations (BACK carries MENU_REDRAW under exit_on_redraw, no entry fade,
// wheel frame_tick + MenuSpecRow dispatch), the hotkey set (1/2/3 tabs,
// PageUp/PageDown pagers, Escape on BACK), the grid relations (tab strip on
// one baseline at a uniform pitch from the frame's left column; the command
// band on one baseline), the footer geometry identity with VIEW LEVEL, and
// the null-state single-page shape the bare sweeps drive.
TEST(MenuEngine, help_screen_spec_shape_pins)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Help);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    EXPECT_STREQ("help", spec.name);
    EXPECT_TRUE(spec.exit_on_redraw);
    EXPECT_EQ(MENU_REDRAW, spec.exit_value);
    EXPECT_TRUE(spec.polls_lobby);
    // DELIBERATE: today's help has no entry fade (and #200 is reworking the
    // FadeAroundEntry path).
    EXPECT_EQ(og::ui::EnterTransition::None, spec.enter);
    EXPECT_EQ(kHelpMenuBackIndex, spec.default_highlight);
    EXPECT_NE(nullptr, spec.frame_tick) << "wheel -> page steps";
    EXPECT_NE(nullptr, spec.on_spec_row) << "tab/pager dispatch";
    ASSERT_EQ(kHelpMenuNextIndex + 1, spec.row_count);
    EXPECT_NE(nullptr, spec.rows[kHelpMenuPrevIndex].state_override);
    EXPECT_NE(nullptr, spec.rows[kHelpMenuNextIndex].state_override);

    // Hotkeys: digit tabs, pager page keys, Escape on BACK.
    EXPECT_EQ(KEYSTATE_1, spec.rows[kHelpMenuControlsTabIndex].hotkey);
    EXPECT_EQ(KEYSTATE_2, spec.rows[kHelpMenuClassesTabIndex].hotkey);
    EXPECT_EQ(KEYSTATE_3, spec.rows[kHelpMenuEditorTabIndex].hotkey);
    EXPECT_EQ(KEYSTATE_ESCAPE, spec.rows[kHelpMenuBackIndex].hotkey);
    EXPECT_EQ(KEYSTATE_PAGEUP, spec.rows[kHelpMenuPrevIndex].hotkey);
    EXPECT_EQ(KEYSTATE_PAGEDOWN, spec.rows[kHelpMenuNextIndex].hotkey);

    // G3: every MenuSpecRow arg is its own spec ordinal.
    for (const int row : {kHelpMenuControlsTabIndex, kHelpMenuClassesTabIndex,
                          kHelpMenuEditorTabIndex, kHelpMenuPrevIndex,
                          kHelpMenuNextIndex}) {
        EXPECT_EQ(ButtonAction::MenuSpecRow, spec.rows[row].action)
            << spec.rows[row].id;
        EXPECT_EQ(row, spec.rows[row].arg) << spec.rows[row].id;
    }
    EXPECT_EQ(ButtonAction::ReturnMenu, spec.rows[kHelpMenuBackIndex].action);
    EXPECT_EQ(MENU_REDRAW, spec.rows[kHelpMenuBackIndex].arg);

    // Grid relations, not absolutes: the tab strip shares one baseline, one
    // face size, and a uniform pitch starting on the frame's left column;
    // the command band shares one baseline and height.
    for (const int tab : {kHelpMenuControlsTabIndex, kHelpMenuClassesTabIndex,
                          kHelpMenuEditorTabIndex}) {
        EXPECT_EQ(kHelpTabY, spec.rows[tab].y) << spec.rows[tab].id;
        EXPECT_EQ(kHelpTabWidth, spec.rows[tab].w) << spec.rows[tab].id;
        EXPECT_EQ(kHelpTabHeight, spec.rows[tab].h) << spec.rows[tab].id;
        EXPECT_EQ(kHelpTabX(tab), spec.rows[tab].x) << spec.rows[tab].id;
    }
    EXPECT_EQ(kHelpFrameLeft, spec.rows[kHelpMenuControlsTabIndex].x)
        << "tab strip starts on the frame's left column";
    EXPECT_EQ(spec.rows[kHelpMenuClassesTabIndex].x -
                  spec.rows[kHelpMenuControlsTabIndex].x,
              spec.rows[kHelpMenuEditorTabIndex].x -
                  spec.rows[kHelpMenuClassesTabIndex].x)
        << "tab pitch must be uniform";
    for (const int row : {kHelpMenuBackIndex, kHelpMenuPrevIndex,
                          kHelpMenuNextIndex}) {
        EXPECT_EQ(kHelpFooterY, spec.rows[row].y) << spec.rows[row].id;
        EXPECT_EQ(kHelpFooterHeight, spec.rows[row].h) << spec.rows[row].id;
    }

    // Shared-layout identity: the BACK/PREV/NEXT band is the VIEW LEVEL
    // footer, field by field.
    const og::ui::MenuScreenSpec& view =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::ViewScenario).spec;
    const std::pair<int, int> band[] = {
        {kHelpMenuBackIndex, kViewScenarioBackIndex},
        {kHelpMenuPrevIndex, kViewScenarioPrevIndex},
        {kHelpMenuNextIndex, kViewScenarioNextIndex}};
    for (const auto& [help_row, view_row] : band) {
        EXPECT_EQ(view.rows[view_row].x, spec.rows[help_row].x)
            << spec.rows[help_row].id;
        EXPECT_EQ(view.rows[view_row].y, spec.rows[help_row].y)
            << spec.rows[help_row].id;
        EXPECT_EQ(view.rows[view_row].w, spec.rows[help_row].w)
            << spec.rows[help_row].id;
        EXPECT_EQ(view.rows[view_row].h, spec.rows[help_row].h)
            << spec.rows[help_row].id;
    }

    // Null state (no open screen): pagers hidden — the single-page shape
    // the bare G5/gate-lattice sweeps drive.
    {
        og::ui::MenuLabelContext context;
        EXPECT_EQ(og::ui::RowState::Hidden,
                  spec.rows[kHelpMenuPrevIndex].state_override(context));
        EXPECT_EQ(og::ui::RowState::Hidden,
                  spec.rows[kHelpMenuNextIndex].state_override(context));
    }
}

// ---------------------------------------------------------------------------
// MAIN OPTIONS content-draw index pins + the sprite-sheet label restore.
// The draw hook reads rows [1]/[2]/[3]/[5] by ordinal (sound face, section
// rule + captions, sprite-sheet face) — pin those ids so a row insertion
// cannot silently redraw the wrong faces. The legacy loop also re-wrote
// "Sprite Sheet" to BOTH surfaces every frame after reset_buttons (the
// pick-spritesheet subscreen swaps allbuttons_ under this screen); that
// restore is now the row's LabelBinding, so pin that it exists and yields
// the exact string.
TEST(MenuEngine, main_options_content_index_pins_and_sprite_label_binding)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;
    EXPECT_STREQ("main_options", spec.name);
    // The legacy loop kept the lobby alive and returned MENU_REDRAW into
    // mainmenu(); remote-start None is pinned by the G5 allowlist.
    EXPECT_TRUE(spec.polls_lobby);
    EXPECT_EQ(MENU_REDRAW, spec.exit_value);

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_EQ(9, count);
    EXPECT_EQ("toggle_sound", buttons[1].id);
    EXPECT_EQ("display_settings", buttons[2].id);
    EXPECT_EQ("gameplay_fx", buttons[3].id);
    EXPECT_EQ("pick_sprite_sheet", buttons[5].id);
    // SPEED sits AFTER the historical ordinals the draw hook reads.
    EXPECT_EQ("game_speed", buttons[8].id);

    const og::ui::LabelFormatter sprite_formatter =
        spec.rows[5].label_binding.formatter;
    ASSERT_NE(nullptr, sprite_formatter)
        << "the sprite-sheet dual-surface label restore must be a binding";
    og::ui::MenuLabelContext context;
    EXPECT_EQ("Sprite Sheet", sprite_formatter(context));

    // SPEED re-derives from cfg gameplay/timer_wait every frame, and a full
    // 11-step lap must come back to where it started (the display number is
    // the inverted (20 - wait) / 2 + 1 the retired options menu showed).
    const og::ui::LabelFormatter speed_formatter =
        spec.rows[8].label_binding.formatter;
    ASSERT_NE(nullptr, speed_formatter)
        << "the SPEED face must re-derive from cfg, not from a click write";
    const std::string previous_speed = cfg.get_setting("gameplay", "timer_wait");
    cfg.apply_setting("gameplay", "timer_wait", "6");
    EXPECT_EQ("SPEED: 8", speed_formatter(context))
        << "the shipped default wait must read as SPEED 8";
    std::string wait = "6";
    for (int step = 0; step < 11; ++step) {
        wait = og::ui::cycle_game_speed(wait);
        cfg.apply_setting("gameplay", "timer_wait", wait);
        EXPECT_EQ(og::ui::format_game_speed_label(wait), speed_formatter(context));
    }
    EXPECT_EQ("6", wait) << "eleven clicks must restore the selector";
    cfg.apply_setting("gameplay", "timer_wait", previous_speed);
}

// ---------------------------------------------------------------------------
// G13 drift pins: while spec ordinals and picker_sdl_defs.h constants
// coexist, the DISPLAY rows must sit at their kDisplayMenu*Index positions,
// and the four cfg-derived LabelBindings must produce exactly the labels
// the legacy per-frame sync_label block derived (the bindings replaced both
// that block and the click-side writes — G8). Includes the unset-key
// fallbacks the injector flow pins on the live surface ("Zoom: 1.0x" /
// "Smooth: Off" for empty cfg keys).
TEST(MenuEngine, display_settings_index_drift_pins_and_label_bindings)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::DisplaySettings);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_EQ(9, count);
    EXPECT_EQ("display_back", buttons[kDisplayMenuBackIndex].id);
    EXPECT_EQ("display_mode", buttons[kDisplayMenuModeIndex].id);
    EXPECT_EQ("display_resolution", buttons[kDisplayMenuResolutionIndex].id);
    EXPECT_EQ("overscan_minus", buttons[kDisplayMenuOverscanMinusIndex].id);
    EXPECT_EQ("overscan_plus", buttons[kDisplayMenuOverscanPlusIndex].id);
    EXPECT_EQ("display_zoom", buttons[kDisplayMenuZoomIndex].id);
    EXPECT_EQ("display_smoothing", buttons[kDisplayMenuSmoothingIndex].id);
    // The brightness pair appends after the constants' block; like the
    // overscan pair it has no label binding (its live value is content-pass
    // text, so the faces stay plain "- " / "+ ").
    EXPECT_EQ("brightness_minus", buttons[7].id);
    EXPECT_EQ("brightness_plus", buttons[8].id);
    EXPECT_EQ(nullptr, spec.rows[7].label_binding.formatter);
    EXPECT_EQ(nullptr, spec.rows[8].label_binding.formatter);

    og::ui::MenuLabelContext context;
    const og::ui::LabelFormatter mode_formatter =
        spec.rows[kDisplayMenuModeIndex].label_binding.formatter;
    const og::ui::LabelFormatter resolution_formatter =
        spec.rows[kDisplayMenuResolutionIndex].label_binding.formatter;
    const og::ui::LabelFormatter zoom_formatter =
        spec.rows[kDisplayMenuZoomIndex].label_binding.formatter;
    const og::ui::LabelFormatter smoothing_formatter =
        spec.rows[kDisplayMenuSmoothingIndex].label_binding.formatter;
    ASSERT_NE(nullptr, mode_formatter);
    ASSERT_NE(nullptr, resolution_formatter);
    ASSERT_NE(nullptr, zoom_formatter);
    ASSERT_NE(nullptr, smoothing_formatter);

    EXPECT_EQ(og::ui::format_display_mode_label(
                  cfg.get_setting("graphics", "fullscreen")),
              mode_formatter(context));
    if (og::ui::parse_display_mode(cfg.get_setting("graphics", "fullscreen"))
        != og::ui::DisplayMode::Borderless) {
        EXPECT_EQ(og::ui::format_resolution_label(
                      cfg.get_setting("graphics", "width"),
                      cfg.get_setting("graphics", "height")),
                  resolution_formatter(context));
    }

    const std::string prev_zoom = cfg.get_setting("graphics", "zoom");
    const std::string prev_smoothing = cfg.get_setting("graphics", "smoothing");
    const std::string prev_render = cfg.get_setting("graphics", "render");
    cfg.apply_setting("graphics", "zoom", "");
    cfg.apply_setting("graphics", "smoothing", "");
    cfg.apply_setting("graphics", "render", "normal");
    EXPECT_EQ("Zoom: 1.0x", zoom_formatter(context))
        << "empty cfg (graphics, zoom) must fall back to the classic default";
    EXPECT_EQ("Smooth: Off", smoothing_formatter(context))
        << "empty cfg (graphics, smoothing) must fall back to Off";
    cfg.apply_setting("graphics", "zoom", prev_zoom);
    cfg.apply_setting("graphics", "smoothing", prev_smoothing);
    cfg.apply_setting("graphics", "render", prev_render);
}

// ---------------------------------------------------------------------------
// MAIN MENU (§1.8 step 4). Registry + spec-shape pins, the G9 variant
// materialization pins (the four legacy k_mainmenu_buttons shapes re-derived
// from the TWO specs — the un-compiled shapes have no other oracle), and the
// binding pins that replaced redraw_mainmenu's raw allbuttons_[N] writes.
// The compiled variant's exact table remains independently pinned by
// tests/test_menu_pins.cpp (G2/G11) and geometry by test_menu_layout.
namespace
{

struct ExpectedSpecRow
{
    const char* id;
    const char* label;
    int hotkey;
    int x, y, w, h;
    ButtonAction action;
    Sint32 arg;
    MenuNav nav;
    // §9.2: the no_company_note row materializes statically hidden
    // (companies present is the default view); every other row starts shown.
    bool hidden = false;
};

void check_materialized_shape(const og::ui::MenuScreenSpec& spec,
                              og::ui::MenuBuildVariant variant,
                              const ExpectedSpecRow* expected,
                              int expected_count, const char* shape_name)
{
    std::vector<button> rows;
    og::ui::materialize_menu_buttons_for(spec, variant, rows);
    ASSERT_EQ(expected_count, static_cast<int>(rows.size())) << shape_name;
    for (int i = 0; i < expected_count; ++i)
    {
        const ExpectedSpecRow& want = expected[i];
        const button& got = rows[static_cast<std::size_t>(i)];
        EXPECT_EQ(want.id, got.id) << shape_name << " index " << i;
        EXPECT_EQ(want.label, got.label) << shape_name << " " << got.id;
        EXPECT_EQ(want.hotkey, got.hotkey) << shape_name << " " << got.id;
        EXPECT_EQ(want.x, got.x) << shape_name << " " << got.id;
        EXPECT_EQ(want.y, got.y) << shape_name << " " << got.id;
        EXPECT_EQ(want.w, got.sizex) << shape_name << " " << got.id;
        EXPECT_EQ(want.h, got.sizey) << shape_name << " " << got.id;
        EXPECT_EQ(button_action_id(want.action), got.myfun)
            << shape_name << " " << got.id;
        EXPECT_EQ(want.arg, got.arg1) << shape_name << " " << got.id;
        EXPECT_EQ(want.nav.up, got.nav.up) << shape_name << " " << got.id;
        EXPECT_EQ(want.nav.down, got.nav.down) << shape_name << " " << got.id;
        EXPECT_EQ(want.nav.left, got.nav.left) << shape_name << " " << got.id;
        EXPECT_EQ(want.nav.right, got.nav.right)
            << shape_name << " " << got.id;
        EXPECT_EQ(want.hidden, got.hidden) << shape_name << " " << got.id;
        EXPECT_FALSE(got.no_draw) << shape_name << " " << got.id;
    }
    // §2.1/§9.2 + #155: load_company, no_company_note, then the CLOUD door
    // are appended at the table END of every variant.
    EXPECT_EQ("cloud", rows.back().id) << shape_name;
    EXPECT_EQ("no_company_note", rows[rows.size() - 2].id) << shape_name;
    EXPECT_EQ("load_company", rows[rows.size() - 3].id) << shape_name;
}

// The centered main chassis shared by every build. HELP is permanent;
// native/web supply enabled/disabled QUIT variants at the same index.
constexpr ExpectedSpecRow kMainMenuMPCommon[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 55, 140, 20,
     ButtonAction::BeginMenu, 1, MenuNav{.down = 1}},
    {"continue_game", "CONTINUE", KEYSTATE_UNKNOWN, 80, 79, 68, 20,
     ButtonAction::CreateTeamMenu, -1, MenuNav{.up = 0, .down = 2, .right = 6}},
    {"level_edit", "Level Editor", KEYSTATE_UNKNOWN, 80, 103, 140, 15,
     ButtonAction::DoLevelEdit, -1, MenuNav{.up = 1, .down = 3}},
    // DIFFICULTY left for the Base Camp command strip
    // (docs/camp-controls-design.md); the settings group is two full-width
    // doors named in full, with no grey caption over them.
    {"options", "GAME SETTINGS", KEYSTATE_UNKNOWN, 80, 131, 140, 15,
     ButtonAction::MainOptions, -1, MenuNav{.up = 2, .down = 8}},
    {"help", "HELP", KEYSTATE_UNKNOWN, 80, 178, 68, 15,
     ButtonAction::ShowHelp, -1, MenuNav{.up = 8, .down = 0, .right = 5}},
};

// The trailing space in "QUIT " is part of the shipped label.
constexpr ExpectedSpecRow kMainMenuMPQuitNative = {
    "quit", "QUIT ", KEYSTATE_ESCAPE, 152, 178, 68, 15,
    ButtonAction::QuitMenu, 0, MenuNav{.up = 8, .down = 0, .left = 4}};
constexpr ExpectedSpecRow kMainMenuMPQuitWeb = {
    "quit", "QUIT ", KEYSTATE_UNKNOWN, 152, 178, 68, 15,
    ButtonAction::QuitMenu, 0, MenuNav{.up = 8, .down = 0, .left = 4}};
constexpr ExpectedSpecRow kMainMenuMPLoad = {
    "load_company", "LOAD", KEYSTATE_UNKNOWN, 152, 79, 68, 20,
    ButtonAction::CreateLoadMenu, 0, MenuNav{.up = 0, .down = 2, .left = 1}};
constexpr ExpectedSpecRow kMainMenuMPNote = {
    "no_company_note", "NO COMPANY YET", KEYSTATE_UNKNOWN, 80, 79, 140, 20,
    ButtonAction::MenuSpecRow, 7, MenuNav{}, true};
// #155: the always-visible CLOUD door (MenuSpecRow arg == materialized
// ordinal 8 on every variant — exactly one QUIT row survives), the second
// full-width settings row under GAME SETTINGS.
constexpr ExpectedSpecRow kMainMenuMPCloud = {
    "cloud", "CLOUD SAVES", KEYSTATE_UNKNOWN, 80, 150, 140, 15,
    ButtonAction::MenuSpecRow, 8, MenuNav{.up = 3, .down = 4}};

// Main geometry no longer changes with multiplayer support: every build
// manages seats in Base Camp and reaches persistent profiles via CONTROLS.
constexpr ExpectedSpecRow kMainMenuNoMPCommon[] = {
    kMainMenuMPCommon[0], kMainMenuMPCommon[1], kMainMenuMPCommon[2],
    kMainMenuMPCommon[3], kMainMenuMPCommon[4],
};

constexpr ExpectedSpecRow kMainMenuNoMPQuitNative = kMainMenuMPQuitNative;
constexpr ExpectedSpecRow kMainMenuNoMPQuitWeb = kMainMenuMPQuitWeb;
constexpr ExpectedSpecRow kMainMenuNoMPLoad = kMainMenuMPLoad;
constexpr ExpectedSpecRow kMainMenuNoMPNote = kMainMenuMPNote;
constexpr ExpectedSpecRow kMainMenuNoMPCloud = kMainMenuMPCloud;

// Materialized order: common chassis, the platform's QUIT row, then the
// appended load_company, no_company_note, and the #155 CLOUD door
// (§2.1/§9.2 index-contract END).
std::vector<ExpectedSpecRow> build_expected_shape(
    const ExpectedSpecRow* common, int common_count,
    const ExpectedSpecRow& quit,
    const ExpectedSpecRow& load, const ExpectedSpecRow& note,
    const ExpectedSpecRow& cloud)
{
    std::vector<ExpectedSpecRow> shape(common, common + common_count);
    shape.push_back(quit);
    shape.push_back(load);
    shape.push_back(note);
    shape.push_back(cloud);
    return shape;
}

} // namespace

// Registry + spec-shape pins. The main menu keeps the legacy remote-start
// check (MainScope, break-with-selection), fade-bracketed entry, and
// exit-bearing return. Player-count editing now belongs to Base Camp.
TEST(MenuEngine, main_menu_registry_and_spec_shape)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::MainMenu);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    ASSERT_EQ(&og::ui::main_menu_screen_spec(), host.spec);
    const og::ui::MenuScreenSpec& spec = *host.spec;

    EXPECT_STREQ("mainmenu", spec.name);
    EXPECT_EQ(og::ui::RemoteStartScope::MainScope, spec.remote_start);
    EXPECT_EQ(og::ui::RemoteStartExit::BreakWithSelection,
              spec.remote_start_exit);
    EXPECT_EQ(og::ui::EnterTransition::FadeWithInitialDraw, spec.enter);
    EXPECT_TRUE(spec.polls_lobby);
    EXPECT_FALSE(spec.right_click_enabled);
    EXPECT_EQ(1, spec.default_highlight);  // continue_game, as the legacy loop
    EXPECT_EQ(MENU_EXIT, spec.exit_value);

    // The legacy-named options-index helper now finds the visible GAME
    // SETTINGS row by id; load_company and no_company_note remain the tail.
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_GT(count, 2);
    // Same lookup-by-id shape: the helper falls back to rows.size() - 1,
    // which is -1 for an empty row set. Bound it before it is used as a
    // subscript (EXPECT below is non-fatal and would not stop the read).
    const int options_index = picker_mainmenu_options_index();
    ASSERT_GE(options_index, 0) << "materialized main menu is empty";
    ASSERT_LT(options_index, count) << "options index past the button vector";
    EXPECT_EQ(3, options_index);
    EXPECT_EQ("options", buttons[options_index].id);
    EXPECT_EQ("load_company", buttons[count - 3].id);
    EXPECT_EQ("no_company_note", buttons[count - 2].id);
    EXPECT_EQ("cloud", buttons[count - 1].id);
}

TEST(MenuEngine, seat_settings_registry_and_player_control_ownership)
{
    const og::ui::MenuScreenHost& seat_host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::SeatSettings);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, seat_host.kind);
    ASSERT_EQ(&og::ui::seat_settings_menu_screen_spec(), seat_host.spec);
    EXPECT_STREQ("seat_settings", seat_host.spec->name);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              seat_host.spec->remote_start);
    EXPECT_TRUE(seat_host.spec->polls_lobby);
    EXPECT_TRUE(seat_host.spec->exit_on_redraw);
    EXPECT_EQ(MENU_REDRAW, seat_host.spec->exit_value);

    const og::ui::MenuScreenSpec& seat_mp =
        og::ui::seat_settings_menu_screen_spec_mp();
    const og::ui::MenuScreenSpec& seat_nomp =
        og::ui::seat_settings_menu_screen_spec_nomp();
    EXPECT_EQ(12, kSeatSettingsButtonCountMP);
    EXPECT_EQ(11, kSeatSettingsButtonCountNoMP);
    EXPECT_EQ(kSeatSettingsButtonCountMP, seat_mp.row_count);
    EXPECT_EQ(kSeatSettingsButtonCountNoMP, seat_nomp.row_count);
    EXPECT_STREQ("seat_settings_back", seat_mp.rows[kSeatSettingsBackIndex].id);
    EXPECT_STREQ("seat_remove", seat_mp.rows[kSeatSettingsRemoveIndex].id);
    EXPECT_STREQ("seat_reset", seat_nomp.rows[kSeatSettingsResetIndex].id);
    // The INPUT cycler is appended, so ordinals 0..5 and the highlight seed
    // are unchanged. Its dispatch arg is 6 in both tables; the no-MP table
    // has no REMOVE row so the same row materializes one slot earlier.
    EXPECT_EQ(6, kSeatSettingsInputIndex);
    EXPECT_EQ(6, kSeatSettingsInputRowMP);
    EXPECT_EQ(5, kSeatSettingsInputRowNoMP);
    EXPECT_STREQ("seat_input", seat_mp.rows[kSeatSettingsInputRowMP].id);
    EXPECT_STREQ("seat_input", seat_nomp.rows[kSeatSettingsInputRowNoMP].id);
    EXPECT_EQ(kSeatSettingsInputIndex,
              seat_mp.rows[kSeatSettingsInputRowMP].arg);
    EXPECT_EQ(kSeatSettingsInputIndex,
              seat_nomp.rows[kSeatSettingsInputRowNoMP].arg);
    EXPECT_EQ(ButtonAction::MenuSpecRow,
              seat_mp.rows[kSeatSettingsInputRowMP].action);
    EXPECT_EQ(ButtonAction::MenuSpecRow,
              seat_nomp.rows[kSeatSettingsInputRowNoMP].action);
    EXPECT_EQ(kSeatSettingsModeIndex, seat_mp.default_highlight);
    EXPECT_EQ(kSeatSettingsModeIndex, seat_nomp.default_highlight);

    const auto expect_geometry = [](const og::ui::MenuButtonSpec& row,
                                    int x, int y, int w, int h) {
        EXPECT_EQ(x, row.x) << row.id;
        EXPECT_EQ(y, row.y) << row.id;
        EXPECT_EQ(w, row.w) << row.id;
        EXPECT_EQ(h, row.h) << row.id;
    };
    for (const og::ui::MenuScreenSpec* spec : {&seat_mp, &seat_nomp}) {
        const bool mp = spec == &seat_mp;
        const int input_row =
            mp ? kSeatSettingsInputRowMP : kSeatSettingsInputRowNoMP;
        expect_geometry(spec->rows[kSeatSettingsModeIndex],
                        12, 30, 98, 18);
        expect_geometry(spec->rows[kSeatSettingsRemapIndex],
                        116, 30, 92, 18);
        expect_geometry(spec->rows[kSeatSettingsResetIndex],
                        214, 30, 90, 18);
        // The aligned grid: columns x=12/116/214 (widths 98/92/90) with 6px
        // gutters and a shared right edge at 304; the cycler takes its own
        // y=54 band above the live binding panel.
        expect_geometry(spec->rows[input_row], 12, 54, 98, 18);
        EXPECT_STREQ("RESET",
                     spec->rows[kSeatSettingsResetIndex].label);
        EXPECT_STREQ("INPUT: WASD", spec->rows[input_row].label);
        EXPECT_EQ(kSeatSettingsModeIndex,
                  spec->rows[kSeatSettingsBackIndex].nav.down);
        EXPECT_EQ(kSeatSettingsRemapIndex,
                  spec->rows[kSeatSettingsModeIndex].nav.right);
        EXPECT_EQ(kSeatSettingsResetIndex,
                  spec->rows[kSeatSettingsRemapIndex].nav.right);
        EXPECT_EQ(kSeatSettingsRemapIndex,
                  spec->rows[kSeatSettingsResetIndex].nav.left);
        // §7.1: the y=54 band is INPUT + ZOOM; the y=30 band drops into it
        // (MODE onto INPUT, REMAP onto ZOOM) and RESET drops onto the HUD
        // stack; INPUT still drops into the bottom band.
        const int zoom_row =
            mp ? kSeatSettingsZoomRowMP : kSeatSettingsZoomRowNoMP;
        const int radar_row =
            mp ? kSeatSettingsHudRadarRowMP : kSeatSettingsHudRadarRowNoMP;
        const int score_row =
            mp ? kSeatSettingsHudScoreRowMP : kSeatSettingsHudScoreRowNoMP;
        EXPECT_EQ(input_row, spec->rows[kSeatSettingsModeIndex].nav.down);
        EXPECT_EQ(zoom_row, spec->rows[kSeatSettingsRemapIndex].nav.down);
        EXPECT_EQ(radar_row, spec->rows[kSeatSettingsResetIndex].nav.down);
        EXPECT_EQ(kSeatSettingsModeIndex, spec->rows[input_row].nav.up);
        EXPECT_EQ(kSeatSettingsTeamIndex, spec->rows[input_row].nav.down);
        EXPECT_EQ(-1, spec->rows[input_row].nav.left);
        EXPECT_EQ(zoom_row, spec->rows[input_row].nav.right);
        EXPECT_EQ(input_row, spec->rows[kSeatSettingsTeamIndex].nav.up);
        // ZOOM + HUD stack geometry and dispatch args (args are the MP
        // positions in BOTH variants — the seat_input precedent).
        expect_geometry(spec->rows[zoom_row], 116, 54, 92, 18);
        EXPECT_STREQ("seat_zoom", spec->rows[zoom_row].id);
        EXPECT_EQ(kSeatSettingsZoomIndex, spec->rows[zoom_row].arg);
        expect_geometry(spec->rows[radar_row], 214, 78, 90, 18);
        expect_geometry(spec->rows[radar_row + 1], 214, 100, 90, 18);
        expect_geometry(spec->rows[radar_row + 2], 214, 122, 90, 18);
        expect_geometry(spec->rows[score_row], 214, 144, 90, 18);
        EXPECT_STREQ("seat_hud_radar", spec->rows[radar_row].id);
        EXPECT_STREQ("seat_hud_life", spec->rows[radar_row + 1].id);
        EXPECT_STREQ("seat_hud_foes", spec->rows[radar_row + 2].id);
        EXPECT_STREQ("seat_hud_score", spec->rows[score_row].id);
        EXPECT_EQ(kSeatSettingsHudRadarIndex, spec->rows[radar_row].arg);
        EXPECT_EQ(kSeatSettingsHudScoreIndex, spec->rows[score_row].arg);
        // Every link stays inside the variant's own row table.
        for (int row = 0; row < spec->row_count; ++row) {
            for (const int link : {spec->rows[row].nav.up,
                                   spec->rows[row].nav.down,
                                   spec->rows[row].nav.left,
                                   spec->rows[row].nav.right}) {
                EXPECT_LT(link, spec->row_count)
                    << spec->rows[row].id << (mp ? " (mp)" : " (no-mp)");
                EXPECT_GE(link, -1) << spec->rows[row].id;
            }
        }
    }
    expect_geometry(seat_mp.rows[kSeatSettingsTeamIndex],
                    12, 169, 138, 18);
    expect_geometry(seat_mp.rows[kSeatSettingsRemoveIndex],
                    166, 169, 138, 18);
    EXPECT_EQ(kSeatSettingsRemoveIndex,
              seat_mp.rows[kSeatSettingsTeamIndex].nav.right);
    EXPECT_EQ(kSeatSettingsTeamIndex,
              seat_mp.rows[kSeatSettingsRemoveIndex].nav.left);
    EXPECT_EQ(kSeatSettingsHudScoreRowMP,
              seat_mp.rows[kSeatSettingsRemoveIndex].nav.up);
    expect_geometry(seat_nomp.rows[kSeatSettingsTeamIndex],
                    91, 169, 138, 18);

    // BFS over both variants: every row reachable from the highlight seed,
    // and the cycler is not a dead end in either.
    for (const og::ui::MenuScreenSpec* spec : {&seat_mp, &seat_nomp}) {
        std::vector<bool> seen(
            static_cast<std::size_t>(spec->row_count), false);
        std::vector<int> queue{spec->default_highlight};
        seen[static_cast<std::size_t>(spec->default_highlight)] = true;
        while (!queue.empty()) {
            const int row = queue.back();
            queue.pop_back();
            for (const int link : {spec->rows[row].nav.up,
                                   spec->rows[row].nav.down,
                                   spec->rows[row].nav.left,
                                   spec->rows[row].nav.right}) {
                if (link < 0 || link >= spec->row_count)
                    continue;
                if (seen[static_cast<std::size_t>(link)])
                    continue;
                seen[static_cast<std::size_t>(link)] = true;
                queue.push_back(link);
            }
        }
        for (int row = 0; row < spec->row_count; ++row) {
            EXPECT_TRUE(seen[static_cast<std::size_t>(row)])
                << "unreachable seat-settings row " << spec->rows[row].id;
        }
    }

    // The per-seat screen is now the ONLY owner of direction mode, remap,
    // reset and input device: the global CONTROLS subscreen that duplicated
    // those rows for all four players is deleted, door and all.
    const og::ui::MenuScreenHost& options_host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, options_host.kind);
    ASSERT_NE(nullptr, options_host.spec);
    const og::ui::MenuScreenSpec& options = *options_host.spec;
    // BACK + Sound + DISPLAY + 3 FX doors + RESTORE + Sprite Sheet + SPEED.
    ASSERT_EQ(9, options.row_count);
    EXPECT_EQ(og::ui::RemoteStartScope::MainScope, options.remote_start);
    for (int row = 0; row < options.row_count; ++row) {
        EXPECT_STRNE("control_settings", options.rows[row].id)
            << "GAME SETTINGS must not carry a global CONTROLS door";
        EXPECT_NE(ButtonAction::RestoreDefaultControls, options.rows[row].action)
            << options.rows[row].id;
    }
    // 48/49/50 are the retired OpenControlSettings / ToggleControlMode /
    // EditPlayerKeymap dispatch ids (button.h marks them do-not-reuse). No
    // registry screen may carry one: the enum names are gone, so this is the
    // pin that keeps the VALUES from coming back under a new name.
    for (int i = 0; i < static_cast<int>(og::ui::MenuScreenId::Count); ++i) {
        const og::ui::MenuScreenHost& host =
            og::ui::menu_screen_host(static_cast<og::ui::MenuScreenId>(i));
        if (host.kind != og::ui::MenuScreenHost::Kind::Engine)
            continue;
        for (int row = 0; row < host.spec->row_count; ++row) {
            const Sint32 action =
                static_cast<Sint32>(host.spec->rows[row].action);
            for (const Sint32 retired : {48, 49, 50}) {
                EXPECT_NE(retired, action)
                    << host.spec->name << " " << host.spec->rows[row].id
                    << " uses retired ButtonAction " << retired;
            }
        }
    }
}

// G9: the four materialized shapes, re-derived from the two specs. The
// compiled shape is independently pinned by test_menu_pins (G11); the other
// three have no legacy oracle anymore — these pins are it.
TEST(MenuEngine, main_menu_four_variant_materialization_pins)
{
    using og::ui::MenuBuildVariant;

    const std::vector<ExpectedSpecRow> mp_native = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPQuitNative, kMainMenuMPLoad, kMainMenuMPNote,
        kMainMenuMPCloud);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Native, mp_native.data(),
                             static_cast<int>(mp_native.size()),
                             "mainmenu_mp_native");

    const std::vector<ExpectedSpecRow> mp_web = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPQuitWeb, kMainMenuMPLoad, kMainMenuMPNote,
        kMainMenuMPCloud);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Web, mp_web.data(),
                             static_cast<int>(mp_web.size()),
                             "mainmenu_mp_web");

    const std::vector<ExpectedSpecRow> nomp_native = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPQuitNative, kMainMenuNoMPLoad,
        kMainMenuNoMPNote, kMainMenuNoMPCloud);
    check_materialized_shape(og::ui::main_menu_screen_spec_nomp(),
                             MenuBuildVariant::Native, nomp_native.data(),
                             static_cast<int>(nomp_native.size()),
                             "mainmenu_nomp_native");

    const std::vector<ExpectedSpecRow> nomp_web = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPQuitWeb, kMainMenuNoMPLoad,
        kMainMenuNoMPNote, kMainMenuNoMPCloud);
    check_materialized_shape(og::ui::main_menu_screen_spec_nomp(),
                             MenuBuildVariant::Web, nomp_web.data(),
                             static_cast<int>(nomp_web.size()),
                             "mainmenu_nomp_web");
}

// Player count and per-level team assignment now live in Base Camp. The main
// screen retains only the BEGIN NEW GAME pixie art and no player bindings.
TEST(MenuEngine, main_menu_binding_pins)
{
    const og::ui::MenuScreenSpec& mp = og::ui::main_menu_screen_spec_mp();
    const og::ui::MenuScreenSpec& nomp = og::ui::main_menu_screen_spec_nomp();
    const og::ui::MenuScreenSpec& seat_mp =
        og::ui::seat_settings_menu_screen_spec_mp();
    const og::ui::MenuScreenSpec& seat_nomp =
        og::ui::seat_settings_menu_screen_spec_nomp();

    // The wrench face is gone: GAME SETTINGS is a normal labeled button.
    EXPECT_EQ(FAMILY_NORMAL1, mp.rows[0].art_family);
    EXPECT_EQ(FAMILY_NORMAL1, nomp.rows[0].art_family);
    for (const og::ui::MenuScreenSpec* spec : {&mp, &nomp})
        for (int i = 1; i < spec->row_count; ++i)
            EXPECT_EQ(-1, spec->rows[i].art_family)
                << spec->name << " " << spec->rows[i].id;

    // Web QUIT keeps the stable footer slot but is explicitly Disabled;
    // native QUIT remains enabled and carries Escape.
    const og::ui::MenuButtonSpec* native_quit = nullptr;
    const og::ui::MenuButtonSpec* web_quit = nullptr;
    for (int i = 0; i < mp.row_count; ++i) {
        if (std::string_view(mp.rows[i].id) != "quit")
            continue;
        if (mp.rows[i].build == og::ui::MenuBuildGate::NativeOnly)
            native_quit = &mp.rows[i];
        else if (mp.rows[i].build == og::ui::MenuBuildGate::WebOnly)
            web_quit = &mp.rows[i];
    }
    ASSERT_NE(nullptr, native_quit);
    ASSERT_NE(nullptr, web_quit);
    EXPECT_EQ(nullptr, native_quit->state_override);
    ASSERT_NE(nullptr, web_quit->state_override);
    EXPECT_EQ(og::ui::RowState::Disabled,
              web_quit->state_override(og::ui::MenuLabelContext{}));

    // Main-menu variants carry no player-specific outlines, formatters, or
    // old player-count actions; those belong to the live Base Camp roster.
    for (const og::ui::MenuScreenSpec* spec : {&mp, &nomp})
        for (int i = 0; i < spec->row_count; ++i) {
            EXPECT_EQ(og::ui::MenuOutlineBinding::None, spec->rows[i].outline)
                << spec->name << " " << spec->rows[i].id;
            EXPECT_EQ(nullptr, spec->rows[i].label_binding.formatter)
                << spec->name << " " << spec->rows[i].id;
            EXPECT_NE(ButtonAction::SetPlayerMode, spec->rows[i].action)
                << spec->name << " " << spec->rows[i].id;
        }

    // Both seat-editor variants dispatch against the selected stable seat;
    // neither revives the old save-backed player-count action or outline.
    for (const og::ui::MenuScreenSpec* spec : {&seat_mp, &seat_nomp})
        for (int i = 0; i < spec->row_count; ++i) {
            EXPECT_EQ(og::ui::MenuOutlineBinding::None, spec->rows[i].outline)
                << spec->name << " " << spec->rows[i].id;
            EXPECT_NE(ButtonAction::SetPlayerMode, spec->rows[i].action)
                << spec->name << " " << spec->rows[i].id;
        }
}

// §2.1: CONTINUE and LOAD gate on company existence, and the nav rewire
// routes the graph around them when hidden (re-asserting the split when
// present) — the §1.5 no-visible-links-into-hidden invariant must hold in
// both states.
TEST(MenuEngine, main_menu_company_gate_and_nav_rewire)
{
    const og::ui::MenuScreenSpec& mp = og::ui::main_menu_screen_spec_mp();

    const og::ui::MenuButtonSpec* continue_row = nullptr;
    const og::ui::MenuButtonSpec* load_row = nullptr;
    for (int i = 0; i < mp.row_count; ++i) {
        if (std::string_view(mp.rows[i].id) == "continue_game")
            continue_row = &mp.rows[i];
        else if (std::string_view(mp.rows[i].id) == "load_company")
            load_row = &mp.rows[i];
    }
    ASSERT_NE(nullptr, continue_row);
    ASSERT_NE(nullptr, load_row);
    EXPECT_EQ(og::ui::MenuGate::Custom, continue_row->gate.gate);
    EXPECT_EQ(og::ui::MenuGate::Custom, load_row->gate.gate);

    SaveData save;
    og::ui::MenuLabelContext ctx;
    ctx.save = &save;

    og::ui::set_main_menu_company_view_for_tests(true, "IRON KETTLE BAND");
    EXPECT_EQ(og::ui::RowState::Visible,
              og::ui::gate_state(continue_row->gate, ctx));
    EXPECT_EQ(og::ui::RowState::Visible,
              og::ui::gate_state(load_row->gate, ctx));

    og::ui::set_main_menu_company_view_for_tests(false, "");
    EXPECT_EQ(og::ui::RowState::Hidden,
              og::ui::gate_state(continue_row->gate, ctx));
    EXPECT_EQ(og::ui::RowState::Hidden,
              og::ui::gate_state(load_row->gate, ctx));

    ASSERT_NE(nullptr, mp.nav.rewire);
    std::vector<button> buttons;
    og::ui::materialize_menu_buttons_for(
        mp, og::ui::MenuBuildVariant::Native, buttons);
    const int count = static_cast<int>(buttons.size());
    const auto index_of = [&](std::string_view id) {
        for (int i = 0; i < count; ++i)
            if (std::string_view(buttons[static_cast<std::size_t>(i)].id) == id)
                return i;
        return -1;
    };
    const int i_begin = index_of("begin_new_game");
    const int i_continue = index_of("continue_game");
    const int i_level_editor = index_of("level_edit");
    // index_of yields -1 for an id that has left the spec, and every use
    // below is a subscript (or a highlight index handed to the rewire).
    // Fail here, naming the missing row, instead of reading the button
    // vector at (size_t)-1.
    ASSERT_GE(i_begin, 0) << "main menu no longer has a begin_new_game row";
    ASSERT_GE(i_continue, 0) << "main menu no longer has a continue_game row";
    ASSERT_GE(i_level_editor, 0) << "main menu no longer has a level_edit row";

    // Absent: mimic the gate pass (mark the pair hidden), then rewire.
    for (auto& b : buttons)
        if (std::string_view(b.id) == "continue_game"
            || std::string_view(b.id) == "load_company")
            b.hidden = true;
    int highlighted = i_level_editor;
    mp.nav.rewire(buttons.data(), count, highlighted);
    for (int i = 0; i < count; ++i) {
        if (buttons[static_cast<std::size_t>(i)].hidden)
            continue;
        for (const int link : {buttons[static_cast<std::size_t>(i)].nav.up, buttons[static_cast<std::size_t>(i)].nav.down,
                               buttons[static_cast<std::size_t>(i)].nav.left, buttons[static_cast<std::size_t>(i)].nav.right}) {
            if (link < 0)
                continue;
            EXPECT_FALSE(buttons[static_cast<std::size_t>(link)].hidden)
                << buttons[static_cast<std::size_t>(i)].id << " must not nav into a hidden row";
        }
    }
    EXPECT_EQ(i_level_editor, buttons[static_cast<std::size_t>(i_begin)].nav.down)
        << "begin.down routes past the hidden pair to LEVEL EDITOR";

    // Present again: the rewire re-asserts begin.down -> continue_game.
    og::ui::set_main_menu_company_view_for_tests(true, "");
    std::vector<button> shown;
    og::ui::materialize_menu_buttons_for(
        mp, og::ui::MenuBuildVariant::Native, shown);
    int hl = 1;
    mp.nav.rewire(shown.data(), static_cast<int>(shown.size()), hl);
    // Both company states materialize the same row set (the gate pass runs
    // later, over the materialized vector), so the indexes taken above index
    // this vector too. Pin that before subscripting it.
    ASSERT_EQ(buttons.size(), shown.size())
        << "the two materializations must agree row-for-row";
    EXPECT_EQ(i_continue, shown[static_cast<std::size_t>(i_begin)].nav.down);

    // §9.2 no_company_note: Hidden while a company exists, Disabled (the
    // engine's greyed keyboard-dead chrome — face GREY, myfun/myfunc zeroed,
    // clicks no-op with the menu_engine/disabled_row_click TRACE, pinned
    // generically by MenuEngine.disabled_row_activation_no_op) when none
    // does. Inert both ways: no nav links out, and NOTHING links into it in
    // either state — keyboard flows keep the hidden-variant routing above.
    for (const og::ui::MenuScreenSpec* variant :
         {&mp, &og::ui::main_menu_screen_spec_nomp()}) {
        const og::ui::MenuButtonSpec* note_row = nullptr;
        for (int i = 0; i < variant->row_count; ++i) {
            if (std::string_view(variant->rows[i].id) == "no_company_note")
                note_row = &variant->rows[i];
        }
        ASSERT_NE(nullptr, note_row);
        ASSERT_NE(nullptr, note_row->state_override);
        og::ui::set_main_menu_company_view_for_tests(true, "");
        EXPECT_EQ(og::ui::RowState::Hidden, note_row->state_override(ctx));
        og::ui::set_main_menu_company_view_for_tests(false, "");
        EXPECT_EQ(og::ui::RowState::Disabled, note_row->state_override(ctx));
        EXPECT_EQ(-1, note_row->nav.up);
        EXPECT_EQ(-1, note_row->nav.down);
        EXPECT_EQ(-1, note_row->nav.left);
        EXPECT_EQ(-1, note_row->nav.right);
    }

    // Zero inbound links, both company states, after the production rewire.
    for (const bool present : {true, false}) {
        og::ui::set_main_menu_company_view_for_tests(present, "");
        std::vector<button> state_buttons;
        og::ui::materialize_menu_buttons_for(
            mp, og::ui::MenuBuildVariant::Native, state_buttons);
        const int n = static_cast<int>(state_buttons.size());
        int note_index = -1;
        for (int i = 0; i < n; ++i)
            if (std::string_view(state_buttons[static_cast<std::size_t>(i)].id) == "no_company_note")
                note_index = i;
        ASSERT_GE(note_index, 0);
        if (!present) {
            for (auto& b : state_buttons)
                if (std::string_view(b.id) == "continue_game"
                    || std::string_view(b.id) == "load_company")
                    b.hidden = true;
        }
        int note_hl = 1;
        mp.nav.rewire(state_buttons.data(), n, note_hl);
        for (int i = 0; i < n; ++i) {
            for (const int link :
                 {state_buttons[static_cast<std::size_t>(i)].nav.up, state_buttons[static_cast<std::size_t>(i)].nav.down,
                  state_buttons[static_cast<std::size_t>(i)].nav.left, state_buttons[static_cast<std::size_t>(i)].nav.right}) {
                EXPECT_NE(note_index, link)
                    << state_buttons[static_cast<std::size_t>(i)].id
                    << " must not nav into the inert note row (present="
                    << present << ")";
            }
        }
    }

    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
}

// Companion pin for the lookup-by-id shape above: those nav pins resolve row
// ids to indexes and then subscript the materialized button vector with the
// result. Every id they resolve must exist, so a rename or removal fails
// here by name — and the ASSERT_GE guards in those tests keep the read
// itself from ever happening with -1.
TEST(MenuEngine, main_menu_lookup_row_ids_exist)
{
    for (const og::ui::MenuScreenSpec* spec :
         {&og::ui::main_menu_screen_spec_mp(),
          &og::ui::main_menu_screen_spec_nomp()}) {
        std::vector<button> buttons;
        og::ui::materialize_menu_buttons_for(
            *spec, og::ui::MenuBuildVariant::Native, buttons);
        ASSERT_FALSE(buttons.empty()) << spec->name;
        for (const char* id : {"begin_new_game", "continue_game", "level_edit",
                               "options", "no_company_note"}) {
            bool found = false;
            for (const button& b : buttons)
                if (std::string_view(b.id) == id)
                    found = true;
            EXPECT_TRUE(found)
                << spec->name << " row '" << id
                << "' is resolved by id and then used as a subscript";
        }
    }
}

// ---------------------------------------------------------------------------
// G13 drift pin: while spec ordinals and picker_sdl_defs.h constants
// coexist, the depth-selector row must sit at kGraphicsFxDepthFxIndex, and
// its LabelBinding must produce exactly the label change_depth_fx() used to
// write click-side (the binding replaced those writes — G8).
TEST(MenuEngine, graphics_fx_depth_row_drift_pin_and_label_binding)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::GraphicsFx);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_GT(count, kGraphicsFxDepthFxIndex);
    EXPECT_EQ("depth_fx", buttons[kGraphicsFxDepthFxIndex].id);

    const og::ui::LabelFormatter formatter =
        spec.rows[kGraphicsFxDepthFxIndex].label_binding.formatter;
    ASSERT_NE(nullptr, formatter)
        << "the depth row's cfg-derived label must be a binding";
    og::ui::MenuLabelContext context;
    // A full five-step lap: the binding tracks every value the selector can
    // store, byte-identical to the legacy click-side write.
    std::string value = cfg.get_setting("effects", "depth_fx");
    const std::string start = value;
    for (int step = 0; step < 5; ++step) {
        cfg.apply_setting("effects", "depth_fx", value);
        EXPECT_EQ(og::ui::format_depth_fx_label(value), formatter(context));
        value = og::ui::cycle_depth_fx(value);
    }
    cfg.apply_setting("effects", "depth_fx", start);
}

// ---------------------------------------------------------------------------
// Valve V2 decision record (Layer E closeout): NETWORKING stays LEGACY.
// configure_networking's loop is a retvalue-as-action-id dispatch over
// private SdlPickerClient state, its staged-commit room-list contract needs
// frame phases the runner deliberately does not have (a pre-input poll and
// the raw pointer-sample idle gate — both pinned by the NetworkingMenu race
// flows), and the web/native tables fork positional indices, which
// MenuBuildGate cannot express until G9 id-keyed nav exists. Full rationale:
// docs/menu-engine.md "V2 decision record". This pin makes the decision
// explicit: the screen is state-machine-owned (no spec, no legacy_entry).
// Whoever migrates it later must flip this pin in the same commit, with the
// NetworkingMenu suite unchanged as the identity oracle.
TEST(MenuEngine, networking_stays_legacy_v2_decision)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Networking);
    EXPECT_EQ(og::ui::MenuScreenHost::Kind::Legacy, host.kind)
        << "NETWORKING migrates only with G9 id-keyed nav + runner pre-input "
           "phases; see docs/menu-engine.md V2 decision record";
    EXPECT_EQ(nullptr, host.spec);
    EXPECT_EQ(nullptr, host.legacy_entry)
        << "owned by the SdlPickerClient state machine, not a free entry fn";

    // Every OTHER registry row is engine-hosted: Layer E is complete, and a
    // new MenuScreenId added without a host assignment fails here.
    for (int i = 0; i < static_cast<int>(og::ui::MenuScreenId::Count); ++i) {
        const auto id = static_cast<og::ui::MenuScreenId>(i);
        if (id == og::ui::MenuScreenId::Networking)
            continue;
        const og::ui::MenuScreenHost& other = og::ui::menu_screen_host(id);
        EXPECT_EQ(og::ui::MenuScreenHost::Kind::Engine, other.kind)
            << "screen ordinal " << i;
        EXPECT_NE(nullptr, other.spec) << "screen ordinal " << i;
    }
}

// The active company name no longer appears on the main menu. Its cached
// value still participates in company-list flows, but changing that value
// must not change a main-menu frame.
TEST(MenuEngine, main_menu_draw_does_not_depend_on_company_name)
{
    const og::ui::MenuScreenSpec& mp = og::ui::main_menu_screen_spec_mp();
    ASSERT_NE(nullptr, mp.draw_content);

    auto frame_hash = [] {
        std::uint64_t hash = 1469598103934665603ULL;
        screen* scr = og::runtime::current_session->myscreen_;
        for (int y = 0; y < 200; ++y) {
            for (int x = 0; x < 320; ++x) {
                Uint8 r = 0, g = 0, b = 0;
                scr->get_pixel(x, y, &r, &g, &b);
                for (const Uint8 channel : {r, g, b}) {
                    hash ^= channel;
                    hash *= 1099511628211ULL;
                }
            }
        }
        return hash;
    };

    screen* scr = og::runtime::current_session->myscreen_;
    scr->clearbuffer();
    og::ui::set_main_menu_company_view_for_tests(true, "FIRST COMPANY");
    mp.draw_content(nullptr);
    const std::uint64_t first = frame_hash();

    scr->clearbuffer();
    og::ui::set_main_menu_company_view_for_tests(
        true, "AN ENTIRELY DIFFERENT OVERLONG COMPANY NAME");
    mp.draw_content(nullptr);
    EXPECT_EQ(first, frame_hash());

    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
}

namespace
{

og::sim::LobbyPlayer make_menu_lobby_player(std::uint8_t player_index,
                                            std::string company)
{
    og::sim::LobbyPlayer player;
    player.player_index = player_index;
    player.seat_id =
        static_cast<og::sim::LobbySeatId>(1000u + player_index);
    player.name = "Player " + std::to_string(player_index + 1);
    player.company = std::move(company);
    player.team = static_cast<short>(player_index % SCORE_TEAM_COUNT);
    return player;
}

struct MenuCallbackStateGuard
{
    og::ui::PickerSaveSlotEditableCallback saved_editable =
        og::ui::g_picker_save_slot_editable_callback;

    ~MenuCallbackStateGuard()
    {
        og::ui::install_base_camp_state_for_screen(nullptr);
        og::ui::install_seat_settings_state_for_screen(nullptr);
        og::ui::install_company_list_state_for_screen(nullptr);
        og::ui::install_company_backups_state_for_screen(nullptr);
        og::ui::g_picker_save_slot_editable_callback = saved_editable;
        picker_testing_yes_or_no_queue_clear();
    }
};

struct SavedRoster
{
    SaveData& save;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> slots;
    unsigned char team_size;
    unsigned char numplayers;
    std::string save_name;
    std::string current_campaign;

    explicit SavedRoster(SaveData& value)
        : save(value)
        , team_size(value.team_size)
        , numplayers(value.numplayers)
        , save_name(value.save_name)
        , current_campaign(value.current_campaign)
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            slots[static_cast<std::size_t>(i)] =
                std::move(save.team_list[static_cast<std::size_t>(i)]);
    }

    ~SavedRoster()
    {
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)] =
                std::move(slots[static_cast<std::size_t>(i)]);
        save.team_size = team_size;
        save.numplayers = numplayers;
        save.save_name = std::move(save_name);
        save.current_campaign = std::move(current_campaign);
    }
};

struct SavedControlState
{
    cfg_store saved_cfg = cfg;
    og::test::ScopedPhysicalFileState config_file;
    InputHardwareState hardware = input_hardware_state();
    int active[4][NUM_KEYS]{};

    SavedControlState()
        : config_file(std::filesystem::path(get_user_path()) /
                      "cfg" / "openglad.yaml")
    {
        for (int player = 0; player < 4; ++player) {
            for (int key = 0; key < NUM_KEYS; ++key) {
                active[player][key] =
                    og::runtime::current_session->player_keys_[player][key];
            }
        }
    }

    ~SavedControlState()
    {
        input_hardware_state() = hardware;
        for (int player = 0; player < 4; ++player) {
            for (int key = 0; key < NUM_KEYS; ++key) {
                og::runtime::current_session->player_keys_[player][key] =
                    active[player][key];
            }
        }
        cfg = std::move(saved_cfg);
    }
};

struct MissingScreenGuard
{
    screen* saved = og::runtime::current_session->myscreen_;

    MissingScreenGuard()
    {
        og::runtime::current_session->myscreen_ = nullptr;
    }

    ~MissingScreenGuard()
    {
        og::runtime::current_session->myscreen_ = saved;
    }
};

std::uint64_t indexed_region_hash(screen& output, int x0, int y0,
                                  int x1, int y1)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            int pixel = 0;
            output.get_pixel(x, y, &pixel);
            hash ^= static_cast<std::uint8_t>(pixel);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace

TEST(MenuEngine, seat_settings_rejects_stale_spectator_and_persists_mode)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    FakeLobbyClient lobby;
    lobby.networked = true;
    lobby.players = {make_menu_lobby_player(7, "LOCAL COMPANY")};
    lobby.local_indices = {7};
    lobby.local_seats = 0;
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::SeatSettingsScreenState state{
        .seat_id = lobby.players.front().seat_id,
        .player_index = lobby.players.front().player_index,
        .local_slot = -1,
    };
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kSeatSettingsTeamIndex;

    // The wire roster can briefly retain a seat after this machine has
    // become a zero-seat spectator. It must not be rebound to profile zero.
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ(-1, state.local_slot);
    EXPECT_EQ("TEAM 1", buttons[kSeatSettingsTeamIndex].label);

    screen& output = *og::runtime::current_session->myscreen_;
    output.clearbuffer();
    const std::uint64_t blank = indexed_region_hash(output, 0, 0, 320, 200);
    spec.draw_content(nullptr);
    spec.draw_content(&state);
    EXPECT_EQ(blank, indexed_region_hash(output, 0, 0, 320, 200))
        << "a stale/spectator seat must not draw another profile's controls";

    trace_clear();
    EXPECT_FALSE(spec.frame_tick(&state, 0));
    EXPECT_TRUE(state.missing_notice_shown);
    EXPECT_TRUE(trace_contains("popup", "SEAT LEFT"));
    trace_clear();
    EXPECT_FALSE(spec.frame_tick(&state, 1));
    EXPECT_FALSE(trace_contains("popup", "SEAT LEFT"))
        << "the disconnect notice is shown at most once";

    // Once authority reports one active local seat, the same stable token
    // resolves to profile zero and the mode action must both toggle and
    // persist the selected profile.
    SavedControlState saved_controls;
    ASSERT_TRUE(saved_controls.config_file.ready())
        << saved_controls.config_file.error().message();
    lobby.local_seats = 1;
    state.missing_notice_shown = false;

    text& font = output.text_normal;
    output.clearbuffer();
    font.write_xy_center(160, 13, DARK_BLUE, "%s",
                         "LOCAL PLAYER 1 / P8");
    const std::uint64_t expected_identity =
        indexed_region_hash(output, 0, 13, 320, 13 + font.sizey);
    output.clearbuffer();
    spec.draw_content(&state);
    EXPECT_EQ(expected_identity,
              indexed_region_hash(output, 0, 13, 320, 13 + font.sizey))
        << "the stable P8 token must resolve to local profile one";
    EXPECT_EQ(0, state.local_slot);

    const int old_mode = get_player_control_mode(0);
    const int expected_mode =
        old_mode == static_cast<int>(ControlDirectionMode::FourDirection)
        ? static_cast<int>(ControlDirectionMode::EightDirection)
        : static_cast<int>(ControlDirectionMode::FourDirection);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsModeIndex, &state));
    EXPECT_EQ(0, state.local_slot);
    EXPECT_EQ(expected_mode, get_player_control_mode(0));
    EXPECT_EQ(std::to_string(expected_mode),
              cfg.get_setting("controls", "player1_mode"));

    // INPUT: seat the profile on a known factory mapping so the cycle order
    // is deterministic under --gtest_shuffle, then prove one click moves the
    // live keymap, the derived name, and the button face together.
    ASSERT_TRUE(og::input::assign_mapping_to_player(
        0, og::input::factory_mapping(0)));
    ASSERT_EQ("WASD", og::ui::current_input_selection(0).name);
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("INPUT: WASD", buttons[kSeatSettingsInputRow].label);

    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsInputIndex, &state));
    EXPECT_EQ("ARROWS", og::ui::current_input_selection(0).name);
    EXPECT_EQ(og::input::factory_default_key(
                  1, 0, KEY_UP),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::FourDirection),
                  KEY_UP))
        << "cycling loads the named mapping into BOTH mode keymaps";
    EXPECT_EQ(og::input::factory_default_key(1, 1, KEY_UP),
              get_player_key_binding_for_mode(
                  0, static_cast<int>(ControlDirectionMode::EightDirection),
                  KEY_UP));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ(std::string("INPUT: ") + og::input::kArrowGlyphs,
              buttons[kSeatSettingsInputRow].label)
        << "the 98px face carries the arrow-glyph short name";
    EXPECT_LE(buttons[kSeatSettingsInputRow].label.size(),
              static_cast<std::size_t>(
                  (buttons[kSeatSettingsInputRow].sizex - 8) / 6));
    EXPECT_TRUE(trace_contains("basecamp", "seat_input slot=1 name=ARROWS"));
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsInputIndex, &state));
    EXPECT_EQ("IJKL", og::ui::current_input_selection(0).name)
        << "the cycler advances through the factory names in order";
    ASSERT_TRUE(og::input::assign_mapping_to_player(
        0, og::input::factory_mapping(0)));

    const int old_up = og::runtime::current_session->player_keys_[0][KEY_UP];
    const int polls_before_remap = lobby.poll_count;
    lobby.start_on_next_poll = true;
    g_start_game_requested = false;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsRemapIndex, &state));
    EXPECT_TRUE(g_start_game_requested)
        << "a blocking remap must yield to a launchable host start";
    EXPECT_EQ(polls_before_remap + 1, lobby.poll_count);
    EXPECT_EQ(old_up,
              og::runtime::current_session->player_keys_[0][KEY_UP])
        << "the aborted prompt must not alter the profile";
    g_start_game_requested = false;

    // A confirmed removal that authority rejects leaves the editor and
    // profile intact, while targeting the exact displayed token.
    lobby.local_seats = 2;
    lobby.remove_seat_result = false;
    picker_testing_yes_or_no_queue_push(true);
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsRemoveIndex, &state));
    EXPECT_EQ(1u, lobby.removed_seats.size());
    if (!lobby.removed_seats.empty()) {
        EXPECT_EQ(std::make_pair(lobby.players.front().player_index,
                                 lobby.players.front().seat_id),
                  lobby.removed_seats.front());
    }
    EXPECT_FALSE(state.removed);
    EXPECT_TRUE(trace_contains("popup", "REMOVE DENIED"));

    // Design §3.2: REMAP writes the seat's live layout through to the mapping
    // library under its re-derived name, and RESET drops that entry again so
    // cycling away and back cannot resurrect an undone customization. (The
    // wizard itself cancels immediately under TESTING; the write-through is
    // what this drives.)
    const auto library_entry = [](cfg_store& config, const char* name) {
        const std::vector<og::input::MappingDefinition> library =
            og::input::load_mapping_library(config);
        return std::find_if(
            library.begin(), library.end(),
            [name](const og::input::MappingDefinition& entry) {
                return entry.name == name;
            }) != library.end();
    };
    EXPECT_FALSE(library_entry(cfg, "WASD"))
        << "an untouched factory layout is implicit";
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_YELL, SDLK_P);
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSeatSettingsRemapIndex, &state));
    EXPECT_TRUE(library_entry(cfg, "WASD"))
        << "a customized WASD is saved under the WASD name";

    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSeatSettingsResetIndex, &state));
    EXPECT_FALSE(library_entry(cfg, "WASD"))
        << "RESET drops the entry the customization wrote";
}

TEST(MenuEngine, base_camp_rail_and_add_seat_boundaries_are_behavioral)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    FakeLobbyClient lobby;
    lobby.networked = true;
    lobby.host = true;
    lobby.local_seats = 1;
    for (int i = 0; i < 5; ++i) {
        lobby.players.push_back(make_menu_lobby_player(
            static_cast<std::uint8_t>(i),
            i == 0 ? "-I.ron Host" : "REMOTE COMPANY"));
    }
    lobby.local_indices = {4};
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kBaseCampSeatCardBase;

    // Five seats create the two-page rail. With [+] unavailable, GO's up
    // link must land on NEXT, and punctuation is ignored in the compact
    // remote-company abbreviation.
    buttons[kBaseCampAddSeatIndex].hidden = true;
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("P1 IRO ", buttons[kBaseCampSeatCardBase].label);
    EXPECT_EQ(kBaseCampSeatPageNextIndex,
              buttons[kCreateMenuGoIndex].nav.up);

    // #236: the ordinal the '+' vacated answers nothing at all.
    EXPECT_EQ(0, spec.on_spec_row(kBaseCampSeatRailSpareIndex, &state));

    const int untouched_highlight = 123;
    int defensive_highlight = untouched_highlight;
    spec.nav.rewire(nullptr, count, defensive_highlight);
    EXPECT_EQ(untouched_highlight, defensive_highlight)
        << "an unavailable descriptor surface is a defensive no-op";

#ifndef DISABLE_MULTIPLAYER
    // Each denial is separately observable and must avoid calling the next
    // layer. Rewind the debounce stamp so the capacity rule is what decides.
    state.last_seat_add_ms = -1;
    lobby.local_seats = MAX_PLAYERS;
    lobby.add_seat_calls = 0;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampAddSeatIndex, &state));
    EXPECT_EQ(0, lobby.add_seat_calls);
    EXPECT_TRUE(trace_contains("popup", "LOCAL LIMIT IS 4"));

    state.last_seat_add_ms = -1;
    lobby.local_seats = 1;
    lobby.players.clear();
    for (std::size_t i = 0; i < og::sim::kMaxGlobalPlayers; ++i) {
        lobby.players.push_back(make_menu_lobby_player(
            static_cast<std::uint8_t>(i), "FULL LOBBY"));
    }
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampAddSeatIndex, &state));
    EXPECT_EQ(0, lobby.add_seat_calls);
    EXPECT_TRUE(trace_contains("popup", "LOBBY FULL"));

    state.last_seat_add_ms = -1;
    lobby.players = {make_menu_lobby_player(0, "LOCAL COMPANY")};
    lobby.local_indices = {0};
    lobby.add_seat_result = false;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampAddSeatIndex, &state));
    EXPECT_EQ(1, lobby.add_seat_calls);
    EXPECT_TRUE(trace_contains("popup", "ADD DENIED"));
#endif
}

// §7.1: the seat editor's ZOOM + HUD rows flip the seat's runtime prefs
// (viewob[slot] when live), mirror to cfg, and keep both label surfaces
// live through the rewire; seats without a live viewscreen carry cfg alone.
TEST(MenuEngine, seat_settings_hud_and_zoom_rows_toggle_and_persist)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    FakeLobbyClient lobby;
    lobby.networked = false;
    lobby.local_seats = 1;
    lobby.players = {make_menu_lobby_player(0, "LOCAL COMPANY")};
    lobby.local_indices = {0};
    og::ui::install_active_picker_lobby_client(&lobby);

    screen& output = *og::runtime::current_session->myscreen_;
    viewscreen* const view = output.viewob[0].get();
    ASSERT_NE(nullptr, view);
    // Save/restore the §7.1 cfg keys + live prefs this test touches.
    struct Saved {
        std::array<std::pair<std::string, std::string>, 12> cfg_keys;
        signed char radar, life;
        Sint32 zoom;
    } saved;
    int at = 0;
    for (int player : {1, 3})
        for (const char* suffix :
             {"hud_radar", "hud_life", "hud_foes", "hud_score", "view_zoom",
              "hud_migrated"})
        {
            const std::string key =
                std::format("player{}_{}", player, suffix);
            saved.cfg_keys[static_cast<std::size_t>(at++)] = {
                key, cfg.get_setting("controls", key)};
        }
    saved.radar = view->prefs[PREF_RADAR];
    saved.life = view->prefs[PREF_LIFE];
    saved.zoom = view->view_zoom_step_;

    og::ui::SeatSettingsScreenState state{
        .seat_id = lobby.players.front().seat_id,
        .player_index = lobby.players.front().player_index,
        .local_slot = 0,
    };
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec_mp();
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    int highlighted = kSeatSettingsModeIndex;

    // RADAR: prefs flip on viewob[0], cfg mirror, label flip on the rewire.
    view->prefs[PREF_RADAR] = PREF_RADAR_ON;
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("RADAR: ON", buttons[kSeatSettingsHudRadarRowMP].label);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsHudRadarIndex, &state));
    EXPECT_EQ(PREF_RADAR_OFF, view->prefs[PREF_RADAR]);
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_radar"));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("RADAR: OFF", buttons[kSeatSettingsHudRadarRowMP].label);

    // HP normalization through the seat screen: legacy SMALL -> OFF -> BOTH.
    view->prefs[PREF_LIFE] = PREF_LIFE_SMALL;
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("HP: ON", buttons[kSeatSettingsHudLifeRowMP].label);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsHudLifeIndex, &state));
    EXPECT_EQ(PREF_LIFE_OFF, view->prefs[PREF_LIFE]);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kSeatSettingsHudLifeIndex, &state));
    EXPECT_EQ(PREF_LIFE_BOTH, view->prefs[PREF_LIFE]);

    // ZOOM: the cycle advances and persists; the label rides the rewire.
    ASSERT_TRUE(og::ui::per_view_zoom_available());
    view->view_zoom_step_ = 0;
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSeatSettingsZoomIndex, &state));
    EXPECT_EQ(1, static_cast<int>(view->view_zoom_step_));
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_view_zoom"));
    spec.nav.rewire(buttons, count, highlighted);
    EXPECT_EQ("ZOOM: 0.9X", buttons[kSeatSettingsZoomRowMP].label);

    // A seat WITHOUT a live viewscreen (Base Camp slot beyond numviews):
    // cfg is the carrier, and the label reads it back.
    og::ui::toggle_player_hud_row(2, og::ui::PlayerHudRow::Radar);
    EXPECT_EQ("0", cfg.get_setting("controls", "player3_hud_radar"));
    EXPECT_EQ("RADAR: OFF",
              og::ui::player_hud_row_label(2, og::ui::PlayerHudRow::Radar));
    og::ui::toggle_player_hud_row(2, og::ui::PlayerHudRow::Radar);
    EXPECT_EQ("1", cfg.get_setting("controls", "player3_hud_radar"));

    for (const auto& [key, value] : saved.cfg_keys)
        cfg.apply_setting("controls", key, value);
    // The handlers persisted mid-test values to disk; re-save the restored
    // ones so later binaries never inherit toggled HUD keys.
    cfg.save_settings();
    view->prefs[PREF_RADAR] = saved.radar;
    view->prefs[PREF_LIFE] = saved.life;
    view->view_zoom_step_ = saved.zoom;
    og::ui::install_seat_settings_state_for_screen(nullptr);
}

TEST(MenuEngine, networked_seat_editor_and_scenario_propagate_remote_start)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    FakeLobbyClient lobby;
    lobby.networked = true;
    lobby.host = false;
    lobby.local_seats = 1;
    lobby.set_ready_result = true;
    lobby.players = {make_menu_lobby_player(0, "LOCAL COMPANY")};
    lobby.local_indices = {0};
    og::ui::install_active_picker_lobby_client(&lobby);

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    og::ui::install_base_camp_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;

    g_start_game_requested = true;
    pks().selected_menu_item = nullptr;
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(kBaseCampSeatCardBase, &state));
    ASSERT_EQ(1u, lobby.ready_requests.size());
    EXPECT_FALSE(lobby.ready_requests.front())
        << "opening a networked seat editor withdraws READY first";
    ASSERT_NE(nullptr, pks().selected_menu_item);
    EXPECT_EQ(og::ui::PickerMenuCommand::StartGame,
              pks().selected_menu_item->command);

    pks().selected_menu_item = nullptr;
    EXPECT_EQ(MENU_EXIT, create_scenario_menu(0))
        << "the SCENARIO wrapper must preserve a remote structural exit";
    ASSERT_NE(nullptr, pks().selected_menu_item);
    EXPECT_EQ(og::ui::PickerMenuCommand::StartGame,
              pks().selected_menu_item->command);
}

TEST(MenuEngine, base_camp_draw_clips_headers_skips_stale_rows_and_locks_team)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    FakeLobbyClient lobby;
    lobby.networked = false;
    og::ui::install_active_picker_lobby_client(&lobby);

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    SavedRoster saved_roster(save);
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "VISIBLE";
    save.team_list[0]->teamnum = 1;
    save.team_size = 1;
    save.numplayers = 1;

    og::ui::BaseCampScreenState state;
    og::ui::base_camp_refresh_rows(state);
    ASSERT_EQ(1u, state.slots.size());
    const og::ui::MenuScreenSpec& spec =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).spec;
    screen& output = *og::runtime::current_session->myscreen_;

    const std::string visible_prefix = "ABCDEFGHIJKLMNOPQRSTUVWXY";
    ASSERT_EQ(25u, visible_prefix.size());
    save.save_name = visible_prefix + "Z";
    output.clearbuffer();
    const std::uint64_t blank_header =
        indexed_region_hash(output, 0, 0, 244, 12);
    spec.draw_content(&state);
    const std::uint64_t clipped_header =
        indexed_region_hash(output, 0, 0, 244, 12);
    EXPECT_NE(blank_header, clipped_header);

    save.save_name = visible_prefix + "Z-THIS-SUFFIX-MUST-NOT-DRAW";
    output.clearbuffer();
    spec.draw_content(&state);
    EXPECT_EQ(clipped_header, indexed_region_hash(output, 0, 0, 244, 12));
    EXPECT_EQ(visible_prefix + "Z-THIS-SUFFIX-MUST-NOT-DRAW",
              save.save_name)
        << "display clipping must not mutate persisted company identity";

    output.clearbuffer();
    const std::uint64_t blank_row =
        indexed_region_hash(output, 20, 45, 310, 55);
    spec.draw_content(&state);
    EXPECT_NE(blank_row, indexed_region_hash(output, 20, 45, 310, 55))
        << "the valid soldier draws its team chip and roster text";
    og::ui::BaseCampScreenState stale = state;
    stale.slots.front().save_slot = MAX_TEAM_SIZE;
    output.clearbuffer();
    spec.draw_content(&stale);
    EXPECT_EQ(blank_row, indexed_region_hash(output, 20, 45, 310, 55))
        << "a stale owned slot is skipped instead of aliasing roster memory";

    const og::ui::MenuScreenSpec& scenario =
        *og::ui::menu_screen_host(og::ui::MenuScreenId::Scenario).spec;
    ASSERT_NE(nullptr, scenario.draw_content);
    button* scenario_buttons = scenario.buttons_accessor();
    const int campaign_y =
        scenario_buttons[kScenarioMenuSetCampaignIndex].y + 4;
    output.clearbuffer();
    const std::uint64_t blank_campaign =
        indexed_region_hash(output, 114, campaign_y - 1,
                            310, campaign_y + 7);
    save.current_campaign = "gladiator";
    scenario.draw_content(nullptr);
    const std::uint64_t gladiator_campaign =
        indexed_region_hash(output, 114, campaign_y - 1,
                            310, campaign_y + 7);
    EXPECT_NE(blank_campaign, gladiator_campaign);
    output.clearbuffer();
    save.current_campaign = "modes";
    scenario.draw_content(nullptr);
    const std::uint64_t ctf_campaign =
        indexed_region_hash(output, 114, campaign_y - 1,
                            310, campaign_y + 7);
    EXPECT_NE(blank_campaign, ctf_campaign);
    EXPECT_NE(gladiator_campaign, ctf_campaign)
        << "the scenario strip must render the selected campaign title";

    og::ui::g_picker_save_slot_editable_callback =
        [](int slot) { return slot != 0; };
    const short old_team = save.team_list[0]->teamnum;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(kBaseCampTeamChipBase, &state));
    EXPECT_EQ(old_team, save.team_list[0]->teamnum);
    EXPECT_TRUE(trace_contains("popup", "LOCKED"));
}

TEST(MenuEngine, company_dispatch_surfaces_invalid_open_delete_and_restore)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;

    const og::ui::MenuScreenSpec& company_spec =
        og::ui::company_list_menu_screen_spec();
    og::ui::CompanyListScreenState companies;
    companies.companies.push_back(
        make_company_info("../invalid-company", 1234, true));
    companies.page = og::ui::PageModel::make(1, 8);

    trace_clear();
    EXPECT_EQ(MENU_REDRAW, company_spec.on_spec_row(0, &companies));
    EXPECT_FALSE(companies.opened);
    EXPECT_TRUE(trace_contains("popup", "COMPANY FILE DAMAGED"))
        << "a display-valid row whose file cannot validate stays unopened";

    picker_testing_yes_or_no_queue_push(true);
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, company_spec.on_spec_row(16, &companies));
    ASSERT_EQ(1u, companies.companies.size());
    EXPECT_TRUE(trace_contains("popup", "DELETE FAILED"))
        << "an API-level delete refusal remains visible";

    const og::ui::MenuScreenSpec& backup_spec =
        og::ui::company_backups_menu_screen_spec();
    og::ui::CompanyBackupsScreenState paged;
    paged.slot = "paged";
    for (int i = 0; i < 11; ++i)
        paged.backups.push_back(make_backup_info("paged", 11 - i, i, true));
    paged.page = og::ui::PageModel::make(11, 10);
    trace_clear();
    EXPECT_EQ(MENU_OK, backup_spec.on_spec_row(12, &paged));
    EXPECT_EQ(1, paged.page.page);
    EXPECT_TRUE(trace_contains("company_backups", "page 2/2"));

    og::ui::CompanyBackupsScreenState invalid;
    invalid.slot = "../invalid-backup";
    invalid.backups.push_back(
        make_backup_info("invalid-backup", 1, 1234, true));
    invalid.page = og::ui::PageModel::make(1, 10);
    picker_testing_yes_or_no_queue_push(true);
    trace_clear();
    EXPECT_EQ(MENU_REDRAW, backup_spec.on_spec_row(0, &invalid));
    EXPECT_FALSE(invalid.opened);
    EXPECT_TRUE(invalid.backups.empty())
        << "a failed restore re-scans the authoritative snapshot list";
    EXPECT_EQ(0, invalid.page.page);
    EXPECT_TRUE(trace_contains("popup", "BACKUP FILE DAMAGED"));
}

TEST(MenuEngine, company_wrappers_reject_a_missing_screen_without_mutation)
{
    EngineTestGuard engine_guard;
    MenuCallbackStateGuard callback_guard;
    MissingScreenGuard missing_screen;

    std::string name = "UNCHANGED";
    EXPECT_FALSE(og::ui::run_new_company_name_entry(name));
    EXPECT_EQ("UNCHANGED", name);
    EXPECT_FALSE(og::ui::run_company_list_screen());
    EXPECT_FALSE(og::ui::run_company_backups_screen(
        "missing", "MISSING COMPANY"));
    EXPECT_EQ(MENU_EXIT, mainmenu(0));
}
