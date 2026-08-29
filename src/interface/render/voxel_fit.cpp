/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Fitted sprite-textured bodies (docs/voxel-render-design.md §13).
//
// The projection used everywhere below is the one the sprites live in. Put the
// body's footprint centre on the ground at the sprite's foot line — world
// (sprite_w/2, sprite_h) — and view it with the game camera at elevation
// theta. A body cell at model-local (mx, my, mz) then lands at
//
//     u = sprite_w/2 + wx
//     v = (sprite_h - 1) + wy*sin(theta) - mz*cos(theta)
//     near = wy*cos(theta) + mz*sin(theta)
//
// where (wx, wy) is (mx, my) turned by the facing's yaw. A cell on the ground
// under the body's centre lands on the sprite's bottom row, which is what
// makes "fit the shape against the frames" a well-posed question: model and
// sprite are measured in the same pixels. The Free camera reproduces this
// exactly with view_cy = ((h-1) - h*sin(theta)) * scale, so nothing about the
// scoring is a private convention.

#include <openglad/interface/render/voxel_fit.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
// Body cells are half a sprite pixel: 2x sprite scale, as in earlier rounds.
constexpr float kCell = 0.5f;
constexpr int kW = 36;
constexpr int kD = 28;
constexpr int kZ = 44;
constexpr std::size_t kCells =
    static_cast<std::size_t>(kW) * kD * kZ;

inline std::size_t idx3(int i, int j, int k)
{
    return (static_cast<std::size_t>(k) * static_cast<std::size_t>(kD) +
            static_cast<std::size_t>(j)) *
               static_cast<std::size_t>(kW) +
           static_cast<std::size_t>(i);
}

struct Body
{
    std::vector<unsigned char> occ;
    std::vector<int> solid; // packed i | j<<8 | k<<16
    Body() : occ(kCells, 0u) { solid.reserve(6000); }
    void reset()
    {
        std::fill(occ.begin(), occ.end(), 0u);
        solid.clear();
    }
    void put(int i, int j, int k)
    {
        if (i < 0 || j < 0 || k < 0 || i >= kW || j >= kD || k >= kZ)
            return;
        const std::size_t s = idx3(i, j, k);
        if (occ[s] != 0)
            return;
        occ[s] = 1u;
        solid.push_back(i | (j << 8) | (k << 16));
    }
    [[nodiscard]] bool at(int i, int j, int k) const
    {
        if (i < 0 || j < 0 || k < 0 || i >= kW || j >= kD || k >= kZ)
            return false;
        return occ[idx3(i, j, k)] != 0;
    }
};

// --- primitives (generic; no proportions are baked in) ---------------------
void box(Body& b, int cx, int cy, int z0, int w, int d, int h)
{
    for (int k = z0; k < z0 + h; ++k)
        for (int j = cy - d / 2; j < cy - d / 2 + d; ++j)
            for (int i = cx - w / 2; i < cx - w / 2 + w; ++i)
                b.put(i, j, k);
}

void ellipsoid(Body& b, int cx, int cy, int cz, int rx, int ry, int rz)
{
    if (rx <= 0 || ry <= 0 || rz <= 0)
        return;
    for (int k = cz - rz; k <= cz + rz; ++k)
        for (int j = cy - ry; j <= cy + ry; ++j)
            for (int i = cx - rx; i <= cx + rx; ++i)
            {
                const float dx = static_cast<float>(i - cx) /
                    static_cast<float>(rx);
                const float dy = static_cast<float>(j - cy) /
                    static_cast<float>(ry);
                const float dz = static_cast<float>(k - cz) /
                    static_cast<float>(rz);
                if (dx * dx + dy * dy + dz * dz <= 1.0f)
                    b.put(i, j, k);
            }
}

// A limb: a square-section rod walked out from a joint along a pitch.
// pitch 0 hangs straight down, 90 points forward and level.
void limb(Body& b, int x, int y, int z, int thickness, int length, int pitch)
{
    const float pr = static_cast<float>(pitch) * kDeg2Rad;
    const float dy = std::sin(pr);
    const float dz = -std::cos(pr);
    for (int s = 0; s < length; ++s)
    {
        const float t = static_cast<float>(s);
        const int jy = y + static_cast<int>(std::lround(dy * t));
        const int kz = z + static_cast<int>(std::lround(dz * t));
        box(b, x, jy, kz, thickness, thickness, 1);
    }
}

void rod(Body& b, int x, int y, int z, int thickness, int length, int pitch,
         int yaw)
{
    const float pr = static_cast<float>(pitch) * kDeg2Rad;
    const float yr = static_cast<float>(yaw) * kDeg2Rad;
    const float dx = std::cos(pr) * std::sin(yr);
    const float dy = std::cos(pr) * std::cos(yr);
    const float dz = std::sin(pr);
    for (int s = 0; s < length; ++s)
    {
        const float t = static_cast<float>(s);
        box(b, x + static_cast<int>(std::lround(dx * t)),
            y + static_cast<int>(std::lround(dy * t)),
            z + static_cast<int>(std::lround(dz * t)), thickness, thickness, 1);
    }
}

