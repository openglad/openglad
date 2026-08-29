/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Space carver (docs/voxel-render-design.md §10).
//
// The camera model, derived from the art rather than assumed:
//
//   gloader.cpp's animation rows are bit1..bit8 = up, up-right, right,
//   down-right, down, down-left, left, up-left, and walker::animate() indexes
//   them with curdir, whose constants (constants.h FACE_UP = 0 ...) sit in
//   exactly that order. So facing d's frame is row ANI_WALK * 8 + d, and
//   d = FACE_DOWN = 4 is the character looking at the viewer.
//
//   Take model space as x right, y toward the viewer (= world/screen +y,
//   which points DOWN the screen), z up, origin at the footprint centre on
//   the ground. Facing d rotates the model by yaw_d = (d - 4) * 45 degrees
//   about z, using the rasterizer's own matrix [[cos,-sin],[sin,cos]]:
//
//       rx = mx*cos(yaw) - my*sin(yaw)
//       ry = mx*sin(yaw) + my*cos(yaw)
//
//   and the orthographic camera at elevation theta projects
//
//       px   = anchor_x + rx
//       py   = anchor_y + ry*sin(theta) - mz*cos(theta)
//       near = ry*cos(theta) + mz*sin(theta)          // larger = nearer
//
//   At theta = 90 this degenerates to px = rx, py = ry: a pure top view in
//   which screen-up is -y, i.e. away from the viewer. At theta < 90 a taller
//   voxel climbs the screen and a nearer one drops down it, which is what the
//   sprites draw.
//
//   The anchor is ONE sprite-space point shared by all eight frames — that is
//   the whole reason eight silhouettes fuse into one hull. anchor_x only
//   makes sense at the horizontal centre (the art is centred); anchor_y is a
//   pure z translation of the hull (raising anchor_y by delta is the same as
//   lowering every voxel by delta/cos(theta)), so it is fitted, not guessed.
//
// Pipeline: fit the anchor at 1x (cheap) -> supersample the frames and carve
// -> prune stray components and fill cavities -> estimate surface normals and
// recolour from the view that faces each voxel most squarely -> despeckle ->
// bake AO. Everything downstream of the carve exists because a raw visual
// hull renders as a flat-lit blob, and the model is the product.

#include <openglad/interface/render/voxel_carve.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <vector>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
constexpr unsigned char kTeamBandLow = 248;

inline bool is_team_band(unsigned char c)
{
    return c >= kTeamBandLow;
}

struct ViewBasis
{
    float cy = 1.0f; // cos(yaw)
    float sy = 0.0f; // sin(yaw)
};

ViewBasis basis_for(float yaw_rad)
{
    ViewBasis b;
    b.cy = std::cos(yaw_rad);
    b.sy = std::sin(yaw_rad);
    return b;
}

// Grid geometry in FRAME-pixel units: one cell is one (possibly supersampled)
// source pixel, which keeps every projection an identity in scale.
struct CarveGeom
{
    int w = 0, d = 0, z = 0;
    float ax = 0.0f, ay = 0.0f;
    float st = 0.0f, ct = 1.0f; // sin/cos(theta)

    [[nodiscard]] float mx(int i) const
    {
        return static_cast<float>(i) + 0.5f - static_cast<float>(w) * 0.5f;
    }
    [[nodiscard]] float my(int j) const
    {
        return static_cast<float>(j) + 0.5f - static_cast<float>(d) * 0.5f;
    }
    [[nodiscard]] static float mz(int k) { return static_cast<float>(k) + 0.5f; }
};

struct Sample
{
    int px = 0;
    int py = 0;
    float near = 0.0f;
    bool inside = false;
};

inline Sample project_voxel(const CarveGeom& g, const ViewBasis& v, float mx,
                            float my, float mz, int fw, int fh)
{
    const float rx = mx * v.cy - my * v.sy;
    const float ry = mx * v.sy + my * v.cy;
    Sample s;
    s.near = ry * g.ct + mz * g.st;
    const float fx = g.ax + rx;
    const float fy = g.ay + ry * g.st - mz * g.ct;
    s.px = static_cast<int>(std::floor(fx));
    s.py = static_cast<int>(std::floor(fy));
    s.inside = (s.px >= 0 && s.py >= 0 && s.px < fw && s.py < fh);
    return s;
}

void fill_yaws(const VoxelCarveParams& p,
               std::array<float, NUM_VOXEL_FACINGS>& out)
{
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        out[static_cast<std::size_t>(d)] =
            p.custom_yaw ? p.yaw_rad[static_cast<std::size_t>(d)]
                         : voxel_facing_yaw_rad(d);
}

// ---------------------------------------------------------------------------
// Supersampling: nearest-upscale by S, then a 3x3 majority filter on the
// ALPHA MASK. The filter rounds the pixel corners the sprite art is made of;
// a one-source-pixel feature is S pixels wide by then, so it survives.
// ---------------------------------------------------------------------------
struct UpFrames
{
    std::vector<unsigned char> plane[NUM_VOXEL_FACINGS];
    int w = 0;
    int h = 0;
};

void supersample(const VoxelCarveFrames& in, int S, UpFrames& out)
{
    out.w = in.w * S;
    out.h = in.h * S;
    const std::size_t n =
        static_cast<std::size_t>(out.w) * static_cast<std::size_t>(out.h);
    std::vector<unsigned char> up(n, 0u);
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        out.plane[d].assign(n, 0u);
        if (in.frame[d] == nullptr)
            continue;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
                up[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(out.w) +
                   static_cast<std::size_t>(x)] =
                    in.frame[d][static_cast<std::size_t>(y / S) *
                                    static_cast<std::size_t>(in.w) +
                                static_cast<std::size_t>(x / S)];
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x)
            {
                int opaque = 0;
                int tally[9] = {};
                unsigned char cand[9] = {};
                int ncand = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        const int qx = x + dx, qy = y + dy;
                        if (qx < 0 || qy < 0 || qx >= out.w || qy >= out.h)
                            continue;
                        const unsigned char c =
                            up[static_cast<std::size_t>(qy) *
                                   static_cast<std::size_t>(out.w) +
                               static_cast<std::size_t>(qx)];
                        if (c == 0)
                            continue;
                        ++opaque;
                        int slot = -1;
                        for (int t = 0; t < ncand; ++t)
                            if (cand[t] == c)
                                slot = t;
                        if (slot < 0)
                        {
                            slot = ncand++;
                            cand[slot] = c;
                        }
                        ++tally[slot];
                    }
                const std::size_t p = static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(out.w) +
                                      static_cast<std::size_t>(x);
                if (opaque < 5)
                {
                    out.plane[d][p] = 0u;
                    continue;
                }
                if (up[p] != 0)
                {
                    out.plane[d][p] = up[p];
                    continue;
                }
                // Newly opaque: take the commonest colour around it.
                int best = 0;
                for (int t = 1; t < ncand; ++t)
                    if (tally[t] > tally[best])
                        best = t;
                out.plane[d][p] = ncand > 0 ? cand[best] : 0u;
            }
    }
}

