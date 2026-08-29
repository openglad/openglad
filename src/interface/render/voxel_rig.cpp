/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Parametric voxel rigs (docs/voxel-render-design.md §10, rounds 3-4).
//
// Proportions are the chunky voxel-art idiom, not human anatomy: a big cube
// head on a short blocky body, limbs as slabs, everything on a 1-voxel gap so
// the silhouette reads at 16 pixels. The figure is built facing +y (toward
// the viewer — the sprite's "down" facing), so yaw 0 is FACE_DOWN and the
// walker's curdir maps straight onto the volume yaw.
//
// Round 4 is the character pass, and almost all of it is silhouette: nobody
// stands in an A-pose, so the weapon arm bends and lifts its weapon clear of
// the body, the off arm sits a voxel back, the head floats on a narrow neck,
// and every family spends its team band on exactly one element. Faces are two
// dark pits with a lighter brow (a helmet swaps them for a T-visor), because
// at scale 4 that is the whole difference between a figure and a doll.
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

// Room for a planted staff, a raised axe and a hat without clipping.
constexpr int kRigW = 40;
constexpr int kRigD = 32;
constexpr int kRigZ = 56;
// Eye pits, boot leather and blade edges all want the palette's true black.
constexpr unsigned char kInk = 16;
// The palette's grey ramp runs 16 (black) to 31 (white); 31 is the only
// dependable near-white, which a beard and a bone need whatever the sprite
// happens to be painted in.
constexpr unsigned char kBone = 31;

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
    void tint_box(int x0, int y0, int z0, int w, int d, int h,
                  unsigned char idx)
    {
        for (int z = z0; z < z0 + h; ++z)
            for (int y = y0; y < y0 + d; ++y)
                for (int x = x0; x < x0 + w; ++x)
                    tint(x, y, z, idx);
    }
    void clear_box(int x0, int y0, int z0, int w, int d, int h)
    {
        for (int z = z0; z < z0 + h; ++z)
            for (int y = y0; y < y0 + d; ++y)
                for (int x = x0; x < x0 + w; ++x)
                    clear(x, y, z);
    }
    // Centred on x = 0, which is how every body part but the limbs is placed.
    void box_c(int w, int d, int h, int z0, unsigned char idx, int y_off = 0)
    {
        box(-(w / 2), -(d / 2) + y_off, z0, w, d, h, idx);
    }
    void tint_box_c(int w, int d, int h, int z0, unsigned char idx,
                    int y_off = 0)
    {
        tint_box(-(w / 2), -(d / 2) + y_off, z0, w, d, h, idx);
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
            const float t = h > 1
                ? static_cast<float>(z) / static_cast<float>(h - 1)
                : 0.0f;
            cylinder(0.0f, ey, z0 + z, 1, r0 + (r1 - r0) * t, idx);
        }
    }
    // The spec's mirror helper: fold the +x half onto -x. Used for the
    // symmetric body before anything one-handed goes on.
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

// --- proportions (cells) ---------------------------------------------------
struct Body
{
    int torso_w = 12, torso_d = 7, torso_h = 10;
    int leg_w = 5, leg_d = 5, leg_h = 7;
    int boot_h = 2;
    int head_w = 10, head_d = 9, head_h = 9;
    int arm_w = 4, arm_d = 4;
    int z_boot = 0, z_leg = 0, z_torso = 0, z_shoulder = 0, z_head = 0,
        z_top = 0;
    // The weapon hand, after the elbow bend.
    int hand_x = 0, hand_y = 0, hand_z = 0;
    bool sunken_head = false; // orc: skull set into the shoulders
};

