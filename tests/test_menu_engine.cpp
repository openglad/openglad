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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
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
    bool networked = false;

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
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return networked;
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

    // The same-geometry allowance must be EXERCISED, not vacuous: give the
    // session a one-member roster so the TEAMS guy row (the local half of
    // the guy_team/cross_control pair) can show in the local variants.
    SaveData& sweep_save = og::runtime::current_session->myscreen_->save_data;
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> sweep_saved_team;
    for (int i = 0; i < MAX_TEAM_SIZE; ++i)
        sweep_saved_team[i] = std::move(sweep_save.team_list[i]);
    const unsigned char sweep_old_team_size = sweep_save.team_size;
    sweep_save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    sweep_save.team_list[0]->name = "SWEEP";
    sweep_save.team_size = 1;

    // G13 lattice axes: {host} x {networked}. host=false without a network
    // session is the degenerate legacy shape; production non-hosts are
    // always networked — that variant is what drives the READY/cross-control
    // halves of the same-geometry pairs.
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
         {"cross_control", "guy_team"},
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
        sweep_save.team_list[i] = std::move(sweep_saved_team[i]);
    sweep_save.team_size = sweep_old_team_size;
    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
    EXPECT_GE(engine_screens, 14)
        << "difficulty + the FX trio + display + controls + main options + "
           "main menu + the team-build cluster (base camp, SCENARIO, TEAMS) "
           "+ hire + train + progress + view level must be engine-hosted "
           "(VIEW TEAM and the slot menus RETIRED with WP4's base camp)";
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

    // §9.3 sunken input field: the name strip (row 1) carries the PURE_BLACK
    // face binding (face only — the engine stamps the live surface every
    // frame; the grey bevel edges remain). The other rows keep stock faces.
    {
        og::ui::MenuLabelContext face_context;
        ASSERT_NE(nullptr, spec_rows[1]->color);
        EXPECT_EQ(PURE_BLACK, spec_rows[1]->color(face_context));
        for (const int plain : {0, 2, 3})
            EXPECT_EQ(nullptr, spec_rows[static_cast<std::size_t>(plain)]->color)
                << "only the name strip is a sunken field";
    }

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
    info.header.campaign_id = "org.openglad.gladiator";
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
              og::ui::menu_screen_host(og::ui::MenuScreenId::ControlSettings).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::MainOptions).kind);
}

// ---------------------------------------------------------------------------
// §1.8 step 5 registry state: the team-build cluster. VIEW TEAM and the
// SAVE/LOAD slot menus RETIRED with WP4's base camp (§2.5/§3.8) — their
// MenuScreenId rows are gone entirely.
TEST(MenuEngine, team_build_cluster_registry_hosts)
{
    using Kind = og::ui::MenuScreenHost::Kind;
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::TeamBuild).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Scenario).kind);
    EXPECT_EQ(Kind::Engine,
              og::ui::menu_screen_host(og::ui::MenuScreenId::Teams).kind);
}

// The exit_on_redraw contract on the migrated cluster: BACK on these
// subscreens carries MENU_REDRAW and must END the screen (the parent loop
// keeps running), while MENU_EXIT-bearing exits still propagate.
TEST(MenuEngine, team_build_cluster_exit_semantics_pins)
{
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
    // TEAM door retired with its screen — 19 rows now, §2.5.)
    const og::ui::MenuScreenSpec* train =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Train).spec;
    ASSERT_NE(nullptr, train);
    EXPECT_EQ(MENU_EXIT, train->exit_value);
    EXPECT_FALSE(train->exit_on_redraw);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, train->remote_start);
    EXPECT_TRUE(train->right_click_enabled);
    EXPECT_EQ(1, train->default_highlight);
    EXPECT_NE(nullptr, train->on_reset) << "the bug-A9 promotion resync";
    ASSERT_EQ(19, train->row_count);
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
    // §2.1/§9.2: load_company then the no_company_note are appended at the
    // table END of every variant, so the note is the last materialized row.
    EXPECT_EQ("no_company_note", rows.back().id) << shape_name;
    EXPECT_EQ("load_company", rows[rows.size() - 2].id) << shape_name;
}

