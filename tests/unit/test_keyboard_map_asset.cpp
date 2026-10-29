/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The keyboard picture (og::ui::keyboard_picture): the one owner of the
// keyboard's layout and its drawing, behind KEYMAP and the CHANGELOG! page.
// The layout keeps the approved picture's grid, every factory key lands on a
// cap, a cap shows its player's colour and the action the binding panels
// name, two players on one cap are both shown, and the hit test finds the
// cap under a pixel. The glyph atlas the labels come from is guarded at
// build time by scripts/check_keyboard_glyphs.sh.

#include <gtest/gtest.h>

#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/ui/keyboard_picture.h>
#include <openglad/interface/web_control_defaults.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

namespace
{

namespace kp = og::ui::keyboard_picture;

std::array<int, 3> pixel(const og::io::RgbaImage& image, int x, int y)
{
    const std::size_t at =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.w) +
         static_cast<std::size_t>(x)) *
        4u;
    return {image.bytes[at], image.bytes[at + 1], image.bytes[at + 2]};
}

std::array<int, 3> hex(std::uint32_t colour)
{
    return {static_cast<int>((colour >> 16) & 0xFF),
            static_cast<int>((colour >> 8) & 0xFF),
            static_cast<int>(colour & 0xFF)};
}

const kp::DrawnCap& drawn_for(const kp::Output& out, int keycode)
{
    const kp::Cap* cap = kp::cap_for_keycode(keycode);
    EXPECT_NE(nullptr, cap) << keycode;
    for (const kp::DrawnCap& drawn : out.drawn)
        if (drawn.cap == cap)
            return drawn;
    static const kp::DrawnCap none{};
    ADD_FAILURE() << "no drawn cap for " << keycode;
    return none;
}

void write_ppm(const og::io::RgbaImage& image, const std::string& path)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out) << "cannot write " << path;
    out << "P6\n" << image.w << " " << image.h << "\n255\n";
    for (std::size_t i = 0; i < image.bytes.size(); i += 4)
        out.write(reinterpret_cast<const char*>(&image.bytes[i]), 3);
}

// The factory keys of the four seats in one direction mode.
kp::Input factory(bool web, bool eight)
{
    const int mode = eight ? og::input::kWebModeEightIndex
                           : og::input::kWebModeFourIndex;
    kp::Input input;
    input.web = web;
    for (int profile = 0; profile < 4; ++profile) {
        kp::Seat seat;
        seat.player_number = profile + 1;
        seat.eight_direction = eight;
        for (int slot = 0; slot < NUM_KEYS; ++slot)
            seat.keys[static_cast<std::size_t>(slot)] =
                og::input::browser_safe_default_key(
                    profile, mode, slot,
                    og::input::factory_default_key(profile, mode, slot), web);
        input.seats.push_back(seat);
    }
    return input;
}

} // namespace

TEST(KeyboardPicture, every_factory_key_of_every_seat_lands_on_a_cap_and_no_cap_is_shared)
{
    for (const bool web : {false, true}) {
        SCOPED_TRACE(web ? "web" : "desktop");
        const kp::Output out = kp::render(kp::factory_input(web));
        // The CHANGELOG! pictures at their own size, for the side-by-side
        // with the approved picture (OG_KEYBOARD_PICTURE_DIR, a directory).
        if (const char* dir = std::getenv("OG_KEYBOARD_PICTURE_DIR"))
            write_ppm(out.picture, std::string(dir) +
                                       (web ? "/keyboard_web.ppm"
                                            : "/keyboard_desktop.ppm"));
        for (const kp::OffPicture& off : out.off_picture)
            ADD_FAILURE() << "P" << off.player_number << " " << off.action
                          << " on keycode " << off.keycode
                          << " has no cap on the picture";
        for (const kp::SharedCap& shared : out.shared)
            ADD_FAILURE() << shared.cap->id << " is used by "
                          << shared.player_numbers.size() << " players";
        // Every factory key of every seat is drawn (none silently dropped):
        // 4 seats x 13 slots (4 directions, 8 named actions, CHEAT), plus
        // PAUSE.
        std::size_t uses = 0;
        for (const kp::DrawnCap& drawn : out.drawn)
            uses += drawn.player_numbers.size();
        EXPECT_EQ(4u * 13u + 1u, uses);
        EXPECT_TRUE(out.labels) << "the glyph atlas did not load";
        EXPECT_EQ(kp::kWidth, out.picture.w);
        EXPECT_EQ(kp::kHeight, out.picture.h);
        EXPECT_NE(0u, out.picture.serial);
    }
    // The web build moves player 1's FIRE to Z and PAUSE to Backspace.
    EXPECT_EQ(1, drawn_for(kp::render(kp::factory_input(true)), KEYCODE_z)
                     .player_numbers.at(0));
    EXPECT_EQ(0, drawn_for(kp::render(kp::factory_input(true)),
                           KEYCODE_BACKSPACE)
                     .player_numbers.at(0));
    EXPECT_EQ(0, drawn_for(kp::render(kp::factory_input(false)),
                           KEYCODE_ESCAPE)
                     .player_numbers.at(0));
}