Body body_for(const RigSpec& s)
{
    Body b;
    switch (s.archetype)
    {
    case RigArchetype::Skeleton:
        b.torso_w = 8;
        b.torso_d = 5;
        b.torso_h = 9;
        b.head_w = 8;
        b.head_d = 8;
        b.head_h = 8;
        b.arm_w = 2;
        b.arm_d = 2;
        b.leg_w = 3;
        b.leg_d = 3;
        b.leg_h = 8;
        break;
    default:
        b.torso_w += s.torso_widen;
        break;
    }
    if (s.torso_widen >= 2) // orc: heavy build all through
    {
        b.torso_w = 14;
        b.torso_d = 9;
        b.torso_h = 11;
        b.head_w = 11;
        b.head_d = 10;
        b.head_h = 9;
        b.arm_w = 5;
        b.arm_d = 5;
        b.sunken_head = true;
    }
    if (s.torso_widen <= -2) // elf: slim and a little taller in the leg
    {
        b.torso_w = 10;
        b.arm_w = 3;
        b.arm_d = 3;
        b.leg_w = 4;
        b.leg_d = 4;
        b.leg_h = 9;
    }
    b.z_boot = 0;
    b.z_leg = b.boot_h;
    b.z_torso = b.z_leg + b.leg_h;
    b.z_shoulder = b.z_torso + b.torso_h - 1;
    // G4: the head floats on a 2x2 neck, so background shows either side of
    // it and the figure stops reading as a stack of boxes. The orc overrides
    // this — a hunched brute has no neck.
    b.z_head = b.sunken_head ? b.z_shoulder - 1 : b.z_shoulder + 3;
    b.z_top = b.z_head + b.head_h;
    return b;
}

// The frontmost OCCUPIED layer of a centred box. box_c places a depth-d box
// at y0 = -(d/2), so its front face is at d - d/2 - 1, not d/2 - 1 — and one
// layer in from the front is invisible from every angle, which is where every
// eye pit and helmet visor was being painted.
int front_of(int depth)
{
    return depth - depth / 2 - 1;
}

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

// --- body ------------------------------------------------------------------
void build_legs(RigCanvas& c, const Body& b, const RigPalette& p)
{
    // A 1-voxel gap between the legs is what makes them read as two legs at
    // sprite size.
    const int x0 = 1;
    c.box_pair(x0, -(b.leg_d / 2), b.z_leg, b.leg_w, b.leg_d, b.leg_h,
               p.secondary);
    c.box_pair(x0 - 1, -(b.leg_d / 2) - 1, b.z_boot, b.leg_w + 1, b.leg_d + 2,
               b.boot_h, kInk);
}

void build_torso(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    c.box_c(b.torso_w, b.torso_d, b.torso_h, b.z_torso, p.primary);
    if (s.pauldrons)
    {
        // Pauldron blocks two voxels proud of the shoulder line: the fastest
        // way to say "heavy" in a 12-voxel-wide torso.
        c.box_pair(b.torso_w / 2, -(b.torso_d / 2) + 1, b.z_shoulder - 2,
                   3, b.torso_d - 2, 4, p.primary);
    }
    else
    {
        c.box_c(b.torso_w + 2, b.torso_d, 2, b.z_shoulder - 1, p.primary);
    }
    // Belt: one dark row at the waist, which separates torso from legs at any
    // scale.
    c.tint_box_c(b.torso_w + 1, b.torso_d, 1, b.z_torso, kInk);
    if (s.vest)
    {
        // A leather panel over the tunic front. A figure in one flat green
        // dissolves into grass; a brown block on its chest does not.
        const int fy = front_of(b.torso_d);
        c.tint_box(-3, fy - 2, b.z_torso + 3, 6, 3, 6, p.secondary);
        c.tint_box(-3, fy - 2, b.z_torso + 2, 6, 3, 1, kInk);
    }
    if (!b.sunken_head)
        c.box_c(2, 2, 3, b.z_shoulder, p.skin); // the floating neck
}

// G1: a bent elbow. The upper arm hangs, the forearm swings up and forward at
// roughly 40 degrees, and the hand ends up at shoulder height out in front —
// which is the only way a weapon leaves the body's silhouette.
void build_arms(RigCanvas& c, Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int x0 = b.torso_w / 2 + 1;
    const int upper = 5;
    const int elbow_z = b.z_shoulder - upper;

    // Off arm: straight, and a voxel further back so the two arms differ.
    c.box(-x0 - b.arm_w, -(b.arm_d / 2) - 1, elbow_z - 3, b.arm_w, b.arm_d,
          upper + 4, p.primary);
    c.box(-x0 - b.arm_w, -(b.arm_d / 2) - 1, elbow_z - 3, b.arm_w, b.arm_d, 2,
          p.skin);

    // Weapon arm: upper arm down, forearm up and forward.
    c.box(x0, -(b.arm_d / 2), elbow_z, b.arm_w, b.arm_d, upper, p.primary);
    for (int t = 0; t <= upper; ++t)
        c.box(x0, -(b.arm_d / 2) + t, elbow_z + t, b.arm_w, b.arm_d, 1,
              p.primary);
    b.hand_x = x0 + b.arm_w / 2;
    b.hand_y = upper - (b.arm_d / 2);
    b.hand_z = elbow_z + upper;
    c.box(x0, b.hand_y - 1, b.hand_z, b.arm_w, b.arm_d - 1, 1, p.skin);
}

