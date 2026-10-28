/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// KEYMAP (#388) in real SDL: run_keymap_screen draws this machine's seats
// on the keyboard picture from their LIVE keys (a custom key shows where it
// is now), titles them, lists what the picture cannot show, and closes on
// BACK; the pause copy pumps the paused session every frame and closes when
// the session ends. The picture is drawn through a native plane, so the
// flows read the PHYSICAL frame back; OG_KEYMAP_SHOT_DIR names a directory
// to write the stills into (640x400 and 1280x800).
//
// Injector flows: every settle is a completed engine frame or a named
// element.

#include <gtest/gtest.h>

#include <openglad/core/test_trace.h>
#include <openglad/interface/input.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/keyboard_picture.h>
#include <openglad/interface/ui/keymap_screen.h>
#include <openglad/interface/ui/menu_screen_spec.h>
#include <openglad/interface/ui/pause_menu.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/platform/game_session.h>
#include <openglad/platform/sai2x.h>

#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_escape_tail.h"
#include "test_fake_lobby_client.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

extern og::input_native::JoystickHandle joysticks[10];

namespace
{

namespace kp = og::ui::keyboard_picture;

constexpr EscapeDoor kKeymapDoor[] = {{"keymap_back", "keymap_back"}};

const char* shot_dir()
{
    return std::getenv("OG_KEYMAP_SHOT_DIR");
}

// The four seats' keys and modes, put back after the test changes them.
struct ControlsRestore
{
    int keys[4][NUM_KEYS] = {};
    int modes[4] = {};
    ControlsRestore()
    {
        std::memcpy(keys, input_player_keys(), sizeof(keys));
        for (int seat = 0; seat < 4; ++seat)
            modes[seat] = get_player_control_mode(seat);
        reset_default_player_controls();
    }
    ~ControlsRestore()
    {
        for (int seat = 0; seat < 4; ++seat)
            set_player_control_mode(seat, modes[seat]);
        std::memcpy(input_player_keys(), keys, sizeof(keys));
    }
    ControlsRestore(const ControlsRestore&) = delete;
    ControlsRestore& operator=(const ControlsRestore&) = delete;
};

// The window at w x h for one capture, put back after.
struct WindowSize
{
    int win_w = 0;
    int win_h = 0;
    float old_w;
    float old_h;
    WindowSize()
        : old_w(og::runtime::current_session->window_w_),
          old_h(og::runtime::current_session->window_h_)
    {
        SDL_GetWindowSize(E_Screen->window, &win_w, &win_h);
    }
    ~WindowSize() { apply(win_w, win_h, old_w, old_h); }
    WindowSize(const WindowSize&) = delete;
    WindowSize& operator=(const WindowSize&) = delete;

