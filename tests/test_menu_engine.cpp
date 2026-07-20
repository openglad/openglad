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
        "display_settings", "control_options", "main_options",
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
            // Build-gated rows (the main-menu quit/help fork) drop at
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
            context.is_host = host_visible;
            for (int i = 0; i < count; ++i) {
                const og::ui::MenuButtonSpec& row =
                    *spec_rows[static_cast<std::size_t>(i)];
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
    EXPECT_GE(engine_screens, 17)
        << "difficulty + the FX trio + display + controls + main options + "
           "main menu + the full team-build cluster (team build, view team, "
           "slot menus, SCENARIO, TEAMS) + hire + train + progress + "
           "view level must be engine-hosted by this stage";
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
              og::ui::menu_screen_host(og::ui::MenuScreenId::ControlSettings).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions).kind);
}

// ---------------------------------------------------------------------------
// §1.8 step 5 registry state: the team-build cluster migrates in order
// (view team + slot menus, then SCENARIO + TEAMS, then team build itself).
// Updated in the SAME commit as each registry flip.
TEST(MenuEngine, team_build_cluster_registry_hosts)
{
    using Kind = og::ui::MenuScreenHost::Kind;
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::ViewTeam).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::SaveSlots).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::LoadSlots).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Scenario).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).kind);
}

// The exit_on_redraw contract on the migrated cluster: BACK on these
// subscreens carries MENU_REDRAW and must END the screen (the parent loop
// keeps running), while MENU_EXIT-bearing exits still propagate. Pinned on
// the spec flags — the direct-call behavior is covered by the unchanged
// legacy tests (test_view_team / test_save_menu).
TEST(MenuEngine, team_build_cluster_exit_semantics_pins)
{
    const og::ui::MenuScreenSpec* view_team =
        og::ui::menu_screen_host(og::ui::MenuScreenId::ViewTeam).spec;
    ASSERT_NE(nullptr, view_team);
    EXPECT_TRUE(view_team->exit_on_redraw);
    EXPECT_EQ(MENU_EXIT, view_team->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
              view_team->remote_start);

    for (const og::ui::MenuScreenId id : {og::ui::MenuScreenId::SaveSlots,
                                          og::ui::MenuScreenId::LoadSlots}) {
        const og::ui::MenuScreenSpec* slots = og::ui::menu_screen_host(id).spec;
        ASSERT_NE(nullptr, slots);
        EXPECT_TRUE(slots->exit_on_redraw);
        EXPECT_EQ(MENU_REDRAW, slots->exit_value);
        EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope,
                  slots->remote_start);
        EXPECT_EQ(10, slots->default_highlight);
    }

    // TEAMS: BACK carries MENU_REDRAW (redraw-exit); MENU_EXIT propagates.
    const og::ui::MenuScreenSpec* teams =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).spec;
    ASSERT_NE(nullptr, teams);
    EXPECT_TRUE(teams->exit_on_redraw);
    EXPECT_EQ(MENU_EXIT, teams->exit_value);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, teams->remote_start);
    EXPECT_NE(nullptr, teams->frame_tick) << "TEAMS level-reload guard";

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
    EXPECT_EQ(1, team_build->default_highlight);
    EXPECT_NE(nullptr, team_build->frame_tick);
    EXPECT_NE(nullptr, team_build->on_reset);
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
    // start was selected); nested DETAILS/RENAME/VIEW TEAM MENU_REDRAWs are
    // consumed by reset_buttons; the +/- pixie faces are art bindings.
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

