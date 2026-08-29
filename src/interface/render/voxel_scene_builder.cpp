/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Scene builder: GameWorld + LevelVisuals -> VoxelScene, in today's exact
// draw order. See docs/voxel-render-design.md §9.

#include <openglad/interface/render/voxel_scene_builder.h>

#include <openglad/core/constants.h>
#include <openglad/core/decordefs.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_visuals.h>

#include <array>

namespace og::render {

namespace {

// Per-PIX extrusion table (§2). Mirrors the genre classes of the
// PIX_to_genre table in src/gameplay/smooth.cpp — TYPE_WALL -> 16,
// TYPE_TREES -> 20, everything else flat — but lives in the RENDERER, which
// is where render-only data belongs.
struct PixHeightTable
{
    std::array<signed char, PIX_MAX> height{};
    std::array<signed char, PIX_MAX> base_z{};
};

constexpr PixHeightTable make_pix_height_table()
{
    PixHeightTable t{};
    for (int i = 0; i < PIX_MAX; ++i)
    {
        t.height[static_cast<std::size_t>(i)] =
            static_cast<signed char>(kVoxelHeightFloor);
        t.base_z[static_cast<std::size_t>(i)] = 0;
    }

    // TYPE_WALL genre (walls, wallsides, arrow walls) + the wall top/side
    // border pieces the OOB loop draws.
    for (int w : {PIX_H_WALL1, PIX_WALL_LL, PIX_WALL2, PIX_WALL3, PIX_WALL4,
                  PIX_WALL5, PIX_WALLSIDE1, PIX_WALLSIDE_L, PIX_WALLSIDE_R,
                  PIX_WALLSIDE_C, PIX_WALLSIDE_CRACK_C1, PIX_WALL_ARROW_GRASS,
                  PIX_WALL_ARROW_FLOOR, PIX_WALL_ARROW_GRASS_DARK,
                  PIX_WALLTOP_H})
    {
        t.height[static_cast<std::size_t>(w)] =
            static_cast<signed char>(kVoxelHeightWall);
    }

    // TYPE_TREES genre.
    for (int w : {PIX_TREE_T1, PIX_TREE_M1, PIX_TREE_ML, PIX_TREE_MR,
                  PIX_TREE_MT, PIX_TREE_B1})
    {
        t.height[static_cast<std::size_t>(w)] =
            static_cast<signed char>(kVoxelHeightTree);
    }

    // Water / lava / marsh / glass: flat, but sunk two pixels.
    for (int w : {PIX_WATER1, PIX_WATER2, PIX_WATER3, PIX_WATERGRASS_LL,
                  PIX_WATERGRASS_LR, PIX_WATERGRASS_UL, PIX_WATERGRASS_UR,
                  PIX_WATERGRASS_U, PIX_WATERGRASS_D, PIX_WATERGRASS_L,
                  PIX_WATERGRASS_R, PIX_GRASSWATER_LL, PIX_GRASSWATER_LR,
                  PIX_GRASSWATER_UL, PIX_GRASSWATER_UR, PIX_LAVA1, PIX_LAVA2,
                  PIX_MARSH1, PIX_MARSH2, PIX_GLASS})
    {
        t.base_z[static_cast<std::size_t>(w)] =
            static_cast<signed char>(kVoxelSunkenBaseZ);
    }

    return t;
}

constexpr PixHeightTable kPixHeights = make_pix_height_table();

// Decor extrusion (§2: 0..12 by kind). Torches and columns stand tall,
// boulders and braziers are chest height, ground litter is flat.
constexpr std::array<signed char, DECOR_MAX> kDecorHeights = {{
    0,  // DECOR_NONE
    12, // DECOR_TORCH1
    12, // DECOR_TORCH2
    12, // DECOR_TORCH3
    10, // DECOR_BRAZIER
    8,  // DECOR_BOULDER_1
    8,  // DECOR_BOULDER_2
    8,  // DECOR_BOULDER_3
    8,  // DECOR_BOULDER_4
    1,  // DECOR_PEBBLES
    12, // DECOR_COLUMN_BOTTOM
    12, // DECOR_COLUMN_TOP
    6,  // DECOR_SHRUB
    1,  // DECOR_BONES
}};

int entity_height(Order order)
{
    switch (order)
    {
    case Order::Weapon:
        return kVoxelHeightWeapon;
    case Order::FX:
        return kVoxelHeightFx;
    case Order::Treasure:
        return kVoxelHeightTreasure;
    default:
        return kVoxelHeightLiving;
    }
}

// Would draw_walker take one of the non-plain walkputbuffer paths for this
// walker? The spike emits every entity with the plain material, so each of
// these is a named pixel deviation rather than a silent one.
bool takes_special_blit(const walker& w)
{
    if (w.hurt_flash())
        return true;
    if (w.outline())
        return true;
    const statistics* const stats = w.stats();
    if (stats == nullptr)
        return false;
    if (stats->query_bit_flags(BIT_PHANTOM))
        return true;
    if (w.invisibility_left())
        return true;
    if (stats->query_bit_flags(BIT_FORESTWALK))
        return true;
    return false;
}

void emit_entity_list(VoxelScene& out, const GameWorld::EntityList& list,
                      int floor, bool multifloor, float floor_z,
                      const VoxelSceneBuildParams& params,
                      VoxelSceneBuildStats& stats)
{
    for (const auto& uptr : list)
    {
        walker* const w = uptr.get();
        if (w == nullptr || w->dead())
            continue;
        if (multifloor && static_cast<int>(w->floor()) != floor)
            continue;
        // draw_walker: dormant walkers are authoring-only.
        if (w->dormant() && !params.draw_dormant)
            continue;
        // draw_walker: the FAMILY_HIT fx gate.
        if (params.skip_hit_fx && w->query_order() == Order::FX &&
            w->family() == FAMILY_HIT)
            continue;
        const unsigned char* const bmp = w->bmp_data();
        if (bmp == nullptr || w->sizex() <= 0 || w->sizey() <= 0)
            continue;

        if (takes_special_blit(*w))
            ++stats.special_mode_entities;

        VoxelVolume v;
        v.texels = bmp;
        v.w = w->sizex();
        v.h = w->sizey();
        v.x = w->worldx();
        v.y = w->worldy();
        v.z = w->worldz() + floor_z;
        v.height = entity_height(w->query_order());
        v.material.team_color = w->query_team_color();
        v.material.opaque = false;
        v.material.lift = (w->query_order() == Order::Weapon);
        // §10: livings become carved models rotated by curdir. Weapons, fx
        // and treasure stay stamps — they have no eight-facing walk cycle to
        // carve from.
        if (params.models != nullptr && w->query_order() == Order::Living)
        {
            float yaw = 0.0f;
            const VoxelModel* const m = params.models->living_model(*w, yaw);
            if (m != nullptr && !m->empty())
            {
                v.model = m;
                v.yaw = yaw;
                // v.x/v.y stay the sprite's top-left, exactly as the Classic
                // camera needs them; the model's own anchor locates its
                // footprint centre inside that box.
            }
        }
        out.emit(v);
        ++stats.entities;
    }
}

void emit_tile(VoxelScene& out, const LevelVisuals& visuals, int tile, int x,
               int y, float floor_z, const VoxelSceneBuildParams& params,
               VoxelSceneBuildStats& stats)
{
    if (tile < 0 || tile >= PIX_MAX)
        return;
    const PixieData& art = visuals.pixdata[tile];
    if (!art.valid() || art.w == 0 || art.h == 0)
        return;
    VoxelVolume v;
    v.texels = art.data.get();
    v.w = art.w;
    v.h = art.h;
    v.x = static_cast<float>(x);
    v.y = static_cast<float>(y);
    v.z = static_cast<float>(kPixHeights.base_z[static_cast<std::size_t>(tile)]) +
          floor_z;
    v.height = kPixHeights.height[static_cast<std::size_t>(tile)];
    // Tiles blit through putbuffer: index 0 is a real colour and the team
    // band is NOT remapped.
    v.material.opaque = true;
    if (params.models != nullptr)
        v.model = params.models->tile_model(tile);
    out.emit(v);
    ++stats.tiles;
}

} // namespace

int voxel_tile_height(int pix)
{
    if (pix < 0 || pix >= PIX_MAX)
        return 0;
    return kPixHeights.height[static_cast<std::size_t>(pix)];
}

float voxel_tile_base_z(int pix)
{
    if (pix < 0 || pix >= PIX_MAX)
        return 0.0f;
    return static_cast<float>(kPixHeights.base_z[static_cast<std::size_t>(pix)]);
}

int voxel_decor_height(int decor)
{
    if (decor <= 0 || decor >= static_cast<int>(DECOR_MAX))
        return 0;
    return kDecorHeights[static_cast<std::size_t>(decor)];
}

VoxelSceneBuildStats build_voxel_scene(VoxelScene& out, GameWorld& world,
                                       const LevelVisuals& visuals,
                                       const VoxelSceneBuildParams& params)
{
    VoxelSceneBuildStats stats;
    const bool multifloor = world.floor_count() > 1;
    const int xneg = (params.topx < 0) ? 1 : 0;
    const int yneg = (params.topy < 0) ? 1 : 0;

    for (int f = params.floor_from; f <= params.floor_to; ++f)
    {
        if (f < 0 || f >= world.floor_count())
            continue;
        const float floor_z = static_cast<float>(f) * params.floor_stride;
        const bool base_floor = (f == 0);
        const PixieData& gridp = world.grid_for_floor(f);
        const PixieData& decorp = world.decor_for_floor(f);
        if (gridp.valid())
        {
            const int maxx = gridp.w;
            const int maxy = gridp.h;
            const bool has_decor =
                decorp.valid() && decorp.w == gridp.w && decorp.h == gridp.h;
            // The tile window viewscreen::redraw walks, verbatim.
            for (int j = (params.topy / GRID_SIZE) - yneg;
                 j < ((params.topy + params.yview) / GRID_SIZE) + 1; ++j)
            {
                for (int i = (params.topx / GRID_SIZE) - xneg;
                     i < ((params.topx + params.xview) / GRID_SIZE) + 1; ++i)
                {
                    if (i < 0 || j < 0 || i >= maxx || j >= maxy)
                    {
                        if (!base_floor)
                            continue; // upper floors: transparent border
                        int border = PIX_WALLTOP_H;
                        if (j == -1 && i > -1 && i < maxx)
                            border = PIX_WALLSIDE1;
                        else if (j == -2 && i > -1 && i < maxx)
                            border = PIX_H_WALL1;
                        emit_tile(out, visuals, border, i * GRID_SIZE,
                                  j * GRID_SIZE, floor_z, params, stats);
                        continue;
                    }
                    const int tile = static_cast<int>(
                        gridp.data[static_cast<std::size_t>(i + maxx * j)]);
                    emit_tile(out, visuals, tile, i * GRID_SIZE, j * GRID_SIZE,
                              floor_z, params, stats);
                    if (!has_decor)
                        continue;
                    const int d = static_cast<int>(
                        decorp.data[static_cast<std::size_t>(i + maxx * j)]);
                    if (d == DECOR_NONE || d >= static_cast<int>(DECOR_MAX))
                        continue;
                    const PixieData& dart = visuals.decor_pixdata[d];
                    if (!dart.valid() || dart.w == 0 || dart.h == 0)
                        continue;
                    VoxelVolume dv;
                    dv.texels = dart.data.get();
                    dv.w = dart.w;
                    dv.h = dart.h;
                    dv.x = static_cast<float>(i * GRID_SIZE);
                    dv.y = static_cast<float>(j * GRID_SIZE);
                    dv.z = floor_z;
                    dv.height = voxel_decor_height(d);
                    // pixie::drawMix passes RED as the (inert) team colour.
                    dv.material.team_color = RED;
                    out.emit(dv);
                    ++stats.decor;
                }
            }
        }
        // Entities, in today's list order: fx, then obs, then weapons.
        emit_entity_list(out, world.fxlist, f, multifloor, floor_z, params,
                         stats);
        emit_entity_list(out, world.oblist, f, multifloor, floor_z, params,
                         stats);
        emit_entity_list(out, world.weaplist, f, multifloor, floor_z, params,
                         stats);
    }

    return stats;
}

} // namespace og::render