    static void apply(int w, int h, float session_w, float session_h)
    {
        (void)SDL_SetWindowSize(E_Screen->window, w, h);
        (void)SDL_SyncWindow(E_Screen->window);
        og::runtime::current_session->window_w_ = session_w;
        og::runtime::current_session->window_h_ = session_h;
        update_overscan_setting();
        E_Screen->set_world_zoom(og::kZoomStepsMax,
                                 og::WorldScaleMode::Integer, w, h);
        E_Screen->set_active_canvas(CanvasTarget::UI);
    }
    static void set(int w, int h)
    {
        apply(w, h, static_cast<float>(w), static_cast<float>(h));
    }
};

// What the injector read on the menu thread while the screen was up.
struct Seen
{
    std::atomic<bool> finished{false};
    const char* shot_name = "keymap";
    bool up = false;
    std::string title;
    std::vector<std::string> legend;
    std::vector<int> players_on_caps;  // per caps() index: first player, -1
    bool pictured = false;
    PhysicalFrame physical;
};

// The picture's key-cap colours in the physical band (test_changelog_flow's
// rule: saturated, red and green at least 40 apart).
long band_cap_pixels(const PhysicalFrame& frame)
{
    const double scale = std::min(frame.w / 320.0, frame.h / 200.0);
    const double ox = (frame.w - 320.0 * scale) / 2.0;
    const double oy = (frame.h - 200.0 * scale) / 2.0;
    long caps = 0;
    for (int y = static_cast<int>(oy + og::ui::kKeymapBandY * scale);
         y < static_cast<int>(oy + (og::ui::kKeymapBandY + og::ui::kKeymapBandH) *
                                       scale);
         y += 2)
        for (int x = static_cast<int>(ox + og::ui::kKeymapBandX * scale);
             x < static_cast<int>(ox + (og::ui::kKeymapBandX + og::ui::kKeymapBandW) *
                                           scale);
             x += 2) {
            const std::array<Uint8, 3> c = frame.at(x, y);
            const int hi = std::max({c[0], c[1], c[2]});
            const int lo = std::min({c[0], c[1], c[2]});
            const int rg = c[0] > c[1] ? c[0] - c[1] : c[1] - c[0];
            if (hi >= 140 && hi - lo >= 90 && rg >= 40)
                ++caps;
        }
    return caps;
}

int read_and_close(void* data)
{
    Seen& seen = *static_cast<Seen*>(data);
    const auto leave = [&seen](int leg, const char* why) {
        return escape_to_the_main_thread(seen.finished, leg, why, kKeymapDoor);
    };
    if (!wait_for_interactable("keymap_back", 15000))
        return leave(1, "KEYMAP never came up");
    seen.up = true;
    if (!wait_for_menu_frames(2))
        return leave(2, "KEYMAP completed no frames");
    (void)run_on_main_thread([&seen] {
        seen.title = og::ui::keymap_screen_title();
        seen.legend = og::ui::keymap_screen_legend();
        if (const kp::Output* out = og::ui::keymap_screen_picture()) {
            seen.pictured = out->labels;
            for (const kp::DrawnCap& drawn : out->drawn)
                seen.players_on_caps.push_back(
                    drawn.player_numbers.empty() ? -1
                                                 : drawn.player_numbers[0]);
        }
    });
    seen.physical = capture_physical_frame(seen.shot_name, shot_dir());
    return leave(0, "");
}

Sint32 run_with_injector(Seen& seen, int seat, const og::ui::KeymapScreenHost& host,
                         bool* session_ended)
{
    trace_clear();
    SDL_Thread* thread = SDL_CreateThread(read_and_close, "keymap", &seen);
    EXPECT_NE(nullptr, thread);
    const Sint32 result = og::ui::run_keymap_screen(seat, host, session_ended);
    seen.finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    clear_allbuttons();
    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    return result;
}

int player_on(const Seen& seen, int keycode)
{
    const kp::Cap* cap = kp::cap_for_keycode(keycode);
    if (cap == nullptr)
        return -2;
    const auto index = static_cast<std::size_t>(cap - kp::caps().data());
    return index < seen.players_on_caps.size() ? seen.players_on_caps[index]
                                               : -2;
}

} // namespace

