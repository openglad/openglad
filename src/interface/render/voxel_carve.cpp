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

#include <openglad/interface/render/voxel_carve.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

struct ViewBasis
{
    float cy = 1.0f; // cos(yaw)
    float sy = 0.0f; // sin(yaw)
};

struct CarveGeom
{
    int w = 0, d = 0, z = 0;
    float ax = 0.0f, ay = 0.0f;
    float st = 0.0f, ct = 1.0f; // sin/cos(theta)

    // Model-space centre of voxel (i, j, k).
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

ViewBasis basis_for(float yaw_rad)
{
    ViewBasis b;
    b.cy = std::cos(yaw_rad);
    b.sy = std::sin(yaw_rad);
    return b;
}

// One carve pass at a fixed anchor. Fills occ/index/lit.
void carve_once(const VoxelCarveFrames& fr, const CarveGeom& g,
                const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                VoxelModel& out, int& carved_transparent, int& carved_bounds)
{
    const int w = g.w, d = g.d, z = g.z;
    const std::size_t n = static_cast<std::size_t>(w) *
                          static_cast<std::size_t>(d) *
                          static_cast<std::size_t>(z);
    out.w = w;
    out.d = d;
    out.z = z;
    out.occ.assign(n, 1u);
    out.index.assign(n, 0u);
    out.lit.assign(n, 0u);
    carved_transparent = 0;
    carved_bounds = 0;

    ViewBasis views[NUM_VOXEL_FACINGS];
    for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
        views[dir] = basis_for(yaws[static_cast<std::size_t>(dir)]);

    // ---- pass 1: carve ----
    for (int k = 0; k < z; ++k)
        for (int j = 0; j < d; ++j)
            for (int i = 0; i < w; ++i)
            {
                const std::size_t idx = out.at(i, j, k);
                for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
                {
                    if (fr.frame[dir] == nullptr)
                        continue;
                    const Sample s = project_voxel(g, views[dir], g.mx(i),
                                                   g.my(j), CarveGeom::mz(k),
                                                   fr.w, fr.h);
                    if (!s.inside)
                    {
                        out.occ[idx] = 0;
                        ++carved_bounds;
                        break;
                    }
                    if (fr.frame[dir][static_cast<std::size_t>(s.py) *
                                          static_cast<std::size_t>(fr.w) +
                                      static_cast<std::size_t>(s.px)] == 0)
                    {
                        out.occ[idx] = 0;
                        ++carved_transparent;
                        break;
                    }
                }
            }

    // ---- pass 2: which view sees which voxel ----
    // A view sees a voxel when no surviving voxel projects to the same pixel
    // NEARER the camera. One z-buffer per view over the frame.
    const std::size_t frame_px = static_cast<std::size_t>(fr.w) *
                                 static_cast<std::size_t>(fr.h);
    std::vector<float> zbuf(frame_px);
    std::vector<std::size_t> owner(frame_px);
    struct Seeing
    {
        std::array<float, NUM_VOXEL_FACINGS> near{};
        std::array<unsigned char, NUM_VOXEL_FACINGS> idx{};
        int count = 0;
    };
    std::vector<Seeing> seeing(n);
    std::vector<unsigned char> seen(n, 0u);

    for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
    {
        if (fr.frame[dir] == nullptr)
            continue;
        std::fill(zbuf.begin(), zbuf.end(),
                  -std::numeric_limits<float>::infinity());
        std::fill(owner.begin(), owner.end(), static_cast<std::size_t>(-1));
        for (int k = 0; k < z; ++k)
            for (int j = 0; j < d; ++j)
                for (int i = 0; i < w; ++i)
                {
                    const std::size_t idx = out.at(i, j, k);
                    if (out.occ[idx] == 0)
                        continue;
                    const Sample s = project_voxel(g, views[dir], g.mx(i),
                                                   g.my(j), CarveGeom::mz(k),
                                                   fr.w, fr.h);
                    if (!s.inside)
                        continue;
                    const std::size_t p = static_cast<std::size_t>(s.py) *
                                              static_cast<std::size_t>(fr.w) +
                                          static_cast<std::size_t>(s.px);
                    if (s.near <= zbuf[p])
                        continue;
                    zbuf[p] = s.near;
                    owner[p] = idx;
                }
        for (std::size_t p = 0; p < frame_px; ++p)
        {
            if (owner[p] == static_cast<std::size_t>(-1))
                continue;
            Seeing& sg = seeing[owner[p]];
            sg.near[static_cast<std::size_t>(sg.count)] = zbuf[p];
            sg.idx[static_cast<std::size_t>(sg.count)] = fr.frame[dir][p];
            ++sg.count;
            seen[owner[p]] = 1u;
        }
    }

    // Colour: the nearest-depth seeing view wins; when several views tie at
    // that depth, the majority index among the tied views decides (lowest
    // palette index breaks a tied vote, so the carve stays deterministic).
    std::vector<unsigned char> best_index(n, 0u);
    for (std::size_t idx = 0; idx < n; ++idx)
    {
        const Seeing& sg = seeing[idx];
        if (sg.count <= 0)
            continue;
        const std::size_t seen_count = static_cast<std::size_t>(sg.count);
        float top = sg.near[0];
        for (std::size_t e = 1; e < seen_count; ++e)
            top = std::max(top, sg.near[e]);
        unsigned char winner = 0;
        int winner_votes = -1;
        for (std::size_t e = 0; e < seen_count; ++e)
        {
            if (sg.near[e] < top - 1e-4f)
                continue;
            int votes = 0;
            for (std::size_t f = 0; f < seen_count; ++f)
                if (sg.near[f] >= top - 1e-4f && sg.idx[f] == sg.idx[e])
                    ++votes;
            if (votes > winner_votes ||
                (votes == winner_votes && sg.idx[e] < winner))
            {
                winner_votes = votes;
                winner = sg.idx[e];
            }
        }
        best_index[idx] = winner;
    }

    // ---- pass 3: unseen interior voxels take the nearest surface index ----
    std::deque<std::size_t> queue;
    for (int k = 0; k < z; ++k)
        for (int j = 0; j < d; ++j)
            for (int i = 0; i < w; ++i)
            {
                const std::size_t idx = out.at(i, j, k);
                if (out.occ[idx] == 0)
                    continue;
                if (seen[idx])
                {
                    out.index[idx] = best_index[idx];
                    queue.push_back(idx);
                }
            }
    unsigned char fallback = 0;
    if (!queue.empty())
        fallback = out.index[queue.front()];
    while (!queue.empty())
    {
        const std::size_t idx = queue.front();
        queue.pop_front();
        const int k = static_cast<int>(idx / (static_cast<std::size_t>(w) *
                                              static_cast<std::size_t>(d)));
        const std::size_t rem =
            idx % (static_cast<std::size_t>(w) * static_cast<std::size_t>(d));
        const int j = static_cast<int>(rem / static_cast<std::size_t>(w));
        const int i = static_cast<int>(rem % static_cast<std::size_t>(w));
        const int nb[6][3] = {{i - 1, j, k}, {i + 1, j, k}, {i, j - 1, k},
                              {i, j + 1, k}, {i, j, k - 1}, {i, j, k + 1}};
        for (const auto& q : nb)
        {
            if (q[0] < 0 || q[1] < 0 || q[2] < 0 || q[0] >= w || q[1] >= d ||
                q[2] >= z)
                continue;
            const std::size_t nidx = out.at(q[0], q[1], q[2]);
            if (out.occ[nidx] == 0 || seen[nidx])
                continue;
            seen[nidx] = 1u;
            out.index[nidx] = out.index[idx];
            queue.push_back(nidx);
        }
    }
    for (std::size_t idx = 0; idx < n; ++idx)
        if (out.occ[idx] != 0 && !seen[idx])
            out.index[idx] = fallback;

    // ---- pass 4: top-surface flag drives the §4 side shade ----
    for (int k = 0; k < z; ++k)
        for (int j = 0; j < d; ++j)
            for (int i = 0; i < w; ++i)
            {
                const std::size_t idx = out.at(i, j, k);
                if (out.occ[idx] == 0)
                    continue;
                out.lit[idx] = out.solid(i, j, k + 1) ? 0u : 1u;
            }
}

void recompute_lit(VoxelModel& m)
{
    for (int k = 0; k < m.z; ++k)
        for (int j = 0; j < m.d; ++j)
            for (int i = 0; i < m.w; ++i)
            {
                const std::size_t idx = m.at(i, j, k);
                if (m.occ[idx] == 0)
                    continue;
                m.lit[idx] = m.solid(i, j, k + 1) ? 0u : 1u;
            }
}

// Agreement of a re-render against the source frame: matched palette indices
// over the union of opaque pixels. Over-covering costs as much as under.
void score_view(const VoxelModel& m, int dir,
                const std::array<float, NUM_VOXEL_FACINGS>& yaws,
                const VoxelCarveFrames& fr,
                std::vector<unsigned char>& scratch, float& agreement,
                float& iou)
{
    scratch.assign(static_cast<std::size_t>(fr.w) *
                       static_cast<std::size_t>(fr.h),
                   0u);
    voxel_model_render_indices(m, yaws[static_cast<std::size_t>(dir)],
                               m.theta_deg, scratch.data(), fr.w, fr.h,
                               m.anchor_x, m.anchor_y);
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
    (void)both;
}

} // namespace