// The archer stance: bow out to the LEFT across the body with its plane
// facing the viewer, string hand drawn back to the chin.
void build_bow_arms(RigCanvas& c, Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int x0 = b.torso_w / 2 + 1;
    const int shoulder = b.z_shoulder;

    // Bow arm reaches out left and level. Mostly SLEEVE: a bare arm the whole
    // way out reads as a plank of skin, not an arm.
    for (int t = 0; t < 6; ++t)
        c.box(-x0 - b.arm_w - t, -(b.arm_d / 2), shoulder - 3, b.arm_w,
              b.arm_d, t < 4 ? (b.arm_w == 3 ? 3 : 4) : 3,
              t < 4 ? p.primary : p.skin);
    // String arm folds in to the chin.
    for (int t = 0; t < 4; ++t)
        c.box(x0 - t, -(b.arm_d / 2) + t, shoulder - 3 + t, b.arm_w, b.arm_d,
              1, p.primary);
    c.box(1, 2, shoulder + 1, 3, 2, 2, p.skin); // string hand at the chin

    b.hand_x = -x0 - b.arm_w - 5;
    b.hand_y = 0;
    b.hand_z = shoulder - 2;
}

void build_head(RigCanvas& c, const Body& b, const RigPalette& p)
{
    c.box_c(b.head_w, b.head_d, b.head_h, b.z_head, p.skin);
}

// G2. Two 2x1 pits with a lighter brow over each: at scale 4 that is the
// whole difference between a face and a blank cube.
void build_face(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int front = front_of(b.head_d);
    const int eye_z = b.z_head + 5;
    for (int dx = 0; dx < 2; ++dx)
    {
        c.tint(2 + dx, front, eye_z, kInk);
        c.tint(-3 + dx, front, eye_z, kInk);
        c.tint(2 + dx, front, eye_z + 1, p.metal);
        c.tint(-3 + dx, front, eye_z + 1, p.metal);
    }
    if (s.tusks)
    {
        // Orc: a lower jaw wider than the skull, with the tusks rising in
        // FRONT of the upper lip so they read from the side too.
        c.box_c(b.head_w + 2, b.head_d, 2, b.z_head, p.skin);
        for (int t = 0; t < 3; ++t)
        {
            c.put(3, front + 1, b.z_head + 1 + t, p.metal);
            c.put(-4, front + 1, b.z_head + 1 + t, p.metal);
        }
    }
    if (s.pointed_ears)
    {
        // Slanted out and up over three rows.
        for (int t = 0; t < 3; ++t)
        {
            c.put(b.head_w / 2 + t / 2, 0, b.z_head + 4 + t, p.skin);
            c.put(-b.head_w / 2 - 1 - t / 2, 0, b.z_head + 4 + t, p.skin);
        }
    }
    if (s.beard)
    {
        // A real beard: six wide under the mouth, tapering to two, standing
        // one voxel proud of the face so it casts its own shade.
        const int fy = front_of(b.head_d);
        const int width[4] = {6, 5, 3, 2};
        for (int t = 0; t < 4; ++t)
            c.box(-(width[t] / 2), fy, b.z_head + 3 - t, width[t], 2, 1,
                  kBone);
    }
    if (s.long_hair)
    {
        // One ramp step lighter than the vest leather, so hair and vest do
        // not merge into a single brown mass from behind.
        const unsigned char hair = (p.secondary % 8) > 0
            ? static_cast<unsigned char>(p.secondary - 1)
            : p.secondary;
        const int back = -(b.head_d / 2);
        c.box(-(b.head_w / 2), back - 1, b.z_head - 3, b.head_w, 1,
              b.head_h + 2, hair);
        c.tint_box_c(b.head_w, b.head_d, 2, b.z_top - 2, hair);
    }
}

// G2: a helmet swaps the face for a T-visor — a 6-wide slit with a vertical
// nasal under it.
void build_helmet(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int base = b.z_top - 5;
    c.tint_box_c(b.head_w, b.head_d, 5, base, p.metal);
    c.tint_box_c(b.head_w + 2, b.head_d + 1, 2, base, p.metal);
    const int front = front_of(b.head_d);
    for (int x = -3; x < 3; ++x)
        c.tint(x, front, base + 1, kInk);
    for (int z = 0; z < 3; ++z)
        c.tint(0, front, base - z, kInk);
    if (s.horns)
        for (int t = 0; t < 2; ++t)
        {
            c.put(b.head_w / 2 - 1 + t, 0, b.z_top + t, p.metal);
            c.put(-b.head_w / 2 - t, 0, b.z_top + t, p.metal);
        }
}

