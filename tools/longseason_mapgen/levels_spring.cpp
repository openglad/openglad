/* The Long Season — SPRING (levels 1-4): 1 Mud Pay (the granary dikes,
 * the campaign's crew-1 opener), 2 The Ferry Right (the causeway hold;
 * the first warm coin), 3 Saltmere Bell (the OPTIONAL dive: the drowned
 * parish below, the belfry above), 4 The Assessor (the first protect job
 * — SAVE_ALL, protectee "Assessor" — and the first hub exit choice).
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
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

namespace longseason {
namespace {

// --- Marsh painters (the westlands act-3B recipes). --------------------------
// Full-rect two-variant checker: the drowned nave, the mound rims.
void marsh_fill(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_MARSH1, PIX_MARSH2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

// Marsh only where plain grass survived the smoother: shorelines
// (watergrass/grasswater), light/dark grass beds, paths and tree edges keep
// their shapes while the open ground waterlogs.
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

// 1 MUD PAY: spring, first thaw, the Weycombe granary dikes. Flooded fields
// south, a drowned orchard north, the granary the company is paid (in
// grain) to keep. Dike wolves rush the yard immediately; the crew walks the
// dike and pokes out three dead-end slime spurs; the granary rats are a
// held door-fight; at tick 400 two lvl-2 big slimes wake at the north pool
// — the NEXT WAVE HUD's first appearance. Gentle opener: the Wave-F
// contract starts a FRESH team here (gates at crew 1). Kill-all, then walk
// the east road out.
void build_mud_pay(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(1, true, &hooks);
    init_world(level, 1, 60, 40);
    GameWorld& w = level.world();

    paint_rect(w.grid, 0, 27, 59, 39, PIX_WATER1);        // the south flood
    paint_rect(w.grid, 2, 24, 57, 26, PIX_DIRT_1);        // the dike
    paint_rect(w.grid, 12, 27, 14, 34, PIX_DIRT_1);       // drowned spur A
    paint_rect(w.grid, 28, 27, 30, 36, PIX_DIRT_1);       // spur B
    paint_rect(w.grid, 44, 27, 46, 33, PIX_DIRT_1);       // spur C
    paint_rect(w.grid, 6, 2, 20, 10, PIX_GRASS_LIGHT_1);  // north pool fringe
    paint_rect(w.grid, 8, 3, 18, 8, PIX_WATER1);          // the north pool
    paint_rect(w.grid, 24, 2, 34, 8, PIX_TREE_M1);        // the drowned orchard
    paint_rect(w.grid, 44, 6, 54, 14, PIX_WALL2);         // the granary
    paint_rect(w.grid, 4, 14, 16, 22, PIX_GRASS_LIGHT_1); // village yard, west
    paint_rect(w.grid, 6, 16, 10, 19, PIX_WALL2);         // the reeve's hut
    smooth_world(w);
    paint_pavement(w.grid, 45, 7, 53, 13);  // granary interior
    paint_pavement(w.grid, 48, 14, 50, 14); // and door
    paint_pavement(w.grid, 7, 17, 9, 18);   // reeve's hut interior
    paint_pavement(w.grid, 8, 19, 8, 19);   // and door
    paint_path(w.grid, 6, 20, 58, 21);      // the village road (the exit road)
    paint_path(w.grid, 48, 15, 49, 19);     // granary lane, door to road
    paint_path(w.grid, 3, 25, 56, 25);      // dike-top path
    paint_path(w.grid, 20, 22, 21, 24);     // ramp, road to dike
    paint_path(w.grid, 13, 27, 13, 33);     // spur A centerline
    paint_path(w.grid, 29, 27, 29, 35);     // spur B
    paint_path(w.grid, 45, 27, 45, 32);     // spur C
    // The waterlogged band above the dike, and the pool fringe.
    marsh_over_grass(w.grid, 0, 22, 59, 23);
    marsh_over_grass(w.grid, 4, 1, 22, 11);

    // The drowned vermin (team 2). Slimes nest at the spur tips and mids.
    static constexpr int small_slimes[6][2] = {{13, 33}, {13, 31}, {29, 35},
                                               {29, 33}, {45, 32}, {45, 30}};
    for (const auto& c : small_slimes)
        place_living(w, FAMILY_SMALL_SLIME, 2, 0, c[0], c[1], 1);
    static constexpr int medium_slimes[3][2] = {{13, 28}, {29, 29}, {45, 28}};
    for (const auto& c : medium_slimes)
        place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, c[0], c[1], 1);
    // Marsh wolves on the dike top — they rush the yard.
    static constexpr int wolves[4][2] = {{8, 25}, {24, 25}, {36, 25}, {52, 25}};
    for (const auto& c : wolves)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1);
    // Rats in the grain: guards flanking the granary door (the door lane
    // x48-50 stays open).
    place_living(w, FAMILY_ORC, 2, 0, 47, 15, 1, true);
    place_living(w, FAMILY_ORC, 2, 0, 51, 15, 1, true);
    // The flood keeps giving: two big slimes wake at the pool fringe (grass,
    // NOT water) at tick 400 — the level's only twist.
    place_living(w, FAMILY_SLIME, 2, 0, 7, 9, 2, false, false, 400);
    place_living(w, FAMILY_SLIME, 2, 0, 19, 9, 2, false, false, 400);

    // The crew forms on the road by the dike ramp, lead facing the fields.
    static constexpr int starts[9][2] = {{18, 20}, {14, 17}, {14, 21},
                                         {16, 15}, {12, 19}, {10, 21},
                                         {16, 23}, {20, 17}, {22, 21}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The granary: pay is grain. A dropped sack on spur B; the reeve's one
    // honest coin (the ledger will remember it); a pool-fringe find.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 46, 8);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 52, 8);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 49, 10);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 29, 30);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 8, 17);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 4);

    place_exit(w, 0, 58, 20, 2); // east road end: downriver, to the ferry

    // Ambience: what the flood left in the mud, the roads' worn pebbles,
    // the orchard margin's brush, and dike-toe rubble west (rows 22-23
    // only; road rows 20-21 stay clear).
    scatter_decor(w, 0, 0, 22, 59, 23, 13, DECOR_BONES,
                  {ScatterGround::Grass, ScatterGround::Marsh});
    scatter_decor(w, 0, 12, 27, 14, 34, 9, DECOR_BONES, {ScatterGround::Dirt});
    scatter_decor(w, 0, 28, 27, 30, 36, 9, DECOR_BONES, {ScatterGround::Dirt});
    scatter_decor(w, 0, 44, 27, 46, 33, 9, DECOR_BONES, {ScatterGround::Dirt});
    scatter_decor(w, 0, 3, 20, 58, 25, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 22, 1, 36, 10, 9, DECOR_SHRUB, {ScatterGround::Grass});
    scatter_boulders(w, 0, 2, 22, 18, 23, 21);
    save_level_files(w, 1, "Mud Pay",
                     {"Ledger, first thaw. Took the",
                      "granary job off Weycombe's reeve.",
                      "Pay is grain and a dry roof.",
                      "The dikes crawl with what the",
                      "flood left. Kettle says: earn",
                      "the roof. Losses go in the book."},
                     2, 3000);
}

// 2 THE FERRY RIGHT: the flooded river crossing. The company holds the
// causeway so the toll-ferry runs; river bandits want the toll. A hold in
// four beats: knifemen and brigands hit the mouth immediately (the
// ferrymen's posts make the first fight winnable at crew 1); boats beach
// upstream and flank the LANDING at 400; the second push funnels up the
// causeway at 800; the river-master's bruisers close it at 1200.
// Extermination then requires crossing the causeway to break the camp
// guards. THE FIRST WARM COIN appears in this level's pay. No generators:
// every beat is authored, calibratable at crew 1.
void build_ferry_right(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(2, true, &hooks);
    init_world(level, 1, 80, 40);
    GameWorld& w = level.world();

    paint_rect(w.grid, 30, 0, 45, 39, PIX_WATER1);        // the river in flood
    paint_rect(w.grid, 30, 18, 45, 22, PIX_DIRT_1);       // the causeway
    paint_rect(w.grid, 8, 8, 20, 14, PIX_GRASS_DARK_1);   // west-bank scrub
    paint_rect(w.grid, 4, 24, 14, 32, PIX_GRASS_DARK_1);  // bandit camp
    paint_rect(w.grid, 0, 0, 6, 10, PIX_TREE_M1);         // west woods, north
    paint_rect(w.grid, 0, 34, 10, 39, PIX_TREE_M1);       // and south
    paint_rect(w.grid, 56, 12, 64, 18, PIX_WALL2);        // the toll house
    paint_rect(w.grid, 56, 26, 70, 34, PIX_GRASS_LIGHT_1); // east fields
    smooth_world(w);
    paint_path(w.grid, 6, 20, 50, 21);      // the causeway road
    paint_pavement(w.grid, 46, 23, 53, 27); // the ferry landing dock
    paint_path(w.grid, 54, 6, 55, 36);      // the east road, north-south
    paint_pavement(w.grid, 54, 2, 64, 5);   // the temple jetty, NE
    paint_path(w.grid, 56, 30, 78, 31);     // the east branch road
    paint_pavement(w.grid, 57, 13, 63, 17); // toll house interior
    paint_pavement(w.grid, 59, 18, 61, 18); // and door, south face
    // Marsh fringes: both waterlines, and the north shallows the boat wave
    // lands on.
    marsh_over_grass(w.grid, 26, 0, 29, 39);
    marsh_over_grass(w.grid, 46, 0, 49, 39);
    marsh_over_grass(w.grid, 47, 6, 54, 10);
    // Torch standards at the causeway's east mouth (road rows 20-21 stay
    // open) and the landing braziers.
    paint(w.grid, 46, 18, PIX_TORCH1);
    paint(w.grid, 46, 22, PIX_TORCH1);
    paint(w.grid, 47, 23, PIX_BRAZIER1);
    // (Design-doc deviation, one brazier: the doc's NE dock brazier at
    // (53,23) sits under start marker 5's 2x2 footprint — anchor (52,22)
    // covers (52..53, 22..23). The marker formation stands verbatim, so
    // the brazier takes the dock's SE corner instead.)
    paint(w.grid, 53, 27, PIX_BRAZIER1);

    // The ferrymen (team 0): an unnamed allied garrison holding posts at
    // the causeway mouth and the landing, plus the toll-keeper inside.
    // (Design-doc deviation, F4 calibration: the doc's lvl-2 ferrymen at
    // lvl 3 — the crew-1 defense band (t0 >= 5 at tick 3000) needs a
    // garrison that can anchor the AI-floor hold through the 800/1200
    // waves.)
    static constexpr int ferrymen[4][2] = {{47, 19}, {47, 21}, {48, 24},
                                           {52, 24}};
    for (const auto& c : ferrymen)
        place_living(w, FAMILY_SOLDIER, 0, 0, c[0], c[1], 3, true);
    place_living(w, FAMILY_ARCHER, 0, 0, 60, 15, 3, true);

    // The river bandits (team 2). Knifemen on the span and brigands on the
    // west approach open the fight.
    static constexpr int knifemen[4][2] = {{31, 19}, {31, 21}, {33, 20},
                                           {35, 20}};
    for (const auto& c : knifemen)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    static constexpr int brigands[3][2] = {{24, 19}, {24, 21}, {26, 20}};
    for (const auto& c : brigands)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1);
    // The camp: bowmen and wardens on guard posts.
    static constexpr int camp_bowmen[3][2] = {{6, 26}, {10, 26}, {6, 30}};
    for (const auto& c : camp_bowmen)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 1, true);
    place_living(w, FAMILY_SOLDIER, 2, 0, 9, 28, 2, true);
    place_living(w, FAMILY_SOLDIER, 2, 0, 12, 30, 2, true);
    // The boats — beach upstream, hit the east flank at 400.
    static constexpr int boats[4][2] = {{48, 7}, {51, 7}, {48, 9}, {51, 9}};
    for (const auto& c : boats)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false, 400);
    // Second push up the west road at 800. (Design-doc deviation, F4
    // calibration: the doc's lvl-2 push soldiers at lvl 1 — the crew-1
    // hold broke to back-to-back lvl-2 waves.)
    static constexpr int second_push[4][2] = {{18, 19}, {18, 21}, {20, 19},
                                              {20, 21}};
    for (const auto& c : second_push)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1, false, false, 800);
    place_living(w, FAMILY_ARCHER, 2, 0, 16, 20, 1, false, false, 800);
    place_living(w, FAMILY_ARCHER, 2, 0, 16, 22, 1, false, false, 800);
    // The river-master's bruisers close it at 1200. (Design-doc deviation,
    // F4 calibration: bruiser barbarians keep lvl 2 — the climax — but
    // their knife escort drops to lvl 1 for the crew-1 band.)
    place_living(w, FAMILY_BARBARIAN, 2, 0, 15, 25, 2, false, false, 1200);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 15, 29, 2, false, false, 1200);
    place_living(w, FAMILY_THIEF, 2, 0, 13, 27, 1, false, false, 1200);
    place_living(w, FAMILY_THIEF, 2, 0, 16, 27, 1, false, false, 1200);

    // The company forms at the causeway's east mouth, in front of the
    // ferrymen; lead plugging the mouth, facing west.
    static constexpr int starts[10][2] = {{50, 20}, {49, 18}, {49, 22},
                                          {52, 18}, {52, 22}, {50, 26},
                                          {54, 20}, {54, 24}, {57, 21},
                                          {57, 24}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The toll chest (one of these is THE warm coin — fiction only), the
    // keeper's shelf, and the landing's stores.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 58, 14);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 62, 14);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 62, 16);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 58, 16);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 47, 26);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 49, 24);

    place_exit(w, 0, 78, 30, 4); // east branch road: the assessor's country
    place_exit(w, 0, 56, 3, 3);  // the temple jetty: the OPTIONAL dive
    place_exit(w, 0, 54, 36, 1); // backtrack: the road home to Weycombe

    // Ambience: pebbles on all three roads, old robberies in the west-bank
    // scrub, brush on the east fields, and the stony west shore (rows
    // 12-18 only; road rows 20-21 untouched).
    scatter_decor(w, 0, 6, 20, 50, 21, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 54, 6, 55, 36, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 56, 30, 78, 31, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 4, 8, 24, 32, 17, DECOR_BONES,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 56, 26, 70, 34, 13, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_boulders(w, 0, 2, 12, 24, 18, 19);
    save_level_files(w, 2, "The Ferry Right",
                     {"Ledger, high water. Ferry job:",
                      "hold the causeway, the toll runs.",
                      "Bandits upriver. Paid in advance,",
                      "and one coin came warm as bread.",
                      "After: the temple wants its bell,",
                      "or the assessor waits east."},
                     3, 4500);
}

// 3 SALTMERE BELL (OPTIONAL): the temple's boat rows the company out to
// the sunken bell-tower of Saltmere. Floor 0 is the drowned parish — open
// water everywhere the streets aren't; floor 1 is the belfry over it, with
// an open bell-eye looking down into the nave (a bloodied hero can drop
// back onto the dais potion). A pier-and-pocket crawl: street risers meet
// the landing push; the mound is a guarded ring with two treasure spurs
// (crypt = guarded hoard, graveyard = the BONES generator that must be
// smashed or ghosts trickle forever); the belfry is a small elite room —
// the lvl-3 Ringer (unnamed by design: naming him would add a SAVE_ALL
// hazard pattern for zero story gain) and two wards over the bell hoard.
// Optional rule: +1 harder than the act's median, and the hoard pays it.
void build_saltmere_bell(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(3, true, &hooks);
    init_world(level, 2, 50, 50);
    GameWorld& w = level.world();
    PixieData& g0 = w.grid;
    PixieData& g1 = w.grid_for_floor(1);

    // Floor 0 — the drowned parish.
    paint_rect(g0, 0, 0, 49, 49, PIX_WATER1);   // everything under water
    paint_rect(g0, 4, 40, 12, 46, PIX_DIRT_1);  // the boat landing (SW islet)
    paint_rect(g0, 13, 42, 34, 44, PIX_DIRT_1); // processional street, east leg
    paint_rect(g0, 32, 20, 34, 44, PIX_DIRT_1); // and north leg
    paint_rect(g0, 20, 10, 38, 26, PIX_DIRT_1); // the churchyard mound
    paint_rect(g0, 24, 12, 33, 21, PIX_WALL2);  // the tower footprint
    paint_rect(g0, 12, 16, 19, 19, PIX_DIRT_1); // crypt spur, west
    paint_rect(g0, 39, 14, 45, 18, PIX_DIRT_1); // graveyard spur, east
    // Floor 1 — the belfry over the flood.
    paint_rect(g1, 0, 0, 49, 49, PIX_AIR);      // open sky
    paint_rect(g1, 24, 12, 33, 21, PIX_WALL2);  // the tower's upper story
    smooth_world(w);

    // Floor 0: the nave (ankle-deep), the bell dais, the tower door.
    marsh_fill(g0, 25, 13, 32, 20);
    paint_pavement(g0, 27, 15, 30, 18);
    paint_pavement(g0, 28, 21, 29, 21);
    // Marsh ring on the mound's outer edge — the flood laps at it.
    marsh_fill(g0, 20, 10, 38, 11);
    marsh_fill(g0, 20, 25, 38, 26);
    marsh_fill(g0, 20, 12, 21, 24);
    marsh_fill(g0, 37, 12, 38, 24);
    paint_path(g0, 5, 43, 33, 43);  // landing to the corner
    paint_path(g0, 33, 21, 33, 42); // north leg to the mound
    paint_path(g0, 13, 17, 23, 17); // crypt lane
    paint_path(g0, 34, 16, 44, 16); // graveyard lane
    // Torches flanking the tower door (door lane x28-29 stays open).
    paint(g0, 27, 22, PIX_TORCH1);
    paint(g0, 30, 22, PIX_TORCH1);
    // Floor 1: the belfry interior and the bell-eye — open center over the
    // dais. FALL-LINE RULE: every floor-1 walkable cell adjacent to this
    // AIR drops onto the floor-0 DAIS (pavement (27,15)-(30,18) covers the
    // full projection); the outside AIR is sealed off by the wall shell.
    paint_pavement(g1, 25, 13, 32, 20);
    paint_rect(g1, 28, 16, 29, 17, PIX_AIR);
    paint(g1, 25, 20, PIX_BRAZIER1);
    paint(g1, 32, 13, PIX_BRAZIER1);
    // Exactly one aligned pair on the single floor boundary: floor-0 cell
    // is nave marsh (standable), floor-1 cell is belfry pavement.
    stair_pair(w, 0, 25, 19);

    // The drowned (team 2). Street risers meet the landing push.
    static constexpr int risers[4][2] = {{18, 43}, {24, 43}, {33, 38},
                                         {33, 30}};
    for (const auto& c : risers)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 1);
    // The churchyard watch, holding the mound ring.
    static constexpr int watch[4][2] = {{22, 23}, {35, 23}, {22, 11},
                                        {36, 12}};
    for (const auto& c : watch)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2, true);
    // Drifters — the drowned risen from the street mud. (Design-doc
    // deviation, F4 calibration: the doc's ghost drifters are SKELETONS
    // now — a flyer that chases over the open flood hovers where no
    // ground walker or brawler-AI archer will finish it, and the sweeps
    // stalled the kill-all on 1-6 unreachable stragglers every seed.
    // Ground drifters keep the drowned-parish fiction and stay killable.)
    static constexpr int drifters[3][2] = {{33, 34}, {24, 25}, {36, 16}};
    for (const auto& c : drifters)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2);
    // Nave scum in the ankle-deep marsh.
    static constexpr int scum[4][2] = {{26, 14}, {30, 14}, {26, 19}, {30, 19}};
    for (const auto& c : scum)
        place_living(w, FAMILY_SMALL_SLIME, 2, 0, c[0], c[1], 1);
    // Crypt guards over the hoard.
    // (F4: the crypt watch skirmishes — a remote guarded spur is a
    // parked-remnant kill-all stall for the AI floor.)
    static constexpr int crypt_guards[3][2] = {{13, 16}, {16, 16}, {18, 18}};
    for (const auto& c : crypt_guards)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2);
    // The belfry: "the Ringer" beside the bell-eye, and two wards.
    // (Design-doc deviation, one cell: the doc anchors him at (30,16)
    // assuming a 2x2 footprint, but the giant-skeleton sprite is 64x64 —
    // a 4x4 TILE footprint — which would clip the east wall at x33.
    // Anchored at (29,15) the footprint (29..32, 15..18) wraps the doc's
    // intended body cells, stays inside the belfry shell, and keeps his
    // center tile on pavement beside the bell-eye; the eye's AIR cells
    // under his west edge are grid-passable by design.)
    place_living(w, FAMILY_GIANT_SKELETON, 2, 1, 29, 15, 3, true);
    // (F4: wards lvl 2 -> 1 AND skirmishing — the stair feeds the crew
    // into the elite room one at a time, so the room's weight sits on the
    // Ringer; a parked corner ward was the kill-all's last unvisited
    // remnant on one curve seed.)
    place_living(w, FAMILY_SKELETON, 2, 1, 27, 19, 1); // stair-head
    place_living(w, FAMILY_SKELETON, 2, 1, 26, 13, 1); // north side
    // The deep answers the bell at tick 500. (Same F4 flyer deviation as
    // the drifters: the deep sends SLIME up out of the flood, not ghosts
    // — it rises onto the mound edge and the crypt spur, and it can be
    // cornered there.)
    place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, 34, 25, 3, false, false, 500);
    place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, 12, 18, 3, false, false, 500);
    // The open grave on the graveyard spur — ghosts rise until it's smashed.
    place_generator(w, FAMILY_BONES, 2, 0, 42, 15, 1);

    // The temple's boat grounds on the SW islet; lead at the street's
    // mouth, facing east.
    static constexpr int starts[8][2] = {{9, 43}, {6, 41}, {6, 45}, {11, 41},
                                         {11, 45}, {4, 43}, {8, 40}, {8, 45}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The bell hoard, ringing the bell-eye (floor 1).
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 28, 15);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 29, 15);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 28, 18);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 29, 18);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 26, 16);
    // The crypt, and the diver's trick.
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 15, 18);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 17, 16);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 19, 18);
    place(w, Order::Treasure, FAMILY_INVIS_POTION, 0, 0, 19, 17);
    // The graveyard.
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 40, 15);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 40, 17);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 44, 17);
    // The dais — visible through the bell-eye from above.
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 28, 17);

    // Both exits on the landing islet — the boat is the only way in or out.
    place_exit(w, 0, 5, 41, 4); // the boat rows you east to the high road
    place_exit(w, 0, 5, 45, 2); // backtrack: downriver to the ferry

    // Ambience: the parish buried its dead here — bones on the mound and
    // both spurs, pebbles on the streets and lanes. No shrubs (nothing
    // grows underwater); no boulder scatter (the streets are narrow).
    scatter_decor(w, 0, 20, 10, 38, 26, 7, DECOR_BONES,
                  {ScatterGround::Dirt, ScatterGround::Marsh});
    scatter_decor(w, 0, 12, 16, 19, 19, 5, DECOR_BONES,
                  {ScatterGround::Dirt, ScatterGround::Marsh});
    scatter_decor(w, 0, 39, 14, 45, 18, 5, DECOR_BONES,
                  {ScatterGround::Dirt, ScatterGround::Marsh});
    scatter_decor(w, 0, 5, 43, 33, 43, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 33, 21, 33, 42, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 13, 17, 23, 17, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 34, 16, 44, 16, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 3, "Saltmere Bell",
                     {"Ledger, still raining. Temple",
                      "job: raise the Saltmere bell",
                      "before the drowned claim it.",
                      "Temple silver is old and cold",
                      "and honest. Note the difference.",
                      "Wet boots. Wetter dead."},
                     3, 4000);
}

// 4 THE ASSESSOR (HUB; the campaign's FIRST protect job): escort the crown
// assessor through bandit country. His wagon has thrown an axle at the
// barrow-road waystation; the company deploys around it inside the walls,
// breaks the ambush springs — the first (tick 300) comes from BEHIND,
// through the west gap the column just walked; gorse spring 700; the toll
// chief's push crosses the ford at 1100 — then clears the road in three
// sorties: barrow field (smash the TENT), gorse camp, east toll posts.
// SAVE_ALL scoping: the placed "Assessor" (11-char budget; the briefing
// affords "the crown assessor") is the ONLY npc_flags-bit-2 walker —
// Kettle is protect-OPTIONAL by story bible; his death stings the ledger,
// not the mission. No flyers anywhere BY DESIGN (the TENT spawns ground
// skeletons only): walls + lvl-4 door-wards suffice at crew 2. He pays in
// the warm coin and won't say whose stamp it is. HUB: east to the summer
// war roads (5) or south to the Saltmere jetty (3 — now or never mind).
void build_the_assessor(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(4, true, &hooks);
    init_world(level, 1, 80, 45);
    GameWorld& w = level.world();
    w.type = SCEN_TYPE_SAVE_ALL;

    paint_rect(w.grid, 0, 0, 5, 44, PIX_TREE_M1);       // west woods band
    paint_rect(w.grid, 0, 20, 5, 24, PIX_GRASS1);       // with the road gap
    paint_rect(w.grid, 70, 0, 79, 6, PIX_TREE_M1);      // NE copse
    paint_rect(w.grid, 8, 2, 30, 10, PIX_GRASS_DARK_1); // the barrow field
    paint_rect(w.grid, 12, 4, 15, 7, PIX_DIRT_DARK_1);  // barrow mounds
    paint_rect(w.grid, 20, 3, 23, 6, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 26, 5, 29, 8, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 20, 34, 48, 42, PIX_GRASS_DARK_1); // the south gorse
    paint_rect(w.grid, 58, 0, 62, 44, PIX_WATER1);        // the swollen stream
    paint_rect(w.grid, 36, 16, 50, 26, PIX_WALL2);        // the waystation
    smooth_world(w);
    paint_pavement(w.grid, 37, 17, 49, 25); // waystation interior
    paint_pavement(w.grid, 36, 20, 36, 23); // west gate
    paint_pavement(w.grid, 50, 20, 50, 23); // east gate
    // The barrow road; the ford over the stream is the path-over-water
    // span (58,21)-(62,22), the only crossing.
    paint_path(w.grid, 2, 21, 35, 22);
    paint_path(w.grid, 51, 21, 78, 22);
    paint_path(w.grid, 20, 7, 21, 20);  // the barrow lane, road to the field
    paint_path(w.grid, 33, 23, 33, 43); // the south lane (the Saltmere spur)
    // Marsh on the stream banks.
    marsh_over_grass(w.grid, 55, 0, 57, 44);
    marsh_over_grass(w.grid, 63, 0, 65, 44);
    // The wagon (thrown axle, parked off the gate lane): a blocking prop;
    // lane rows 20-23 stay clear.
    paint(w.grid, 43, 19, PIX_BOULDER_2);
    // The Assessor's counting nook (F4, the westlands bolt-hole pattern):
    // two raw wall spurs pinch the north-wall bay at (45..47, 17..18) down
    // to one 2x2 mouth at (45..46, 19..20) that a single door-ward's body
    // fills. The brawler-AI crew empties the fort on its sorties; with
    // open gates the 1100 push killed the warded Assessor on the 4-soldier
    // floor even behind lvl-8 wards. Painted AFTER smooth_world like the
    // westlands hut so the spurs stay solid rock.
    paint(w.grid, 44, 17, PIX_WALL2);
    paint(w.grid, 44, 18, PIX_WALL2);
    paint(w.grid, 47, 17, PIX_WALL2);
    paint(w.grid, 47, 18, PIX_WALL2);
    // Seal the wagon-side dead pocket: the (44,19) cell between the west
    // spur and the wagon boulder wedged a straggler out of the mouth
    // ward's reach on one sweep seed.
    paint(w.grid, 44, 19, PIX_WALL2);
    paint(w.grid, 38, 17, PIX_BRAZIER1); // waystation braziers
    paint(w.grid, 49, 17, PIX_BRAZIER1);
    // Gate torches, outside the walls, off the road rows 21-22.
    paint(w.grid, 35, 19, PIX_TORCH1);
    paint(w.grid, 35, 24, PIX_TORCH1);
    paint(w.grid, 51, 19, PIX_TORCH1);
    paint(w.grid, 51, 24, PIX_TORCH1);

    // The halted column (team 0). The Assessor — the SAVE_ALL protectee;
    // save_level_files hands him npc_flags bit 2 — counts coin, he doesn't
    // fight (specials disabled), warded at the north wall off the gate
    // lane. Kettle inside the west gate, south of the lane; two unnamed
    // door-wards flanking the Assessor's post.
    place_hero(w, FAMILY_CLERIC, 0, 45, 17, 4, "Assessor", true, true, 0);
    place_hero(w, FAMILY_SOLDIER, 0, 38, 24, 3, "Kettle", true, false, 0);
    // (F4 calibration, past the doc's own "harden wards to lvl 5" clause:
    // lvl-8 westlands door-ward weight, and one ward's 2x2 body FILLS the
    // counting nook's mouth at (45,19). The brawler-AI crew empties the
    // fort for its sorties — the doc's protect lesson — and open-gate
    // wards were drawn off and dead before the 1100 push, which then
    // killed the Assessor with the crew alive on 4 of 6 curve runs.)
    place_living(w, FAMILY_SOLDIER, 0, 0, 42, 17, 8, true);
    place_living(w, FAMILY_SOLDIER, 0, 0, 45, 19, 8, true);
    place_living(w, FAMILY_SOLDIER, 0, 0, 41, 19, 8, true); // approach lane

    // Bandit country (team 2). Road rocks west, ford watch and the east
    // road toll, dogs and camp in the gorse, and what the digging woke.
    static constexpr int road_rocks[3][2] = {{10, 20}, {16, 23}, {24, 20}};
    for (const auto& c : road_rocks)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 2, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 63, 20, 2, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 63, 23, 2, true);
    static constexpr int toll_posts[3][2] = {{66, 20}, {66, 23}, {70, 21}};
    for (const auto& c : toll_posts)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 2, true);
    static constexpr int dogs[4][2] = {{24, 36}, {30, 38}, {38, 36}, {44, 38}};
    for (const auto& c : dogs)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1);
    // The gorse camp (lane x33 stays open).
    static constexpr int gorse_camp[3][2] = {{28, 40}, {34, 40}, {40, 40}};
    for (const auto& c : gorse_camp)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 2, true);
    static constexpr int woken[4][2] = {{13, 5}, {21, 4}, {27, 6}, {17, 8}};
    for (const auto& c : woken)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 1);
    // The first spring — from the rocks BEHIND the column, tick 300.
    // (Design-doc deviation, F4 calibration: the three ambush springs all
    // shed one level — the doc's lvl-2/lvl-3 waves broke the crew-2 kill
    // gate and, worse, the SAVE_ALL sub-gate: the Assessor died at curve
    // on 4 of 6 sweep runs.)
    static constexpr int first_spring[4][2] = {{6, 19}, {6, 24}, {8, 18},
                                               {8, 25}};
    for (const auto& c : first_spring)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false, 300);
    // Gorse spring at 700, off the north edge.
    static constexpr int gorse_spring[3][2] = {{30, 33}, {36, 33}, {42, 33}};
    for (const auto& c : gorse_spring)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1, false, false, 700);
    // The toll chief's push across the ford at 1100, two ranks.
    static constexpr int push_front[3][2] = {{74, 20}, {74, 23}, {76, 22}};
    for (const auto& c : push_front)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 2, false, false,
                     1100);
    static constexpr int push_rear[3][2] = {{72, 19}, {72, 24}, {78, 24}};
    for (const auto& c : push_rear)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1, false, false,
                     1100);
    // The diggers' camp on the barrow field — the coin they dig for is the
    // arc's; spawns ground skeletons ONLY (keeps the protect job honest).
    // (Design-doc deviation, F4 calibration: lvl 1, not 2 — a slower
    // trickle a crew-2 sortie can outpace.)
    place_generator(w, FAMILY_TENT, 2, 0, 24, 8, 1);

    // The company deploys INSIDE the waystation around the wagon; lead on
    // the lane inside the west gate.
    static constexpr int starts[10][2] = {{40, 21}, {43, 20}, {40, 24},
                                          {46, 21}, {43, 24}, {46, 24},
                                          {40, 18}, {48, 20}, {37, 21},
                                          {48, 24}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The assessor's strongbox (THE WARM COIN, paid on the spot — fiction),
    // Kettle's float, the column's stores, the diggers' loot, the gorse
    // camp's take, and a dropped pack at the ford.
    // (Strongbox gold moved off the nook's west spur cell (44,18).)
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 39, 18);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 44, 20);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 39, 17);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 45, 25);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 39, 22);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 14, 4);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 34, 41);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 56, 21);

    // THE HUB: forward to 5 and back to the optional 3 only. The exit cell
    // (78,21) is left unoccupied (push soldiers anchor at (78,24)).
    place_exit(w, 0, 78, 21, 5); // east road end: the summer war roads
    place_exit(w, 0, 33, 43, 3); // south lane's end: the Saltmere jetty

    // Ambience: the diggings' bones on the barrow field (the mounds are
    // dark dirt — the doc's "Dirt" class for the diggings), concealment IS
    // the ambush country in the gorse (off the road and both lanes'
    // required routes), pebbles on road and lanes, and the moor stones /
    // east-bank scree (road rows 20-24 excluded by both rects).
    scatter_decor(w, 0, 8, 2, 30, 10, 7, DECOR_BONES,
                  {ScatterGround::DarkGrass, ScatterGround::DarkDirt});
    scatter_decor(w, 0, 20, 36, 48, 42, 9, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 2, 21, 78, 22, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 20, 7, 21, 20, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 33, 23, 33, 43, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_boulders(w, 0, 6, 10, 30, 18, 17);
    scatter_boulders(w, 0, 64, 28, 76, 40, 19);
    save_level_files(w, 4, "The Assessor",
                     {"Ledger, mud month. Escort job:",
                      "the crown assessor, kept alive",
                      "through the barrow road. He pays",
                      "in the warm coin and will not",
                      "say whose stamp it bears.",
                      "War pay east; the bell south."},
                     4, 5000);
}

} // namespace

void build_spring(const LevelDataHooks& hooks)
{
    build_mud_pay(hooks);
    build_ferry_right(hooks);
    build_saltmere_bell(hooks);
    build_the_assessor(hooks);
}

std::vector<ExpectedLevel> spring_expectations()
{
    return {
        // id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
        // delayed, no-specials, stairs, exits
        {1, 1, "Mud Pay", 9, 0, 0, 0, 0, 17, 0, 2, 0, true, {2}},
        {2, 1, "The Ferry Right", 10, 5, 0, 0, 0, 26, 0, 14, 0, true,
         {4, 3, 1}},
        {3, 2, "Saltmere Bell", 8, 0, 0, 0, 0, 23, 1, 2, 0, true, {4, 2}},
        {4, 1, "The Assessor", 10, 5, 0, 0, 0, 32, 1, 13, 1, true, {5, 3}},
    };
}

} // namespace longseason
