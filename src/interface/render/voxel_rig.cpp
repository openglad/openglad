/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Parametric voxel rigs (docs/voxel-render-design.md §10, round 3).
//
// Proportions are the chunky voxel-art idiom, not human anatomy: a big cube
// head on a short blocky body, limbs as slabs, everything on a 1-voxel gap so
// the silhouette reads at 16 pixels. The figure is built facing +y (toward
// the viewer — the sprite's "down" facing), so yaw 0 is FACE_DOWN and the
// walker's curdir maps straight onto the volume yaw.
//
// Cell coordinates are (x, y, z) with x right, y toward the viewer, z up from
// the ground, and x/y measured from the footprint centre so the model's own
// anchor is simply the grid middle.

#include <openglad/interface/render/voxel_rig.h>

#include <openglad/interface/render/voxel_carve.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace og::render {

namespace {

// Generous enough for a staff, a plume and a cape without clipping.
constexpr int kRigW = 36;
constexpr int kRigD = 30;
constexpr int kRigZ = 48;

struct RigCanvas
{
    VoxelModel m;
    int cx = kRigW / 2;
    int cy = kRigD / 2;

    RigCanvas()
    {
        m.w = kRigW;
        m.d = kRigD;
        m.z = kRigZ;
        m.cell = kVoxelRigCell;
        m.cube_faces = true;
        const std::size_t n = static_cast<std::size_t>(kRigW) *
                              static_cast<std::size_t>(kRigD) *
                              static_cast<std::size_t>(kRigZ);
        m.occ.assign(n, 0u);
        m.index.assign(n, 0u);
    }

    [[nodiscard]] bool in_range(int x, int y, int z) const
    {
        const int i = cx + x, j = cy + y;
        return i >= 0 && j >= 0 && z >= 0 && i < m.w && j < m.d && z < m.z;
    }
    void put(int x, int y, int z, unsigned char idx)
    {
        if (!in_range(x, y, z))
            return;
        const std::size_t s = m.at(cx + x, cy + y, z);
        m.occ[s] = 1u;
        m.index[s] = idx;
    }
    // Recolour without creating anything: how a helmet takes over the top of
    // a head, or a hood shadows a face.
    void tint(int x, int y, int z, unsigned char idx)
    {
        if (!in_range(x, y, z))
            return;
        const std::size_t s = m.at(cx + x, cy + y, z);
        if (m.occ[s] != 0)
            m.index[s] = idx;
    }
    void clear(int x, int y, int z)
    {
        if (!in_range(x, y, z))
            return;
        m.occ[m.at(cx + x, cy + y, z)] = 0u;
    }
    [[nodiscard]] bool filled(int x, int y, int z) const
    {
        if (!in_range(x, y, z))
            return false;
        return m.occ[m.at(cx + x, cy + y, z)] != 0;
    }