// ---------------------------------------------------------------------------
// Carve
// ---------------------------------------------------------------------------
void carve_occupancy(const unsigned char* const* frames, int fw, int fh,
                     const CarveGeom& g,
                     const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                     std::vector<unsigned char>& occ)
{
    const std::size_t n = static_cast<std::size_t>(g.w) *
                          static_cast<std::size_t>(g.d) *
                          static_cast<std::size_t>(g.z);
    occ.assign(n, 1u);
    ViewBasis views[NUM_VOXEL_FACINGS];
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        views[d] = basis_for(yaws[static_cast<std::size_t>(d)]);

    std::size_t idx = 0;
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i, ++idx)
                for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
                {
                    if (frames[d] == nullptr)
                        continue;
                    const Sample s = project_voxel(g, views[d], g.mx(i),
                                                   g.my(j), CarveGeom::mz(k),
                                                   fw, fh);
                    if (!s.inside ||
                        frames[d][static_cast<std::size_t>(s.py) *
                                      static_cast<std::size_t>(fw) +
                                  static_cast<std::size_t>(s.px)] == 0)
                    {
                        occ[idx] = 0;
                        break;
                    }
                }
}

// ---------------------------------------------------------------------------
// Cleanup: opening, stray components, then enclosed cavities.
// ---------------------------------------------------------------------------

// Erode then dilate back, the dilation confined to cells that were solid to
// begin with. Flat surfaces survive exactly; flanges thinner than
// 2*iterations cells do not come back.
int morphological_open(VoxelModel& m, int iterations)
{
    if (iterations <= 0)
        return 0;
    const int W = m.w, D = m.d, Z = m.z;
    const std::vector<unsigned char> original = m.occ;
    std::vector<unsigned char> cur = m.occ;
    std::vector<unsigned char> next(cur.size(), 0u);
    const auto solid_in = [&](const std::vector<unsigned char>& v, int i, int j,
                              int k) {
        if (i < 0 || j < 0 || k < 0 || i >= W || j >= D || k >= Z)
            return false;
        return v[m.at(i, j, k)] != 0;
    };
    for (int it = 0; it < iterations; ++it)
    {
        std::fill(next.begin(), next.end(), 0u);
        for (int k = 0; k < Z; ++k)
            for (int j = 0; j < D; ++j)
                for (int i = 0; i < W; ++i)
                {
                    const std::size_t s = m.at(i, j, k);
                    if (cur[s] == 0)
                        continue;
                    if (solid_in(cur, i - 1, j, k) && solid_in(cur, i + 1, j, k) &&
                        solid_in(cur, i, j - 1, k) && solid_in(cur, i, j + 1, k) &&
                        solid_in(cur, i, j, k - 1) && solid_in(cur, i, j, k + 1))
                        next[s] = 1u;
                }
        cur.swap(next);
    }
    for (int it = 0; it < iterations; ++it)
    {
        next = cur;
        for (int k = 0; k < Z; ++k)
            for (int j = 0; j < D; ++j)
                for (int i = 0; i < W; ++i)
                {
                    const std::size_t s = m.at(i, j, k);
                    if (original[s] == 0 || cur[s] != 0)
                        continue;
                    if (solid_in(cur, i - 1, j, k) || solid_in(cur, i + 1, j, k) ||
                        solid_in(cur, i, j - 1, k) || solid_in(cur, i, j + 1, k) ||
                        solid_in(cur, i, j, k - 1) || solid_in(cur, i, j, k + 1))
                        next[s] = 1u;
                }
        cur.swap(next);
    }
    int removed = 0;
    for (std::size_t s = 0; s < cur.size(); ++s)
        if (original[s] != 0 && cur[s] == 0)
            ++removed;
    m.occ.swap(cur);
    return removed;
}
int prune_components(VoxelModel& m, float keep_fraction)
{
    const int W = m.w, D = m.d, Z = m.z;
    const std::size_t n = m.occ.size();
    std::vector<int> label(n, -1);
    std::vector<int> sizes;
    std::deque<std::size_t> q;
    int total_solid = 0;
    for (unsigned char o : m.occ)
        if (o != 0)
            ++total_solid;

    for (int k = 0; k < Z; ++k)
        for (int j = 0; j < D; ++j)
            for (int i = 0; i < W; ++i)
            {
                const std::size_t s = m.at(i, j, k);
                if (m.occ[s] == 0 || label[s] >= 0)
                    continue;
                const int id = static_cast<int>(sizes.size());
                sizes.push_back(0);
                label[s] = id;
                q.push_back(s);
                while (!q.empty())
                {
                    const std::size_t c = q.front();
                    q.pop_front();
                    ++sizes[static_cast<std::size_t>(id)];
                    const int ck = static_cast<int>(
                        c / (static_cast<std::size_t>(W) *
                             static_cast<std::size_t>(D)));
                    const std::size_t rem =
                        c % (static_cast<std::size_t>(W) *
                             static_cast<std::size_t>(D));
                    const int cj =
                        static_cast<int>(rem / static_cast<std::size_t>(W));
                    const int ci =
                        static_cast<int>(rem % static_cast<std::size_t>(W));
                    for (int dk = -1; dk <= 1; ++dk)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                            {
                                if (di == 0 && dj == 0 && dk == 0)
                                    continue;
                                const int qi = ci + di, qj = cj + dj,
                                          qk = ck + dk;
                                if (qi < 0 || qj < 0 || qk < 0 || qi >= W ||
                                    qj >= D || qk >= Z)
                                    continue;
                                const std::size_t t = m.at(qi, qj, qk);
                                if (m.occ[t] == 0 || label[t] >= 0)
                                    continue;
                                label[t] = id;
                                q.push_back(t);
                            }
                }
            }
    if (sizes.empty())
        return 0;
    int biggest = 0;
    for (std::size_t i = 1; i < sizes.size(); ++i)
        if (sizes[i] > sizes[static_cast<std::size_t>(biggest)])
            biggest = static_cast<int>(i);
    const int floor_size = static_cast<int>(
        keep_fraction * static_cast<float>(total_solid));
    int dropped = 0;
    for (std::size_t s = 0; s < n; ++s)
    {
        if (m.occ[s] == 0)
            continue;
        const int id = label[s];
        if (id == biggest)
            continue;
        if (sizes[static_cast<std::size_t>(id)] >= floor_size &&
            floor_size > 0)
            continue;
        m.occ[s] = 0;
        ++dropped;
    }
    return dropped;
}

