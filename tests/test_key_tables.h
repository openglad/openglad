#pragma once

// Literal key tables for the key-binding tests: the factory keys New Ability
// Select ships (DESIGN C.2, "Proposal 1" and its 8-direction variant) and the
// factory keys as they stood at d758bcb1 (before it). Written out by hand on
// purpose: the tests compare the live tables (input_state.cpp) against these,
// so editing a table without editing its pin fails loudly.
//
// Slot order: UP UR RIGHT DR DOWN DL LEFT UL FIRE SPECIAL/SPEC1 SWITCH
// (11 change special) YELL SHIFTER (14) CHEAT LOOKUP [SPEC2 SPEC3].

#include <openglad/interface/input.h>

namespace og::test_keys
{

inline constexpr int U = KEYCODE_UNKNOWN;

// --- New Ability Select factory tables (19 slots) --------------------------
inline constexpr int kNewFourDir[4][19] = {
    {KEYCODE_w, U, KEYCODE_d, U, KEYCODE_s, U, KEYCODE_a, U,
     KEYCODE_LCTRL, KEYCODE_1, KEYCODE_BACKQUOTE, U, KEYCODE_e, KEYCODE_LSHIFT, U,
     KEYCODE_F5, KEYCODE_q, KEYCODE_2, KEYCODE_3},
    {KEYCODE_UP, U, KEYCODE_RIGHT, U, KEYCODE_DOWN, U, KEYCODE_LEFT, U,
     KEYCODE_RSHIFT, KEYCODE_LEFTBRACKET, KEYCODE_QUOTE, U, KEYCODE_RETURN, KEYCODE_SLASH, U,
     KEYCODE_F6, KEYCODE_RCTRL, KEYCODE_RIGHTBRACKET, KEYCODE_BACKSLASH},
    {KEYCODE_i, U, KEYCODE_l, U, KEYCODE_k, U, KEYCODE_j, U,
     KEYCODE_SPACE, KEYCODE_8, KEYCODE_MINUS, U, KEYCODE_o, KEYCODE_m, U,
     KEYCODE_F7, KEYCODE_u, KEYCODE_9, KEYCODE_0},
    {KEYCODE_t, U, KEYCODE_h, U, KEYCODE_g, U, KEYCODE_f, U,
     KEYCODE_v, KEYCODE_4, KEYCODE_n, U, KEYCODE_y, KEYCODE_b, U,
     KEYCODE_F8, KEYCODE_r, KEYCODE_5, KEYCODE_6},
};
inline constexpr int kNewEightDir[4][19] = {
    {KEYCODE_w, KEYCODE_e, KEYCODE_d, KEYCODE_c, KEYCODE_x, KEYCODE_z, KEYCODE_a, KEYCODE_q,
     KEYCODE_LCTRL, KEYCODE_1, KEYCODE_BACKQUOTE, U, KEYCODE_s, KEYCODE_LSHIFT, U,
     KEYCODE_F5, KEYCODE_TAB, KEYCODE_2, KEYCODE_3},
    {KEYCODE_UP, U, KEYCODE_RIGHT, U, KEYCODE_DOWN, U, KEYCODE_LEFT, U,
     KEYCODE_RSHIFT, KEYCODE_LEFTBRACKET, KEYCODE_QUOTE, U, KEYCODE_RETURN, KEYCODE_SLASH, U,
     KEYCODE_F6, KEYCODE_RCTRL, KEYCODE_RIGHTBRACKET, KEYCODE_BACKSLASH},
    {KEYCODE_i, KEYCODE_o, KEYCODE_l, KEYCODE_PERIOD, KEYCODE_COMMA, KEYCODE_m, KEYCODE_j, KEYCODE_u,
     KEYCODE_SPACE, KEYCODE_8, KEYCODE_MINUS, U, KEYCODE_k, KEYCODE_SEMICOLON, U,
     KEYCODE_F7, KEYCODE_p, KEYCODE_9, KEYCODE_0},
    {KEYCODE_t, KEYCODE_y, KEYCODE_h, KEYCODE_n, KEYCODE_b, KEYCODE_v, KEYCODE_f, KEYCODE_r,
     KEYCODE_g, KEYCODE_4, KEYCODE_EQUALS, U, KEYCODE_7, KEYCODE_LALT, U,
     KEYCODE_F8, U, KEYCODE_5, KEYCODE_6},
};
// Web P1 (profile 0) differs only here: 4-dir FIRE z; 8-dir FIRE s, YELL Tab,
// look-up unbound.
inline constexpr int kWebFourDirFire = KEYCODE_z;
inline constexpr int kWebEightDirFire = KEYCODE_s;
inline constexpr int kWebEightDirYell = KEYCODE_TAB;
inline constexpr int kWebEightDirLookup = KEYCODE_UNKNOWN;
// Web P2 (profile 1) differs in one slot, both maps: LOOK UP on Delete, not
// Right Ctrl (#391). No other web default binds Ctrl.
inline constexpr int kWebP2Lookup = KEYCODE_DELETE;

// The factory value of (profile, mode, slot) on this build flavour.
inline constexpr int new_factory_key(int profile, bool eight, int k, bool web)
{
    if (web && profile == 0)
    {
        if (k == KEY_FIRE)
            return eight ? kWebEightDirFire : kWebFourDirFire;
        if (eight && k == KEY_YELL)
            return kWebEightDirYell;
        if (eight && k == KEY_LOOKUP)
            return kWebEightDirLookup;
    }
    if (web && profile == 1 && k == KEY_LOOKUP)
        return kWebP2Lookup;
    return eight ? kNewEightDir[profile][k] : kNewFourDir[profile][k];
}

// --- d758bcb1 factory tables (17 slots) -------------------------------------
inline constexpr int kOldFourDir[4][17] = {
    {KEYCODE_w, U, KEYCODE_d, U, KEYCODE_s, U, KEYCODE_a, U,
     KEYCODE_LCTRL, KEYCODE_LALT, KEYCODE_BACKQUOTE, KEYCODE_TAB, KEYCODE_e, KEYCODE_LSHIFT, U,
     KEYCODE_F5, KEYCODE_v},
    {KEYCODE_UP, U, KEYCODE_RIGHT, U, KEYCODE_DOWN, U, KEYCODE_LEFT, U,
     KEYCODE_PERIOD, KEYCODE_SLASH, KEYCODE_RETURN, KEYCODE_QUOTE, KEYCODE_BACKSLASH, KEYCODE_RSHIFT, U,
     KEYCODE_F6, KEYCODE_RCTRL},
    {KEYCODE_i, U, KEYCODE_l, U, KEYCODE_k, U, KEYCODE_j, U,
     KEYCODE_SPACE, KEYCODE_SEMICOLON, KEYCODE_MINUS, KEYCODE_9, KEYCODE_u, KEYCODE_0, U,
     KEYCODE_F7, KEYCODE_p},
    {KEYCODE_t, U, KEYCODE_h, U, KEYCODE_g, U, KEYCODE_f, U,
     KEYCODE_5, KEYCODE_6, KEYCODE_EQUALS, KEYCODE_7, KEYCODE_y, KEYCODE_8, U,
     KEYCODE_F8, KEYCODE_b},
};
inline constexpr int kOldEightDir[4][17] = {
    {KEYCODE_w, KEYCODE_e, KEYCODE_d, KEYCODE_c, KEYCODE_x, KEYCODE_z, KEYCODE_a, KEYCODE_q,
     KEYCODE_LCTRL, KEYCODE_LALT, KEYCODE_BACKQUOTE, KEYCODE_TAB, KEYCODE_s, KEYCODE_LSHIFT, U,
     KEYCODE_F5, KEYCODE_v},
    {KEYCODE_UP, U, KEYCODE_RIGHT, U, KEYCODE_DOWN, U, KEYCODE_LEFT, U,
     KEYCODE_PERIOD, KEYCODE_SLASH, KEYCODE_RETURN, KEYCODE_QUOTE, KEYCODE_BACKSLASH, KEYCODE_RSHIFT, U,
     KEYCODE_F6, KEYCODE_RCTRL},
    {KEYCODE_i, KEYCODE_o, KEYCODE_l, U, KEYCODE_COMMA, KEYCODE_m, KEYCODE_j, KEYCODE_u,
     KEYCODE_SPACE, KEYCODE_SEMICOLON, KEYCODE_MINUS, KEYCODE_9, KEYCODE_k, KEYCODE_0, U,
     KEYCODE_F7, KEYCODE_p},
    {KEYCODE_t, KEYCODE_y, KEYCODE_h, KEYCODE_n, KEYCODE_b, KEYCODE_v, KEYCODE_f, KEYCODE_r,
     KEYCODE_5, KEYCODE_6, KEYCODE_EQUALS, KEYCODE_7, KEYCODE_g, KEYCODE_8, U,
     KEYCODE_F8, U},
};
// The browser scheme that shipped for profile 0 (web, pre-New Ability
// Select): 4-dir FIRE z, SPECIAL x; 8-dir FIRE s, YELL v, look-up unbound.
inline constexpr int old_web_key(bool eight, int k)
{
    if (k == KEY_FIRE)
        return eight ? KEYCODE_s : KEYCODE_z;
    if (!eight && k == KEY_SPECIAL)
        return KEYCODE_x;
    if (eight && k == KEY_YELL)
        return KEYCODE_v;
    if (eight && k == KEY_LOOKUP)
        return KEYCODE_UNKNOWN;
    return eight ? kOldEightDir[0][k] : kOldFourDir[0][k];
}

} // namespace og::test_keys
