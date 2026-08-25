/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
//
// input.cpp
//
// input code
//

#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/device_seats.h>
#include <openglad/interface/input_hardware_state.h>
#include <openglad/interface/screen.h> // active canvas dims for pointer mapping
#include <openglad/gameplay/input_state.h> // MAX_PLAYERS: the local-seat hard cap
#include <openglad/core/util.h>
#include <openglad/core/test_trace.h>
#include <algorithm> //buffers: for std::min when clamping joystick counts
#include <cstdio>
#include <ctime>
#include <cstring> //buffers: for strlen
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define YIELD_SLEEP(ms) emscripten_sleep(ms)
#else
#define YIELD_SLEEP(ms) og::input_native::sleep_ms(ms)
#endif

void quit(Sint32 arg1);

// Input scalar globals (raw_key, player_keys, keystates, viewport_*, etc.)
// are now members of GameSession, accessed via macros defined in input.h.

int (&input_player_keys())[4][NUM_KEYS]
{
    return og::runtime::current_session->player_keys_;
}

int& input_raw_key_ref()
{
    return og::runtime::current_session->raw_key_;
}

std::string& input_raw_text_input_ref()
{
    return og::runtime::current_session->raw_text_input_;
}

bool& input_continue_ref()
{
    return og::runtime::current_session->input_continue_;
}

short& input_scroll_amount_ref()
{
    return og::runtime::current_session->scroll_amount_;
}

short& input_key_press_event_ref()
{
    return og::runtime::current_session->key_press_event_;
}

unsigned query_key_press_serial()
{
    return og::runtime::current_session->key_press_serial_;
}

short& input_text_input_event_ref()
{
    return og::runtime::current_session->text_input_event_;
}

// Input hardware state now lives in GameSession::input_hw_ (InputHardwareState).
// Access via hw() helper for fields without macros; mouse_state and player_joy
// are already macros defined in input.h.
static inline auto& hw() { return input_hardware_state(); }

// Menus translate to the fixed 320x200 UI canvas; gameplay and the editor map
// to the graphics/zoom-derived world canvas. Presentation aspect-fits either
// canvas inside the overscan viewport, and pointer conversion must use that
// same fitted rectangle. Before the screen exists events use classic UI dims.
static inline float active_canvas_w()
{
    const ::screen* s = og::runtime::current_session->myscreen_;
    return s ? static_cast<float>(s->canvas_w()) : static_cast<float>(kUiCanvasW);
}

static inline float active_canvas_h()
{
    const ::screen* s = og::runtime::current_session->myscreen_;
    return s ? static_cast<float>(s->canvas_h()) : static_cast<float>(kUiCanvasH);
}

static inline float gameplay_ui_canvas_w()
{
    const ::screen* s = og::runtime::current_session->myscreen_;
    return s ? static_cast<float>(
                   s->active_canvas() == CanvasTarget::UI
                       ? s->canvas_w()
                       : (s->gameplay_ui_canvas_available()
                              ? s->gameplay_ui_canvas_w()
                              : s->world_canvas_w()))
             : static_cast<float>(kUiCanvasW);
}

static inline float gameplay_ui_canvas_h()
{
    const ::screen* s = og::runtime::current_session->myscreen_;
    return s ? static_cast<float>(
                   s->active_canvas() == CanvasTarget::UI
                       ? s->canvas_h()
                       : (s->gameplay_ui_canvas_available()
                              ? s->gameplay_ui_canvas_h()
                              : s->world_canvas_h()))
             : static_cast<float>(kUiCanvasH);
}

static og::CanvasViewport canvas_viewport(float canvas_w, float canvas_h)
{
    const auto* session = og::runtime::current_session;
    if (session == nullptr)
        return {0, 0, kUiCanvasW, kUiCanvasH};
    return og::fit_canvas_in_viewport(
        static_cast<int>(canvas_w), static_cast<int>(canvas_h),
        static_cast<int>(session->viewport_offset_x_),
        static_cast<int>(session->viewport_offset_y_),
        static_cast<int>(session->viewport_w_),
        static_cast<int>(session->viewport_h_));
}

og::CanvasViewport active_canvas_viewport()
{
    return canvas_viewport(active_canvas_w(), active_canvas_h());
}

og::CanvasViewport gameplay_ui_canvas_viewport()
{
    const auto* session = og::runtime::current_session;
    const ::screen* s = session != nullptr ? session->myscreen_ : nullptr;
    if (s == nullptr || s->active_canvas() == CanvasTarget::UI)
        return active_canvas_viewport();

    // Match Screen::swap's independently aspect-fitted HUD destination.
    // Fractional World dimensions round to scaler-safe integers and can have
    // a slightly different aspect; inheriting their fitted rectangle would
    // distort the fixed overlay and offset its touch hit targets.
    return canvas_viewport(gameplay_ui_canvas_w(), gameplay_ui_canvas_h());
}

bool window_point_in_active_canvas(float x, float y)
{
    const og::CanvasViewport viewport = active_canvas_viewport();
    return x >= static_cast<float>(viewport.x) &&
           y >= static_cast<float>(viewport.y) &&
           x < static_cast<float>(viewport.x + viewport.w) &&
           y < static_cast<float>(viewport.y + viewport.h);
}

std::pair<float, float> window_to_active_canvas(float x, float y)
{
    const og::CanvasViewport viewport = active_canvas_viewport();
    const float w = static_cast<float>(std::max(1, viewport.w));
    const float h = static_cast<float>(std::max(1, viewport.h));
    return {(x - static_cast<float>(viewport.x)) * active_canvas_w() / w,
            (y - static_cast<float>(viewport.y)) * active_canvas_h() / h};
}

bool window_point_in_gameplay_ui_canvas(float x, float y)
{
    const og::CanvasViewport viewport = gameplay_ui_canvas_viewport();
    return x >= static_cast<float>(viewport.x) &&
           y >= static_cast<float>(viewport.y) &&
           x < static_cast<float>(viewport.x + viewport.w) &&
           y < static_cast<float>(viewport.y + viewport.h);
}

std::pair<float, float> window_to_gameplay_ui_canvas(float x, float y)
{
    const og::CanvasViewport viewport = gameplay_ui_canvas_viewport();
    const float w = static_cast<float>(std::max(1, viewport.w));
    const float h = static_cast<float>(std::max(1, viewport.h));
    return {(x - static_cast<float>(viewport.x)) * gameplay_ui_canvas_w() / w,
            (y - static_cast<float>(viewport.y)) * gameplay_ui_canvas_h() / h};
}

std::pair<float, float> active_canvas_to_window(float x, float y)
{
    const og::CanvasViewport viewport = active_canvas_viewport();
    return {static_cast<float>(viewport.x) +
                x * static_cast<float>(viewport.w) / active_canvas_w(),
            static_cast<float>(viewport.y) +
                y * static_cast<float>(viewport.h) / active_canvas_h()};
}

std::pair<float, float> ui_canvas_to_window(float x, float y)
{
    // Forward map pinned to the FIXED UI canvas (kUiCanvasW x kUiCanvasH),
    // independent of which canvas is active right now. The main thread flips
    // the active canvas World<->UI inside every frame's draw pass, so a
    // cross-thread caller (the test click injectors) sampling
    // active_canvas_to_window races that flip: whenever the window aspect
    // differs from 16:10 the World(320x240)/UI(320x200) transforms diverge
    // and a menu click computed with the World state inverse-maps off the UI
    // canvas (the seed-23 og_test_menu_ui wedge: game y=187 -> win 718 ->
    // game y=204, a dead click). Menu buttons live on the UI canvas by
    // definition; map them with its transform only.
    const og::CanvasViewport viewport = canvas_viewport(
        static_cast<float>(kUiCanvasW), static_cast<float>(kUiCanvasH));
    return {static_cast<float>(viewport.x) +
                x * static_cast<float>(viewport.w) /
                    static_cast<float>(kUiCanvasW),
            static_cast<float>(viewport.y) +
                y * static_cast<float>(viewport.h) /
                    static_cast<float>(kUiCanvasH)};
}

