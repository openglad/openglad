// Web touch-control bridge: EMSCRIPTEN_KEEPALIVE exports called by the DOM
// overlay in web/shell.html. Held keys are written into the runtime touch
// seam (InputHardwareState::touch_keystate, OR-ed into isPlayerHoldingKey);
// edge-style actions (BACK, text entry) are pushed as SDL events.
//
// This file is only compiled into the Emscripten web target (see the
// EMSCRIPTEN block appending to GAME_SOURCES_NO_MAIN in CMakeLists.txt).
// Native touch input writes the same held-state seam from its SDL path.
#ifdef __EMSCRIPTEN__

#include <openglad/interface/input.h>
#include <openglad/interface/native_input.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/web_back_key.h>

#include <emscripten.h>

#include <array>
#include <cstring>

namespace
{
constexpr int kMaxTouchPlayers = 4;

bool session_ready()
{
    return og::runtime::current_session != nullptr;
}

// Test/diagnostic mirror: window.__opengladTouchKeys reflects the touch seam
// after C++-side bounds checks, so e2e tests assert delivery end to end.
// Player 0 is flattened onto the top-level object (m.fire, m.up, ...).
EM_JS(void, og_touch_mirror_set_js, (int player, int key, int down), {
    var names = ['up', 'up_right', 'right', 'down_right', 'down', 'down_left',
                 'left', 'up_left', 'fire', 'special', 'switch',
                 'special_switch', 'yell', 'shifter', 'prefs', 'cheat',
                 'lookup'];
    var m = window.__opengladTouchKeys || (window.__opengladTouchKeys = {});
    var pk = m['p' + player] || (m['p' + player] = {});
    pk[names[key]] = !!down;
    if (player === 0)
        m[names[key]] = !!down;
});

EM_JS(void, og_touch_mirror_clear_js, (), {
    window.__opengladTouchKeys = {};
});

EM_JS(void, og_touch_mirror_enable_js, (int on), {
    var m = window.__opengladTouchKeys || (window.__opengladTouchKeys = {});
    m.enabled = !!on;
});

// Same mirror contract for the device class: e2e asserts the phone rule
// arrived in C++ rather than re-deriving it in JS.
EM_JS(void, og_single_seat_mirror_js, (int on), {
    window.__opengladSingleSeat = !!on;
});
} // namespace

