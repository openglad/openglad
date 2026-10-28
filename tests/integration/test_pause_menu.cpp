// PAUSED menu + in-game player screen (docs/pause-menu-design.md §2.1/§2.2).
//
// Layers, cheapest first:
//   - exact-table pins for both MenuScreenSpecs (test_menu_pins' shape),
//   - nav BFS across the gating variants (solo / splitscreen / 4-seat /
//     networked host / joiner) through the installed-state seam,
//   - player-row label derivation (local and networked seat collection),
//   - row handlers driven directly through on_spec_row with a stub host,
//   - frame-tick pump/keep-alive/Esc-edge behavior,
//   - one REAL blocking menu driven end-to-end with interact() (idiom C).

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/families/belt.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/button.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/abilities_screen.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/pause_menu.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/save_data.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_ability_select_scope.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_fake_keyboard.h"
#include "test_input_helpers.h"
#include "test_save_state_guard.h"
#include "test_interact.h"

#include <atomic>

void glad_init(bool preserve_frame_timing = false);
void glad_init(bool preserve_frame_timing,
               const og::ui::PickerLobbyGameStartConfig* lobby_config);
short new_score_panel(screen* scr, short do_it);  // score_panel.cpp, the HUD
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);
// TESTING seams: help.cpp's real-scroll-view switch and view.cpp's one-shot
// pass budget for view_team's compiled-out wait loop.
void help_testing_set_force_scroll_text(bool enabled);
void view_team_testing_set_poll_passes(int passes);
extern og::input_native::JoystickHandle joysticks[10];

// Picker entry points for the full-flow RESTART regression (the fairy_death
// idiom: picker_main blocks on the main thread, an injector drives the UI).
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
extern std::atomic<int> g_test_game_frame_ticks;

namespace
{

using og::ui::PauseMenuHost;
using og::ui::PauseMenuResult;

// ---------------------------------------------------------------------------
// Injector progress gates measured in COMPLETED MENU FRAMES, not milliseconds.
//
// A menu frame costs more wall time on a loaded box, so a flat millisecond
// ceiling turns "the machine is busy" into "the test failed" — the shape this
// suite's 5-10 s wait_for_interactable ceilings kept producing under a
// multi-agent load, on a different flow each time. Count the thing that
// actually produces the button instead:
// og::ui::menu_screen_testing_completed_frames() ticks once per completed
// run_menu_screen frame (menu_screen_runner.cpp), and both pause screens run
// through run_menu_screen.
//
// The millisecond argument survives only as a liveness backstop for a menu
// that has stopped producing frames ENTIRELY — the main thread is between
// menus, or wedged. It is restarted every time a frame completes, so a slow
// but progressing menu may take as long as the box needs, and no ceiling here
// is larger than the flat one it replaces.
//
// These stay file-local on purpose: tests/test_interact.h is shared with
// og_test_picker / og_test_view / og_test_matchup / og_test_menu_ui, and
// changing wait_for_interactable's semantics would change every injector flow
// in the repo at once.
//
// WHY A FRAME-COUNTED WAIT CAN STILL TIME OUT (measured, not inferred). A
// click whose dispatch writes the config stalls the MAIN thread inside the
// kernel, and no frame counter can tick while it is there. The INPUT / MODE /
// REMAP / RESET / ZOOM / HUD / REMOVE row handlers call
// persist_pause_player_controls() inside the click dispatch
// (src/interface/ui/pause_menu.cpp:885-899 and :904/:913/:919/:928/:959),
// which runs cfg_store::save_settings() — a whole-file libyaml emit of
// cfg/openglad.yaml on the menu thread (src/resources/gparser.cpp:324-390,
// whose Log("Saving settings") is the last line printed before the stall).
// menu_screen_runner.cpp posts the completed-frame counter AFTER that
// dispatch, so a frame blocked in the write completes nothing.
//
// Sampling /proc/<pid>/task/<main tid>/{stat,wchan} at 50 ms through a failing
// run caught the main thread in state D on balance_dirty_pages for 4.7 s, and
// 7.9 s at the next cycle, each starting the instant "Saving settings" is
// logged (200 of 216 samples in D). Paired on one box, one filter: with
// dirty-page writers running beside the suite, 60/60 runs failed; with the
// writers stopped, 72/72 passed.
//
// The backstop below is therefore doing its job — it NAMES a stalled menu
// instead of hanging the binary, and the failure is self-recovering: a re-run
// without a build beside it passes. Do not raise the 5 s / 10 s ceilings to
// paper over it. The two honest alternatives, both deliberately unshipped:
// make the backstop I/O-aware (stop accruing stalled_ms while the main
// thread's /proc state is 'D'), rejected as Linux-only machinery inside a test
// helper; or coalesce the cfg write to screen exit, which is a product design
// decision — it changes persistence semantics and breaks the four tests in
// this file that read cfg immediately after a row dispatch, plus
// docs/pause-menu-design.md §3.2.
constexpr int kPauseMenuFrameBudget = 240;

// Injector diagnostics quote the label they are waiting for verbatim, and some
// of those labels are control bytes: og::input::mapping_short_name("ARROWS")
// is include/openglad/interface/input_mappings.h's kArrowGlyphs,
// "\x01\x03\x02\x04" — the SDL font's arrow glyphs. A terminal eats
// SOH/ETX/STX/EOT, so a failed ARROWS wait printed as
//   'pause_input' reading 'INPUT: ' did not happen
// and read as an EMPTY expected label. That cost a sweep agent a whole pass
// chasing a non-existent empty named-mapping registry (wave-C N8), so render
// every non-printable byte as <hh> and let the diagnostic say what it means.
std::string escape_for_log(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        if (byte >= 0x20 && byte < 0x7f) {
            out.push_back(raw);
        } else {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "<%02X>", byte);
            out += hex;
        }
    }
    return out;
}

template <typename Ready>
bool wait_for_pause_menu_progress(const std::string& what,
                                  Ready ready,
                                  int frames,
                                  int stall_timeout_ms)
{
    std::uint64_t last_seen = og::ui::menu_screen_testing_completed_frames();
    const std::uint64_t frame_deadline =
        last_seen + static_cast<std::uint64_t>(frames);
    int stalled_ms = 0;
    for (;;) {
        if (ready())
            return true;
        const std::uint64_t now = og::ui::menu_screen_testing_completed_frames();
        if (now != last_seen) {
            last_seen = now;
            stalled_ms = 0;
        }
        if (now >= frame_deadline) {
            fprintf(stderr,
                    "  [pause] %s did not happen within %d completed menu "
                    "frames\n",
                    escape_for_log(what).c_str(), frames);
            return false;
        }
        if (stalled_ms >= stall_timeout_ms) {
            fprintf(stderr,
                    "  [pause] %s did not happen; the menu produced no frame "
                    "for %d ms\n",
                    escape_for_log(what).c_str(), stall_timeout_ms);
            return false;
        }
        SDL_Delay(50);
        stalled_ms += 50;
    }
}

bool wait_for_pause_interactable(const std::string& id,
                                 int stall_timeout_ms = 10'000,
                                 int frames = kPauseMenuFrameBudget)
{
    return wait_for_pause_menu_progress(
        "'" + id + "' appearing",
        [&id] { return has_interactable(id); },
        frames, stall_timeout_ms);
}

bool wait_for_pause_interactable_label(const std::string& id,
                                       const std::string& label,
                                       int stall_timeout_ms = 5'000,
                                       int frames = kPauseMenuFrameBudget)
{
    return wait_for_pause_menu_progress(
        "'" + id + "' reading '" + label + "'",
        [&id, &label] { return interactable_label(id) == label; },
        frames, stall_timeout_ms);
}

// The settle half of the same rule: wait for COMPLETED menu frames instead of
// sleeping a guess. A completion is posted after the frame consumed input,
// dispatched and reset its action and ran the screen's frame tick, so one
// completed frame proves the screen that just published its buttons actually
// composed — the statement a flat SDL_Delay only assumed. Converted injector
// settles pass the same 10 s liveness budget their neighbouring appearance
// waits already carry, so no wait here is tighter than the one before it, and
// a miss is named rather than silent.
bool wait_for_completed_pause_menu_frames(int frames = 1,
                                          int timeout_ms = 5'000)
{
    const std::uint64_t target =
        og::ui::menu_screen_testing_completed_frames() +
        static_cast<std::uint64_t>(frames < 1 ? 1 : frames);
    for (int waited = 0; waited < timeout_ms; waited += 50)
    {
        if (og::ui::menu_screen_testing_completed_frames() >= target)
            return true;
        SDL_Delay(50);
    }
    fprintf(stderr,
            "  [pause] %d completed menu frame(s) did not arrive within "
            "%d ms\n",
            frames, timeout_ms);
    return false;
}

// ---------------------------------------------------------------------------
// Stub host: plain function pointers over one file-static state blob.

struct StubHostState {
    bool pump_result = true;
    int pump_calls = 0;
    int request_calls = 0;
    bool paused = true;
    std::string remote_owner;
    bool can_add = true;
    bool add_result = true;
    int add_calls = 0;
    bool can_remove = true;
    int remove_calls = 0;
    int last_remove_seat = -1;
    bool remove_result = true;
};
StubHostState g_stub;

bool stub_pump() { ++g_stub.pump_calls; return g_stub.pump_result; }
void stub_request() { ++g_stub.request_calls; }
void stub_resume() { g_stub.paused = false; }
bool stub_is_paused() { return g_stub.paused; }
std::string stub_remote_owner() { return g_stub.remote_owner; }
bool stub_can_add() { return g_stub.can_add; }
bool stub_add() { ++g_stub.add_calls; return g_stub.add_result; }
bool stub_can_remove(int /*seat*/) { return g_stub.can_remove; }
bool stub_remove(int seat)
{
    ++g_stub.remove_calls;
    g_stub.last_remove_seat = seat;
    return g_stub.remove_result;
}

PauseMenuHost make_stub_host(bool networked, bool restart_allowed)
{
    PauseMenuHost host;
    host.pump_paused = &stub_pump;
    host.request_pause = &stub_request;
    host.resume = &stub_resume;
    host.is_paused = &stub_is_paused;
    host.remote_pause_owner = &stub_remote_owner;
    host.can_add_player = &stub_can_add;
    host.add_player = &stub_add;
    host.can_remove_player = &stub_can_remove;
    host.remove_player = &stub_remove;
    host.networked = networked;
    host.restart_allowed = restart_allowed;
    return host;
}

// Restore controller state after handler tests (the test_input_mappings
// guard shape: whole hardware blob + the live per-player key maps).
struct ControlStateGuard {
    InputHardwareState hardware = input_hardware_state();
    int player_keys[4][NUM_KEYS]{};

    ControlStateGuard()
    {
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                player_keys[player][key] =
                    og::runtime::current_session->player_keys_[player][key];
        reset_default_player_controls();
    }

    ~ControlStateGuard()
    {
        input_hardware_state() = hardware;
        for (int player = 0; player < 4; ++player)
            for (int key = 0; key < NUM_KEYS; ++key)
                og::runtime::current_session->player_keys_[player][key] =
                    player_keys[player][key];
    }
};

struct NumplayersGuard {
    unsigned char saved =
        og::runtime::current_session->myscreen_->save_data.numplayers;
    explicit NumplayersGuard(int numplayers)
    {
        og::runtime::current_session->myscreen_->save_data.numplayers =
            static_cast<unsigned char>(numplayers);
    }
    ~NumplayersGuard()
    {
        og::runtime::current_session->myscreen_->save_data.numplayers = saved;
    }
};

struct OwnIndicesGuard {
    std::vector<std::uint8_t> saved =
        og::runtime::current_session->own_player_indices_;
    explicit OwnIndicesGuard(std::vector<std::uint8_t> indices)
    {
        og::runtime::current_session->own_player_indices_ = std::move(indices);
    }
    ~OwnIndicesGuard()
    {
        og::runtime::current_session->own_player_indices_ = std::move(saved);
    }
};

// Mirror of the runner's gate pass for headless nav sweeps: evaluate each
// row's state override into hidden, then run the spec's rewire.
void apply_gates_and_rewire(const og::ui::MenuScreenSpec& spec,
                            std::vector<button>& buttons,
                            int& highlighted)
{
    const std::vector<const og::ui::MenuButtonSpec*> rows =
        og::ui::materialized_spec_rows(spec);
    ASSERT_EQ(rows.size(), buttons.size());
    og::ui::MenuLabelContext context{};
    for (std::size_t i = 0; i < buttons.size(); ++i)
    {
        const og::ui::MenuButtonSpec& row = *rows[i];
        const og::ui::RowState state = row.state_override != nullptr
            ? row.state_override(context)
            : og::ui::RowState::Visible;
        buttons[i].hidden = (state == og::ui::RowState::Hidden);
    }
    if (spec.nav.rewire != nullptr)
        spec.nav.rewire(buttons.data(), static_cast<int>(buttons.size()),
                        highlighted);
}

// Nav closure + BFS reachability over the visible rows (the
// check_nav_closed_and_reachable contract, local copy).
void check_pause_nav(const std::vector<button>& buttons, const char* variant)
{
    const int count = static_cast<int>(buttons.size());
    int first_visible = -1;
    for (int i = 0; i < count; ++i)
    {
        if (buttons[static_cast<std::size_t>(i)].hidden)
            continue;
        if (first_visible < 0)
            first_visible = i;
        const MenuNav& nav = buttons[static_cast<std::size_t>(i)].nav;
        for (const int link : {nav.up, nav.down, nav.left, nav.right})
        {
            ASSERT_LT(link, count) << variant << ": link out of range from "
                                   << buttons[static_cast<std::size_t>(i)].id;
            if (link >= 0)
            {
                EXPECT_FALSE(buttons[static_cast<std::size_t>(link)].hidden)
                    << variant << ": visible "
                    << buttons[static_cast<std::size_t>(i)].id
                    << " links to hidden "
                    << buttons[static_cast<std::size_t>(link)].id;
            }
        }
    }
    ASSERT_GE(first_visible, 0) << variant << ": no visible buttons";

    std::vector<bool> reached(static_cast<std::size_t>(count), false);
    std::queue<int> frontier;
    frontier.push(first_visible);
    reached[static_cast<std::size_t>(first_visible)] = true;
    while (!frontier.empty())
    {
        const int index = frontier.front();
        frontier.pop();
        const MenuNav& nav = buttons[static_cast<std::size_t>(index)].nav;
        for (const int link : {nav.up, nav.down, nav.left, nav.right})
        {
            if (link >= 0 && link < count &&
                !reached[static_cast<std::size_t>(link)])
            {
                reached[static_cast<std::size_t>(link)] = true;
                frontier.push(link);
            }
        }
    }
    for (int i = 0; i < count; ++i)
    {
        if (!buttons[static_cast<std::size_t>(i)].hidden)
        {
            EXPECT_TRUE(reached[static_cast<std::size_t>(i)])
                << variant << ": visible "
                << buttons[static_cast<std::size_t>(i)].id
                << " unreachable by keyboard nav";
        }
    }
}

struct ExpectedButton
{
    const char* id;
    const char* label;
    int hotkey;
    int x, y, w, h;
    Sint32 myfun;
    Sint32 arg;
    MenuNav nav;
    bool hidden = false;
};

void check_exact_table(button* buttons, int count,
                       const ExpectedButton* expected, int expected_count,
                       const char* screen_name)
{
    ASSERT_EQ(expected_count, count) << screen_name << " row count";
    for (int i = 0; i < count; ++i)
    {
        const ExpectedButton& want = expected[i];
        const button& got = buttons[i];
        EXPECT_EQ(want.id, got.id) << screen_name << " index " << i;
        EXPECT_EQ(want.label, got.label) << screen_name << " " << got.id;
        EXPECT_EQ(want.hotkey, got.hotkey) << screen_name << " " << got.id;
        EXPECT_EQ(want.x, got.x) << screen_name << " " << got.id;
        EXPECT_EQ(want.y, got.y) << screen_name << " " << got.id;
        EXPECT_EQ(want.w, got.sizex) << screen_name << " " << got.id;
        EXPECT_EQ(want.h, got.sizey) << screen_name << " " << got.id;
        EXPECT_EQ(want.myfun, got.myfun) << screen_name << " " << got.id;
        EXPECT_EQ(want.arg, got.arg1) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.up, got.nav.up) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.down, got.nav.down) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.left, got.nav.left) << screen_name << " " << got.id;
        EXPECT_EQ(want.nav.right, got.nav.right)
            << screen_name << " " << got.id;
        EXPECT_EQ(want.hidden, got.hidden) << screen_name << " " << got.id;
        // Every label must fit the (w-8)/6 face budget with no clipping.
        EXPECT_LE(static_cast<int>(std::string(want.label).size()),
                  (want.w - 8) / 6)
            << screen_name << " " << got.id << " label over budget";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Exact-table pins (independent oracles over the spec transcriptions).

TEST(PauseMenuPins, pause_menu_exact_table)
{
    static const ExpectedButton kExpected[] = {
        {"pause_resume", "RESUME", KEYSTATE_UNKNOWN, 90, 32, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 0,
         MenuNav{.up = 9, .down = 1}},
        {"pause_restart", "RESTART MISSION", KEYSTATE_UNKNOWN, 90, 50, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 1,
         MenuNav{.up = 0, .down = 2}},
        {"pause_quit", "QUIT MISSION", KEYSTATE_UNKNOWN, 90, 68, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 2,
         MenuNav{.up = 1, .down = 3}},
        // Design §7.2: the half-width pair shares the y=86 band — 66px faces
        // at x=90 and x=164 spanning the same 90..230 column, linked
        // left/right, chaining up/down like one row.
        {"pause_view_team", "VIEW TEAM", KEYSTATE_UNKNOWN, 90, 86, 66, 15,
         button_action_id(ButtonAction::MenuSpecRow), 3,
         MenuNav{.up = 2, .down = 5, .right = 4}},
        {"pause_briefing", "BRIEFING", KEYSTATE_UNKNOWN, 164, 86, 66, 15,
         button_action_id(ButtonAction::MenuSpecRow), 4,
         MenuNav{.up = 2, .down = 5, .left = 3}},
        {"pause_player_0", "P1", KEYSTATE_UNKNOWN, 90, 112, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 5,
         MenuNav{.up = 3, .down = 6}},
        {"pause_player_1", "P2", KEYSTATE_UNKNOWN, 90, 130, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 6,
         MenuNav{.up = 5, .down = 7}, true},
        {"pause_player_2", "P3", KEYSTATE_UNKNOWN, 90, 148, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 7,
         MenuNav{.up = 6, .down = 8}, true},
        {"pause_player_3", "P4", KEYSTATE_UNKNOWN, 90, 166, 140, 15,
         button_action_id(ButtonAction::MenuSpecRow), 8,
         MenuNav{.up = 7, .down = 9}, true},
        {"pause_add_player", "+ ADD PLAYER", KEYSTATE_UNKNOWN, 90, 182, 140,
         15, button_action_id(ButtonAction::MenuSpecRow), 9,
         MenuNav{.up = 8, .down = 0}},
    };

    og::ui::install_pause_menu_state_for_screen(nullptr);
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    button* const buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)), "pause_menu");

    EXPECT_EQ(og::ui::kPauseMenuResumeIndex, spec.default_highlight);
    EXPECT_FALSE(spec.polls_lobby);
    EXPECT_EQ(og::ui::RemoteStartScope::None, spec.remote_start);
    // #237: the pause family are the only Overlay screens — true modals over
    // the live world, never faded even at depth 1.
    EXPECT_EQ(og::ui::MenuScreenKind::Overlay, spec.kind);
    EXPECT_FALSE(spec.exit_on_redraw);
}

TEST(PauseMenuPins, pause_player_exact_table)
{
    // §7.1 unified geometry, aligned grid: three columns x=12/116/214 with
    // 6px gutters (widths 98/92/90, shared right edge 304), bands y=30 and
    // y=54, the binding panel + HUD stack sharing y=78..161, REMOVE on the
    // y=169 command band. Identical to Base Camp seat settings (minus TEAM).
    // #358 appends the sixteen key cells over the panel lines (11..26), then
    // the bottom-left band's two doors, ABILITIES (27, #376) and KEYMAP
    // (28, #388): half-width 66 px faces at x 12 and x 84 on the y=169
    // band, left of REMOVE.
    std::vector<ExpectedButton> expected = {
        {"pause_player_back", "BACK", KEYSTATE_ESCAPE, 12, 8, 50, 15,
         button_action_id(ButtonAction::ReturnMenu), MENU_REDRAW,
         MenuNav{.up = 5, .down = 2}},
        {"pause_input", "INPUT: WASD", KEYSTATE_UNKNOWN, 12, 54, 98, 18,
         button_action_id(ButtonAction::MenuSpecRow), 1,
         MenuNav{.up = 2, .down = 11, .right = 6}},
        {"pause_direction", "4-DIRECTION", KEYSTATE_UNKNOWN, 12, 30, 98, 18,
         button_action_id(ButtonAction::MenuSpecRow), 2,
         MenuNav{.up = 0, .down = 1, .right = 3}},
        {"pause_remap", "REMAP ALL", KEYSTATE_UNKNOWN, 116, 30, 92, 18,
         button_action_id(ButtonAction::MenuSpecRow), 3,
         MenuNav{.up = 0, .down = 6, .left = 2, .right = 4}},
        {"pause_reset", "RESET", KEYSTATE_UNKNOWN, 214, 30, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 4,
         MenuNav{.up = 0, .down = 7, .left = 3}},
        {"pause_remove", "REMOVE PLAYER", KEYSTATE_UNKNOWN, 166, 169, 138, 18,
         button_action_id(ButtonAction::MenuSpecRow), 5,
         MenuNav{.up = 10, .down = 0}},
        {"pause_zoom", "ZOOM: GAME", KEYSTATE_UNKNOWN, 116, 54, 92, 18,
         button_action_id(ButtonAction::MenuSpecRow), 6,
         MenuNav{.up = 3, .down = 19, .left = 1, .right = 7}},
        {"pause_hud_radar", "RADAR: ON", KEYSTATE_UNKNOWN, 214, 78, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 7,
         MenuNav{.up = 4, .down = 8, .left = 19}},
        {"pause_hud_life", "HP: ON", KEYSTATE_UNKNOWN, 214, 100, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 8,
         MenuNav{.up = 7, .down = 9, .left = 21}},
        {"pause_hud_foes", "FOES: ON", KEYSTATE_UNKNOWN, 214, 122, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 9,
         MenuNav{.up = 8, .down = 10, .left = 23}},
        {"pause_hud_score", "SCORE: ON", KEYSTATE_UNKNOWN, 214, 144, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 10,
         MenuNav{.up = 9, .down = 5, .left = 26}},
    };
    // The key cells: one 8 px row per panel line from y 92, the MOVEMENT
    // column x 14..101, the ACTIONS column x 102..205. Up/down walk a
    // column (INPUT and ZOOM above; ABILITIES under MOVEMENT, REMOVE under
    // ACTIONS), left/right cross to the twin line, and an action line's
    // right is the HUD row beside it.
    static const int kHudBeside[8] = {7, 8, 8, 8, 9, 9, 10, 10};
    std::vector<std::string> ids;
    ids.reserve(16);
    for (int i = 0; i < 8; ++i)
        ids.push_back(std::format("pause_key_move_{}", i));
    for (int j = 0; j < 8; ++j)
        ids.push_back(std::format("pause_key_action_{}", j));
    for (int i = 0; i < 8; ++i)
        expected.push_back(
            {ids[static_cast<std::size_t>(i)].c_str(), "", KEYSTATE_UNKNOWN,
             14, 92 + 8 * i, 88, 8,
             button_action_id(ButtonAction::MenuSpecRow), 11 + i,
             MenuNav{.up = i > 0 ? 10 + i : 1,
                     .down = i < 7 ? 12 + i : 27,
                     .right = 19 + i}});
    for (int j = 0; j < 8; ++j)
        expected.push_back(
            {ids[static_cast<std::size_t>(8 + j)].c_str(), "",
             KEYSTATE_UNKNOWN, 102, 92 + 8 * j, 104, 8,
             button_action_id(ButtonAction::MenuSpecRow), 19 + j,
             MenuNav{.up = j > 0 ? 18 + j : 6,
                     .down = j < 7 ? 20 + j : 5,
                     .left = 11 + j,
                     .right = kHudBeside[j]}});
    // The band: ABILITIES (up from the MOVEMENT column's last line), KEYMAP
    // (up from the ACTIONS column's), REMOVE; left/right along it, down to
    // BACK.
    expected.push_back({"pause_abilities", "ABILITIES", KEYSTATE_UNKNOWN, 12,
                        169, 66, 18,
                        button_action_id(ButtonAction::MenuSpecRow), 27,
                        MenuNav{.up = 18, .down = 0, .right = 28}});
    expected.push_back({"pause_keymap", "KEYMAP", KEYSTATE_UNKNOWN, 84, 169,
                        66, 18, button_action_id(ButtonAction::MenuSpecRow),
                        28, MenuNav{.up = 26, .down = 0, .left = 27,
                                    .right = 5}});
    ASSERT_EQ(29u, expected.size());
    EXPECT_EQ(11, og::ui::kPausePlayerCellFirstIndex);
    EXPECT_EQ(27, og::ui::kPausePlayerAbilitiesIndex);
    EXPECT_EQ(28, og::ui::kPausePlayerKeymapIndex);
    EXPECT_EQ(29, og::ui::kPausePlayerButtonCount);

    og::ui::install_pause_player_state_for_screen(nullptr);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    button* const buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    check_exact_table(buttons, count, expected.data(),
                      static_cast<int>(expected.size()), "pause_player");
    // The cells are invisible hit areas with a steady focus ring on their
    // own edge (a pulsing one would cross the line's text or its neighbours
    // at the 8 px pitch).
    const std::vector<const og::ui::MenuButtonSpec*> rows =
        og::ui::materialized_spec_rows(spec);
    for (int i = 0; i < count; ++i)
    {
        const bool cell = i >= 11 && i < 27;
        EXPECT_EQ(cell, rows[static_cast<std::size_t>(i)]->no_draw)
            << rows[static_cast<std::size_t>(i)]->id;
        EXPECT_EQ(cell, rows[static_cast<std::size_t>(i)]->cell_highlight)
            << rows[static_cast<std::size_t>(i)]->id;
    }

    EXPECT_EQ(og::ui::kPausePlayerInputIndex, spec.default_highlight);
    EXPECT_TRUE(spec.exit_on_redraw);
    EXPECT_FALSE(spec.polls_lobby);
    EXPECT_EQ(og::ui::RemoteStartScope::None, spec.remote_start);
    // #237: pause family = Overlay, never faded.
    EXPECT_EQ(og::ui::MenuScreenKind::Overlay, spec.kind);
}

