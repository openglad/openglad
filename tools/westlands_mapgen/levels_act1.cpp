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
#include <openglad/core/decordefs.h>
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
    // E7 ambience: the vale while it still lives — hedgerow shrubs on the
    // wood margins, pond-fringe brush, scrub on the north edge, and the
    // roads' worn pebbles. The Scouring re-dresses this same ground,
    // thinner, when the crew comes home.
    scatter_decor(w, 0, 8, 0, 19, 39, 13, DECOR_SHRUB, {ScatterGround::Grass});
    scatter_decor(w, 0, 42, 2, 52, 10, 9, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 20, 1, 30, 4, 7, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 2, 16, 57, 24, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 1, "The Quiet Vale",
                     {"Night falls on the quiet vale.",
                      "Wolves howl in the west wood.",
                      "Pale Riders seek the Bearer.",
                      "Hold the village till dawn,",
                      "then take the east road."},
                     2, 3000);
}

// 2 THE FOREST ROAD (THE FLIGHT): a winding corridor maze carved through
// wall-to-wall trees. The Bearer starts IN the crew's column at the west
// muster clearing and runs the gauntlet east with the company (non-guard:
// team-0 AI hunts the same pockets the crew fights through, so he moves
// with the line). Pursuit riders wake BEHIND the runners (250/550) and the
// rider den starts feeding at 400; every wolf pocket is a working GUARD
// post anchored at a chokepoint EDGE — never across the road centerline —
// so a sprinting crew can slip past and only wrong turns get punished.
// CAN_EXIT + SAVE_ALL: run, don't win. Intercept geometry (see the design
// doc's table): every rider spawn is west of the lead marker, and its
// delay + straight-line flight to the east gate exceeds the crew's
// nonstop road time, so the hunt can chase but never cut ahead.
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
    // Seg A runs one column further east (x=26) than the original cut so
    // it meets bend 1's east column FLUSH (2026-07-10, forest-pathing RCA
    // 4.5i): the old junction left a one-cell grass strip at bend-1 SE —
    // column x=26 pinched between the x=27 tree wall and the tree band
    // below y=19 — which was the anchor geometry of the shove-theft column
    // livelock (three crew bodies frozen 350+ ticks). The engine fix
    // (living.cpp shove probe) breaks the livelock itself; widening the
    // junction removes the pocket so columns never form there at all.
    paint_rect(w.grid, 11, 19, 26, 22, PIX_GRASS1);  // seg A, east
    paint_rect(w.grid, 23, 8, 26, 19, PIX_GRASS1);   // bend 1, north
    paint_rect(w.grid, 26, 8, 45, 11, PIX_GRASS1);   // seg B, east
    paint_rect(w.grid, 43, 11, 46, 28, PIX_GRASS1);  // bend 2, south
    paint_rect(w.grid, 39, 25, 42, 27, PIX_GRASS1);  // glade east connector
    paint_rect(w.grid, 28, 24, 38, 28, PIX_GRASS1);  // the wolf glade
    // The glade's WEST door (Wave E1): a 2-wide cut from seg A's east end.
    // With one door off bend 2 the glade was a pure dead end whose wolves
    // sat 7 Euclidean tiles from the road across an uncrossable tree band
    // — the classic nearest-foe lure that wedged AI companies (and tempts
    // players) into grinding at the wall. Two doors make it a RISKY
    // SHORTCUT instead: brave the wolves and the pool bank, skip bend 1
    // and seg B entirely.
    paint_rect(w.grid, 26, 22, 27, 24, PIX_GRASS1);
    paint_rect(w.grid, 46, 25, 68, 28, PIX_GRASS1);  // seg C, east
    paint_rect(w.grid, 56, 18, 57, 24, PIX_GRASS1);  // pocket 2 connector
    paint_rect(w.grid, 52, 13, 61, 18, PIX_GRASS1);  // pocket 2
    paint_rect(w.grid, 66, 14, 69, 25, PIX_GRASS1);  // bend 3, north
    paint_rect(w.grid, 69, 14, 86, 17, PIX_GRASS1);  // seg D, east
    paint_rect(w.grid, 80, 10, 87, 20, PIX_GRASS1);  // east clearing
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

    // The hunt (team 2): pursuit riders wake BEHIND the company — waves 1
    // and 2 in the muster clearing, wave 3 riding out of the den. With
    // ghost flight at soldier speed, delay + straight flight to the gate
    // beats the crew's ~342-tick road time on every wave (worst margin
    // x1.69; see the design doc's intercept table) — the riders hunt the
    // runners, they never hold the finish line. Rider levels stay low
    // (2/2/3) and their scare special is SHEATHED (a frozen or fleeing
    // company mid-melee is a death sentence): an entry-power company must
    // survive being CAUGHT once and still run on.
    place_living(w, FAMILY_GHOST, 2, 0, 3, 18, 2, false, true, 250);
    place_living(w, FAMILY_GHOST, 2, 0, 3, 22, 2, false, true, 250);
    place_living(w, FAMILY_GHOST, 2, 0, 2, 20, 2, false, true, 550);
    place_living(w, FAMILY_GHOST, 2, 0, 4, 20, 2, false, true, 550);
    // ...while wolf pockets hold the chokepoint EDGES (2x2 guards anchored
    // off the centerline of every 4-wide corridor, so 2 clear tiles always
    // remain for a 2x2 runner) and one pack roams seg C. Guards hold their
    // posts now (restored Living GUARD command byte): the pockets punish
    // wrong turns and looting, not the road itself. Wolf levels climb 1..2
    // west to east — scenario NPCs carry full level-derived stats, and the
    // whole company (Bearer included) must be able to FIGHT its way east
    // when the road cannot simply be sprinted (Wave E1 playtest: an
    // AI-driven entry-power crew has to make the east clearing alive).
    static constexpr int bend1[3][2] = {{23, 10}, {25, 14}, {23, 17}};
    for (const auto& c : bend1)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1, true);
    place_living(w, FAMILY_ORC, 2, 0, 33, 8, 1, true);  // seg B watchers
    place_living(w, FAMILY_ORC, 2, 0, 38, 10, 1, true);
    static constexpr int glade[4][2] = {{29, 25}, {30, 27}, {35, 24}, {36, 26}};
    for (const auto& c : glade)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1, true);
    static constexpr int bend2[3][2] = {{43, 13}, {45, 19}, {43, 24}};
    for (const auto& c : bend2)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 1, true);
    // The pack that SHADOWS the road (Wave E1): four runners wake in
    // sequence ever further east, so from the mid-run on there is always
    // a live wolf somewhere ahead. They are the AI company's forward
    // pull: guards hold their posts by design, and a fight-through crew
    // needs moving foes to chase or it parks at the first post it cannot
    // shift (the guard-standoff engine quirk — see the Wave E report).
    // For a human runner they are the mid-run scare beats. All wake well
    // after a nonstop crew would have passed their cell.
    // First shadow-pack wolf wakes at 400 (was 500; 2026-07-10, RCA 4.5iii)
    // so the level's opening minutes — and the preview-card capture window —
    // show a moving hunter on the road. Intercept contract holds: a nonstop
    // crew passes its cell (x=48, ~seg C mouth) by ~tick 200, well before
    // the wake, and the wake still trails the 342-tick road time margin.
    place_living(w, FAMILY_ORC, 2, 0, 48, 26, 1, false, false, 400);
    place_living(w, FAMILY_ORC, 2, 0, 58, 27, 1, false, false, 1100);
    place_living(w, FAMILY_ORC, 2, 0, 67, 20, 1, false, false, 1700);
    place_living(w, FAMILY_ORC, 2, 0, 76, 15, 2, false, false, 2300);
    // ...and the last straggler crossing the east clearing itself.
    place_living(w, FAMILY_ORC, 2, 0, 83, 15, 1, false, false, 2900);
    static constexpr int pocket2[3][2] = {{54, 15}, {56, 16}, {59, 14}};
    for (const auto& c : pocket2)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2, true);
    place_living(w, FAMILY_ORC, 2, 0, 66, 17, 2, true); // bend 3 watchers
    place_living(w, FAMILY_ORC, 2, 0, 68, 22, 2, true);
    // The last watch: wolves on seg D's edges — the road's end has teeth,
    // but the gate itself stays open (nothing camps the east clearing).
    place_living(w, FAMILY_ORC, 2, 0, 72, 14, 2, true);
    place_living(w, FAMILY_ORC, 2, 0, 78, 16, 2, true);
    // The rider den empties in a LAST wave at 900 rather than hosting a
    // generator: authored when every crew death ended a SAVE_ALL mission
    // (pre-F2 legacy rule; the Bearer alone is watched now), and
    // an endless den guarantees eventual attrition in any long fight —
    // the E1 playtest needs the whole company able to reach the road's
    // end alive. Three bounded waves keep the "riders behind you" dread;
    // the den pocket itself stays as scenery (old bones).
    place_living(w, FAMILY_GHOST, 2, 0, 3, 28, 3, false, true, 900);
    place_living(w, FAMILY_GHOST, 2, 0, 5, 29, 3, false, true, 900);

    // The crew at the road mouth, lead first, facing east.
    static constexpr int starts[9][2] = {{8, 20}, {5, 17}, {5, 23},
                                         {7, 17}, {7, 23}, {9, 17},
                                         {9, 23}, {3, 16}, {3, 24}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer runs WITH the crew: he starts inside the marker wedge
    // (heart of the column, behind the lead) and is NOT a guard — team-0
    // AI chases the same foes the crew fights through, so the cargo moves
    // east with the line instead of holding a post the pursuit would
    // overrun. Thief speed (5) lets him keep up with soldiers (4) and
    // outrun the wolves (3). Specials sheathed: no bombs in the column.
    // SAVE_ALL rides on him from tick 0.
    place_hero(w, FAMILY_THIEF, 0, 6, 21, 4, "The Bearer", false, true, 0);

    // Bait: stopping for it costs you.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 32, 24);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 24);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 28, 26);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 53, 14);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 60, 16);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 57, 14);
    // Dropped supplies ON the road (the refugees before you left in haste):
    // a fighting company can eat mid-run without leaving the gauntlet.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 20, 21);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 58, 27);

    place_exit(w, 0, 85, 14, 3); // the road's end, east clearing
    place_exit(w, 0, 2, 16, 1);  // backtrack, NW corner of the muster clearing
    // Forest texture, all of it passable: shrubs on the open grass off the
    // road (DECOR_SHRUB blocks nothing), a few pebbles worn into the path,
    // and old bones around the rider den and the wolf pockets. Nothing on
    // this map's decor plane blocks ground — the road must stay runnable.
    for (int y = 0; y < w.grid.h; ++y)
    {
        for (int x = 0; x < w.grid.w; ++x)
        {
            const unsigned char t = w.grid.data[x + y * w.grid.w];
            const bool plain_grass = (t == PIX_GRASS1 || t == PIX_GRASS2 ||
                                      t == PIX_GRASS3 || t == PIX_GRASS4);
            const bool worn_path = (t == PIX_PATH_1 || t == PIX_PATH_2 ||
                                    t == PIX_PATH_3 || t == PIX_PATH_4);
            if (plain_grass && (x * 5 + y * 11) % 17 == 0 &&
                !cell_near_entity(w, 0, x, y, 0))
            {
                paint_decor(w, 0, x, y, DECOR_SHRUB);
            }
            else if (worn_path && (x * 7 + y * 3) % 23 == 0 &&
                     !cell_near_entity(w, 0, x, y, 0))
            {
                paint_decor(w, 0, x, y, DECOR_PEBBLES);
            }
        }
    }
    static constexpr int bones[6][2] = {{4, 26}, {5, 30}, {25, 12},
                                        {37, 27}, {60, 13}, {69, 24}};
    for (const auto& b : bones)
        paint_decor(w, 0, b[0], b[1], DECOR_BONES);
    // Clearing rubble only: no litter anywhere — this is a chase map and
    // the road must stay runnable.
    scatter_boulders(w, 0, 2, 16, 10, 25, 23);
    save_level_files(w, 2, "The Forest Road",
                     {"Do not fight. RUN.",
                      "The riders wake behind you.",
                      "The Bearer runs at your side;",
                      "he must not fall.",
                      "The east gate is always open."},
                     3, 3500);
}

