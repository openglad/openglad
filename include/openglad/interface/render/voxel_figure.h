/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Voting-hull voxel figures (docs/voxel-render-design.md §15).
//
// One solid per family at SPRITE resolution — one voxel is one sprite pixel,
// drawn as a visible cube, so the figure has the same coarseness the art has.
// Two stages:
//
//   * a voting hull, which is the round-1 space carve with the "every view
//     must agree" rule relaxed to "at least k of 8": a strict intersection
//     deletes anything only some facings can see, which is how the sword went
//     missing;
//   * per-facing residuals, which put back exactly the pixels the hull still
//     fails to cover — the sword, the bow, a cape edge — as thin details
//     sitting on the facing's own plane at the depth where its view ray first
//     touches the hull.
//
// Colour is back-projected from the frames (never invented), residual voxels
// keep the pixel they came from, and nothing is hand-placed.
//
// SDL-free, deterministic.

#include <openglad/interface/render/voxel_carve.h>
#include <openglad/interface/render/voxel_scene.h>

#include <vector>

namespace og::render {

// The elevation the sprites read at, and the space the fit is measured in.
inline constexpr float kVoxelFigureTheta = 55.0f;

struct FigureReport
{
    VoxelModel model;
    int votes_required = 7;
    int hull_voxels = 0;
    int residual_voxels = 0;
    int components_dropped = 0;
    int cavities_filled = 0;
    float iou[NUM_VOXEL_FACINGS] = {};
    float agreement[NUM_VOXEL_FACINGS] = {};
    float mean_iou = 0.0f;
    float mean_agreement = 0.0f;
    double seconds = 0.0;
};

// Build one figure. `votes_required` is k in "kept if at least k of the 8
// facings see this voxel inside their silhouette".
FigureReport voxel_build_figure(const VoxelCarveFrames& frames,
                                int votes_required,
                                float theta_deg = kVoxelFigureTheta);

// Point-sampled index render of a figure under the game camera at one facing,
// in the sprite's own pixel space. Used for the fidelity numbers.
void voxel_figure_render(const VoxelModel& model, int facing, float theta_deg,
                         unsigned char* out, int out_w, int out_h,
                         int sprite_w, int sprite_h, float scale);

} // namespace og::render