// ---------------------------------------------------------------------------
// Nav BFS across the gating variants.

TEST(PauseMenuNav, pause_menu_gating_variants_reachable)
{
    struct Variant {
        const char* name;
        bool networked;
        bool restart_allowed;
        bool can_add;
        int local_players;                    // save numplayers (local)
        std::vector<std::uint8_t> own_seats;  // own indices (networked)
        std::vector<std::string> expect_visible;
        bool single_seat_device = false;      // #249 phone class
    };
    const std::vector<Variant> variants = {
        {"local_solo", false, true, true, 1, {},
         {"pause_resume", "pause_restart", "pause_quit", "pause_view_team",
          "pause_briefing", "pause_player_0", "pause_add_player"}},
        {"local_splitscreen", false, true, true, 2, {},
         {"pause_resume", "pause_restart", "pause_quit", "pause_view_team",
          "pause_briefing", "pause_player_0", "pause_player_1",
          "pause_add_player"}},
        // 4 seats: ADD PLAYER is Hidden (design §7.2) — the row would only
        // say no, and hiding it keeps the nav ring clean.
        {"local_four_seats", false, true, false, 4, {},
         {"pause_resume", "pause_restart", "pause_quit", "pause_view_team",
          "pause_briefing", "pause_player_0", "pause_player_1",
          "pause_player_2", "pause_player_3"}},
        // #249: a phone is already full at its one built-in seat, so the
        // seat door (local_transport_shadow_can_add_player, which reads the
        // same og::input::local_seat_cap()) closes with a single seat
        // playing and the row hides exactly as it does at four.
        {"local_single_seat_device", false, true, false, 1, {},
         {"pause_resume", "pause_restart", "pause_quit", "pause_view_team",
          "pause_briefing", "pause_player_0"},
         true},
        {"networked_host", true, false, true, 1, {0, 1},
         {"pause_resume", "pause_quit", "pause_view_team", "pause_briefing",
          "pause_player_0", "pause_player_1"}},
        {"networked_joiner", true, false, true, 1, {2},
         {"pause_resume", "pause_quit", "pause_view_team",
          "pause_briefing", "pause_player_0"}},
    };

    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    for (const Variant& variant : variants)
    {
        SCOPED_TRACE(variant.name);
        g_stub = StubHostState{};
        g_stub.can_add = variant.can_add;
        PauseMenuHost host =
            make_stub_host(variant.networked, variant.restart_allowed);
        og::ui::PauseMenuScreenState state;
        state.host = &host;
        og::ui::install_pause_menu_state_for_screen(&state);
        NumplayersGuard numplayers(variant.local_players);
        OwnIndicesGuard own(variant.own_seats);
        // The device class reaches this row through the host's seat door,
        // never directly; the flag is set here so the variant describes a
        // whole machine. Other tests share the process, so it is handed back.
        struct DeviceClassGuard {
            bool saved = input_hardware_state().single_seat_device;
            explicit DeviceClassGuard(bool single_seat)
            {
                input_hardware_state().single_seat_device = single_seat;
            }
            ~DeviceClassGuard()
            {
                input_hardware_state().single_seat_device = saved;
            }
        } device_class(variant.single_seat_device);

        std::vector<button> buttons;
        og::ui::materialize_menu_buttons(spec, buttons);
        int highlighted = spec.default_highlight;
        apply_gates_and_rewire(spec, buttons, highlighted);

        std::vector<std::string> visible;
        for (const button& b : buttons)
        {
            if (!b.hidden)
                visible.push_back(b.id);
        }
        EXPECT_EQ(variant.expect_visible, visible);
        check_pause_nav(buttons, variant.name);

        // The half-width pair after every rewire: left/right link the two
        // faces, BRIEFING mirrors VIEW TEAM's vertical links, and the
        // vertical ring passes through the band exactly once (nothing
        // chains up/down INTO BRIEFING).
        const MenuNav& vt = buttons[og::ui::kPauseMenuViewTeamIndex].nav;
        const MenuNav& bf = buttons[og::ui::kPauseMenuBriefingIndex].nav;
        EXPECT_EQ(og::ui::kPauseMenuBriefingIndex, vt.right);
        EXPECT_EQ(og::ui::kPauseMenuViewTeamIndex, bf.left);
        EXPECT_EQ(vt.up, bf.up);
        EXPECT_EQ(vt.down, bf.down);
        EXPECT_EQ(og::ui::kPauseMenuQuitIndex, vt.up);
        EXPECT_EQ(og::ui::kPauseMenuPlayerBaseIndex, vt.down);
        for (const button& b : buttons)
        {
            if (b.hidden)
                continue;
            EXPECT_NE(og::ui::kPauseMenuBriefingIndex, b.nav.up) << b.id;
            EXPECT_NE(og::ui::kPauseMenuBriefingIndex, b.nav.down) << b.id;
        }
    }
    og::ui::install_pause_menu_state_for_screen(nullptr);
}

TEST(PauseMenuNav, pause_player_remove_gating_variants_reachable)
{
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();

    struct Variant {
        const char* name;
        bool networked;
        bool can_remove;
        bool expect_remove_visible;
    };
    const std::vector<Variant> variants = {
        {"local_multi_seat", false, true, true},
        {"local_last_seat", false, false, false},
        {"networked", true, true, false},
    };
    for (const Variant& variant : variants)
    {
        SCOPED_TRACE(variant.name);
        g_stub = StubHostState{};
        g_stub.can_remove = variant.can_remove;
        PauseMenuHost host = make_stub_host(variant.networked, false);
        og::ui::PausePlayerScreenState state;
        state.host = &host;
        state.seat = 0;
        og::ui::install_pause_player_state_for_screen(&state);

        std::vector<button> buttons;
        og::ui::materialize_menu_buttons(spec, buttons);
        int highlighted = spec.default_highlight;
        apply_gates_and_rewire(spec, buttons, highlighted);

        EXPECT_EQ(variant.expect_remove_visible,
                  !buttons[og::ui::kPausePlayerRemoveIndex].hidden);
        check_pause_nav(buttons, variant.name);
    }
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// Seat collection + player-row labels.

TEST(PauseMenuLabels, seat_collection_and_player_row_labels)
{
    ControlStateGuard controls;

    {
        NumplayersGuard numplayers(3);
        const std::vector<og::ui::PauseSeatInfo> seats =
            og::ui::collect_pause_seats(/*networked=*/false);
        ASSERT_EQ(3u, seats.size());
        for (int k = 0; k < 3; ++k)
        {
            EXPECT_EQ(k, seats[static_cast<std::size_t>(k)].seat);
            EXPECT_EQ(k + 1,
                      seats[static_cast<std::size_t>(k)].player_number);
        }
        // Factory defaults: profile 0 = WASD, 1 = ARROWS, 2 = IJKL.
        EXPECT_EQ("P1: WASD", og::ui::pause_player_row_label(seats[0]));
        EXPECT_EQ(std::string("P2: ") + og::input::kArrowGlyphs,
                  og::ui::pause_player_row_label(seats[1]));
        EXPECT_EQ("P3: IJKL", og::ui::pause_player_row_label(seats[2]));
    }

    {
        // A one-seat networked joiner owning global player 2: displayed P3,
        // driven by LOCAL controller-profile slot 0.
        OwnIndicesGuard own({2});
        const std::vector<og::ui::PauseSeatInfo> seats =
            og::ui::collect_pause_seats(/*networked=*/true);
        ASSERT_EQ(1u, seats.size());
        EXPECT_EQ(0, seats[0].seat);
        EXPECT_EQ(3, seats[0].player_number);
        EXPECT_EQ("P3: WASD", og::ui::pause_player_row_label(seats[0]));
        // 22-char face budget on the 140px row.
        EXPECT_LE(og::ui::pause_player_row_label(seats[0]).size(), 22u);
    }

    {
        // #249: a phone's keyboard-mapped FIRST seat names the screen here
        // too — the pause rows, the INPUT cycler and the seat card all
        // compose the owner token through the same seat_owner_is_screen
        // rule. RAII on the flag: an ASSERT return must not leak the phone
        // class into the rest of the process.
        NumplayersGuard numplayers(2);
        struct SingleSeatGuard {
            bool saved = input_hardware_state().single_seat_device;
            SingleSeatGuard() { input_hardware_state().single_seat_device = true; }
            ~SingleSeatGuard() { input_hardware_state().single_seat_device = saved; }
        } device_class;
        const std::vector<og::ui::PauseSeatInfo> seats =
            og::ui::collect_pause_seats(/*networked=*/false);
        ASSERT_EQ(2u, seats.size());
        EXPECT_EQ("P1: SCRN", og::ui::pause_player_row_label(seats[0]));
        // Only local slot 0 is the screen: the overlay drives player 0
        // alone, so a later seat keeps its real mapping name.
        EXPECT_NE("P2: SCRN", og::ui::pause_player_row_label(seats[1]));
    }
}

// ---------------------------------------------------------------------------
// Draw paths: the player screen's binding grid for a joystick-driven seat and
// the PAUSED screen's "by <name>" remote-pause subtitle. Asserted by pinning
// glyph pixels on the headless screen buffer (the test_view_team idiom).

namespace
{

// Every opaque pixel of the small-font glyph at (x, y) must read back as
// expected_color; putdatatext writes the flat color for the >247 ink values
// these fonts use.
void expect_glyph_at(screen* output, char label, int x, int y,
                     int expected_color)
{
    text& font = output->text_normal;
    ASSERT_NE(nullptr, font.letters);
    ASSERT_TRUE(font.letters->valid());
    const std::size_t stride = static_cast<std::size_t>(font.sizex) *
                               static_cast<std::size_t>(font.sizey);
    const unsigned char* const glyph =
        font.letters->data.get() +
        static_cast<std::size_t>(static_cast<unsigned char>(label)) * stride;
    int opaque_pixels = 0;
    for (Sint32 row = 0; row < font.sizey; ++row)
    {
        for (Sint32 col = 0; col < font.sizex; ++col)
        {
            const unsigned char source =
                glyph[static_cast<std::size_t>(row * font.sizex + col)];
            if (source == 0)
                continue;
            ++opaque_pixels;
            int actual = -1;
            output->get_pixel(x + col, y + row, &actual);
            EXPECT_EQ(expected_color, actual)
                << "glyph '" << label << "' pixel " << col << "," << row
                << " at " << x << "," << y;
        }
    }
    EXPECT_GT(opaque_pixels, 0) << "glyph '" << label << "' has no ink";
}

// Minimal copies of the test_input_joystick virtual-device helpers.
struct JoystickSubsystemGuard
{
    JoystickSubsystemGuard()
    {
        og::input_native::joystick_init_subsystem();
    }
    ~JoystickSubsystemGuard()
    {
        og::input_native::joystick_quit_subsystem();
    }
};

class VirtualJoystick
{
public:
    VirtualJoystick(Uint16 axes, Uint16 buttons, Uint16 hats, const char* name)
    {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = axes;
        desc.nbuttons = buttons;
        desc.nhats = hats;
        desc.name = name;
        instance_id_ = SDL_AttachVirtualJoystick(&desc);
        if (instance_id_ != 0)
            joystick_ = SDL_OpenJoystick(instance_id_);
    }

    ~VirtualJoystick()
    {
        if (joystick_ != nullptr)
            SDL_CloseJoystick(joystick_);
        if (instance_id_ != 0)
            SDL_DetachVirtualJoystick(instance_id_);
    }

    VirtualJoystick(const VirtualJoystick&) = delete;
    VirtualJoystick& operator=(const VirtualJoystick&) = delete;

    SDL_Joystick* get() const { return joystick_; }
    SDL_JoystickID instance_id() const { return instance_id_; }

private:
    SDL_JoystickID instance_id_ = 0;
    SDL_Joystick* joystick_ = nullptr;
};

struct JoystickHandleGuard
{
    og::input_native::JoystickHandle old[10];

    JoystickHandleGuard()
    {
        for (int i = 0; i < 10; ++i)
        {
            old[i] = joysticks[i];
            joysticks[i] = nullptr;
        }
    }

    ~JoystickHandleGuard()
    {
        for (int i = 0; i < 10; ++i)
            joysticks[i] = old[i];
    }
};

int device_index_of(SDL_JoystickID instance)
{
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    int found = -1;
    if (ids != nullptr)
    {
        for (int i = 0; i < count; ++i)
        {
            if (ids[i] == instance)
            {
                found = i;
                break;
            }
        }
        SDL_free(ids);
    }
    return found;
}

} // namespace

TEST(PausePlayerDraw, binding_grid_shows_joystick_names_for_assigned_seat)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    // The classic ACTIONS rows ("SPECIAL: B0" on row 1); the New Ability
    // Select twin is binding_grid_rows_follow_ability_select below.
    ScopedAbilitySelect off;
    ControlStateGuard controls;
    JoystickSubsystemGuard subsystem_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad pause grid pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.draw_content);

    // write_formatted advances one glyph cell per character.
    const int pitch = scr->text_normal.sizex + 1;

    // Keyboard seat first: the movement grid shows key names ("UP: W").
    // §7.1 unified geometry: movement column x=20, actions x=104, first
    // binding line y=93 (panel top y=78 + 15).
    spec.draw_background(&state);
    spec.draw_content(&state);
    expect_glyph_at(scr, 'W', 20 + 4 * pitch, 93, DARK_BLUE);

    // Assign the pad: the same cells now show joy_binding_display_name
    // output — "UP: A1-", and the reversed gamepad default puts FIRE on
    // button 1 ("FIRE: B1", actions row 0) and SPECIAL on button 0
    // ("SPECIAL: B0", actions row 1, one 8px line lower).
    ASSERT_TRUE(assign_joystick_to_player(0, d));
    spec.draw_background(&state);
    spec.draw_content(&state);
    expect_glyph_at(scr, 'A', 20 + 4 * pitch, 93, DARK_BLUE);
    expect_glyph_at(scr, '1', 20 + 5 * pitch, 93, DARK_BLUE);
    expect_glyph_at(scr, 'B', 104 + 6 * pitch, 93, DARK_BLUE);
    expect_glyph_at(scr, '1', 104 + 7 * pitch, 93, DARK_BLUE);
    expect_glyph_at(scr, 'B', 104 + 9 * pitch, 101, DARK_BLUE);
    expect_glyph_at(scr, '0', 104 + 10 * pitch, 101, DARK_BLUE);
    // joy_binding_display_name is the oracle for those grid values.
    EXPECT_EQ("A1-", joy_binding_display_name(
                         player_joy[0].key_type[KEY_UP],
                         player_joy[0].key_index[KEY_UP]));
    EXPECT_EQ("B1", joy_binding_display_name(
                        player_joy[0].key_type[KEY_FIRE],
                        player_joy[0].key_index[KEY_FIRE]));
    EXPECT_EQ("B0", joy_binding_display_name(
                        player_joy[0].key_type[KEY_SPECIAL],
                        player_joy[0].key_index[KEY_SPECIAL]));

    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The ACTIONS column draws og::ui::binding_panel_action_rows for the
// setting in force: off, row 1 is "SPECIAL: 1" and row 6 "SPEC SW: 2" (the
// SPEC 2 key, which cycles with the setting off, never the unbound slot 11
// that read "--"); on, rows 1..3 are "SPEC 1/2/3" with keys 1, 2, 3 and the
// list runs to eight rows (CHAR SW on row 7).
//
// RED: the pause panel's actions back on its own seven-row table with
// KEY_SPECIAL_SWITCH ("SPEC SW: --", and no SPEC 3 row with the setting on).
TEST(PausePlayerDraw, binding_grid_rows_follow_ability_select)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    ControlStateGuard controls;
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);
    set_player_control_mode(0, static_cast<int>(ControlDirectionMode::FourDirection));
    set_player_key_binding(0, KEY_SPECIAL, SDLK_1);
    set_player_key_binding(0, KEY_SPEC2, SDLK_2);
    set_player_key_binding(0, KEY_SPEC3, SDLK_3);
    set_player_key_binding(0, KEY_SWITCH, SDLK_N);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    const int pitch = scr->text_normal.sizex + 1;
    const auto row_y = [](int row) { return 93 + 8 * row; };

    {
        ScopedAbilitySelect off;
        spec.draw_background(&state);
        spec.draw_content(&state);
        expect_glyph_at(scr, '1', 104 + 9 * pitch, row_y(1), DARK_BLUE);  // "SPECIAL: 1"
        expect_glyph_at(scr, '2', 104 + 9 * pitch, row_y(6), DARK_BLUE);  // "SPEC SW: 2"
    }
    {
        ScopedAbilitySelect on(true);
        spec.draw_background(&state);
        spec.draw_content(&state);
        expect_glyph_at(scr, '1', 104 + 8 * pitch, row_y(1), DARK_BLUE);  // "SPEC 1: 1"
        expect_glyph_at(scr, '2', 104 + 8 * pitch, row_y(2), DARK_BLUE);  // "SPEC 2: 2"
        expect_glyph_at(scr, '3', 104 + 8 * pitch, row_y(3), DARK_BLUE);  // "SPEC 3: 3"
        expect_glyph_at(scr, 'N', 104 + 9 * pitch, row_y(7), DARK_BLUE);  // "CHAR SW: N"
    }

    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The REMAP wizard asks for one key per prompt, in order, and the list
// follows the setting: off, the classic 11 (4-direction) / 15 (8-direction)
// with SPECIAL SWITCH binding the SPEC 2 key (slot 17); on, 12 / 16 with
// SPEC 1, SPEC 2 and SPEC 3 and no SPECIAL SWITCH. The "remap" trace names
// each prompt as it is drawn; under TESTING every prompt is answered with
// a fake ESC (keep the current key).
//
// RED: the ON tables replaced by the classic ones (11 prompts, no SPEC 3);
// the classic SPECIAL SWITCH back on KEY_SPECIAL_SWITCH (key=11).
TEST(PausePlayerHandlers, remap_prompts_follow_ability_select)
{
    ControlStateGuard controls;
    NumplayersGuard numplayers(1);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);

    const auto run = [&](bool ability_select, bool eight_dir) {
        ScopedAbilitySelect setting(ability_select);
        set_player_control_mode(
            0, static_cast<int>(eight_dir ? ControlDirectionMode::EightDirection
                                          : ControlDirectionMode::FourDirection));
        trace_clear();
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(og::ui::kPausePlayerRemapIndex, &state));
    };

    run(false, false);
    EXPECT_TRUE(trace_contains("remap", "prompt 11/11 key=16 label=LOOK UP (HOLD)"));
    EXPECT_TRUE(trace_contains("remap", "prompt 7/11 key=17 label=SPECIAL SWITCH"))
        << "the classic change-special prompt binds the SPEC 2 key";
    EXPECT_FALSE(trace_contains("remap", "key=11 "));
    run(false, true);
    EXPECT_TRUE(trace_contains("remap", "prompt 15/15 key=16"));

    run(true, false);
    EXPECT_TRUE(trace_contains("remap", "prompt 6/12 key=9 label=SPEC 1"));
    EXPECT_TRUE(trace_contains("remap", "prompt 7/12 key=17 label=SPEC 2"));
    EXPECT_TRUE(trace_contains("remap", "prompt 8/12 key=18 label=SPEC 3"));
    EXPECT_TRUE(trace_contains("remap", "prompt 12/12 key=16 label=LOOK UP (HOLD)"));
    EXPECT_FALSE(trace_contains("remap", "SPECIAL SWITCH"));
    run(true, true);
    EXPECT_TRUE(trace_contains("remap", "prompt 12/16 key=18 label=SPEC 3"));
    EXPECT_TRUE(trace_contains("remap", "prompt 16/16 key=16"));

    og::ui::install_pause_player_state_for_screen(nullptr);
}

TEST(PauseMenuDraw, remote_pause_owner_subtitle_renders_only_when_present)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(true, false);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);
    OwnIndicesGuard own({0});
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.draw_content);

    const int pitch = scr->text_normal.sizex + 1;
    // "by GRETA" is centered on x=160 at y=22 (write_xy_center).
    const int base_x = 160 - (8 * pitch) / 2;

    // No remote owner: the subtitle band stays untouched (no YELLOW ink).
    constexpr int kProbeColor = 13;
    scr->fastbox(base_x - 2, 21, 8 * pitch + 4, 10, kProbeColor);
    spec.draw_content(&state);
    bool any_yellow = false;
    for (int y = 21; y < 31 && !any_yellow; ++y)
    {
        for (int x = base_x - 2; x < base_x + 8 * pitch + 2; ++x)
        {
            int actual = -1;
            scr->get_pixel(x, y, &actual);
            if (actual == YELLOW)
            {
                any_yellow = true;
                break;
            }
        }
    }
    EXPECT_FALSE(any_yellow)
        << "no subtitle may render without a remote pause owner";

    // Remote owner named: "by GRETA" renders under the PAUSED title.
    g_stub.remote_owner = "GRETA";
    spec.draw_content(&state);
    expect_glyph_at(scr, 'b', base_x + 0 * pitch, 22, YELLOW);
    expect_glyph_at(scr, 'y', base_x + 1 * pitch, 22, YELLOW);
    expect_glyph_at(scr, 'G', base_x + 3 * pitch, 22, YELLOW);
    expect_glyph_at(scr, 'A', base_x + 7 * pitch, 22, YELLOW);

    og::ui::install_pause_menu_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// PR proof media (MENUSHOTS_DIR gate): compose one settled player-screen
// frame exactly as the runner draws it (background -> buttons -> content)
// and dump it as a 320x200 PPM. Skipped without MENUSHOTS_DIR — the layout
// truth lives in the exact-table pins; these frames are visual evidence.

namespace
{

bool write_menushot(screen* scr, const char* name)
{
    const char* const dir = std::getenv("MENUSHOTS_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return false;
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error)
        return false;
    const std::string path = std::string(dir) + "/" + name + ".ppm";
    FILE* const f = fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;
    fprintf(f, "P6\n320 200\n255\n");
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 320; ++x)
        {
            Uint8 rgb[3] = {0, 0, 0};
            scr->get_pixel(x, y, &rgb[0], &rgb[1], &rgb[2]);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    fprintf(stderr, "  [menushot] wrote %s\n", path.c_str());
    return true;
}

} // namespace

TEST(PausePlayerDraw, menushots_player_screen_keyboard_and_joystick)
{
    if (std::getenv("MENUSHOTS_DIR") == nullptr)
        GTEST_SKIP() << "set MENUSHOTS_DIR to write the PR proof frames";

    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    ControlStateGuard controls;
    JoystickSubsystemGuard subsystem_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad menushot pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    g_stub = StubHostState{};
    g_stub.can_remove = false;  // solo shape: REMOVE hidden, panel is last
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();

    const auto compose_and_capture = [&](const char* name) {
        button* const buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        og::runtime::current_session->localbuttons_ =
            init_buttons(buttons, count);
        // The runner's gate pass + rewire, inline over the accessor array.
        const std::vector<const og::ui::MenuButtonSpec*> rows =
            og::ui::materialized_spec_rows(spec);
        ASSERT_EQ(count, static_cast<int>(rows.size()));
        og::ui::MenuLabelContext context{};
        for (int i = 0; i < count; ++i)
        {
            const og::ui::MenuButtonSpec& row =
                *rows[static_cast<std::size_t>(i)];
            const og::ui::RowState row_state = row.state_override != nullptr
                ? row.state_override(context)
                : og::ui::RowState::Visible;
            buttons[i].hidden = (row_state == og::ui::RowState::Hidden);
        }
        int highlighted = spec.default_highlight;
        spec.nav.rewire(buttons, count, highlighted);
        spec.draw_background(&state);
        draw_buttons(buttons, count);
        spec.draw_content(&state);
        EXPECT_TRUE(write_menushot(scr, name)) << name;
        clear_allbuttons();
        og::runtime::current_session->localbuttons_ = nullptr;
    };

    {
        ScopedAbilitySelect on(true);
        compose_and_capture("pause_player_screen");
        // Two seats: REMOVE PLAYER joins the bottom band beside the doors.
        g_stub.can_remove = true;
        compose_and_capture("pause_player_screen_two_seats");
        g_stub.can_remove = false;
    }
    {
        // The same panel with New Ability Select off: the classic seven
        // ACTIONS rows on the same default keys.
        ScopedAbilitySelect off;
        compose_and_capture("pause_player_screen_off");
    }

    ASSERT_TRUE(assign_joystick_to_player(0, d));
    compose_and_capture("pause_player_joystick");
    clear_player_joystick(0);

    og::ui::install_pause_player_state_for_screen(nullptr);
}

