/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Per-facing sprite reliefs (docs/voxel-render-design.md §12).
//
// The parametric rigs invented shapes and lost the art. A relief invents
// nothing: its front face IS the sprite frame — same silhouette, same palette
// indices, no lighting — and the only thing added is thickness, taken from
// the silhouette's own distance transform so the cross-section follows the
// drawing. Every frame of every facing gets one, so turning the camera shows
// the art the artists drew for that side rather than a guess at it.
//
// SDL-free, deterministic, cached by frame pointer.

#include <openglad/interface/render/voxel_scene.h>

#include <openglad/core/order.h>

#include <map>

namespace og::render {

// Thickness caps per order (§12). A fighter reads as a slab about a third as
// deep as it is wide; a projectile is barely more than paper.
inline constexpr int kVoxelReliefDepthLiving = 6;
inline constexpr int kVoxelReliefDepthGenerator = 4;
inline constexpr int kVoxelReliefDepthTreasure = 3;
inline constexpr int kVoxelReliefDepthWeapon = 2;
inline constexpr int kVoxelReliefDepthFx = 2;
// h = clamp(round(k * EDT), 1, max). k = 1 keeps the profile at the same
// scale as the drawing.
inline constexpr float kVoxelReliefK = 1.0f;

[[nodiscard]] int voxel_relief_max_depth(Order order) noexcept;

// Build one relief from one frame of palette indices (0 = transparent).
[[nodiscard]] VoxelRelief voxel_build_relief(const unsigned char* frame, int w,
                                             int h, int max_depth);

// Reliefs are built lazily and kept for the life of the cache. The key is the
// frame's own data pointer, which is unique per (PixieData, frame index) and
// stays valid as long as the loader's graphics do.
class VoxelReliefCache
{
public:
    [[nodiscard]] const VoxelRelief* get(const unsigned char* frame, int w,
                                         int h, int max_depth);
    void clear() { cache_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return cache_.size(); }

private:
    std::map<const unsigned char*, VoxelRelief> cache_;
};

} // namespace og::render