int fill_cavities(VoxelModel& m)
{
    const int W = m.w, D = m.d, Z = m.z;
    const std::size_t n = m.occ.size();
    std::vector<unsigned char> outside(n, 0u);
    std::deque<std::size_t> q;
    const auto seed = [&](int i, int j, int k) {
        const std::size_t s = m.at(i, j, k);
        if (m.occ[s] != 0 || outside[s])
            return;
        outside[s] = 1u;
        q.push_back(s);
    };
    for (int k = 0; k < Z; ++k)
        for (int j = 0; j < D; ++j)
        {
            seed(0, j, k);
            seed(W - 1, j, k);
        }
    for (int k = 0; k < Z; ++k)
        for (int i = 0; i < W; ++i)
        {
            seed(i, 0, k);
            seed(i, D - 1, k);
        }
    for (int j = 0; j < D; ++j)
        for (int i = 0; i < W; ++i)
        {
            seed(i, j, 0);
            seed(i, j, Z - 1);
        }
    while (!q.empty())
    {
        const std::size_t c = q.front();
        q.pop_front();
        const int ck = static_cast<int>(
            c / (static_cast<std::size_t>(W) * static_cast<std::size_t>(D)));
        const std::size_t rem =
            c % (static_cast<std::size_t>(W) * static_cast<std::size_t>(D));
        const int cj = static_cast<int>(rem / static_cast<std::size_t>(W));
        const int ci = static_cast<int>(rem % static_cast<std::size_t>(W));
        const int nb[6][3] = {{ci - 1, cj, ck}, {ci + 1, cj, ck},
                              {ci, cj - 1, ck}, {ci, cj + 1, ck},
                              {ci, cj, ck - 1}, {ci, cj, ck + 1}};
        for (const auto& p : nb)
        {
            if (p[0] < 0 || p[1] < 0 || p[2] < 0 || p[0] >= W || p[1] >= D ||
                p[2] >= Z)
                continue;
            const std::size_t t = m.at(p[0], p[1], p[2]);
            if (m.occ[t] != 0 || outside[t])
                continue;
            outside[t] = 1u;
            q.push_back(t);
        }
    }
    int filled = 0;
    for (std::size_t s = 0; s < n; ++s)
        if (m.occ[s] == 0 && !outside[s])
        {
            m.occ[s] = 1u;
            ++filled;
        }
    return filled;
}

// ---------------------------------------------------------------------------
// Photo-consistency carving (voxel colouring).
//
// The eight silhouettes of a 16x16 top-down sprite are nearly the same shape,
// so their intersection is a solid of revolution and the figure comes out a
// mushroom. Colour is the one signal that distinguishes the facings, so this
// pass keeps only voxels the views that SEE them agree about: recompute
// visibility, ask each seeing view what colour it reads there, and carve the
// voxel when the views disagree. Removing a shell exposes a new one, so it
// iterates.
// ---------------------------------------------------------------------------
bool same_ramp(unsigned char a, unsigned char b, int tol)
{
    if (is_team_band(a) || is_team_band(b))
        return is_team_band(a) && is_team_band(b);
    const int d = static_cast<int>(a) - static_cast<int>(b);
    return (d < 0 ? -d : d) <= tol;
}

