/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Voting-hull voxel figures (docs/voxel-render-design.md §15).
//
// The projection is the sprite's own space, the same one the fidelity strips
// use: put the figure's footprint centre on the ground at the sprite's foot
// line and view it with the game camera at elevation theta, so a voxel at
// model-local (mx, my, mz) lands at
//
//     u = w/2 + wx
//     v = (h - 1) + wy*sin(theta) - mz*cos(theta)
//     near = wy*cos(theta) + mz*sin(theta)
//
// with (wx, wy) being (mx, my) turned by the facing's yaw. One voxel is one
// sprite pixel, so the solid is exactly as coarse as the art.

#include <openglad/interface/render/voxel_figure.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

struct Grid
{
    int w = 0, d = 0, z = 0;
    std::vector<unsigned char> occ;
    [[nodiscard]] std::size_t at(int i, int j, int k) const
    {
        return (static_cast<std::size_t>(k) * static_cast<std::size_t>(d) +
                static_cast<std::size_t>(j)) *
                   static_cast<std::size_t>(w) +
               static_cast<std::size_t>(i);
    }
    [[nodiscard]] bool inside(int i, int j, int k) const
    {
        return i >= 0 && j >= 0 && k >= 0 && i < w && j < d && k < z;
    }
    [[nodiscard]] bool solid(int i, int j, int k) const
    {
        return inside(i, j, k) && occ[at(i, j, k)] != 0;
    }
};

struct View
{
    float ca = 1.0f, sa = 0.0f;
    float st = 0.0f, ct = 1.0f;
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

// A voxel centre, projected into the sprite's pixel space.
inline Hit project(const View& vw, const Grid& g, int i, int j, int k, int sw,
                   int sh, float scale)
{
    const float mx = static_cast<float>(i) + 0.5f -
        static_cast<float>(g.w) * 0.5f;
    const float my = static_cast<float>(j) + 0.5f -
        static_cast<float>(g.d) * 0.5f;
    const float mz = static_cast<float>(k) + 0.5f;
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

// The inverse: a pixel plus a depth gives back a grid cell. Used to hang the
// residual details at the depth their view ray meets the hull.
bool unproject(const View& vw, const Grid& g, int u, int v, float near, int sw,
               int sh, int& oi, int& oj, int& ok)
{
    const float wx = static_cast<float>(u) + 0.5f -
        static_cast<float>(sw) * 0.5f;
    const float dv = static_cast<float>(v) + 0.5f -
        static_cast<float>(sh - 1);
    // Invert the 2x2 rotation between (wy, mz) and (screen-v, near).
    const float wy = dv * vw.st + near * vw.ct;
    const float mz = -dv * vw.ct + near * vw.st;
    const float mx = wx * vw.ca + wy * vw.sa;
    const float my = -wx * vw.sa + wy * vw.ca;
    oi = static_cast<int>(std::floor(mx + static_cast<float>(g.w) * 0.5f));
    oj = static_cast<int>(std::floor(my + static_cast<float>(g.d) * 0.5f));
    ok = static_cast<int>(std::floor(mz));
    return g.inside(oi, oj, ok);
}

int prune_components(Grid& g)
{
    const std::size_t n = g.occ.size();
    std::vector<int> label(n, -1);
    std::vector<int> sizes;
    std::deque<std::size_t> q;
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                const std::size_t s = g.at(i, j, k);
                if (g.occ[s] == 0 || label[s] >= 0)
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
                        c / (static_cast<std::size_t>(g.w) *
                             static_cast<std::size_t>(g.d)));
                    const std::size_t rem =
                        c % (static_cast<std::size_t>(g.w) *
                             static_cast<std::size_t>(g.d));
                    const int cj =
                        static_cast<int>(rem / static_cast<std::size_t>(g.w));
                    const int ci =
                        static_cast<int>(rem % static_cast<std::size_t>(g.w));
                    for (int dk = -1; dk <= 1; ++dk)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                            {
                                if (di == 0 && dj == 0 && dk == 0)
                                    continue;
                                if (!g.solid(ci + di, cj + dj, ck + dk))
                                    continue;
                                const std::size_t t =
                                    g.at(ci + di, cj + dj, ck + dk);
                                if (label[t] >= 0)
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
    int dropped = 0;
    for (std::size_t s = 0; s < n; ++s)
        if (g.occ[s] != 0 && label[s] != biggest)
        {
            g.occ[s] = 0;
            ++dropped;
        }
    return dropped;
}

int fill_cavities(Grid& g)
{
    const std::size_t n = g.occ.size();
    std::vector<unsigned char> outside(n, 0u);
    std::deque<std::size_t> q;
    const auto seed = [&](int i, int j, int k) {
        if (!g.inside(i, j, k))
            return;
        const std::size_t s = g.at(i, j, k);
        if (g.occ[s] != 0 || outside[s])
            return;
        outside[s] = 1u;
        q.push_back(s);
    };
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
        {
            seed(0, j, k);
            seed(g.w - 1, j, k);
        }
    for (int k = 0; k < g.z; ++k)
        for (int i = 0; i < g.w; ++i)
        {
            seed(i, 0, k);
            seed(i, g.d - 1, k);
        }
    for (int j = 0; j < g.d; ++j)
        for (int i = 0; i < g.w; ++i)
        {
            seed(i, j, 0);
            seed(i, j, g.z - 1);
        }
    while (!q.empty())
    {
        const std::size_t c = q.front();
        q.pop_front();
        const int ck = static_cast<int>(
            c / (static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.d)));
        const std::size_t rem =
            c % (static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.d));
        const int cj = static_cast<int>(rem / static_cast<std::size_t>(g.w));
        const int ci = static_cast<int>(rem % static_cast<std::size_t>(g.w));
        const int nb[6][3] = {{ci - 1, cj, ck}, {ci + 1, cj, ck},
                              {ci, cj - 1, ck}, {ci, cj + 1, ck},
                              {ci, cj, ck - 1}, {ci, cj, ck + 1}};
        for (const auto& p : nb)
        {
            if (!g.inside(p[0], p[1], p[2]))
                continue;
            const std::size_t t = g.at(p[0], p[1], p[2]);
            if (g.occ[t] != 0 || outside[t])
                continue;
            outside[t] = 1u;
            q.push_back(t);
        }
    }
    int filled = 0;
    for (std::size_t s = 0; s < n; ++s)
        if (g.occ[s] == 0 && !outside[s])
        {
            g.occ[s] = 1u;
            ++filled;
        }
    return filled;
}

} // namespace