extern "C"
{

EMSCRIPTEN_KEEPALIVE void openglad_web_touch_set_key(int player, int key_enum, int down)
{
    if (!session_ready())
        return;
    if (player < 0 || player >= kMaxTouchPlayers || key_enum < 0 || key_enum >= NUM_KEYS)
        return;
    input_hardware_state().touch_keystate[player][key_enum] = (down != 0);
    og_touch_mirror_set_js(player, key_enum, down != 0 ? 1 : 0);
}

EMSCRIPTEN_KEEPALIVE void openglad_web_touch_clear()
{
    og_touch_mirror_clear_js();
    og::input_native::set_virtual_back_key(false);
    if (!session_ready())
        return;
    for (int p = 0; p < kMaxTouchPlayers; ++p)
        for (int k = 0; k < NUM_KEYS; ++k)
            input_hardware_state().touch_keystate[p][k] = false;
}

// Returns 1 once the request was applied to a live session (the shell retries
// from its state watcher until then; the game session is created inside
// openglad_web_boot, potentially after the overlay decides to enable).
EMSCRIPTEN_KEEPALIVE int openglad_web_touch_enable(int on)
{
    og_touch_mirror_enable_js(on != 0 ? 1 : 0);
    if (!session_ready())
        return 0;
    // Touch diagonals are source-aware in input_state_from_sdl and therefore
    // do not mutate (or depend on) the player's keyboard control mode.
    if (on == 0)
    {
        openglad_web_touch_clear();
    }
    return 1;
}

// #249: the shell reports the device class (a phone has one built-in seat —
// the touchscreen). Deliberately NOT gated on session_ready(): the shell
// classifies the device during page boot, which can precede
// openglad_web_boot, and set_single_seat_device latches an early call for
// init_input. Always returns 1 (recorded) so the shell's retry loop stops on
// the first call.
EMSCRIPTEN_KEEPALIVE int openglad_web_set_single_seat_device(int on)
{
    const bool single_seat = (on != 0);
    og::input::set_single_seat_device(single_seat);
    og_single_seat_mirror_js(single_seat ? 1 : 0);
    return 1;
}

// Touch BACK is two-tier like the movement seam: a pushed Backspace key pair
// serves the edge-driven consumers (game_loop pause/abort, raw_key_,
// text-prompt handling), while the virtual held flag serves keystate-driven
// menu hotkeys and release spin-waits, which pushed events cannot reach. The
// web back-key filter translates both to Escape outside text input.
EMSCRIPTEN_KEEPALIVE void openglad_web_back_key(int down)
{
    if (!session_ready())
        return;
    og::input_native::set_virtual_back_key(down != 0);
    og::input_native::push_key_event(down != 0, og::input::kBackKeycodeBackspace);
}

// Quick taps inside the virtual-joystick zone that never left the dead zone
// are forwarded as a left-mouse click at canvas-logical coordinates, so
// in-gameplay dialogs (the Abort Mission prompt draws under the zone) stay
// tappable. Harmless during normal play: gameplay ignores the mouse.
EMSCRIPTEN_KEEPALIVE void openglad_web_push_mouse(
    int down, int x, int y, int css_w, int css_h)
{
    if (!session_ready())
        return;
    // x/y are CSS pixels relative to the canvas whose CSS size is
    // css_w/css_h; the seam rescales into SDL window-logical space (the
    // picker's logical window is larger than a phone's CSS canvas box).
    og::input_native::push_mouse_button_event_css(
        down != 0, og::input_native::kMouseButtonLeft, x, y, css_w, css_h);
}

EMSCRIPTEN_KEEPALIVE void openglad_web_push_key(int keycode, int down)
{
    if (!session_ready())
        return;
    // BACK sends SDLK_BACKSPACE: the web back-key filter translates it to
    // Escape everywhere except during text input (where it stays
    // delete-character). Never push SDLK_ESCAPE — the filter swallows it.
    og::input_native::push_key_event(down != 0, keycode);
}

// CANCEL affordance of the shell's DOM text-entry overlay. Distinct from
// BACK: during text input Backspace means delete-character, and pushed Escape
// is normally swallowed by the web back-key filter, so cancel needs this
// dedicated seam (input_string's Escape path restores the original value and
// returns null). No-op unless a text prompt is active.
EMSCRIPTEN_KEEPALIVE void openglad_web_text_cancel()
{
    if (!session_ready())
        return;
    og::input_native::push_text_cancel_key();
}

EMSCRIPTEN_KEEPALIVE void openglad_web_push_text(const char* utf8)
{
    if (!session_ready() || utf8 == nullptr || utf8[0] == '\0')
        return;
    og::input_native::push_text_event(utf8);
}

// Allocation-free variant for the shell's text-entry overlay: one Unicode
// codepoint per call, encoded to UTF-8 here so JS needs no heap helpers
// (stringToNewUTF8 is not in the exported runtime methods).
EMSCRIPTEN_KEEPALIVE void openglad_web_push_char(int codepoint)
{
    if (!session_ready() || codepoint <= 0 || codepoint > 0x10FFFF)
        return;
    const unsigned int cp = static_cast<unsigned int>(codepoint);
    std::array<char, 5> buf{};
    if (cp < 0x80u)
    {
        buf[0] = static_cast<char>(cp);
    }
    else if (cp < 0x800u)
    {
        buf[0] = static_cast<char>(0xC0u | (cp >> 6u));
        buf[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
    }
    else if (cp < 0x10000u)
    {
        buf[0] = static_cast<char>(0xE0u | (cp >> 12u));
        buf[1] = static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
        buf[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
    }
    else
    {
        buf[0] = static_cast<char>(0xF0u | (cp >> 18u));
        buf[1] = static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu));
        buf[2] = static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu));
        buf[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
    }
    og::input_native::push_text_event(buf.data());
}

} // extern "C"

#endif // __EMSCRIPTEN__
