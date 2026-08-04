/* Deterministic level-authoring builder library (og::mapgen).
 *
 * COPIED (not moved) from tools/westlands_mapgen — the subset the Tower
 * Climb generator needs (tower-triple spec, WP-4): grid/paint helpers,
 * Z-stair pairing, genre smoothing, entity placement, clearance-keeping
 * scatters, and the level audits (footing/standability, stair alignment +
 * clearance, fall-line + fall-depth, A*-reachability). The tools keep
 * their private copies in v1 (protects committed .glad bytes); THIS
 * library is authoritative going forward — tool retargeting is the WP-8
 * follow-up with byte-identity verification.
 *
 * Determinism contract (spec §1.1): SDL-free, no global state, no libc
 * rand(), no ctx().rng. Helpers that scatter by position hash take an
 * explicit std::uint32_t seed mixed into the hash; everything else is a
 * pure function of its arguments. Genre smoothing (smooth_world) draws
 * ONLY the scratch world's own SimRandom (GameWorld(seed) wires every
 * floor smoother to world.rng_), so a whole build is a pure function of
 * the construction seed — callers must not have a gameplay-RNG override
 * installed while building.
 *
 * Audit execution context: audit_reachability needs an installed
 * GameplayContext (pathfinding + obmap). At generation time (GO, spec
 * §D8) no context is installed, so the caller wraps the call in a
 * GameplayContextGuard over a scratch context whose world is the level
 * under audit. Audits return human-readable failure strings; empty
 * result = pass.
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

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

class GameWorld;
class walker;

namespace og::mapgen {

// --- Deterministic position hashing. ----------------------------------------
// A small avalanche mix over (seed, x, y, salt). Every scatter helper keys
// its cell selection and variant pick on this — never on shared RNG state —
// so identical (seed, arguments) always paint identical bytes. `salt`
// separates the streams (cell-select vs variant pick vs scatter kind) so
// different scatters interleave instead of stacking on the same cells.
[[nodiscard]] std::uint32_t position_hash(std::uint32_t seed, int x, int y,
                                          std::uint32_t salt = 0) noexcept;

// --- Grid / paint helpers. ---------------------------------------------------
// A (tw x th) tile field, every cell `fill`; PixieData owns the heap buffer.
[[nodiscard]] PixieData make_grid(int tw, int th, unsigned char fill);
void paint(PixieData& g, int tx, int ty, unsigned char tile);
// Fill a rectangle [tx0,tx1] x [ty0,ty1] (inclusive) with a tile.
void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                unsigned char tile);
// Fill every tile whose center distance from (cx, cy) lies in [r0, r1).
void paint_ring(PixieData& g, double cx, double cy, double r0, double r1,
                unsigned char tile);
// A vertically-aligned Z-stair pair: UP on floor f, DOWN on floor f+1 at the
// SAME cell (Z-stairs are positional; see docs/z-axis-design.md).
void stair_pair(GameWorld& w, int f, int tx, int ty);
// DECOR plane authoring (BASE + DECOR tile layering, .fss v11): set the
// decor id at a tile, leaving the base byte alone. Allocates the floor's
// plane lazily. Refuses (returns false) out-of-grid tiles, out-of-range
// decor ids, and decor over air / Z-stair / void bases: decor floating over
// a hole is nonsense and the passability composition (base AND decor) would
// be misleading there.
bool paint_decor(GameWorld& w, int floor, int tx, int ty,
                 unsigned char decor_id);
// Run the genre autotiler over every floor (grass/water/tree/wall/cobble/
// carpet shaping). Draws only the world's own SimRandom (see header note).
void smooth_world(GameWorld& w);

// --- Level bootstrap. ----------------------------------------------------------
// Common world bootstrap: N floors all sized tw x th, floor 0 filled grass;
// extra floors filled grass too (callers paint specials). Note: the tool's
// original takes a LevelRuntimeData& (an interface-component type invisible
// to og_gameplay); the library operates on the wrapped GameWorld directly.
void init_world(GameWorld& world, int floors, int tw, int th);

// --- Entity placement. --------------------------------------------------------
// Returns nullptr when the world refuses the entity (no failure callback:
// callers audit / reroll).
walker* place(GameWorld& world, Order order, int family, int team, int floor,
              int tx, int ty);
walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard = false,
                     bool specials_disabled = false, int spawn_delay = 0);
walker* place_generator(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level);
// A team-0 player START MARKER (Order::Special, FAMILY_RESERVED_TEAM). At
// level load the game deploys one team character per marker, consuming
// markers in oblist order — so place the formation's lead position first.
// Markers are 32x32 (2x2 tiles) — anchors need shoulder room.
void place_start(GameWorld& w, int floor, int tx, int ty);
// An exit that names its destination level (campaign chaining).
void place_exit(GameWorld& w, int floor, int tx, int ty, int destination);
// True when the tile (tx, ty) on 'floor' is covered by (or within 'margin'
// tiles of) any already-placed entity's footprint.
[[nodiscard]] bool cell_near_entity(const GameWorld& w, int floor, int tx,
                                    int ty, int margin);

// --- Seeded scatters. ----------------------------------------------------------
// Boulder / jagged-litter scatter over a rectangle. Runs AFTER army placement
// and keeps one tile of clearance around every entity (and never covers a
// Z-stair or mines a fall landing). Boulders are DECOR over the existing
// biome ground (base-tile boulders only over AIR, sealing upper-floor
// holes); jagged litter stays a BASE tile.
void scatter_boulders(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                      int ty0, int tx1, int ty1, int modulus);
void scatter_litter(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                    int ty0, int tx1, int ty1, int modulus);
// Ambience-decor scatter: NON-BLOCKING set dressing — pebbles, bones,
// shrubs — hashed over a rectangle, restricted to the ground classes the art
// reads on. Returns false (and paints nothing) for a blocking decor id.
// Cells that already carry decor keep it; entity cells are skipped.
enum class ScatterGround : unsigned char
{
    Grass,      // plain grass, all four smoothed variants
    LightGrass, // meadow / garden / field sheen (interior tile only)
    DarkGrass,  // scrub, moss beds, trampled ground (interior variants)
    Dirt,       // packed earth
    DarkDirt,   // carved cavern floor
    Snow,       // both drift variants
    Marsh,      // both bog variants
    Ash,        // both cinder variants
    Path,       // the worn-track variants
    Pavement,   // dressed stone floors
    Cobble,     // street cobbles
};
bool scatter_decor(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                   int ty0, int tx1, int ty1, int modulus,
                   unsigned char decor_id,
                   std::initializer_list<ScatterGround> grounds);

// --- Standability primitives (shared by the scatters and the audits). ---------
// Single-cell ground passability: the Living arm of query_grid_passable with
// none of the flyer / forestwalk / ethereal escapes — the tiles a plain
// ground walker can STAND on. Z-stairs, glass and drop blocks are legal
// landings; PIX_AIR deliberately is not (the fall audit chases air columns).
[[nodiscard]] bool ground_cell_standable(unsigned char tile) noexcept;
// Base tile AND decor plane both standable for a ground walker.
[[nodiscard]] bool cell_standable(const GameWorld& world, int floor, int tx,
                                  int ty);
// True when (tx, ty) on 'floor' is the LANDING cell of some fall line above.
[[nodiscard]] bool cell_is_fall_landing(const GameWorld& w, int floor, int tx,
                                        int ty);

// --- Audits (audits.cpp). Each returns failure strings; empty = pass. ---------
// Footing/standability: every entity stands on ground its own footprint can
// occupy; no ground troop over air or on blocking decor.
[[nodiscard]] std::vector<std::string> audit_footing(GameWorld& world);
// Stair alignment + clearance: every floor boundary has >= 1 vertically
// aligned UP/DOWN pair (when require_every_boundary), and no immobile post
// (ACT_GUARD, generator, blocking decor) seals a stair cell or its
// 4-neighborhood arrival cells on either floor of a pair.
[[nodiscard]] std::vector<std::string> audit_stairs(
    GameWorld& world, bool require_every_boundary = true);
// Fall lines: any AIR cell a ground walker can step into must land its
// faller on standable ground (base AND decor), and the fall must drop at
// most max_fall_depth stories. Falling past floor 0 is a pit death — a
// designed mechanic — and stays legal.
[[nodiscard]] std::vector<std::string> audit_fall_lines(
    GameWorld& world, int max_fall_depth = 4);
// A*-reachability: every non-flying living, every generator and every exit
// must be reachable from the crew's lead start marker by a ground probe,
// respecting passability, air holes, lava and Z-stairs. Needs an installed
// GameplayContext and the world's obmap (see header note); the probe is
// removed again, leaving the entity lists exactly as found.
[[nodiscard]] std::vector<std::string> audit_reachability(GameWorld& world);

// --- Generator spawn egress. --------------------------------------------------
// One engine spawn position: the top-left pixel corner walker::fire() hands a
// generator's newly created living.
struct SpawnExit
{
    int x = 0;
    int y = 0;
};
// The EIGHT positions walker::fire() can place a spawn at (walker.cpp
// :526-566) for a generator whose pixel bbox is (gx, gy, gen_w, gen_h) and
// whose spawn measures spawn_w x spawn_h. Returned in the FACE_* order of
// fire()'s switch: RIGHT, LEFT, DOWN, UP, UP_RIGHT, UP_LEFT, DOWN_RIGHT,
// DOWN_LEFT. act_generate rolls lastx/lasty over {-1,0,1} and forces (0,0)
// to (1,0) (walker.cpp:1372-1376), so every one of the eight is live.
// Pure arithmetic — the audit and the engine-pin test share it.
[[nodiscard]] std::array<SpawnExit, 8> generator_spawn_exits(
    int gx, int gy, int gen_w, int gen_h, int spawn_w, int spawn_h) noexcept;
// Spawn-egress audit: for every live generator, classify the eight positions
// above. A position is USABLE when the spawn's own body fits there (terrain
// plus the durable obstructions below) and CONNECTED when a ground route
// runs from it to the crew's lead start marker. Three failures:
//   * "no usable spawn exit" -- all eight blocked, so nothing is ever born
//     and the generator is dead scenery;
//   * "every spawn exit is cut off from the lead start marker" -- spawns
//     appear but land off the crew's walking map (the sealed-alcove bug).
//     A level whose generator is deliberately served by transport rather
//     than feet already declares that through its reachability exception,
//     and the caller matches it against the same anchor text;
//   * "spawn pocket at (x, y)" -- a stranded position ALONGSIDE a working
//     one, so a deterministic share of the spawns piles up in a closed cell.
// The obstruction model is terrain plus GENERATOR BODIES only: generators
// never move and Onslaught flips them rather than removing them, so their
// footprints are permanent walls, while authored livings step aside within a
// few ticks and the generator simply retries at its next cadence. Diagonal
// steps obey the engine's no-corner-cutting rule (gameplay_context.cpp
// adjacent_cost). Needs an installed GameplayContext and the world's obmap;
// probes are removed again, leaving the entity lists exactly as found.
[[nodiscard]] std::vector<std::string> audit_generator_spawn_exits(
    GameWorld& world);

} // namespace og::mapgen
