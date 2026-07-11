/* Westlands terrain tiles (PIX_SNOW/LAVA/MARSH/ASH 1-2) headless mechanics:
 * ID pins + legacy-header lockstep, genre mapping + autotiler inertness,
 * grid passability (snow/marsh/ash walkable; lava solid-to-ground but
 * flyer/weapon-passable), and an A*-routing smoke over a lava strip.
 * Single-floor byte-identity of the passability switch is covered by
 * og_test_parity; render membership (reflective/ripple/radar/weather) lives
 * in the SDL integration groups.
 */
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/og_file.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>

namespace og_test {
// Defined in test_new_tiles_legacy_ids.cpp (isolated TU over legacy/pixdefs.h).
std::array<int, 9> legacy_westlands_pix_ids();
} // namespace og_test

namespace {

// Fill the whole floor-0 grid with one tile byte.
void fill_grid(GameWorld& w, unsigned char tile)
{
    const int n = static_cast<int>(w.grid.w) * static_cast<int>(w.grid.h);
    std::fill(w.grid.data.get(), w.grid.data.get() + n, tile);
}

} // namespace

// Fixed IDs are persisted bytes in shipped grid PNGs: append-only, never
// renumber. The legacy header is a duplicate copy and must stay in lockstep.
TEST(NewTiles, ids_are_pinned_and_legacy_header_is_in_lockstep)
{
    ASSERT_EQ(142, PIX_SNOW1);
    ASSERT_EQ(143, PIX_SNOW2);
    ASSERT_EQ(144, PIX_LAVA1);
    ASSERT_EQ(145, PIX_LAVA2);
    ASSERT_EQ(146, PIX_MARSH1);
    ASSERT_EQ(147, PIX_MARSH2);
    ASSERT_EQ(148, PIX_ASH1);
    ASSERT_EQ(149, PIX_ASH2);
    ASSERT_EQ(150, PIX_MAX);

    const std::array<int, 9> core = {PIX_SNOW1,  PIX_SNOW2, PIX_LAVA1,
                                     PIX_LAVA2,  PIX_MARSH1, PIX_MARSH2,
                                     PIX_ASH1,   PIX_ASH2,  PIX_MAX};
    const std::array<int, 9> legacy = og_test::legacy_westlands_pix_ids();
    for (std::size_t i = 0; i < core.size(); i++)
        EXPECT_EQ(core[i], legacy[i])
            << "core/pixdefs.h and legacy/pixdefs.h drifted at slot " << i
            << " — edit both headers in lockstep";
}

// Each tile maps to its genre and the autotiler leaves the bytes alone
// (genre-inert, cliff/jagged precedent; variants are painted by the mapgen).
TEST(NewTiles, genres_map_and_autotiler_is_inert)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    const int n = static_cast<int>(w.grid.w) * static_cast<int>(w.grid.h);

    const struct
    {
        unsigned char pix;
        int genre;
    } rows[] = {
        {PIX_SNOW1, TYPE_SNOW},   {PIX_SNOW2, TYPE_SNOW},
        {PIX_LAVA1, TYPE_LAVA},   {PIX_LAVA2, TYPE_LAVA},
        {PIX_MARSH1, TYPE_MARSH}, {PIX_MARSH2, TYPE_MARSH},
        {PIX_ASH1, TYPE_ASH},     {PIX_ASH2, TYPE_ASH},
    };
    for (const auto& row : rows)
    {
        fill_grid(w, row.pix);
        EXPECT_EQ(row.genre, w.mysmoother.query_genre_x_y(5, 5))
            << "pix " << static_cast<int>(row.pix);
        w.mysmoother.smooth();
        bool untouched = true;
        for (int i = 0; i < n; i++)
            if (w.grid.data[i] != row.pix)
                untouched = false;
        EXPECT_TRUE(untouched)
            << "smooth() must be inert over pix " << static_cast<int>(row.pix);
    }
}

