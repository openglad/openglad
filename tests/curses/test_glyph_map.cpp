/* Unit tests for the glyph mapping table (pure, no terminal). */
#include <gtest/gtest.h>

#include <openglad/platform/curses/glyph_map.h>

#include <openglad/core/constants.h>
#include <openglad/core/order.h>
#include <openglad/core/terrain_types.h>

using namespace og::curses;

TEST(GlyphMap, living_families_have_distinct_shapes)
{
    // Every living family 0..NUM_FAMILIES-1 maps to a non-space ascii glyph, and
    // the glyphs are distinct so creatures are visually separable.
    bool seen[128] = {};
    for (int f = 0; f < NUM_FAMILIES; ++f) {
        Glyph g = living_glyph(f);
        ASSERT_FALSE(g.skip) << "family " << f << " should be drawn";
        ASSERT_GT(g.ascii, ' ') << "family " << f << " has blank glyph";
        const auto idx = static_cast<unsigned char>(g.ascii);
        EXPECT_FALSE(seen[idx]) << "duplicate glyph '" << g.ascii << "' at family " << f;
        seen[idx] = true;
    }
}

TEST(GlyphMap, specific_family_glyphs)
{
    EXPECT_EQ(living_glyph(FAMILY_SOLDIER).ascii, 'S');
    EXPECT_EQ(living_glyph(FAMILY_THIEF).ascii, 't');
    EXPECT_EQ(living_glyph(FAMILY_MAGE).ascii, 'm');
    EXPECT_EQ(living_glyph(FAMILY_ARCHMAGE).ascii, 'M');
    EXPECT_EQ(living_glyph(FAMILY_BARBARIAN).ascii, 'B');
    EXPECT_EQ(living_glyph(FAMILY_GHOST).ascii, 'g');
}

TEST(GlyphMap, out_of_range_family_is_safe)
{
    Glyph g = living_glyph(999);
    EXPECT_FALSE(g.skip);
    EXPECT_EQ(g.ascii, '?');
    Glyph n = living_glyph(-1);
    EXPECT_EQ(n.ascii, '?');
}

TEST(GlyphMap, followed_avatar_is_at_sign_and_bold)
{
    Glyph g = entity_glyph(Order::Living, FAMILY_THIEF, /*team*/ 0,
                           /*is_followed*/ true, /*my_team*/ 0);
    EXPECT_EQ(g.ascii, '@');
    EXPECT_TRUE(g.bold);
    EXPECT_FALSE(g.skip);
}

TEST(GlyphMap, living_entity_takes_team_color)
{
    Glyph red = entity_glyph(Order::Living, FAMILY_SOLDIER, 0, false, 0);
    Glyph green = entity_glyph(Order::Living, FAMILY_SOLDIER, 1, false, 0);
    EXPECT_EQ(red.ascii, 'S');
    EXPECT_EQ(red.fg, Color::Red);
    EXPECT_EQ(green.fg, Color::Green);
    EXPECT_TRUE(red.bold) << "player-team creature should be bold";
    EXPECT_FALSE(green.bold) << "enemy-team creature should not be bold";
}

TEST(GlyphMap, team_color_ramp_wraps)
{
    EXPECT_EQ(team_color(0), Color::Red);
    EXPECT_EQ(team_color(1), Color::Green);
    EXPECT_EQ(team_color(2), Color::Blue);
    EXPECT_EQ(team_color(8), team_color(0)) << "ramp should wrap mod 8";
}

TEST(GlyphMap, treasure_and_weapon_glyphs)
{
    EXPECT_EQ(entity_glyph(Order::Treasure, FAMILY_GOLD_BAR, 0, false, 0).ascii, '$');
    EXPECT_EQ(entity_glyph(Order::Treasure, FAMILY_EXIT, 0, false, 0).ascii, '>');
    EXPECT_EQ(entity_glyph(Order::Treasure, FAMILY_KEY, 0, false, 0).ascii, '[');
    EXPECT_TRUE(entity_glyph(Order::Treasure, FAMILY_STAIN, 0, false, 0).skip)
        << "blood stains are floor decals, not drawn as entities";
}

// Every weapon family (a projectile/thrown thing a character fires) renders a
// visible glyph — nothing you throw is silently invisible. FAMILY_BOULDER (19) is
// the last weapon family.
TEST(GlyphMap, every_weapon_family_is_visible)
{
    for (int f = 0; f <= FAMILY_BOULDER; ++f) {
        const Glyph g = entity_glyph(Order::Weapon, f, /*team*/ 0, /*followed*/ false,
                                     /*my_team*/ 0);
        EXPECT_FALSE(g.skip) << "weapon family " << f << " is skipped (invisible)";
        EXPECT_GT(g.ascii, ' ') << "weapon family " << f << " has a blank glyph";
    }
}

