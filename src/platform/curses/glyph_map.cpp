/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Glyph mapping: terrain genres and decor ids get their terminal characters
 * from the tables below; ENTITY glyphs come from the family descriptors, so a
 * class pack's family draws itself with no engine change. See
 * docs/ncurses-client.md for the rationale behind each assignment.
 */
#include <openglad/platform/curses/glyph_map.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/family_presentation.h>
#include <openglad/core/order.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/generator_family_descriptor.h>
#include <openglad/gameplay/families/treasure_family_descriptor.h>
#include <openglad/gameplay/families/weapon_family_descriptor.h>

#include <array>
#include <optional>

namespace og::curses {

namespace {

// Translates a descriptor's portable glyph into the terminal Glyph.
// og::GlyphColor 0..8 mirrors og::curses::Color entry for entry, so the cast
// is exact; GlyphColor::Team is the one extra member and resolves to the
// colour the caller passes in (CTF flags and control points ship it).
Glyph to_glyph(const og::FamilyGlyph& g, Color team)
{
    const Color fg = (g.color == og::GlyphColor::Team)
                         ? team
                         : static_cast<Color>(g.color);
    return Glyph{g.codepoint, g.ascii, fg, g.bold, g.transparent};
}

// The glyph of one family's descriptor, or — when the family is not
// registered at all — the descriptor struct's own default glyph. That default
// IS the `default:` branch these lookups used to end with, so an unknown
// family still renders exactly as it did before.
template <typename Descriptor>
Glyph glyph_of(const Descriptor* d, Color team)
{
    static constexpr Descriptor kBlank{};
    return to_glyph(d != nullptr ? d->glyph : kBlank.glyph, team);
}

} // namespace

// Livings carry the SHAPE only: entity_glyph paints the team colour over it
// (every core living ships GlyphColor::Default for exactly that reason), so a
// descriptor asking for the team colour resolves to Default here too.
Glyph living_glyph(int family)
{
    return glyph_of(get_family_descriptor(family), Color::Default);
}

// Effects (Order::FX) — the visible part of most specials (boomerang, magic
// shield, explosions, ...). They live in the world's fxlist, which the renderer
// now draws. Every effect family gets a visible glyph so nothing a player casts
// is silently invisible. (The lasting bloodstain is FAMILY_STAIN, a treasure,
// which stays blank so it doesn't litter the floor.)
Glyph effect_glyph(int family)
{
    return glyph_of(get_effect_family_descriptor(family), Color::Default);
}

Color team_color(unsigned char team_num)
{
    static constexpr std::array<Color, 8> kRamp = {
        Color::Red,     // 0
        Color::Green,   // 1
        Color::Blue,    // 2
        Color::Yellow,  // 3
        Color::Magenta, // 4
        Color::Cyan,    // 5
        Color::White,   // 6
        Color::Black,   // 7 (rendered bright by the terminal)
    };
    return kRamp[team_num % kRamp.size()];
}

Glyph entity_glyph(Order order, int family, unsigned char team_num,
                   bool is_followed_avatar, unsigned char my_team)
{
    if (is_followed_avatar)
        return Glyph{U'@', '@', team_color(team_num), true, false};

    const Color team = team_color(team_num);

    switch (order) {
    case Order::Living: {
        // Shape identifies the family, colour identifies the side. Default
        // means "paint me in the team colour" for these two orders, which is
        // what the whole core pack ships; a pack family that names a colour
        // keeps it.
        Glyph g = living_glyph(family);
        if (g.fg == Color::Default)
            g.fg = team;
        g.bold = (team_num == my_team);
        return g;
    }
    case Order::Treasure:
        // CTF entities tint by team ('F' = flag, 'O' = control point): their
        // descriptors carry GlyphColor::Team, which to_glyph resolves here.
        return glyph_of(get_treasure_family_descriptor(family), team);
    case Order::Weapon:
        return glyph_of(get_weapon_family_descriptor(family), team);
    case Order::Generator: {
        Glyph g = glyph_of(get_generator_family_descriptor(family), team);
        if (g.fg == Color::Default)
            g.fg = team;
        return g;
    }
    case Order::FX:
        return effect_glyph(family);
    case Order::Special:
    case Order::Button1:
    default:
        return Glyph{U' ', ' ', Color::Default, false, true};
    }
}

Glyph border_glyph()
{
    // A dim hatched fill marks "outside the arena" — clearly not walkable grass
    // and distinct from in-level walls (which are a bright solid block).
    return Glyph{U'▓', '#', Color::Blue, false, false};
}

Glyph tile_glyph(int genre)
{
    switch (genre) {
    case TYPE_GRASS:       return Glyph{U'.', '.', Color::Green, false, false};
    case TYPE_GRASS_DARK:  return Glyph{U',', ',', Color::Green, false, false};
    case TYPE_GRASS_LIGHT: return Glyph{U'`', '`', Color::Green, false, false};
    case TYPE_DIRT:        return Glyph{U':', ':', Color::Yellow, false, false};
    case TYPE_DIRT_DARK:   return Glyph{U';', ';', Color::Yellow, false, false};
    case TYPE_COBBLE:      return Glyph{U'▒', '%', Color::White, false, false};
    case TYPE_CARPET:      return Glyph{U'=', '=', Color::Magenta, false, false};
    case TYPE_WALL:        return Glyph{U'█', '#', Color::White, true, false};
    case TYPE_WATER:       return Glyph{U'≈', '~', Color::Blue, false, false};
    case TYPE_TREES:       return Glyph{U'♣', '&', Color::Green, false, false};
    case TYPE_AIR:         return Glyph{U'▒', ':', Color::Blue, false, false};   // a hole you fall through
    case TYPE_GLASS:       return Glyph{U'░', '"', Color::Cyan, false, false};   // see-through floor
    case TYPE_DROP_BLOCK:  return Glyph{U'▔', '_', Color::White, false, false};  // edge guard
    case TYPE_ZSTAIRS:     return Glyph{U'≡', 'H', Color::Yellow, true, false};  // Z-stairs (direction unknown; see zstair_glyph)
    case TYPE_SNOW:        return Glyph{U'*', '*', Color::White, false, false};  // snowfield
    case TYPE_LAVA:        return Glyph{U'≈', '~', Color::Red, true, false};     // molten (bold; water ≈ is blue)
    case TYPE_MARSH:       return Glyph{U'"', '"', Color::Green, false, false};  // bog reeds
    case TYPE_ASH:         return Glyph{U'░', '-', Color::White, false, false};  // ash field (glass ░ is cyan)
    case TYPE_UNKNOWN:
    default:               return Glyph{U' ', ' ', Color::Default, false, false};
    }
}

std::optional<Glyph> decor_glyph(unsigned char decor_id)
{
    switch (decor_id) {
    // Wall torches: bold yellow flame. ('!' is also the potion ENTITY glyph,
    // but that one is magenta and entities draw over tiles anyway.)
    case DECOR_TORCH1:
    case DECOR_TORCH2:
    case DECOR_TORCH3:        return Glyph{U'!', '!', Color::Yellow, true, false};
    // Fire bowl: bold red.
    case DECOR_BRAZIER:       return Glyph{U'☼', 'o', Color::Red, true, false};
    // Boulders: white rock lump.
    case DECOR_BOULDER_1:
    case DECOR_BOULDER_2:
    case DECOR_BOULDER_3:
    case DECOR_BOULDER_4:     return Glyph{U'o', 'o', Color::White, false, false};
    // Columns: white pillar.
    case DECOR_COLUMN_BOTTOM:
    case DECOR_COLUMN_TOP:    return Glyph{U'|', '|', Color::White, false, false};
    // Shrub (conceals like trees): bold green tuft — marsh reeds use the same
    // '"' shape but green non-bold, so the two stay distinguishable.
    case DECOR_SHRUB:         return Glyph{U'"', '"', Color::Green, true, false};
    // Ground litter reads as its ground: pebbles/bones inherit the base tile,
    // as do DECOR_NONE and any out-of-registry byte.
    case DECOR_PEBBLES:
    case DECOR_BONES:
    default:                  return std::nullopt;
    }
}

Glyph zstair_glyph(bool up)
{
    // Roguelike convention: '<' goes up, '>' goes down. Bold yellow like the
    // direction-less TYPE_ZSTAIRS fallback, so stairs keep their color even
    // when the direction is known. (The FAMILY_EXIT ENTITY also uses '>',
    // but entities draw over tiles and carry their own color/blink.)
    if (up)
        return Glyph{U'▲', '<', Color::Yellow, true, false};
    return Glyph{U'▼', '>', Color::Yellow, true, false};
}

} // namespace og::curses