int photo_consistency_carve(VoxelModel& m, const unsigned char* const* frames,
                            int fw, int fh, const CarveGeom& g,
                            const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                            const VoxelCarveParams& p, bool& rolled_back)
{
    rolled_back = false;
    if (p.photo_passes <= 0)
        return 0;
    const std::vector<unsigned char> before = m.occ;
    int solid_before = 0;
    for (unsigned char o : before)
        if (o != 0)
            ++solid_before;
    if (solid_before == 0)
        return 0;

    const std::size_t n = m.occ.size();
    const std::size_t frame_px =
        static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh);
    std::vector<float> zbuf(frame_px);
    std::vector<std::size_t> owner(frame_px);
    std::vector<std::array<unsigned char, NUM_VOXEL_FACINGS>> seen_idx(n);
    std::vector<unsigned char> seen_count(n);
    int removed_total = 0;

    for (int pass = 0; pass < p.photo_passes; ++pass)
    {
        std::fill(seen_count.begin(), seen_count.end(), 0u);
        for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        {
            if (frames[d] == nullptr)
                continue;
            const ViewBasis vb = basis_for(yaws[static_cast<std::size_t>(d)]);
            std::fill(zbuf.begin(), zbuf.end(),
                      -std::numeric_limits<float>::infinity());
            std::fill(owner.begin(), owner.end(),
                      static_cast<std::size_t>(-1));
            for (int k = 0; k < m.z; ++k)
                for (int j = 0; j < m.d; ++j)
                    for (int i = 0; i < m.w; ++i)
                    {
                        const std::size_t s = m.at(i, j, k);
                        if (m.occ[s] == 0)
                            continue;
                        const Sample sm = project_voxel(
                            g, vb, g.mx(i), g.my(j), CarveGeom::mz(k), fw, fh);
                        if (!sm.inside)
                            continue;
                        const std::size_t q =
                            static_cast<std::size_t>(sm.py) *
                                static_cast<std::size_t>(fw) +
                            static_cast<std::size_t>(sm.px);
                        if (sm.near <= zbuf[q])
                            continue;
                        zbuf[q] = sm.near;
                        owner[q] = s;
                    }
            for (std::size_t q = 0; q < frame_px; ++q)
            {
                if (owner[q] == static_cast<std::size_t>(-1))
                    continue;
                const unsigned char c = frames[d][q];
                if (c == 0)
                    continue;
                const std::size_t s = owner[q];
                if (seen_count[s] >= NUM_VOXEL_FACINGS)
                    continue;
                seen_idx[s][seen_count[s]] = c;
                ++seen_count[s];
            }
        }

        int removed = 0;
        std::vector<unsigned char> next = m.occ;
        for (std::size_t s = 0; s < n; ++s)
        {
            if (m.occ[s] == 0)
                continue;
            const int seen = static_cast<int>(seen_count[s]);
            if (seen < p.photo_min_views)
                continue; // interior, or barely observed: leave it alone
            int best = 0;
            for (int a = 0; a < seen; ++a)
            {
                int votes = 0;
                for (int b = 0; b < seen; ++b)
                    if (same_ramp(seen_idx[s][static_cast<std::size_t>(a)],
                                  seen_idx[s][static_cast<std::size_t>(b)],
                                  p.ramp_tolerance))
                        ++votes;
                best = std::max(best, votes);
            }
            const float agree =
                static_cast<float>(best) / static_cast<float>(seen);
            if (agree < p.photo_agreement)
            {
                next[s] = 0u;
                ++removed;
            }
        }
        if (removed == 0)
            break;
        m.occ.swap(next);
        removed_total += removed;
        if (static_cast<float>(removed_total) >
            p.photo_max_loss * static_cast<float>(solid_before))
        {
            m.occ = before;
            rolled_back = true;
            return 0;
        }
    }
    return removed_total;
}

// ---------------------------------------------------------------------------
// Normals from the local occupancy gradient over the 5^3 neighbourhood.
// ---------------------------------------------------------------------------
void estimate_normals(const VoxelModel& m, std::vector<float>& nx,
                      std::vector<float>& ny, std::vector<float>& nz,
                      std::vector<unsigned char>& surface)
{
    const std::size_t n = m.occ.size();
    nx.assign(n, 0.0f);
    ny.assign(n, 0.0f);
    nz.assign(n, 1.0f);
    surface.assign(n, 0u);
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                const std::size_t s = m.at(i, j, k);
                if (m.occ[s] == 0)
                    continue;
                bool any_empty = false;
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                for (int dk = -2; dk <= 2 && true; ++dk)
                    for (int dj = -2; dj <= 2; ++dj)
                        for (int di = -2; di <= 2; ++di)
                        {
                            if (di == 0 && dj == 0 && dk == 0)
                                continue;
                            if (m.solid(i + di, j + dj, k + dk))
                                continue;
                            any_empty = true;
                            const float r2 = static_cast<float>(
                                di * di + dj * dj + dk * dk);
                            const float wgt = 1.0f / r2;
                            ax += static_cast<float>(di) * wgt;
                            ay += static_cast<float>(dj) * wgt;
                            az += static_cast<float>(dk) * wgt;
                        }
                if (!any_empty)
                    continue;
                surface[s] = 1u;
                const float len = std::sqrt(ax * ax + ay * ay + az * az);
                if (len < 1e-6f)
                {
                    nx[s] = 0.0f;
                    ny[s] = 0.0f;
                    nz[s] = 1.0f;
                    continue;
                }
                nx[s] = ax / len;
                ny[s] = ay / len;
                nz[s] = az / len;
            }
}

// ---------------------------------------------------------------------------
// Colour: among the views that SEE a voxel, the one facing it most squarely
// wins. Nearest depth is only the tiebreak. Grazing views were the round-1
// smear — a cape colour landing on a chest because that view happened to be
// nearest along the ray.
// ---------------------------------------------------------------------------
void colour_from_views(VoxelModel& m, const unsigned char* const* frames,
                       int fw, int fh, const CarveGeom& g,
                       const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                       const std::vector<float>& nx,
                       const std::vector<float>& ny,
                       const std::vector<float>& nz,
                       std::vector<unsigned char>& coloured)
{
    const std::size_t n = m.occ.size();
    m.index.assign(n, 0u);
    coloured.assign(n, 0u);
    std::vector<float> best_dot(n, -2.0f);
    std::vector<float> best_near(n, -std::numeric_limits<float>::infinity());

    const std::size_t frame_px =
        static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh);
    std::vector<float> zbuf(frame_px);
    std::vector<std::size_t> owner(frame_px);

    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        if (frames[d] == nullptr)
            continue;
        const ViewBasis vb = basis_for(yaws[static_cast<std::size_t>(d)]);
        // The direction from a voxel toward the camera, expressed in MODEL
        // space for this facing: the world "toward camera" vector
        // (0, cos(theta), sin(theta)) turned by -yaw_d.
        const float vdx = g.ct * vb.sy;
        const float vdy = g.ct * vb.cy;
        const float vdz = g.st;

        std::fill(zbuf.begin(), zbuf.end(),
                  -std::numeric_limits<float>::infinity());
        std::fill(owner.begin(), owner.end(), static_cast<std::size_t>(-1));
        for (int k = 0; k < m.z; ++k)
            for (int j = 0; j < m.d; ++j)
                for (int i = 0; i < m.w; ++i)
                {
                    const std::size_t s = m.at(i, j, k);
                    if (m.occ[s] == 0)
                        continue;
                    const Sample sm = project_voxel(g, vb, g.mx(i), g.my(j),
                                                    CarveGeom::mz(k), fw, fh);
                    if (!sm.inside)
                        continue;
                    const std::size_t p = static_cast<std::size_t>(sm.py) *
                                              static_cast<std::size_t>(fw) +
                                          static_cast<std::size_t>(sm.px);
                    if (sm.near <= zbuf[p])
                        continue;
                    zbuf[p] = sm.near;
                    owner[p] = s;
                }
        for (std::size_t p = 0; p < frame_px; ++p)
        {
            if (owner[p] == static_cast<std::size_t>(-1))
                continue;
            const std::size_t s = owner[p];
            const unsigned char c = frames[d][p];
            if (c == 0)
                continue;
            const float dot = nx[s] * vdx + ny[s] * vdy + nz[s] * vdz;
            if (dot > best_dot[s] + 1e-4f ||
                (dot > best_dot[s] - 1e-4f && zbuf[p] > best_near[s]))
            {
                best_dot[s] = std::max(best_dot[s], dot);
                best_near[s] = zbuf[p];
                m.index[s] = c;
                coloured[s] = 1u;
            }
        }
    }
}