void build_hood(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const unsigned char cloth = cloth_of(p);
    c.tint_box_c(b.head_w, b.head_d, b.head_h - 2, b.z_head + 2, cloth);
    c.box_c(b.head_w + 2, b.head_d + 2, 1, b.z_top - 1, cloth);
    // A point above the crown, which is what separates a hood from a cap.
    c.box_c(4, 4, 2, b.z_top, cloth);
    // Face in shadow under the hood: most of a hood's read.
    const int front = front_of(b.head_d);
    c.tint_box(-3, front, b.z_head + 3, 6, 1, 4, kInk);
}

void build_hat(RigCanvas& c, const Body& b, const RigPalette& p)
{
    // A proper wizard hat, not a party cone: a wide flat brim sitting on the
    // crown, then a tall cone whose tip leans back. The hat wears the robe's
    // BASE colour and the robe itself is a ramp step darker, so the two read
    // apart instead of merging into one brown lump. (The brim is 16 across
    // both ways as asked, but rounded — a square brim reads as a table.)
    const unsigned char cloth = p.secondary != p.primary ? p.secondary
                                                         : cloth_of(p);
    const int brim_z = b.z_head + 8;
    c.cylinder(0.0f, 0.0f, brim_z, 1, 8.0f, cloth);
    for (int t = 0; t < 13; ++t)
    {
        const float u = static_cast<float>(t) / 12.0f;
        const float r = 5.0f * (1.0f - u) + 0.6f * u;
        // Tip bent two voxels toward the back over the length of the cone.
        const float ey = -2.0f * u;
        c.cylinder(0.0f, ey, brim_z + 1 + t, 1, r, cloth);
    }
}

void build_cape(RigCanvas& c, const Body& b, const RigPalette& p,
                unsigned char cloth)
{
    const int back = -(b.torso_d / 2) - 1;
    // Narrower than the shoulders and stopping at the knee: a cape as wide as
    // the figure reads as a slab from every angle but the front.
    for (int z = b.z_boot + 4; z <= b.z_shoulder; ++z)
    {
        const int half = (z < b.z_boot + 10) ? 5 : 4;
        for (int x = -half; x < half; ++x)
            for (int y = back - 1; y <= back; ++y)
                c.put(x, y, z, cloth);
    }
    (void)p;
}

void build_robe(RigCanvas& c, const Body& b, const RigPalette& p)
{
    // Belt high on the torso, hem on the ground: no legs show, and the cone
    // widens all the way down so the figure reads as robed rather than as a
    // man wearing a skirt.
    const int belt_z = b.z_torso + 2;
    c.clear_box(-12, -12, b.z_boot, 24, 24, belt_z - b.z_boot + 1);
    c.taper(b.z_boot, belt_z - b.z_boot, 8.0f, 5.0f, 0.0f, cloth_of(p));
    c.cylinder(0.0f, 0.0f, b.z_boot, 1, 8.0f, p.dark); // hem line
    c.cylinder(0.0f, 0.0f, belt_z, 1, 5.4f, kInk);     // belt
}

void build_quiver(RigCanvas& c, const Body& b, const RigPalette& p)
{
    const int back = -(b.torso_d / 2) - 2;
    for (int z = 0; z < 12; ++z)
    {
        const int lean = z / 3; // about 20 degrees across the back
        c.box(-5 + lean, back, b.z_torso + 3 + z, 4, 3, 1, kInk);
    }
    // Three nocks above the shoulder with lighter tips.
    for (int a = 0; a < 3; ++a)
        for (int z = 0; z < 4; ++z)
            c.put(-2 + a, back + 1, b.z_torso + 15 + z,
                  z >= 2 ? p.metal : p.secondary);
}

