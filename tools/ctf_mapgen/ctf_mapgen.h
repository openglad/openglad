/* CTF campaign generator — shared declarations.
 *
 * Builds builtin/org.openglad.ctf.glad: six classic levels adapted for
 * capture-the-flag plus four originals, dressed with per-team flags,
 * respawn-anchor clusters, and control points. SDL-free; reuses the
 * headless platform glue (io_init, headless_level_data_hooks).
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/gameplay/pixie_data.h>

#include <array>
#include <string>
#include <vector>

class GameWorld;

namespace ctfgen {

struct TilePos
{
    short tx = -1;
    short ty = -1;
};

// A consumable / utility treasure placed for map spice.
struct Spice
{
    int family;     // FAMILY_* under the treasure heading
    int team;
    TilePos at;
    int level = 1;
};

// Entity dressing shared by adapted and original maps.
struct Dressing
{
    int team_count = 2;
    std::array<TilePos, 4> base{};      // base-zone centers, index == team
    int flag_level = 1;                  // >1 authors a per-map capture limit
    std::vector<TilePos> cps;            // contested middles (team 7)
    std::vector<Spice> spice;
    int anchors_per_team = 12;           // 8..16
};

struct AdaptedSpec
{
    int out_id;
    int src_id;
    const char* title;
    // source team -> ctf team, -1 strips the source team's livings.
    std::array<int, 8> team_map;
    int keep_livings_per_team = 0;       // 0 = strip all livings
    bool add_siege_tent = false;         // authored generator (AI-side team 1)
    TilePos tent_at{};
    int tent_level = 2;
    short par_value = 6;
    Dressing dress;
    std::vector<std::string> description;
};

struct OriginalSpec
{
    int out_id;
    const char* title;
    PixieData (*paint)();                // grid painter
    short par_value = 5;
    Dressing dress;
    std::vector<std::string> description;
};

// Original grid painters (grid_painters.cpp).
PixieData paint_first_blood();   // 506, 40x30, 2 teams
PixieData paint_river_run();     // 507, 60x40, 2 teams
PixieData paint_triad();         // 508, 51x51, 3 teams
PixieData paint_crossfire();     // 509, 60x60, 4 teams

// Original specs incl. dressing/spice tables (grid_painters.cpp).
std::vector<OriginalSpec> original_specs();

} // namespace ctfgen
