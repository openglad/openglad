/* War of the Westlands — Act I, THE FLIGHT EAST (levels 1-4), plus the
 * epilogue shore (25 The Scouring, 26 The Grey Ships).
 *
 * The epilogue lives here because 25 replays level 1's map: the Quiet Vale
 * geometry is factored into shared painters (paint_quiet_vale_base/_decor)
 * used by both builders — the full-circle callback IS the level.
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
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

namespace westlands {
namespace {

// --- The Quiet Vale, shared geometry (levels 1 and 25). ---------------------
// Pre-smooth base: the west wood split by the road gap (rows 17-23), the
// north scrub, the pond, the village green, and the two huts that survive
// both visits (A and C). Levels add their own structures (1: huts B and D;
// 25: the burned lot, the old hall, the stockade) before smoothing.
void paint_quiet_vale_base(GameWorld& w)
{
    paint_rect(w.grid, 0, 0, 7, 16, PIX_TREE_M1);          // west wood, north
    paint_rect(w.grid, 0, 24, 7, 39, PIX_TREE_M1);         // and south of the gap
    paint_rect(w.grid, 20, 1, 30, 4, PIX_GRASS_DARK_1);    // north scrub
    paint_rect(w.grid, 42, 2, 52, 10, PIX_GRASS_LIGHT_1);  // pond fringe
    paint_rect(w.grid, 44, 3, 50, 8, PIX_WATER1);          // the pond
    paint_rect(w.grid, 26, 17, 32, 22, PIX_GRASS_LIGHT_1); // village green
    paint_rect(w.grid, 18, 12, 22, 15, PIX_WALL2);         // hut A
    paint_rect(w.grid, 20, 24, 24, 27, PIX_WALL2);         // hut C
}

// Post-smooth decor shared by both visits: hut A/C hearths and doors, the
// well on the green, the west and east roads, and lanes A and C.
void paint_quiet_vale_decor(GameWorld& w)
{
    paint_pavement(w.grid, 19, 13, 21, 14); // hut A hearth
    paint_pavement(w.grid, 20, 15, 20, 15); // and south door
    paint_pavement(w.grid, 21, 25, 23, 26); // hut C hearth
    paint_pavement(w.grid, 22, 24, 22, 24); // and north door
    paint_pavement(w.grid, 28, 19, 30, 21); // the well on the green
    paint(w.grid, 29, 20, PIX_WATER1);
    paint_path(w.grid, 2, 19, 25, 20);      // west road
    paint_path(w.grid, 33, 19, 57, 20);     // east road
    paint_path(w.grid, 20, 16, 20, 18);     // lane A
    paint_path(w.grid, 22, 21, 22, 23);     // lane C
}

// 1 THE QUIET VALE: village night-raid. Wolves (orcs) rush the green from
// the west road gap while a scrub pack trickles in from the north; at tick
// 600 the first two Pale Riders wake on the deep west road. The Bearer
// shelters behind the crew's line (not yet SAVE_ALL cargo — that starts on
// the Forest Road). Kill-all, then take the east road.
void build_quiet_vale(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(1, true, &hooks);
    init_world(level, 1, 60, 40);
    GameWorld& w = level.world();

    paint_quiet_vale_base(w);
    paint_rect(w.grid, 30, 10, 34, 13, PIX_WALL2); // hut B
    paint_rect(w.grid, 32, 25, 36, 28, PIX_WALL2); // hut D
    smooth_world(w);
    paint_quiet_vale_decor(w);
    paint_pavement(w.grid, 31, 11, 33, 12); // hut B hearth
    paint_pavement(w.grid, 32, 13, 32, 13); // and south door
    paint_pavement(w.grid, 33, 26, 35, 27); // hut D hearth
    paint_pavement(w.grid, 34, 25, 34, 25); // and north door
    paint_path(w.grid, 32, 14, 32, 18);     // lane B
    paint_path(w.grid, 34, 21, 34, 24);     // lane D

    // The wolves (team 2): the west-gap rush. All scrubs are level 1 — the
    // night raid's menace is numbers and the Riders behind it, not stats
    // (an entry-power lvl-1 crew must be able to hold the green).
    static constexpr int wolves[4][2] = {{2, 18}, {2, 21}, {4, 17}, {4, 22}};
    for (const auto& c : wolves)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1);
    static constexpr int scrub_pack[3][2] = {{21, 2}, {24, 3}, {27, 2}};
    for (const auto& c : scrub_pack)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1);
    // The first Pale Riders, waking on the deep west road at tick 600 —
    // after the wolf rush is decided, not on top of it. Level 2: a dread
    // rearguard fight for a battle-worn lvl-1 crew, not an execution.
    place_living(w, FAMILY_GHOST, 2, 0, 1, 19, 2, false, false, 600);
    place_living(w, FAMILY_GHOST, 2, 0, 1, 21, 2, false, false, 600);

    // The crew musters on the green, lead on the west road facing the wood.
    static constexpr int starts[10][2] = {{24, 19}, {26, 17}, {26, 21},
                                          {31, 17}, {31, 21}, {22, 17},
                                          {34, 18}, {28, 15}, {28, 22},
                                          {36, 19}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer shelters on the east road behind the whole crew line (the
    // green is a melee — anywhere nearer and he joins it). Not protected
    // here (type 0).
    place_hero(w, FAMILY_THIEF, 0, 40, 19, 3, "The Bearer", true, false, 0);

    // Hearth provisions, hut savings, and the pond cache.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 19, 13);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 31, 11);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 21, 25);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 26);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 43, 9);

    place_exit(w, 0, 57, 19, 2); // the east road, on to the forest
    scatter_boulders(w, 0, 8, 0, 19, 16, 21);  // stony wood margins, north
    scatter_boulders(w, 0, 8, 24, 19, 39, 21); // and south
    save_level_files(w, 1, "The Quiet Vale",
                     {"Night falls on the quiet vale.",
                      "Wolves howl in the west wood.",
                      "Pale Riders seek the Bearer.",
                      "Hold the village till dawn,",
                      "then take the east road."},
                     2, 3000);
}

// 2 THE FOREST ROAD (THE FLIGHT): a winding corridor maze carved through
// wall-to-wall trees. Pursuit ghosts wake behind the crew (150/400) and the
// rider den never stops feeding; guard-orc pockets sit at chokepoint EDGES
// so a sprinting crew can slip past. CAN_EXIT + SAVE_ALL: the Bearer has
// gone ahead to the hollow by the road's end — run, don't win.
void build_forest_road(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(2, true, &hooks);
    init_world(level, 1, 90, 40);
    GameWorld& w = level.world();
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);

    paint_rect(w.grid, 0, 0, 89, 39, PIX_TREE_M1);   // the forest, wall to wall
    paint_rect(w.grid, 2, 16, 10, 25, PIX_GRASS1);   // west muster clearing
    paint_rect(w.grid, 2, 27, 6, 30, PIX_GRASS1);    // the rider den
    paint_rect(w.grid, 3, 25, 4, 27, PIX_GRASS1);    // and its connector
    paint_rect(w.grid, 11, 19, 25, 22, PIX_GRASS1);  // seg A, east
    paint_rect(w.grid, 23, 8, 26, 19, PIX_GRASS1);   // bend 1, north
    paint_rect(w.grid, 26, 8, 45, 11, PIX_GRASS1);   // seg B, east
    paint_rect(w.grid, 43, 11, 46, 28, PIX_GRASS1);  // bend 2, south
    paint_rect(w.grid, 39, 25, 42, 27, PIX_GRASS1);  // dead-end glade connector
    paint_rect(w.grid, 28, 24, 38, 28, PIX_GRASS1);  // the loot glade
    paint_rect(w.grid, 46, 25, 68, 28, PIX_GRASS1);  // seg C, east
    paint_rect(w.grid, 56, 18, 57, 24, PIX_GRASS1);  // pocket 2 connector
    paint_rect(w.grid, 52, 13, 61, 18, PIX_GRASS1);  // pocket 2
    paint_rect(w.grid, 66, 14, 69, 25, PIX_GRASS1);  // bend 3, north
    paint_rect(w.grid, 69, 14, 86, 17, PIX_GRASS1);  // seg D, east
    paint_rect(w.grid, 80, 10, 87, 20, PIX_GRASS1);  // east clearing
    paint_rect(w.grid, 76, 6, 80, 9, PIX_GRASS1);    // the Bearer's hollow
    paint_rect(w.grid, 78, 10, 79, 13, PIX_GRASS1);  // and its connector
    paint_rect(w.grid, 31, 25, 34, 27, PIX_WATER1);  // the glade pool
    smooth_world(w);
    // The worn road, following the corridor centerlines.
    paint_path(w.grid, 11, 20, 25, 21);
    paint_path(w.grid, 24, 9, 25, 19);
    paint_path(w.grid, 26, 9, 44, 10);
    paint_path(w.grid, 44, 11, 45, 27);
    paint_path(w.grid, 46, 26, 67, 27);
    paint_path(w.grid, 67, 15, 68, 25);
    paint_path(w.grid, 69, 15, 85, 16);

    // The hunt (team 2): pursuit ghosts wake behind the crew...
    place_living(w, FAMILY_GHOST, 2, 0, 3, 18, 5, false, false, 150);
    place_living(w, FAMILY_GHOST, 2, 0, 3, 22, 5, false, false, 150);
    place_living(w, FAMILY_GHOST, 2, 0, 2, 20, 6, false, false, 400);
    place_living(w, FAMILY_GHOST, 2, 0, 5, 20, 6, false, false, 400);
    // ...while wolf pockets hold the chokepoint edges (guards punish the
    // wrong turn, not the runner) and one pack roams seg C.
    static constexpr int bend1[3][2] = {{24, 10}, {24, 14}, {24, 17}};
    for (const auto& c : bend1)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2, true);
    place_living(w, FAMILY_ORC, 2, 0, 33, 9, 3, true);  // seg B watchers
    place_living(w, FAMILY_ORC, 2, 0, 38, 10, 3, true);
    static constexpr int glade[4][2] = {{29, 25}, {30, 27}, {35, 24}, {36, 26}};
    for (const auto& c : glade)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 3, true);
    static constexpr int bend2[3][2] = {{44, 13}, {44, 19}, {44, 24}};
    for (const auto& c : bend2)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 3, true);
    static constexpr int roamers[4][2] = {{50, 26}, {55, 26}, {60, 27}, {64, 26}};
    for (const auto& c : roamers)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2);
    static constexpr int pocket2[3][2] = {{54, 15}, {56, 16}, {59, 14}};
    for (const auto& c : pocket2)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 4, true);
    place_living(w, FAMILY_ORC, 2, 0, 67, 17, 4, true); // bend 3 watchers
    place_living(w, FAMILY_ORC, 2, 0, 67, 22, 4, true);
    // The seg-D rider picket wakes at 600: ghosts fly and chase on sight,
    // and the Bearer's hollow is minutes past a sprinting crew's reach
    // (lead -> exit is ~444 nonstop soldier ticks) — a tick-0 picket
    // corners the cargo before any player could matter.
    place_living(w, FAMILY_GHOST, 2, 0, 72, 15, 5, true, false, 600);
    place_living(w, FAMILY_GHOST, 2, 0, 75, 16, 5, true, false, 600);
    // The rider den: endless pursuit pressure — standing still bleeds you.
    place_generator(w, FAMILY_BONES, 2, 0, 3, 28, 4);

    // The crew at the road mouth, lead first, facing east.
    static constexpr int starts[9][2] = {{8, 20}, {5, 17}, {5, 23},
                                         {7, 17}, {7, 23}, {9, 17},
                                         {9, 23}, {3, 16}, {3, 24}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer is still AHEAD of the hunt: he reaches the hollow at tick
    // 450, about when a running crew makes the road's end (lead -> exit is
    // ~444 nonstop soldier ticks) — dormant cargo cannot be cornered by
    // whatever roams the east half before then. SAVE_ALL protects him from
    // the moment he arrives.
    place_hero(w, FAMILY_THIEF, 0, 77, 7, 3, "The Bearer", true, false, 450);

    // Bait: stopping for it costs you.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 32, 24);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 24);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 28, 26);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 53, 14);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 60, 16);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 57, 14);

    place_exit(w, 0, 85, 14, 3); // the road's end, east clearing
    place_exit(w, 0, 2, 16, 1);  // backtrack, NW corner of the muster clearing
    // Clearing rubble only: no litter anywhere — this is a chase map and
    // the road must stay runnable.
    scatter_boulders(w, 0, 2, 16, 10, 25, 23);
    save_level_files(w, 2, "The Forest Road",
                     {"Do not fight. RUN.",
                      "The riders are behind you.",
                      "The Bearer waits at the road's",
                      "end. He must not fall.",
                      "The east gate is always open."},
                     3, 3500);
}

// 3 THE LAST FORD: a north-south river splits the map and the only crossing
// is a three-tile path laid over the water. Five wave beats roll out of the
// west — skeletons at once, flying Riders over open water at 400 and 1200,
// ground waves funneling through the ford at 800 and 1600 — and the two
// west-bank camps keep spawning until the crew crosses and burns them.
// Extermination win: hold the east bank, break every wave, none may pass.
void build_last_ford(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(3, true, &hooks);
    init_world(level, 1, 70, 45);
    GameWorld& w = level.world();

    paint_rect(w.grid, 30, 0, 39, 44, PIX_WATER1);         // the river
    paint_rect(w.grid, 0, 0, 10, 8, PIX_TREE_M1);          // west-bank woods
    paint_rect(w.grid, 2, 30, 12, 40, PIX_TREE_M1);
    paint_rect(w.grid, 14, 4, 22, 9, PIX_GRASS_DARK_1);    // trampled scrub
    paint_rect(w.grid, 8, 16, 16, 20, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 16, 28, 24, 34, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 48, 8, 58, 14, PIX_GRASS_LIGHT_1);  // east-bank fields
    paint_rect(w.grid, 50, 28, 60, 34, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 60, 0, 69, 6, PIX_TREE_M1);         // east-bank woods
    paint_rect(w.grid, 62, 38, 69, 44, PIX_TREE_M1);
    paint_rect(w.grid, 41, 14, 42, 18, PIX_WALL2);         // earthworks flanking
    paint_rect(w.grid, 41, 26, 42, 30, PIX_WALL2);         // the ford road
    smooth_world(w);
    paint_path(w.grid, 30, 21, 39, 23); // THE FORD: the only crossing
    paint_path(w.grid, 2, 21, 29, 23);  // west approach
    paint_path(w.grid, 40, 21, 66, 23); // east road
    paint_decor(w, 0, 24, 19, DECOR_TORCH1);  // torch standards on the approach
    paint_decor(w, 0, 24, 25, DECOR_TORCH1);
    paint_decor(w, 0, 27, 19, DECOR_TORCH1);
    paint_decor(w, 0, 27, 25, DECOR_TORCH1);
    paint_decor(w, 0, 43, 17, DECOR_BRAZIER); // watch fires on the earthworks'
    paint_decor(w, 0, 43, 27, DECOR_BRAZIER); // inner lips

    // The waves (team 2), sized for an entry-power lvl-2 crew that must
    // eventually EXTERMINATE them (scenario NPCs carry full level-derived
    // stats — a lvl-4 ghost outweighs a lvl-2 crew soldier one on one, so
    // the beats stay small and the levels stay low). Wave 0 hits the ford
    // immediately.
    static constexpr int wave0[5][2] = {{14, 21}, {14, 23}, {16, 20},
                                        {16, 24}, {18, 22}};
    for (const auto& c : wave0)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2);
    // Beats land every ~400 ticks, not every 300: defense here is
    // attrition, and the line needs a recovery window between waves.
    static constexpr int wave1[2][2] = {{10, 18}, {10, 26}};
    for (const auto& c : wave1) // Riders fly: they cross anywhere
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 4, false, false, 400);
    static constexpr int wave2[5][2] = {{4, 21}, {4, 23},  {6, 20},
                                        {6, 24}, {8, 22}};
    for (int i = 0; i < 5; ++i)
        place_living(w, FAMILY_ORC, 2, 0, wave2[i][0], wave2[i][1],
                     2 + (i % 2), false, false, 800);
    static constexpr int wave3[2][2] = {{2, 16}, {2, 28}};
    for (const auto& c : wave3)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 4, false, false, 1200);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 2, 20, 3, false, false, 1600);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 2, 24, 3, false, false, 1600);
    static constexpr int last_push[3][2] = {{5, 18}, {5, 26}, {7, 19}};
    for (const auto& c : last_push)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3, false, false,
                     1600);
    // The camps: crossing to smash them is the endgame. Both are lvl 2:
    // generator cadence is rng(level*3), and a hotter camp refills the
    // field faster than a lvl-2 crew can drain it.
    place_generator(w, FAMILY_TENT, 2, 0, 15, 5, 2);   // north camp
    place_generator(w, FAMILY_BONES, 2, 0, 14, 36, 2); // south camp

    // The shield-line at the ford mouth, ranks behind, flank posts on the
    // fields against the flying Riders.
    static constexpr int starts[10][2] = {{43, 21}, {43, 23}, {46, 20},
                                          {46, 23}, {49, 20}, {49, 23},
                                          {45, 16}, {45, 27}, {48, 13},
                                          {48, 30}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The realm's rearguard (team 0): two soldiers left to hold the
    // earthworks with the crew — the only act-1 defense with placed allies,
    // because the flying Riders ignore the ford funnel entirely.
    place_living(w, FAMILY_SOLDIER, 0, 0, 43, 19, 3, true);
    place_living(w, FAMILY_SOLDIER, 0, 0, 43, 25, 3, true);

    // The quartermaster's cache, behind the line.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 55, 20);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 55, 24);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 57, 22);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 58, 22);

    place_exit(w, 0, 65, 22, 4); // the road to the refuge
    place_exit(w, 0, 1, 22, 2);  // backtrack, west road end
    scatter_boulders(w, 0, 0, 9, 29, 15, 21);  // stony west bank, north
    scatter_boulders(w, 0, 13, 35, 29, 44, 21); // and south
    save_level_files(w, 3, "The Last Ford",
                     {"The river is the last line.",
                      "They must cross at the ford.",
                      "Hold the east bank. Break",
                      "every wave. None may pass."},
                     4, 5000);
}

// 4 THE HIDDEN REFUGE: a tree-walled valley sanctuary around the hall of
// the council (two stories: the chamber below, an open-centered gallery
// above — a careless step on the gallery drops you onto the carpet). Quiet
// for ~500 ticks, then the dawn horn: orcs up the vale road, and at 1000 the
// north gap disgorges Riders and skeletons at the hall's back. Kill-all,
// then THE FIRST BRANCH: the high pass south (5) or the northern plea (7).
void build_hidden_refuge(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(4, true, &hooks);
    init_world(level, 2, 70, 50);
    GameWorld& w = level.world();

    // Floor 0: the valley walls with three gaps, the hall, huts, pool,
    // gardens.
    paint_rect(w.grid, 0, 0, 69, 6, PIX_TREE_M1);    // north band
    paint_rect(w.grid, 50, 0, 53, 6, PIX_GRASS1);    // the north path gap
    paint_rect(w.grid, 0, 44, 69, 49, PIX_TREE_M1);  // south band
    paint_rect(w.grid, 34, 44, 37, 49, PIX_GRASS1);  // the south road gap
    paint_rect(w.grid, 0, 7, 6, 43, PIX_TREE_M1);    // west band
    paint_rect(w.grid, 0, 22, 6, 27, PIX_GRASS1);    // the vale mouth
    paint_rect(w.grid, 64, 7, 69, 43, PIX_TREE_M1);  // east band — no gap;
                                                     // the hall backs onto it
    paint_rect(w.grid, 44, 18, 58, 32, PIX_WALL2);   // the hall of the council
    paint_rect(w.grid, 49, 23, 53, 27, PIX_CARPET_M); // the council carpet
    paint_rect(w.grid, 20, 14, 24, 17, PIX_WALL2);   // three huts
    paint_rect(w.grid, 18, 30, 22, 33, PIX_WALL2);
    paint_rect(w.grid, 30, 36, 34, 39, PIX_WALL2);
    paint_rect(w.grid, 25, 13, 34, 21, PIX_GRASS_LIGHT_1); // pool fringe
    paint_rect(w.grid, 27, 15, 32, 19, PIX_WATER1);        // the pool
    paint_rect(w.grid, 40, 12, 60, 16, PIX_GRASS_LIGHT_1); // gardens
    paint_rect(w.grid, 40, 34, 60, 38, PIX_GRASS_LIGHT_1);
    // Floor 1: open sky, the hall's upper story, the open gallery center
    // overlooking the chamber (air = fall-through).
    paint_rect(w.grid_for_floor(1), 0, 0, 69, 49, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 44, 18, 58, 32, PIX_WALL2);
    paint_rect(w.grid_for_floor(1), 47, 21, 55, 29, PIX_AIR);
    smooth_world(w);
    // Floor 0: the pavement ring around the carpet, the west door, braziers.
    paint_pavement(w.grid, 45, 19, 57, 22);
    paint_pavement(w.grid, 45, 28, 57, 31);
    paint_pavement(w.grid, 45, 23, 48, 27);
    paint_pavement(w.grid, 54, 23, 57, 27);
    paint_pavement(w.grid, 44, 24, 44, 26); // hall door, west face
    paint_decor(w, 0, 46, 20, DECOR_BRAZIER);
    paint_decor(w, 0, 56, 20, DECOR_BRAZIER);
    paint_decor(w, 0, 46, 30, DECOR_BRAZIER);
    paint_decor(w, 0, 56, 30, DECOR_BRAZIER);
    paint_pavement(w.grid, 21, 15, 23, 16); // hut hearths and doors
    paint_pavement(w.grid, 22, 17, 22, 17);
    paint_pavement(w.grid, 19, 31, 21, 32);
    paint_pavement(w.grid, 20, 30, 20, 30);
    paint_pavement(w.grid, 31, 37, 33, 38);
    paint_pavement(w.grid, 32, 36, 32, 36);
    paint_path(w.grid, 2, 24, 43, 25);   // vale road, mouth to hall door
    paint_path(w.grid, 51, 7, 52, 23);   // north path (through the hall's
                                         // north door, up to the gap)
    paint_path(w.grid, 35, 26, 36, 43);  // south road
    paint_path(w.grid, 22, 18, 22, 23);  // hut lanes
    paint_path(w.grid, 20, 26, 20, 29);
    paint_path(w.grid, 32, 26, 32, 35);
    // Floor 1: the gallery walkway ring and its braziers (kept clear of the
    // elf wardens' 2x2 footprints at (50,19)/(50,30)).
    paint_pavement(w.grid_for_floor(1), 45, 19, 57, 20);
    paint_pavement(w.grid_for_floor(1), 45, 30, 57, 31);
    paint_pavement(w.grid_for_floor(1), 45, 21, 46, 29);
    paint_pavement(w.grid_for_floor(1), 56, 21, 57, 29);
    paint_decor(w, 1, 53, 19, DECOR_BRAZIER);
    paint_decor(w, 1, 53, 31, DECOR_BRAZIER);
    stair_pair(w, 0, 46, 28); // SW corner of the hall interior

    // The refuge's wardens (team 0): archers on the gallery over the door,
    // a cleric at the chamber's edge.
    place_living(w, FAMILY_ELF, 0, 1, 50, 19, 3, true);
    place_living(w, FAMILY_ELF, 0, 1, 50, 30, 3, true);
    place_living(w, FAMILY_CLERIC, 0, 0, 51, 28, 4, true);

    // The dawn assault (team 2): scouts prowl now; the horn sounds at 500.
    static constexpr int scouts[4][2] = {{10, 23}, {10, 26}, {14, 22}, {14, 27}};
    for (const auto& c : scouts)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2);
    static constexpr int dawn_orcs[7][2] = {{1, 22}, {1, 24}, {1, 26},
                                            {3, 22}, {3, 24}, {3, 26},
                                            {5, 24}};
    for (const auto& c : dawn_orcs)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 3, false, false, 500);
    static constexpr int dawn_bigs[2][2] = {{2, 23}, {2, 25}};
    for (const auto& c : dawn_bigs)
        place_living(w, FAMILY_BIG_ORC, 2, 0, c[0], c[1], 4, false, false, 500);
    // The north wave lands at 1000 — the second act of the assault, against
    // a defense that just survived the dawn horn, not a coup de grace.
    static constexpr int north_ghosts[4][2] = {{50, 3}, {53, 3}, {50, 5},
                                               {53, 5}};
    for (const auto& c : north_ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 5, false, false, 1000);
    static constexpr int north_skels[6][2] = {{51, 2}, {52, 2}, {51, 4},
                                              {52, 4}, {51, 6}, {52, 6}};
    for (const auto& c : north_skels)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3, false, false,
                     1000);
    place_generator(w, FAMILY_TENT, 2, 0, 9, 9, 2);    // the raiders' camps
    place_generator(w, FAMILY_BONES, 2, 0, 12, 11, 2); // in the west valley

    // The crew camps on the road before the hall door, facing the mouth.
    static constexpr int starts[10][2] = {{40, 24}, {36, 24}, {38, 21},
                                          {38, 27}, {38, 18}, {38, 30},
                                          {35, 20}, {35, 29}, {33, 22},
                                          {33, 28}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Ranger-King takes the field at the council carpet's heart when
    // the dawn horn sounds (tick 550) — arriving with the assault keeps
    // the roaming king out of 500 ticks of pre-battle drift, so the story
    // still meets him AT the hall when the line breaks.
    place_hero(w, FAMILY_SOLDIER, 0, 50, 25, 9, "Ranger-King", true, false,
               550);

    // Council gifts on the carpet corners, gold on the hall ring, hearth
    // provisions in the huts.
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 49, 23);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 53, 27);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 47, 19);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 55, 19);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 47, 31);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 55, 31);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 21, 15);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 19, 31);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 31, 37);

    // THE BRANCH: the briefing names both roads.
    place_exit(w, 0, 35, 47, 5); // the south road: the High Pass
    place_exit(w, 0, 51, 0, 7);  // the north path: the northern plea
    place_exit(w, 0, 0, 27, 3);  // backtrack, the vale mouth
    scatter_boulders(w, 0, 7, 7, 24, 12, 21); // scree under the north rim
    save_level_files(w, 4, "The Hidden Refuge",
                     {"The refuge sleeps. The council",
                      "argues till dawn. Then horns:",
                      "the vale mouth is aflame.",
                      "Two roads lie open: the high",
                      "pass south, or the north plea."},
                     4, 4500);
}

// 25 THE SCOURING (OPTIONAL epilogue): home again — level 1's vale, occupied
// and scarred. Hut B is burned rubble, hut D has grown into the chief's old
// hall, and a stockade sits astride the east road. The crew attacks from
// the east this time: the gate fight, a running street fight across the
// green, tent camps trickling reinforcements, and the lvl-7 chief as the
// door-boss. A victory lap with teeth.
void build_scouring(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(25, true, &hooks);
    init_world(level, 1, 60, 40);
    GameWorld& w = level.world();

    paint_quiet_vale_base(w);
    // V1: hut B is BURNED — rubble where its walls stood: dark scorched
    // grass (same TYPE_GRASS_DARK genre the legacy combined rubble tile
    // had, so the autotiler shapes the patch identically) under a PEBBLES
    // decor plane.
    paint_rect(w.grid, 30, 10, 34, 13, PIX_GRASS_DARK_1);
    paint_decor_rect(w, 0, 30, 10, 34, 13, DECOR_PEBBLES);
    // V2: hut D grows into THE OLD HALL, the chief's den.
    paint_rect(w.grid, 32, 25, 38, 30, PIX_WALL2);
    // V3: the ruffians' stockade astride the east road (gap rows 19-20).
    paint_rect(w.grid, 38, 15, 38, 18, PIX_WALL2);
    paint_rect(w.grid, 38, 21, 38, 24, PIX_WALL2);
    smooth_world(w);
    paint_quiet_vale_decor(w); // no lane B — the hut it served is ash
    paint_decor(w, 0, 31, 11, DECOR_BOULDER_2); // charred beams in the burned lot
    paint_decor(w, 0, 33, 12, DECOR_BOULDER_3);
    paint_pavement(w.grid, 33, 26, 37, 29); // the old hall's floor
    paint_pavement(w.grid, 35, 25, 35, 25); // and north door
    paint_path(w.grid, 35, 21, 35, 24);     // the hall lane
    // V5: torches at the stockade gate — their watch.
    paint_decor(w, 0, 39, 16, DECOR_TORCH1);
    paint_decor(w, 0, 39, 23, DECOR_TORCH1);

    // The ruffians (team 2): enforcers flanking the stockade gap...
    place_living(w, FAMILY_BIG_ORC, 2, 0, 39, 18, 4, true);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 39, 21, 4, true);
    // ...their footpads around the well and the roads...
    static constexpr int ruffians[6][2] = {{27, 19}, {31, 20}, {28, 16},
                                           {30, 22}, {22, 19}, {44, 19}};
    for (const auto& c : ruffians)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 2);
    // ...the bully-boys quartered across the vale (a rabble, not an army:
    // scenario NPCs carry full level-derived stats, and the whole vale
    // converges on the crew — the victory lap must stay winnable)...
    static constexpr int bullies[6][2] = {{18, 17}, {22, 22}, {21, 28},
                                          {25, 25}, {37, 17}, {46, 21}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_ORC, 2, 0, bullies[i][0], bullies[i][1],
                     2 + (i % 2));
    // ...and THE CHIEF, squatting inside the old hall (deliberately unnamed:
    // a plain team-2 boss, per the cast list). He wakes at tick 400 — the
    // door-boss beat happens when the crew reaches his door, instead of him
    // wandering into the stockade scrum and dying off-screen by 200.
    place_living(w, FAMILY_BIG_ORC, 2, 0, 35, 27, 7, true, false, 400);
    place_generator(w, FAMILY_TENT, 2, 0, 27, 17, 1); // squatting on the green
    place_generator(w, FAMILY_TENT, 2, 0, 10, 19, 1); // west road picket camp

    // The crew walks home along the east road, lead first.
    static constexpr int starts[9][2] = {{55, 19}, {53, 16}, {53, 22},
                                         {57, 16}, {57, 22}, {51, 20},
                                         {49, 17}, {49, 21}, {57, 19}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer, home again and grown (lvl 5), walking in the heart of the
    // crew's column — and a few steps behind it (tick 350): the fastest
    // team-0 walker must not sprint ahead and die at the stockade gate in
    // the epilogue. NOT SAVE_ALL: the burden is gone — his death no longer
    // ends the world.
    place_hero(w, FAMILY_THIEF, 0, 55, 17, 5, "The Bearer", true, false, 350);

    // What they stole, stacked in the old hall — plus the hearths and the
    // pond cache, where it always was.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 26);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 37, 29);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 34, 29);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 36, 26);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 19, 13);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 21, 25);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 43, 9);

    place_exit(w, 0, 2, 19, 26);  // the west road, toward the grey havens
    place_exit(w, 0, 58, 22, 24); // backtrack, east road edge
    // V4: occupation squalor — smashed boards around the green (HIGH modulus
    // so the road stays passable), stony wood margins as level 1.
    scatter_litter(w, 0, 26, 16, 34, 23, 31);
    scatter_boulders(w, 0, 8, 0, 19, 16, 21);
    scatter_boulders(w, 0, 8, 24, 19, 39, 21);
    save_level_files(w, 25, "The Scouring",
                     {"Home again. Smoke on the vale.",
                      "Ruffians hold the well and the",
                      "east gate. Their chief squats",
                      "in the old hall.",
                      "Scour them out."},
                     3, 3500);
}

// 26 THE GREY SHIPS: a quiet shore at sunset. The sea fills the west, one
// grey pier reaches into it, and the crew walks in from the east road past
// the farewell stones. Three stragglers sit ON the route (so the level
// never insta-ends and nobody combs the woods); the ship at the pier's end
// sails the cursor back to level 1 — full-circle replay.
void build_grey_ships(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(26, true, &hooks);
    init_world(level, 1, 60, 35);
    GameWorld& w = level.world();

    paint_rect(w.grid, 0, 0, 14, 34, PIX_WATER1);          // the sea
    paint_rect(w.grid, 15, 0, 20, 34, PIX_GRASS_LIGHT_1);  // the strand
    paint_rect(w.grid, 24, 0, 59, 4, PIX_TREE_M1);         // framing woods
    paint_rect(w.grid, 26, 30, 59, 34, PIX_TREE_M1);
    paint_rect(w.grid, 22, 6, 30, 10, PIX_GRASS_LIGHT_1);  // dunes
    paint_rect(w.grid, 40, 22, 48, 26, PIX_GRASS_LIGHT_1);
    smooth_world(w);
    paint_pavement(w.grid, 6, 16, 20, 18); // THE PIER, shore to sea
    paint_decor(w, 0, 6, 16, DECOR_TORCH1);      // gangplank posts framing the
    paint_decor(w, 0, 6, 18, DECOR_TORCH1);      // boarding cell
    paint_path(w.grid, 21, 16, 58, 17);    // the last road
    paint_decor(w, 0, 30, 12, DECOR_BOULDER_1);  // the farewell stones
    paint_decor(w, 0, 34, 20, DECOR_BOULDER_1);
    paint_decor(w, 0, 38, 12, DECOR_BOULDER_1);
    paint_decor(w, 0, 42, 19, DECOR_BOULDER_1);

    // Token stragglers only, placed directly on the walk to the pier.
    place_living(w, FAMILY_ORC, 2, 0, 36, 15, 2);
    place_living(w, FAMILY_ORC, 2, 0, 36, 19, 2);
    place_living(w, FAMILY_SKELETON, 2, 0, 26, 17, 3);

    // The crew walks in from the east road, lead first.
    static constexpr int starts[9][2] = {{54, 16}, {56, 13}, {56, 19},
                                         {52, 12}, {52, 20}, {50, 16},
                                         {48, 12}, {48, 20}, {57, 16}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Parting gifts at the pier root: one last draught for the road.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 19, 14);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 19, 20);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 17, 17);

    // THE SHIP: the pier's end, between the torch posts. Sails home to 1.
    place_exit(w, 0, 6, 17, 1);
    scatter_boulders(w, 0, 15, 0, 22, 34, 27); // driftwood on the strand
    save_level_files(w, 26, "The Grey Ships",
                     {"The war is over. The wound",
                      "remains. At the grey havens",
                      "a white ship waits.",
                      "Say your farewells.",
                      "Board, and sail into the west."},
                     2, 2500);
}

} // namespace

void build_act1(const LevelDataHooks& hooks)
{
    build_quiet_vale(hooks);
    build_forest_road(hooks);
    build_last_ford(hooks);
    build_hidden_refuge(hooks);
    build_scouring(hooks);
    build_grey_ships(hooks);
}

std::vector<ExpectedLevel> act1_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {1, 1, "The Quiet Vale", 10, 1, 0, 0, 0, 9, 0, 2, 0, true, {2}},
        {2, 1, "The Forest Road", 9, 1, 0, 0, 0, 27, 1, 7, 0, true, {3, 1}},
        {3, 1, "The Last Ford", 10, 2, 0, 0, 0, 19, 2, 14, 0, true, {4, 2}},
        {4, 2, "The Hidden Refuge", 10, 4, 0, 0, 0, 23, 2, 20, 0, true,
         {5, 7, 3}},
        {25, 1, "The Scouring", 9, 1, 0, 0, 0, 15, 2, 2, 0, true, {26, 24}},
        {26, 1, "The Grey Ships", 9, 0, 0, 0, 0, 3, 0, 0, 0, true, {1}},
    };
}

} // namespace westlands