// --- parameter ranges -------------------------------------------------------
struct Range
{
    int lo, hi;
};

const std::array<Range, FP_COUNT>& ranges()
{
    static const std::array<Range, FP_COUNT> r = [] {
        std::array<Range, FP_COUNT> a{};
        a[FP_HEAD_CX] = {-4, 4};
        a[FP_HEAD_Z] = {8, 40};
        a[FP_HEAD_RX] = {2, 10};
        a[FP_HEAD_RY] = {2, 10};
        a[FP_HEAD_RZ] = {2, 10};
        a[FP_TORSO_W] = {3, 22};
        a[FP_TORSO_D] = {2, 16};
        a[FP_TORSO_H] = {3, 24};
        a[FP_TORSO_Z] = {0, 26};
        a[FP_TORSO_Y] = {-5, 5};
        a[FP_ARML_TH] = {1, 7};
        a[FP_ARML_LEN] = {0, 18};
        a[FP_ARML_Z] = {4, 40};
        a[FP_ARML_X] = {2, 16};
        a[FP_ARML_Y] = {-6, 6};
        a[FP_ARML_PITCH] = {-30, 110};
        a[FP_ARMR_TH] = {1, 7};
        a[FP_ARMR_LEN] = {0, 18};
        a[FP_ARMR_Z] = {4, 40};
        a[FP_ARMR_X] = {2, 16};
        a[FP_ARMR_Y] = {-6, 6};
        a[FP_ARMR_PITCH] = {-30, 110};
        a[FP_LEG_TH] = {1, 8};
        a[FP_LEG_LEN] = {0, 20};
        a[FP_LEG_GAP] = {0, 10};
        a[FP_CAPE_ON] = {0, 1};
        a[FP_CAPE_W] = {2, 20};
        a[FP_CAPE_LEN] = {2, 26};
        a[FP_CAPE_TH] = {1, 4};
        a[FP_WEAP_ON] = {0, 1};
        a[FP_WEAP_SIDE] = {0, 1};
        a[FP_WEAP_LEN] = {0, 22};
        a[FP_WEAP_TH] = {1, 4};
        a[FP_WEAP_PITCH] = {-90, 90};
        a[FP_WEAP_YAW] = {-90, 90};
        a[FP_WEAP_HAND_Z] = {4, 40};
        a[FP_GEAR_KIND] = {0, 3};
        a[FP_GEAR_RX] = {2, 10};
        a[FP_GEAR_RZ] = {1, 12};
        a[FP_BLOB_HEM_ROWS] = {0, 12};
        a[FP_BLOB_HEM_STEP] = {0, 3};
        return a;
    }();
    return r;
}

int clamp_param(int slot, int v)
{
    const Range r = ranges()[static_cast<std::size_t>(slot)];
    return std::clamp(v, r.lo, r.hi);
}