// Interior voxels no view ever owned take the nearest coloured neighbour.
void flood_interior_colour(VoxelModel& m, std::vector<unsigned char>& coloured)
{
    std::deque<std::size_t> q;
    for (std::size_t s = 0; s < m.occ.size(); ++s)
        if (m.occ[s] != 0 && coloured[s])
            q.push_back(s);
    unsigned char fallback = q.empty() ? 1u : m.index[q.front()];
    while (!q.empty())
    {
        const std::size_t c = q.front();
        q.pop_front();
        const int ck = static_cast<int>(
            c / (static_cast<std::size_t>(m.w) * static_cast<std::size_t>(m.d)));
        const std::size_t rem =
            c % (static_cast<std::size_t>(m.w) * static_cast<std::size_t>(m.d));
        const int cj = static_cast<int>(rem / static_cast<std::size_t>(m.w));
        const int ci = static_cast<int>(rem % static_cast<std::size_t>(m.w));
        const int nb[6][3] = {{ci - 1, cj, ck}, {ci + 1, cj, ck},
                              {ci, cj - 1, ck}, {ci, cj + 1, ck},
                              {ci, cj, ck - 1}, {ci, cj, ck + 1}};
        for (const auto& p : nb)
        {
            if (p[0] < 0 || p[1] < 0 || p[2] < 0 || p[0] >= m.w ||
                p[1] >= m.d || p[2] >= m.z)
                continue;
            const std::size_t t = m.at(p[0], p[1], p[2]);
            if (m.occ[t] == 0 || coloured[t])
                continue;
            coloured[t] = 1u;
            m.index[t] = m.index[c];
            q.push_back(t);
        }
    }
    for (std::size_t s = 0; s < m.occ.size(); ++s)
        if (m.occ[s] != 0 && !coloured[s])
            m.index[s] = fallback;
}

// One pass of surface-neighbour majority, to kill singleton speckles. The
// team band is treated as a single class so a team-coloured patch is neither
// eroded nor split by it.
int despeckle(VoxelModel& m, const std::vector<unsigned char>& surface)
{
    const std::vector<unsigned char> src = m.index;
    int changed = 0;
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                const std::size_t s = m.at(i, j, k);
                if (m.occ[s] == 0 || !surface[s])
                    continue;
                const unsigned char self = src[s];
                int cls[9] = {};
                unsigned char rep[9] = {};
                int ncls = 0;
                int self_votes = 0;
                for (int dk = -1; dk <= 1; ++dk)
                    for (int dj = -1; dj <= 1; ++dj)
                        for (int di = -1; di <= 1; ++di)
                        {
                            if (di == 0 && dj == 0 && dk == 0)
                                continue;
                            if (!m.solid(i + di, j + dj, k + dk))
                                continue;
                            const std::size_t t = m.at(i + di, j + dj, k + dk);
                            if (!surface[t])
                                continue;
                            const unsigned char c = src[t];
                            const bool same_class =
                                is_team_band(c) ? is_team_band(self) : c == self;
                            if (same_class)
                                ++self_votes;
                            int slot = -1;
                            for (int q = 0; q < ncls; ++q)
                            {
                                const bool match = is_team_band(c)
                                    ? is_team_band(rep[q])
                                    : rep[q] == c;
                                if (match)
                                    slot = q;
                            }
                            if (slot < 0 && ncls < 9)
                            {
                                slot = ncls++;
                                rep[slot] = c;
                            }
                            if (slot >= 0)
                                ++cls[slot];
                        }
                if (self_votes >= 3 || ncls == 0)
                    continue; // already part of a patch
                int best = 0;
                for (int q = 1; q < ncls; ++q)
                    if (cls[q] > cls[best])
                        best = q;
                if (cls[best] < self_votes + 2)
                    continue;
                if (rep[best] == self)
                    continue;
                m.index[s] = rep[best];
                ++changed;
            }
    return changed;
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

// Agreement of a reprojection against a frame: matched palette indices over
// the union of opaque pixels. Over-covering costs as much as under-covering.
void score_view(const VoxelModel& m, int dir,
                const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                const VoxelCarveFrames& fr,
                std::vector<unsigned char>& scratch, float& agreement,
                float& iou)
{
    scratch.assign(
        static_cast<std::size_t>(fr.w) * static_cast<std::size_t>(fr.h), 0u);
    voxel_model_reproject(m, yaws[static_cast<std::size_t>(dir)], m.theta_deg,
                          scratch.data(), fr.w, fr.h, m.anchor_x, m.anchor_y,
                          1.0f);
    int matched = 0, both = 0, uni = 0;
    const unsigned char* src = fr.frame[dir];
    for (std::size_t p = 0; p < scratch.size(); ++p)
    {
        const bool a = src != nullptr && src[p] != 0;
        const bool b = scratch[p] != 0;
        if (a || b)
            ++uni;
        if (a && b)
        {
            ++both;
            if (src[p] == scratch[p])
                ++matched;
        }
    }
    agreement = uni > 0 ? static_cast<float>(matched) / static_cast<float>(uni)
                        : 0.0f;
    iou = uni > 0 ? static_cast<float>(both) / static_cast<float>(uni) : 0.0f;
}

