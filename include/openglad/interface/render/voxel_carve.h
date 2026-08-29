/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Space carver: reconstruct a per-family voxel model from its eight facing
// frames (docs/voxel-render-design.md §10).
//
// The eight walk frames of a family are eight rotations of ONE character seen
// by ONE camera, so the model is reconstructed, not authored. Assume that
// camera is orthographic at elevation theta above the ground plane; back-
// project every voxel of a grid into all eight frames and delete it the
// moment any view says "transparent" (or "off the frame"). What survives is
// the visual hull. Colours stay PALETTE INDICES the whole way through — the
// team band 248..255 has to reach the blitter intact.
//
// SDL-free, deterministic, no rng: a pure function of (frames, theta).

#include <openglad/interface/render/voxel_scene.h>

#include <array>
#include <cstddef>
#include <vector>

namespace og::render {

// The eight source frames, in curdir order (constants.h FACE_UP = 0 ...
// FACE_UP_LEFT = 7). Each is w*h palette indices with 0 transparent.
struct VoxelCarveFrames
{
    const unsigned char* frame[NUM_VOXEL_FACINGS] = {};
    int w = 0;
    int h = 0;
};

// Z is clamped here so a tall sprite cannot blow the grid out (§10: "Z ~ 20").
inline constexpr int kVoxelCarveMaxZ = 24;

struct VoxelCarveParams
{
    // Camera elevation above the ground plane. 90 degenerates to a pure top
    // view; the sprite art reads as roughly 45-65.
    float theta_deg = 45.0f;
    // 0 => the §10 defaults: grid_w = grid_d = sprite w (square footprint),
    // grid_z = ceil(h / sin(theta)) clamped to kVoxelCarveMaxZ.
    int grid_w = 0;
    int grid_d = 0;
    int grid_z = 0;
    // Sprite-space image of the model origin (footprint centre at z = 0). The
    // SAME anchor serves all eight frames — that shared anchor is what ties
    // the eight silhouettes into one hull. Negative => fit automatically.
    float anchor_x = -1.0f;
    float anchor_y = -1.0f;
    // Drop the carved hull so its lowest occupied layer is z = 0, and fold the
    // shift back into the model's stored anchor so a re-render still lands on
    // the source frames.
    bool normalize_base = true;
    // Override the facing -> yaw map. Only the convention probe uses this:
    // carving under a wrong map intersects the eight silhouettes in the wrong
    // relative orientations, and the agreement collapses — which is how the
    // derived convention is checked against the art instead of asserted.
    bool custom_yaw = false;
    std::array<float, NUM_VOXEL_FACINGS> yaw_rad{};
};

struct VoxelCarveReport
{
    VoxelModel model;
    int voxel_count = 0;
    int carved_by_transparency = 0;
    int carved_by_frame_bounds = 0;
    double carve_seconds = 0.0;
    // Re-render agreement vs the source frame, per facing and meaned:
    //   matched / union, where matched counts pixels opaque in BOTH with the
    //   same palette index and union counts pixels opaque in EITHER. A model
    //   that simply covers everything cannot win this.
    float agreement[NUM_VOXEL_FACINGS] = {};
    float silhouette_iou[NUM_VOXEL_FACINGS] = {};
    float agreement_mean = 0.0f;
    float silhouette_iou_mean = 0.0f;
    int worst_facing = 0;
};

// Carve one model. Deterministic and side-effect free.
VoxelCarveReport voxel_carve(const VoxelCarveFrames& frames,
                             const VoxelCarveParams& params);

// Render a carved model under the assumed camera at an arbitrary yaw into a
// palette-index buffer (0 = transparent). `anchor_x/anchor_y` place the model
// origin in the output; pass the model's own anchor to land on the source
// frame exactly.
void voxel_model_render_indices(const VoxelModel& model, float yaw_rad,
                                float theta_deg, unsigned char* out, int out_w,
                                int out_h, float anchor_x, float anchor_y);

// The two §10 terrain fixes, as models. Both keep their volume's extruded
// texels, so the Classic camera (which ignores models) is untouched.
//
// Wall: the top slice is the wall-top art; every slice below samples the
// matching PIX_WALLSIDE1 art, tiled vertically, instead of repeating the top.
VoxelModel voxel_build_wall_model(const unsigned char* top, int tw, int th,
                                  const unsigned char* side, int sw, int sh,
                                  int height);
// Tree: a canopy profile instead of a flat extrusion — a `trunk_size` square
// trunk for the lower `trunk_h` voxels, the full footprint for the top
// `canopy_h`.
VoxelModel voxel_build_tree_model(const unsigned char* top, int tw, int th,
                                  int trunk_h, int canopy_h, int trunk_size);

} // namespace og::render
