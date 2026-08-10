/* Multiplayer Game Modes campaign generator — Basketball (824-828).
 *
 * Court grammar (all five, docs/basketball-design.md §6.0): a CLOSED
 * impassable perimeter, a hardwood PIX_FLOOR1 court, dashed carpet-runner
 * three-point arcs (cosmetic, D18 — the manifest arc_radius is the sim
 * truth), a cobble center circle on the jump tile, cosmetic cobble keys,
 * and per hoop a 3x3 PIX_CARPET_M dunk carpet whose PIX_CARPET_M2 center
 * IS the rim tile (D3). Hoops/arc/jump-ball are ROW data the manifest
 * carries in pixels; the ball and its shadow are pack families the mode
 * Lua spawns at center court, never authored here. Paint order is
 * load-bearing (later paint wins): walls + hardwood, arc rings, center
 * line(s), center circle, keys, free-throw discs, dunk carpet + M2,
 * gimmick walls, decor.
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

void place_markers(GameWorld& world, int team,
                   const std::vector<TilePos>& tiles)
{
    for (const TilePos& t : tiles)
        place_at(world, Order::Special, FAMILY_RESERVED_TEAM, team, t);
}

std::vector<TilePos> mirror_x(const std::vector<TilePos>& pts, int w)
{
    std::vector<TilePos> out;
    for (const TilePos& p : pts)
        out.push_back({static_cast<short>(w - 1 - p.tx), p.ty});
    return out;
}

// Dashed three-point ring of carpet runners at PIXEL radius r around a
// hoop PIXEL center (Euclidean band, half-width 8 px). VER on the
// east/west sides (diagonal ties included), HOR on the north/south — the
// runner axis follows the ring's local tangent.
void paint_arc_ring(Canvas& c, int hx, int hy, int r)
{
    for (int ty = 1; ty < c.h() - 1; ++ty)
        for (int tx = 1; tx < c.w() - 1; ++tx)
        {
            const int dx = tx * GRID_SIZE + GRID_SIZE / 2 - hx;
            const int dy = ty * GRID_SIZE + GRID_SIZE / 2 - hy;
            const int d2 = dx * dx + dy * dy;
            if (d2 >= (r - 8) * (r - 8) && d2 < (r + 8) * (r + 8))
                c.set(tx, ty, (dx * dx >= dy * dy) ? PIX_CARPET_SMALL_VER
                                                   : PIX_CARPET_SMALL_HOR);
        }
}

// ---------------------------------------------------------------------------
// 824 CENTER COURT — 45x25, 2 teams, W/E hoops. The reference court.
// ---------------------------------------------------------------------------
void build_center_court(const ExpectedLevel& row)
{
    LevelRuntimeData level(824, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(45, 25);
    c.hline(0, 44, 0, PIX_H_WALL1);
    c.hline(0, 44, 24, PIX_H_WALL1);
    c.vline(0, 0, 24, PIX_H_WALL1);
    c.vline(44, 0, 24, PIX_H_WALL1);
    c.rect(1, 1, 43, 23, PIX_FLOOR1);
    paint_arc_ring(c, 56, 200, 160);
    paint_arc_ring(c, 664, 200, 160);
    c.vline(22, 1, 23, PIX_CARPET_SMALL_VER);
    c.cobble_disc(44, 24, 7); // center (22,12), r 3.5
    c.cobble_rect(1, 9, 7, 15);
    c.cobble_rect(37, 9, 43, 15); // keys
    c.cobble_disc(16, 24, 5);
    c.cobble_disc(72, 24, 5); // FT circles
    c.carpet_rect(2, 11, 4, 13);
    c.set(3, 12, PIX_CARPET_M2);
    c.carpet_rect(40, 11, 42, 13);
    c.set(41, 12, PIX_CARPET_M2);
    c.set_decor(1, 9, DECOR_COLUMN_BOTTOM);
    c.set_decor(1, 15, DECOR_COLUMN_BOTTOM);
    c.set_decor(43, 9, DECOR_COLUMN_BOTTOM);
    c.set_decor(43, 15, DECOR_COLUMN_BOTTOM);
    install_painted(world, c.finish());

    // PG lead (2x2 clearance), two wings, two bigs (D8) — lead first.
    const std::vector<TilePos> west = {
        {17, 12}, {14, 9}, {14, 15}, {9, 10}, {9, 14}};
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 45));

    for (const TilePos t :
         {TilePos{12, 2}, TilePos{22, 2}, TilePos{32, 2}, TilePos{12, 22},
          TilePos{22, 22}, TilePos{32, 22}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 825 THE PLAYGROUND — 31x19, 2 teams. Tight half-court brawler: short
// arc, everything is a shooting spot, first to 11.
// ---------------------------------------------------------------------------
void build_the_playground(const ExpectedLevel& row)
{
    LevelRuntimeData level(825, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(31, 19);
    c.hline(0, 30, 0, PIX_H_WALL1);
    c.hline(0, 30, 18, PIX_H_WALL1);
    c.vline(0, 0, 18, PIX_H_WALL1);
    c.vline(30, 0, 18, PIX_H_WALL1);
    c.rect(1, 1, 29, 17, PIX_FLOOR1);
    paint_arc_ring(c, 56, 152, 96);
    paint_arc_ring(c, 440, 152, 96);
    c.vline(15, 1, 17, PIX_CARPET_SMALL_VER);
    c.cobble_disc(30, 18, 5); // center (15,9), r 2.5
    c.cobble_rect(1, 7, 6, 11);
    c.cobble_rect(24, 7, 29, 11);
    c.cobble_disc(14, 18, 5);
    c.cobble_disc(46, 18, 5);
    c.carpet_rect(2, 8, 4, 10);
    c.set(3, 9, PIX_CARPET_M2);
    c.carpet_rect(26, 8, 28, 10);
    c.set(27, 9, PIX_CARPET_M2);
    c.set_decor(1, 7, DECOR_COLUMN_BOTTOM);
    c.set_decor(1, 11, DECOR_COLUMN_BOTTOM);
    c.set_decor(29, 7, DECOR_COLUMN_BOTTOM);
    c.set_decor(29, 11, DECOR_COLUMN_BOTTOM);
    install_painted(world, c.finish());

    const std::vector<TilePos> west = {
        {11, 9}, {9, 6}, {9, 12}, {6, 8}, {6, 10}};
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 31));

    for (const TilePos t : {TilePos{15, 2}, TilePos{15, 16}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 826 FOUR HOOPS — 41x41, 4 teams, one hoop per wall; team k defends
// wall k (0 N, 1 E, 2 S, 3 W). Also the 3-team court: inactive hoops are
// not banked, so they neither score nor count.
// ---------------------------------------------------------------------------
void build_four_hoops(const ExpectedLevel& row)
{
    LevelRuntimeData level(826, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(41, 41);
    c.hline(0, 40, 0, PIX_H_WALL1);
    c.hline(0, 40, 40, PIX_H_WALL1);
    c.vline(0, 0, 40, PIX_H_WALL1);
    c.vline(40, 0, 40, PIX_H_WALL1);
    c.rect(1, 1, 39, 39, PIX_FLOOR1);
    paint_arc_ring(c, 328, 56, 144);  // N
    paint_arc_ring(c, 600, 328, 144); // E
    paint_arc_ring(c, 328, 600, 144); // S
    paint_arc_ring(c, 56, 328, 144);  // W
    c.vline(20, 1, 39, PIX_CARPET_SMALL_VER);
    c.hline(1, 39, 20, PIX_CARPET_SMALL_HOR);
    c.cobble_disc(40, 40, 7);      // center (20,20), r 3.5
    c.cobble_rect(17, 1, 23, 6);   // N key (team 0)
    c.cobble_rect(34, 17, 39, 23); // E (team 1)
    c.cobble_rect(17, 34, 23, 39); // S (team 2)
    c.cobble_rect(1, 17, 6, 23);   // W (team 3)
    c.carpet_rect(19, 2, 21, 4);
    c.set(20, 3, PIX_CARPET_M2); // N
    c.carpet_rect(36, 19, 38, 21);
    c.set(37, 20, PIX_CARPET_M2); // E
    c.carpet_rect(19, 36, 21, 38);
    c.set(20, 37, PIX_CARPET_M2); // S
    c.carpet_rect(2, 19, 4, 21);
    c.set(3, 20, PIX_CARPET_M2); // W
    // 8 posts flanking each key mouth on its baseline.
    for (const auto& post :
         {std::pair{16, 1}, std::pair{24, 1}, std::pair{39, 16},
          std::pair{39, 24}, std::pair{16, 39}, std::pair{24, 39},
          std::pair{1, 16}, std::pair{1, 24}})
        c.set_decor(post.first, post.second, DECOR_COLUMN_BOTTOM);
    install_painted(world, c.finish());

    // North team formation, rotated 90 degrees per team:
    // (x, y) -> (40 - y, x) maps the north wall onto the east wall.
    std::vector<TilePos> pts = {
        {20, 15}, {17, 12}, {23, 12}, {15, 8}, {25, 8}};
    for (int team = 0; team < 4; ++team)
    {
        place_markers(world, team, pts);
        for (TilePos& p : pts)
            p = {static_cast<short>(40 - p.ty), p.tx};
    }

    for (const TilePos t :
         {TilePos{5, 5}, TilePos{35, 5}, TilePos{35, 35}, TilePos{5, 35},
          TilePos{12, 12}, TilePos{28, 12}, TilePos{28, 28},
          TilePos{12, 28}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 827 THE BANKHOUSE — 45x27, 2 teams. Protruding backboard stubs, wing
// pillars and a mid-court pillar quartet: straight lanes die, banks and
// angles live (D12 — pillars break flat and chest passes; lobs and shots
// clear them).
// ---------------------------------------------------------------------------
void build_the_bankhouse(const ExpectedLevel& row)
{
    LevelRuntimeData level(827, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(45, 27);
    c.hline(0, 44, 0, PIX_H_WALL1);
    c.hline(0, 44, 26, PIX_H_WALL1);
    c.vline(0, 0, 26, PIX_H_WALL1);
    c.vline(44, 0, 26, PIX_H_WALL1);
    c.rect(1, 1, 43, 25, PIX_FLOOR1);
    paint_arc_ring(c, 56, 216, 176);
    paint_arc_ring(c, 664, 216, 176);
    c.vline(22, 1, 25, PIX_CARPET_SMALL_VER);
    c.cobble_disc(44, 26, 7); // center (22,13), r 3.5
    c.cobble_rect(1, 10, 7, 16);
    c.cobble_rect(37, 10, 43, 16);
    c.carpet_rect(2, 12, 4, 14);
    c.set(3, 13, PIX_CARPET_M2);
    c.carpet_rect(40, 12, 42, 14);
    c.set(41, 13, PIX_CARPET_M2);
    // GIMMICK 1: protruding backboard stubs (1 tile in, 5 tall; rim gap
    // 24 px).
    c.vline(1, 11, 15, PIX_H_WALL1);
    c.vline(43, 11, 15, PIX_H_WALL1);
    // GIMMICK 2: wing pillars off the key's front corners (bank pockets).
    c.set(5, 9, PIX_H_WALL1);
    c.set(5, 17, PIX_H_WALL1);
    c.set(39, 9, PIX_H_WALL1);
    c.set(39, 17, PIX_H_WALL1);
    // GIMMICK 3: mid-court pillar quartet (breaks flat sideline lanes).
    c.set(17, 7, PIX_H_WALL1);
    c.set(17, 19, PIX_H_WALL1);
    c.set(27, 7, PIX_H_WALL1);
    c.set(27, 19, PIX_H_WALL1);
    // Decor: pebble scuff in the four rebound pockets.
    c.set_decor(8, 4, DECOR_PEBBLES);
    c.set_decor(36, 4, DECOR_PEBBLES);
    c.set_decor(8, 22, DECOR_PEBBLES);
    c.set_decor(36, 22, DECOR_PEBBLES);
    install_painted(world, c.finish());

    const std::vector<TilePos> west = {
        {18, 13}, {15, 10}, {15, 16}, {11, 12}, {11, 14}};
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 45));

    for (const TilePos t :
         {TilePos{12, 2}, TilePos{22, 2}, TilePos{32, 2}, TilePos{12, 24},
          TilePos{22, 24}, TilePos{32, 24}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 828 BENCHWARMERS — 47x29, 2 teams. Each side's north-wall bench alcove
// tent raises skeleton substitutes (capped 4+4 by mode_caps); 3x3
// interior with a 1-wide south mouth, BONEYARD CUP's audited shape.
// ---------------------------------------------------------------------------
void build_benchwarmers(const ExpectedLevel& row)
{
    LevelRuntimeData level(828, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(47, 29);
    c.hline(0, 46, 0, PIX_H_WALL1);
    c.hline(0, 46, 28, PIX_H_WALL1);
    c.vline(0, 0, 28, PIX_H_WALL1);
    c.vline(46, 0, 28, PIX_H_WALL1);
    c.rect(1, 1, 45, 27, PIX_FLOOR1);
    paint_arc_ring(c, 56, 232, 160);
    paint_arc_ring(c, 696, 232, 160);
    c.vline(23, 1, 27, PIX_CARPET_SMALL_VER);
    c.cobble_disc(46, 28, 7); // center (23,14), r 3.5
    c.cobble_rect(1, 11, 7, 17);
    c.cobble_rect(39, 11, 45, 17);
    c.cobble_disc(16, 28, 5);
    c.cobble_disc(76, 28, 5);
    c.carpet_rect(2, 13, 4, 15);
    c.set(3, 14, PIX_CARPET_M2);
    c.carpet_rect(42, 13, 44, 15);
    c.set(43, 14, PIX_CARPET_M2);
    // Bench alcoves on the north wall, 3x3 interior, 1-wide south mouth
    // (BONEYARD CUP's shape, which passes audit_generator_spawn_exits).
    c.hline(8, 12, 1, PIX_H_WALL1);
    c.hline(8, 12, 5, PIX_H_WALL1);
    c.vline(8, 1, 5, PIX_H_WALL1);
    c.vline(12, 1, 5, PIX_H_WALL1);
    c.cobble_rect(9, 2, 11, 4);
    c.set(10, 5, Canvas::cobble(10, 5)); // mouth
    c.hline(34, 38, 1, PIX_H_WALL1);
    c.hline(34, 38, 5, PIX_H_WALL1);
    c.vline(34, 1, 5, PIX_H_WALL1);
    c.vline(38, 1, 5, PIX_H_WALL1);
    c.cobble_rect(35, 2, 37, 4);
    c.set(36, 5, Canvas::cobble(36, 5));
    // Decor: posts at key baseline corners + bones at the bench mouths.
    c.set_decor(1, 11, DECOR_COLUMN_BOTTOM);
    c.set_decor(1, 17, DECOR_COLUMN_BOTTOM);
    c.set_decor(45, 11, DECOR_COLUMN_BOTTOM);
    c.set_decor(45, 17, DECOR_COLUMN_BOTTOM);
    c.set_decor(10, 6, DECOR_BONES);
    c.set_decor(36, 6, DECOR_BONES);
    install_painted(world, c.finish());

    // Tents anchor at the alcove interior's NW corner (BONEYARD's
    // corner-anchor idiom): the 32x32 body spans the two NORTH interior
    // rows, keeping the tile above the 1-wide south mouth open so spawns
    // can walk out.
    place_at(world, Order::Generator, FAMILY_TENT, 0, {9, 2}, 1);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {36, 2}, 1);

    const std::vector<TilePos> west = {
        {19, 14}, {16, 11}, {16, 17}, {12, 13}, {12, 15}};
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 47));

    for (const TilePos t :
         {TilePos{17, 2}, TilePos{23, 2}, TilePos{29, 2}, TilePos{17, 26},
          TilePos{23, 26}, TilePos{29, 26}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

ExpectedLevel basketball_row(int id, const char* title, int par, int w,
                             int h, int teams, int treasures,
                             std::vector<TilePos> hoops, int arc_radius,
                             TilePos jump_ball,
                             std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Basketball;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = teams;
    row.markers_per_team = 5; // D8: five-a-side, one bot per anchor
    row.treasures = treasures;
    row.time_limit = 7200;   // D10 (825 overrides to 5400)
    row.score_limit = 21;    // D9 (825 overrides to 11)
    row.hoops = std::move(hoops);
    row.arc_radius = arc_radius;
    row.jump_ball = jump_ball;
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> basketball_expectations()
{
    std::vector<ExpectedLevel> out;

    ExpectedLevel center_court = basketball_row(
        824, "Basketball: CENTER COURT", 6, 45, 25, 2, 6,
        {{3, 12}, {41, 12}}, 160, {22, 12},
        {
            "A NEW GAME, CONTENDERS.",
            "CARRY THE BALL TO THE HOOP AND",
            "STEP IN: TWO POINTS. OR THROW",
            "IT HIGH - TWO INSIDE THE ARC,",
            "THREE BEYOND. BLEED AND YOU",
            "DROP IT. FIRST TO 21 TAKES",
            "THE COURT.",
            "-- THE GAMESMASTER",
        });
    center_court.decor_cells = 4;
    out.push_back(std::move(center_court));

    ExpectedLevel playground = basketball_row(
        825, "Basketball: THE PLAYGROUND", 6, 31, 19, 2, 2,
        {{3, 9}, {27, 9}}, 96, {15, 9},
        {
            "THE PLAYGROUND. A CRAMPED",
            "LITTLE COURT WHERE EVERY SPOT",
            "IS A SHOOTING SPOT AND EVERY",
            "DRIVE IS A BRAWL. SHORT ARC,",
            "SHORT TEMPERS. FIRST TO 11.",
            "-- THE GAMESMASTER",
        });
    playground.score_limit = 11;
    playground.time_limit = 5400;
    playground.decor_cells = 4;
    out.push_back(std::move(playground));

    ExpectedLevel four_hoops = basketball_row(
        826, "Basketball: FOUR HOOPS", 8, 41, 41, 4, 8,
        {{20, 3}, {37, 20}, {20, 37}, {3, 20}}, 144, {20, 20},
        {
            "FOUR HOOPS, FOUR BANDS, ONE",
            "BALL. GUARD YOUR OWN RIM AND",
            "SCORE ON ANY RIVAL'S. EVERY",
            "REBOUND HAS FOUR CLAIMANTS.",
            "FIRST TO 21 TAKES THE CIRCUS.",
            "-- THE GAMESMASTER",
        });
    four_hoops.decor_cells = 8;
    out.push_back(std::move(four_hoops));

    ExpectedLevel bankhouse = basketball_row(
        827, "Basketball: THE BANKHOUSE", 8, 45, 27, 2, 6,
        {{3, 13}, {41, 13}}, 176, {22, 13},
        {
            "THE BANKHOUSE. WALLS JUT AND",
            "PILLARS CROWD THE LANES: A",
            "STRAIGHT PASS DIES, A CLEVER",
            "BANK LIVES. PLAY THE ANGLES.",
            "FIRST TO 21.",
            "-- THE GAMESMASTER",
        });
    bankhouse.decor_cells = 4;
    out.push_back(std::move(bankhouse));

    ExpectedLevel benchwarmers = basketball_row(
        828, "Basketball: BENCHWARMERS", 10, 47, 29, 2, 6,
        {{3, 14}, {43, 14}}, 160, {23, 14},
        {
            "BENCHWARMERS. EACH SIDE'S OLD",
            "TENT RAISES BONY SUBSTITUTES",
            "WHO NEVER TIRE AND NEVER",
            "FLINCH. DEEP BENCHES, CHEAP",
            "FOULS. FIRST TO 21.",
            "-- THE GAMESMASTER",
        });
    benchwarmers.generators_per_team[0] = 1;
    benchwarmers.generators_per_team[1] = 1;
    benchwarmers.spawn_caps = {{0, 4}, {1, 4}};
    benchwarmers.decor_cells = 6;
    out.push_back(std::move(benchwarmers));

    return out;
}

void build_basketball()
{
    const std::vector<ExpectedLevel> rows = basketball_expectations();
    build_center_court(rows[0]);
    build_the_playground(rows[1]);
    build_four_hoops(rows[2]);
    build_the_bankhouse(rows[3]);
    build_benchwarmers(rows[4]);
}

} // namespace modesgen
