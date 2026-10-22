/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The CHANGELOG! keyboard pictures cannot drift from the key tables
// (docs/ability-select-design.md F.4). The chain has three links:
//   1. this test: the text built from the live default key tables, desktop
//      and web, equals the committed scripts/keyboard_map/keyboard_map.txt;
//   2. and 3. scripts/check_keyboard_map.sh (a build-time check): the text
//      file and both pictures hash to what keyboard_map.stamp says, and only
//      make_keyboard_map.py --write, which draws the pictures FROM the text
//      file, rewrites the stamp.
// After a key change: OG_KEYBOARD_MAP_WRITE=1 og_unit_core
// --gtest_filter='KeyboardMapAsset.*' rewrites the text file, then
// make_keyboard_map.py --write redraws the pictures.

#include <gtest/gtest.h>

#include <openglad/interface/input.h>
#include <openglad/interface/input_mappings.h>
#include <openglad/interface/web_control_defaults.h>
#include <openglad/interface/ui/changelog_screen.h>
#include <openglad/resources/og_file.h>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>

namespace
{

constexpr const char* kMapPath = "scripts/keyboard_map/keyboard_map.txt";

// The action each key slot shows on the picture; nullptr for the slots the
// 4-direction map never binds (the diagonals and the two reserved slots).
constexpr std::array<const char*, NUM_KEYS> kSlotActions = {
    "UP", nullptr, "RIGHT", nullptr, "DOWN", nullptr, "LEFT", nullptr,
    "FIRE", "SPEC_1", "SWITCH", nullptr, "YELL", "SHIFTER", nullptr,
    "CHEAT", "LOOK_UP", "SPEC_2", "SPEC_3",
};
static_assert(KEY_SPECIAL == 9 && KEY_YELL == 12 && KEY_LOOKUP == 16 &&
                  KEY_SPEC3 == 18 && NUM_KEYS == 19,
              "kSlotActions follows the key slot numbers");

// The token make_keyboard_map.py reads for a keycode: the character itself
// for printable keys, a name for the rest.
std::string key_token(int keycode)
{
    struct Named { int code; const char* name; };
    static constexpr Named kNamed[] = {
        {KEYCODE_SPACE, "SPACE"},   {KEYCODE_RETURN, "RETURN"},
        {KEYCODE_TAB, "TAB"},       {KEYCODE_BACKSPACE, "BACKSPACE"},
        {KEYCODE_ESCAPE, "ESCAPE"}, {KEYCODE_LSHIFT, "LSHIFT"},
        {KEYCODE_RSHIFT, "RSHIFT"}, {KEYCODE_LCTRL, "LCTRL"},
        {KEYCODE_RCTRL, "RCTRL"},   {KEYCODE_LALT, "LALT"},
        {KEYCODE_RALT, "RALT"},     {KEYCODE_UP, "UP"},
        {KEYCODE_DOWN, "DOWN"},     {KEYCODE_LEFT, "LEFT"},
        {KEYCODE_RIGHT, "RIGHT"},   {KEYCODE_F1, "F1"},
        {KEYCODE_F2, "F2"},         {KEYCODE_F3, "F3"},
        {KEYCODE_F4, "F4"},         {KEYCODE_F5, "F5"},
        {KEYCODE_F6, "F6"},         {KEYCODE_F7, "F7"},
        {KEYCODE_F8, "F8"},         {KEYCODE_F9, "F9"},
        {KEYCODE_F10, "F10"},       {KEYCODE_F11, "F11"},
        {KEYCODE_F12, "F12"},
    };
    for (const Named& named : kNamed)
        if (named.code == keycode)
            return named.name;
    if (keycode > KEYCODE_SPACE && keycode < KEYCODE_DELETE)
        return std::string(1, static_cast<char>(keycode));
    // A key the picture has no cap for: the text then differs from the
    // committed file, and the generator would refuse it too.
    return "?" + std::to_string(keycode);
}

// The text form of the default 4-direction tables, desktop then web.
std::string keyboard_map_from_tables()
{
    std::string text =
        "# The default 4-direction keys on pix/changelog/keyboard_belt*.png.\n"
        "# Written from the game's key tables by "
        "tests/unit/test_keyboard_map_asset.cpp;\n"
        "# scripts/keyboard_map/make_keyboard_map.py draws the pictures from "
        "this file.\n";
    for (const bool web : {false, true}) {
        for (int profile = 0; profile < 4; ++profile) {
            for (int slot = 0; slot < NUM_KEYS; ++slot) {
                const int key = og::input::browser_safe_default_key(
                    profile, og::input::kWebModeFourIndex, slot,
                    og::input::factory_default_key(
                        profile, og::input::kWebModeFourIndex, slot),
                    web);
                if (key == KEYCODE_UNKNOWN)
                    continue;
                const char* action = kSlotActions[static_cast<std::size_t>(slot)];
                // A slot the picture has no label for, newly bound: name it
                // so the diff says which.
                const std::string action_text = action != nullptr
                    ? std::string(action)
                    : "SLOT" + std::to_string(slot);
                text += std::string(web ? "web" : "desktop") + " P" +
                        std::to_string(profile + 1) + " " + action_text + " " +
                        key_token(key) + "\n";
            }
        }
    }
    return text;
}

std::optional<std::string> read_file(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST(KeyboardMapAsset, text_built_from_the_key_tables_matches_the_committed_file)
{
    const std::string expected = keyboard_map_from_tables();
    if (std::getenv("OG_KEYBOARD_MAP_WRITE") != nullptr) {
        std::ofstream out(kMapPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out) << "cannot write " << kMapPath;
        out << expected;
    }
    const std::optional<std::string> committed = read_file(kMapPath);
    ASSERT_TRUE(committed.has_value())
        << kMapPath << " is missing (run from the repo root)";
    EXPECT_EQ(expected, *committed)
        << "The default keys changed and the CHANGELOG! keyboard picture still "
           "shows the old ones. Rewrite the text with OG_KEYBOARD_MAP_WRITE=1 "
           "og_unit_core --gtest_filter='KeyboardMapAsset.*', then redraw: "
           "nix develop -c python3 scripts/keyboard_map/make_keyboard_map.py "
           "--write";
}

TEST(KeyboardMapAsset, every_bound_key_has_a_cap_on_the_picture)
{
    const std::string text = keyboard_map_from_tables();
    EXPECT_EQ(std::string::npos, text.find(" ?"))
        << "a default key has no cap on the picture:\n" << text;
    EXPECT_EQ(std::string::npos, text.find(" SLOT"))
        << "a default 4-direction key sits on a slot the picture has no "
           "label for:\n" << text;
    // The web picture differs from the desktop one exactly where the web
    // build substitutes a key (player 1's FIRE, Z).
    EXPECT_NE(std::string::npos, text.find("desktop P1 FIRE LCTRL\n"));
    EXPECT_NE(std::string::npos, text.find("web P1 FIRE z\n"));
    EXPECT_EQ("?0", key_token(KEYCODE_UNKNOWN));
}

TEST(KeyboardMapAsset, both_pictures_decode_as_rgba_at_the_band_aspect)
{
    for (const og::ui::ChangelogEntry& entry : og::ui::kChangelog) {
        for (const bool web : {false, true}) {
            const char* path = og::ui::changelog_entry_image(entry, web);
            ASSERT_NE(nullptr, path);
            const std::optional<og::io::RgbaImage> image =
                og::io::read_rgba_png(path);
            ASSERT_TRUE(image.has_value()) << path << " did not decode";
            EXPECT_EQ(1560, image->w) << path;
            EXPECT_EQ(550, image->h) << path;
            EXPECT_EQ(static_cast<std::size_t>(image->w) *
                          static_cast<std::size_t>(image->h) * 4u,
                      image->bytes.size())
                << path;
        }
    }
    EXPECT_STRNE(og::ui::changelog_entry_image(og::ui::kChangelog[0], false),
                 og::ui::changelog_entry_image(og::ui::kChangelog[0], true))
        << "the web build shows its own picture (player 1 attacks with Z)";
}