void voxel_figure_render(const VoxelModel& model, int facing, float theta_deg,
                         unsigned char* out, int out_w, int out_h,
                         int sprite_w, int sprite_h, float scale)
{
    if (out == nullptr || model.empty())
        return;
    std::memset(out, 0,
                static_cast<std::size_t>(out_w) *
                    static_cast<std::size_t>(out_h));
    const View vw = view_for(facing, theta_deg);
    Grid g;
    g.w = model.w;
    g.d = model.d;
    g.z = model.z;
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
                const Hit hh =
                    project(vw, g, i, j, k, sprite_w, sprite_h, scale);
                if (hh.u < 0 || hh.v < 0 || hh.u >= out_w || hh.v >= out_h)
                    continue;
                const std::size_t p = static_cast<std::size_t>(hh.v) *
                        static_cast<std::size_t>(out_w) +
                    static_cast<std::size_t>(hh.u);
                if (hh.near <= zbuf[p])
                    continue;
                zbuf[p] = hh.near;
                out[p] = model.index[s];
            }
}

FigureReport voxel_build_figure(const VoxelCarveFrames& frames,
                                int votes_required, float theta_deg)
{
    FigureReport rep;
    rep.votes_required = votes_required;
    if (frames.w <= 0 || frames.h <= 0)
        return rep;
    const auto t0 = std::chrono::steady_clock::now();

    const float st = std::sin(theta_deg * kDeg2Rad);
    Grid g;
    g.w = frames.w;
    g.d = frames.w;
    g.z = std::max(4, static_cast<int>(std::ceil(
                          static_cast<float>(frames.h) /
                          std::max(0.2f, st))) +
                          2);
    const std::size_t n = static_cast<std::size_t>(g.w) *
                          static_cast<std::size_t>(g.d) *
                          static_cast<std::size_t>(g.z);
    g.occ.assign(n, 0u);

    View views[NUM_VOXEL_FACINGS];
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
        views[d] = view_for(d, theta_deg);

    // ---- 1. the vote --------------------------------------------------
    // A strict intersection (k = 8) deletes everything only some facings can
    // see, which is exactly what a sword or a bow is. Relaxing the quorum
    // keeps those without letting the hull inflate into the background.
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                int votes = 0;
                for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
                {
                    if (frames.frame[d] == nullptr)
                        continue;
                    const Hit hh =
                        project(views[d], g, i, j, k, frames.w, frames.h, 1.0f);
                    if (hh.u < 0 || hh.v < 0 || hh.u >= frames.w ||
                        hh.v >= frames.h)
                        continue;
                    if (frames.frame[d][static_cast<std::size_t>(hh.v) *
                                            static_cast<std::size_t>(frames.w) +
                                        static_cast<std::size_t>(hh.u)] != 0)
                        ++votes;
                }
                if (votes >= votes_required)
                    g.occ[g.at(i, j, k)] = 1u;
            }
    rep.components_dropped = prune_components(g);
    rep.cavities_filled = fill_cavities(g);
    for (unsigned char o : g.occ)
        if (o != 0)
            ++rep.hull_voxels;

    // ---- 2. per-facing residuals --------------------------------------
    // Whatever the hull still fails to cover in a frame goes back as a thin
    // detail on that facing's own plane, two voxels deep, hung at the depth
    // where the view ray first touches the hull.
    const std::size_t frame_px = static_cast<std::size_t>(frames.w) *
                                 static_cast<std::size_t>(frames.h);
    std::vector<unsigned char> residual_owner(n, 0xFFu);
    std::vector<unsigned char> residual_index(n, 0u);
    std::vector<float> depth(frame_px);
    std::vector<unsigned char> cover(frame_px);
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        if (frames.frame[d] == nullptr)
            continue;
        std::fill(depth.begin(), depth.end(),
                  -std::numeric_limits<float>::infinity());
        std::fill(cover.begin(), cover.end(), 0u);
        for (int k = 0; k < g.z; ++k)
            for (int j = 0; j < g.d; ++j)
                for (int i = 0; i < g.w; ++i)
                {
                    if (g.occ[g.at(i, j, k)] == 0)
                        continue;
                    const Hit hh =
                        project(views[d], g, i, j, k, frames.w, frames.h, 1.0f);
                    if (hh.u < 0 || hh.v < 0 || hh.u >= frames.w ||
                        hh.v >= frames.h)
                        continue;
                    const std::size_t p = static_cast<std::size_t>(hh.v) *
                            static_cast<std::size_t>(frames.w) +
                        static_cast<std::size_t>(hh.u);
                    cover[p] = 1u;
                    depth[p] = std::max(depth[p], hh.near);
                }
        // Dilate the coverage by one pixel: a detail one pixel from the body
        // is the body's own edge, not a missing feature.
        std::vector<unsigned char> grown = cover;
        for (int v = 0; v < frames.h; ++v)
            for (int u = 0; u < frames.w; ++u)
            {
                if (cover[static_cast<std::size_t>(v) *
                              static_cast<std::size_t>(frames.w) +
                          static_cast<std::size_t>(u)] == 0)
                    continue;
                for (int dv = -1; dv <= 1; ++dv)
                    for (int du = -1; du <= 1; ++du)
                    {
                        const int qu = u + du, qv = v + dv;
                        if (qu < 0 || qv < 0 || qu >= frames.w ||
                            qv >= frames.h)
                            continue;
                        grown[static_cast<std::size_t>(qv) *
                                  static_cast<std::size_t>(frames.w) +
                              static_cast<std::size_t>(qu)] = 1u;
                    }
            }
        for (int v = 0; v < frames.h; ++v)
            for (int u = 0; u < frames.w; ++u)
            {
                const std::size_t p = static_cast<std::size_t>(v) *
                        static_cast<std::size_t>(frames.w) +
                    static_cast<std::size_t>(u);
                const unsigned char c = frames.frame[d][p];
                if (c == 0 || grown[p] != 0)
                    continue;
                // Depth of the nearest covered pixel around it; if the ray
                // never meets the hull, fall back to the plane through the
                // body's centre.
                float near = 0.0f;
                bool found = false;
                for (int r = 1; r <= 3 && !found; ++r)
                    for (int dv = -r; dv <= r && !found; ++dv)
                        for (int du = -r; du <= r && !found; ++du)
                        {
                            const int qu = u + du, qv = v + dv;
                            if (qu < 0 || qv < 0 || qu >= frames.w ||
                                qv >= frames.h)
                                continue;
                            const std::size_t qp =
                                static_cast<std::size_t>(qv) *
                                    static_cast<std::size_t>(frames.w) +
                                static_cast<std::size_t>(qu);
                            if (cover[qp] == 0)
                                continue;
                            near = depth[qp];
                            found = true;
                        }
                for (int t = 0; t < 2; ++t)
                {
                    int oi = 0, oj = 0, ok = 0;
                    if (!unproject(views[d], g, u, v,
                                   near - static_cast<float>(t), frames.w,
                                   frames.h, oi, oj, ok))
                        continue;
                    const std::size_t s = g.at(oi, oj, ok);
                    if (g.occ[s] != 0)
                        continue;
                    g.occ[s] = 1u;
                    residual_owner[s] = static_cast<unsigned char>(d);
                    residual_index[s] = c;
                    ++rep.residual_voxels;
                }
            }
    }

    // ---- 3. colour -----------------------------------------------------
    VoxelModel& m = rep.model;
    m.w = g.w;
    m.d = g.d;
    m.z = g.z;
    m.cell = 1.0f; // one voxel is one sprite pixel: nice big voxels
    m.cube_faces = true;
    m.theta_deg = theta_deg;
    m.anchor_x = static_cast<float>(frames.w) * 0.5f;
    m.anchor_y = static_cast<float>(frames.h);
    m.occ = g.occ;
    m.index.assign(n, 0u);
    m.lit.assign(n, 0u);
    m.shade.clear(); // §15: no AO

    std::vector<float> nx(n, 0.0f), ny(n, 0.0f), nz(n, 1.0f);
    std::vector<unsigned char> surface(n, 0u);
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                const std::size_t s = g.at(i, j, k);
                if (g.occ[s] == 0)
                    continue;
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                bool any = false;
                for (int dk = -2; dk <= 2; ++dk)
                    for (int dj = -2; dj <= 2; ++dj)
                        for (int di = -2; di <= 2; ++di)
                        {
                            if (di == 0 && dj == 0 && dk == 0)
                                continue;
                            if (g.solid(i + di, j + dj, k + dk))
                                continue;
                            any = true;
                            const float r2 = static_cast<float>(
                                di * di + dj * dj + dk * dk);
                            ax += static_cast<float>(di) / r2;
                            ay += static_cast<float>(dj) / r2;
                            az += static_cast<float>(dk) / r2;
                        }
                if (!any)
                    continue;
                surface[s] = 1u;
                const float len = std::sqrt(ax * ax + ay * ay + az * az);
                if (len > 1e-6f)
                {
                    nx[s] = ax / len;
                    ny[s] = ay / len;
                    nz[s] = az / len;
                }
            }

    std::vector<float> best_dot(n, -2.0f);
    std::vector<float> best_near(n, -std::numeric_limits<float>::infinity());
    std::vector<unsigned char> painted(n, 0u);
    // Residual voxels already know their colour: it is the pixel they were
    // put back for.
    for (std::size_t s = 0; s < n; ++s)
        if (residual_owner[s] != 0xFFu)
        {
            m.index[s] = residual_index[s];
            painted[s] = 1u;
        }

    std::vector<float> zbuf(frame_px);
    std::vector<std::size_t> owner(frame_px);
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        if (frames.frame[d] == nullptr)
            continue;
        std::fill(zbuf.begin(), zbuf.end(),
                  -std::numeric_limits<float>::infinity());
        std::fill(owner.begin(), owner.end(), static_cast<std::size_t>(-1));
        for (int k = 0; k < g.z; ++k)
            for (int j = 0; j < g.d; ++j)
                for (int i = 0; i < g.w; ++i)
                {
                    const std::size_t s = g.at(i, j, k);
                    if (g.occ[s] == 0)
                        continue;
                    const Hit hh =
                        project(views[d], g, i, j, k, frames.w, frames.h, 1.0f);
                    if (hh.u < 0 || hh.v < 0 || hh.u >= frames.w ||
                        hh.v >= frames.h)
                        continue;
                    const std::size_t p = static_cast<std::size_t>(hh.v) *
                            static_cast<std::size_t>(frames.w) +
                        static_cast<std::size_t>(hh.u);
                    if (hh.near <= zbuf[p])
                        continue;
                    zbuf[p] = hh.near;
                    owner[p] = s;
                }
        const float vdx = views[d].ct * views[d].sa;
        const float vdy = views[d].ct * views[d].ca;
        const float vdz = views[d].st;
        for (std::size_t p = 0; p < frame_px; ++p)
        {
            if (owner[p] == static_cast<std::size_t>(-1))
                continue;
            const std::size_t s = owner[p];
            if (residual_owner[s] != 0xFFu)
                continue; // keeps its own pixel
            const unsigned char c = frames.frame[d][p];
            if (c == 0)
                continue;
            const float dot = nx[s] * vdx + ny[s] * vdy + nz[s] * vdz;
            if (dot > best_dot[s] + 1e-4f ||
                (dot > best_dot[s] - 1e-4f && zbuf[p] > best_near[s]))
            {
                best_dot[s] = std::max(best_dot[s], dot);
                best_near[s] = zbuf[p];
                m.index[s] = c;
                painted[s] = 1u;
            }
        }
    }

    std::deque<std::size_t> q;
    for (std::size_t s = 0; s < n; ++s)
        if (g.occ[s] != 0 && painted[s])
            q.push_back(s);
    const unsigned char fallback = q.empty() ? 1u : m.index[q.front()];
    while (!q.empty())
    {
        const std::size_t s = q.front();
        q.pop_front();
        const int k = static_cast<int>(
            s / (static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.d)));
        const std::size_t rem =
            s % (static_cast<std::size_t>(g.w) * static_cast<std::size_t>(g.d));
        const int j = static_cast<int>(rem / static_cast<std::size_t>(g.w));
        const int i = static_cast<int>(rem % static_cast<std::size_t>(g.w));
        const int nb[6][3] = {{i - 1, j, k}, {i + 1, j, k}, {i, j - 1, k},
                              {i, j + 1, k}, {i, j, k - 1}, {i, j, k + 1}};
        for (const auto& p : nb)
        {
            if (!g.solid(p[0], p[1], p[2]))
                continue;
            const std::size_t t = g.at(p[0], p[1], p[2]);
            if (painted[t])
                continue;
            painted[t] = 1u;
            m.index[t] = m.index[s];
            q.push_back(t);
        }
    }
    for (std::size_t s = 0; s < n; ++s)
        if (g.occ[s] != 0 && !painted[s])
            m.index[s] = fallback;

    // One surface-majority pass, team band as its own class.
    {
        const std::vector<unsigned char> src = m.index;
        const auto same = [](unsigned char a, unsigned char c) {
            return (a >= 248) ? (c >= 248) : (a == c);
        };
        for (int k = 0; k < g.z; ++k)
            for (int j = 0; j < g.d; ++j)
                for (int i = 0; i < g.w; ++i)
                {
                    const std::size_t s = g.at(i, j, k);
                    if (g.occ[s] == 0 || !surface[s])
                        continue;
                    if (residual_owner[s] != 0xFFu)
                        continue; // a detail is not a speckle
                    const unsigned char self = src[s];
                    int self_votes = 0;
                    unsigned char rep_c[8] = {};
                    int cnt[8] = {};
                    int ncls = 0;
                    for (int dk = -1; dk <= 1; ++dk)
                        for (int dj = -1; dj <= 1; ++dj)
                            for (int di = -1; di <= 1; ++di)
                            {
                                if (di == 0 && dj == 0 && dk == 0)
                                    continue;
                                if (!g.solid(i + di, j + dj, k + dk))
                                    continue;
                                const std::size_t t =
                                    g.at(i + di, j + dj, k + dk);
                                if (!surface[t])
                                    continue;
                                const unsigned char c = src[t];
                                if (same(self, c))
                                    ++self_votes;
                                int slot = -1;
                                for (int e = 0; e < ncls; ++e)
                                    if (same(rep_c[e], c))
                                        slot = e;
                                if (slot < 0 && ncls < 8)
                                {
                                    slot = ncls++;
                                    rep_c[slot] = c;
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
                        m.index[s] = rep_c[best];
                }
    }

    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                const std::size_t s = g.at(i, j, k);
                if (g.occ[s] == 0)
                    continue;
                m.lit[s] = g.solid(i, j, k + 1) ? 0u : 1u;
            }

    // ---- 4. score ------------------------------------------------------
    std::vector<unsigned char> shot(frame_px);
    float sum_i = 0.0f, sum_a = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        voxel_figure_render(m, d, theta_deg, shot.data(), frames.w, frames.h,
                            frames.w, frames.h, 1.0f);
        int inter = 0, uni = 0, matched = 0;
        for (std::size_t p = 0; p < frame_px; ++p)
        {
            const unsigned char a = frames.frame[d] != nullptr
                ? frames.frame[d][p]
                : 0u;
            const unsigned char b = shot[p];
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

    const auto t1 = std::chrono::steady_clock::now();
    rep.seconds = std::chrono::duration<double>(t1 - t0).count();
    return rep;
}

} // namespace og::render