TEST(KeymapScreen, shows_the_live_keys_of_every_seat_and_back_closes_it)
{
    ASSERT_TRUE(E_Screen);
    ControlsRestore restore;
    // Player 1 looks up with Caps Lock, a key no factory table uses; player
    // 2 also fires with Space, player 3's FIRE; player 2 yells on keypad 5,
    // which has no cap.
    input_player_keys()[0][KEY_LOOKUP] = KEYCODE_CAPSLOCK;
    input_player_keys()[1][KEY_FIRE] = KEYCODE_SPACE;
    input_player_keys()[1][KEY_YELL] = KEYCODE_KP_5;
    og::ui::KeymapScreenHost host;
    host.seats = {{0, 1}, {1, 2}, {2, 3}};
    WindowSize window;
    struct Size { int w, h; const char* name; };
    for (const Size size : {Size{640, 400, "keymap_640x400"},
                            Size{1280, 800, "keymap_1280x800"}}) {
        SCOPED_TRACE(size.name);
        WindowSize::set(size.w, size.h);
        Seen seen;
        seen.shot_name = size.name;
        // Opened by player 2: it goes first in the title.
        const Sint32 result = run_with_injector(seen, 1, host, nullptr);
        EXPECT_EQ(MENU_REDRAW, result) << "BACK returns to the seat screen";
        ASSERT_TRUE(seen.up);
        EXPECT_TRUE(trace_contains("keymap", "KEYMAP: closed seat=1"));
        EXPECT_EQ(0u, seen.title.rfind("KEYS: P2 ", 0)) << seen.title;
        EXPECT_NE(std::string::npos, seen.title.find("  P1 ")) << seen.title;
        EXPECT_NE(std::string::npos, seen.title.find("  P3 ")) << seen.title;
        EXPECT_TRUE(seen.pictured) << "the picture drew no labels";
        // The custom key is where it is now; the seat's old LOOK UP key
        // (q) is free; player 4 is not one of this machine's seats.
        EXPECT_EQ(1, player_on(seen, KEYCODE_CAPSLOCK));
        EXPECT_EQ(-1, player_on(seen, KEYCODE_q));
        EXPECT_EQ(-1, player_on(seen, KEYCODE_t)) << "P4 is not seated";
        EXPECT_EQ(2, player_on(seen, KEYCODE_UP));
        // The opening seat's uses come first.
        const std::vector<std::string> want = {"P2 FIRE + P3 FIRE: SPACE",
                                               "P2 YELL: KEYPAD 5"};
        EXPECT_EQ(want, seen.legend);
        ASSERT_TRUE(seen.physical.captured);
        EXPECT_EQ(size.w, seen.physical.w);
        EXPECT_GT(band_cap_pixels(seen.physical), 500)
            << "the picture band shows no keyboard";
    }
}

namespace
{

struct PumpCount
{
    int calls = 0;
    int live_for = 0;
};

bool count_pump(void* context)
{
    auto& count = *static_cast<PumpCount*>(context);
    ++count.calls;
    return count.calls <= count.live_for;
}

} // namespace

TEST(KeymapScreen, pause_copy_pumps_every_frame_and_closes_when_the_session_ends)
{
    ASSERT_TRUE(E_Screen);
    ControlsRestore restore;
    PumpCount count;
    count.live_for = 1 << 30;
    og::ui::KeymapScreenHost host;
    host.seats = {{0, 1}};
    host.pump = &count_pump;
    host.pump_context = &count;
    Seen seen;
    bool ended = true;
    EXPECT_EQ(MENU_REDRAW, run_with_injector(seen, 0, host, &ended));
    EXPECT_FALSE(ended) << "BACK is not the session ending";
    EXPECT_GE(count.calls, 2) << "the paused session was not pumped per frame";

    // The session ends under the open screen: it closes by itself (the
    // injector only presses BACK if it has not closed by the deadline, so a
    // screen that ignores the pump fails instead of hanging).
    count = {};
    count.live_for = 3;
    trace_clear();
    ended = false;
    std::atomic<bool> finished{false};
    struct Watch {
        std::atomic<bool>* finished;
    } watch{&finished};
    SDL_Thread* thread = SDL_CreateThread(
        [](void* data) -> int {
            auto& w = *static_cast<Watch*>(data);
            if (wait_for_trace("keymap", "KEYMAP: closed", 10000))
                return 0;
            return escape_to_the_main_thread(
                *w.finished, 1, "KEYMAP stayed up after the session ended",
                kKeymapDoor);
        },
        "keymap_watch", &watch);
    const Sint32 result = og::ui::run_keymap_screen(0, host, &ended);
    finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    clear_allbuttons();
    EXPECT_EQ(0, thread_result) << "the screen did not close by itself";
    EXPECT_EQ(MENU_REDRAW, result);
    EXPECT_TRUE(ended);
    EXPECT_EQ(4, count.calls) << "the screen ran on after the pump said stop";
}

