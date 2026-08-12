/* Deterministic level-authoring builders (og::mapgen) — see builders.h.
 *
 * COPIED from tools/westlands_mapgen/main.cpp (grid/paint :112-222,
 * placement :224-329, scatters :331-686, bootstrap :768-784) with the
 * WP-4 adaptations: og::mapgen namespace, no fail()/warn() globals
 * (refusals return false / nullptr), seeded position hashes instead of
 * bare linear cell hashes, and init_world over GameWorld directly.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/gameplay/mapgen/builders.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/smooth.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace og::mapgen {

// Stream salts: keep the boulder / litter / decor scatters (and their
// variant picks) on disjoint hash streams so the dressings interleave
// instead of stacking on the same cells — the seeded replacement for the
// tool's distinct linear hashes ((x*7+y*11) vs (x*5+y*7)).
namespace {
inline constexpr std::uint32_t kSaltBoulderCell = 1;
inline constexpr std::uint32_t kSaltLitterCell = 2;
inline constexpr std::uint32_t kSaltDecorCell = 3;
inline constexpr std::uint32_t kSaltVariant = 16;
} // namespace

std::uint32_t position_hash(std::uint32_t seed, int x, int y,
                            std::uint32_t salt) noexcept
{
    // murmur3-style finalizer over the mixed words: cheap, well-avalanched,
    // and a pure function of its inputs.
    std::uint32_t h = seed ^ (salt * 0x9E3779B9u);
    h ^= static_cast<std::uint32_t>(x) * 0x85EBCA6Bu;
    h ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35u;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

// A grass field of (tw x th) tiles; PixieData owns the heap buffer.
PixieData make_grid(int tw, int th, unsigned char fill)
{
    auto* buf = new unsigned char[static_cast<std::size_t>(tw) * static_cast<std::size_t>(th)];
    std::fill(buf, buf + static_cast<std::size_t>(tw) * static_cast<std::size_t>(th), fill);
    return PixieData(1, static_cast<unsigned char>(tw),
                     static_cast<unsigned char>(th), buf);
}

void paint(PixieData& g, int tx, int ty, unsigned char tile)
{
    if (tx >= 0 && ty >= 0 && tx < g.w && ty < g.h)
        g.data[static_cast<std::size_t>(tx + ty * g.w)] = tile;
}

void paint_rect(PixieData& g, int tx0, int ty0, int tx1, int ty1,
                unsigned char tile)
{
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
            paint(g, x, y, tile);
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

bool paint_decor(GameWorld& w, int floor, int tx, int ty,
                 unsigned char decor_id)
{
    PixieData& g = w.grid_for_floor(floor);
    if (tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (decor_id >= DECOR_MAX)
        return false;
    const unsigned char base = g.data[static_cast<std::size_t>(tx + ty * g.w)];
    if (base == PIX_AIR || base == PIX_ZSTAIR_UP || base == PIX_ZSTAIR_DOWN ||
        base == PIX_VOID1)
    {
        return false;
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
    dec.data[static_cast<std::size_t>(tx + ty * dec.w)] = decor_id;
    return true;
}

void smooth_world(GameWorld& w)
{
    for (int f = 0; f < w.floor_count(); ++f)
        w.smoother_for_floor(f).smooth();
}

void init_world(GameWorld& world, int floors, int tw, int th)
{
    // The tool reached this point through LevelRuntimeData, which guarantees
    // an obmap; a bare scratch GameWorld may not have allocated one yet.
    if (world.myobmap == nullptr)
        world.myobmap = std::make_unique<obmap>();
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

walker* place(GameWorld& world, Order order, int family, int team, int floor,
              int tx, int ty)
{
    walker* w = (order == Order::Treasure) ? world.add_fx_ob(order, family)
                                           : world.add_ob(order, family);
    if (w == nullptr)
        return nullptr;
    w->set_floor(static_cast<short>(floor));
    w->setxy(static_cast<short>(tx * GRID_SIZE),
             static_cast<short>(ty * GRID_SIZE));
    w->set_team_num(static_cast<unsigned char>(team));
    w->set_real_team_num(static_cast<unsigned char>(team));
    return w;
}

walker* place_living(GameWorld& w, int family, int team, int floor, int tx,
                     int ty, int level, bool guard, bool hold_post,
                     bool specials_disabled, int spawn_delay)
{
    walker* ob = place(w, Order::Living, family, team, floor, tx, ty);
    if (ob == nullptr)
        return nullptr;
    ob->stats()->set_level(level);
    if (guard)
    {
        ob->set_act_type(ACT_GUARD);
        // Guard wake policy (npc_flags bit 1): a plain guard is an ambush
        // post — it holds until a foe walks into genuine sight (range +
        // clear ray, walker::act_guard) and then hunts. hold_post makes it
        // the classic stationary sentry instead (escorts, door-wards,
        // chokepoint garrisons — posts that must never march off their
        // spot). The policy is EXPLICITLY the caller's: team number
        // implies nothing here. (An earlier revision inferred hold-post
        // from team <= 1 — the Westlands allied-escort convention — which
        // froze any campaign whose ENEMY garrison lives on team 1.)
        if (hold_post)
            ob->set_guard_hold_post(true);
    }
    ob->set_specials_disabled(specials_disabled);
    ob->set_spawn_delay(static_cast<std::uint16_t>(spawn_delay));
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

void place_exit(GameWorld& w, int floor, int tx, int ty, int destination)
{
    walker* e = place(w, Order::Treasure, FAMILY_EXIT, 0, floor, tx, ty);
    if (e != nullptr)
        e->stats()->set_level(destination);
}

bool cell_near_entity(const GameWorld& w, int floor, int tx, int ty,
                      int margin)
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

// --- Standability / fall-line support (shared with audits.cpp). --------------

// Single-cell ground passability: the Living arm of
// GameWorld::query_grid_passable with none of the flyer / forestwalk /
// ethereal escapes — the tiles a plain ground walker can STAND on. A fall
// landing must be immediately standable; water, lava, boulder and torch
// bases all bounce the faller into the engine's A5 landing nudge, and
// levels must not rely on the nudge.
bool ground_cell_standable(unsigned char tile) noexcept
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

bool cell_standable(const GameWorld& world, int floor, int tx, int ty)
{
    const PixieData& g = world.grid_for_floor(floor);
    if (!g.valid() || tx < 0 || ty < 0 || tx >= g.w || ty >= g.h)
        return false;
    if (!ground_cell_standable(g.data[static_cast<std::size_t>(tx + ty * g.w)]))
        return false;
    const PixieData& dec = world.decor_for_floor(floor);
    if (dec.valid() && dec.w == g.w && dec.h == g.h)
    {
        const unsigned char d = dec.data[static_cast<std::size_t>(tx + ty * dec.w)];
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
// blocking litter or boulders — the same way they never cover a Z-stair.
// On levels whose upper floors are untouched grass fills this is always
// false, so single-floor scatters are unaffected.
bool cell_is_fall_landing(const GameWorld& w, int floor, int tx, int ty)
{
    for (int g = floor + 1; g < w.floor_count(); ++g)
    {
        const PixieData& gg = w.grid_for_floor(g);
        if (!gg.valid() || tx < 0 || ty < 0 || tx >= gg.w || ty >= gg.h ||
            gg.data[static_cast<std::size_t>(tx + ty * gg.w)] != PIX_AIR)
            return false; // solid above: no fall reaches this cell
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx != 0 || dy != 0) &&
                    cell_standable(w, g, tx + dx, ty + dy))
                    return true;
    }
    return false;
}

// --- Seeded scatters. ---------------------------------------------------------

namespace {

// Scatter a 4-variant BASE tile set over a rectangle (jagged litter — a
// full-tile terrain, not decor). Runs AFTER army placement and keeps one
// tile of clearance around every entity (and never covers a Z-stair) so no
// one spawns wedged in the scenery. A full-tile base replaces the whole
// cell, decor included — legacy overwrite semantics.
void scatter_base_tiles(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                        int ty0, int tx1, int ty1, int modulus,
                        std::uint32_t salt,
                        const unsigned char (&variants)[4])
{
    if (modulus <= 0)
        return;
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if (position_hash(seed, x, y, salt) %
                    static_cast<std::uint32_t>(modulus) != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            paint(g, x, y,
                  variants[position_hash(seed, x, y, salt + kSaltVariant) % 4u]);
            PixieData& dec = w.decor_for_floor(floor);
            if (dec.valid() && x < dec.w && y < dec.h)
                dec.data[static_cast<std::size_t>(x + y * dec.w)] = DECOR_NONE;
        }
}

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

void scatter_boulders(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                      int ty0, int tx1, int ty1, int modulus)
{
    // The rock is DECOR over the existing biome ground (snow stays snow
    // under it). An AIR cell keeps the legacy base-tile boulder instead: a
    // decor rock cannot plug a fall-through hole the way a base tile could
    // (paint_decor refuses air by design). Bases that block weapons or
    // flyers (walls, mid-canopy trees) are skipped outright: eroding such
    // cells into weapon-permeable rock would silently breach authored walls
    // — the composition invariant (a BlocksGround decor must reproduce the
    // combined tile's "weapons and flyers pass" semantics exactly) keeps
    // the structures intact instead.
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
    if (modulus <= 0)
        return;
    PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            if (position_hash(seed, x, y, kSaltBoulderCell) %
                    static_cast<std::uint32_t>(modulus) != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
            if (t == PIX_ZSTAIR_UP || t == PIX_ZSTAIR_DOWN || t == PIX_VOID1)
                continue;
            if (blocks_weapons_or_flyers(t))
                continue;
            if (cell_is_fall_landing(w, floor, x, y))
                continue; // never mine a fall landing — and never PLUG an
                          // air hole into one (a base-boulder plug under a
                          // fall entry would be a blocked landing)
            if (cell_near_entity(w, floor, x, y, 1))
                continue;
            const std::uint32_t variant =
                position_hash(seed, x, y, kSaltBoulderCell + kSaltVariant) % 4u;
            if (t == PIX_AIR)
                paint(g, x, y, base_boulders[variant]);
            else
                paint_decor(w, floor, x, y, decor_boulders[variant]);
        }
}

void scatter_litter(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                    int ty0, int tx1, int ty1, int modulus)
{
    static constexpr unsigned char litter[4] = {
        PIX_JAGGED_GROUND_1, PIX_JAGGED_GROUND_2, PIX_JAGGED_GROUND_3,
        PIX_JAGGED_GROUND_4};
    scatter_base_tiles(w, seed, floor, tx0, ty0, tx1, ty1, modulus,
                       kSaltLitterCell, litter);
}

bool scatter_decor(GameWorld& w, std::uint32_t seed, int floor, int tx0,
                   int ty0, int tx1, int ty1, int modulus,
                   unsigned char decor_id,
                   std::initializer_list<ScatterGround> grounds)
{
    // Ambience only: a blocking id here could mine a route or a fall
    // landing the audits would then have to re-prove — refuse it outright.
    if (decor_id >= DECOR_MAX ||
        kDecorRegistry[decor_id].pass != DecorPassability::None)
    {
        return false;
    }
    if (modulus <= 0)
        return true;
    const PixieData& g = w.grid_for_floor(floor);
    for (int y = ty0; y <= ty1; ++y)
        for (int x = tx0; x <= tx1; ++x)
        {
            // A different stream salt than the boulder/litter scatters so
            // the dressings interleave instead of stacking on the same
            // cells.
            if (position_hash(seed, x, y, kSaltDecorCell) %
                    static_cast<std::uint32_t>(modulus) != 0)
                continue;
            if (x < 0 || y < 0 || x >= g.w || y >= g.h)
                continue;
            const unsigned char t = g.data[static_cast<std::size_t>(x + y * g.w)];
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
                dec.data[static_cast<std::size_t>(x + y * dec.w)] != DECOR_NONE)
                continue; // hand-placed decor keeps its cell
            if (cell_near_entity(w, floor, x, y, 0))
                continue; // no one spawns standing in the set dressing
            paint_decor(w, floor, x, y, decor_id);
        }
    return true;
}

} // namespace og::mapgen
