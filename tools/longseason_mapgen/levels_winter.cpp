/* The Long Season — WINTER (levels 14-16): The Long Toll (the Grey Tolls
 * fort under snow), Wolf Winter (SAVE_ALL, protectee "The Reeve"; Thornby's
 * dusk wolf waves), The Frozen Ford (the collapsing-ice CAN_EXIT run).
 *
 * Built per campaigns/longseason/README.md, "Level 14" .. "Level 16".
 * Every map is snow-filled wall to wall (thousands of tiles over the >=40
 * threshold), so the post-roll weather override always lands
 * WeatherKind::Snow.
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

// Deterministic snowfield pattern, mirroring paint_pavement (the shared
// helper cloned from westlands_mapgen): the SNOW tiles are genre-inert to
// the autotiler, so the drift variety must be painted in rather than
// smoothed in.
void paint_snow(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_SNOW1, PIX_SNOW2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

// 14 THE LONG TOLL: the Grey Tolls pass — the same fort as optional level 7,
// IN WINTER (winter re-dress, same landmark grammar: a walled gatehouse
// astride the road, west and east gates, the toll strongroom in the
// northeast corner). An east-west pass corridor between two cliff bands;
// the crew holds the fort, toll-breakers come from both mouths in four
// waves. Classic kill-all + walk-to-exit (type 0): the garrison are unnamed
// team-0 posts, so no protected bit is set anywhere — the first bit2 walker
// of winter is the Reeve in 15.
void build_long_toll(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(14, true, &hooks);
    init_world(level, 1, 60, 60);
    GameWorld& w = level.world();
    // type_bits 0 (the GameWorld default): kill-all, then walk to an exit.

    // Snow fill wall to wall, then the pass geometry. The pass corridor is
    // y21..38; the snow above y12 and below y47 is scenic mountainside —
    // NOTHING is placed there.
    paint_snow(w.grid, 0, 0, 59, 59);
    paint_rect(w.grid, 0, 12, 59, 20, PIX_WALL2);  // north cliff band
    paint_rect(w.grid, 0, 39, 59, 47, PIX_WALL2);  // south cliff band
    paint_snow(w.grid, 8, 12, 11, 20);  // NW gully: gap through the band,
    paint_snow(w.grid, 4, 4, 14, 11);   // ...then the pocket
    paint_rect(w.grid, 24, 22, 37, 37, PIX_WALL2); // fort walls astride the
    paint_snow(w.grid, 26, 24, 35, 35); // road; carve the courtyard,
    paint_snow(w.grid, 24, 28, 25, 31); // the WEST gate and the EAST gate
    paint_snow(w.grid, 36, 28, 37, 31); // (2-thick walls, 4-wide gates)
    paint_rect(w.grid, 32, 24, 32, 26, PIX_WALL2); // toll strongroom stubs:
    paint_rect(w.grid, 33, 26, 34, 26, PIX_WALL2); // the nook (33..35,
                                                   // 24..25) opens at (35,26)
    paint_rect(w.grid, 44, 33, 52, 37, PIX_WATER1); // frozen tarn choking
                                                    // the east approach
    // Pine clumps at the mouths, north side. (Design-doc deviation, two
    // rows: the doc's west clump (2,22)-(6,26) overlaps its own west-wave
    // cells (2,25)/(3,25) — the army table stands verbatim, the scenery
    // yields rows 25-26.)
    paint_rect(w.grid, 2, 22, 6, 24, PIX_TREE_M1);
    paint_rect(w.grid, 50, 22, 55, 26, PIX_TREE_M1);
    paint_rect(w.grid, 14, 34, 18, 37, PIX_GRASS_DARK_1); // scrub through
    paint_rect(w.grid, 40, 22, 43, 25, PIX_GRASS_DARK_1); // the snow
    smooth_world(w);
    paint_pavement(w.grid, 26, 24, 35, 35);        // courtyard pavement...
    paint_rect(w.grid, 32, 24, 32, 26, PIX_WALL2); // ...then re-assert the
    paint_rect(w.grid, 33, 26, 34, 26, PIX_WALL2); // strongroom stubs
    paint_path(w.grid, 1, 29, 23, 30);  // the toll road: west approach,
    paint_path(w.grid, 26, 29, 35, 30); // through the courtyard,
    paint_path(w.grid, 38, 29, 58, 30); // east approach
    paint(w.grid, 24, 27, PIX_TORCH1);  // gate torches
    paint(w.grid, 24, 32, PIX_TORCH1);
    paint(w.grid, 37, 27, PIX_TORCH1);
    paint(w.grid, 37, 32, PIX_TORCH1);
    paint(w.grid, 27, 25, PIX_BRAZIER1); // courtyard braziers: the winter
    paint(w.grid, 27, 34, PIX_BRAZIER1); // watch's fires
    paint(w.grid, 34, 34, PIX_BRAZIER1);

    // Team 0 — the winter watch (unnamed garrison; guard holds the posts).
    // (Design-doc deviation, F4 calibration: the watch hardens one level
    // and the toll-breaker waves shed one — the doc's siege had team 0
    // extinct by tick 1500 at curve, a third of the band's 3000.)
    static constexpr int gate_watch[4][2] = {
        {26, 28}, {26, 31}, {35, 28}, {35, 31}};
    for (const auto& p : gate_watch)
        place_living(w, FAMILY_SOLDIER, 0, 0, p[0], p[1], 7, true);
    place_living(w, FAMILY_ARCHER, 0, 0, 27, 26, 6, true); // wall watch on
    place_living(w, FAMILY_ARCHER, 0, 0, 34, 33, 6, true); // the corners

    // Team 2 — wolf strays probe first...
    static constexpr int strays[6][2] = {{12, 24}, {16, 35}, {20, 26},
                                         {42, 25}, {48, 28}, {54, 35}};
    for (const auto& p : strays)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 3);
    place_living(w, FAMILY_GHOST, 2, 0, 6, 6, 3);  // the frozen watch, in
    place_living(w, FAMILY_GHOST, 2, 0, 12, 9, 3); // the gully pocket
    // ...then the toll-breakers in four waves (300/700/1200/1800). Every
    // wave spawns at a pass MOUTH (x1..3 / x55..58), far from the crew's
    // courtyard deploy — dormant walkers are untargetable and wake with a
    // flash, so no wake-on-top-of-crew shocks.
    static constexpr int west1[5][2] = {
        {1, 26}, {1, 29}, {2, 27}, {2, 31}, {3, 29}};
    for (const auto& p : west1)
        place_living(w, FAMILY_SOLDIER, 2, 0, p[0], p[1], 3, false, false, 300);
    place_living(w, FAMILY_ARCHER, 2, 0, 1, 32, 3, false, false, 300);
    place_living(w, FAMILY_ARCHER, 2, 0, 3, 25, 3, false, false, 300);
    static constexpr int east1[4][2] = {{57, 26}, {57, 29}, {58, 27}, {58, 31}};
    for (const auto& p : east1)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 3, false, false,
                     700);
    place_living(w, FAMILY_ORC, 2, 0, 56, 24, 2, false, false, 700);
    place_living(w, FAMILY_ORC, 2, 0, 58, 24, 2, false, false, 700);
    static constexpr int west2[5][2] = {
        {1, 24}, {2, 25}, {1, 31}, {2, 33}, {3, 31}};
    for (const auto& p : west2)
        place_living(w, FAMILY_SOLDIER, 2, 0, p[0], p[1], 3, false, false,
                     1200);
    place_living(w, FAMILY_ARCHER, 2, 0, 3, 27, 3, false, false, 1200);
    place_living(w, FAMILY_ARCHER, 2, 0, 3, 33, 3, false, false, 1200);
    // The caravan master (unnamed) and his east push — the last wake.
    place_living(w, FAMILY_BARBARIAN, 2, 0, 58, 29, 4, false, false, 1800);
    static constexpr int east2[4][2] = {{56, 27}, {56, 31}, {57, 33}, {58, 33}};
    for (const auto& p : east2)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 3, false, false,
                     1800);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 55, 29, 3, false, false, 1800);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 55, 31, 3, false, false, 1800);
    // The frozen watch rises: a LOW-level den (the post-obmap-fix rule — a
    // lvl-5+ generator outbreeds a curve crew); destroying it is the
    // kill-all's last errand, one push through the gully gap.
    place_generator(w, FAMILY_BONES, 2, 0, 9, 7, 1);

    // Start markers (lead FIRST; all on courtyard pavement with 2x2
    // shoulder room, clear of the strongroom cells, the x26/x35 guard
    // posts, the stubs and the braziers).
    place_start(w, 0, 29, 30); // LEAD, mid-courtyard on the road line
    static constexpr int starts[9][2] = {{27, 28}, {32, 28}, {27, 32},
                                         {32, 32}, {29, 26}, {29, 33},
                                         {33, 30}, {27, 30}, {33, 32}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The toll chest (strongroom nook): the year's takings, warm to the
    // touch — entirely warm coin this year, and the briefing says so.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 25);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 34, 25);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 34, 24);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 28, 25); // watch stores
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 33, 34);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 30, 25);

    // East mouth: the road down to Thornby. West mouth south shoulder: the
    // switchbacks back to the Smelter's Road (both clear of the wave cells
    // and the tarn).
    place_exit(w, 0, 57, 34, 15);
    place_exit(w, 0, 2, 36, 13);

    // Ambience: the pass takes its dead (hand-set bones on the approaches
    // and in the gully pocket), two shrub accents in the scrub off every
    // lane, boulder scatter on the open pass floor outside the fort, and
    // pebbles worn through the snow on the road.
    static constexpr int bones[5][2] = {
        {6, 31}, {18, 27}, {42, 31}, {54, 27}, {9, 10}};
    for (const auto& b : bones)
        paint_decor(w, 0, b[0], b[1], DECOR_BONES);
    paint_decor(w, 0, 15, 35, DECOR_SHRUB);
    paint_decor(w, 0, 41, 23, DECOR_SHRUB);
    scatter_boulders(w, 0, 0, 21, 23, 38, 23);
    scatter_boulders(w, 0, 38, 21, 59, 38, 23);
    scatter_decor(w, 0, 0, 0, 59, 59, 13, DECOR_PEBBLES,
                  {ScatterGround::Path});
    save_level_files(w, 14, "The Long Toll",
                     {"Ledger, winter work. The Grey",
                      "Tolls want holding through the",
                      "pass trade. Some of us drew",
                      "summer pay here; the rest heard",
                      "the story. Toll chest is warm",
                      "coin to the last penny. Hold."},
                     5, 6000);
}

// 15 WOLF WINTER: Thornby, a palisaded village snowed in below the Grey
// Tolls — forest north and south, the toll road in from the west and out
// east. Wolves probe from the fields at once; the packs come out of the
// treelines in dusk waves; the den wave empties the den for good (NO
// generators — on a SAVE_ALL map an endless den guarantees eventual
// attrition against the protectee). SAVE_ALL watches the Reeve and ONLY
// him: npc_flags bit2 lands centrally in save_level_files on exactly one
// walker, and he is also the only NAMED team-0 walker on the map.
void build_wolf_winter(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(15, true, &hooks);
    init_world(level, 1, 60, 60);
    GameWorld& w = level.world();
    // Kill-all win; the mission fails if the protected walker dies.
    w.type = static_cast<char>(SCEN_TYPE_SAVE_ALL);

    paint_snow(w.grid, 0, 0, 59, 59);
    paint_rect(w.grid, 0, 0, 59, 6, PIX_TREE_M1);   // north forest
    paint_rect(w.grid, 0, 53, 59, 59, PIX_TREE_M1); // south forest
    paint_snow(w.grid, 26, 4, 30, 6); // wolf den, carved INTO the forest so
    paint_snow(w.grid, 28, 7, 28, 9); // wave 4 is reachable; den mouth
                                      // (y7..9 is open snow anyway)
    paint_rect(w.grid, 18, 10, 42, 10, PIX_WALL2); // palisade arcs, 1 thick,
    paint_snow(w.grid, 29, 10, 31, 10);            // with gaps (east and
    paint_rect(w.grid, 18, 50, 42, 50, PIX_WALL2); // west sides are open —
    paint_snow(w.grid, 29, 50, 31, 50);            // the road was the
                                                   // defense, once)
    paint_rect(w.grid, 27, 26, 34, 32, PIX_WALL2); // moot-house (the Reeve's
    paint_snow(w.grid, 28, 27, 33, 31);            // hall): walls, interior,
    paint_snow(w.grid, 30, 32, 31, 32);            // south-facing 2-wide door
    static constexpr int houses[6][4] = {
        {12, 14, 15, 16}, {44, 14, 47, 16}, {12, 42, 15, 44},
        {44, 42, 47, 44}, {20, 45, 23, 47}, {38, 12, 41, 14}};
    for (const auto& h : houses) // solid blocks — roofs seen from above
        paint_rect(w.grid, h[0], h[1], h[2], h[3], PIX_WALL2);
    paint_rect(w.grid, 48, 34, 54, 38, PIX_WATER1); // frozen mill pond
    paint_rect(w.grid, 6, 20, 16, 26, PIX_GRASS_DARK_1); // winter fields,
    paint_rect(w.grid, 44, 22, 54, 28, PIX_GRASS_DARK_1); // stubble through
    paint_rect(w.grid, 8, 32, 18, 38, PIX_GRASS_DARK_1);  // snow
    smooth_world(w);
    paint_pavement(w.grid, 28, 27, 33, 31); // moot-house floor
    paint(w.grid, 28, 28, PIX_BRAZIER1);    // the hearth
    paint_path(w.grid, 1, 29, 26, 30);      // the road: west approach,
    paint_path(w.grid, 27, 33, 34, 34);     // across the green skirting the
    paint_path(w.grid, 35, 29, 58, 30);     // hall's south face, then east
    paint_path(w.grid, 29, 11, 30, 25);     // green paths to the gaps
    paint_path(w.grid, 29, 35, 30, 49);
    paint(w.grid, 29, 32, PIX_TORCH1);      // door torches flanking the
    paint(w.grid, 32, 32, PIX_TORCH1);      // moot-house door
    paint(w.grid, 28, 11, PIX_TORCH1);      // palisade-gap torches, on the
    paint(w.grid, 32, 11, PIX_TORCH1);      // snow INSIDE the arcs
    paint(w.grid, 28, 49, PIX_TORCH1);
    paint(w.grid, 32, 49, PIX_TORCH1);

    // Team 0 — Thornby's own (all UNNAMED except the Reeve). Protection
    // recipe (the Westlands E4 pattern, simplified): no ghosts on this map,
    // so no scare-wail force-march threat — walls + two lvl-7 door wards +
    // the guard-held Reeve suffice. He is a cleric (a ledger-keeping
    // official, staff not sword) with specials_disabled so the AI never
    // walks him out the door chasing a heal; SAVE_ALL watches him from
    // tick 0 (bit2 is flagged centrally in save_level_files).
    place_hero(w, FAMILY_CLERIC, 0, 29, 28, 6, "The Reeve", true, true, 0);
    place_living(w, FAMILY_SOLDIER, 0, 0, 30, 31, 7, true); // door wards,
    place_living(w, FAMILY_SOLDIER, 0, 0, 31, 31, 7, true); // inside the door
    // (Design-doc deviation, F4 calibration: the militia hardens lvl 4 ->
    // 6 and every pack sheds two levels — the doc's packs left zero clears
    // at crew 7 AND 8; the hunt must end while the crew still stands.)
    static constexpr int militia[4][2] = {
        {30, 12}, {30, 48}, {10, 29}, {50, 29}}; // palisade gaps, road posts
    for (const auto& m : militia)
        place_living(w, FAMILY_SOLDIER, 0, 0, m[0], m[1], 6, true);

    // Team 2 — the packs (ORC = wolves, BIG_ORC = dire wolves, per
    // convention). Field roamers pull the crew out early...
    // (Design-doc deviation, one cell: the doc's seventh roamer at (46,44)
    // sits inside the SE house block (44,42)-(47,44) and would fail the
    // footing audit; he stands at (46,45), directly south of the house.)
    static constexpr int roamers[8][2] = {{8, 22},  {13, 25}, {10, 34},
                                          {16, 37}, {46, 24}, {52, 27},
                                          {46, 45}, {24, 40}};
    for (const auto& p : roamers)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 2);
    // ...then the dusk waves: north treeline, south treeline, down the west
    // road with dire leaders, and last the winter-king's den wave — the
    // final count on the NEXT WAVE HUD, and the kill-all's final errand.
    static constexpr int dusk1[6][2] = {{22, 8}, {26, 8}, {32, 8},
                                        {36, 8}, {24, 9}, {34, 9}};
    for (const auto& p : dusk1)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 2, false, false, 400);
    static constexpr int dusk2[6][2] = {{22, 51}, {26, 52}, {32, 52},
                                        {36, 51}, {24, 51}, {34, 51}};
    for (const auto& p : dusk2)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 3, false, false, 900);
    static constexpr int dusk3[5][2] = {
        {2, 26}, {2, 32}, {4, 28}, {4, 31}, {6, 29}};
    for (const auto& p : dusk3)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 3, false, false, 1500);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 3, 29, 3, false, false, 1500);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 5, 27, 3, false, false, 1500);
    place_living(w, FAMILY_BIG_ORC, 2, 0, 28, 5, 4, false, false, 2200);
    static constexpr int kings_pack[4][2] = {
        {26, 4}, {30, 4}, {26, 6}, {30, 6}};
    for (const auto& p : kings_pack)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 2, false, false, 2200);

    // Start markers (lead FIRST; on the green around the moot-house, 2x2
    // clearance, two tiles off the hall walls, clear of torches and wards).
    place_start(w, 0, 30, 35); // LEAD, on the road before the door
    static constexpr int starts[9][2] = {{26, 35}, {34, 35}, {25, 29},
                                         {36, 29}, {26, 24}, {34, 24},
                                         {30, 22}, {30, 38}, {36, 33}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The Reeve's strongbox: tithe coin, and a warm-coin sample he kept
    // back "for evidence". Village stores by the house blocks.
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 32, 27);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 33, 27);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 33, 28);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 16, 15);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 43, 43);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 24, 44);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 30, 24);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 28, 30);

    // The east road down to the ford; the west road back up to the Grey
    // Tolls (both on open snow adjoining the road, clear of the wave lanes).
    place_exit(w, 0, 57, 33, 16);
    place_exit(w, 0, 2, 35, 14);

    // Ambience: the wolves' winter so far (bones at the den, the field
    // kills, one by the pond), hedgerow stubs in the FIELDS ONLY (off the
    // green, off the road, off every wave lane), boulders on the west waste
    // edge, and well-worn village-road pebbles.
    static constexpr int bones[6][2] = {{27, 5},  {29, 6},  {10, 23},
                                        {47, 25}, {14, 35}, {50, 33}};
    for (const auto& b : bones)
        paint_decor(w, 0, b[0], b[1], DECOR_BONES);
    scatter_boulders(w, 0, 2, 12, 10, 48, 27);
    scatter_decor(w, 0, 0, 0, 59, 59, 11, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 6, 20, 16, 26, 7, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 44, 22, 54, 28, 7, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    save_level_files(w, 15, "Wolf Winter",
                     {"Ledger, deep winter. Thornby is",
                      "snowed in and the wolves have",
                      "the roads. Pay is the Reeve's",
                      "word: he knows whose ledger our",
                      "warm coin balances. Keep the",
                      "old man alive till he says it."},
                     5, 5000);
}

// 16 THE FROZEN FORD: the river below Thornby, frozen — barely. A CAN_EXIT
// run across three braided ice causeways (the mid lane already broken open
// where the pay chest sat overnight: warm coin melts its footing). The
// "collapse" is honestly authored: pre-broken ice forcing lane changes,
// bounded pursuit waves waking BEHIND the crew at 350/700/1000, and
// CAN_EXIT so the crew never has to clear the map. No generators (bounded
// pursuit — an endless den behind a run level just farms attrition), and
// GHOST specials_disabled EVERYWHERE: the scare-wail force-march over open
// water shoves walkers onto unstandable tiles — sheathed on all 10 ghosts;
// they fly and fight, they do not wail. Every wraith stands ON a snow lane
// (no water-locked flyers), so footing and reachability audit clean with an
// empty allowlist.
void build_frozen_ford(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(16, true, &hooks);
    init_world(level, 1, 90, 40);
    GameWorld& w = level.world();
    // Run, don't win: the exit works while foes remain. No SAVE_ALL — the
    // Reeve stays in Thornby; nothing on this map carries npc_flags bit2.
    w.type = SCEN_TYPE_CAN_EXIT;

    paint_snow(w.grid, 0, 0, 89, 39); // banks first
    paint_rect(w.grid, 29, 0, 63, 39, PIX_WATER1); // the river: impassable
                                                   // black water, bank to
                                                   // bank, edge to edge
    // Ice causeways: snow painted OVER the water reads as river ice.
    paint_snow(w.grid, 29, 8, 63, 11);  // NORTH lane (short, guarded)
    paint_snow(w.grid, 29, 19, 45, 22); // MID lane, BROKEN: west half,
    paint_snow(w.grid, 50, 19, 63, 22); // open lead x46..49, east half
    paint_snow(w.grid, 29, 30, 63, 33); // SOUTH lane (long, intact)
    paint_snow(w.grid, 44, 12, 45, 18); // connector N<->M
    paint_snow(w.grid, 52, 23, 53, 29); // connector M<->S
    paint_snow(w.grid, 46, 14, 49, 17); // the islet (a gravel bar)
    paint_snow(w.grid, 47, 12, 48, 13); // islet neck to the N lane
    paint_rect(w.grid, 2, 2, 8, 8, PIX_TREE_M1);   // west bank pines
    paint_rect(w.grid, 2, 31, 8, 37, PIX_TREE_M1);
    paint_rect(w.grid, 70, 0, 89, 6, PIX_WALL2);   // east bank bluffs: the
    paint_rect(w.grid, 70, 33, 89, 39, PIX_WALL2); // climb-out funnels to
                                                   // the middle road
    paint_rect(w.grid, 66, 2, 69, 5, PIX_TREE_M1); // east bank pines
    paint_rect(w.grid, 12, 12, 18, 17, PIX_GRASS_DARK_1); // bank scrub
    paint_rect(w.grid, 74, 24, 80, 29, PIX_GRASS_DARK_1);
    smooth_world(w);
    paint_path(w.grid, 4, 19, 28, 20);  // the ford road: west approach,
    paint_path(w.grid, 64, 19, 86, 20); // east climb-out
    paint(w.grid, 28, 18, PIX_TORCH1);  // waymarks at the west ice-edge...
    paint(w.grid, 28, 21, PIX_TORCH1);
    paint(w.grid, 64, 18, PIX_TORCH1);  // ...and the east ice-edge
    paint(w.grid, 64, 21, PIX_TORCH1);

    // Team 2 (team 0 places nothing — a run level stays light). Bank wolves
    // toll the lane mouths at the lane EDGES: each 4-wide lane keeps 2
    // clear tiles for a sprinting runner.
    static constexpr int wolves[6][2] = {{30, 8},  {30, 11}, {30, 19},
                                         {30, 22}, {30, 30}, {30, 33}};
    for (const auto& p : wolves)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 6, true);
    // Ice wraiths ON the lanes (guard; specials sheathed — see above).
    static constexpr int wraiths[4][2] = {
        {40, 9}, {38, 20}, {56, 21}, {44, 31}};
    for (const auto& p : wraiths)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 6, true, true);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 47, 15, 6, true); // islet wards
    place_living(w, FAMILY_BARBARIAN, 2, 0, 48, 16, 6, true);
    // The far shore's toll: the one fight a runner cannot fully skip — four
    // lvl-7 posts on the bank window the bluffs leave open (y7..32).
    static constexpr int far_toll[4][2] = {
        {66, 9}, {66, 30}, {67, 18}, {67, 21}};
    for (const auto& p : far_toll)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 7, true);
    // The pursuit, waking BEHIND the lead marker (the ice groaning behind):
    // the river dead at 350 and 700, the creditors' men at 1000.
    static constexpr int pursuit1[3][2] = {{2, 18}, {2, 21}, {3, 20}};
    for (const auto& p : pursuit1)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 6, false, true, 350);
    static constexpr int pursuit2[3][2] = {{1, 19}, {3, 17}, {3, 22}};
    for (const auto& p : pursuit2)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 7, false, true, 700);
    static constexpr int pursuit3[4][2] = {{2, 16}, {2, 23}, {4, 18}, {4, 21}};
    for (const auto& p : pursuit3)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 6, false, false,
                     1000);

    // Start markers (lead FIRST; west bank open snow, 2x2 clearance, ALL at
    // x >= 6, clear of the x1..4 pursuit cells).
    place_start(w, 0, 9, 20); // LEAD, on the ford road, facing the ice
    static constexpr int starts[9][2] = {{6, 16},  {6, 24},  {8, 16},
                                         {8, 24},  {11, 17}, {11, 23},
                                         {13, 20}, {6, 20},  {13, 24}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The islet cache (a sunken toll-cart's strongbox): warm coin again —
    // the mid-lane break sits beside it; the coin did that. Dropped
    // supplies on the lanes, and the "go NOW" prop at the west ice-edge.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 46, 14);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 49, 17);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 48, 14);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 36, 9);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 42, 20);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 50, 31);
    place(w, Order::Treasure, FAMILY_SPEED_POTION, 0, 0, 28, 20);

    // East bank: the road up to Ashfall Gate (usable while foes remain).
    // West bank: the Thornby road — declining the crossing is the withdraw
    // flow (clear of pursuit cells x1..4/y16..23 and of all markers).
    place_exit(w, 0, 86, 20, 17);
    place_exit(w, 0, 5, 27, 15);

    // Ambience: last year's crossing frozen INTO the ice, gravel on the
    // bank roads, shrub on the bank scrub patches ONLY (nothing concealing
    // on the ice; the lanes stay honest), and the west bank moraine (NO
    // litter/jagged anywhere: this is a chase map, nothing may block). No
    // decor of any kind on the connectors — they are the choke cells.
    static constexpr int bones[7][2] = {{35, 10}, {58, 9},  {33, 21},
                                        {55, 20}, {40, 32}, {60, 31},
                                        {47, 16}};
    for (const auto& b : bones)
        paint_decor(w, 0, b[0], b[1], DECOR_BONES);
    scatter_boulders(w, 0, 10, 2, 27, 37, 25);
    scatter_decor(w, 0, 0, 0, 89, 39, 9, DECOR_PEBBLES,
                  {ScatterGround::Path});
    scatter_decor(w, 0, 12, 12, 18, 17, 5, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    scatter_decor(w, 0, 74, 24, 80, 29, 5, DECOR_SHRUB,
                  {ScatterGround::DarkGrass});
    save_level_files(w, 16, "The Frozen Ford",
                     {"Ledger, thaw-that-lies. The",
                      "ford is ice and the ice is",
                      "going. The pay chest melts its",
                      "own footing; warm coin, warm",
                      "grief. Losses on the ice stay",
                      "there. Cross fast, count later."},
                     4, 3500);
}

} // namespace

void build_winter(const LevelDataHooks& hooks)
{
    build_long_toll(hooks);
    build_wolf_winter(hooks);
    build_frozen_ford(hooks);
}

std::vector<ExpectedLevel> winter_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {14, 1, "The Long Toll", 10, 6, 0, 0, 0, 35, 1, 27, 0, false, {15, 13}},
        {15, 1, "Wolf Winter", 10, 7, 0, 0, 0, 32, 0, 24, 1, false, {16, 14}},
        {16, 1, "The Frozen Ford", 10, 0, 0, 0, 0, 26, 0, 10, 10, false,
         {17, 15}},
    };
}

} // namespace longseason
