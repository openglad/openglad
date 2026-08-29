/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Voxel world renderer — scene model, cameras and the slice rasterizer.
// See docs/voxel-render-design.md (§2 scene, §3 cameras, §4 materials).
//
// Every drawable thing (tile, decor, sprite frame) is a *volume*: its 8-bit
// palette-index bitmap extruded straight up by a per-kind height. The
// Classic camera collapses every volume to its base slice and lands it on
// exactly the pixel today's blitters use; the Free camera stacks the slices
// into real columns under an orthographic yaw/pitch/scale.
//
// SDL-free by contract: this is the `interface` component's renderer core and
// takes a caller-supplied XRGB buffer plus a 256-entry palette LUT.

#include <cstdint>
#include <vector>

namespace og::render {

// Per-kind extrusion heights (§2 table). Render-only data: the sim never
// reads any of this.
inline constexpr int kVoxelHeightFloor = 0;
inline constexpr int kVoxelHeightWall = 16;
inline constexpr int kVoxelHeightTree = 20;
inline constexpr int kVoxelHeightLiving = 12;
inline constexpr int kVoxelHeightWeapon = 3;
inline constexpr int kVoxelHeightFx = 8;
inline constexpr int kVoxelHeightTreasure = 3;
// Water / lava / marsh / glass sit two pixels below the floor plane.
inline constexpr float kVoxelSunkenBaseZ = -2.0f;
// Upper floors stack this far above the one below.
inline constexpr float kVoxelFloorStride = 24.0f;
// §7 D3: projectiles ride this high so they clear fighter columns. Applied
// ONLY when the camera does not collapse, so Classic is untouched.
inline constexpr float kVoxelProjectileLift = 8.0f;
// §4: slices below the top are drawn at RGB * this constant.
inline constexpr float kVoxelSideShade = 0.72f;

// The per-pixel colour logic that lives in the walkputbuffer family today.
// The spike only needs the plain material; the mode flags (alpha, flash,
// invisible, outline, phantom) are named here but not yet implemented.
struct VoxelMaterial
{
    // Palette band 248..255 remaps to team_color + (255 - c).
    unsigned char team_color = 0;
    unsigned char alpha = 255;
    // Tiles blit through putbuffer, which is OPAQUE: index 0 is a real
    // colour, not a hole. Sprites (walkputbuffer / drawMix) skip it.
    bool opaque = false;
    // §7 D3 projectile lift, honoured only when !collapse.
    bool lift = false;
};

// A texture extruded by `height`: column (px, py) exists iff
// texels[py*w + px] != 0, and occupies z .. z+height. The texels are
// borrowed, never owned — the scene is rebuilt every frame from live art.
struct VoxelVolume
{
    const unsigned char* texels = nullptr;
    int w = 0;
    int h = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int height = 0;
    VoxelMaterial material{};
    // Painter rank for the Classic camera (§3): the position in today's draw
    // sequence. Assigned by VoxelScene::emit in emission order.
    int rank = 0;
};

class VoxelScene
{
public:
    void clear() noexcept
    {
        volumes_.clear();
        next_rank_ = 0;
    }

    // Appends `v` with the next painter rank. Emission order IS draw order.
    void emit(VoxelVolume v)
    {
        v.rank = next_rank_++;
        volumes_.push_back(v);
    }

    [[nodiscard]] const std::vector<VoxelVolume>& volumes() const noexcept
    {
        return volumes_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return volumes_.size(); }
    void reserve(std::size_t n) { volumes_.reserve(n); }

private:
    std::vector<VoxelVolume> volumes_;
    int next_rank_ = 0;
};

enum class VoxelCameraKind : unsigned char
{
    Classic,
    Free,
};

struct VoxelProjection
{
    float sx = 0.0f;
    float sy = 0.0f;
    float depth = 0.0f;
};

// A camera is project(x, y, z) -> {sx, sy, depth} plus the collapse flag.
struct VoxelCamera
{
    VoxelCameraKind kind = VoxelCameraKind::Classic;

    // --- Classic (the game camera) ---
    int topx = 0;
    int topy = 0;
    int xloc = 0;
    int yloc = 0;

    // --- Free (demo renders) ---
    float cx = 0.0f;        // world target
    float cy = 0.0f;
    float yaw_deg = 0.0f;   // theta, about z
    float pitch_deg = 90.0f;// phi, 90 = top-down
    float scale = 1.0f;
    float view_cx = 0.0f;   // screen anchor of the target
    float view_cy = 0.0f;

    [[nodiscard]] bool collapse() const noexcept
    {
        return kind == VoxelCameraKind::Classic;
    }

    // Classic:
    //   sx = trunc(x - topx + xloc)
    //   sy = trunc(y - topy + yloc) - trunc(z)
    //   depth = rank (supplied by the raster loop, not here)
    // The split truncation is deliberate: walker_draw.cpp:543 converts the
    // float world position to int FIRST and subtracts the (float) worldz
    // afterwards, so a fractional z rounds separately.
    //
    // Free: orthographic yaw/pitch/scale, depth = z*sin(phi) - y'*cos(phi)
    // ("near"; larger is nearer).
    [[nodiscard]] VoxelProjection project(float x, float y, float z) const;
};

// Caller-supplied XRGB destination. The clip rect is half-open
// [x0, x1) x [y0, y1) in target pixels, and is intersected with the buffer.
struct VoxelRenderTarget
{
    std::uint32_t* pixels = nullptr;
    int pitch_px = 0;
    int w = 0;
    int h = 0;
    int clip_x0 = 0;
    int clip_y0 = 0;
    int clip_x1 = 0;
    int clip_y1 = 0;
    const std::uint32_t* lut256 = nullptr;
};

// Per-frame cost accounting, so the wasm viability question has a number.
struct VoxelRasterStats
{
    std::uint64_t volumes = 0;       // volumes considered
    std::uint64_t slices = 0;        // slices whose bbox met the clip
    std::uint64_t pixel_samples = 0; // inverse-mapped texel lookups
    std::uint64_t pixels_written = 0;// depth test passed and wrote
};

class VoxelRaster
{
public:
    // Renders the whole scene through `camera` into `target`. Owns an
    // internal per-pixel float depth buffer, reset each call.
    VoxelRasterStats render(const VoxelScene& scene, const VoxelCamera& camera,
                            const VoxelRenderTarget& target);

private:
    void reset_depth(int w, int h);

    std::vector<float> depth_;
    std::vector<std::uint32_t> shaded_lut_;
    int depth_w_ = 0;
    int depth_h_ = 0;
};

} // namespace og::render
