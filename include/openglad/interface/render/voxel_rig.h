/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Parametric voxel rigs (docs/voxel-render-design.md §10, round 3).
//
// Space carving reconstructs shape from the eight facing frames, and it
// cannot: those silhouettes are near-rotationally symmetric, so their
// intersection is a solid of revolution — a mushroom. The prior that fixes
// that is not a better carve, it is a FIGURE: a stylized voxel-art body built
// from primitives per archetype, with the sprite supplying only its palette.
//
// Output is the same VoxelModel the cube-face renderer already draws, at 2x
// sprite scale (cell = 0.5 world px): a canonical figure ~31 voxels tall on a
// ~22 x 14 footprint, centred, facing +y — the sprite's "down" facing — at
// yaw 0, so the existing curdir -> yaw mapping applies unchanged.
//
// Deterministic, no rng, SDL-free.

#include <openglad/interface/render/voxel_scene.h>

namespace og::render {

// Cells per world pixel. A 16 px sprite becomes a 32-voxel figure.
inline constexpr float kVoxelRigCell = 0.5f;

// Palette INDICES, never RGB: the team band 248..255 has to survive to the
// blitter. Picked per family from the sprite's own histogram.
struct RigPalette
{
    unsigned char skin = 0;
    unsigned char primary = 0;   // cloth / armour body colour
    unsigned char secondary = 0; // trim
    unsigned char metal = 0;     // helmet, blade
    unsigned char dark = 0;      // boots, hafts, eye pits
    unsigned char team = 0;      // 0 when the sprite uses no team band
    unsigned char accent = 0;    // plume, orb, trim
};

enum class RigArchetype : unsigned char
{
    Humanoid,
    Skeleton,
    Ghost,
    Slime,
};

enum class RigWeapon : unsigned char
{
    None,
    Sword,
    Bow,
    Staff,
    Axe,
    Club,
    Dagger,
    Spear,
    Mace,
};

struct RigSpec
{
    RigArchetype archetype = RigArchetype::Humanoid;
    RigPalette pal{};
    RigWeapon weapon = RigWeapon::None;

    bool helmet = false;
    bool plume = false;
    bool hood = false;
    bool hat = false;
    bool cape = false;
    bool robe = false;
    bool quiver = false;
    bool shield = false;
    bool tusks = false;
    bool pointed_ears = false;
    bool horns = false;

    // Orc broadens, elf slims.
    int torso_widen = 0;
    // Ghosts float: the model's base sits this many voxels off the ground.
    int lift = 0;
};

// Build one rig. `sprite_w`/`sprite_h` set the model's anchor so it lands
// where that family's sprite lands under the game camera.
VoxelModel voxel_build_rig(const RigSpec& spec, int sprite_w, int sprite_h);

// A flat drop-shadow disc sized to a model's footprint, for hero and
// turntable plates. One layer, no AO, so it reads as a soft dark ellipse.
VoxelModel voxel_build_shadow(const VoxelModel& figure, unsigned char index);

} // namespace og::render