// --- template construction --------------------------------------------------
void build_humanoid(Body& b, const FitParams& fp)
{
    const auto& p = fp.p;
    const int cx = kW / 2;
    const int cy = kD / 2;

    // Legs
    if (p[FP_LEG_LEN] > 0 && p[FP_LEG_TH] > 0)
    {
        const int off = p[FP_LEG_GAP] / 2 + p[FP_LEG_TH] / 2;
        box(b, cx + off, cy, 0, p[FP_LEG_TH], p[FP_LEG_TH], p[FP_LEG_LEN]);
        box(b, cx - off, cy, 0, p[FP_LEG_TH], p[FP_LEG_TH], p[FP_LEG_LEN]);
    }
    // Torso
    box(b, cx, cy + p[FP_TORSO_Y], p[FP_TORSO_Z], p[FP_TORSO_W],
        p[FP_TORSO_D], p[FP_TORSO_H]);
    // Cape: a sheet behind the torso.
    if (p[FP_CAPE_ON] != 0)
    {
        const int back = cy + p[FP_TORSO_Y] - p[FP_TORSO_D] / 2 -
            p[FP_CAPE_TH];
        box(b, cx, back, p[FP_TORSO_Z] + p[FP_TORSO_H] - p[FP_CAPE_LEN],
            p[FP_CAPE_W], p[FP_CAPE_TH], p[FP_CAPE_LEN]);
    }
    // Arms, independently posed left and right.
    limb(b, cx + p[FP_ARMR_X], cy + p[FP_ARMR_Y], p[FP_ARMR_Z],
         p[FP_ARMR_TH], p[FP_ARMR_LEN], p[FP_ARMR_PITCH]);
    limb(b, cx - p[FP_ARML_X], cy + p[FP_ARML_Y], p[FP_ARML_Z],
         p[FP_ARML_TH], p[FP_ARML_LEN], p[FP_ARML_PITCH]);
    // Head
    ellipsoid(b, cx + p[FP_HEAD_CX], cy, p[FP_HEAD_Z], p[FP_HEAD_RX],
              p[FP_HEAD_RY], p[FP_HEAD_RZ]);
    // Headgear
    switch (p[FP_GEAR_KIND])
    {
    case 1: // dome
        ellipsoid(b, cx + p[FP_HEAD_CX], cy, p[FP_HEAD_Z] + p[FP_HEAD_RZ] / 2,
                  p[FP_GEAR_RX], p[FP_GEAR_RX], p[FP_GEAR_RZ]);
        break;
    case 2: // cone
        for (int t = 0; t < p[FP_GEAR_RZ]; ++t)
        {
            const float u = p[FP_GEAR_RZ] > 1
                ? static_cast<float>(t) /
                    static_cast<float>(p[FP_GEAR_RZ] - 1)
                : 0.0f;
            const int r = std::max(1, static_cast<int>(std::lround(
                                          static_cast<float>(p[FP_GEAR_RX]) *
                                          (1.0f - u))));
            ellipsoid(b, cx + p[FP_HEAD_CX], cy,
                      p[FP_HEAD_Z] + p[FP_HEAD_RZ] + t, r, r, 1);
        }
        break;
    case 3: // brim
        ellipsoid(b, cx + p[FP_HEAD_CX], cy, p[FP_HEAD_Z] + p[FP_HEAD_RZ],
                  p[FP_GEAR_RX], p[FP_GEAR_RX], std::max(1, p[FP_GEAR_RZ] / 3));
        break;
    default:
        break;
    }
    // Weapon in one hand.
    if (p[FP_WEAP_ON] != 0 && p[FP_WEAP_LEN] > 0)
    {
        const int side = (p[FP_WEAP_SIDE] != 0) ? 1 : -1;
        const int hx = cx + side * (p[FP_WEAP_SIDE] != 0 ? p[FP_ARMR_X]
                                                         : p[FP_ARML_X]);
        rod(b, hx, cy, p[FP_WEAP_HAND_Z], p[FP_WEAP_TH], p[FP_WEAP_LEN],
            p[FP_WEAP_PITCH], p[FP_WEAP_YAW]);
    }
}

void build_blob(Body& b, const FitParams& fp)
{
    const auto& p = fp.p;
    const int cx = kW / 2;
    const int cy = kD / 2;
    const int cz = p[FP_HEAD_Z];
    ellipsoid(b, cx, cy, cz, p[FP_HEAD_RX], p[FP_HEAD_RY], p[FP_HEAD_RZ]);
    // Optional skirt: rows that step outward as they descend, for a hem.
    for (int t = 0; t < p[FP_BLOB_HEM_ROWS]; ++t)
    {
        const int z = cz - p[FP_HEAD_RZ] - t;
        if (z < 0)
            break;
        const int r = p[FP_HEAD_RX] + (t * p[FP_BLOB_HEM_STEP]) / 4;
        const int ry = p[FP_HEAD_RY] + (t * p[FP_BLOB_HEM_STEP]) / 4;
        ellipsoid(b, cx, cy, z, r, ry, 1);
    }
}

void build_body(Body& b, const FitParams& fp)
{
    b.reset();
    if (fp.tmpl == FitTemplate::Blob)
        build_blob(b, fp);
    else
        build_humanoid(b, fp);
}

// --- projection ---------------------------------------------------------
struct View
{
    float ca = 1.0f, sa = 0.0f; // facing yaw
    float st = 0.0f, ct = 1.0f; // theta
};

View view_for(int facing, float theta_deg)
{
    View v;
    const float a = static_cast<float>((facing - 4) * 45) * kDeg2Rad;
    v.ca = std::cos(a);
    v.sa = std::sin(a);
    v.st = std::sin(theta_deg * kDeg2Rad);
    v.ct = std::cos(theta_deg * kDeg2Rad);
    return v;
}

struct Hit
{
    int u = 0, v = 0;
    float near = 0.0f;
};

inline Hit project_cell(const View& vw, int i, int j, int k, int sw, int sh,
                        float scale)
{
    const float mx = (static_cast<float>(i) + 0.5f -
                      static_cast<float>(kW) * 0.5f) * kCell;
    const float my = (static_cast<float>(j) + 0.5f -
                      static_cast<float>(kD) * 0.5f) * kCell;
    const float mz = (static_cast<float>(k) + 0.5f) * kCell;
    const float wx = mx * vw.ca - my * vw.sa;
    const float wy = mx * vw.sa + my * vw.ca;
    Hit hh;
    hh.u = static_cast<int>(std::floor(
        (static_cast<float>(sw) * 0.5f + wx) * scale));
    hh.v = static_cast<int>(std::floor(
        (static_cast<float>(sh - 1) + wy * vw.st - mz * vw.ct) * scale));
    hh.near = wy * vw.ct + mz * vw.st;
    return hh;
}

