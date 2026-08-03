#pragma once

// Browser-safe default controls (issue #144).
//
// Player 1's native factory keymap binds FIRE to Left Ctrl and SPECIAL to
// Left Alt. In a browser that makes "attack while walking up" literally
// Ctrl+W — a chord Firefox reserves at browser-chrome level (close tab), so
// no page script or SDL preventDefault can stop it, fullscreen or not (and
// Keyboard Lock is Chromium-only). On web builds player 1's FIRE/SPECIAL
// defaults therefore move off the modifier keys:
//   4-direction map: FIRE = Z, SPECIAL = X (free next to WASD);
//   8-direction map: FIRE = Space, SPECIAL = [ (z/x are P1's 8-dir
//     diagonals; Space overlaps P3's fire, an accepted shared-keyboard
//     overlap of the same kind as the documented P1/P4 'v' quirk).
// The native kDefault*DirKeys tables are the unchanged source of truth;
// web builds substitute at the read sites via browser_safe_default_key().
//
// Persisted-state migration: the engine writes the whole controls block back
// to config on every boot, so every existing web player has the old LCtrl /
// LAlt values frozen in persisted storage. migrate_persisted_control_key()
// performs a one-shot, version-keyed rewrite of stored values that still
// equal the old factory default — a deliberate user rebind (anything else,
// including LCtrl re-chosen AFTER the migration ran) is never touched. The
// marker key is controls/web_default_keys_version.
//
// This header is pure logic (no SDL includes) so headless unit tests can
// cover both web and native branches; the web_mode default argument pattern
// mirrors web_back_key.h.

namespace og::input
{

// Key-map slot and mode indices plus SDL keycode values, duplicated here to
// stay self-contained (this header is included by input.h ahead of the
// KEYCODE_* constants). input_state.cpp static_asserts every one of these
// against the real constants.
inline constexpr int kWebKeySlotFire = 8;    // KEY_FIRE
inline constexpr int kWebKeySlotSpecial = 9; // KEY_SPECIAL
inline constexpr int kWebModeFourIndex = 0;  // 4-direction key map
inline constexpr int kWebModeEightIndex = 1; // 8-direction key map

inline constexpr int kWebFourDirFireKey = 122;          // KEYCODE_z
inline constexpr int kWebFourDirSpecialKey = 120;       // KEYCODE_x
inline constexpr int kWebEightDirFireKey = 32;          // KEYCODE_SPACE
inline constexpr int kWebEightDirSpecialKey = 91;       // KEYCODE_LEFTBRACKET
inline constexpr int kWebLegacyFireKey = 1073742048;    // KEYCODE_LCTRL
inline constexpr int kWebLegacySpecialKey = 1073742050; // KEYCODE_LALT

// True when this build ships the browser-safe defaults.
inline constexpr bool kWebControlDefaults =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

// Bump when the web defaults change again; stored persistently as
// controls/web_default_keys_version so the migration runs exactly once.
inline constexpr int kWebControlDefaultsVersion = 1;

// Substitute a browser-safe key for profile 0's FIRE/SPECIAL defaults in web
// mode; everything else (native mode, profiles 1-3, all other key slots)
// passes the native default through untouched.
constexpr int browser_safe_default_key(int profile, int mode_index,
                                       int key_enum, int native_default,
                                       bool web_mode)
{
    if (!web_mode || profile != 0)
        return native_default;
    if (key_enum == kWebKeySlotFire)
        return (mode_index == kWebModeEightIndex) ? kWebEightDirFireKey
                                                  : kWebFourDirFireKey;
    if (key_enum == kWebKeySlotSpecial)
        return (mode_index == kWebModeEightIndex) ? kWebEightDirSpecialKey
                                                  : kWebFourDirSpecialKey;
    return native_default;
}

// One-shot migration of a persisted binding: rewrite only when the stored
// value still equals the old factory default and the version-keyed migrate
// flag is set. Never touches a user's own choice.
constexpr int migrate_persisted_control_key(int stored, int old_default,
                                            int new_default, bool migrate)
{
    return (migrate && stored == old_default) ? new_default : stored;
}

} // namespace og::input
