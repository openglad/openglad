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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/interface/button.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/input_cycler.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/pause_menu.h>
#include <openglad/platform/game_loop.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/local_transport_shadow.h>
#include <openglad/platform/sai2x.h>
#include <openglad/resources/save_data.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <atomic>

void glad_init(bool preserve_frame_timing = false);
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
    EXPECT_FALSE(spec.backdrop);
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
    static const ExpectedButton kExpected[] = {
        {"pause_player_back", "BACK", KEYSTATE_ESCAPE, 10, 8, 50, 15,
         button_action_id(ButtonAction::ReturnMenu), MENU_REDRAW,
         MenuNav{.up = 5, .down = 2}},
        {"pause_input", "INPUT: WASD", KEYSTATE_UNKNOWN, 12, 54, 98, 18,
         button_action_id(ButtonAction::MenuSpecRow), 1,
         MenuNav{.up = 2, .down = 5, .right = 6}},
        {"pause_direction", "4-DIRECTION", KEYSTATE_UNKNOWN, 12, 30, 98, 18,
         button_action_id(ButtonAction::MenuSpecRow), 2,
         MenuNav{.up = 0, .down = 1, .right = 3}},
        {"pause_remap", "REMAP", KEYSTATE_UNKNOWN, 116, 30, 92, 18,
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
         MenuNav{.up = 3, .down = 7, .left = 1}},
        {"pause_hud_radar", "RADAR: ON", KEYSTATE_UNKNOWN, 214, 78, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 7,
         MenuNav{.up = 4, .down = 8, .left = 6}},
        {"pause_hud_life", "HP: ON", KEYSTATE_UNKNOWN, 214, 100, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 8,
         MenuNav{.up = 7, .down = 9, .left = 1}},
        {"pause_hud_foes", "FOES: ON", KEYSTATE_UNKNOWN, 214, 122, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 9,
         MenuNav{.up = 8, .down = 10, .left = 1}},
        {"pause_hud_score", "SCORE: ON", KEYSTATE_UNKNOWN, 214, 144, 90, 18,
         button_action_id(ButtonAction::MenuSpecRow), 10,
         MenuNav{.up = 9, .down = 5, .left = 1}},
    };

    og::ui::install_pause_player_state_for_screen(nullptr);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();
    button* const buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    check_exact_table(buttons, count, kExpected,
                      static_cast<int>(std::size(kExpected)), "pause_player");

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

    compose_and_capture("pause_player_screen");

    ASSERT_TRUE(assign_joystick_to_player(0, d));
    compose_and_capture("pause_player_joystick");
    clear_player_joystick(0);

    og::ui::install_pause_player_state_for_screen(nullptr);
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

    // RESTART confirmed.
    state = og::ui::PauseMenuScreenState{};
    state.host = &host;
    picker_testing_yes_or_no_queue_push(true);
    EXPECT_EQ(MENU_EXIT,
              spec.on_spec_row(og::ui::kPauseMenuRestartIndex, &state));
    EXPECT_EQ(PauseMenuResult::Restart, state.outcome);

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