// Nearest-cell index render into a sprite-sized buffer. `owner` optionally
// receives the winning cell so texturing can reuse the same visibility.
void render_cells(const Body& b, const View& vw, int sw, int sh, float scale,
                  int out_w, int out_h, std::vector<float>& zbuf,
                  std::vector<int>& owner)
{
    const std::size_t n =
        static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h);
    zbuf.assign(n, -std::numeric_limits<float>::infinity());
    owner.assign(n, -1);
    for (int packed : b.solid)
    {
        const int i = packed & 0xFF;
        const int j = (packed >> 8) & 0xFF;
        const int k = (packed >> 16) & 0xFF;
        const Hit hh = project_cell(vw, i, j, k, sw, sh, scale);
        if (hh.u < 0 || hh.v < 0 || hh.u >= out_w || hh.v >= out_h)
            continue;
        const std::size_t s = static_cast<std::size_t>(hh.v) *
                static_cast<std::size_t>(out_w) +
            static_cast<std::size_t>(hh.u);
        if (hh.near <= zbuf[s])
            continue;
        zbuf[s] = hh.near;
        owner[s] = packed;
    }
}

// --- loss -------------------------------------------------------------------
struct Scratch
{
    Body body;
    std::vector<float> zbuf;
    std::vector<int> owner;
};

float silhouette_loss(const VoxelCarveFrames& fr, const FitParams& fp,
                      float theta_deg, Scratch& sc, float* iou_out)
{
    build_body(sc.body, fp);
    if (sc.body.solid.empty())
        return 2.0f;
    float sum = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        const View vw = view_for(d, theta_deg);
        render_cells(sc.body, vw, fr.w, fr.h, 1.0f, fr.w, fr.h, sc.zbuf,
                     sc.owner);
        int inter = 0, uni = 0;
        for (int i = 0; i < fr.w * fr.h; ++i)
        {
            const bool a = fr.frame[d] != nullptr &&
                fr.frame[d][static_cast<std::size_t>(i)] != 0;
            const bool bb = sc.owner[static_cast<std::size_t>(i)] >= 0;
            if (a || bb)
                ++uni;
            if (a && bb)
                ++inter;
        }
        const float iou = uni > 0 ? static_cast<float>(inter) /
                static_cast<float>(uni)
                                  : 0.0f;
        if (iou_out != nullptr)
            iou_out[d] = iou;
        sum += 1.0f - iou;
    }
    return sum / static_cast<float>(NUM_VOXEL_FACINGS);
}

void texture_body(const Body& b, const VoxelCarveFrames& fr, float theta_deg,
                  VoxelModel& out);

// Stage 2 of the loss: silhouette plus a quarter weight on how wrong the
// back-projected colours come out. Shape that puts the right pixels in the
// right place scores better than shape that merely covers the same area.
float textured_loss(const VoxelCarveFrames& fr, const FitParams& fp,
                    float theta_deg, Scratch& sc, VoxelModel& tmp)
{
    const float sil = silhouette_loss(fr, fp, theta_deg, sc, nullptr);
    if (sc.body.solid.empty())
        return sil;
    texture_body(sc.body, fr, theta_deg, tmp);
    std::vector<unsigned char> shot(
        static_cast<std::size_t>(fr.w) * static_cast<std::size_t>(fr.h));
    float miss = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        voxel_fit_render(tmp, d, theta_deg, shot.data(), fr.w, fr.h, fr.w,
                         fr.h, 1.0f);
        int both = 0, bad = 0;
        for (int i = 0; i < fr.w * fr.h; ++i)
        {
            const unsigned char a = fr.frame[d] != nullptr
                ? fr.frame[d][static_cast<std::size_t>(i)]
                : 0u;
            const unsigned char b = shot[static_cast<std::size_t>(i)];
            if (a == 0 || b == 0)
                continue;
            ++both;
            if (a != b)
                ++bad;
        }
        miss += both > 0 ? static_cast<float>(bad) / static_cast<float>(both)
                         : 1.0f;
    }
    return sil + 0.25f * miss / static_cast<float>(NUM_VOXEL_FACINGS);
}

// --- deterministic restarts -------------------------------------------------
struct Lcg
{
    std::uint32_t s;
    std::uint32_t next()
    {
        s = s * 1664525u + 1013904223u;
        return s;
    }
    int range(int lo, int hi)
    {
        if (hi <= lo)
            return lo;
        return lo + static_cast<int>(next() %
                                     static_cast<std::uint32_t>(hi - lo + 1));
    }
};