    // --- primitives -------------------------------------------------------
    void box(int x0, int y0, int z0, int w, int d, int h, unsigned char idx)
    {
        for (int z = z0; z < z0 + h; ++z)
            for (int y = y0; y < y0 + d; ++y)
                for (int x = x0; x < x0 + w; ++x)
                    put(x, y, z, idx);
    }
    // Centred on x = 0 (and optionally on y = 0), which is how every body
    // part but the limbs is placed.
    void box_c(int w, int d, int h, int z0, unsigned char idx, int y_off = 0)
    {
        box(-(w / 2), -(d / 2) + y_off, z0, w, d, h, idx);
    }
    void box_pair(int x0, int y0, int z0, int w, int d, int h,
                  unsigned char idx)
    {
        box(x0, y0, z0, w, d, h, idx);
        box(-x0 - w, y0, z0, w, d, h, idx);
    }
    void ellipsoid(float ex, float ey, float ez, float rx, float ry, float rz,
                   unsigned char idx)
    {
        const int x0 = static_cast<int>(std::floor(ex - rx));
        const int x1 = static_cast<int>(std::ceil(ex + rx));
        const int y0 = static_cast<int>(std::floor(ey - ry));
        const int y1 = static_cast<int>(std::ceil(ey + ry));
        const int z0 = static_cast<int>(std::floor(ez - rz));
        const int z1 = static_cast<int>(std::ceil(ez + rz));
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = (static_cast<float>(x) + 0.5f - ex) / rx;
                    const float dy = (static_cast<float>(y) + 0.5f - ey) / ry;
                    const float dz = (static_cast<float>(z) + 0.5f - ez) / rz;
                    if (dx * dx + dy * dy + dz * dz <= 1.0f)
                        put(x, y, z, idx);
                }
    }
    // Upright cylinder, z axis.
    void cylinder(float ex, float ey, int z0, int h, float r,
                  unsigned char idx)
    {
        const int x0 = static_cast<int>(std::floor(ex - r));
        const int x1 = static_cast<int>(std::ceil(ex + r));
        const int y0 = static_cast<int>(std::floor(ey - r));
        const int y1 = static_cast<int>(std::ceil(ey + r));
        for (int z = z0; z < z0 + h; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                {
                    const float dx = static_cast<float>(x) + 0.5f - ex;
                    const float dy = static_cast<float>(y) + 0.5f - ey;
                    if (dx * dx + dy * dy <= r * r)
                        put(x, y, z, idx);
                }
    }
    // A stack of discs whose radius walks from r0 to r1: robe hems, hat cones.
    void taper(int z0, int h, float r0, float r1, float ey, unsigned char idx)
    {
        for (int z = 0; z < h; ++z)
        {
            const float t = h > 1 ? static_cast<float>(z) /
                    static_cast<float>(h - 1)
                                  : 0.0f;
            cylinder(0.0f, ey, z0 + z, 1, r0 + (r1 - r0) * t, idx);
        }
    }
    // The spec's mirror helper: fold the +x half onto -x. Used after the
    // symmetric body is drawn, before anything one-handed goes on.
    void mirror_x()
    {
        for (int z = 0; z < m.z; ++z)
            for (int y = -cy; y < m.d - cy; ++y)
                for (int x = 1; x < m.w - cx; ++x)
                {
                    if (!filled(x, y, z))
                        continue;
                    const std::size_t s = m.at(cx + x, cy + y, z);
                    put(-x, y, z, m.index[s]);
                }
    }
};

// --- humanoid dimensions (cells) -------------------------------------------
struct Body
{
    int torso_w = 12, torso_d = 7, torso_h = 10;
    int leg_w = 5, leg_d = 5, leg_h = 7;
    int boot_h = 2;
    int head_w = 10, head_d = 9, head_h = 9;
    int arm_w = 4, arm_d = 4, arm_h = 9;
    int z_boot = 0, z_leg = 2, z_torso = 9, z_neck = 19, z_head = 20,
        z_top = 29;
    int hand_x = 0, hand_z = 0;
};

// Cloth has to differ from skin or the garment vanishes: a hood in the same
// palette ramp as the face is not a hood, it is a bigger head.
unsigned char cloth_of(const RigPalette& p)
{
    if (p.primary / 8 != p.skin / 8)
        return p.primary;
    if (p.secondary != 0 && p.secondary / 8 != p.skin / 8)
        return p.secondary;
    return p.dark;
}

Body body_for(const RigSpec& s)
{
    Body b;
    b.torso_w += s.torso_widen;
    if (s.archetype == RigArchetype::Skeleton)
    {
        b.arm_w = 2;
        b.arm_d = 2;
        b.leg_w = 3;
        b.leg_d = 3;
    }
    b.z_leg = b.z_boot + b.boot_h;
    b.z_torso = b.z_leg + b.leg_h;
    b.z_neck = b.z_torso + b.torso_h;
    b.z_head = b.z_neck + 1;
    b.z_top = b.z_head + b.head_h;
    b.hand_x = b.torso_w / 2 + 1 + b.arm_w / 2;
    b.hand_z = b.z_neck - b.arm_h + 1;
    return b;
}

void build_legs(RigCanvas& c, const Body& b, const RigPalette& p)
{
    // A 1-voxel gap between the legs is what makes them read as two legs at
    // sprite size.
    const int x0 = 1;
    c.box_pair(x0, -(b.leg_d / 2), b.z_leg, b.leg_w, b.leg_d, b.leg_h,
               p.secondary);
    c.box_pair(x0 - 1, -(b.leg_d / 2) - 1, b.z_boot, b.leg_w + 1, b.leg_d + 2,
               b.boot_h, p.dark);
}

void build_torso(RigCanvas& c, const Body& b, const RigPalette& p)
{
    c.box_c(b.torso_w, b.torso_d, b.torso_h, b.z_torso, p.primary);
    // Shoulders one voxel wider than the torso: the classic voxel-art cue
    // that the thing has a build.
    c.box_c(b.torso_w + 2, b.torso_d, 2, b.z_neck - 2, p.primary);
    // Whatever the sprite paints in the team band is what the game recolours
    // per team, so the rig spends it on the chest, where it reads at 16 px.
    if (p.team != 0)
    {
        for (int z = b.z_neck - 5; z < b.z_neck - 2; ++z)
            for (int y = -(b.torso_d / 2); y < b.torso_d - b.torso_d / 2; ++y)
                for (int x = -(b.torso_w / 2) - 1;
                     x < b.torso_w - b.torso_w / 2 + 1; ++x)
                    c.tint(x, y, z, p.team);
    }
    else
    {
        c.box_c(b.torso_w + 1, b.torso_d, 1, b.z_neck - 5, p.secondary);
    }
    c.box_c(3, 3, 1, b.z_neck, p.skin); // neck
}

void build_arms(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const int x0 = b.torso_w / 2 + 1;
    c.box_pair(x0, -(b.arm_d / 2), b.hand_z, b.arm_w, b.arm_d, b.arm_h,
               p.primary);
    // Bare hands at the bottom of each sleeve.
    c.box_pair(x0, -(b.arm_d / 2), b.hand_z, b.arm_w, b.arm_d, 2, p.skin);
}

void build_head(RigCanvas& c, const Body& b, const RigPalette& p)
{
    c.box_c(b.head_w, b.head_d, b.head_h, b.z_head, p.skin);
}

// Drawn AFTER any headgear: a helmet or hood recolours the whole head block,
// so eyes cut before it are painted straight over.
void build_face(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int front = b.head_d / 2 - 1;
    // Low enough to sit on the exposed face band rather than on a helmet's
    // brow: a helmet takes the top five rows of the head.
    const int eye_z = b.z_head + 3;
    // Two 1-voxel pits, which is all a face needs at this scale.
    c.tint(2, front, eye_z, p.dark);
    c.tint(-2, front, eye_z, p.dark);
    if (s.tusks)
    {
        for (int z = 0; z < 2; ++z)
        {
            c.put(2, front + 1, b.z_head + 2 - z, p.metal);
            c.put(-2, front + 1, b.z_head + 2 - z, p.metal);
        }
    }
    if (s.pointed_ears)
    {
        const int ear_z = b.z_head + 4;
        for (int e = 0; e < 2; ++e)
        {
            c.put(b.head_w / 2 + e, 0, ear_z + e, p.skin);
            c.put(-b.head_w / 2 - 1 - e, 0, ear_z + e, p.skin);
        }
    }
}

void build_helmet(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int base = b.z_top - 5;
    for (int z = base; z < b.z_top; ++z)
        for (int y = -b.head_d / 2; y < b.head_d - b.head_d / 2; ++y)
            for (int x = -b.head_w / 2; x < b.head_w - b.head_w / 2; ++x)
                c.tint(x, y, z, p.metal);
    // Nasal bar down the front: one voxel wide, three tall.
    const int front = b.head_d / 2 - 1;
    for (int z = base - 3; z < base; ++z)
        c.tint(0, front, z, p.metal);
    if (s.horns)
        for (int t = 0; t < 2; ++t)
        {
            c.put(b.head_w / 2 - 1 + t, 0, b.z_top + t, p.metal);
            c.put(-b.head_w / 2 - t, 0, b.z_top + t, p.metal);
        }
    if (s.plume)
    {
        const unsigned char accent = p.accent != 0 ? p.accent : p.secondary;
        for (int z = 0; z < 4; ++z)
            for (int y = -2; y <= 0; ++y)
                c.put(0, y, b.z_top + z, accent);
    }
}

void build_hood(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const unsigned char cloth = cloth_of(p);
    for (int z = b.z_head + 2; z < b.z_top; ++z)
        for (int y = -b.head_d / 2; y < b.head_d - b.head_d / 2; ++y)
            for (int x = -b.head_w / 2; x < b.head_w - b.head_w / 2; ++x)
                c.tint(x, y, z, cloth);
    // Face in shadow under the hood, which is most of a hood's read.
    const int front = b.head_d / 2 - 1;
    for (int z = b.z_head + 3; z < b.z_head + 8; ++z)
        for (int x = -3; x <= 3; ++x)
            c.tint(x, front, z, p.dark);
    c.box_c(b.head_w + 2, b.head_d + 2, 1, b.z_top - 1, cloth);
}

void build_hat(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const unsigned char cloth = cloth_of(p);
    c.cylinder(0.0f, 0.0f, b.z_top, 1, 7.0f, cloth);   // brim, 14 across
    c.taper(b.z_top + 1, 12, 6.0f, 0.8f, 0.0f, cloth); // cone
    if (p.accent != 0)
        c.cylinder(0.0f, 0.0f, b.z_top + 1, 1, 6.0f, p.accent); // band
}

void build_cape(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const unsigned char cloth = p.team != 0 ? p.team : cloth_of(p);
    const int back = -(b.torso_d / 2) - 1;
    // Narrower than the shoulders and stopping at the knee: a cape as wide as
    // the figure reads as a red wall from every angle but the front.
    for (int z = b.z_boot + 4; z <= b.z_neck; ++z)
    {
        const int half = (z < b.z_boot + 10) ? 5 : 4; // flare at the hem
        for (int x = -half; x < half; ++x)
            for (int y = back - 1; y <= back; ++y)
                c.put(x, y, z, cloth);
    }
}

void build_robe(RigCanvas& c, const Body& b, const RigPalette& p)
{
    for (int z = b.z_boot; z < b.z_torso; ++z)
        for (int y = -8; y <= 8; ++y)
            for (int x = -9; x <= 9; ++x)
                c.clear(x, y, z);
    const int h = b.z_torso - b.z_boot;
    c.taper(b.z_boot, h, 7.0f, 5.0f, 0.0f, cloth_of(p));
    // Hem trim reads as a line at sprite scale.
    c.cylinder(0.0f, 0.0f, b.z_boot, 1, 7.0f, p.secondary);
}

void build_quiver(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const int back = -(b.torso_d / 2) - 2;
    for (int z = 0; z < 10; ++z)
    {
        const int lean = z / 4; // angled across the back
        c.box(-5 + lean, back, b.z_torso + 4 + z, 4, 3, 1, p.dark);
    }
    for (int z = 0; z < 3; ++z)
        c.box(-3, back, b.z_torso + 14 + z, 2, 2, 1, p.accent);
}

void build_shield(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const int x0 = -(b.torso_w / 2 + 1 + b.arm_w) - 1;
    const int y = b.torso_d / 2;
    c.box(x0, y, b.hand_z + 1, 8, 1, 10, p.metal);
    c.box(x0 + 3, y + 1, b.hand_z + 5, 2, 1, 2,
          p.accent != 0 ? p.accent : p.secondary);
}

// Weapons go in the hand on +x — the side the sprites draw them on when the
// character faces the viewer.
void build_weapon(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int hx = b.hand_x + 1; // clear of the body, or it is invisible
    const int hz = b.hand_z;
    switch (s.weapon)
    {
    case RigWeapon::None:
        break;
    case RigWeapon::Sword:
    case RigWeapon::Dagger:
    {
        const int blade = (s.weapon == RigWeapon::Sword) ? 14 : 6;
        for (int z = 0; z < 3; ++z) // grip
            c.box(hx - 1, -1, hz - 2 + z, 2, 2, 1, p.dark);
        c.box(hx - 3, -1, hz + 1, 6, 2, 1, p.secondary); // crossguard
        for (int z = 0; z < blade; ++z)
        {
            // Angled ~20 degrees forward, one voxel of lean every third row.
            const int lean = (z * 36) / 100;
            c.box(hx - 1, -1 + lean, hz + 2 + z, 2, 2, 1, p.metal);
        }
        break;
    }
    case RigWeapon::Bow:
    {
        const float pi = 3.14159265358979f;
        for (int t = 0; t <= 18; ++t)
        {
            const float u = static_cast<float>(t) / 18.0f;
            const int z = hz - 4 + t;
            const int y = static_cast<int>(std::lround(
                6.0f * std::sin(pi * u)));
            c.box(hx, y - 1, z, 2, 2, 1, p.dark);
        }
        for (int t = 0; t <= 18; ++t) // string
            c.put(hx, 0, hz - 4 + t, p.secondary);
        break;
    }
    case RigWeapon::Staff:
    {
        for (int z = 0; z < 22; ++z)
            c.box(hx - 1, -1, hz - 2 + z, 2, 2, 1, p.dark);
        c.ellipsoid(static_cast<float>(hx), 0.0f,
                    static_cast<float>(hz + 21), 2.2f, 2.2f, 2.2f,
                    p.accent != 0 ? p.accent : p.metal);
        break;
    }
    case RigWeapon::Axe:
    {
        for (int z = 0; z < 18; ++z)
            c.box(hx - 1, -1, hz - 3 + z, 2, 2, 1, p.dark);
        c.box(hx - 1, -1, hz + 9, 6, 2, 6, p.metal);
        c.box(hx + 4, -1, hz + 11, 1, 2, 2, p.metal);
        break;
    }
    case RigWeapon::Club:
    {
        for (int z = 0; z < 12; ++z)
        {
            const int w = 2 + (z > 7 ? 2 : 0);
            c.box(hx - w / 2, -w / 2, hz - 2 + z, w, w, 1, p.dark);
        }
        break;
    }
    case RigWeapon::Mace:
    {
        for (int z = 0; z < 12; ++z)
            c.box(hx - 1, -1, hz - 2 + z, 2, 2, 1, p.dark);
        c.ellipsoid(static_cast<float>(hx), 0.0f,
                    static_cast<float>(hz + 11), 2.5f, 2.5f, 2.5f, p.metal);
        break;
    }
    case RigWeapon::Spear:
    {
        for (int z = 0; z < 24; ++z)
            c.box(hx - 1, -1, hz - 4 + z, 2, 2, 1, p.dark);
        for (int z = 0; z < 4; ++z)
            c.box(hx - 1, -1, hz + 20 + z, 2, 2, 1, p.metal);
        break;
    }
    }
}

void build_skeleton_extras(RigCanvas& c, const Body& b, const RigPalette& p)
{
    // Ribcage: alternate rows of the torso hollowed out so the background
    // shows between the ribs.
    for (int z = b.z_torso + 2; z < b.z_neck - 3; z += 2)
        for (int y = -(b.torso_d / 2) + 1; y < b.torso_d - b.torso_d / 2 - 1;
             ++y)
            for (int x = -(b.torso_w / 2) + 1; x < b.torso_w - b.torso_w / 2 - 1;
                 ++x)
                c.clear(x, y, z);
    // Bigger eye pits, and a jaw line.
    const int front = b.head_d / 2 - 1;
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
        {
            c.tint(2 + dx, front, b.z_head + 4 + dz, p.dark);
            c.tint(-3 + dx, front, b.z_head + 4 + dz, p.dark);
        }
    for (int x = -2; x <= 2; ++x)
        c.tint(x, front, b.z_head + 2, p.dark);
}

void build_ghost(RigCanvas& c, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const unsigned char body = p.primary;
    // Domed head-top, then a hollow shell tapering to a ragged hem.
    c.ellipsoid(0.0f, 0.0f, 24.0f, 6.0f, 5.0f, 4.0f, body);
    for (int z = 6; z <= 24; ++z)
    {
        const float t = static_cast<float>(24 - z) / 18.0f;
        const float r = 5.0f + 3.0f * t;
        c.cylinder(0.0f, 0.0f, z, 1, r, body);
    }
    // Hollow it out: only the shell survives, so it reads as a sheet.
    for (int z = 8; z <= 22; ++z)
    {
        const float t = static_cast<float>(24 - z) / 18.0f;
        const float r = 5.0f + 3.0f * t - 1.6f;
        if (r > 0.5f)
            for (int y = -10; y <= 10; ++y)
                for (int x = -10; x <= 10; ++x)
                {
                    const float dx = static_cast<float>(x) + 0.5f;
                    const float dy = static_cast<float>(y) + 0.5f;
                    if (dx * dx + dy * dy <= r * r)
                        c.clear(x, y, z);
                }
    }
    // Wavy hem: knock alternate columns out of the bottom two rows.
    for (int x = -9; x <= 9; ++x)
        for (int y = -9; y <= 9; ++y)
            if (((x + y) & 1) != 0)
            {
                c.clear(x, y, 6);
                c.clear(x, y, 7);
            }
    const int front = 4;
    for (int dz = 0; dz < 2; ++dz)
    {
        c.tint(2, front, 24 + dz, p.dark);
        c.tint(-3, front, 24 + dz, p.dark);
    }
    if (p.team != 0)
        c.cylinder(0.0f, 0.0f, 12, 1, 7.0f, p.team);
}

void build_slime(RigCanvas& c, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    c.ellipsoid(0.0f, 0.0f, 0.0f, 9.0f, 7.0f, 10.0f, p.primary);
    for (int z = 0; z < 4; ++z)
        for (int y = -8; y <= 8; ++y)
            for (int x = -10; x <= 10; ++x)
                c.clear(x, y, -1); // nothing below ground anyway
    const int front = 5;
    for (int dz = 0; dz < 2; ++dz)
    {
        c.tint(2, front, 6 + dz, p.dark);
        c.tint(-3, front, 6 + dz, p.dark);
    }
}

// Drop everything below the lowest occupied layer and above the highest, so
// the model's own extent is the figure's.
void trim(VoxelModel& m, int lift)
{
    int lo = m.z, hi = -1;
    for (int k = 0; k < m.z; ++k)
    {
        bool any = false;
        for (int j = 0; j < m.d && !any; ++j)
            for (int i = 0; i < m.w; ++i)
                if (m.occ[m.at(i, j, k)] != 0)
                {
                    any = true;
                    break;
                }
        if (any)
        {
            lo = std::min(lo, k);
            hi = k;
        }
    }
    if (hi < 0)
        return;
    VoxelModel out;
    out.w = m.w;
    out.d = m.d;
    out.z = hi - lo + 1 + lift;
    out.cell = m.cell;
    out.cube_faces = m.cube_faces;
    const std::size_t n = static_cast<std::size_t>(out.w) *
                          static_cast<std::size_t>(out.d) *
                          static_cast<std::size_t>(out.z);
    out.occ.assign(n, 0u);
    out.index.assign(n, 0u);
    for (int k = lo; k <= hi; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                const std::size_t src = m.at(i, j, k);
                if (m.occ[src] == 0)
                    continue;
                const std::size_t dst = out.at(i, j, k - lo + lift);
                out.occ[dst] = 1u;
                out.index[dst] = m.index[src];
            }
    m = std::move(out);
}

void recompute_lit(VoxelModel& m)
{
    m.lit.assign(m.occ.size(), 0u);
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                const std::size_t s = m.at(i, j, k);
                if (m.occ[s] == 0)
                    continue;
                m.lit[s] = m.solid(i, j, k + 1) ? 0u : 1u;
            }
}

} // namespace