TEST(KeymapScreen, base_camp_copy_is_in_the_registry_and_the_pause_copy_is_an_overlay)
{
    const og::ui::MenuScreenHost& host =
        og::ui::menu_screen_host(og::ui::MenuScreenId::Keymap);
    ASSERT_EQ(og::ui::MenuScreenHost::Kind::Engine, host.kind);
    EXPECT_EQ(&og::ui::keymap_menu_screen_spec(), host.spec);
    EXPECT_EQ(og::ui::RemoteStartScope::TeamBuildScope, host.spec->remote_start);
    EXPECT_TRUE(host.spec->polls_lobby);
    const og::ui::MenuScreenSpec& pause = og::ui::keymap_pause_menu_screen_spec();
    EXPECT_EQ(og::ui::MenuScreenKind::Overlay, pause.kind);
    EXPECT_EQ(og::ui::RemoteStartScope::None, pause.remote_start);
    EXPECT_NE(nullptr, pause.frame_tick);
    // One BACK row, bottom left, Escape.
    ASSERT_EQ(1, host.spec->row_count);
    EXPECT_STREQ("keymap_back", host.spec->rows[0].id);
    EXPECT_EQ(KEYSTATE_ESCAPE, host.spec->rows[0].hotkey);
    EXPECT_EQ(og::ui::kKeymapBackY, host.spec->rows[0].y);
}

TEST(KeymapScreen, title_and_legend_words)
{
    using og::ui::KeymapSeatView;
    const std::vector<KeymapSeatView> seats = {
        {1, "WASD", false}, {2, "Arrows", false}, {4, "JOY1", true}};
    EXPECT_EQ("KEYS: P1 WASD  P2 ARROWS  P4 JOY1", og::ui::keymap_title(seats));
    const std::vector<KeymapSeatView> many(9, KeymapSeatView{3, "IJKL", false});
    EXPECT_EQ(og::ui::kKeymapMaxGlyphs, og::ui::keymap_title(many).size());

    kp::Input input = kp::factory_input(true);
    input.seats[1].keys[KEY_LOOKUP] = KEYCODE_DELETE;
    input.seats[0].keys[KEY_YELL] = KEYCODE_BACKSPACE;  // the web PAUSE key
    input.seats[2].keys[KEY_CHEAT] = KEYCODE_KP_1;
    const kp::Output out = kp::render(input);
    const std::vector<std::string> lines = og::ui::keymap_legend_lines(
        seats, out, true, [](int) { return std::string("Keypad 1"); });
    const std::vector<std::string> want = {
        "P4: GAMEPAD (JOY1), NOT ON THE PICTURE", "PAUSE + P1 YELL: BKSP",
        "P3 CHEAT: KEYPAD 1", og::ui::kKeymapMacDeleteLine};
    EXPECT_EQ(want, lines);
    // Desktop: Delete is just a key.
    const std::vector<std::string> desktop =
        og::ui::keymap_legend_lines({}, out, false, nullptr);
    EXPECT_EQ(std::find(desktop.begin(), desktop.end(),
                        og::ui::kKeymapMacDeleteLine),
              desktop.end());
    // More than fit: the last line says how many more.
    kp::Input crowded = kp::factory_input(false);
    const std::array<std::pair<int, int>, 5> numpad = {{
        {KEY_FIRE, KEYCODE_KP_2}, {KEY_YELL, KEYCODE_KP_3},
        {KEY_LOOKUP, KEYCODE_KP_4}, {KEY_CHEAT, KEYCODE_KP_6},
        {KEY_SPEC2, KEYCODE_KP_7}}};
    for (const auto& [slot, key] : numpad)
        crowded.seats[0].keys[static_cast<std::size_t>(slot)] = key;
    const std::vector<std::string> cut =
        og::ui::keymap_legend_lines({}, kp::render(crowded), false, nullptr);
    ASSERT_EQ(og::ui::kKeymapLegendLines, cut.size());
    EXPECT_EQ("AND 2 MORE ON THE KEY LIST", cut.back());
    for (const std::string& line : cut)
        EXPECT_LE(line.size(), og::ui::kKeymapMaxGlyphs) << line;
}

namespace og::ui {
// The production gate pass (menu_screen_runner.cpp, TESTING).
void picker_testing_apply_row_states(const MenuScreenSpec& spec,
                                     button* buttons, int num_buttons);
} // namespace og::ui

// ---------------------------------------------------------------------------
// The two doors (#388): KEYMAP on the pause player screen and on the Base
// Camp seat screen, each opening the picture of every local seat with the
// opening seat first; hidden for a phone's touch seat.

