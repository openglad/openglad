/* Multiplayer Game Modes campaign generator — Free For All (850-855).
 *
 * Six arenas for the mode where the team byte stops meaning anything
 * (docs/ffa-design.md §6-7): every deployed character fights every other,
 * so each map carries FOUR start-marker clusters (teams 0-3) INTERLEAVED
 * around the arena instead of stacked in four corners. The mode reads
 * them as position pools, not identities — its placement cursor walks
 * pool 0, 1, 2, 3, 0, ... so an interleaved authoring is what scatters
 * sixteen fighters over the whole floor. No exits, no generators, and
 * spice that stays deathmatch-honest: drumsticks and speed potions only
 * (invisibility and invulnerability would rot a sixteen-way brawl the
 * same way they rot the Mutant hunt).
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
#include <iterator>
#include <utility>
#include <vector>

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

// The four start clusters, INTERLEAVED: spot i belongs to team i % 4, so
// walking the list around the arena hands each pool every fourth stop and
// the mode's pool rotation lands consecutive fighters far apart. The list
// length must be a whole number of rounds; a short tail would give one
// pool more anchors than the others and skew the scatter.
void place_interleaved_markers(GameWorld& world,
                               const std::vector<TilePos>& spots,
                               const ExpectedLevel& row)
{
    if (static_cast<int>(spots.size()) != 4 * row.markers_per_team)
    {
        fail(std::format("scen{}: {} marker spots for 4 x {} clusters",
                         row.id, spots.size(), row.markers_per_team));
        return;
    }
    for (std::size_t i = 0; i < spots.size(); ++i)
        place_at(world, Order::Special, FAMILY_RESERVED_TEAM,
                 static_cast<int>(i % 4), spots[i]);
}

// Evenly stepped spots around an axis-aligned rectangle, walked clockwise
// from (x0, y0): per_side stops along the top edge, then the right, the
// bottom and the left. Every corner is a stop, so the walk is symmetric
// under the arena's own 4-fold symmetry.
std::vector<TilePos> rect_ring(int x0, int y0, int x1, int y1, int per_side)
{
    std::vector<TilePos> out;
    const int step_x = (x1 - x0) / per_side;
    const int step_y = (y1 - y0) / per_side;
    for (int i = 0; i < per_side; ++i)
        out.push_back({static_cast<short>(x0 + i * step_x),
                       static_cast<short>(y0)});
    for (int i = 0; i < per_side; ++i)
        out.push_back({static_cast<short>(x1),
                       static_cast<short>(y0 + i * step_y)});
    for (int i = 0; i < per_side; ++i)
        out.push_back({static_cast<short>(x1 - i * step_x),
                       static_cast<short>(y1)});
    for (int i = 0; i < per_side; ++i)
        out.push_back({static_cast<short>(x0),
                       static_cast<short>(y1 - i * step_y)});
    return out;
}

void perimeter_wall(Canvas& c)
{
    c.hline(0, c.w() - 1, 0, PIX_H_WALL1);
    c.hline(0, c.w() - 1, c.h() - 1, PIX_H_WALL1);
    c.vline(0, 0, c.h() - 1, PIX_H_WALL1);
    c.vline(c.w() - 1, 0, c.h() - 1, PIX_H_WALL1);
}

// A standing column: the two-tile art pair the colonnade maps use.
void column(Canvas& c, int x, int y)
{
    c.set_decor(x, y, DECOR_COLUMN_BOTTOM);
    c.set_decor(x, y - 1, DECOR_COLUMN_TOP);
}

// ---------------------------------------------------------------------------
// 850 THE MELEE — 34x34 open colosseum: one cobble bowl under a ring of
// eight columns, nothing to hide behind but the pillars. The eight-fighter
// opener, where the whole arena is inside one screen of sightline.
// ---------------------------------------------------------------------------
void build_the_melee(const ExpectedLevel& row)
{
    LevelRuntimeData level(850, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(34, 34);

    perimeter_wall(c);
    c.cobble_disc(33, 33, 25); // center (16.5, 16.5), radius 12.5

    for (const auto& p :
         {std::pair{24, 16}, std::pair{8, 16}, std::pair{16, 24},
          std::pair{16, 8}, std::pair{22, 22}, std::pair{10, 22},
          std::pair{22, 10}, std::pair{10, 10}})
        column(c, p.first, p.second);

    install_painted(world, c.finish());

    place_interleaved_markers(world, rect_ring(4, 4, 28, 28, 8), row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 851 CROSSFIRE — 40x40, a drowning moat around a cobble island with four
// plank bridges. Ten fighters, four chokepoints, and a center worth the
// crossing.
// ---------------------------------------------------------------------------
void build_crossfire(const ExpectedLevel& row)
{
    LevelRuntimeData level(851, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(40, 40);

    perimeter_wall(c);
    c.water_rect(12, 12, 27, 27);
    c.cobble_rect(15, 15, 24, 24);

    // The four bridges, two planks wide each.
    c.cobble_rect(19, 12, 20, 14);
    c.cobble_rect(19, 25, 20, 27);
    c.cobble_rect(12, 19, 14, 20);
    c.cobble_rect(25, 19, 27, 20);

    // Torch posts on the island corners: light over the water, and four
    // fewer places to back into.
    c.set_decor(15, 15, DECOR_TORCH1);
    c.set_decor(24, 15, DECOR_TORCH1);
    c.set_decor(15, 24, DECOR_TORCH2);
    c.set_decor(24, 24, DECOR_TORCH2);

    // Rubble banks on the four outer approaches — the only cover on an
    // otherwise naked shore.
    for (const auto& bank :
         {std::pair{8, 8}, std::pair{31, 8}, std::pair{8, 31},
          std::pair{31, 31}})
        for (const auto& off :
             {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}})
        {
            const int bx = bank.first + off.first;
            const int by = bank.second + off.second;
            c.set_decor(bx, by, Canvas::boulder_decor(bx, by));
        }

    install_painted(world, c.finish());

    place_interleaved_markers(world, rect_ring(4, 4, 36, 36, 8), row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 852 SHARDS — 44x44 broken hall: a 3x3 room grid whose walls have fallen
// open in the middle of every span, boulders strewn for line-of-sight
// breaks. Twelve fighters, and no sightline longer than a room.
// ---------------------------------------------------------------------------
void build_shards(const ExpectedLevel& row)
{
    LevelRuntimeData level(852, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(44, 44);

    perimeter_wall(c);
    const int lines[] = {14, 29};
    for (const int v : lines)
    {
        c.vline(v, 1, 42, PIX_H_WALL1);
        c.hline(1, 42, v, PIX_H_WALL1);
    }
    // The breaks: one three-tile gap per wall span, so every room pair
    // shares a mouth and the room graph is the full 3x3 lattice.
    for (const int v : lines)
        for (const int band : {6, 20, 35})
            for (int k = 0; k < 3; ++k)
            {
                c.set(v, band + k, Canvas::grass(v, band + k));
                c.set(band + k, v, Canvas::grass(band + k, v));
            }

    // The eight outer rooms, clockwise from the north-west. The middle
    // room stays a battleground and never a spawn, so each outer room
    // fields four markers — one of every pool, no room anyone's home.
    const std::pair<int, int> rooms[] = {{7, 7},   {22, 7},  {36, 7},
                                         {36, 22}, {36, 36}, {22, 36},
                                         {7, 36},  {7, 22}};
    std::vector<TilePos> spots;
    for (std::size_t r = 0; r < std::size(rooms); ++r)
    {
        const int cx = rooms[r].first;
        const int cy = rooms[r].second;
        c.set_decor(cx, cy - 4, Canvas::boulder_decor(cx, cy - 4));
        c.set_decor(cx, cy + 4, Canvas::boulder_decor(cx, cy + 4));
        c.set_decor(cx - 4, cy + 3, DECOR_PEBBLES);
        c.set_decor(cx + 4, cy - 3, DECOR_PEBBLES);
        // A fallen span, laid flat in one room and on end in the next:
        // the hall came down differently everywhere and the sightlines
        // follow the wreckage.
        if (r % 2 == 0)
            c.hline(cx - 1, cx + 1, cy - 2, PIX_H_WALL1);
        else
            c.vline(cx + 2, cy - 1, cy + 1, PIX_H_WALL1);
        for (const auto& off :
             {std::pair{-3, -3}, std::pair{3, -3}, std::pair{3, 3},
              std::pair{-3, 3}})
            spots.push_back({static_cast<short>(cx + off.first),
                             static_cast<short>(cy + off.second)});
    }
    // The middle room's own rubble, framing the contested pads.
    c.set_decor(19, 22, Canvas::boulder_decor(19, 22));
    c.set_decor(25, 22, Canvas::boulder_decor(25, 22));
    c.set_decor(22, 19, Canvas::boulder_decor(22, 19));
    c.set_decor(22, 25, Canvas::boulder_decor(22, 25));

    install_painted(world, c.finish());

    place_interleaved_markers(world, spots, row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 853 THE ROSE — 45x45: eight wall spokes off a cobble heart, an outer
// corridor closing the petals into a ring. Sixteen fighters, one center
// pad, and every petal a lane back to it.
// ---------------------------------------------------------------------------
void build_the_rose(const ExpectedLevel& row)
{
    LevelRuntimeData level(853, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(45, 45);

    perimeter_wall(c);
    c.cobble_disc(44, 44, 13); // center (22, 22), radius 6.5

    // Spokes run radius 9 to 17: clear of the heart, clear of the outer
    // corridor, so the petals open at both ends.
    c.vline(22, 5, 13, PIX_H_WALL1);
    c.vline(22, 31, 39, PIX_H_WALL1);
    c.hline(5, 13, 22, PIX_H_WALL1);
    c.hline(31, 39, 22, PIX_H_WALL1);
    for (int k = 7; k <= 13; ++k)
    {
        c.set(22 + k, 22 + k, PIX_H_WALL1);
        c.set(22 - k, 22 - k, PIX_H_WALL1);
        c.set(22 + k, 22 - k, PIX_H_WALL1);
        c.set(22 - k, 22 + k, PIX_H_WALL1);
    }

    // Torches on the heart's rim, off the spoke lines.
    c.set_decor(17, 17, DECOR_TORCH1);
    c.set_decor(27, 17, DECOR_TORCH1);
    c.set_decor(17, 27, DECOR_TORCH2);
    c.set_decor(27, 27, DECOR_TORCH2);

    install_painted(world, c.finish());

    place_interleaved_markers(world, rect_ring(3, 3, 43, 43, 8), row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 854 SCRAMBLE — 48x48 open ground under a lattice of stub walls, with
// twin cobble pads at mid-field. Sixteen fighters, cover everywhere and
// safety nowhere.
// ---------------------------------------------------------------------------
void build_scramble(const ExpectedLevel& row)
{
    LevelRuntimeData level(854, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(48, 48);

    perimeter_wall(c);
    c.cobble_rect(14, 22, 18, 26);
    c.cobble_rect(29, 22, 33, 26);

    // Cover on a loose lattice: each stub steps off its anchor and takes
    // one of three shapes, so the field scatters instead of tiling. The
    // offsets and shapes are pure functions of the lattice index — the
    // scramble is authored, not rolled.
    const int lattice[] = {9, 20, 27, 38};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            const int cx = lattice[i] + (2 * i + j) % 3 - 1;
            const int cy = lattice[j] + (i + 2 * j) % 3 - 1;
            switch ((i + j) % 3)
            {
                case 0: c.rect(cx, cy, cx + 1, cy + 1, PIX_H_WALL1); break;
                case 1: c.hline(cx, cx + 2, cy, PIX_H_WALL1); break;
                default: c.vline(cx, cy, cy + 2, PIX_H_WALL1); break;
            }
            c.set_decor(cx - 1, cy, Canvas::boulder_decor(cx - 1, cy));
        }

    install_painted(world, c.finish());

    place_interleaved_markers(world, rect_ring(4, 4, 44, 44, 8), row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 855 NIGHTFALL — 50x50 after dark: a torch-lit corridor loop between two
// wall rings, its mouths offset so the way in is never the way through,
// and a cobble chamber at the heart. Sixteen fighters in the lamplight.
// ---------------------------------------------------------------------------
void build_nightfall(const ExpectedLevel& row)
{
    LevelRuntimeData level(855, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(50, 50);

    perimeter_wall(c);
    c.dark_grass_rect(1, 1, 48, 48);
    c.cobble_rect(15, 15, 34, 34);

    // Outer ring, mouths at the four mid-sides.
    c.hline(8, 41, 8, PIX_H_WALL1);
    c.hline(8, 41, 41, PIX_H_WALL1);
    c.vline(8, 8, 41, PIX_H_WALL1);
    c.vline(41, 8, 41, PIX_H_WALL1);
    for (int k = 23; k <= 25; ++k)
    {
        c.set(k, 8, Canvas::dark_grass(k, 8));
        c.set(k, 41, Canvas::dark_grass(k, 41));
        c.set(8, k, Canvas::dark_grass(8, k));
        c.set(41, k, Canvas::dark_grass(41, k));
    }

    // Inner ring, mouths rotated a quarter turn off the outer ones: every
    // entrance costs a walk along the loop.
    c.hline(14, 35, 14, PIX_H_WALL1);
    c.hline(14, 35, 35, PIX_H_WALL1);
    c.vline(14, 14, 35, PIX_H_WALL1);
    c.vline(35, 14, 35, PIX_H_WALL1);
    for (int k = 0; k < 3; ++k)
    {
        c.set(16 + k, 14, Canvas::cobble(16 + k, 14));
        c.set(35, 16 + k, Canvas::cobble(35, 16 + k));
        c.set(31 + k, 35, Canvas::cobble(31 + k, 35));
        c.set(14, 31 + k, Canvas::cobble(14, 31 + k));
    }

    // The lamps: one run down each wall of the loop, clear of the mouths.
    for (const int k : {10, 14, 18, 22, 27, 31, 35, 39})
    {
        c.set_decor(k, 9, DECOR_TORCH1);
        c.set_decor(k, 40, DECOR_TORCH2);
        c.set_decor(9, k, DECOR_TORCH3);
        c.set_decor(40, k, DECOR_TORCH1);
    }
    c.set_decor(16, 16, DECOR_BRAZIER);
    c.set_decor(33, 16, DECOR_BRAZIER);
    c.set_decor(16, 33, DECOR_BRAZIER);
    c.set_decor(33, 33, DECOR_BRAZIER);

    install_painted(world, c.finish());

    place_interleaved_markers(world, rect_ring(4, 4, 44, 44, 8), row);
    place_item_pads(world, row);

    emit_painted(world, row);
}

// Every FFA treasure is a respawnable pad (the arenas ship ONLY food and
// speed potions), so the pad list IS the treasure scatter: the builders
// place from it, and row.treasures must equal its length. Interval 180
// (15 s) matches the Mutant trickle — sixteen fighters eat a map bare.
//
// spawn_caps bank a hard zero on all four pools: FFA retires authored
// generators at init, and a zero cap means any generator that survives
// (a latched hook, a summon family that lands in the Generator order)
// can never add a body to a sixteen-way brawl.
ExpectedLevel ffa_row(int id, const char* title, int par, int w, int h,
                      int fighters, int decor_cells, std::vector<ItemPad> pads,
                      std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Ffa;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = 4;
    row.markers_per_team = 8;
    row.treasures = static_cast<int>(pads.size());
    row.item_pads = std::move(pads);
    row.item_interval = 180;
    row.time_limit = 7200;
    row.score_limit = 15;
    row.fighters = fighters;
    for (int team = 0; team < 4; ++team)
        row.spawn_caps.push_back({team, 0});
    row.decor_cells = decor_cells;
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> ffa_expectations()
{
    std::vector<ExpectedLevel> out;

    out.push_back(ffa_row(850, "FFA: THE MELEE", 6, 34, 34, 8, 16,
                          {
                              {FAMILY_SPEED_POTION, {16, 16}},
                              {FAMILY_SPEED_POTION, {17, 17}},
                              {FAMILY_DRUMSTICK, {16, 12}},
                              {FAMILY_DRUMSTICK, {16, 21}},
                              {FAMILY_DRUMSTICK, {12, 16}},
                              {FAMILY_DRUMSTICK, {21, 16}},
                              {FAMILY_DRUMSTICK, {9, 9}},
                              {FAMILY_DRUMSTICK, {24, 9}},
                              {FAMILY_DRUMSTICK, {9, 24}},
                              {FAMILY_DRUMSTICK, {24, 24}},
                          },
                          {
                              "FREE FOR ALL, CONTENDERS.",
                              "YOUR TEAM COLOR BUYS NOTHING",
                              "HERE - EVERY BLADE IS AIMED AT",
                              "YOU, AND YOURS AT EVERY BLADE.",
                              "EIGHT ENTER THE PILLAR RING.",
                              "THE PURSE GOES TO THE NAME",
                              "STILL COUNTING KILLS.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ffa_row(851, "FFA: CROSSFIRE", 8, 40, 40, 10, 16,
                          {
                              {FAMILY_SPEED_POTION, {19, 19}},
                              {FAMILY_SPEED_POTION, {20, 20}},
                              {FAMILY_DRUMSTICK, {17, 17}},
                              {FAMILY_DRUMSTICK, {22, 22}},
                              {FAMILY_DRUMSTICK, {17, 22}},
                              {FAMILY_DRUMSTICK, {22, 17}},
                              {FAMILY_DRUMSTICK, {19, 8}},
                              {FAMILY_DRUMSTICK, {19, 31}},
                              {FAMILY_DRUMSTICK, {8, 19}},
                              {FAMILY_DRUMSTICK, {31, 19}},
                          },
                          {
                              "FOUR BRIDGES, ONE DROWNING",
                              "MOAT, TEN NAMES ON THE SLATE.",
                              "FREE FOR ALL: TEAM COLORS DO",
                              "NOT RESTRICT YOUR TARGETS.",
                              "MIND THE PLANKS - THE WATER",
                              "KEEPS NO SCORE.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ffa_row(852, "FFA: SHARDS", 8, 44, 44, 12, 36,
                          {
                              {FAMILY_SPEED_POTION, {21, 21}},
                              {FAMILY_SPEED_POTION, {22, 22}},
                              {FAMILY_DRUMSTICK, {18, 18}},
                              {FAMILY_DRUMSTICK, {25, 25}},
                              {FAMILY_DRUMSTICK, {18, 25}},
                              {FAMILY_DRUMSTICK, {25, 18}},
                              {FAMILY_DRUMSTICK, {7, 7}},
                              {FAMILY_DRUMSTICK, {36, 7}},
                              {FAMILY_DRUMSTICK, {7, 36}},
                              {FAMILY_DRUMSTICK, {36, 36}},
                          },
                          {
                              "THE HALL BROKE LONG AGO AND",
                              "NOBODY SWEPT IT. TWELVE FIGHT",
                              "AMONG THE SHARDS.",
                              "FREE FOR ALL: TEAM CHOICE",
                              "RESTRICTS NO TARGET. TRUST THE",
                              "WALLS, NEVER THE COMPANY.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ffa_row(853, "FFA: THE ROSE", 10, 45, 45, 16, 4,
                          {
                              {FAMILY_SPEED_POTION, {21, 21}},
                              {FAMILY_SPEED_POTION, {22, 22}},
                              {FAMILY_DRUMSTICK, {28, 8}},
                              {FAMILY_DRUMSTICK, {36, 16}},
                              {FAMILY_DRUMSTICK, {36, 28}},
                              {FAMILY_DRUMSTICK, {28, 36}},
                              {FAMILY_DRUMSTICK, {16, 36}},
                              {FAMILY_DRUMSTICK, {8, 28}},
                              {FAMILY_DRUMSTICK, {8, 16}},
                              {FAMILY_DRUMSTICK, {16, 8}},
                          },
                          {
                              "EIGHT PETALS, ONE HEART, AND",
                              "SIXTEEN THORNS.",
                              "FREE FOR ALL: YOUR TEAM COLOR",
                              "NAMES NO ALLY. THE HEART FEEDS",
                              "WHOEVER HOLDS IT, AND HOLDING",
                              "IT IS THE HARD PART.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ffa_row(854, "FFA: SCRAMBLE", 10, 48, 48, 16, 16,
                          {
                              {FAMILY_SPEED_POTION, {16, 24}},
                              {FAMILY_SPEED_POTION, {31, 24}},
                              {FAMILY_DRUMSTICK, {15, 23}},
                              {FAMILY_DRUMSTICK, {17, 25}},
                              {FAMILY_DRUMSTICK, {30, 23}},
                              {FAMILY_DRUMSTICK, {32, 25}},
                              {FAMILY_DRUMSTICK, {23, 6}},
                              {FAMILY_DRUMSTICK, {23, 41}},
                              {FAMILY_DRUMSTICK, {6, 23}},
                              {FAMILY_DRUMSTICK, {41, 23}},
                          },
                          {
                              "NO LINES, NO RANKS, NO FRIENDS",
                              "- JUST COVER AND A COUNT.",
                              "FREE FOR ALL: TEAM CHOICE DOES",
                              "NOT RESTRICT YOUR TARGETS.",
                              "TWIN PADS FEED THE BOLD AND",
                              "SIXTEEN COME TO CLAIM THEM.",
                              "-- THE GAMESMASTER",
                          }));

    out.push_back(ffa_row(855, "FFA: NIGHTFALL", 12, 50, 50, 16, 36,
                          {
                              {FAMILY_SPEED_POTION, {24, 24}},
                              {FAMILY_SPEED_POTION, {25, 25}},
                              {FAMILY_DRUMSTICK, {18, 18}},
                              {FAMILY_DRUMSTICK, {31, 31}},
                              {FAMILY_DRUMSTICK, {18, 31}},
                              {FAMILY_DRUMSTICK, {31, 18}},
                              {FAMILY_DRUMSTICK, {24, 11}},
                              {FAMILY_DRUMSTICK, {24, 38}},
                              {FAMILY_DRUMSTICK, {11, 24}},
                              {FAMILY_DRUMSTICK, {38, 24}},
                          },
                          {
                              "THE TORCHES ARE LIT AND THE",
                              "LOOP IS LONG. SIXTEEN WALK IT",
                              "TONIGHT, ALL OF THEM HUNTING.",
                              "FREE FOR ALL: TEAM COLOR BUYS",
                              "NO TRUCE. WHAT STANDS BEHIND",
                              "YOU IS A TARGET, NOT A FRIEND.",
                              "-- THE GAMESMASTER",
                          }));

    return out;
}

void build_ffa()
{
    const std::vector<ExpectedLevel> rows = ffa_expectations();
    build_the_melee(rows[0]);
    build_crossfire(rows[1]);
    build_shards(rows[2]);
    build_the_rose(rows[3]);
    build_scramble(rows[4]);
    build_nightfall(rows[5]);
}

} // namespace modesgen