// Drop the hull onto z = 0, trim the empty top, and fold the shift back into
// the anchor so a reprojection still lands on the source frame.
void normalize_base(VoxelModel& m, float ct)
{
    int lowest = m.z, highest = -1;
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
            lowest = std::min(lowest, k);
            highest = k;
        }
    }
    if (highest < 0 || (lowest == 0 && highest == m.z - 1))
        return;
    VoxelModel out;
    out.w = m.w;
    out.d = m.d;
    out.z = highest - lowest + 1;
    out.cell = m.cell;
    out.cube_faces = m.cube_faces;
    const std::size_t n = static_cast<std::size_t>(out.w) *
                          static_cast<std::size_t>(out.d) *
                          static_cast<std::size_t>(out.z);
    out.occ.assign(n, 0u);
    out.index.assign(n, 0u);
    const bool has_shade = !m.shade.empty();
    if (has_shade)
        out.shade.assign(n, 255u);
    for (int k = 0; k < out.z; ++k)
        for (int j = 0; j < out.d; ++j)
            for (int i = 0; i < out.w; ++i)
            {
                const std::size_t s = m.at(i, j, k + lowest);
                const std::size_t t = out.at(i, j, k);
                out.occ[t] = m.occ[s];
                out.index[t] = m.index[s];
                if (has_shade)
                    out.shade[t] = m.shade[s];
            }
    out.anchor_x = m.anchor_x;
    // py = ay + ry*sin - mz*cos: lowering every voxel by `lowest` cells adds
    // lowest*cos to py, so the anchor drops by the same amount.
    out.anchor_y = m.anchor_y - static_cast<float>(lowest) * ct;
    out.theta_deg = m.theta_deg;
    recompute_lit(out);
    m = std::move(out);
}

} // namespace

void voxel_model_reproject(const VoxelModel& model, float yaw_rad,
                           float theta_deg, unsigned char* out, int out_w,
                           int out_h, float anchor_x, float anchor_y,
                           float scale)
{
    if (out == nullptr || out_w <= 0 || out_h <= 0 || model.empty())
        return;
    std::memset(out, 0,
                static_cast<std::size_t>(out_w) *
                    static_cast<std::size_t>(out_h));
    const float st = std::sin(theta_deg * kDeg2Rad);
    const float ct = std::cos(theta_deg * kDeg2Rad);
    const ViewBasis v = basis_for(yaw_rad);
    const float c = model.cell;
    const float hw = model.extent_x() * 0.5f;
    const float hd = model.extent_y() * 0.5f;

    std::vector<float> zbuf(static_cast<std::size_t>(out_w) *
                                static_cast<std::size_t>(out_h),
                            -std::numeric_limits<float>::infinity());
    for (int k = 0; k < model.z; ++k)
        for (int j = 0; j < model.d; ++j)
            for (int i = 0; i < model.w; ++i)
            {
                const std::size_t s = model.at(i, j, k);
                if (model.occ[s] == 0)
                    continue;
                const float mx = (static_cast<float>(i) + 0.5f) * c - hw;
                const float my = (static_cast<float>(j) + 0.5f) * c - hd;
                const float mz = (static_cast<float>(k) + 0.5f) * c;
                const float rx = mx * v.cy - my * v.sy;
                const float ry = mx * v.sy + my * v.cy;
                const float near = ry * ct + mz * st;
                const int px = static_cast<int>(
                    std::floor(anchor_x + rx * scale));
                const int py = static_cast<int>(std::floor(
                    anchor_y + (ry * st - mz * ct) * scale));
                if (px < 0 || py < 0 || px >= out_w || py >= out_h)
                    continue;
                const std::size_t p = static_cast<std::size_t>(py) *
                                          static_cast<std::size_t>(out_w) +
                                      static_cast<std::size_t>(px);
                if (near <= zbuf[p])
                    continue;
                zbuf[p] = near;
                out[p] = model.index[s];
            }
}

void voxel_model_bake_ao(VoxelModel& model, float reference)
{
    const std::size_t n = model.occ.size();
    model.shade.assign(n, 255u);
    if (reference <= 0.0f)
        return;
    constexpr int kR = 2;
    constexpr float kCells =
        static_cast<float>((2 * kR + 1) * (2 * kR + 1) * (2 * kR + 1) - 1);
    for (int k = 0; k < model.z; ++k)
        for (int j = 0; j < model.d; ++j)
            for (int i = 0; i < model.w; ++i)
            {
                const std::size_t s = model.at(i, j, k);
                if (model.occ[s] == 0)
                    continue;
                int empty = 0;
                for (int dk = -kR; dk <= kR; ++dk)
                    for (int dj = -kR; dj <= kR; ++dj)
                        for (int di = -kR; di <= kR; ++di)
                        {
                            if (di == 0 && dj == 0 && dk == 0)
                                continue;
                            if (!model.solid(i + di, j + dj, k + dk))
                                ++empty;
                        }
                // A flat face sits at half-empty, so dividing by `reference`
                // (0.5) leaves flat surfaces open and darkens only creases.
                const float open =
                    std::min(1.0f, (static_cast<float>(empty) / kCells) /
                                       reference);
                model.shade[s] = static_cast<unsigned char>(
                    std::lround(open * 255.0f));
            }
}

VoxelModel voxel_model_downsample(const VoxelModel& src, int factor)
{
    VoxelModel out;
    if (src.empty() || factor <= 1)
        return src;
    out.w = (src.w + factor - 1) / factor;
    out.d = (src.d + factor - 1) / factor;
    out.z = (src.z + factor - 1) / factor;
    out.cell = src.cell * static_cast<float>(factor);
    out.anchor_x = src.anchor_x;
    out.anchor_y = src.anchor_y;
    out.theta_deg = src.theta_deg;
    out.cube_faces = src.cube_faces;
    const std::size_t n = static_cast<std::size_t>(out.w) *
                          static_cast<std::size_t>(out.d) *
                          static_cast<std::size_t>(out.z);
    out.occ.assign(n, 0u);
    out.index.assign(n, 0u);
    const int block = factor * factor * factor;
    for (int k = 0; k < out.z; ++k)
        for (int j = 0; j < out.d; ++j)
            for (int i = 0; i < out.w; ++i)
            {
                int solid = 0;
                unsigned char cand[32] = {};
                int tally[32] = {};
                int ncand = 0;
                for (int dk = 0; dk < factor; ++dk)
                    for (int dj = 0; dj < factor; ++dj)
                        for (int di = 0; di < factor; ++di)
                        {
                            const int si = i * factor + di;
                            const int sj = j * factor + dj;
                            const int sk = k * factor + dk;
                            if (!src.solid(si, sj, sk))
                                continue;
                            ++solid;
                            const unsigned char c =
                                src.index[src.at(si, sj, sk)];
                            int slot = -1;
                            for (int q = 0; q < ncand; ++q)
                                if (cand[q] == c)
                                    slot = q;
                            if (slot < 0 && ncand < 32)
                            {
                                slot = ncand++;
                                cand[slot] = c;
                            }
                            if (slot >= 0)
                                ++tally[slot];
                        }
                if (solid * 2 <= block)
                    continue;
                int best = 0;
                for (int q = 1; q < ncand; ++q)
                    if (tally[q] > tally[best])
                        best = q;
                const std::size_t t = out.at(i, j, k);
                out.occ[t] = 1u;
                out.index[t] = ncand > 0 ? cand[best] : 0u;
            }
    recompute_lit(out);
    voxel_model_bake_ao(out, 0.5f);
    return out;
}