namespace
{

bool pause_pump_alive()
{
    return true;
}

// A phone (single-seat device) for the guard's lifetime.
struct SingleSeatDevice
{
    bool saved = input_hardware_state().single_seat_device;
    explicit SingleSeatDevice(bool on)
    {
        input_hardware_state().single_seat_device = on;
    }
    ~SingleSeatDevice() { input_hardware_state().single_seat_device = saved; }
    SingleSeatDevice(const SingleSeatDevice&) = delete;
    SingleSeatDevice& operator=(const SingleSeatDevice&) = delete;
};

// Runs a door's row handler (which blocks in KEYMAP) with the read-and-close
// injector.
Sint32 press_door(Seen& seen, const og::ui::MenuScreenSpec& spec, int row,
                  void* state)
{
    trace_clear();
    SDL_Thread* thread = SDL_CreateThread(read_and_close, "keymap_door", &seen);
    EXPECT_NE(nullptr, thread);
    const Sint32 result = spec.on_spec_row(row, state);
    seen.finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    clear_allbuttons();
    EXPECT_EQ(0, thread_result) << "the injector gave up at leg "
                                << thread_result;
    return result;
}

bool row_hidden(const og::ui::MenuScreenSpec& spec, int row)
{
    button* const buttons = spec.buttons_accessor();
    const int count = spec.count_accessor();
    og::ui::picker_testing_apply_row_states(spec, buttons, count);
    return buttons[row].hidden;
}

} // namespace

// RED (planted): the pause handler hands KEYMAP the opening seat alone (the
// title loses P1).
TEST(KeymapScreen, pause_player_door_shows_every_local_seat_this_one_first)
{
    ASSERT_TRUE(E_Screen);
    ControlsRestore restore;
    screen& game_screen = *og::runtime::current_session->myscreen_;
    const unsigned char saved_numplayers = game_screen.save_data.numplayers;
    game_screen.save_data.numplayers = 2;

    og::ui::PauseMenuHost host;
    host.pump_paused = &pause_pump_alive;
    og::ui::PausePlayerScreenState state;
    state.host = &host;
    state.seat = 1;
    state.player_number = 2;
    og::ui::install_pause_player_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::pause_player_menu_screen_spec();

    {
        SingleSeatDevice desktop(false);
        EXPECT_FALSE(row_hidden(spec, og::ui::kPausePlayerKeymapIndex));
        Seen seen;
        EXPECT_EQ(MENU_OK, press_door(seen, spec,
                                      og::ui::kPausePlayerKeymapIndex, &state));
        ASSERT_TRUE(seen.up);
        EXPECT_TRUE(trace_contains("pause_menu", "keymap seat=1"));
        EXPECT_EQ(0u, seen.title.rfind("KEYS: P2 ", 0)) << seen.title;
        EXPECT_NE(std::string::npos, seen.title.find("  P1 ")) << seen.title;
        EXPECT_FALSE(state.session_ended);
    }
    {
        // A phone's touch seat has no keys to picture: no door.
        SingleSeatDevice phone(true);
        state.seat = 0;
        state.player_number = 1;
        EXPECT_TRUE(row_hidden(spec, og::ui::kPausePlayerKeymapIndex));
        trace_clear();
        EXPECT_EQ(MENU_OK,
                  spec.on_spec_row(og::ui::kPausePlayerKeymapIndex, &state));
        EXPECT_FALSE(trace_contains("keymap", "KEYMAP: open"));
    }
    og::ui::install_pause_player_state_for_screen(nullptr);
    game_screen.save_data.numplayers = saved_numplayers;
}