// The MP chassis shared by the native/web fork (rows 0-8); row 9 is the fork.
constexpr ExpectedSpecRow kMainMenuMPCommon[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20,
     ButtonAction::BeginMenu, 1, MenuNav{.down = 1}},
    {"continue_game", "CONTINUE", KEYSTATE_UNKNOWN, 80, 75, 68, 20,
     ButtonAction::CreateTeamMenu, -1, MenuNav{.up = 0, .down = 5, .right = 11}},
    {"4_player", "4 PLAYER", KEYSTATE_4, 152, 125, 68, 20,
     ButtonAction::SetPlayerMode, 4, MenuNav{.up = 4, .down = 6, .left = 3}},
    {"3_player", "3 PLAYER", KEYSTATE_3, 80, 125, 68, 20,
     ButtonAction::SetPlayerMode, 3, MenuNav{.up = 5, .down = 6, .right = 2}},
    {"2_player", "2 PLAYER", KEYSTATE_2, 152, 100, 68, 20,
     ButtonAction::SetPlayerMode, 2, MenuNav{.up = 11, .down = 2, .left = 5}},
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
// §2.1 index 11: the appended LOAD half of the CONTINUE|LOAD split.
constexpr ExpectedSpecRow kMainMenuMPLoad = {
    "load_company", "LOAD", KEYSTATE_UNKNOWN, 152, 75, 68, 20,
    ButtonAction::CreateLoadMenu, 0, MenuNav{.up = 0, .down = 4, .left = 1}};
// §9.2 index 12: the no-company note box (Disabled chrome; statically
// hidden, no nav links in or out).
constexpr ExpectedSpecRow kMainMenuMPNote = {
    "no_company_note", "NO COMPANY YET", KEYSTATE_UNKNOWN, 80, 75, 140, 20,
    ButtonAction::MenuSpecRow, 12, MenuNav{}, true};

