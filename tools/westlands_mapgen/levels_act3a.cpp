/* War of the Westlands — Act IIIA, THE WAR IN THE WEST (levels 13-17).
 *
 * 13 The Plains of the Horse-lords (open cavalry battle on the war-road),
 * 14 The Wizard's Vale, 15 The Deeping Wall, 17 The Black Gate (the three
 * war stories moved from the Concept Playground — concept 606/605/609,
 * renumbered and adapted per the campaign design), and 16 The White City
 * (the act's set piece: a three-floor walled-city siege).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "builders.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

#include <cmath>

namespace westlands {
namespace {

// Burning-wreckage patches for the sacked lower town: the NEW LAVA tiles,
// variants picked deterministically like pavement (the ORANGE cycled band
// IS the fire's shimmer; lava is impassable to ground walkers, so every
// patch stays off the streets).
void paint_fire(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, ((x * 7 + y * 13) % 2) ? PIX_LAVA2 : PIX_LAVA1);
}

// 13 THE PLAINS OF THE HORSE-LORDS: open cavalry country. The war-road runs
// edge to edge; an orc column (team 2) is strung along it, marching stolen
// herds west toward its camp, with skeleton flankers on the north scrub and
// ghost outriders drifting ahead. The player's crew deploys on the road at
// the east edge behind the Ranger-King — and at the horn's call (delayed
// spawn 600) the Horse-lord and his riders fall on the column's northern
// flank from the NE horizon.
void build_plains_of_horse_lords(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(13, true, &hooks);
    init_world(level, 1, 90, 50);
    GameWorld& w = level.world();

    // The dry upland fringe, three copses (corners + north-center), meadow
    // sheen, trampled swathes flanking the road, the dew-pond, and the
    // sacked homestead's walls.
    paint_rect(w.grid, 0, 0, 89, 5, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 2, 2, 8, 6, PIX_TREE_M1);    // NW wood
    paint_rect(w.grid, 46, 2, 52, 6, PIX_TREE_M1);  // north-center wood
    paint_rect(w.grid, 84, 44, 88, 48, PIX_TREE_M1); // SE wood
    paint_rect(w.grid, 12, 12, 26, 18, PIX_GRASS_LIGHT_1); // cavalry meadows
    paint_rect(w.grid, 60, 32, 74, 40, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 64, 8, 76, 14, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 28, 20, 56, 22, PIX_GRASS_DARK_1); // trampled swathes
    paint_rect(w.grid, 28, 27, 56, 29, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 10, 42, 16, 46, PIX_WATER1); // the dew-pond
    paint_rect(w.grid, 58, 32, 63, 36, PIX_WALL2);  // the homestead ruin
    smooth_world(w);
    // The war-road, edge to edge; the ruin's interior, door and hearth; and
    // the torch standards the column planted along the road.
    paint_path(w.grid, 2, 24, 87, 25);
    paint_pavement(w.grid, 59, 33, 62, 35); // ruin interior
    paint_pavement(w.grid, 60, 36, 61, 36); // its door gap
    paint_decor(w, 0, 59, 33, DECOR_BRAZIER);    // the hearth
    for (int x : {30, 38, 46, 54})
    {
        paint_decor(w, 0, x, 22, DECOR_TORCH1);
        paint_decor(w, 0, x, 27, DECOR_TORCH1);
    }

    // The orc column (team 2): the main lattice astride the road, big-orc
    // ranks on the southern flank, fast skeleton flankers on the north
    // scrub, ghost outriders to the west, and the two column-masters on
    // the road itself.
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 6; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 32 + col * 4, 18 + row * 3,
                         2 + ((row + col) % 3));
    for (int i = 0; i < 4; ++i)
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 34 + i * 3, 34, 4 + (i % 2));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 34 + i * 3, 37, 4 + (i % 2));
    }
    // (F4 fresh-team calibration: skeleton flankers 2-4 -> 2-3 and the
    // west camps 5 -> 3 — at lvl 5 the camps re-filled the plain as fast
    // as the horn's riders could clear it; at 3 the west push can finish.)
    for (int i = 0; i < 5; ++i)
    {
        place_living(w, FAMILY_SKELETON, 2, 0, 34 + i * 5, 9, 2 + (i % 2));
        place_living(w, FAMILY_SKELETON, 2, 0, 36 + i * 5, 12, 2 + (i % 2));
    }
    place_living(w, FAMILY_GHOST, 2, 0, 26, 10, 3); // west outriders
    place_living(w, FAMILY_GHOST, 2, 0, 26, 38, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 20, 20, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 20, 30, 3);
    // The column-masters on the road itself; the northern one is NAMED —
    // "Herd-Reaver", the captain who drives the stolen herds (content
    // batch 2026-07: the war levels each carry one named orc captain at
    // the design doc's champion/vanguard post; naming the post leaves
    // every census and balance number untouched). Names serialize through
    // a 12-byte buffer — 11 chars exactly fills it.
    walker* herd_reaver = place_living(w, FAMILY_BIG_ORC, 2, 0, 46, 23, 8);
    if (herd_reaver != nullptr)
        herd_reaver->stats()->name = "Herd-Reaver";
    place_living(w, FAMILY_BIG_ORC, 2, 0, 46, 26, 8);
    place_generator(w, FAMILY_TENT, 2, 0, 6, 10, 3); // the column's camp
    place_generator(w, FAMILY_TENT, 2, 0, 6, 38, 3);
    place_generator(w, FAMILY_BONES, 2, 0, 8, 30, 3);

    // The allies (team 0): elf outriders screening the deployment, and the
    // Horse-lord's eight riders on the NE horizon — dormant until the
    // horn's call at tick 600. The Ranger-King marches west with the crew.
    place_living(w, FAMILY_ELF, 0, 0, 72, 12, 4);
    place_living(w, FAMILY_ELF, 0, 0, 72, 36, 4);
    place_living(w, FAMILY_ELF, 0, 0, 68, 8, 4);
    place_living(w, FAMILY_ELF, 0, 0, 68, 40, 4);
    // (F4: riders 5 -> 6 — the horn's relief must be able to TURN the
    // column, not merely join the grind; the tick-600 story beat stays.)
    static constexpr int riders13[8][2] = {{62, 2}, {65, 2}, {68, 2}, {74, 2},
                                           {77, 2}, {80, 2}, {65, 4}, {74, 4}};
    for (const auto& r : riders13)
        place_living(w, FAMILY_BARBARIAN, 0, 0, r[0], r[1], 6, false, false,
                     600);
    place_hero(w, FAMILY_SOLDIER, 0, 76, 24, 9, "Ranger-King", false, false,
               0);
    place_hero(w, FAMILY_BARBARIAN, 0, 70, 3, 9, "Horse-lord", false, false,
               600);

    // The crew deploys east, on and around the war-road (lead first).
    static constexpr int starts13[15][2] = {
        {78, 24}, {78, 21}, {78, 27}, {81, 20}, {81, 24}, {81, 28}, {84, 18},
        {84, 22}, {84, 26}, {84, 30}, {87, 20}, {87, 24}, {87, 28}, {81, 32},
        {81, 16}};
    for (const auto& s : starts13)
        place_start(w, 0, s[0], s[1]);

    // West road exit to the Wizard's Vale; east backtrack to the Falls.
    place_exit(w, 0, 2, 24, 14);
    place_exit(w, 0, 88, 24, 12);

    // The homestead's looted strongbox, and road provisions near the start.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 59, 34);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 61, 34);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 60, 35);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 62, 33);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 73, 24);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 75, 25);

    // The stony north fringe, and the wreckage of the column's passage.
    // The litter is scattered in two bands (y18-21 and y28-31) so the
    // war-road corridor y22-27 stays clear of jagged ground: the scatter
    // helper does not skip path tiles, and the split preserves the exact
    // (x*7+y*11) pattern the single y18-31 band would have painted there.
    scatter_boulders(w, 0, 0, 0, 89, 6, 17);
    scatter_litter(w, 0, 30, 18, 58, 21, 31);
    scatter_litter(w, 0, 30, 28, 58, 31, 31);
    // E7 ambience: cavalry country — tall meadow brush on the light-grass
    // sheens, the column's dead on the trampled swathes (real bone art on
    // the dark cells the litter left open), and the war-road's hoof-worn
    // pebbles edge to edge.
    scatter_decor(w, 0, 12, 12, 26, 18, 11, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 60, 32, 74, 40, 11, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 64, 8, 76, 14, 13, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 28, 18, 58, 31, 13, DECOR_BONES,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 2, 24, 87, 25, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 13, "The Plains of the Horse-lords",
                     {"The war-road runs west through",
                      "burning grass. An orc column",
                      "drives stolen herds to the",
                      "gates of the south. Break it.",
                      "At the horn's call, the",
                      "Horse-lord rides."},
                     5, 7000);
}

// 14 THE WIZARD'S VALE: a ringed vale — forest, garden ring, moat, cobble
// ring — around a four-story tower. Floors 1-2 are stone (floor 2 carpeted:
// the library), floor 3 is the glass observatory where the Traitor Wizard
// watches the war. The player's crew deploys in a besieging ring at the
// forest's edge; the tower (team 2) holds.
void build_wizards_vale(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(14, true, &hooks);
    init_world(level, 4, 60, 60);
    GameWorld& w = level.world();
    constexpr double cx = 29.5, cy = 29.5;

    // Floor 0 rings: forest, garden ring, moat, cobble ring, tower footprint.
    // (The garden ring is 4 tiles wide so the 2x2 crew markers on the 20.5
    // ring never clip the moat or the tree line.)
    paint_ring(w.grid, cx, cy, 22.5, 100.0, PIX_TREE_M1);
    paint_ring(w.grid, cx, cy, 18.5, 22.5, PIX_GRASS_LIGHT_1); // ring gardens
    paint_ring(w.grid, cx, cy, 15.5, 18.5, PIX_WATER1);
    paint_ring(w.grid, cx, cy, 0.0, 15.5, PIX_COBBLE_1);
    // Waystone glades among the trees (each gets a standing stone below).
    static constexpr int glades[3][2] = {{51, 42}, {8, 42}, {29, 4}};
    for (const auto& c : glades)
        paint_rect(w.grid, c[0] - 1, c[1] - 1, c[0] + 2, c[1] + 2, PIX_GRASS1);
    // The tower ground floor: walls with north and south doors.
    paint_rect(w.grid, 23, 23, 36, 36, PIX_WALL2);
    // Upper floors: air everywhere except the tower.
    for (int f = 1; f <= 3; ++f)
    {
        paint_rect(w.grid_for_floor(f), 0, 0, 59, 59, PIX_AIR);
        paint_rect(w.grid_for_floor(f), 23, 23, 36, 36, PIX_WALL2);
    }
    paint_rect(w.grid_for_floor(2), 26, 26, 33, 33, PIX_CARPET_M); // library
    smooth_world(w);
    // Post-smoothing decor: interiors, doors, causeways, glass observatory.
    paint_pavement(w.grid, 24, 24, 35, 35);
    paint_pavement(w.grid, 29, 23, 30, 23); // north door
    paint_pavement(w.grid, 29, 36, 30, 36); // south door
    paint_pavement(w.grid, 29, 9, 30, 15);  // causeways across the moat
    paint_pavement(w.grid, 29, 44, 30, 50);
    paint_pavement(w.grid, 9, 29, 15, 30);
    paint_pavement(w.grid, 44, 29, 50, 30);
    // The fountain court on the south approach: a paved court around a
    // water basin, cornered by columns (clear of the door wards' footprints).
    paint_pavement(w.grid, 27, 41, 32, 44);
    paint_rect(w.grid, 29, 42, 30, 43, PIX_WATER1);
    paint(w.grid, 26, 41, PIX_COLUMN1);
    paint(w.grid, 33, 41, PIX_COLUMN2);
    paint(w.grid, 26, 44, PIX_COLUMN2);
    paint(w.grid, 33, 44, PIX_COLUMN1);
    for (const auto& c : glades) // the standing stones
        paint_decor(w, 0, c[0], c[1], DECOR_BOULDER_1);
    // Tower interiors: pavement on floor 1, a pavement gallery around the
    // library carpet on floor 2, glass under the sky on floor 3.
    paint_pavement(w.grid_for_floor(1), 24, 24, 35, 35);
    paint_decor(w, 1, 27, 27, DECOR_BRAZIER);
    paint_decor(w, 1, 32, 32, DECOR_BRAZIER);
    paint_pavement(w.grid_for_floor(2), 24, 24, 35, 25);
    paint_pavement(w.grid_for_floor(2), 24, 34, 35, 35);
    paint_pavement(w.grid_for_floor(2), 24, 26, 25, 33);
    paint_pavement(w.grid_for_floor(2), 34, 26, 35, 33);
    paint_rect(w.grid_for_floor(3), 24, 24, 35, 35, PIX_GLASS);
    paint(w.grid_for_floor(3), 24, 24, PIX_COLUMN1); // observatory corners
    paint(w.grid_for_floor(3), 35, 24, PIX_COLUMN2);
    paint(w.grid_for_floor(3), 24, 35, PIX_COLUMN2);
    stair_pair(w, 0, 24, 24); // the interior stair chain spirals corner
    stair_pair(w, 1, 35, 24); // to corner: NW, then NE, then SE
    stair_pair(w, 2, 35, 35);

    // The tower (team 2): mages on every floor, golems at the doors, the
    // Traitor Wizard alone on the glass top — an archmage, ACT_GUARD, his
    // specials disabled so he cannot teleport off his tower (the same
    // rationale as the Grey Wizard's stand on the bridge).
    static constexpr int mage_f0[6][2] = {{26, 26}, {33, 26}, {26, 33},
                                          {33, 33}, {29, 30}, {30, 29}};
    static constexpr int mage_f1[6][2] = {{25, 25}, {34, 25}, {25, 34},
                                          {34, 34}, {25, 30}, {34, 30}};
    static constexpr int mage_f2[6][2] = {{25, 25}, {34, 25}, {25, 34},
                                          {34, 34}, {25, 29}, {34, 29}};
    // (F4 batch 2: floor mages 3-5/4-6 -> 3-4/4-5 and door golems 5 -> 4
    // — the ring must be breakable by a curve-7 crew fighting door by
    // door; the traitor on the glass keeps his lvl 10.)
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_MAGE, 2, 0, mage_f0[i][0], mage_f0[i][1],
                     3 + (i % 2), true);
        place_living(w, FAMILY_MAGE, 2, 1, mage_f1[i][0], mage_f1[i][1],
                     3 + (i % 2), true);
        place_living(w, FAMILY_MAGE, 2, 2, mage_f2[i][0], mage_f2[i][1],
                     4 + (i % 2), true);
    }
    place_living(w, FAMILY_ARCHMAGE, 2, 3, 29, 29, 10, true, true); // the traitor
    place_living(w, FAMILY_GOLEM, 2, 0, 26, 20, 4, true); // door wards
    place_living(w, FAMILY_GOLEM, 2, 0, 31, 20, 4, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 26, 37, 4, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 31, 37, 4, true);
    // (F4 fresh-team calibration: the tower musters 5 -> 3 — at lvl 5 the
    // pair OUTBRED the siege, 35 foes at contact growing to ~97 by tick
    // 6000; a besieged tower that multiplies is unwinnable at any power.
    // At 3 the answer thickens but the ring can still be broken.)
    place_generator(w, FAMILY_TOWER, 2, 1, 28, 26, 3);
    place_generator(w, FAMILY_TOWER, 2, 2, 28, 30, 3);

    // The player's crew: the lead pair on the south causeway (stacked — the
    // causeway is exactly one 2x2 marker wide), then a besieging ring through
    // the gardens — every other post of the old elf ring plus the druid
    // stations between them.
    place_start(w, 0, 29, 46);
    place_start(w, 0, 29, 48);
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i < 30; i += 2)
    {
        const double a = (2.0 * kPi * i) / 30.0;
        const int tx = static_cast<int>(std::lround(cx + 20.5 * std::cos(a)));
        const int ty = static_cast<int>(std::lround(cy + 20.5 * std::sin(a)));
        place_start(w, 0, tx, ty);
    }
    for (int i = 0; i < 6; ++i)
    {
        const double a = (2.0 * kPi * (i + 0.5)) / 6.0;
        const int tx = static_cast<int>(std::lround(cx + 20.5 * std::cos(a)));
        const int ty = static_cast<int>(std::lround(cy + 20.5 * std::sin(a)));
        place_start(w, 0, tx, ty);
    }

    // The observatory prize leads on to the Deeping Wall; the backtrack exit
    // on the garden ring at the south causeway's foot withdraws to the
    // Plains of the Horse-lords.
    place_exit(w, 3, 31, 31, 15);
    place_exit(w, 0, 29, 51, 13);
    // E7 ambience: the cage of rings in untended bloom — garden brush on
    // the light-grass annulus (the LightGrass ground picks the ring out
    // of the enclosing rect by itself), thick growth in the waystone
    // glades, and worn grit on the cobble ring at the tower's foot.
    scatter_decor(w, 0, 7, 7, 52, 52, 9, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    for (const auto& c : glades)
        scatter_decor(w, 0, c[0] - 1, c[1] - 1, c[0] + 2, c[1] + 2, 3,
                      DECOR_SHRUB, {ScatterGround::Grass});
    scatter_decor(w, 0, 9, 9, 50, 50, 21, DECOR_PEBBLES,
                  {ScatterGround::Cobble});
    save_level_files(w, 14, "The Wizard's Vale",
                     {"The traitor watches the war",
                      "from his glass crown. His vale",
                      "is a cage of rings. Break the",
                      "door wards, climb the tower,",
                      "and cast him down."},
                     6, 8000);
}

// 15 THE DEEPING WALL: a fortress wall crosses the map east-west. Floor 0 is
// the wall itself with a gate gap; floor 1 is the walkable rampart parapet
// bounded by air. The player's crew deploys along the wall under a stone
// keep; the horde (team 2) masses in the southern field — and at first light
// the White Rider arrives behind its eastern flank.
void build_deeping_wall(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(15, true, &hooks);
    init_world(level, 2, 80, 50);
    GameWorld& w = level.world();

    // Floor 0: keep courtyard (cobble) north, the wall, the field south.
    paint_rect(w.grid, 0, 0, 79, 17, PIX_COBBLE_1);
    paint_rect(w.grid, 0, 18, 79, 21, PIX_WALL2);
    paint_rect(w.grid, 30, 0, 49, 6, PIX_WALL2);        // the keep
    paint_rect(w.grid, 34, 2, 45, 4, PIX_CARPET_M);     // its long hall
    // Field texture: flanking woods, a pond, ground trampled by the horde.
    paint_rect(w.grid, 2, 25, 10, 33, PIX_TREE_M1);
    paint_rect(w.grid, 68, 43, 77, 48, PIX_TREE_M1);
    paint_rect(w.grid, 4, 42, 14, 48, PIX_WATER1);
    paint_rect(w.grid, 16, 24, 24, 28, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 56, 30, 66, 36, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 24, 45, 33, 48, PIX_GRASS_DARK_1);
    // Floor 1: a rampart strip directly over the wall, parapet edged by air.
    paint_rect(w.grid_for_floor(1), 0, 0, 79, 49, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 31, 1, 48, 1);                // keep interior ring
    paint_pavement(w.grid, 31, 5, 48, 5);                // around the hall
    paint_pavement(w.grid, 31, 2, 33, 4);
    paint_pavement(w.grid, 46, 2, 48, 4);
    paint_pavement(w.grid, 38, 6, 41, 6);                // keep door
    paint_pavement(w.grid, 39, 7, 40, 17);               // processional way
    paint_decor(w, 0, 37, 7, DECOR_BRAZIER);                  // keep-door braziers
    paint_decor(w, 0, 42, 7, DECOR_BRAZIER);
    paint_pavement(w.grid, 38, 18, 41, 21);              // the gate gap
    paint_path(w.grid, 38, 22, 41, 25);                  // the war-road, worn
    paint_path(w.grid, 39, 26, 40, 49);                  // to the gate
    paint_pavement(w.grid_for_floor(1), 0, 18, 79, 21);  // the rampart
    paint_decor(w, 1, 10, 18, DECOR_BRAZIER);    // rampart fire posts
    paint_decor(w, 1, 30, 18, DECOR_BRAZIER);
    paint_decor(w, 1, 50, 18, DECOR_BRAZIER);
    paint_decor(w, 1, 70, 18, DECOR_BRAZIER);
    stair_pair(w, 0, 20, 17); // up onto the rampart, north (courtyard) side
    stair_pair(w, 0, 60, 17);

    // The defense is yours (team-0 start markers in the garrison's posture):
    // the lead pair over the gate arch, a double rank plugging the gate,
    // archer posts along the parapet, reserves in the courtyard. Markers are
    // 32x32 (2x2 tiles), so anchors keep a tile of shoulder room from walls.
    place_start(w, 1, 38, 19);
    place_start(w, 1, 40, 19);
    place_start(w, 0, 38, 19); // the gate gap, plugged rank on rank
    place_start(w, 0, 40, 19);
    place_start(w, 0, 38, 21);
    place_start(w, 0, 40, 21);
    static constexpr int parapet_posts[8] = {8, 16, 24, 32, 46, 54, 62, 70};
    for (int i = 0; i < 8; ++i)
        place_start(w, 1, parapet_posts[i], 19);
    static constexpr int courtyard_posts[4] = {36, 38, 41, 43};
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, courtyard_posts[i], 16);
    static constexpr int courtyard_line2[4] = {37, 39, 41, 43};
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, courtyard_line2[i], 13);
    place_start(w, 0, 34, 16);
    place_start(w, 0, 37, 15);
    place_start(w, 0, 42, 15);
    place_start(w, 0, 45, 16);
    // Allies: the courtyard treehouse musters elves for the wall, and at
    // dawn (delayed spawn 500 ticks) the White Rider — the grey one, come
    // again — arrives at the east edge of the field, behind the horde's
    // flank. ("White Wizard" is 12 chars and overflows the name field;
    // the briefing sells the identity.)
    // (Generator levels here and below carry the E6 post-single-scaling
    // bump: spawns take set_difficulty once now, so the lvl the camps were
    // designed at — against double-scaled spawns — no longer buys the same
    // pressure. Level ~7 restores a lvl-5 camp's old rate x strength.)
    // (F4: the courtyard muster 5 -> 6 — the wall's own sustain has to
    // carry the garrison through the night the briefing promises.)
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 35, 9, 7);
    place_hero(w, FAMILY_ARCHMAGE, 0, 76, 33, 10, "White Rider", false, false,
               500);

    // The horde (team 2): orc ranks, big-orc flank, champions, siege camp.
    // (F4 batch 2: ranks 2-4 -> 2-3, flank bigs 3/4 -> 3, champions
    // 8 -> 7 — the wall's own garrison has to live to see the dawn beat.)
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 7; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 20 + col * 5, 30 + row * 2,
                         2 + ((row + col) % 2));
    for (int i = 0; i < 5; ++i)
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 22 + i * 6, 41, 3);
        place_living(w, FAMILY_BIG_ORC, 2, 0, 25 + i * 6, 43, 3);
    }
    // The champions before the gate; the western one is NAMED —
    // "Ram-Captain", who leads the ram to the gate (the war levels' named
    // orc captains, content batch 2026-07; see the Plains note).
    walker* ram_captain = place_living(w, FAMILY_BIG_ORC, 2, 0, 38, 28, 6);
    if (ram_captain != nullptr)
        ram_captain->stats()->name = "Ram-Captain";
    place_living(w, FAMILY_BIG_ORC, 2, 0, 42, 28, 6);
    // (F4: siege camp 7 -> 5 — the E6 lvl-7 bump refilled the field to a
    // standing ~55 against the whole garrison; at 5 the horde still owns
    // the night but the dawn can actually break it.)
    place_generator(w, FAMILY_TENT, 2, 0, 30, 46, 4); // the siege camp
    place_generator(w, FAMILY_TENT, 2, 0, 48, 46, 4);

    // The keep exit leads on to the White City; the backtrack exit at the
    // south end of the war-road withdraws through the horde to the Vale.
    place_exit(w, 0, 40, 3, 16);
    place_exit(w, 0, 40, 48, 14);
    scatter_boulders(w, 0, 0, 22, 18, 49, 17);  // rocky field margins
    scatter_boulders(w, 0, 52, 22, 79, 49, 13);
    // E7 ambience: the field has drunk sieges before this one — the last
    // war's dead across the trampled ground, and the war-road to the gate
    // stamped down to grit.
    scatter_decor(w, 0, 14, 22, 66, 44, 19, DECOR_BONES,
                  {ScatterGround::Grass, ScatterGround::DarkGrass});
    scatter_decor(w, 0, 38, 22, 41, 49, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 15, "The Deeping Wall",
                     {"Rain and drums in the dark.",
                      "The horde is come to the wall.",
                      "Hold the gate until the dawn.",
                      "At first light, look east:",
                      "the grey one rides again --",
                      "the White Rider comes."},
                     7, 9000);
}

// 16 THE WHITE CITY (the act's set piece): a three-floor walled-city siege.
// West to east: siege plain | outer wall (gate already BREACHED) | burning
// lower town | inner wall | citadel, with the White Tower rising through
// floors 1 and 2. The crew deploys in the torn gate with the Ranger-King as
// the anvil; the siege host funnels into the breach while ghosts flow over
// the walls; at tick 700 the Horse-lord's riders hit the camp from the NW.
// Then the climb: tower stairs, the beacon, and the ride to the Black Gate.
void build_white_city(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(16, true, &hooks);
    init_world(level, 3, 90, 50);
    GameWorld& w = level.world();
    PixieData& f1 = w.grid_for_floor(1);
    PixieData& f2 = w.grid_for_floor(2);

    // Floor 0, west to east: the trampled siege camp and plain, the outer
    // wall, the lower town, the inner wall, the citadel, and the White
    // Tower's ground storey.
    paint_rect(w.grid, 2, 12, 14, 36, PIX_DIRT_1); // trampled camp ground
    paint_rect(w.grid, 16, 16, 38, 18, PIX_GRASS_DARK_1); // trampled bands
    paint_rect(w.grid, 16, 31, 38, 33, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 40, 0, 44, 49, PIX_WALL2);  // the outer wall
    paint_rect(w.grid, 45, 0, 69, 49, PIX_COBBLE_1); // the lower town
    paint_rect(w.grid, 48, 4, 54, 9, PIX_WALL2);   // town buildings
    paint_rect(w.grid, 58, 3, 64, 8, PIX_WALL2);
    paint_rect(w.grid, 48, 14, 53, 18, PIX_WALL2);
    paint_rect(w.grid, 57, 12, 63, 17, PIX_WALL2);
    paint_rect(w.grid, 48, 32, 54, 37, PIX_WALL2);
    paint_rect(w.grid, 58, 33, 64, 38, PIX_WALL2);
    paint_rect(w.grid, 48, 42, 54, 47, PIX_WALL2);
    paint_rect(w.grid, 57, 41, 63, 46, PIX_WALL2);
    paint_rect(w.grid, 70, 0, 73, 49, PIX_WALL2);  // the inner wall
    paint_rect(w.grid, 74, 0, 89, 49, PIX_COBBLE_1); // the citadel
    paint_rect(w.grid, 78, 18, 87, 32, PIX_WALL2); // the White Tower, ground
    // Floors 1-2: open sky except the tower's second-storey shell.
    paint_rect(f1, 0, 0, 89, 49, PIX_AIR);
    paint_rect(f2, 0, 0, 89, 49, PIX_AIR);
    paint_rect(f1, 78, 18, 87, 32, PIX_WALL2);
    smooth_world(w);

    // Floor 0 carving and decor. THE BREACH: the outer gate, already torn
    // open; the narrower inner gate; the torch-lined approach road; the
    // main street and cross street; the burning wreckage (lava fire
    // patches, all off the streets); the tower door, interior, hall carpet
    // and door braziers; and the citadel court between gate and door.
    paint_pavement(w.grid, 40, 22, 44, 27); // the breach
    paint_pavement(w.grid, 70, 23, 73, 26); // the inner gate
    paint_path(w.grid, 2, 24, 39, 25);      // the approach road
    for (int x : {8, 15, 22, 29, 36})
    {
        paint_decor(w, 0, x, 21, DECOR_TORCH1);
        paint_decor(w, 0, x, 28, DECOR_TORCH1);
    }
    paint_pavement(w.grid, 45, 22, 69, 27); // the main street
    paint_pavement(w.grid, 55, 2, 56, 47);  // the cross street
    paint_fire(w.grid, 50, 10, 51, 11);     // fires in the lower town
    paint_fire(w.grid, 60, 19, 61, 20);
    paint_fire(w.grid, 50, 30, 51, 31);
    paint_fire(w.grid, 60, 39, 61, 40);
    paint_fire(w.grid, 46, 44, 47, 45);
    paint_fire(w.grid, 66, 6, 67, 7);
    paint_pavement(w.grid, 78, 24, 78, 25); // tower door, west face
    paint_pavement(w.grid, 79, 19, 86, 31); // tower interior
    paint_rect(w.grid, 81, 21, 84, 29, PIX_CARPET_M); // the hall carpet
    // (Historical note: "door braziers" at (77, 23)/(77, 26) were authored
    // here as combined tiles, but the citadel-court pavement on the next
    // line always painted straight over them — the shipped level never had
    // them. Decor would resurrect two blocking cells by the courtyard
    // door, so they stay dropped to preserve the balanced shipped layout.)
    paint_pavement(w.grid, 74, 20, 77, 30); // the citadel court
    // Floor 1: the two wall ramparts (the outer one arches over the gate),
    // fire posts, and the tower's first-storey interior.
    paint_pavement(f1, 40, 0, 44, 49); // outer rampart
    for (int y : {2, 8, 20, 29, 41, 47})
        paint_decor(w, 1, 42, y, DECOR_BRAZIER);
    paint_pavement(f1, 70, 0, 73, 49); // inner rampart
    for (int y : {2, 14, 35, 47})
        paint_decor(w, 1, 71, y, DECOR_BRAZIER);
    paint_pavement(f1, 79, 19, 86, 31); // tower first storey
    paint_decor(w, 1, 80, 20, DECOR_BRAZIER);
    paint_decor(w, 1, 85, 30, DECOR_BRAZIER);
    // Floor 2: the tower-top platform, its glass crown, the four corner
    // columns, and THE BEACON.
    paint_pavement(f2, 78, 18, 87, 32);
    paint_rect(f2, 81, 21, 84, 29, PIX_GLASS);
    paint(f2, 78, 18, PIX_COLUMN1);
    paint(f2, 87, 18, PIX_COLUMN2);
    paint(f2, 78, 32, PIX_COLUMN2);
    paint(f2, 87, 32, PIX_COLUMN1);
    paint_decor(w, 2, 82, 25, DECOR_BRAZIER); // the beacon
    // Stairs (after decor; the cells above are kept clear). The floor-0
    // wall stairs sit on the cobble beside the wall faces; their floor-1
    // twins are DOWN cells adjoining the rampart pavement (the same
    // adjacency trick as the Deeping Wall's courtyard stairs).
    stair_pair(w, 0, 45, 12); // town side up to the outer rampart (north)
    stair_pair(w, 0, 45, 37); // town side up to the outer rampart (south)
    stair_pair(w, 0, 74, 12); // citadel side up to the inner rampart (north)
    stair_pair(w, 0, 74, 37); // citadel side up to the inner rampart (south)
    stair_pair(w, 0, 79, 19); // tower ground -> first storey (NW corner)
    stair_pair(w, 1, 86, 31); // tower first storey -> the top (SE corner)

    // The siege host (team 2). The war-beasts lead the breach — giant
    // skeletons stand 4x4 tiles, so the second beast anchors at (37,24)
    // rather than the drafted (37,26): that keeps its whole footprint in
    // the gate span (the drafted cell would plant it on the wall).
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 37, 23, 9);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 37, 24, 9);
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_ORC, 2, 0, 36, 22 + i, 3); // the breach
        place_living(w, FAMILY_ORC, 2, 0, 38, 22 + i, 3); // column
    }
    // The breach champions; the northern one is NAMED — "Breach-Lord",
    // the captain who tore the gate and now drives the column through it
    // (the war levels' named orc captains, content batch 2026-07; the
    // briefing's sixth line names him in-fiction).
    walker* breach_lord = place_living(w, FAMILY_BIG_ORC, 2, 0, 34, 20, 8);
    if (breach_lord != nullptr)
        breach_lord->stats()->name = "Breach-Lord";
    place_living(w, FAMILY_BIG_ORC, 2, 0, 34, 29, 8);
    for (int row = 0; row < 6; ++row) // the plain lattice
        for (int col = 0; col < 5; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 18 + col * 4, 14 + row * 4,
                         2 + ((row + col) % 3));
    for (int i = 0; i < 5; ++i) // big-orc wings
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 20 + i * 3, 9, 4 + (i % 2));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 20 + i * 3, 41, 4 + (i % 2));
    }
    for (int i = 0; i < 7; ++i) // rear ranks, clear of the BONES gens at x8
    {
        place_living(w, FAMILY_SKELETON, 2, 0, 12 + i * 2, 15, 2 + (i % 3));
        place_living(w, FAMILY_SKELETON, 2, 0, 12 + i * 2, 34, 2 + (i % 3));
    }
    static constexpr int town_ghosts[6][2] = {{47, 10}, {53, 20}, {47, 30},
                                              {53, 40}, {65, 10}, {65, 40}};
    for (const auto& g : town_ghosts) // already over the walls
        place_living(w, FAMILY_GHOST, 2, 0, g[0], g[1], 4);
    static constexpr int plain_ghosts[4][2] = {{24, 20}, {24, 30}, {30, 15},
                                               {30, 35}};
    for (const auto& g : plain_ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, g[0], g[1], 3);
    // E6 post-single-scaling bump (see the Deeping Wall note): lvl-7 TENT
    // camps restore the ground pressure the lvl-5 design bought from
    // double-scaled spawns — silencing them stays the battle's second act.
    // The BONES dens stay at lvl 5 (the flyer-den rule, as at the Black
    // Gate): their ghosts skip every wall and at 7 they swarm the
    // Horse-lord's dawn relief to a man before it ever reaches the camps.
    place_generator(w, FAMILY_TENT, 2, 0, 4, 12, 7); // the siege camp
    place_generator(w, FAMILY_TENT, 2, 0, 4, 36, 7);
    place_generator(w, FAMILY_TENT, 2, 0, 4, 44, 7);
    place_generator(w, FAMILY_BONES, 2, 0, 8, 16, 5);
    place_generator(w, FAMILY_BONES, 2, 0, 8, 33, 5);

    // The garrison (team 0): wall guards on the outer rampart, archers on
    // the inner, the citadel's mage tower — and the Horse-lord's dawn
    // relief, dormant on the NW plain until tick 700. The Ranger-King
    // holds the breach mouth from inside as the anvil.
    for (int y : {4, 10, 16, 33, 39, 45})
        place_living(w, FAMILY_SOLDIER, 0, 1, 42, y, 4, true);
    for (int y : {6, 18, 31, 44})
        place_living(w, FAMILY_ARCHER, 0, 1, 71, y, 4, true);
    static constexpr int riders16[6][2] = {{4, 3}, {7, 3}, {13, 3},
                                           {16, 3}, {7, 6}, {13, 6}};
    for (const auto& r : riders16) // lvl 7 (E6): the dawn relief charges
        place_living(w, FAMILY_BARBARIAN, 0, 0, r[0], r[1], 7, false, false,
                     700); // straight into the hotter lvl-7 camps
    // (75,33), not the drafted (75,34): the tower generator stands 4x4
    // tiles, and at y34 its south row sat on (75,37) — the east ARRIVAL
    // cell of the citadel-south rampart stair (74,37) — sealing it for
    // anyone stepping down (the B2 stair-clearance audit flags it).
    place_generator(w, FAMILY_TOWER, 0, 0, 75, 33, 5);
    place_hero(w, FAMILY_SOLDIER, 0, 46, 24, 9, "Ranger-King", true, false,
               0);
    place_hero(w, FAMILY_BARBARIAN, 0, 10, 4, 9, "Horse-lord", false, false,
               700);

    // The crew (lead first): the breach line in the torn gate, main-street
    // reserves, rampart posts on both walls, and the citadel court.
    static constexpr int starts16[17][3] = {
        {0, 41, 23}, {0, 41, 25}, {0, 43, 23}, {0, 43, 25}, // the breach
        {0, 50, 24}, {0, 58, 24}, {0, 66, 24},              // main street
        {1, 42, 6},  {1, 42, 12}, {1, 42, 18},              // outer rampart
        {1, 42, 31}, {1, 42, 37}, {1, 42, 43},
        {1, 71, 10}, {1, 71, 38},                           // inner rampart
        {0, 76, 21}, {0, 76, 28}};                          // citadel court
    for (const auto& s : starts16)
        place_start(w, s[0], s[1], s[2]);

    // Light the beacon on the tower top, then ride for the Black Gate; the
    // backtrack exit at the west end of the road withdraws to the Wall.
    place_exit(w, 2, 82, 23, 17);
    place_exit(w, 0, 2, 24, 15);

    // The citadel stores (a defender's second wind) and the tower potions.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 75, 22);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 75, 28);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 77, 31);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 77, 19);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 82, 20);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 2, 85, 24);

    // Ruined-block rubble in the town (the street band y22-27 and the
    // scatter's entity clearance keep every route open) and boulders on
    // the siege plain.
    scatter_litter(w, 0, 46, 2, 69, 20, 33);
    scatter_litter(w, 0, 46, 30, 69, 47, 33);
    scatter_boulders(w, 0, 16, 2, 38, 47, 21);
    // E7 ambience: the sack in progress — the burned dead on the cobbles
    // beside each fire patch (hand-set, one per blaze), rubble-grit
    // across the citadel court, and the approach road stamped by the
    // siege columns.
    static constexpr int fallen16[6][2] = {{52, 10}, {59, 19}, {52, 31},
                                           {62, 39}, {46, 46}, {65, 5}};
    for (const auto& b : fallen16)
        paint_decor(w, 0, b[0], b[1], DECOR_BONES);
    scatter_decor(w, 0, 74, 0, 89, 49, 21, DECOR_PEBBLES,
                  {ScatterGround::Cobble});
    scatter_decor(w, 0, 2, 24, 39, 25, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 16, "The White City",
                     {"The White City burns. The gate",
                      "is breached; the war-beasts",
                      "are through. Hold the walls,",
                      "clear the streets, and climb",
                      "to the Tower. Light the beacon.",
                      "The Breach-Lord drives them on."},
                     8, 10000);
}

// 17 THE BLACK GATE: a north-south wall with a six-tile gate splits the map;
// gatehouse ramparts flank the gap on floor 1. The player's host deploys on
// the western meadows behind the Ranger-King; the legion (team 2) pours
// through the gate from the torch-lined, bone-littered eastern waste.
// THE FEINT: the battle is fought to turn every eye from the Bearer.
void build_black_gate(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(17, true, &hooks);
    init_world(level, 2, 90, 50);
    GameWorld& w = level.world();

    // Floor 0: grass west (woods at the corners, soft meadow bands), the
    // wall, blasted dirt waste east with one foul pool.
    paint_rect(w.grid, 50, 0, 89, 49, PIX_DIRT_1);
    paint_rect(w.grid, 2, 2, 10, 8, PIX_TREE_M1);
    paint_rect(w.grid, 2, 42, 12, 48, PIX_TREE_M1);
    paint_rect(w.grid, 2, 12, 9, 20, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 3, 28, 10, 36, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 74, 38, 84, 46, PIX_GRASS1); // fringe of the pool
    paint_rect(w.grid, 76, 40, 82, 44, PIX_WATER1);
    paint_rect(w.grid, 44, 0, 49, 49, PIX_WALL2);
    // Floor 1: the two gatehouse ramparts flanking the gate, edged by air.
    paint_rect(w.grid_for_floor(1), 0, 0, 89, 49, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 44, 22, 49, 27);              // the gate gap
    paint_path(w.grid, 2, 24, 43, 25);                   // the war-road west
    paint_path(w.grid, 50, 24, 86, 25);                  // and into the waste
    // Torch standards line the legion's approach like banners.
    for (int px = 54; px <= 75; px += 7)
    {
        paint_decor(w, 0, px, 21, DECOR_TORCH1);
        paint_decor(w, 0, px, 28, DECOR_TORCH1);
    }
    // Gate-mouth torches stand a step east at x 52 (Wave E5): x 51 sits in
    // the ramparts' fall shadow, and a blocking torch there would be a
    // wedged landing for anyone stepping off a gatehouse corner.
    paint_decor(w, 0, 52, 21, DECOR_TORCH1); // flanking the gate mouth
    paint_decor(w, 0, 52, 28, DECOR_TORCH1);
    paint_pavement(w.grid_for_floor(1), 43, 15, 50, 20); // north gatehouse
    paint_pavement(w.grid_for_floor(1), 43, 29, 50, 34); // south gatehouse
    // End parapets (Wave E5): where a gatehouse floor ends over the WALL
    // (x 44..49), stepping off would drop the faller onto the wall top —
    // an unstandable landing. A battlement wall closes those two ends of
    // each rampart; the meadow (west, x 43) and waste (east, x 50) edges
    // stay open sky, so wall-jumping off the gatehouse flanks still works.
    paint_rect(w.grid_for_floor(1), 44, 14, 49, 14, PIX_WALL2);
    paint_rect(w.grid_for_floor(1), 44, 21, 49, 21, PIX_WALL2);
    paint_rect(w.grid_for_floor(1), 44, 28, 49, 28, PIX_WALL2);
    paint_rect(w.grid_for_floor(1), 44, 35, 49, 35, PIX_WALL2);
    paint_decor(w, 1, 44, 15, DECOR_BRAZIER);    // watch fires
    paint_decor(w, 1, 49, 15, DECOR_BRAZIER);
    paint_decor(w, 1, 44, 34, DECOR_BRAZIER);
    paint_decor(w, 1, 49, 34, DECOR_BRAZIER);
    stair_pair(w, 0, 50, 17); // into the gatehouses from the east side
    stair_pair(w, 0, 50, 32);

    // The player's host on the western meadows, in the old parade's posture:
    // the banner pair leads, the barbarian vanguard, then thinned ranks —
    // soldiers, elf line, mage line, clerics — 30 posts for a full crew.
    place_start(w, 0, 41, 23);
    place_start(w, 0, 41, 26);
    for (int i = 0; i < 4; ++i)
        place_start(w, 0, 39, 18 + i * 4);
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 5; ++col)
            if ((row + col) % 2 == 0)
                place_start(w, 0, 24 + col * 3, 14 + row * 5);
    for (int i = 0; i < 7; i += 2)
        place_start(w, 0, 20, 12 + i * 4);
    for (int i = 0; i < 3; i += 2)
        place_start(w, 0, 17, 18 + i * 6);
    for (int i = 0; i < 6; i += 2)
        place_start(w, 0, 14, 14 + i * 4);
    for (int i = 0; i < 4; i += 2)
        place_start(w, 0, 11, 18 + i * 4);
    // The Ranger-King leads the host: on the meadow grass just north of the
    // banner pair, marching with it (not a guard; and not SAVE_ALL — if he
    // falls, the feint still holds).
    place_hero(w, FAMILY_SOLDIER, 0, 41, 20, 9, "Ranger-King", false, false, 0);
    // The host of the West itself (F4 fresh-team calibration): the parade
    // posture always DREW a host — thirty posts — but only the crew ever
    // stood in it, and a curve-8 party of eight was erased by tick 1500.
    // Six veteran banner-soldiers now march in the ranks; the feint stays
    // desperate (the legion still outnumbers the host four to one).
    static constexpr int host17[6][2] = {{36, 17}, {34, 24}, {36, 31},
                                         {33, 15}, {31, 22}, {33, 33}};
    for (const auto& h : host17)
        place_living(w, FAMILY_SOLDIER, 0, 0, h[0], h[1], 6);

    // The legion (team 2): orcs in and around the gate, big-orc wings,
    // skeletons and ghosts behind, champions on the ramparts, the east camp.
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_ORC, 2, 0, 46, 22 + i, 2 + (i % 3)); // in the
        place_living(w, FAMILY_ORC, 2, 0, 48, 22 + i, 2 + (i % 3)); // gate
    }
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 6; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 52 + col * 3, 20 + row * 5,
                         2 + ((row + col) % 3));
    for (int i = 0; i < 3; ++i)
    {
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 12, 3 + (i % 3));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 16, 3 + (i % 3));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 36, 4 + (i % 2));
        place_living(w, FAMILY_BIG_ORC, 2, 0, 54 + i * 4, 40, 4 + (i % 2));
    }
    for (int i = 0; i < 5; ++i)
    {
        place_living(w, FAMILY_SKELETON, 2, 0, 70, 15 + i * 5, 2 + (i % 3));
        place_living(w, FAMILY_SKELETON, 2, 0, 74, 15 + i * 5, 2 + (i % 3));
    }
    place_living(w, FAMILY_GHOST, 2, 0, 56, 8, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 60, 42, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 68, 10, 3);
    place_living(w, FAMILY_GHOST, 2, 0, 68, 40, 3);
    // The rampart champions hold the gatehouses as ACT_GUARD posts; the
    // northern one is NAMED — "Gate-Warden", the captain of the Black
    // Gate itself (the war levels' named orc captains, content batch
    // 2026-07). Both posts sit 4 tiles west of the (50,17)/(50,32) stair
    // arrivals — clear of the B2 stair-clearance audit's guard rule.
    walker* gate_warden = place_living(w, FAMILY_ORC, 2, 1, 46, 17, 8, true);
    if (gate_warden != nullptr)
        gate_warden->stats()->name = "Gate-Warden";
    place_living(w, FAMILY_ORC, 2, 1, 46, 32, 8, true);
    // E6 post-single-scaling bump (see the Deeping Wall note): the legion's
    // TENT camps keep the eastern waste a grind at lvl 7 now that spawns
    // take set_difficulty once. The BONES den stays at lvl 5: it spawns
    // FLYING Riders that skip the gate funnel and reach the western line
    // early, and any hotter wipes the smoke's stand-in crew before tick
    // 300 — the feint must feel desperate, not be a massacre.
    // (F4: tents 7 -> 5 alongside the six placed host soldiers — the waste
    // stays a grind, but a grind the war-road host can survive.)
    place_generator(w, FAMILY_TENT, 2, 0, 83, 13, 5);
    place_generator(w, FAMILY_BONES, 2, 0, 83, 23, 5);
    place_generator(w, FAMILY_TENT, 2, 0, 83, 34, 5);

    // Beyond the gate and the waste lies the Mountain of Fire (the
    // convergence — the Burden's Road arrives there too); the backtrack
    // exit at the west end of the war-road withdraws to the White City.
    place_exit(w, 0, 88, 24, 24);
    place_exit(w, 0, 2, 24, 16);
    scatter_boulders(w, 0, 51, 0, 89, 49, 23); // the blasted waste
    scatter_litter(w, 0, 52, 0, 89, 49, 29);   // strewn with old bones
    // E7 ambience: the briefing's "old bones" made literal — real bone
    // art on the waste dirt between the litter drifts, the war-road's
    // grit on both sides of the gate, and meadow brush on the host's own
    // green edge behind the deployment.
    scatter_decor(w, 0, 51, 0, 89, 49, 17, DECOR_BONES,
                  {ScatterGround::Dirt});
    scatter_decor(w, 0, 2, 24, 86, 25, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 2, 12, 10, 36, 15, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    save_level_files(w, 17, "The Black Gate",
                     {"We cannot win this fight, and",
                      "need not. Every eye of the",
                      "enemy must turn to us -- and",
                      "away from the Bearer. March",
                      "on the Black Gate. Buy the",
                      "hours. Buy them all."},
                     7, 9000);
}

} // namespace

void build_act3a(const LevelDataHooks& hooks)
{
    build_plains_of_horse_lords(hooks);
    build_wizards_vale(hooks);
    build_deeping_wall(hooks);
    build_white_city(hooks);
    build_black_gate(hooks);
}

std::vector<ExpectedLevel> act3a_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {13, 1, "The Plains of the Horse-lords", 15, 14, 0, 0, 0, 54, 3, 9, 0,
         true, {14, 12}},
        {14, 4, "The Wizard's Vale", 23, 0, 0, 0, 0, 23, 2, 0, 1, true, {15, 13}},
        {15, 2, "The Deeping Wall", 26, 1, 1, 0, 0, 47, 2, 1, 0, true, {16, 14}},
        {16, 3, "The White City", 17, 18, 1, 0, 0, 80, 5, 7, 0, true, {17, 15}},
        {17, 2, "The Black Gate", 30, 7, 0, 0, 0, 58, 3, 0, 0, true, {24, 16}},
    };
}

} // namespace westlands
