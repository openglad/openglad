/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Fitted sprite-textured bodies (docs/voxel-render-design.md §13).
//
// Round 6's reliefs are pixel-perfect and have no volume; the rigs before them
// had volume and no art. This is the third answer: a body whose SHAPE is
// solved against the sprites and whose COLOUR is back-projected from them.
// Nothing here is hand-chosen — the templates are generic (a humanoid of
// boxes and ellipsoids, a blob), and every number in a fit comes out of a
// search against the eight facing frames.
//
// SDL-free and deterministic: the solver's restarts run off a fixed seed.

#include <openglad/interface/render/voxel_carve.h>
#include <openglad/interface/render/voxel_scene.h>

#include <array>
#include <vector>

namespace og::render {

enum class FitTemplate : unsigned char
{
    Humanoid,
    Blob,
};

// Parameter slots. Integers, in body cells at 2x sprite scale, so one cell is
// half a sprite pixel.
enum FitParam : int
{
    FP_HEAD_CX = 0, FP_HEAD_Z, FP_HEAD_RX, FP_HEAD_RY, FP_HEAD_RZ,
    FP_TORSO_W, FP_TORSO_D, FP_TORSO_H, FP_TORSO_Z, FP_TORSO_Y,
    FP_ARML_TH, FP_ARML_LEN, FP_ARML_Z, FP_ARML_X, FP_ARML_Y, FP_ARML_PITCH,
    FP_ARMR_TH, FP_ARMR_LEN, FP_ARMR_Z, FP_ARMR_X, FP_ARMR_Y, FP_ARMR_PITCH,
    FP_LEG_TH, FP_LEG_LEN, FP_LEG_GAP,
    FP_CAPE_ON, FP_CAPE_W, FP_CAPE_LEN, FP_CAPE_TH,
    FP_WEAP_ON, FP_WEAP_SIDE, FP_WEAP_LEN, FP_WEAP_TH, FP_WEAP_PITCH,
    FP_WEAP_YAW, FP_WEAP_HAND_Z,
    FP_GEAR_KIND, FP_GEAR_RX, FP_GEAR_RZ,
    // Blob reuses the head slots for its ellipsoid plus these two.
    FP_BLOB_HEM_ROWS, FP_BLOB_HEM_STEP,
    FP_COUNT
};

struct FitParams
{
    FitTemplate tmpl = FitTemplate::Humanoid;
    std::array<int, FP_COUNT> p{};
};

struct FitReport
{
    FitParams params;
    VoxelModel model; // textured, 2x sprite scale, ready for the cube renderer
    float theta_deg = kVoxelCarveTheta;
    float loss = 1e9f;
    float iou[NUM_VOXEL_FACINGS] = {};
    float agreement[NUM_VOXEL_FACINGS] = {};
    float mean_iou = 0.0f;
    float mean_agreement = 0.0f;
    int voxels = 0;
    int surface = 0;
    double fit_seconds = 0.0;
    double texture_seconds = 0.0;
};

// Solve shape against the eight facing frames at this camera elevation, then
// texture the winner by back-projection. Templates compete; the lower loss
// wins.
FitReport voxel_fit_family(const VoxelCarveFrames& frames, float theta_deg);

// Silhouette-only loss for one already-solved parameter set, which is what the
// theta sweep compares.
[[nodiscard]] float voxel_fit_loss(const VoxelCarveFrames& frames,
                                   const FitParams& params, float theta_deg);

// Render a fitted body into a palette-index buffer under the game camera at
// one facing, at `scale` output pixels per sprite pixel. This is the same
// projection the sprites live in, which is why the fit can be scored in it.
void voxel_fit_render(const VoxelModel& body, int facing, float theta_deg,
                      unsigned char* out, int out_w, int out_h, int sprite_w,
                      int sprite_h, float scale);

} // namespace og::render