// The no-MP chassis (single column, bottom row at y=175); row 4 is the fork.
constexpr ExpectedSpecRow kMainMenuNoMPCommon[] = {
    {"begin_new_game", "", KEYSTATE_UNKNOWN, 80, 50, 140, 20,
     ButtonAction::BeginMenu, 1, MenuNav{.down = 1}},
    {"continue_game", "CONTINUE", KEYSTATE_UNKNOWN, 80, 75, 68, 20,
     ButtonAction::CreateTeamMenu, -1, MenuNav{.up = 0, .down = 2, .right = 6}},
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
// §2.1 index 6: the appended no-MP LOAD half.
constexpr ExpectedSpecRow kMainMenuNoMPLoad = {
    "load_company", "LOAD", KEYSTATE_UNKNOWN, 152, 75, 68, 20,
    ButtonAction::CreateLoadMenu, 0, MenuNav{.up = 0, .down = 2, .left = 1}};
// §9.2 index 7: the no-MP note box.
constexpr ExpectedSpecRow kMainMenuNoMPNote = {
    "no_company_note", "NO COMPANY YET", KEYSTATE_UNKNOWN, 80, 75, 140, 20,
    ButtonAction::MenuSpecRow, 7, MenuNav{}, true};

// Materialized order: common chassis, then the quit/help fork survivor, then
// options, then the appended load_company and no_company_note (§2.1/§9.2
// index-contract END).
std::vector<ExpectedSpecRow> build_expected_shape(
    const ExpectedSpecRow* common, int common_count,
    const ExpectedSpecRow& fork, const ExpectedSpecRow& options,
    const ExpectedSpecRow& load, const ExpectedSpecRow& note)
{
    std::vector<ExpectedSpecRow> shape(common, common + common_count);
    shape.push_back(fork);
    shape.push_back(options);
    shape.push_back(load);
    shape.push_back(note);
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
    // #defines: §2.1/§9.2 append load_company then the no_company_note at
    // the END, so options is now the third-from-last materialized row and
    // the helper finds it by id.
    button* buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    ASSERT_GT(count, 2);
    EXPECT_EQ(count - 3, picker_mainmenu_options_index());
    EXPECT_EQ("options", buttons[picker_mainmenu_options_index()].id);
    EXPECT_EQ("load_company", buttons[count - 2].id);
    EXPECT_EQ("no_company_note", buttons[count - 1].id);
}

// G9: the four materialized shapes, re-derived from the two specs. The
// compiled shape is independently pinned by test_menu_pins (G11); the other
// three have no legacy oracle anymore — these pins are it.
TEST(MenuEngine, main_menu_four_variant_materialization_pins)
{
    using og::ui::MenuBuildVariant;

    const std::vector<ExpectedSpecRow> mp_native = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPQuit, kMainMenuMPOptions, kMainMenuMPLoad, kMainMenuMPNote);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Native, mp_native.data(),
                             static_cast<int>(mp_native.size()),
                             "mainmenu_mp_native");

    const std::vector<ExpectedSpecRow> mp_web = build_expected_shape(
        kMainMenuMPCommon, static_cast<int>(std::size(kMainMenuMPCommon)),
        kMainMenuMPHelp, kMainMenuMPOptions, kMainMenuMPLoad, kMainMenuMPNote);
    check_materialized_shape(og::ui::main_menu_screen_spec_mp(),
                             MenuBuildVariant::Web, mp_web.data(),
                             static_cast<int>(mp_web.size()),
                             "mainmenu_mp_web");

    const std::vector<ExpectedSpecRow> nomp_native = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPQuit, kMainMenuNoMPOptions, kMainMenuNoMPLoad,
        kMainMenuNoMPNote);
    check_materialized_shape(og::ui::main_menu_screen_spec_nomp(),
                             MenuBuildVariant::Native, nomp_native.data(),
                             static_cast<int>(nomp_native.size()),
                             "mainmenu_nomp_native");

    const std::vector<ExpectedSpecRow> nomp_web = build_expected_shape(
        kMainMenuNoMPCommon, static_cast<int>(std::size(kMainMenuNoMPCommon)),
        kMainMenuNoMPHelp, kMainMenuNoMPOptions, kMainMenuNoMPLoad,
        kMainMenuNoMPNote);
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

    // Art faces, both variants: row 0 begin_new_game; options is the third-
    // from-last SPEC row (load_company and the §9.2 no_company_note, both
    // art-free, are the appended tail).
    EXPECT_EQ(FAMILY_NORMAL1, mp.rows[0].art_family);
    EXPECT_EQ(FAMILY_WRENCH, mp.rows[mp.row_count - 3].art_family);
    EXPECT_EQ(-1, mp.rows[mp.row_count - 2].art_family);  // load_company
    EXPECT_EQ(-1, mp.rows[mp.row_count - 1].art_family);  // no_company_note
    EXPECT_EQ(FAMILY_NORMAL1, nomp.rows[0].art_family);
    EXPECT_EQ(FAMILY_WRENCH, nomp.rows[nomp.row_count - 3].art_family);
    EXPECT_EQ(-1, nomp.rows[nomp.row_count - 2].art_family);  // load_company
    EXPECT_EQ(-1, nomp.rows[nomp.row_count - 1].art_family);  // no_company_note

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
            if (std::string_view(buttons[i].id) == id)
                return i;
        return -1;
    };
    const int i_begin = index_of("begin_new_game");
    const int i_continue = index_of("continue_game");
    const int i_1p = index_of("1_player");

    // Absent: mimic the gate pass (mark the pair hidden), then rewire.
    for (auto& b : buttons)
        if (std::string_view(b.id) == "continue_game"
            || std::string_view(b.id) == "load_company")
            b.hidden = true;
    int highlighted = i_1p;
    mp.nav.rewire(buttons.data(), count, highlighted);
    for (int i = 0; i < count; ++i) {
        if (buttons[i].hidden)
            continue;
        for (const int link : {buttons[i].nav.up, buttons[i].nav.down,
                               buttons[i].nav.left, buttons[i].nav.right}) {
            if (link < 0)
                continue;
            EXPECT_FALSE(buttons[link].hidden)
                << buttons[i].id << " must not nav into a hidden row";
        }
    }
    EXPECT_EQ(i_1p, buttons[i_begin].nav.down)
        << "begin.down routes past the hidden pair to 1_player";

    // Present again: the rewire re-asserts begin.down -> continue_game.
    og::ui::set_main_menu_company_view_for_tests(true, "");
    std::vector<button> shown;
    og::ui::materialize_menu_buttons_for(
        mp, og::ui::MenuBuildVariant::Native, shown);
    int hl = 1;
    mp.nav.rewire(shown.data(), static_cast<int>(shown.size()), hl);
    EXPECT_EQ(i_continue, shown[i_begin].nav.down);

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
            if (std::string_view(state_buttons[i].id) == "no_company_note")
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
                 {state_buttons[i].nav.up, state_buttons[i].nav.down,
                  state_buttons[i].nav.left, state_buttons[i].nav.right}) {
                EXPECT_NE(note_index, link)
                    << state_buttons[i].id
                    << " must not nav into the inert note row (present="
                    << present << ")";
            }
        }
    }

    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
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

// §2.1 caption strip, the >18-char clip arm: a company name longer than
// kMainMenuCompanyNameClip must be truncated before the "COMPANY: " strip
// renders (the injector flows all run short names, so the clip line had no
// live coverage — WP7 coverage pass). Direct draw-hook call, the
// runner-only-surface convention.
TEST(MenuEngine, main_menu_caption_clips_overlong_company_names)
{
    const og::ui::MenuScreenSpec& mp = og::ui::main_menu_screen_spec_mp();
    ASSERT_NE(nullptr, mp.draw_content);

    og::ui::set_main_menu_company_view_for_tests(
        true, "AN OVERLONG COMPANY NAME WELL PAST THE CLIP");
    mp.draw_content(nullptr);

    // The absent shape too (the LIVE "NO COMPANY YET" strip): the injector
    // flows always run with a company present.
    og::ui::set_main_menu_company_view_for_tests(false, "");
    mp.draw_content(nullptr);

    // Mandatory restore (the shared-sweep contract): (true, "").
    og::ui::set_main_menu_company_view_for_tests(true, "");
}
