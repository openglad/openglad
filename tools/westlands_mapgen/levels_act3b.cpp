/* War of the Westlands — Act IIIB, THE BURDEN'S ROAD (levels 19-23).
 *
 * The quiet half of the great branch: 19 The Dead Marshes, 20 The
 * Crossroads, 21 The Pass of the Spider, 22 The Tower of the Moon, 23 The
 * Ash Plains. Except for the Crossroads ambush (the branch's one pitched
 * battle) every level is CAN_EXIT | SAVE_ALL — the Burden's Road is about
 * passage, not extermination, and the named Bearer must live through all
 * of it. 19 walks the new MARSH tiles; 23 crosses ASH plains threaded by
 * impassable LAVA rivers.
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

namespace westlands {
namespace {

// --- New-tile painters (marsh / ash / lava). ---------------------------------
// The new Westlands ground tiles are genre-inert in the autotiler, so their
// two-variant texture is painted in deterministically, exactly like pavement.
void paint_marsh(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_MARSH1, PIX_MARSH2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

// Marsh only where plain grass survived the smoother: the shorelines
// (watergrass/grasswater), light/dark grass beds, and tree edges keep their
// shapes while the open ground drowns.
void marsh_over_grass(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_MARSH1, PIX_MARSH2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[x + y * g.w];
            if (t == PIX_GRASS1 || t == PIX_GRASS2 || t == PIX_GRASS3 ||
                t == PIX_GRASS4)
            {
                paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
            }
        }
}

void paint_ash(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_ASH1, PIX_ASH2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

void paint_lava(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_LAVA1, PIX_LAVA2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

// 19 THE DEAD MARSHES: a drowned battlefield of six meres, marsh-lights
// (ghosts) drifting over every pool and BONES generators sunk on the bog
// islands — extermination is impossible by design; you pick your way east
// along Sneak's causeway. CAN_EXIT + SAVE_ALL: the running fight is the
// level, and the Bearer must not touch the water.
void build_dead_marshes(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(19, true, &hooks);
    init_world(level, 1, 90, 50);
    GameWorld& w = level.world();
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);

    paint_rect(w.grid, 2, 2, 14, 12, PIX_GRASS_LIGHT_1); // firm start shelf NW
    paint_rect(w.grid, 4, 20, 12, 27, PIX_TREE_M1);      // drowned snag-woods
    paint_rect(w.grid, 20, 2, 30, 6, PIX_TREE_M1);
    paint_rect(w.grid, 70, 2, 80, 8, PIX_TREE_M1);
    paint_rect(w.grid, 78, 40, 87, 47, PIX_TREE_M1);
    paint_rect(w.grid, 10, 12, 28, 24, PIX_WATER1);      // the meres: P1
    paint_rect(w.grid, 38, 4, 58, 22, PIX_WATER1);       // P2, the great mere
    paint_rect(w.grid, 6, 30, 26, 44, PIX_WATER1);       // P3
    paint_rect(w.grid, 40, 34, 58, 46, PIX_WATER1);      // P4
    paint_rect(w.grid, 68, 18, 84, 24, PIX_WATER1);      // P5
    paint_rect(w.grid, 68, 30, 86, 44, PIX_WATER1);      // P6
    paint_rect(w.grid, 16, 16, 20, 20, PIX_GRASS_DARK_1); // bog islands: I1
    paint_rect(w.grid, 46, 10, 50, 14, PIX_GRASS_DARK_1); // I2
    paint_rect(w.grid, 46, 38, 50, 42, PIX_GRASS_DARK_1); // I3
    paint_rect(w.grid, 74, 34, 78, 38, PIX_GRASS_DARK_1); // I4
    smooth_world(w);
    // The marsh drowns whatever plain grass the smoother left standing.
    marsh_over_grass(w.grid, 0, 0, 89, 49);
    // The causeway — Sneak's secret way, a worn track snaking west to east
    // between the meres.
    paint_path(w.grid, 4, 6, 34, 8);    // seg A, east from the shelf
    paint_path(w.grid, 32, 9, 34, 30);  // seg B, south between P1 and P2
    paint_path(w.grid, 35, 28, 64, 30); // seg C, east between P2 and P4
    paint_path(w.grid, 62, 12, 64, 27); // seg D, north between P2 and P5
    paint_path(w.grid, 65, 12, 87, 14); // seg E, east to the far mouth
    // Marsh necks: wading bars from causeway ground to each generator island.
    paint_marsh(w.grid, 18, 21, 19, 24); // I1, south to the P1 shore
    paint_marsh(w.grid, 48, 15, 49, 22); // I2, south to the ground north of C
    paint_marsh(w.grid, 48, 34, 49, 37); // I3, north to the ground south of C
    paint_marsh(w.grid, 76, 39, 77, 44); // I4, south to the P6 south shore

    // Marsh-lights (team 2): ghosts drifting over every mere, levels 4/5
    // alternating, with one elder light in the great mere and one in P6.
    static constexpr int p1[5][2] = {{12, 15}, {24, 20}, {14, 22}, {26, 14},
                                     {22, 12}};
    for (int i = 0; i < 5; ++i)
        place_living(w, FAMILY_GHOST, 2, 0, p1[i][0], p1[i][1], 4 + (i % 2));
    static constexpr int p2[6][2] = {{40, 8},  {44, 18}, {52, 6},
                                     {56, 16}, {42, 14}, {54, 20}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_GHOST, 2, 0, p2[i][0], p2[i][1],
                     (i == 2) ? 6 : 4 + (i % 2));
    static constexpr int p3[4][2] = {{10, 34}, {18, 40}, {24, 32}, {14, 42}};
    static constexpr int p4[4][2] = {{42, 36}, {56, 40}, {44, 44}, {54, 36}};
    for (int i = 0; i < 4; ++i)
    {
        place_living(w, FAMILY_GHOST, 2, 0, p3[i][0], p3[i][1], 4 + (i % 2));
        place_living(w, FAMILY_GHOST, 2, 0, p4[i][0], p4[i][1], 4 + (i % 2));
    }
    place_living(w, FAMILY_GHOST, 2, 0, 72, 20, 5); // P5, the small mere
    place_living(w, FAMILY_GHOST, 2, 0, 80, 22, 5);
    static constexpr int p6[4][2] = {{70, 32}, {82, 40}, {72, 42}, {84, 34}};
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_GHOST, 2, 0, p6[i][0], p6[i][1],
                     (i == 1) ? 6 : 4 + (i % 2));
    // The marsh wakes: a second rising at tick 400 punishes dawdling.
    static constexpr int wake[6][2] = {{42, 6},  {50, 12}, {56, 8},
                                       {46, 40}, {52, 44}, {78, 36}};
    for (const auto& c : wake)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 5, false, false, 400);
    // The sunken dead: guard skeletons anchoring every shoreline pinch.
    static constexpr int watchers[10][2] = {
        {9, 13},  {29, 17}, {37, 10}, {59, 16}, {36, 25},
        {27, 32}, {39, 47}, {66, 22}, {85, 28}, {67, 38}};
    for (const auto& c : watchers)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3, true);
    // Mire-crawlers wandering the open bog.
    static constexpr int crawlers[4][2] = {{30, 40}, {50, 25}, {70, 15},
                                           {16, 28}};
    for (const auto& c : crawlers)
        place_living(w, FAMILY_SLIME, 2, 0, c[0], c[1], 3);
    // Ghost-light wellsprings, sunk on the bog islands (each 50x40 pile
    // exactly fills its island's interior).
    place_generator(w, FAMILY_BONES, 2, 0, 17, 17, 5); // I1
    place_generator(w, FAMILY_BONES, 2, 0, 47, 11, 5); // I2
    place_generator(w, FAMILY_BONES, 2, 0, 47, 39, 6); // I3
    place_generator(w, FAMILY_BONES, 2, 0, 75, 35, 6); // I4

    // The crew on the firm shelf, lead at the causeway mouth. ((8,11) sits
    // one tile west of the designed post: a 2x2 marker at (9,11) would clip
    // the P1 mere corner at (10,12).)
    place_start(w, 0, 10, 7);
    static constexpr int starts[9][2] = {{7, 5}, {7, 9},  {5, 3},
                                         {5, 7}, {5, 11}, {3, 5},
                                         {3, 9}, {9, 3},  {8, 11}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer shelters behind the marker line; SAVE_ALL protects him.
    place_hero(w, FAMILY_THIEF, 0, 3, 7, 5, "The Bearer", true, true, 0);

    // Rest-stones on the causeway, and a reward for braving I3's neck.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 33, 18);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 63, 20);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 50, 42);

    place_exit(w, 0, 86, 13, 20); // the far mouth, end of seg E
    place_exit(w, 0, 2, 4, 12);   // backtrack, on the shelf: the Falls
    // Leaning grave-stones and the bones of the old battle.
    scatter_boulders(w, 0, 0, 0, 89, 49, 31);
    scatter_litter(w, 0, 0, 0, 89, 49, 41);
    save_level_files(w, 19, "The Dead Marshes",
                     {"The straight road drowned here",
                      "an age ago. Dead faces burn",
                      "in the pools. Do not heed them.",
                      "Sneak knows the secret ways.",
                      "Keep the Bearer from the water."},
                     4, 6000);
}

// 20 THE CROSSROADS: a forest crossroads ambush on a southron column —
// golem war-beasts in the van, sixteen ranks of southrons behind, orc
// outriders on the glades. At tick 250 the rangers of the wood spring from
// the east glade and catch the column in a crossfire. The branch's one
// pitched battle: classic kill-all (type 0), then take the north road.
void build_crossroads(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(20, true, &hooks);
    init_world(level, 1, 80, 50);
    GameWorld& w = level.world();

    paint_rect(w.grid, 0, 0, 79, 49, PIX_TREE_M1);  // forest everywhere
    paint_rect(w.grid, 30, 18, 50, 32, PIX_GRASS1); // the crossroads clearing
    paint_rect(w.grid, 6, 20, 16, 30, PIX_GRASS1);  // W ambush glade (player)
    paint_rect(w.grid, 62, 20, 72, 30, PIX_GRASS1); // E ranger glade
    paint_rect(w.grid, 18, 6, 26, 12, PIX_GRASS1);  // NW glade
    paint_rect(w.grid, 52, 38, 60, 44, PIX_GRASS1); // SE glade
    paint_rect(w.grid, 44, 4, 54, 12, PIX_GRASS1);  // N pond glade
    paint_rect(w.grid, 34, 44, 46, 48, PIX_GRASS1); // S camp clearing
    paint_rect(w.grid, 37, 0, 42, 49, PIX_GRASS1);  // N-S road bed
    paint_rect(w.grid, 0, 23, 79, 27, PIX_GRASS1);  // E-W road bed
    paint_rect(w.grid, 46, 6, 52, 10, PIX_WATER1);  // the pond
    smooth_world(w);
    paint_path(w.grid, 38, 0, 41, 49); // the north-south road
    paint_path(w.grid, 0, 24, 79, 26); // the east-west road
    // The old crossroads king: broken paving at the crossing, the headless
    // plinth, and the fallen, crowned head on the east verge.
    paint_pavement(w.grid, 37, 23, 42, 27);
    paint(w.grid, 36, 22, PIX_COLUMN1);
    paint_decor(w, 0, 43, 28, DECOR_BOULDER_2);

    // The southron column (team 2), marching north up the road. The
    // war-beasts are 48px wide (3x3 tiles): the van pair anchors at
    // (37,14)/(40,14) so both fit the carved road bed, and the mid-column
    // beast sits at (39,31), one tile north of the designed cell, clear of
    // the rank at (41,34).
    place_living(w, FAMILY_GOLEM, 2, 0, 37, 14, 7); // war-beasts, van
    place_living(w, FAMILY_GOLEM, 2, 0, 40, 14, 7);
    place_living(w, FAMILY_GOLEM, 2, 0, 39, 31, 7); // mid-column
    for (int y = 18; y <= 46; y += 4) // paired southron files, ranks 3/4
    {
        place_living(w, FAMILY_BARBARIAN, 2, 0, 38, y, 3 + ((y / 4) % 2));
        place_living(w, FAMILY_BARBARIAN, 2, 0, 41, y, 3 + ((y / 4) % 2));
    }
    static constexpr int captains[4][2] = {{39, 20}, {40, 28}, {39, 36},
                                           {40, 44}};
    for (const auto& c : captains)
        place_living(w, FAMILY_BIG_ORC, 2, 0, c[0], c[1], 5);
    static constexpr int outriders[4][2] = {{30, 24}, {30, 26}, {48, 24},
                                            {48, 26}};
    for (const auto& c : outriders) // outriders on the W+E road
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 4);
    static constexpr int pickets[4][2] = {{20, 8}, {24, 10}, {54, 40},
                                          {58, 42}};
    for (const auto& c : pickets)   // glade pickets
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 3);
    place_living(w, FAMILY_ORC, 2, 0, 36, 47, 3); // rear guard, at the camp
    place_living(w, FAMILY_ORC, 2, 0, 44, 47, 3);
    // The rear camp trickles reinforcements until stormed. (One tile south
    // of the designed cell: the 2x2 tent at (40,46) would sit on the
    // barbarian rank at (41,46).)
    place_generator(w, FAMILY_TENT, 2, 0, 40, 47, 4);

    // The rangers of the wood (team 0): delayed-spawn friendlies who spring
    // from the east glade at tick 250 — the story beat and the balance valve.
    static constexpr int rangers[6][2] = {{63, 22}, {66, 22}, {69, 22},
                                          {63, 28}, {66, 28}, {69, 28}};
    for (const auto& c : rangers)
        place_living(w, FAMILY_ARCHER, 0, 0, c[0], c[1], 4, false, false, 250);
    place_living(w, FAMILY_ARCHER, 0, 0, 71, 24, 5, false, false, 250);
    place_living(w, FAMILY_ARCHER, 0, 0, 71, 26, 5, false, false, 250);

    // The crew in the west glade, lead on the glade lip facing the crossing.
    place_start(w, 0, 14, 24);
    static constexpr int starts[11][2] = {{14, 27}, {12, 21}, {12, 25},
                                          {12, 29}, {10, 23}, {10, 27},
                                          {8, 21},  {8, 25},  {8, 29},
                                          {6, 23},  {6, 27}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    place_exit(w, 0, 39, 1, 21); // the head of the north road
    place_exit(w, 0, 1, 25, 19); // backtrack, west end of the E-W road
    // Old ambush wrack on the EAST verge only (litter blocks all movement:
    // both roads and the west approach stay clean), stones in the clearing
    // margins.
    scatter_boulders(w, 0, 30, 18, 50, 32, 27);
    scatter_litter(w, 0, 44, 30, 60, 44, 33);
    save_level_files(w, 20, "The Crossroads",
                     {"Sneak spies them out: a",
                      "southron column crawling",
                      "north, war-beasts of stone",
                      "in the van. Strike from the",
                      "green shade; the wood strikes",
                      "with you."},
                     4, 4500);
}

// 21 THE PASS OF THE SPIDER: a web-choked defile carved out of cliff, in
// three acts — thrall pickets at the pinches, the Hollow where THE LURKER
// (a lvl-10 slime that will not chase) broods over its splitting young, and
// the throat where Sneak turns his coat at the last daylight. CAN_EXIT +
// SAVE_ALL: shepherd the Bearer through and run the last stretch bloodied.
void build_pass_of_the_spider(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(21, true, &hooks);
    init_world(level, 1, 90, 40);
    GameWorld& w = level.world();
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);

    // The defile is carved OUT of cliff: bury everything, then cut the way.
    paint_rect(w.grid, 0, 0, 89, 39, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 0, 0, 89, 39, PIX_WALL2);
    paint_rect(w.grid, 0, 16, 12, 25, PIX_DIRT_DARK_1);  // C1 west mouth
    paint_rect(w.grid, 13, 18, 28, 23, PIX_DIRT_DARK_1); // C2 corridor east
    paint_rect(w.grid, 20, 6, 25, 17, PIX_DIRT_DARK_1);  // C3 north branch
    paint_rect(w.grid, 16, 2, 31, 8, PIX_DIRT_DARK_1);   // L, the web larder
    paint_rect(w.grid, 26, 24, 31, 34, PIX_DIRT_DARK_1); // C4 south turn
    paint_rect(w.grid, 32, 29, 52, 34, PIX_DIRT_DARK_1); // C5 low corridor
    paint_rect(w.grid, 50, 12, 68, 30, PIX_DIRT_DARK_1); // H, the Hollow
    paint_rect(w.grid, 69, 18, 83, 23, PIX_DIRT_DARK_1); // C7 exit throat
    paint_rect(w.grid, 84, 16, 89, 25, PIX_DIRT_DARK_1); // east mouth
    smooth_world(w);
    // The worn track, mouth to mouth.
    paint_path(w.grid, 13, 20, 28, 21); // through C2
    paint_path(w.grid, 28, 24, 29, 34); // down C4
    paint_path(w.grid, 32, 31, 52, 32); // along C5
    paint_path(w.grid, 51, 24, 52, 30); // up into H from the C5 junction
    paint_path(w.grid, 52, 21, 68, 22); // across H
    paint_path(w.grid, 69, 20, 89, 21); // out C7 to the east mouth
    paint_decor(w, 0, 12, 17, DECOR_TORCH1);  // guttering torches at the
    paint_decor(w, 0, 12, 24, DECOR_TORCH1);  // C1 -> C2 narrowing

    // THE LURKER (team 2, named): it does not chase; the brood does, and
    // every slime killed splits into medium then small — the chamber fight
    // swells before it ends.
    walker* lurker = place_living(w, FAMILY_SLIME, 2, 0, 59, 20, 10, true);
    if (lurker != nullptr)
        lurker->stats()->name = "The Lurker";
    static constexpr int brood[8][2] = {{54, 15}, {64, 15}, {54, 26},
                                        {64, 26}, {58, 13}, {62, 28},
                                        {52, 18}, {66, 21}};
    for (const auto& c : brood)
        place_living(w, FAMILY_SLIME, 2, 0, c[0], c[1], 5);
    static constexpr int hatchlings[6][2] = {{21, 10}, {24, 12}, {36, 30},
                                             {46, 33}, {72, 19}, {78, 22}};
    for (const auto& c : hatchlings) // C3, C5 and C7 skitterers
        place_living(w, FAMILY_SMALL_SLIME, 2, 0, c[0], c[1], 4);
    // Skeleton thralls: pass pickets, larder wards, the C4 bend, wanderers
    // in the Hollow, and the throat wards.
    static constexpr int pickets[4][2] = {{16, 19}, {16, 22}, {38, 31},
                                          {44, 32}};
    for (const auto& c : pickets)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3, true);
    place_living(w, FAMILY_SKELETON, 2, 0, 18, 7, 4, true); // larder wards
    place_living(w, FAMILY_SKELETON, 2, 0, 29, 7, 4, true);
    place_living(w, FAMILY_SKELETON, 2, 0, 28, 27, 3, true); // the C4 bend
    place_living(w, FAMILY_SKELETON, 2, 0, 29, 31, 3, true);
    place_living(w, FAMILY_SKELETON, 2, 0, 56, 24, 4); // loose in the Hollow
    place_living(w, FAMILY_SKELETON, 2, 0, 62, 17, 4);
    place_living(w, FAMILY_SKELETON, 2, 0, 74, 21, 4, true); // throat wards
    place_living(w, FAMILY_SKELETON, 2, 0, 80, 20, 4, true);
    // THE BETRAYAL: Sneak lurks at the throat's end, between the party and
    // daylight — a named team-2 thief, guard-anchored, specials disabled.
    walker* sneak = place_living(w, FAMILY_THIEF, 2, 0, 82, 20, 8, true, true);
    if (sneak != nullptr)
        sneak->stats()->name = "Sneak";
    // Ghosts rise off the bone-drifts at the rear of the larder.
    place_generator(w, FAMILY_BONES, 2, 0, 27, 2, 4);

    // The crew at the west mouth, lead at the C1 -> C2 narrowing.
    place_start(w, 0, 9, 20);
    static constexpr int starts[8][2] = {{7, 17}, {7, 20}, {7, 23}, {5, 17},
                                         {5, 20}, {5, 23}, {3, 17}, {3, 23}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    place_hero(w, FAMILY_THIEF, 0, 3, 20, 5, "The Bearer", true, true, 0);

    // The larder: what the Lurker keeps.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 18, 3);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 20, 5);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 22, 3);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 24, 5);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 26, 3);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 28, 5);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 17, 6);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 30, 6);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 23, 7);

    place_exit(w, 0, 87, 20, 22); // the east mouth, past Sneak
    place_exit(w, 0, 1, 20, 20);  // backtrack, the west mouth
    // Web curtains (jagged litter blocks ALL movement — they wall the
    // Hollow's dark rims, never the track: rows 18-23 stay clear), bone
    // drifts in the larder, cocoon boulders along the low way.
    scatter_litter(w, 0, 52, 12, 68, 13, 4); // north rim
    scatter_litter(w, 0, 66, 14, 67, 17, 2); // east rim, above the throat
    scatter_litter(w, 0, 66, 24, 67, 27, 2); // east rim, below the throat
    scatter_litter(w, 0, 16, 2, 31, 4, 6);   // bone drifts
    scatter_boulders(w, 0, 13, 18, 52, 34, 17);
    save_level_files(w, 21, "The Pass of the Spider",
                     {"Sneak swears the pass is safe.",
                      "The dark here is old and",
                      "hungry; it drinks the light.",
                      "Old bones hang in the webs.",
                      "Watch the guide. Watch above."},
                     4, 5000);
}

// 22 THE TOWER OF THE MOON: a pale 4-floor tower in a walled court — the
// small-squad infiltration (eight markers only). Each floor is a small
// elite room fight; two TOWER alarm generators thicken the answer behind a
// slow squad; the Moon Warden (archmage, specials ENABLED — his teleport IS
// the fight) waits on the glass moon-disc. CAN_EXIT + SAVE_ALL: climb
// quiet, take the high door, and leave him raging.
void build_tower_of_the_moon(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(22, true, &hooks);
    init_world(level, 4, 60, 50);
    GameWorld& w = level.world();
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);

    // Floor 0, the pale court: dead land outside the walls, the bailey
    // ring, withered garths, and the solid tower footprint.
    paint_rect(w.grid, 0, 0, 59, 49, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 2, 2, 6, 10, PIX_TREE_M1);   // dead woods
    paint_rect(w.grid, 53, 30, 58, 44, PIX_TREE_M1);
    paint_rect(w.grid, 8, 6, 51, 7, PIX_WALL2);     // the bailey wall ring
    paint_rect(w.grid, 8, 42, 51, 43, PIX_WALL2);
    paint_rect(w.grid, 8, 8, 9, 41, PIX_WALL2);
    paint_rect(w.grid, 50, 8, 51, 41, PIX_WALL2);
    paint_rect(w.grid, 10, 8, 49, 41, PIX_DIRT_DARK_1); // the court floor
    paint_rect(w.grid, 12, 10, 18, 14, PIX_GRASS_DARK_1); // withered garths
    paint_rect(w.grid, 41, 10, 47, 14, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 12, 35, 18, 39, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 41, 35, 47, 39, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 22, 16, 37, 31, PIX_WALL2);  // the tower, solid
    // Floors 1-3: air everywhere except the tower shaft; the scriptorium
    // carpet on floor 2 autotiles inside its ring.
    for (int f = 1; f <= 3; ++f)
    {
        paint_rect(w.grid_for_floor(f), 0, 0, 59, 49, PIX_AIR);
        paint_rect(w.grid_for_floor(f), 22, 16, 37, 31, PIX_WALL2);
    }
    paint_rect(w.grid_for_floor(2), 26, 20, 33, 27, PIX_CARPET_M);
    smooth_world(w);
    // Court pavement: frame rects around the four garths and the tower.
    paint_pavement(w.grid, 10, 8, 49, 9);
    paint_pavement(w.grid, 10, 10, 11, 14);
    paint_pavement(w.grid, 19, 10, 40, 14);
    paint_pavement(w.grid, 48, 10, 49, 14);
    paint_pavement(w.grid, 10, 15, 49, 15);
    paint_pavement(w.grid, 10, 16, 21, 31);
    paint_pavement(w.grid, 38, 16, 49, 31);
    paint_pavement(w.grid, 10, 32, 49, 34);
    paint_pavement(w.grid, 10, 35, 11, 39);
    paint_pavement(w.grid, 19, 35, 40, 39);
    paint_pavement(w.grid, 48, 35, 49, 39);
    paint_pavement(w.grid, 10, 40, 49, 41);
    paint_pavement(w.grid, 28, 42, 31, 43); // the south gate gap
    paint_pavement(w.grid, 28, 31, 31, 31); // the tower door, south face
    paint_decor(w, 0, 27, 41, DECOR_BRAZIER);    // moon braziers at the gate
    paint_decor(w, 0, 32, 41, DECOR_BRAZIER);
    paint_decor(w, 0, 27, 32, DECOR_TORCH1);      // torches flanking the door
    paint_decor(w, 0, 32, 32, DECOR_TORCH1);
    paint_path(w.grid, 28, 44, 31, 47); // the approach road, south from the
    paint_path(w.grid, 2, 46, 31, 47);  // gate, west along the pass road
    // Tower interiors: pavement halls on floors 0 and 1; floor 2's pavement
    // frames the scriptorium carpet.
    paint_pavement(w.grid, 23, 17, 36, 30);
    paint_pavement(w.grid_for_floor(1), 23, 17, 36, 30);
    paint_decor(w, 1, 24, 18, DECOR_BRAZIER);
    paint_decor(w, 1, 35, 29, DECOR_BRAZIER);
    paint_pavement(w.grid_for_floor(2), 23, 17, 36, 19);
    paint_pavement(w.grid_for_floor(2), 23, 28, 36, 30);
    paint_pavement(w.grid_for_floor(2), 23, 20, 25, 27);
    paint_pavement(w.grid_for_floor(2), 34, 20, 36, 27);
    paint_decor(w, 2, 24, 29, DECOR_BRAZIER);
    paint_decor(w, 2, 35, 18, DECOR_BRAZIER);
    // Floor 3, the moon-court: an open roof, the glass moon-disc looking
    // down into the scriptorium, corner finials.
    paint_pavement(w.grid_for_floor(3), 22, 16, 37, 31);
    paint_rect(w.grid_for_floor(3), 27, 21, 32, 26, PIX_GLASS);
    paint(w.grid_for_floor(3), 22, 16, PIX_COLUMN1);
    paint(w.grid_for_floor(3), 37, 31, PIX_COLUMN1);
    paint(w.grid_for_floor(3), 37, 16, PIX_COLUMN2);
    paint(w.grid_for_floor(3), 22, 31, PIX_COLUMN2);
    stair_pair(w, 0, 23, 17); // the spiral, corner to corner: NW,
    stair_pair(w, 1, 36, 17); // then NE,
    stair_pair(w, 2, 36, 30); // then SE up to the moon-court

    // The garrison (team 2), sparse and elite. Court sentries drift; the
    // tower wakes (alarm-echo ghosts) at tick 300.
    static constexpr int sentries[4][2] = {{14, 20}, {45, 20}, {14, 33},
                                           {45, 33}};
    for (const auto& c : sentries)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 6);
    place_living(w, FAMILY_GHOST, 2, 0, 20, 25, 7, false, false, 300);
    place_living(w, FAMILY_GHOST, 2, 0, 39, 25, 7, false, false, 300);
    place_living(w, FAMILY_GOLEM, 2, 0, 26, 33, 6, true); // door wards; the
    place_living(w, FAMILY_GOLEM, 2, 0, 33, 33, 6, true); // gate lane stays open
    place_living(w, FAMILY_MAGE, 2, 0, 26, 20, 5, true);  // the ground hall
    place_living(w, FAMILY_MAGE, 2, 0, 33, 20, 5, true);
    place_living(w, FAMILY_MAGE, 2, 0, 29, 27, 5, true);
    static constexpr int first_floor[4][2] = {{26, 19}, {33, 19}, {26, 28},
                                              {33, 28}};
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_MAGE, 2, 1, first_floor[i][0], first_floor[i][1],
                     5 + (i / 2), true);
    static constexpr int scriptorium[4][2] = {{25, 18}, {34, 18}, {25, 29},
                                              {34, 29}};
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_MAGE, 2, 2, scriptorium[i][0], scriptorium[i][1],
                     6 + (i / 2), true);
    place_living(w, FAMILY_GHOST, 2, 2, 29, 19, 7); // scriptorium wards
    place_living(w, FAMILY_GHOST, 2, 2, 30, 28, 7);
    // The summit: the Moon Warden on the glass disc, his pale riders beside.
    // His archmage specials stay ENABLED — the teleport is the boss texture.
    walker* warden = place_living(w, FAMILY_ARCHMAGE, 2, 3, 29, 23, 9, true);
    if (warden != nullptr)
        warden->stats()->name = "Moon Warden";
    place_living(w, FAMILY_GHOST, 2, 3, 25, 25, 8);
    place_living(w, FAMILY_GHOST, 2, 3, 34, 21, 8);
    // The TOWER alarms: each musters mages while it stands — the longer the
    // squad lingers, the thicker the tower's answer.
    place_generator(w, FAMILY_TOWER, 2, 1, 29, 23, 5);
    place_generator(w, FAMILY_TOWER, 2, 2, 29, 23, 6);

    // The squad on the south road outside the gate, lead at the gate's
    // mouth — eight markers only, the small-squad level. (The rear pair
    // anchors at row 47, not the designed 48: a 2x2 marker at row 48 would
    // touch the map's south pixel bound and fail the footing audit.)
    place_start(w, 0, 29, 44);
    static constexpr int starts[7][2] = {{25, 45}, {33, 45}, {22, 46},
                                         {36, 46}, {29, 46}, {25, 47},
                                         {33, 47}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    place_hero(w, FAMILY_THIEF, 0, 29, 48, 5, "The Bearer", true, true, 0);

    // The infiltrator's draught in the ground hall, provisions above, and
    // moon-silver on the scriptorium carpet.
    place(w, Order::Treasure, FAMILY_INVIS_POTION, 0, 0, 29, 24);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 26, 26);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 2, 27, 21);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 2, 32, 21);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 2, 27, 26);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 2, 32, 26);

    place_exit(w, 3, 23, 17, 23); // the high door, NW of the moon-court
    place_exit(w, 0, 3, 46, 21);  // backtrack, west end of the pass road
    // Stones on the dead land outside the walls only (the court is paved).
    scatter_boulders(w, 0, 0, 0, 7, 49, 21);
    scatter_boulders(w, 0, 52, 0, 59, 49, 21);
    scatter_boulders(w, 0, 8, 0, 51, 5, 21);
    scatter_boulders(w, 0, 8, 44, 51, 49, 21);
    save_level_files(w, 22, "The Tower of the Moon",
                     {"The pale tower never sleeps.",
                      "Its cold eye sweeps the road.",
                      "Eight may pass where an army",
                      "cannot. Climb quiet, strike",
                      "quick, and take the high door."},
                     3, 4500);
}

// 23 THE ASH PLAINS: a stealth-march that keeps failing. Ash from edge to
// edge, two impassable LAVA rivers (plus two tributaries) crossed only at
// trodden-crust fords, three roaming patrol columns on the lanes, wraiths
// riding the fire where ground crews cannot answer, and camps that never
// let the plain empty. CAN_EXIT is the win, SAVE_ALL the constraint,
// attrition the texture. East. Always east.
void build_ash_plains(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(23, true, &hooks);
    init_world(level, 1, 90, 50);
    GameWorld& w = level.world();
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);

    // No pre-smooth features: the base grass is buried whole. The smoothing
    // pass is kept for builder uniformity.
    smooth_world(w);
    paint_ash(w.grid, 0, 0, 89, 49);
    paint_lava(w.grid, 27, 0, 30, 49); // fire-river 1, north-south
    paint_lava(w.grid, 57, 0, 60, 49); // fire-river 2, north-south
    paint_lava(w.grid, 61, 6, 89, 9);  // the NE tributary
    paint_lava(w.grid, 0, 40, 26, 43); // the SW tributary (walls off the
                                       // corner behind the start)
    // The crossings: trodden crusts over the fire, shared by patrols and
    // party alike.
    paint_path(w.grid, 27, 14, 30, 17); // river 1, north ford
    paint_path(w.grid, 27, 36, 30, 39); // river 1, south ford
    paint_path(w.grid, 57, 8, 60, 11);  // river 2, north ford
    paint_path(w.grid, 57, 30, 60, 33); // river 2, south ford
    paint_path(w.grid, 74, 6, 77, 9);   // NE tributary ford
    paint_path(w.grid, 12, 40, 14, 43); // SW tributary ford
    // The patrol lanes: the enemy's marching ruts.
    paint_path(w.grid, 12, 4, 14, 45);  // west lane
    paint_path(w.grid, 44, 2, 46, 47);  // central lane
    paint_path(w.grid, 74, 12, 76, 47); // east lane
    paint_path(w.grid, 15, 15, 43, 16); // connector A (river 1 north ford)
    paint_path(w.grid, 15, 37, 43, 38); // connector B (river 1 south ford)
    paint_path(w.grid, 47, 9, 73, 10);  // connector C (river 2 north ford)
    paint_path(w.grid, 47, 31, 73, 32); // connector D (river 2 south ford)
    // The host's banner-poles: torch standards along the central lane.
    paint_decor(w, 0, 43, 8, DECOR_TORCH1);
    paint_decor(w, 0, 47, 14, DECOR_TORCH1);
    paint_decor(w, 0, 43, 20, DECOR_TORCH1);
    paint_decor(w, 0, 47, 28, DECOR_TORCH1);
    paint_decor(w, 0, 43, 34, DECOR_TORCH1);

    // The patrol columns (team 2), free to roam. The west column marches
    // the west lane...
    static constexpr int west_ys[8] = {6, 10, 14, 18, 22, 28, 32, 36};
    for (const int y : west_ys)
        place_living(w, FAMILY_ORC, 2, 0, 13, y, 3);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 13, 25, 5); // its captain
    // ...the central host in paired files with two captains...
    for (int i = 0; i < 6; ++i)
    {
        place_living(w, FAMILY_ORC, 2, 0, 44, 6 + i * 6, 3 + (i % 2));
        place_living(w, FAMILY_ORC, 2, 0, 46, 6 + i * 6, 3 + (i % 2));
    }
    place_living(w, FAMILY_BIG_ORC, 2, 0, 45, 15, 6);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 45, 33, 6);
    // ...runners on the connectors...
    static constexpr int runners[6][2] = {{34, 37}, {40, 38}, {50, 31},
                                          {54, 32}, {36, 16}, {52, 10}};
    for (const auto& c : runners)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3);
    // ...and the east legion filing down the east lane.
    static constexpr int legion_ys[8] = {14, 18, 22, 26, 30, 34, 38, 42};
    for (const int y : legion_ys)
        place_living(w, FAMILY_SKELETON, 2, 0, 75, y, 4);
    place_living(w, FAMILY_SKELETON, 2, 0, 73, 20, 4);
    place_living(w, FAMILY_SKELETON, 2, 0, 77, 28, 4);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 75, 12, 6); // legion captains
    place_living(w, FAMILY_BIG_ORC, 2, 0, 75, 45, 6);
    // The ford watches: the fights you MUST take.
    place_living(w, FAMILY_ORC, 2, 0, 32, 15, 7, true); // river 1 north ford
    place_living(w, FAMILY_ORC, 2, 0, 32, 17, 7, true);
    place_living(w, FAMILY_SKELETON, 2, 0, 62, 31, 5, true); // river 2 south
    place_living(w, FAMILY_SKELETON, 2, 0, 62, 33, 5, true); // ford
    // Wraiths riding the fire-rivers, where ground crews cannot answer.
    static constexpr int wraiths[6][2] = {{28, 25}, {58, 20}, {58, 44},
                                          {28, 45}, {80, 7},  {46, 25}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_GHOST, 2, 0, wraiths[i][0], wraiths[i][1],
                     5 + (i % 2));
    // The relief column rises from the south camps at tick 350.
    static constexpr int relief[6][2] = {{44, 42}, {46, 42}, {44, 45},
                                         {46, 45}, {42, 44}, {48, 44}};
    for (const auto& c : relief)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 4, false, false, 350);
    // The plain never empties: two camps and the SE bone-yard.
    place_generator(w, FAMILY_TENT, 2, 0, 48, 4, 5);   // north camp
    place_generator(w, FAMILY_TENT, 2, 0, 18, 34, 5);  // west camp
    place_generator(w, FAMILY_BONES, 2, 0, 80, 40, 5); // the dead also march

    // The crew on the west edge, on open ash, lead first.
    place_start(w, 0, 4, 25);
    static constexpr int starts[8][2] = {{2, 22}, {2, 28}, {6, 22}, {6, 28},
                                         {4, 20}, {4, 30}, {2, 25}, {6, 25}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    place_hero(w, FAMILY_THIEF, 0, 0, 25, 5, "The Bearer", true, true, 0);

    place_exit(w, 0, 88, 25, 24); // the east edge: the mountain's feet
    place_exit(w, 0, 1, 20, 22);  // backtrack, the west edge
    // Basalt teeth everywhere; slag drifts (blocking) in the three off-lane
    // zones only, so every lane, ford and connector stays marchable.
    scatter_boulders(w, 0, 0, 0, 89, 49, 23);
    scatter_litter(w, 0, 2, 2, 10, 38, 31);
    scatter_litter(w, 0, 32, 2, 42, 46, 31);
    scatter_litter(w, 0, 62, 12, 72, 46, 31);
    save_level_files(w, 23, "The Ash Plains",
                     {"No water. No shade. The air",
                      "itself is cinders. Between",
                      "the fire-rivers the enemy",
                      "marches in columns, counting",
                      "nothing so small as us.",
                      "East. Always east. Endure."},
                     4, 6000);
}

} // namespace

void build_act3b(const LevelDataHooks& hooks)
{
    build_dead_marshes(hooks);
    build_crossroads(hooks);
    build_pass_of_the_spider(hooks);
    build_tower_of_the_moon(hooks);
    build_ash_plains(hooks);
}

std::vector<ExpectedLevel> act3b_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {19, 1, "The Dead Marshes", 10, 1, 0, 0, 0, 45, 4, 6, 1, true, {20, 12}},
        {20, 1, "The Crossroads", 12, 8, 0, 0, 0, 33, 1, 8, 0, true, {21, 19}},
        {21, 1, "The Pass of the Spider", 9, 1, 0, 0, 0, 28, 1, 0, 2, true,
         {22, 20}},
        {22, 4, "The Tower of the Moon", 8, 1, 0, 0, 0, 24, 2, 2, 1, true,
         {23, 21}},
        {23, 1, "The Ash Plains", 9, 1, 0, 0, 0, 57, 3, 6, 1, true, {24, 22}},
    };
}

} // namespace westlands