namespace
{
int scale_touch_coordinate(int value, int canvas_extent, int reference_extent)
{
    return static_cast<int>(static_cast<long long>(value) * canvas_extent /
                            reference_extent);
}

TouchControlRect scale_touch_rect(int x, int y, int w, int h,
                                  int canvas_w, int canvas_h)
{
    const int left = scale_touch_coordinate(
        x, canvas_w, TOUCH_REFERENCE_CANVAS_W);
    const int top = scale_touch_coordinate(
        y, canvas_h, TOUCH_REFERENCE_CANVAS_H);
    const int right = scale_touch_coordinate(
        x + w, canvas_w, TOUCH_REFERENCE_CANVAS_W);
    const int bottom = scale_touch_coordinate(
        y + h, canvas_h, TOUCH_REFERENCE_CANVAS_H);
    return {left, top, std::max(1, right - left), std::max(1, bottom - top)};
}
} // namespace

TouchControlLayout touch_control_layout(int canvas_w, int canvas_h)
{
    TouchControlLayout layout;
    layout.canvas_w = std::max(TOUCH_REFERENCE_CANVAS_W, canvas_w);
    layout.canvas_h = std::max(TOUCH_REFERENCE_CANVAS_H, canvas_h);

    const auto rect = [&](int x, int y, int w, int h) {
        return scale_touch_rect(x, y, w, h, layout.canvas_w, layout.canvas_h);
    };
    const auto sx = [&](int value) {
        return scale_touch_coordinate(
            value, layout.canvas_w, TOUCH_REFERENCE_CANVAS_W);
    };
    const auto sy = [&](int value) {
        return scale_touch_coordinate(
            value, layout.canvas_h, TOUCH_REFERENCE_CANVAS_H);
    };

    layout.fire = rect(FIRE_BUTTON_X, FIRE_BUTTON_Y, BUTTON_DIM, BUTTON_DIM);
    layout.special = rect(
        SPECIAL_BUTTON_X, SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM);
    layout.yell = rect(YO_BUTTON_X - BUTTON_DIM / 2,
                       YO_BUTTON_Y - BUTTON_DIM / 2,
                       BUTTON_DIM, BUTTON_DIM);
    layout.switch_character = rect(
        SWITCH_CHARACTER_BUTTON_X, SWITCH_CHARACTER_BUTTON_Y,
        BUTTON_DIM * 2, BUTTON_DIM * 2);
    layout.next_special = rect(
        NEXT_SPECIAL_BUTTON_X, NEXT_SPECIAL_BUTTON_Y, BUTTON_DIM, BUTTON_DIM);
    layout.alternate_special = rect(
        ALTERNATE_SPECIAL_BUTTON_X, ALTERNATE_SPECIAL_BUTTON_Y,
        BUTTON_DIM, BUTTON_DIM);

    layout.movement_region_right = sx(
        TOUCH_REFERENCE_CANVAS_W / 2 - BUTTON_DIM / 2);
    layout.movement_region_top = sy(BUTTON_DIM * 2);
    layout.movement_center_min_x = sx(MOVE_AREA_DIM / 2 + 1);
    layout.movement_center_min_y = sy(MOVE_AREA_DIM / 2 + 1);
    layout.movement_center_max_y = sy(
        TOUCH_REFERENCE_CANVAS_H - (MOVE_AREA_DIM / 2 + 1));
    layout.movement_dead_zone_x = std::max(1, sx(MOVE_DEAD_ZONE));
    layout.movement_dead_zone_y = std::max(1, sy(MOVE_DEAD_ZONE));
    layout.tap_slop_x = std::max(1, sx(2));
    layout.tap_slop_y = std::max(1, sy(2));

    layout.movement_area_offset_x = sx(MOVE_AREA_DIM / 2);
    layout.movement_area_offset_y = sy(MOVE_AREA_DIM / 2);
    layout.movement_area_w = std::max(1, sx(MOVE_AREA_DIM));
    layout.movement_area_h = std::max(1, sy(MOVE_AREA_DIM));
    layout.movement_center_offset_x = sx(4);
    layout.movement_center_offset_y = sy(4);
    layout.movement_center_w = std::max(1, sx(8));
    layout.movement_center_h = std::max(1, sy(8));
    layout.movement_target_offset_x = sx(2);
    layout.movement_target_offset_y = sy(2);
    layout.movement_target_w = std::max(1, sx(4));
    layout.movement_target_h = std::max(1, sy(4));
    return layout;
}

void update_overscan_setting()
{
    if(og::runtime::current_session->overscan_percentage_ < 0.0f)
        og::runtime::current_session->overscan_percentage_ = 0.0f;
    else if(og::runtime::current_session->overscan_percentage_ > 0.25f)
        og::runtime::current_session->overscan_percentage_ = 0.25f;
    
    og::runtime::current_session->viewport_offset_x_ = og::runtime::current_session->window_w_ * og::runtime::current_session->overscan_percentage_/2;
    og::runtime::current_session->viewport_offset_y_ = og::runtime::current_session->window_h_ * og::runtime::current_session->overscan_percentage_/2;
    og::runtime::current_session->viewport_w_ = og::runtime::current_session->window_w_ * (1.0f - og::runtime::current_session->overscan_percentage_);
    og::runtime::current_session->viewport_h_ = og::runtime::current_session->window_h_ * (1.0f - og::runtime::current_session->overscan_percentage_);
}

inline constexpr int JOY_DEAD_ZONE = 8000;
inline constexpr int MAX_NUM_JOYSTICKS = 10;  // Just in case there are joysticks attached that are not useable (e.g. accelerometer)
og::input_native::JoystickHandle joysticks[MAX_NUM_JOYSTICKS];

namespace
{
bool as_event_data(const void* native_event, og::input_native::EventData& out)
{
    return og::input_native::decode_event(native_event, out);
}

// (Re)open every connected device into the process-global handle table.
// Deliberately never closes the previous handles: SDL joystick handles are
// refcounted and this table has no shutdown phase (see the joystick tests'
// notes), matching the historical behavior.
void reopen_joystick_device_table()
{
    for (int i = 0; i < MAX_NUM_JOYSTICKS; i++)
        joysticks[i] = nullptr;
    const int numjoy =
        std::min(og::input_native::num_joysticks(), MAX_NUM_JOYSTICKS);
    for (int i = 0; i < numjoy; i++)
    {
        joysticks[i] = og::input_native::joystick_open(i);
        // Enumerated but unopenable (a udev rule denying the device node is
        // the field report). The slot stays null and every consumer treats it
        // as "no usable device at this index".
        if (joysticks[i] == nullptr)
            TRACE("joystick", "open_failed device=%d", i);
    }
}

bool joydata_has_any_binding(const JoyData& joy)
{
    for (int k = 0; k < NUM_KEYS; ++k)
        if (joy.key_type[k] != JoyData::NONE)
            return true;
    return false;
}

bool device_held_by_other_seat(int device_index, int player)
{
    for (int q = 0; q < 4; ++q)
        if (q != player && player_joy[q].index == device_index)
            return true;
    return false;
}

// The web shell classifies the device during page boot, which can land
// before openglad_web_boot has created the session that owns the hardware
// block. The latch bridges that boot order only: init_input copies it into
// the session, and every read goes to the hardware block, so the flag still
// has exactly one live home. That home is per-session: a scope that
// activates a different session (e.g. the shadow's server session, which
// never runs init_input) reads that session's default false — every current
// door runs under the display session, and a future door must too.
bool g_pending_single_seat_device = false;
} // namespace


