/* War of the Westlands — Act II, THE DARK ROAD (levels 5-12).
 *
 * 5 The High Pass, 9 The Lost Delve, 10 The Golden Wood, 11 The Great
 * River, and 12 The Falls (the great branch) are built here per their
 * design files; 6 Under the Mountain, 7 The Frozen Wall (optional;
 * rejoins the road at 5), and 8 The Bridge of Shadow are the three war
 * stories moved from the Concept Playground (concept 608/610/607,
 * renumbered and adapted per the campaign design).
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

// Deterministic snowfield pattern, mirroring paint_pavement: the SNOW tiles
// are genre-inert to the autotiler, so the drift variety must be painted in
// rather than smoothed in.
void paint_snow(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[2] = {PIX_SNOW1, PIX_SNOW2};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 2]);
}

// 6 UNDER THE MOUNTAIN: a descent — columned entry hall (floor 2), the
// great gallery with its carpeted nave and twin colonnades (floor 1), and the
// torchlit treasure vault (floor 0) where the wardens (team 2) and their
// sleeping keeper await the player's crew. The gate shut behind the company:
// the vault is the BRANCH point — the bridge (8) or deeper still (9).
void build_under_the_mountain(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(6, true, &hooks);
    init_world(level, 3, 60, 60);
    GameWorld& w = level.world();
    // The descent is a PASSAGE, not an extermination: the doors out of the
    // vault open whenever you reach them (the wardens' 5000+ hitpoints are
    // the price of the hoard, not of the road on). Same ratified pattern
    // as the Burden's Road levels, without SAVE_ALL.
    w.type = static_cast<char>(SCEN_TYPE_CAN_EXIT);

    // Floor 0: dark cavern (moss in the corners) with the walled vault;
    // carpet marks the hoard.
    paint_rect(w.grid, 0, 0, 59, 59, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 4, 2, 14, 8, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 38, 4, 50, 10, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 2, 20, 57, 57, PIX_WALL2);
    paint_pavement(w.grid, 3, 21, 56, 56);               // vault interior
    paint_rect(w.grid, 18, 30, 41, 47, PIX_CARPET_M);    // the hoard carpet
    // Floor 1: the great gallery, its nave carpeted end to end. Floor 2: the
    // entry hall.
    paint_rect(w.grid_for_floor(1), 0, 0, 59, 59, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 4, 4, 55, 44, PIX_WALL2);
    paint_pavement(w.grid_for_floor(1), 5, 5, 54, 43);   // gallery interior
    paint_rect(w.grid_for_floor(1), 8, 21, 51, 27, PIX_CARPET_M); // the nave
    paint_rect(w.grid_for_floor(2), 0, 0, 59, 59, PIX_AIR);
    paint_rect(w.grid_for_floor(2), 4, 4, 55, 18, PIX_WALL2);
    paint_pavement(w.grid_for_floor(2), 5, 5, 54, 17);   // hall interior
    paint_rect(w.grid_for_floor(2), 6, 10, 50, 12, PIX_CARPET_M); // runner
    smooth_world(w);
    paint(w.grid, 17, 29, PIX_BRAZIER1); // braziers frame the hoard
    paint(w.grid, 42, 29, PIX_BRAZIER1);
    paint(w.grid, 17, 48, PIX_BRAZIER1);
    paint(w.grid, 42, 48, PIX_BRAZIER1);
    for (int px = 6; px <= 54; px += 8)  // torches down the vault's north wall
        paint(w.grid, px, 21, PIX_TORCH1);
    // The twin colonnades flanking the gallery nave, and corner braziers.
    for (int px = 12; px <= 48; px += 6)
    {
        paint(w.grid_for_floor(1), px, 19,
              (px % 12 == 0) ? PIX_COLUMN1 : PIX_COLUMN2);
        paint(w.grid_for_floor(1), px, 29,
              (px % 12 == 0) ? PIX_COLUMN2 : PIX_COLUMN1);
    }
    paint(w.grid_for_floor(1), 6, 6, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 53, 6, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 6, 42, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 53, 42, PIX_BRAZIER1);
    for (int px = 20; px <= 44; px += 6) // hall columns march to the stair
    {
        paint(w.grid_for_floor(2), px, 9, PIX_COLUMN1);
        paint(w.grid_for_floor(2), px, 13, PIX_COLUMN2);
    }
    stair_pair(w, 1, 52, 11); // hall's far end, down into the gallery
    stair_pair(w, 0, 7, 41);  // across the gallery, down into the vault

    // The wardens (team 2): guards ringing the hoard, its keeper at center.
    // Lvl 4-6 stone and bone: a wall an entry-power lvl-4 crew can chip
    // through, not an unkillable equilibrium (guards roam — the whole vault
    // eventually walks up the stairs to meet the crew).
    static constexpr int golems[4][2] = {{19, 31}, {37, 31}, {19, 43},
                                         {37, 43}};
    for (const auto& g : golems)
        place_living(w, FAMILY_GOLEM, 2, 0, g[0], g[1], 4, true);
    // The drum below: the giant dead and the keeper are the DEEP vault —
    // they stir at 1500/1200, long after the descent starts, so the whole
    // elite doesn't march up the stairs into the entry-hall fight (guards
    // roam; only a delay actually holds the deep watch down).
    static constexpr int giants[3][2] = {{24, 34}, {32, 34}, {24, 40}};
    for (const auto& g : giants)
        place_living(w, FAMILY_GIANT_SKELETON, 2, 0, g[0], g[1], 5, true,
                     false, 1500);
    static constexpr int gallery_skeletons[6][2] = {{9, 9},  {17, 13},
                                                    {25, 9}, {33, 13},
                                                    {41, 9}, {49, 13}};
    for (const auto& s : gallery_skeletons)
        place_living(w, FAMILY_SKELETON, 2, 1, s[0], s[1], 2);
    place_generator(w, FAMILY_BONES, 2, 0, 27, 52, 2);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 0, 29, 38, 10, true, false,
                 1200); // the keeper

    // The player's crew assembles in the entry hall: cleric lead at the
    // front line, the barbarian file, elf pairs, thieves in the rear.
    place_start(w, 2, 16, 9);
    place_start(w, 2, 16, 13);
    for (int i = 0; i < 6; ++i)
        place_start(w, 2, 14, 6 + i * 2);
    for (int i = 0; i < 4; ++i)
    {
        place_start(w, 2, 10, 7 + i * 2);
        place_start(w, 2, 12, 7 + i * 2);
    }
    for (int i = 0; i < 6; ++i)
    {
        place_start(w, 2, 6, 6 + i * 2);
        place_start(w, 2, 8, 6 + i * 2);
    }
    // The Bearer travels with the company: on the hall's carpet runner just
    // east of the marker block, a lvl-5 team-0 ally hardened by the road
    // (this is NOT one of his SAVE_ALL protect levels — his loss does not
    // fail the mission here). He catches up at tick 600: the fastest
    // team-0 walker must not lead the charge into the gallery watch
    // climbing the far stair.
    place_hero(w, FAMILY_THIEF, 2, 18, 11, 5, "The Bearer", false, false, 600);

    // The hoard itself, denser than ever: gold, silver, provisions, and four
    // old potions at the corners. Cells under a warden's feet stay bare.
    for (int gy = 32; gy <= 44; gy += 2)
        for (int gx = 20; gx <= 38; gx += 2)
        {
            if (cell_near_entity(w, 0, gx, gy, 0))
                continue;
            const int fam = (((gx + gy) / 2) % 3 == 2) ? FAMILY_SILVER_BAR
                                                       : FAMILY_GOLD_BAR;
            place(w, Order::Treasure, fam, 0, 0, gx, gy);
        }
    for (int i = 0; i < 6; ++i)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 21 + i * 3, 46);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 33);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 33);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 19, 45);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 45);

    // The BRANCH: the heart of the hoard leads on to the Bridge of Shadow;
    // a stair mouth in the vault's south-east corner goes DEEPER (the Lost
    // Delve, optional). No backtrack exit — the gate shut behind us.
    place_exit(w, 0, 30, 39, 8);
    place_exit(w, 0, 48, 54, 9);
    save_level_files(w, 6, "Under the Mountain",
                     {"The gate shut behind us.",
                      "Dark, and a long stair down.",
                      "Far down, a drum begins.",
                      "The hoard sleeps below. So",
                      "does its keeper. Choose your",
                      "door: the bridge, or deeper."},
                     3, 4000);
}

// 7 THE FROZEN WALL (OPTIONAL — the northern plea, reached from 4; the exit
// rejoins the main road at 5 The High Pass): the wall IS the arena — floor 0
// at its base (wild north, farm villages south), floor 1 the brazier-lit
// interior galleries, floor 2 the open-sky top ribbon. The player's crew
// mans the Watch posts; the Wild (team 2) climbs from the north via the end
// stairs. Reward-flavored: the Watch's pay chest and stores are theirs.
void build_frozen_wall(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(7, true, &hooks);
    init_world(level, 3, 80, 45);
    GameWorld& w = level.world();

    // Floor 0: the wall body with end chambers and a center gate; the wild
    // north (trees, tundra, the frozen mere), farms and hearths south.
    paint_rect(w.grid, 0, 18, 79, 23, PIX_WALL2);
    paint_rect(w.grid, 8, 2, 20, 7, PIX_TREE_M1); // beyond-the-wall woods
    paint_rect(w.grid, 24, 5, 32, 10, PIX_TREE_M1);
    paint_rect(w.grid, 52, 2, 62, 7, PIX_TREE_M1);
    paint_rect(w.grid, 64, 2, 74, 6, PIX_TREE_M1);
    paint_rect(w.grid, 36, 3, 48, 9, PIX_WATER1); // the frozen mere
    paint_rect(w.grid, 2, 2, 7, 5, PIX_GRASS_DARK_1); // tundra scrub
    paint_rect(w.grid, 44, 11, 50, 14, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 66, 8, 76, 12, PIX_GRASS_DARK_1);
    // South of the wall: two farmsteads and their strip fields.
    paint_rect(w.grid, 18, 30, 22, 33, PIX_WALL2);       // west hut
    paint_rect(w.grid, 56, 29, 60, 32, PIX_WALL2);       // east hut
    paint_rect(w.grid, 8, 36, 15, 41, PIX_GRASS_LIGHT_1); // strip fields
    paint_rect(w.grid, 25, 35, 31, 40, PIX_GRASS_LIGHT_1);
    paint_rect(w.grid, 63, 34, 71, 40, PIX_GRASS_LIGHT_1);
    // Floor 1: interior galleries inside the wall footprint.
    paint_rect(w.grid_for_floor(1), 0, 0, 79, 44, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 0, 18, 79, 23, PIX_WALL2);
    // Floor 2: the top ribbon, open sky on every side.
    paint_rect(w.grid_for_floor(2), 0, 0, 79, 44, PIX_AIR);
    smooth_world(w);
    paint_pavement(w.grid, 0, 18, 3, 22);   // west end chamber (opens north;
    paint_pavement(w.grid, 76, 18, 79, 22); // east end chamber — climbers go UP)
    paint_pavement(w.grid, 38, 18, 41, 23); // the gate
    paint_path(w.grid, 39, 10, 40, 17);     // trails worn to the gate,
    paint_path(w.grid, 39, 24, 40, 31);     // north and south
    paint_pavement(w.grid, 19, 31, 21, 32); // hut hearth floors + doors
    paint_pavement(w.grid, 20, 33, 20, 33);
    paint_pavement(w.grid, 57, 30, 59, 31);
    paint_pavement(w.grid, 58, 32, 58, 32);
    paint_path(w.grid, 20, 34, 38, 34);     // village lanes, door to road
    paint_path(w.grid, 41, 33, 58, 33);
    paint_pavement(w.grid_for_floor(1), 1, 19, 78, 22); // gallery corridors
    for (int px = 10; px <= 70; px += 10) // partitions with door gaps
    {
        paint(w.grid_for_floor(1), px, 19, PIX_WALL2);
        paint(w.grid_for_floor(1), px, 22, PIX_WALL2);
    }
    static constexpr int gallery_braziers[6][2] = {
        {8, 19}, {18, 22}, {28, 19}, {38, 22}, {58, 19}, {68, 22}};
    for (const auto& b : gallery_braziers) // hearth-light room to room
        paint(w.grid_for_floor(1), b[0], b[1], PIX_BRAZIER1);
    paint_rect(w.grid_for_floor(1), 33, 20, 37, 21, PIX_CARPET_M); // barracks rug
    paint_pavement(w.grid_for_floor(2), 0, 18, 79, 23);
    for (int px = 0; px <= 79; ++px) // stone/glass ribbon under open sky
        if (px % 8 == 3 || px % 8 == 4)
            for (int py = 18; py <= 23; ++py)
                paint(w.grid_for_floor(2), px, py, PIX_GLASS);
    static constexpr int top_braziers[5] = {13, 23, 33, 53, 63};
    for (const int px : top_braziers) // fire posts between the watch posts
        paint(w.grid_for_floor(2), px, 18, PIX_BRAZIER1);
    stair_pair(w, 0, 1, 20);  // climber stairs at both wall ends: 0 -> 1
    stair_pair(w, 0, 78, 20);
    stair_pair(w, 1, 4, 20);  // and up again: 1 -> 2
    stair_pair(w, 1, 75, 20);

    // The Watch is yours: the champion's door lead in the gallery, the
    // patrol rooms, then every post along the wind-scoured top.
    place_start(w, 1, 48, 20);
    place_start(w, 1, 15, 20); // gallery patrol posts
    place_start(w, 1, 35, 21);
    place_start(w, 1, 55, 20);
    place_start(w, 1, 65, 21);
    static constexpr int watch_posts[14] = {6,  11, 16, 21, 26, 31, 36,
                                            43, 48, 53, 58, 63, 68, 73};
    for (int i = 0; i < 14; ++i)
        place_start(w, 2, watch_posts[i], 20 + (i % 2));
    static constexpr int elf_posts[6] = {9, 19, 29, 45, 55, 65};
    for (int i = 0; i < 6; ++i)
        place_start(w, 2, elf_posts[i], 19);
    // The gallery tower musters mages for the Watch.
    place_generator(w, FAMILY_TOWER, 0, 1, 43, 19, 4);

    // The Wild (team 2): war bands massing at both end stairs, ghosts over
    // the mere, the giant at the gate, camps in the northern woods.
    for (int i = 0; i < 9; ++i)
    {
        place_living(w, FAMILY_BARBARIAN, 2, 0, 2 + (i % 3) * 3, 8 + (i / 3) * 3,
                     2 + (i % 3));
        place_living(w, FAMILY_BARBARIAN, 2, 0, 71 + (i % 3) * 3, 8 + (i / 3) * 3,
                     2 + (i % 3));
    }
    static constexpr int ghosts[10][2] = {{26, 15}, {34, 15}, {46, 15},
                                          {54, 15}, {30, 12}, {50, 12},
                                          {38, 14}, {42, 14}, {22, 12},
                                          {58, 12}};
    for (const auto& g : ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, g[0], g[1], 3);
    static constexpr int skels[8][2] = {{12, 15}, {18, 15}, {62, 15}, {68, 15},
                                        {20, 13}, {65, 12}, {12, 9},  {64, 9}};
    for (const auto& s : skels)
        place_living(w, FAMILY_SKELETON, 2, 0, s[0], s[1], 2);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 38, 15, 9); // the giant at
    place_generator(w, FAMILY_BONES, 2, 0, 14, 10, 5);       // the gate
    place_generator(w, FAMILY_TENT, 2, 0, 57, 10, 5);

    // The Watch's thanks (all behind the defense, not in the Wild's path):
    // the pay chest on the barracks rug, and stores in the end chambers.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 33, 20);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 35, 20);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 37, 20);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 34, 21);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 1, 36, 21);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 33, 21);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 1, 19);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 2, 21);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 77, 19);
    place(w, Order::Treasure, FAMILY_SILVER_BAR, 0, 0, 78, 21);

    // The top of the Wall sends the company back south to the High Pass.
    place_exit(w, 2, 40, 21, 5);
    scatter_boulders(w, 0, 0, 0, 79, 16, 19); // the stony wild north
    save_level_files(w, 7, "The Frozen Wall",
                     {"A raven from the north: the",
                      "Watch is failing on the Wall.",
                      "This is not your war, yet no",
                      "road is safe while it falls.",
                      "Hold the Wall through the",
                      "night, and go with our thanks."},
                     3, 4000);
}

// 8 THE BRIDGE OF SHADOW: a narrow stone causeway spans a sea of air on
// floor 1; whatever falls lands in the bone-strewn cavern on floor 0. The
// player's crew holds the west; the dead (team 2) pour from the east, and
// the Grey Wizard stands alone at mid-span. He is FATED to fall here —
// not SAVE_ALL — and returns as the White Rider at 15 and the finale.
void build_bridge_of_shadow(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(8, true, &hooks);
    init_world(level, 2, 70, 40);
    GameWorld& w = level.world();

    // Floor 0: the dark cavern — moss beds glowing in the black, and two
    // chasms that even the light abandons. Floor 1: air, two landings, the
    // causeway, and the crumbling side-spans of the older, greater bridge.
    paint_rect(w.grid, 0, 0, 69, 39, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 10, 22, 16, 27, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 30, 12, 36, 16, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 45, 25, 52, 30, PIX_GRASS_DARK_1);
    paint_rect(w.grid_for_floor(1), 0, 0, 69, 39, PIX_AIR);
    smooth_world(w);
    paint_rect(w.grid, 24, 2, 29, 5, PIX_VOID1);   // the chasms
    paint_rect(w.grid, 55, 34, 61, 38, PIX_VOID1);
    paint_pavement(w.grid_for_floor(1), 2, 15, 8, 24);   // west landing
    paint_pavement(w.grid_for_floor(1), 61, 15, 67, 24); // east landing
    paint_pavement(w.grid_for_floor(1), 9, 19, 60, 21);  // the causeway
    // Crumbling side-spans: dead-end spurs bitten by air, their broken lips
    // shored with directional drop-blocks.
    paint_pavement(w.grid_for_floor(1), 20, 12, 22, 18); // north spur
    paint(w.grid_for_floor(1), 20, 12, PIX_AIR);
    paint(w.grid_for_floor(1), 22, 14, PIX_AIR);
    paint(w.grid_for_floor(1), 21, 12, PIX_DROPBLOCK_UP);
    paint_pavement(w.grid_for_floor(1), 47, 22, 49, 30); // south spur
    paint(w.grid_for_floor(1), 49, 30, PIX_AIR);
    paint(w.grid_for_floor(1), 47, 27, PIX_AIR);
    paint(w.grid_for_floor(1), 48, 30, PIX_DROPBLOCK_DOWN);
    stair_pair(w, 0, 4, 20);  // stairs down from each landing
    stair_pair(w, 0, 65, 20);

    // The player's crew holds the west: the shield-line lead on the span,
    // the column behind it, pickets on the west landing.
    place_start(w, 1, 33, 19);
    place_start(w, 1, 33, 21);
    place_start(w, 1, 31, 19);
    place_start(w, 1, 31, 21);
    for (int i = 0; i < 5; ++i)
    {
        place_start(w, 1, 12 + i * 4, 19);
        place_start(w, 1, 12 + i * 4, 21);
    }
    place_start(w, 1, 3, 17);
    place_start(w, 1, 3, 23);
    place_start(w, 1, 5, 20);
    // He shall not pass: the Grey Wizard guards mid-span on his own two
    // feet — specials disabled, he cannot teleport away.
    place_hero(w, FAMILY_MAGE, 1, 35, 20, 9, "Grey Wizard", true, true, 0);

    // The pursuers (team 2): skeletons and ghosts from the east, the shadow.
    for (int i = 0; i < 8; ++i)
    {
        place_living(w, FAMILY_SKELETON, 2, 1, 45 + i * 2, 19, 2 + (i % 3));
        place_living(w, FAMILY_SKELETON, 2, 1, 45 + i * 2, 21, 2 + (i % 3));
    }
    for (int i = 0; i < 4; ++i)
        place_living(w, FAMILY_SKELETON, 2, 1, 52 + i * 2, 20, 3);
    static constexpr int ghost_xs[4] = {48, 52, 56, 60};
    for (int i = 0; i < 4; ++i)
    {
        place_living(w, FAMILY_GHOST, 2, 1, ghost_xs[i], 17, 3); // flying
        place_living(w, FAMILY_GHOST, 2, 1, ghost_xs[i], 23, 3); // beside it
    }
    place_generator(w, FAMILY_BONES, 2, 1, 62, 16, 6);
    place_living(w, FAMILY_FIREELEMENTAL, 2, 1, 38, 20, 10); // THE SHADOW

    // Floor 0: slimes wandering among the fallen.
    place_living(w, FAMILY_SLIME, 2, 0, 15, 10, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 40, 8, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 25, 30, 2);
    place_living(w, FAMILY_SLIME, 2, 0, 51, 32, 2);

    // The east landing leads out of the dark, on to the Golden Wood.
    place_exit(w, 1, 66, 23, 10);
    scatter_boulders(w, 0, 0, 0, 69, 39, 19); // the fallen, among boulders
    scatter_litter(w, 0, 0, 0, 69, 39, 26);   // and the bones of the rest
    save_level_files(w, 8, "The Bridge of Shadow",
                     {"Out of the deep it came, a",
                      "shadow wreathed in flame.",
                      "The Grey Wizard bars the span",
                      "alone. His last word: run.",
                      "Win the far stair. Grieve",
                      "later."},
                     3, 4000);
}

// 5 THE HIGH PASS: a blizzard mountainside climbed by three switchback
// terraces — three east-west cliff bands cross the map, each with a gap at
// the opposite end from the previous one, forcing the S-shaped climb. The
// summit road is buried; the only way on is the mine gate in the head wall
// (exit to 6). Wolf packs and wild men hold each terrace, the chief's band
// waits before the gate, and the hunt (with Pale Riders rising behind it)
// presses up from the valley so the crew cannot turtle at the start.
void build_high_pass(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(5, true, &hooks);
    init_world(level, 1, 60, 60);
    GameWorld& w = level.world();

    paint_snow(w.grid, 0, 0, 59, 59); // ~90% snow: the blizzard fires
    paint_rect(w.grid, 0, 44, 49, 46, PIX_WALL2);    // cliff band A, gap east
    paint_rect(w.grid, 10, 30, 59, 32, PIX_WALL2);   // cliff band B, gap west
    paint_rect(w.grid, 0, 16, 49, 18, PIX_WALL2);    // cliff band C, gap east
    paint_rect(w.grid, 4, 0, 18, 6, PIX_WALL2);      // the mine face (NW)
    paint_rect(w.grid, 20, 50, 28, 55, PIX_WATER1);  // the frozen tarn
    paint_rect(w.grid, 4, 48, 9, 53, PIX_TREE_M1);   // pine clumps
    paint_rect(w.grid, 32, 34, 37, 38, PIX_TREE_M1);
    paint_rect(w.grid, 14, 20, 19, 24, PIX_TREE_M1);
    paint_rect(w.grid, 40, 2, 46, 7, PIX_TREE_M1);
    paint_rect(w.grid, 12, 34, 16, 37, PIX_GRASS_DARK_1); // scrub through
    paint_rect(w.grid, 30, 20, 34, 23, PIX_GRASS_DARK_1); // the snow
    paint_rect(w.grid, 50, 9, 54, 12, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 22, 34, 26, 37, PIX_WALL2);   // the ruined waystation
    smooth_world(w);
    paint_pavement(w.grid, 23, 35, 25, 36);          // waystation interior
    paint_pavement(w.grid, 24, 37, 24, 37);          // and its door gap
    paint(w.grid, 24, 35, PIX_BRAZIER1);             // the last warm fire
    paint_pavement(w.grid, 8, 7, 14, 11);            // mine forecourt
    paint(w.grid, 7, 8, PIX_TORCH1);                 // gate torches
    paint(w.grid, 15, 8, PIX_TORCH1);
    paint_path(w.grid, 51, 47, 52, 58);  // the worn trail: south approach,
    paint_path(w.grid, 51, 44, 52, 46);  // through gap A,
    paint_path(w.grid, 6, 38, 52, 39);   // terrace 1 traverse west,
    paint_path(w.grid, 4, 30, 5, 32);    // through gap B,
    paint_path(w.grid, 4, 33, 5, 37);
    paint_path(w.grid, 4, 24, 53, 25);   // terrace 2 traverse east,
    paint_path(w.grid, 4, 26, 5, 29);
    paint_path(w.grid, 52, 16, 53, 18);  // through gap C,
    paint_path(w.grid, 52, 19, 53, 23);
    paint_path(w.grid, 12, 13, 52, 14);  // head traverse west to the gate
    paint_path(w.grid, 11, 12, 12, 12);

    // The pass (team 2): the tarn pack, terrace ambushes, the chief's band
    // at the gate, and the hunt coming up behind (dormant until tick 900).
    // Sized for an entry-power lvl-3 crew: the climb is a gauntlet of small
    // packs, not a wall of wild-man hitpoints.
    static constexpr int tarn_pack[3][2] = {{18, 52}, {30, 52}, {24, 48}};
    for (const auto& p : tarn_pack)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 1);
    static constexpr int terrace1[3][2] = {{10, 36}, {18, 35}, {38, 36}};
    for (const auto& p : terrace1)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 2);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 20, 36, 2); // waystation prowlers
    place_living(w, FAMILY_BARBARIAN, 2, 0, 28, 37, 2);
    static constexpr int wild_men[4][2] = {{8, 22}, {22, 21}, {30, 26},
                                           {46, 26}};
    for (const auto& p : wild_men)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 3);
    static constexpr int t2_wolves[3][2] = {{12, 22}, {26, 27}, {40, 22}};
    for (const auto& p : t2_wolves)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 2);
    place_living(w, FAMILY_BARBARIAN, 2, 0, 24, 10, 5, true); // the chief
    static constexpr int chiefs_band[3][2] = {{20, 9}, {28, 9}, {22, 13}};
    for (const auto& p : chiefs_band)
        place_living(w, FAMILY_BARBARIAN, 2, 0, p[0], p[1], 3, true);
    place_living(w, FAMILY_ORC, 2, 0, 34, 10, 2); // head-wall strays
    place_living(w, FAMILY_ORC, 2, 0, 38, 12, 2);
    // The hunt wakes at 900 — pressure on a turtling crew, not a second
    // front landing in the middle of the tarn fight.
    static constexpr int the_hunt[4][2] = {{36, 55}, {39, 56}, {42, 57},
                                           {39, 58}};
    for (const auto& p : the_hunt)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 3, false, false, 900);
    place_generator(w, FAMILY_BONES, 2, 0, 34, 57, 2); // Pale Riders behind

    // The waystation stores.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 23, 36);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 25, 35);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 24, 36);

    // The crew starts on the south apron, lead first, clear of tarn and
    // trees with 2x2 shoulder room on open snow.
    place_start(w, 0, 50, 55);
    static constexpr int starts[9][2] = {{46, 53}, {54, 53}, {46, 57},
                                         {54, 57}, {50, 51}, {44, 55},
                                         {57, 55}, {50, 57}, {57, 52}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // The mine gate on the forecourt, and the trail back down to the Refuge.
    place_exit(w, 0, 11, 9, 6);
    place_exit(w, 0, 58, 58, 4);
    scatter_boulders(w, 0, 0, 19, 59, 29, 23);
    scatter_boulders(w, 0, 0, 33, 59, 43, 23);
    scatter_boulders(w, 0, 30, 47, 59, 59, 27);
    scatter_boulders(w, 0, 20, 0, 59, 15, 25);
    save_level_files(w, 5, "The High Pass",
                     {"The pass lay open for a day.",
                      "Then the sky came down.",
                      "Wolves run the switchbacks",
                      "and wild men hold the heights.",
                      "The summit is buried. Only the",
                      "mine gate remains. Go under."},
                     4, 5000);
}

// 9 THE LOST DELVE (OPTIONAL, reached only via the second exit of 6; the
// deep road out rejoins the main road at the bridge): the reward-vault
// detour, deliberately over-tuned. Floor 1 is the upper delve — an entry
// hall and a broken colonnaded gallery bitten by air; floor 0 is the deep
// vault where a guard-locked double ring of golems and giant skeletons
// wards gold beyond counting, the Delve-king at its heart.
void build_lost_delve(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(9, true, &hooks);
    init_world(level, 2, 60, 50);
    GameWorld& w = level.world();

    // Floor 0: black cavern, moss beds, the walled vault, the hoard carpet.
    paint_rect(w.grid, 0, 0, 59, 49, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 2, 2, 8, 6, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 52, 30, 57, 35, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 12, 8, 51, 41, PIX_WALL2);
    paint_rect(w.grid, 20, 14, 41, 33, PIX_CARPET_M);
    // Floor 1: air, the entry hall east.
    paint_rect(w.grid_for_floor(1), 0, 0, 59, 49, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 44, 17, 57, 32, PIX_WALL2);
    smooth_world(w);
    paint_pavement(w.grid, 13, 9, 50, 40);           // vault interior
    paint_pavement(w.grid, 12, 23, 12, 26);          // west door (4 tiles)
    paint_rect(w.grid, 2, 44, 7, 48, PIX_VOID1);     // chasms the light
    paint_rect(w.grid, 54, 2, 58, 5, PIX_VOID1);     // abandons
    paint(w.grid, 19, 13, PIX_BRAZIER1);             // braziers frame the hoard
    paint(w.grid, 42, 13, PIX_BRAZIER1);
    paint(w.grid, 19, 34, PIX_BRAZIER1);
    paint(w.grid, 42, 34, PIX_BRAZIER1);
    for (int px = 14; px <= 46; px += 8)             // the vault's north row
        paint(w.grid, px, 9, PIX_TORCH1);
    paint_pavement(w.grid_for_floor(1), 45, 18, 56, 31); // hall interior
    paint_pavement(w.grid_for_floor(1), 44, 24, 44, 25); // hall west door
    paint_pavement(w.grid_for_floor(1), 10, 23, 44, 26); // the broken gallery
    paint_pavement(w.grid_for_floor(1), 6, 20, 12, 29);  // west landing
    paint(w.grid_for_floor(1), 24, 23, PIX_AIR);     // air bites in the span
    paint(w.grid_for_floor(1), 25, 23, PIX_AIR);
    paint(w.grid_for_floor(1), 33, 26, PIX_AIR);
    paint(w.grid_for_floor(1), 34, 26, PIX_AIR);
    paint(w.grid_for_floor(1), 26, 23, PIX_DROPBLOCK_DOWN); // shored lips
    paint(w.grid_for_floor(1), 32, 26, PIX_DROPBLOCK_UP);
    static constexpr int colonnade[5] = {14, 21, 28, 35, 42};
    for (int i = 0; i < 5; ++i)                      // gallery edge columns
    {
        paint(w.grid_for_floor(1), colonnade[i], 23,
              (i % 2 == 0) ? PIX_COLUMN1 : PIX_COLUMN2);
        paint(w.grid_for_floor(1), colonnade[i], 26,
              (i % 2 == 0) ? PIX_COLUMN2 : PIX_COLUMN1);
    }
    paint(w.grid_for_floor(1), 46, 19, PIX_BRAZIER1); // hall braziers
    paint(w.grid_for_floor(1), 55, 19, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 46, 30, PIX_BRAZIER1);
    paint(w.grid_for_floor(1), 55, 30, PIX_BRAZIER1);
    stair_pair(w, 0, 8, 24);  // west landing down before the vault door
    stair_pair(w, 0, 54, 20); // the entry hall down to the east cavern

    // The wards of the delve (team 2): the guard-locked hoard rings, the
    // Delve-king at center, cavern prowlers, the gallery watch, and the
    // dead that stir when the looting slows (dormant until tick 300).
    static constexpr int hoard_golems[8][2] = {{22, 16}, {31, 15}, {40, 16},
                                               {18, 23}, {43, 23}, {22, 31},
                                               {31, 32}, {40, 31}};
    for (int i = 0; i < 8; ++i)
        place_living(w, FAMILY_GOLEM, 2, 0, hoard_golems[i][0],
                     hoard_golems[i][1], 6 + (i % 3), true);
    static constexpr int inner_ring[6][2] = {{24, 18}, {38, 18}, {24, 30},
                                             {38, 30}, {31, 19}, {31, 29}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_GIANT_SKELETON, 2, 0, inner_ring[i][0],
                     inner_ring[i][1], (i < 4) ? 8 : 9, true);
    place_living(w, FAMILY_GIANT_SKELETON, 2, 0, 31, 24, 10, true); // the
    static constexpr int prowlers[6][2] = {{4, 10},  {8, 30},   // Delve-king
                                           {30, 45}, {45, 44},
                                           {56, 14}, {50, 6}};
    for (const auto& p : prowlers)
        place_living(w, FAMILY_SKELETON, 2, 0, p[0], p[1], 4);
    static constexpr int cavern_ghosts[4][2] = {{10, 44}, {40, 44},
                                                {56, 26}, {4, 36}};
    for (const auto& p : cavern_ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 5);
    static constexpr int gallery_watch[6][2] = {{14, 24}, {18, 25}, {22, 24},
                                                {30, 25}, {36, 24}, {40, 25}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_SKELETON, 2, 1, gallery_watch[i][0],
                     gallery_watch[i][1], 3 + (i % 2));
    place_living(w, FAMILY_GHOST, 2, 1, 25, 22, 5); // hovering at the bites
    place_living(w, FAMILY_GHOST, 2, 1, 33, 27, 5);
    static constexpr int dead_stir[4][2] = {{20, 44}, {26, 44},
                                            {53, 38}, {53, 44}};
    for (const auto& p : dead_stir)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 6, false, false, 300);
    place_generator(w, FAMILY_BONES, 2, 0, 48, 45, 6);
    place_generator(w, FAMILY_BONES, 2, 0, 4, 4, 5);

    // The point of the level: the gold/silver lattice on the carpet (cells
    // under a warden's feet stay bare), provisions, and the unique prizes
    // flanking the Delve-king.
    for (int gy = 18; gy <= 30; gy += 2)
        for (int gx = 22; gx <= 38; gx += 2)
        {
            if (cell_near_entity(w, 0, gx, gy, 0))
                continue;
            const int fam = (((gx + gy) / 2) % 3 == 2) ? FAMILY_SILVER_BAR
                                                       : FAMILY_GOLD_BAR;
            place(w, Order::Treasure, fam, 0, 0, gx, gy);
        }
    for (int i = 0; i < 6; ++i)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 22 + i * 3, 35);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 21, 15);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 15);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 21, 32);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 40, 32);
    place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 0, 31, 22);
    place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 0, 31, 26);

    // The crew comes alone, mustering in the entry hall before the west
    // door; the Bearer waits with the main road at level 6.
    place_start(w, 1, 46, 24);
    static constexpr int starts[9][2] = {{48, 21}, {48, 27}, {50, 19},
                                         {50, 24}, {50, 29}, {52, 21},
                                         {52, 27}, {54, 24}, {54, 28}};
    for (const auto& s : starts)
        place_start(w, 1, s[0], s[1]);

    // The deep road out rejoins at the Bridge of Shadow; the stair you came
    // down leads back up to the hoard.
    place_exit(w, 0, 31, 10, 8);
    place_exit(w, 1, 55, 25, 6);
    scatter_boulders(w, 0, 0, 0, 59, 49, 21);
    scatter_litter(w, 0, 0, 42, 59, 49, 29); // old bones near the chasm
    save_level_files(w, 9, "The Lost Delve",
                     {"Below the hoard, older doors.",
                      "The miners dug too far down,",
                      "and what they woke still walks.",
                      "Gold beyond counting lies here,",
                      "warded by stone and bone.",
                      "Take it, if you dare."},
                     4, 4500);
}

// 10 THE GOLDEN WOOD: elf sanctuary defense. The pursuing orc column enters
// on the west road and threads the eaves; the crew meets it at the glade
// mouth with elf reinforcements streaming from four treehouses, the Lady as
// an anchor at the heart, and healing pools (water basins ringed by
// drumsticks with cleric wardens) in the backfield. Ghost scouts fly over
// the trees at tick 200; a second wave crosses the west edge at tick 500.
// Floor 1 is the canopy: three platforms in the great trees.
void build_golden_wood(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(10, true, &hooks);
    init_world(level, 2, 70, 50);
    GameWorld& w = level.world();

    // Floor 0: the bordering wood (an east lane gap for the exit road),
    // the inner eaves, the glade, and its pools.
    paint_rect(w.grid, 0, 0, 69, 2, PIX_TREE_M1);    // north border
    paint_rect(w.grid, 0, 47, 69, 49, PIX_TREE_M1);  // south border
    paint_rect(w.grid, 64, 0, 69, 19, PIX_TREE_M1);  // east border, leaving
    paint_rect(w.grid, 64, 30, 69, 49, PIX_TREE_M1); // the lane gap y20..29
    paint_rect(w.grid, 10, 5, 18, 13, PIX_TREE_M1);  // the inner eaves
    paint_rect(w.grid, 10, 36, 18, 44, PIX_TREE_M1);
    paint_rect(w.grid, 22, 8, 30, 16, PIX_TREE_M1);
    paint_rect(w.grid, 22, 32, 30, 40, PIX_TREE_M1);
    paint_rect(w.grid, 30, 14, 36, 19, PIX_TREE_M1);
    paint_rect(w.grid, 30, 30, 36, 35, PIX_TREE_M1);
    paint_rect(w.grid, 38, 12, 62, 38, PIX_GRASS_LIGHT_1); // the glade
    paint_rect(w.grid, 46, 16, 49, 19, PIX_WATER1);  // the healing pools
    paint_rect(w.grid, 54, 26, 57, 29, PIX_WATER1);
    paint_rect(w.grid, 44, 30, 47, 33, PIX_WATER1);
    // Floor 1: the canopy.
    paint_rect(w.grid_for_floor(1), 0, 0, 69, 49, PIX_AIR);
    smooth_world(w);
    paint_path(w.grid, 0, 24, 37, 25);   // the west road
    paint_path(w.grid, 38, 24, 66, 25);  // the glade road (skirts pool 2)
    paint_pavement(w.grid_for_floor(1), 40, 12, 46, 18); // canopy platforms
    paint_pavement(w.grid_for_floor(1), 56, 14, 62, 20);
    paint_pavement(w.grid_for_floor(1), 48, 28, 54, 34);
    static constexpr int lanterns[6][2] = {{40, 12}, {46, 18}, {56, 14},
                                           {62, 20}, {48, 28}, {54, 34}};
    for (const auto& l : lanterns)
        paint(w.grid_for_floor(1), l[0], l[1], PIX_TORCH2);
    stair_pair(w, 0, 41, 17); // the great trees: up into each platform
    stair_pair(w, 0, 61, 19);
    stair_pair(w, 0, 49, 33);

    // The elves of the wood (team 0): pool wardens, canopy sentries, the
    // treehouses mustering reinforcements, and the Lady between the pools
    // at the glade's heart — she does not leave it.
    place_living(w, FAMILY_CLERIC, 0, 0, 50, 17, 5, true);
    place_living(w, FAMILY_CLERIC, 0, 0, 48, 32, 5, true);
    static constexpr int sentries[4][2] = {{42, 14}, {58, 16},
                                           {50, 30}, {52, 32}};
    for (const auto& s : sentries)
        place_living(w, FAMILY_ELF, 0, 1, s[0], s[1], 4, true);
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 39, 12, 4);
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 61, 12, 4);
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 39, 37, 5);
    place_generator(w, FAMILY_TREEHOUSE, 0, 0, 61, 37, 5);
    place_hero(w, FAMILY_ELF, 0, 51, 20, 9, "The Lady", true, false, 0);

    // The pursuing column (team 2): ranks on the west road, a big-orc
    // vanguard, Pale Rider scouts over the trees (tick 200), and a second
    // wave at the west edge (tick 500) with the column's roadside camp.
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 5; ++col)
            place_living(w, FAMILY_ORC, 2, 0, 3 + col * 3, 20 + row * 3,
                         3 + ((row + col) % 2));
    static constexpr int vanguard[6][2] = {{18, 18}, {18, 31}, {21, 22},
                                           {21, 27}, {24, 24}, {24, 26}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_BIG_ORC, 2, 0, vanguard[i][0], vanguard[i][1],
                     4 + (i % 2));
    static constexpr int scouts[4][2] = {{26, 22}, {26, 27}, {28, 24},
                                         {28, 25}};
    for (const auto& p : scouts)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 5, false, false, 200);
    static constexpr int second_wave[10][2] = {
        {1, 18}, {1, 21}, {1, 24}, {1, 27}, {1, 30},
        {2, 19}, {2, 22}, {2, 25}, {2, 28}, {2, 31}};
    for (const auto& p : second_wave)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 4, false, false, 500);
    place_generator(w, FAMILY_TENT, 2, 0, 3, 33, 5);

    // Pool-rim offerings, and the heart of the glade.
    static constexpr int offerings[6][2] = {{45, 16}, {50, 18}, {53, 27},
                                            {58, 28}, {44, 34}, {48, 31}};
    for (const auto& t : offerings)
        place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, t[0], t[1]);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 51, 22);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 52, 21);

    // The crew holds the lane where the wood meets the glade, lead first;
    // two canopy posts for archers and mages.
    place_start(w, 0, 38, 24);
    static constexpr int starts[9][2] = {{35, 21}, {35, 27}, {32, 23},
                                         {32, 26}, {29, 21}, {29, 27},
                                         {37, 18}, {37, 30}, {44, 16}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);
    place_start(w, 1, 43, 13);
    place_start(w, 1, 59, 15);

    // The river path through the east lane gap, and the way back to the
    // bridge along the west edge.
    place_exit(w, 0, 67, 24, 11);
    place_exit(w, 0, 1, 45, 8);
    scatter_boulders(w, 0, 2, 4, 20, 46, 33); // mossy stones, west wood only
    save_level_files(w, 10, "The Golden Wood",
                     {"The wood took us in at dusk.",
                      "The Lady bade us rest by her",
                      "pools. But the hunt found the",
                      "eaves at dawn. Stand with the",
                      "elves. Drive the column back."},
                     4, 4500);
}

// 11 THE GREAT RIVER: island-hopping down a tall river map, north to south —
// a chain of isles linked by narrow dirt fords, ambushes on both banks,
// guarded archers raking the crossings, and two optional mid-river treasure
// isles (one plank-reachable, one flyer-only; its prize is unlocked by the
// flight potion on the last main isle). Pale Riders rise from the water at
// tick 350 to hit whichever isle the crew is crossing.
void build_great_river(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(11, true, &hooks);
    init_world(level, 1, 60, 80);
    GameWorld& w = level.world();

    paint_rect(w.grid, 18, 0, 41, 79, PIX_WATER1);   // the great river
    paint_rect(w.grid, 12, 20, 17, 35, PIX_WATER1);  // meander bulge west
    paint_rect(w.grid, 42, 44, 47, 55, PIX_WATER1);  // meander bulge east
    paint_rect(w.grid, 24, 8, 33, 16, PIX_GRASS1);   // Isle A
    paint_rect(w.grid, 22, 30, 31, 40, PIX_GRASS1);  // Isle B
    paint_rect(w.grid, 28, 52, 37, 62, PIX_GRASS1);  // Isle C
    paint_rect(w.grid, 36, 10, 39, 13, PIX_GRASS_LIGHT_1); // treasure isle T1
    paint_rect(w.grid, 20, 66, 23, 69, PIX_GRASS_LIGHT_1); // T2 (flyer-only)
    paint_rect(w.grid, 14, 11, 23, 12, PIX_DIRT_1);  // F1: west bank -> A
    paint_rect(w.grid, 26, 17, 27, 29, PIX_DIRT_1);  // F2: A -> B
    paint_rect(w.grid, 30, 41, 31, 51, PIX_DIRT_1);  // F3: B -> C
    paint_rect(w.grid, 38, 58, 44, 59, PIX_DIRT_1);  // F4: C -> east bank
    paint_rect(w.grid, 34, 11, 35, 12, PIX_DIRT_1);  // the plank, A -> T1
    paint_rect(w.grid, 2, 8, 8, 14, PIX_TREE_M1);    // bank woods, west
    paint_rect(w.grid, 4, 38, 10, 44, PIX_TREE_M1);
    paint_rect(w.grid, 2, 60, 8, 66, PIX_TREE_M1);
    paint_rect(w.grid, 48, 10, 54, 16, PIX_TREE_M1); // bank woods, east
    paint_rect(w.grid, 50, 34, 56, 40, PIX_TREE_M1);
    paint_rect(w.grid, 46, 62, 52, 66, PIX_TREE_M1);
    paint_rect(w.grid, 15, 14, 17, 19, PIX_GRASS_DARK_1); // reeds
    paint_rect(w.grid, 42, 20, 44, 26, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 14, 46, 17, 52, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 44, 60, 46, 63, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 48, 68, 56, 74, PIX_DIRT_1);  // south camp ground
    smooth_world(w);
    paint_pavement(w.grid, 4, 6, 12, 11);            // the landing quay
    paint_path(w.grid, 12, 11, 13, 12);              // connector to F1
    paint(w.grid, 4, 6, PIX_TORCH1);

    // The banks and isles (team 2): the F1 ambush, Isle B's undead, guarded
    // archers raking F2/F3 over the water, the Isle C southrons, treasure
    // isle wards, the south camp, and the riders rising from the water.
    static constexpr int f1_ambush[6][2] = {{6, 15},  {9, 16}, {12, 14},
                                            {7, 18},  {10, 19}, {13, 17}};
    for (const auto& p : f1_ambush)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 3);
    static constexpr int isle_b_guards[3][2] = {{24, 32}, {28, 32}, {24, 37}};
    for (const auto& p : isle_b_guards)
        place_living(w, FAMILY_SKELETON, 2, 0, p[0], p[1], 4, true);
    static constexpr int isle_b_rovers[3][2] = {{28, 37}, {26, 34}, {30, 35}};
    for (const auto& p : isle_b_rovers)
        place_living(w, FAMILY_SKELETON, 2, 0, p[0], p[1], 4);
    place_living(w, FAMILY_GHOST, 2, 0, 23, 35, 5);
    place_living(w, FAMILY_GHOST, 2, 0, 31, 32, 5);
    static constexpr int harassers[6][2] = {{43, 28}, {45, 31}, {43, 34},
                                            {45, 37}, {43, 40}, {46, 42}};
    for (const auto& p : harassers)
        place_living(w, FAMILY_ARCHER, 2, 0, p[0], p[1], 4, true);
    static constexpr int southrons[8][2] = {{29, 53}, {33, 53}, {36, 55},
                                            {29, 57}, {33, 57}, {36, 59},
                                            {30, 60}, {34, 61}};
    for (int i = 0; i < 8; ++i)
        place_living(w, FAMILY_BARBARIAN, 2, 0, southrons[i][0],
                     southrons[i][1], 4 + (i % 2));
    static constexpr int risen[4][2] = {{20, 44}, {34, 44},
                                        {26, 46}, {30, 26}};
    for (const auto& p : risen) // flyers, rising from the water itself
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 6, false, false, 350);
    place_living(w, FAMILY_GOLEM, 2, 0, 36, 11, 6, true); // T1 wards
    place_living(w, FAMILY_GOLEM, 2, 0, 37, 10, 6, true);
    place_living(w, FAMILY_GHOST, 2, 0, 21, 67, 6, true); // T2 wards
    place_living(w, FAMILY_GHOST, 2, 0, 22, 68, 6, true);
    static constexpr int camp[6][2] = {{50, 70}, {53, 70}, {56, 71},
                                       {50, 73}, {53, 73}, {56, 73}};
    for (const auto& p : camp)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 4);
    place_generator(w, FAMILY_TENT, 2, 0, 49, 69, 5);
    // Beside the east bulge: its ghosts drift out over the river.
    place_generator(w, FAMILY_BONES, 2, 0, 48, 43, 5);

    // The isles' prizes.
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 37, 11);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 38, 12);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 37, 12);
    place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, 38, 11);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 0, 36, 10);
    static constexpr int t2_gold[6][2] = {{20, 66}, {22, 66}, {20, 69},
                                          {23, 69}, {21, 66}, {23, 66}};
    for (const auto& t : t2_gold)
        place(w, Order::Treasure, FAMILY_GOLD_BAR, 0, 0, t[0], t[1]);
    place(w, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, 0, 21, 68);
    place(w, Order::Treasure, FAMILY_FLIGHT_POTION, 0, 0, 32, 57); // the key
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 23, 31);    // to T2
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 30, 39);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 24, 39);

    // The crew lands on the NW quay, lead at its east end facing F1.
    place_start(w, 0, 11, 9);
    static constexpr int starts[9][2] = {{8, 7},  {9, 11}, {6, 9},
                                         {4, 7},  {4, 9},  {11, 6},
                                         {13, 10}, {6, 6}, {13, 7}};
    for (const auto& s : starts)
        place_start(w, 0, s[0], s[1]);

    // Downstream to the falls, or back upstream to the wood.
    place_exit(w, 0, 46, 76, 12);
    place_exit(w, 0, 2, 2, 10);
    scatter_boulders(w, 0, 0, 0, 17, 79, 27);  // west bank
    scatter_boulders(w, 0, 42, 0, 59, 79, 27); // east bank
    scatter_litter(w, 0, 48, 66, 58, 76, 31);  // the camp's refuse
    save_level_files(w, 11, "The Great River",
                     {"The river bears us south now.",
                      "Ford by ford, isle by isle.",
                      "Eyes watch from either bank.",
                      "The riders will not swim,",
                      "but the dead need no boats.",
                      "Make the falls by nightfall."},
                     4, 5500);
}

// 12 THE FALLS — THE GREAT BRANCH. The seat of seeing stands on the upper
// river shelf (floor 1); the river pours over the cliff into the landing
// basin (floor 0) where the Ranger-King holds the quay. The ambush breaks
// over the shelf from three wooded sides; two dormant waves hit the basin.
// When it is done, TWO exits wait at opposite edges: west the war road (13),
// east the Dead Marshes (19). No backtrack — the party breaks here.
void build_the_falls(const LevelDataHooks& hooks)
{
    LevelRuntimeData level(12, true, &hooks);
    init_world(level, 2, 80, 50);
    GameWorld& w = level.world();

    // Floor 1: the shelf — open sky south, the river channel running into
    // the lip (water meeting the air edge IS the falls), woods and scrub.
    paint_rect(w.grid_for_floor(1), 0, 20, 79, 49, PIX_AIR);
    paint_rect(w.grid_for_floor(1), 34, 0, 45, 19, PIX_WATER1);
    paint_rect(w.grid_for_floor(1), 0, 0, 6, 4, PIX_TREE_M1);
    paint_rect(w.grid_for_floor(1), 20, 0, 26, 3, PIX_TREE_M1);
    paint_rect(w.grid_for_floor(1), 50, 0, 57, 5, PIX_TREE_M1);
    paint_rect(w.grid_for_floor(1), 66, 0, 72, 4, PIX_TREE_M1);
    paint_rect(w.grid_for_floor(1), 58, 12, 64, 16, PIX_TREE_M1);
    paint_rect(w.grid_for_floor(1), 4, 13, 8, 16, PIX_GRASS_DARK_1);
    paint_rect(w.grid_for_floor(1), 70, 8, 75, 12, PIX_GRASS_DARK_1);
    // Floor 0: the basin — under-cliff shadow, the plunge pool, spray
    // reeds, the marsh fringe east (foreshadowing 19), woods and the camp.
    paint_rect(w.grid, 0, 0, 79, 19, PIX_DIRT_DARK_1);
    paint_rect(w.grid, 32, 4, 47, 26, PIX_WATER1);
    paint_rect(w.grid, 29, 20, 31, 26, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 48, 20, 50, 26, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 62, 36, 78, 46, PIX_GRASS_DARK_1);
    paint_rect(w.grid, 64, 38, 66, 40, PIX_WATER1);  // marsh puddles; the
    paint_rect(w.grid, 70, 44, 72, 46, PIX_WATER1);  // east exit approach
    paint_rect(w.grid, 10, 44, 18, 48, PIX_TREE_M1); // stays clear
    paint_rect(w.grid, 30, 45, 38, 49, PIX_TREE_M1);
    paint_rect(w.grid, 2, 38, 10, 44, PIX_DIRT_1);   // west camp ground
    smooth_world(w);
    paint_pavement(w.grid_for_floor(1), 34, 9, 45, 10); // the stone bridge
    paint_pavement(w.grid_for_floor(1), 10, 6, 16, 12); // the seat of seeing
    paint(w.grid_for_floor(1), 13, 9, PIX_BOULDER_1);   // the seat itself
    paint(w.grid_for_floor(1), 10, 6, PIX_COLUMN1);
    paint(w.grid_for_floor(1), 16, 6, PIX_COLUMN2);
    paint(w.grid_for_floor(1), 10, 12, PIX_COLUMN2);
    paint(w.grid_for_floor(1), 16, 12, PIX_COLUMN1);
    paint_pavement(w.grid, 52, 28, 60, 33);          // the landing quay
    paint(w.grid, 52, 28, PIX_TORCH1);
    paint(w.grid, 60, 28, PIX_TORCH1);
    paint_path(w.grid, 2, 34, 30, 35);               // the war-road west
    paint_path(w.grid, 30, 30, 31, 33);              // pool rim connector
    stair_pair(w, 0, 4, 18);  // the shelf ends: west descent
    stair_pair(w, 0, 75, 18); // and east descent

    // The ambush (team 2): orcs bursting from the shelf woods on three
    // sides, a big-orc sweep, the captains at the seat, then the dormant
    // waves below — west road at tick 250, east fringe at tick 400.
    static constexpr int shelf_orcs[12][2] = {
        {7, 5},  {9, 7},  {21, 4}, {23, 6}, {25, 8},  {52, 6},
        {54, 8}, {56, 6}, {68, 5}, {70, 7}, {60, 17}, {62, 11}};
    for (const auto& p : shelf_orcs)
        place_living(w, FAMILY_ORC, 2, 1, p[0], p[1], 4);
    static constexpr int sweep[6][2] = {{11, 8},  {15, 8}, {46, 10},
                                        {48, 12}, {65, 13}, {30, 4}};
    for (int i = 0; i < 6; ++i)
        place_living(w, FAMILY_BIG_ORC, 2, 1, sweep[i][0], sweep[i][1],
                     5 + (i % 2));
    place_living(w, FAMILY_BIG_ORC, 2, 1, 12, 11, 8, true); // the captains,
    place_living(w, FAMILY_BIG_ORC, 2, 1, 14, 11, 8, true); // at the seat
    static constexpr int west_wave[10][2] = {
        {3, 33}, {6, 33}, {9, 33}, {12, 33}, {3, 36},
        {6, 36}, {9, 36}, {12, 36}, {5, 39}, {8, 39}};
    for (const auto& p : west_wave)
        place_living(w, FAMILY_ORC, 2, 0, p[0], p[1], 4, false, false, 250);
    static constexpr int east_bones[6][2] = {{63, 36}, {67, 37}, {70, 38},
                                             {63, 43}, {68, 42}, {71, 41}};
    for (const auto& p : east_bones)
        place_living(w, FAMILY_SKELETON, 2, 0, p[0], p[1], 4, false, false,
                     400);
    static constexpr int east_ghosts[4][2] = {{74, 36}, {76, 38},
                                              {74, 44}, {69, 45}};
    for (const auto& p : east_ghosts)
        place_living(w, FAMILY_GHOST, 2, 0, p[0], p[1], 6, false, false, 400);
    place_generator(w, FAMILY_TENT, 2, 0, 6, 41, 5);   // the west camp
    place_generator(w, FAMILY_BONES, 2, 0, 73, 47, 5); // the marsh edge

    // He holds the landing below the falls against both waves.
    place_hero(w, FAMILY_SOLDIER, 0, 56, 30, 9, "Ranger-King", true, false, 0);

    // The landing's stores, and the prize at the foot of the seat — claimed
    // only after the captains fall.
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 54, 32);
    place(w, Order::Treasure, FAMILY_DRUMSTICK, 0, 0, 58, 32);
    place(w, Order::Treasure, FAMILY_MAGIC_POTION, 0, 1, 13, 10);

    // The company beaches on the shelf west of the channel, lead first;
    // two rear-guard posts on the quay beside the Ranger-King.
    place_start(w, 1, 30, 13);
    static constexpr int shelf_starts[7][2] = {{27, 11}, {27, 15}, {24, 13},
                                               {24, 16}, {21, 13}, {30, 16},
                                               {27, 18}};
    for (const auto& s : shelf_starts)
        place_start(w, 1, s[0], s[1]);
    place_start(w, 0, 53, 31);
    place_start(w, 0, 58, 32);

    // The great branch, at opposite edges of the basin: west to the war,
    // east into the marshes. No way back.
    place_exit(w, 0, 2, 35, 13);
    place_exit(w, 0, 77, 42, 19);
    scatter_boulders(w, 0, 0, 0, 79, 19, 25); // rockfall under the cliff
    scatter_litter(w, 0, 62, 36, 78, 46, 31); // the fringe of the marshes
    save_level_files(w, 12, "The Falls",
                     {"At the falls the road divides.",
                      "West: the horns of war call",
                      "the strong to the horse-lords.",
                      "East: the marshes swallow all",
                      "trails, and the burden goes on.",
                      "Choose, and do not look back."},
                     4, 4500);
}

} // namespace

void build_act2(const LevelDataHooks& hooks)
{
    build_high_pass(hooks);
    build_under_the_mountain(hooks);
    build_frozen_wall(hooks);
    build_bridge_of_shadow(hooks);
    build_lost_delve(hooks);
    build_golden_wood(hooks);
    build_great_river(hooks);
    build_the_falls(hooks);
}

std::vector<ExpectedLevel> act2_expectations()
{
    // {id, floors, title, starts, t0 liv/gen, t1 liv/gen, t2 liv/gen,
    //  delayed spawns, specials-disabled, stairs-every-boundary, exit dests}
    return {
        {5, 1, "The High Pass", 10, 0, 0, 0, 0, 25, 1, 4, 0, true, {6, 4}},
        {6, 3, "Under the Mountain", 28, 1, 0, 0, 0, 14, 1, 5, 0, true, {8, 9}},
        {7, 3, "The Frozen Wall", 25, 0, 1, 0, 0, 37, 2, 0, 0, true, {5}},
        {8, 2, "The Bridge of Shadow", 17, 1, 0, 0, 0, 33, 1, 0, 1, true, {10}},
        {9, 2, "The Lost Delve", 10, 0, 0, 0, 0, 37, 2, 4, 0, true, {8, 6}},
        {10, 2, "The Golden Wood", 12, 7, 4, 0, 0, 40, 1, 14, 0, true, {11, 8}},
        {11, 1, "The Great River", 10, 0, 0, 0, 0, 42, 2, 4, 0, false, {12, 10}},
        {12, 2, "The Falls", 10, 1, 0, 0, 0, 40, 2, 20, 0, true, {13, 19}},
    };
}

} // namespace westlands