// 3 THE LAST FORD: a north-south river splits the map and the only crossing
// is a three-tile path laid over the water. Five wave beats roll out of the
// west — skeletons at once, flying Riders over open water at 400 and 1200,
// ground waves funneling through the ford at 800 and 1600 — and the two
// west-bank camps keep spawning until the crew crosses and burns them.
// (Content batch 2026-07: every beat roughly doubled — 39 placed foes, the
// Rider wings 2 -> 5 each — because the rearguard's lvl-6 skeleton camp
// plus the four lvl-9 wards made the designed 19 feel like a skirmish, not
// the briefing's flood. The beat ticks and the five-beat shape are the
// design contract and stay exactly as drafted.)
// Extermination win: hold the east bank, break every wave, none may pass.
// The Bearer shelters behind the shield-line (SAVE_ALL from here east).
void build_last_ford(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(3, true, &hooks);
    init_world(level, 1, 70, 45);
    GameWorld& w = level.world();
    // Extermination win stays (no CAN_EXIT), but from the Forest Road on
    // the cargo travels with the company: SAVE_ALL rides on the Bearer.
    w.type = static_cast<char>(SCEN_TYPE_SAVE_ALL);

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
    // The cargo redoubt: a raw-rock dogleg off the east road behind the
    // shield-line (the finale-cleft pattern, painted AFTER smoothing on
    // purpose — smoothed wallside faces are flyer-passable and the Riders
    // FLY; raw rock blocks every mover and every shot). The mouth opens
    // north at (55,25), the shelf row 26 makes the turn, and the lane runs
    // west along row 27 to the Bearer: nothing outside has line of sight
    // on the cargo, and the way in is one body at a time past the wards.
    paint_rect(w.grid, 51, 25, 56, 25, PIX_WALL2); // north wall
    paint(w.grid, 55, 25, PIX_GRASS1);             // the mouth
    paint_rect(w.grid, 51, 26, 54, 26, PIX_WALL2); // the dogleg shelf
    paint(w.grid, 56, 26, PIX_WALL2);
    paint(w.grid, 51, 27, PIX_WALL2);
    paint(w.grid, 56, 27, PIX_WALL2);
    paint_rect(w.grid, 51, 28, 56, 28, PIX_WALL2); // south wall

    // The waves (team 2), sized against the WHOLE defense — the crew, the
    // two rearguard soldiers, the four lvl-9 door-wards, and above all the
    // rearguard's lvl-6 skeleton camp that keeps feeding the line (scenario
    // NPCs carry full level-derived stats — a lvl-4 ghost outweighs a lvl-2
    // crew soldier one on one, so the LEVELS stay low and the COUNTS carry
    // the flood). Wave 0 hits the ford immediately.
    static constexpr int wave0[9][2] = {{14, 21}, {14, 23}, {16, 20},
                                        {16, 24}, {18, 22}, {12, 20},
                                        {12, 24}, {18, 18}, {18, 26}};
    for (const auto& c : wave0)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2);
    // Beats land every ~400 ticks, not every 300: defense here is
    // attrition, and the line needs a recovery window between waves.
    static constexpr int wave1[5][2] = {{10, 18}, {10, 26}, {10, 22},
                                        {12, 16}, {12, 28}};
    for (const auto& c : wave1) // Riders fly: they cross anywhere
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 4, false, false, 400);
    static constexpr int wave2[10][2] = {{4, 21}, {4, 23}, {6, 20}, {6, 24},
                                         {8, 22}, {2, 19}, {4, 18}, {4, 26},
                                         {6, 18}, {6, 26}};
    for (int i = 0; i < 10; ++i)
        place_living(w, FAMILY_ORC, 2, 0, wave2[i][0], wave2[i][1],
                     2 + (i % 2), false, false, 800);
    static constexpr int wave3[5][2] = {{2, 16}, {2, 28}, {4, 15},
                                        {4, 29}, {6, 16}};
    for (const auto& c : wave3)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 4, false, false, 1200);
    static constexpr int last_bigs[4][2] = {{2, 20}, {2, 24}, {4, 20},
                                            {4, 24}};
    for (const auto& c : last_bigs)
        place_living(w, FAMILY_BIG_ORC, 2, 0, c[0], c[1], 3, false, false,
                     1600);
    static constexpr int last_push[6][2] = {{5, 18}, {5, 26}, {7, 19},
                                            {7, 25}, {9, 20}, {9, 24}};
    for (const auto& c : last_push)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 3, false, false,
                     1600);
    // The camps: crossing to smash them is the endgame — the trickle must
    // stay worth crossing for. The north TENT at lvl 3 (E6): generator
    // spawns take set_difficulty once now, and its GROUND spawns must
    // funnel through the held ford, so the designed level is safe to
    // restore. The south BONES den stays at the pass-1 lvl 2: its FLYING
    // Riders cross the river anywhere, and every hotter setting measured
    // (3 and the designed 4) wail-trickles the cargo redoubt open by tick
    // 6000 (Bearer gate 2/3 and 0/3 vs the required 3/3).
    place_generator(w, FAMILY_TENT, 2, 0, 15, 5, 3);   // north camp
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
    // The Bearer shelters in the redoubt behind the shield-line, its
    // door-wards body-blocking the turn and the lane in series (the
    // finale's warded-corridor pattern): SAVE_ALL rides on him from here
    // east, and anything that wants the cargo kills the wards one at a
    // time in a lane no bow or Rider can cross.
    // (Ward weight follows the finale's door-wards — lvl-9, four deep,
    // mouth/turn/lane/gate in series: a ward that falls to the wave-trickle
    // is no ward at all, and the wards never leave the redoubt. The mouth
    // post is E6's: the tick-1350..3300 surge — waves 3/4 landing on top of
    // the camps' trickle — broke a three-ward lane about one seed in three.)
    place_living(w, FAMILY_SOLDIER, 0, 0, 55, 24, 9, true); // the mouth
    place_living(w, FAMILY_SOLDIER, 0, 0, 55, 26, 9, true); // the turn
    place_living(w, FAMILY_SOLDIER, 0, 0, 54, 27, 9, true); // the lane
    place_living(w, FAMILY_SOLDIER, 0, 0, 53, 27, 9, true); // the gate
    place_hero(w, FAMILY_THIEF, 0, 52, 27, 4, "The Bearer", true, true, 0);
    // The rearguard's own camp beside the redoubt: the flying Riders'
    // scare-wail force-marches whatever it touches (through walls — it is
    // a wail), so no static ward line is enough on a Rider level. The
    // camp keeps the east road MANNED: its soldiers run down anything
    // that slips or scatters past the earthworks. Lvl 6 (E6): its spawns
    // take set_difficulty once now too, and this camp is load-bearing for
    // the cargo — the bump keeps the defense/offense ratio where the
    // pass-1 harness left it, with margin for the mid-game surge.
    // (Content batch 2026-07: 6 -> 7 alongside the doubled waves — with
    // ten Riders across the two wings, the unattended-run wail-trickle
    // cracked the redoubt by ~4800 on one seed in three; the hotter camp
    // restores the 3/3 Bearer gate the F4 pass shipped with.)
    place_generator(w, FAMILY_TENT, 0, 0, 58, 26, 7);

    // The quartermaster's cache, behind the line.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 55, 20);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 55, 24);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 57, 22);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 58, 22);

    place_exit(w, 0, 65, 22, 4); // the road to the refuge
    place_exit(w, 0, 1, 22, 2);  // backtrack, west road end
    scatter_boulders(w, 0, 0, 9, 29, 15, 21);  // stony west bank, north
    scatter_boulders(w, 0, 13, 35, 29, 44, 21); // and south
    // E7 ambience: the ford has been fought over before — old bones on
    // the west-bank flood meadows the waves march across, pebbles their
    // boots grind into the road, and field-hedge brush behind the
    // east-bank shield-line.
    scatter_decor(w, 0, 0, 9, 29, 44, 23, DECOR_BONES,
                  {ScatterGround::Grass, ScatterGround::DarkGrass});
    scatter_decor(w, 0, 2, 20, 66, 24, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 48, 8, 60, 34, 15, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
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
    // Kill-all win stays; SAVE_ALL protects the named cargo (Burden's Road).
    w.type = static_cast<char>(SCEN_TYPE_SAVE_ALL);

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
    paint_decor(w, 0, 43, 23, DECOR_TORCH1); // torch posts flanking the
    paint_decor(w, 0, 43, 27, DECOR_TORCH1); // hall door's road approach
                                             // (rows 24-26 stay open)
    paint_pavement(w.grid, 21, 15, 23, 16); // hut hearths and doors
    paint_pavement(w.grid, 22, 17, 22, 17);
    paint_pavement(w.grid, 19, 31, 21, 32);
    paint_pavement(w.grid, 20, 30, 20, 30);
    paint_pavement(w.grid, 31, 37, 33, 38);
    paint_pavement(w.grid, 32, 36, 32, 36);
    // Harden hut 3 into the cargo's bolt-hole: re-paint its south face as
    // raw rock AFTER smoothing (smoothed wallside faces are flyer-passable
    // and the north-gap Riders FLY; raw rock blocks every mover and shot),
    // so the hearth's only way in is the one-tile north door.
    paint_rect(w.grid, 30, 39, 34, 39, PIX_WALL2);
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
    // a cleric at the chamber's edge. Lvl 4/6 (E6): with the Ranger-King
    // holding the carpet from tick 0 the hall's wardens are his line of
    // sustain through the dawn and north waves — the lvl-6 cleric in
    // particular keeps the SAVE_ALL-fatal king's post standing.
    place_living(w, FAMILY_ELF, 0, 1, 50, 19, 4, true);
    place_living(w, FAMILY_ELF, 0, 1, 50, 30, 4, true);
    place_living(w, FAMILY_CLERIC, 0, 0, 51, 28, 6, true);
    // The refuge MUSTERS (content batch 2026-07, with the scaled waves):
    // a treehouse in the north garden — the sanctuary's own kin answering
    // the horn, the Deeping Wall's courtyard-muster pattern at act-1
    // weight. It is the Ford's load-bearing-ally-camp lesson applied here:
    // static wards alone cannot hold a doubled flood after the line falls,
    // and the widened north descent took the Bearer's hut on one seed in
    // three without it. Its roaming elves also finish what the guards
    // cannot — the west-valley camp trickle that used to stall the
    // kill-all endgame at ~16 foes. Lvl 3, NOT the Wall's 7: at 5 the
    // muster measured hotter than the whole flood (foes swept by ~1800,
    // the trivial fight the batch exists to fix); at 3 the horde still
    // owns the valley to ~2700 and the muster mops up, not takes over.
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 42, 13, 3);

    // The dawn assault (team 2): scouts prowl now; the horn sounds at 500.
    // (F4 batch 4: horn back at 500 — retiming to 650 traded one losing
    // seed for another; the whole wave at lvl 2 is the stable fix.)
    // (Content batch 2026-07: every wave scaled up — 38 placed foes, dawn
    // 9 -> 16, the north descent 10 -> 16 — because the refuge's own
    // wardens (the gallery elves, the lvl-6 cleric, the lvl-9 king and
    // door-wards) made the designed 23 a mop-up. The F4 lesson holds: the
    // per-walker LEVELS stay exactly at the F4 floor — the cascade wiped
    // crews when the waves got HOTTER, not when they got wider.)
    static constexpr int scouts[6][2] = {{10, 23}, {10, 26}, {14, 22},
                                         {14, 27}, {12, 20}, {12, 29}};
    for (const auto& c : scouts)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2);
    // (F4 fresh-team calibration: the whole assault dropped one level —
    // dawn orcs 3->2, dawn bigs 4->3, north ghosts 5->4, north skels 3->2.
    // The obmap context-before-load fix made harness collision production-
    // accurate, and the two-wave cascade wiped a curve-3 crew by ~1300;
    // one level off each wave lets the curve crew clear the vale.)
    static constexpr int dawn_orcs[12][2] = {{1, 22}, {1, 24}, {1, 26},
                                             {3, 22}, {3, 24}, {3, 26},
                                             {5, 24}, {1, 23}, {1, 25},
                                             {3, 23}, {3, 25}, {5, 22}};
    for (const auto& c : dawn_orcs)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 2, false, false, 500);
    static constexpr int dawn_bigs[4][2] = {{2, 23}, {2, 25}, {5, 23},
                                            {5, 25}};
    for (const auto& c : dawn_bigs)
        place_living(w, FAMILY_BIG_ORC, 2, 0, c[0], c[1], 2, false, false, 500);
    // The north wave lands at 1000 — the second act of the assault, against
    // a defense that just survived the dawn horn, not a coup de grace.
    static constexpr int north_ghosts[6][2] = {{50, 3}, {53, 3}, {50, 5},
                                               {53, 5}, {51, 1}, {52, 1}};
    for (const auto& c : north_ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, c[0], c[1], 3, false, false, 1000);
    static constexpr int north_skels[10][2] = {{51, 2}, {52, 2}, {51, 4},
                                               {52, 4}, {51, 6}, {52, 6},
                                               {50, 2}, {53, 2}, {50, 4},
                                               {53, 4}};
    for (const auto& c : north_skels)
        place_living(w, FAMILY_SKELETON, 2, 0, c[0], c[1], 2, false, false,
                     1000);
    // (F4 batch 2: camps 2 -> 1 — the AI-run endgame ground the last two
    // camp spawns for 3000 ticks; a lvl-1 trickle ends when the war does.)
    place_generator(w, FAMILY_TENT, 2, 0, 9, 9, 1);    // the raiders' camps
    place_generator(w, FAMILY_BONES, 2, 0, 12, 11, 1); // in the west valley

    // The crew camps on the road before the hall door, facing the mouth.
    static constexpr int starts[10][2] = {{40, 24}, {36, 24}, {38, 21},
                                          {38, 27}, {38, 18}, {38, 30},
                                          {35, 20}, {35, 29}, {33, 22},
                                          {33, 28}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Ranger-King at the council carpet's heart, holding it from tick 0
    // (the intended design, restored by E6): authored ACT_GUARD is honored
    // now, so he stands where the story meets him instead of needing the
    // pass-1 spawn-delay workaround that kept a ROAMING king from drifting
    // out of the hall before the horn.
    place_hero(w, FAMILY_SOLDIER, 0, 50, 25, 9, "Ranger-King", true, false, 0);
    // The Bearer shelters at hut 3's hearth, down the lane behind the
    // crew's whole line — off the vale-road axis AND off the north-wave
    // corridor through the hall. A door-ward body-blocks the hut's only
    // door; the hardened south face keeps the flying Riders out. SAVE_ALL
    // rides on him.
    // (Ward weight follows the finale's door-wards — lvl-9, in series.
    // Content batch 2026-07: a fourth ward on the porch, one step up the
    // hut lane — the same fix as the Ford's E6 mouth post: the widened
    // north descent broke a three-ward hut on one seed in three once the
    // stand-in crew wiped, and the wards must hold without them.)
    place_living(w, FAMILY_SOLDIER, 0, 0, 32, 35, 9, true); // the porch
    place_living(w, FAMILY_SOLDIER, 0, 0, 32, 37, 9, true); // the door-ward
    place_living(w, FAMILY_SOLDIER, 0, 0, 32, 38, 9, true); // the hearth
    place_living(w, FAMILY_SOLDIER, 0, 0, 33, 37, 9, true); // the corner
    place_hero(w, FAMILY_THIEF, 0, 33, 38, 4, "The Bearer", true, true, 0);

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
    // E7 ambience: the sanctuary in bloom — garden shrubs in the
    // light-grass beds and around the pool, and pebbles worn down the
    // vale road and the hut lanes.
    scatter_decor(w, 0, 40, 12, 60, 16, 7, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 40, 34, 60, 38, 7, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 25, 13, 34, 21, 9, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 2, 7, 52, 43, 15, DECOR_PEBBLES,
                  {ScatterGround::Path});
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
    // a plain team-2 boss, per the cast list). Guard holds him at his den's
    // heart now (E6: the pass-1 wake-at-400 workaround existed only because
    // a roaming chief wandered into the stockade scrum and died off-screen),
    // so the door-boss beat lands when the crew reaches his door — at the
    // designed lvl 8: a lvl-9 victory-lap crew meets him on its own terms.
    place_living(w, FAMILY_BIG_ORC, 2, 0, 35, 27, 8, true);
    place_generator(w, FAMILY_TENT, 2, 0, 27, 17, 1); // squatting on the green
    place_generator(w, FAMILY_TENT, 2, 0, 10, 19, 1); // west road picket camp

    // The crew walks home along the east road, lead first.
    static constexpr int starts[9][2] = {{55, 19}, {53, 16}, {53, 22},
                                         {57, 16}, {57, 22}, {51, 20},
                                         {49, 17}, {49, 21}, {57, 19}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    // The Bearer, home again and grown (lvl 5), standing beside the crew's
    // road at his designed post (E6: guard holds him there now — the pass-1
    // arrive-at-350 workaround only existed because a roaming Bearer would
    // sprint ahead and die at the stockade gate). NOT SAVE_ALL: the burden
    // is gone — his death no longer ends the world.
    place_hero(w, FAMILY_THIEF, 0, 52, 18, 5, "The Bearer", true, false, 0);

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
    // E7 ambience: level 1's dressing come home wrong — the hedgerows
    // gone ragged (thinner than the living vale's), the same worn road
    // pebbles, and the ruffians' gnawed bones flung around the green.
    scatter_decor(w, 0, 8, 0, 19, 39, 17, DECOR_SHRUB, {ScatterGround::Grass});
    scatter_decor(w, 0, 42, 2, 52, 10, 13, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 2, 16, 57, 24, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 18, 14, 46, 28, 19, DECOR_BONES,
                  {ScatterGround::Grass, ScatterGround::LightGrass});
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
    // E7 ambience: the quiet shore — dune-grass tufts, shingle on the
    // strand, and the last road's worn grit. Nothing blocks; quiet.
    scatter_decor(w, 0, 22, 5, 30, 11, 5, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 40, 21, 48, 27, 5, DECOR_SHRUB,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 15, 0, 20, 34, 11, DECOR_PEBBLES,
                  {ScatterGround::LightGrass});
    scatter_decor(w, 0, 21, 15, 58, 18, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
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
        {2, 1, "The Forest Road", 9, 1, 0, 0, 0, 30, 0, 11, 7, true, {3, 1}},
        {3, 1, "The Last Ford", 10, 7, 1, 0, 0, 39, 2, 30, 1, true, {4, 2}},
        {4, 2, "The Hidden Refuge", 10, 9, 1, 0, 0, 38, 2, 32, 1, true,
         {5, 7, 3}},
        {25, 1, "The Scouring", 9, 1, 0, 0, 0, 15, 2, 0, 0, true, {26, 24}},
        {26, 1, "The Grey Ships", 9, 0, 0, 0, 0, 3, 0, 0, 0, true, {1}},
    };
}

} // namespace westlands