//
// Input routines (for handling all events and then setting the appropriate vars)
//

void init_input()
{
    // SDL_CreateWindow only auto-inits VIDEO; in SDL3 nothing else brings the
    // joystick subsystem up, so without this no hardware ever enumerates.
    if (!og::input_native::joystick_subsystem_initialized())
        og::input_native::joystick_init_subsystem();

    reset_default_player_controls();
    input_hardware_state().single_seat_device = g_pending_single_seat_device;
    // On web this installs the Backspace-as-Escape remap at the SDL event
    // source before the first event is queued; a no-op on native builds.
    og::input_native::install_back_key_remap();
    og::runtime::current_session->keystates_ = og::input_native::keyboard_state();

    // Open every connected device. No seat gets a joystick here — assignment
    // is explicit (assign_joystick_to_player) or a persisted-GUID re-attach.
    reopen_joystick_device_table();
    og::input_native::joystick_set_event_state(true);

    // The SDL-free controls loader stores GUID strings; resolving them
    // against connected hardware happens here on the SDL side.
    set_joystick_guid_reattach_hook(&reattach_saved_joysticks);
    reattach_saved_joysticks();
}

int joystick_device_count()
{
    return std::min(og::input_native::num_joysticks(), MAX_NUM_JOYSTICKS);
}

namespace og::input
{

bool is_single_seat_device()
{
    return input_hardware_state().single_seat_device;
}

void set_single_seat_device(bool single_seat)
{
    g_pending_single_seat_device = single_seat;
    if (og::runtime::current_session != nullptr)
        input_hardware_state().single_seat_device = single_seat;
}

int local_seat_cap()
{
    // Menus poll this per frame; only a single-seat device needs the
    // joystick enumeration (an SDL_GetJoysticks alloc), so desktops skip it.
    if (!is_single_seat_device())
        return MAX_PLAYERS;
    return max_local_seats(true, joystick_device_count(), MAX_PLAYERS);
}

} // namespace og::input

int player_joystick_device(int player)
{
    if (player < 0 || player >= 4)
        return -1;
    return (player_joy[player].index >= 0) ? player_joy[player].index : -1;
}

bool assign_joystick_to_player(int player, int device_index)
{
    if (player < 0 || player >= 4)
        return false;
    if (device_index < 0 || device_index >= MAX_NUM_JOYSTICKS ||
        joysticks[device_index] == nullptr)
    {
        TRACE("joystick", "assign_refused player=%d device=%d reason=unopened",
              player, device_index);
        return false;
    }
    // Synthesize BEFORE touching any seat. A device can open and still expose
    // nothing bindable (every capability reads 0 on a half-permissioned node;
    // every axis resting past the dead zone on a hostile pad), and a failed
    // assignment must not have already cost another seat its device.
    JoyData layout(device_index);
    if (!joydata_has_any_binding(layout))
    {
        TRACE("joystick",
              "assign_refused player=%d device=%d reason=no_bindings",
              player, device_index);
        return false;
    }
    // One device drives one seat: an explicit assignment steals it, voiding
    // the robbed seat's persisted claim.
    for (int q = 0; q < 4; ++q)
    {
        if (q != player && player_joy[q].index == device_index)
        {
            player_joy[q].index = -1;
            hw().player_joystick_guids[q].clear();
            TRACE("joystick", "assign_steal from_player=%d device=%d",
                  q, device_index);
        }
    }
    player_joy[player] = layout; // default layout synthesis
    hw().player_joystick_guids[player] =
        og::input_native::joystick_device_guid(device_index);
    TRACE("joystick", "assigned player=%d device=%d", player, device_index);
    return true;
}

void clear_player_joystick(int player)
{
    if (player < 0 || player >= 4)
        return;
    player_joy[player] = JoyData(); // full unbind: device, layout, and claim
    hw().player_joystick_guids[player].clear();
}

void reattach_saved_joysticks()
{
    const int device_count = joystick_device_count();
    for (int p = 0; p < 4; ++p)
    {
        if (player_joy[p].index >= 0 || hw().player_joystick_guids[p].empty())
            continue;
        for (int d = 0; d < device_count; ++d)
        {
            if (joysticks[d] == nullptr ||
                og::input_native::joystick_device_guid(d) !=
                    hw().player_joystick_guids[p] ||
                device_held_by_other_seat(d, p))
                continue;
            // A replug keeps the seat's session layout; a fresh attach (GUID
            // loaded from cfg into a blank JoyData) synthesizes the default.
            if (joydata_has_any_binding(player_joy[p]))
            {
                player_joy[p].index = d;
            }
            else
            {
                JoyData layout(d);
                if (!joydata_has_any_binding(layout))
                {
                    // Opened, but nothing bindable: leave the seat on its
                    // keyboard mapping and keep scanning (an identical twin
                    // unit further down the list may still be usable).
                    TRACE("joystick",
                          "reattach_refused player=%d device=%d", p, d);
                    continue;
                }
                player_joy[p] = layout;
            }
            TRACE("joystick", "guid_reattach player=%d device=%d", p, d);
            break;
        }
    }
}

void get_input_events(bool type)
{
    //key_press_event = 0;

    if (type == POLL)
    {
        while (const void* event = og::input_native::poll_event())
            handle_events(event);
    }
    if (type == WAIT)
    {
        if (const void* event = og::input_native::wait_event())
            handle_events(event);
    }
}

#ifdef USE_TOUCH_INPUT

#endif

void sendFakeKeyDownEvent(int keycode)
{
    og::input_native::push_key_event(true, keycode);
}

void sendFakeKeyUpEvent(int keycode)
{
    og::input_native::push_key_event(false, keycode);
}

// handle_window_event and handle_key_event are implemented in
// runtime/input_event_bridge.cpp to avoid runtime/render deps in input module.

void handle_text_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    og::runtime::current_session->raw_text_input_ = event.text.data();
    og::runtime::current_session->text_input_event_ = 1;
}

// Instantaneous press/release pairs (touch taps forwarded as mouse clicks
// can land down+up inside ONE event pump) are invisible to per-frame
// up->down edge detection: the sampled state never shows the press. Worse,
// query_mouse() can run more than once per menu frame (grab_mouse +
// leftmouse), so a "keep the press visible for one poll" latch still loses
// the press before the click detector samples it. Instead, a collapsed
// press/release pair is recorded as an explicit pending click that the
// click detectors consume via take_pending_left/right_click().
static bool g_mouse_left_unqueried_press = false;
static bool g_mouse_right_unqueried_press = false;
static int g_pending_left_clicks = 0;
static int g_pending_right_clicks = 0;

