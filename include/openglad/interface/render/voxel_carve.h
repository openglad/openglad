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
// The model is the product, not a fidelity exercise (§10 "bar, raised"), so
// the carve runs supersampled and is followed by a cleanup, a normal-aligned
// recolour and a baked ambient-occlusion pass.
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

// Z is clamped here (per sprite pixel, before supersampling) so a tall sprite
// cannot blow the grid out.
inline constexpr int kVoxelCarveMaxZ = 24;
// The camera elevation the art reads at. Fixed: 45, 55 and 65 measured within
// a percentage point of each other and were visually indistinguishable, so
// this stopped being a tunable.
inline constexpr float kVoxelCarveTheta = 55.0f;

struct VoxelCarveParams
{
    float theta_deg = kVoxelCarveTheta;
    // Frames are nearest-upscaled by this factor and their alpha mask run
    // through a 3x3 majority filter before carving, so the hull gets rounded
    // corners and sub-sprite-pixel detail instead of 16-cube stair steps.
    // 1-pixel features survive the filter because they are `supersample`
    // pixels wide by the time it runs.
    int supersample = 4;
    // 0 => grid_w = grid_d = supersample * sprite w (square footprint),
    // grid_z = ceil(supersample * h / sin(theta)), capped at
    // supersample * kVoxelCarveMaxZ.
    int grid_w = 0;
    int grid_d = 0;
    int grid_z = 0;
    // Sprite-space image of the model origin (footprint centre at z = 0), in
    // SPRITE pixels. The SAME anchor serves all eight frames — that shared
    // anchor is what ties the eight silhouettes into one hull. Negative =>
    // fitted automatically, at 1x, before the supersampled carve.
    float anchor_x = -1.0f;
    float anchor_y = -1.0f;
    // Drop the carved hull so its lowest occupied layer is z = 0, folding the
    // shift back into the stored anchor.
    bool normalize_base = true;
    // Morphological opening (erode then constrained dilate) before anything
    // else. A visual hull grows thin flanges wherever the eight silhouettes
    // graze each other, and they render as jagged wings; one iteration at
    // 4x supersampling trims anything under half a sprite pixel thick and
    // leaves real features alone.
    int open_iterations = 1;
    // Post-carve cleanup: keep the largest 26-connected component plus any
    // component holding at least this fraction of the voxels, then fill any
    // fully enclosed cavity.
    float component_keep_fraction = 0.02f;
    bool fill_cavities = true;
    // Photo-consistency carving (voxel colouring). The alpha carve uses only
    // the silhouettes, and these sprites' eight silhouettes are nearly
    // rotationally symmetric, so the visual hull comes out a solid of
    // revolution — a mushroom. Colour is the only signal in the art that
    // breaks that symmetry: a voxel the seeing views disagree about is not a
    // real surface point, so it goes. Indices only; two indices count as
    // agreeing when they sit within `ramp_tolerance` of each other (palette
    // ramps are contiguous runs) or both fall in the team band.
    int photo_passes = 4;
    int photo_min_views = 3;
    int ramp_tolerance = 2;
    float photo_agreement = 0.6f;
    // Give up on the whole photo pass rather than hand back a model it ate:
    // if it would remove more than this fraction, roll back to the hull.
    float photo_max_loss = 0.45f;
    // Surface-neighbour majority passes over the palette indices. One pass
    // leaves the colour visibly speckled where the normal-aligned view choice
    // flips between adjacent voxels; three settle it into patches.
    int despeckle_passes = 3;
    // AO: the empty fraction of the 5^3 neighbourhood that counts as fully
    // open. A flat face sits at 0.5, so 0.5 leaves flat surfaces unshaded and
    // darkens only creases.
    float ao_reference = 0.5f;
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
    int surface_voxels = 0;
    int opened_away = 0;
    int photo_carved = 0;
    bool photo_rolled_back = false;
    int components_dropped = 0;
    int cavity_voxels_filled = 0;
    int despeckled = 0;
    double fit_seconds = 0.0;
    double carve_seconds = 0.0;
    // Sanity check only (§10: the gate is the visual review). Agreement of the
    // 1x fit reprojection against the source frames: matched palette index
    // over the union of opaque pixels, so a hull that simply covers
    // everything cannot win it.
    float fit_agreement[NUM_VOXEL_FACINGS] = {};
    float fit_agreement_mean = 0.0f;
    float fit_iou_mean = 0.0f;
};

// Carve one model. Deterministic and side-effect free.
VoxelCarveReport voxel_carve(const VoxelCarveFrames& frames,
                             const VoxelCarveParams& params);

// Majority-occupancy downsample, for the in-scene copy of a hero model.
// `factor` cells of the source collapse into one, keeping the world size.
VoxelModel voxel_model_downsample(const VoxelModel& src, int factor);

// (Re)bake ambient occlusion over the 5^3 neighbourhood.
void voxel_model_bake_ao(VoxelModel& model, float reference);

// The carve's OWN projection test — project cell centres, keep the nearest —
// used to fit the anchor and to report the sanity agreement. This is not the
// product renderer: the cube-face path in VoxelRaster is, and the numbers
// quoted for a rendered strip come from that renderer's index plane.
void voxel_model_reproject(const VoxelModel& model, float yaw_rad,
                           float theta_deg, unsigned char* out, int out_w,
                           int out_h, float anchor_x, float anchor_y,
                           float scale);

// The §10 terrain fixes, as models. Both keep their volume's extruded texels,
// so the Classic camera (which ignores models) is untouched, and both render
// through the stacked-slice path rather than as cubes.
//
// Wall: the top slice is the wall-top art; every slice below samples the
// matching PIX_WALLSIDE1 art, tiled vertically, instead of repeating the top.
VoxelModel voxel_build_wall_model(const unsigned char* top, int tw, int th,
                                  const unsigned char* side, int sw, int sh,
                                  int height);
// Tree: a canopy profile instead of a flat extrusion — a `trunk_size` square
// trunk for the lower `trunk_h` voxels, the full footprint for the top
// `canopy_h`, and the ground tile as the base slice so the overhang shows
// grass underneath instead of a black trench.
VoxelModel voxel_build_tree_model(const unsigned char* top, int tw, int th,
                                  const unsigned char* ground, int gw, int gh,
                                  int trunk_h, int canopy_h, int trunk_size);

} // namespace og::render