TEST(KeyboardPicture, eight_direction_factory_keys_draw_the_diagonals)
{
    for (const bool web : {false, true}) {
        SCOPED_TRACE(web ? "web" : "desktop");
        const kp::Output out = kp::render(factory(web, true));
        std::size_t diagonals = 0;
        for (const kp::DrawnCap& drawn : out.drawn)
            if (drawn.action.find("UP-") != std::string::npos ||
                drawn.action.find("DN-") != std::string::npos)
                ++diagonals;
        // Every diagonal a seat binds is shown, on its cap or, off the
        // picture (the numpad), in the list.
        std::size_t off_diagonals = 0;
        for (const kp::OffPicture& off : out.off_picture)
            if (std::string(off.action).find('-') != std::string::npos)
                ++off_diagonals;
        std::size_t bound = 0;
        for (const kp::Seat& seat : factory(web, true).seats)
            for (const int slot :
                 {KEY_UP_RIGHT, KEY_DOWN_RIGHT, KEY_DOWN_LEFT, KEY_UP_LEFT})
                if (seat.keys[static_cast<std::size_t>(slot)] != KEYCODE_UNKNOWN)
                    ++bound;
        EXPECT_GT(bound, 0u);
        EXPECT_EQ(bound, diagonals + off_diagonals);
        // And a 4-direction seat shows none.
        for (const kp::DrawnCap& drawn : kp::render(factory(web, false)).drawn)
            EXPECT_EQ(std::string::npos, drawn.action.find('-'))
                << drawn.cap->id;
    }
}

TEST(KeyboardPicture, a_bound_cap_shows_its_players_colour_and_a_free_cap_is_grey)
{
    const kp::Output out = kp::render(kp::factory_input(false));
    for (const kp::DrawnCap& drawn : out.drawn) {
        const kp::CapRect r = kp::cap_rect(*drawn.cap);
        // The cap's centre sits between the name and the action label.
        const auto at = pixel(out.picture,
                              static_cast<int>(r.x + r.w / 2.0f),
                              static_cast<int>(r.y + r.h / 2.0f));
        const std::uint32_t want =
            drawn.player_numbers.empty()
                ? kp::kFreeCapColour
                : kp::player_colour(drawn.player_numbers.front());
        EXPECT_EQ(hex(want), at) << drawn.cap->id;
    }
    EXPECT_EQ(hex(0xe5484d), hex(kp::player_colour(1)));
    EXPECT_EQ(hex(0x3e8ef7), hex(kp::player_colour(2)));
    EXPECT_EQ(hex(0x30a46c), hex(kp::player_colour(3)));
    EXPECT_EQ(hex(0xf5b400), hex(kp::player_colour(4)));
    EXPECT_EQ(hex(kp::kSharedKeyColour), hex(kp::player_colour(0)));
}

