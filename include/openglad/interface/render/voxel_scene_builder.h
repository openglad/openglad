/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Fills a VoxelScene from a live GameWorld + LevelVisuals in TODAY'S EXACT
// draw order (docs/voxel-render-design.md §9): per floor, tiles + decor cell
// by cell over the same i/j window viewscreen::redraw walks, then
// fxlist -> oblist -> weaplist in list order. Rank is the running emission
// counter, so the Classic camera's painter depth is today's painter order.

#include <openglad/interface/render/voxel_scene.h>

class GameWorld;
class walker;
struct LevelVisuals;

namespace og::render {

// §10: where the builder gets carved models from. A null source (the default)
// leaves every volume a stage-1 stamp, so the Classic pass is untouched.
struct VoxelModelSource
{
    virtual ~VoxelModelSource() = default;
    // Carved model for a living walker, plus the world-space yaw its facing
    // implies. Return nullptr to leave it an extruded sprite stamp.
    virtual const VoxelModel* living_model(const walker& w,
                                           float& yaw_rad) const = 0;
    // Terrain model for a base tile id (§10's wall-side and tree-canopy
    // fixes). Return nullptr to leave it a flat extrusion.
    virtual const VoxelModel* tile_model(int pix) const = 0;
};

struct VoxelSceneBuildParams
{
    // The camera window the tile loop walks, in the same terms
    // viewscreen::redraw uses.
    int topx = 0;
    int topy = 0;
    int xview = 0;
    int yview = 0;

    // Inclusive floor range to emit (the Classic pass emits only the camera
    // floor; a Free demo render emits the whole stack).
    int floor_from = 0;
    int floor_to = 0;

    // Vertical spacing between floors. 0 for Classic (D5: the floor layer,
    // not camera geometry, expresses multifloor depth today);
    // kVoxelFloorStride for Free.
    float floor_stride = 0.0f;

    // Mirrors viewscreen::editor_floor_override_ >= 0, which is what makes
    // draw_walker render dormant (delayed-spawn) walkers.
    bool draw_dormant = false;

    // Mirrors cfg "effects"/"hit_anim" == off, which makes draw_walker skip
    // Order::FX walkers of FAMILY_HIT.
    bool skip_hit_fx = false;

    // Optional carved-model source (§10). Borrowed, never owned; the models
    // must outlive the scene. Volumes keep their texels either way, so the
    // Classic camera renders exactly what it renders today.
    const VoxelModelSource* models = nullptr;
};

// Reports what the builder could not represent with the spike's plain
// material, so a nonzero pixel diff has a named cause.
struct VoxelSceneBuildStats
{
    int tiles = 0;
    int decor = 0;
    int entities = 0;
    // Entities the old path draws through a NON-plain blit (hurt flash,
    // outline, invisible, phantom, forestwalk conceal). The spike emits them
    // with the plain material, so each is a known pixel deviation.
    int special_mode_entities = 0;
};

VoxelSceneBuildStats build_voxel_scene(VoxelScene& out, GameWorld& world,
                                       const LevelVisuals& visuals,
                                       const VoxelSceneBuildParams& params);

// Render-only extrusion height for a base tile id (§2 table).
[[nodiscard]] int voxel_tile_height(int pix);
// Render-only base-z offset for a base tile id (water/lava/marsh/glass sink).
[[nodiscard]] float voxel_tile_base_z(int pix);
// Render-only extrusion height for a decor id (§2 table).
[[nodiscard]] int voxel_decor_height(int decor);

} // namespace og::render
