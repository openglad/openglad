/* War of the Westlands — CONVERGENCE & EPILOGUE (levels 24-26).
 *
 * Holds 24 The Mountain of Fire, the campaign finale: a three-floor volcano
 * cone rising out of an ash plain, the war host clashing with the dark host
 * below while the Bearer climbs to the summit crack. The epilogue levels
 * (25 The Scouring, 26 The Grey Ships) live in levels_act1.cpp alongside
 * the shared vale painters they reuse; this file registers only 24.
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

#include <cmath>

namespace westlands {
namespace {

// --- New-tile paint helpers. -------------------------------------------------
// Ash and lava are genre-inert to the autotiler, so — exactly like pavement —
// their two-variant texture is painted in AFTER smooth_world rather than
// smoothed in, with the same deterministic (x*7 + y*13) picker.
constexpr unsigned char kAshTiles[2] = {PIX_ASH1, PIX_ASH2};
constexpr unsigned char kLavaTiles[2] = {PIX_LAVA1, PIX_LAVA2};

void paint_variants(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                    const unsigned char (&variants)[2])
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

void paint_ash(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    paint_variants(g, tx0, ty0, tx1, ty1, kAshTiles);
}

void paint_lava(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    paint_variants(g, tx0, ty0, tx1, ty1, kLavaTiles);
}

// Annulus fills with the same pickers (paint_ring's shape rule: every tile
// whose center distance from (cx, cy) lies in [r0, r1)).
void ring_variants(PixieData& g, double cx, double cy, double r0, double r1,
                   const unsigned char (&variants)[2])
{
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d >= r0 && d < r1)
                paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
        }
}

void ring_ash(PixieData& g, double cx, double cy, double r0, double r1)
{
    ring_variants(g, cx, cy, r0, r1, kAshTiles);
}

void ring_lava(PixieData& g, double cx, double cy, double r0, double r1)
{
    ring_variants(g, cx, cy, r0, r1, kLavaTiles);
}

// 24 THE MOUNTAIN OF FIRE (THE FINALE): all roads end here. Floor 0 is the
// ash plain — lava rivers cut it into three bands and funnel the dark host
// into a center-band line-brawl — with the cone's only way in, the
// Undergate, carved into its west face. Floor 1 is the terrace ring around
// the mountain's shoulder, its circuit pinched by two lava falls. Floor 2
// is the summit rim over a caldera of fire, where the two exits — the vale
// or the sea — crack open on the west rim. CAN_EXIT + SAVE_ALL: the point
// is the climb, not extermination, and the Bearer must live. The
// Ranger-King's muster already holds the west plain (the briefing's "war
// host holds the plain" — without it the horde overruns the spawn wedge
// and the Bearer long before any relief); reinforcements arrive in delayed
// waves: the wild-men relief at tick 400 as the line buckles, the White
// Rider's at tick 900 to turn it.
void build_mountain_of_fire(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(24, true, &hooks);
    init_world(level, 3, 90, 50);
    GameWorld& w = level.world();
    constexpr double cx = 66.5, cy = 24.5; // the cone's axis

    // Floor 0 before smoothing: the cone is a solid rock disc; the Undergate
    // cleft and chamber are carved as grass placeholders so the rock face
    // smooths against them (both are re-ashed below).
    paint_ring(w.grid, cx, cy, 0.0, 17.5, PIX_WALL2);
    paint_rect(w.grid, 49, 22, 53, 27, PIX_GRASS1); // the cleft
    paint_rect(w.grid, 53, 20, 60, 29, PIX_GRASS1); // the Undergate chamber
    // Floor 1: open sky — whatever steps off the ledge falls to the plain —
    // around the cone at shoulder height. Floor 2: the summit cap.
    paint_rect(w.grid_for_floor(1), 0, 0, 89, 49, PIX_AIR);
    paint_ring(w.grid_for_floor(1), cx, cy, 0.0, 12.5, PIX_WALL2);
    paint_rect(w.grid_for_floor(2), 0, 0, 89, 49, PIX_AIR);
    paint_ring(w.grid_for_floor(2), cx, cy, 0.0, 8.5, PIX_WALL2);
    smooth_world(w);

    // Floor 0 after smoothing: ash everywhere the plain shows, and through
    // the carved Undergate.
    paint_ash(w.grid, 0, 0, 48, 49);
    paint_ash(w.grid, 49, 22, 53, 27);
    paint_ash(w.grid, 53, 20, 60, 29);
    // The Bearer's cleft: a dogleg burrow in the west rim, painted AFTER the
    // smoothing pass on purpose — the autotiler shapes free-standing walls
    // into wallside faces, and wallside tiles are flyer-passable (the ghost
    // outriders would sail straight in over them); raw rock blocks every
    // mover AND every rock-thrower's shot. The mouth opens at (3, 23),
    // turns at (2, 23), and runs one tile wide down row 24 to the Bearer at
    // (0, 24): the lane cap at (3, 24) means nothing outside ever has line
    // of sight on the cargo, and the only melee cell against him is
    // (1, 24), at the end of the warded corridor.
    paint_rect(w.grid, 0, 22, 2, 22, PIX_WALL2); // north wall
    paint_rect(w.grid, 0, 23, 1, 23, PIX_WALL2); // the dogleg's inner corner
    paint(w.grid, 3, 24, PIX_WALL2);             // the row-24 lane cap
    paint_rect(w.grid, 0, 25, 3, 25, PIX_WALL2); // south wall
    // The lava rivers cut the plain into three bands; four three-wide
    // causeways are the only ground crossings.
    paint_lava(w.grid, 0, 10, 54, 12);  // north river
    paint_lava(w.grid, 0, 37, 54, 39);  // south river
    paint_ash(w.grid, 14, 10, 16, 12);  // its causeways
    paint_ash(w.grid, 38, 10, 40, 12);
    paint_ash(w.grid, 20, 37, 22, 39);  // south causeways
    paint_ash(w.grid, 44, 37, 46, 39);
    // The pinch spurs squeeze the Undergate approach into a six-wide throat.
    paint_lava(w.grid, 43, 19, 48, 21);
    paint_lava(w.grid, 43, 28, 48, 30);
    paint(w.grid, 53, 20, PIX_BRAZIER1); // braziers at the chamber corners
    paint(w.grid, 60, 20, PIX_BRAZIER1); // (the walk to the stair, y 22..27,
    paint(w.grid, 53, 29, PIX_BRAZIER1); // stays clear)
    paint(w.grid, 60, 29, PIX_BRAZIER1);

    // Floor 1 after smoothing: carve the terrace — a four-wide walkable
    // ledge around a solid rock core — and the summit-stair landing notch
    // into the core (it meets the terrace at x 76). The lava falls cover the
    // outer rows of the ring, leaving the two- to three-wide inner lips that
    // pinch the circuit.
    ring_ash(w.grid_for_floor(1), cx, cy, 8.5, 12.5);
    paint_ash(w.grid_for_floor(1), 72, 23, 76, 26);
    paint_lava(w.grid_for_floor(1), 65, 12, 68, 13); // north fall (lip y 14..16)
    paint_lava(w.grid_for_floor(1), 65, 36, 68, 37); // south fall (lip y 33..35)
    paint(w.grid_for_floor(1), 58, 17, PIX_BRAZIER1); // watch fires
    paint(w.grid_for_floor(1), 58, 31, PIX_BRAZIER1);

    // Floor 2 after smoothing: the three-wide rim walk around a lake of
    // fire. No decor here — the rim stays clean for the final fight.
    ring_ash(w.grid_for_floor(2), cx, cy, 5.5, 8.5);
    ring_lava(w.grid_for_floor(2), cx, cy, 0.0, 5.5);

    // Stairs last, so no carve or decor overwrites them: the Undergate
    // stair inside the chamber, the summit stair inside the notch.
    stair_pair(w, 0, 55, 24);
    stair_pair(w, 1, 74, 24);

    // The dark host (team 2) on the plain: orc ranks in the center band,
    // big-orc wings, skeletons massed before the throat, ghost outriders.
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 6; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 26 + col * 3, 14 + row * 5,
                         3 + ((row + col) % 3));
    static constexpr int wings[8][2] = {{22, 15}, {22, 33}, {26, 13}, {26, 35},
                                        {34, 13}, {34, 35}, {38, 15}, {38, 33}};
    for (int i = 0; i < 8; ++i)
        place_living(w, FAMILY_BIG_ORC, 2, 0, wings[i][0], wings[i][1],
                     5 + (i % 2));
    for (int i = 0; i < 10; ++i)
        place_living(w, FAMILY_SKELETON, 2, 0, 42 + (i % 5),
                     (i < 5) ? 23 : 26, 4 + (i % 3));
    static constexpr int outriders[6][2] = {{15, 5},  {35, 5},  {15, 44},
                                            {35, 44}, {14, 20}, {14, 29}};
    for (int i = 0; i < 6; ++i)
    {
        // The two center-band outriders ride with the tick-400 push: at
        // tick 0 they would start fights at the crew's wedge (and pull the
        // cleft wards off their posts) long before any relief exists.
        const int delay = (i >= 4) ? 400 : 0;
        place_living(w, FAMILY_GHOST, 2, 0, outriders[i][0], outriders[i][1],
                     6 + (i % 2), false, false, delay);
    }
    // The cone guards hold their posts (ACT_GUARD) so the climb stays fresh
    // for the endgame. The golem wards bar the cleft — a golem stands three
    // tiles square, so the pair anchors at y 22 and 25 to fill the carved
    // cleft (y 22..27) exactly, wall to wall.
    place_living(w, FAMILY_GOLEM, 2, 0, 50, 22, 7, true);
    place_living(w, FAMILY_GOLEM, 2, 0, 50, 25, 7, true);
    static constexpr int chamber_guards[4][2] = {
        {54, 21}, {54, 28}, {58, 21}, {58, 28}};
    for (const auto& c : chamber_guards)
        place_living(w, FAMILY_ORC, 2, 0, c[0], c[1], 6, true);
    place_living(w, FAMILY_BIG_ORC, 2, 1, 57, 24, 8, true); // stair-down ward
    static constexpr int terrace_watch[4][2] = {
        {60, 16}, {60, 33}, {73, 16}, {73, 33}};
    for (const auto& t : terrace_watch)
        place_living(w, FAMILY_SKELETON, 2, 1, t[0], t[1], 6, true);
    // East-terrace mages beneath their tower (the tower sprite stands four
    // tiles square from (75, 18), so the north mage posts just south of it).
    place_living(w, FAMILY_MAGE, 2, 1, 77, 22, 6, true);
    place_living(w, FAMILY_MAGE, 2, 1, 77, 29, 6, true);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 1, 66, 15, 8, true); // fall lips
    place_living(w, FAMILY_FIREELEMENTAL, 2, 1, 66, 34, 8, true);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 2, 71, 21, 9, true); // the rim
    place_living(w, FAMILY_FIREELEMENTAL, 2, 2, 71, 28, 9, true);
    place_living(w, FAMILY_BIG_ORC, 2, 2, 74, 21, 10, true); // the summit guard
    // Ghosts hovering OVER the caldera lava — flyers, legal footing.
    place_living(w, FAMILY_GHOST, 2, 2, 63, 22, 8);
    place_living(w, FAMILY_GHOST, 2, 2, 63, 27, 8);
    // The host's camps: 96 seeded livings + generator headroom stays well
    // under the MAXOBS living cap. (The bone-pile sprite spreads four tiles
    // wide, so it anchors at x 42, clear of the easternmost orc rank.)
    place_generator(w, FAMILY_TENT, 2, 0, 40, 16, 5);
    place_generator(w, FAMILY_BONES, 2, 0, 42, 32, 5);
    place_generator(w, FAMILY_TOWER, 2, 1, 75, 18, 4);

    // The war host (team 0): the Ranger-King's muster already holds the
    // north flank of the wedge — the horde reaches the west plain around
    // tick 120, so the line must exist from tick 0 or the crew and the
    // Bearer are overrun before any relief. Reinforcements arrive in
    // waves: the wild-men relief at tick 400 as the line buckles, the
    // White Rider at tick 900 to turn it. ("White Wizard" is 12 chars and
    // overflows the name field; campaign-wide he is the White Rider.)
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_SOLDIER, 0, 0, 5 + i * 3, 13, 5);
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_ELF, 0, 0, 5 + i * 6, 15, 5);
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_BARBARIAN, 0, 0, 5 + i * 3, 34, 6, false, false,
                     400);
    place_living(w, FAMILY_CLERIC, 0, 0, 5, 32, 5, false, false, 400);
    place_living(w, FAMILY_CLERIC, 0, 0, 11, 32, 5, false, false, 400);
    // The Bearer holds the back of his cleft (ACT_GUARD, so team-0 AI never
    // marches him into the horde; the rock keeps the ghost outriders off
    // his back and the dogleg keeps every thrown rock out); SAVE_ALL rides
    // on him. Mechanically the player does the climb — AI cannot use
    // exits. The King's door-wards body-block the mouth and the turn in
    // series — the mouth, the turn, and the gate cell itself — so anything
    // that wants the cargo kills all three, strictly one at a time.
    // (Deliberately unnamed — on SAVE_ALL maps a named team-0 death ends
    // the mission.)
    place_hero(w, FAMILY_THIEF, 0, 0, 24, 5, "The Bearer", true, false, 0);
    place_living(w, FAMILY_SOLDIER, 0, 0, 3, 23, 9, true); // the mouth
    place_living(w, FAMILY_SOLDIER, 0, 0, 2, 23, 9, true); // the turn
    place_living(w, FAMILY_SOLDIER, 0, 0, 1, 24, 9, true); // the gate
    place_hero(w, FAMILY_SOLDIER, 0, 2, 13, 9, "Ranger-King", false, false, 0);
    place_hero(w, FAMILY_ARCHMAGE, 0, 1, 35, 10, "White Rider", false, false,
               900);

    // The player's crew: a wedge on the west plain, point east, the lead
    // marker at the banner.
    place_start(w, 0, 9, 24);
    place_start(w, 0, 7, 21);
    place_start(w, 0, 7, 27);
    place_start(w, 0, 5, 18);
    place_start(w, 0, 5, 24);
    place_start(w, 0, 5, 30);
    place_start(w, 0, 3, 15);
    place_start(w, 0, 3, 21);
    place_start(w, 0, 3, 27);
    place_start(w, 0, 3, 33);
    place_start(w, 0, 7, 17);
    place_start(w, 0, 7, 31);
    place_start(w, 0, 9, 20);
    place_start(w, 0, 9, 28);

    // Small treasure — the finale rewards the climb, not looting: gold in
    // the Undergate chamber, one old draught in the landing notch.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 57, 22);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 59, 24);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 57, 27);
    place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 1, 73, 25);

    // The summit crack: two exits on the west rim, nine tiles apart around
    // the ring — a real choice at the top. The north crack goes home by the
    // vale (The Scouring); the south goes straight to the sea (The Grey
    // Ships). Deliberately NO backtrack exit: with CAN_EXIT_WHENEVER a
    // west-edge exit would insta-finish the finale from the spawn wedge.
    place_exit(w, 2, 62, 20, 25);
    place_exit(w, 2, 62, 29, 26);

    scatter_boulders(w, 0, 0, 0, 48, 9, 21);   // the stony north strip
    scatter_boulders(w, 0, 0, 40, 48, 49, 21); // and south strip
    scatter_boulders(w, 0, 0, 13, 20, 36, 33); // sparse west-margin rocks
    scatter_litter(w, 0, 42, 22, 48, 27, 17);  // old bones in the throat
    scatter_boulders(w, 1, 60, 14, 78, 35, 31); // scree on the shoulder

    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT | SCEN_TYPE_SAVE_ALL);
    save_level_files(w, 24, "The Mountain of Fire",
                     {"All roads end at the mountain.",
                      "The war host holds the plain;",
                      "the Bearer climbs to the crack.",
                      "Guard each stair. At the summit,",
                      "choose: the vale, or the sea."},
                     9, 7000);
}

} // namespace

void build_finale(const LevelDataHooks& hooks)
{
    build_mountain_of_fire(hooks);
}

std::vector<ExpectedLevel> finale_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {24, 3, "The Mountain of Fire", 14, 22, 0, 0, 0, 74, 3, 9, 0, true,
         {25, 26}},
    };
}

} // namespace westlands
