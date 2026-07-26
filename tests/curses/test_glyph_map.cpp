/* Unit tests for the glyph mapping table (pure, no terminal). */
#include <gtest/gtest.h>

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
#include <openglad/gameplay/families/treasure_family_descriptor.h>

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

    for (int family : {FAMILY_DRUMSTICK, FAMILY_SILVER_BAR,
                       FAMILY_TELEPORTER, FAMILY_LIFE_GEM,
                       FAMILY_MAGIC_POTION, FAMILY_INVIS_POTION,
                       FAMILY_INVULNERABLE_POTION, FAMILY_FLIGHT_POTION,
                       FAMILY_SPEED_POTION, 999}) {
        const Glyph glyph =
            entity_glyph(Order::Treasure, family, 0, false, 0);
        EXPECT_FALSE(glyph.skip) << family;
        EXPECT_GT(glyph.ascii, ' ') << family;
    }

    for (int family : {FAMILY_TENT, FAMILY_TOWER, FAMILY_BONES,
                       FAMILY_TREEHOUSE, 999}) {
        const Glyph glyph =
            entity_glyph(Order::Generator, family, 3, false, 0);
        EXPECT_FALSE(glyph.skip) << family;
        EXPECT_NE(glyph.fg, Color::Default) << family;
    }

    EXPECT_TRUE(entity_glyph(Order::Special, 0, 0, false, 0).skip);
    EXPECT_TRUE(entity_glyph(Order::Button1, 0, 0, false, 0).skip);
    EXPECT_TRUE(entity_glyph(static_cast<Order>(255), 0, 0, false, 0).skip);
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
    EXPECT_EQ('*', entity_glyph(Order::Weapon, 999, 0, false, 0).ascii);
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
    EXPECT_EQ('*', entity_glyph(Order::FX, 999, 0, false, 0).ascii);
}

// --- descriptor-driven glyphs -------------------------------------------
//
// The glyph tables that used to live in glyph_map.cpp are gone: every entity
// glyph is read off its family descriptor, which is what a class pack writes
// when it installs. Patching a descriptor here is exactly what a pack does, so
// these pin that a mod family draws itself with no engine change.

TEST(GlyphMap, living_glyph_follows_the_family_descriptor)
{
    const FamilyDescriptor* original = get_family_descriptor(FAMILY_GOLEM);
    ASSERT_NE(nullptr, original);
    const FamilyDescriptor saved = *original;

    FamilyDescriptor patched = saved;
    patched.glyph = {U'Ω', 'w', og::GlyphColor::Default, false, false};
    ASSERT_TRUE(set_family_descriptor(FAMILY_GOLEM, patched));

    const Glyph shape = living_glyph(FAMILY_GOLEM);
    const Glyph drawn = entity_glyph(Order::Living, FAMILY_GOLEM, /*team*/ 1,
                                     false, /*my_team*/ 0);

    ASSERT_TRUE(set_family_descriptor(FAMILY_GOLEM, saved));

    EXPECT_EQ(U'Ω', shape.unicode);
    EXPECT_EQ('w', shape.ascii);
    EXPECT_EQ(Color::Default, shape.fg) << "living_glyph returns the shape only";
    EXPECT_EQ('w', drawn.ascii);
    EXPECT_EQ(team_color(1), drawn.fg) << "a Default living is painted by team";

    EXPECT_EQ('G', living_glyph(FAMILY_GOLEM).ascii) << "descriptor restored";
}

TEST(GlyphMap, treasure_and_effect_glyphs_follow_their_descriptors)
{
    const TreasureFamilyDescriptor* t = get_treasure_family_descriptor(FAMILY_KEY);
    const EffectFamilyDescriptor* e = get_effect_family_descriptor(FAMILY_CLOUD);
    ASSERT_NE(nullptr, t);
    ASSERT_NE(nullptr, e);
    const TreasureFamilyDescriptor saved_t = *t;
    const EffectFamilyDescriptor saved_e = *e;

    TreasureFamilyDescriptor patched_t = saved_t;
    patched_t.glyph = {U'¤', 'k', og::GlyphColor::Green, true, false};
    EffectFamilyDescriptor patched_e = saved_e;
    patched_e.glyph = {U'§', 'S', og::GlyphColor::Blue, false, false};
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_KEY, patched_t));
    ASSERT_TRUE(set_effect_family_descriptor(FAMILY_CLOUD, patched_e));

    const Glyph key = entity_glyph(Order::Treasure, FAMILY_KEY, 0, false, 0);
    const Glyph cloud = entity_glyph(Order::FX, FAMILY_CLOUD, 0, false, 0);

    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_KEY, saved_t));
    ASSERT_TRUE(set_effect_family_descriptor(FAMILY_CLOUD, saved_e));

    EXPECT_EQ('k', key.ascii);
    EXPECT_EQ(Color::Green, key.fg);
    EXPECT_TRUE(key.bold);
    EXPECT_EQ('S', cloud.ascii);
    EXPECT_EQ(Color::Blue, cloud.fg);

    EXPECT_EQ('[', entity_glyph(Order::Treasure, FAMILY_KEY, 0, false, 0).ascii);
    EXPECT_EQ('%', entity_glyph(Order::FX, FAMILY_CLOUD, 0, false, 0).ascii);
}

