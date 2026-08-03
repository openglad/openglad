/* Multiplayer Game Modes campaign generator — Onslaught (800-803).
 *
 * New maps for the generator-warfare mode: per-class generators in
 * defensible positions, waypoint posts (treasure family 14) for the
 * spawn-rate objective, and obmap budgets sized by the §2.3 ledger. The
 * mode Lua (flip-on-lethal-hit, spawn caps, elimination) ships with the
 * Lua-mode wave; the maps carry no mode-specific entities beyond the
 * standard vocabulary.
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
#include <openglad/core/ctf_constants.h>
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

// ---------------------------------------------------------------------------
// 800 FOUNDRY LINE — 50x35, 2 teams. The teaching map: each team's four
// generators (one per family) sit in walled alcoves with one gate tile;
// two open lanes flank a walled center yard holding the waypoint.
// ---------------------------------------------------------------------------
void build_foundry_line(const ExpectedLevel& row)
{
    LevelRuntimeData level(800, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(50, 35);

    // Perimeter.
    c.rect(0, 0, 49, 0, PIX_H_WALL1);
    c.rect(0, 34, 49, 34, PIX_H_WALL1);
    c.vline(0, 0, 34, PIX_H_WALL1);
    c.vline(49, 0, 34, PIX_H_WALL1);

    // Foundry alcoves: west column x2..7 (team 0), east x42..47 (team 1),
    // one 4x4 cobble cell per generator (the tower sprite needs the full
    // 4x4-tile footprint), gate on the field side.
    for (const int r : {3, 12, 21, 30})
    {
        c.hline(2, 7, r - 1, PIX_H_WALL1);
        c.hline(2, 7, r + 4, PIX_H_WALL1);
        c.vline(2, r - 1, r + 4, PIX_H_WALL1);
        c.vline(7, r - 1, r + 4, PIX_H_WALL1);
        c.cobble_rect(3, r, 6, r + 3);
        c.set(7, r + 1, Canvas::cobble(7, r + 1)); // gate
        c.set_decor(3, r - 1, DECOR_TORCH1);
        c.set_decor(6, r - 1, DECOR_TORCH1);

        c.hline(42, 47, r - 1, PIX_H_WALL1);
        c.hline(42, 47, r + 4, PIX_H_WALL1);
        c.vline(42, r - 1, r + 4, PIX_H_WALL1);
        c.vline(47, r - 1, r + 4, PIX_H_WALL1);
        c.cobble_rect(43, r, 46, r + 3);
        c.set(42, r + 1, Canvas::cobble(42, r + 1)); // gate
        c.set_decor(43, r - 1, DECOR_TORCH1);
        c.set_decor(46, r - 1, DECOR_TORCH1);
    }

    // Center yard with four mouths; the lanes run north and south of it.
    c.hline(18, 31, 11, PIX_H_WALL1);
    c.hline(18, 31, 23, PIX_H_WALL1);
    c.vline(18, 11, 23, PIX_H_WALL1);
    c.vline(31, 11, 23, PIX_H_WALL1);
    c.cobble_rect(19, 12, 30, 22);
    for (int x = 23; x <= 26; ++x)
    {
        c.set(x, 11, Canvas::cobble(x, 11));
        c.set(x, 23, Canvas::cobble(x, 23));
    }
    for (int y = 15; y <= 19; ++y)
    {
        c.set(18, y, Canvas::cobble(18, y));
        c.set(31, y, Canvas::cobble(31, y));
    }
    c.set_decor(18, 11, DECOR_TORCH1);
    c.set_decor(31, 11, DECOR_TORCH1);
    c.set_decor(18, 23, DECOR_TORCH1);
    c.set_decor(31, 23, DECOR_TORCH1);

    // Door-sealed treasure pockets north and south of the yard.
    c.vline(22, 1, 4, PIX_H_WALL1);
    c.vline(27, 1, 4, PIX_H_WALL1);
    c.hline(22, 27, 4, PIX_H_WALL1);
    c.set(24, 4, Canvas::grass(24, 4));
    c.set(25, 4, Canvas::grass(25, 4));
    c.vline(22, 30, 33, PIX_H_WALL1);
    c.vline(27, 30, 33, PIX_H_WALL1);
    c.hline(22, 27, 30, PIX_H_WALL1);
    c.set(24, 30, Canvas::grass(24, 30));
    c.set(25, 30, Canvas::grass(25, 30));

    install_painted(world, c.finish());

    // Generators: one of each family per team, top to bottom, anchored at
    // the alcove interior's top-left so every sprite footprint fits.
    const int families[] = {FAMILY_TENT, FAMILY_TOWER, FAMILY_BONES,
                            FAMILY_TREEHOUSE};
    for (int i = 0; i < 4; ++i)
    {
        const short r = static_cast<short>(3 + 9 * i);
        place_at(world, Order::Generator, families[i], 0, {3, r}, 2);
        place_at(world, Order::Generator, families[i], 1, {43, r}, 2);
    }

    place_markers(world, 0,
                  {{10, 17}, {9, 8}, {11, 8}, {9, 13}, {11, 13}, {9, 21},
                   {11, 21}, {9, 26}, {11, 26}, {12, 11}, {12, 17},
                   {12, 23}});
    place_markers(world, 1,
                  {{39, 17}, {40, 8}, {38, 8}, {40, 13}, {38, 13}, {40, 21},
                   {38, 21}, {40, 26}, {38, 26}, {37, 11}, {37, 17},
                   {37, 23}});

    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {24, 17});

    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {24, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {25, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {24, 30});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {25, 30});

    for (const TilePos t : {TilePos{23, 2}, TilePos{23, 32}})
        place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, t);
    for (const TilePos t : {TilePos{26, 2}, TilePos{26, 32}})
        place_at(world, Order::Treasure, FAMILY_INVIS_POTION, 0, t);
    for (const TilePos t :
         {TilePos{19, 12}, TilePos{30, 12}, TilePos{19, 22}, TilePos{30, 22},
          TilePos{12, 5}, TilePos{37, 5}, TilePos{12, 29}, TilePos{37, 29}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);
    for (const TilePos t :
         {TilePos{24, 13}, TilePos{25, 21}, TilePos{8, 17}, TilePos{41, 17}})
        place_at(world, Order::Treasure, FAMILY_GOLD_BAR, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 801 TWIN SPIRES — 60x60, 2 teams mirrored across a bridged river. Each
// side: a walled spire compound (2 towers, 2 hold-post guards), an outer
// camp (2 tents), and woods pockets (bones, treehouse in shrub cover).
// Waypoints on the two bridges.
// ---------------------------------------------------------------------------
void build_twin_spires(const ExpectedLevel& row)
{
    LevelRuntimeData level(801, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(60, 60);

    c.rect(0, 0, 59, 0, PIX_H_WALL1);
    c.rect(0, 59, 59, 59, PIX_H_WALL1);
    c.vline(0, 0, 59, PIX_H_WALL1);
    c.vline(59, 0, 59, PIX_H_WALL1);

    // The river and its two plank bridges.
    c.water_rect(1, 29, 58, 31);
    for (int y = 29; y <= 31; ++y)
        for (const int x : {14, 15, 16, 43, 44, 45})
            c.set(x, y, ((x + y) % 2) ? PIX_PATH_2 : PIX_PATH_1);

    // Spire compounds (north = team 0, south mirrored).
    for (const int top : {6, 44})
    {
        c.hline(24, 35, top, PIX_H_WALL1);
        c.hline(24, 35, top + 9, PIX_H_WALL1);
        c.vline(24, top, top + 9, PIX_H_WALL1);
        c.vline(35, top, top + 9, PIX_H_WALL1);
        c.cobble_rect(25, top + 1, 34, top + 8);
        const int gate_row = (top == 6) ? top + 9 : top; // face the river
        c.set(29, gate_row, Canvas::cobble(29, gate_row));
        c.set(30, gate_row, Canvas::cobble(30, gate_row));
        c.set_decor(26, top, DECOR_TORCH1);
        c.set_decor(33, top, DECOR_TORCH1);
    }

    // Woods pockets: tree rings with an east/west approach, shrub cover.
    const struct { int cx; int cy; } woods[] = {
        {5, 8}, {54, 8}, {5, 51}, {54, 51}};
    for (const auto& wd : woods)
    {
        for (const int dx : {-2, 0, 2})
        {
            c.set(wd.cx + dx, wd.cy - 3, Canvas::tree(wd.cx + dx, wd.cy - 3));
            c.set(wd.cx + dx, wd.cy + 3, Canvas::tree(wd.cx + dx, wd.cy + 3));
        }
        c.set(wd.cx - 3, wd.cy - 1, Canvas::tree(wd.cx - 3, wd.cy - 1));
        c.set(wd.cx - 3, wd.cy + 1, Canvas::tree(wd.cx - 3, wd.cy + 1));
        c.set_decor(wd.cx - 1, wd.cy - 2, DECOR_SHRUB);
        c.set_decor(wd.cx + 1, wd.cy + 2, DECOR_SHRUB);
        c.set_decor(wd.cx - 2, wd.cy, DECOR_SHRUB);
    }

    install_painted(world, c.finish());

    // Team 0 (north): 2 towers + guards in the keep, 2 tents in camp,
    // bones NW / treehouse NE in the woods.
    place_at(world, Order::Generator, FAMILY_TOWER, 0, {26, 8}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 0, {31, 8}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 0, {12, 21}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 0, {46, 21}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 0, {4, 7}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 0, {53, 7}, 2);
    // Team 1 (south, mirrored; towers anchored so their 4-tile-tall
    // sprite footprint mirrors the north pair's rows exactly).
    place_at(world, Order::Generator, FAMILY_TOWER, 1, {26, 48}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 1, {31, 48}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {12, 37}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {46, 37}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 1, {4, 51}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 1, {53, 51}, 2);

    // Compound guards: hold-post soldiers (allied-guard rule).
    for (const TilePos t : {TilePos{28, 12}, TilePos{31, 12}})
    {
        walker* guard = place_at(world, Order::Living, FAMILY_SOLDIER, 0, t, 3);
        if (guard != nullptr)
        {
            guard->set_act_type(ACT_GUARD);
            guard->set_guard_hold_post(true);
        }
    }
    for (const TilePos t : {TilePos{28, 47}, TilePos{31, 47}})
    {
        walker* guard = place_at(world, Order::Living, FAMILY_SOLDIER, 1, t, 3);
        if (guard != nullptr)
        {
            guard->set_act_type(ACT_GUARD);
            guard->set_guard_hold_post(true);
        }
    }

    place_markers(world, 0,
                  {{29, 18}, {25, 17}, {34, 17}, {22, 19}, {37, 19}, {25, 21},
                   {34, 21}, {20, 23}, {39, 23}, {27, 23}, {32, 23},
                   {29, 25}});
    place_markers(world, 1,
                  {{29, 41}, {25, 42}, {34, 42}, {22, 40}, {37, 40}, {25, 38},
                   {34, 38}, {20, 36}, {39, 36}, {27, 36}, {32, 36},
                   {29, 34}});

    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {15, 30});
    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {44, 30});

    for (const TilePos t :
         {TilePos{8, 26}, TilePos{20, 26}, TilePos{40, 26}, TilePos{51, 26},
          TilePos{8, 33}, TilePos{20, 33}, TilePos{40, 33}, TilePos{51, 33},
          TilePos{29, 4}, TilePos{29, 55}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);
    for (const TilePos t :
         {TilePos{13, 23}, TilePos{46, 23}, TilePos{13, 36}, TilePos{46, 36},
          TilePos{30, 12}, TilePos{30, 47}})
        place_at(world, Order::Treasure, FAMILY_GOLD_BAR, 0, t);
    place_at(world, Order::Treasure, FAMILY_FLIGHT_POTION, 0, {3, 27});
    place_at(world, Order::Treasure, FAMILY_FLIGHT_POTION, 0, {56, 32});
    place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, {18, 27});
    place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, {41, 32});

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 802 THE MARCHES — 80x60, 3 teams across T-shaped rivers with fords.
// Every team's four generators are spread over its territory; a hill
// saddle between the river arms carries the lone waypoint.
// ---------------------------------------------------------------------------
void build_the_marches(const ExpectedLevel& row)
{
    LevelRuntimeData level(802, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(80, 60);

    c.rect(0, 0, 79, 0, PIX_H_WALL1);
    c.rect(0, 59, 79, 59, PIX_H_WALL1);
    c.vline(0, 0, 59, PIX_H_WALL1);
    c.vline(79, 0, 59, PIX_H_WALL1);

    // Border-country mud: deterministic dirt speckle.
    for (int y = 1; y < 59; ++y)
        for (int x = 1; x < 79; ++x)
            if ((x * 11 + y * 17) % 23 == 0)
                c.set(x, y, PIX_DIRT_1);

    // The vertical arm (NW/NE border) with two fords, and the horizontal
    // river (north/south border) with four.
    c.water_rect(39, 1, 41, 33);
    c.water_rect(1, 38, 78, 40);
    for (int x = 39; x <= 41; ++x)
        for (const int y : {10, 11, 24, 25})
            c.set(x, y, ((x + y) % 2) ? PIX_PATH_2 : PIX_PATH_1);
    for (int y = 38; y <= 40; ++y)
        for (const int x : {15, 16, 30, 31, 48, 49, 63, 64})
            c.set(x, y, ((x + y) % 2) ? PIX_PATH_2 : PIX_PATH_1);

    // The hill saddle between the arms: cobble mound, waypoint on top.
    c.cobble_disc(80, 72, 5); // center (40, 36), radius 2.5

    // Door-sealed treasure pockets.
    c.vline(18, 1, 4, PIX_H_WALL1);
    c.vline(23, 1, 4, PIX_H_WALL1);
    c.hline(18, 23, 4, PIX_H_WALL1);
    c.set(20, 4, Canvas::grass(20, 4));
    c.set(21, 4, Canvas::grass(21, 4));
    c.vline(56, 1, 4, PIX_H_WALL1);
    c.vline(61, 1, 4, PIX_H_WALL1);
    c.hline(56, 61, 4, PIX_H_WALL1);
    c.set(58, 4, Canvas::grass(58, 4));
    c.set(59, 4, Canvas::grass(59, 4));
    c.vline(37, 55, 58, PIX_H_WALL1);
    c.vline(42, 55, 58, PIX_H_WALL1);
    c.hline(37, 42, 55, PIX_H_WALL1);
    c.set(39, 55, Canvas::grass(39, 55));
    c.set(40, 55, Canvas::grass(40, 55));

    install_painted(world, c.finish());

    // Spread generators: one per family per team, one per quadrant of the
    // team's third.
    place_at(world, Order::Generator, FAMILY_TENT, 0, {8, 6}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 0, {28, 6}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 0, {8, 28}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 0, {28, 28}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {70, 6}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 1, {50, 6}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 1, {70, 28}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 1, {50, 28}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 2, {12, 50}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 2, {66, 50}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 2, {30, 52}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 2, {48, 52}, 2);

    place_markers(world, 0,
                  {{19, 18}, {16, 15}, {22, 15}, {14, 18}, {24, 18}, {16, 21},
                   {22, 21}, {19, 14}, {19, 22}, {12, 16}, {26, 16},
                   {19, 25}});
    place_markers(world, 1,
                  {{60, 18}, {63, 15}, {57, 15}, {65, 18}, {55, 18}, {63, 21},
                   {57, 21}, {60, 14}, {60, 22}, {67, 16}, {53, 16},
                   {60, 25}});
    place_markers(world, 2,
                  {{40, 48}, {36, 46}, {44, 46}, {34, 49}, {46, 49}, {36, 52},
                   {44, 52}, {40, 44}, {32, 47}, {48, 47}, {38, 53},
                   {42, 53}});

    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {40, 36});

    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {20, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {21, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {58, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {59, 4});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {39, 55});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {40, 55});

    for (const TilePos t :
         {TilePos{19, 2}, TilePos{22, 2}, TilePos{57, 2}, TilePos{38, 57}})
        place_at(world, Order::Treasure, FAMILY_GOLD_BAR, 0, t);
    place_at(world, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, {60, 2});
    place_at(world, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, {41, 57});
    for (const TilePos t :
         {TilePos{37, 10}, TilePos{43, 24}, TilePos{15, 36}, TilePos{31, 43},
          TilePos{48, 36}, TilePos{64, 43}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);
    for (const TilePos t : {TilePos{5, 20}, TilePos{74, 20}, TilePos{40, 46}})
        place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, t);
    for (const TilePos t :
         {TilePos{37, 33}, TilePos{43, 33}, TilePos{40, 54}})
        place_at(world, Order::Treasure, FAMILY_FLIGHT_POTION, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 803 LAST BASTION — 70x70, 2 teams + a neutral bailey. Team estates line
// the west and east walls behind commitment corridors; the door-gated
// bailey pumps hostile ghosts until captured; waypoints on the ring road.
// ---------------------------------------------------------------------------
void build_last_bastion(const ExpectedLevel& row)
{
    LevelRuntimeData level(803, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(70, 70);

    c.rect(0, 0, 69, 0, PIX_H_WALL1);
    c.rect(0, 69, 69, 69, PIX_H_WALL1);
    c.vline(0, 0, 69, PIX_H_WALL1);
    c.vline(69, 0, 69, PIX_H_WALL1);

    // Ring road.
    c.cobble_rect(20, 20, 49, 23);
    c.cobble_rect(20, 46, 49, 49);
    c.cobble_rect(20, 24, 23, 45);
    c.cobble_rect(46, 24, 49, 45);

    // The bailey: wall ring, cobble floor, four gated entrances (the north
    // mouth is three tiles with one left doorless so the center is always
    // reachable on foot).
    c.hline(25, 44, 25, PIX_H_WALL1);
    c.hline(25, 44, 44, PIX_H_WALL1);
    c.vline(25, 25, 44, PIX_H_WALL1);
    c.vline(44, 25, 44, PIX_H_WALL1);
    c.cobble_rect(26, 26, 43, 43);
    for (const int x : {33, 34, 35})
        c.set(x, 25, Canvas::cobble(x, 25)); // north mouth
    for (const int x : {34, 35})
        c.set(x, 44, Canvas::cobble(x, 44)); // south mouth
    for (const int y : {34, 35})
    {
        c.set(25, y, Canvas::cobble(25, y)); // west mouth
        c.set(44, y, Canvas::cobble(44, y)); // east mouth
    }
    c.set_decor(27, 25, DECOR_TORCH1);
    c.set_decor(42, 25, DECOR_TORCH1);
    c.set_decor(27, 44, DECOR_TORCH1);
    c.set_decor(42, 44, DECOR_TORCH1);
    c.set_decor(30, 30, DECOR_BONES);
    c.set_decor(39, 39, DECOR_BONES);

    // Estate corridors: a long wall with commitment gaps in front of each
    // team's generator column.
    for (const int x : {11, 58})
    {
        c.vline(x, 8, 62, PIX_H_WALL1);
        for (const int gap : {16, 30, 44, 56})
            for (int y = gap; y <= gap + 2; ++y)
                c.set(x, y, Canvas::grass(x, y));
    }

    install_painted(world, c.finish());

    // Estates: five generators per team along the wall columns.
    place_at(world, Order::Generator, FAMILY_TENT, 0, {6, 12}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 0, {6, 26}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 0, {6, 38}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 0, {6, 50}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 0, {6, 59}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {62, 12}, 2);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {62, 26}, 2);
    place_at(world, Order::Generator, FAMILY_TOWER, 1, {62, 38}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 1, {62, 50}, 2);
    place_at(world, Order::Generator, FAMILY_TREEHOUSE, 1, {62, 59}, 2);

    // The haunted bailey engines (neutral team 7).
    place_at(world, Order::Generator, FAMILY_BONES, 7, {31, 33}, 2);
    place_at(world, Order::Generator, FAMILY_BONES, 7, {37, 35}, 2);

    place_markers(world, 0,
                  {{15, 35}, {14, 20}, {16, 20}, {14, 27}, {16, 27}, {14, 31},
                   {16, 31}, {14, 39}, {16, 39}, {14, 43}, {16, 43},
                   {15, 49}});
    place_markers(world, 1,
                  {{54, 35}, {55, 20}, {53, 20}, {55, 27}, {53, 27}, {55, 31},
                   {53, 31}, {55, 39}, {53, 39}, {55, 43}, {53, 43},
                   {54, 49}});

    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {34, 21});
    place_at(world, Order::Treasure, og::FAMILY_CTF_POINT, 7, {35, 48});

    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {33, 25});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {34, 25});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {34, 44});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {35, 44});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {25, 34});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {25, 35});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {44, 34});
    place_at(world, Order::Weapon, FAMILY_DOOR, 4, {44, 35});

    for (const TilePos t :
         {TilePos{22, 21}, TilePos{47, 21}, TilePos{22, 48}, TilePos{47, 48},
          TilePos{13, 19}, TilePos{56, 19}, TilePos{13, 50}, TilePos{56, 50}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);
    for (const TilePos t :
         {TilePos{28, 28}, TilePos{41, 28}, TilePos{28, 41}, TilePos{41, 41},
          TilePos{34, 34}, TilePos{35, 40}})
        place_at(world, Order::Treasure, FAMILY_GOLD_BAR, 0, t);
    for (const TilePos t :
         {TilePos{17, 17}, TilePos{52, 17}, TilePos{17, 52}, TilePos{52, 52}})
        place_at(world, Order::Treasure, FAMILY_SPEED_POTION, 0, t);
    place_at(world, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, {2, 35});
    place_at(world, Order::Treasure, FAMILY_INVULNERABLE_POTION, 0, {67, 35});

    emit_painted(world, row);
}

ExpectedLevel ons_row(int id, const char* title, int par, int w, int h,
                      int teams, int cps, int doors, int treasures,
                      int livings, std::vector<SpawnCap> caps,
                      std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Onslaught;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = teams;
    row.markers_per_team = 12;
    row.control_points = cps;
    row.doors = doors;
    row.treasures = treasures;
    row.authored_livings = livings;
    row.spawn_caps = std::move(caps);
    row.time_limit = 14400;
    row.score_limit = 0; // elimination decides
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> onslaught_expectations()
{
    std::vector<ExpectedLevel> out;

    ExpectedLevel foundry = ons_row(
        800, "Onslaught: FOUNDRY LINE", 8, 50, 35, 2, 1, 4, 16, 0,
        {{0, 24}, {1, 24}},
        {
            "THE FOUNDRY GAME, CONTENDERS.",
            "YOUR ENGINES POUR OUT SOLDIERS;",
            "BREAK AN ENEMY ENGINE AND IT",
            "JOINS YOUR LINE, STILL WARM.",
            "LOSE YOUR LAST AND THE BOOK",
            "CLOSES ON YOU. GUARD THE ROWS.",
            "THE YARD POST QUICKENS YOUR",
            "REINFORCEMENTS.",
            "-- THE GAMESMASTER",
        });
    foundry.generators_per_team[0] = 4;
    foundry.generators_per_team[1] = 4;
    foundry.decor_cells = 20;
    out.push_back(std::move(foundry));

    ExpectedLevel spires = ons_row(
        801, "Onslaught: TWIN SPIRES", 10, 60, 60, 2, 2, 0, 20, 4,
        {{0, 20}, {1, 20}},
        {
            "TWO SPIRE COMPOUNDS, ONE",
            "RIVER. YOUR ENGINES HIDE IN",
            "KEEP AND CAMP AND WOOD; BREAK",
            "THEIRS AND THE SPOILS ENLIST.",
            "THE BRIDGE POSTS QUICKEN YOUR",
            "LINES. LOSE EVERY ENGINE AND",
            "THE BOOK CLOSES ON YOU.",
            "-- THE GAMESMASTER",
        });
    spires.generators_per_team[0] = 6;
    spires.generators_per_team[1] = 6;
    spires.decor_cells = 16;
    out.push_back(std::move(spires));

    ExpectedLevel marches = ons_row(
        802, "Onslaught: THE MARCHES", 12, 80, 60, 3, 1, 6, 18, 0,
        {{0, 14}, {1, 14}, {2, 14}},
        {
            "THREE BANDS IN THE MUD, AND",
            "EVERY ENGINE SPREAD THIN ON",
            "BORDER COUNTRY. THE FORDS ARE",
            "FEW AND THE HILL POST WATCHES",
            "THEM ALL. GANG THE LEADER,",
            "CONTENDERS - THE MARCHES",
            "FORGIVE NO CROWNS.",
            "-- THE GAMESMASTER",
        });
    marches.generators_per_team[0] = 4;
    marches.generators_per_team[1] = 4;
    marches.generators_per_team[2] = 4;
    marches.decor_cells = 0;
    out.push_back(std::move(marches));

    ExpectedLevel bastion = ons_row(
        803, "Onslaught: LAST BASTION", 12, 70, 70, 2, 2, 8, 20, 0,
        {{0, 18}, {1, 18}, {7, 8}},
        {
            "THE LAST BASTION, CONTENDERS.",
            "YOUR ESTATES RING THE WALLS;",
            "THE BAILEY HOLDS TWO HAUNTED",
            "ENGINES POURING GHOSTS ON ALL.",
            "BREAK ITS DOORS, FLIP THE",
            "BONES, AND THE CENTER FIGHTS",
            "FOR YOU. LAST LINE STANDING",
            "TAKES THE PURSE.",
            "-- THE GAMESMASTER",
        });
    bastion.generators_per_team[0] = 5;
    bastion.generators_per_team[1] = 5;
    bastion.generators_per_team[7] = 2;
    bastion.decor_cells = 6;
    out.push_back(std::move(bastion));

    return out;
}

void build_onslaught()
{
    const std::vector<ExpectedLevel> rows = onslaught_expectations();
    build_foundry_line(rows[0]);
    build_twin_spires(rows[1]);
    build_the_marches(rows[2]);
    build_last_bastion(rows[3]);
}

} // namespace modesgen
