/* Multiplayer Game Modes campaign generator — Mutant (840-843).
 *
 * Four 4-team FFA hunting grounds sized for the beacon chase: occlusion
 * over sightlines, ZERO teleporter pads (the mode's own teleport clamp
 * governs blinks; a pad ride would break the beacon hunt), and spice that
 * deliberately excludes invisibility and invulnerability (both break
 * "hunt the Mutant") — drumsticks and speed potions only.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "modes_mapgen.h"

#include "grid_canvas.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/level_data_hooks.h>

#include <cstdio>
#include <format>

bool write_pixie_png(const char* filepath, const PixieData& data);
std::string get_user_path();

namespace modesgen {
namespace {

void install_painted(GameWorld& world, PaintedLevel painted)
{
    world.grid = std::move(painted.base);
    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;
    if (painted.decor.valid())
        world.decor = std::move(painted.decor);
}

void emit_painted(GameWorld& world, const ExpectedLevel& row)
{
    apply_mode_metadata(world, row.title, row.par);
    if (!save_level(world, row))
        return;
    const std::string grid_path =
        get_user_path() + std::format("temp/pix/scen{:04d}.png", row.id);
    if (!write_pixie_png(grid_path.c_str(), world.grid))
        fail(std::format("failed to write {}", grid_path));
    write_decor_plane(world, row.id);
    std::printf("modes_mapgen: built %d '%s' (%dx%d)\n", row.id, row.title,
                world.grid.w, world.grid.h);
}

// Author the row's respawnable food/potion scatter (D8 single source: the
// same pads drive the manifest, lib/mode_items' respawner and the
// self-check pin). Authoring order is the row list order.
void place_item_pads(GameWorld& world, const ExpectedLevel& row)
{
    for (const ItemPad& pad : row.item_pads)
        place_at(world, Order::Treasure, pad.family, 0, pad.at);
}

// Place one team's 12 markers, then emit the other three teams as exact
// symmetric copies (FFA maps are 4-way symmetric by design): team 1
// mirrors x, team 2 mirrors y, team 3 mirrors both. Lead-first order is
// preserved per team.
void place_ffa_markers(GameWorld& world, const std::vector<TilePos>& team0,
                       int w, int h)
{
    for (int team = 0; team < 4; ++team)
        for (const TilePos& p : team0)
        {
            TilePos t = p;
            if (team == 1 || team == 3)
                t.tx = static_cast<short>(w - 1 - t.tx);
            if (team == 2 || team == 3)
                t.ty = static_cast<short>(h - 1 - t.ty);
            place_at(world, Order::Special, FAMILY_RESERVED_TEAM, team, t);
        }
}

// ---------------------------------------------------------------------------
// 840 THE PIT — 30x30, concentric: open cobble bowl, one ring corridor,
// four corner spawn pockets. The duel-grade opener.
// ---------------------------------------------------------------------------
void build_the_pit(const ExpectedLevel& row)
{
    LevelRuntimeData level(840, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(30, 30);

    c.rect(0, 0, 29, 0, PIX_H_WALL1);
    c.rect(0, 29, 29, 29, PIX_H_WALL1);
    c.vline(0, 0, 29, PIX_H_WALL1);
    c.vline(29, 0, 29, PIX_H_WALL1);

    // Outer ring with open corners (the pocket mouths).
    c.hline(3, 26, 3, PIX_H_WALL1);
    c.hline(3, 26, 26, PIX_H_WALL1);
    c.vline(3, 3, 26, PIX_H_WALL1);
    c.vline(26, 3, 26, PIX_H_WALL1);
    for (const int a : {4, 5})
    {
        c.set(a, 3, Canvas::grass(a, 3));
        c.set(3, a, Canvas::grass(3, a));
        c.set(29 - a, 3, Canvas::grass(29 - a, 3));
        c.set(26, a, Canvas::grass(26, a));
        c.set(a, 26, Canvas::grass(a, 26));
        c.set(3, 29 - a, Canvas::grass(3, 29 - a));
        c.set(29 - a, 26, Canvas::grass(29 - a, 26));
        c.set(26, 29 - a, Canvas::grass(26, 29 - a));
    }

    // Inner ring with mid-side gaps into the bowl.
    c.hline(6, 23, 6, PIX_H_WALL1);
    c.hline(6, 23, 23, PIX_H_WALL1);
    c.vline(6, 6, 23, PIX_H_WALL1);
    c.vline(23, 6, 23, PIX_H_WALL1);
    for (int a = 13; a <= 16; ++a)
    {
        c.set(a, 6, Canvas::grass(a, 6));
        c.set(a, 23, Canvas::grass(a, 23));
        c.set(6, a, Canvas::grass(6, a));
        c.set(23, a, Canvas::grass(23, a));
    }
    c.set_decor(6, 6, DECOR_TORCH1);
    c.set_decor(23, 6, DECOR_TORCH1);
    c.set_decor(6, 23, DECOR_TORCH1);
    c.set_decor(23, 23, DECOR_TORCH1);

    // The bowl.
    c.cobble_disc(29, 29, 13); // center (14.5, 14.5), radius 6.5

    install_painted(world, c.finish());

    place_ffa_markers(world,
                      {{2, 2}, {1, 1}, {2, 1}, {1, 2}, {1, 4}, {2, 4},
                       {1, 6}, {2, 6}, {4, 1}, {6, 1}, {4, 2}, {6, 2}},
                      30, 30);

    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 841 CATACOMBS — 50x50 corridor maze; rooms of a 6x6 grid joined by
// offset gaps (connected by construction), torch-lit, bone-strewn. The
// beacon map: long sightlines are rare.
// ---------------------------------------------------------------------------
void build_catacombs(const ExpectedLevel& row)
{
    LevelRuntimeData level(841, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(50, 50);

    c.rect(0, 0, 49, 0, PIX_H_WALL1);
    c.rect(0, 49, 49, 49, PIX_H_WALL1);
    c.vline(0, 0, 49, PIX_H_WALL1);
    c.vline(49, 0, 49, PIX_H_WALL1);

    const int lines[] = {8, 16, 24, 32, 40};
    const int band_starts[] = {1, 9, 17, 25, 33, 41};
    for (const int v : lines)
        c.vline(v, 1, 48, PIX_H_WALL1);
    for (const int hzn : lines)
        c.hline(1, 48, hzn, PIX_H_WALL1);
    // Offset 2-tile gaps: every vertical wall opens once per row band,
    // every horizontal wall once per column band — fully connected.
    for (int i = 0; i < 5; ++i)
        for (int k = 0; k < 6; ++k)
        {
            const int v = lines[i];
            const int gy = band_starts[k] + 2 + ((i + k) % 3);
            c.set(v, gy, Canvas::grass(v, gy));
            c.set(v, gy + 1, Canvas::grass(v, gy + 1));
            c.set_decor(v, gy - 1, DECOR_TORCH1);
            const int hzn = lines[i];
            const int gx = band_starts[k] + 2 + ((i + k) % 3);
            c.set(gx, hzn, Canvas::grass(gx, hzn));
            c.set(gx + 1, hzn, Canvas::grass(gx + 1, hzn));
            c.set_decor(gx - 1, hzn, DECOR_TORCH2);
        }
    for (const auto& b :
         {std::pair{4, 12}, std::pair{44, 12}, std::pair{4, 36},
          std::pair{44, 36}, std::pair{20, 20}, std::pair{28, 28}})
        c.set_decor(b.first, b.second, DECOR_BONES);

    install_painted(world, c.finish());

    place_ffa_markers(world,
                      {{3, 3}, {1, 1}, {3, 1}, {5, 1}, {1, 3}, {5, 3},
                       {1, 5}, {3, 5}, {5, 5}, {1, 7}, {3, 7}, {5, 7}},
                      50, 50);

    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 842 MOONCOURT — 40x40 open dark-grass plaza with a colonnade grid;
// dodge-the-lane fights under the columns.
// ---------------------------------------------------------------------------
void build_mooncourt(const ExpectedLevel& row)
{
    LevelRuntimeData level(842, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(40, 40);

    c.rect(0, 0, 39, 0, PIX_H_WALL1);
    c.rect(0, 39, 39, 39, PIX_H_WALL1);
    c.vline(0, 0, 39, PIX_H_WALL1);
    c.vline(39, 0, 39, PIX_H_WALL1);
    c.dark_grass_rect(1, 1, 38, 38);

    // The colonnade: a 5x5 grid of column pairs, center left open.
    for (const int cx : {8, 14, 20, 26, 32})
        for (const int cy : {8, 14, 20, 26, 32})
        {
            if (cx == 20 && cy == 20)
                continue;
            c.set_decor(cx, cy, DECOR_COLUMN_BOTTOM);
            c.set_decor(cx, cy - 1, DECOR_COLUMN_TOP);
        }

    install_painted(world, c.finish());

    place_ffa_markers(world,
                      {{4, 4}, {2, 2}, {4, 2}, {6, 2}, {2, 4}, {6, 4},
                       {2, 6}, {4, 6}, {6, 6}, {2, 8}, {4, 8}, {6, 8}},
                      40, 40);

    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 843 BROKEN CROWN — 45x45 ruined castle ring: crumbled outer wall with
// rubble chokepoints, an inner keep with two entrances.
// ---------------------------------------------------------------------------
void build_broken_crown(const ExpectedLevel& row)
{
    LevelRuntimeData level(843, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(45, 45);

    c.rect(0, 0, 44, 0, PIX_H_WALL1);
    c.rect(0, 44, 44, 44, PIX_H_WALL1);
    c.vline(0, 0, 44, PIX_H_WALL1);
    c.vline(44, 0, 44, PIX_H_WALL1);

    // Crumbled outer wall with gap chokepoints.
    c.hline(8, 36, 8, PIX_H_WALL1);
    c.hline(8, 36, 36, PIX_H_WALL1);
    c.vline(8, 8, 36, PIX_H_WALL1);
    c.vline(36, 8, 36, PIX_H_WALL1);
    for (const int a : {13, 14, 30, 31})
    {
        c.set(a, 8, Canvas::grass(a, 8));
        c.set(a, 36, Canvas::grass(a, 36));
        c.set(8, a, Canvas::grass(8, a));
        c.set(36, a, Canvas::grass(36, a));
    }
    // Rubble beside every gap.
    for (const int a : {12, 15, 29, 32})
    {
        c.set_decor(a, 8, Canvas::boulder_decor(a, 8));
        c.set_decor(a, 36, Canvas::boulder_decor(a, 36));
        c.set_decor(8, a, Canvas::boulder_decor(8, a));
        c.set_decor(36, a, Canvas::boulder_decor(36, a));
    }

    // The keep: cobble interior, north and south entrances.
    c.hline(17, 27, 17, PIX_H_WALL1);
    c.hline(17, 27, 27, PIX_H_WALL1);
    c.vline(17, 17, 27, PIX_H_WALL1);
    c.vline(27, 17, 27, PIX_H_WALL1);
    c.cobble_rect(18, 18, 26, 26);
    for (const int x : {21, 22})
    {
        c.set(x, 17, Canvas::cobble(x, 17));
        c.set(x, 27, Canvas::cobble(x, 27));
    }
    c.set_decor(19, 19, DECOR_BONES);
    c.set_decor(25, 25, DECOR_BONES);
    c.set_decor(19, 25, DECOR_BONES);
    c.set_decor(25, 19, DECOR_BONES);

    install_painted(world, c.finish());

    place_ffa_markers(world,
                      {{3, 3}, {1, 1}, {3, 1}, {5, 1}, {1, 3}, {5, 3},
                       {1, 5}, {3, 5}, {5, 5}, {6, 2}, {6, 4}, {6, 6}},
                      45, 45);

    place_item_pads(world, row);

    emit_painted(world, row);
}

// Every Mutant treasure is a respawnable pad (the maps ship ONLY food and
// speed potions), so the pad list IS the treasure scatter: the builders
// place from it, and row.treasures must equal its length. Pad order is
// authoring (and manifest/rotation) order. Interval 180 (15 s): the
// HP-decay economy starves without a food trickle.
ExpectedLevel mutant_row(int id, const char* title, int par, int w, int h,
                         int decor_cells, std::vector<ItemPad> pads,
                         std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Mutant;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = 4;
    row.markers_per_team = 12;
    row.treasures = static_cast<int>(pads.size());
    row.item_pads = std::move(pads);
    row.item_interval = 180;
    row.time_limit = 7200;
    row.score_limit = 10;
    // The hunt's current feel: four competitors on these grounds. The
    // roster machinery reads up to 16, so the number is a per-map dial
    // rather than the old hard cap.
    row.fighters = 4;
    row.decor_cells = decor_cells;
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> mutant_expectations()
{
    std::vector<ExpectedLevel> out;

    out.push_back(mutant_row(840, "Mutant: THE PIT", 6, 30, 30, 4,
                             {
                                 {FAMILY_SPEED_POTION, {14, 14}},
                                 {FAMILY_SPEED_POTION, {15, 15}},
                                 {FAMILY_DRUMSTICK, {14, 4}},
                                 {FAMILY_DRUMSTICK, {15, 25}},
                                 {FAMILY_DRUMSTICK, {4, 15}},
                                 {FAMILY_DRUMSTICK, {25, 14}},
                                 {FAMILY_DRUMSTICK, {9, 9}},
                                 {FAMILY_DRUMSTICK, {20, 20}},
                             },
                             {
                                 "A SPECIAL PAGE OF THE BOOK.",
                                 "FIRST BLOOD MAKES A MUTANT:",
                                 "SWIFT, UNSEEN, AND ROTTING.",
                                 "ONLY THE MUTANT MAY BE HARMED;",
                                 "ONLY KILLING KEEPS IT ALIVE.",
                                 "SLAY IT AND WEAR THE CURSE.",
                                 "WATCH YOUR COMPASS, CONTENDERS.",
                                 "-- THE GAMESMASTER",
                             }));

    out.push_back(mutant_row(841, "Mutant: CATACOMBS", 8, 50, 50, 66,
                             {
                                 {FAMILY_DRUMSTICK, {12, 4}},
                                 {FAMILY_DRUMSTICK, {36, 4}},
                                 {FAMILY_DRUMSTICK, {4, 20}},
                                 {FAMILY_DRUMSTICK, {44, 20}},
                                 {FAMILY_DRUMSTICK, {20, 12}},
                                 {FAMILY_DRUMSTICK, {28, 36}},
                                 {FAMILY_DRUMSTICK, {12, 44}},
                                 {FAMILY_DRUMSTICK, {36, 44}},
                                 {FAMILY_DRUMSTICK, {20, 28}},
                                 {FAMILY_DRUMSTICK, {28, 20}},
                                 {FAMILY_SPEED_POTION, {22, 22}},
                                 {FAMILY_SPEED_POTION, {27, 27}},
                             },
                             {
                                 "THE CATACOMBS HIDE THE CURSE",
                                 "WELL. CORRIDORS TURN, TORCHES",
                                 "GUTTER, AND YOUR COMPASS IS",
                                 "THE ONLY HONEST GUIDE. FIRST",
                                 "BLOOD MAKES THE MUTANT; ONLY",
                                 "KILLING KEEPS IT FED.",
                                 "-- THE GAMESMASTER",
                             }));

    out.push_back(mutant_row(842, "Mutant: MOONCOURT", 8, 40, 40, 48,
                             {
                                 {FAMILY_DRUMSTICK, {19, 3}},
                                 {FAMILY_DRUMSTICK, {19, 36}},
                                 {FAMILY_DRUMSTICK, {3, 19}},
                                 {FAMILY_DRUMSTICK, {36, 19}},
                                 {FAMILY_DRUMSTICK, {11, 11}},
                                 {FAMILY_DRUMSTICK, {28, 11}},
                                 {FAMILY_DRUMSTICK, {11, 28}},
                                 {FAMILY_DRUMSTICK, {28, 28}},
                                 {FAMILY_SPEED_POTION, {17, 19}},
                                 {FAMILY_SPEED_POTION, {22, 19}},
                                 {FAMILY_SPEED_POTION, {19, 17}},
                                 {FAMILY_SPEED_POTION, {19, 22}},
                             },
                             {
                                 "THE MOONLIT COURT, CONTENDERS.",
                                 "COLUMNS CARVE THE PLAZA INTO",
                                 "LANES AND EVERY LANE IS A",
                                 "DARE. THE MUTANT RUNS SWIFT",
                                 "AND ROTTING - DODGE, HUNT,",
                                 "AND WEAR THE CURSE IN TURN.",
                                 "-- THE GAMESMASTER",
                             }));

    out.push_back(mutant_row(843, "Mutant: BROKEN CROWN", 10, 45, 45, 20,
                             {
                                 {FAMILY_DRUMSTICK, {22, 10}},
                                 {FAMILY_DRUMSTICK, {22, 34}},
                                 {FAMILY_DRUMSTICK, {10, 22}},
                                 {FAMILY_DRUMSTICK, {34, 22}},
                                 {FAMILY_DRUMSTICK, {12, 12}},
                                 {FAMILY_DRUMSTICK, {32, 12}},
                                 {FAMILY_DRUMSTICK, {12, 32}},
                                 {FAMILY_DRUMSTICK, {32, 32}},
                                 {FAMILY_SPEED_POTION, {20, 22}},
                                 {FAMILY_SPEED_POTION, {24, 22}},
                             },
                             {
                                 "A RUINED CROWN FOR A ROTTING",
                                 "KING. HOLD THE KEEP OR STORM",
                                 "IT; THE CURSE CARES ONLY FOR",
                                 "KILLS. WATCH THE RUBBLE GAPS,",
                                 "WATCH YOUR COMPASS, AND TAKE",
                                 "THE CURSE WHEN IT IS RIPE.",
                                 "-- THE GAMESMASTER",
                             }));

    return out;
}

void build_mutant()
{
    const std::vector<ExpectedLevel> rows = mutant_expectations();
    build_the_pit(rows[0]);
    build_catacombs(rows[1]);
    build_mooncourt(rows[2]);
    build_broken_crown(rows[3]);
}

} // namespace modesgen
