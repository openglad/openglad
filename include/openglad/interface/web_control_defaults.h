#pragma once

// Browser-safe default controls (issue #144).
//
// Player 1's native factory keymap binds FIRE to Left Ctrl. In a browser that
// makes "attack while walking up" literally Ctrl+W — a chord the browser
// owns in windowed play: Firefox delivers the keydown and closes the tab
// anyway (WONTFIX since Firefox 48), and Chromium never delivers it to the
// page at all. The only escape is JS-initiated fullscreen (Chromium honors
// preventDefault there; Firefox 151 and Safari 26.4 need
// requestFullscreen({keyboardLock:"browser"}), which also turns Escape into
// hold-to-exit) — deliberately not used here, so windowed play decides.
// Ctrl is the whole problem, so web P1 keeps FIRE off Ctrl, and everything
// else that moves is a knock-on from the key FIRE lands on.
//
//   4-direction map: FIRE = Z (free next to WASD; Z is only P1's own
//     8-direction DOWN-LEFT, and a player's two maps are never co-active).
//
//   8-direction map: FIRE = S, the center of the W/E/D/C/X/Z/A/Q movement
//     diamond (Z is P1's own diagonal in this map). S is collision-free on a
//     shared keyboard: no OTHER player binds it in either mode. It is P1's
//     own 4-dir DOWN, which is never co-active.
//     YELL, displaced off S, moves to TAB (the native 8-direction look-up).
//     LOOK-UP, displaced off TAB, starts UNBOUND and can be assigned with
//     REMAP on the player screen. That is the exact precedent of P4's 8-dir
//     look-up: when an 8-direction cluster consumes the keys around it,
//     look-up is the slot that yields.
//
// The native kDefault*DirKeys tables (input_state.cpp) are the source of
// truth; web builds substitute at the read sites via
// browser_safe_default_key().
//
// Persisted-state migration: the engine writes the whole controls block back
// to config on every boot, so every returning player has old factory values
// frozen in persisted storage. ONE version-keyed migration moves them, native
// and web alike (og::input::migrate_seat_keys, input_mappings.h; marker
// controls/default_keys_version). The web scheme that shipped in August-
// September 2026 (this header from 07b4a7dc, 2026-08-03; per-commit releases
// from v2.1073, 2026-09-12) put P1's 4-direction SPECIAL on X and its
// 8-direction YELL on V; the kLegacy* values below are the stored values that
// migration counts as "still the factory key" on top of the native legacy
// tables. The old migration counter controls/web_default_keys_version is no
// longer read or written.
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
inline constexpr int kWebKeySlotFire = 8;     // KEY_FIRE
inline constexpr int kWebKeySlotSpecial = 9;  // KEY_SPECIAL (legacy web X)
inline constexpr int kWebKeySlotYell = 12;    // KEY_YELL
inline constexpr int kWebKeySlotLookup = 16;  // KEY_LOOKUP
inline constexpr int kWebModeFourIndex = 0;   // 4-direction key map
inline constexpr int kWebModeEightIndex = 1;  // 8-direction key map

// The substituted values.
inline constexpr int kWebFourDirFireKey = 122;      // KEYCODE_z
inline constexpr int kWebEightDirFireKey = 115;     // KEYCODE_s
inline constexpr int kWebEightDirYellKey = 9;       // KEYCODE_TAB
inline constexpr int kWebEightDirLookupKey = 0;     // KEYCODE_UNKNOWN

// Stored values the one-time migration (migrate_seat_keys) may rewrite, on
// top of the native legacy tables: the native P1 FIRE/SPECIAL the browser
// scheme displaced, and the web scheme's own 4-direction SPECIAL and
// 8-direction YELL. (The web scheme's 4-direction FIRE z, 8-direction FIRE s
// and unbound 8-direction LOOK-UP are this header's current values.)
inline constexpr int kLegacyNativeFireKey = 1073742048;     // KEYCODE_LCTRL
inline constexpr int kLegacyNativeSpecialKey = 1073742050;  // KEYCODE_LALT
inline constexpr int kLegacyWebFourDirSpecialKey = 120;     // KEYCODE_x
inline constexpr int kLegacyWebEightDirYellKey = 118;       // KEYCODE_v

// True when this build ships the browser-safe defaults.
inline constexpr bool kWebControlDefaults =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

// Substitute a browser-safe key for profile 0's defaults in web mode;
// everything else (native mode, profiles 1-3, every unlisted key slot)
// passes the native default through untouched.
constexpr int browser_safe_default_key(int profile, int mode_index,
                                       int key_enum, int native_default,
                                       bool web_mode)
{
    if (!web_mode || profile != 0)
        return native_default;
    const bool eight_dir = (mode_index == kWebModeEightIndex);
    if (key_enum == kWebKeySlotFire)
        return eight_dir ? kWebEightDirFireKey : kWebFourDirFireKey;
    // Knock-on moves from FIRE taking S in the 8-direction map.
    if (key_enum == kWebKeySlotYell && eight_dir)
        return kWebEightDirYellKey;
    if (key_enum == kWebKeySlotLookup && eight_dir)
        return kWebEightDirLookupKey;
    return native_default;
}

// The browser scheme's value for a profile-0 slot as it shipped before New
// Ability Select, or `fallback` when the scheme left that slot native. The
// migration counts a stored value equal to this as a factory key.
constexpr int legacy_web_default_key(int mode_index, int key_enum, int fallback)
{
    const bool eight_dir = (mode_index == kWebModeEightIndex);
    if (key_enum == kWebKeySlotFire)
        return eight_dir ? kWebEightDirFireKey : kWebFourDirFireKey;
    if (key_enum == kWebKeySlotSpecial && !eight_dir)
        return kLegacyWebFourDirSpecialKey;
    if (key_enum == kWebKeySlotYell && eight_dir)
        return kLegacyWebEightDirYellKey;
    if (key_enum == kWebKeySlotLookup && eight_dir)
        return kWebEightDirLookupKey;
    return fallback;
}

} // namespace og::input
