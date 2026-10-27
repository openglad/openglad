/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// KEYMAP (#388): the keyboard picture (og::ui::keyboard_picture) of this
// machine's keyboard seats, drawn from their live keys, with a legend for
// what the picture cannot show: a seat on a pad, a cap two players share, a
// key with no cap. One draw body behind two specs: the Base Camp seat
// screen's (in the registry, MenuScreenId::Keymap, follows the host's GO)
// and the pause player screen's (an overlay that pumps the paused session
// every frame, like the pause menu itself).

#include <openglad/interface/ui/changelog_screen.h>
#include <openglad/interface/ui/keyboard_picture.h>
#include <openglad/interface/ui/menu_screen_spec.h>

#include <cstddef>
#include <string>
#include <vector>

struct button;

namespace og::ui {

// Geometry (menu pixels). The picture band is the CHANGELOG! band's size
// moved up under the title; the legend under it; BACK bottom left.
inline constexpr int kKeymapTitleY = 8;
inline constexpr int kKeymapBandX = kChangelogBandX;
inline constexpr int kKeymapBandY = 24;
inline constexpr int kKeymapBandW = kChangelogBandW;
inline constexpr int kKeymapBandH = kChangelogBandH;
inline constexpr int kKeymapLegendX = kKeymapBandX;
inline constexpr int kKeymapLegendY = kKeymapBandY + kKeymapBandH + 8;
inline constexpr int kKeymapLegendPitch = 8;
inline constexpr std::size_t kKeymapLegendLines = 4;
inline constexpr std::size_t kKeymapMaxGlyphs = 48;
inline constexpr int kKeymapBackX = 10;
inline constexpr int kKeymapBackY = 180;
inline constexpr int kKeymapBackW = 50;
inline constexpr int kKeymapBackH = 15;
static_assert(kKeymapLegendY + kKeymapLegendPitch *
                      static_cast<int>(kKeymapLegendLines - 1) + 6 <
                  kKeymapBackY,
              "the legend ends above BACK");

// One of this machine's seats: its local controller slot (the row of
// input_player_keys()) and the player number it plays as.
struct KeymapSeat {
    int seat = 0;
    int player_number = 1;
};

struct KeymapScreenHost {
    // This machine's seats. Empty: the opening seat alone.
    std::vector<KeymapSeat> seats;
    // Set by the pause player screen: one paused-session pump per frame
    // (transport + keepalive); false = the session ended and the screen
    // closes. Unset: the Base Camp screen, which polls the lobby instead.
    bool (*pump)(void* context) = nullptr;
    void* pump_context = nullptr;
};

// The KEYMAP door on both player screens: shown for every seat but a
// phone's touch seat (the screen is its controller: no keys to picture).
bool keymap_door_shown(int seat);

const MenuScreenSpec& keymap_menu_screen_spec();
const MenuScreenSpec& keymap_pause_menu_screen_spec();

// Open KEYMAP for `seat` (a KeymapSeat::seat of host.seats, which goes first
// in the title). Blocks until BACK (MENU_REDRAW), a host's GO in Base Camp
// (MENU_EXIT), or the paused session ending (MENU_REDRAW with
// *session_ended set).
Sint32 run_keymap_screen(int seat, const KeymapScreenHost& host,
                         bool* session_ended = nullptr);

// --- the screen's words, pure (the draw body and the tests read these) -----

// A seat as the screen describes it.
struct KeymapSeatView {
    int player_number = 1;
    std::string owner;  // "WASD", "ARROWS", "SCRN", the pad's name
    bool on_pad = false;
};

// "KEYS: P1 WASD  P2 ARROWS  P3 IJKL", cut to kKeymapMaxGlyphs.
std::string keymap_title(const std::vector<KeymapSeatView>& seats);

// The legend: a line per pad seat ("P4: GAMEPAD (JOY1), NOT ON THE
// PICTURE"), per shared cap ("P1 FIRE + P2 YELL: SPACE"), per key with no
// cap ("P2 LOOK UP: KEYPAD 5"), and on the web build, when a seat uses
// Delete, how a Mac laptop types it. At most kKeymapLegendLines lines of
// kKeymapMaxGlyphs; the last says how many more did not fit.
std::vector<std::string> keymap_legend_lines(
    const std::vector<KeymapSeatView>& seats,
    const keyboard_picture::Output& picture, bool web,
    std::string (*key_name)(int keycode));

inline constexpr const char* kKeymapMacDeleteLine = "DEL ON A MAC: FN + DELETE";
inline constexpr const char* kKeymapNoPictureLine =
    "No keyboard picture: the key list is on BACK.";

// What the open screen shows now; empty when no KEYMAP is open.
std::string keymap_screen_title();
std::vector<std::string> keymap_screen_legend();
// The open screen's picture (nullptr when none is open).
const keyboard_picture::Output* keymap_screen_picture();

}  // namespace og::ui

// The D3 accessor pair (buttons() fills what count() reads); both specs
// share the one BACK row.
button* picker_keymap_buttons();
int picker_keymap_button_count();
