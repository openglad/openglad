/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Voting-hull voxel figures (docs/voxel-render-design.md §15, §16).
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
//
// Round 10 cleans up round 9's three dirt sources — floating cubes, speckled
// colour, an inflated hull — without changing the construction:
//
//   * the vote still starts the hull, but the hull is then carved down until
//     every voxel a view can actually SEE lands inside that view's silhouette;
//   * a residual pixel is only put back when it is part of the frame's own
//     body (8-connected to the main silhouette) and lands within two pixels of
//     the hull's projection, and anything that ends up not touching the hull
//     is deleted again;
//   * a surface voxel takes its colour from the one view that sees it most
//     head-on, so the frames stop voting pixel by pixel.

#include <openglad/interface/render/voxel_figure.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

// How many refinement passes the visibility-tight carve is allowed. The carve
// is monotone (it only ever deletes) and bounded below by the strict
// intersection, so it converges; the cap is there so a pathological frame set
// cannot spend the whole build.
constexpr int kTightenPasses = 4;

// A residual pixel must land this close to the hull's own projection. Further
// than that and it is not a detail on the body, it is a floating cube.
constexpr int kResidualReach = 2;

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
    void decompose(std::size_t s, int& i, int& j, int& k) const
    {
        const std::size_t plane =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(d);
        k = static_cast<int>(s / plane);
        const std::size_t rem = s % plane;
        j = static_cast<int>(rem / static_cast<std::size_t>(w));
        i = static_cast<int>(rem % static_cast<std::size_t>(w));
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

// A point of the model, projected into the sprite's pixel space.
inline Hit project_at(const View& vw, const Grid& g, float fi, float fj,
                      float fk, int sw, int sh, float scale)
{
    const float mx = fi - static_cast<float>(g.w) * 0.5f;
    const float my = fj - static_cast<float>(g.d) * 0.5f;
    const float mz = fk;
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

// A voxel centre, projected into the sprite's pixel space.
inline Hit project(const View& vw, const Grid& g, int i, int j, int k, int sw,
                   int sh, float scale)
{
    return project_at(vw, g, static_cast<float>(i) + 0.5f,
                      static_cast<float>(j) + 0.5f,
                      static_cast<float>(k) + 0.5f, sw, sh, scale);
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

// The visibility buffers are wider than the frame: a voxel that projects OFF
// the sprite is outside that view's silhouette just as surely as one that
// lands on a transparent pixel, and the carve has to be able to see that.
struct ViewBuffer
{
    int pad = 0;
    int w = 0, h = 0;
    std::vector<std::size_t> owner;
    std::vector<float> zbuf;
    [[nodiscard]] std::size_t at(int u, int v) const
    {
        return static_cast<std::size_t>(v + pad) * static_cast<std::size_t>(w) +
            static_cast<std::size_t>(u + pad);
    }
    [[nodiscard]] bool holds(int u, int v) const
    {
        return u + pad >= 0 && v + pad >= 0 && u + pad < w && v + pad < h;
    }
};

// Frontmost occupied voxel per pixel, for one view. This is the "seen by"
// relation the colour pass already used; the carve uses the same one.
void build_view_buffer(const Grid& g, const View& vw, int fw, int fh, int pad,
                       ViewBuffer& vb)
{
    vb.pad = pad;
    vb.w = fw + pad * 2;
    vb.h = fh + pad * 2;
    const std::size_t n =
        static_cast<std::size_t>(vb.w) * static_cast<std::size_t>(vb.h);
    vb.owner.assign(n, static_cast<std::size_t>(-1));
    vb.zbuf.assign(n, -std::numeric_limits<float>::infinity());
    for (int k = 0; k < g.z; ++k)
        for (int j = 0; j < g.d; ++j)
            for (int i = 0; i < g.w; ++i)
            {
                const std::size_t s = g.at(i, j, k);
                if (g.occ[s] == 0)
                    continue;
                const Hit hh = project(vw, g, i, j, k, fw, fh, 1.0f);
                if (!vb.holds(hh.u, hh.v))
                    continue;
                const std::size_t p = vb.at(hh.u, hh.v);
                if (hh.near <= vb.zbuf[p])
                    continue;
                vb.zbuf[p] = hh.near;
                vb.owner[p] = s;
            }
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
                    int ci = 0, cj = 0, ck = 0;
                    g.decompose(c, ci, cj, ck);
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
        int ci = 0, cj = 0, ck = 0;
        g.decompose(c, ci, cj, ck);
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

// Silhouette-only IoU of a grid against the frames, for the before/after
// report. No colour, no model — just "does this solid cover the sprite".
float silhouette_iou(const Grid& g, const VoxelCarveFrames& frames,
                     const View (&views)[NUM_VOXEL_FACINGS])
{
    const std::size_t frame_px = static_cast<std::size_t>(frames.w) *
                                 static_cast<std::size_t>(frames.h);
    std::vector<unsigned char> shot(frame_px);
    std::vector<float> zb(frame_px);
    float sum = 0.0f;
    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
    {
        std::fill(shot.begin(), shot.end(), 0u);
        std::fill(zb.begin(), zb.end(),
                  -std::numeric_limits<float>::infinity());
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
                    if (hh.near <= zb[p])
                        continue;
                    zb[p] = hh.near;
                    shot[p] = 1u;
                }
        int inter = 0, uni = 0;
        for (std::size_t p = 0; p < frame_px; ++p)
        {
            const bool a =
                frames.frame[d] != nullptr && frames.frame[d][p] != 0;
            const bool b = shot[p] != 0;
            if (a || b)
                ++uni;
            if (a && b)
                ++inter;
        }
        sum += uni > 0
            ? static_cast<float>(inter) / static_cast<float>(uni)
            : 0.0f;
    }
    return sum / static_cast<float>(NUM_VOXEL_FACINGS);
}

// The frame's own body: 8-connected components of the opaque pixels, largest
// wins. A residual pixel that is not part of it is a stray in the ART, and
// putting it back is how round 9 grew fringes of unattached cubes.
void main_silhouette(const unsigned char* frame, int fw, int fh,
                     std::vector<unsigned char>& in_main)
{
    const std::size_t n =
        static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh);
    in_main.assign(n, 0u);
    std::vector<int> label(n, -1);
    std::vector<int> sizes;
    std::deque<std::size_t> q;
    for (int v = 0; v < fh; ++v)
        for (int u = 0; u < fw; ++u)
        {
            const std::size_t s =
                static_cast<std::size_t>(v) * static_cast<std::size_t>(fw) +
                static_cast<std::size_t>(u);
            if (frame[s] == 0 || label[s] >= 0)
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
                const int cv = static_cast<int>(c / static_cast<std::size_t>(fw));
                const int cu = static_cast<int>(c % static_cast<std::size_t>(fw));
                for (int dv = -1; dv <= 1; ++dv)
                    for (int du = -1; du <= 1; ++du)
                    {
                        const int qu = cu + du, qv = cv + dv;
                        if (qu < 0 || qv < 0 || qu >= fw || qv >= fh)
                            continue;
                        const std::size_t t =
                            static_cast<std::size_t>(qv) *
                                static_cast<std::size_t>(fw) +
                            static_cast<std::size_t>(qu);
                        if (frame[t] == 0 || label[t] >= 0)
                            continue;
                        label[t] = id;
                        q.push_back(t);
                    }
            }
        }
    if (sizes.empty())
        return;
    int biggest = 0;
    for (std::size_t i = 1; i < sizes.size(); ++i)
        if (sizes[i] > sizes[static_cast<std::size_t>(biggest)])
            biggest = static_cast<int>(i);
    for (std::size_t s = 0; s < n; ++s)
        if (label[s] == biggest)
            in_main[s] = 1u;
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

    const std::size_t frame_px = static_cast<std::size_t>(frames.w) *
                                 static_cast<std::size_t>(frames.h);
    // Wide enough that no voxel of this grid can project past the buffer:
    // the widest possible turn of a w x d footprint plus the whole column.
    const int pad = frames.w + frames.h + g.z;
    const auto silhouette = [&](int d, int u, int v) -> unsigned char {
        if (frames.frame[d] == nullptr)
            return 0u;
        if (u < 0 || v < 0 || u >= frames.w || v >= frames.h)
            return 0u;
        return frames.frame[d][static_cast<std::size_t>(v) *
                                   static_cast<std::size_t>(frames.w) +
                               static_cast<std::size_t>(u)];
    };
    // "Its projection falls outside the silhouette" is a claim about the whole
    // cube, not about the one pixel its centre happens to land in. A voxel
    // projects to about two pixels, so testing the centre alone turns a
    // half-pixel misregistration into "outside" — and since deleting that
    // voxel exposes the next one to the same error, the carve peels a shell
    // per pass instead of converging (measured: 33% of the hull gone in four
    // passes, and the silhouette fit gets WORSE, not better). So the cube is
    // outside when most of its eight projected corners land on background.
    const auto covers_silhouette = [&](int d, int i, int j, int k) {
        if (frames.frame[d] == nullptr)
            return true;
        int hits = 0;
        for (int c = 0; c < 8; ++c)
        {
            const Hit hh = project_at(
                views[d], g, static_cast<float>(i) + ((c & 1) ? 1.0f : 0.0f),
                static_cast<float>(j) + ((c & 2) ? 1.0f : 0.0f),
                static_cast<float>(k) + ((c & 4) ? 1.0f : 0.0f), frames.w,
                frames.h, 1.0f);
            if (silhouette(d, hh.u, hh.v) != 0)
                ++hits;
        }
        return hits >= 4;
    };

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
                    if (silhouette(d, hh.u, hh.v) != 0)
                        ++votes;
                }
                if (votes >= votes_required)
                    g.occ[g.at(i, j, k)] = 1u;
            }
    (void)prune_components(g);
    (void)fill_cavities(g);
    for (unsigned char o : g.occ)
        if (o != 0)
            ++rep.hull_voxels_initial;
    rep.mean_iou_initial = silhouette_iou(g, frames, views);

    // ---- 2. carve the hull tight to what the views can SEE -------------
    // The vote lets a voxel survive on seven agreements even when the eighth
    // view is looking straight at it and says "background" — which is the
    // inflation. So: find the views that actually see each voxel (nothing of
    // ours nearer along that ray) and delete it the moment a seeing view puts
    // it outside its silhouette. Only ever deletes, so it converges.
    {
        ViewBuffer vb[NUM_VOXEL_FACINGS];
        std::vector<unsigned char> doomed(n, 0u);
        for (int pass = 0; pass < kTightenPasses; ++pass)
        {
            for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
                build_view_buffer(g, views[d], frames.w, frames.h, pad, vb[d]);
            std::fill(doomed.begin(), doomed.end(), 0u);
            int cut = 0;
            for (int k = 0; k < g.z; ++k)
                for (int j = 0; j < g.d; ++j)
                    for (int i = 0; i < g.w; ++i)
                    {
                        const std::size_t s = g.at(i, j, k);
                        if (g.occ[s] == 0)
                            continue;
                        bool violates = false;
                        for (int d = 0; d < NUM_VOXEL_FACINGS && !violates; ++d)
                        {
                            if (frames.frame[d] == nullptr)
                                continue;
                            const Hit hh = project(views[d], g, i, j, k,
                                                   frames.w, frames.h, 1.0f);
                            if (!vb[d].holds(hh.u, hh.v))
                                continue;
                            if (vb[d].owner[vb[d].at(hh.u, hh.v)] != s)
                                continue; // this view cannot see it
                            if (!covers_silhouette(d, i, j, k))
                                violates = true;
                        }
                        if (violates)
                        {
                            doomed[s] = 1u;
                            ++cut;
                        }
                    }
            if (cut == 0)
                break;
            for (std::size_t s = 0; s < n; ++s)
                if (doomed[s])
                    g.occ[s] = 0u;
            rep.tighten_deleted += cut;
            ++rep.tighten_passes;
        }
    }
    rep.components_dropped += prune_components(g);
    rep.cavities_filled += fill_cavities(g);
    for (unsigned char o : g.occ)
        if (o != 0)
            ++rep.hull_voxels;
    rep.mean_iou_hull = silhouette_iou(g, frames, views);
    std::vector<unsigned char> is_hull(n, 0u);
    for (std::size_t s = 0; s < n; ++s)
        is_hull[s] = g.occ[s];

    // ---- 3. attached residuals -----------------------------------------
    // A frame pixel the hull misses goes back ONLY if it is part of the
    // frame's own body and lands within two pixels of where the hull draws —
    // that is what makes it a detail ON the figure rather than a cube in the
    // air near it. It is placed one voxel behind the depth its ray meets the
    // hull, two voxels thick, so it sits on the surface.
    std::vector<unsigned char> residual_index(n, 0u);
    std::vector<unsigned char> is_residual(n, 0u);
    {
        std::vector<std::vector<unsigned char>> in_main(NUM_VOXEL_FACINGS);
        for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
            if (frames.frame[d] != nullptr)
                main_silhouette(frames.frame[d], frames.w, frames.h,
                                in_main[static_cast<std::size_t>(d)]);
        std::vector<unsigned char> cover(frame_px);
        std::vector<unsigned char> grown(frame_px);
        std::vector<unsigned char> reach(frame_px);
        // The whole depth range a voxel of this grid can occupy, walked one
        // voxel at a time from the camera inwards.
        const float near_far = static_cast<float>(g.w + g.d) * 0.5f +
            static_cast<float>(g.z);
        const int near_steps = static_cast<int>(near_far * 2.0f) + 2;
        const auto dilate = [&](std::vector<unsigned char>& mask, int steps) {
            for (int r = 0; r < steps; ++r)
            {
                const std::vector<unsigned char> src = mask;
                for (int v = 0; v < frames.h; ++v)
                    for (int u = 0; u < frames.w; ++u)
                    {
                        if (src[static_cast<std::size_t>(v) *
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
                                mask[static_cast<std::size_t>(qv) *
                                         static_cast<std::size_t>(frames.w) +
                                     static_cast<std::size_t>(qu)] = 1u;
                            }
                    }
            }
        };
        // Grown outward, one ring per round: a sword is four pixels long and
        // only its innermost pixel is next to the body, so a single pass that
        // demands "within two pixels of the hull" would keep the hilt and drop
        // the blade. Each round re-reads the solid as it now stands, so every
        // pixel that goes back is next to something that is already there.
        for (int round = 0; round < 8; ++round)
        {
            int placed = 0;
            for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
            {
                if (frames.frame[d] == nullptr)
                    continue;
                std::fill(cover.begin(), cover.end(), 0u);
                for (int k = 0; k < g.z; ++k)
                    for (int j = 0; j < g.d; ++j)
                        for (int i = 0; i < g.w; ++i)
                        {
                            if (g.occ[g.at(i, j, k)] == 0)
                                continue;
                            const Hit hh = project(views[d], g, i, j, k,
                                                   frames.w, frames.h, 1.0f);
                            if (hh.u < 0 || hh.v < 0 || hh.u >= frames.w ||
                                hh.v >= frames.h)
                                continue;
                            const std::size_t p =
                                static_cast<std::size_t>(hh.v) *
                                    static_cast<std::size_t>(frames.w) +
                                static_cast<std::size_t>(hh.u);
                            cover[p] = 1u;
                        }
                // One pixel of slack closes the holes a point-sampled
                // projection leaves inside its own body. Without it every
                // gap in the hull's own footprint reads as a missing detail
                // and the figure grows a fur coat.
                grown = cover;
                dilate(grown, 1);
                reach = grown;
                dilate(reach, kResidualReach);
                for (int v = 0; v < frames.h; ++v)
                    for (int u = 0; u < frames.w; ++u)
                    {
                        const std::size_t p = static_cast<std::size_t>(v) *
                                static_cast<std::size_t>(frames.w) +
                            static_cast<std::size_t>(u);
                        const unsigned char c = frames.frame[d][p];
                        if (c == 0 || grown[p] != 0)
                            continue;
                        if (in_main[static_cast<std::size_t>(d)][p] == 0 ||
                            reach[p] == 0)
                            continue;
                        if (round == 0)
                            ++rep.residual_candidates;
                        // Where this pixel's ray first touches the solid. The
                        // ray does not pierce it — this pixel is one the hull
                        // does not draw — so the touch is the frontmost empty
                        // cell along the ray with an occupied neighbour. Using
                        // a NEIGHBOURING pixel's depth instead (round 9) puts
                        // the voxel two cells off the body in space, which is
                        // the floating cube: two pixels apart on screen is two
                        // cells apart in the grid.
                        float touch = 0.0f;
                        bool found = false;
                        for (int step = 0; step <= near_steps && !found; ++step)
                        {
                            const float near =
                                near_far - static_cast<float>(step);
                            int oi = 0, oj = 0, ok = 0;
                            if (!unproject(views[d], g, u, v, near, frames.w,
                                           frames.h, oi, oj, ok))
                                continue;
                            if (g.occ[g.at(oi, oj, ok)] != 0)
                                continue;
                            bool touching = false;
                            for (int dk = -1; dk <= 1 && !touching; ++dk)
                                for (int dj = -1; dj <= 1 && !touching; ++dj)
                                    for (int di = -1; di <= 1 && !touching;
                                         ++di)
                                        if (g.solid(oi + di, oj + dj, ok + dk))
                                            touching = true;
                            if (!touching)
                                continue;
                            touch = near;
                            found = true;
                        }
                        if (!found)
                            continue;
                        // Two voxels thick along the ray, starting on the
                        // surface it just touched.
                        for (int t = 0; t < 2; ++t)
                        {
                            int oi = 0, oj = 0, ok = 0;
                            if (!unproject(views[d], g, u, v,
                                           touch - static_cast<float>(t),
                                           frames.w, frames.h, oi, oj, ok))
                                continue;
                            const std::size_t s = g.at(oi, oj, ok);
                            if (g.occ[s] != 0)
                                continue;
                            g.occ[s] = 1u;
                            is_residual[s] = 1u;
                            residual_index[s] = c;
                            ++placed;
                        }
                    }
            }
            if (placed == 0)
                break;
            ++rep.residual_rounds;
        }
    }

    // Anything that did not end up touching the hull is a floating cube.
    {
        std::vector<unsigned char> attached(n, 0u);
        std::deque<std::size_t> q;
        for (std::size_t s = 0; s < n; ++s)
            if (is_hull[s] != 0)
            {
                attached[s] = 1u;
                q.push_back(s);
            }
        while (!q.empty())
        {
            const std::size_t c = q.front();
            q.pop_front();
            int ci = 0, cj = 0, ck = 0;
            g.decompose(c, ci, cj, ck);
            for (int dk = -1; dk <= 1; ++dk)
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di)
                    {
                        if (di == 0 && dj == 0 && dk == 0)
                            continue;
                        if (!g.solid(ci + di, cj + dj, ck + dk))
                            continue;
                        const std::size_t t = g.at(ci + di, cj + dj, ck + dk);
                        if (attached[t])
                            continue;
                        attached[t] = 1u;
                        q.push_back(t);
                    }
        }
        for (std::size_t s = 0; s < n; ++s)
            if (is_residual[s] != 0 && !attached[s])
            {
                g.occ[s] = 0u;
                is_residual[s] = 0u;
                ++rep.residual_dropped;
            }
    }
    // ---- 4. one solid: nothing outside the main 26-component survives ---
    rep.components_dropped += prune_components(g);
    rep.cavities_filled += fill_cavities(g);
    for (std::size_t s = 0; s < n; ++s)
    {
        if (g.occ[s] == 0)
        {
            is_residual[s] = 0u;
            is_hull[s] = 0u;
            continue;
        }
        if (is_residual[s] != 0)
            ++rep.residual_voxels;
    }

    // ---- 5. colour ------------------------------------------------------
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
                // Surface = the 26-neighbourhood is not full. The normal is
                // estimated over a wider window so it is not quantised to the
                // six axes.
                for (int dk = -1; dk <= 1 && surface[s] == 0; ++dk)
                    for (int dj = -1; dj <= 1 && surface[s] == 0; ++dj)
                        for (int di = -1; di <= 1 && surface[s] == 0; ++di)
                            if (!g.solid(i + di, j + dj, k + dk))
                                surface[s] = 1u;
                if (surface[s] == 0)
                    continue;
                float ax = 0.0f, ay = 0.0f, az = 0.0f;
                for (int dk = -2; dk <= 2; ++dk)
                    for (int dj = -2; dj <= 2; ++dj)
                        for (int di = -2; di <= 2; ++di)
                        {
                            if (di == 0 && dj == 0 && dk == 0)
                                continue;
                            if (g.solid(i + di, j + dj, k + dk))
                                continue;
                            const float r2 = static_cast<float>(
                                di * di + dj * dj + dk * dk);
                            ax += static_cast<float>(di) / r2;
                            ay += static_cast<float>(dj) / r2;
                            az += static_cast<float>(dk) / r2;
                        }
                const float len = std::sqrt(ax * ax + ay * ay + az * az);
                if (len > 1e-6f)
                {
                    nx[s] = ax / len;
                    ny[s] = ay / len;
                    nz[s] = az / len;
                }
            }

    // Primary view: of the views that SEE this voxel, the one it faces most
    // squarely. One view decides the pixel — the frames stop voting, which is
    // where round 9's speckle came from.
    std::vector<unsigned char> painted(n, 0u);
    std::vector<unsigned char> primary_index(n, 0u);
    std::vector<unsigned char> has_primary(n, 0u);
    {
        ViewBuffer vb[NUM_VOXEL_FACINGS];
        for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
            build_view_buffer(g, views[d], frames.w, frames.h, pad, vb[d]);
        for (int k = 0; k < g.z; ++k)
            for (int j = 0; j < g.d; ++j)
                for (int i = 0; i < g.w; ++i)
                {
                    const std::size_t s = g.at(i, j, k);
                    if (g.occ[s] == 0 || surface[s] == 0)
                        continue;
                    float dots[NUM_VOXEL_FACINGS];
                    unsigned char pix[NUM_VOXEL_FACINGS];
                    int seeing = 0;
                    for (int d = 0; d < NUM_VOXEL_FACINGS; ++d)
                    {
                        if (frames.frame[d] == nullptr)
                            continue;
                        const Hit hh = project(views[d], g, i, j, k, frames.w,
                                               frames.h, 1.0f);
                        if (!vb[d].holds(hh.u, hh.v))
                            continue;
                        if (vb[d].owner[vb[d].at(hh.u, hh.v)] != s)
                            continue;
                        const float vdx = views[d].ct * views[d].sa;
                        const float vdy = views[d].ct * views[d].ca;
                        const float vdz = views[d].st;
                        dots[seeing] =
                            nx[s] * vdx + ny[s] * vdy + nz[s] * vdz;
                        pix[seeing] = silhouette(d, hh.u, hh.v);
                        ++seeing;
                    }
                    // Best-aligned seeing view whose pixel is not transparent.
                    while (seeing > 0)
                    {
                        int best = 0;
                        for (int e = 1; e < seeing; ++e)
                            if (dots[e] > dots[best])
                                best = e;
                        if (pix[best] != 0)
                        {
                            m.index[s] = pix[best];
                            primary_index[s] = pix[best];
                            has_primary[s] = 1u;
                            painted[s] = 1u;
                            break;
                        }
                        dots[best] = dots[seeing - 1];
                        pix[best] = pix[seeing - 1];
                        --seeing;
                    }
                    if (painted[s] == 0 && is_residual[s] != 0)
                    {
                        m.index[s] = residual_index[s];
                        painted[s] = 1u;
                    }
                }
    }
    // A residual nobody could see still knows the pixel it was put back for.
    for (std::size_t s = 0; s < n; ++s)
        if (g.occ[s] != 0 && !painted[s] && is_residual[s] != 0)
        {
            m.index[s] = residual_index[s];
            painted[s] = 1u;
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
        int i = 0, j = 0, k = 0;
        g.decompose(s, i, j, k);
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

    // Two surface-majority passes, team band as its own class. A voxel still
    // wearing the pixel its primary view gave it, with two surface neighbours
    // agreeing, is a feature and is left alone; everything else follows its
    // neighbourhood. Interior voxels are never touched.
    {
        const auto same = [](unsigned char a, unsigned char c) {
            return (a >= 248) ? (c >= 248) : (a == c);
        };
        for (int sweep = 0; sweep < 2; ++sweep)
        {
            const std::vector<unsigned char> src = m.index;
            for (int k = 0; k < g.z; ++k)
                for (int j = 0; j < g.d; ++j)
                    for (int i = 0; i < g.w; ++i)
                    {
                        const std::size_t s = g.at(i, j, k);
                        if (g.occ[s] == 0 || !surface[s])
                            continue;
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
                        if (ncls == 0)
                            continue;
                        if (has_primary[s] != 0 && self == primary_index[s] &&
                            self_votes >= 2)
                            continue;
                        int best = 0;
                        for (int e = 1; e < ncls; ++e)
                            if (cnt[e] > cnt[best])
                                best = e;
                        if (cnt[best] >= 3 && !same(self, rep_c[best]))
                            m.index[s] = rep_c[best];
                    }
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

    // ---- 6. score ------------------------------------------------------
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