FitParams initial_humanoid(const VoxelCarveFrames& fr)
{
    // A starting point taken from the sprite's own bounding box, not a design:
    // the solver moves every one of these.
    int x0 = fr.w, x1 = -1, y0 = fr.h, y1 = -1;
    for (int v = 0; v < fr.h; ++v)
        for (int u = 0; u < fr.w; ++u)
            if (fr.frame[4] != nullptr &&
                fr.frame[4][static_cast<std::size_t>(v) *
                                static_cast<std::size_t>(fr.w) +
                            static_cast<std::size_t>(u)] != 0)
            {
                x0 = std::min(x0, u);
                x1 = std::max(x1, u);
                y0 = std::min(y0, v);
                y1 = std::max(y1, v);
            }
    if (x1 < 0)
    {
        x0 = 0;
        x1 = fr.w - 1;
        y0 = 0;
        y1 = fr.h - 1;
    }
    const int bw = (x1 - x0 + 1) * 2; // cells
    const int bh = (y1 - y0 + 1) * 2;
    FitParams fp;
    fp.tmpl = FitTemplate::Humanoid;
    auto& p = fp.p;
    p[FP_LEG_TH] = std::max(2, bw / 5);
    p[FP_LEG_LEN] = std::max(2, bh / 4);
    p[FP_LEG_GAP] = 2;
    p[FP_TORSO_W] = std::max(4, bw * 2 / 3);
    p[FP_TORSO_D] = std::max(3, bw / 3);
    p[FP_TORSO_H] = std::max(3, bh / 3);
    p[FP_TORSO_Z] = p[FP_LEG_LEN];
    p[FP_TORSO_Y] = 0;
    p[FP_HEAD_RX] = std::max(2, bw / 4);
    p[FP_HEAD_RY] = std::max(2, bw / 4);
    p[FP_HEAD_RZ] = std::max(2, bh / 8);
    p[FP_HEAD_Z] = p[FP_TORSO_Z] + p[FP_TORSO_H] + p[FP_HEAD_RZ];
    p[FP_HEAD_CX] = 0;
    p[FP_ARMR_TH] = std::max(1, bw / 6);
    p[FP_ARMR_LEN] = std::max(2, bh / 4);
    p[FP_ARMR_Z] = p[FP_TORSO_Z] + p[FP_TORSO_H] - 1;
    p[FP_ARMR_X] = p[FP_TORSO_W] / 2 + 1;
    p[FP_ARMR_Y] = 0;
    p[FP_ARMR_PITCH] = 0;
    p[FP_ARML_TH] = p[FP_ARMR_TH];
    p[FP_ARML_LEN] = p[FP_ARMR_LEN];
    p[FP_ARML_Z] = p[FP_ARMR_Z];
    p[FP_ARML_X] = p[FP_ARMR_X];
    p[FP_ARML_Y] = 0;
    p[FP_ARML_PITCH] = 0;
    p[FP_CAPE_ON] = 0;
    p[FP_CAPE_W] = p[FP_TORSO_W];
    p[FP_CAPE_LEN] = p[FP_TORSO_H];
    p[FP_CAPE_TH] = 1;
    p[FP_WEAP_ON] = 0;
    p[FP_WEAP_SIDE] = 1;
    p[FP_WEAP_LEN] = 8;
    p[FP_WEAP_TH] = 1;
    p[FP_WEAP_PITCH] = 45;
    p[FP_WEAP_YAW] = 0;
    p[FP_WEAP_HAND_Z] = p[FP_ARMR_Z] - p[FP_ARMR_LEN];
    p[FP_GEAR_KIND] = 0;
    p[FP_GEAR_RX] = p[FP_HEAD_RX];
    p[FP_GEAR_RZ] = 2;
    for (int s = 0; s < FP_COUNT; ++s)
        p[static_cast<std::size_t>(s)] =
            clamp_param(s, p[static_cast<std::size_t>(s)]);
    return fp;
}

FitParams initial_blob(const VoxelCarveFrames& fr)
{
    FitParams fp;
    fp.tmpl = FitTemplate::Blob;
    auto& p = fp.p;
    p[FP_HEAD_RX] = std::max(2, fr.w / 2);
    p[FP_HEAD_RY] = std::max(2, fr.w / 3);
    p[FP_HEAD_RZ] = std::max(2, fr.h / 2);
    p[FP_HEAD_Z] = p[FP_HEAD_RZ];
    p[FP_BLOB_HEM_ROWS] = 0;
    p[FP_BLOB_HEM_STEP] = 1;
    for (int s = 0; s < FP_COUNT; ++s)
        p[static_cast<std::size_t>(s)] =
            clamp_param(s, p[static_cast<std::size_t>(s)]);
    return fp;
}

// Which slots the solver is allowed to touch for each template.
const std::vector<int>& active_slots(FitTemplate t)
{
    static const std::vector<int> humanoid = [] {
        std::vector<int> v;
        for (int s = FP_HEAD_CX; s <= FP_GEAR_RZ; ++s)
            v.push_back(s);
        return v;
    }();
    static const std::vector<int> blob = {FP_HEAD_RX,        FP_HEAD_RY,
                                          FP_HEAD_RZ,        FP_HEAD_Z,
                                          FP_BLOB_HEM_ROWS,  FP_BLOB_HEM_STEP};
    return t == FitTemplate::Blob ? blob : humanoid;
}