// Every effect family (the visible part of specials: boomerang, magic shield,
// explosions, ...) renders a visible glyph. Effects live in fxlist, which the
// renderer now draws. FAMILY_HIT (12) is the last effect family.
TEST(GlyphMap, every_effect_family_is_visible)
{
    for (int f = 0; f <= FAMILY_HIT; ++f) {
        const Glyph g = entity_glyph(Order::FX, f, 0, false, 0);
        EXPECT_FALSE(g.skip) << "effect family " << f << " is skipped (invisible)";
        EXPECT_GT(g.ascii, ' ') << "effect family " << f << " has a blank glyph";
    }
    // The boomerang specifically — the family the user reported invisible.
    const Glyph boom = entity_glyph(Order::FX, FAMILY_BOOMERANG, 0, false, 0);
    EXPECT_FALSE(boom.skip);
    EXPECT_EQ(boom.ascii, '%') << "the boomerang renders as '%'";
}

TEST(GlyphMap, tile_genres_map_to_expected_glyphs)
{
    EXPECT_EQ(tile_glyph(TYPE_GRASS).ascii, '.');
    EXPECT_EQ(tile_glyph(TYPE_WATER).ascii, '~');
    EXPECT_EQ(tile_glyph(TYPE_WALL).ascii, '#');
    EXPECT_TRUE(tile_glyph(TYPE_WALL).bold);
    EXPECT_EQ(tile_glyph(TYPE_WATER).fg, Color::Blue);
    EXPECT_EQ(tile_glyph(TYPE_GRASS).fg, Color::Green);
    // Unknown / unmapped genres render as blank space.
    EXPECT_EQ(tile_glyph(TYPE_UNKNOWN).ascii, ' ');
    EXPECT_EQ(tile_glyph(12345).ascii, ' ');
}

TEST(GlyphMap, zstair_glyphs_show_direction)
{
    // B1: the direction-aware stair glyphs (roguelike convention '<' up,
    // '>' down) — bold yellow like the direction-less TYPE_ZSTAIRS fallback.
    const Glyph up = zstair_glyph(true);
    const Glyph down = zstair_glyph(false);
    EXPECT_EQ(up.ascii, '<');
    EXPECT_EQ(down.ascii, '>');
    EXPECT_EQ(up.unicode, U'▲');
    EXPECT_EQ(down.unicode, U'▼');
    EXPECT_NE(up.unicode, down.unicode);
    EXPECT_EQ(up.fg, Color::Yellow);
    EXPECT_EQ(down.fg, Color::Yellow);
    EXPECT_TRUE(up.bold);
    EXPECT_TRUE(down.bold);
    EXPECT_FALSE(up.skip);
    EXPECT_FALSE(down.skip);
    // The genre-only fallback stays available for direction-less callers.
    EXPECT_EQ(tile_glyph(TYPE_ZSTAIRS).ascii, 'H');
}

TEST(GlyphMap, westlands_tile_genres_map_to_expected_glyphs)
{
    // Snowfield: white asterisks.
    const Glyph snow = tile_glyph(TYPE_SNOW);
    EXPECT_EQ(snow.unicode, U'*');
    EXPECT_EQ(snow.ascii, '*');
    EXPECT_EQ(snow.fg, Color::White);
    EXPECT_FALSE(snow.bold);

    // Molten lava: bold red waves (water's identical wave shape is blue and
    // not bold, so the two stay distinguishable).
    const Glyph lava = tile_glyph(TYPE_LAVA);
    EXPECT_EQ(lava.unicode, U'≈');
    EXPECT_EQ(lava.ascii, '~');
    EXPECT_EQ(lava.fg, Color::Red);
    EXPECT_TRUE(lava.bold);
    EXPECT_EQ(tile_glyph(TYPE_WATER).unicode, lava.unicode);
    EXPECT_NE(tile_glyph(TYPE_WATER).fg, lava.fg);

    // Bog reeds: green quotes.
    const Glyph marsh = tile_glyph(TYPE_MARSH);
    EXPECT_EQ(marsh.unicode, U'"');
    EXPECT_EQ(marsh.ascii, '"');
    EXPECT_EQ(marsh.fg, Color::Green);
    EXPECT_FALSE(marsh.bold);

    // Ash field: white light-shade (glass uses the same shade in Cyan —
    // color disambiguates them).
    const Glyph ash = tile_glyph(TYPE_ASH);
    EXPECT_EQ(ash.unicode, U'░');
    EXPECT_EQ(ash.ascii, '-');
    EXPECT_EQ(ash.fg, Color::White);
    EXPECT_FALSE(ash.bold);
    EXPECT_EQ(tile_glyph(TYPE_GLASS).unicode, ash.unicode);
    EXPECT_NE(tile_glyph(TYPE_GLASS).fg, ash.fg);
}

TEST(GlyphMap, ascii_fallback_differs_from_unicode_for_some_tiles)
{
    // Wall is a solid block in Unicode but '#' in ASCII.
    Glyph wall = tile_glyph(TYPE_WALL);
    EXPECT_EQ(wall.pick(/*allow_unicode=*/false), static_cast<char32_t>('#'));
    EXPECT_NE(wall.pick(/*allow_unicode=*/true), static_cast<char32_t>('#'));
}