int pause_menu_flow_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();

    // Leg 1: RESUME closes the menu.
    if (!wait_for_interactable("pause_resume", 10'000))
        return 1;
    SDL_Delay(300);
    interact("pause_resume");

    // Leg 2: ADD PLAYER creates a real second seat mid-game; its player row
    // appears in place.
    if (!wait_for_interactable("pause_add_player", 10'000))
        return 2;
    SDL_Delay(300);
    interact("pause_add_player");
    if (!wait_for_interactable("pause_player_1", 10'000))
        return 3;
    SDL_Delay(300);

    // Leg 3: the new seat's row opens the player screen; REMOVE PLAYER
    // (NO-first confirm answered from the queue) drops the seat and pops
    // back to the PAUSED screen whose row re-hides.
    interact("pause_player_1");
    if (!wait_for_interactable("pause_remove", 10'000))
        return 4;
    SDL_Delay(300);
    picker_testing_yes_or_no_queue_push(true);
    interact("pause_remove");

    // Leg 4: back on the PAUSED screen, QUIT ends the mission.
    if (!wait_for_interactable("pause_quit", 10'000))
        return 5;
    SDL_Delay(300);
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

    SDL_Thread* const injector = SDL_CreateThread(
        pause_menu_flow_injector, "pause_menu_injector", nullptr);
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

    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
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

int pause_yield_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    if (!wait_for_interactable("pause_resume", 10'000))
        return 1;
    // Long enough that a yielding loop (10 ms per iteration) racks up a clear
    // count, while a busy-wait would leave it at zero.
    SDL_Delay(400);
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

    SDL_Thread* const injector = SDL_CreateThread(
        pause_yield_injector, "pause_yield_injector", nullptr);
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
    // The injector held the menu open for ~400 ms at a 10 ms yield cadence.
    // A raw spin would report 0; the loose bound keeps the pin off wall-clock
    // scheduling luck.
    EXPECT_GT(yields_after - yields_before, 5UL)
        << "the blocking pause loop must call og::input_native::sleep_ms once "
           "per iteration — under Emscripten that is the only thing that hands "
           "control back to the browser";

    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
    EXPECT_EQ(0, injector_result) << "injector timed out";

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

int pause_view_team_return_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();
    if (!wait_for_interactable("pause_view_team", 10'000))
        return 1;
    SDL_Delay(300);
    interact("pause_view_team");
    SDL_Delay(300);
    if (!wait_for_interactable("pause_resume", 10'000))
        return 2;
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
    SDL_Thread* const injector = SDL_CreateThread(
        pause_view_team_return_injector, "pause_vt_injector", nullptr);
    ASSERT_NE(nullptr, injector);

    trace_clear();
    const PauseMenuResult outcome = og::ui::run_pause_menu(host);

    int injector_result = -1;
    SDL_WaitThread(injector, &injector_result);
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
    SDL_Delay(750); // fadeblack eats events
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
    SDL_Delay(750); // fadeblack eats events
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
    SDL_Delay(1'000);
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

int add_cycle_input_injector(void* /*data*/)
{
    og::runtime::ensure_thread_session();

    // Menu 1: ADD PLAYER (P2 row appears in place), then RESUME.
    if (!wait_for_interactable("pause_add_player", 10'000))
        return 1;
    SDL_Delay(300);
    interact("pause_add_player");
    if (!wait_for_interactable("pause_player_1", 10'000))
        return 2;
    SDL_Delay(300);
    interact("pause_resume");

    // Menu 2 (the main thread plays real frames, then sends Esc again):
    // open the ADDED seat's player screen and cycle INPUT twice — the
    // reporter's crash step — then BACK and RESUME.
    if (!wait_for_interactable("pause_player_1", 15'000))
        return 3;
    SDL_Delay(300);
    interact("pause_player_1");
    if (!wait_for_interactable("pause_input", 10'000))
        return 4;
    SDL_Delay(300);
    interact("pause_input");
    SDL_Delay(200);
    interact("pause_input");
    SDL_Delay(200);
    interact("pause_player_back");
    if (!wait_for_interactable("pause_resume", 10'000))
        return 5;
    SDL_Delay(300);
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

    SDL_Thread* const injector = SDL_CreateThread(
        add_cycle_input_injector, "add_cycle_injector", nullptr);
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
    SDL_WaitThread(injector, &injector_result);
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
};

bool wait_for_completed_pause_menu_frame(int timeout_ms = 5'000)
{
    const std::uint64_t completed_before =
        og::ui::menu_screen_testing_completed_frames();
    for (int waited = 0; waited < timeout_ms; waited += 50)
    {
        if (og::ui::menu_screen_testing_completed_frames() > completed_before)
            return true;
        SDL_Delay(50);
    }
    return false;
}

int pause_hostile_pad_injector(void* data)
{
    auto* const flow = static_cast<HostilePadFlow*>(data);
    og::runtime::ensure_thread_session();

    // The solo seat's player screen.
    if (!wait_for_interactable("pause_player_0", 10'000))
        return 1;
    SDL_Delay(300);
    interact("pause_player_0");
    if (!wait_for_interactable("pause_input", 10'000))
        return 2;
    int failure = 0;
    if (!wait_for_interactable_label("pause_input", "INPUT: WASD", 5'000))
        failure = 3;
    else if (!wait_for_completed_pause_menu_frame())
        failure = 4;

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
        if (!wait_for_interactable_label(
                "pause_input", expected_input_labels[i], 5'000))
            failure = 10 + static_cast<int>(i);
        else if (!wait_for_completed_pause_menu_frame())
            failure = 20 + static_cast<int>(i);
    }

    // The reported hang struck on the frame after the assignment. A live
    // menu still consumes BACK, re-materializes the PAUSED screen, and
    // consumes RESUME.
    if (failure == 0)
    {
        interact("pause_player_back");
        if (wait_for_interactable("pause_resume", 5'000) &&
            wait_for_completed_pause_menu_frame())
        {
            interact("pause_resume");
            for (int waited = 0; waited < 5'000; waited += 50)
            {
                if (flow->menu_returned.load())
                    return 0;
                SDL_Delay(50);
            }
            failure = 31;
        }
        else
            failure = 30;
    }

    // Wedged (the pre-fix behavior). Neutralize the resting axis so the
    // spin can exit and the suite stays bounded, then flush the queued
    // clicks until the menu finally closes.
    SDL_SetJoystickVirtualAxis(flow->pad, 1, 0);
    if (has_interactable("pause_player_back"))
    {
        interact("pause_player_back");
        if (wait_for_interactable("pause_resume", 5'000))
            (void)wait_for_completed_pause_menu_frame();
    }
    for (int waited = 0; waited < 10'000; waited += 250)
    {
        if (flow->menu_returned.load())
            return failure != 0 ? failure : 7;
        if (has_interactable("pause_resume"))
            interact("pause_resume");
        SDL_Delay(250);
    }
    return 8;
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