void handle_mouse_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    switch(event.type)
    {
    case og::input_native::EventType::MouseWheel:
        og::runtime::current_session->scroll_amount_ = static_cast<short>(5*event.wheel_y);
        og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;
        break;

#ifndef USE_TOUCH_INPUT
        // Mouse event
    case og::input_native::EventType::MouseMotion:
        {
            const auto [x, y] = window_to_active_canvas(
                static_cast<float>(event.motion_x),
                static_cast<float>(event.motion_y));
            mouse_state.x = static_cast<float>(static_cast<int>(x));
            mouse_state.y = static_cast<float>(static_cast<int>(y));
        }
        break;
    case og::input_native::EventType::MouseButtonUp:
        if (event.button == og::input_native::kMouseButtonLeft)
        {
            // A release before any query observed the press = a collapsed
            // tap; record it as a pending click for the edge detectors.
            if (g_mouse_left_unqueried_press)
                ++g_pending_left_clicks;
            g_mouse_left_unqueried_press = false;
            mouse_state.left = 0;
        }
        if (event.button == og::input_native::kMouseButtonRight)
        {
            if (g_mouse_right_unqueried_press)
                ++g_pending_right_clicks;
            g_mouse_right_unqueried_press = false;
            mouse_state.right = 0;
        }

        {
            const auto [x, y] = window_to_active_canvas(
                static_cast<float>(event.button_x),
                static_cast<float>(event.button_y));
            mouse_state.x = static_cast<float>(static_cast<int>(x));
            mouse_state.y = static_cast<float>(static_cast<int>(y));
        }
        break;
    case og::input_native::EventType::MouseButtonDown:
        if (!window_point_in_active_canvas(
                static_cast<float>(event.button_x),
                static_cast<float>(event.button_y)))
        {
            break;
        }
        if (event.button == og::input_native::kMouseButtonLeft)
        {
            mouse_state.left = 1;
            g_mouse_left_unqueried_press = true;
            // "Press ESC to continue" waits (scenario info, intro pages)
            // also accept a click/tap; clear_keyboard() at dialog entry
            // resets the flag, so only clicks during the wait dismiss it.
            og::runtime::current_session->input_continue_ = true;
        }
        else if (event.button == og::input_native::kMouseButtonRight)
        {
            mouse_state.right = 1;
            g_mouse_right_unqueried_press = true;
        }

        {
            const auto [x, y] = window_to_active_canvas(
                static_cast<float>(event.button_x),
                static_cast<float>(event.button_y));
            mouse_state.x = static_cast<float>(static_cast<int>(x));
            mouse_state.y = static_cast<float>(static_cast<int>(y));
        }
        break;
#else
#ifdef FAKE_TOUCH_EVENTS
    // Convert SDL mouse events to fake SDL touch events
    case og::input_native::EventType::MouseMotion:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerMotion,
                event.motion_x / og::runtime::current_session->window_w_,
                event.motion_y / og::runtime::current_session->window_h_,
                event.motion_dx / og::runtime::current_session->window_w_,
                event.motion_dy / og::runtime::current_session->window_h_,
                1);
        }
        break;
    case og::input_native::EventType::MouseButtonUp:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerUp,
                event.button_x / og::runtime::current_session->window_w_,
                event.button_y / og::runtime::current_session->window_h_,
                0.0f, 0.0f,
                1);
        }
        break;
    case og::input_native::EventType::MouseButtonDown:
        {
            og::input_native::push_touch_event(
                og::input_native::EventType::FingerDown,
                event.button_x / og::runtime::current_session->window_w_,
                event.button_y / og::runtime::current_session->window_h_,
                0.0f, 0.0f,
                1);
        }
        break;
#endif
        // Mouse event
    case og::input_native::EventType::FingerMotion:
        {
            const auto [mapped_x, mapped_y] = window_to_gameplay_ui_canvas(
                event.finger_x * og::runtime::current_session->window_w_,
                event.finger_y * og::runtime::current_session->window_h_);
            int x = static_cast<int>(mapped_x);
            int y = static_cast<int>(mapped_y);
            const TouchControlLayout layout = touch_control_layout(
                static_cast<int>(gameplay_ui_canvas_w()),
                static_cast<int>(gameplay_ui_canvas_h()));
        
        og::runtime::current_session->scroll_amount_ = y - mouse_state.y;
        
        mouse_state.x = x;
        mouse_state.y = y;
        
        if(hw().moving && event.finger_id == hw().movingTouch)
        {
            hw().moving_touch_target_x = x;
            hw().moving_touch_target_y = y;
            
            hw().touch_keystate[0][KEY_UP] = false;
            hw().touch_keystate[0][KEY_UP_RIGHT] = false;
            hw().touch_keystate[0][KEY_RIGHT] = false;
            hw().touch_keystate[0][KEY_DOWN_RIGHT] = false;
            hw().touch_keystate[0][KEY_DOWN] = false;
            hw().touch_keystate[0][KEY_DOWN_LEFT] = false;
            hw().touch_keystate[0][KEY_LEFT] = false;
            hw().touch_keystate[0][KEY_UP_LEFT] = false;
            
            const int movement_dx = x - hw().moving_touch_x;
            const int movement_dy = y - hw().moving_touch_y;
            if(abs(movement_dx) > layout.movement_dead_zone_x ||
               abs(movement_dy) > layout.movement_dead_zone_y)
            {
                float offset = -M_PI + M_PI/8;
                float interval = M_PI/4;
                // Canvas dimensions round slightly at some zoom steps. Convert
                // the delta back into reference-layout units before selecting
                // a direction so that rounding cannot skew the radial sectors.
                const float reference_dx = static_cast<float>(movement_dx) *
                    static_cast<float>(TOUCH_REFERENCE_CANVAS_W) /
                    static_cast<float>(layout.canvas_w);
                const float reference_dy = static_cast<float>(movement_dy) *
                    static_cast<float>(TOUCH_REFERENCE_CANVAS_H) /
                    static_cast<float>(layout.canvas_h);
                float angle = atan2(reference_dy, reference_dx);
                if(angle < -M_PI + M_PI/8 || angle >= M_PI - M_PI/8)
                    hw().touch_keystate[0][KEY_LEFT] = true;
                else if(angle >= offset && angle < offset + interval)
                    hw().touch_keystate[0][KEY_UP_LEFT] = true;
                else if(angle >= offset + interval && angle < offset + 2*interval)
                    hw().touch_keystate[0][KEY_UP] = true;
                else if(angle >= offset + 2*interval && angle < offset + 3*interval)
                    hw().touch_keystate[0][KEY_UP_RIGHT] = true;
                else if(angle >= offset + 3*interval && angle < offset + 4*interval)
                    hw().touch_keystate[0][KEY_RIGHT] = true;
                else if(angle >= offset + 4*interval && angle < offset + 5*interval)
                    hw().touch_keystate[0][KEY_DOWN_RIGHT] = true;
                else if(angle >= offset + 5*interval && angle < offset + 6*interval)
                    hw().touch_keystate[0][KEY_DOWN] = true;
                else if(angle >= offset + 6*interval && angle < offset + 7*interval)
                    hw().touch_keystate[0][KEY_DOWN_LEFT] = true;
            }
        }
        }
        break;
    case og::input_native::EventType::FingerUp:
        {
            const auto [mapped_x, mapped_y] = window_to_gameplay_ui_canvas(
                event.finger_x * og::runtime::current_session->window_w_,
                event.finger_y * og::runtime::current_session->window_h_);
            int x = static_cast<int>(mapped_x);
            int y = static_cast<int>(mapped_y);
            const TouchControlLayout layout = touch_control_layout(
                static_cast<int>(gameplay_ui_canvas_w()),
                static_cast<int>(gameplay_ui_canvas_h()));
            if(hw().tapping)
            {
                hw().tapping = false;
                if(abs(x - hw().start_tap_x) < layout.tap_slop_x &&
                   abs(y - hw().start_tap_y) < layout.tap_slop_y)
                    og::runtime::current_session->input_continue_ = true;
                else
                    og::runtime::current_session->input_continue_ = false;
                hw().start_tap_x = x;
                hw().start_tap_y = y;
            }
            
            if(hw().moving && event.finger_id == hw().movingTouch)
            {
                hw().moving = false;
                
                hw().touch_keystate[0][KEY_UP] = false;
                hw().touch_keystate[0][KEY_UP_RIGHT] = false;
                hw().touch_keystate[0][KEY_RIGHT] = false;
                hw().touch_keystate[0][KEY_DOWN_RIGHT] = false;
                hw().touch_keystate[0][KEY_DOWN] = false;
                hw().touch_keystate[0][KEY_DOWN_LEFT] = false;
                hw().touch_keystate[0][KEY_LEFT] = false;
                hw().touch_keystate[0][KEY_UP_LEFT] = false;
            }
            if(hw().firing && event.finger_id == hw().firingTouch)
            {
                hw().firing = false;
                hw().touch_keystate[0][KEY_FIRE] = false;
            }
            
            mouse_state.left = 0;
        }
        break;
    case og::input_native::EventType::FingerDown:
        {
            const float window_x =
                event.finger_x * og::runtime::current_session->window_w_;
            const float window_y =
                event.finger_y * og::runtime::current_session->window_h_;
            if (!window_point_in_gameplay_ui_canvas(window_x, window_y))
                break;
            hw().tapping = true;

            const auto [mapped_x, mapped_y] = window_to_gameplay_ui_canvas(
                window_x, window_y);
            int x = static_cast<int>(mapped_x);
            int y = static_cast<int>(mapped_y);
            const TouchControlLayout layout = touch_control_layout(
                static_cast<int>(gameplay_ui_canvas_w()),
                static_cast<int>(gameplay_ui_canvas_h()));
            
            hw().start_tap_x = x;
            hw().start_tap_y = y;
            og::runtime::current_session->input_continue_ = false;
            
            if(!hw().firing && layout.fire.contains(x, y))
            {
                hw().firing = true;
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_FIRE]);
                hw().touch_keystate[0][KEY_FIRE] = true;
                hw().firingTouch = event.finger_id;
            }
            else if(layout.special.contains(x, y))
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SPECIAL]);
            }
            else if(layout.yell.contains(x, y))
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_YELL]);
            }
            else if(layout.switch_character.contains(x, y))
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SWITCH]);
            }
            else if(layout.next_special.contains(x, y))
            {
                sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SPECIAL_SWITCH]);
            }
            else if(layout.alternate_special.contains(x, y))
            {
                // Treat KEY_SHIFTER as an action instead of a modifier
                if(input_touch_has_alternate())
                    sendFakeKeyDownEvent(og::runtime::current_session->player_keys_[0][KEY_SHIFTER]);
            }
            // Only move with the lower left corner of the screen (and offset for other buttons)
            else if(!hw().moving && x < layout.movement_region_right &&
                    y > layout.movement_region_top)
            {
                hw().moving_touch_x = x;
                hw().moving_touch_y = y;
                hw().moving_touch_target_x = x;
                hw().moving_touch_target_y = y;
                if(hw().moving_touch_x < layout.movement_center_min_x)
                    hw().moving_touch_x = layout.movement_center_min_x;
                if(hw().moving_touch_y < layout.movement_center_min_y)
                    hw().moving_touch_y = layout.movement_center_min_y;
                else if(hw().moving_touch_y > layout.movement_center_max_y)
                    hw().moving_touch_y = layout.movement_center_max_y;
                hw().moving = true;
                hw().movingTouch = event.finger_id;
            }
            
            
            og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;
            mouse_state.left = 1;
            mouse_state.x = x;
            mouse_state.y = y;
        }
        break;
