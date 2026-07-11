/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Glyph mapping: the authoritative table translating the game's entity
 * taxonomy and terrain genres into terminal characters and colors. See
 * docs/ncurses-client.md for the rationale behind each assignment.
 */
#include <openglad/platform/curses/glyph_map.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/core/terrain_types.h>

#include <array>

namespace og::curses {

namespace {

// One living family's glyph shape (no color; the caller applies the team color).
struct ShapeEntry {
    char ascii;
    char32_t unicode;
};

// Indexed by living family id (FAMILY_SOLDIER .. FAMILY_TOWER1, 0..20).
constexpr std::array<ShapeEntry, NUM_FAMILIES> kLivingShapes = {{
    {'S', U'S'}, // 0  SOLDIER
    {'e', U'e'}, // 1  ELF
    {'a', U'a'}, // 2  ARCHER
    {'m', U'm'}, // 3  MAGE
    {'k', U'k'}, // 4  SKELETON
    {'c', U'c'}, // 5  CLERIC
    {'E', U'E'}, // 6  FIREELEMENTAL
    {'f', U'f'}, // 7  FAERIE
    {'J', U'J'}, // 8  SLIME (big)
    {'i', U'i'}, // 9  SMALL_SLIME
    {'j', U'j'}, // 10 MEDIUM_SLIME
    {'t', U't'}, // 11 THIEF
    {'g', U'g'}, // 12 GHOST
    {'d', U'd'}, // 13 DRUID
    {'o', U'o'}, // 14 ORC
    {'O', U'O'}, // 15 BIG_ORC (captain)
    {'B', U'B'}, // 16 BARBARIAN
    {'M', U'M'}, // 17 ARCHMAGE
    {'G', U'G'}, // 18 GOLEM
    {'K', U'K'}, // 19 GIANT_SKELETON
    {'Y', U'Y'}, // 20 TOWER1
}};

Glyph treasure_glyph(int family)
{
    switch (family) {
    case FAMILY_STAIN:      return Glyph{U' ', ' ', Color::Default, false, true};
    case FAMILY_DRUMSTICK:  return Glyph{U'%', '%', Color::Yellow, false, false};
    case FAMILY_GOLD_BAR:   return Glyph{U'$', '$', Color::Yellow, true, false};
    case FAMILY_SILVER_BAR: return Glyph{U'$', '$', Color::White, false, false};
    case FAMILY_EXIT:       return Glyph{U'>', '>', Color::Cyan, true, false};
    case FAMILY_TELEPORTER: return Glyph{U'<', '<', Color::Cyan, true, false};
    case FAMILY_LIFE_GEM:   return Glyph{U'+', '+', Color::Red, true, false};
    case FAMILY_KEY:        return Glyph{U'[', '[', Color::Yellow, true, false};
    case FAMILY_MAGIC_POTION:
    case FAMILY_INVIS_POTION:
    case FAMILY_INVULNERABLE_POTION:
    case FAMILY_FLIGHT_POTION:
    case FAMILY_SPEED_POTION: return Glyph{U'!', '!', Color::Magenta, true, false};
    default:                return Glyph{U'$', '$', Color::Yellow, false, false};
    }
}

Glyph weapon_glyph(int family)
{
    switch (family) {
    case FAMILY_KNIFE:      return Glyph{U'\'', '\'', Color::White, false, false};
    case FAMILY_ARROW:      return Glyph{U'\'', '\'', Color::White, false, false};
    case FAMILY_FIRE_ARROW: return Glyph{U'\'', '\'', Color::Red, true, false};
    case FAMILY_BONE:       return Glyph{U'\'', '/', Color::White, false, false};
    case FAMILY_ROCK:
    case FAMILY_BOULDER:
    case FAMILY_HAMMER:     return Glyph{U'*', '*', Color::White, false, false};
    case FAMILY_FIREBALL:
    case FAMILY_METEOR:     return Glyph{U'*', '*', Color::Red, true, false};
    case FAMILY_LIGHTNING:  return Glyph{U'↯', '/', Color::Yellow, true, false};
    case FAMILY_DOOR:       return Glyph{U'+', '+', Color::Yellow, false, false};
    case FAMILY_TREE:       return Glyph{U'♣', '&', Color::Green, false, false};
    case FAMILY_BLOB:       return Glyph{U'o', 'o', Color::Green, false, false};
    case FAMILY_WAVE:
    case FAMILY_WAVE2:
    case FAMILY_WAVE3:      return Glyph{U'≈', '~', Color::Cyan, true, false};
    case FAMILY_CIRCLE_PROTECTION: return Glyph{U'○', 'O', Color::Cyan, true, false};
    case FAMILY_GLOW:       return Glyph{U'∘', 'o', Color::Yellow, true, false};
    case FAMILY_SPRINKLE:   return Glyph{U'·', '+', Color::Magenta, false, false};
    case FAMILY_BLOOD:      return Glyph{U'·', ',', Color::Red, false, false};
    default:                return Glyph{U'*', '*', Color::White, false, false};
    }
}

Glyph generator_shape(int family)
{
    switch (family) {
    case FAMILY_TENT:      return Glyph{U'^', '^', Color::Default, false, false};
    case FAMILY_TOWER:     return Glyph{U'T', 'T', Color::Default, false, false};
    case FAMILY_BONES:     return Glyph{U'n', 'n', Color::Default, false, false};
    case FAMILY_TREEHOUSE: return Glyph{U'A', 'A', Color::Default, false, false};
    default:               return Glyph{U'#', '#', Color::Default, false, false};
    }
}

} // namespace

Glyph living_glyph(int family)
{
    if (family < 0 || family >= static_cast<int>(kLivingShapes.size()))
        return Glyph{U'?', '?', Color::Default, false, false};
    const ShapeEntry& e = kLivingShapes[static_cast<std::size_t>(family)];
    return Glyph{e.unicode, e.ascii, Color::Default, false, false};
}

// Effects (Order::FX) — the visible part of most specials (boomerang, magic
// shield, explosions, ...). They live in the world's fxlist, which the renderer
// now draws. Every effect family gets a visible glyph so nothing a player casts
// is silently invisible. (The lasting bloodstain is FAMILY_STAIN, a treasure,
// which stays blank so it doesn't litter the floor.)
Glyph effect_glyph(int family)
{
    switch (family) {
    case FAMILY_BOOMERANG:    return Glyph{U'%', '%', Color::Yellow, true, false};
    case FAMILY_MAGIC_SHIELD: return Glyph{U'○', 'O', Color::Cyan, true, false};
    case FAMILY_KNIFE_BACK:   return Glyph{U'\'', '\'', Color::White, false, false};
    case FAMILY_GHOST_SCARE:  return Glyph{U'!', '!', Color::Magenta, true, false};
    case FAMILY_EXPLOSION:    return Glyph{U'✺', '*', Color::Red, true, false};
    case FAMILY_BOMB:         return Glyph{U'◍', 'o', Color::Red, true, false};
    case FAMILY_CLOUD:        return Glyph{U'☁', '%', Color::White, false, false};
    case FAMILY_CHAIN:        return Glyph{U'╪', '#', Color::White, false, false};
    case FAMILY_DOOR_OPEN:    return Glyph{U'/', '/', Color::Yellow, false, false};
    case FAMILY_EXPAND:       return Glyph{U'*', '*', Color::White, false, false};
    case FAMILY_FLASH:        return Glyph{U'*', '*', Color::Yellow, true, false};
    case FAMILY_MARKER:       return Glyph{U'+', '+', Color::White, false, false};
    case FAMILY_HIT:          return Glyph{U'∗', '*', Color::White, true, false};
    default:                  return Glyph{U'*', '*', Color::White, false, false};
    }
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

    switch (order) {
    case Order::Living: {
        Glyph g = living_glyph(family);
        g.fg = team_color(team_num);
        g.bold = (team_num == my_team);
        return g;
    }
    case Order::Treasure:
        // CTF entities tint by team: 'F' = flag (owning team), 'O' = control
        // point (current owner).
        if (family == FAMILY_FLAG)
            return Glyph{U'F', 'F', team_color(team_num), true, false};
        if (family == FAMILY_CTF_POINT)
            return Glyph{U'O', 'O', team_color(team_num), true, false};
        return treasure_glyph(family);
    case Order::Weapon:
        return weapon_glyph(family);
    case Order::Generator: {
        Glyph g = generator_shape(family);
        g.fg = team_color(team_num);
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
    case TYPE_ZSTAIRS:     return Glyph{U'≡', 'H', Color::Yellow, true, false};  // Z-stairs
    case TYPE_SNOW:        return Glyph{U'*', '*', Color::White, false, false};  // snowfield
    case TYPE_LAVA:        return Glyph{U'≈', '~', Color::Red, true, false};     // molten (bold; water ≈ is blue)
    case TYPE_MARSH:       return Glyph{U'"', '"', Color::Green, false, false};  // bog reeds
    case TYPE_ASH:         return Glyph{U'░', '-', Color::White, false, false};  // ash field (glass ░ is cyan)
    case TYPE_UNKNOWN:
    default:               return Glyph{U' ', ' ', Color::Default, false, false};
    }
}

} // namespace og::curses