TEST(KeyboardPicture, the_action_label_follows_new_ability_select)
{
    ASSERT_TRUE(kp::glyph_atlas_loaded());
    kp::Input on = kp::factory_input(false);
    on.ability_select = true;
    kp::Input off = on;
    off.ability_select = false;
    const kp::Output with = kp::render(on);
    const kp::Output without = kp::render(off);
    // Player 1's SPEC 1 key ('1'): its words, and the ink they leave.
    const kp::DrawnCap& spec1 = drawn_for(with, KEYCODE_1);
    const kp::DrawnCap& special = drawn_for(without, KEYCODE_1);
    EXPECT_EQ("SPEC 1", spec1.action);
    EXPECT_EQ("SPECIAL", special.action);
    // Setting off, SPEC 3 does nothing: its key is a free cap.
    EXPECT_TRUE(drawn_for(without, KEYCODE_3).player_numbers.empty());
    EXPECT_EQ("SPEC 3", drawn_for(with, KEYCODE_3).action);

    // The label's ink: the near-black pixels in the bottom band of the cap,
    // centred, as wide as the atlas says the words are.
    for (const auto* pair : {&spec1, &special}) {
        const kp::Output& out = pair == &spec1 ? with : without;
        const kp::CapRect r = kp::cap_rect(*pair->cap);
        int left = out.picture.w;
        int right = -1;
        for (int y = static_cast<int>(r.y + r.h - 30);
             y < static_cast<int>(r.y + r.h - 6); ++y)
            for (int x = static_cast<int>(r.x) + 2;
                 x < static_cast<int>(r.x + r.w) - 2; ++x) {
                const auto c = pixel(out.picture, x, y);
                if (c[0] < 90 && c[1] < 90 && c[2] < 90) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
            }
        ASSERT_GE(right, left) << pair->action << ": no ink on the cap";
        const float width =
            kp::action_label_width(pair->action, pair->action_size);
        ASSERT_GT(width, 0.0f) << pair->action;
        EXPECT_NEAR(width, static_cast<float>(right - left + 1), 3.0f)
            << pair->action;
        EXPECT_NEAR(r.x + r.w / 2.0f, static_cast<float>(left + right + 1) / 2.0f, 2.0f)
            << pair->action << " is not centred";
        EXPECT_LE(width, r.w - 10.0f) << pair->action << " overflows the cap";
    }
    // The approved picture's sizes: 21 where it fits, smaller on a narrow
    // cap with a long word.
    EXPECT_FLOAT_EQ(21.0f, drawn_for(with, KEYCODE_w).action_size);
    EXPECT_FLOAT_EQ(18.0f, spec1.action_size);
    // LOOK UP on a one-unit cap: the old picture's 15.7, less what the
    // real width needs.
    EXPECT_GE(drawn_for(with, KEYCODE_q).action_size, 15.0f);
    EXPECT_LE(drawn_for(with, KEYCODE_q).action_size, 15.5f);
}

TEST(KeyboardPicture, two_players_on_one_cap_are_both_shown_and_listed)
{
    kp::Input input = kp::factory_input(false);
    // P1 also fires with Space, P3's FIRE; P2 looks up with keypad 5, which
    // has no cap.
    input.seats[0].keys[KEY_FIRE] = KEYCODE_SPACE;
    input.seats[1].keys[KEY_LOOKUP] = KEYCODE_KP_5;
    const kp::Output out = kp::render(input);
    ASSERT_EQ(1u, out.shared.size());
    EXPECT_STREQ("Space", out.shared[0].cap->id);
    ASSERT_EQ(2u, out.shared[0].uses.size());
    EXPECT_EQ(1, out.shared[0].uses[0].player_number);
    EXPECT_EQ(3, out.shared[0].uses[1].player_number);
    ASSERT_EQ(1u, out.off_picture.size());
    EXPECT_EQ(2, out.off_picture[0].player_number);
    EXPECT_STREQ("LOOK UP", out.off_picture[0].action);
    EXPECT_EQ(KEYCODE_KP_5, out.off_picture[0].keycode);
    // Split in both colours, left to right in seat order.
    const kp::CapRect r = kp::cap_rect(*out.shared[0].cap);
    const int y = static_cast<int>(r.y + r.h / 2.0f);
    EXPECT_EQ(hex(kp::player_colour(1)),
              pixel(out.picture, static_cast<int>(r.x + r.w * 0.25f), y));
    EXPECT_EQ(hex(kp::player_colour(3)),
              pixel(out.picture, static_cast<int>(r.x + r.w * 0.75f), y));
    // A player on the PAUSE key shares it with everyone.
    input = kp::factory_input(false);
    input.seats[3].keys[KEY_YELL] = KEYCODE_ESCAPE;
    const kp::Output pause = kp::render(input);
    ASSERT_EQ(1u, pause.shared.size());
    EXPECT_EQ(0, pause.shared[0].uses[0].player_number);
    EXPECT_STREQ(kp::kPauseAction, pause.shared[0].uses[0].action);
    // A seat on a pad draws nothing.
    input = kp::factory_input(false);
    input.seats[0].on_keyboard = false;
    EXPECT_TRUE(drawn_for(kp::render(input), KEYCODE_w).player_numbers.empty());
}