void voxel_model_render_indices(const VoxelModel& model, float yaw_rad,
                                float theta_deg, unsigned char* out, int out_w,
                                int out_h, float anchor_x, float anchor_y)
{
    if (out == nullptr || out_w <= 0 || out_h <= 0 || model.empty())
        return;
    std::memset(out, 0,
                static_cast<std::size_t>(out_w) *
                    static_cast<std::size_t>(out_h));
    CarveGeom g;
    g.w = model.w;
    g.d = model.d;
    g.z = model.z;
    g.ax = anchor_x;
    g.ay = anchor_y;
    g.st = std::sin(theta_deg * kDeg2Rad);
    g.ct = std::cos(theta_deg * kDeg2Rad);
    const ViewBasis v = basis_for(yaw_rad);

    std::vector<float> zbuf(static_cast<std::size_t>(out_w) *
                                static_cast<std::size_t>(out_h),
                            -std::numeric_limits<float>::infinity());
    for (int k = 0; k < model.z; ++k)
        for (int j = 0; j < model.d; ++j)
            for (int i = 0; i < model.w; ++i)
            {
                const std::size_t idx = model.at(i, j, k);
                if (model.occ[idx] == 0)
                    continue;
                const Sample s = project_voxel(g, v, g.mx(i), g.my(j),
                                               CarveGeom::mz(k), out_w, out_h);
                if (!s.inside)
                    continue;
                const std::size_t p = static_cast<std::size_t>(s.py) *
                                          static_cast<std::size_t>(out_w) +
                                      static_cast<std::size_t>(s.px);
                if (s.near <= zbuf[p])
                    continue;
                zbuf[p] = s.near;
                out[p] = model.index[idx];
            }
}