void build_shield(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int x0 = -(b.torso_w / 2 + 1 + b.arm_w) - 2;
    const int y = b.torso_d / 2;
    c.box(x0, y, b.z_shoulder - 8, 8, 1, 10, p.metal);
    c.box(x0, y, b.z_shoulder - 8, 8, 1, 1, kInk);
    c.box(x0, y, b.z_shoulder + 1, 8, 1, 1, kInk);
    if (s.chest_cross)
    {
        c.box(x0 + 3, y + 1, b.z_shoulder - 7, 2, 1, 8, p.metal);
        c.box(x0 + 1, y + 1, b.z_shoulder - 4, 6, 1, 2, p.metal);
    }
}

// Weapons go in the hand on +x — the side the sprites draw them on when the
// character faces the viewer — and the round-4 arm puts that hand at shoulder
// height and forward, so everything below clears the body.
void build_weapon(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const int hx = b.hand_x - 1;
    const int hy = b.hand_y;
    const int hz = b.hand_z;
    switch (s.weapon)
    {
    case RigWeapon::None:
        break;
    case RigWeapon::Sword:
    case RigWeapon::Dagger:
    {
        const int blade = (s.weapon == RigWeapon::Sword) ? 16 : 6;
        c.box(hx, hy - 1, hz - 3, 2, 2, 3, kInk);          // grip
        c.box(hx - 2, hy - 1, hz, 6, 2, 1, p.secondary);   // crossguard
        c.box(hx - 2, hy - 1, hz + 1, 6, 2, 1, kInk);      // G3 edge line
        c.box(hx, hy - 1, hz + 2, 2, 2, blade, p.metal);
        break;
    }
    case RigWeapon::Bow:
    {
        // Held out to the left, plane facing the viewer, string vertical.
        const float pi = 3.14159265358979f;
        for (int t = 0; t <= 20; ++t)
        {
            const float u = static_cast<float>(t) / 20.0f;
            const int bow_x = hx - static_cast<int>(std::lround(
                                       4.0f * std::sin(pi * u)));
            c.box(bow_x, hy - 1, hz - 10 + t, 2, 2, 1, kInk);
        }
        for (int t = 2; t <= 18; ++t)
            c.put(hx + 1, hy, hz - 10 + t, p.metal);
        break;
    }
    case RigWeapon::Staff:
    {
        // Planted on the ground beside the right foot when asked, otherwise
        // carried; the orb rides at hat height either way.
        const int sx = s.planted_staff ? b.torso_w / 2 + 2 : hx;
        const int sy = s.planted_staff ? 2 : hy - 1;
        const int base = s.planted_staff ? 0 : hz - 3;
        // Three wide: a two-voxel shaft is one world pixel and disappears
        // against a robe of the same family of browns.
        c.box(sx, sy, base, 3, 3, 30, p.metal);
        c.ellipsoid(static_cast<float>(sx) + 1.0f,
                    static_cast<float>(sy) + 1.0f,
                    static_cast<float>(base) + 32.0f, 4.0f, 4.0f, 4.0f,
                    kVoxelRigOrb);
        break;
    }
    case RigWeapon::Axe:
    {
        c.box(hx, hy - 1, hz - 4, 2, 2, 20, kInk);          // haft
        c.box(hx - 3, hy - 1, hz + 9, 8, 2, 7, p.metal);   // double-bit head
        // Waist the middle top and bottom so it reads as two blades on a
        // haft rather than a picture frame.
        c.clear_box(hx - 1, hy - 1, hz + 9, 4, 2, 2);
        c.clear_box(hx - 1, hy - 1, hz + 14, 4, 2, 2);
        c.box(hx, hy - 1, hz + 9, 2, 2, 7, kInk);          // haft through it
        break;
    }
    case RigWeapon::Club:
    {
        for (int z = 0; z < 14; ++z)
        {
            const int w = 2 + (z > 8 ? 2 : 0);
            c.box(hx - w / 2 + 1, hy - w / 2, hz - 3 + z, w, w, 1, p.dark);
        }
        break;
    }
    case RigWeapon::Mace:
    {
        c.box(hx, hy - 1, hz - 3, 2, 2, 12, kInk);
        c.box(hx - 1, hy - 2, hz + 9, 4, 4, 4, p.metal);
        c.box(hx - 2, hy - 1, hz + 10, 6, 2, 2, p.metal); // flanges
        break;
    }
    case RigWeapon::Spear:
    {
        c.box(hx, hy - 1, hz - 8, 2, 2, 24, kInk);
        c.box(hx, hy - 1, hz + 16, 2, 2, 4, p.metal);
        break;
    }
    }
}