TEST(KeyboardPicture, cap_at_finds_every_cap_and_nothing_in_a_gap)
{
    for (const kp::Cap& cap : kp::caps()) {
        const kp::CapRect r = kp::cap_rect(cap);
        EXPECT_EQ(&cap, kp::cap_at(static_cast<int>(r.x + r.w / 2.0f),
                                   static_cast<int>(r.y + r.h / 2.0f)))
            << cap.id;
        EXPECT_EQ(&cap, kp::cap_for_keycode(cap.keycode)) << cap.id;
        EXPECT_GE(r.x, 0.0f) << cap.id;
        EXPECT_LE(r.x + r.w, static_cast<float>(kp::kWidth)) << cap.id;
        EXPECT_LE(r.y + r.h, static_cast<float>(kp::kHeight)) << cap.id;
    }
    // The gap between Q and W, the corner of the picture, the blank above
    // the arrows.
    const kp::CapRect q = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_q));
    EXPECT_EQ(nullptr, kp::cap_at(static_cast<int>(q.x + q.w + 1.5f),
                                  static_cast<int>(q.y + q.h / 2.0f)));
    EXPECT_EQ(nullptr, kp::cap_at(1, 1));
    const kp::CapRect up = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_UP));
    EXPECT_EQ(nullptr, kp::cap_at(static_cast<int>(up.x + up.w / 2.0f),
                                  static_cast<int>(up.y - kp::kUnit / 2.0f)));
    EXPECT_EQ(nullptr, kp::cap_for_keycode(KEYCODE_KP_5));
    EXPECT_EQ(nullptr, kp::cap_for_keycode(KEYCODE_UNKNOWN));
}

TEST(KeyboardPicture, the_grid_is_the_approved_pictures)
{
    // make_keyboard_map.py's grid, which the picture keeps: 18.3 units
    // centred across 1560 pixels, 6.25 rows down 550, 83 a unit, 5 between.
    const kp::CapRect esc = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_ESCAPE));
    EXPECT_NEAR(23.05f, esc.x, 0.01f);
    EXPECT_NEAR(18.125f, esc.y, 0.01f);
    EXPECT_FLOAT_EQ(78.0f, esc.w);
    EXPECT_FLOAT_EQ(78.0f, esc.h);
    const kp::CapRect space = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_SPACE));
    EXPECT_FLOAT_EQ(6.25f * 83.0f - 5.0f, space.w);
    EXPECT_NEAR(esc.y + 5.25f * 83.0f, space.y, 0.01f);
    // The side column: Delete under Insert, the arrows under the
    // navigation block, all right of the main block's 15 units.
    const kp::CapRect ins = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_INSERT));
    const kp::CapRect del = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_DELETE));
    const kp::CapRect left = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_LEFT));
    const kp::CapRect up = kp::cap_rect(*kp::cap_for_keycode(KEYCODE_UP));
    EXPECT_NEAR(esc.x + 15.3f * 83.0f, ins.x, 0.01f);
    EXPECT_FLOAT_EQ(ins.x, del.x);
    EXPECT_FLOAT_EQ(ins.x, left.x);
    EXPECT_NEAR(ins.y + 83.0f, del.y, 0.01f);
    EXPECT_NEAR(left.x + 83.0f, up.x, 0.01f);
    EXPECT_NEAR(space.y, left.y, 0.01f);
    EXPECT_EQ(84u, kp::caps().size());
}