namespace
{
// The REMAP prompt the menushot films: the wizard calls the poll once per
// prompt, right after drawing it (TESTING answers each with a fake ESC), so
// the Nth call sees prompt N on the canvas. Writing it and returning false
// ends the wizard there.
int g_remap_shot_polls = 0;
int g_remap_shot_prompt = 0;
const char* g_remap_shot_name = "remap_p1_spec2";
bool g_remap_shot_written = false;

bool remap_shot_poll()
{
    if (++g_remap_shot_polls < g_remap_shot_prompt)
        return true;
    g_remap_shot_written =
        write_menushot(og::runtime::current_session->myscreen_,
                       g_remap_shot_name);
    return false;
}
} // namespace

// The REMAP prompt for "P1 SPEC 2" (prompt 7 of 12, 4-direction, New Ability
// Select on) with its three summary lines, on the shipped default keys.
TEST(PausePlayerDraw, menushots_remap_prompt_spec2)
{
    if (std::getenv("MENUSHOTS_DIR") == nullptr)
        GTEST_SKIP() << "set MENUSHOTS_DIR to write the PR proof frames";

    ControlStateGuard controls;
    ScopedAbilitySelect on(true);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    trace_clear();
    g_remap_shot_polls = 0;
    g_remap_shot_prompt = 7;
    g_remap_shot_written = false;
    edit_player_keymap_with_poll(0, &remap_shot_poll);
    EXPECT_TRUE(trace_contains("remap", "prompt 7/12 key=17 label=SPEC 2"));
    EXPECT_FALSE(trace_contains("remap", "prompt 8/12"))
        << "the wizard stops at the filmed prompt";
    EXPECT_TRUE(g_remap_shot_written);
}

namespace
{
// The REMAP canvas at prompt 1, read back as palette indices.
std::array<int, 320 * 200> g_remap_frame{};
bool g_remap_frame_taken = false;

bool remap_frame_poll()
{
    screen* const scr = og::runtime::current_session->myscreen_;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x)
            scr->get_pixel(x, y, &g_remap_frame[static_cast<std::size_t>(
                                     y * 320 + x)]);
    g_remap_frame_taken = true;
    return false;  // end the wizard at its first prompt
}

int remap_px(int x, int y)
{
    return g_remap_frame[static_cast<std::size_t>(y * 320 + x)];
}
} // namespace

// The REMAP prompt's inset box sits inside the canvas, centred under its
// centred text, and holds every line: even a 48-glyph summary under the
// worst-case key names (the MenuLayout summary pins' set) never touches its
// bevels, and nothing but the outer face is drawn outside it. Both settings
// (two summary lines off, three on).
//
// RED (planted, both settings): draw_button_inverted back to
// (20, 30, 300, 170), the old x2/y2 reading of an (x, y, w, h) call: the
// box runs to x 319 / y 199 ("left 20, right 319"; "a margin below the
// box: 199").
TEST(PausePlayerDraw, remap_prompt_box_stays_on_canvas_and_holds_every_line)
{
    struct KeySet
    {
        const char* name;
        bool ability_select;
        bool eight_dir;
        std::vector<std::pair<int, int>> keys;
    };
    const std::vector<std::pair<int, int>> numpad = {
        {KEY_UP, SDLK_KP_8},        {KEY_UP_RIGHT, SDLK_KP_9},
        {KEY_RIGHT, SDLK_KP_6},     {KEY_DOWN_RIGHT, SDLK_KP_3},
        {KEY_DOWN, SDLK_KP_2},      {KEY_DOWN_LEFT, SDLK_KP_1},
        {KEY_LEFT, SDLK_KP_4},      {KEY_UP_LEFT, SDLK_KP_7}};
    std::vector<std::pair<int, int>> ten_glyphs;
    for (const int key : {KEY_UP, KEY_UP_RIGHT, KEY_RIGHT, KEY_DOWN_RIGHT,
                          KEY_DOWN, KEY_DOWN_LEFT, KEY_LEFT, KEY_UP_LEFT})
        ten_glyphs.emplace_back(key, SDLK_VOLUMEDOWN);
    for (const int key : {KEY_FIRE, KEY_SPECIAL, KEY_SPEC2, KEY_SPEC3})
        ten_glyphs.emplace_back(key, SDLK_MEDIA_PAUSE);
    for (const int key : {KEY_YELL, KEY_SWITCH, KEY_SHIFTER, KEY_LOOKUP})
        ten_glyphs.emplace_back(key, SDLK_AC_REFRESH);
    const std::vector<KeySet> sets = {
        // Each setting's own worst-case names, as the MenuLayout summary
        // pins set them.
        {"classic_pins", false, true,
         {{KEY_YELL, SDLK_BACKSPACE}, {KEY_LOOKUP, SDLK_CAPSLOCK},
          {KEY_SWITCH, SDLK_RETURN}, {KEY_FIRE, SDLK_LCTRL},
          {KEY_SPECIAL, SDLK_LALT}, {KEY_SPEC2, SDLK_TAB},
          {KEY_SHIFTER, SDLK_RSHIFT}}},
        {"ability_pins", true, true,
         {{KEY_YELL, SDLK_BACKSPACE}, {KEY_LOOKUP, SDLK_CAPSLOCK},
          {KEY_SWITCH, SDLK_RETURN}, {KEY_FIRE, SDLK_PRINTSCREEN},
          {KEY_SPECIAL, SDLK_RETURN}, {KEY_SPEC2, SDLK_DELETE},
          {KEY_SPEC3, SDLK_PAGEDOWN}, {KEY_SHIFTER, SDLK_PRINTSCREEN}}},
        // #383's report, setting off: FIRE and SHIFTER on Print Screen,
        // YELL Backspace, LOOK UP Caps Lock, SWITCH Return.
        {"issue_383", false, false,
         {{KEY_FIRE, SDLK_PRINTSCREEN}, {KEY_SHIFTER, SDLK_PRINTSCREEN},
          {KEY_YELL, SDLK_BACKSPACE}, {KEY_LOOKUP, SDLK_CAPSLOCK},
          {KEY_SWITCH, SDLK_RETURN}}},
        // 8-direction on the numpad, both settings (75 glyphs on one line,
        // 450 px, before #383).
        {"numpad_off", false, true, numpad},
        {"numpad_on", true, true, numpad},
        // Ten-glyph names everywhere with the setting on: six lines.
        {"ten_glyphs_on", true, true, ten_glyphs},
    };
    for (const KeySet& set : sets)
    {
        SCOPED_TRACE(set.name);
        ControlStateGuard controls;
        ScopedAbilitySelect scope(set.ability_select);
        set_player_control_mode(
            0, static_cast<int>(set.eight_dir
                                    ? ControlDirectionMode::EightDirection
                                    : ControlDirectionMode::FourDirection));
        for (const auto& [slot, key] : set.keys)
            set_player_key_binding(0, slot, key);
        const std::size_t line_count =
            build_player_control_summary_lines(0, true).size();
        g_remap_frame_taken = false;
        edit_player_keymap_with_poll(0, &remap_frame_poll);
        ASSERT_TRUE(g_remap_frame_taken);
        // The inset bevels (draw_text_bar: top 10, bottom 15, left 11,
        // right 14), found along the middle row and column.
        int left = -1, right = -1, top = -1, bottom = -1;
        for (int x = 0; x < 320; ++x)
        {
            if (left < 0 && remap_px(x, 120) == 11)
                left = x;
            if (remap_px(x, 120) == 14)
                right = x;
        }
        for (int y = 0; y < 200; ++y)
            if (remap_px(160, y) == 15)
                bottom = y;
        for (int y = 0; y < 200; ++y)
            if (remap_px(left + 1, y) == 10)
            {
                top = y;
                break;
            }
        ASSERT_GE(left, 0) << "left bevel";
        ASSERT_GT(right, left) << "right bevel";
        ASSERT_GE(top, 0) << "top bevel";
        ASSERT_GT(bottom, top) << "bottom bevel";
        EXPECT_EQ(left, 319 - right)
            << "the box is centred across the canvas: left " << left
            << ", right " << right;
        EXPECT_GE(199 - bottom, 8) << "a margin below the box: " << bottom;

        // Outside the box: the outer face only. Inside: no ink on a bevel
        // column or row but the bevel itself, i.e. the interior holds the
        // text.
        const int outer = remap_px(2, 2);
        int outside_ink = 0;
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                if ((x < left || x > right || y < top || y > bottom) &&
                    remap_px(x, y) != outer)
                    ++outside_ink;
        EXPECT_EQ(0, outside_ink) << "nothing is drawn outside the box";
        int bevel_breaks = 0;
        for (int y = top + 1; y < bottom; ++y)
            bevel_breaks += remap_px(left, y) != 11;
        for (int y = top + 1; y < bottom; ++y)
            bevel_breaks += remap_px(right, y) != 14;
        for (int x = left + 1; x < right; ++x)
            bevel_breaks += remap_px(x, bottom) != 15;
        EXPECT_EQ(0, bevel_breaks) << "no text runs over a bevel";
        // The summary keeps to its band: no text under the band's last row
        // (178) inside the box, and a block of four lines or more starts
        // below the ESC line's ink (116).
        const auto text_ink = [](int y) {
            int ink = 0;
            for (int x = 16; x < 304; ++x)
            {
                const int index = remap_px(x, y);
                ink += index >= DARK_BLUE && index <= DARK_BLUE + 4;
            }
            return ink;
        };
        for (int y = 179; y < bottom; ++y)
            EXPECT_EQ(0, text_ink(y)) << "summary ink on row " << y;
        if (line_count >= 4)
        {
            int first_ink = -1;
            for (int y = 117; y < bottom && first_ink < 0; ++y)
                if (text_ink(y) > 0)
                    first_ink = y;
            EXPECT_GE(first_ink, 117);
            for (int y = 111; y < 117; ++y)
                EXPECT_EQ(0, text_ink(y))
                    << "a gap between the ESC line and the summary, row " << y;
        }
    }
}

// A pause that cannot be established never shows a menu. The pause request
// goes out first and is confirmed by a pump; if that pump says the session
// is gone (#246: the host vanished between the Esc and the request landing),
// the caller must be told the session ended right there — showing a PAUSED
// overlay over a dead session strands the player behind chrome nothing can
// clear.
TEST(PauseMenuHandlers, dead_session_at_open_never_shows_the_menu)
{
    g_stub = StubHostState{};
    g_stub.pump_result = false;
    PauseMenuHost host = make_stub_host(true, false);

    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(PauseMenuResult::Quit,
                                             /*release_pause=*/false);
    ASSERT_EQ(1, og::ui::pause_menu_testing_queue_remaining());

    trace_clear();
    EXPECT_EQ(PauseMenuResult::SessionEnded, og::ui::run_pause_menu(host));
    EXPECT_EQ(1, g_stub.request_calls)
        << "the pause is requested before it is confirmed";
    EXPECT_EQ(1, g_stub.pump_calls)
        << "exactly one pump answers the request, and it answered no";
    EXPECT_EQ(1, og::ui::pause_menu_testing_queue_remaining())
        << "the menu never opened, so it consumed no outcome";
    EXPECT_FALSE(trace_contains("pause_menu", "scripted_outcome"))
        << "the menu body was never reached";

    // Paired control: the same call with a live session DOES open the menu
    // and consume the queued outcome.
    g_stub = StubHostState{};
    trace_clear();
    EXPECT_EQ(PauseMenuResult::Quit, og::ui::run_pause_menu(host));
    EXPECT_EQ(1, g_stub.request_calls);
    EXPECT_EQ(0, og::ui::pause_menu_testing_queue_remaining())
        << "a live session opens the menu, which consumes the outcome";
    EXPECT_TRUE(trace_contains("pause_menu", "scripted_outcome"));

    og::ui::pause_menu_testing_clear_queue();
}

// The state-less (engine sweep / pin) shape of the pause backdrop must show
// the SAME darkened world the real menu's first frame shows. A bare sweep
// that skipped the darkening would render the menu's text straight over a
// bright, unreadable world.
TEST(PauseMenuDraw, bare_backdrop_darkens_the_world_like_the_real_first_frame)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.draw_background);

    // A flat, bright ground well below the y=3..31 title strip (which the
    // real menu dims a second time) so the two paths are comparable there.
    constexpr int kProbeX = 160;
    constexpr int kProbeY = 120;
    constexpr int kGroundColor = 15;  // brightest grey in the picker ramp

    const auto paint_ground = [&] {
        scr->fastbox(0, 100, kUiCanvasW, 50, kGroundColor);
    };
    const auto probe = [&] {
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        scr->get_pixel(kProbeX, kProbeY, &r, &g, &b);
        return std::array<int, 3>{r, g, b};
    };

    og::ui::install_pause_menu_state_for_screen(nullptr);
    std::array<int, 3> bright{};
    std::array<int, 3> bare{};
    std::array<int, 3> first_frame{};
    {
        // The menu's fixed modal canvas, as run_pause_menu establishes it.
        ScopedUiCanvas ui_canvas(*scr);

        paint_ground();
        bright = probe();

        // Bare shape: no installed state at all.
        spec.draw_background(nullptr);
        bare = probe();

        // The real menu's FIRST frame, from the same bright ground.
        paint_ground();
        ASSERT_EQ(bright, probe())
            << "the ground must be repainted identically";
        g_stub = StubHostState{};
        PauseMenuHost host = make_stub_host(false, true);
        og::ui::PauseMenuScreenState state;
        state.host = &host;
        ASSERT_TRUE(state.backdrop.empty());
        og::ui::install_pause_menu_state_for_screen(&state);
        spec.draw_background(&state);
        first_frame = probe();
        og::ui::install_pause_menu_state_for_screen(nullptr);
    }

    EXPECT_NE(bright, bare) << "the bare shape must still darken the world";
    EXPECT_EQ(first_frame, bare)
        << "the state-less shape paints the same darkened world";
}

// ---------------------------------------------------------------------------
// PAUSED-screen row handlers (on_spec_row driven directly).

TEST(PauseMenuHandlers, resume_quit_restart_and_add_rows)
{
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);

    // RESUME: structural exit with the Resumed outcome.
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuResumeIndex, &state));
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome);

    // QUIT declined: stays open (the backdrop re-seeds every frame, so a
    // dialog's scribbles vanish on the next background pass).
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    EXPECT_EQ(MENU_OK, spec.on_spec_row(og::ui::kPauseMenuQuitIndex, &state));
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome);

    // QUIT confirmed.
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_EXIT, spec.on_spec_row(og::ui::kPauseMenuQuitIndex, &state));
    EXPECT_EQ(PauseMenuResult::Quit, state.outcome);

    // RESTART declined: the mission is NOT thrown away. A confirm the
    // player answered NO to must leave the menu standing on its default
    // outcome — the arm that would lose a mission in progress.
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuRestartIndex, &state));
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome)
        << "a declined restart keeps the mission";
    EXPECT_FALSE(trace_contains("pause_menu", "restart_confirmed"))
        << "nothing was confirmed, so nothing may be announced";

    // RESTART confirmed.
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    trace_clear();
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuRestartIndex, &state));
    EXPECT_EQ(PauseMenuResult::Restart, state.outcome);
    EXPECT_TRUE(trace_contains("pause_menu", "restart_confirmed"))
        << "and a confirmed restart says so";

    // ADD PLAYER success refreshes rows in place (no exit).
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuAddPlayerIndex, &state));
    EXPECT_EQ(1, g_stub.add_calls);
    EXPECT_TRUE(trace_contains("pause_menu", "add_player"));

    // ADD PLAYER denial pops the notice.
    g_stub.add_result = false;
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuAddPlayerIndex, &state));
    EXPECT_TRUE(trace_contains("popup", "COULD NOT ADD"));

    picker_testing_yes_or_no_queue_clear();
    og::ui::install_pause_menu_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// VIEW TEAM row (design §7.2): hosts viewscreen::view_team with a poll
// callback that pumps the paused transport and keeps the pause alive; a dead
// session ends the wait AND closes the menu.

TEST(PauseMenuHandlers, view_team_row_polls_pump_keepalive_and_session_death)
{
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    ASSERT_NE(nullptr, og::runtime::current_session->myscreen_->viewob[0]);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);

    // Plain activation: view_team runs (returns immediately under TESTING)
    // and the menu stays open.
    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuViewTeamIndex, &state));
    EXPECT_TRUE(trace_contains("pause_menu", "view_team"));
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome);

    // Each wait pass polls: 3 budgeted passes = 3 transport pumps.
    g_stub.pump_calls = 0;
    view_team_testing_set_poll_passes(3);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuViewTeamIndex, &state));
    EXPECT_EQ(3, g_stub.pump_calls)
        << "the poll must pump the paused transport once per wait pass";

    // The keep-alive cadence runs inside the wait: a stale stamp plus a 1ms
    // interval override re-requests the pause during the polled wait.
    og::ui::pause_menu_testing_set_keepalive_interval_ms(1);
    state.last_keepalive_ms = og::input_native::ticks_ms() - 100;
    g_stub.request_calls = 0;
    view_team_testing_set_poll_passes(1);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuViewTeamIndex, &state));
    EXPECT_EQ(1, g_stub.request_calls)
        << "the pause keep-alive must run while the roster blocks";
    og::ui::pause_menu_testing_set_keepalive_interval_ms(0);

    // Session death during the wait: the poll ends the wait on its first
    // false and the handler closes the menu with SessionEnded.
    g_stub.pump_calls = 0;
    g_stub.pump_result = false;
    view_team_testing_set_poll_passes(5);
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuViewTeamIndex, &state));
    EXPECT_EQ(PauseMenuResult::SessionEnded, state.outcome);
    EXPECT_EQ(1, g_stub.pump_calls)
        << "the wait must end on the first failed pump, not run the budget";

    // No installed pause state (bare engine shape): the poll defaults open
    // and never touches a transport.
    og::ui::install_pause_menu_state_for_screen(nullptr);
    g_stub.pump_calls = 0;
    g_stub.pump_result = true;
    state.outcome = PauseMenuResult::Resumed;
    view_team_testing_set_poll_passes(2);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuViewTeamIndex, &state));
    EXPECT_EQ(0, g_stub.pump_calls)
        << "without an installed pause state the poll must not pump";
}

// ---------------------------------------------------------------------------
// BRIEFING row (design §7.2): read_scenario over the pause backdrop. The row
// is Disabled when the level has no description lines; the scroll view's
// full-screen scribbles are healed by the next backdrop restore pass.

namespace
{

struct PauseLevelDescriptionGuard {
    std::list<std::string>& description;
    std::list<std::string> saved;
    explicit PauseLevelDescriptionGuard(std::list<std::string>& lines)
        : description(lines), saved(lines) {}
    ~PauseLevelDescriptionGuard() { description = std::move(saved); }
};

struct ForceScrollTextGuard {
    ForceScrollTextGuard() { help_testing_set_force_scroll_text(true); }
    ~ForceScrollTextGuard() { help_testing_set_force_scroll_text(false); }
};

std::atomic<bool> g_briefing_dismissed{false};

// The scroll view dismisses on any key; repeat Escape until the main thread
// confirms on_spec_row returned (scroll_text_view clears the keyboard at
// entry, so a single early press can be eaten).
int briefing_dismiss_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    SDL_Delay(120);
    while (!g_briefing_dismissed.load(std::memory_order_acquire))
    {
        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(40);
    }
    return 0;
}

int count_backdrop_mismatches(screen* scr,
                              const std::vector<unsigned char>& backdrop)
{
    int mismatches = 0;
    std::size_t at = 0;
    for (int y = 0; y < kUiCanvasH; ++y)
    {
        for (int x = 0; x < kUiCanvasW; ++x)
        {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            if (r != backdrop[at] || g != backdrop[at + 1] ||
                b != backdrop[at + 2])
            {
                ++mismatches;
            }
            at += 3;
        }
    }
    return mismatches;
}

} // namespace

TEST(PauseMenuHandlers, briefing_row_disabled_without_description_lines)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    PauseLevelDescriptionGuard description(scr->level_description());

    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    const std::vector<const og::ui::MenuButtonSpec*> rows =
        og::ui::materialized_spec_rows(spec);
    ASSERT_LT(og::ui::kPauseMenuBriefingIndex,
              static_cast<int>(rows.size()));
    const og::ui::MenuButtonSpec& briefing =
        *rows[og::ui::kPauseMenuBriefingIndex];
    ASSERT_STREQ("pause_briefing", briefing.id);
    ASSERT_NE(nullptr, briefing.state_override);

    og::ui::MenuLabelContext context{};
    scr->level_description().clear();
    EXPECT_EQ(og::ui::RowState::Disabled, briefing.state_override(context))
        << "no description lines -> the row must read Disabled";
    scr->level_description().push_back("A briefing line");
    EXPECT_EQ(og::ui::RowState::Visible, briefing.state_override(context));
}

TEST(PauseMenuHandlers, briefing_row_scroll_view_scribbles_healed_by_backdrop)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    ForceScrollTextGuard force_scroll;
    PauseLevelDescriptionGuard description(scr->level_description());
    scr->level_description().clear();
    for (int i = 0; i < 40; ++i)
        scr->level_description().push_back("Briefing line " +
                                           std::to_string(i));

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.draw_background);
    ASSERT_NE(nullptr, spec.on_spec_row);

    {
        // The menu's fixed modal canvas, as run_pause_menu establishes it.
        ScopedUiCanvas ui_canvas(*scr);

        // First background pass darkens the world frame and captures it.
        spec.draw_background(&state);
        ASSERT_FALSE(state.backdrop.empty());
        ASSERT_EQ(0, count_backdrop_mismatches(scr, state.backdrop))
            << "the capture pass must leave the canvas at the captured image";

        // BRIEFING blocks in the real scroll view until dismissed.
        g_briefing_dismissed.store(false, std::memory_order_release);
        SDL_Thread* const injector = SDL_CreateThread(
            briefing_dismiss_injector, "briefing_dismiss", nullptr);
        ASSERT_NE(nullptr, injector);
        trace_clear();
        const Sint32 result =
            spec.on_spec_row(og::ui::kPauseMenuBriefingIndex, &state);
        g_briefing_dismissed.store(true, std::memory_order_release);
        int injector_result = -1;
        SDL_WaitThread(injector, &injector_result);
        EXPECT_EQ(0, injector_result);
        EXPECT_EQ(MENU_OK, result);
        EXPECT_TRUE(trace_contains("pause_menu", "briefing"));
        // The keep-alive window is refreshed at entry (the scroll view has
        // no poll hook) and the transport pumped once on return.
        EXPECT_EQ(1, g_stub.request_calls)
            << "BRIEFING must refresh the pause keep-alive before blocking";
        EXPECT_EQ(1, g_stub.pump_calls)
            << "BRIEFING must pump the paused transport on return";

        // The scroll view scribbled the shared 320x200 canvas...
        EXPECT_GT(count_backdrop_mismatches(scr, state.backdrop), 0)
            << "the scroll view must actually have painted over the menu";

        // ...and the next backdrop pass heals every pixel (no stale chrome).
        spec.draw_background(&state);
        EXPECT_EQ(0, count_backdrop_mismatches(scr, state.backdrop))
            << "the backdrop restore must heal the scroll view's scribbles";
    }

    // Drain any surplus injected Escapes so they cannot leak into later
    // tests' event polling.
    SDL_PumpEvents();
    SDL_Event drained;
    while (SDL_PollEvent(&drained))
    {
    }

    og::ui::install_pause_menu_state_for_screen(nullptr);
}

// A dead session on the post-briefing pump closes the menu.
TEST(PauseMenuHandlers, briefing_row_session_death_on_return_closes_menu)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    PauseLevelDescriptionGuard description(scr->level_description());
    scr->level_description().clear();
    scr->level_description().push_back("One line");

    g_stub = StubHostState{};
    g_stub.pump_result = false;
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();

    // Without the force flag read_scenario returns immediately under
    // TESTING; the handler's return pump still sees the dead session.
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuBriefingIndex, &state));
    EXPECT_EQ(PauseMenuResult::SessionEnded, state.outcome);

    og::ui::install_pause_menu_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// Player-screen row handlers.

