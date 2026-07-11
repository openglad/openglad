/* The Long Season — SUMMER (levels 5-9): 5 Two Banners (both payrolls,
 * the war-contract skirmish), 6 The Hay War (the summer hub: burn the levy
 * camps, then the toll fort north or the pay road east), 7 Grey Tolls (the
 * OPTIONAL fort defense), 8 The Paymaster Vanishes (Long Tom on the pay
 * chest), 9 Ashfall Fair (the strongroom riot; Kettle placed, protect-
 * OPTIONAL — no SAVE_ALL here by the story bible).
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

// 5 TWO BANNERS: a wheat valley between two hired armies — the clerks hired
// the company twice and Kettle kept both purses. The crew deploys at the
// blue camp's apron (west edge) and clears the russet skirmish line east
// across two hedged field strips and a stream ford, ending at the russet
// palisade. Three spaced fights west-to-east, a short guard fight at the
// gate, and a tick-500 reserve that wakes BEHIND the line of advance — the
// summer act's NEXT WAVE lesson. Kill-all, then walk the russet muster
// road on. Curve: crew 3 (the summer step up).
void build_two_banners(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(5, true, &hooks);
    init_world(level, 1, 60, 40);
    GameWorld& w = level.world();

    // Wheat strips (ripe grain), the fallow headland, hedgerows and the
    // stream (both with the road gap y17-22), the SW pond, the palisade.
    paint_rect(w.grid, 10, 4, 26, 14, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 10, 25, 26, 36, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 36, 4, 52, 14, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 36, 25, 52, 36, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 10, 0, 52, 2, PIX_GRASS_DARK_1);  // fallow headland
    paint_rect(w.grid, 14, 0, 15, 16, PIX_TREE_M1);      // west hedgerow
    paint_rect(w.grid, 14, 23, 15, 39, PIX_TREE_M1);
    paint_rect(w.grid, 30, 0, 31, 16, PIX_WATER1);       // the stream
    paint_rect(w.grid, 30, 23, 31, 39, PIX_WATER1);
    paint_rect(w.grid, 44, 0, 45, 16, PIX_TREE_M1);      // east hedgerow
    paint_rect(w.grid, 44, 23, 45, 39, PIX_TREE_M1);
    paint_rect(w.grid, 18, 30, 24, 35, PIX_WATER1);      // SW fallow pond
    paint_rect(w.grid, 50, 12, 58, 27, PIX_WALL2);       // russet palisade
    smooth_world(w);
    // Blue camp apron; russet interior + the west gate doorway (pavement
    // over wall carves it); the contract road, west edge to the gate (the
    // ford is just the road rows crossing the stream gap).
    paint_pavement(w.grid, 2, 17, 5, 23);
    paint_pavement(w.grid, 51, 13, 57, 26);
    paint_pavement(w.grid, 50, 19, 50, 20);
    paint_path(w.grid, 2, 19, 49, 20);
    // Cook-fires on the apron and gate torches, all off the road rows
    // 19-20 and off every marker footprint.
    paint_decor(w, 0, 2, 17, DECOR_BRAZIER);
    paint_decor(w, 0, 5, 17, DECOR_BRAZIER);
    paint_decor(w, 0, 49, 17, DECOR_TORCH1);
    paint_decor(w, 0, 49, 22, DECOR_TORCH1);

    // The russet banner (team 2). Pickets in strip A. (Design-doc
    // deviation, one cell: the doc's picket at (22,32) sits inside its own
    // SW pond rect (18,30)-(24,35); the picket holds the same south-strip
    // watch from the dry margin at (22,28).)
    // (F4 batch 2: the line ranks flatten to lvl 1 and the gate pieces
    // shed another level — 26 foes against a lone 8-man crew is the
    // pressure; the levels are the tax.)
    static constexpr int pickets[6][2] = {{18, 8},  {22, 12}, {25, 6},
                                          {18, 28}, {22, 28}, {25, 26}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_SOLDIER, 2, 0, pickets[i][0], pickets[i][1],
                     1);
    // Ford mercs on the east bank. (Design-doc deviation, F4 calibration:
    // the whole russet order of battle sheds levels — the doc's army
    // wiped the curve-3 crew by tick 600 on all seeds.)
    static constexpr int mercs[3][2] = {{33, 18}, {34, 21}, {36, 20}};
    for (const auto& c : mercs)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 1);
    // Archers behind the east hedge. (F4: skirmishers, not posts — parked
    // full-HP guard archers were the kill-all's last unvisited remnant on
    // two of three curve seeds; roamers join the line fights and die with
    // the line.)
    static constexpr int hedge_archers[4][2] = {{46, 10}, {46, 14}, {46, 26},
                                                {46, 30}};
    for (const auto& c : hedge_archers)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 1);
    // Strip B line.
    static constexpr int strip_b[6][2] = {{38, 8},  {42, 12}, {47, 6},
                                          {38, 30}, {42, 26}, {47, 33}};
    for (const auto& c : strip_b)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1);
    // The sergeant before the gate, and the gate archers inside (the
    // archers skirmish — same F4 remnant rule as the hedge).
    place_living(w, FAMILY_SOLDIER, 2, 0, 48, 20, 3, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 51, 16, 1);
    place_living(w, FAMILY_ARCHER, 2, 0, 51, 24, 1);
    // The reserve wakes on the NE headland at tick 500, behind the crew's
    // line of advance.
    static constexpr int reserve[5][2] = {{52, 2}, {54, 3}, {56, 4}, {55, 6},
                                          {57, 7}};
    for (const auto& c : reserve)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1, false, false,
                     500);

    // The crew forms on the apron and road, lead facing the fields east.
    static constexpr int starts[10][2] = {{5, 20}, {3, 19}, {7, 18}, {7, 22},
                                          {3, 22}, {9, 20}, {6, 17}, {9, 17},
                                          {6, 23}, {9, 23}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Blue camp stores, the RUSSET purse (the second warm payment), a
    // pond-fringe find, and the russet larder.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 3, 18);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 4, 22);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 53, 14);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 21, 29);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 52, 25);

    place_exit(w, 0, 56, 20, 6); // inside the russet camp: the muster road
    place_exit(w, 0, 1, 19, 4);  // backtrack: the assessor's road west

    // Ambience: road pebbles, wheat-margin shrubs (LightGrass only — off
    // the road band by ground class), an older season's skirmish in the
    // fallow, and its stones.
    scatter_decor(w, 0, 2, 17, 57, 23, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 10, 4, 26, 36, 17, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 36, 4, 52, 36, 17, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 10, 0, 52, 2, 23, DECOR_BONES,
                  {ScatterGround::DarkGrass});
    scatter_boulders(w, 0, 10, 0, 52, 2, 25);
    save_level_files(w, 5, "Two Banners",
                     {"Ledger, hay-month. Two banners",
                      "hired us for the same field.",
                      "Clerks' error. Kettle kept both",
                      "purses. The blue lot paid first",
                      "and their coin came up warm.",
                      "Break the russet line by dusk."},
                     3, 4000);
}

// 6 THE HAY WAR (the SUMMER HUB): a dirt-road cross through four hay
// quarters; three walled harvest depots (NW, NE, S) each hold a muster
// TENT — the "camps to burn" (GEN_EXIT is a dead bit; extermination
// enforces the objective de facto, the levy respawns until every camp is
// torched). Road patrols rove between them; a relief column marches in
// from the east at tick 600. The company earns its name here. Completing
// it unlocks BOTH 7 (toll fort north, optional) and 8 (the pay road east).
void build_hay_war(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(6, true, &hooks);
    init_world(level, 1, 60, 60);
    GameWorld& w = level.world();

    // Hay quarters, the old-battle fallow, the three depot shells.
    paint_rect(w.grid, 4, 4, 24, 24, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 36, 4, 56, 24, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 4, 36, 24, 56, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 36, 36, 56, 56, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 4, 31, 20, 34, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 8, 8, 16, 15, PIX_WALL2);   // NW depot
    paint_rect(w.grid, 42, 8, 50, 15, PIX_WALL2);  // NE depot
    paint_rect(w.grid, 26, 42, 34, 49, PIX_WALL2); // S depot
    // Hedgerows framing the east-west road (painted AFTER the fallow so
    // trees win), lane gaps x11-13 and x45-47; the SE south hedge is
    // shortened clear of the pond.
    paint_rect(w.grid, 4, 26, 10, 27, PIX_TREE_M1);
    paint_rect(w.grid, 14, 26, 24, 27, PIX_TREE_M1);
    paint_rect(w.grid, 36, 26, 44, 27, PIX_TREE_M1);
    paint_rect(w.grid, 48, 26, 56, 27, PIX_TREE_M1);
    paint_rect(w.grid, 4, 33, 10, 34, PIX_TREE_M1);
    paint_rect(w.grid, 14, 33, 24, 34, PIX_TREE_M1);
    paint_rect(w.grid, 36, 33, 44, 34, PIX_TREE_M1);
    paint_rect(w.grid, 48, 33, 54, 37, PIX_WATER1); // the pond
    smooth_world(w);
    // Depot interiors + gates (pavement over wall carves the doorways).
    paint_pavement(w.grid, 9, 9, 15, 14);
    paint_pavement(w.grid, 12, 15, 12, 15);
    paint_pavement(w.grid, 43, 9, 49, 14);
    paint_pavement(w.grid, 46, 15, 46, 15);
    paint_pavement(w.grid, 27, 43, 33, 48);
    paint_pavement(w.grid, 30, 42, 30, 42);
    // Roads: the east-west cross, the north-south road down to the S depot
    // gate, and the two depot lanes.
    paint_path(w.grid, 2, 29, 57, 30);
    paint_path(w.grid, 29, 2, 30, 41);
    paint_path(w.grid, 12, 16, 12, 28);
    paint_path(w.grid, 46, 16, 46, 28);
    // Burn-fires flanking each gate, one tile OFF the gate lanes; torch
    // pairs inside each depot at interior corners.
    paint_decor(w, 0, 10, 16, DECOR_BRAZIER);
    paint_decor(w, 0, 14, 16, DECOR_BRAZIER);
    paint_decor(w, 0, 44, 16, DECOR_BRAZIER);
    paint_decor(w, 0, 48, 16, DECOR_BRAZIER);
    paint_decor(w, 0, 28, 41, DECOR_BRAZIER);
    paint_decor(w, 0, 32, 41, DECOR_BRAZIER);
    paint_decor(w, 0, 9, 9, DECOR_TORCH1);
    paint_decor(w, 0, 15, 9, DECOR_TORCH1);
    paint_decor(w, 0, 43, 9, DECOR_TORCH1);
    paint_decor(w, 0, 49, 9, DECOR_TORCH1);
    paint_decor(w, 0, 27, 43, DECOR_TORCH1);
    paint_decor(w, 0, 33, 43, DECOR_TORCH1);

    // The enemy levy (team 2). Depot garrisons, all on guard posts.
    // (Design-doc deviation, F4 calibration: the levy sheds one level on
    // its lvl-3+ ranks and the tents drop to lvl 1 — the doc's muster
    // wiped the curve-3 crew by tick 900 with zero clears at crew 4.)
    static constexpr int nw_soldiers[3][2] = {{10, 10}, {14, 10}, {12, 13}};
    for (const auto& c : nw_soldiers)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 2, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 9, 12, 2, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 15, 12, 2, true);
    static constexpr int ne_soldiers[4][2] = {{44, 10}, {48, 10}, {44, 13},
                                              {48, 13}};
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_SOLDIER, 2, 0, ne_soldiers[i][0],
                     ne_soldiers[i][1], 1 + (i % 2), true);
    static constexpr int s_soldiers[3][2] = {{28, 44}, {32, 44}, {30, 47}};
    for (const auto& c : s_soldiers)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1, true);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 30, 43, 3, true); // the hay-reeve
    // Road patrols. (F4 batch 2: roamers to lvl 1 — the roaming half of
    // the levy converges on the first fight, so it fights at mob weight.)
    static constexpr int patrols[6][2] = {{20, 29}, {40, 30}, {29, 20},
                                          {30, 40}, {24, 30}, {36, 29}};
    for (const auto& c : patrols)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1);
    static constexpr int bow_patrols[4][2] = {{29, 12}, {30, 24}, {29, 36},
                                              {16, 30}};
    for (const auto& c : bow_patrols)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 1);
    // The relief column, east road, tick 600 — punishes slow sweeps.
    static constexpr int relief[6][2] = {{53, 29}, {55, 28}, {57, 29},
                                         {53, 31}, {55, 31}, {57, 30}};
    for (const auto& c : relief)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1, false, false,
                     600);
    // The camps to burn: lvl-1 muster tents, one per depot (pace-limited
    // trickles a curve-3 sortie outpaces).
    place_generator(w, FAMILY_TENT, 2, 0, 12, 11, 1);
    place_generator(w, FAMILY_TENT, 2, 0, 46, 11, 1);
    place_generator(w, FAMILY_TENT, 2, 0, 30, 45, 1);

    // The crew forms on the west road and its verges, lead at the cross.
    static constexpr int starts[10][2] = {{4, 29}, {2, 28}, {6, 28}, {2, 31},
                                          {6, 31}, {8, 29}, {4, 24}, {8, 24},
                                          {4, 35}, {8, 35}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Depot stores, the S depot strongbox, a pond-fringe find, and a
    // dropped ration by the fallow bones.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 10, 13);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 49, 10);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 31, 46);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 28, 47);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 51, 32);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 18, 32);

    // THE HUB: mainline east, the optional toll road north, and the
    // backtrack west.
    place_exit(w, 0, 57, 30, 8); // east: the paymaster's road
    place_exit(w, 0, 29, 2, 7);  // north: the toll road to the Grey Tolls
    place_exit(w, 0, 2, 30, 5);  // backtrack: west to the Two Banners field

    // Ambience: road pebbles on the cross and lanes, hay-field shrubs in
    // all four quarters (off the roads by ground class), last summer's hay
    // war in the fallow, and its stones.
    scatter_decor(w, 0, 2, 2, 57, 57, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 4, 4, 24, 24, 19, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 36, 4, 56, 24, 19, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 4, 36, 24, 56, 19, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 36, 36, 56, 56, 19, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 4, 31, 20, 34, 9, DECOR_BONES,
                  {ScatterGround::DarkGrass});
    scatter_boulders(w, 0, 4, 31, 20, 34, 21);
    save_level_files(w, 6, "The Hay War",
                     {"Ledger, hay war. Burn the levy",
                      "camps before they muster twice.",
                      "The troops call us the Brass",
                      "Kettles now. It sticks. Pay is",
                      "warm coin again. Toll fort",
                      "north; the pay road east."},
                     3, 4500);
}

// 7 GREY TOLLS (OPTIONAL): a mountain toll fort astride a north-south pass
// road, cliffs walling both sides. The company garrisons the bailey beside
// the standing (unnamed, unprotected) toll-watch and holds the wall through
// four waves (ticks 0/350/800/1400) plus a goat-path thief flank that
// arrives BEHIND the north gate at 1000. Turtling never finishes it: the
// two camp tents respawn lvl-3 levies until a sortie south torches them.
// +1 over the summer median (crew 4), paid back in the strongbox room.
// Layout deliberately re-usable by the L14 winter repaint.
void build_grey_tolls(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(7, true, &hooks);
    init_world(level, 1, 50, 60);
    GameWorld& w = level.world();

    // Cliff masses; the pass corridor is x17-32, full height. The goat
    // path is carved BACK out of the west block — a narrow shelf reaching
    // the corridor at (17,20)-(17,23).
    paint_rect(w.grid, 0, 0, 16, 59, PIX_WALL2);
    paint_rect(w.grid, 33, 0, 49, 59, PIX_WALL2);
    paint_rect(w.grid, 6, 2, 9, 23, PIX_GRASS1);
    paint_rect(w.grid, 9, 20, 16, 23, PIX_GRASS1);
    // The fort bailey plugging the pass, and the strongbox room shell
    // inside it (both pre-smooth; interiors carved below).
    paint_rect(w.grid, 15, 24, 34, 35, PIX_WALL2);
    paint_rect(w.grid, 17, 26, 21, 29, PIX_WALL2);
    // South camp scrub.
    paint_rect(w.grid, 18, 48, 31, 56, PIX_GRASS_DARK_1);
    smooth_world(w);
    // Bailey interior pavement painted AROUND the strongbox shell (pavement
    // over wall carves, so the shell must be skipped), then the gates —
    // here the carve is the point — and the strongbox interior + door.
    paint_pavement(w.grid, 22, 25, 33, 34);
    paint_pavement(w.grid, 16, 25, 21, 25);
    paint_pavement(w.grid, 16, 30, 21, 34);
    paint_pavement(w.grid, 16, 26, 16, 29);
    paint_pavement(w.grid, 24, 24, 25, 24); // north gate
    paint_pavement(w.grid, 24, 35, 25, 35); // south gate
    paint_pavement(w.grid, 18, 27, 20, 28); // strongbox interior
    paint_pavement(w.grid, 19, 29, 19, 29); // strongbox door
    // The toll road, both approaches (the bailey pavement carries it
    // through y24-35).
    paint_path(w.grid, 24, 2, 25, 23);
    paint_path(w.grid, 24, 36, 25, 57);
    // Gate torches (off the road cols 24-25), camp braziers, and the
    // strongbox torch on the inner corner off the door lane.
    paint_decor(w, 0, 23, 23, DECOR_TORCH1);
    paint_decor(w, 0, 26, 23, DECOR_TORCH1);
    paint_decor(w, 0, 23, 36, DECOR_TORCH1);
    paint_decor(w, 0, 26, 36, DECOR_TORCH1);
    paint_decor(w, 0, 18, 51, DECOR_BRAZIER);
    paint_decor(w, 0, 31, 51, DECOR_BRAZIER);
    paint_decor(w, 0, 21, 26, DECOR_TORCH1);

    // The toll-watch (team 0): unnamed allied garrison, all on posts —
    // deliberately unprotected (the defense band is the measure, not
    // SAVE_ALL).
    // (Design-doc deviation, F4 calibration: the toll-watch hardens to
    // westlands garrison weight — lvl-8 warders with a lvl-6 cleric as
    // the line of sustain — and the assault sheds levels across the
    // board. The doc's build lost the fort with the whole garrison dead
    // by tick 900 at curve; the brawler-AI crew sorties instead of
    // holding, so the fort must be able to hold its own doors.)
    // (F4: the two gate warders' doc anchors (24,33)/(25,33) OVERLAP as
    // 2x2 footprints — an interpenetrated pair is a wedge magnet; spread
    // one anchor per gate shoulder.)
    static constexpr int warders[3][2] = {{23, 33}, {26, 33}, {24, 29}};
    for (const auto& c : warders)
        place_living(w, FAMILY_SOLDIER, 0, 0, c[0], c[1], 8, true);
    place_living(w, FAMILY_ARCHER, 0, 0, 18, 31, 6, true);
    place_living(w, FAMILY_ARCHER, 0, 0, 31, 31, 6, true);
    place_living(w, FAMILY_CLERIC, 0, 0, 27, 28, 6, true); // the toll-clerk

    // The assault (team 2), from the south camp. Wave 1 at tick 0.
    static constexpr int wave1[6][2] = {{20, 45}, {23, 44}, {26, 45},
                                        {29, 44}, {22, 46}, {27, 46}};
    for (const auto& c : wave1)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1);
    // Wave 2 at 350.
    static constexpr int wave2[5][2] = {{21, 50}, {24, 49}, {27, 50},
                                        {23, 51}, {26, 51}};
    for (int i = 0; i < 5; ++i)
        place_living(w, FAMILY_BARBARIAN, 2, 0, wave2[i][0], wave2[i][1],
                     1 + (i % 2), false, false, 350);
    // Wave 3 at 800, with its archers.
    static constexpr int wave3[6][2] = {{19, 54}, {22, 53}, {25, 54},
                                        {28, 53}, {31, 54}, {24, 55}};
    for (const auto& c : wave3)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 2, false, false,
                     800);
    place_living(w, FAMILY_ARCHER, 2, 0, 20, 56, 2, false, false, 800);
    place_living(w, FAMILY_ARCHER, 2, 0, 29, 56, 2, false, false, 800);
    // The goat-path flank at 1000 — BEHIND the north gate.
    static constexpr int flankers[4][2] = {{7, 4}, {8, 7}, {7, 10}, {8, 13}};
    for (const auto& c : flankers)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 2, false, false,
                     1000);
    // The captain and his heavies close it at 1400.
    place_living(w, FAMILY_BARBARIAN, 2, 0, 24, 57, 3, false, false, 1400);
    static constexpr int heavies[3][2] = {{21, 57}, {27, 57}, {24, 56}};
    for (const auto& c : heavies)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 2, false, false,
                     1400);
    // Camp keepers with the tents (skirmishers — the F4 parked-guard
    // remnant rule; the tents alone are the sortie objective).
    place_living(w, FAMILY_SOLDIER, 2, 0, 18, 52, 2);
    place_living(w, FAMILY_SOLDIER, 2, 0, 31, 52, 2);
    // The assault's muster — the sortie objective. (F4: lvl 1 trickles;
    // the doc's lvl-3 pair outbred the curve-4 crew.)
    place_generator(w, FAMILY_TENT, 2, 0, 20, 52, 1);
    place_generator(w, FAMILY_TENT, 2, 0, 29, 52, 1);

    // The company garrisons the bailey, lead on the west floor between
    // the gates.
    static constexpr int starts[10][2] = {{22, 31}, {22, 27}, {28, 31},
                                          {28, 26}, {30, 28}, {20, 32},
                                          {26, 32}, {30, 32}, {24, 26},
                                          {32, 29}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The optional level pays: the strongbox room, and the larder.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 18, 27);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 19, 27);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 20, 27);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 18, 28);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 20, 28);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 28);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 32, 26);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 32, 27);

    place_exit(w, 0, 24, 3, 8);   // north: down to the paymaster's road
    place_exit(w, 0, 24, 56, 6);  // backtrack: south to the Hay War cross

    // Ambience: road pebbles, corridor scree (boulders + pebbles on the
    // margins; road cols stay statistically clear), and camp scrub shrubs
    // (off Path by ground class — the camp is no crew lane until the
    // sortie).
    scatter_decor(w, 0, 17, 2, 32, 57, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 17, 36, 32, 47, 13, DECOR_PEBBLES,
                  {ScatterGround::Grass});
    scatter_decor(w, 0, 18, 48, 31, 56, 15, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_boulders(w, 0, 17, 2, 32, 22, 19);
    scatter_boulders(w, 0, 17, 36, 32, 47, 19);
    save_level_files(w, 7, "Grey Tolls",
                     {"Ledger, side work. The Grey",
                      "Tolls fort wants a garrison.",
                      "Pay is a cut of the road-toll,",
                      "counted in that same warm coin.",
                      "Hold the wall till the assault",
                      "tires. Losses go in the book."},
                     4, 5000);
}

// 8 THE PAYMASTER VANISHES: goat tracks switchbacking up dry hills to a
// rock hollow where Long Tom — the campaign's first named enemy boss, a
// lvl-7 thief whose invisibility stays ON (a vanishing paymaster-thief is
// the joke) — sits on the army's pay chest. Three cliff bands force the
// S-climb; ambushes on each terrace; a rear-guard wakes BEHIND the crew at
// tick 450; the hollow is a guard-locked ring (heavies, archers, a bought
// chirurgeon patching them). The forward exit is INSIDE the hollow: the
// crew breaks the ring and walks out over the recovered pay. Crew 4 —
// summer's mainline peak; the fork from 6/7 rejoins here.
void build_paymaster_vanishes(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(8, true, &hooks);
    init_world(level, 1, 60, 60);
    GameWorld& w = level.world();

    // Drought scrub, the dry dust pans (the dead tarn among them), and the
    // three cliff bands with alternating gaps that force the S-climb.
    paint_rect(w.grid, 4, 34, 56, 42, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 4, 20, 56, 28, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 24, 48, 44, 58, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 20, 50, 28, 55, PIX_DIRT_1); // the dead tarn
    paint_rect(w.grid, 10, 36, 16, 40, PIX_DIRT_1);
    paint_rect(w.grid, 36, 22, 42, 26, PIX_DIRT_1);
    paint_rect(w.grid, 0, 44, 49, 46, PIX_WALL2);  // band A; gap x50-59
    paint_rect(w.grid, 10, 30, 59, 32, PIX_WALL2); // band B; gap x0-9
    paint_rect(w.grid, 0, 16, 49, 18, PIX_WALL2);  // band C; gap x50-59
    // Long Tom's hollow: a ring shell with the mouth carved on the east
    // side — the east wall is two runs, leaving (20,5)-(20,8) open.
    paint_rect(w.grid, 6, 2, 19, 10, PIX_WALL2);
    paint_rect(w.grid, 20, 2, 20, 4, PIX_WALL2);
    paint_rect(w.grid, 20, 9, 20, 10, PIX_WALL2);
    // Pine clumps.
    paint_rect(w.grid, 32, 34, 37, 38, PIX_TREE_M1);
    paint_rect(w.grid, 14, 20, 19, 24, PIX_TREE_M1);
    paint_rect(w.grid, 40, 2, 46, 7, PIX_TREE_M1);
    smooth_world(w);
    // The hollow floor (kept clean — the guard fight needs open floor).
    paint_pavement(w.grid, 7, 3, 19, 9);
    // The goat track, the whole climb.
    paint_path(w.grid, 52, 47, 53, 58); // south approach
    paint_path(w.grid, 52, 44, 53, 46); // through gap A
    paint_path(w.grid, 6, 38, 53, 39);  // terrace 1 traverse west
    paint_path(w.grid, 4, 33, 5, 37);   // connector
    paint_path(w.grid, 4, 30, 5, 32);   // through gap B
    paint_path(w.grid, 4, 24, 53, 25);  // terrace 2 traverse east
    paint_path(w.grid, 52, 19, 53, 23); // connector
    paint_path(w.grid, 52, 16, 53, 18); // through gap C
    paint_path(w.grid, 22, 12, 53, 13); // head traverse west to the mouth
    paint_path(w.grid, 21, 6, 22, 12);  // up to the mouth rows y5-8
    // (Design-doc deviation, F4 calibration: two goat SCRAMBLES carved
    // mid-band through B and C, post-smooth like the westlands gate
    // carves. The doc's full-width bands defeated the AI floor outright —
    // sweeps showed the crew parked against band B for 4500 ticks at
    // every bracket up to DOUBLE curve while the head zone stood
    // untouched; the harness's brawler pathing cannot reliably take a
    // 50-tile detour twice. A human still reads the S-climb. Two walkers
    // wide (4 tiles): a 2x2-tile walker per side — narrower tubes DEADLOCK
    // when both sides path through at once, the same wedge family the F1
    // fix covers for melee pairs.)
    paint_path(w.grid, 28, 30, 31, 32); // the band-B scramble
    paint_path(w.grid, 22, 16, 25, 18); // the band-C scramble
    // The camp fire at the mouth (off the mouth lane y5-8 and the track).
    paint_decor(w, 0, 22, 4, DECOR_BRAZIER);
    // Jagged scree, SPARINGLY and OFF every lane (jagged blocks movement
    // AND projectiles): two cells in the scrub, two in the pine shadow.
    paint(w.grid, 36, 35, PIX_JAGGED_GROUND_1);
    paint(w.grid, 37, 35, PIX_JAGGED_GROUND_1);
    paint(w.grid, 16, 21, PIX_JAGGED_GROUND_1);
    paint(w.grid, 17, 21, PIX_JAGGED_GROUND_1);
    // Torches inside the hollow, west corners — clear of the chest, the
    // guards, the exit, and the mouth lane.
    paint_decor(w, 0, 7, 3, DECOR_TORCH1);
    paint_decor(w, 0, 7, 9, DECOR_TORCH1);

    // Long Tom's hillmen + war deserters (team 2). The scrub ambush on the
    // lower apron.
    // (Design-doc deviation, F4 calibration: every rank of the climb sheds
    // one level — the doc's hillmen wiped the curve-4 crew on the apron —
    // and the hollow ring trims per the doc's own clause, "trim the hollow
    // ring rather than Long Tom".)
    static constexpr int ambush[6][2] = {{18, 52}, {30, 52}, {24, 48},
                                         {44, 50}, {48, 54}, {40, 56}};
    for (const auto& c : ambush)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    // Deserters on terrace 1, with overwatch archers on the band-B lip.
    // (Design-doc deviation, one cell: the doc's (34,36) is inside its own
    // pine clump (32,34)-(37,38); the deserter posts on the open scrub
    // just west at (30,36).)
    static constexpr int deserters[4][2] = {{10, 36}, {22, 40}, {30, 36},
                                            {44, 40}};
    for (const auto& c : deserters)
        place_living(w, FAMILY_SOLDIER, 2, 0, c[0], c[1], 1);
    static constexpr int overwatch[3][2] = {{14, 34}, {28, 34}, {42, 34}};
    for (const auto& c : overwatch)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 1, true);
    // Terrace 2: bruisers and skulkers.
    static constexpr int bruisers[5][2] = {{8, 22}, {20, 26}, {30, 21},
                                           {40, 26}, {48, 22}};
    for (const auto& c : bruisers)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 2);
    place_living(w, FAMILY_THIEF, 2, 0, 14, 26, 2);
    place_living(w, FAMILY_THIEF, 2, 0, 36, 22, 2);
    // The head-zone approach watch (skirmishers — the F4 parked-guard
    // remnant rule).
    place_living(w, FAMILY_ARCHER, 2, 0, 30, 10, 2);
    place_living(w, FAMILY_ARCHER, 2, 0, 38, 12, 2);
    // The hollow ring: Long Tom by the chest (invisibility ENABLED — the
    // vanishing is the joke), his heavies, his eyes, and the bought
    // chirurgeon the intended play kills first through the mouth choke.
    // (F4: the ring SKIRMISHES — heavies, eyes and chirurgeon meet the
    // crew at the mouth and die on the field; only Long Tom holds the
    // chest. Parked guard rings were the kill-all's unvisited remnant.)
    place_named_foe(w, FAMILY_THIEF, 2, 0, 13, 6, 7, "Long Tom", true);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 11, 5, 3);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 15, 8, 3);
    place_living(w, FAMILY_ARCHER, 2, 0, 10, 8, 2);
    place_living(w, FAMILY_ARCHER, 2, 0, 16, 4, 2);
    place_living(w, FAMILY_CLERIC, 2, 0, 12, 8, 2);
    // The rear-guard wakes SW of the start at tick 450, behind the crew.
    static constexpr int rear_guard[5][2] = {{36, 55}, {39, 56}, {42, 57},
                                             {36, 58}, {39, 54}};
    for (const auto& c : rear_guard)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 2, false, false,
                     450);

    // The crew forms on the south apron, lead on the track facing north.
    // (Design-doc deviation, one row: the doc's (46,58)/(54,58) anchors put
    // the markers' 2x2 footprints flush against the map's bottom edge,
    // which the engine's passability check rejects (xover/yover >= pixmax
    // is out); one row up keeps the same rear-rank shape.)
    static constexpr int starts[10][2] = {{50, 56}, {46, 54}, {54, 54},
                                          {46, 57}, {54, 57}, {50, 53},
                                          {57, 56}, {48, 51}, {57, 52},
                                          {52, 50}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // THE CHEST, spilled in the hollow — the crew walks over the recovered
    // pay to leave — plus Tom's table.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 9, 4);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 10, 4);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 9, 5);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 10, 5);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 11, 4);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 11, 5);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 17, 8);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 17, 3);

    place_exit(w, 0, 18, 8, 9);   // Tom's own bolt-track down to Ashfall
    place_exit(w, 0, 57, 57, 6);  // backtrack: the goat track to the cross

    // Ambience: track pebbles the whole climb, drought bones (the escort
    // didn't make it), scrub shrubs off the track by ground class, and the
    // four boulder scatters dressing every zone.
    scatter_decor(w, 0, 0, 0, 59, 59, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 4, 34, 56, 42, 21, DECOR_BONES,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 20, 50, 28, 55, 9, DECOR_BONES,
                  {ScatterGround::Dirt});
    scatter_decor(w, 0, 4, 20, 56, 28, 19, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_boulders(w, 0, 0, 19, 49, 29, 23);
    scatter_boulders(w, 0, 10, 33, 59, 43, 23);
    scatter_boulders(w, 0, 30, 47, 59, 59, 27);
    scatter_boulders(w, 0, 22, 0, 49, 15, 25);
    save_level_files(w, 8, "The Paymaster Vanishes",
                     {"Ledger, black day. The army's",
                      "paymaster is gone, chest and",
                      "all. A hill thief, Long Tom,",
                      "took the season's warm coin up",
                      "the goat tracks. Fetch the",
                      "chest back or we fetch nothing."},
                     4, 5000);
}

// 9 ASHFALL FAIR: the war ends mid-fair and the fairground riots. A
// cobbled market square with two stall rows, the town strongroom on its
// north side (Kettle and the season's pay inside — Kettle is placed but
// protect-OPTIONAL per the story bible: NO SAVE_ALL, npc_flags bit 2 on
// NOBODY; his death is a ledger line, not a mission failure), looter mobs
// already in the square, three timed gate waves (250/600/1000; the
// ringleader's south wave at the door is the crisis beat), and two
// looters' bonfires feeding the riot until stamped out. Crew 5 — the
// summer close-out; the defense band is the calibration measure.
void build_ashfall_fair(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(9, true, &hooks);
    init_world(level, 1, 60, 50);
    GameWorld& w = level.world();

    // The trampled fair green, the four corner houses (solid facades, off
    // all routes), the strongroom shell, and the well cell.
    paint_rect(w.grid, 14, 10, 46, 38, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 6, 6, 14, 12, PIX_WALL2);
    paint_rect(w.grid, 46, 6, 54, 12, PIX_WALL2);
    paint_rect(w.grid, 6, 38, 14, 44, PIX_WALL2);
    paint_rect(w.grid, 46, 38, 54, 44, PIX_WALL2);
    paint_rect(w.grid, 26, 8, 34, 14, PIX_WALL2); // the strongroom
    paint(w.grid, 23, 22, PIX_WATER1);            // the well
    smooth_world(w);
    // The market square floor as one field, then the stall stubs as RAW
    // post-smooth wall (the Westlands raw-rock pattern: blocks movers and
    // shots, autotiler-inert).
    paint_pavement(w.grid, 18, 15, 42, 34);
    paint_rect(w.grid, 20, 18, 22, 19, PIX_WALL2); // stall row A
    paint_rect(w.grid, 26, 18, 28, 19, PIX_WALL2);
    paint_rect(w.grid, 32, 18, 34, 19, PIX_WALL2);
    paint_rect(w.grid, 38, 18, 40, 19, PIX_WALL2);
    paint_rect(w.grid, 23, 26, 25, 27, PIX_WALL2); // stall row B
    paint_rect(w.grid, 29, 26, 31, 27, PIX_WALL2);
    paint_rect(w.grid, 35, 26, 37, 27, PIX_WALL2);
    // Strongroom interior + door.
    paint_pavement(w.grid, 27, 9, 33, 13);
    paint_pavement(w.grid, 30, 14, 30, 14);
    // The four gate roads.
    paint_path(w.grid, 2, 24, 17, 25);  // west gate
    paint_path(w.grid, 43, 24, 57, 25); // east gate
    paint_path(w.grid, 29, 35, 30, 48); // south gate
    paint_path(w.grid, 29, 2, 30, 7);   // north lane behind the strongroom
    // The well surround; the square field covered the water cell, so
    // re-assert it — a one-cell hazard in the pavement.
    paint_pavement(w.grid, 22, 21, 24, 23);
    paint(w.grid, 23, 22, PIX_WATER1);
    // Fair torches (all OFF lanes and marker footprints) and the bonfire
    // pits beside each tent.
    paint_decor(w, 0, 19, 18, DECOR_TORCH1);
    paint_decor(w, 0, 41, 18, DECOR_TORCH1);
    paint_decor(w, 0, 22, 26, DECOR_TORCH1);
    paint_decor(w, 0, 38, 26, DECOR_TORCH1);
    paint_decor(w, 0, 26, 15, DECOR_TORCH1); // strongroom front pair
    paint_decor(w, 0, 34, 15, DECOR_TORCH1);
    // (Design-doc deviation, two rows: the doc's east pit at (49,30) lands
    // inside the east TENT's 2x2 footprint (49,29)-(50,30); (49,28)
    // mirrors the west pit's brazier-north-of-tent arrangement.)
    paint_decor(w, 0, 11, 20, DECOR_BRAZIER);
    paint_decor(w, 0, 49, 28, DECOR_BRAZIER);

    // The strongroom watch (team 0): Kettle inside — NO bit2, protect-
    // OPTIONAL; the SAVE_ALL protectees are exactly 4's Assessor and 15's
    // Reeve — and two unnamed wardens flanking the door.
    place_hero(w, FAMILY_SOLDIER, 0, 30, 11, 5, "Kettle", true, false, 0);
    // (Design-doc deviation, F4 calibration: the wardens harden lvl 3 ->
    // 7 — westlands door-ward weight — and the riot sheds levels; the
    // doc's riot broke the defense band with team 0 extinct by tick 1200
    // at curve.)
    place_living(w, FAMILY_SOLDIER, 0, 0, 28, 15, 7, true);
    place_living(w, FAMILY_SOLDIER, 0, 0, 32, 15, 7, true);

    // The riot (team 2). The square mob presses the strongroom from
    // tick 0.
    static constexpr int mob[8][2] = {{20, 21}, {24, 17}, {36, 17}, {40, 21},
                                      {21, 29}, {27, 31}, {33, 31}, {39, 29}};
    for (const auto& c : mob)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    // Fire-tenders by the bonfires.
    place_living(w, FAMILY_THIEF, 2, 0, 13, 21, 2, true);
    place_living(w, FAMILY_THIEF, 2, 0, 47, 29, 2, true);
    // West gate wave at 250.
    static constexpr int west_wave[6][2] = {{2, 23}, {4, 24}, {2, 26},
                                            {6, 25}, {4, 26}, {6, 23}};
    for (const auto& c : west_wave)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false, 250);
    // East gate wave at 600, with the drunk mercs.
    static constexpr int east_wave[5][2] = {{53, 23}, {55, 24}, {57, 25},
                                            {53, 27}, {55, 26}};
    for (const auto& c : east_wave)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false, 600);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 57, 23, 2, false, false, 600);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 57, 27, 2, false, false, 600);
    // South gate wave at 1000 — the crisis beat, straight up the south
    // road at the door, the ringleader leading it in.
    static constexpr int south_wave[6][2] = {{27, 44}, {32, 44}, {28, 46},
                                             {31, 46}, {27, 48}, {32, 48}};
    for (const auto& c : south_wave)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false,
                     1000);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 29, 47, 3, false, false, 1000);
    // The looters' bonfires — the riot feeds itself until they're stamped
    // out. (F4: lvl 1 trickles.)
    place_generator(w, FAMILY_TENT, 2, 0, 11, 21, 1);
    place_generator(w, FAMILY_TENT, 2, 0, 49, 29, 1);

    // The crew deploys between the strongroom door and the square mob —
    // protect-first posture from tick 0.
    static constexpr int starts[10][2] = {{30, 16}, {24, 16}, {36, 16},
                                          {20, 16}, {40, 16}, {24, 21},
                                          {30, 22}, {36, 21}, {18, 21},
                                          {42, 21}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // THE SEASON'S PAY, inside the strongroom (the crew eats its own
    // wages; the ledger notes it dryly), and the fair goods.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 28, 10);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 29, 10);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 31, 10);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 28, 12);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 32, 12);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 24, 18);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 36, 18);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 25, 22);

    place_exit(w, 0, 57, 24, 10); // east: the Foundry's road — autumn next
    place_exit(w, 0, 2, 24, 8);   // backtrack: west road into the hills

    // Ambience: pebbles on all four gate roads, trampled-green shrubs
    // (LightGrass — the square itself is pavement, so concealment never
    // lands on the fight), and the fair's butcher row behind the SW
    // houses.
    scatter_decor(w, 0, 2, 2, 57, 48, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 14, 10, 46, 38, 21, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 6, 45, 14, 48, 9, DECOR_BONES,
                  {ScatterGround::Grass});
    save_level_files(w, 9, "Ashfall Fair",
                     {"Ledger, fair day. War ended at",
                      "noon; Ashfall Fair went mad by",
                      "two. Looters at the strongroom",
                      "where our season's pay sits.",
                      "Kettle guards the door. Keep",
                      "him breathing. The coin is warm."},
                     3, 4500);
}

} // namespace

void build_summer(const LevelDataHooks& hooks)
{
    build_two_banners(hooks);
    build_hay_war(hooks);
    build_grey_tolls(hooks);
    build_paymaster_vanishes(hooks);
    build_ashfall_fair(hooks);
}

std::vector<ExpectedLevel> summer_expectations()
{
    return {
        // id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
        // delayed, no-specials, stairs, exits
        {5, 1, "Two Banners", 10, 0, 0, 0, 0, 27, 0, 5, 0, true, {6, 4}},
        {6, 1, "The Hay War", 10, 0, 0, 0, 0, 29, 3, 6, 0, true, {8, 7, 5}},
        {7, 1, "Grey Tolls", 10, 6, 0, 0, 0, 29, 2, 21, 0, true, {8, 6}},
        {8, 1, "The Paymaster Vanishes", 10, 0, 0, 0, 0, 33, 0, 5, 0, true,
         {9, 6}},
        {9, 1, "Ashfall Fair", 10, 3, 0, 0, 0, 30, 2, 20, 0, true, {10, 8}},
    };
}

} // namespace longseason