VoxelCarveReport voxel_carve(const VoxelCarveFrames& frames,
                             const VoxelCarveParams& params)
{
    VoxelCarveReport rep;
    if (frames.w <= 0 || frames.h <= 0)
        return rep;
    const auto t0 = std::chrono::steady_clock::now();

    const float st = std::sin(params.theta_deg * kDeg2Rad);
    const float ct = std::cos(params.theta_deg * kDeg2Rad);

    std::array<float, NUM_VOXEL_FACINGS> yaws{};
    for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
        yaws[static_cast<std::size_t>(dir)] =
            params.custom_yaw ? params.yaw_rad[static_cast<std::size_t>(dir)]
                              : voxel_facing_yaw_rad(dir);

    CarveGeom g;
    g.w = params.grid_w > 0 ? params.grid_w : frames.w;
    g.d = params.grid_d > 0 ? params.grid_d : frames.w;
    if (params.grid_z > 0)
        g.z = params.grid_z;
    else
    {
        const float raw = st > 1e-4f ? static_cast<float>(frames.h) / st
                                     : static_cast<float>(frames.h);
        g.z = std::min(kVoxelCarveMaxZ,
                       std::max(1, static_cast<int>(std::ceil(raw))));
    }
    g.st = st;
    g.ct = ct;

    // The anchor fit. anchor_y is a pure z translation of the hull, so the
    // search is really "where in the grid does the hull sit without being
    // clipped by the floor or the ceiling"; anchor_x is NOT a rigid motion
    // (it shifts x in the up/down views and y in the left/right ones), so it
    // stays near the art's horizontal centre and only jitters half a pixel.
    // Coarse pass on anchor_y alone, then a refinement around the winner.
    std::vector<unsigned char> scratch;
    VoxelModel best;
    float best_score = -1.0f;
    int best_ct_ = 0, best_cb_ = 0;
    float best_ay = static_cast<float>(frames.h);

    const auto try_anchor = [&](float ax, float ay) {
        CarveGeom trial = g;
        trial.ax = ax;
        trial.ay = ay;
        VoxelModel m;
        int c_t = 0, c_b = 0;
        carve_once(frames, trial, yaws, m, c_t, c_b);
        m.anchor_x = ax;
        m.anchor_y = ay;
        m.theta_deg = params.theta_deg;
        float sum = 0.0f;
        for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
        {
            float a = 0.0f, i = 0.0f;
            score_view(m, dir, yaws, frames, scratch, a, i);
            sum += a;
        }
        const float score = sum / static_cast<float>(NUM_VOXEL_FACINGS);
        if (score > best_score)
        {
            best_score = score;
            best = std::move(m);
            best_ct_ = c_t;
            best_cb_ = c_b;
            best_ay = ay;
        }
    };

    const float cx_default = static_cast<float>(frames.w) * 0.5f;
    const float fixed_ax = params.anchor_x >= 0.0f ? params.anchor_x : cx_default;
    if (params.anchor_y >= 0.0f)
    {
        try_anchor(fixed_ax, params.anchor_y);
    }
    else
    {
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

    if (best.empty())
        return rep;

    // Drop the hull onto z = 0 and fold the shift back into the anchor, so a
    // re-render at the model's own anchor still lands on the source frame.
    if (params.normalize_base)
    {
        int lowest = best.z;
        int highest = -1;
        for (int k = 0; k < best.z; ++k)
        {
            bool any = false;
            for (int j = 0; j < best.d && !any; ++j)
                for (int i = 0; i < best.w; ++i)
                    if (best.occ[best.at(i, j, k)] != 0)
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
        if (highest >= 0 && lowest > 0)
        {
            VoxelModel shifted;
            shifted.w = best.w;
            shifted.d = best.d;
            shifted.z = highest - lowest + 1;
            const std::size_t n = static_cast<std::size_t>(shifted.w) *
                                  static_cast<std::size_t>(shifted.d) *
                                  static_cast<std::size_t>(shifted.z);
            shifted.occ.assign(n, 0u);
            shifted.index.assign(n, 0u);
            shifted.lit.assign(n, 0u);
            for (int k = 0; k < shifted.z; ++k)
                for (int j = 0; j < shifted.d; ++j)
                    for (int i = 0; i < shifted.w; ++i)
                    {
                        const std::size_t s = best.at(i, j, k + lowest);
                        const std::size_t t = shifted.at(i, j, k);
                        shifted.occ[t] = best.occ[s];
                        shifted.index[t] = best.index[s];
                    }
            shifted.anchor_x = best.anchor_x;
            // py = ay + ry*sin - mz*cos; lowering every voxel by `lowest`
            // adds lowest*cos to py, so the anchor drops by the same amount.
            shifted.anchor_y =
                best.anchor_y - static_cast<float>(lowest) * ct;
            shifted.theta_deg = best.theta_deg;
            recompute_lit(shifted);
            best = std::move(shifted);
        }
        else if (highest >= 0 && highest + 1 < best.z)
        {
            best.z = highest + 1;
            const std::size_t n = static_cast<std::size_t>(best.w) *
                                  static_cast<std::size_t>(best.d) *
                                  static_cast<std::size_t>(best.z);
            best.occ.resize(n);
            best.index.resize(n);
            best.lit.resize(n);
            recompute_lit(best);
        }
    }

    rep.model = std::move(best);
    rep.carved_by_transparency = best_ct_;
    rep.carved_by_frame_bounds = best_cb_;
    for (unsigned char o : rep.model.occ)
        if (o != 0)
            ++rep.voxel_count;

    float sum_a = 0.0f, sum_i = 0.0f;
    float worst = 2.0f;
    for (int dir = 0; dir < NUM_VOXEL_FACINGS; ++dir)
    {
        score_view(rep.model, dir, yaws, frames, scratch, rep.agreement[dir],
                   rep.silhouette_iou[dir]);
        sum_a += rep.agreement[dir];
        sum_i += rep.silhouette_iou[dir];
        if (rep.agreement[dir] < worst)
        {
            worst = rep.agreement[dir];
            rep.worst_facing = dir;
        }
    }
    rep.agreement_mean = sum_a / static_cast<float>(NUM_VOXEL_FACINGS);
    rep.silhouette_iou_mean = sum_i / static_cast<float>(NUM_VOXEL_FACINGS);

    const auto t1 = std::chrono::steady_clock::now();
    rep.carve_seconds =
        std::chrono::duration<double>(t1 - t0).count();
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
    const std::size_t n = static_cast<std::size_t>(tw) *
                          static_cast<std::size_t>(th) *
                          static_cast<std::size_t>(height);
    m.occ.assign(n, 1u);
    m.index.assign(n, 0u);
    m.lit.assign(n, 0u);
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
                                  int trunk_h, int canopy_h, int trunk_size)
{
    VoxelModel m;
    if (top == nullptr || tw <= 0 || th <= 0 || trunk_h + canopy_h <= 0)
        return m;
    m.w = tw;
    m.d = th;
    m.z = trunk_h + canopy_h;
    const std::size_t n = static_cast<std::size_t>(tw) *
                          static_cast<std::size_t>(th) *
                          static_cast<std::size_t>(m.z);
    m.occ.assign(n, 0u);
    m.index.assign(n, 0u);
    m.lit.assign(n, 0u);
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
                if (k < trunk_h && !in_trunk)
                    continue;
                const std::size_t idx = m.at(i, j, k);
                m.occ[idx] = 1u;
                m.index[idx] = top[static_cast<std::size_t>(j) *
                                       static_cast<std::size_t>(tw) +
                                   static_cast<std::size_t>(i)];
            }
    recompute_lit(m);
    return m;
}

} // namespace og::render