// ---------------------------------------------------------------------------
// MAIN OPTIONS content-draw index pins + the sprite-sheet label restore.
// The draw hook reads rows [1]/[2]/[3]/[6] by ordinal (sound face, section
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
    EXPECT_EQ("pick_sprite_sheet", buttons[6].id);

    const og::ui::LabelFormatter sprite_formatter =
        spec.rows[6].label_binding.formatter;
    ASSERT_NE(nullptr, sprite_formatter)
        << "the sprite-sheet dual-surface label restore must be a binding";
    og::ui::MenuLabelContext context;
    EXPECT_EQ("Sprite Sheet", sprite_formatter(context));
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
    ASSERT_EQ(7, count);
    EXPECT_EQ("display_back", buttons[kDisplayMenuBackIndex].id);
    EXPECT_EQ("display_mode", buttons[kDisplayMenuModeIndex].id);
    EXPECT_EQ("display_resolution", buttons[kDisplayMenuResolutionIndex].id);
    EXPECT_EQ("overscan_minus", buttons[kDisplayMenuOverscanMinusIndex].id);
    EXPECT_EQ("overscan_plus", buttons[kDisplayMenuOverscanPlusIndex].id);
    EXPECT_EQ("display_zoom", buttons[kDisplayMenuZoomIndex].id);
    EXPECT_EQ("display_smoothing", buttons[kDisplayMenuSmoothingIndex].id);

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
        EXPECT_FALSE(got.hidden) << shape_name << " " << got.id;
        EXPECT_FALSE(got.no_draw) << shape_name << " " << got.id;
    }
    // The options gear is the last row of every variant.
    EXPECT_EQ("options", rows.back().id) << shape_name;
}

// The MP chassis shared by the native/web fork (rows 0-8); row 9 is the fork.
constexpr ExpectedSpecRow kMainMenuMPCommon[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20,
     ButtonAction::BeginMenu, 1, MenuNav{.down = 1}},
    {"continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20,
     ButtonAction::CreateTeamMenu, -1, MenuNav{.up = 0, .down = 5}},
    {"4_player", "4 PLAYER", KEYSTATE_4, 152, 125, 68, 20,
     ButtonAction::SetPlayerMode, 4, MenuNav{.up = 4, .down = 6, .left = 3}},
    {"3_player", "3 PLAYER", KEYSTATE_3, 80, 125, 68, 20,
     ButtonAction::SetPlayerMode, 3, MenuNav{.up = 5, .down = 6, .right = 2}},
    {"2_player", "2 PLAYER", KEYSTATE_2, 152, 100, 68, 20,
     ButtonAction::SetPlayerMode, 2, MenuNav{.up = 1, .down = 2, .left = 5}},
    {"1_player", "1 PLAYER", KEYSTATE_1, 80, 100, 68, 20,
     ButtonAction::SetPlayerMode, 1, MenuNav{.up = 1, .down = 3, .right = 4}},
    {"difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 148, 140, 10,
     ButtonAction::OpenDifficultyMenu, -1, MenuNav{.up = 3, .down = 7}},
    {"pvp_allied", "PVP: Allied", KEYSTATE_UNKNOWN, 80, 160, 68, 10,
     ButtonAction::AlliedMode, -1, MenuNav{.up = 6, .down = 9, .right = 8}},
    {"level_edit", "Level Edit", KEYSTATE_UNKNOWN, 152, 160, 68, 10,
     ButtonAction::DoLevelEdit, -1, MenuNav{.up = 6, .down = 10, .left = 7}},
};

// The trailing space in "QUIT " is part of the shipped label.
constexpr ExpectedSpecRow kMainMenuMPQuit = {
    "quit", "QUIT ", KEYSTATE_ESCAPE, 120, 182, 60, 15,
    ButtonAction::QuitMenu, 0, MenuNav{.up = 7, .left = 10}};
constexpr ExpectedSpecRow kMainMenuMPHelp = {
    "help", "HELP", KEYSTATE_UNKNOWN, 120, 182, 60, 15,
    ButtonAction::ShowHelp, -1, MenuNav{.up = 7, .left = 10}};
constexpr ExpectedSpecRow kMainMenuMPOptions = {
    "options", "", KEYSTATE_UNKNOWN, 90, 182, 20, 15,
    ButtonAction::MainOptions, -1, MenuNav{.up = 8, .right = 9}};