VoxelModel voxel_build_rig(const RigSpec& spec, int sprite_w, int sprite_h)
{
    RigCanvas c;
    const Body b = body_for(spec);

    switch (spec.archetype)
    {
    case RigArchetype::Ghost:
        build_ghost(c, spec);
        break;
    case RigArchetype::Slime:
        build_slime(c, spec);
        break;
    case RigArchetype::Humanoid:
    case RigArchetype::Skeleton:
        build_legs(c, b, spec.pal);
        build_torso(c, b, spec.pal);
        build_arms(c, b, spec.pal);
        build_head(c, b, spec.pal);
        if (spec.archetype == RigArchetype::Skeleton)
            build_skeleton_extras(c, b, spec.pal);
        if (spec.robe)
            build_robe(c, b, spec.pal);
        if (spec.cape)
            build_cape(c, b, spec.pal);
        if (spec.helmet)
            build_helmet(c, b, spec);
        if (spec.hood)
            build_hood(c, b, spec.pal);
        if (spec.hat)
            build_hat(c, b, spec.pal);
        build_face(c, b, spec);
        if (spec.quiver)
            build_quiver(c, b, spec.pal);
        if (spec.shield)
            build_shield(c, b, spec.pal);
        build_weapon(c, b, spec);
        break;
    }

    trim(c.m, spec.lift);
    c.m.theta_deg = kVoxelCarveTheta;
    // Where the figure's ground centre lands in the sprite's own box. The
    // carve fitted 12.0 for a 16-row sprite; three quarters down is the same
    // place and generalises to the short frames.
    c.m.anchor_x = static_cast<float>(sprite_w) * 0.5f;
    c.m.anchor_y = static_cast<float>(sprite_h) * 0.75f;
    recompute_lit(c.m);
    voxel_model_bake_ao(c.m, 0.5f);
    return std::move(c.m);
}

