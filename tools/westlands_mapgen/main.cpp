/* War of the Westlands campaign generator.
 *
 * Produces campaigns/westlands/ (the source tree the build
 * composes into builtin/westlands.glad): a 26-level story campaign
 * (docs at scratch design + docs/z-axis-design.md) built act by act — the
 * flight east, the dark road, the war in the west, the burden's road, and
 * the convergence at the Mountain of Fire. In every battle the player's
 * crew IS the defense: team-0 start markers arranged in a tactical
 * formation deploy the whole team, a few placed team-0 allies (named
 * heroes, defender generators) fight alongside, and the enemy host is
 * team 2. SDL-free; reuses the headless platform glue, mirrors
 * tools/concept_mapgen (where the six epic war stories now numbered
 * 6, 7, 8, 14, 15 and 17 were first authored). Builds the v10 multi-floor
 * scenario format, zips a campaign package, mounts it, and self-checks
 * every registered level by reloading it — including exit-destination
 * validation against the registered id set.
 *
 * Usage: westlands_mapgen [output.glad]
 *        (default: campaigns/westlands)
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "../campaign_export.h"
#include "builders.h"

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/terrain_types.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pathfinding_grid.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

bool write_pixie_png(const char* filepath, const PixieData& data);
void io_init(int argc, char* argv[]);
void io_exit();
std::string get_user_path();

// --- Headless process globals (same shape as the dedicated server binary). ---
namespace og::runtime {
static SessionState s_mapgen_session{};
thread_local SessionState* current_session = &s_mapgen_session;
std::atomic<SessionState*> primary_session{&s_mapgen_session};
std::atomic<GameplayContext*> primary_game{&s_mapgen_session.game_};
} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 20260628u;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace westlands {

namespace {
int g_errors = 0;
} // namespace

void fail(const std::string& message)
{
    std::fprintf(stderr, "westlands_mapgen: ERROR: %s\n", message.c_str());
    ++g_errors;
}

void warn(const std::string& message)
{
    std::fprintf(stderr, "westlands_mapgen: WARNING: %s\n", message.c_str());
}

// A grass field of (tw x th) tiles; PixieData owns the heap buffer.
PixieData make_grid(int tw, int th, unsigned char fill)
{
    auto* buf = new unsigned char[static_cast<std::size_t>(tw) * th];
    std::fill(buf, buf + static_cast<std::size_t>(tw) * th, fill);
    return PixieData(1, static_cast<unsigned char>(tw),
                     static_cast<unsigned char>(th), buf);
}

void paint(PixieData& g, int tx, int ty, unsigned char tile)
{
    if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h)
        g.data[tx + ty * g.w] = tile;
}

void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1, unsigned char tile)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, tile);
}

void paint_pavement(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[3] = {PIX_PAVEMENT1, PIX_PAVEMENT2,
                                                  PIX_PAVEMENT3};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 7 + y * 13) % 3]);
}

void paint_path(PixieData& g, int tx0, int ty0, int tx1, int ty1)
{
    static constexpr unsigned char variants[4] = {PIX_PATH_1, PIX_PATH_2,
                                                  PIX_PATH_3, PIX_PATH_4};
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, variants[(x * 5 + y * 3) % 4]);
}

void paint_ring(PixieData& g, double cx, double cy, double r0, double r1,
                unsigned char tile)
{
    for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
        {
            const double dx = x - cx;
            const double dy = y - cy;
            const double d = std::sqrt(dx * dx + dy * dy);
            if (d >= r0 && d < r1)
                paint(g, x, y, tile);
        }
}

void stair_pair(GameWorld& w, int f, int tx, int ty)
{
    paint(w.grid_for_floor(f), tx, ty, PIX_ZSTAIR_UP);
    paint(w.grid_for_floor(f + 1), tx, ty, PIX_ZSTAIR_DOWN);
}

void paint_decor(GameWorld& w, int floor, int tx, int ty,
                 unsigned char decor_id)
{
    PixieData& g = w.grid_for_floor(floor);
    if (tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
    {
        fail(std::format("paint_decor: tile ({}, {}) outside floor {} grid",
                         tx, ty, floor));
        return;
    }
    if (decor_id >= DECOR_MAX)
    {
        fail(std::format("paint_decor: decor id {} out of range", decor_id));
        return;
    }
    const unsigned char base = g.data[tx + ty * g.w];
    if (base == PIX_AIR || base == PIX_ZSTAIR_UP || base == PIX_ZSTAIR_DOWN ||
        base == PIX_VOID1)
    {
        fail(std::format("paint_decor: decor {} over air/stair/void base {} "
                         "at ({}, {}) floor {}", decor_id, base, tx, ty,
                         floor));
        return;
    }
    PixieData& dec = w.decor_for_floor(floor);
    if (!dec.valid())
    {
        // Lazy plane allocation, zero-filled (same pattern as add_floor's
        // grid allocation): an untouched floor keeps an invalid plane and
        // the level saves without it.
        auto* buf = new unsigned char[static_cast<std::size_t>(g.w) * g.h];
        std::fill(buf, buf + static_cast<std::size_t>(g.w) * g.h,
                  static_cast<unsigned char>(DECOR_NONE));
        dec = PixieData(1, static_cast<unsigned char>(g.w),
                        static_cast<unsigned char>(g.h), buf);
    }
    dec.data[tx + ty * dec.w] = decor_id;
}

void paint_decor_rect(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, unsigned char decor_id)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint_decor(w, floor, x, y, decor_id);
}

void smooth_world(GameWorld& w)
{
    for (int f = 0; f < w.floor_count(); ++f)
        w.smoother_for_floor(f).smooth();
}

walker* place(GameWorld& world, Order order, int family, int team, int floor,
              int tx, int ty)
{
    walker* w = (order == Order::Treasure) ? world.add_fx_ob(order, family)
                                           : world.add_ob(order, family);
    if (w == nullptr)
    {
        fail(std::format("could not place order {} family {}",
                         static_cast<int>(order), family));
        return nullptr;
    }
    w->set_floor(static_cast<short>(floor));
    w->setxy(static_cast<short>(tx * GRID_SIZE), static_cast<short>(ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    return w;
}

void set_npc_extras(walker* ob, bool specials_disabled, int spawn_delay)
{
    if (ob == nullptr)
        return;
    ob->set_specials_disabled(specials_disabled);
    ob->set_spawn_delay(static_cast<std::uint16_t>(spawn_delay));
}

walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard, bool specials_disabled,
                     int spawn_delay)
{
    walker* ob = place(w, Order::Living, family, team, floor, tx, ty);
    if (ob == nullptr)
        return nullptr;
    ob->stats()->set_level(level);
    if (guard)
    {
        ob->set_act_type(ACT_GUARD);
        // Guard wake policy (npc_flags bit 1): enemy guards are ambush
        // posts — they hold until a foe walks into genuine sight (range +
        // clear ray, walker::act_guard) and then hunt. Allied guards
        // (teams 0/1) are the opposite kind of post: Bearer escorts,
        // door-wards, redoubt garrisons — waking would march them off the
        // very chokepoint they exist to hold, so they keep the classic
        // stationary-sentry policy. Audited placement-by-placement
        // (2026-07-11): every allied guard in this campaign is a posted
        // NPC/garrison, every enemy guard an ambusher. A future exception
        // can override on the returned walker; self_check_level enforces
        // the allied rule on the reloaded package either way.
        if (team <= 1)
            ob->set_guard_hold_post(true);
    }
    set_npc_extras(ob, specials_disabled, spawn_delay);
    return ob;
}

walker* place_generator(GameWorld& w, int family, int team, int floor, int tx,
                        int ty, int level)
{
    walker* ob = place(w, Order::Generator, family, team, floor, tx, ty);
    if (ob != nullptr)
        ob->stats()->set_level(level);
    return ob;
}

void place_start(GameWorld& w, int floor, int tx, int ty)
{
    place(w, Order::Special, FAMILY_RESERVED_TEAM, 0, floor, tx, ty);
}

walker* place_hero(GameWorld& w, int family, int floor, int tx, int ty,
                   int level, const char* name, bool guard,
                   bool specials_disabled, int spawn_delay)
{
    walker* ob = place_living(w, family, 0, floor, tx, ty, level, guard,
                              specials_disabled, spawn_delay);
    if (ob != nullptr)
        ob->stats()->name = name;
    return ob;
}

void place_exit(GameWorld& w, int floor, int tx, int ty, int destination)
{
    walker* e = place(w, Order::Treasure, FAMILY_EXIT, 0, floor, tx, ty);
    if (e != nullptr)
        e->stats()->set_level(destination);
}

bool cell_near_entity(GameWorld& w, int floor, int tx, int ty, int margin)
{
    auto overlaps = [&](walker* ob) {
        if (ob == nullptr || ob->floor() != floor)
            return false;
        const int x0 = ob->xpos() / GRID_SIZE - margin;
        const int y0 = ob->ypos() / GRID_SIZE - margin;
        const int x1 = (ob->xpos() + ob->sizex() - 1) / GRID_SIZE + margin;
        const int y1 = (ob->ypos() + ob->sizey() - 1) / GRID_SIZE + margin;
        return tx >= x0 && tx <= x1 && ty >= y0 && ty <= y1;
    };
    for (const auto& uptr : w.oblist)
        if (overlaps(uptr.get()))
            return true;
    for (const auto& uptr : w.fxlist)
        if (overlaps(uptr.get()))
            return true;
    return false;
}

namespace {

// --- Fall-line support (Wave E5). --------------------------------------------
// Single-cell ground passability: the Living arm of
// GameWorld::query_grid_passable with none of the flyer / forestwalk /
// ethereal escapes — the tiles a plain ground walker can STAND on. A fall
// landing must be immediately standable; water, lava, boulder and torch
// bases all bounce the faller into the engine's A5 landing nudge, and
// levels must not rely on the nudge. Keep in lockstep with the mirrored
// classifier in tests/unit/test_westlands_levels.cpp.
bool ground_cell_standable(unsigned char tile)
{
    switch (tile)
    {
        case PIX_GRASS1:
        case PIX_GRASS2:
        case PIX_GRASS3:
        case PIX_GRASS4:
        case PIX_GRASS_DARK_1:
        case PIX_GRASS_DARK_2:
        case PIX_GRASS_DARK_3:
        case PIX_GRASS_DARK_4:
        case PIX_GRASS_DARK_LL:
        case PIX_GRASS_DARK_UR:
        case PIX_GRASS_DARK_B1:
        case PIX_GRASS_DARK_B2:
        case PIX_GRASS_DARK_BR:
        case PIX_GRASS_DARK_R1:
        case PIX_GRASS_DARK_R2:
        case PIX_GRASS_RUBBLE:
        case PIX_GRASS1_DAMAGED:
        case PIX_GRASS_LIGHT_1:
        case PIX_GRASS_LIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT_TOP:
        case PIX_GRASS_LIGHT_RIGHT:
        case PIX_GRASS_LIGHT_RIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT_BOTTOM:
        case PIX_GRASS_LIGHT_LEFT:
        case PIX_GRASS_LIGHT_LEFT_TOP:
        case PIX_GRASSWATER_LL:
        case PIX_GRASSWATER_LR:
        case PIX_GRASSWATER_UL:
        case PIX_GRASSWATER_UR:
        case PIX_PAVEMENT1:
        case PIX_PAVEMENT2:
        case PIX_PAVEMENT3:
        case PIX_COBBLE_1:
        case PIX_COBBLE_2:
        case PIX_COBBLE_3:
        case PIX_COBBLE_4:
        case PIX_FLOOR_PAVEL:
        case PIX_FLOOR_PAVER:
        case PIX_FLOOR_PAVEU:
        case PIX_FLOOR_PAVED:
        case PIX_PAVESTEPS1:
        case PIX_PAVESTEPS2:
        case PIX_PAVESTEPS2L:
        case PIX_PAVESTEPS2R:
        case PIX_FLOOR1:
        case PIX_CARPET_LL:
        case PIX_CARPET_B:
        case PIX_CARPET_LR:
        case PIX_CARPET_UR:
        case PIX_CARPET_U:
        case PIX_CARPET_UL:
        case PIX_CARPET_L:
        case PIX_CARPET_M:
        case PIX_CARPET_M2:
        case PIX_CARPET_R:
        case PIX_CARPET_SMALL_HOR:
        case PIX_CARPET_SMALL_VER:
        case PIX_CARPET_SMALL_CUP:
        case PIX_CARPET_SMALL_CAP:
        case PIX_CARPET_SMALL_LEFT:
        case PIX_CARPET_SMALL_RIGHT:
        case PIX_CARPET_SMALL_TINY:
        case PIX_DIRT_1:
        case PIX_DIRTGRASS_UL1:
        case PIX_DIRTGRASS_UR1:
        case PIX_DIRTGRASS_LL1:
        case PIX_DIRTGRASS_LR1:
        case PIX_DIRT_DARK_1:
        case PIX_DIRTGRASS_DARK_UL1:
        case PIX_DIRTGRASS_DARK_UR1:
        case PIX_DIRTGRASS_DARK_LL1:
        case PIX_DIRTGRASS_DARK_LR1:
        case PIX_PATH_1:
        case PIX_PATH_2:
        case PIX_PATH_3:
        case PIX_PATH_4:
        case PIX_SNOW1:
        case PIX_SNOW2:
        case PIX_MARSH1:
        case PIX_MARSH2:
        case PIX_ASH1:
        case PIX_ASH2:
        // Z tiles a ground walker occupies at the grid layer (stairs carry
        // you off; glass and drop blocks resolve in movement) — all legal
        // landings. PIX_AIR is deliberately NOT here: the fall audit chases
        // air columns itself.
        case PIX_ZSTAIR_UP:
        case PIX_ZSTAIR_DOWN:
        case PIX_GLASS:
        case PIX_DROPBLOCK_UP:
        case PIX_DROPBLOCK_RIGHT:
        case PIX_DROPBLOCK_DOWN:
        case PIX_DROPBLOCK_LEFT:
            return true;
        default:
            return false; // walls, trees, water, lava, boulders, torches,
                          // braziers, columns, jagged litter, void, air
    }
}

// Base tile AND decor plane both standable for a ground walker.
bool cell_standable(GameWorld& world, int floor, int tx, int ty)
{
    const PixieData& g = world.grid_for_floor(floor);
    if (!g.valid() || tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (!ground_cell_standable(g.data[tx + ty * g.w]))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = dec.data[tx + ty * dec.w];
        if (d < DECOR_MAX &&
            kDecorRegistry[d].pass == DecorPassability::BlocksGround)
            return false;
    }
    return true;
}

// True when (tx, ty) on 'floor' is the LANDING cell of some fall line
// above: scan up through stacked AIR cells; any air floor a ground walker
// can step into (an 8-adjacent standable cell of that same floor) drops
// its faller here. The scatters must never mine such a landing with
// blocking litter or boulders (Wave E5 fall-line rule) — the same way
// they never cover a Z-stair. On levels whose upper floors are untouched
// grass fills this is always false, so single-floor scatters are
// unaffected.
bool cell_is_fall_landing(GameWorld& w, int floor, int tx, int ty)
{
    for (int g = floor + 1; g < w.floor_count(); ++g)
    {
        const PixieData& gg = w.grid_for_floor(g);
        if (!gg.valid() || tx >= gg.w || ty >= gg.h ||
            gg.data[tx + ty * gg.w] != PIX_AIR)
            return false; // solid above: no fall reaches this cell
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx != 0 || dy != 0) &&
                    cell_standable(w, g, tx + dx, ty + dy))
                    return true;
    }
    return false;
}

// Scatter a 4-variant BASE tile set over a rectangle (jagged litter — a
// full-tile terrain, not decor). Runs AFTER army placement and keeps one
// tile of clearance around every entity (and never covers a Z-stair) so no
// one spawns wedged in the scenery. A full-tile base replaces the whole
// cell, decor included — legacy overwrite semantics: when litter lands on
// a cell an earlier scatter gave a boulder, the boulder goes away exactly
// as it did when both were base tiles.
void scatter_base_tiles(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                        int ty1, int modulus,
                        const unsigned char (&variants)[4])
{
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 7 + y * 11) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[x + y * g.w];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing (Wave E5)
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            paint(g, x, y, variants[(x + y) % 4]);
            PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && x < dec.w && y < dec.h)
                dec.data[x + y * dec.w] = DECOR_NONE;
        }
}

} // namespace

void scatter_boulders(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                      int ty1, int modulus)
{
    // Same cell hash and variant pick as the legacy base-tile scatter, but
    // the rock is now DECOR over the existing biome ground (snow stays snow
    // under it). An AIR cell keeps the legacy base-tile boulder instead: a
    // decor rock cannot plug a fall-through hole the way a base tile could
    // (paint_decor hard-fails over air by design), and the shipped levels
    // rely on the scatter sealing the odd upper-floor hole. Bases that block
    // weapons or flyers (walls, mid-canopy trees) are skipped outright: the
    // legacy scatter ERODED such cells into weapon-permeable rock, silently
    // breaching authored walls — the composition invariant (a BlocksGround
    // decor must reproduce the combined tile's "weapons and flyers pass"
    // semantics exactly, pinned by tests/unit/test_migrated_campaigns.cpp)
    // keeps the structures intact instead.
    static constexpr unsigned char decor_boulders[4] = {
        DECOR_BOULDER_1, DECOR_BOULDER_2, DECOR_BOULDER_3, DECOR_BOULDER_4};
    static constexpr unsigned char base_boulders[4] = {
        PIX_BOULDER_1, PIX_BOULDER_2, PIX_BOULDER_3, PIX_BOULDER_4};
    auto blocks_weapons_or_flyers = [](unsigned char t) {
        switch (t)
        {
            case PIX_H_WALL1:
            case PIX_WALL2:
            case PIX_WALL3:
            case PIX_WALL4:
            case PIX_WALL5:
            case PIX_WALL_LL:
            case PIX_WALLTOP_H:
            case PIX_WALL_ARROW_GRASS:
            case PIX_WALL_ARROW_FLOOR:
            case PIX_WALL_ARROW_GRASS_DARK:
            case PIX_TREE_M1:
            case PIX_TREE_ML:
            case PIX_TREE_MR:
            case PIX_TREE_MT:
            case PIX_TREE_T1:
                return true;
            default:
                return false;
        }
    };
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if ((x * 7 + y * 11) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[x + y * g.w];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (blocks_weapons_or_flyers(t))
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing (Wave E5) — and never
                          // PLUG an air hole into one (a base-boulder plug
                          // under a fall entry would be a blocked landing)
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            if (t == PIX_AIR)
                paint(g, x, y, base_boulders[(x + y) % 4]);
            else
                paint_decor(w, floor, x, y, decor_boulders[(x + y) % 4]);
        }
}

void scatter_litter(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                    int ty1, int modulus)
{
    static constexpr unsigned char litter[4] = {
        PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2, PIX_JAGGED_GROUND_3,
        PIX_JAGGED_GROUND_4};
    scatter_base_tiles(w, floor, tx0, ty0, tx1, ty1, modulus, litter);
}

namespace {

// The exact base tiles each ScatterGround class dresses. Interior tiles
// only: the smoothed edge/corner variants (shorelines, tree eaves, dirt
// aprons) are deliberately excluded so the dressing hugs the middle of a
// zone instead of crowding its seams.
bool scatter_ground_matches(unsigned char t, ScatterGround g)
{
    switch (g)
    {
        case ScatterGround::Grass:
            return t == PIX_GRASS1 || t == PIX_GRASS2 || t == PIX_GRASS3 ||
                   t == PIX_GRASS4;
        case ScatterGround::LightGrass:
            return t == PIX_GRASS_LIGHT_1;
        case ScatterGround::DarkGrass:
            return t == PIX_GRASS_DARK_1 || t == PIX_GRASS_DARK_2 ||
                   t == PIX_GRASS_DARK_3 || t == PIX_GRASS_DARK_4;
        case ScatterGround::Dirt:
            return t == PIX_DIRT_1;
        case ScatterGround::DarkDirt:
            return t == PIX_DIRT_DARK_1;
        case ScatterGround::Snow:
            return t == PIX_SNOW1 || t == PIX_SNOW2;
        case ScatterGround::Marsh:
            return t == PIX_MARSH1 || t == PIX_MARSH2;
        case ScatterGround::Ash:
            return t == PIX_ASH1 || t == PIX_ASH2;
        case ScatterGround::Path:
            return t == PIX_PATH_1 || t == PIX_PATH_2 || t == PIX_PATH_3 ||
                   t == PIX_PATH_4;
        case ScatterGround::Pavement:
            return t == PIX_PAVEMENT1 || t == PIX_PAVEMENT2 ||
                   t == PIX_PAVEMENT3;
        case ScatterGround::Cobble:
            return t == PIX_COBBLE_1 || t == PIX_COBBLE_2 ||
                   t == PIX_COBBLE_3 || t == PIX_COBBLE_4;
    }
    return false;
}

} // namespace

void scatter_decor(GameWorld& w, int floor, int tx0, int ty0, int tx1,
                   int ty1, int modulus, unsigned char decor_id,
                   std::initializer_list<ScatterGround> grounds)
{
    // Ambience only: a blocking id here could mine a route or a fall
    // landing the audits would then have to re-prove — refuse it outright.
    if (decor_id >= DECOR_MAX ||
        kDecorRegistry[decor_id].pass != DecorPassability::None)
    {
        fail(std::format("scatter_decor: id {} is not non-blocking ambience "
                         "decor", decor_id));
        return;
    }
    const PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            // A different cell hash than the boulder/litter scatters so the
            // dressings interleave instead of stacking on the same cells.
            if ((x * 5 + y * 7) % modulus != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[x + y * g.w];
            bool allowed = false;
            for (const ScatterGround ground : grounds)
                if (scatter_ground_matches(t, ground))
                {
                    allowed = true;
                    break;
                }
            if (!allowed)
                continue;
            const PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && x < dec.w && y < dec.h &&
                dec.data[x + y * dec.w] != DECOR_NONE)
                continue; // hand-placed decor keeps its cell
            if (cell_near_entity(w, floor, x, y, 0))
                continue; // no one spawns standing in the set dressing
            paint_decor(w, floor, x, y, decor_id);
        }
}

void save_level_files(GameWorld& world, int id, const char* title,
                      const std::vector<std::string>& description,
                      int par_value, int time_bonus_limit)
{
    world.title = title;
    world.par_value = static_cast<short>(par_value);
    world.time_bonus_limit = static_cast<short>(time_bonus_limit);

    // SAVE_ALL scoping (Wave F2): on every SAVE_ALL level the Bearer — and
    // ONLY him — carries npc_flags bit 2 ("protected"). With any flagged
    // NPC placed, the engine watches ONLY flagged walkers, so the other
    // named allies (Ranger-King, The Lady, White Rider) and any archmage
    // summons are no longer mission-fail conditions. Centralized here so a
    // level builder cannot forget the flag or hand it to anyone else.
    if ((world.type & SCEN_TYPE_SAVE_ALL) != 0)
    {
        int flagged = 0;
        for (auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Living)
                continue;
            if (ob->stats()->name == "The Bearer")
            {
                ob->set_save_all_protected(true);
                ++flagged;
            }
        }
        if (flagged != 1)
            fail(std::format("scen{}: a SAVE_ALL level must carry exactly one "
                             "'The Bearer' to protect, found {}", id, flagged));
    }

    og::data::LevelFileMetadata metadata;
    metadata.grid_file = std::format("scen{:04d}", id);
    metadata.generated = true; // provenance mark: this scen is tool output
    for (const std::string& line : description)
        metadata.description.push_back(line);

    const std::string user = get_user_path();
    const std::string fss = user + std::format("temp/scen/scen{}.fss", id);
    og::data::LevelFileIoError err = og::data::LevelFileIoError::None;
    if (!og::data::save_level_scenario_file(world, fss, metadata, &err))
    {
        fail(std::format("failed to write {}", fss));
        return;
    }
    // Floor 0 grid, then extra floors by derived name "{grid}_f{N}".
    const std::string base = user + "temp/pix/" + metadata.grid_file;
    if (!write_pixie_png((base + ".png").c_str(), world.grid))
        fail(std::format("failed to write {}.png", base));
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const std::string p = std::format("{}_f{}.png", base, f);
        if (!write_pixie_png(p.c_str(), world.grid_for_floor(f)))
            fail(std::format("failed to write {}", p));
    }
    // Decor planes by derived name "{grid}_d{N}" (including floor 0), for
    // exactly the floors the .fss payload flags as present: the writer's
    // predicate is "valid plane with at least one nonzero byte", so an
    // all-zero plane is treated as absent on both sides.
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const std::size_t cells =
            static_cast<std::size_t>(dec.w) * static_cast<std::size_t>(dec.h);
        bool nonzero = false;
        for (std::size_t c = 0; c < cells && !nonzero; ++c)
            nonzero = dec.data[c] != 0;
        if (!nonzero)
            continue;
        const std::string p = std::format("{}_d{}.png", base, f);
        if (!write_pixie_png(p.c_str(), dec))
            fail(std::format("failed to write {}", p));
    }
    std::printf("westlands_mapgen: built %d '%s' (%d floors)\n", id, title,
                world.floor_count());
}

void init_world(LevelRuntimeData& level, int floors, int tw, int th)
{
    GameWorld& world = level.world();
    world.grid = make_grid(tw, th, PIX_GRASS1);
    world.pixmaxx = world.grid.w * GRID_SIZE;
    world.pixmaxy = world.grid.h * GRID_SIZE;
    world.mysmoother.set_target(world.grid);
    if (floors > 1)
    {
        world.set_floor_count(floors);
        for (int f = 1; f < floors; ++f)
        {
            world.grid_for_floor(f) = make_grid(tw, th, PIX_GRASS1);
            world.smoother_for_floor(f).set_target(world.grid_for_floor(f));
        }
    }
}

namespace {

void write_campaign_yaml(const std::string& path)
{
    std::ofstream out(path);
    out << "format_version:  1\n"
        << "title:           War of the Westlands\n"
        << "version:         1\n"
        << "first_level:     1\n"
        << "suggested_power: 0\n"
        << "authors:         OpenGlad\n"
        << "contributors:    \n"
        << "\n"
        << "description:     |\n"
        << "    The Westlands burn. A small\n"
        << "    burden must cross a great war:\n"
        << "    the flight through the forest,\n"
        << "    the dark road under the mountain,\n"
        << "    sieges of wall and gate and city,\n"
        << "    marsh and spider-pass and ash —\n"
        << "    until two roads meet again at\n"
        << "    the Mountain of Fire, and all\n"
        << "    is weighed in the crack of the\n"
        << "    world.\n";
    if (!out)
        fail(std::format("cannot write {}", path));
}

// A 32x32 icon: the White Tree — a bare white tree with three tiers of
// upswept branches on a pure black field. Painted in the engine palette's
// grey ramp (16..31 runs dark -> bright; 31 is the brightest white). No
// cycled palette bands anywhere: the sigil must not shimmer.
void write_icon(const std::string& path)
{
    constexpr int kSize = 32;
    PixieData icon = make_grid(kSize, kSize, 0); // the black field
    auto put = [&](int x, int y, unsigned char c) { paint(icon, x, y, c); };
    for (int x = 11; x <= 20; ++x)              // the root mound
        for (int y = 28; y <= 29; ++y)
            put(x, y, 24);
    for (int y = 10; y <= 27; ++y)              // the trunk, two shaded columns
    {
        put(15, y, 28);
        put(16, y, 31);
    }
    for (int i = 0; i <= 6; ++i)                // lower branch pair
    {
        put(14 - i, 21 - i, 30);
        put(17 + i, 21 - i, 30);
    }
    for (int i = 0; i <= 5; ++i)                // middle branch pair
    {
        put(14 - i, 17 - i, 30);
        put(17 + i, 17 - i, 30);
    }
    for (int i = 0; i <= 4; ++i)                // upper branch pair
    {
        put(14 - i, 13 - i, 30);
        put(17 + i, 13 - i, 30);
    }
    static constexpr int tips[6][2] = {{8, 15}, {23, 15}, {9, 12},
                                       {22, 12}, {10, 9}, {21, 9}};
    for (const auto& t : tips)                  // brightest buds at the tips
        put(t[0], t[1], 31);
    put(15, 7, 31);                             // the crown
    put(16, 7, 31);
    put(14, 8, 31);
    put(17, 8, 31);
    put(15, 6, 26);                             // a faint taper above it
    put(16, 6, 26);
    put(13, 27, 26);                            // root flare
    put(18, 27, 26);
    if (!write_pixie_png(path.c_str(), icon))
        fail(std::format("cannot write {}", path));
}

// The full designed level graph: ids 1-26, contiguous except 18 (act gap).
// Every level is now registered, so exit-destination validation is a hard
// failure: any exit naming a level absent from the package fails the build.
constexpr bool kRequireAllDestinationsBuilt = true;

bool is_planned_level(int id)
{
    return (id >= 1 && id <= 17) || (id >= 19 && id <= 26);
}

std::string join_ids(const std::vector<int>& ids)
{
    std::string out;
    for (const int id : ids)
    {
        if (!out.empty())
            out += ", ";
        out += std::to_string(id);
    }
    return out;
}

// SCENARIO INFORMATION dialog budget (33 glyphs per briefing line).
constexpr std::size_t kBriefingLineBudget = 33;

// --- Reachability audit (Wave E3). -----------------------------------------
// Multi-floor A* path-state encoding (walker_pathing.cpp's MAKE_STATE).
PathState make_path_state(int x, int y, int floor)
{
    return reinterpret_cast<PathState>(
        static_cast<intptr_t>(floor) * FLOOR_STRIDE +
        static_cast<intptr_t>(y / GRID_SIZE) * MAP_WIDTH + (x / GRID_SIZE));
}

// DELIBERATELY ground-unreachable placements, matched by (level, floor,
// tile). Flyers are already exempt (ghosts hover over lava, meres and air
// pits by design); everything ELSE a ground probe cannot reach from the
// lead start marker fails the build unless it is listed here with a reason.
// Keep this table in lockstep with tests/unit/test_westlands_levels.cpp.
struct ReachabilityException
{
    int id;
    int floor;
    int tx;
    int ty;
};
const std::vector<ReachabilityException>& reachability_exceptions()
{
    static const std::vector<ReachabilityException> exceptions = {
        // (none: every ground living and generator in the shipped package
        //  must be reachable — the kill-all levels demand it, and the
        //  CAN_EXIT levels promise the player no foe is sealed away.)
    };
    return exceptions;
}

bool reachability_exception_allowed(int id, int floor, int tx, int ty)
{
    for (const ReachabilityException& e : reachability_exceptions())
        if (e.id == id && e.floor == floor && e.tx == tx && e.ty == ty)
            return true;
    return false;
}

void self_check_level(const ExpectedLevel& ex, const std::set<int>& registered)
{
    LevelRuntimeData level(ex.id, true, &headless_level_data_hooks());
    if (!level.load())
    {
        fail(std::format("self-check: scen{} failed to load", ex.id));
        return;
    }
    GameWorld& world = level.world();
    if (world.floor_count() != ex.floors)
        fail(std::format("self-check scen{}: floor_count {} != expected {}",
                         ex.id, world.floor_count(), ex.floors));
    if (world.title != ex.title)
        fail(std::format("self-check scen{}: title '{}' != '{}'", ex.id,
                         world.title, ex.title));
    // Every floor grid must have loaded (extra-floor PNGs round-tripped).
    for (int f = 0; f < world.floor_count(); ++f)
        if (!world.grid_for_floor(f).valid())
            fail(std::format("self-check scen{}: floor {} grid invalid", ex.id, f));

    // Decor plane audit (BASE + DECOR layering, .fss v11): planes that
    // round-tripped must match their floor grid's dims, carry only known
    // decor ids, and never sit over air / Z-stair / void bases.
    for (int f = 0; f < world.floor_count(); ++f)
    {
        const PixieData& dec = world.decor_for_floor(f);
        if (!dec.valid())
            continue;
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid() || dec.w != g.w || dec.h != g.h)
        {
            fail(std::format("self-check scen{}: floor {} decor plane dims "
                             "mismatch", ex.id, f));
            continue;
        }
        for (int ty = 0; ty < dec.h; ++ty)
            for (int tx = 0; tx < dec.w; ++tx)
            {
                const unsigned char d = dec.data[tx + ty * dec.w];
                if (d >= DECOR_MAX)
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor id {} out of range",
                                     ex.id, f, tx, ty, d));
                const unsigned char base = g.data[tx + ty * g.w];
                if (d != DECOR_NONE &&
                    (base == PIX_AIR || base == PIX_ZSTAIR_UP ||
                     base == PIX_ZSTAIR_DOWN || base == PIX_VOID1))
                {
                    fail(std::format("self-check scen{}: floor {} cell "
                                     "({}, {}) decor {} over air/stair/void",
                                     ex.id, f, tx, ty, d));
                }
            }
    }

    for (const std::string& line : level.description)
        if (line.size() > kBriefingLineBudget)
            fail(std::format("self-check scen{}: briefing line '{}' overflows "
                             "the {}-char budget", ex.id, line,
                             kBriefingLineBudget));

    // Army audit: exact per-team living/generator/marker counts, the v10
    // per-NPC extras round-trip, and the seeded armies must leave headroom
    // under the engine's living cap.
    int livings[MAX_TEAM + 1] = {};
    int generators[MAX_TEAM + 1] = {};
    int total_livings = 0;
    int starts = 0;
    int delayed = 0;
    int no_specials = 0;
    int protected_walkers = 0;
    bool protected_is_the_bearer = true;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr)
            continue;
        if (ob->spawn_delay() > 0)
            ++delayed;
        if (ob->specials_disabled())
            ++no_specials;
        // Guard wake-policy audit: an allied (team 0/1) guard exists to hold
        // a post — Bearer escorts, door-wards, garrison lines. If the
        // hold-post bit failed to round-trip, the wake rule would march the
        // whole garrison off its chokepoint at first sight of the enemy.
        // Enemy guards are deliberately left waking (ambush posts); an
        // authored enemy statue is legal but must be set explicitly.
        if (ob->query_order() == Order::Living &&
            ob->act_type() == ACT_GUARD && ob->team_num() <= 1 &&
            !ob->guard_hold_post())
            fail(std::format("self-check scen{}: allied guard (family {}, "
                             "team {}) at ({}, {}) is missing hold-post",
                             ex.id, static_cast<int>(ob->family()),
                             static_cast<int>(ob->team_num()),
                             ob->xpos() / GRID_SIZE, ob->ypos() / GRID_SIZE));
        if (ob->save_all_protected())
        {
            ++protected_walkers;
            if (ob->query_order() != Order::Living ||
                ob->stats()->name != "The Bearer")
                protected_is_the_bearer = false;
        }
        const int team = ob->team_num();
        if (team < 0 || team > MAX_TEAM)
            continue;
        if (ob->query_order() == Order::Living)
        {
            ++livings[team];
            ++total_livings;
        }
        else if (ob->query_order() == Order::Generator)
            ++generators[team];
        else if (ob->query_order() == Order::Special &&
                 ob->family() == FAMILY_RESERVED_TEAM && team == 0)
            ++starts;
    }
    if (starts != ex.start_markers)
        fail(std::format("self-check scen{}: {} start markers, expected {}",
                         ex.id, starts, ex.start_markers));
    if (livings[0] != ex.team0_livings || generators[0] != ex.team0_generators)
        fail(std::format("self-check scen{}: team 0 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[0],
                         generators[0], ex.team0_livings, ex.team0_generators));
    if (livings[1] != ex.team1_livings || generators[1] != ex.team1_generators)
        fail(std::format("self-check scen{}: team 1 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[1],
                         generators[1], ex.team1_livings, ex.team1_generators));
    if (livings[2] != ex.team2_livings || generators[2] != ex.team2_generators)
        fail(std::format("self-check scen{}: team 2 has {} livings / {} "
                         "generators, expected {} / {}", ex.id, livings[2],
                         generators[2], ex.team2_livings, ex.team2_generators));
    if (delayed != ex.delayed_spawns || no_specials != ex.specials_disabled)
        fail(std::format("self-check scen{}: {} delayed spawns / {} "
                         "specials-disabled NPCs, expected {} / {}", ex.id,
                         delayed, no_specials, ex.delayed_spawns,
                         ex.specials_disabled));
    if (total_livings > MAXOBS)
        fail(std::format("self-check scen{}: {} seeded livings exceed the "
                         "MAXOBS={} living cap", ex.id, total_livings, MAXOBS));

    // SAVE_ALL scoping audit (Wave F2): npc_flags bit 2 must round-trip
    // through the package on exactly the Bearer — one protected Living named
    // "The Bearer" on every SAVE_ALL level, zero protected walkers anywhere
    // else. (The engine narrows the SAVE_ALL watch to flagged walkers the
    // moment any is present, so a stray flag would silently rewrite a
    // level's loss condition.)
    const int expected_protected =
        ((world.type & SCEN_TYPE_SAVE_ALL) != 0) ? 1 : 0;
    if (protected_walkers != expected_protected || !protected_is_the_bearer)
        fail(std::format("self-check scen{}: {} protected walkers (bearer-only "
                         "{}), expected {} on a {} level", ex.id,
                         protected_walkers, protected_is_the_bearer,
                         expected_protected,
                         expected_protected == 1 ? "SAVE_ALL" : "non-SAVE_ALL"));

    // Exit audit: the authored destination set must match the design graph
    // exactly, and every destination must exist in the package (warn-only
    // for planned-but-not-yet-built levels until the final integrate phase).
    std::vector<int> destinations;
    for (const auto& uptr : world.fxlist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Treasure &&
            ob->family() == FAMILY_EXIT)
        {
            destinations.push_back(static_cast<int>(ob->stats()->level()));
        }
    }
    std::vector<int> expected_dests = ex.exit_destinations;
    std::sort(destinations.begin(), destinations.end());
    std::sort(expected_dests.begin(), expected_dests.end());
    if (destinations != expected_dests)
        fail(std::format("self-check scen{}: exit destinations [{}] != "
                         "expected [{}]", ex.id, join_ids(destinations),
                         join_ids(expected_dests)));
    for (const int dest : destinations)
    {
        if (registered.count(dest) != 0)
            continue;
        if (!kRequireAllDestinationsBuilt && is_planned_level(dest))
        {
            warn(std::format("scen{}: exit destination {} is planned but not "
                             "yet built", ex.id, dest));
        }
        else
        {
            fail(std::format("self-check scen{}: exit destination {} does not "
                             "exist in the package", ex.id, dest));
        }
    }

    // Stair audit: each floor boundary reachable through at least one
    // vertically-aligned UP/DOWN pair.
    if (ex.stairs_every_boundary)
    {
        for (int f = 0; f + 1 < world.floor_count(); ++f)
        {
            const PixieData& lo = world.grid_for_floor(f);
            const PixieData& hi = world.grid_for_floor(f + 1);
            int pairs = 0;
            const int cells = lo.w * lo.h;
            for (int i = 0; i < cells; ++i)
                if (lo.data[i] == PIX_ZSTAIR_UP && hi.data[i] == PIX_ZSTAIR_DOWN)
                    ++pairs;
            if (pairs < 1)
                fail(std::format("self-check scen{}: no aligned stair pair on "
                                 "floor boundary {}<->{}", ex.id, f, f + 1));
        }
    }

    // Stair-clearance audit (B2 tooling rule, docs/z-axis-design.md): the
    // engine's blocked-arrival nudge rescues a blocked stair at runtime, but
    // no shipped level may RELY on it — and before the nudge an IMMOBILE
    // blocker (an ACT_GUARD post, a generator, or ground-blocking decor)
    // sitting on a stair cell sealed the staircase outright ("the stairs are
    // blocked from above so I can't ascend"). Rule: neither the stair-pair
    // cell nor any of its 4-neighborhood arrival cells, on EITHER floor of
    // the pair, may hold an ACT_GUARD post, a generator, or BlocksGround
    // decor. Roaming livings are fine (they move off; the nudge covers the
    // transient). Mirrored by tests/unit/test_westlands_levels.cpp.
    {
        static constexpr int kArrivalOffsets[5][2] = {
            {0, 0}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        auto audit_arrival_cell = [&](int pf, int nx, int ny)
        {
            const PixieData& g = world.grid_for_floor(pf);
            if (!g.valid() || nx < 0 || ny < 0 || nx >= g.w || ny >= g.h)
                return;
            const PixieData& dec = world.decor_for_floor(pf);
            if (dec.valid() && nx < dec.w && ny < dec.h)
            {
                const unsigned char d = dec.data[nx + ny * dec.w];
                if (d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                {
                    fail(std::format(
                        "self-check scen{}: blocking decor {} on stair "
                        "cell/arrival ({}, {}) floor {}", ex.id, d, nx, ny,
                        pf));
                }
            }
            const int x0 = nx * GRID_SIZE;
            const int y0 = ny * GRID_SIZE;
            for (const auto& uptr : world.oblist)
            {
                walker* ob = uptr.get();
                if (ob == nullptr || ob->floor() != pf)
                    continue;
                const Order order = ob->query_order();
                const bool immobile_post =
                    order == Order::Generator ||
                    (order == Order::Living &&
                     ob->act_type() == ACT_GUARD);
                if (!immobile_post)
                    continue;
                if (ob->xpos() + ob->sizex() > x0 &&
                    ob->xpos() < x0 + GRID_SIZE &&
                    ob->ypos() + ob->sizey() > y0 &&
                    ob->ypos() < y0 + GRID_SIZE)
                {
                    fail(std::format(
                        "self-check scen{}: {} (family {}) posted on stair "
                        "cell/arrival ({}, {}) floor {} would seal the "
                        "staircase", ex.id,
                        order == Order::Generator ? "generator"
                                                  : "ACT_GUARD post",
                        static_cast<int>(ob->family()), nx, ny, pf));
                }
            }
        };
        for (int f = 0; f + 1 < world.floor_count(); ++f)
        {
            const PixieData& lo = world.grid_for_floor(f);
            const PixieData& hi = world.grid_for_floor(f + 1);
            if (!lo.valid() || !hi.valid())
                continue;
            for (int ty = 0; ty < lo.h; ++ty)
                for (int tx = 0; tx < lo.w; ++tx)
                {
                    const int i = tx + ty * lo.w;
                    if (lo.data[i] != PIX_ZSTAIR_UP ||
                        hi.data[i] != PIX_ZSTAIR_DOWN)
                        continue;
                    for (const auto& off : kArrivalOffsets)
                    {
                        audit_arrival_cell(f, tx + off[0], ty + off[1]);
                        audit_arrival_cell(f + 1, tx + off[0], ty + off[1]);
                    }
                }
        }
    }

    // Fall-line audit (Wave E5): any AIR cell a ground walker can actually
    // step into — 8-adjacent to a standable cell of the SAME floor — must
    // land its faller cleanly. Chase the column down through stacked AIR;
    // the landing cell must be standable (base AND decor): never a wall
    // top, water, lava or blocking decor. Falling past floor 0 is a pit
    // death, a designed mechanic, and stays legal. The engine's A5 nudge
    // can rescue a blocked landing, but no level may RELY on the nudge.
    // Mirrored by tests/unit/test_westlands_levels.cpp.
    //
    // Fall-DEPTH audit (fall damage, docs/z-axis-design.md): the same walk
    // now also measures each designed fall line's depth in stories (entry
    // floor minus landing floor). Report-only for the per-level max and the
    // >= 2-story count (those lines now cost HP — each gets a design
    // ruling: optional-shortcut keeps, mandatory-path reroutes); a line
    // deeper than 4 stories (the 50%-cap knee) is a self-check failure.
    // Generation is untouched: committed .glads stay byte-identical.
    int max_fall_depth = 0;
    int damaging_fall_lines = 0;
    for (int f = 1; f < world.floor_count(); ++f)
    {
        const PixieData& g = world.grid_for_floor(f);
        if (!g.valid())
            continue;
        for (int ty = 0; ty < g.h; ++ty)
        {
            for (int tx = 0; tx < g.w; ++tx)
            {
                if (g.data[tx + ty * g.w] != PIX_AIR)
                    continue;
                bool fall_entry = false;
                for (int dy = -1; dy <= 1 && !fall_entry; ++dy)
                    for (int dx = -1; dx <= 1 && !fall_entry; ++dx)
                        if ((dx != 0 || dy != 0) &&
                            cell_standable(world, f, tx + dx, ty + dy))
                            fall_entry = true;
                if (!fall_entry)
                    continue; // open sky no walker can step into
                int lf = f - 1;
                while (lf > 0 &&
                       world.grid_for_floor(lf).data[tx + ty * g.w] == PIX_AIR)
                    --lf;
                if (world.grid_for_floor(lf).data[tx + ty * g.w] == PIX_AIR)
                    continue; // fell past floor 0: pit death by design
                if (!cell_standable(world, lf, tx, ty))
                {
                    fail(std::format(
                        "self-check scen{}: fall line at tile ({}, {}) floor "
                        "{} lands on impassable ground of floor {} (the level "
                        "must not rely on the engine's landing nudge)",
                        ex.id, tx, ty, f, lf));
                }
                const int depth = f - lf;
                max_fall_depth = std::max(max_fall_depth, depth);
                if (depth >= 2)
                    ++damaging_fall_lines;
                if (depth > 4)
                {
                    fail(std::format(
                        "self-check scen{}: fall line at tile ({}, {}) floor "
                        "{} drops {} stories to floor {} — deeper than the "
                        "4-story fall-damage cap knee (reroute or shallow "
                        "the shaft)", ex.id, tx, ty, f, depth, lf));
                }
            }
        }
    }
    if (world.floor_count() > 1)
    {
        std::printf("westlands_mapgen: scen%d fall-depth: max %d stories, "
                    "%d damaging (>=2-story) fall line(s)\n",
                    ex.id, max_fall_depth, damaging_fall_lines);
    }

    // Footing audit: every authored entity stands on a tile of its own floor
    // that its own footprint can occupy, and no ground troop spawns over air.
    auto check_footing = [&](walker* ob)
    {
        if (ob == nullptr)
            return;
        if (!world.query_grid_passable(static_cast<float>(ob->xpos()),
                                       static_cast<float>(ob->ypos()), ob,
                                       ob->floor()))
        {
            fail(std::format(
                "self-check scen{}: order {} family {} at tile ({}, {}) floor "
                "{} stands on impassable ground", ex.id,
                static_cast<int>(ob->query_order()),
                static_cast<int>(ob->family()),
                ob->xpos() / GRID_SIZE, ob->ypos() / GRID_SIZE, ob->floor()));
        }
        if (ob->query_order() == Order::Living &&
            !ob->stats()->query_bit_flags(BIT_FLYING))
        {
            const PixieData& g = world.grid_for_floor(ob->floor());
            const int tx = (ob->xpos() + ob->sizex() / 2) / GRID_SIZE;
            const int ty = (ob->ypos() + ob->sizey() / 2) / GRID_SIZE;
            if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h &&
                g.data[tx + ty * g.w] == PIX_AIR)
            {
                fail(std::format(
                    "self-check scen{}: ground unit family {} at tile ({}, {}) "
                    "floor {} spawns over air", ex.id,
                    static_cast<int>(ob->family()), tx, ty, ob->floor()));
            }
            // No ground footprint on BlocksGround decor: the scatter keeps
            // entity clearance and hand-placed decor must not pin troops.
            const PixieData& dec = world.decor_for_floor(ob->floor());
            if (dec.valid() && tx >= 0 && ty >= 0 && tx < dec.w &&
                ty < dec.h)
            {
                const unsigned char d = dec.data[tx + ty * dec.w];
                if (d < DECOR_MAX &&
                    kDecorRegistry[d].pass == DecorPassability::BlocksGround)
                {
                    fail(std::format(
                        "self-check scen{}: ground unit family {} at tile "
                        "({}, {}) floor {} spawns on blocking decor {}",
                        ex.id, static_cast<int>(ob->family()), tx, ty,
                        ob->floor(), d));
                }
            }
        }
    };
    for (const auto& uptr : world.oblist)
        check_footing(uptr.get());
    for (const auto& uptr : world.fxlist)
        check_footing(uptr.get());

    // Reachability audit (Wave E3): every living and every generator must
    // be A*-reachable from the crew's lead start marker by a ground probe,
    // respecting passability, air holes, lava and Z-stairs. Kill-all
    // levels demand the player can close with every foe; the allowlist
    // above is the only escape hatch for deliberate exceptions.
    walker* lead = nullptr;
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Special &&
            ob->family() == FAMILY_RESERVED_TEAM && ob->team_num() == 0)
        {
            lead = ob;
            break;
        }
    }
    if (lead == nullptr || current_game == nullptr ||
        world.myobmap == nullptr)
    {
        fail(std::format("self-check scen{}: reachability audit needs a lead "
                         "start marker, a gameplay context and an obmap",
                         ex.id));
        return;
    }
    GameWorld* const prev_world = current_game->world;
    current_game->world = &world;
    // A ground probe on the lead marker; removed from the obmap so it
    // never self-blocks a solve.
    walker* probe = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (probe == nullptr)
    {
        fail(std::format("self-check scen{}: could not seed the reachability "
                         "probe", ex.id));
        current_game->world = prev_world;
        return;
    }
    probe->set_team_num(0);
    probe->set_real_team_num(0);
    probe->set_floor(lead->floor());
    probe->setxy(lead->xpos(), lead->ypos());
    (void)world.myobmap->remove(probe);
    GameplayPathfindingState* pathing = ensure_pathfinding_state(*current_game);
    const PathState start =
        make_path_state(probe->xpos(), probe->ypos(), probe->floor());
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob == nullptr || ob == probe)
            continue;
        const Order order = ob->query_order();
        if (order != Order::Living && order != Order::Generator)
            continue;
        if (order == Order::Living &&
            ob->stats()->query_bit_flags(BIT_FLYING))
        {
            continue; // flyers cross lava/water/pits by design
        }
        const int tx = ob->xpos() / GRID_SIZE;
        const int ty = ob->ypos() / GRID_SIZE;
        if (reachability_exception_allowed(ex.id, ob->floor(), tx, ty))
            continue;
        const PathState goal =
            make_path_state(ob->xpos(), ob->ypos(), ob->floor());
        if (goal == start)
            continue;
        std::vector<PathState> path;
        float total_cost = 0.0f;
        pathing->solve_for_point(probe, static_cast<short>(ob->xpos()),
                                 static_cast<short>(ob->ypos()), start, goal,
                                 path, total_cost);
        if (path.empty())
        {
            fail(std::format(
                "self-check scen{}: order {} family {} at tile ({}, {}) "
                "floor {} is unreachable from the lead start marker "
                "(fix the map or add a reachability-allowlist entry)",
                ex.id, static_cast<int>(order),
                static_cast<int>(ob->family()), tx, ty, ob->floor()));
        }
    }
    current_game->world = prev_world;
}

} // namespace
} // namespace westlands

int main(int argc, char* argv[])
{
    using namespace westlands;
    namespace fs = std::filesystem;

    const std::string out_tree =
        (argc > 1) ? argv[1] : "campaigns/westlands";
    const fs::path out_abs = fs::absolute(out_tree);

    fs::path scratch;
    if (const char* preset = std::getenv("OPENGLAD_CONFIG_DIR");
        preset == nullptr || preset[0] == '\0')
    {
        scratch = fs::temp_directory_path() /
                  ("westlands_mapgen_" + std::to_string(getpid()));
        fs::create_directories(scratch);
        setenv("OPENGLAD_CONFIG_DIR", scratch.c_str(), 1);
    }

    init_logging();
    io_init(argc, argv);
    if (get_mounted_campaign() != "gladiator")
    {
        std::fprintf(stderr, "westlands_mapgen: ERROR: stock campaign not "
                             "mounted; run next to staged assets (build dir)\n");
        io_exit();
        return 1;
    }
    cfg.load_settings();
    init_all_registries();

    og::runtime::SessionState& session = og::runtime::s_mapgen_session;
    static FixedRandom mapgen_rng{0};
    static GameWorld fallback_world(0);
    static SaveData fallback_save;
    session.ctx_.rng = &mapgen_rng;
    session.game_.world = &fallback_world;
    session.game_.save = &fallback_save;
    session.game_.sim_events = session.ctx_.sim_events.get();
    session.game_.config = &cfg;
    session.game_.session_rng_ref = &session.ctx_.rng;
    session.game_.gameplay_active_ref = &session.gameplay_active_;
    current_game = &session.game_;

    const std::string user = get_user_path();
    cleanup_unpacked_campaign();
    create_dir(user + "temp/");
    create_dir(user + "temp/scen/");
    create_dir(user + "temp/pix/");
    write_campaign_yaml(user + "temp/campaign.yaml");
    write_icon(user + "temp/icon.png");

    const LevelDataHooks& hooks = headless_level_data_hooks();
    build_act1(hooks);
    build_act2(hooks);
    build_act3a(hooks);
    build_act3b(hooks);
    build_finale(hooks);

    std::vector<ExpectedLevel> expectations;
    for (auto rows : {act1_expectations(), act2_expectations(),
                      act3a_expectations(), act3b_expectations(),
                      finale_expectations()})
    {
        for (ExpectedLevel& row : rows)
            expectations.push_back(std::move(row));
    }
    std::set<int> registered;
    for (const ExpectedLevel& e : expectations)
    {
        if (!is_planned_level(e.id))
            fail(std::format("scen{} is not in the planned level graph", e.id));
        if (!registered.insert(e.id).second)
            fail(std::format("scen{} registered twice", e.id));
    }

    const std::string glad_path = user + "campaigns/westlands.glad";
    std::remove(glad_path.c_str());
    if (zip_contents_with_error(user + "temp/", glad_path) != ArchiveIoError::None)
        fail(std::format("failed to zip campaign into {}", glad_path));

    if (g_errors == 0)
    {
        if (mount_campaign_package_with_error("westlands") !=
            CampaignPackageIoError::None)
        {
            fail("failed to mount the produced campaign");
        }
        else
        {
            for (const ExpectedLevel& e : expectations)
                self_check_level(e, registered);
            (void)unmount_campaign_package_with_error("westlands");
        }
    }

    int result = 1;
    if (g_errors == 0)
    {
        if (!og::toolexport::export_campaign_tree(user + "temp/", out_abs))
            fail(std::format("failed to export the campaign tree to {}",
                             out_abs.string()));
        else
        {
            std::printf("westlands_mapgen: wrote %s\n", out_abs.c_str());
            result = 0;
        }
    }

    cleanup_unpacked_campaign();
    io_exit();
    if (!scratch.empty())
    {
        std::error_code ec;
        fs::remove_all(scratch, ec);
    }
    if (result != 0)
        std::fprintf(stderr, "westlands_mapgen: FAILED with %d error(s)\n",
                     g_errors);
    return result;
}
