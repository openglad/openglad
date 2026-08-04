/* Multiplayer Game Modes campaign generator — shared declarations.
 *
 * Builds builtin/org.openglad.modes.glad: the 28-scenario five-mode
 * campaign (Team Deathmatch 300-305, CTF 500-509, Onslaught 800-803,
 * Soccer 820-823, Mutant 840-843) that absorbs the arenas and CTF
 * packages. SDL-free; reuses the headless platform glue.
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

#include <array>
#include <memory>
#include <string>
#include <vector>

class GameWorld;
class walker;

namespace modesgen {

// The pack's flag/waypoint treasure wire bytes (tools/modes_mapgen/pack/
// families/treasure-flag.lua and treasure-waypoint.lua claim the retired
// core CTF slots). The builders author these bytes into the .fss grids.
inline constexpr int kFlagFamily = 13;
inline constexpr int kWaypointFamily = 14;

struct TilePos
{
    short tx = -1;
    short ty = -1;
};

enum class ModeKind
{
    Tdm,
    Ctf,
    Onslaught,
    Soccer,
    Mutant
};

const char* mode_name(ModeKind mode); // "tdm", "ctf", ...

// A per-team live-spawn cap the mode director enforces (D5 mechanism).
// The obmap ledger and the generated manifest both read these rows.
struct SpawnCap
{
    int team;
    int cap;
};

// A goal strip a team DEFENDS, in tile coords (inclusive). The manifest
// emits the equivalent pixel rect; the self-check asserts every tile in
// it carries the goal carpet id.
struct GoalRect
{
    int x0, y0, x1, y1;
};

// The single source of truth per level: the builders author from it, the
// generated manifest serializes it, the mapgen self-check re-validates the
// remounted package against it, and tests/unit/test_modes_levels.cpp pins
// a mirror of it.
struct ExpectedLevel
{
    int id = 0;
    ModeKind mode = ModeKind::Tdm;
    const char* title = "";
    int par = 0;
    int grid_w = 0, grid_h = 0;
    int team_count = 0;
    int markers_per_team = 0;
    int flags = 0;                       // CTF: total flag treasures
    int control_points = 0;              // family-14 count (CTF CPs, ONS posts)
    std::array<int, 8> generators_per_team{}; // index = team byte (7 neutral)
    int authored_livings = 0;
    int treasures = 0;   // Treasure-order spice incl. keys/teleporters,
                         // excl. flags/CPs/exits/stains
    int doors = 0;       // Weapon FAMILY_DOOR count
    int other_weapons = 0; // non-door authored weapons (scen506 blood pools)
    std::vector<SpawnCap> spawn_caps;    // manifest spawn_caps rows
    int time_limit = 0;                  // manifest tuning, sim ticks
    int score_limit = 0;                 // manifest tuning, 0 = mode rule
    bool a_star_waived = false;          // 303/305 documented obmap overrun
    std::vector<GoalRect> goal_rects;    // soccer: index = defending team
    TilePos kickoff;                     // soccer: ball spawn tile
    int decor_cells = 0;                 // exact nonzero floor-0 decor cells
    // Documented substrings of reachability-audit failures this level is
    // allowed to keep (deliberate design, e.g. teleporter-served vaults).
    std::vector<std::string> reachability_exceptions;
    // Generator tile anchors whose spawn-POCKET findings are a deliberate
    // design (see the spawn-egress block in self_check_level). Tier-1 (all
    // exits sealed) is never waivable, and a listed anchor that stops
    // producing a pocket FAILS so the list cannot rot.
    std::vector<TilePos> spawn_pocket_ok;
    std::vector<std::string> briefing;   // <=33 chars/line, Gamesmaster sign-off
};

// ---------------------------------------------------------------------------
// Shared authoring helpers (builders_common.cpp).
// ---------------------------------------------------------------------------
extern int g_errors;
void fail(const std::string& message);

// Living-sized (16x16) passability probe; never added to the world.
std::unique_ptr<walker> make_probe(GameWorld& world);
bool tile_passable(GameWorld& world, walker* probe, TilePos t);

// Place an entity at a tile (Treasure via add_fx_ob, everything else via
// add_ob); sets team and real team; returns nullptr on failure (and
// counts an error).
walker* place_at(GameWorld& world, Order order, int family, int team,
                 TilePos at, int level = 1);

// Metadata + serialization shared by every mode TU.
void apply_mode_metadata(GameWorld& world, const char* title, int par_value);
bool save_level(GameWorld& world, const ExpectedLevel& row);

// Byte-copy a vendored grid member (data/<pkg>/pix/<name>) into the
// staging pix/ dir.
void copy_vendored_pix(const std::string& pkg, const std::string& name);

// Write the floor-0 decor plane member when the world carries a nonzero
// plane (same predicate as the .fss v11 writer).
void write_decor_plane(GameWorld& world, int out_id);

// Mount/unmount a vendored data directory (data/<pkg>) at the virtual
// root so the production level reader can load the absorbed layouts.
bool mount_vendored_data(const std::string& pkg);
void unmount_vendored_data(const std::string& pkg);

// ---------------------------------------------------------------------------
// Per-mode entry points. Each build_* authors its levels into the staging
// dir; each *_expectations() returns the matching self-check rows.
// ---------------------------------------------------------------------------
void build_tdm();
void build_ctf();
void build_onslaught();
void build_soccer();
void build_mutant();

std::vector<ExpectedLevel> tdm_expectations();
std::vector<ExpectedLevel> ctf_expectations();
std::vector<ExpectedLevel> onslaught_expectations();
std::vector<ExpectedLevel> soccer_expectations();
std::vector<ExpectedLevel> mutant_expectations();

std::vector<ExpectedLevel> all_expectations();

// ---------------------------------------------------------------------------
// Generated manifest (manifest.cpp): the committed og.use module
// tools/modes_mapgen/pack/lib/mode_levels.lua, regenerated from the
// expectation rows. Returns false (and fails) when the committed copy was
// stale — the fresh bytes are written in place so a rerun passes.
// ---------------------------------------------------------------------------
std::string manifest_lua_text(const std::vector<ExpectedLevel>& rows);
bool check_and_refresh_manifest(const std::vector<ExpectedLevel>& rows);

} // namespace modesgen