// The no-MP chassis (single column, bottom row at y=175); row 4 is the fork.
constexpr ExpectedSpecRow kMainMenuNoMPCommon[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20,
     ButtonAction::BeginMenu, 1, MenuNav{.down = 1}},
    {"continue_game", "CONTINUE GAME", KEYSTATE_UNKNOWN, 80, 75, 140, 20,
     ButtonAction::CreateTeamMenu, -1, MenuNav{.up = 0, .down = 2}},
    {"difficulty", "DIFFICULTY", KEYSTATE_UNKNOWN, 80, 100, 140, 15,
     ButtonAction::OpenDifficultyMenu, -1, MenuNav{.up = 1, .down = 3}},
    {"level_edit", "Level Edit", KEYSTATE_UNKNOWN, 80, 118, 140, 15,
     ButtonAction::DoLevelEdit, -1, MenuNav{.up = 2, .down = 4}},
};

constexpr ExpectedSpecRow kMainMenuNoMPQuit = {
    "quit", "QUIT ", KEYSTATE_ESCAPE, 120, 175, 60, 15,
    ButtonAction::QuitMenu, 0, MenuNav{.up = 3, .left = 5}};
constexpr ExpectedSpecRow kMainMenuNoMPHelp = {
    "help", "HELP", KEYSTATE_UNKNOWN, 120, 175, 60, 15,
    ButtonAction::ShowHelp, -1, MenuNav{.up = 3, .left = 5}};
constexpr ExpectedSpecRow kMainMenuNoMPOptions = {
    "options", "", KEYSTATE_UNKNOWN, 90, 175, 20, 15,
    ButtonAction::MainOptions, -1, MenuNav{.up = 3, .right = 4}};

std::vector<ExpectedSpecRow> build_expected_shape(
    const ExpectedSpecRow* common, int common_count,
    const ExpectedSpecRow& fork, const ExpectedSpecRow& options)
{
    std::vector<ExpectedSpecRow> shape(common, common + common_count);
    shape.push_back(fork);
    shape.push_back(options);
    return shape;
}

} // namespace

// Registry + spec-shape pins. The main menu is the heaviest 10a screen: its
// legacy loop HAD a remote-start check (MainScope, break-with-selection), a
// fade-bracketed entry, live right-click (spectator deselect), and returns
// an exit-bearing value every caller ignores.
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
    EXPECT_TRUE(spec.right_click_enabled);
    EXPECT_EQ(1, spec.default_highlight);  // continue_game, as the legacy loop
    EXPECT_EQ(MENU_EXIT, spec.exit_value);

    // The options-index helper that retired both OPTIONS_BUTTON_INDEX
    // #defines: always the last materialized row.
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_GT(count, 0);
    EXPECT_EQ(count - 1, picker_mainmenu_options_index());
    EXPECT_EQ("options", buttons[picker_mainmenu_options_index()].id);
}

// G9: the four materialized shapes, re-derived from the two specs. The
// compiled shape is independently pinned by test_menu_pins (G11); the other
// three have no legacy oracle anymore — these pins are it.
TEST(MenuEngine, main_menu_four_variant_materialization_pins)
{
    using og::ui::MenuBuildVariant;

    const std::vector<ExpectedSpecRow> mp_native = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPQuit, kMainMenuMPOptions);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Native, mp_native.data(),
                             static_cast<int>(mp_native.size()),
                             "mainmenu_mp_native");

    const std::vector<ExpectedSpecRow> mp_web = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPHelp, kMainMenuMPOptions);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Web, mp_web.data(),
                             static_cast<int>(mp_web.size()),
                             "mainmenu_mp_web");

    const std::vector<ExpectedSpecRow> nomp_native = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPQuit, kMainMenuNoMPOptions);
    check_materialized_shape(og::ui::main_menu_screen_spec_nomp(),
                             MenuBuildVariant::Native, nomp_native.data(),
                             static_cast<int>(nomp_native.size()),
                             "mainmenu_nomp_native");

    const std::vector<ExpectedSpecRow> nomp_web = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPHelp, kMainMenuNoMPOptions);
    check_materialized_shape(og::ui::main_menu_screen_spec_nomp(),
                             MenuBuildVariant::Web, nomp_web.data(),
                             static_cast<int>(nomp_web.size()),
                             "mainmenu_nomp_web");
}

