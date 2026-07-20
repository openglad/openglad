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
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>
#include "../src/interface/ui/picker_sdl_defs.h"
#include "test_interact.h"
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <atomic>
#include <set>
#include <string>
#include <vector>

extern bool g_start_game_requested;
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

} // namespace

TEST(MenuEngine, engine_screen_gate_lattice_sweep)
{
    EngineTestGuard guard;
    FakeLobbyClient lobby;
    og::ui::install_active_picker_lobby_client(&lobby);
    // Null out any live vbuttons a prior test left behind: the legacy Rewire
    // hooks write hidden state through allbuttons_ when entries are non-null.
    clear_allbuttons();

    int engine_screens = 0;
    for (int s = 0; s < static_cast<int>(og::ui::MenuScreenId::Count); ++s) {
        const og::ui::MenuScreenHost& host =
            og::ui::menu_screen_host(static_cast<og::ui::MenuScreenId>(s));
        if (host.kind != og::ui::MenuScreenHost::Kind::Engine)
            continue;
        ++engine_screens;
        const og::ui::MenuScreenSpec& spec = *host.spec;

        for (const bool host_visible : {true, false}) {
            lobby.host = host_visible;
            // Fresh materialization per variant (buttons() fills what
            // count() reads — the D3 sequencing contract).
            button* buttons = spec.buttons_accessor();
            const int count = spec.count_accessor();
            ASSERT_GT(count, 0) << spec.name;
            ASSERT_LE(count, MAX_BUTTONS) << spec.name;
            // Layer E: no build-gated rows on migrated screens yet, so spec
            // ordinals == materialized indices (nav links line up 1:1).
            ASSERT_EQ(spec.row_count, count) << spec.name;

            std::set<std::string> ids;
            for (int i = 0; i < count; ++i) {
                const button& b = buttons[i];
                EXPECT_TRUE(ids.insert(b.id).second)
                    << spec.name << " duplicate id " << b.id;
                EXPECT_FALSE(b.id.empty()) << spec.name << " row " << i;
                EXPECT_NE(0, b.myfun)
                    << spec.name << " keyboard-dead row " << b.id;
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
            context.is_host = host_visible;
            for (int i = 0; i < count; ++i) {
                const og::ui::MenuButtonSpec& row = spec.rows[i];
                const og::ui::RowState state = row.state_override != nullptr
                    ? row.state_override(context)
                    : og::ui::gate_state(row.gate, context);
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
                        << spec.name << " host=" << host_visible << ": "
                        << buttons[i].id << " overlaps " << buttons[j].id;
                }
            }

            // Nav closure + BFS reachability over the visible subgraph.
            std::vector<bool> reached(static_cast<std::size_t>(count), false);
            std::vector<int> frontier;
            ASSERT_GE(highlighted, 0) << spec.name;
            ASSERT_LT(highlighted, count) << spec.name;
            EXPECT_FALSE(buttons[highlighted].hidden)
                << spec.name << " host=" << host_visible
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
                        << spec.name << " host=" << host_visible << ": "
                        << buttons[at].id << " nav link out of range";
                    if (link < 0 || link >= count)
                        continue;
                    EXPECT_FALSE(buttons[link].hidden)
                        << spec.name << " host=" << host_visible << ": "
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
                    EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                        << spec.name << " host=" << host_visible << ": "
                        << buttons[i].id << " unreachable by keyboard";
                }
            }
        }
    }
    EXPECT_GE(engine_screens, 4)
        << "difficulty + the FX trio must be engine-hosted by this stage";
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
    EXPECT_EQ(Kind::Legacy,
              og::ui::menu_screen_host(og::ui::MenuScreenId::DisplaySettings).kind);
    EXPECT_EQ(Kind::Legacy,
              og::ui::menu_screen_host(og::ui::MenuScreenId::ControlSettings).kind);
    EXPECT_EQ(Kind::Legacy,
              og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions).kind);
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
