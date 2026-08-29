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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace og::render {

// The eight walker facings (core/constants.h FACE_UP = 0 ... FACE_UP_LEFT =
// 7). Named here so the renderer core needs no gameplay header.
inline constexpr int NUM_VOXEL_FACINGS = 8;

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
// §4: slices below the top are drawn at RGB * this constant. For a carved
// model the same shade marks every voxel that is not a top surface, which is
// what gives a rotating model its lit top and shaded flanks.
inline constexpr float kVoxelSideShade = 0.72f;

// A carved voxel model (§10). Layer-major: (k * d + j) * w + i, with i across
// the footprint width, j across its depth, k up. Colours are palette INDICES
// — the team band 248..255 must survive to the blitter — so occupancy needs
// its own plane rather than the "index 0 = empty" convention a texture uses.
struct VoxelModel
{
    int w = 0;
    int d = 0;
    int z = 0;
    // World units per cell. A model carved from frames upscaled 4x has four
    // cells to the sprite pixel, so cell = 0.25 and the model still occupies
    // exactly the sprite's footprint in the world.
    float cell = 1.0f;
    std::vector<unsigned char> occ;   // 1 = solid
    std::vector<unsigned char> index; // palette index
    std::vector<unsigned char> lit;   // 1 = top surface (nothing solid above)
    // Baked ambient occlusion, 0..255 over the [kVoxelAoFloor, 1] range.
    // Multiplied into the colour AFTER the palette LUT, so the index stays an
    // index and the team-colour remap still happens on it.
    std::vector<unsigned char> shade;
    // Carved figures draw as cubes (three faces, lit); terrain models keep the
    // cheap stacked-slice look they were built for.
    bool cube_faces = true;
    // Sprite-space image of the model origin, in WORLD pixels: the footprint
    // centre at z = 0 projects here in every facing frame. Placement reads it.
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    float theta_deg = 0.0f;

    [[nodiscard]] bool empty() const noexcept { return occ.empty(); }
    [[nodiscard]] std::size_t at(int i, int j, int k) const noexcept
    {
        return (static_cast<std::size_t>(k) * static_cast<std::size_t>(d) +
                static_cast<std::size_t>(j)) *
                   static_cast<std::size_t>(w) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] bool solid(int i, int j, int k) const noexcept
    {
        if (i < 0 || j < 0 || k < 0 || i >= w || j >= d || k >= z)
            return false;
        return occ[at(i, j, k)] != 0;
    }
    [[nodiscard]] float extent_x() const noexcept
    {
        return static_cast<float>(w) * cell;
    }
    [[nodiscard]] float extent_y() const noexcept
    {
        return static_cast<float>(d) * cell;
    }
};

// Cube-face shading (§10 round 2). A carved figure only reads as a figure if
// its faces are lit differently, so each visible face carries a fixed factor
// and the voxel's baked AO multiplies into it.
inline constexpr float kVoxelFaceTop = 1.00f;
inline constexpr float kVoxelFaceSun = 0.85f;
inline constexpr float kVoxelFaceShadow = 0.70f;
// AO maps the fraction of empty neighbours into [kVoxelAoFloor, 1].
inline constexpr float kVoxelAoFloor = 0.55f;
// The silhouette gets a one-pixel darker rim, which is what makes pixel art
// read at small sizes.
inline constexpr float kVoxelEdgeShade = 0.5f;
// Quantisation of the shade axis in the precomputed shade x palette table.
inline constexpr int kVoxelShadeLevels = 32;

// The game camera's elevation, which is the tilt the relief plane is built
// for. A Free camera at this pitch sees a relief as exactly its sprite.
inline constexpr float kVoxelReliefTheta = 55.0f;
// §14 shading: sides 0.72, upward-facing steps 0.88, front never touched.
inline constexpr float kVoxelReliefSide = 0.72f;
inline constexpr float kVoxelReliefTop = 0.88f;

// A per-facing sprite RELIEF (§12). The front face IS the sprite frame —
// exact silhouette, exact palette indices, never shaded, never AO'd — and the
// thickness behind each pixel is a heightfield from the silhouette's distance
// transform. The relief lies on a plane perpendicular to a game camera at
// elevation kVoxelReliefTheta, so under that camera it projects to exactly the
// pixels the stamp blits: the classic look is reproduced by construction
// rather than approximated.
struct VoxelRelief
{
    int w = 0;
    int h = 0;
    int depth = 0;                      // thickness layers, >= 1
    std::vector<unsigned char> index;   // w*h front palette indices, 0 = empty
    std::vector<unsigned char> thick;   // w*h thickness in layers
    // w*h*depth shade factors, 255 = unshaded. Layer 0 is always 255: the
    // front face carries no lighting at all.
    std::vector<unsigned char> shade;
    // Tilt of the plane this relief was built for, in the game camera's pitch
    // convention. 90 lays the plane on the ground and the sprite becomes a
    // top face on a solid standing out of it.
    float theta_deg = kVoxelReliefTheta;

    [[nodiscard]] bool empty() const noexcept { return index.empty(); }
    [[nodiscard]] std::size_t at(int u, int v) const noexcept
    {
        return static_cast<std::size_t>(v) * static_cast<std::size_t>(w) +
               static_cast<std::size_t>(u);
    }
    [[nodiscard]] std::size_t at(int u, int v, int t) const noexcept
    {
        return (static_cast<std::size_t>(t) * static_cast<std::size_t>(h) +
                static_cast<std::size_t>(v)) *
                   static_cast<std::size_t>(w) +
               static_cast<std::size_t>(u);
    }
};


// World-space yaw for a walker facing, in the rasterizer's rotation sense
// (positive turns +x toward +y, i.e. clockwise on a y-down screen).
//
// Derivation: FACE_DOWN (curdir 4, gloader.cpp bit5) is the character facing
// the viewer, so it is yaw 0; each curdir step is 45 degrees. Checking the
// ends: curdir 2 (FACE_RIGHT) wants the model's front on +x, which the matrix
// [[cos,-sin],[sin,cos]] delivers at -90 degrees, and curdir 6 (FACE_LEFT) at
// +90. Hence (curdir - FACE_DOWN) * 45.
[[nodiscard]] constexpr float voxel_facing_yaw_rad(int curdir) noexcept
{
    return static_cast<float>((curdir - 4) * 45) *
           (3.14159265358979323846f / 180.0f);
}

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

    // §10: a volume may ALSO carry a carved model, rotated by `yaw` about its
    // own footprint centre. The Classic camera ignores it entirely — the
    // sprite frames are the baked view of the model from the game camera, an
    // imposter cache rather than a renderer twin — so `texels`/`height` stay
    // populated and Classic output is unchanged. Free cameras draw the model.
    // When set, the unrotated footprint is [x, x + model->w) x [y, y +
    // model->d) and `height` is ignored.
    const VoxelModel* model = nullptr;
    float yaw = 0.0f; // radians

    // §12: a sprite relief, billboarded to the camera's yaw and tilted to the
    // game camera's elevation. Takes precedence over `model`. Classic ignores
    // it and blits `texels`, so parity is untouched.
    const VoxelRelief* relief = nullptr;
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
    // Optional parallel plane of palette INDICES, same pitch and dimensions.
    // Written wherever a colour is written, so a caller can measure what the
    // real renderer produced in index space instead of re-deriving it — the
    // sprite-agreement number comes from the product path, not a twin of it.
    unsigned char* index_plane = nullptr;
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

    // Darkens every pixel that sits on a silhouette: one whose depth jumps by
    // more than `depth_jump` against a 4-neighbour, or which borders a pixel
    // nothing was drawn to. Reads the depth buffer left by the last render(),
    // so call it straight after.
    void edge_darken(const VoxelRenderTarget& target, float depth_jump,
                     float factor);

private:
    void reset_depth(int w, int h);
    void build_shade_tables(const VoxelRenderTarget& target);

    std::vector<float> depth_;
    std::vector<std::uint32_t> shaded_lut_;
    // kVoxelShadeLevels x 256 of pre-shaded palette entries, so a cube face
    // costs a table lookup rather than three float multiplies per pixel.
    std::vector<std::uint32_t> shade_table_;
    int depth_w_ = 0;
    int depth_h_ = 0;
};

} // namespace og::render