VoxelCarveReport voxel_carve(const VoxelCarveFrames& frames,
                             const VoxelCarveParams& params)
{
    VoxelCarveReport rep;
    if (frames.w <= 0 || frames.h <= 0)
        return rep;

    std::array<float, NUM_VOXEL_FACINGS> yaws{};
    fill_yaws(params, yaws);

    const float st = std::sin(params.theta_deg * kDeg2Rad);
    const float ct = std::cos(params.theta_deg * kDeg2Rad);
    const int S = std::max(1, params.supersample);

    // ---- 1. fit the anchor at 1x -----------------------------------------
    // anchor_y is a pure z translation of the hull, so the fit is really
    // "where in the grid does the hull sit without being clipped"; anchor_x
    // is NOT a rigid motion (it shifts x in the up/down views and y in the
    // left/right ones), so it stays near the art's centre and only jitters
    // half a pixel. Coarse pass on anchor_y, then a refinement.
    const auto fit_t0 = std::chrono::steady_clock::now();
    CarveGeom fit;
    fit.w = frames.w;
    fit.d = frames.w;
    fit.z = std::min(kVoxelCarveMaxZ,
                     std::max(1, static_cast<int>(std::ceil(
                                     static_cast<float>(frames.h) /
                                     std::max(1e-3f, st)))));
    fit.st = st;
    fit.ct = ct;

    std::vector<unsigned char> scratch;
    float best_score = -1.0f;
    float best_ax = static_cast<float>(frames.w) * 0.5f;
    float best_ay = static_cast<float>(frames.h);

    const auto try_anchor = [&](float ax, float ay) {
        CarveGeom trial = fit;
        trial.ax = ax;
        trial.ay = ay;
        VoxelModel m;
        m.w = trial.w;
        m.d = trial.d;
        m.z = trial.z;
        m.cell = 1.0f;
        m.anchor_x = ax;
        m.anchor_y = ay;
        m.theta_deg = params.theta_deg;
        carve_occupancy(frames.frame, frames.w, frames.h, trial, yaws, m.occ);
        m.index.assign(m.occ.size(), 0u);
        // The fit only needs a silhouette, so colour every survivor with a
        // constant and score coverage; the real colouring is far downstream.
        for (std::size_t s = 0; s < m.occ.size(); ++s)
            m.index[s] = m.occ[s];
        float sum = 0.0f;
        for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        {
            float a = 0.0f, i = 0.0f;
            score_view(m, d, yaws, frames, scratch, a, i);
            sum += i; // silhouette overlap: colour is not decided yet
        }
        const float score = sum / static_cast<float>(NUM_VOXEL_FACINGS);
        if (score > best_score)
        {
            best_score = score;
            best_ax = ax;
            best_ay = ay;
        }
    };

    const float cx_default = static_cast<float>(frames.w) * 0.5f;
    if (params.anchor_x >= 0.0f && params.anchor_y >= 0.0f)
    {
        best_ax = params.anchor_x;
        best_ay = params.anchor_y;
    }
    else
    {
        const float fixed_ax =
            params.anchor_x >= 0.0f ? params.anchor_x : cx_default;
        const float lo = static_cast<float>(frames.h) * 0.4f;
        const float hi = static_cast<float>(frames.h) * 1.7f;
        for (float ay = lo; ay <= hi + 1e-3f; ay += 1.0f)
            try_anchor(fixed_ax, ay);
        const float centre = best_ay;
        for (int sx = -1; sx <= 1; ++sx)
        {
            const float ax = params.anchor_x >= 0.0f
                ? params.anchor_x
                : cx_default + static_cast<float>(sx) * 0.5f;
            for (int sy = -4; sy <= 4; ++sy)
                try_anchor(ax, centre + static_cast<float>(sy) * 0.25f);
        }
    }
    const auto fit_t1 = std::chrono::steady_clock::now();
    rep.fit_seconds = std::chrono::duration<double>(fit_t1 - fit_t0).count();

    // ---- 2. supersample and carve ----------------------------------------
    const auto t0 = std::chrono::steady_clock::now();
    UpFrames up;
    supersample(frames, S, up);
    const unsigned char* planes[NUM_VOXEL_FACINGS] = {};
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        planes[d] = frames.frame[d] != nullptr ? up.plane[d].data() : nullptr;

    CarveGeom g;
    g.w = params.grid_w > 0 ? params.grid_w : up.w;
    g.d = params.grid_d > 0 ? params.grid_d : up.w;
    g.z = params.grid_z > 0
        ? params.grid_z
        : std::min(kVoxelCarveMaxZ * S,
                   std::max(1, static_cast<int>(std::ceil(
                                   static_cast<float>(up.h) /
                                   std::max(1e-3f, st)))));
    g.st = st;
    g.ct = ct;
    g.ax = best_ax * static_cast<float>(S);
    g.ay = best_ay * static_cast<float>(S);

    VoxelModel& m = rep.model;
    m.w = g.w;
    m.d = g.d;
    m.z = g.z;
    m.cell = 1.0f / static_cast<float>(S);
    m.theta_deg = params.theta_deg;
    m.cube_faces = true;
    carve_occupancy(planes, up.w, up.h, g, yaws, m.occ);

    // ---- 3. cleanup -------------------------------------------------------
    rep.opened_away = morphological_open(m, params.open_iterations);
    rep.photo_carved = photo_consistency_carve(m, planes, up.w, up.h, g, yaws,
                                               params, rep.photo_rolled_back);
    rep.components_dropped = prune_components(m, params.component_keep_fraction);
    if (params.fill_cavities)
        rep.cavity_voxels_filled = fill_cavities(m);

    // ---- 4. normals, colour, despeckle -----------------------------------
    std::vector<float> nx, ny, nz;
    std::vector<unsigned char> surface;
    estimate_normals(m, nx, ny, nz, surface);
    std::vector<unsigned char> coloured;
    colour_from_views(m, planes, up.w, up.h, g, yaws, nx, ny, nz, coloured);
    flood_interior_colour(m, coloured);
    for (int pass = 0; pass < params.despeckle_passes; ++pass)
        rep.despeckled += despeckle(m, surface);

    // ---- 5. bake AO, settle on the ground --------------------------------
    voxel_model_bake_ao(m, params.ao_reference);
    recompute_lit(m);
    // Everything above ran in frame-pixel cells; the model's anchor is in
    // world (sprite) pixels.
    m.anchor_x = g.ax;
    m.anchor_y = g.ay;
    if (params.normalize_base)
        normalize_base(m, ct);
    m.anchor_x /= static_cast<float>(S);
    m.anchor_y /= static_cast<float>(S);

    const auto t1 = std::chrono::steady_clock::now();
    rep.carve_seconds = std::chrono::duration<double>(t1 - t0).count();

    for (std::size_t s = 0; s < m.occ.size(); ++s)
        if (m.occ[s] != 0)
            ++rep.voxel_count;
    for (unsigned char f : surface)
        if (f)
            ++rep.surface_voxels;

    float sum_a = 0.0f, sum_i = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        float iou = 0.0f;
        score_view(m, d, yaws, frames, scratch, rep.fit_agreement[d], iou);
        sum_a += rep.fit_agreement[d];
        sum_i += iou;
    }
    rep.fit_agreement_mean = sum_a / static_cast<float>(NUM_VOXEL_FACINGS);
    rep.fit_iou_mean = sum_i / static_cast<float>(NUM_VOXEL_FACINGS);
    return rep;
}