TEST(PausePlayerHandlers, input_mode_remap_reset_rows)
{
    ControlStateGuard controls;
    NumplayersGuard numplayers(2);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 1;
    state.player_number = 2;
    og::ui::install_pause_player_state_for_screen(&state);

    // INPUT: cycles the seat off its current mapping (factory ARROWS).
    const std::string before = og::ui::current_input_selection(1).name;
    EXPECT_EQ("ARROWS", before);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerInputIndex, &state));
    EXPECT_NE(before, og::ui::current_input_selection(1).name);

    // MODE: 4 <-> 8 direction toggle.
    const int mode_before = get_player_control_mode(1);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerModeIndex, &state));
    EXPECT_NE(mode_before, get_player_control_mode(1));

    // REMAP: the wizard runs with the pause poll callback (TESTING answers
    // every prompt with a fake ESC = keep current key; the callback still
    // pumps once per prompt).
    g_stub.pump_calls = 0;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerRemapIndex, &state));
    EXPECT_GT(g_stub.pump_calls, 0)
        << "the remap poll callback must pump the paused transport";
    EXPECT_FALSE(state.session_ended);

    // REMAP with a dying session: the poll cancels the prompt sequence and
    // flags the player screen to close.
    g_stub.pump_result = false;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerRemapIndex, &state));
    EXPECT_TRUE(state.session_ended);
    g_stub.pump_result = true;
    state.session_ended = false;

    // RESET: a scribbled binding returns to the seat's factory default.
    const int factory_up = og::runtime::current_session->player_keys_[1][KEY_UP];
    set_player_key_binding(1, KEY_UP, SDLK_F24);
    ASSERT_NE(factory_up,
              og::runtime::current_session->player_keys_[1][KEY_UP]);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerResetIndex, &state));
    EXPECT_EQ(factory_up,
              og::runtime::current_session->player_keys_[1][KEY_UP]);

    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The udev report: SDL enumerates the pad but its device node will not open,
// so assignment fails. The INPUT row must SAY so — a silent no-op reads as a
// broken button — and must not park the cycle on the device it cannot use.
TEST(PausePlayerHandlers, input_row_names_a_device_it_could_not_open)
{
    ControlStateGuard controls;
    NumplayersGuard numplayers(1);
    JoystickSubsystemGuard subsystem_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad pause udev pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    // Every handle slot nulled while the device stays enumerated: exactly the
    // state joystick_open failing on a permission-denied node leaves behind.
    JoystickHandleGuard handle_guard;
    ASSERT_GE(device_index_of(pad.instance_id()), 0);
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);

    // Park the seat on the option just before the first JOY entry, so the
    // next press lands on the device that cannot be opened.
    const std::vector<og::ui::InputCycleOption> options =
        og::ui::input_cycle_options(cfg, 0, 1);
    std::size_t first_joy = options.size();
    for (std::size_t i = 0; i < options.size(); ++i)
    {
        if (options[i].is_joystick)
        {
            first_joy = i;
            break;
        }
    }
    ASSERT_LT(first_joy, options.size())
        << "an enumerated device must still be offered by the cycler";
    ASSERT_GT(first_joy, 0u);
    ASSERT_TRUE(
        og::ui::apply_input_cycle_selection(cfg, 0, options[first_joy - 1]));
    const std::string parked = og::ui::current_input_selection(0).name;
    const std::string joy_name = options[first_joy].name;

    trace_clear();
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerInputIndex, &state));
    EXPECT_TRUE(trace_contains("popup",
                               ("COULD NOT OPEN " + joy_name).c_str()))
        << "the row must name the device it could not open";
    EXPECT_FALSE(playerHasJoystick(0))
        << "a device that will not open must never bind the seat";
    EXPECT_FALSE(og::ui::current_input_selection(0).is_joystick);
    EXPECT_NE(parked, og::ui::current_input_selection(0).name)
        << "the cycle must skip the broken device, not park on it";

    og::ui::install_pause_player_state_for_screen(nullptr);
}

TEST(PausePlayerHandlers, remove_row_confirms_and_removes)
{
    ControlStateGuard controls;
    NumplayersGuard numplayers(2);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 1;
    state.player_number = 2;
    og::ui::install_pause_player_state_for_screen(&state);

    // Declined confirm: nothing happens.
    picker_testing_yes_or_no_queue_clear();
    picker_testing_yes_or_no_queue_push(false);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerRemoveIndex, &state));
    EXPECT_EQ(0, g_stub.remove_calls);
    EXPECT_FALSE(state.removed);

    // Confirmed: the host removal runs and the screen pops back to PAUSED
    // (MENU_REDRAW under exit_on_redraw).
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_REDRAW,
              spec.on_spec_row(og::ui::kPausePlayerRemoveIndex, &state));
    EXPECT_EQ(1, g_stub.remove_calls);
    EXPECT_EQ(1, g_stub.last_remove_seat);
    EXPECT_TRUE(state.removed);

    // Denied removal pops the notice and keeps the screen.
    state.removed = false;
    g_stub.remove_result = false;
    trace_clear();
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerRemoveIndex, &state));
    EXPECT_TRUE(trace_contains("popup", "REMOVE DENIED"));
    EXPECT_FALSE(state.removed);

    // Networked: the row handler is inert (the row is hidden anyway).
    PauseMenuHost networked_host = make_stub_host(true, false);
    state.host = &networked_host;
    g_stub.remove_calls = 0;
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerRemoveIndex, &state));
    EXPECT_EQ(0, g_stub.remove_calls);

    picker_testing_yes_or_no_queue_clear();
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// §7.1 HUD toggle + ZOOM rows (shared helpers behind both player screens).

// Save/restore the §7.1 cfg keys and the live view prefs one seat touches.
struct HudStateGuard {
    int seat;
    std::array<std::string, 6> saved;
    signed char prefs[4]{};
    Sint32 zoom = 0;
    static constexpr const char* kSuffixes[6] = {
        "hud_radar", "hud_life", "hud_foes", "hud_score", "view_zoom",
        "hud_migrated"};

    explicit HudStateGuard(int seat_index) : seat(seat_index)
    {
        for (int k = 0; k < 6; ++k)
            saved[static_cast<std::size_t>(k)] = cfg.get_setting(
                "controls", std::format("player{}_{}", seat + 1,
                                        kSuffixes[k]));
        if (viewscreen* const view = seat_viewob(seat))
        {
            prefs[0] = view->prefs[PREF_RADAR];
            prefs[1] = view->prefs[PREF_LIFE];
            prefs[2] = view->prefs[PREF_FOES];
            prefs[3] = view->prefs[PREF_SCORE];
            zoom = view->view_zoom_step_;
        }
    }
    ~HudStateGuard()
    {
        for (int k = 0; k < 6; ++k)
            cfg.apply_setting(
                "controls",
                std::format("player{}_{}", seat + 1, kSuffixes[k]),
                saved[static_cast<std::size_t>(k)]);
        // The handlers under test persisted mid-test values to disk; put the
        // restored ones back so later binaries never inherit toggled HUD keys.
        cfg.save_settings();
        if (viewscreen* const view = seat_viewob(seat))
        {
            view->prefs[PREF_RADAR] = prefs[0];
            view->prefs[PREF_LIFE] = prefs[1];
            view->prefs[PREF_FOES] = prefs[2];
            view->prefs[PREF_SCORE] = prefs[3];
            view->view_zoom_step_ = zoom;
        }
    }
    static viewscreen* seat_viewob(int seat_index)
    {
        screen* const scr = og::runtime::current_session->myscreen_;
        if (scr == nullptr || seat_index < 0 || seat_index >= scr->numviews)
            return nullptr;
        return scr->viewob[static_cast<std::size_t>(seat_index)].get();
    }
};

TEST(PausePlayerHandlers, hud_toggle_rows_flip_prefs_labels_and_cfg)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    viewscreen* const view = HudStateGuard::seat_viewob(0);
    ASSERT_NE(nullptr, view) << "seat 0 must have a live viewscreen";
    HudStateGuard guard(0);

    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);

    // RADAR: pref flip + label flip + cfg mirror + immediate redraw request.
    view->prefs[PREF_RADAR] = PREF_RADAR_ON;
    EXPECT_EQ("RADAR: ON",
              og::ui::player_hud_row_label(0, og::ui::PlayerHudRow::Radar));
    scr->redrawme = 0;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudRadarIndex, &state));
    EXPECT_EQ(PREF_RADAR_OFF, view->prefs[PREF_RADAR]);
    EXPECT_EQ("RADAR: OFF",
              og::ui::player_hud_row_label(0, og::ui::PlayerHudRow::Radar));
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_radar"));
    EXPECT_EQ(1, scr->redrawme) << "the toggle must request a redraw";
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudRadarIndex, &state));
    EXPECT_EQ(PREF_RADAR_ON, view->prefs[PREF_RADAR]);
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_hud_radar"));

    // The live vbutton surface gets the flipped label through the rewire.
    view->prefs[PREF_FOES] = PREF_FOES_ON;
    std::vector<button> buttons;
    og::ui::materialize_menu_buttons(spec, buttons);
    int highlighted = spec.default_highlight;
    apply_gates_and_rewire(spec, buttons, highlighted);
    EXPECT_EQ("FOES: ON",
              buttons[og::ui::kPausePlayerHudFoesIndex].label);
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudFoesIndex, &state));
    apply_gates_and_rewire(spec, buttons, highlighted);
    EXPECT_EQ("FOES: OFF",
              buttons[og::ui::kPausePlayerHudFoesIndex].label);
    EXPECT_EQ(PREF_FOES_OFF, view->prefs[PREF_FOES]);

    // SCORE follows the same shape.
    view->prefs[PREF_SCORE] = PREF_SCORE_ON;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudScoreIndex, &state));
    EXPECT_EQ(PREF_SCORE_OFF, view->prefs[PREF_SCORE]);
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_score"));

    og::ui::install_pause_player_state_for_screen(nullptr);
}

TEST(PausePlayerHandlers, hp_row_is_on_off_and_normalizes_legacy_values)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    viewscreen* const view = HudStateGuard::seat_viewob(0);
    ASSERT_NE(nullptr, view);
    HudStateGuard guard(0);

    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);

    // Every legacy non-OFF state displays as ON...
    for (const signed char legacy :
         {PREF_LIFE_TEXT, PREF_LIFE_BARS, PREF_LIFE_BOTH, PREF_LIFE_SMALL})
    {
        view->prefs[PREF_LIFE] = legacy;
        EXPECT_EQ("HP: ON",
                  og::ui::player_hud_row_label(0, og::ui::PlayerHudRow::Life))
            << "legacy value " << static_cast<int>(legacy);
    }
    // ...and the first toggle normalizes: legacy TEXT -> OFF -> BOTH.
    view->prefs[PREF_LIFE] = PREF_LIFE_TEXT;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudLifeIndex, &state));
    EXPECT_EQ(PREF_LIFE_OFF, view->prefs[PREF_LIFE]);
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_hud_life"));
    EXPECT_EQ("HP: OFF",
              og::ui::player_hud_row_label(0, og::ui::PlayerHudRow::Life));
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerHudLifeIndex, &state));
    EXPECT_EQ(PREF_LIFE_BOTH, view->prefs[PREF_LIFE])
        << "ON always writes BOTH, never a legacy TEXT/BARS/SMALL state";
    EXPECT_EQ("1", cfg.get_setting("controls", "player1_hud_life"));

    og::ui::install_pause_player_state_for_screen(nullptr);
}

TEST(PausePlayerHandlers, zoom_row_cycles_labels_persists_and_clamps)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    viewscreen* const view = HudStateGuard::seat_viewob(0);
    ASSERT_NE(nullptr, view);
    HudStateGuard guard(0);

    ASSERT_TRUE(og::ui::per_view_zoom_available())
        << "the SDL video backend has the partitioned-presentation seam";

    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);

    // Full cycle on the default canvas: GAME -> 0.9 .. 0.5 -> GAME.
    view->view_zoom_step_ = 0;
    EXPECT_EQ("ZOOM: GAME", og::ui::player_view_zoom_label(0));
    const char* const expected_labels[5] = {
        "ZOOM: 0.9X", "ZOOM: 0.8X", "ZOOM: 0.7X", "ZOOM: 0.6X",
        "ZOOM: 0.5X"};
    for (int step = 1; step <= 5; ++step)
    {
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(og::ui::kPausePlayerZoomIndex, &state));
        EXPECT_EQ(step, static_cast<int>(view->view_zoom_step_));
        EXPECT_EQ(expected_labels[step - 1],
                  og::ui::player_view_zoom_label(0));
        EXPECT_EQ(std::to_string(step),
                  cfg.get_setting("controls", "player1_view_zoom"));
    }
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerZoomIndex, &state));
    EXPECT_EQ(0, static_cast<int>(view->view_zoom_step_));
    EXPECT_EQ("ZOOM: GAME", og::ui::player_view_zoom_label(0));
    EXPECT_EQ("0", cfg.get_setting("controls", "player1_view_zoom"));

    // Budget clamp (§7.1 composition gate): the canvas derives from
    // global x per-view zoom, so at a deep GLOBAL zoom the deep per-view
    // steps overflow the split-canvas budget and the cycle skips them. At
    // global 0.1 on a 640x400 window: x0.9 = 3552x2220 (~7.9M) still fits,
    // x0.8 = 4000x2500 (10M) exceeds kWorldCanvasPixelBudget (8.38M).
    ASSERT_TRUE(E_Screen);
    E_Screen->set_world_zoom(1, og::WorldScaleMode::Integer, 640, 400);
    scr->relayout_views();
    EXPECT_TRUE(scr->world_zoom_composition_fits(9));  // 0.9x
    EXPECT_FALSE(scr->world_zoom_composition_fits(8)); // 0.8x
    EXPECT_FALSE(scr->world_zoom_composition_fits(5)); // 0.5x
    view->view_zoom_step_ = 1; // 0.9x — the deepest fitting step
    cfg.apply_setting("controls", "player1_view_zoom", "1");
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerZoomIndex, &state));
    EXPECT_EQ(0, static_cast<int>(view->view_zoom_step_))
        << "over-budget steps are skipped; the cycle wraps to GAME";
    E_Screen->set_world_zoom(og::kZoomStepsMax, og::WorldScaleMode::Integer,
                             640, 400);
    scr->relayout_views();

    og::ui::install_pause_player_state_for_screen(nullptr);
}

// Diagnostic legibility, not a product contract (wave-C N8). The reported
// defect — mapping_short_name("ARROWS") returning an empty string because the
// "named-mapping registry" was torn down by some test order — does not exist
// and cannot: src/interface/input/input_mappings.cpp branches on the literal
// and hands back a compile-time constant, pinned by
// tests/unit/test_input_mappings.cpp. What was really seen was this file's own
// injector diagnostic printing four control bytes into a terminal that ate
// them. The escaper is the fix; the label itself is untouched.
TEST(PausePlayerHandlers, expected_input_labels_print_visibly_in_diagnostics)
{
    EXPECT_EQ(
        "INPUT: <01><03><02><04>",
        escape_for_log("INPUT: " + og::input::mapping_short_name("ARROWS")));
    // Printable labels pass through byte for byte.
    EXPECT_EQ("INPUT: WASD",
              escape_for_log("INPUT: " + og::input::mapping_short_name("WASD")));
}

// ---------------------------------------------------------------------------
// Frame tick: pump, keep-alive cadence, re-pause, Esc edge machine.

TEST(PauseMenuFrameTick, pump_keepalive_and_esc_edge)
{
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.frame_tick);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;

    // Fake keystates so the Esc edge machine is deterministic.
    const bool* saved_keystates = og::runtime::current_session->keystates_;
    std::array<bool, SDL_SCANCODE_COUNT> fake_keystates{};
    fake_keystates.fill(false);
    og::runtime::current_session->keystates_ = fake_keystates.data();

    // Paused steady state inside the keep-alive window: pump only.
    og::ui::pause_menu_testing_set_keepalive_interval_ms(60'000);
    state.last_keepalive_ms = og::input_native::ticks_ms();
    EXPECT_TRUE(spec.frame_tick(&state, 1));
    EXPECT_EQ(1, g_stub.pump_calls);
    EXPECT_EQ(0, g_stub.request_calls);

    // Keep-alive fires once the cadence window elapses.
    og::ui::pause_menu_testing_set_keepalive_interval_ms(1);
    state.last_keepalive_ms = og::input_native::ticks_ms() - 100;
    EXPECT_TRUE(spec.frame_tick(&state, 2));
    EXPECT_EQ(1, g_stub.request_calls);

    // Unpaused underneath: re-request on the 1s retry cadence, not the
    // keep-alive cadence.
    og::ui::pause_menu_testing_set_keepalive_interval_ms(1);
    g_stub.paused = false;
    g_stub.request_calls = 0;
    state.last_keepalive_ms = og::input_native::ticks_ms() - 500;
    EXPECT_TRUE(spec.frame_tick(&state, 3));
    EXPECT_EQ(0, g_stub.request_calls)
        << "a 500ms-old stamp is inside the 1s re-pause retry window";
    state.last_keepalive_ms = og::input_native::ticks_ms() - 1'500;
    EXPECT_TRUE(spec.frame_tick(&state, 4));
    EXPECT_EQ(1, g_stub.request_calls);
    g_stub.paused = true;

    // Esc edge: held at entry -> no close until released and re-pressed.
    fake_keystates[KEYSTATE_ESCAPE] = true;
    state.esc_seen_up = false;
    state.last_keepalive_ms = og::input_native::ticks_ms();
    og::ui::pause_menu_testing_set_keepalive_interval_ms(60'000);
    EXPECT_TRUE(spec.frame_tick(&state, 5))
        << "the opening press (or a web autorepeat) must not close the menu";
    fake_keystates[KEYSTATE_ESCAPE] = false;
    EXPECT_TRUE(spec.frame_tick(&state, 6));
    EXPECT_TRUE(state.esc_seen_up);
    fake_keystates[KEYSTATE_ESCAPE] = true;
    EXPECT_FALSE(spec.frame_tick(&state, 7));
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome);

    // A dead session closes the menu with the SessionEnded outcome.
    fake_keystates[KEYSTATE_ESCAPE] = false;
    state.outcome = PauseMenuResult::Resumed;
    g_stub.pump_result = false;
    EXPECT_FALSE(spec.frame_tick(&state, 8));
    EXPECT_EQ(PauseMenuResult::SessionEnded, state.outcome);

    og::ui::pause_menu_testing_set_keepalive_interval_ms(0);
    og::runtime::current_session->keystates_ = saved_keystates;
}

TEST(PauseMenuFrameTick, player_screen_pump_and_seat_validity)
{
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    ASSERT_NE(nullptr, spec.frame_tick);
    NumplayersGuard numplayers(2);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 1;
    state.last_keepalive_ms = og::input_native::ticks_ms();

    EXPECT_TRUE(spec.frame_tick(&state, 1));
    EXPECT_EQ(1, g_stub.pump_calls);

    // The seat vanished (removed underneath): the screen closes.
    {
        NumplayersGuard shrunk(1);
        EXPECT_FALSE(spec.frame_tick(&state, 2));
    }

    // Session death closes with the flag the parent folds into its outcome.
    g_stub.pump_result = false;
    EXPECT_FALSE(spec.frame_tick(&state, 3));
    EXPECT_TRUE(state.session_ended);

    // A removed seat state closes immediately without pumping.
    g_stub = StubHostState{};
    state.session_ended = false;
    state.removed = true;
    EXPECT_FALSE(spec.frame_tick(&state, 4));
    EXPECT_EQ(0, g_stub.pump_calls);
}

// ---------------------------------------------------------------------------
// Shadow guard paths without a runtime installed.

TEST(PauseMenuShadow, guard_paths_without_runtime)
{
    og::runtime::GameSession::Config session_cfg;
    session_cfg.allocate_screen = false;
    session_cfg.allocate_prefs = false;
    session_cfg.install_legacy_globals = false;
    og::runtime::GameSession session(session_cfg);

    EXPECT_FALSE(og::runtime::local_transport_shadow_pump_paused(session));
    // No display client: the keep-alive is a silent no-op.
    og::runtime::local_transport_shadow_request_pause_keepalive(session);
    EXPECT_TRUE(
        og::runtime::local_transport_shadow_remote_pause_owner(session)
            .empty());
    EXPECT_FALSE(og::runtime::local_transport_shadow_restart_level(session));
}

// ---------------------------------------------------------------------------
// The REAL menu, end to end (idiom C): Esc opens the blocking menu over a
// live glad_init shadow; an injector thread walks RESUME, the player screen,
// and QUIT through interact() while the main thread is parked inside
// game_frame_with_result.