TEST(NewTiles, snow_marsh_ash_are_plain_walkable_ground)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(grounder, nullptr);
    grounder->setxy(100, 100);

    for (const int pix_id : {PIX_SNOW1, PIX_SNOW2, PIX_MARSH1, PIX_MARSH2,
                             PIX_ASH1, PIX_ASH2})
    {
        const unsigned char pix = static_cast<unsigned char>(pix_id);
        fill_grid(w, pix);
        EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
            << "pix " << static_cast<int>(pix)
            << " must be walkable for a ground unit";
    }
}

TEST(NewTiles, lava_is_solid_to_ground_but_flyer_and_weapon_pass)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();

    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* proj = w.add_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(grounder, nullptr);
    ASSERT_NE(proj, nullptr);
    grounder->setxy(100, 100);
    proj->setxy(100, 100);

    for (const int pix_id : {PIX_LAVA1, PIX_LAVA2})
    {
        const unsigned char pix = static_cast<unsigned char>(pix_id);
        fill_grid(w, pix);

        EXPECT_FALSE(w.query_grid_passable(100, 100, grounder))
            << "pix " << static_cast<int>(pix)
            << " must block a ground unit";

        EXPECT_TRUE(w.query_grid_passable(100, 100, proj))
            << "pix " << static_cast<int>(pix)
            << " must let projectiles fly over";

        grounder->stats()->set_bit_flags(BIT_FLYING, 1);
        EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
            << "pix " << static_cast<int>(pix) << " must let flyers cross";
        grounder->stats()->set_bit_flags(BIT_FLYING, 0);

        grounder->set_flight_left(10);
        EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
            << "pix " << static_cast<int>(pix)
            << " must let temporary flight cross";
        grounder->set_flight_left(0);

        grounder->stats()->set_bit_flags(BIT_ETHEREAL, 1);
        EXPECT_TRUE(w.query_grid_passable(100, 100, grounder))
            << "the ethereal early-out precedes every tile arm";
        grounder->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    }
}

// A* routing smoke (mirrors the CTF wall-detour test): a lava strip down
// grid column 12 (rows 0..14) blocks the straight line for a grounder —
// the solved path must detour around it and never touch lava — while a
// flyer's path crosses the strip directly.
TEST(NewTiles, grounder_routes_around_lava_strip_flyer_crosses)
{
    TestGameWorld tw;
    GameWorld& w = tw.world();
    for (int gy = 0; gy <= 14; gy++)
        w.grid.data[12 + gy * w.grid.w] = PIX_LAVA1;

    walker* grounder = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(grounder, nullptr);
    grounder->setxy(6 * GRID_SIZE, 10 * GRID_SIZE);
    grounder->find_path_to_point(19 * GRID_SIZE, 10 * GRID_SIZE);
    ASSERT_FALSE(grounder->path_to_foe.empty())
        << "a route around the lava strip exists (below row 14)";
    bool detoured = false;
    for (const PathState state : grounder->path_to_foe)
    {
        const int gx = GET_STATE_X(state) / GRID_SIZE;
        const int gy = GET_STATE_Y(state) / GRID_SIZE;
        EXPECT_FALSE(gx == 12 && gy <= 14)
            << "no grounder path node may sit on a lava tile (" << gx << ","
            << gy << ")";
        if (gy > 14)
            detoured = true;
    }
    EXPECT_TRUE(detoured) << "the only open route passes below the strip";

    walker* flyer = w.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(flyer, nullptr);
    flyer->stats()->set_bit_flags(BIT_FLYING, 1);
    flyer->setxy(6 * GRID_SIZE, 10 * GRID_SIZE);
    flyer->find_path_to_point(19 * GRID_SIZE, 10 * GRID_SIZE);
    ASSERT_FALSE(flyer->path_to_foe.empty());
    bool crossed_lava = false;
    for (const PathState state : flyer->path_to_foe)
    {
        const int gx = GET_STATE_X(state) / GRID_SIZE;
        const int gy = GET_STATE_Y(state) / GRID_SIZE;
        if (gx == 12 && gy <= 14)
            crossed_lava = true;
    }
    EXPECT_TRUE(crossed_lava)
        << "a flyer's straight route crosses the lava strip";
}

