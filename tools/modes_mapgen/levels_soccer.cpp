/* Multiplayer Game Modes campaign generator — Soccer (820-823).
 *
 * Pitch grammar (all four): a CLOSED impassable perimeter (ball and
 * players can never leave — the self-check flood-fills the border), a
 * grass field with a cobble center circle and halfway line, and 8x2-tile
 * carpet goal strips recessed into the end walls. Goals are TERRAIN —
 * the manifest carries the matching pixel rects and the kickoff point;
 * the ball is a pack family the mode Lua spawns at kickoff, never
 * authored here.
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

// West/east pitch shell shared by 820/821/823: border ring, two-deep end
// walls whose middle rows are the carpet goal recesses, grass field,
// cobble halfway columns + center circle, and column-decor goal posts.
void paint_pitch_we(Canvas& c, int goal_y0, int goal_y1)
{
    const int w = c.w();
    const int h = c.h();
    c.rect(0, 0, w - 1, 0, PIX_H_WALL1);
    c.rect(0, h - 1, w - 1, h - 1, PIX_H_WALL1);
    for (const int x : {0, 1, 2, w - 3, w - 2, w - 1})
        c.vline(x, 0, h - 1, PIX_H_WALL1);
    c.grass_rect(3, 1, w - 4, h - 2);
    c.carpet_rect(1, goal_y0, 2, goal_y1);
    c.carpet_rect(w - 3, goal_y0, w - 2, goal_y1);
    const int mid_left = w / 2 - 1;
    for (const int x : {mid_left, mid_left + 1})
        for (int y = 1; y < h - 1; ++y)
            c.set(x, y, Canvas::cobble(x, y));
    c.cobble_disc(w - 1, h - 1, 7); // center (w-1)/2, (h-1)/2, radius 3.5
    c.set_decor(3, goal_y0 - 1, DECOR_COLUMN_BOTTOM);
    c.set_decor(3, goal_y1 + 1, DECOR_COLUMN_BOTTOM);
    c.set_decor(w - 4, goal_y0 - 1, DECOR_COLUMN_BOTTOM);
    c.set_decor(w - 4, goal_y1 + 1, DECOR_COLUMN_BOTTOM);
}

// The 1-4-4-3 kickoff formation for a west-side team, lead = center
// striker first (deploy order = oblist order).
std::vector<TilePos> formation_west(short lead_x, short mid_y,
                                    short back_x, short mid_x,
                                    short keeper_x,
                                    const std::array<short, 4>& rows)
{
    std::vector<TilePos> out;
    out.push_back({lead_x, mid_y});
    out.push_back({static_cast<short>(lead_x - 1),
                   static_cast<short>(mid_y - 4)});
    out.push_back({static_cast<short>(lead_x - 1),
                   static_cast<short>(mid_y + 5)});
    for (const short y : rows)
        out.push_back({mid_x, y});
    for (const short y : rows)
        out.push_back({back_x, y});
    out.push_back({keeper_x, mid_y});
    return out;
}

std::vector<TilePos> mirror_x(const std::vector<TilePos>& pts, int w)
{
    std::vector<TilePos> out;
    for (const TilePos& p : pts)
        out.push_back({static_cast<short>(w - 1 - p.tx), p.ty});
    return out;
}

// ---------------------------------------------------------------------------
// 820 THE PITCH — 44x28, 2 teams. The reference pitch.
// ---------------------------------------------------------------------------
void build_the_pitch(const ExpectedLevel& row)
{
    LevelRuntimeData level(820, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(44, 28);
    paint_pitch_we(c, 10, 17);
    install_painted(world, c.finish());

    const std::vector<TilePos> west =
        formation_west(19, 13, 8, 13, 5, {5, 10, 17, 22});
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 44));

    for (const TilePos t :
         {TilePos{10, 2}, TilePos{17, 2}, TilePos{26, 2}, TilePos{33, 2},
          TilePos{10, 25}, TilePos{17, 25}, TilePos{26, 25}, TilePos{33, 25}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 821 THE MUDBOWL — 50x30, 2 teams; two water pools flank mid-field.
// ---------------------------------------------------------------------------
void build_the_mudbowl(const ExpectedLevel& row)
{
    LevelRuntimeData level(821, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(50, 30);
    paint_pitch_we(c, 11, 18);
    c.water_rect(20, 5, 23, 7);
    c.water_rect(26, 22, 29, 24);
    install_painted(world, c.finish());

    const std::vector<TilePos> west =
        formation_west(21, 14, 9, 15, 5, {6, 11, 18, 23});
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 50));

    for (const TilePos t :
         {TilePos{10, 2}, TilePos{20, 2}, TilePos{30, 2}, TilePos{40, 2},
          TilePos{10, 27}, TilePos{20, 27}, TilePos{30, 27}, TilePos{40, 27}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);
    place_at(world, Order::Treasure, FAMILY_FLIGHT_POTION, 0, {19, 6});
    place_at(world, Order::Treasure, FAMILY_FLIGHT_POTION, 0, {30, 23});

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 822 FOURSQUARE — 40x40, 4 teams, a goal in every wall; score into ANY
// rival goal. Team k defends wall k (0 N, 1 E, 2 S, 3 W).
// ---------------------------------------------------------------------------
void build_foursquare(const ExpectedLevel& row)
{
    LevelRuntimeData level(822, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(40, 40);

    for (const int ring : {0, 1, 2})
    {
        c.hline(ring, 39 - ring, ring, PIX_H_WALL1);
        c.hline(ring, 39 - ring, 39 - ring, PIX_H_WALL1);
        c.vline(ring, ring, 39 - ring, PIX_H_WALL1);
        c.vline(39 - ring, ring, 39 - ring, PIX_H_WALL1);
    }
    c.grass_rect(3, 3, 36, 36);
    c.carpet_rect(16, 1, 23, 2);   // N (team 0)
    c.carpet_rect(37, 16, 38, 23); // E (team 1)
    c.carpet_rect(16, 37, 23, 38); // S (team 2)
    c.carpet_rect(1, 16, 2, 23);   // W (team 3)
    c.cobble_rect(3, 19, 36, 20);
    c.cobble_rect(19, 3, 20, 36);
    c.cobble_disc(39, 39, 6); // center circle
    // Goal posts at every mouth's corners.
    for (const auto& post :
         {std::pair{15, 3}, std::pair{24, 3}, std::pair{36, 15},
          std::pair{36, 24}, std::pair{15, 36}, std::pair{24, 36},
          std::pair{3, 15}, std::pair{3, 24}})
        c.set_decor(post.first, post.second, DECOR_COLUMN_BOTTOM);
    install_painted(world, c.finish());

    // North team formation, then rotated 90 degrees per team:
    // (x, y) -> (39 - y, x) maps the north wall onto the east wall.
    std::vector<TilePos> pts = {
        {19, 16}, {15, 14}, {24, 14},                     // strikers, lead first
        {12, 11}, {17, 11}, {22, 11}, {27, 11},           // mids
        {12, 7}, {17, 7}, {22, 7}, {27, 7},               // backs
        {19, 4},                                          // keeper
    };
    for (int team = 0; team < 4; ++team)
    {
        place_markers(world, team, pts);
        for (TilePos& p : pts)
            p = {static_cast<short>(39 - p.ty), p.tx};
    }

    for (const TilePos t :
         {TilePos{8, 8}, TilePos{31, 8}, TilePos{8, 31}, TilePos{31, 31},
          TilePos{8, 19}, TilePos{31, 19}, TilePos{19, 8}, TilePos{19, 31}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

// ---------------------------------------------------------------------------
// 823 BONEYARD CUP — 46x30, 2 teams; each side's walled alcove tent
// raises skeleton teammates behind its own goal.
// ---------------------------------------------------------------------------
void build_boneyard_cup(const ExpectedLevel& row)
{
    LevelRuntimeData level(823, true, &headless_level_data_hooks());
    GameWorld& world = level.world();
    Canvas c(46, 30);
    paint_pitch_we(c, 11, 18);

    // Generator alcoves in the north corners, mouth toward the field.
    c.hline(3, 7, 1, PIX_H_WALL1);
    c.hline(3, 7, 5, PIX_H_WALL1);
    c.vline(3, 1, 5, PIX_H_WALL1);
    c.vline(7, 1, 5, PIX_H_WALL1);
    c.cobble_rect(4, 2, 6, 4);
    c.set(7, 3, Canvas::cobble(7, 3));
    c.hline(38, 42, 1, PIX_H_WALL1);
    c.hline(38, 42, 5, PIX_H_WALL1);
    c.vline(38, 1, 5, PIX_H_WALL1);
    c.vline(42, 1, 5, PIX_H_WALL1);
    c.cobble_rect(39, 2, 41, 4);
    c.set(38, 3, Canvas::cobble(38, 3));
    c.set_decor(8, 2, DECOR_BONES);
    c.set_decor(37, 2, DECOR_BONES);
    c.set_decor(9, 27, DECOR_BONES);
    c.set_decor(36, 27, DECOR_BONES);
    install_painted(world, c.finish());

    place_at(world, Order::Generator, FAMILY_TENT, 0, {4, 2}, 1);
    place_at(world, Order::Generator, FAMILY_TENT, 1, {40, 2}, 1);

    const std::vector<TilePos> west =
        formation_west(20, 14, 9, 14, 5, {7, 11, 18, 23});
    place_markers(world, 0, west);
    place_markers(world, 1, mirror_x(west, 46));

    for (const TilePos t :
         {TilePos{17, 2}, TilePos{28, 2}, TilePos{12, 7}, TilePos{22, 7},
          TilePos{33, 7}, TilePos{12, 22}, TilePos{22, 22}, TilePos{33, 22}})
        place_at(world, Order::Treasure, FAMILY_DRUMSTICK, 0, t);

    emit_painted(world, row);
}

ExpectedLevel soccer_row(int id, const char* title, int par, int w, int h,
                         int teams, int treasures,
                         std::vector<GoalRect> goals, TilePos kickoff,
                         std::vector<std::string> briefing)
{
    ExpectedLevel row;
    row.id = id;
    row.mode = ModeKind::Soccer;
    row.title = title;
    row.par = par;
    row.grid_w = w;
    row.grid_h = h;
    row.team_count = teams;
    row.markers_per_team = 12;
    row.treasures = treasures;
    row.time_limit = 10800;
    row.score_limit = 3;
    row.goal_rects = std::move(goals);
    row.kickoff = kickoff;
    row.briefing = std::move(briefing);
    return row;
}

} // namespace

std::vector<ExpectedLevel> soccer_expectations()
{
    std::vector<ExpectedLevel> out;

    ExpectedLevel pitch = soccer_row(
        820, "Soccer: THE PITCH", 6, 44, 28, 2, 8,
        {{1, 10, 2, 17}, {41, 10, 42, 17}}, {21, 13},
        {
            "A GENTLER GAME? HARDLY.",
            "STRIKE THE BALL AND IT FLIES;",
            "STAND IN ITS WAY AND IT BITES.",
            "PUT IT PAST THEIR LINE THREE",
            "TIMES AND THE MATCH IS YOURS.",
            "THE DEAD MAY REJOIN THE FIELD.",
            "PLAY ON, CONTENDERS.",
            "-- THE GAMESMASTER",
        });
    pitch.decor_cells = 4;
    out.push_back(std::move(pitch));

    ExpectedLevel mudbowl = soccer_row(
        821, "Soccer: THE MUDBOWL", 8, 50, 30, 2, 10,
        {{1, 11, 2, 18}, {47, 11, 48, 18}}, {24, 14},
        {
            "THE MUDBOWL, CONTENDERS. TWO",
            "POOLS FLANK THE MIDDLE AND",
            "THE BALL CARES NOT AT ALL.",
            "WADE AND YOU DROWN BY INCHES;",
            "FLY AND YOU GAMBLE. THREE",
            "GOALS TAKE THE MATCH.",
            "-- THE GAMESMASTER",
        });
    mudbowl.decor_cells = 4;
    out.push_back(std::move(mudbowl));

    ExpectedLevel foursquare = soccer_row(
        822, "Soccer: FOURSQUARE", 8, 40, 40, 4, 8,
        {{16, 1, 23, 2}, {37, 16, 38, 23}, {16, 37, 23, 38}, {1, 16, 2, 23}},
        {19, 19},
        {
            "FOUR GOALS, FOUR BANDS, ONE",
            "BALL. GUARD YOUR OWN WALL AND",
            "SMASH THE BALL PAST ANY RIVAL",
            "LINE. THREE GOALS TAKE THE",
            "MATCH - AND EVERY MISS FEEDS",
            "SOMEBODY ELSE'S PAGE.",
            "-- THE GAMESMASTER",
        });
    foursquare.decor_cells = 8;
    out.push_back(std::move(foursquare));

    ExpectedLevel boneyard = soccer_row(
        823, "Soccer: BONEYARD CUP", 10, 46, 30, 2, 8,
        {{1, 11, 2, 18}, {43, 11, 44, 18}}, {22, 14},
        {
            "THE BONEYARD CUP. EACH SIDE'S",
            "OLD TENT RAISES SKELETON",
            "TEAMMATES BEHIND THE GOAL -",
            "BONY KEEPERS, TIRELESS WINGS.",
            "THE BALL BITES ALL THE SAME.",
            "THREE GOALS TAKE THE MATCH.",
            "-- THE GAMESMASTER",
        });
    boneyard.generators_per_team[0] = 1;
    boneyard.generators_per_team[1] = 1;
    boneyard.spawn_caps = {{0, 6}, {1, 6}};
    boneyard.decor_cells = 8;
    out.push_back(std::move(boneyard));

    return out;
}

void build_soccer()
{
    const std::vector<ExpectedLevel> rows = soccer_expectations();
    build_the_pitch(rows[0]);
    build_the_mudbowl(rows[1]);
    build_foursquare(rows[2]);
    build_boneyard_cup(rows[3]);
}

} // namespace modesgen