VoxelModel voxel_build_shadow(const VoxelModel& figure, unsigned char index)
{
    VoxelModel m;
    if (figure.empty())
        return m;
    m.w = figure.w;
    m.d = figure.d;
    m.z = 1;
    m.cell = figure.cell;
    m.cube_faces = true;
    m.anchor_x = figure.anchor_x;
    m.anchor_y = figure.anchor_y;
    m.theta_deg = figure.theta_deg;
    const std::size_t n =
        static_cast<std::size_t>(m.w) * static_cast<std::size_t>(m.d);
    m.occ.assign(n, 0u);
    m.index.assign(n, 0u);
    // A soft ellipse a little wider than the footprint the figure occupies.
    int i0 = m.w, i1 = -1, j0 = m.d, j1 = -1;
    for (int k = 0; k < figure.z; ++k)
        for (int j = 0; j < figure.d; ++j)
            for (int i = 0; i < figure.w; ++i)
                if (figure.occ[figure.at(i, j, k)] != 0)
                {
                    i0 = std::min(i0, i);
                    i1 = std::max(i1, i);
                    j0 = std::min(j0, j);
                    j1 = std::max(j1, j);
                }
    if (i1 < 0)
        return m;
    const float ex = (static_cast<float>(i0) + static_cast<float>(i1)) * 0.5f;
    const float ey = (static_cast<float>(j0) + static_cast<float>(j1)) * 0.5f;
    const float rx = std::max(3.0f, static_cast<float>(i1 - i0) * 0.42f);
    const float ry = std::max(3.0f, static_cast<float>(j1 - j0) * 0.42f);
    for (int j = 0; j < m.d; ++j)
        for (int i = 0; i < m.w; ++i)
        {
            const float dx = (static_cast<float>(i) - ex) / rx;
            const float dy = (static_cast<float>(j) - ey) / ry;
            if (dx * dx + dy * dy > 1.0f)
                continue;
            const std::size_t s = m.at(i, j, 0);
            m.occ[s] = 1u;
            m.index[s] = index;
        }
    m.lit.assign(n, 1u);
    // Floor AO everywhere: the shadow is meant to sit at the dark end.
    m.shade.assign(n, 0u);
    return m;
}

} // namespace og::render