// --- skeleton --------------------------------------------------------------
// No skin anywhere: bone is the light grey ramp, and the read comes from the
// gaps — hollow rib rows, a jaw with tooth gaps, knobbed joints.
void build_skeleton(RigCanvas& c, Body& b, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const unsigned char bone = p.primary;
    const unsigned char bone_lit = p.skin;

    // Legs and arms as 2x2 bones with 3x3 joint knobs.
    c.box_pair(1, -1, b.z_leg, 2, 2, b.leg_h, bone);
    c.box_pair(1, -1, b.z_boot, 3, 3, b.boot_h, kInk); // tattered wrappings
    c.box_pair(1, -1, b.z_leg + b.leg_h / 2, 3, 3, 1, bone_lit); // knees
    c.box_pair(1, -1, b.z_torso, 3, 3, 1, bone_lit);             // hips

    // Ribcage: rows 2, 4 and 6 hollow across the front and back faces,
    // leaving a two-wide spine column.
    c.box_c(b.torso_w, b.torso_d, b.torso_h, b.z_torso, bone);
    for (int r : {2, 4, 6})
    {
        const int z = b.z_torso + r;
        c.clear_box(-(b.torso_w / 2), -(b.torso_d / 2), z, b.torso_w,
                    b.torso_d, 1);
        c.box(-1, -(b.torso_d / 2), z, 2, b.torso_d, 1, bone_lit); // spine
    }
    // Tattered loincloth, two rows.
    c.tint_box_c(b.torso_w + 1, b.torso_d, 2, b.z_torso, kInk);

    // Shoulders and arms, weapon arm bent as for a humanoid.
    c.box_c(b.torso_w + 2, b.torso_d - 1, 1, b.z_shoulder, bone);
    const int x0 = b.torso_w / 2 + 1;
    const int upper = 5;
    const int elbow_z = b.z_shoulder - upper;
    c.box(-x0 - b.arm_w, -1, elbow_z - 3, b.arm_w, b.arm_d, upper + 4, bone);
    c.box(x0, -1, elbow_z, b.arm_w, b.arm_d, upper, bone);
    for (int t = 0; t <= upper; ++t)
        c.box(x0, -1 + t, elbow_z + t, b.arm_w, b.arm_d, 1, bone);
    c.box(x0 - 1, -1, b.z_shoulder - 1, 3, 3, 1, bone_lit);  // shoulder knob
    c.box(x0 - 1, -1, elbow_z, 3, 3, 1, bone_lit);           // elbow knob
    b.hand_x = x0 + b.arm_w / 2;
    b.hand_y = upper - 1;
    b.hand_z = elbow_z + upper;

    // Neck and skull.
    c.box_c(2, 2, 3, b.z_shoulder, bone);
    c.box_c(b.head_w, b.head_d, b.head_h, b.z_head, bone);
    const int front = front_of(b.head_d);
    // Deep 2x2 sockets, a nose hole, and a jaw one voxel narrower with three
    // tooth gaps.
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
        {
            c.tint(1 + dx, front, b.z_head + 4 + dz, kInk);
            c.tint(-3 + dx, front, b.z_head + 4 + dz, kInk);
        }
    c.tint(0, front, b.z_head + 3, kInk);
    c.clear_box(-(b.head_w / 2), -(b.head_d / 2), b.z_head, 1, b.head_d, 2);
    c.clear_box(b.head_w / 2 - 1, -(b.head_d / 2), b.z_head, 1, b.head_d, 2);
    for (int t = -2; t <= 2; t += 2)
        c.tint(t, front, b.z_head + 1, kInk);
}

