/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Core-pack presentation seed data: an EXACT transcription of the family
// switch statements the UI layers used to carry —
//
//   src/platform/curses/glyph_map.cpp   glyph shape / colour / bold / skip
//   src/interface/render/radar.cpp      treasure blip colour + rng jitter
//   src/interface/ui/level_editor.cpp   generator editor labels
//
// tools/pack_export emits these into packs/core/classpack.yaml, which the
// classpack installer folds back onto the descriptors at mount time. The
// numbers here are the ONLY authoring copy: do not "improve" a value, and
// change it in lockstep with a regenerated packs/core/classpack.yaml.
//
// Each table's out-of-range answer is the matching switch's `default:`
// branch, which is also the descriptor struct's default member initializer,
// so a mod family that declares nothing renders exactly like an unknown
// family did before.

#include <openglad/core/colors.h>
#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/family_presentation.h>

#include <array>
#include <cstddef>

namespace og::corepack {

// --- glyphs ---------------------------------------------------------------

// Livings carry the SHAPE only: the renderer paints the team colour over it
// (glyph_map.cpp's living_glyph returns Color::Default for the same reason).
inline constexpr std::array<FamilyGlyph, NUM_FAMILIES> kLivingGlyphs = {{
    {U'S', 'S', GlyphColor::Default, false, false},  // 0  SOLDIER
    {U'e', 'e', GlyphColor::Default, false, false},  // 1  ELF
    {U'a', 'a', GlyphColor::Default, false, false},  // 2  ARCHER
    {U'm', 'm', GlyphColor::Default, false, false},  // 3  MAGE
    {U'k', 'k', GlyphColor::Default, false, false},  // 4  SKELETON
    {U'c', 'c', GlyphColor::Default, false, false},  // 5  CLERIC
    {U'E', 'E', GlyphColor::Default, false, false},  // 6  FIREELEMENTAL
    {U'f', 'f', GlyphColor::Default, false, false},  // 7  FAERIE
    {U'J', 'J', GlyphColor::Default, false, false},  // 8  SLIME
    {U'i', 'i', GlyphColor::Default, false, false},  // 9  SMALL_SLIME
    {U'j', 'j', GlyphColor::Default, false, false},  // 10 MEDIUM_SLIME
    {U't', 't', GlyphColor::Default, false, false},  // 11 THIEF
    {U'g', 'g', GlyphColor::Default, false, false},  // 12 GHOST
    {U'd', 'd', GlyphColor::Default, false, false},  // 13 DRUID
    {U'o', 'o', GlyphColor::Default, false, false},  // 14 ORC
    {U'O', 'O', GlyphColor::Default, false, false},  // 15 BIG_ORC
    {U'B', 'B', GlyphColor::Default, false, false},  // 16 BARBARIAN
    {U'M', 'M', GlyphColor::Default, false, false},  // 17 ARCHMAGE
    {U'G', 'G', GlyphColor::Default, false, false},  // 18 GOLEM
    {U'K', 'K', GlyphColor::Default, false, false},  // 19 GIANT_SKELETON
    {U'Y', 'Y', GlyphColor::Default, false, false},  // 20 TOWER1
}};

inline constexpr std::array<FamilyGlyph, 20> kWeaponGlyphs = {{
    {U'\'', '\'', GlyphColor::White, false, false},  // 0  KNIFE
    {U'*', '*', GlyphColor::White, false, false},    // 1  ROCK
    {U'\'', '\'', GlyphColor::White, false, false},  // 2  ARROW
    {U'*', '*', GlyphColor::Red, true, false},       // 3  FIREBALL
    {U'♣', '&', GlyphColor::Green, false, false},  // 4  TREE
    {U'*', '*', GlyphColor::Red, true, false},       // 5  METEOR
    {U'·', '+', GlyphColor::Magenta, false, false},  // 6  SPRINKLE
    {U'\'', '/', GlyphColor::White, false, false},   // 7  BONE
    {U'·', ',', GlyphColor::Red, false, false},  // 8  BLOOD
    {U'o', 'o', GlyphColor::Green, false, false},    // 9  BLOB
    {U'\'', '\'', GlyphColor::Red, true, false},     // 10 FIRE_ARROW
    {U'↯', '/', GlyphColor::Yellow, true, false},  // 11 LIGHTNING
    {U'∘', 'o', GlyphColor::Yellow, true, false},  // 12 GLOW
    {U'≈', '~', GlyphColor::Cyan, true, false},    // 13 WAVE
    {U'≈', '~', GlyphColor::Cyan, true, false},    // 14 WAVE2
    {U'≈', '~', GlyphColor::Cyan, true, false},    // 15 WAVE3
    {U'○', 'O', GlyphColor::Cyan, true, false},    // 16 CIRCLE_PROTECTION
    {U'*', '*', GlyphColor::White, false, false},    // 17 HAMMER
    {U'+', '+', GlyphColor::Yellow, false, false},   // 18 DOOR
    {U'*', '*', GlyphColor::White, false, false},    // 19 BOULDER
}};

inline constexpr std::array<FamilyGlyph, 15> kTreasureGlyphs = {{
    {U' ', ' ', GlyphColor::Default, false, true},   // 0  STAIN (skip)
    {U'%', '%', GlyphColor::Yellow, false, false},   // 1  DRUMSTICK
    {U'$', '$', GlyphColor::Yellow, true, false},    // 2  GOLD_BAR
    {U'$', '$', GlyphColor::White, false, false},    // 3  SILVER_BAR
    {U'!', '!', GlyphColor::Magenta, true, false},   // 4  MAGIC_POTION
    {U'!', '!', GlyphColor::Magenta, true, false},   // 5  INVIS_POTION
    {U'!', '!', GlyphColor::Magenta, true, false},   // 6  INVULNERABLE_POTION
    {U'!', '!', GlyphColor::Magenta, true, false},   // 7  FLIGHT_POTION
    {U'>', '>', GlyphColor::Cyan, true, false},      // 8  EXIT
    {U'<', '<', GlyphColor::Cyan, true, false},      // 9  TELEPORTER
    {U'+', '+', GlyphColor::Red, true, false},       // 10 LIFE_GEM
    {U'[', '[', GlyphColor::Yellow, true, false},    // 11 KEY
    {U'!', '!', GlyphColor::Magenta, true, false},   // 12 SPEED_POTION
    // CTF entities tint by team; entity_glyph special-cased them ahead of
    // the treasure switch, so the shape lives here and Team carries the tint.
    {U'F', 'F', GlyphColor::Team, true, false},      // 13 FLAG
    {U'O', 'O', GlyphColor::Team, true, false},      // 14 CTF_POINT
}};

inline constexpr std::array<FamilyGlyph, 13> kEffectGlyphs = {{
    {U'*', '*', GlyphColor::White, false, false},    // 0  EXPAND
    {U'!', '!', GlyphColor::Magenta, true, false},   // 1  GHOST_SCARE
    {U'◍', 'o', GlyphColor::Red, true, false},  // 2  BOMB
    {U'✺', '*', GlyphColor::Red, true, false},  // 3  EXPLOSION
    {U'*', '*', GlyphColor::Yellow, true, false},    // 4  FLASH
    {U'○', 'O', GlyphColor::Cyan, true, false},  // 5  MAGIC_SHIELD
    {U'\'', '\'', GlyphColor::White, false, false},  // 6  KNIFE_BACK
    {U'%', '%', GlyphColor::Yellow, true, false},    // 7  BOOMERANG
    {U'☁', '%', GlyphColor::White, false, false},  // 8  CLOUD
    {U'+', '+', GlyphColor::White, false, false},    // 9  MARKER
    {U'╪', '#', GlyphColor::White, false, false},  // 10 CHAIN
    {U'/', '/', GlyphColor::Yellow, false, false},   // 11 DOOR_OPEN
    {U'∗', '*', GlyphColor::White, true, false},   // 12 HIT
}};

// Generators carry the shape only (the renderer paints the team colour).
inline constexpr std::array<FamilyGlyph, 4> kGeneratorGlyphs = {{
    {U'^', '^', GlyphColor::Default, false, false},  // 0 TENT
    {U'T', 'T', GlyphColor::Default, false, false},  // 1 TOWER
    {U'n', 'n', GlyphColor::Default, false, false},  // 2 BONES
    {U'A', 'A', GlyphColor::Default, false, false},  // 3 TREEHOUSE
}};

// --- radar blips ----------------------------------------------------------

// radar.cpp's family switch runs over the treasure entities only; livings,
// weapons and generators blip in their team colour through order-level code
// that never looks at the family. SPEED_POTION is deliberately absent from
// the potion case list, so it draws no blip.
inline constexpr std::array<RadarBlip, 15> kTreasureRadar = {{
    {kRadarColorNone, 0},   // 0  STAIN
    {COLOR_BROWN, 2},       // 1  DRUMSTICK      COLOR_BROWN + rng(2)
    {COLOR_YELLOW, 5},      // 2  GOLD_BAR       YELLOW + rng(5)
    {COLOR_GREY, 5},        // 3  SILVER_BAR     GREY + rng(5)
    {COLOR_BLUE, 5},        // 4  MAGIC_POTION   COLOR_BLUE + rng(5)
    {COLOR_BLUE, 5},        // 5  INVIS_POTION
    {COLOR_BLUE, 5},        // 6  INVULNERABLE_POTION
    {COLOR_BLUE, 5},        // 7  FLIGHT_POTION
    {COLOR_CYAN, 7},        // 8  EXIT           LIGHT_BLUE + rng(7)
    {COLOR_CYAN, 7},        // 9  TELEPORTER
    {kRadarColorNone, 0},   // 10 LIFE_GEM
    {kRadarColorNone, 0},   // 11 KEY
    {kRadarColorNone, 0},   // 12 SPEED_POTION
    {kRadarColorTeam, 7},   // 13 FLAG           team colour + rng(7)
    {kRadarColorTeam, 7},   // 14 CTF_POINT
}};

// --- editor labels --------------------------------------------------------

inline constexpr std::array<const char*, 4> kGeneratorEditorLabels = {
    "TENT", "MAGE TOWER", "BONEPILE", "TREEHOUSE",
};

// --- accessors (out of range = the legacy `default:` branch) --------------

namespace detail {
template <std::size_t N>
constexpr FamilyGlyph pick(const std::array<FamilyGlyph, N>& table, int id,
                           FamilyGlyph fallback)
{
    return (id >= 0 && static_cast<std::size_t>(id) < N)
               ? table[static_cast<std::size_t>(id)]
               : fallback;
}
}  // namespace detail

constexpr FamilyGlyph living_glyph(int family)
{
    return detail::pick(kLivingGlyphs, family,
                        {U'?', '?', GlyphColor::Default, false, false});
}

constexpr FamilyGlyph weapon_glyph(int family)
{
    return detail::pick(kWeaponGlyphs, family,
                        {U'*', '*', GlyphColor::White, false, false});
}

constexpr FamilyGlyph treasure_glyph(int family)
{
    return detail::pick(kTreasureGlyphs, family,
                        {U'$', '$', GlyphColor::Yellow, false, false});
}

constexpr FamilyGlyph effect_glyph(int family)
{
    return detail::pick(kEffectGlyphs, family,
                        {U'*', '*', GlyphColor::White, false, false});
}

constexpr FamilyGlyph generator_glyph(int family)
{
    return detail::pick(kGeneratorGlyphs, family,
                        {U'#', '#', GlyphColor::Default, false, false});
}

constexpr RadarBlip treasure_radar(int family)
{
    return (family >= 0 &&
            static_cast<std::size_t>(family) < kTreasureRadar.size())
               ? kTreasureRadar[static_cast<std::size_t>(family)]
               : RadarBlip{};
}

constexpr const char* generator_editor_label(int family)
{
    return (family >= 0 &&
            static_cast<std::size_t>(family) < kGeneratorEditorLabels.size())
               ? kGeneratorEditorLabels[static_cast<std::size_t>(family)]
               : "GENERATOR";
}

}  // namespace og::corepack
