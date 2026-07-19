#pragma once

// Web builds must not depend on the Escape key: browsers reserve Escape for
// exiting fullscreen (and some reserve it in other UI states), so a single
// physical press can both leave fullscreen AND back out of a game menu. On
// web, Backspace is the universal back/cancel/menu key and physical Escape is
// swallowed entirely. While a text-entry field is active, Backspace keeps its
// delete-character meaning and is NOT translated.
//
// This header is pure logic (no SDL includes) so headless unit tests can
// cover the full translation matrix. The __EMSCRIPTEN__ platform layer
// (src/platform/sdl/native_input.cpp) applies it with web_mode=true at the
// SDL event source and to the keyboard-state mirror; native builds never
// engage it, so native Escape behavior is untouched.

namespace og::input
{

// SDL keycode/scancode values, duplicated here to stay SDL-free. The SDL
// platform layer static_asserts these against the real SDL constants.
inline constexpr int kBackKeycodeEscape = 27;     // SDLK_ESCAPE
inline constexpr int kBackKeycodeBackspace = 8;   // SDLK_BACKSPACE
inline constexpr int kBackScancodeEscape = 41;    // SDL_SCANCODE_ESCAPE
inline constexpr int kBackScancodeBackspace = 42; // SDL_SCANCODE_BACKSPACE

// True when this build routes the back/cancel action through Backspace.
inline constexpr bool kWebBackKeyMode =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

struct BackKeyEventTranslation
{
    bool swallow = false; // drop the key event entirely
    int keycode = 0;      // keycode the game must see (valid when !swallow)
    int scancode = 0;     // scancode the game must see (valid when !swallow)
};

// Translate one key event (down or up). In web mode:
//  - physical Escape is always swallowed (a browser fullscreen-exit Escape
//    must never ALSO trigger the in-game back action),
//  - Backspace becomes Escape, except while text input is active, where it
//    keeps its delete-character meaning.
// Outside web mode the event passes through unchanged.
constexpr BackKeyEventTranslation translate_back_key(
    int keycode, int scancode, bool web_mode, bool text_input_active)
{
    if (!web_mode)
        return {false, keycode, scancode};
    if (keycode == kBackKeycodeEscape || scancode == kBackScancodeEscape)
        return {true, keycode, scancode};
    if (!text_input_active &&
        (keycode == kBackKeycodeBackspace || scancode == kBackScancodeBackspace))
        return {false, kBackKeycodeEscape, kBackScancodeEscape};
    return {false, keycode, scancode};
}

struct BackKeyStates
{
    bool escape_down = false;    // what keystates_[KEYSTATE_ESCAPE] must read
    bool backspace_down = false; // what the Backspace scancode slot must read
};

// Translate held-key state for the keyboard-state array (scancode-indexed).
// In web mode the Escape slot mirrors physical Backspace (unless text input
// is active) and never mirrors physical Escape; the Backspace slot is zeroed
// while it is acting as Escape so a Backspace gameplay binding cannot fire in
// the same press.
constexpr BackKeyStates translate_back_key_states(
    bool physical_escape_down, bool physical_backspace_down,
    bool web_mode, bool text_input_active)
{
    if (!web_mode)
        return {physical_escape_down, physical_backspace_down};
    if (text_input_active)
        return {false, physical_backspace_down};
    return {physical_backspace_down, false};
}

// User-visible name of the back/cancel key for help screens and hints.
constexpr const char* back_key_label(bool web_mode)
{
    return web_mode ? "BACKSPACE" : "ESC";
}

// Abbreviated form for space-constrained HUD/footer text.
constexpr const char* back_key_label_short(bool web_mode)
{
    return web_mode ? "BKSP" : "ESC";
}

constexpr const char* back_key_label() { return back_key_label(kWebBackKeyMode); }
constexpr const char* back_key_label_short() { return back_key_label_short(kWebBackKeyMode); }

} // namespace og::input