#endif
    default:
        break;
    }
}

// Per-tick gameplay reads joystick state by polling (JoyData::getState); the
// event stream's one job here is feeding "press any key" waits.
// Gameplay reads joysticks by polling (isPlayerHoldingKey -> JoyData::getState).
// This should not be in an event or else it's jerky. Events only feed the
// "any key pressed" edge that continue/menu screens wait on.
void handle_joy_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    switch(event.type)
    {
    case og::input_native::EventType::JoyAxisMotion:
        if (event.joy_axis_value > JOY_DEAD_ZONE ||
            event.joy_axis_value < -JOY_DEAD_ZONE)
            og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;
        break;
    case og::input_native::EventType::JoyButtonDown:
        og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;
        break;
    case og::input_native::EventType::JoyHatMotion:
        // A hat returning to center is a release, not a press.
        if (event.joy_hat_value != 0)
            og::runtime::current_session->key_press_event_ = 1;
        ++og::runtime::current_session->key_press_serial_;
        break;
    default:
        break;
    }
}

namespace
{
// Hotplug. Either direction reshuffles the 0-based device indices, so the
// handle table is re-enumerated first and every seat re-anchored after.
void handle_joy_device_event(const og::input_native::EventData& event)
{
    reopen_joystick_device_table();

    if (event.type == og::input_native::EventType::JoyDeviceAdded)
    {
        TRACE("joystick", "device_added index=%d instance=%d",
              event.joy_device_index, event.joy_device_instance);
        // A device matching a seat's saved GUID re-attaches automatically
        // (mandatory on web, where the Gamepad API exposes devices only
        // after a button press).
        reattach_saved_joysticks();
        return;
    }

    const int device_count = joystick_device_count();
    for (int p = 0; p < 4; ++p)
    {
        if (player_joy[p].index < 0)
            continue;
        const std::string& guid = hw().player_joystick_guids[p];
        if (!guid.empty())
        {
            // Re-anchor an assigned seat onto its device's new index. The
            // GUID survives the unplug so the seat re-attaches when the
            // device returns (JoyDeviceAdded above).
            int found = -1;
            for (int d = 0; d < device_count; ++d)
            {
                if (joysticks[d] != nullptr &&
                    og::input_native::joystick_device_guid(d) == guid &&
                    !device_held_by_other_seat(d, p))
                {
                    found = d;
                    break;
                }
            }
            if (found >= 0)
            {
                player_joy[p].index = found;
            }
            else
            {
                player_joy[p].index = -1; // keyboard fallback; GUID kept
                TRACE("joystick", "device_removed player=%d", p);
            }
        }
        else if (player_joy[p].index >= device_count || joysticks[player_joy[p].index] == nullptr)
        {
            // Legacy (takeover-bound, no GUID) seat pointing past the
            // shrunken table: identity is unknowable, fall back to keyboard.
            disablePlayerJoystick(p);
            TRACE("joystick", "device_removed player=%d", p);
        }
    }
}
} // namespace

void handle_events(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    switch (event.type)
    {
    case og::input_native::EventType::Window:
        handle_window_event(native_event);
    break;
    case og::input_native::EventType::TextInput:
        handle_text_event(native_event);
        break;
    case og::input_native::EventType::MouseWheel:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerMotion:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerUp:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::FingerDown:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::KeyDown:
        handle_key_event(native_event);
        break;
    case og::input_native::EventType::KeyUp:
        handle_key_event(native_event);
        break;
    case og::input_native::EventType::MouseMotion:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::MouseButtonUp:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::MouseButtonDown:
        handle_mouse_event(native_event);
        break;
    case og::input_native::EventType::JoyAxisMotion:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyHatMotion:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyButtonDown:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyButtonUp:
        handle_joy_event(native_event);
        break;
    case og::input_native::EventType::JoyDeviceAdded:
    case og::input_native::EventType::JoyDeviceRemoved:
        handle_joy_device_event(event);
        break;
    case og::input_native::EventType::Quit:
        quit(0);
        break;
    default:
        break;
    }
}


//
//Keyboard routines
//

bool query_key_event(int key, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    return (event.type == og::input_native::EventType::KeyDown && event.key_sym == key);
}

bool isKeyboardEvent(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    // does not handle key up events
    return (event.type == og::input_native::EventType::KeyDown);
}

bool isJoystickEvent(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;
    // does not handle button up, hats, or balls
    return (event.type == og::input_native::EventType::JoyAxisMotion
            || event.type == og::input_native::EventType::JoyHatMotion
            || event.type == og::input_native::EventType::JoyButtonDown);
}

