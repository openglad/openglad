/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The keyboard picture: a US keyboard with every key a player uses drawn in
// that player's colour, the key's name top left, the player's tag top right
// and the action along the bottom. One owner for the layout (the caps, their
// geometry, colours and hit test) and for drawing it: the KEYMAP screen draws
// it from the live key tables, the CHANGELOG! page from the factory ones.
//
// The look is the approved "Proposal 1" picture: the labels are DejaVu Sans,
// baked by scripts/keyboard_picture/generate_glyphs.py into the committed
// glyph atlas pix/keyboard/glyphs.png and its table
// (src/interface/ui/keyboard_glyphs.inc); this renderer composes the caps
// and the text from them at run time. SDL-free: it fills an RgbaImage that
// a screen draws through a native plane (screen::draw_rgba_scaled).

#include <openglad/interface/input.h>
#include <openglad/resources/rgba_image.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace og::ui::keyboard_picture {

// The picture's size and grid, in picture pixels: one key unit, the gap
// between two caps. Its aspect is the CHANGELOG! band's (296 x 104 menu
// pixels).
inline constexpr int kWidth = 1560;
inline constexpr int kHeight = 550;
inline constexpr int kUnit = 83;
inline constexpr int kGap = 5;

// One cap: its id, the name it prints (UTF-8; the arrow caps print arrows),
// the keycode (input.h KEYCODE_*) that lights it, and where it sits in key
// units from the top-left cap (the function row is row 0; the rows under it
// start a quarter unit lower).
struct Cap {
    const char* id;
    const char* text;
    int keycode;
    float x_units;
    float y_units;
    float w_units;
};

// Every cap: the six main rows, the navigation block (Ins Home PgUp, Del
// End PgDn) and the arrows.
std::span<const Cap> caps();
// The cap a keycode lights; nullptr for a key the picture has no cap for
// (the numpad, media keys, keys of other layouts).
const Cap* cap_for_keycode(int keycode);
// The cap under picture pixel (px, py); nullptr in a gap or off the keys.
const Cap* cap_at(int px, int py);

// A cap's rectangle in picture pixels (fractional: the grid is centred).
struct CapRect {
    float x, y, w, h;
};
CapRect cap_rect(const Cap& cap);

// The players' colours on the picture (P1 red, P2 blue, P3 green, P4
// yellow; P5 and up take them round again) and the colour of the key every
// player shares (PAUSE), 0xRRGGBB.
std::uint32_t player_colour(int player_number);
inline constexpr std::uint32_t kSharedKeyColour = 0xc9a7ff;
// A cap nobody uses.
inline constexpr std::uint32_t kFreeCapColour = 0x2b2d31;

// One seat on the picture. A seat that plays on a pad (on_keyboard false)
// draws nothing; its keys are ignored.
struct Seat {
    int player_number = 1;
    bool on_keyboard = true;
    std::array<int, NUM_KEYS> keys{};
    bool eight_direction = false;
};

struct Input {
    std::vector<Seat> seats;
    bool web = false;            // PAUSE on Backspace instead of Escape
    bool ability_select = true;  // the New Ability Select action names
};

// A seat's key the picture could not draw, because it has no cap.
struct OffPicture {
    int player_number;
    const char* action;
    int keycode;
};

// One use of a cap: who (0: the PAUSE key every player shares) and what.
struct CapUse {
    int player_number;
    const char* action;
};

// A cap two or more players use (drawn split in their colours), every use
// in binding order, PAUSE first.
struct SharedCap {
    const Cap* cap;
    std::vector<int> player_numbers;
    std::vector<CapUse> uses;
};

// What was drawn on one cap.
struct DrawnCap {
    const Cap* cap;
    std::vector<int> player_numbers;  // empty: free; {0}: the PAUSE key
    std::string action;               // the bottom label, "" on a free cap
    float action_size = 0.0f;         // its DejaVu size in picture pixels
    bool tag = false;                 // the P tag drawn top right
};

struct Output {
    og::io::RgbaImage picture;            // kWidth x kHeight, serial set
    std::vector<OffPicture> off_picture;
    std::vector<SharedCap> shared;
    std::vector<DrawnCap> drawn;          // one per cap, in caps() order
    bool labels = false;  // false: the glyph atlas did not load, no text
};

// The picture for these seats. Every slot the binding panels show
// (og::ui::binding_panel_action_rows) plus movement and CHEAT is drawn
// with its action name; a slot bound to KEYCODE_UNKNOWN draws nothing.
Output render(const Input& input);

// The factory keys of the four seats on this kind of build: what the
// CHANGELOG! page shows (4-direction, New Ability Select names).
Input factory_input(bool web);

// The action names the picture prints for each key slot ("" for a slot it
// never shows): movement, the binding panels' rows, CHEAT.
std::string_view slot_action(int slot, bool ability_select);
// The label on the shared PAUSE cap.
inline constexpr const char* kPauseAction = "PAUSE";

// The width in picture pixels of an action label at `size` (one of the
// atlas's sizes), from the atlas's advances and kerning; -1 when the atlas
// has no such size or the atlas did not load.
float action_label_width(std::string_view text, float size);
// True when the committed glyph atlas loaded at the size its table records.
bool glyph_atlas_loaded();

}  // namespace og::ui::keyboard_picture
