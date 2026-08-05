/* The Long Season — AUTUMN (levels 10-13): The Ledger Debt (the two-floor
 * Undermill; the Foundry buys the company's paper), Cold Seams (the deep
 * mine, lava-vein warm seams, the breach to the optional vault), The Old
 * Count's Vault (OPTIONAL catacombs; The Count is a named foe), The
 * Smelter's Road (the ore-wagon escort under the first snow — a deliberate
 * SUB-40 dusting, and pure CAN_EXIT).
 *
 * Built per campaigns/longseason/README.md, "Level 10" .. "Level 13".
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

// scatter_decor's worked-stone arm, local to the vault: level 12 is carved
// in PIX_FLOOR1 ("worked stone, not dirt" — the deliberate contrast with
// 11's raw dark dirt), and PIX_FLOOR1 is smoother-inert so no ScatterGround
// class dresses it. Same hash, same guards as the shared helper: ambience
// ids only, hand-placed decor keeps its cell, nobody spawns standing in a
// bone pile.
void scatter_decor_worked_stone(GameWorld& w, int floor, int tx0, int ty0,
                                int tx1, int ty1, int modulus,
                                unsigned char decor_id)
{
    const PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 5 + y * 7) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            if (g.data[x + y * g.w] != PIX_FLOOR1)
                continue;
            const PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && dec.data[x + y * dec.w] != DECOR_NONE)
                continue; // hand-placed decor keeps its cell
            if (cell_near_entity(w, floor, x, y, 0))
                continue; // no one spawns standing in the set dressing
            paint_decor(w, floor, x, y, decor_id);
        }
}

// 10 THE LEDGER DEBT: autumn opens — the Foundry buys the Brass Kettle
// Company's paper, and the first debt-job is clearing their Undermill: a
// working watermill over race channels, squatters holding the deck and
// loft, slime breeding in the races. Floor 0 is the yard, the mill ground
// floor and the races; floor 1 is the grain loft over the mill footprint
// (PIX_AIR outside it), with two grain hatches as fast drops back down.
// Classic kill-all, then walk to an exit; NO npc_flags bit2 anywhere —
// "The Factor" (team-0 named soldier) is deliberately expendable.
void build_ledger_debt(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(10, true, &hooks);
    init_world(level, 2, 58, 44);
    GameWorld& w = level.world();
    PixieData& g0 = w.grid;
    PixieData& g1 = w.grid_for_floor(1);

    // Floor 0 — the yard, the mill, the races (pre-smooth; order matters:
    // the mill box overwrites the head-race, then the wheel pits re-open
    // the channel through the building).
    paint_rect(g0, 3, 30, 14, 40, PIX_GRASS_DARK_1); // autumn ground, SW
    paint_rect(g0, 46, 3, 56, 12, PIX_GRASS_DARK_1); // and NE
    paint_rect(g0, 0, 20, 57, 23, PIX_WATER1);  // the head-race, full width
    paint_rect(g0, 44, 24, 47, 43, PIX_WATER1); // the tail-race, south
    paint_rect(g0, 20, 8, 41, 36, PIX_WALL2);   // the mill: walls (the box),
    paint_rect(g0, 21, 9, 40, 35, PIX_FLOOR1);  // then the carved interior
    paint_rect(g0, 21, 20, 40, 23, PIX_WATER1); // wheel pits: the race back
    paint_rect(g0, 20, 20, 20, 23, PIX_WATER1); // through the building, and
    paint_rect(g0, 41, 20, 41, 23, PIX_WATER1); // the wall re-opened over it
    paint(g0, 30, 8, PIX_FLOOR1);  // north door
    paint(g0, 31, 8, PIX_FLOOR1);
    paint(g0, 30, 36, PIX_FLOOR1); // south door
    paint(g0, 31, 36, PIX_FLOOR1);
    // Floor 1 — the grain loft over the mill footprint, open air outside.
    paint_rect(g1, 0, 0, 57, 43, PIX_AIR);
    paint_rect(g1, 20, 8, 41, 36, PIX_WALL2);  // upper-storey wall ring
    paint_rect(g1, 21, 9, 40, 35, PIX_FLOOR1); // (FALL-LINE RULE: the ring
                                               // seals the loft off the
                                               // outside air; the only
                                               // falls are the hatches)
    smooth_world(w);
    // Post-smooth, autotiler-inert. Bridges (pavement over water):
    paint_pavement(g0, 10, 20, 11, 23); // west footbridge
    paint_pavement(g0, 28, 20, 29, 23); // the pit catwalk, inside the mill
    paint_pavement(g0, 52, 20, 53, 23); // east ore-bridge
    paint_pavement(g0, 44, 38, 47, 39); // tail-race bridge
    // Paths:
    paint_path(g0, 30, 2, 31, 7);   // north approach
    paint_path(g0, 4, 4, 29, 4);    // yard spur west along y4
    paint_path(g0, 30, 37, 31, 39); // the ore-track off the south door,
    paint_path(g0, 32, 38, 54, 39); // then east to the forward exit
    // Grain hatches (PIX_AIR): hatch 1 over the floor-0 pit CATWALK
    // pavement (standable below — a legal fall), hatch 2 over the south
    // hall's floor.
    paint_rect(g1, 28, 21, 29, 22, PIX_AIR);
    paint_rect(g1, 34, 28, 35, 29, PIX_AIR);
    // The Miller's den (inert dressing, painted after the smoother ran).
    paint_rect(g1, 24, 10, 30, 14, PIX_CARPET_M);
    // Stairs (aligned pairs, both cells interior floor on BOTH floors).
    stair_pair(w, 0, 38, 10); // NE stair, ground-floor north hall to loft
    stair_pair(w, 0, 23, 34); // SW stair, south hall to loft

    // Team 2 — squatters up top, slime below. Yard pickets roam.
    // (Design-doc deviation, F4 calibration: the squat sheds one level on
    // its lvl-4+ ranks, and the four BIG slimes work sheathed — a lost
    // run's unchecked big-slime splits compounded to 143 of the 150
    // MAXOBS by tick 6000, drowning the kill-all. The small/medium vermin
    // keep the split flavor; the bounded growth a curve crew races is
    // theirs.)
    static constexpr int pickets[3][2] = {{14, 10}, {44, 10}, {16, 28}};
    for (const auto& c : pickets)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    // Bank vermin along the race banks. (F4 second pass: ALL of the
    // Undermill's slime works sheathed, not just the big ones — split
    // growth during the fight itself was the margin between the 8-mixed
    // crew clearing and wiping at curve, and lost runs still flooded
    // toward MAXOBS off the small/medium chains.)
    static constexpr int vermin_small[6][2] = {{6, 18},  {15, 18}, {49, 18},
                                               {6, 25},  {16, 25}, {50, 25}};
    for (const auto& c : vermin_small)
        place_living(w, FAMILY_SMALL_SLIME, 2, 0, c[0], c[1], 1, false, true);
    static constexpr int vermin_medium[4][2] = {{8, 17}, {52, 17}, {8, 27},
                                                {52, 27}};
    for (const auto& c : vermin_medium)
        place_living(w, FAMILY_MEDIUM_SLIME, 2, 0, c[0], c[1], 1, false, true);
    // Tail-race mothers on the banks (west and east), not water; they
    // ooze (F4: remote parked guards were the kill-all's last remnants —
    // roamers seek the fight and die in it).
    place_living(w, FAMILY_SLIME, 2, 0, 42, 30, 2, false, true);
    place_living(w, FAMILY_SLIME, 2, 0, 50, 30, 2, false, true);
    // The mill halls: north squat, south squat, pit-crept slime.
    static constexpr int north_squat[4][2] = {{24, 12}, {36, 14}, {26, 17},
                                              {34, 17}};
    for (const auto& c : north_squat)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    static constexpr int south_squat[3][2] = {{25, 27}, {35, 28}, {30, 31}};
    for (const auto& c : south_squat)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1);
    place_living(w, FAMILY_SLIME, 2, 0, 23, 25, 2, false, true);
    place_living(w, FAMILY_SLIME, 2, 0, 38, 25, 2, false, true);
    // The loft crew roams the grain floor.
    static constexpr int loft_crew[4][2] = {{24, 16}, {36, 12}, {26, 30},
                                            {36, 32}};
    for (const auto& c : loft_crew)
        place_living(w, FAMILY_THIEF, 2, 1, c[0], c[1], 1);
    // The night shift returns at tick 500, at the yard edges — the NEXT
    // WAVE HUD keeps a slow crew honest after the yard is clear.
    static constexpr int night_shift[4][2] = {{2, 12}, {4, 14}, {54, 10},
                                              {56, 12}};
    for (const auto& c : night_shift)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 1, false, false, 500);
    // "The Miller" holds his den on the loft carpet (doc posture kept —
    // an F4 experiment with him prowling sent an invisible lvl-6 assassin
    // into the yard fight and wiped the curve crew).
    place_named_foe(w, FAMILY_THIEF, 2, 1, 27, 12, 6, "The Miller", true);
    // Team 0 — the creditor's witness, by the backtrack gate. Expendable:
    // no bit2, no SAVE_ALL; if he dies the ledger only docks the fee.
    place_hero(w, FAMILY_SOLDIER, 0, 5, 6, 5, "The Factor", true, true, 0);

    // The crew forms on the yard north of the mill, lead FIRST on the
    // approach path facing the door.
    place_start(w, 0, 30, 6); // LEAD
    static constexpr int starts[9][2] = {{28, 4}, {32, 4}, {26, 6}, {34, 6},
                                         {28, 2}, {32, 2}, {24, 4}, {36, 4},
                                         {22, 2}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The Miller's hoard: gold checkered over the den carpet (clear of his
    // footprint at (27,12)), silver along the loft's east shelf.
    static constexpr int hoard_gold[6][2] = {{24, 10}, {28, 10}, {26, 12},
                                             {30, 12}, {24, 14}, {28, 14}};
    for (const auto& c : hoard_gold)
        place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, c[0], c[1]);
    static constexpr int hoard_silver[4][2] = {{32, 10}, {34, 10}, {33, 11},
                                               {35, 12}};
    for (const auto& c : hoard_silver)
        place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 1, c[0], c[1]);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 22, 28);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 24, 33);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 37, 33);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 38, 30);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 33, 3);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 39, 34);

    place_exit(w, 0, 3, 4, 9);    // backtrack: the road back to Ashfall
    place_exit(w, 0, 55, 38, 11); // the ore-track east, to the deep mine

    // Ambience. Torches flanking the doors inside (door lanes x30-31 stay
    // open) and the loft stairheads; the squatters' fires in the den.
    paint_decor(w, 0, 29, 9, DECOR_TORCH1);
    paint_decor(w, 0, 32, 9, DECOR_TORCH1);
    paint_decor(w, 0, 29, 35, DECOR_TORCH1);
    paint_decor(w, 0, 32, 35, DECOR_TORCH1);
    paint_decor(w, 1, 37, 11, DECOR_TORCH1);
    paint_decor(w, 1, 24, 33, DECOR_TORCH1);
    paint_decor(w, 1, 22, 10, DECOR_BRAZIER);
    paint_decor(w, 1, 39, 10, DECOR_BRAZIER);
    // Worn pebbles over the paths; what the slime left of the mill hands
    // along the race banks OUTSIDE the mill; yard-corner brush kept OFF
    // the door lanes, the bridges and the ore-track.
    scatter_decor(w, 0, 4, 4, 31, 7, 13, DECOR_PEBBLES, {ScatterGround::Path});
    scatter_decor(w, 0, 30, 37, 54, 39, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 0, 18, 19, 19, 7, DECOR_BONES, {ScatterGround::Grass});
    scatter_decor(w, 0, 42, 18, 57, 19, 7, DECOR_BONES,
                  {ScatterGround::Grass});
    scatter_decor(w, 0, 0, 24, 19, 25, 7, DECOR_BONES, {ScatterGround::Grass});
    scatter_decor(w, 0, 48, 24, 57, 25, 7, DECOR_BONES,
                  {ScatterGround::Grass});
    scatter_decor(w, 0, 0, 0, 16, 14, 17, DECOR_SHRUB, {ScatterGround::Grass});
    scatter_decor(w, 0, 48, 28, 57, 43, 17, DECOR_SHRUB,
                  {ScatterGround::Grass});
    scatter_boulders(w, 0, 0, 0, 18, 16, 31); // a few mossy yard stones
    save_level_files(w, 10, "The Ledger Debt",
                     {"Ledger, first frost. The Foundry",
                      "bought our paper. All of it.",
                      "Terms: work it off underground.",
                      "Job one, clear their Undermill.",
                      "Squatters up top, slime below.",
                      "The advance was warm coin. Again."},
                     3, 4000);
}

// 11 COLD SEAMS: the Foundry's deep mine. The dead miners never clocked
// out; things of fire nest where the seams run WARM — the campaign's first
// physical hint that the coin's metal is wrong. Floor 1 is the upper
// gallery / adit level (the ENTRY floor); floor 0 the deep seams with the
// lava-vein visuals: impassable ribbons solid to ground walkers, crossed
// by flyers and projectiles, each leaving a trodden gap on the required
// route. The west wall of the deep workings has broken into old built
// vaults — the branch to optional 12. Kill-all; no bit2, no team-0 NPCs:
// the company goes down alone.
void build_cold_seams(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(11, true, &hooks);
    init_world(level, 2, 62, 48);
    GameWorld& w = level.world();
    PixieData& g0 = w.grid;
    PixieData& g1 = w.grid_for_floor(1);

    // Floor 1 — the upper gallery (entry): solid rock, carved in dark dirt.
    paint_rect(g1, 0, 0, 61, 47, PIX_WALL2);
    paint_rect(g1, 4, 20, 14, 28, PIX_DIRT_DARK_1);  // adit mouth + hall
    paint_rect(g1, 15, 22, 46, 26, PIX_DIRT_DARK_1); // main gallery, E-W
    paint_rect(g1, 24, 8, 40, 18, PIX_DIRT_DARK_1);  // north timber hall
    paint_rect(g1, 28, 19, 31, 21, PIX_DIRT_DARK_1); // and its corridor
    paint_rect(g1, 20, 27, 32, 34, PIX_DIRT_DARK_1); // south store rooms
    paint_rect(g1, 47, 18, 58, 30, PIX_DIRT_DARK_1); // east winch room
    // Floor 0 — the deep seams.
    paint_rect(g0, 0, 0, 61, 47, PIX_WALL2);
    paint_rect(g0, 24, 18, 40, 30, PIX_DIRT_DARK_1); // the deep crossing
    paint_rect(g0, 8, 20, 23, 25, PIX_DIRT_DARK_1);  // west drift
    paint_rect(g0, 4, 26, 12, 36, PIX_DIRT_DARK_1);  // the VAULT BREACH room
    paint_rect(g0, 26, 6, 48, 17, PIX_DIRT_DARK_1);  // north seam gallery
    paint_rect(g0, 41, 20, 56, 30, PIX_DIRT_DARK_1); // east seam
    paint_rect(g0, 20, 31, 44, 40, PIX_DIRT_DARK_1); // south seam
    paint_rect(g0, 50, 32, 58, 42, PIX_DIRT_DARK_1); // shaft room
    paint_rect(g0, 52, 31, 53, 31, PIX_DIRT_DARK_1); // and its link carve
    smooth_world(w);
    // WARM SEAMS — the lava veins (post-smooth impassable ribbons; each
    // leaves a trodden gap on the required route).
    paint_rect(g0, 34, 6, 35, 13, PIX_LAVA1);  // vein A — lanes y14-17 open
    paint_rect(g0, 46, 22, 47, 28, PIX_LAVA1); // vein B — gaps y20-21/y29-30
    paint_rect(g0, 28, 36, 41, 37, PIX_LAVA1); // vein C — lanes y31-35 above
    paint_rect(g0, 53, 40, 56, 42, PIX_LAVA1); // vein D — decorative glow
    // Stairs (aligned pairs; the cell is carved on BOTH floors).
    stair_pair(w, 0, 28, 24); // the main winze: crossing up to the gallery
    stair_pair(w, 0, 50, 28); // the winch shaft: east seam up to the winch

    // Floor 1 — the mine's dead (team 2).
    // (Design-doc deviation, F4 calibration: the mine sheds one level on
    // its lvl-5+ ranks, the seep slimes work sheathed — split growth in a
    // stalled run ran the obmap toward MAXOBS — and the open grave drops
    // to lvl 2: the doc's build cleared zero seeds at any bracket.)
    static constexpr int adit_watch[3][2] = {{17, 23}, {19, 25}, {21, 23}};
    for (const auto& c : adit_watch)
        place_living(w, FAMILY_SKELETON, 2, 1, c[0], c[1], 2);
    place_living(w, FAMILY_SKELETON, 2, 1, 26, 10, 2); // timber hall dead
    place_living(w, FAMILY_SKELETON, 2, 1, 36, 15, 2);
    place_living(w, FAMILY_SKELETON, 2, 1, 30, 12, 2);
    place_living(w, FAMILY_SKELETON, 2, 1, 33, 10, 2);
    // (F4, whole level: the mine's dead ROAM. The doc posted the winch
    // wards, lights, nests and riders on guard; every sweep ended with
    // the crew parked and 5-16 full-HP remote guards standing — the
    // parked-guard kill-all stall. Roamers hunt the crew and the fight
    // resolves; the lanes still funnel where they meet.)
    place_living(w, FAMILY_GHOST, 2, 1, 32, 16, 2); // hall light
    static constexpr int store_seep[3][2] = {{22, 29}, {27, 32}, {31, 29}};
    for (const auto& c : store_seep)
        place_living(w, FAMILY_SLIME, 2, 1, c[0], c[1], 2, false, true);
    static constexpr int winch_wards[3][2] = {{50, 20}, {54, 24}, {50, 26}};
    for (const auto& c : winch_wards)
        place_living(w, FAMILY_SKELETON, 2, 1, c[0], c[1], 2);
    place_living(w, FAMILY_GHOST, 2, 1, 55, 28, 2); // winch light
    // The open grave in the timber hall: LOW level per the generator-flood
    // lesson — a trickle, not a stream.
    place_generator(w, FAMILY_BONES, 2, 1, 25, 9, 1);

    // Floor 0 — the deep seams.
    // (F4: the deep works in SHIFTS — half the miners, riders and nests
    // wake on delays. All-at-once roamers converged and wiped the curve
    // crew at 900; all-guard posts parked the kill-all. The NEXT WAVE HUD
    // carries the second shift.)
    static constexpr int miners[8][2] = {{26, 20}, {34, 22}, {38, 28},
                                         {30, 14}, {44, 10}, {48, 24},
                                         {30, 34}, {38, 39}};
    for (int i = 0; i < 8; ++i)
        place_living(w, FAMILY_SKELETON, 2, 0, miners[i][0], miners[i][1], 2,
                     false, false, (i % 2) ? 900 : 0);
    // Vein riders — flyers posted BESIDE the lava. (Design-doc deviation,
    // F4 calibration: the doc anchors them ON the veins, but a guard
    // flyer over lava is permanently melee-unreachable and the kill-all
    // stalled on the parked riders; posted one cell off the vein they
    // still harass the lanes and can be cornered on rock.)
    static constexpr int riders[4][2] = {{32, 8}, {44, 25}, {33, 34},
                                         {51, 38}};
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_GHOST, 2, 0, riders[i][0], riders[i][1], 2,
                     false, false, (i % 2) ? 500 : 0);
    // Warm nests prowling the vein gaps the routes must thread.
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 37, 15, 2);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 43, 21, 2);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 26, 38, 2, false, false, 1100);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 52, 36, 2, false, false, 1100);
    // The breach warden — the level's mini-boss, and the signpost for the
    // optional vault (4x4 footprint inside the breach carve; he holds the
    // breach — the vault signpost must stay where the doc points).
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 8, 31, 3, true);
    static constexpr int breach_watch[3][2] = {{6, 28}, {11, 27}, {10, 35}};
    for (const auto& c : breach_watch)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2);
    // The seams wake at tick 700 — punishes camping the crossing.
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 30, 8, 2, false, false, 700);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 24, 36, 2, false, false, 700);
    place_living(w, FAMILY_GHOST, 2, 0, 40, 12, 2, false, false, 700);
    place_living(w, FAMILY_GHOST, 2, 0, 36, 32, 2, false, false, 700);

    // The crew forms in the floor-1 entry hall, lead FIRST at the hall's
    // east mouth facing the main gallery.
    place_start(w, 1, 13, 24); // LEAD
    static constexpr int starts[9][2] = {{11, 21}, {11, 24}, {11, 27},
                                         {9, 21},  {9, 24},  {9, 27},
                                         {7, 21},  {7, 24},  {7, 27}};
    for (const auto& s : starts)
        place_start(w, 1, s[0], s[1]);

    // Ore-glints — the warm metal, in situ.
    static constexpr int glints[8][2] = {{28, 7},  {44, 7},  {44, 15},
                                         {54, 22}, {54, 29}, {22, 33},
                                         {43, 39}, {28, 29}};
    for (const auto& c : glints)
        place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, c[0], c[1]);
    // The shift-boss's pay chest.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 56, 21);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 57, 22);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 57, 33);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 57, 35);
    // The miners' cache.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 21, 28);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 24, 33);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 1, 30, 33);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 25, 19);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 39, 19);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 10, 21);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 39, 9);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 21, 39);
    // By vein C: fly the veins, once.
    place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 0, 42, 33);

    // The briefing names the branch: the adit back out, the breach into
    // the Old Count's vault (OPTIONAL), the ore shaft down to the road.
    place_exit(w, 1, 4, 20, 10);
    place_exit(w, 0, 5, 34, 12);
    place_exit(w, 0, 56, 34, 13);

    // Ambience (no shrubs underground): torches at the entry mouth, the
    // winch room, both floor-1 stairhead shoulders and both floor-0 stair
    // landings; then pebbles over all carved dirt, bones over the
    // dead-miner workings, and sparse jagged spoil in the south seam
    // (modulus high, and the scatter's entity/route clearance keeps it
    // honest).
    paint_decor(w, 1, 5, 21, DECOR_TORCH1);
    paint_decor(w, 1, 5, 27, DECOR_TORCH1);
    paint_decor(w, 1, 48, 19, DECOR_TORCH1);
    paint_decor(w, 1, 57, 19, DECOR_TORCH1);
    paint_decor(w, 1, 27, 23, DECOR_TORCH1);
    paint_decor(w, 1, 49, 27, DECOR_TORCH1);
    paint_decor(w, 0, 30, 25, DECOR_TORCH1);
    paint_decor(w, 0, 52, 27, DECOR_TORCH1);
    scatter_litter(w, 0, 20, 31, 44, 40, 47);
    scatter_decor(w, 0, 0, 0, 61, 47, 9, DECOR_PEBBLES,
                  {ScatterGround::DarkDirt});
    scatter_decor(w, 1, 0, 0, 61, 47, 9, DECOR_PEBBLES,
                  {ScatterGround::DarkDirt});
    scatter_decor(w, 0, 26, 6, 48, 17, 6, DECOR_BONES,
                  {ScatterGround::DarkDirt});
    scatter_decor(w, 0, 24, 18, 40, 30, 6, DECOR_BONES,
                  {ScatterGround::DarkDirt});
    scatter_decor(w, 1, 24, 8, 40, 18, 6, DECOR_BONES,
                  {ScatterGround::DarkDirt});
    save_level_files(w, 11, "Cold Seams",
                     {"Ledger, week two below. The",
                      "Foundry's dead miners never",
                      "left. Sweep the seams. They",
                      "are warm to the touch, same as",
                      "the coin. The west wall broke",
                      "into old vaults. Your call."},
                     4, 4500);
}

// 12 THE OLD COUNT'S VAULT (OPTIONAL): the mine's west wall broke into a
// dead lord's burial vault — two hundred years of grave-goods minted from
// the SAME warm metal, which means the wrongness is old, not new. Single
// floor of WORKED STONE: a concentric guard puzzle — outer gallery ring,
// gated inner precinct, walled rotunda — held by the Count's household
// dead. Each ring gate is a STAGGERED giant + skeleton pair: bait the
// escort and it comes alone; charge the gate and both answer. Tuned +1
// over the act's median and pays it back in the act's biggest hoard.
// No bit2, no team-0: "The Count" is a NAMED ENEMY.
void build_old_counts_vault(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(12, true, &hooks);
    init_world(level, 1, 56, 44);
    GameWorld& w = level.world();
    PixieData& g = w.grid;

    // Fill solid, then carve the concentric plan in worked stone.
    paint_rect(g, 0, 0, 55, 43, PIX_WALL2);
    paint_rect(g, 2, 16, 9, 28, PIX_FLOOR1);   // BREACH ENTRY room, west
    paint_rect(g, 10, 6, 46, 11, PIX_FLOOR1);  // outer ring: north gallery
    paint_rect(g, 10, 33, 46, 38, PIX_FLOOR1); // south gallery
    paint_rect(g, 10, 12, 15, 32, PIX_FLOOR1); // west link
    paint_rect(g, 41, 12, 46, 32, PIX_FLOOR1); // east link
    paint_rect(g, 17, 13, 39, 31, PIX_FLOOR1); // inner PRECINCT (the wall
                                               // lines x16/x40/y12/y32 seal
                                               // it off the ring)
    paint_rect(g, 21, 17, 35, 27, PIX_WALL2);  // re-wall the VAULT BLOCK,
    paint_rect(g, 23, 19, 33, 25, PIX_FLOOR1); // then carve the ROTUNDA
    // GATES, carved through the wall lines (3 wide / 3 tall).
    paint_rect(g, 27, 12, 29, 12, PIX_FLOOR1); // north gate
    paint_rect(g, 27, 32, 29, 32, PIX_FLOOR1); // south gate
    paint_rect(g, 16, 21, 16, 23, PIX_FLOOR1); // west gate
    paint_rect(g, 40, 21, 40, 23, PIX_FLOOR1); // east gate
    paint_rect(g, 27, 17, 29, 18, PIX_FLOOR1); // VAULT GATE, north wall
    smooth_world(w);
    // Post-smooth dressing: the tomb floor, and the robbers' drag-marks.
    paint_pavement(g, 23, 19, 33, 25);
    paint_path(g, 3, 21, 9, 23);

    // The Count's household (team 2). The galleries are the safe(ish) lap.
    // (Design-doc deviation, F4 calibration: the household sheds two
    // levels rank-for-rank — the doc's build wiped the curve-6 crew by
    // tick 300 and cleared zero seeds at crew 7; the optional stays the
    // hardest autumn contract on the giant wall's HP and the lvl-9 Count.)
    place_living(w, FAMILY_SKELETON, 2, 0, 4, 20, 3); // breach watch
    place_living(w, FAMILY_SKELETON, 2, 0, 7, 25, 3);
    static constexpr int patrol[8][2] = {{14, 8},  {26, 8},  {38, 8},
                                         {44, 15}, {44, 28}, {38, 35},
                                         {24, 35}, {12, 28}};
    for (const auto& c : patrol)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3);
    place_living(w, FAMILY_GHOST, 2, 0, 12, 7, 3, true);  // gallery lights
    place_living(w, FAMILY_GHOST, 2, 0, 44, 36, 3, true);
    // GATE WARDS — the guard puzzle: a staggered giant + skeleton pair per
    // ring gate (giant anchors keep the full 4x4 inside the precinct).
    // The doc's ACT_GUARD posture is KEPT — "the giants HOLD their posts"
    // is this level's design gate — even though the AI-floor sweeps park
    // against a standing giant wall; the standard optional kill gate is
    // the documented calibration trade (see campaign_meta).
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 22, 13, 4, true); // north
    place_living(w, FAMILY_SKELETON, 2, 0, 31, 14, 3, true);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 26, 28, 4, true); // south
    place_living(w, FAMILY_SKELETON, 2, 0, 23, 29, 3, true);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 17, 20, 4, true); // west
    place_living(w, FAMILY_SKELETON, 2, 0, 17, 25, 3, true);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 36, 20, 4, true); // east
    place_living(w, FAMILY_SKELETON, 2, 0, 38, 25, 3, true);
    place_living(w, FAMILY_GHOST, 2, 0, 24, 14, 3, true); // gate lights
    place_living(w, FAMILY_GHOST, 2, 0, 32, 29, 3, true);
    static constexpr int precinct[4][2] = {{18, 17}, {37, 16}, {18, 27},
                                           {36, 28}};
    for (const auto& c : precinct)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3);
    // The VAULT WARD denies the straight line north gate -> vault gate;
    // the honor guard shares the rotunda with the Count without collision
    // (footprints (24-27,20-23) and (29-32,21-24)).
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 27, 13, 5, true);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 24, 20, 5, true);
    // The dead stir at tick 800 — punishes looting the ring before
    // winning it.
    static constexpr int stirred[4][2] = {{16, 7}, {42, 7}, {16, 37},
                                          {42, 37}};
    for (const auto& c : stirred)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 4, true, false, 800);
    // The set-piece boss; his death-line belongs to the results screen.
    place_named_foe(w, FAMILY_GIANT_SKELETON, 2, 0, 29, 21, 9, "The Count",
                    true);

    // The crew enters through the breach, lead FIRST at the room's east
    // mouth facing the west link.
    place_start(w, 0, 8, 22); // LEAD
    static constexpr int starts[8][2] = {{6, 18}, {6, 22}, {6, 26}, {4, 18},
                                         {4, 22}, {4, 26}, {2, 22}, {2, 26}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Decor BEFORE the treasure lattice, which skips decor-blocked cells:
    // rotunda-corner braziers, gate-shoulder torches (walkable cells OFF
    // the 3-wide gate lanes), and the precinct band-corner columns
    // (blocking — never on a gate lane or the kill-all routes).
    paint_decor(w, 0, 23, 19, DECOR_BRAZIER);
    paint_decor(w, 0, 33, 19, DECOR_BRAZIER);
    paint_decor(w, 0, 23, 25, DECOR_BRAZIER);
    paint_decor(w, 0, 33, 25, DECOR_BRAZIER);
    // (Design-doc deviation, one cell: the doc's vault-approach torch at
    // (30,16) lands inside the vault ward's 4x4 footprint (27-30, 13-16);
    // one cell east still flanks the gate approach and keeps the giant's
    // footing legal.)
    static constexpr int torches[10][2] = {{26, 11}, {30, 11}, {26, 33},
                                           {30, 33}, {15, 20}, {15, 24},
                                           {41, 20}, {41, 24}, {26, 16},
                                           {31, 16}};
    for (const auto& t : torches)
        paint_decor(w, 0, t[0], t[1], DECOR_TORCH1);
    paint_decor(w, 0, 17, 13, DECOR_COLUMN_BOTTOM);
    paint_decor(w, 0, 39, 13, DECOR_COLUMN_BOTTOM);
    paint_decor(w, 0, 17, 31, DECOR_COLUMN_BOTTOM);
    paint_decor(w, 0, 39, 31, DECOR_COLUMN_BOTTOM);
    // The hand-placed breach cluster — the last crew that tried.
    paint_decor(w, 0, 3, 17, DECOR_BONES);
    paint_decor(w, 0, 6, 26, DECOR_BONES);
    paint_decor(w, 0, 8, 19, DECOR_BONES);

    // TREASURE — the point of the level. The ROTUNDA hoard: gold/silver
    // checkerboard, skipping the Count/honor-guard footprints (entity
    // check) and the brazier corners (decor check) — the grave-coin, warm.
    for (int y = 19; y <= 25; ++y)
        for (int x = 23; x <= 33; ++x)
        {
            if ((x + y) % 2 != 0)
                continue;
            const PixieData& dec = w.decor_for_floor(0);
            if (dec.valid() && dec.data[x + y * dec.w] != DECOR_NONE)
                continue;
            if (cell_near_entity(w, 0, x, y, 0))
                continue;
            const int fam = (((x + y) / 2) % 3 == 2) ? FAMILY_SILVER_BAR
                                                     : FAMILY_GOLD_BAR;
            place(w, Order::Treasure, fam, 0, 0, x, y);
        }
    // Precinct corner caches.
    static constexpr int caches[8][2] = {{18, 14}, {19, 14}, {37, 29},
                                         {38, 29}, {18, 29}, {19, 29},
                                         {37, 14}, {38, 14}};
    for (const auto& c : caches)
        place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, c[0], c[1]);
    // Gallery grave-coins.
    static constexpr int coins[6][2] = {{13, 7},  {30, 7},  {45, 13},
                                        {45, 31}, {30, 37}, {13, 37}};
    for (const auto& c : coins)
        place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, c[0], c[1]);
    // Funeral offerings, still good — the engine's wry joke.
    static constexpr int offerings[6][2] = {{11, 10}, {45, 10}, {11, 34},
                                            {45, 34}, {20, 22}, {36, 22}};
    for (const auto& c : offerings)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, c[0], c[1]);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 12, 9);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 44, 35);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 28, 25);
    // One gulp before the vault ward.
    place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 0, 28, 16);

    place_exit(w, 0, 2, 16, 11);   // backtrack: the breach, into the mine
    place_exit(w, 0, 45, 37, 13);  // the Count's stair — up to the road

    // Bones decor is the level's signature: hashed over the outer
    // galleries and the precinct bands (worked stone, so the local
    // scatter), plus breach-room rubble.
    scatter_decor_worked_stone(w, 0, 10, 6, 46, 11, 4, DECOR_BONES);
    scatter_decor_worked_stone(w, 0, 10, 33, 46, 38, 4, DECOR_BONES);
    scatter_decor_worked_stone(w, 0, 10, 12, 15, 32, 4, DECOR_BONES);
    scatter_decor_worked_stone(w, 0, 41, 12, 46, 32, 4, DECOR_BONES);
    scatter_decor_worked_stone(w, 0, 17, 13, 39, 31, 6, DECOR_BONES);
    scatter_decor_worked_stone(w, 0, 2, 16, 9, 28, 9, DECOR_PEBBLES);
    save_level_files(w, 12, "The Old Count's Vault",
                     {"Ledger, side venture. The mine",
                      "broke into the Old Count's",
                      "vault. Two hundred years dead",
                      "and his grave-coin is warm as",
                      "our pay. Dead men keep house.",
                      "Rob it anyway. Note the losses."},
                     4, 4500);
}

// 13 THE SMELTER'S ROAD: down the mountain switchbacks from the mine head
// to the Foundry's smelter, walking the season's ore past every broke
// company in the hills. Four terraces, three bends, an ambush at each,
// pursuit on the clock behind. The first snow dusts the shoulders — 36
// tiles, DELIBERATELY under the 40-tile threshold, so WeatherKind::Snow is
// NOT forced: a dusting, not the blizzard (winter owns level 14). Pure
// SCEN_TYPE_CAN_EXIT: the crew may exit while foes remain; npc_flags bit2
// is set on NOBODY — the Ore Wagon's survival is a CALIBRATION design
// gate, not a mission-fail.
void build_smelters_road(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(13, true, &hooks);
    init_world(level, 1, 50, 60);
    GameWorld& w = level.world();
    w.type = SCEN_TYPE_CAN_EXIT;

    // The lower, colder terraces darken; the crag bands force the
    // serpentine, each leaving one wide gap (>=11 tiles — roomy for the
    // wagon's large footprint).
    paint_rect(w.grid, 0, 16, 49, 25, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 0, 44, 49, 53, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 0, 12, 38, 15, PIX_WALL2);  // band A — gap EAST
    paint_rect(w.grid, 12, 26, 49, 29, PIX_WALL2); // band B — gap WEST
    paint_rect(w.grid, 0, 40, 38, 43, PIX_WALL2);  // band C — gap EAST
    paint_rect(w.grid, 4, 8, 7, 10, PIX_TREE_M1);  // tree cover clumps,
    paint_rect(w.grid, 44, 20, 47, 22, PIX_TREE_M1); // all clear of the
    paint_rect(w.grid, 4, 34, 7, 36, PIX_TREE_M1);   // gaps and the road
    paint_rect(w.grid, 44, 52, 47, 54, PIX_TREE_M1);
    smooth_world(w);
    // FIRST SNOW on the shoulders: 12 + 12 + 12 = 36 tiles, pinned <= 39 —
    // autumn is ending, but the Snow weather stays un-forced.
    paint_rect(w.grid, 2, 2, 5, 4, PIX_SNOW1);
    paint_rect(w.grid, 44, 16, 47, 18, PIX_SNOW1);
    paint_rect(w.grid, 2, 44, 5, 46, PIX_SNOW1);
    // The road: mine gate, three bends, the smelter gate.
    paint_path(w.grid, 24, 2, 25, 7);   // off the mine gate
    paint_path(w.grid, 26, 6, 43, 7);   // east along terrace 1
    paint_path(w.grid, 42, 8, 43, 15);  // south through gap A
    paint_path(w.grid, 6, 20, 43, 21);  // west along terrace 2
    paint_path(w.grid, 6, 22, 7, 29);   // south through gap B
    paint_path(w.grid, 6, 34, 43, 35);  // east along terrace 3
    paint_path(w.grid, 42, 36, 43, 43); // south through gap C
    paint_path(w.grid, 10, 48, 43, 49); // west along terrace 4
    paint_path(w.grid, 24, 50, 25, 58); // south to the smelter gate

    // Team 0 — the convoy. The wagon IS the pile of ore: huge golem HP,
    // no specials, and it "throws its load" when pressed. It does not need
    // to reach the exit (AI never exits): the WIN is the crew reaching the
    // smelter gate; keeping the Ore Wagon alive is the design gate. The
    // second cart is ballast; the assay ore rides the lead.
    place_hero(w, FAMILY_GOLEM, 0, 23, 3, 8, "Ore Wagon", false, true, 0);
    place_hero(w, FAMILY_GOLEM, 0, 29, 1, 6, "Spare Cart", false, true, 0);
    place_living(w, FAMILY_SOLDIER, 0, 0, 21, 3, 5); // the drovers
    place_living(w, FAMILY_SOLDIER, 0, 0, 33, 5, 5); // (clear of both carts)

    // Team 2 — every broke company in the hills. Bend 1: shoulder bows in
    // the shrub patch.
    // (Design-doc deviation, F4 calibration: every ambush shoulder sheds
    // one level — the doc's hills held the curve-6 crew under the exit
    // gate's 50%-at-900 line on one seed of three.)
    static constexpr int bend1_bows[3][2] = {{45, 10}, {47, 12}, {44, 14}};
    for (const auto& c : bend1_bows)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 4, true);
    static constexpr int t2_line[3][2] = {{14, 19}, {20, 18}, {28, 22}};
    for (const auto& c : t2_line)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 5, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 34, 17, 5, true); // terrace 2 bows
    place_living(w, FAMILY_ARCHER, 2, 0, 10, 22, 5, true);
    static constexpr int bend2_knives[3][2] = {{2, 24}, {4, 27}, {9, 26}};
    for (const auto& c : bend2_knives)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 4, true);
    place_living(w, FAMILY_ARCHER, 2, 0, 2, 31, 4, true); // south lip of
    place_living(w, FAMILY_ARCHER, 2, 0, 9, 31, 4, true); // the gap
    static constexpr int t3_line[3][2] = {{16, 32}, {26, 36}, {36, 33}};
    for (const auto& c : t3_line)
        place_living(w, FAMILY_BARBARIAN, 2, 0, c[0], c[1], 5, true);
    static constexpr int t3_muscle[4][2] = {{12, 37}, {22, 33}, {30, 37},
                                            {40, 32}};
    for (const auto& c : t3_muscle)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 4); // roam
    static constexpr int bend3_bows[3][2] = {{45, 38}, {47, 40}, {44, 42}};
    for (const auto& c : bend3_bows)
        place_living(w, FAMILY_ARCHER, 2, 0, c[0], c[1], 5, true);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 24, 52, 6, true); // toll takers
    place_living(w, FAMILY_BARBARIAN, 2, 0, 28, 54, 6, true);
    place_living(w, FAMILY_THIEF, 2, 0, 14, 48, 5); // lower prowlers
    place_living(w, FAMILY_THIEF, 2, 0, 38, 50, 5);
    // The PURSUIT, on a visible clock behind the convoy (NEXT WAVE HUD;
    // wave cells sit x<=20 / x>=34, clear of both cart footprints and the
    // backtrack exit).
    static constexpr int wave1[4][2] = {{14, 2}, {17, 2}, {11, 3}, {14, 4}};
    for (const auto& c : wave1)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 4, false, false, 400);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 20, 1, 5, false, false, 400);
    static constexpr int wave2[3][2] = {{34, 2}, {37, 2}, {40, 3}};
    for (const auto& c : wave2)
        place_living(w, FAMILY_THIEF, 2, 0, c[0], c[1], 5, false, false, 800);
    place_living(w, FAMILY_ARCHER, 2, 0, 34, 4, 5, false, false, 800);
    place_living(w, FAMILY_ARCHER, 2, 0, 37, 4, 5, false, false, 800);
    // The terrace-3 bandit camp: CAN_EXIT means the trickle pressures but
    // can never block the win; lvl 2 per the flood lesson.
    place_generator(w, FAMILY_TENT, 2, 0, 36, 37, 2);

    // The crew forms on terrace 1, AHEAD of the carts — the crew screens
    // the front; the pursuit takes the rear. Lead FIRST, on the road.
    place_start(w, 0, 24, 8); // LEAD
    static constexpr int starts[9][2] = {{21, 8},  {27, 8}, {19, 6},
                                         {29, 6},  {17, 8}, {31, 8},
                                         {19, 10}, {29, 10}, {24, 10}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Rest-stone caches, one per leg; the bandits' takings at the tent
    // camp; and the downhill legs for the last stretch.
    static constexpr int caches[5][2] = {{8, 20},  {40, 21}, {8, 34},
                                         {40, 35}, {20, 48}};
    for (const auto& c : caches)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, c[0], c[1]);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 37);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 34, 39);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 39, 36);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 6, 26);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 46, 41);
    place(w, Order::Treasure, FAMILY_SPEED_POTION, 0, 0, 24, 44);

    place_exit(w, 0, 26, 1, 11);  // backtrack: the mine gate
    place_exit(w, 0, 24, 58, 14); // the smelter gate; winter's contract

    // Ambience: the LAST convoy at bend 1; warm light at the end of the
    // road (the exit beacon); pebbles down the whole road; deliberate
    // concealment ONLY on the two ambush shoulders where the guards post
    // (shrubs sit OFF the road band itself); rockfall dressing on the crag
    // shoulders.
    paint_decor(w, 0, 44, 9, DECOR_BONES);
    paint_decor(w, 0, 46, 12, DECOR_BONES);
    paint_decor(w, 0, 43, 13, DECOR_BONES);
    paint_decor(w, 0, 22, 57, DECOR_BRAZIER);
    paint_decor(w, 0, 27, 57, DECOR_BRAZIER);
    scatter_decor(w, 0, 0, 0, 49, 59, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 44, 10, 48, 14, 17, DECOR_SHRUB,
                  {ScatterGround::Grass, ScatterGround::DarkGrass});
    scatter_decor(w, 0, 2, 24, 6, 28, 17, DECOR_SHRUB,
                  {ScatterGround::Grass, ScatterGround::DarkGrass});
    scatter_boulders(w, 0, 0, 12, 49, 15, 23);
    scatter_boulders(w, 0, 12, 26, 49, 29, 23);
    scatter_boulders(w, 0, 0, 40, 49, 43, 23);
    save_level_files(w, 13, "The Smelter's Road",
                     {"Ledger, first snow on the",
                      "switchbacks. We walk the ore",
                      "down to the smelter. Every",
                      "broke company in the hills",
                      "wants a cut of the warm metal.",
                      "Lose the lead cart, lose all."},
                     3, 4000);
}

} // namespace

void build_autumn(const LevelDataHooks& hooks)
{
    build_ledger_debt(hooks);
    build_cold_seams(hooks);
    build_old_counts_vault(hooks);
    build_smelters_road(hooks);
}

std::vector<ExpectedLevel> autumn_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {10, 2, "The Ledger Debt", 10, 1, 0, 0, 0, 33, 0, 4, 15, true,
         {9, 11}},
        {11, 2, "Cold Seams", 10, 0, 0, 0, 0, 39, 1, 12, 3, true,
         {10, 12, 13}},
        {12, 1, "The Old Count's Vault", 9, 0, 0, 0, 0, 33, 0, 4, 0, false,
         {11, 13}},
        {13, 1, "The Smelter's Road", 10, 4, 0, 0, 0, 37, 1, 10, 2, false,
         {11, 14}},
    };
}

} // namespace longseason