namespace
{

const void* wait_for_key_event_polling(KeyWaitPollCallback poll_callback)
{
#ifdef TESTING
    if (poll_callback != nullptr && !poll_callback())
    {
        TRACE("input", "wait_for_key_event: cancelled by poll callback");
        return nullptr;
    }
    // In test mode, return immediately with a fake ESC keypress
    TRACE("input", "wait_for_key_event: returning fake ESC (test mode)");
    return og::input_native::make_test_keydown_event(KEYCODE_ESCAPE, KEYSTATE_ESCAPE);
#else
    while(1)
    {
        if (poll_callback != nullptr && !poll_callback())
            return nullptr;
        while(const void* event = og::input_native::poll_event())
        {
            og::input_native::EventData event_data;
            if (!as_event_data(event, event_data))
                continue;
            if(event_data.type == og::input_native::EventType::Quit
                    || event_data.type == og::input_native::EventType::KeyDown
                    || (event_data.type == og::input_native::EventType::JoyAxisMotion
                        && (event_data.joy_axis_value > JOY_DEAD_ZONE || event_data.joy_axis_value < -JOY_DEAD_ZONE))
                    || event_data.type == og::input_native::EventType::JoyButtonDown
                    || event_data.type == og::input_native::EventType::JoyHatMotion)
                return event;
        }
        YIELD_SLEEP(10);
    }
    return nullptr;
#endif
}

} // namespace

const void* wait_for_key_event()
{
    return wait_for_key_event_polling(nullptr);
}

void quit_if_quit_event(const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    if(event.type == og::input_native::EventType::Quit)
        quit(0);
}

void clear_events()
{
    while (og::input_native::poll_event() != nullptr) {}
}

bool assignKeyFromWaitEventPolling(
    int player_num, int key_enum, KeyWaitPollCallback poll_callback)
{
    const void* event = wait_for_key_event_polling(poll_callback);
    if (event == nullptr)
    {
        // A remap key queued in the same frame as a remote launch must not
        // leak through into the newly-started game.
        clear_events();
        return false;
    }
    og::input_native::EventData event_data;
    if (!as_event_data(event, event_data))
        return true;
    quit_if_quit_event(event);
    if(isKeyboardEvent(event))
    {
        if(event_data.key_sym != KEYCODE_ESCAPE)
        {
            set_player_key_binding(player_num, key_enum, event_data.key_sym);
        }
    }
    else if(isJoystickEvent(event))
        player_joy[player_num].setKeyFromEvent(key_enum, event);

#ifndef TESTING
    if (poll_callback == nullptr)
    {
        YIELD_SLEEP(400);
    }
    else
    {
        // Preserve the remap debounce without going dark to the lobby for
        // another 400 ms after each accepted key.
        for (int waited_ms = 0; waited_ms < 400; waited_ms += 10)
        {
            if (!poll_callback())
            {
                clear_events();
                return false;
            }
            YIELD_SLEEP(10);
        }
    }
#endif
    clear_events();
    return true;
}

//
// Set the keyboard array to all zeros, the
// virgin state, nothing depressed
//
void clear_keyboard()
{
    og::runtime::current_session->key_press_event_ = 0;
    og::runtime::current_session->raw_key_ = 0;

    og::runtime::current_session->text_input_event_ = 0;
    og::runtime::current_session->raw_text_input_.clear();
    
    og::runtime::current_session->input_continue_ = false;

    #ifdef USE_TOUCH_INPUT
    hw().tapping = false;
    #endif
}

//
// Focus/visibility loss: every "physically held" input we believe in may be
// stale — the release can go to another window or app and never reach us,
// and iPad Safari swallows keyups outright around system gestures. Drop all
// transient held state; genuine holds re-assert on the next delivered
// events. (This is what un-latches a key whose keyup was missed.)
//
void clear_transient_input_state()
{
    og::input_native::reset_keyboard_state();
    for (int p = 0; p < 4; ++p)
    {
        hw().direction_grace[p] = {};
        hw().direction_conflict[p] = {};
        for (int k = 0; k < NUM_KEYS; ++k)
            hw().touch_keystate[p][k] = false;
    }
    TRACE("input", "transient input cleared (focus/visibility loss)");
}

void wait_for_key(int somekey)
{
#ifdef TESTING
    (void)somekey;
    TRACE("input", "wait_for_key: skipping wait (test mode)");
    return;
#else
    // First wait for key press ..
    while (!og::runtime::current_session->keystates_[og::input_native::scancode_from_key(somekey)])
        get_input_events(WAIT);

    // And now for the key to be released ..
    while (!og::runtime::current_session->keystates_[og::input_native::scancode_from_key(somekey)])
        get_input_events(WAIT);
#endif
}

// JoyData::JoyData() (default ctor) is defined in the SDL-free input_state.cpp so
// InputHardwareState (which value-initializes a JoyData[4]) can be constructed by
// the headless builds (curses/server/text) that do not compile this file.

JoyData::JoyData(int joy_index)
    : index(-1), numAxes(0), numButtons(0), numHats(0)
{
    // Both early returns leave a fully EMPTY layout, never garbage: key_type /
    // key_index / held_at_assign_mask are zero-initialized by their default
    // member initializers, and NONE == 0. An unopenable device (the udev case)
    // therefore yields index -1 and no bindings, so the seat keeps its
    // keyboard mapping instead of reading a device that cannot be read.
    if(joy_index < 0 || joy_index >= MAX_NUM_JOYSTICKS)
        return;
    const og::input_native::JoystickHandle js = joysticks[joy_index];
    if(js == nullptr)
        return;

    this->index = joy_index;
    // The SDL capability getters report -1 on failure (a handle whose device
    // went away or lost permissions mid-session). Clamp: a negative count must
    // never reach the synthesis loops or a stored numButtons.
    numAxes = std::max(0, og::input_native::joystick_num_axes(js));
    numButtons = std::max(0, og::input_native::joystick_num_buttons(js));
    numHats = std::max(0, og::input_native::joystick_num_hats(js));

    // Clear all keys for this joystick
    for(int i = 0; i < NUM_KEYS; i++)
    {
        key_type[i] = NONE;
        key_index[i] = 0;
    }

    // Default movement: prefer a two-axis stick, but never bind an axis
    // whose RESTING value is already past the dead zone. Real hardware
    // violates the axes-0/1-are-the-stick assumption — sticks drift,
    // GameCube-style analog triggers rest at an extreme, adapters reorder
    // axes — and a cardinal bound to such an axis reads permanently held:
    // gameplay runs away and every release wait (menu nav) wedges forever.
    int calm_axes[2] = {0, 0};
    int calm_count = 0;
    for (int axis = 0; axis < numAxes && calm_count < 2; ++axis)
    {
        const int resting = og::input_native::joystick_get_axis(js, axis);
        if (resting > JOY_DEAD_ZONE || resting < -JOY_DEAD_ZONE)
        {
            TRACE("joystick", "synthesis_skip_axis device=%d axis=%d rest=%d",
                  joy_index, axis, resting);
            continue;
        }
        calm_axes[calm_count++] = axis;
    }
    if(calm_count >= 2) // Prefer two (calm) axes
    {
        key_type[KEY_RIGHT] = POS_AXIS;
        key_index[KEY_RIGHT] = calm_axes[0];
        key_type[KEY_LEFT] = NEG_AXIS;
        key_index[KEY_LEFT] = calm_axes[0];

        key_type[KEY_UP] = NEG_AXIS;
        key_index[KEY_UP] = calm_axes[1];
        key_type[KEY_DOWN] = POS_AXIS;
        key_index[KEY_DOWN] = calm_axes[1];
    }
    else if(numHats > 0) // But a single hat is okay otherwise
    {
        // indices default to hat 0
        key_type[KEY_UP] = HAT_UP;
        key_type[KEY_UP_RIGHT] = HAT_UP_RIGHT;
        key_type[KEY_RIGHT] = HAT_RIGHT;
        key_type[KEY_DOWN_RIGHT] = HAT_DOWN_RIGHT;
        key_type[KEY_DOWN] = HAT_DOWN;
        key_type[KEY_DOWN_LEFT] = HAT_DOWN_LEFT;
        key_type[KEY_LEFT] = HAT_LEFT;
        key_type[KEY_UP_LEFT] = HAT_UP_LEFT;
    }


    // Default actions. Button 0 is SPECIAL and button 1 is FIRE (the shipped
    // gamepad default, reversed from the keyboard-era order at the player's
    // request). Consequence on a one-button device: FIRE stays unbound and
    // keeps coming from the seat's keyboard map.
    if(numButtons > 0)
    {
        key_type[KEY_SPECIAL] = BUTTON;
        key_index[KEY_SPECIAL] = 0;
    }
    if(numButtons > 1)
    {
        key_type[KEY_FIRE] = BUTTON;
        key_index[KEY_FIRE] = 1;
    }
    if(numButtons > 2)
    {
        key_type[KEY_SPECIAL_SWITCH] = BUTTON;
        key_index[KEY_SPECIAL_SWITCH] = 2;
    }
    if(numButtons > 3)
    {
        key_type[KEY_YELL] = BUTTON;
        key_index[KEY_YELL] = 3;
    }
    if(numButtons > 4)
    {
        key_type[KEY_SHIFTER] = BUTTON;
        key_index[KEY_SHIFTER] = 4;
    }
    if(numButtons > 5)
    {
        key_type[KEY_SWITCH] = BUTTON;
        key_index[KEY_SWITCH] = 5;
    }

    // Baseline snapshot: any binding that reads ACTIVE right now (a resting
    // pressed button/hat, the owner's grip during assignment) starts
    // suppressed; isPlayerHoldingKey ignores it until one real release is
    // observed. See held_at_assign_mask in input.h.
    for (int k = 0; k < NUM_KEYS; ++k)
    {
        if (key_type[k] != NONE && getState(k))
        {
            held_at_assign_mask |= (1 << k);
            TRACE("joystick", "synthesis_suppress device=%d key=%d",
                  joy_index, k);
        }
    }
}