// RED (planted): the seat handler hands KEYMAP the opening seat alone.
TEST(KeymapScreen, seat_screen_door_shows_every_local_seat_this_one_first)
{
    ASSERT_TRUE(E_Screen);
    ControlsRestore restore;
    FakeLobbyClient lobby;
    for (std::uint8_t index = 0; index < 2; ++index)
    {
        og::sim::LobbyPlayer player;
        player.player_index = index;
        player.seat_id = static_cast<og::sim::LobbySeatId>(1000u + index);
        player.name = "Player " + std::to_string(index + 1);
        lobby.players.push_back(player);
    }
    lobby.local_indices = {0, 1};
    lobby.local_seats = 2;
    og::ui::install_active_picker_lobby_client(&lobby);
    og::ui::SeatSettingsScreenState state{
        .seat_id = lobby.players[1].seat_id,
        .player_index = lobby.players[1].player_index,
        .local_slot = 1,
    };
    og::ui::install_seat_settings_state_for_screen(&state);
    const og::ui::MenuScreenSpec& spec =
        og::ui::seat_settings_menu_screen_spec();

    {
        SingleSeatDevice desktop(false);
        EXPECT_FALSE(row_hidden(spec, kSeatSettingsKeymapRow));
        Seen seen;
        EXPECT_EQ(MENU_OK,
                  press_door(seen, spec, kSeatSettingsKeymapRow, &state))
            << "BACK returns to the seat screen";
        ASSERT_TRUE(seen.up);
        EXPECT_TRUE(trace_contains("basecamp", "seat_keymap slot=2"));
        EXPECT_EQ(0u, seen.title.rfind("KEYS: P2 ", 0)) << seen.title;
        EXPECT_NE(std::string::npos, seen.title.find("  P1 ")) << seen.title;
    }
    {
        SingleSeatDevice phone(true);
        state.local_slot = 0;
        state.seat_id = lobby.players[0].seat_id;
        state.player_index = lobby.players[0].player_index;
        EXPECT_TRUE(row_hidden(spec, kSeatSettingsKeymapRow));
    }
    og::ui::install_seat_settings_state_for_screen(nullptr);
    og::ui::install_active_picker_lobby_client(nullptr);
}

// PR proof frame: all four seats of a desktop on KEYMAP at 1280x800, player
// 1 on a custom key (LOOK UP on Caps Lock) and player 4 on a gamepad, so the
// legend's gamepad line shows. A capture tool, not a check: skipped unless
// OG_KEYMAP_SHOT_DIR names where it goes. The pad is an SDL virtual device
// (the same minimal shape as test_pause_menu.cpp's menushot pad).
TEST(KeymapScreen, zz_capture_four_seats_one_on_a_pad)
{
    if (shot_dir() == nullptr)
        GTEST_SKIP() << "set OG_KEYMAP_SHOT_DIR to write the PR proof frame";
    ASSERT_TRUE(E_Screen);
    ControlsRestore restore;
    og::input_native::joystick_init_subsystem();
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 2;
    desc.nbuttons = 6;
    desc.name = "OpenGlad keymap shot pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    ASSERT_NE(0u, id) << SDL_GetError();
    SDL_Joystick* const pad = SDL_OpenJoystick(id);
    ASSERT_NE(nullptr, pad) << SDL_GetError();
    int device = -1;
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
        for (int i = 0; i < count; ++i)
            if (ids[i] == id)
                device = i;
        SDL_free(ids);
    }
    ASSERT_GE(device, 0);
    ASSERT_LT(device, 10);
    const og::input_native::JoystickHandle old_handle = joysticks[device];
    joysticks[device] = pad;

    input_player_keys()[0][KEY_LOOKUP] = KEYCODE_CAPSLOCK;
    ASSERT_TRUE(assign_joystick_to_player(3, device));
    og::ui::KeymapScreenHost host;
    host.seats = {{0, 1}, {1, 2}, {2, 3}, {3, 4}};
    {
        WindowSize window;
        WindowSize::set(1280, 800);
        Seen seen;
        seen.shot_name = "keymap_four_seats_1280x800";
        EXPECT_EQ(MENU_REDRAW, run_with_injector(seen, 0, host, nullptr));
        EXPECT_EQ(0u, seen.title.rfind("KEYS: P1 ", 0)) << seen.title;
        EXPECT_EQ(1, player_on(seen, KEYCODE_CAPSLOCK));
        ASSERT_FALSE(seen.legend.empty());
        EXPECT_EQ(0u, seen.legend.front().rfind("P4: GAMEPAD", 0))
            << seen.legend.front();
        EXPECT_TRUE(seen.physical.captured);
    }

    clear_player_joystick(3);
    joysticks[device] = old_handle;
    SDL_CloseJoystick(pad);
    SDL_DetachVirtualJoystick(id);
    og::input_native::joystick_quit_subsystem();
}