float descend(const VoxelCarveFrames& fr, FitParams& fp, float theta_deg,
              Scratch& sc, int sweeps)
{
    const std::vector<int>& slots = active_slots(fp.tmpl);
    const int steps[6] = {1, -1, 2, -2, 4, -4};
    float best = silhouette_loss(fr, fp, theta_deg, sc, nullptr);
    for (int sweep = 0; sweep < sweeps; ++sweep)
    {
        bool improved = false;
        for (int slot : slots)
        {
            const int base = fp.p[static_cast<std::size_t>(slot)];
            int best_v = base;
            for (int st : steps)
            {
                const int cand = clamp_param(slot, base + st);
                if (cand == base)
                    continue;
                fp.p[static_cast<std::size_t>(slot)] = cand;
                const float l =
                    silhouette_loss(fr, fp, theta_deg, sc, nullptr);
                if (l < best - 1e-5f)
                {
                    best = l;
                    best_v = cand;
                    improved = true;
                }
            }
            fp.p[static_cast<std::size_t>(slot)] = best_v;
        }
        if (!improved)
            break;
    }
    return best;
}

float descend_textured(const VoxelCarveFrames& fr, FitParams& fp,
                       float theta_deg, Scratch& sc, int sweeps)
{
    const std::vector<int>& slots = active_slots(fp.tmpl);
    const int steps[4] = {1, -1, 2, -2};
    VoxelModel tmp;
    float best = textured_loss(fr, fp, theta_deg, sc, tmp);
    for (int sweep = 0; sweep < sweeps; ++sweep)
    {
        bool improved = false;
        for (int slot : slots)
        {
            const int base = fp.p[static_cast<std::size_t>(slot)];
            int best_v = base;
            for (int st : steps)
            {
                const int cand = clamp_param(slot, base + st);
                if (cand == base)
                    continue;
                fp.p[static_cast<std::size_t>(slot)] = cand;
                const float l = textured_loss(fr, fp, theta_deg, sc, tmp);
                if (l < best - 1e-5f)
                {
                    best = l;
                    best_v = cand;
                    improved = true;
                }
            }
            fp.p[static_cast<std::size_t>(slot)] = best_v;
        }
        if (!improved)
            break;
    }
    return best;
}