VoxelModel voxel_build_wall_model(const unsigned char* top, int tw, int th,
                                  const unsigned char* side, int sw, int sh,
                                  int height)
{
    VoxelModel m;
    if (top == nullptr || tw <= 0 || th <= 0 || height <= 0)
        return m;
    m.w = tw;
    m.d = th;
    m.z = height;
    m.cell = 1.0f;
    m.cube_faces = false;
    const std::size_t n = static_cast<std::size_t>(tw) *
                          static_cast<std::size_t>(th) *
                          static_cast<std::size_t>(height);
    m.occ.assign(n, 1u);
    m.index.assign(n, 0u);
    m.anchor_x = static_cast<float>(tw) * 0.5f;
    m.anchor_y = static_cast<float>(th) * 0.5f;
    for (int k = 0; k < height; ++k)
        for (int j = 0; j < th; ++j)
            for (int i = 0; i < tw; ++i)
            {
                const std::size_t idx = m.at(i, j, k);
                if (k == height - 1 || side == nullptr || sw <= 0 || sh <= 0)
                {
                    m.index[idx] = top[static_cast<std::size_t>(j) *
                                           static_cast<std::size_t>(tw) +
                                       static_cast<std::size_t>(i)];
                    continue;
                }
                // Slice k shows the wall face row that stands at that height;
                // the side art is tiled vertically so a taller-than-one-tile
                // wall keeps repeating its own courses.
                const int row = ((height - 1 - k) % sh + sh) % sh;
                // The rim you actually see is the near/far edge for a wall
                // running east-west and the left/right edge for one running
                // north-south, so take the face coordinate along whichever
                // edge this voxel sits on.
                const bool on_jedge = (j == 0 || j == th - 1);
                const int col = on_jedge ? (i % sw) : (j % sw);
                m.index[idx] = side[static_cast<std::size_t>(row) *
                                        static_cast<std::size_t>(sw) +
                                    static_cast<std::size_t>(col)];
            }
    recompute_lit(m);
    return m;
}

VoxelModel voxel_build_tree_model(const unsigned char* top, int tw, int th,
                                  const unsigned char* ground, int gw, int gh,
                                  int trunk_h, int canopy_h, int trunk_size)
{
    VoxelModel m;
    if (top == nullptr || tw <= 0 || th <= 0 || trunk_h + canopy_h <= 0)
        return m;
    m.w = tw;
    m.d = th;
    m.z = trunk_h + canopy_h;
    m.cell = 1.0f;
    m.cube_faces = false;
    const std::size_t n = static_cast<std::size_t>(tw) *
                          static_cast<std::size_t>(th) *
                          static_cast<std::size_t>(m.z);
    m.occ.assign(n, 0u);
    m.index.assign(n, 0u);
    m.anchor_x = static_cast<float>(tw) * 0.5f;
    m.anchor_y = static_cast<float>(th) * 0.5f;
    const int lo_i = (tw - trunk_size) / 2;
    const int lo_j = (th - trunk_size) / 2;
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < th; ++j)
            for (int i = 0; i < tw; ++i)
            {
                const bool in_trunk = (i >= lo_i && i < lo_i + trunk_size &&
                                       j >= lo_j && j < lo_j + trunk_size);
                // The base slice is GROUND, full footprint: a canopy that
                // overhangs bare grid reads as a black trench otherwise.
                const bool base = (k == 0);
                if (!base && k < trunk_h && !in_trunk)
                    continue;
                const std::size_t idx = m.at(i, j, k);
                m.occ[idx] = 1u;
                const unsigned char* src = top;
                int sw = tw, sh = th;
                if (base && ground != nullptr && gw > 0 && gh > 0)
                {
                    src = ground;
                    sw = gw;
                    sh = gh;
                }
                m.index[idx] = src[static_cast<std::size_t>(j % sh) *
                                       static_cast<std::size_t>(sw) +
                                   static_cast<std::size_t>(i % sw)];
            }
    recompute_lit(m);
    return m;
}

} // namespace og::render