void JoyData::setKeyFromEvent(int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return;
    if (key_enum < 0 || key_enum >= NUM_KEYS) return;  // key_type/key_index are sized NUM_KEYS

    // Diagonals are ignored because they are combinations of the cardinals
    // Things get really messy when diagonals are assigned
    if(key_enum == KEY_UP_RIGHT || key_enum == KEY_UP_LEFT || key_enum == KEY_DOWN_RIGHT || key_enum == KEY_DOWN_LEFT)
    {
        key_type[key_enum] = NONE;
        key_index[key_enum] = 0;
        return;
    }

    // A seat with an explicit assignment (GUID set) only remaps from its own
    // device: ignore events from other devices instead of following the
    // legacy last-device-pressed takeover. Standalone JoyData objects and
    // unassigned seats keep the legacy behavior.
    {
        int owner = -1;
        for (int i = 0; i < 4; ++i)
        {
            if (this == &player_joy[i])
            {
                owner = i;
                break;
            }
        }
        if (owner >= 0 && !hw().player_joystick_guids[owner].empty())
        {
            int source_device = -1;
            if (event.type == og::input_native::EventType::JoyAxisMotion)
                source_device = event.joy_axis_which;
            else if (event.type == og::input_native::EventType::JoyButtonDown)
                source_device = event.joy_button_which;
            else if (event.type == og::input_native::EventType::JoyHatMotion)
                source_device = event.joy_hat_which;
            if (source_device >= 0 && source_device != index)
                return;
        }
    }

    bool gotJoy = false;
    if(event.type == og::input_native::EventType::JoyAxisMotion)
    {
        if(event.joy_axis_value >= 0)
            key_type[key_enum] = POS_AXIS;
        else
            key_type[key_enum] = NEG_AXIS;
        key_index[key_enum] = event.joy_axis_axis;
        index = event.joy_axis_which;  // USES THE LAST JOYSTICK PRESSED
        gotJoy = true;
    }
    else if(event.type == og::input_native::EventType::JoyButtonDown)
    {
        key_type[key_enum] = BUTTON;
        key_index[key_enum] = event.joy_button_button;
        index = event.joy_button_which;  // USES THE LAST JOYSTICK PRESSED
        gotJoy = true;
    }
    else if(event.type == og::input_native::EventType::JoyHatMotion)
    {
        bool badHat = false;
        if(event.joy_hat_value == og::input_native::kHatUp)
            key_type[key_enum] = HAT_UP;
        else if(event.joy_hat_value == og::input_native::kHatRight)
            key_type[key_enum] = HAT_RIGHT;
        else if(event.joy_hat_value == og::input_native::kHatDown)
            key_type[key_enum] = HAT_DOWN;
        else if(event.joy_hat_value == og::input_native::kHatLeft)
            key_type[key_enum] = HAT_LEFT;
        else
        {
            badHat = true;
            // Diagonal hat values are ignored because they are combinations
            // of the cardinals and complicate key assignment.
        }
        if(!badHat)
        {
            key_index[key_enum] = event.joy_hat_hat;
            index = event.joy_hat_which;  // USES THE LAST JOYSTICK PRESSED
            gotJoy = true;
        }
    }

    if(gotJoy)
    {
        // A rebound key is live again: the user just proved this input can
        // move, so any held-at-assignment suppression is stale.
        held_at_assign_mask &= ~(1 << key_enum);
        // Take over this joystick, voiding the robbed seat's explicit claim.
        for(int i = 0; i < 4; i++)
        {
            if(this != &player_joy[i] && player_joy[i].index == index)
            {
                player_joy[i].index = -1;
                hw().player_joystick_guids[i].clear();
            }
        }
    }
}

bool JoyData::getState(int key_enum) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    switch(key_type[key_enum])
    {
    case POS_AXIS:
        return og::input_native::joystick_get_axis(joysticks[index], key_index[key_enum]) > JOY_DEAD_ZONE;
    case NEG_AXIS:
        return og::input_native::joystick_get_axis(joysticks[index], key_index[key_enum]) < -JOY_DEAD_ZONE;
    case BUTTON:
        return og::input_native::joystick_get_button(joysticks[index], key_index[key_enum]);
    case HAT_UP:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatUp);
    case HAT_RIGHT:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatRight);
    case HAT_DOWN:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatDown);
    case HAT_LEFT:
        return (og::input_native::joystick_get_hat(joysticks[index], key_index[key_enum]) & og::input_native::kHatLeft);
        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::getPress(int key_enum, const void* native_event) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    switch(key_type[key_enum])
    {
    case BUTTON:
        if(event.type == og::input_native::EventType::JoyButtonDown)
        {
            return (event.joy_button_which == index && event.joy_button_button == key_index[key_enum]);
        }
        return false;
    case POS_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value > JOY_DEAD_ZONE);
        }
        return false;
    case NEG_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value < -JOY_DEAD_ZONE);
        }
        return false;
    case HAT_UP:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatUp));
    case HAT_RIGHT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatRight));
    case HAT_DOWN:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatDown));
    case HAT_LEFT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatLeft));

        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::getRelease(int key_enum, const void* native_event) const
{
    if(index < 0 || index >= MAX_NUM_JOYSTICKS)
        return false;
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    switch(key_type[key_enum])
    {
    case BUTTON:
        if(event.type == og::input_native::EventType::JoyButtonUp)
        {
            return (event.joy_button_which == index && event.joy_button_button == key_index[key_enum]);
        }
        return false;
    case POS_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value < JOY_DEAD_ZONE);
        }
        return false;
    case NEG_AXIS:
        if(event.type == og::input_native::EventType::JoyAxisMotion)
        {
            return (event.joy_axis_which == index && event.joy_axis_axis == key_index[key_enum] && event.joy_axis_value > -JOY_DEAD_ZONE);
        }
        return false;
    case HAT_UP:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatUp));
    case HAT_RIGHT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatRight));
    case HAT_DOWN:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatDown));
    case HAT_LEFT:
        return (event.joy_hat_which == index && event.joy_hat_hat == key_index[key_enum] && (event.joy_hat_value & og::input_native::kHatLeft));

        // Diagonals are ignored because they are combinations of the cardinals
    case HAT_UP_RIGHT:
    case HAT_DOWN_RIGHT:
    case HAT_DOWN_LEFT:
    case HAT_UP_LEFT:
    default:
        return false;
    }
}