// --- texturing --------------------------------------------------------------
void texture_body(const Body& b, const VoxelCarveFrames& fr, float theta_deg,
                  VoxelModel& out)
{
    out.w = kW;
    out.d = kD;
    out.z = kZ;
    out.cell = kCell;
    out.cube_faces = true;
    out.theta_deg = theta_deg;
    out.anchor_x = static_cast<float>(fr.w) * 0.5f;
    out.anchor_y = static_cast<float>(fr.h);
    out.occ.assign(kCells, 0u);
    out.index.assign(kCells, 0u);
    out.lit.assign(kCells, 0u);
    out.shade.clear(); // §13: no AO
    for (int packed : b.solid)
    {
        const int i = packed & 0xFF;
        const int j = (packed >> 8) & 0xFF;
        const int k = (packed >> 16) & 0xFF;
        out.occ[idx3(i, j, k)] = 1u;
    }

    // Surface normals from the local occupancy gradient, so "the view that
    // faces this voxel most squarely" is a real question.
    std::vector<float> nx(kCells, 0.0f), ny(kCells, 0.0f), nz(kCells, 0.0f);
    std::vector<unsigned char> surface(kCells, 0u);
    for (int packed : b.solid)
    {
        const int i = packed & 0xFF;
        const int j = (packed >> 8) & 0xFF;
        const int k = (packed >> 16) & 0xFF;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        bool any = false;
        for (int dk = -2; dk <= 2; ++dk)
            for (int dj = -2; dj <= 2; ++dj)
                for (int di = -2; di <= 2; ++di)
                {
                    if (di == 0 && dj == 0 && dk == 0)
                        continue;
                    if (b.at(i + di, j + dj, k + dk))
                        continue;
                    any = true;
                    const float r2 = static_cast<float>(di * di + dj * dj +
                                                        dk * dk);
                    ax += static_cast<float>(di) / r2;
                    ay += static_cast<float>(dj) / r2;
                    az += static_cast<float>(dk) / r2;
                }
        if (!any)
            continue;
        const std::size_t s = idx3(i, j, k);
        surface[s] = 1u;
        const float len = std::sqrt(ax * ax + ay * ay + az * az);
        if (len > 1e-6f)
        {
            nx[s] = ax / len;
            ny[s] = ay / len;
            nz[s] = az / len;
        }
        else
            nz[s] = 1.0f;
    }

    std::vector<float> best_dot(kCells, -2.0f);
    std::vector<unsigned char> painted(kCells, 0u);
    std::vector<float> zbuf;
    std::vector<int> owner;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        if (fr.frame[d] == nullptr)
            continue;
        const View vw = view_for(d, theta_deg);
        render_cells(b, vw, fr.w, fr.h, 1.0f, fr.w, fr.h, zbuf, owner);
        // The direction from a voxel toward this view's camera, in model
        // space: the world "toward camera" vector turned by minus the yaw.
        const float vdx = vw.ct * vw.sa;
        const float vdy = vw.ct * vw.ca;
        const float vdz = vw.st;
        for (std::size_t q = 0; q < owner.size(); ++q)
        {
            if (owner[q] < 0)
                continue;
            const unsigned char c = fr.frame[d][q];
            if (c == 0)
                continue;
            const int packed = owner[q];
            const std::size_t s = idx3(packed & 0xFF, (packed >> 8) & 0xFF,
                                       (packed >> 16) & 0xFF);
            const float dot = nx[s] * vdx + ny[s] * vdy + nz[s] * vdz;
            if (dot <= best_dot[s])
                continue;
            best_dot[s] = dot;
            out.index[s] = c;
            painted[s] = 1u;
        }
    }

    // Anything no view saw takes its nearest painted neighbour.
    std::deque<std::size_t> q;
    for (int packed : b.solid)
    {
        const std::size_t s = idx3(packed & 0xFF, (packed >> 8) & 0xFF,
                                   (packed >> 16) & 0xFF);
        if (painted[s])
            q.push_back(s);
    }
    const unsigned char fallback = q.empty() ? 1u : out.index[q.front()];
    while (!q.empty())
    {
        const std::size_t s = q.front();
        q.pop_front();
        const int k = static_cast<int>(s / (static_cast<std::size_t>(kW) * kD));
        const std::size_t rem = s % (static_cast<std::size_t>(kW) * kD);
        const int j = static_cast<int>(rem / static_cast<std::size_t>(kW));
        const int i = static_cast<int>(rem % static_cast<std::size_t>(kW));
        const int nb[6][3] = {{i - 1, j, k}, {i + 1, j, k}, {i, j - 1, k},
                              {i, j + 1, k}, {i, j, k - 1}, {i, j, k + 1}};
        for (const auto& t : nb)
        {
            if (!b.at(t[0], t[1], t[2]))
                continue;
            const std::size_t u = idx3(t[0], t[1], t[2]);
            if (painted[u])
                continue;
            painted[u] = 1u;
            out.index[u] = out.index[s];
            q.push_back(u);
        }
    }
    for (int packed : b.solid)
    {
        const std::size_t s = idx3(packed & 0xFF, (packed >> 8) & 0xFF,
                                   (packed >> 16) & 0xFF);
        if (!painted[s])
            out.index[s] = fallback;
    }

    // One surface-majority pass to kill singleton speckles. The team band is
    // its own class so a team patch is neither eroded nor grown into.
    const std::vector<unsigned char> src = out.index;
    for (int packed : b.solid)
    {
        const int i = packed & 0xFF;
        const int j = (packed >> 8) & 0xFF;
        const int k = (packed >> 16) & 0xFF;
        const std::size_t s = idx3(i, j, k);
        if (!surface[s])
            continue;
        const unsigned char self = src[s];
        const auto same = [](unsigned char a, unsigned char c) {
            return (a >= 248) ? (c >= 248) : (a == c);
        };
        int self_votes = 0;
        unsigned char rep[8] = {};
        int cnt[8] = {};
        int ncls = 0;
        for (int dk = -1; dk <= 1; ++dk)
            for (int dj = -1; dj <= 1; ++dj)
                for (int di = -1; di <= 1; ++di)
                {
                    if (di == 0 && dj == 0 && dk == 0)
                        continue;
                    if (!b.at(i + di, j + dj, k + dk))
                        continue;
                    const std::size_t t = idx3(i + di, j + dj, k + dk);
                    if (!surface[t])
                        continue;
                    const unsigned char c = src[t];
                    if (same(self, c))
                        ++self_votes;
                    int slot = -1;
                    for (int e = 0; e < ncls; ++e)
                        if (same(rep[e], c))
                            slot = e;
                    if (slot < 0 && ncls < 8)
                    {
                        slot = ncls++;
                        rep[slot] = c;
                    }
                    if (slot >= 0)
                        ++cnt[slot];
                }
        if (self_votes >= 2 || ncls == 0)
            continue;
        int best = 0;
        for (int e = 1; e < ncls; ++e)
            if (cnt[e] > cnt[best])
                best = e;
        if (cnt[best] >= 3)
            out.index[s] = rep[best];
    }

    for (int k = 0; k < kZ; ++k)
        for (int j = 0; j < kD; ++j)
            for (int i = 0; i < kW; ++i)
            {
                const std::size_t s = idx3(i, j, k);
                if (out.occ[s] == 0)
                    continue;
                out.lit[s] = b.at(i, j, k + 1) ? 0u : 1u;
            }
}

} // namespace