// --- ghost / slime ---------------------------------------------------------
void build_ghost(RigCanvas& c, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    const unsigned char body = p.primary;
    c.ellipsoid(0.0f, 0.0f, 24.0f, 6.0f, 5.0f, 4.0f, body);
    for (int z = 6; z <= 24; ++z)
    {
        const float t = static_cast<float>(24 - z) / 18.0f;
        c.cylinder(0.0f, 0.0f, z, 1, 5.0f + 3.0f * t, body);
    }
    // Hollow it out so it reads as a sheet rather than a lump.
    for (int z = 8; z <= 22; ++z)
    {
        const float t = static_cast<float>(24 - z) / 18.0f;
        const float r = 5.0f + 3.0f * t - 1.6f;
        if (r <= 0.5f)
            continue;
        for (int y = -10; y <= 10; ++y)
            for (int x = -10; x <= 10; ++x)
            {
                const float dx = static_cast<float>(x) + 0.5f;
                const float dy = static_cast<float>(y) + 0.5f;
                if (dx * dx + dy * dy <= r * r)
                    c.clear(x, y, z);
            }
    }
    // Wavy hem, two rows deep so the notches actually read.
    for (int x = -9; x <= 9; ++x)
        for (int y = -9; y <= 9; ++y)
            if (((x + y) & 2) != 0)
            {
                c.clear(x, y, 6);
                c.clear(x, y, 7);
                c.clear(x, y, 8);
            }
    const int front = 4;
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
        {
            c.tint(1 + dx, front, 24 + dz, kInk);
            c.tint(-3 + dx, front, 24 + dz, kInk);
        }
    // A ghost has no clothing to put a team band on, so it wears a sash.
    if (p.team != 0)
        c.cylinder(0.0f, 0.0f, 14, 2, 7.0f, p.team);
}

void build_slime(RigCanvas& c, const RigSpec& s)
{
    const RigPalette& p = s.pal;
    c.ellipsoid(0.0f, 0.0f, 0.0f, 9.0f, 7.0f, 10.0f, p.primary);
    const int front = 5;
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz)
        {
            c.tint(1 + dx, front, 6 + dz, kInk);
            c.tint(-3 + dx, front, 6 + dz, kInk);
        }
}

