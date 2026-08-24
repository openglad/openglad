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
// Ctrl is the whole problem: SPECIAL stays on Left Alt. A
// page cannot reach Ctrl+W, but it can reach Alt — Firefox activates its menu
// bar on an unconsumed Alt keyup, and the game page suppresses that with a
// capture-phase preventDefault (web/shell.html), which leaves the key itself
// fully readable by SDL. So both FIRE defaults move, SPECIAL follows only in
// the 4-direction map (to keep it paired with fire next to WASD), and
// everything else that moves is a knock-on from the key FIRE lands on.
//
//   4-direction map: FIRE = Z, SPECIAL = X (both free next to WASD).
//
//   8-direction map: FIRE = S, SPECIAL = Left Alt (unchanged from native).
//     Z and X are P1's own diagonals in this map, so fire lands on S, the
//     center of the W/E/D/C/X/Z/A/Q movement diamond. S is collision-free on
//     a shared keyboard: no OTHER player binds it in either mode. It is
//     already P1's own 8-dir YELL and P1's own 4-dir DOWN, but a player runs
//     one mode at a time and the two maps are never co-active, so neither
//     self-overlap can fire.
//     YELL, displaced off S, moves to V. V is P1's native look-up in both
//     maps and overlaps P4's 8-dir DOWN-LEFT — the same grandfathered
//     overlap input_state.cpp already documents for native, now an
//     occasional tap instead of a hold.
//     LOOK-UP, displaced off V, starts UNBOUND and can be assigned with
//     REMAP on the player screen. That is the exact precedent of P4's 8-dir
//     look-up (see the comment above kDefaultFourDirKeys): when an
//     8-direction cluster consumes the keys around it, look-up is the slot
//     that yields.
//
// The native kDefault*DirKeys tables are the unchanged source of truth;
// web builds substitute at the read sites via browser_safe_default_key().
//
// Persisted-state migration: the engine writes the whole controls block back
// to config on every boot, so every existing web player has the old factory
// values frozen in persisted storage. migrate_persisted_control_key()
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
inline constexpr int kWebKeySlotFire = 8;     // KEY_FIRE
inline constexpr int kWebKeySlotSpecial = 9;  // KEY_SPECIAL
inline constexpr int kWebKeySlotYell = 12;    // KEY_YELL
inline constexpr int kWebKeySlotLookup = 16;  // KEY_LOOKUP
inline constexpr int kWebModeFourIndex = 0;   // 4-direction key map
inline constexpr int kWebModeEightIndex = 1;  // 8-direction key map

// The substituted values.
inline constexpr int kWebFourDirFireKey = 122;      // KEYCODE_z
inline constexpr int kWebFourDirSpecialKey = 120;   // KEYCODE_x
inline constexpr int kWebEightDirFireKey = 115;     // KEYCODE_s
inline constexpr int kWebEightDirYellKey = 118;     // KEYCODE_v
inline constexpr int kWebEightDirLookupKey = 0;     // KEYCODE_UNKNOWN

// The native factory defaults they displace — the only stored values the
// one-shot migration is allowed to rewrite. (8-dir SPECIAL is absent on
// purpose: Left Alt stays Left Alt on web.)
inline constexpr int kWebLegacyFireKey = 1073742048;       // KEYCODE_LCTRL
inline constexpr int kWebLegacySpecialKey = 1073742050;    // KEYCODE_LALT
inline constexpr int kWebLegacyEightDirYellKey = 115;      // KEYCODE_s
inline constexpr int kWebLegacyEightDirLookupKey = 118;    // KEYCODE_v

// True when this build ships the browser-safe defaults.
inline constexpr bool kWebControlDefaults =
#ifdef __EMSCRIPTEN__
    true;
#else
    false;
#endif

// Bump when the web defaults change again; stored persistently as
// controls/web_default_keys_version so the migration runs exactly once.
// Still 1: the v1 migration never reached a released build, so the scheme it
// migrates to can still be edited in place.
inline constexpr int kWebControlDefaultsVersion = 1;

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
    // 8-direction SPECIAL keeps the native Left Alt; only the 4-direction
    // map moves it (to keep Z/X together next to WASD).
    if (key_enum == kWebKeySlotSpecial)
        return eight_dir ? native_default : kWebFourDirSpecialKey;
    // Knock-on moves from FIRE taking S in the 8-direction map.
    if (key_enum == kWebKeySlotYell && eight_dir)
        return kWebEightDirYellKey;
    if (key_enum == kWebKeySlotLookup && eight_dir)
        return kWebEightDirLookupKey;
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