void voxel_fit_render(const VoxelModel& body, int facing, float theta_deg,
                      unsigned char* out, int out_w, int out_h, int sprite_w,
                      int sprite_h, float scale)
{
    if (out == nullptr || body.empty())
        return;
    std::memset(out, 0,
                static_cast<std::size_t>(out_w) *
                    static_cast<std::size_t>(out_h));
    const View vw = view_for(facing, theta_deg);
    std::vector<float> zbuf(static_cast<std::size_t>(out_w) *
                                static_cast<std::size_t>(out_h),
                            -std::numeric_limits<float>::infinity());
    for (int k = 0; k < body.z; ++k)
        for (int j = 0; j < body.d; ++j)
            for (int i = 0; i < body.w; ++i)
            {
                const std::size_t s = body.at(i, j, k);
                if (body.occ[s] == 0)
                    continue;
                const Hit hh =
                    project_cell(vw, i, j, k, sprite_w, sprite_h, scale);
                if (hh.u < 0 || hh.v < 0 || hh.u >= out_w || hh.v >= out_h)
                    continue;
                const std::size_t q = static_cast<std::size_t>(hh.v) *
                        static_cast<std::size_t>(out_w) +
                    static_cast<std::size_t>(hh.u);
                if (hh.near <= zbuf[q])
                    continue;
                zbuf[q] = hh.near;
                out[q] = body.index[s];
            }
}

float voxel_fit_loss(const VoxelCarveFrames& frames, const FitParams& params,
                     float theta_deg)
{
    Scratch sc;
    return silhouette_loss(frames, params, theta_deg, sc, nullptr);
}

FitReport voxel_fit_family(const VoxelCarveFrames& frames, float theta_deg)
{
    FitReport rep;
    rep.theta_deg = theta_deg;
    if (frames.w <= 0 || frames.h <= 0)
        return rep;
    const auto t0 = std::chrono::steady_clock::now();
    Scratch sc;

    FitParams best;
    float best_loss = 1e9f;
    // Both templates compete; the lower loss wins, nothing is forced.
    for (int which = 0; which < 2; ++which)
    {
        const FitParams seed = (which == 0) ? initial_humanoid(frames)
                                            : initial_blob(frames);
        Lcg rng{0x9E3779B9u + static_cast<std::uint32_t>(which) * 7919u};
        for (int restart = 0; restart < 6; ++restart)
        {
            FitParams fp = seed;
            if (restart > 0)
            {
                // Jitter every active slot: the seed is a starting point, not
                // a design, and the restarts are there to say so.
                for (int slot : active_slots(fp.tmpl))
                {
                    const Range r = ranges()[static_cast<std::size_t>(slot)];
                    const int span = std::max(1, (r.hi - r.lo) / 6);
                    fp.p[static_cast<std::size_t>(slot)] = clamp_param(
                        slot, fp.p[static_cast<std::size_t>(slot)] +
                                  rng.range(-span, span));
                }
            }
            const float l = descend(frames, fp, theta_deg, sc, 8);
            if (l < best_loss)
            {
                best_loss = l;
                best = fp;
            }
        }
    }

    // Stage 2: re-score with colour and take two more sweeps, which is where
    // "covers the same area" separates from "puts the right pixels there".
    (void)descend_textured(frames, best, theta_deg, sc, 2);

    rep.params = best;
    rep.loss = silhouette_loss(frames, best, theta_deg, sc, rep.iou);
    const auto t1 = std::chrono::steady_clock::now();
    rep.fit_seconds = std::chrono::duration<double>(t1 - t0).count();

    build_body(sc.body, best);
    texture_body(sc.body, frames, theta_deg, rep.model);
    const auto t2 = std::chrono::steady_clock::now();
    rep.texture_seconds = std::chrono::duration<double>(t2 - t1).count();

    // Score the textured result the way the fidelity strip will show it.
    std::vector<unsigned char> shot(
        static_cast<std::size_t>(frames.w) * static_cast<std::size_t>(frames.h));
    float sum_i = 0.0f, sum_a = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        voxel_fit_render(rep.model, d, theta_deg, shot.data(), frames.w,
                         frames.h, frames.w, frames.h, 1.0f);
        int inter = 0, uni = 0, matched = 0;
        for (int i = 0; i < frames.w * frames.h; ++i)
        {
            const unsigned char a = frames.frame[d] != nullptr
                ? frames.frame[d][static_cast<std::size_t>(i)]
                : 0u;
            const unsigned char b = shot[static_cast<std::size_t>(i)];
            if (a != 0 || b != 0)
                ++uni;
            if (a != 0 && b != 0)
            {
                ++inter;
                if (a == b)
                    ++matched;
            }
        }
        rep.iou[d] = uni > 0 ? static_cast<float>(inter) /
                static_cast<float>(uni)
                             : 0.0f;
        rep.agreement[d] = uni > 0 ? static_cast<float>(matched) /
                static_cast<float>(uni)
                                   : 0.0f;
        sum_i += rep.iou[d];
        sum_a += rep.agreement[d];
    }
    rep.mean_iou = sum_i / static_cast<float>(NUM_VOXEL_FACINGS);
    rep.mean_agreement = sum_a / static_cast<float>(NUM_VOXEL_FACINGS);
    for (unsigned char o : rep.model.occ)
        if (o != 0)
            ++rep.voxels;
    rep.surface = rep.voxels;
    return rep;
}

} // namespace og::render