// G5: exactly one element per family carries the team band.
void apply_team(RigCanvas& c, const Body& b, const RigSpec& s)
{
    const unsigned char team = s.pal.team;
    if (team == 0 || s.team_slot == RigTeamSlot::None)
        return;
    switch (s.team_slot)
    {
    case RigTeamSlot::Sash:
        c.tint_box_c(b.torso_w + 3, b.torso_d, 3, b.z_shoulder - 4, team);
        break;
    case RigTeamSlot::Crest:
        // A ridge along the top of the helmet, not a single plume voxel.
        for (int y = -3; y <= 2; ++y)
            for (int z = 0; z < 3; ++z)
                c.put(0, y, b.z_top + z, team);
        break;
    case RigTeamSlot::Armband:
        c.tint_box(b.torso_w / 2 + 1, -2, b.z_shoulder - 3, b.arm_w, 3, 2,
                   team);
        break;
    case RigTeamSlot::ChestStrap:
        // Diagonal across the chest, following the strap.
        for (int t = 0; t < 9; ++t)
            c.tint_box(-4 + t, -(b.torso_d / 2), b.z_shoulder - 8 + t, 2,
                       b.torso_d, 2, team);
        break;
    case RigTeamSlot::HatBand:
        // One row above the brim. The round-4 band landed at z_top - 2, which
        // is inside the HEAD — it read as a red blindfold across the face.
        c.cylinder(0.0f, 0.0f, b.z_head + 9, 1, 5.4f, team);
        break;
    case RigTeamSlot::HoodTrim:
        c.tint_box_c(b.head_w + 2, b.head_d + 2, 1, b.z_head + 2, team);
        c.tint_box_c(b.head_w + 2, b.head_d + 2, 1, b.z_head + 3, team);
        break;
    case RigTeamSlot::Belt:
        c.tint_box_c(b.torso_w + 1, b.torso_d, 1, b.z_torso, team);
        c.tint_box_c(b.torso_w + 1, b.torso_d, 1, b.z_torso + 1, team);
        break;
    case RigTeamSlot::None:
        break;
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
    Body b = body_for(spec);

    switch (spec.archetype)
    {
    case RigArchetype::Ghost:
        build_ghost(c, spec);
        break;
    case RigArchetype::Slime:
        build_slime(c, spec);
        break;
    case RigArchetype::Skeleton:
        build_skeleton(c, b, spec);
        build_weapon(c, b, spec);
        break;
    case RigArchetype::Humanoid:
        build_legs(c, b, spec.pal);
        build_torso(c, b, spec);
        if (spec.bow_pose)
            build_bow_arms(c, b, spec);
        else
            build_arms(c, b, spec);
        build_head(c, b, spec.pal);
        if (spec.robe)
            build_robe(c, b, spec.pal);
        if (spec.cape)
            // The team band is spent elsewhere for every caped family so far,
            // and a cape in the armour's own colour is not a cape. Black.
            build_cape(c, b, spec.pal,
                       spec.team_slot == RigTeamSlot::Sash && spec.pal.team != 0
                           ? spec.pal.team
                           : kInk);
        if (spec.helmet)
            build_helmet(c, b, spec);
        if (spec.hood)
            build_hood(c, b, spec.pal);
        if (spec.hat)
            build_hat(c, b, spec.pal);
        if (!spec.helmet)
            build_face(c, b, spec);
        if (spec.chest_cross)
        {
            // A white cross on the chest, three wide over five rows.
            c.tint_box_c(3, b.torso_d, 5, b.z_shoulder - 7, spec.pal.metal);
            c.tint_box_c(7, b.torso_d, 2, b.z_shoulder - 5, spec.pal.metal);
        }
        if (spec.quiver)
            build_quiver(c, b, spec.pal);
        if (spec.shield)
            build_shield(c, b, spec);
        build_weapon(c, b, spec);
        break;
    }
    apply_team(c, b, spec);

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

VoxelModel voxel_build_projectile(RigProjectile kind, unsigned char body,
                                  unsigned char tip, unsigned char tail,
                                  int sprite_w, int sprite_h, int span_px)
{
    // Built pointing along +y, which is the direction a living faces at yaw 0,
    // so the walker's curdir maps onto the volume yaw with no special case.
    VoxelModel m;
    m.cell = kVoxelRigCell;
    m.cube_faces = true;
    m.theta_deg = kVoxelCarveTheta;
    m.anchor_x = static_cast<float>(sprite_w) * 0.5f;
    m.anchor_y = static_cast<float>(sprite_h) * 0.5f;
    if (kind == RigProjectile::Dart)
    {
        m.w = 4;
        m.d = 24;
        m.z = 4;
    }
    else
    {
        // Two cells to the world pixel, so a sprite-sized blob needs twice
        // its diameter in cells.
        const int span = std::clamp(span_px > 0 ? span_px : 4, 3, 12);
        m.w = span * 2;
        m.d = span * 2;
        m.z = span * 2;
    }
    const std::size_t n = static_cast<std::size_t>(m.w) *
                          static_cast<std::size_t>(m.d) *
                          static_cast<std::size_t>(m.z);
    m.occ.assign(n, 0u);
    m.index.assign(n, 0u);
    const auto put = [&](int i, int j, int k, unsigned char idx) {
        if (i < 0 || j < 0 || k < 0 || i >= m.w || j >= m.d || k >= m.z)
            return;
        const std::size_t t = m.at(i, j, k);
        m.occ[t] = 1u;
        m.index[t] = idx;
    };

    if (kind == RigProjectile::Dart)
    {
        // Shaft, a bright head at the leading end, dark flights at the tail.
        for (int j = 2; j < 22; ++j)
            for (int k = 1; k <= 2; ++k)
                for (int i = 1; i <= 2; ++i)
                    put(i, j, k, j >= 18 ? tip : body);
        for (int j = 2; j < 6; ++j)
        {
            put(0, j, 1, tail);
            put(0, j, 2, tail);
            put(3, j, 1, tail);
            put(3, j, 2, tail);
        }
    }
    else
    {
        const float c = static_cast<float>(m.w) * 0.5f;
        const float r = c - 0.4f;
        const float core = r - 1.8f;
        for (int k = 0; k < m.z; ++k)
            for (int j = 0; j < m.d; ++j)
                for (int i = 0; i < m.w; ++i)
                {
                    const float dx = static_cast<float>(i) + 0.5f - c;
                    const float dy = static_cast<float>(j) + 0.5f - c;
                    const float dz = static_cast<float>(k) + 0.5f - c;
                    const float d2 = dx * dx + dy * dy + dz * dz;
                    if (d2 <= r * r)
                        put(i, j, k,
                            (core > 0.0f && d2 <= core * core) ? tip : body);
                }
    }
    recompute_lit(m);
    voxel_model_bake_ao(m, 0.5f);
    return m;
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
    // Sized to the footprint the figure's LOWER half occupies: a raised axe
    // must not cast a shadow the width of the map.
    int i0 = m.w, i1 = -1, j0 = m.d, j1 = -1;
    const int knee = std::max(1, figure.z / 3);
    for (int k = 0; k < knee; ++k)
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
    const float rx = std::max(3.0f, static_cast<float>(i1 - i0) * 0.62f);
    const float ry = std::max(3.0f, static_cast<float>(j1 - j0) * 0.62f);
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
    m.shade.assign(n, 0u);
    return m;
}

} // namespace og::render