// Asset conformance: the 8 committed tile PNGs (scripts/generate_tile_art.py)
// load through read_pixie_file's strict our.pal check, and their index
// histograms pin the deliberate-shimmer design forever:
//  - LAVA flows via the ORANGE cycled band 224-231 (do_cycle IS the
//    animation) with static crust 232/233 + crack 134, and never touches
//    the WATER band;
//  - MARSH carries 1-8 WATER-band glint pixels (208/209 only);
//  - SNOW stays inside the white/grey ramp 27-31 and ASH never enters any
//    cycled band;
//  - index 0 (transparent-by-convention) never appears in an opaque floor.
TEST(NewTiles, committed_tile_art_loads_and_pins_palette_budgets)
{
    enum class Biome { Snow, Lava, Marsh, Ash };
    const struct
    {
        const char* file;
        Biome biome;
    } rows[] = {
        {"16snow1.png", Biome::Snow},   {"16snow2.png", Biome::Snow},
        {"16lava1.png", Biome::Lava},   {"16lava2.png", Biome::Lava},
        {"16marsh1.png", Biome::Marsh}, {"16marsh2.png", Biome::Marsh},
        {"16ash1.png", Biome::Ash},     {"16ash2.png", Biome::Ash},
    };
    for (const auto& row : rows)
    {
        const std::string label = row.file;
        PixieData p = read_pixie_file(row.file);
        ASSERT_TRUE(p.valid())
            << "pix/" << label << " must parse (256-entry our.pal palette)";
        ASSERT_EQ(1, static_cast<int>(p.frames)) << label;
        ASSERT_EQ(16, static_cast<int>(p.w)) << label;
        ASSERT_EQ(16, static_cast<int>(p.h)) << label;

        std::array<int, 256> hist{};
        for (int i = 0; i < 16 * 16; i++)
            hist[p.data[i]]++;

        EXPECT_EQ(0, hist[0]) << label << ": opaque floors never use index 0";
        int water_band = 0, orange_band = 0;
        for (int v = 208; v <= 223; v++)
            water_band += hist[static_cast<std::size_t>(v)];
        for (int v = 224; v <= 231; v++)
            orange_band += hist[static_cast<std::size_t>(v)];

        switch (row.biome)
        {
        case Biome::Snow:
        {
            int in_ramp = 0;
            for (int v = 27; v <= 31; v++)
                in_ramp += hist[static_cast<std::size_t>(v)];
            EXPECT_EQ(256, in_ramp)
                << label << ": snow must stay inside the 27..31 white ramp";
            break;
        }
        case Biome::Lava:
        {
            EXPECT_GE(orange_band, 256 * 40 / 100)
                << label << ": at least 40% of lava must flow (cycled band)";
            EXPECT_EQ(0, water_band)
                << label << ": lava may not touch the WATER cycled band";
            const int allowed = orange_band + hist[232] + hist[233] +
                hist[134];
            EXPECT_EQ(256, allowed)
                << label << ": lava uses only flow + crust 232/233 + crack 134";
            break;
        }
        case Biome::Marsh:
        {
            const int glints = hist[208] + hist[209];
            EXPECT_GE(glints, 1) << label << ": marsh needs a moving glint";
            EXPECT_LE(glints, 8) << label << ": glints stay sparse";
            EXPECT_EQ(glints, water_band)
                << label << ": only 208/209 may shimmer";
            EXPECT_EQ(0, orange_band)
                << label << ": marsh never touches the ORANGE band";
            break;
        }
        case Biome::Ash:
            EXPECT_EQ(0, water_band + orange_band)
                << label << ": ash is dead ground — nothing shimmers";
            break;
        }
    }
}