bool JoyData::hasButtonSet(int key_enum) const
{
    return (index >= 0 && key_type[key_enum] != NONE);
}

bool isPlayerHoldingKey(int player_index, int key_enum)
{
    if (player_index < 0 || player_index >= 4 || key_enum < 0 || key_enum >= NUM_KEYS)
        return false;
    // Touch held-state seam, written by the web DOM overlay or native SDL
    // touch path. It is additive with joystick and keyboard and deliberately
    // independent of keyboard control mode.
    if (hw().touch_keystate[player_index][key_enum])
        return true;
    // FIXME: On Android/iOS, do not mistake an accelerometer for a gamepad.
    JoyData& joy = player_joy[player_index];
    if(joy.hasButtonSet(key_enum))
    {
        const bool held = joy.getState(key_enum);
        if (joy.held_at_assign_mask & (1 << key_enum))
        {
            // Held since the layout was synthesized (drift, a resting
            // trigger, a grip hold during assignment): suppressed until one
            // real release proves the input can rest. See JoyData(int).
            if (held)
                return false;
            joy.held_at_assign_mask &= ~(1 << key_enum);
        }
        return held;
    }
    else
        return og::runtime::current_session->keystates_[og::input_native::scancode_from_key(og::runtime::current_session->player_keys_[player_index][key_enum])];
}

bool didPlayerPressKey(int player_index, int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    if(player_joy[player_index].hasButtonSet(key_enum))
    {
        // This key is on the joystick, so check it.
        return player_joy[player_index].getPress(key_enum, native_event);
    }
    else
    {
        // If the player is using KEYBOARD or doesn't have a joystick button set for this key, then check the keyboard.
        if(event.type == og::input_native::EventType::KeyDown)
        {
            if(event.key_repeat) // Repeats don't count!
                return false;
            return (event.key_sym == og::runtime::current_session->player_keys_[player_index][key_enum]);
        }
        return false;
    }
}

bool didPlayerReleaseKey(int player_index, int key_enum, const void* native_event)
{
    og::input_native::EventData event;
    if (!as_event_data(native_event, event))
        return false;

    if(player_joy[player_index].hasButtonSet(key_enum))
    {
        // This key is on the joystick, so check it.
        return player_joy[player_index].getRelease(key_enum, native_event);
    }
    else
    {
        // If the player is using KEYBOARD or doesn't have a joystick button set for this key, then check the keyboard.
        if(event.type == og::input_native::EventType::KeyUp)
        {
            return (event.key_sym == og::runtime::current_session->player_keys_[player_index][key_enum]);
        }
        return false;
    }
}

//
// Mouse routines
//

void grab_mouse()
{
    og::input_native::show_cursor(true);
}

void release_mouse()
{
    #ifndef FAKE_TOUCH_EVENTS
    og::input_native::show_cursor(false);
    #endif
}

MouseState& query_mouse()
{
    // The mouse_state thing is set using get_input_events, though
    // it should probably get its own function
    get_input_events(POLL);

    // Any press standing now is observable by normal edge detection; only
    // releases arriving before this point count as collapsed taps.
    g_mouse_left_unqueried_press = false;
    g_mouse_right_unqueried_press = false;
    return mouse_state;
}

void reset_mouse_click_tracking()
{
    // Consume events that belong to the outgoing surface before establishing
    // the new baseline. This covers both a complete press/release pair that
    // was queued between frames and a pointer that is still held while the
    // next surface is constructed.
    get_input_events(POLL);

    g_mouse_left_unqueried_press = false;
    g_mouse_right_unqueried_press = false;
    g_pending_left_clicks = 0;
    g_pending_right_clicks = 0;

    hw().picker_was_left_down = mouse_state.left;
    hw().picker_was_right_down = mouse_state.right;
}

void acknowledge_mouse_presses()
{
    // The pointer-handoff rule (docs/menu-engine.md, "Pointer handoff"): a
    // surface acknowledges the presses it saw. This is query_mouse()'s
    // observed-press bookkeeping without the poll — the callers run their
    // own pumps, and a second poll here would steal their events. A press
    // and release that land inside ONE pump still mint a collapsed tap,
    // exactly as they do for a query_mouse() surface.
    g_mouse_left_unqueried_press = false;
    g_mouse_right_unqueried_press = false;
}

#ifdef TESTING
namespace og::input {
int testing_pending_left_clicks()
{
    return g_pending_left_clicks;
}
} // namespace og::input
#endif

bool take_pending_left_click()
{
    if (g_pending_left_clicks > 0)
    {
        --g_pending_left_clicks;
        return true;
    }
    return false;
}

bool take_pending_right_click()
{
    if (g_pending_right_clicks > 0)
    {
        --g_pending_right_clicks;
        return true;
    }
    return false;
}

// Convert from scancode to ascii, ie, KEYCODE_a to 'A'
unsigned char convert_to_ascii(int scancode)
{
    switch (scancode)
    {
    case KEYCODE_a:
        return 'A';
    case KEYCODE_b:
        return 'B';
    case KEYCODE_c:
        return 'C';
    case KEYCODE_d:
        return 'D';
    case KEYCODE_e:
        return 'E';
    case KEYCODE_f:
        return 'F';
    case KEYCODE_g:
        return 'G';
    case KEYCODE_h:
        return 'H';
    case KEYCODE_i:
        return 'I';
    case KEYCODE_j:
        return 'J';
    case KEYCODE_k:
        return 'K';
    case KEYCODE_l:
        return 'L';
    case KEYCODE_m:
        return 'M';
    case KEYCODE_n:
        return 'N';
    case KEYCODE_o:
        return 'O';
    case KEYCODE_p:
        return 'P';
    case KEYCODE_q:
        return 'Q';
    case KEYCODE_r:
        return 'R';
    case KEYCODE_s:
        return 'S';
    case KEYCODE_t:
        return 'T';
    case KEYCODE_u:
        return 'U';
    case KEYCODE_v:
        return 'V';
    case KEYCODE_w:
        return 'W';
    case KEYCODE_x:
        return 'X';
    case KEYCODE_y:
        return 'Y';
    case KEYCODE_z:
        return 'Z';

    case KEYCODE_1:
        return '1';
    case KEYCODE_2:
        return '2';
    case KEYCODE_3:
        return '3';
    case KEYCODE_4:
        return '4';
    case KEYCODE_5:
        return '5';
    case KEYCODE_6:
        return '6';
    case KEYCODE_7:
        return '7';
    case KEYCODE_8:
        return '8';
    case KEYCODE_9:
        return '9';
    case KEYCODE_0:
        return '0';

    case KEYCODE_SPACE:
        return 32;
        //    case KEYCODE_BACKSPACE: return 8;
    case KEYCODE_RETURN:
        return 13;
    case KEYCODE_ESCAPE:
        return 27;
    case KEYCODE_PERIOD:
        return '.';
    case KEYCODE_COMMA:
        return ',';
    case KEYCODE_QUOTE:
        return '\'';
    case KEYCODE_BACKQUOTE:
        return '`';

    default:
        return 255;
    }
}