// The bindings that replaced redraw_mainmenu's raw allbuttons_[N] writes
// (§1.6): the player-count outlines, the SPECTATOR/PVP label, and the two
// pixie art faces (normal1.png Begin New Game, wrench Options) — the art is
// re-applied by the runner after init_buttons and after every reset.
TEST(MenuEngine, main_menu_binding_pins)
{
    const og::ui::MenuScreenSpec& mp = og::ui::main_menu_screen_spec_mp();
    const og::ui::MenuScreenSpec& nomp = og::ui::main_menu_screen_spec_nomp();

    // Art faces, both variants: row 0 begin_new_game, last row options.
    EXPECT_EQ(FAMILY_NORMAL1, mp.rows[0].art_family);
    EXPECT_EQ(FAMILY_WRENCH, mp.rows[mp.row_count - 1].art_family);
    EXPECT_EQ(FAMILY_NORMAL1, nomp.rows[0].art_family);
    EXPECT_EQ(FAMILY_WRENCH, nomp.rows[nomp.row_count - 1].art_family);

    // Player-count outlines: spec rows 2..5 are 4/3/2/1 PLAYER.
    for (int i = 2; i <= 5; ++i) {
        EXPECT_EQ(og::ui::MenuOutlineBinding::PlayerCountEquals,
                  mp.rows[i].outline)
            << mp.rows[i].id;
        EXPECT_EQ(mp.rows[i].arg, mp.rows[i].outline_arg) << mp.rows[i].id;
    }

    // The pvp_allied live label: SPECTATOR at numplayers==0, else the
    // shared allied-mode formatter (redraw_mainmenu's write, verbatim).
    const og::ui::LabelFormatter pvp_formatter =
        mp.rows[7].label_binding.formatter;
    ASSERT_NE(nullptr, pvp_formatter);
    SaveData save;
    og::ui::MenuLabelContext context;
    context.save = &save;
    save.numplayers = 0;
    EXPECT_EQ("SPECTATOR", pvp_formatter(context));
    save.numplayers = 1;
    save.allied_mode = 1;
    EXPECT_EQ("PVP: Ally", pvp_formatter(context));
    save.allied_mode = 0;
    EXPECT_EQ("PVP: Enemy", pvp_formatter(context));

    // The no-MP variant carries none of the MP-only bindings.
    for (int i = 0; i < nomp.row_count; ++i) {
        EXPECT_EQ(og::ui::MenuOutlineBinding::None, nomp.rows[i].outline)
            << nomp.rows[i].id;
        EXPECT_EQ(nullptr, nomp.rows[i].label_binding.formatter)
            << nomp.rows[i].id;
    }
}

// The CONTROLS mode faces: the LabelBindings must track the live control
// mode exactly as the legacy loop's per-frame writes did.
TEST(MenuEngine, control_options_mode_label_bindings_follow_the_mode)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::ControlSettings);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    const og::ui::MenuScreenSpec& spec = *host.spec;

    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_EQ(10, count);

    og::ui::MenuLabelContext context;
    for (int player = 0; player < 4; ++player) {
        const int mode_index = 1 + player * 2;
        EXPECT_EQ("player" + std::to_string(player + 1) + "_mode",
                  buttons[mode_index].id);
        const og::ui::LabelFormatter formatter =
            spec.rows[mode_index].label_binding.formatter;
        ASSERT_NE(nullptr, formatter) << buttons[mode_index].id;

        const int saved_mode = get_player_control_mode(player);
        set_player_control_mode(
            player, static_cast<int>(ControlDirectionMode::FourDirection));
        EXPECT_EQ("4-DIRECTION", formatter(context)) << buttons[mode_index].id;
        set_player_control_mode(
            player, static_cast<int>(ControlDirectionMode::EightDirection));
        EXPECT_EQ("8-DIRECTION", formatter(context)) << buttons[mode_index].id;
        set_player_control_mode(player, saved_mode);
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
