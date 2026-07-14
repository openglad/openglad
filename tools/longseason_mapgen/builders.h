/* The Long Season campaign generator — shared builder API.
 *
 * Declares the level-authoring helpers (defined in main.cpp), the
 * self-check expectation row, and the per-season entry points (defined in
 * levels_spring.cpp / levels_summer.cpp / levels_autumn.cpp /
 * levels_winter.cpp / levels_reckoning.cpp). Each season file builds its
 * levels into the staging directory and returns the expectation rows the
 * self-check validates after the package is zipped and mounted.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <openglad/core/order.h>
#include <openglad/gameplay/pixie_data.h>

#include <initializer_list>
#include <string>
#include <vector>

class GameWorld;
class LevelRuntimeData;
class walker;
struct LevelDataHooks;

namespace longseason {

// --- Error reporting (shared error counter lives in main.cpp). -------------
void fail(const std::string& message);
void warn(const std::string& message);

// --- Grid / paint helpers. --------------------------------------------------
PixieData make_grid(int tw, int th, unsigned char fill);
void paint(PixieData& g, int tx, int ty, unsigned char tile);
// Fill a rectangle [tx0,tx1] x [ty0,ty1] (inclusive) with a tile.
void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                unsigned char tile);
// Deterministic pavement pattern (PAVEMENT1-3 are inert to the autotiler, so
// the variety must be painted in rather than smoothed in).
void paint_pavement(PixieData& g, int tx0, int ty0, int tx1, int ty1);
// Deterministic worn-path pattern (PATH_1-4 are likewise autotiler-inert and
// walkable: the mud track a working company's boots leave behind).
void paint_path(PixieData& g, int tx0, int ty0, int tx1, int ty1);
// Fill every tile whose center distance from (cx, cy) lies in [r0, r1).
void paint_ring(PixieData& g, double cx, double cy, double r0, double r1,
                unsigned char tile);
// Ash checker over OPEN GROUND only (the level-17 camp plain): the same
// (x * 7 + y * 13) % 2 recipe as the westlands paint_ash, but cells a plain
// ground walker cannot stand on — smoothed walls, tree eaves, water, lava,
// void — keep their tile, and so do the Z-furniture cells (stairs, glass,
// drop blocks). One call ashes a whole plain around authored structures.
void paint_ash_open(PixieData& g, int tx0, int ty0, int tx1, int ty1);
// A vertically-aligned Z-stair pair: UP on floor f, DOWN on floor f+1 at the
// SAME cell (Z-stairs are positional; see docs/z-axis-design.md).
void stair_pair(GameWorld& w, int f, int tx, int ty);
// DECOR plane authoring (BASE + DECOR tile layering, .fss v11): set the
// decor id at a tile, leaving the base byte alone — the whole point is that
// the ground under a torch or boulder keeps autotiling as ground. Allocates
// the floor's plane lazily. Refuses (hard fail) decor over air / Z-stair /
// void bases: decor floating over a hole is nonsense and the passability
// composition (base AND decor) would be misleading there.
void paint_decor(GameWorld& w, int floor, int tx, int ty,
                 unsigned char decor_id);
void paint_decor_rect(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, unsigned char decor_id);
// Run the genre autotiler over every floor (grass/water/tree/wall/cobble/
// carpet shaping). The Z tiles and pavement/boulder decor are genre-inert.
void smooth_world(GameWorld& w);

// --- Entity placement. -------------------------------------------------------
walker* place(GameWorld& world, Order order, int family, int team, int floor,
              int tx, int ty);
// Per-placed-NPC scenario extras (level file v10, reserved[3..5]): disable the
// family special and/or delay the walker's entry into the world by
// `spawn_delay` sim ticks. Safe on any AI NPC, team 0 included: players deploy
// onto reserved-team START MARKERS (game.cpp), never onto placed livings, so a
// delayed team-0 ally simply arrives late. Defaults leave the level
// byte-identical to a v9-era save.
void set_npc_extras(walker* ob, bool specials_disabled, int spawn_delay);
walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard = false,
                     bool specials_disabled = false, int spawn_delay = 0);
walker* place_generator(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level);
// A team-0 player START MARKER (Order::Special, FAMILY_RESERVED_TEAM). At
// level load the game deploys one team character per marker, consuming
// markers in oblist order (game.cpp) — so place the formation's lead position
// first and let the rest of the crew fill the defensive posture. Markers
// beyond the team size are simply destroyed at load. Markers are 32x32
// (2x2 tiles) — anchors need shoulder room.
void place_start(GameWorld& w, int floor, int tx, int ty);
// A named team-0 hero NPC. The level-file name field serializes through a
// 12-byte buffer via snprintf, so names must fit in 11 characters
// ("The Founder" exactly fills it) — an oversize name is a hard fail, not a
// silent truncation.
walker* place_hero(GameWorld& w, int family, int floor, int tx, int ty,
                   int level, const char* name, bool guard = false,
                   bool specials_disabled = false, int spawn_delay = 0);
// A named ENEMY (any non-0 team): The Count, Long Tom's lieutenants, The
// Founder. Same 11-character name budget as place_hero; named foes never
// interact with SAVE_ALL scoping (npc_flags bit 2 lives on team-0 NPCs only).
walker* place_named_foe(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level, const char* name,
                        bool guard = false);
// An exit that names its destination level (campaign chaining).
void place_exit(GameWorld& w, int floor, int tx, int ty, int destination);
// True when the tile (tx, ty) on 'floor' is covered by (or within 'margin'
// tiles of) any already-placed entity's footprint.
bool cell_near_entity(GameWorld& w, int floor, int tx, int ty, int margin);
// Boulder / jagged-litter scatter over a rectangle. Runs AFTER army placement
// and keeps one tile of clearance around every entity (and never covers a
// Z-stair). Boulders are DECOR (DECOR_BOULDER_1..4 over the existing biome
// ground — snow stays snow under the rock); jagged litter stays a BASE tile
// (full-tile terrain art, blocks even projectiles — nothing to cut out), so
// keep the litter modulus high.
void scatter_boulders(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, int modulus);
void scatter_litter(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                    int ty1, int modulus);
// Ambience-decor scatter: NON-BLOCKING set dressing — pebbles, bones,
// shrubs — hashed over a rectangle, restricted to the ground classes the art
// reads on. Non-blocking ids leave passability untouched by construction
// (the helper hard-fails a blocking id), so routes, footing, fall lines and
// reachability are unaffected; cells that already carry decor (hand-placed
// torches, scattered rocks) keep it, and entity cells are skipped so nothing
// spawns standing in a bone pile. Runs AFTER army placement and AFTER the
// base-tile scatters (litter replaces its cells' bases, so dressed cells
// stay dressed).
enum class ScatterGround : unsigned char
{
    Grass,      // plain grass, all four smoothed variants
    LightGrass, // meadow / garden / field sheen (interior tile only)
    DarkGrass,  // scrub, moss beds, trampled ground (interior variants)
    Dirt,       // packed earth (fords, camp grounds, dike works)
    DarkDirt,   // carved mine floor
    Snow,       // both drift variants
    Marsh,      // both bog variants
    Ash,        // both cinder variants
    Path,       // the worn-track variants
    Pavement,   // dressed stone floors
    Cobble,     // street cobbles
};
void scatter_decor(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                   int ty1, int modulus, unsigned char decor_id,
                   std::initializer_list<ScatterGround> grounds);

// --- Level bootstrap / save. --------------------------------------------------
// Common world bootstrap: a LevelRuntimeData with N floors all sized tw x th,
// floor 0 filled grass; extra floors filled grass too (callers paint specials).
void init_world(LevelRuntimeData& level, int floors, int tw, int th);
// Writes scen{id}.fss plus the per-floor grid PNGs into the staging directory.
// par_value / time_bonus_limit are explicit: every level passes its designed
// values. SAVE_ALL protectee flagging is centralized here (see main.cpp's
// save_all_protectee_name table): on a SCEN_TYPE_SAVE_ALL level the designed
// protectee — and ONLY that walker — gets npc_flags bit 2.
void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description,
                      int par_value, int time_bonus_limit);

// --- Self-check expectation row. ---------------------------------------------
struct ExpectedLevel
{
    int id;
    int floors;
    const char* title;
    // Team-0 start markers (the player crew's authored formation) plus the
    // placed team-0 allies (named heroes, defender generators).
    int start_markers;
    int team0_livings;
    int team0_generators;
    int team1_livings;
    int team1_generators;
    int team2_livings;
    int team2_generators;
    // Per-NPC v10 extras that must round-trip through the package.
    int delayed_spawns;
    int specials_disabled;
    // When set, every floor boundary must have >= 1 aligned UP/DOWN stair pair
    // (levels traversed by falling opt out).
    bool stairs_every_boundary;
    // The exact exit-destination set (campaign_meta exit graph). Every
    // destination must also exist in the package — see the PLANNED-set
    // warn-not-fail rule in main.cpp's self-check.
    std::vector<int> exit_destinations;
};

// --- Per-season entry points. --------------------------------------------------
// Each build_<season> builds its levels through the given GameWorld-factory
// context (the level-data hooks LevelRuntimeData needs); <season>_expectations
// returns the matching self-check rows.
void build_spring(const LevelDataHooks& hooks);
std::vector<ExpectedLevel> spring_expectations();
void build_summer(const LevelDataHooks& hooks);
std::vector<ExpectedLevel> summer_expectations();
void build_autumn(const LevelDataHooks& hooks);
std::vector<ExpectedLevel> autumn_expectations();
void build_winter(const LevelDataHooks& hooks);
std::vector<ExpectedLevel> winter_expectations();
void build_reckoning(const LevelDataHooks& hooks);
std::vector<ExpectedLevel> reckoning_expectations();

} // namespace longseason