// GlyphColor::Team is the one colour that is not a terminal colour: it means
// "paint me in the entity's team colour", which is how the CTF flag and
// control point tint. Any family may ask for it.
TEST(GlyphMap, team_colored_glyphs_resolve_per_entity)
{
    const TreasureFamilyDescriptor* original =
        get_treasure_family_descriptor(FAMILY_LIFE_GEM);
    ASSERT_NE(nullptr, original);
    const TreasureFamilyDescriptor saved = *original;

    TreasureFamilyDescriptor patched = saved;
    patched.glyph = {U'+', '+', og::GlyphColor::Team, true, false};
    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_LIFE_GEM, patched));

    const Glyph a = entity_glyph(Order::Treasure, FAMILY_LIFE_GEM, 0, false, 0);
    const Glyph b = entity_glyph(Order::Treasure, FAMILY_LIFE_GEM, 2, false, 0);

    ASSERT_TRUE(set_treasure_family_descriptor(FAMILY_LIFE_GEM, saved));

    EXPECT_EQ(team_color(0), a.fg);
    EXPECT_EQ(team_color(2), b.fg);
    EXPECT_EQ(Color::Red, entity_glyph(Order::Treasure, FAMILY_LIFE_GEM, 2,
                                       false, 0).fg)
        << "restored descriptor is team-independent again";
}

// A family with no descriptor at all (an id no pack claimed) falls back to the
// descriptor struct's default glyph — which is exactly the `default:` branch
// the deleted switches ended with, per order.
TEST(GlyphMap, unregistered_families_keep_the_legacy_default_glyphs)
{
    EXPECT_EQ('?', living_glyph(200).ascii);
    EXPECT_EQ('$', entity_glyph(Order::Treasure, 200, 0, false, 0).ascii);
    EXPECT_EQ('*', entity_glyph(Order::Weapon, 200, 0, false, 0).ascii);
    EXPECT_EQ('*', entity_glyph(Order::FX, 200, 0, false, 0).ascii);
    EXPECT_EQ('#', entity_glyph(Order::Generator, 200, 0, false, 0).ascii);
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

    for (int genre : {TYPE_GRASS_DARK, TYPE_GRASS_LIGHT, TYPE_DIRT,
                      TYPE_DIRT_DARK, TYPE_COBBLE, TYPE_CARPET, TYPE_TREES,
                      TYPE_AIR, TYPE_DROP_BLOCK}) {
        const Glyph glyph = tile_glyph(genre);
        EXPECT_GT(glyph.ascii, ' ') << genre;
        EXPECT_FALSE(glyph.skip) << genre;
    }
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

// Decor overrides (tile layering): decor wins over the base tile when it
// defines a glyph; ground litter (pebbles/bones) inherits the base instead.
TEST(GlyphMap, decor_glyph_overrides_are_pinned)
{
    // Torches: bold yellow '!'.
    for (unsigned char t : {DECOR_TORCH1, DECOR_TORCH2, DECOR_TORCH3}) {
        const auto g = decor_glyph(t);
        ASSERT_TRUE(g.has_value()) << "torch id " << int(t) << " must override";
        EXPECT_EQ(g->ascii, '!');
        EXPECT_EQ(g->fg, Color::Yellow);
        EXPECT_TRUE(g->bold);
        EXPECT_FALSE(g->skip);
    }
    // Brazier: bold red fire bowl.
    const auto braz = decor_glyph(DECOR_BRAZIER);
    ASSERT_TRUE(braz.has_value());
    EXPECT_EQ(braz->ascii, 'o');
    EXPECT_EQ(braz->unicode, U'☼');
    EXPECT_EQ(braz->fg, Color::Red);
    EXPECT_TRUE(braz->bold);
    // Boulders: white 'o'.
    for (unsigned char b : {DECOR_BOULDER_1, DECOR_BOULDER_2, DECOR_BOULDER_3,
                            DECOR_BOULDER_4}) {
        const auto g = decor_glyph(b);
        ASSERT_TRUE(g.has_value()) << "boulder id " << int(b) << " must override";
        EXPECT_EQ(g->ascii, 'o');
        EXPECT_EQ(g->fg, Color::White);
    }
    // Columns: white '|'.
    for (unsigned char c : {DECOR_COLUMN_BOTTOM, DECOR_COLUMN_TOP}) {
        const auto g = decor_glyph(c);
        ASSERT_TRUE(g.has_value());
        EXPECT_EQ(g->ascii, '|');
        EXPECT_EQ(g->fg, Color::White);
    }
    // Shrub: bold green '"' — same shape as marsh reeds, disambiguated by
    // bold (marsh is non-bold).
    const auto shrub = decor_glyph(DECOR_SHRUB);
    ASSERT_TRUE(shrub.has_value());
    EXPECT_EQ(shrub->ascii, '"');
    EXPECT_EQ(shrub->fg, Color::Green);
    EXPECT_TRUE(shrub->bold);
    EXPECT_FALSE(tile_glyph(TYPE_MARSH).bold);
}

TEST(GlyphMap, decor_ground_litter_and_unknown_ids_inherit_base)
{
    EXPECT_FALSE(decor_glyph(DECOR_NONE).has_value());
    EXPECT_FALSE(decor_glyph(DECOR_PEBBLES).has_value())
        << "pebbles read as their ground tile";
    EXPECT_FALSE(decor_glyph(DECOR_BONES).has_value())
        << "bones read as their ground tile";
    EXPECT_FALSE(decor_glyph(DECOR_MAX).has_value())
        << "out-of-registry bytes inherit (hostile-plane hardening)";
    EXPECT_FALSE(decor_glyph(255).has_value());
}

TEST(GlyphMap, ascii_fallback_differs_from_unicode_for_some_tiles)
{
    // Wall is a solid block in Unicode but '#' in ASCII.
    Glyph wall = tile_glyph(TYPE_WALL);
    EXPECT_EQ(wall.pick(/*allow_unicode=*/false), static_cast<char32_t>('#'));
    EXPECT_NE(wall.pick(/*allow_unicode=*/true), static_cast<char32_t>('#'));
}