namespace
{

struct EventScriptLocal {
    std::vector<SDL_Event> events;
    std::size_t next = 0;
};
EventScriptLocal* g_pause_script = nullptr;

int pause_scripted_poll(SDL_Event* out)
{
    // Deliver the scripted Esc first, then fall through to the real queue so
    // the injector thread's synthetic clicks reach the frame too.
    if (g_pause_script != nullptr &&
        g_pause_script->next < g_pause_script->events.size())
    {
        *out = g_pause_script->events[g_pause_script->next++];
        return 1;
    }
    return SDL_PollEvent(out);
}

// Escape tail for every pause-menu injector in this file, through the shared
// implementation in tests/test_escape_tail.h.
// This flow drives a BLOCKING menu from the main thread: nothing but a
// click lets it return, so an injector that gives up mid-flow leaves the
// whole binary hung instead of failing. Keep closing whatever screen is
// open until the main thread says it is out of the menu for good, then
// report the leg that gave up.
//
// No wall-clock bound on the loop: the main thread cannot leave the menu
// on its own, so a tail that stopped trying early would GUARANTEE the
// wedge it exists to prevent. The player sub-screen publishes BACK and no
// RESUME, so a leg that dies in there needs both clicks.
constexpr EscapeDoor kPauseEscapeDoors[] = {
    {"pause_resume", "pause_resume"},
    {"pause_player_back", "pause_player_back"},
};

int escape_the_pause_menu(std::atomic<bool>& test_finished, int leg,
                          const char* why)
{
    return escape_to_the_main_thread(test_finished, leg, why,
                                     kPauseEscapeDoors);
}

// Flow control shared by the three real-menu injectors below, in the shape
// AddCycleFlow already carries: `sabotage_first_leg` points leg 1 at an id the
// menu never publishes (so the give-up path itself is testable), and
// `test_finished` is how the main thread tells the injector it has left the
// menu for good.
// The yield window: how many COMPLETED menu frames the yield regression holds
// the pause menu open for before it clicks RESUME.
constexpr int kYieldWindowFrames = 12;

struct PauseFlowScript
{
    bool sabotage_first_leg = false;
    std::atomic<bool> test_finished{false};
    // Completed menu frames the yield injector observed inside its window.
    std::atomic<std::uint64_t> frames_observed{0};
};

int pause_menu_flow_injector(void* data)
{
    auto* const flow = static_cast<PauseFlowScript*>(data);
    og::runtime::ensure_thread_session();

    // Leg 1: RESUME closes the menu.
    const char* const resume_id =
        flow->sabotage_first_leg ? "pause_never_published" : "pause_resume";
    if (!wait_for_pause_interactable(resume_id, 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 1,
            "the PAUSED screen never published RESUME (or never composed)");
    interact("pause_resume");

    // Leg 2: ADD PLAYER creates a real second seat mid-game; its player row
    // appears in place.
    if (!wait_for_pause_interactable("pause_add_player", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 2,
            "ADD PLAYER never published after RESUME");
    interact("pause_add_player");
    if (!wait_for_pause_interactable("pause_player_1", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 3,
            "the new seat's row never appeared");

    // Leg 3: the new seat's row opens the player screen; REMOVE PLAYER
    // (NO-first confirm answered from the queue) drops the seat and pops
    // back to the PAUSED screen whose row re-hides.
    interact("pause_player_1");
    if (!wait_for_pause_interactable("pause_remove", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 4,
            "the player screen never published REMOVE");
    picker_testing_yes_or_no_queue_push(true);
    interact("pause_remove");

    // Leg 4: back on the PAUSED screen, QUIT ends the mission.
    if (!wait_for_pause_interactable("pause_quit", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 5,
            "the PAUSED screen never came back with QUIT");
    picker_testing_yes_or_no_queue_push(true);
    interact("pause_quit");
    return 0;
}

} // namespace

TEST(PauseMenuFlow, real_menu_resume_add_remove_player_and_quit_via_interact)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    // One deployed hero: the mid-game ADD PLAYER claim/spawn scan needs a
    // live teammate (or team anchors) to place the second seat's walker.
    {
        auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
        leader->name = "Leader";
        leader->teamnum = 0;
        game_screen->save_data.team_list[0] = std::move(leader);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    og::ui::pause_menu_testing_set_force_real(true);
    picker_testing_yes_or_no_queue_clear();

    PauseFlowScript flow;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_menu_flow_injector, "pause_menu_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    const auto run_escape_frame = [&]() {
        EventScriptLocal script;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        e.key.repeat = false;
        script.events.push_back(e);
        g_pause_script = &script;

        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        deps.poll_event = pause_scripted_poll;
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        g_pause_script = nullptr;
        return std::pair<GameFrameResult, bool>{result, st.done};
    };

    // Frame 1 blocks in the real menu until the injector clicks RESUME.
    trace_clear();
    const auto [resume_result, resume_done] = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, resume_result);
    EXPECT_FALSE(resume_done);
    EXPECT_TRUE(trace_contains("pause_menu", "open"));
    EXPECT_FALSE(game_screen->world().paused)
        << "RESUME must release the pause";
    EXPECT_FALSE(has_interactable("pause_resume"))
        << "exit hygiene must clear the menu's buttons";

    // Frame 2 blocks again: ADD PLAYER -> the new seat's player screen ->
    // REMOVE PLAYER -> QUIT (all real transport-side seat mutations).
    const auto [quit_result, quit_done] = run_escape_frame();
    EXPECT_EQ(GameFrameResult::AbortedMission, quit_result);
    EXPECT_TRUE(quit_done);
    EXPECT_TRUE(trace_contains("pause_menu", "add_player"));
    EXPECT_TRUE(trace_contains("seats", "add_player index=1"));
    EXPECT_TRUE(trace_contains("pause_menu", "remove_player seat=1"));
    EXPECT_TRUE(trace_contains("seats", "remove_player index=1"));
    EXPECT_TRUE(trace_contains("pause_menu", "quit_confirmed"));
    EXPECT_FALSE(game_screen->world().paused)
        << "QUIT must release the pause too, or the next mission starts "
           "behind a PAUSED overlay nobody can clear (#246)";
    EXPECT_EQ(1, static_cast<int>(game_screen->save_data.numplayers))
        << "the added seat must be gone again after REMOVE PLAYER";

    // The escape tail spins until this is set: signal BEFORE the join, or a
    // leg that gave up would wedge the wait it exists to free.
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result
                                  << " timed out";

    og::ui::pause_menu_testing_set_force_real(false);
    picker_testing_yes_or_no_queue_clear();
    set_game_speed(old_speed);
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}


// ---------------------------------------------------------------------------
// Escape-tail regressions for the three remaining real-menu injectors, in the
// shape add_player_injector_escapes_a_failed_leg_instead_of_wedging already
// pins. Each of these flows drives a BLOCKING pause menu from the main thread:
// run_pause_menu / game_frame_with_result return only when something clicks
// their way out, so an injector leg that gives up used to leave the whole
// binary hung — a missed click under load cost the 420 s og_test_game_core
// ctest ceiling with no assertion text instead of a named failure. Leg 1 is
// sabotaged here (it waits for an id the menu never publishes), so the give-up
// path is the thing under test: the injector must free the main thread and
// report WHICH leg failed.
//
// On a tree without the tails these cases do not return at all, which is how
// they were verified before the fix.
TEST(PauseMenuFlow,
     a_leg_that_gives_up_frees_the_main_thread_for_the_resume_add_remove_flow)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    {
        auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
        leader->name = "Leader";
        leader->teamnum = 0;
        game_screen->save_data.team_list[0] = std::move(leader);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);

    og::ui::pause_menu_testing_set_force_real(true);
    picker_testing_yes_or_no_queue_clear();

    PauseFlowScript flow;
    flow.sabotage_first_leg = true;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_menu_flow_injector, "pause_menu_escape_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    EventScriptLocal script;
    SDL_Event escape_key{};
    escape_key.type = SDL_EVENT_KEY_DOWN;
    escape_key.key.key = SDLK_ESCAPE;
    escape_key.key.repeat = false;
    script.events.push_back(escape_key);
    g_pause_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.enable_frame_timing = false;
    deps.poll_event = pause_scripted_poll;
    const GameFrameResult result =
        game_frame_with_result(*game_screen, st, deps);
    g_pause_script = nullptr;

    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    // The tail's last click may have been pushed after the menu closed under
    // it; a stray mouse event must not ride into the next test's menu.
    escape_tail_join_hygiene();

    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(GameFrameResult::Continue, result)
        << "the escape tail must have closed the menu and let the frame finish";
    EXPECT_FALSE(st.done);
    EXPECT_EQ(1, static_cast<int>(game_screen->save_data.numplayers))
        << "the sabotaged flow never reached ADD PLAYER, so no seat was added";

    og::ui::pause_menu_testing_set_force_real(false);
    picker_testing_yes_or_no_queue_clear();
    set_game_speed(old_speed);
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

// ---------------------------------------------------------------------------
// Regression, re-homed from the retired options_menu (design §7.4): the
// in-game blocking modal must YIELD, not spin.
//
// The old GameLoop.game_frame_options_menu_via_key_prefs_completes covered the
// Emscripten hang: game_frame_with_result -> a modal whose wait loop never
// returns to the browser. Natively the yield is invisible (a plain SDL_Delay),
// so "it completed" proved nothing on its own — the observable is that the
// loop went through og::input_native::sleep_ms, the call that suspends under
// -sASYNCIFY. The PAUSED menu is now the only in-game modal, so the contract
// lives here: open it through the real game_frame_with_result call chain and
// count the yields the blocking frame performs.

namespace
{

int pause_yield_injector(void* data)
{
    auto* const flow = static_cast<PauseFlowScript*>(data);
    og::runtime::ensure_thread_session();

    const char* const resume_id =
        flow->sabotage_first_leg ? "pause_never_published" : "pause_resume";
    if (!wait_for_pause_interactable(resume_id, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 1,
            "the PAUSED screen never published RESUME");
    // Hold the menu open for a COUNTED number of completed frames instead of
    // a flat 400 ms. run_menu_screen posts each completion and then calls
    // og::input_native::sleep_ms(10) once per iteration
    // (menu_screen_runner.cpp), so K completed frames inside this window
    // guarantee at least K-1 yields — while a busy-wait would report zero
    // however many frames it drew. The test reads the count back and pins the
    // yields against it.
    const std::uint64_t frames_before =
        og::ui::menu_screen_testing_completed_frames();
    if (!wait_for_completed_pause_menu_frames(kYieldWindowFrames, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 2,
            "the counted frame window never closed");
    flow->frames_observed.store(
        og::ui::menu_screen_testing_completed_frames() - frames_before);
    interact("pause_resume");
    return 0;
}

} // namespace

TEST(PauseMenuFlow, blocking_menu_yields_to_the_browser_each_iteration)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);

    og::ui::pause_menu_testing_set_force_real(true);

    PauseFlowScript flow;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_yield_injector, "pause_yield_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    const unsigned long yields_before = og::input_native::yield_count();

    EventScriptLocal script;
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_ESCAPE;
    e.key.repeat = false;
    script.events.push_back(e);
    g_pause_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.enable_frame_timing = false;
    deps.poll_event = pause_scripted_poll;
    // Blocks in the real menu until the injector clicks RESUME.
    const GameFrameResult result =
        game_frame_with_result(*game_screen, st, deps);
    g_pause_script = nullptr;

    const unsigned long yields_after = og::input_native::yield_count();

    EXPECT_EQ(GameFrameResult::Continue, result);
    // Counts, not clocks: the injector held the menu open for a known number
    // of COMPLETED frames, and run_menu_screen yields exactly once per
    // iteration after posting the completion — so the yields cannot be fewer
    // than one per completed frame bar the last, whose yield may not have run
    // when the counter was sampled. A raw spin reports zero.
    const std::uint64_t frames_observed = flow.frames_observed.load();
    ASSERT_GE(frames_observed, static_cast<std::uint64_t>(kYieldWindowFrames))
        << "the injector's frame window never closed";
    EXPECT_GE(static_cast<std::uint64_t>(yields_after - yields_before),
              frames_observed - 1)
        << "the blocking pause loop must call og::input_native::sleep_ms once "
           "per iteration — under Emscripten that is the only thing that hands "
           "control back to the browser";

    // The escape tail spins until this is set: signal BEFORE the join, or a
    // leg that gave up would wedge the wait it exists to free.
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    EXPECT_EQ(0, injector_result) << "injector timed out";

    og::ui::pause_menu_testing_set_force_real(false);
    set_game_speed(old_speed);
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

TEST(PauseMenuFlow,
     a_leg_that_gives_up_frees_the_main_thread_for_the_yield_flow)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);

    og::ui::pause_menu_testing_set_force_real(true);

    PauseFlowScript flow;
    flow.sabotage_first_leg = true;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_yield_injector, "pause_yield_escape_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    EventScriptLocal script;
    SDL_Event escape_key{};
    escape_key.type = SDL_EVENT_KEY_DOWN;
    escape_key.key.key = SDLK_ESCAPE;
    escape_key.key.repeat = false;
    script.events.push_back(escape_key);
    g_pause_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.enable_frame_timing = false;
    deps.poll_event = pause_scripted_poll;
    const GameFrameResult result =
        game_frame_with_result(*game_screen, st, deps);
    g_pause_script = nullptr;

    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();

    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(GameFrameResult::Continue, result)
        << "the escape tail must have closed the menu and let the frame finish";
    EXPECT_FALSE(st.done);

    og::ui::pause_menu_testing_set_force_real(false);
    set_game_speed(old_speed);
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

// ---------------------------------------------------------------------------
// Regression, re-homed from the retired options_menu (design §7.2): VIEW
// TEAM return must redraw the SPLIT world canvas — with zoom active the
// world surface is wider than the fixed 320px UI canvas, and a modal that
// only repaints the UI region leaves the extension stale. The pause menu's
// exit dance (World-target redraw + swap) is the mechanism under test.

namespace
{

int pause_view_team_return_injector(void* data)
{
    auto* const flow = static_cast<PauseFlowScript*>(data);
    og::runtime::ensure_thread_session();

    const char* const view_team_id =
        flow->sabotage_first_leg ? "pause_never_published" : "pause_view_team";
    if (!wait_for_pause_interactable(view_team_id, 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 1,
            "the PAUSED screen never published VIEW TEAM");
    interact("pause_view_team");
    SDL_Delay(300);
    // VIEW TEAM's own screen is not a run_menu_screen frame producer, so no
    // completed frame can be counted while it is up: that settle stays a
    // clock, and this leg leans on the stall backstop until the pause menu is
    // back.
    if (!wait_for_pause_interactable("pause_resume", 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 2,
            "the PAUSED screen never came back after VIEW TEAM");
    interact("pause_resume");
    return 0;
}

} // namespace

TEST(PauseMenuFlow, view_team_return_redraws_the_split_world_canvas)
{
    screen* const game = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, game);
    viewscreen* const vs = game->viewob[0].get();
    ASSERT_NE(nullptr, vs);
    ASSERT_NE(nullptr, E_Screen);

    if (!game->world().grid.valid())
        game->world().create_new_grid();
    walker* const saved_control = vs->control;
    walker* created_control = nullptr;
    if (!vs->control)
    {
        created_control = game->world().add_ob(Order::Living, FAMILY_SOLDIER);
        ASSERT_NE(nullptr, created_control);
        created_control->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
        created_control->set_team_num(0);
        created_control->set_user(0);
        vs->control = created_control;
    }

    const int saved_zoom = E_Screen->world_zoom_steps();
    og::WorldScaleMode saved_smoothing = E_Screen->world_scale().mode;
    if (saved_smoothing == og::WorldScaleMode::Legacy)
        saved_smoothing = og::WorldScaleMode::Integer;
    const CanvasTarget saved_target = E_Screen->active_canvas();
    const signed char saved_view = vs->prefs[PREF_VIEW];

    E_Screen->set_world_zoom(5, og::WorldScaleMode::Sai);
    vs->prefs[PREF_VIEW] = PREF_VIEW_FULL;
    game->relayout_views();
    E_Screen->set_active_canvas(CanvasTarget::World);
    ASSERT_EQ(game->world_canvas_w(), E_Screen->render->w);
    ASSERT_EQ(game->world_canvas_h(), E_Screen->render->h);
    ASSERT_GT(E_Screen->render->w, kUiCanvasW)
        << "the test requires a split world canvas";

    constexpr Uint8 sentinel_r = 231;
    constexpr Uint8 sentinel_g = 17;
    constexpr Uint8 sentinel_b = 199;
    const SDL_PixelFormatDetails* const details =
        SDL_GetPixelFormatDetails(E_Screen->render->format);
    ASSERT_NE(nullptr, details);
    const Uint32 sentinel = SDL_MapRGB(
        details, nullptr, sentinel_r, sentinel_g, sentinel_b);
    ASSERT_TRUE(SDL_FillSurfaceRect(E_Screen->render, nullptr, sentinel));

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::pause_menu_testing_set_force_real(true);
    PauseFlowScript flow;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_view_team_return_injector, "pause_vt_injector", &flow);
    ASSERT_NE(nullptr, injector);

    trace_clear();
    const PauseMenuResult outcome = og::ui::run_pause_menu(host);

    // The escape tail spins until this is set: signal BEFORE the join, or a
    // leg that gave up would wedge the wait it exists to free.
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    og::ui::pause_menu_testing_set_force_real(false);
    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result
                                  << " timed out";
    EXPECT_EQ(PauseMenuResult::Resumed, outcome);
    EXPECT_TRUE(trace_contains("pause_menu", "view_team"))
        << "the VIEW TEAM row must actually have hosted the roster";

    EXPECT_EQ(CanvasTarget::World, E_Screen->active_canvas())
        << "the modal must restore gameplay canvas routing";
    bool extended_world_changed = false;
    for (int y = 16; y < 384 && !extended_world_changed; y += 11)
    {
        for (int x = 336; x < 624; x += 13)
        {
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            game->get_pixel(x, y, &r, &g, &b);
            if (r != sentinel_r || g != sentinel_g || b != sentinel_b)
            {
                extended_world_changed = true;
                break;
            }
        }
    }
    EXPECT_TRUE(extended_world_changed)
        << "VIEW TEAM return must redraw beyond the fixed 320px UI width";

    E_Screen->set_world_zoom(saved_zoom, saved_smoothing);
    E_Screen->set_active_canvas(saved_target);
    vs->prefs[PREF_VIEW] = saved_view;
    game->relayout_views();
    vs->control = saved_control;
    if (created_control != nullptr)
    {
        EXPECT_TRUE(game->world().remove_ob(created_control));
    }
}

TEST(PauseMenuFlow,
     a_leg_that_gives_up_frees_the_main_thread_for_the_view_team_flow)
{
    NumplayersGuard numplayers(1);

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::pause_menu_testing_set_force_real(true);

    PauseFlowScript flow;
    flow.sabotage_first_leg = true;
    SDL_Thread* const injector = SDL_CreateThread(
        pause_view_team_return_injector, "pause_vt_escape_injector", &flow);
    ASSERT_NE(nullptr, injector);

    const PauseMenuResult outcome = og::ui::run_pause_menu(host);

    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    og::ui::pause_menu_testing_set_force_real(false);

    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(PauseMenuResult::Resumed, outcome)
        << "the escape tail's RESUME click is what returns run_pause_menu";
}

// ---------------------------------------------------------------------------
// Regression (user report): pause menu -> RESTART MISSION -> confirm landed
// back in Base Camp exactly like QUIT. The frame-level test above only proves
// world().retry gets ARMED; this one reproduces the reporter's flow through
// the REAL relaunch path — picker_main -> GO -> go_menu's
// `do { glad_main } while (world().retry)` — and asserts the level actually
// RELAUNCHES (g_test_game_epoch increments a second time and play continues)
// instead of falling out to the team-build menu.

namespace
{

struct RestartFlowState {
    bool started = false;
    bool finished = false;
    bool game_started = false;
    bool saw_relaunch = false;
    bool relaunch_played = false;
    const char* failure_message = nullptr;
};

constexpr int kRestartPollMs = 25;
// The third-fade observation window, in COMPLETED Base Camp menu frames.
constexpr int kQuitFadeSettleFrames = 20;
constexpr int kRestartStageTimeoutMs = 20'000;

bool restart_wait_for_team_menu(int timeout_ms)
{
    int elapsed = 0;
    int stable_polls = 0;
    while (elapsed < timeout_ms)
    {
        bool has_hire_troops = false;
        bool has_go = false;
        for (const std::string& id : get_button_ids())
        {
            has_hire_troops = has_hire_troops || id == "hire_troops";
            has_go = has_go || id == "go";
        }
        if (has_hire_troops && has_go)
        {
            if (++stable_polls >= 2)
                return true;
        }
        else
        {
            stable_polls = 0;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    return false;
}

int fail_restart_flow(RestartFlowState* state, const char* message)
{
    fprintf(stderr, "  [test] ERROR: %s\n", message);
    state->failure_message = message;
    og::ui::pause_menu_testing_clear_queue();
    picker_testing_yes_or_no_queue_clear();

    // Best-effort unwind so picker_main cannot deadlock: quit any running
    // game, then back out of the team menu.
    if (g_test_in_game.load(std::memory_order_acquire))
    {
        og::ui::pause_menu_testing_queue_outcome(
            og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
        inject_key_press(SDLK_ESCAPE);
        int waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        og::ui::pause_menu_testing_clear_queue();
    }
    if (restart_wait_for_team_menu(5'000))
    {
        SDL_Delay(250);
        // §2.5 base camp: BACK sits on the command strip at (8,178,44,18).
        const auto [back_x, back_y] = ui_canvas_to_window(30.0f, 187.0f);
        inject_click(static_cast<int>(back_x), static_cast<int>(back_y));
    }
    state->finished = true;
    return 0;
}

int restart_relaunch_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<RestartFlowState*>(data);
    state->started = true;

    // -- Main menu -> Continue -> Base Camp (team saved in save0) --
    if (!wait_for_interactable("continue_game", 15'000))
        return fail_restart_flow(state, "main menu never appeared");
    // Counts, not clocks: FadeBetween is a single blit under TESTING, so the
    // settle is about the main menu having COMPOSED, not about an animation.
    // The re-click loop below still covers a click the transition eats.
    if (!wait_for_menu_frames(2))
        return fail_restart_flow(state, "main menu never composed a frame");
    interact("continue_game");
    {
        // The fade can eat the first click: re-click while the main menu is
        // still up (the enter_team_menu_from_main_menu idiom).
        int elapsed = 0;
        while (elapsed < kRestartStageTimeoutMs &&
               !has_interactable("hire_troops"))
        {
            if (has_interactable("continue_game"))
                interact("continue_game");
            SDL_Delay(250);
            elapsed += 250;
        }
    }
    if (!restart_wait_for_team_menu(kRestartStageTimeoutMs))
        return fail_restart_flow(state, "team menu did not appear");
    SDL_Delay(150);

    // -- GO: enter the level through the real go_menu do/while --
    const int epoch_before =
        g_test_game_epoch.load(std::memory_order_acquire);
    {
        int elapsed = 0;
        int since_click = 250;
        while (elapsed < kRestartStageTimeoutMs &&
               g_test_game_epoch.load(std::memory_order_acquire) ==
                   epoch_before)
        {
            if (since_click >= 250 && has_interactable("go"))
            {
                interact("go");
                since_click = 0;
            }
            SDL_Delay(50);
            elapsed += 50;
            since_click += 50;
        }
    }
    if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before)
        return fail_restart_flow(state, "game never started (epoch unchanged)");
    state->game_started = true;

    // Let the launched level actually play a few frames.
    {
        int waited_ms = 0;
        while (g_test_game_frame_ticks.load(std::memory_order_acquire) < 3 &&
               g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        if (g_test_game_frame_ticks.load(std::memory_order_acquire) < 3)
            return fail_restart_flow(state, "game never advanced frames");
    }

    // -- The reporter's action: Esc -> RESTART MISSION -> confirm. The
    // scripted outcome stands in for the (separately tested) menu clicks. --
    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Restart, /*release_pause=*/false);
    inject_key_press(SDLK_ESCAPE);

    // -- EXPECTED: go_menu's while(world().retry) relaunches glad_main, so
    // the epoch increments a SECOND time. --
    {
        int waited_ms = 0;
        while (waited_ms < kRestartStageTimeoutMs)
        {
            if (g_test_game_epoch.load(std::memory_order_acquire) >=
                epoch_before + 2)
            {
                state->saw_relaunch = true;
                break;
            }
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
    }
    if (!state->saw_relaunch)
    {
        return fail_restart_flow(
            state,
            "RESTART did not relaunch the level (no second game epoch; "
            "landed back in Base Camp like QUIT)");
    }

    // -- Play continues in the relaunched level... --
    {
        int waited_ms = 0;
        while (g_test_game_frame_ticks.load(std::memory_order_acquire) < 3 &&
               g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        state->relaunch_played =
            g_test_game_frame_ticks.load(std::memory_order_acquire) >= 3;
    }
    if (!state->relaunch_played)
        return fail_restart_flow(state, "relaunched level never played");

    // -- ...and a real QUIT still exits to Base Camp (no sticky retry). --
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
    inject_key_press(SDLK_ESCAPE);
    {
        int waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        if (g_test_in_game.load(std::memory_order_acquire))
            return fail_restart_flow(state, "QUIT after restart did not exit");
    }

    if (!restart_wait_for_team_menu(kRestartStageTimeoutMs))
        return fail_restart_flow(state, "team menu did not return after quit");
    SDL_Delay(250);
    const auto [back_x, back_y] = ui_canvas_to_window(30.0f, 187.0f);
    inject_click(static_cast<int>(back_x), static_cast<int>(back_y));

    state->finished = true;
    return 0;
}

} // namespace

TEST(PauseMenuFlow, restart_mission_relaunches_level_through_picker_loop)
{
    trace_clear();
    og::ui::pause_menu_testing_clear_queue();
    picker_testing_yes_or_no_queue_clear();

    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    game_screen->save_data.reset();
    game_screen->save_data.numplayers = 1;
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    {
        // Sturdy team so the level neither wins nor loses underneath the test.
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "Leader";
        soldier->constitution = 200;
        soldier->armor = 200;
        game_screen->save_data.team_list[0] = std::move(soldier);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    RestartFlowState state;
    SDL_Thread* const thread = SDL_CreateThread(
        restart_relaunch_injector, "restart_relaunch", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    g_picker_max_mainmenu_calls = 0;
    og::ui::pause_menu_testing_clear_queue();
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(state.finished)
        << (state.failure_message != nullptr ? state.failure_message
                                             : "injector did not finish");
    ASSERT_TRUE(state.game_started) << "GO must start the level";
    EXPECT_TRUE(trace_contains("pause_menu", "restart_level"))
        << "the restart request must reach local_transport_shadow_restart_level";
    ASSERT_TRUE(state.saw_relaunch)
        << "RESTART MISSION must relaunch the level through the picker "
           "do/while (user report: it returned to Base Camp like QUIT)";
    ASSERT_TRUE(state.relaunch_played)
        << "the relaunched level must actually play frames";
    ASSERT_EQ(nullptr, state.failure_message) << state.failure_message;
}

// ---------------------------------------------------------------------------
// Regression (#200): quitting a scenario faded it out TWICE. The teardown in
// go_menu fades whichever canvas was last presented (World, after the pause
// menu's exit dance) and clears it, but the UI canvas still held the pause
// menu image; Base Camp's depth-1 entry fade (#237 derivation) then faded
// THAT out as a second visible transition. On a WIN the results screen
// presents on the UI canvas, so the second fade was an invisible no-op —
// hence "only when I quit". Today the teardown's fade leaves the WINDOW
// black (the video layer's single source of truth), and Base Camp's entry
// fade-out is a no-op on a black window — no note to forget.
//
// Under TESTING every fadeblack lands in FadeBetween's test-mode branch, which
// traces one line per fade. Counting them across the quit is the pin: the
// transition from a running mission back into Base Camp is exactly one
// fade-out (teardown) plus one fade-in (Base Camp entry).

namespace
{

int count_fade_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer)
    {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
        {
            ++fades;
        }
    }
    return fades;
}

struct QuitFadeFlowState {
    bool started = false;
    bool finished = false;
    bool game_started = false;
    bool returned_to_base_camp = false;
    int fades_after_quit = -1;
    const char* failure_message = nullptr;
};

int fail_quit_fade_flow(QuitFadeFlowState* state, const char* message)
{
    fprintf(stderr, "  [test] ERROR: %s\n", message);
    state->failure_message = message;
    og::ui::pause_menu_testing_clear_queue();

    if (g_test_in_game.load(std::memory_order_acquire))
    {
        og::ui::pause_menu_testing_queue_outcome(
            og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
        inject_key_press(SDLK_ESCAPE);
        int waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        og::ui::pause_menu_testing_clear_queue();
    }
    if (restart_wait_for_team_menu(5'000))
    {
        SDL_Delay(250);
        const auto [back_x, back_y] = ui_canvas_to_window(30.0f, 187.0f);
        inject_click(static_cast<int>(back_x), static_cast<int>(back_y));
    }
    state->finished = true;
    return 0;
}

int quit_fade_count_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<QuitFadeFlowState*>(data);
    state->started = true;

    // -- Main menu -> Continue -> Base Camp --
    if (!wait_for_interactable("continue_game", 15'000))
        return fail_quit_fade_flow(state, "main menu never appeared");
    // Counts, not clocks (see restart_relaunch_injector): the fade is a single
    // blit under TESTING, so what the settle wants is a composed main menu.
    if (!wait_for_menu_frames(2))
        return fail_quit_fade_flow(state, "main menu never composed a frame");
    interact("continue_game");
    {
        int elapsed = 0;
        while (elapsed < kRestartStageTimeoutMs &&
               !has_interactable("hire_troops"))
        {
            if (has_interactable("continue_game"))
                interact("continue_game");
            SDL_Delay(250);
            elapsed += 250;
        }
    }
    if (!restart_wait_for_team_menu(kRestartStageTimeoutMs))
        return fail_quit_fade_flow(state, "team menu did not appear");
    SDL_Delay(150);

    // -- GO: enter the level through the real go_menu do/while --
    const int epoch_before =
        g_test_game_epoch.load(std::memory_order_acquire);
    {
        int elapsed = 0;
        int since_click = 250;
        while (elapsed < kRestartStageTimeoutMs &&
               g_test_game_epoch.load(std::memory_order_acquire) ==
                   epoch_before)
        {
            if (since_click >= 250 && has_interactable("go"))
            {
                interact("go");
                since_click = 0;
            }
            SDL_Delay(50);
            elapsed += 50;
            since_click += 50;
        }
    }
    if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before)
        return fail_quit_fade_flow(state, "game never started (epoch unchanged)");
    state->game_started = true;

    {
        int waited_ms = 0;
        while (g_test_game_frame_ticks.load(std::memory_order_acquire) < 3 &&
               g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        if (g_test_game_frame_ticks.load(std::memory_order_acquire) < 3)
            return fail_quit_fade_flow(state, "game never advanced frames");
    }

    // The counting window opens here: glad_init's own two fades are long
    // done, the mission is playing, and nothing fades again until the quit.
    trace_clear();

    // -- Esc -> QUIT -> confirm (the scripted outcome stands in for the
    // separately tested menu clicks). --
    og::ui::pause_menu_testing_clear_queue();
    og::ui::pause_menu_testing_queue_outcome(
        og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
    inject_key_press(SDLK_ESCAPE);
    {
        int waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
        if (g_test_in_game.load(std::memory_order_acquire))
            return fail_quit_fade_flow(state, "QUIT did not exit the mission");
    }

    state->returned_to_base_camp =
        restart_wait_for_team_menu(kRestartStageTimeoutMs);
    if (!state->returned_to_base_camp)
        return fail_quit_fade_flow(state, "team menu did not return after quit");

    // Both expected fades are in, then settle: a third (the bug) lands right
    // between them, so the settled total is what separates 2 from 3.
    {
        int waited_ms = 0;
        while (count_fade_traces() < 2 && waited_ms < kRestartStageTimeoutMs)
        {
            SDL_Delay(kRestartPollMs);
            waited_ms += kRestartPollMs;
        }
    }
    // The window the third fade would land in, counted in COMPOSED Base Camp
    // frames rather than a flat second: the bug's extra fade lands between the
    // two expected ones, so the window only has to outlast the transition, and
    // 20 completed menu frames say it did more strongly than any sleep.
    if (!wait_for_menu_frames(kQuitFadeSettleFrames))
        return fail_quit_fade_flow(state, "base camp stopped composing frames");
    state->fades_after_quit = count_fade_traces();

    SDL_Delay(250);
    const auto [back_x, back_y] = ui_canvas_to_window(30.0f, 187.0f);
    inject_click(static_cast<int>(back_x), static_cast<int>(back_y));

    state->finished = true;
    return 0;
}

} // namespace

TEST(PauseMenuFlow, quit_to_base_camp_fades_out_once)
{
    trace_clear();
    og::ui::pause_menu_testing_clear_queue();
    picker_testing_yes_or_no_queue_clear();

    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    game_screen->save_data.reset();
    game_screen->save_data.numplayers = 1;
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    {
        // Sturdy team so the level neither wins nor loses underneath the test
        // (a win would present the results screen and change the fade count).
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "Leader";
        soldier->constitution = 200;
        soldier->armor = 200;
        game_screen->save_data.team_list[0] = std::move(soldier);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    QuitFadeFlowState state;
    SDL_Thread* const thread =
        SDL_CreateThread(quit_fade_count_injector, "quit_fade_count", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    g_picker_max_mainmenu_calls = 0;
    og::ui::pause_menu_testing_clear_queue();
    picker_testing_yes_or_no_queue_clear();

    ASSERT_TRUE(state.finished)
        << (state.failure_message != nullptr ? state.failure_message
                                             : "injector did not finish");
    ASSERT_TRUE(state.game_started) << "GO must start the level";
    ASSERT_TRUE(state.returned_to_base_camp)
        << "QUIT must land back in Base Camp";
    ASSERT_EQ(2, state.fades_after_quit)
        << "quitting a mission must play exactly one fade-out (go_menu's "
           "teardown) and one fade-in (Base Camp's entry) — a third fade is "
           "#200, the stale UI-canvas menu image faded out a second time";
    ASSERT_EQ(nullptr, state.failure_message) << state.failure_message;
}

// ---------------------------------------------------------------------------
// BUG repro (user report, PR #198): local 1-player game, pause menu ADD
// PLAYER, resume, play, Esc again into the ADDED seat's player screen, cycle
// INPUT — the app died with "InProcessTransport peer 1 is not connected"
// (resolve_peer throw, caught only at main). This walks the reporter's exact
// click sequence through the REAL menus and must survive with the transport
// whole: two live in-process peers, a live world, and further play frames.

namespace
{

struct AddCycleFlow
{
    // Sabotage leg 1 by waiting for an id the menu never publishes. The
    // escape-tail regression below uses it to prove that a leg which gives up
    // still frees the main thread.
    bool sabotage_first_leg = false;
    // Set by the main thread once it is out of the menu for good.
    std::atomic<bool> test_finished{false};
};

int add_cycle_input_injector(void* data)
{
    auto* const flow = static_cast<AddCycleFlow*>(data);
    og::runtime::ensure_thread_session();

    // Menu 1: ADD PLAYER (P2 row appears in place), then RESUME.
    const char* const add_player_id =
        flow->sabotage_first_leg ? "pause_never_published" : "pause_add_player";
    if (!wait_for_pause_interactable(add_player_id, 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 1,
            "the PAUSED screen never published ADD PLAYER");
    interact("pause_add_player");
    if (!wait_for_pause_interactable("pause_player_1", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 2,
            "the new seat's row never appeared");
    interact("pause_resume");

    // Menu 2 (the main thread plays real frames, then sends Esc again):
    // open the ADDED seat's player screen and cycle INPUT twice — the
    // reporter's crash step — then BACK and RESUME.
    if (!wait_for_pause_interactable("pause_player_1", 15'000) ||
        !wait_for_completed_pause_menu_frames(2, 15'000))
        return escape_the_pause_menu(
            flow->test_finished, 3,
            "the second menu never published the added seat's row");
    interact("pause_player_1");
    if (!wait_for_pause_interactable("pause_input", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 4,
            "the player screen never published INPUT");
    // Every press below is proven consumed by what it writes before the next
    // one goes out (tests/test_click_ladder.h), never by a flat settle. Two
    // facts make the ladder safe on the INPUT row, whose dispatch runs a
    // synchronous cfg write that has been measured stalling the main thread
    // for seconds (see kPauseMenuFrameBudget's note): the ladder re-presses
    // only after run_on_main_thread(reset_mouse_click_tracking) has RUN, which
    // blocks until the stalled thread is back, and it then re-checks the edge
    // before pressing — so a press that landed behind a stall is found there
    // and never sent twice. The edge itself is the seat's input selection,
    // read on the menu thread: the value the INPUT row writes.
    const auto seat_input = [] {
        std::string name;
        if (!run_on_main_thread(
                [&name] { name = og::ui::current_input_selection(1).name; }))
            return std::string();
        return name;
    };
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        const std::string before = seat_input();
        if (before.empty())
            return escape_the_pause_menu(
                flow->test_finished, 4,
                "the menu thread never answered the seat's input before an "
                "INPUT press");
        const bool cycled = click_until_edge(
            "pause_input",
            [&before, &seat_input](int wait_ms) {
                const Uint64 started_at = SDL_GetTicks();
                for (;;)
                {
                    const std::string now = seat_input();
                    if (!now.empty() && now != before)
                        return true;
                    if (SDL_GetTicks() - started_at >=
                        static_cast<Uint64>(wait_ms))
                        return false;
                    SDL_Delay(50);
                }
            });
        if (!cycled)
            return escape_the_pause_menu(
                flow->test_finished, 4,
                cycle == 0 ? "the first INPUT press never moved the seat's "
                             "input"
                           : "the second INPUT press never moved the seat's "
                             "input");
    }
    if (!click_until_edge("pause_player_back", [](int wait_ms) {
            return wait_for_interactable("pause_resume", wait_ms);
        }))
        return escape_the_pause_menu(
            flow->test_finished, 5,
            "BACK never brought the PAUSED screen back");
    if (!wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(
            flow->test_finished, 5,
            "the PAUSED screen never composed after BACK");
    interact("pause_resume");
    return 0;
}

} // namespace

TEST(PauseMenuFlow, real_menu_add_player_cycle_input_then_play_survives)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    {
        auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
        leader->name = "Leader";
        leader->teamnum = 0;
        game_screen->save_data.team_list[0] = std::move(leader);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    ASSERT_TRUE(
        og::runtime::local_transport_active(*og::runtime::current_session));

    og::ui::pause_menu_testing_set_force_real(true);
    picker_testing_yes_or_no_queue_clear();

    AddCycleFlow flow;
    SDL_Thread* const injector = SDL_CreateThread(
        add_cycle_input_injector, "add_cycle_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    const auto run_escape_frame = [&]() {
        EventScriptLocal script;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        e.key.repeat = false;
        script.events.push_back(e);
        g_pause_script = &script;

        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        deps.poll_event = pause_scripted_poll;
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        g_pause_script = nullptr;
        return std::pair<GameFrameResult, bool>{result, st.done};
    };

    const auto run_play_frames = [&](int frames) {
        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        for (int i = 0; i < frames; ++i)
        {
            const GameFrameResult result =
                game_frame_with_result(*game_screen, st, deps);
            ASSERT_EQ(GameFrameResult::Continue, result)
                << "play frame " << i << " must continue";
            if (st.done)
                break;
        }
        ASSERT_FALSE(st.done) << "the session must survive play frames";
    };

    // Frame 1 blocks in the menu: ADD PLAYER + RESUME.
    trace_clear();
    const auto [add_result, add_done] = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, add_result);
    EXPECT_FALSE(add_done);
    EXPECT_TRUE(trace_contains("pause_menu", "add_player"));
    EXPECT_TRUE(trace_contains("seats", "add_player index=1"));
    EXPECT_EQ(2, static_cast<int>(game_screen->save_data.numplayers));

    // The user played with both seats before re-pausing.
    run_play_frames(40);

    // Frame 2 blocks again: P2's player screen, INPUT cycle x2, BACK, RESUME
    // (the reporter's crash step).
    const auto [cycle_result, cycle_done] = run_escape_frame();
    EXPECT_EQ(GameFrameResult::Continue, cycle_result);
    EXPECT_FALSE(cycle_done);

    // The session and both in-process peers survived; further play works.
    run_play_frames(40);
    EXPECT_EQ(0, static_cast<int>(game_screen->world().end));

    int injector_result = -1;
    flow.test_finished.store(true);
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result
                                  << " timed out";

    og::ui::pause_menu_testing_set_force_real(false);
    picker_testing_yes_or_no_queue_clear();
    set_game_speed(old_speed);
    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

// Escape-tail regression. This flow drives a BLOCKING pause menu from the main
// thread: game_frame_with_result does not return until something clicks
// RESUME. Before the tail above existed, an injector leg that gave up simply
// returned, nothing ever closed the menu, and the whole binary hung — a
// missed click under load cost a 420 s ctest timeout with no assertion text
// instead of a named failure. Here leg 1 is sabotaged (it waits for an id the
// menu never publishes), so the give-up path is the one under test: the
// injector must free the main thread and report WHICH leg failed.
//
// Bounded by the escape tail's own ceiling — on a tree without the tail this
// case does not return at all, which is how it was verified before the fix.
TEST(PauseMenuFlow, add_player_injector_escapes_a_failed_leg_instead_of_wedging)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    reset_default_player_controls();

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    {
        auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
        leader->name = "Leader";
        leader->teamnum = 0;
        game_screen->save_data.team_list[0] = std::move(leader);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);

    og::ui::pause_menu_testing_set_force_real(true);
    picker_testing_yes_or_no_queue_clear();

    AddCycleFlow flow;
    flow.sabotage_first_leg = true;
    SDL_Thread* const injector = SDL_CreateThread(
        add_cycle_input_injector, "add_cycle_escape_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    EventScriptLocal script;
    SDL_Event escape_key{};
    escape_key.type = SDL_EVENT_KEY_DOWN;
    escape_key.key.key = SDLK_ESCAPE;
    escape_key.key.repeat = false;
    script.events.push_back(escape_key);
    g_pause_script = &script;

    GameLoopFrameState st;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.enable_frame_timing = false;
    deps.poll_event = pause_scripted_poll;
    const GameFrameResult result =
        game_frame_with_result(*game_screen, st, deps);
    g_pause_script = nullptr;

    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    // The tail's last click may have been pushed after the menu closed under
    // it; a stray mouse event must not ride into the next test's menu.
    escape_tail_join_hygiene();

    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(GameFrameResult::Continue, result)
        << "the escape tail must have closed the menu and let the frame finish";
    EXPECT_FALSE(st.done);
    EXPECT_EQ(1, static_cast<int>(game_screen->save_data.numplayers))
        << "the sabotaged flow never reached ADD PLAYER, so no seat was added";

    og::ui::pause_menu_testing_set_force_real(false);
    picker_testing_yes_or_no_queue_clear();
    set_game_speed(old_speed);
    reset_default_player_controls();
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

// ---------------------------------------------------------------------------
// BUG repro (user report, Switch-2 GameCube pad): cycling INPUT past TFGH
// onto the connected joystick HUNG the game one frame after the assignment —
// the click frame completed (the binding grid drew the fresh pad bindings)
// but the next frame's menu-nav release wait spun forever on an axis that
// RESTS beyond the dead zone, which polling can never observe released. The
// cycler face still read "INPUT: TFGH" because the player screen's label
// rewire runs at frame top, before the wedged nav poll ever lets the frame
// draw. This walks the reporter's exact flow through the REAL menus with a
// virtual pad whose Y axis rests at -32768.

namespace
{

// SDL drops joystick state updates while the window lacks focus
// (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS); the virtual axis writes below
// must land regardless of which tests pumped events earlier.
struct BackgroundJoystickEventsGuard
{
    BackgroundJoystickEventsGuard()
    {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    }
    ~BackgroundJoystickEventsGuard()
    {
        SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    }
};

struct HostilePadFlow
{
    SDL_Joystick* pad = nullptr;
    std::atomic<bool> menu_returned{false};
    // The per-cycle give-up reasons are built at the arm; the shared tail
    // prints the pointer after the arm's scope is gone, so it lives here.
    std::string why_detail;
};

int pause_hostile_pad_injector(void* data)
{
    auto* const flow = static_cast<HostilePadFlow*>(data);
    og::runtime::ensure_thread_session();

    // The solo seat's player screen. Every give-up below falls through to
    // the unwind block at the bottom: a bare return here would leave
    // run_pause_menu blocked on the main thread with nothing left to click it
    // out (the wedge the other injectors' escape tails exist to prevent).
    int failure = 0;
    const char* why = "";
    if (!wait_for_pause_interactable("pause_player_0", 10'000))
    {
        failure = 1;
        why = "the solo seat's player row never published (or never composed)";
    }
    if (failure == 0 && !wait_for_completed_pause_menu_frames(2, 10'000))
    {
        failure = 1;
        why = "the solo seat's player row never published (or never composed)";
    }
    if (failure == 0)
    {
        interact("pause_player_0");
        if (!wait_for_pause_interactable("pause_input", 10'000))
        {
            failure = 2;
            why = "the player screen never published INPUT";
        }
        else if (!wait_for_pause_interactable_label(
                     "pause_input", "INPUT: WASD", 5'000))
        {
            failure = 3;
            why = "INPUT did not read WASD";
        }
        else if (!wait_for_completed_pause_menu_frames())
        {
            failure = 4;
            why = "the INPUT frame never completed";
        }
    }

    // WASD -> ARROWS -> IJKL -> TFGH -> JOY1: the fourth cycle assigns the
    // hostile pad to seat 0 (the reporter's "past TFGH" step).
    const std::array<std::string, 4> expected_input_labels = {
        "INPUT: " + og::input::mapping_short_name("ARROWS"),
        "INPUT: " + og::input::mapping_short_name("IJKL"),
        "INPUT: " + og::input::mapping_short_name("TFGH"),
        "INPUT: " + og::input::mapping_short_name("JOY1")};
    for (std::size_t i = 0; i < expected_input_labels.size() && failure == 0;
         ++i)
    {
        interact("pause_input");
        if (!wait_for_pause_interactable_label(
                "pause_input", expected_input_labels[i], 5'000))
        {
            failure = 10 + static_cast<int>(i);
            flow->why_detail = "INPUT cycle " + std::to_string(i) +
                               " never read " + expected_input_labels[i];
            why = flow->why_detail.c_str();
        }
        else if (!wait_for_completed_pause_menu_frames())
        {
            failure = 20 + static_cast<int>(i);
            flow->why_detail =
                "INPUT cycle " + std::to_string(i) + " frame never completed";
            why = flow->why_detail.c_str();
        }
    }

    // The reported hang struck on the frame after the assignment. A live
    // menu still consumes BACK, re-materializes the PAUSED screen, and
    // consumes RESUME.
    if (failure == 0)
    {
        interact("pause_player_back");
        if (wait_for_pause_interactable("pause_resume", 5'000) &&
            wait_for_completed_pause_menu_frames())
        {
            interact("pause_resume");
            for (int waited = 0; waited < 5'000; waited += 50)
            {
                if (flow->menu_returned.load())
                    return 0;
                SDL_Delay(50);
            }
            failure = 31;
            why = "RESUME was consumed but run_pause_menu did not return";
        }
        else
        {
            failure = 30;
            why = "BACK never re-materialized the PAUSED screen";
        }
    }

    // Wedged (the pre-fix behavior). Neutralize the resting axis so the
    // spin can exit and the suite stays bounded, then flush the queued
    // clicks until the menu finally closes.
    SDL_SetJoystickVirtualAxis(flow->pad, 1, 0);
    if (has_interactable("pause_player_back"))
    {
        interact("pause_player_back");
        if (wait_for_interactable("pause_resume", 5'000))
            (void)wait_for_completed_pause_menu_frames();
    }
    // No wall-clock bound: run_pause_menu only returns when something clicks
    // its way out, so a tail that stopped trying would leave the main thread
    // blocked forever — the hang this tail exists to prevent.
    return escape_the_pause_menu(flow->menu_returned, failure, why);
}

} // namespace

TEST(PauseMenuFlow, input_cycler_onto_hostile_resting_pad_does_not_hang_menu)
{
    screen* const scr = og::runtime::current_session->myscreen_;
    ASSERT_NE(nullptr, scr);
    ControlStateGuard controls;
    reset_default_player_controls();
    NumplayersGuard numplayers(1);
    JoystickSubsystemGuard subsystem_guard;
    BackgroundJoystickEventsGuard background_events_guard;
    VirtualJoystick pad(2, 6, 0, "OpenGlad hostile resting pad");
    ASSERT_NE(nullptr, pad.get()) << SDL_GetError();
    JoystickHandleGuard handle_guard;
    const int d = device_index_of(pad.instance_id());
    ASSERT_GE(d, 0);
    ASSERT_LT(d, 10);
    joysticks[d] = pad.get();
    og::input_native::joystick_set_event_state(true);
    for (int p = 0; p < 4; ++p)
        clear_player_joystick(p);

    // The hostile shape: the axis the default layout would bind to UP/DOWN
    // rests at an extreme (GameCube-style analog trigger / hard drift).
    ASSERT_TRUE(SDL_SetJoystickVirtualAxis(pad.get(), 1, -32768));
    SDL_UpdateJoysticks();
    ASSERT_EQ(-32768, SDL_GetJoystickAxis(pad.get(), 1));

    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::pause_menu_testing_set_force_real(true);

    HostilePadFlow flow;
    flow.pad = pad.get();
    SDL_Thread* const injector = SDL_CreateThread(
        pause_hostile_pad_injector, "hostile_pad_injector", &flow);
    ASSERT_NE(nullptr, injector);

    trace_clear();
    const PauseMenuResult outcome = og::ui::run_pause_menu(host);
    flow.menu_returned.store(true);

    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    og::ui::pause_menu_testing_set_force_real(false);

    EXPECT_EQ(0, injector_result)
        << "the menu wedged after the JOY assignment (injector code "
        << injector_result << ")";
    EXPECT_EQ(PauseMenuResult::Resumed, outcome);
    EXPECT_TRUE(trace_contains("joystick", "assigned player=0"))
        << "the fourth INPUT cycle must land on the pad";
    EXPECT_TRUE(playerHasJoystick(0));

    // The flow's persist calls wrote the cycled state into the sandbox cfg;
    // put the factory defaults back on disk so shuffled later tests load
    // clean controls (ControlStateGuard restores only the in-memory state).
    clear_player_joystick(0);
    reset_default_player_controls();
    save_player_control_settings_to_cfg(cfg);
    cfg.save_settings();
}

// ---------------------------------------------------------------------------
// PLAYER row (design §2.2): the pause menu hosts the per-seat settings
// sub-screen. When the paused session dies while that sub-screen is open,
// the sub-screen's first frame tick pumps, sees the dead transport and ends
// the sub-screen; the PAUSED menu must then close too, with SessionEnded,
// instead of reopening over a session that no longer exists.

namespace
{
// The paired control's pump: the session stays alive, but the seat the
// sub-screen was opened for vanishes (numplayers drops to 0) — the
// sub-screen closes for a reason that is NOT a dead session.
bool stub_pump_alive_but_seat_vanishes()
{
    ++g_stub.pump_calls;
    og::runtime::current_session->myscreen_->save_data.numplayers = 0;
    return true;
}
} // namespace

TEST(PauseMenuHandlers, player_row_session_death_in_sub_screen_closes_pause_menu)
{
    const og::ui::MenuScreenSpec& spec = og::ui::pause_menu_screen_spec();
    ASSERT_NE(nullptr, spec.on_spec_row);
    NumplayersGuard numplayers(1);

    g_stub = StubHostState{};
    g_stub.pump_result = false;
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PauseMenuScreenState state;
    state.host = &host;
    og::ui::install_pause_menu_state_for_screen(&state);

    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuPlayerBaseIndex, &state))
        << "a session that died under the player screen closes PAUSED too";
    EXPECT_EQ(PauseMenuResult::SessionEnded, state.outcome);
    EXPECT_EQ(1, g_stub.pump_calls)
        << "the sub-screen's first frame tick pumps once and vetoes the frame";

    // Paired control: the sub-screen closes because its seat vanished while
    // the session is alive -> PAUSED stays open (MENU_OK, Resumed).
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    g_stub = StubHostState{};
    host.pump_paused = &stub_pump_alive_but_seat_vanishes;
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPauseMenuPlayerBaseIndex, &state))
        << "a sub-screen that closed with the session alive keeps PAUSED open";
    EXPECT_EQ(PauseMenuResult::Resumed, state.outcome);
    EXPECT_EQ(1, g_stub.pump_calls);

    og::ui::install_pause_menu_state_for_screen(nullptr);
}

// ---------------------------------------------------------------------------
// #358: click one key in the binding panel to change only that key.

namespace og::ui {
void picker_testing_draw_menu_highlight(const MenuScreenSpec& spec,
                                        const button* buttons,
                                        int highlighted_button);
} // namespace og::ui

namespace
{

PickerState& key_cell_pks() { return *og::runtime::current_session->picker_; }

// The cfg the save writes (memory and the file on disk) and every control
// table, put back exactly as found.
struct KeyCellGuard {
    cfg_store saved_cfg = cfg;
    og::test::ScopedPhysicalFileState config_file{
        std::filesystem::path(get_user_path()) / "cfg" / "openglad.yaml"};
    ControlStateGuard controls;
    KeyCellGuard() { og::input::testing_clear_key_wait_events(); }
    ~KeyCellGuard()
    {
        og::input::testing_clear_key_wait_events();
        cfg = std::move(saved_cfg);
    }
};

og::input_native::EventData key_wait_press(int keycode, bool repeat = false)
{
    og::input_native::EventData event;
    event.type = og::input_native::EventType::KeyDown;
    event.key_sym = keycode;
    event.key_scancode =
        static_cast<int>(SDL_GetScancodeFromKey(
            static_cast<SDL_Keycode>(keycode), nullptr));
    event.key_repeat = repeat;
    return event;
}

std::string saved_mode4_key(int seat, int key)
{
    return cfg.get_setting("controls",
                           std::format("player{}_mode4_key{}", seat + 1, key));
}

constexpr int kFireCell = og::ui::kPausePlayerCellFirstIndex + 8;     // line 0
constexpr int kSpec1Cell = og::ui::kPausePlayerCellFirstIndex + 9;    // line 1
constexpr int kUpRightCell = og::ui::kPausePlayerCellFirstIndex + 1;  // UP-R

} // namespace

// The shared accept rule of the key wait (the real loop and the TESTING
// queue both ask it).
TEST(PausePlayerHandlers, key_wait_accepts_presses_and_drops_repeats)
{
    using og::input_native::EventData;
    using og::input_native::EventType;
    EventData event;
    event.type = EventType::Quit;
    EXPECT_TRUE(og::input::accepts_key_wait_event(event));
    EXPECT_TRUE(og::input::accepts_key_wait_event(key_wait_press(SDLK_F13)));
    EXPECT_FALSE(
        og::input::accepts_key_wait_event(key_wait_press(SDLK_F13, true)))
        << "a held key's auto-repeat is not a press";
    event = EventData{};
    event.type = EventType::KeyUp;
    EXPECT_FALSE(og::input::accepts_key_wait_event(event));
    event = EventData{};
    event.type = EventType::JoyButtonDown;
    EXPECT_TRUE(og::input::accepts_key_wait_event(event));
    event.type = EventType::JoyHatMotion;
    EXPECT_TRUE(og::input::accepts_key_wait_event(event));
    event.type = EventType::JoyAxisMotion;
    event.joy_axis_value = 4000;
    EXPECT_FALSE(og::input::accepts_key_wait_event(event)) << "dead zone";
    event.joy_axis_value = -20000;
    EXPECT_TRUE(og::input::accepts_key_wait_event(event));
    event.joy_axis_value = 20000;
    EXPECT_TRUE(og::input::accepts_key_wait_event(event));
    event = EventData{};
    event.type = EventType::MouseButtonDown;
    EXPECT_FALSE(og::input::accepts_key_wait_event(event));
}

// One cell, one key: the FIRE line's cell binds the key pressed at its
// prompt to FIRE, and the save writes it through to the controls block.
//
// RED (planted): the cell handler skips save_player_keys_after_remap.
TEST(PausePlayerHandlers, key_cell_binds_that_key_alone_and_saves_it)
{
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    NumplayersGuard numplayers(1);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);

    int before[NUM_KEYS];
    for (int k = 0; k < NUM_KEYS; ++k)
        before[k] = og::runtime::current_session->player_keys_[0][k];
    ASSERT_NE(SDLK_F13, before[KEY_FIRE]);

    trace_clear();
    og::input::testing_push_key_wait_event(key_wait_press(SDLK_F13));
    g_stub.pump_calls = 0;
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kFireCell, &state));
    EXPECT_TRUE(trace_contains("remap", "rebind key=8 label=FIRE"));
    EXPECT_TRUE(trace_contains("pause_menu", "rebind seat=0 key=8"));
    EXPECT_GT(g_stub.pump_calls, 0) << "the pause poll runs during the wait";
    EXPECT_EQ(0, og::input::testing_pending_key_wait_events());
    EXPECT_EQ(SDLK_F13,
              og::runtime::current_session->player_keys_[0][KEY_FIRE]);
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        if (k != KEY_FIRE)
        {
            EXPECT_EQ(before[k],
                      og::runtime::current_session->player_keys_[0][k])
                << "only FIRE changes, slot " << k;
        }
    }
    EXPECT_EQ(std::to_string(SDLK_F13), saved_mode4_key(0, KEY_FIRE))
        << "the one-key change is saved like a REMAP";
    EXPECT_EQ("F13", player_control_key_display_name(0, KEY_FIRE));

    // Esc at the prompt keeps the key (the queue is empty: fake ESC).
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kFireCell, &state));
    EXPECT_EQ(SDLK_F13,
              og::runtime::current_session->player_keys_[0][KEY_FIRE]);

    // A cell the seat cannot use asks nothing: UP-R in 4-direction, and the
    // eighth ACTIONS line with the setting off.
    trace_clear();
    og::input::testing_push_key_wait_event(key_wait_press(SDLK_F14));
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kUpRightCell, &state));
    {
        ScopedAbilitySelect off;
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(og::ui::kPausePlayerCellFirstIndex + 15,
                                   &state));
    }
    EXPECT_FALSE(trace_contains("remap", "rebind key="));
    EXPECT_EQ(1, og::input::testing_pending_key_wait_events())
        << "no prompt ran, so nothing read the queue";
    og::input::testing_clear_key_wait_events();

    // ABILITIES with the seat driving nothing opens nothing (the door is
    // dimmed): the handler returns at once.
    EXPECT_EQ(MENU_OK,
              spec.on_spec_row(og::ui::kPausePlayerAbilitiesIndex, &state));
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The conflict rule: the key moves, the other slot of this seat that held
// it shows "--"; another seat holding the same key keeps it.
TEST(PausePlayerHandlers, key_cell_takes_the_key_off_the_seats_other_slot)
{
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    NumplayersGuard numplayers(2);
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::FourDirection));
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);

    const int spec1 = og::runtime::current_session->player_keys_[0][KEY_SPECIAL];
    ASSERT_NE(KEYCODE_UNKNOWN, spec1);
    set_player_key_binding(1, KEY_YELL, spec1);  // P2 shares the key

    og::input::testing_push_key_wait_event(key_wait_press(spec1));
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kFireCell, &state));
    EXPECT_EQ(spec1, og::runtime::current_session->player_keys_[0][KEY_FIRE]);
    EXPECT_EQ(KEYCODE_UNKNOWN,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL]);
    EXPECT_EQ("--", player_control_key_display_name(0, KEY_SPECIAL));
    EXPECT_EQ(spec1, og::runtime::current_session->player_keys_[1][KEY_YELL])
        << "another seat's key is not this seat's to take";
    EXPECT_EQ(std::to_string(KEYCODE_UNKNOWN),
              saved_mode4_key(0, KEY_SPECIAL));
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The key press that opened the prompt (still queued when the wait starts)
// must not bind itself: the wait drops everything queued before it.
//
// RED (planted): assignKeyFromWaitEventPolling skips the clear_events()
// before the wait.
TEST(PausePlayerHandlers, the_key_that_opened_the_prompt_is_not_bound)
{
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    NumplayersGuard numplayers(1);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);
    clear_events();

    const int fire = og::runtime::current_session->player_keys_[0][KEY_FIRE];
    const int spec1 = og::runtime::current_session->player_keys_[0][KEY_SPECIAL];
    ASSERT_NE(fire, spec1);
    // The menu FIRE press that activated the SPEC 1 cell, still queued.
    og::input_native::push_key_event(true, fire);
    trace_clear();
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSpec1Cell, &state));
    EXPECT_TRUE(trace_contains("input", "returning fake ESC"))
        << "nothing was pressed during the wait";
    EXPECT_EQ(spec1,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL])
        << "the activating key must not bind itself to SPEC 1";
    EXPECT_EQ(fire, og::runtime::current_session->player_keys_[0][KEY_FIRE]);
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// A key held down through the prompt auto-repeats; a repeat is not a press.
//
// RED (planted): accepts_key_wait_event ignores key_repeat.
TEST(PausePlayerHandlers, a_held_keys_auto_repeat_binds_nothing)
{
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    NumplayersGuard numplayers(1);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);

    const int spec1 = og::runtime::current_session->player_keys_[0][KEY_SPECIAL];
    og::input::testing_push_key_wait_event(key_wait_press(SDLK_F14, true));
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSpec1Cell, &state));
    EXPECT_EQ(spec1,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL])
        << "a repeat must not bind";

    // Paired control: the same key as a real press binds.
    og::input::testing_push_key_wait_event(key_wait_press(SDLK_F14));
    EXPECT_EQ(MENU_OK, spec.on_spec_row(kSpec1Cell, &state));
    EXPECT_EQ(SDLK_F14,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL]);
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// Which cells a seat can use, and the keyboard walk through the ones it
// can: every mode x setting x REMOVE shape is closed and reachable, a column
// walks top to bottom, and the hidden cells are skipped.
TEST(PauseMenuNav, pause_player_key_cells_route_around_hidden_cells)
{
    ControlStateGuard controls;
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    constexpr int first = og::ui::kPausePlayerCellFirstIndex;
    for (const bool eight_dir : {false, true})
        for (const bool ability_select : {false, true})
            for (const bool can_remove : {false, true})
            {
                SCOPED_TRACE(std::format("eight_dir={} ability_select={} "
                                         "can_remove={}",
                                         eight_dir, ability_select,
                                         can_remove));
                ScopedAbilitySelect scope(ability_select);
                set_player_control_mode(
                    0, static_cast<int>(
                           eight_dir ? ControlDirectionMode::EightDirection
                                     : ControlDirectionMode::FourDirection));
                g_stub = StubHostState{};
                g_stub.can_remove = can_remove;
                PauseMenuHost host = make_stub_host(false, true);
                og::ui::PausePlayerScreenState state;
                state.host = &host;
                state.seat = 0;
                og::ui::install_pause_player_state_for_screen(&state);

                std::vector<button> buttons;
                og::ui::materialize_menu_buttons(spec, buttons);
                int highlighted = spec.default_highlight;
                apply_gates_and_rewire(spec, buttons, highlighted);
                check_pause_nav(buttons, "pause_player");

                std::vector<int> movement;
                std::vector<int> actions;
                for (int line = 0; line < 8; ++line)
                {
                    const bool diagonal = line % 2 == 1;
                    EXPECT_EQ(diagonal && !eight_dir,
                              buttons[static_cast<std::size_t>(first + line)]
                                  .hidden)
                        << "movement line " << line;
                    EXPECT_EQ(line == 7 && !ability_select,
                              buttons[static_cast<std::size_t>(first + 8 +
                                                               line)]
                                  .hidden)
                        << "action line " << line;
                    if (!buttons[static_cast<std::size_t>(first + line)].hidden)
                        movement.push_back(first + line);
                    if (!buttons[static_cast<std::size_t>(first + 8 + line)]
                             .hidden)
                        actions.push_back(first + 8 + line);
                }
                // #376/#388: both doors are on the band (ABILITIES dimmed,
                // still focusable: no body under the seat here).
                EXPECT_FALSE(buttons[og::ui::kPausePlayerAbilitiesIndex].hidden);
                EXPECT_FALSE(buttons[og::ui::kPausePlayerKeymapIndex].hidden);
                const auto& b = buttons;
                EXPECT_EQ(movement.front(), b[og::ui::kPausePlayerInputIndex].nav.down);
                EXPECT_EQ(actions.front(), b[og::ui::kPausePlayerZoomIndex].nav.down);
                // Down out of MOVEMENT is ABILITIES; out of ACTIONS it is
                // REMOVE, or KEYMAP when REMOVE is gone.
                const int movement_exit = og::ui::kPausePlayerAbilitiesIndex;
                const int actions_exit = can_remove
                    ? og::ui::kPausePlayerRemoveIndex
                    : og::ui::kPausePlayerKeymapIndex;
                for (const std::vector<int>* column : {&movement, &actions})
                    for (std::size_t k = 0; k < column->size(); ++k)
                    {
                        const int exit =
                            column == &movement ? movement_exit : actions_exit;
                        const button& cell = b[static_cast<std::size_t>((*column)[k])];
                        EXPECT_EQ(k + 1 < column->size() ? (*column)[k + 1] : exit,
                                  cell.nav.down)
                            << cell.id;
                        if (k > 0)
                        {
                            EXPECT_EQ((*column)[k - 1], cell.nav.up) << cell.id;
                        }
                    }
                // The band, left to right, each face down to BACK; up from
                // ABILITIES is the MOVEMENT column, from KEYMAP the ACTIONS.
                const MenuNav& abilities =
                    b[og::ui::kPausePlayerAbilitiesIndex].nav;
                const MenuNav& keymap = b[og::ui::kPausePlayerKeymapIndex].nav;
                EXPECT_EQ(movement.back(), abilities.up);
                EXPECT_EQ(actions.back(), keymap.up);
                EXPECT_EQ(-1, abilities.left);
                EXPECT_EQ(og::ui::kPausePlayerKeymapIndex, abilities.right);
                EXPECT_EQ(og::ui::kPausePlayerAbilitiesIndex, keymap.left);
                EXPECT_EQ(can_remove ? og::ui::kPausePlayerRemoveIndex : -1,
                          keymap.right);
                EXPECT_EQ(og::ui::kPausePlayerBackIndex, abilities.down);
                EXPECT_EQ(og::ui::kPausePlayerBackIndex, keymap.down);
                if (can_remove)
                {
                    EXPECT_EQ(og::ui::kPausePlayerKeymapIndex,
                              b[og::ui::kPausePlayerRemoveIndex].nav.left);
                }
                // Out right from an action line is its HUD row; the HUD rows
                // come back into the column.
                EXPECT_EQ(og::ui::kPausePlayerHudRadarIndex,
                          b[static_cast<std::size_t>(first + 8)].nav.right);
                for (int hud = og::ui::kPausePlayerHudRadarIndex;
                     hud <= og::ui::kPausePlayerHudScoreIndex; ++hud)
                {
                    const int left = b[static_cast<std::size_t>(hud)].nav.left;
                    EXPECT_TRUE(std::find(actions.begin(), actions.end(), left) !=
                                actions.end())
                        << b[static_cast<std::size_t>(hud)].id;
                }
            }
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// The focus ring on a key cell is a steady box on the cell's own edge: the
// default ring pulses 3 px outward and would paint over the neighbouring
// lines (8 px pitch), and an inward pulse would strike through the line's
// own text.
//
// RED (planted): the cells lose cell_highlight (the outward ring).
TEST(PausePlayerDraw, key_cell_focus_ring_stays_inside_the_cell)
{
    ControlStateGuard controls;
    ScopedAbilitySelect on(true);
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    screen* const scr = og::runtime::current_session->myscreen_;
    std::vector<button> buttons;
    og::ui::materialize_menu_buttons(spec, buttons);
    int highlighted = kFireCell;
    apply_gates_and_rewire(spec, buttons, highlighted);

    const auto capture = [scr]() {
        std::vector<int> frame(320 * 200);
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
                scr->get_pixel(x, y, &frame[static_cast<std::size_t>(y * 320 + x)]);
        return frame;
    };
    const auto render = [&]() {
        spec.draw_background(&state);
        draw_buttons(buttons.data(), static_cast<int>(buttons.size()));
        spec.draw_content(&state);
    };
    render();
    const std::vector<int> baseline = capture();
    const bool nav_was = key_cell_pks().menu_nav_enabled;
    const button& cell = buttons[static_cast<std::size_t>(kFireCell)];
    int changed = 0;
    int outside = 0;
    int over_text = 0;
    for (int sample = 0; sample < 60; ++sample)
    {
        render();
        key_cell_pks().menu_nav_enabled = true;
        og::ui::picker_testing_draw_menu_highlight(spec, buttons.data(),
                                                   kFireCell);
        const std::vector<int> focused = capture();
        for (int y = 0; y < 200; ++y)
            for (int x = 0; x < 320; ++x)
            {
                const std::size_t i = static_cast<std::size_t>(y * 320 + x);
                if (focused[i] == baseline[i])
                    continue;
                ++changed;
                if (x < cell.x || x >= cell.x + cell.sizex || y < cell.y ||
                    y >= cell.y + cell.sizey)
                    ++outside;
                else if (x != cell.x && x != cell.x + cell.sizex - 1 &&
                         y != cell.y && y != cell.y + cell.sizey - 1)
                    ++over_text;
            }
        SDL_Delay(5);
    }
    key_cell_pks().menu_nav_enabled = nav_was;
    EXPECT_GT(changed, 0) << "the focus ring is drawn";
    EXPECT_EQ(0, outside) << "the ring never leaves the 8 px cell";
    EXPECT_EQ(0, over_text) << "the ring never crosses the line's text";
    og::ui::install_pause_player_state_for_screen(nullptr);
}

// PR proof frames: the FIRE line's cell under the focus ring, then the
// one-key prompt it opens; and the REMAP prompt with a four-line and a
// six-line summary (#383).
TEST(PausePlayerDraw, menushots_key_cell_and_wrapped_prompts)
{
    if (std::getenv("MENUSHOTS_DIR") == nullptr)
        GTEST_SKIP() << "set MENUSHOTS_DIR to write the PR proof frames";
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    g_stub = StubHostState{};
    g_stub.can_remove = false;
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    state.player_number = 1;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    screen* const scr = og::runtime::current_session->myscreen_;

    const auto shoot_panel = [&](const char* name) {
        button* const buttons = spec.buttons_accessor();
        const int count = spec.count_accessor();
        og::runtime::current_session->localbuttons_ =
            init_buttons(buttons, count);
        std::vector<button> gated(buttons, buttons + count);
        int highlighted = kFireCell;
        apply_gates_and_rewire(spec, gated, highlighted);
        for (int i = 0; i < count; ++i)
            buttons[i].hidden = gated[static_cast<std::size_t>(i)].hidden;
        spec.draw_background(&state);
        draw_buttons(buttons, count);
        spec.draw_content(&state);
        const bool nav_was = key_cell_pks().menu_nav_enabled;
        key_cell_pks().menu_nav_enabled = true;
        og::ui::picker_testing_draw_menu_highlight(spec, buttons, kFireCell);
        key_cell_pks().menu_nav_enabled = nav_was;
        EXPECT_TRUE(write_menushot(scr, name));
        clear_allbuttons();
        og::runtime::current_session->localbuttons_ = nullptr;
    };
    shoot_panel("pause_key_cell_fire");

    g_remap_shot_polls = 0;
    g_remap_shot_prompt = 1;
    g_remap_shot_name = "pause_key_cell_fire_prompt";
    g_remap_shot_written = false;
    (void)rebind_player_key_with_poll(0, KEY_FIRE, &remap_shot_poll);
    EXPECT_TRUE(g_remap_shot_written);
    // The panel once the prompt took a key (F, as a player would press it).
    set_player_key_binding(0, KEY_FIRE, SDLK_F);
    shoot_panel("pause_key_cell_fire_after");

    // Four lines: 8-direction numpad movement with the issue's long names.
    set_player_control_mode(
        0, static_cast<int>(ControlDirectionMode::EightDirection));
    const int numpad[8] = {SDLK_KP_8, SDLK_KP_9, SDLK_KP_6, SDLK_KP_3,
                           SDLK_KP_2, SDLK_KP_1, SDLK_KP_4, SDLK_KP_7};
    const int dirs[8] = {KEY_UP, KEY_UP_RIGHT, KEY_RIGHT, KEY_DOWN_RIGHT,
                         KEY_DOWN, KEY_DOWN_LEFT, KEY_LEFT, KEY_UP_LEFT};
    for (int i = 0; i < 8; ++i)
        set_player_key_binding(0, dirs[i], numpad[i]);
    set_player_key_binding(0, KEY_FIRE, SDLK_VOLUMEDOWN);
    set_player_key_binding(0, KEY_SPECIAL, SDLK_MEDIA_PAUSE);
    set_player_key_binding(0, KEY_SPEC2, SDLK_AC_REFRESH);
    set_player_key_binding(0, KEY_SPEC3, SDLK_MODE);
    ASSERT_EQ(4u, build_player_control_summary_lines(0, true).size());
    g_remap_shot_polls = 0;
    g_remap_shot_name = "remap_wrapped_4_lines";
    g_remap_shot_written = false;
    edit_player_keymap_with_poll(0, &remap_shot_poll);
    EXPECT_TRUE(g_remap_shot_written);

    // Six lines: ten-glyph names everywhere (the worst case).
    const int ten[8] = {SDLK_VOLUMEDOWN, SDLK_MEDIA_PAUSE, SDLK_AC_REFRESH,
                        SDLK_AC_FORWARD, SDLK_MODE, SDLK_VOLUMEDOWN,
                        SDLK_MEDIA_PAUSE, SDLK_AC_REFRESH};
    for (int i = 0; i < 8; ++i)
        set_player_key_binding(0, dirs[i], ten[i]);
    for (const int key : {KEY_FIRE, KEY_SPECIAL, KEY_SPEC2, KEY_SPEC3,
                          KEY_YELL, KEY_SWITCH, KEY_SHIFTER, KEY_LOOKUP})
        set_player_key_binding(0, key, SDLK_AC_FORWARD);
    ASSERT_EQ(6u, build_player_control_summary_lines(0, true).size());
    g_remap_shot_polls = 0;
    g_remap_shot_name = "remap_wrapped_6_lines";
    g_remap_shot_written = false;
    edit_player_keymap_with_poll(0, &remap_shot_poll);
    EXPECT_TRUE(g_remap_shot_written);
    g_remap_shot_name = "remap_p1_spec2";
    og::ui::install_pause_player_state_for_screen(nullptr);
}

namespace
{

struct KeyCellFlow
{
    std::atomic<bool> test_finished{false};
};

// The real menu: PAUSED -> P1's player screen -> one click on the FIRE
// line's cell (the key "pressed" at its prompt waits in the test queue) ->
// BACK -> RESUME. The click is proven by the value it writes, read on the
// menu thread.
int key_cell_click_injector(void* data)
{
    auto* const flow = static_cast<KeyCellFlow*>(data);
    og::runtime::ensure_thread_session();

    if (!wait_for_pause_interactable("pause_player_0", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(flow->test_finished, 1,
                                     "the PAUSED screen never showed P1");
    if (!click_until_edge("pause_player_0", [](int wait_ms) {
            return wait_for_interactable("pause_key_action_0", wait_ms);
        }))
        return escape_the_pause_menu(flow->test_finished, 2,
                                     "P1's player screen never opened");
    if (!wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(flow->test_finished, 2,
                                     "the player screen never composed");

    og::input::testing_push_key_wait_event(key_wait_press(SDLK_F15));
    const auto fire_key = [] {
        int key = KEYCODE_UNKNOWN;
        if (!run_on_main_thread([&key] {
                key = og::runtime::current_session->player_keys_[0][KEY_FIRE];
            }))
            return KEYCODE_UNKNOWN;
        return key;
    };
    if (!click_until_edge("pause_key_action_0", [&fire_key](int wait_ms) {
            const Uint64 started_at = SDL_GetTicks();
            for (;;)
            {
                if (fire_key() == SDLK_F15)
                    return true;
                if (SDL_GetTicks() - started_at >= static_cast<Uint64>(wait_ms))
                    return false;
                SDL_Delay(50);
            }
        }))
        return escape_the_pause_menu(flow->test_finished, 3,
                                     "the FIRE cell never bound the key");

    if (!click_until_edge("pause_player_back", [](int wait_ms) {
            return wait_for_interactable("pause_resume", wait_ms);
        }))
        return escape_the_pause_menu(flow->test_finished, 4,
                                     "BACK never brought PAUSED back");
    if (!wait_for_completed_pause_menu_frames(2, 10'000))
        return escape_the_pause_menu(flow->test_finished, 4,
                                     "PAUSED never composed after BACK");
    interact("pause_resume");
    return 0;
}

} // namespace

// #358 end to end: a click on the FIRE line in the PAUSED player screen
// rebinds FIRE alone and saves it.
//
// RED (planted): the cell handler skips save_player_keys_after_remap (the
// cfg pin below).
TEST(PauseMenuFlow, one_click_on_the_fire_line_rebinds_fire)
{
    KeyCellGuard guard;
    ScopedAbilitySelect on(true);
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);

    game_screen->save_data.reset();
    game_screen->save_data.current_campaign = "gladiator";
    game_screen->save_data.current_levels["gladiator"] = 1;
    game_screen->save_data.scen_num = 1;
    game_screen->save_data.numplayers = 1;
    {
        auto leader = std::make_unique<guy>(FAMILY_SOLDIER);
        leader->name = "Leader";
        leader->teamnum = 0;
        game_screen->save_data.team_list[0] = std::move(leader);
        game_screen->save_data.team_size = 1;
    }
    ASSERT_TRUE(game_screen->save_data.save("save0"));

    glad_init();
    ASSERT_TRUE(og::runtime::current_game_session != nullptr);
    og::ui::pause_menu_testing_set_force_real(true);
    picker_testing_yes_or_no_queue_clear();
    const int before_special =
        og::runtime::current_session->player_keys_[0][KEY_SPECIAL];

    KeyCellFlow flow;
    SDL_Thread* const injector = SDL_CreateThread(
        key_cell_click_injector, "key_cell_injector", &flow);
    ASSERT_TRUE(injector != nullptr);

    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);
    {
        EventScriptLocal script;
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_ESCAPE;
        e.key.repeat = false;
        script.events.push_back(e);
        g_pause_script = &script;
        GameLoopFrameState st;
        GameLoopDeps deps;
        deps.enable_render = false;
        deps.enable_event_poll = true;
        deps.enable_frame_timing = false;
        deps.poll_event = pause_scripted_poll;
        trace_clear();
        const GameFrameResult result =
            game_frame_with_result(*game_screen, st, deps);
        g_pause_script = nullptr;
        EXPECT_EQ(GameFrameResult::Continue, result);
        EXPECT_FALSE(st.done);
    }

    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result
                                  << " timed out";

    EXPECT_TRUE(trace_contains("pause_menu", "rebind seat=0 key=8"));
    EXPECT_EQ(SDLK_F15,
              og::runtime::current_session->player_keys_[0][KEY_FIRE]);
    EXPECT_EQ(before_special,
              og::runtime::current_session->player_keys_[0][KEY_SPECIAL])
        << "one click changes one key";
    EXPECT_EQ(std::to_string(SDLK_F15), saved_mode4_key(0, KEY_FIRE))
        << "the click's key is saved";

    og::ui::pause_menu_testing_set_force_real(false);
    picker_testing_yes_or_no_queue_clear();
    set_game_speed(old_speed);
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    game_screen->world().delete_objects();
    game_screen->world().end = 0;
    game_screen->world().retry = false;
}

// ---------------------------------------------------------------------------
// #376: ABILITIES from the PAUSED menu. The pause player screen's ABILITIES
// door opens the screen over the character the seat drives; BACK sends a
// changed layout to the server (one BeltChange), whose next snapshot carries
// it back to the mirror and so to the HUD.

namespace
{

namespace belt = og::sim::belt;

// The entry the family names `name` (a primary or an alternate).
belt::Entry entry_named(int family, const std::string& name)
{
    const FamilyDescriptor* const fd = get_family_descriptor(family);
    for (const belt::Entry& entry : belt::catalog(fd, 1))
    {
        const char* const named =
            entry.alt ? og::sim::alternate_name(fd, entry.slot, 1)
                      : og::sim::special_name(fd, entry.slot, 1);
        if (named != nullptr && name == named)
            return entry;
    }
    ADD_FAILURE() << "no ability named " << name;
    return {};
}

// An Archmage (level 12: MIND CONTROL waits on the bench) under seat 1 and a
// Soldier teammate, started the way Base Camp's GO starts a mission (the
// local lobby's start config), so the display world is the in-process
// server's mirror and both heroes carry their guys. Returns the mirror hero.
walker* start_abilities_mission(screen& game_screen)
{
    SaveData& save = game_screen.save_data;
    save.reset();
    save.current_campaign = "gladiator";
    save.current_levels["gladiator"] = 1;
    save.scen_num = 1;
    save.numplayers = 1;
    save.new_specials = 1;
    auto archmage = std::make_unique<guy>(FAMILY_ARCHMAGE);
    archmage->name = "MERLIN";
    archmage->teamnum = 0;
    archmage->upgrade_to_level(12);
    archmage->deployed = true;
    auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
    soldier->name = "BRAVO";
    soldier->teamnum = 0;
    soldier->deployed = true;
    save.team_list[0] = std::move(archmage);
    save.team_list[1] = std::move(soldier);
    save.team_size = 2;
    if (!save.save("save0"))
        return nullptr;
    std::optional<og::ui::PickerLobbyGameStartConfig> config;
    {
        auto client = og::ui::create_local_picker_lobby_client();
        client->initialize_from_save();
        config = client->build_game_start_config();
        client->shutdown();
    }
    if (!config.has_value())
        return nullptr;
    glad_init(false, &*config);
    if (og::runtime::current_game_session == nullptr ||
        !og::runtime::local_transport_active(*og::runtime::current_session))
        return nullptr;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int frame = 0;
         frame < 10 && game_screen.viewob[0]->control == nullptr; ++frame)
        game_frame_with_result(game_screen,
                               og::runtime::current_session->frame_state_,
                               deps);
    walker* const hero = game_screen.viewob[0]->control;
    return hero != nullptr && hero->myguy != nullptr &&
                   hero->myguy->name == "MERLIN"
        ? hero
        : nullptr;
}

void end_abilities_mission(screen& game_screen)
{
    og::runtime::clear_local_transport_shadow(
        *og::runtime::current_game_session);
    for (auto& view : game_screen.viewob)
    {
        if (view)
            view->control = nullptr;
    }
    game_screen.world().delete_objects();
    game_screen.world().end = 0;
    game_screen.world().retry = false;
}

// The mirror walker whose guy is named `name`, or nullptr.
walker* mirror_hero_named(screen& game_screen, const std::string& name)
{
    for (const auto& ob : game_screen.world().oblist)
    {
        if (ob && !ob->dead() && ob->myguy != nullptr &&
            ob->myguy->name == name)
            return ob.get();
    }
    return nullptr;
}

// The SERVER's guy behind `entity_id` (its belt), read on the main thread.
std::optional<belt::Bytes> server_belt(std::uint32_t entity_id)
{
    screen* const server = og::runtime::local_transport_shadow_testing_server_screen(
        *og::runtime::current_game_session);
    if (server == nullptr)
        return std::nullopt;
    const walker* const body = server->world().find_by_id(entity_id);
    if (body == nullptr || body->myguy == nullptr)
        return std::nullopt;
    return body->myguy->belt;
}

// Doors, in precedence order: ABILITIES' BACK, then the pause menu's.
constexpr EscapeDoor kPauseAbilitiesDoors[] = {
    {"abilities_back", "abilities_back"},
    {"pause_resume", "pause_resume"},
    {"pause_player_back", "pause_player_back"},
};

bool label_selected(const std::string& id, int ms)
{
    return wait_for_interactable_label_matching(
        id, [](const std::string& label) {
            return !label.empty() && label.front() == '>';
        },
        ms);
}

bool label_has(const std::string& id, const std::string& want, int ms)
{
    return wait_for_interactable_label_matching(
        id, [&want](const std::string& label) {
            return label.find(want) != std::string::npos;
        },
        ms);
}

// On the open ABILITIES screen: select belt face `belt_id`, then put the
// benched MIND CONTROL there. False when a step never landed.
bool place_mind_control(const std::string& belt_id, int slot)
{
    if (!wait_for_interactable(belt_id, 10'000) || !wait_for_menu_frames(2))
        return false;
    const std::string select = "select " + std::to_string(slot);
    if (!click_until_edge(
            belt_id, [&belt_id](int ms) { return label_selected(belt_id, ms); },
            select.c_str(), 3, 2500, "abilities"))
        return false;
    if (interactable_label("bench_0").find("MIND CONTROL") == std::string::npos)
    {
        fprintf(stderr, "  [abilities] bench_0 reads '%s'\n",
                interactable_label("bench_0").c_str());
        return false;
    }
    const std::string place = "place slot=" + std::to_string(slot);
    return click_until_edge(
        "bench_0",
        [&belt_id](int ms) { return label_has(belt_id, "MIND CONTROL", ms); },
        place.c_str(), 3, 2500, "abilities");
}

struct PauseAbilitiesFlow
{
    std::atomic<bool> test_finished{false};
    std::string belt_id = "belt_5";  // S3
    int slot = 5;
    std::uint32_t hero_id = 0;
    belt::Bytes before{};
    // What the injector read on the main thread after BACK.
    std::optional<belt::Bytes> server_after;
    bool door_lit = false;
    bool shots = false;  // MENUSHOTS_DIR stills of the door and the screen
};

// PAUSED -> P1 -> ABILITIES -> MIND CONTROL on the flow's slot -> BACK
// (wait for the SERVER's guy to take it, while still paused) -> BACK ->
// RESUME.
int pause_abilities_injector(void* data)
{
    auto* const flow = static_cast<PauseAbilitiesFlow*>(data);
    og::runtime::ensure_thread_session();
    const auto leave = [flow](int leg, const char* why) {
        return escape_to_the_main_thread(flow->test_finished, leg, why,
                                         kPauseAbilitiesDoors);
    };

    if (!wait_for_pause_interactable("pause_player_0", 10'000) ||
        !wait_for_completed_pause_menu_frames(2, 10'000))
        return leave(1, "the PAUSED screen never showed P1");
    if (!click_until_edge("pause_player_0", [](int ms) {
            return wait_for_interactable("pause_abilities", ms);
        }))
        return leave(2, "P1's player screen never opened");
    if (!wait_for_completed_pause_menu_frames(2, 10'000))
        return leave(2, "the player screen never composed");
    flow->door_lit = has_enabled_interactable("pause_abilities");
    if (flow->shots)
        (void)run_on_main_thread([] {
            (void)write_menushot(og::runtime::current_session->myscreen_,
                                 "pause_player_doors");
        });
    if (!click_until_edge("pause_abilities", [](int ms) {
            return wait_for_interactable("abilities_back", ms);
        }))
        return leave(3, "ABILITIES never opened");
    if (flow->shots && wait_for_interactable(flow->belt_id, 10'000) &&
        wait_for_menu_frames(2))
        (void)run_on_main_thread([] {
            (void)write_menushot(og::runtime::current_session->myscreen_,
                                 "pause_abilities_before");
        });
    if (!place_mind_control(flow->belt_id, flow->slot))
        return leave(4, "MIND CONTROL never reached the slot");
    if (flow->shots && wait_for_menu_frames(2))
        (void)run_on_main_thread([] {
            (void)write_menushot(og::runtime::current_session->myscreen_,
                                 "pause_abilities_mind_control_s3");
        });
    if (!click_until_edge("abilities_back", [](int ms) {
            return wait_for_interactable("pause_abilities", ms);
        }))
        return leave(5, "BACK never brought the player screen back");

    // The player screen pumps the paused session every frame: the server's
    // guy takes the layout within a few frames.
    const Uint64 started = SDL_GetTicks();
    for (;;)
    {
        std::optional<belt::Bytes> now;
        (void)run_on_main_thread(
            [flow, &now] { now = server_belt(flow->hero_id); });
        if (now.has_value() && *now != flow->before)
        {
            flow->server_after = now;
            break;
        }
        if (SDL_GetTicks() - started >= 10'000)
        {
            flow->server_after = now;
            break;  // the main thread's assertions name the miss
        }
        SDL_Delay(50);
    }

    if (!click_until_edge("pause_player_back", [](int ms) {
            return wait_for_interactable("pause_resume", ms);
        }))
        return leave(6, "BACK never brought PAUSED back");
    if (!wait_for_completed_pause_menu_frames(2, 10'000))
        return leave(6, "PAUSED never composed after BACK");
    interact("pause_resume");
    return 0;
}

// One Esc frame through the real game loop (the real PauseMenuHost, so the
// real set_belt wiring), blocking until the injector clicks RESUME.
GameFrameResult run_escape_frame_with(screen& game_screen,
                                      PauseAbilitiesFlow& flow)
{
    SDL_Thread* const injector = SDL_CreateThread(
        pause_abilities_injector, "pause_abilities_injector", &flow);
    EXPECT_TRUE(injector != nullptr);
    EventScriptLocal script;
    SDL_Event e{};
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.key = SDLK_ESCAPE;
    e.key.repeat = false;
    script.events.push_back(e);
    g_pause_script = &script;
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = true;
    deps.enable_frame_timing = false;
    deps.poll_event = pause_scripted_poll;
    const GameFrameResult result = game_frame_with_result(
        game_screen, og::runtime::current_session->frame_state_, deps);
    g_pause_script = nullptr;
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result
                                  << " gave up";
    return result;
}

} // namespace

// #376 end to end: P1 opens ABILITIES from the PAUSED menu, puts MIND
// CONTROL on S3 and goes BACK; the SERVER's guy takes the layout while the
// game is still paused; after RESUME the mirror hero carries it and the HUD's
// SHIFTER row draws MIND CONTROL on key 3.
//
// RED (planted): the screen's BACK skips host.set_belt (the server's guy
// keeps the old layout).
TEST(PauseMenuFlow, abilities_from_the_paused_menu_reach_the_server_guy_and_the_hud)
{
    ScopedAbilitySelect on(true);
    KeyCellGuard guard;
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    walker* const hero = start_abilities_mission(*game_screen);
    ASSERT_NE(nullptr, hero) << "the mission never put MERLIN under seat 1";
    og::ui::pause_menu_testing_set_force_real(true);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    PauseAbilitiesFlow flow;
    flow.hero_id = hero->entity_id();
    flow.before = hero->myguy->belt;
    flow.shots = std::getenv("MENUSHOTS_DIR") != nullptr;
    const std::optional<belt::Bytes> server_before = server_belt(flow.hero_id);
    ASSERT_TRUE(server_before.has_value());
    belt::Bytes want = *server_before;
    want[5] = belt::encode(entry_named(FAMILY_ARCHMAGE, "MIND CONTROL"));
    ASSERT_NE(want, *server_before) << "S3 must not hold MIND CONTROL yet";

    trace_clear();
    EXPECT_EQ(GameFrameResult::Continue,
              run_escape_frame_with(*game_screen, flow));
    EXPECT_TRUE(flow.door_lit) << "ABILITIES must be lit for a living hero";
    EXPECT_TRUE(trace_contains("abilities", "pause open seat=0"));
    EXPECT_TRUE(trace_contains("abilities", "pause commit seat=0"));
    ASSERT_TRUE(flow.server_after.has_value());
    EXPECT_EQ(want, *flow.server_after)
        << "the server's guy takes the layout while the game is paused";
    EXPECT_FALSE(game_screen->world().paused) << "RESUME released the pause";

    // The next frames' snapshots carry it to the mirror.
    GameLoopDeps deps;
    deps.enable_render = false;
    deps.enable_event_poll = false;
    deps.enable_frame_timing = false;
    for (int frame = 0; frame < 3; ++frame)
        game_frame_with_result(*game_screen,
                               og::runtime::current_session->frame_state_,
                               deps);
    walker* const mirror = game_screen->world().find_by_id(flow.hero_id);
    ASSERT_NE(nullptr, mirror);
    ASSERT_NE(nullptr, mirror->myguy);
    EXPECT_EQ(want, mirror->myguy->belt);

    // The HUD: with SHIFTER held, key 3's cell is MIND CONTROL.
    {
        FakeKeyboard keyboard;
        ASSERT_TRUE(keyboard.set(0, KEY_SHIFTER, true));
        trace_clear();
        game_screen->clearbuffer();
        (void)new_score_panel(game_screen, 1);
        // The PR still of that frame, world and HUD together: with the world
        // canvas pinned to the classic 320x200 the scenery and the HUD land
        // on the one surface write_menushot reads (unpinned, the world draws
        // in its own plane and the still would be the HUD over black).
        if (flow.shots)
        {
            const CanvasTarget saved_target = game_screen->active_canvas();
            game_screen->set_world_canvas_pinned_classic(true);
            game_screen->relayout_views();
            (void)game_screen->redraw();
            (void)new_score_panel(game_screen, 1);
            EXPECT_TRUE(write_menushot(game_screen,
                                       "pause_abilities_hud_after_resume"));
            game_screen->set_active_canvas(CanvasTarget::UI);
            game_screen->set_world_canvas_pinned_classic(false);
            game_screen->relayout_views();
            game_screen->set_active_canvas(saved_target);
        }
    }
    const belt::Entry mind = entry_named(FAMILY_ARCHMAGE, "MIND CONTROL");
    EXPECT_TRUE(trace_contains(
        "hud", std::format("belt_cell key=3 shift=1 slot={} alt=0 "
                           "icon=icons/mind_control.png",
                           static_cast<int>(mind.slot))
                   .c_str()))
        << "the HUD's S3 cell must show MIND CONTROL";

    og::ui::pause_menu_testing_set_force_real(false);
    set_game_speed(old_speed);
    end_abilities_mission(*game_screen);
}

// A SPEC key held through the pause latched its entry from the OLD layout.
// The layout changes under it (MIND CONTROL onto key 1); on the first frame
// after RESUME the key, still down, must not cast the old entry.
//
// RED (planted): the commit leaves input_hardware_state().belt_latch alone.
TEST(PauseMenuFlow, a_spec_key_held_through_the_pause_cannot_cast_the_old_entry)
{
    ScopedAbilitySelect on(true);
    KeyCellGuard guard;
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    walker* const hero = start_abilities_mission(*game_screen);
    ASSERT_NE(nullptr, hero);
    og::ui::pause_menu_testing_set_force_real(true);
    const float old_speed = og::runtime::current_session->g_game_speed_factor_;
    set_game_speed(0.0f);

    FakeKeyboard keyboard;
    ASSERT_TRUE(keyboard.set(0, KEY_SPECIAL, true));
    GameLoopDeps press;
    press.enable_render = false;
    press.enable_event_poll = false;
    press.enable_frame_timing = false;
    press.enable_tick = false;
    game_frame_with_result(*game_screen,
                           og::runtime::current_session->frame_state_, press);
    const std::uint8_t old_cast = ctx().input.players[0].belt_cast;
    ASSERT_NE(0, old_cast) << "SPEC 1 down latches key 1's entry";
    ASSERT_EQ(1, input_hardware_state().belt_latch[0].active_key);

    PauseAbilitiesFlow flow;
    flow.belt_id = "belt_0";
    flow.slot = 0;
    flow.hero_id = hero->entity_id();
    flow.before = hero->myguy->belt;
    trace_clear();
    EXPECT_EQ(GameFrameResult::Continue,
              run_escape_frame_with(*game_screen, flow));
    ASSERT_TRUE(trace_contains("abilities", "pause commit seat=0"));

    // The Esc frame sampled the keys after the menu closed, SPEC 1 still
    // down: no press edge, so nothing casts until the key goes up and down.
    const PlayerInput& sent = ctx().input.players[0];
    EXPECT_NE(old_cast, sent.belt_cast)
        << "the old layout's key-1 entry was cast after RESUME";
    EXPECT_EQ(0, sent.belt_cast);
    EXPECT_FALSE(sent.held[static_cast<int>(InputAction::Special)]);

    og::ui::pause_menu_testing_set_force_real(false);
    set_game_speed(old_speed);
    end_abilities_mission(*game_screen);
}

namespace
{

int g_stub_set_belt_calls = 0;
bool stub_set_belt(int /*seat*/, std::uint32_t /*entity_id*/,
                   const belt::Bytes& /*belt*/)
{
    ++g_stub_set_belt_calls;
    return true;
}

bool stub_abilities_pump(void* /*context*/)
{
    return true;
}

struct StaleBodyFlow
{
    std::atomic<bool> test_finished{false};
    std::uint32_t other_id = 0;
    bool closed_by_itself = false;
};

// MIND CONTROL onto S3, then the seat starts driving the teammate (a
// Switch): the screen must close by itself, sending nothing. Only if it
// stays up does the tail press BACK, which is what would send the edit.
int stale_body_injector(void* data)
{
    auto* const flow = static_cast<StaleBodyFlow*>(data);
    og::runtime::ensure_thread_session();
    constexpr EscapeDoor kDoors[] = {{"abilities_back", "abilities_back"}};
    const auto leave = [flow, &kDoors](int leg, const char* why) {
        return escape_to_the_main_thread(flow->test_finished, leg, why, kDoors);
    };
    if (!place_mind_control("belt_5", 5))
        return leave(1, "MIND CONTROL never reached S3");
    (void)run_on_main_thread([flow] {
        screen& s = *og::runtime::current_session->myscreen_;
        s.viewob[0]->control = s.world().find_by_id(flow->other_id);
    });
    // The closed screen's buttons stay registered until the main thread
    // tears them down, so the screen's own trace is the witness.
    if (!wait_for_trace("abilities", "pause closed: control changed", 10'000))
        return leave(2, "ABILITIES stayed up after the body changed");
    flow->closed_by_itself = true;
    return 0;
}

} // namespace

// The stale-body rule on the screen's side: the edit names the body the
// seat drove when the screen opened; a Switch (or a possession) between
// opening and BACK closes the screen and sends nothing.
//
// RED (planted): the "same body" check always passes (the screen stays up,
// the tail's BACK sends the edit for a body the seat no longer drives).
TEST(PausePlayerHandlers, abilities_close_without_sending_when_the_seat_changes_body)
{
    ScopedAbilitySelect on(true);
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    walker* const hero = start_abilities_mission(*game_screen);
    ASSERT_NE(nullptr, hero);
    walker* const teammate = mirror_hero_named(*game_screen, "BRAVO");
    ASSERT_NE(nullptr, teammate) << "the Soldier teammate is not on the mirror";

    g_stub_set_belt_calls = 0;
    og::ui::PauseAbilitiesHost host;
    host.seat = 0;
    host.pump = &stub_abilities_pump;
    host.set_belt = &stub_set_belt;
    StaleBodyFlow flow;
    flow.other_id = teammate->entity_id();
    trace_clear();
    SDL_Thread* const injector =
        SDL_CreateThread(stale_body_injector, "stale_body", &flow);
    ASSERT_TRUE(injector != nullptr);
    const og::ui::PauseAbilitiesResult result =
        og::ui::run_pause_abilities_screen(host);
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    clear_allbuttons();

    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result;
    EXPECT_TRUE(flow.closed_by_itself);
    EXPECT_TRUE(result.opened);
    EXPECT_TRUE(result.control_changed);
    EXPECT_FALSE(result.committed);
    EXPECT_EQ(0, g_stub_set_belt_calls)
        << "an edit for a body the seat no longer drives must not be sent";
    EXPECT_TRUE(trace_contains("abilities", "pause closed: control changed seat=0"));

    game_screen->viewob[0]->control = hero;
    end_abilities_mission(*game_screen);
}

namespace
{

struct ReadOnlyFlow
{
    std::atomic<bool> test_finished{false};
    std::string status;
    std::string title_face;
};

int read_only_injector(void* data)
{
    auto* const flow = static_cast<ReadOnlyFlow*>(data);
    og::runtime::ensure_thread_session();
    constexpr EscapeDoor kDoors[] = {{"abilities_back", "abilities_back"}};
    if (!wait_for_interactable("belt_0", 10'000) || !wait_for_menu_frames(2))
        return escape_to_the_main_thread(flow->test_finished, 1,
                                         "ABILITIES never opened", kDoors);
    (void)run_on_main_thread([flow] {
        flow->status = og::ui::abilities_screen_status_line();
    });
    // A click on a slot is refused, not an edit.
    if (!click_and_acknowledge_trace("belt_0", "abilities", "read_only slot=0",
                                     /*waits_for_autosave=*/false))
        return escape_to_the_main_thread(flow->test_finished, 2,
                                         "the read-only click never landed",
                                         kDoors);
    return escape_to_the_main_thread(flow->test_finished, 0, "", kDoors);
}

} // namespace

// A body with no guy record (a summon, a raised dead, a possessed host)
// opens read-only with its class default and sends nothing.
TEST(PausePlayerHandlers, abilities_on_a_body_with_no_saved_layout_open_read_only)
{
    ScopedAbilitySelect on(true);
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    walker* const hero = start_abilities_mission(*game_screen);
    ASSERT_NE(nullptr, hero);
    // The hero's body with its record taken away for the test: what a
    // summon or a possessed host looks like to the screen.
    guy* const record = hero->myguy;
    hero->myguy = nullptr;

    g_stub_set_belt_calls = 0;
    og::ui::PauseAbilitiesHost host;
    host.seat = 0;
    host.pump = &stub_abilities_pump;
    host.set_belt = &stub_set_belt;
    ReadOnlyFlow flow;
    trace_clear();
    SDL_Thread* const injector =
        SDL_CreateThread(read_only_injector, "read_only", &flow);
    ASSERT_TRUE(injector != nullptr);
    const og::ui::PauseAbilitiesResult result =
        og::ui::run_pause_abilities_screen(host);
    flow.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    escape_tail_join_hygiene();
    clear_allbuttons();
    hero->myguy = record;

    EXPECT_EQ(0, injector_result) << "injector leg " << injector_result;
    EXPECT_TRUE(result.opened);
    EXPECT_FALSE(result.committed);
    EXPECT_EQ(0, g_stub_set_belt_calls);
    EXPECT_EQ(og::ui::kNoSavedLayoutNotice, flow.status);
    EXPECT_TRUE(trace_contains("abilities", "editable=0"));
    end_abilities_mission(*game_screen);
}

// The door's state: dimmed with New Ability Select off, dimmed with no
// living body under the seat, lit otherwise.
TEST(PausePlayerHandlers, abilities_door_follows_the_setting_and_the_body)
{
    screen* const game_screen = og::runtime::current_session->myscreen_;
    ASSERT_TRUE(game_screen != nullptr);
    ScopedAbilitySelect on(true);
    walker* const hero = start_abilities_mission(*game_screen);
    ASSERT_NE(nullptr, hero);
    g_stub = StubHostState{};
    PauseMenuHost host = make_stub_host(false, true);
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 0;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    const std::vector<const og::ui::MenuButtonSpec*> rows =
        og::ui::materialized_spec_rows(spec);
    const og::ui::MenuButtonSpec& door =
        *rows[static_cast<std::size_t>(og::ui::kPausePlayerAbilitiesIndex)];
    ASSERT_NE(nullptr, door.state_override);
    const og::ui::MenuLabelContext context{};

    EXPECT_EQ(og::ui::RowState::Visible, door.state_override(context));
    {
        ScopedAbilitySelect off(false);
        EXPECT_EQ(og::ui::RowState::Disabled, door.state_override(context));
        trace_clear();
        EXPECT_EQ(MENU_OK, spec.on_spec_row(og::ui::kPausePlayerAbilitiesIndex,
                                            &state));
        EXPECT_FALSE(trace_contains("abilities", "pause open"));
    }
    game_screen->viewob[0]->control = nullptr;
    EXPECT_EQ(og::ui::RowState::Disabled, door.state_override(context));
    game_screen->viewob[0]->control = hero;
    hero->set_dead(1);
    EXPECT_EQ(og::ui::RowState::Disabled, door.state_override(context));
    hero->set_dead(0);

    og::ui::install_pause_player_state_for_screen(nullptr);
    end_abilities_mission(*game_screen);
}
