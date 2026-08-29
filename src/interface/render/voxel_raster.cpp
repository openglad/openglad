/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Voxel slice rasterizer (docs/voxel-render-design.md §3). One primitive:
// draw a textured quad at a height with a depth test. The Classic camera
// takes an integer identity path so its output is today's blit loop by
// construction; the Free camera takes the general affine inverse-map path.
//
// SDL-free by contract — the caller hands in an XRGB buffer and a LUT.

#include <openglad/interface/render/voxel_scene.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace og::render {

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

// The team band: 248..255 remaps to teamcolor + (255 - c), exactly as every
// walkputbuffer variant does (video_sdl.cpp:2131). The opaque (tile) material
// is the putbuffer path, which does NO band remap — it is a plain converting
// row copy (video_sdl.cpp:1381), so a tile whose art lands in 248..255 keeps
// its literal palette entry.
inline unsigned char apply_material(unsigned char c, const VoxelMaterial& m)
{
    if (!m.opaque && c > static_cast<unsigned char>(247))
        return static_cast<unsigned char>(m.team_color + (255 - c));
    return c;
}

// XRGB8888 channel shade. The LUT is a packed 8-bit-per-channel value; the
// alpha/x byte is left alone.
inline std::uint32_t shade_xrgb(std::uint32_t c, float f)
{
    const std::uint32_t x = c & 0xFF000000u;
    const std::uint32_t r = (c >> 16) & 0xFFu;
    const std::uint32_t g = (c >> 8) & 0xFFu;
    const std::uint32_t b = c & 0xFFu;
    const auto sc = [f](std::uint32_t v) {
        return static_cast<std::uint32_t>(static_cast<float>(v) * f);
    };
    return x | (sc(r) << 16) | (sc(g) << 8) | sc(b);
}

// One textured slice: an affine quad given by three projected corners
// (origin, +u edge, +v edge), inverse-mapped over its screen bounding box.
// The extruded-texture path and the carved-model path share this body — the
// only difference between them is the sampler, so there is exactly one
// implementation of the slice raster.
template <typename Sampler>
inline void raster_affine_slice(const VoxelProjection& p00,
                                const VoxelProjection& p10,
                                const VoxelProjection& p01, int tex_w,
                                int tex_h, int cx0, int cy0, int cx1, int cy1,
                                const VoxelRenderTarget& target,
                                float* depth_buf, int depth_w,
                                VoxelRasterStats& stats, Sampler&& sample)
{
    const float ax = p10.sx - p00.sx, ay = p10.sy - p00.sy;
    const float bx = p01.sx - p00.sx, by = p01.sy - p00.sy;
    const float det = ax * by - ay * bx;
    if (std::fabs(det) < 1e-6f)
        return; // degenerate (edge-on) slice

    const float p11x = p00.sx + ax + bx;
    const float p11y = p00.sy + ay + by;
    int bx0 = static_cast<int>(std::floor(
        std::min(std::min(p00.sx, p10.sx), std::min(p01.sx, p11x))));
    int bx1 = static_cast<int>(std::ceil(
        std::max(std::max(p00.sx, p10.sx), std::max(p01.sx, p11x))));
    int by0 = static_cast<int>(std::floor(
        std::min(std::min(p00.sy, p10.sy), std::min(p01.sy, p11y))));
    int by1 = static_cast<int>(std::ceil(
        std::max(std::max(p00.sy, p10.sy), std::max(p01.sy, p11y))));
    bx0 = std::max(bx0, cx0);
    by0 = std::max(by0, cy0);
    bx1 = std::min(bx1, cx1);
    by1 = std::min(by1, cy1);
    if (bx0 >= bx1 || by0 >= by1)
        return;
    ++stats.slices;
    std::uint64_t wrote = 0;

    // Screen -> unit quad, then unit -> texel.
    const float inv = 1.0f / det;
    const float iu_x = by * inv, iu_y = -bx * inv;
    const float iv_x = -ay * inv, iv_y = ax * inv;
    // depth is affine in (u, v) too.
    const float d00 = p00.depth;
    const float ddu = p10.depth - d00;
    const float ddv = p01.depth - d00;

    for (int y = by0; y < by1; ++y)
    {
        std::uint32_t* row =
            target.pixels + static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(target.pitch_px);
        float* drow = depth_buf + static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(depth_w);
        const float ry = static_cast<float>(y) + 0.5f - p00.sy;
        for (int x = bx0; x < bx1; ++x)
        {
            const float rx = static_cast<float>(x) + 0.5f - p00.sx;
            const float u = rx * iu_x + ry * iu_y;
            if (u < 0.0f || u >= 1.0f)
                continue;
            const float vv = rx * iv_x + ry * iv_y;
            if (vv < 0.0f || vv >= 1.0f)
                continue;
            const int tx = static_cast<int>(u * static_cast<float>(tex_w));
            const int ty = static_cast<int>(vv * static_cast<float>(tex_h));
            if (tx < 0 || tx >= tex_w || ty < 0 || ty >= tex_h)
                continue;
            ++stats.pixel_samples;
            std::uint32_t color = 0;
            unsigned char pal = 0;
            if (!sample(tx, ty, color, pal))
                continue;
            const float depth = d00 + u * ddu + vv * ddv;
            if (depth < drow[x])
                continue; // keep nearer; ties overwrite
            drow[x] = depth;
            row[x] = color;
            if (target.index_plane != nullptr)
                target.index_plane[static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(target.pitch_px) +
                                   static_cast<std::size_t>(x)] = pal;
            ++stats.pixels_written;
            ++wrote;
        }
    }

    if (wrote != 0)
        return;
    // Sub-pixel quad. A carved model has four cells to the sprite pixel, so at
    // the game camera's 1:1 scale every cube face is a quarter of a pixel
    // across and NO pixel centre lands inside it — the inverse map rejects
    // them all and the figure renders full of holes. Plot the quad's centre
    // instead, which is what a point-sampled face of sub-pixel size means.
    const float qcx = p00.sx + (ax + bx) * 0.5f;
    const float qcy = p00.sy + (ay + by) * 0.5f;
    const int px = static_cast<int>(std::floor(qcx));
    const int py = static_cast<int>(std::floor(qcy));
    if (px < cx0 || py < cy0 || px >= cx1 || py >= cy1)
        return;
    ++stats.pixel_samples;
    std::uint32_t color = 0;
    unsigned char pal = 0;
    if (!sample(tex_w / 2, tex_h / 2, color, pal))
        return;
    const float depth = d00 + (ddu + ddv) * 0.5f;
    float* const drow = depth_buf + static_cast<std::size_t>(py) *
                                        static_cast<std::size_t>(depth_w);
    if (depth < drow[px])
        return;
    drow[px] = depth;
    target.pixels[static_cast<std::size_t>(py) *
                      static_cast<std::size_t>(target.pitch_px) +
                  static_cast<std::size_t>(px)] = color;
    if (target.index_plane != nullptr)
        target.index_plane[static_cast<std::size_t>(py) *
                               static_cast<std::size_t>(target.pitch_px) +
                           static_cast<std::size_t>(px)] = pal;
    ++stats.pixels_written;
}

} // namespace

VoxelProjection VoxelCamera::project(float x, float y, float z) const
{
    VoxelProjection p;
    if (kind == VoxelCameraKind::Classic)
    {
        // walker_draw.cpp:543 / pixie.cpp:117: the world position converts to
        // int first, and the worldz raise is subtracted afterwards as its own
        // truncated int.
        p.sx = static_cast<float>(static_cast<int>(
            x - static_cast<float>(topx) + static_cast<float>(xloc)));
        p.sy = static_cast<float>(static_cast<int>(
                   y - static_cast<float>(topy) + static_cast<float>(yloc)) -
               static_cast<int>(z));
        p.depth = 0.0f; // painter rank, supplied per volume by the raster loop
        return p;
    }

    const float th = yaw_deg * kDeg2Rad;
    const float ph = pitch_deg * kDeg2Rad;
    const float ct = std::cos(th), st = std::sin(th);
    const float cp = std::cos(ph), sp = std::sin(ph);
    const float dx = x - cx;
    const float dy = y - cy;
    const float rx = dx * ct - dy * st;
    const float ry = dx * st + dy * ct;
    p.sx = rx * scale + view_cx;
    p.sy = (ry * sp - z * cp) * scale + view_cy;
    // "near": larger = nearer. The camera sits above the scene AND on the +y
    // side of it (world +y runs down the screen, toward the viewer), so a
    // larger y' is nearer, not farther. §3 of the design doc wrote this as
    // `z*sinφ - y'*cosφ`, which only orders columns correctly for φ > 45 and
    // inverts below it — visible as walls and fighters drawn behind the
    // ground they stand on in the φ=35 spike render. Two points that share a
    // screen row differ by Δy' = Δz·cosφ/sinφ, so the correct near must gain
    // Δz/sinφ across them; y'·cosφ + z·sinφ does, for every φ.
    p.depth = ry * cp + z * sp;
    return p;
}

void VoxelRaster::build_shade_tables(const VoxelRenderTarget& target)
{
    shade_table_.resize(static_cast<std::size_t>(kVoxelShadeLevels) * 256u);
    for (int level = 0; level < kVoxelShadeLevels; ++level)
    {
        const float f = (static_cast<float>(level) + 0.5f) /
            static_cast<float>(kVoxelShadeLevels);
        for (int i = 0; i < 256; ++i)
            shade_table_[static_cast<std::size_t>(level) * 256u +
                         static_cast<std::size_t>(i)] =
                shade_xrgb(target.lut256[i], f);
    }
}

void VoxelRaster::edge_darken(const VoxelRenderTarget& target,
                              float depth_jump, float factor)
{
    if (target.pixels == nullptr || depth_w_ <= 0 || depth_h_ <= 0)
        return;
    const int cx0 = std::max(0, target.clip_x0);
    const int cy0 = std::max(0, target.clip_y0);
    const int cx1 = std::min(target.w, target.clip_x1);
    const int cy1 = std::min(target.h, target.clip_y1);
    if (cx0 >= cx1 || cy0 >= cy1)
        return;
    // Two passes: decide from the untouched depth buffer, then write, so a
    // darkened pixel cannot seed its neighbours.
    std::vector<unsigned char> mark(
        static_cast<std::size_t>(target.w) * static_cast<std::size_t>(target.h),
        0u);
    for (int y = cy0; y < cy1; ++y)
        for (int x = cx0; x < cx1; ++x)
        {
            const std::size_t di = static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(depth_w_) +
                                   static_cast<std::size_t>(x);
            const float d = depth_[di];
            if (d == -std::numeric_limits<float>::infinity())
                continue; // nothing drawn here
            const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
            for (const auto& q : nb)
            {
                if (q[0] < cx0 || q[1] < cy0 || q[0] >= cx1 || q[1] >= cy1)
                    continue;
                const float nd = depth_[static_cast<std::size_t>(q[1]) *
                                            static_cast<std::size_t>(depth_w_) +
                                        static_cast<std::size_t>(q[0])];
                // A neighbour that is much FARTHER (or empty) means this pixel
                // sits on a silhouette against the background behind it.
                if (nd == -std::numeric_limits<float>::infinity() ||
                    nd < d - depth_jump)
                {
                    mark[static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(target.w) +
                         static_cast<std::size_t>(x)] = 1u;
                    break;
                }
            }
        }
    for (int y = cy0; y < cy1; ++y)
        for (int x = cx0; x < cx1; ++x)
        {
            if (mark[static_cast<std::size_t>(y) *
                         static_cast<std::size_t>(target.w) +
                     static_cast<std::size_t>(x)] == 0u)
                continue;
            std::uint32_t* const px =
                target.pixels + static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(target.pitch_px) +
                static_cast<std::size_t>(x);
            *px = shade_xrgb(*px, factor);
        }
}

void VoxelRaster::reset_depth(int w, int h)
{
    const std::size_t need =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    if (depth_.size() < need)
        depth_.resize(need);
    depth_w_ = w;
    depth_h_ = h;
    std::fill(depth_.begin(), depth_.begin() + static_cast<std::ptrdiff_t>(need),
              -std::numeric_limits<float>::infinity());
}

VoxelRasterStats VoxelRaster::render(const VoxelScene& scene,
                                     const VoxelCamera& camera,
                                     const VoxelRenderTarget& target)
{
    VoxelRasterStats stats;
    if (target.pixels == nullptr || target.lut256 == nullptr ||
        target.w <= 0 || target.h <= 0)
    {
        return stats;
    }

    const int cx0 = std::max(0, target.clip_x0);
    const int cy0 = std::max(0, target.clip_y0);
    const int cx1 = std::min(target.w, target.clip_x1);
    const int cy1 = std::min(target.h, target.clip_y1);
    if (cx0 >= cx1 || cy0 >= cy1)
        return stats;

    reset_depth(target.w, target.h);

    const bool collapse = camera.collapse();
    if (!collapse)
    {
        shaded_lut_.resize(256);
        for (int i = 0; i < 256; ++i)
            shaded_lut_[static_cast<std::size_t>(i)] =
                shade_xrgb(target.lut256[i], kVoxelSideShade);
        build_shade_tables(target);
    }

    // A cube face is one flat colour, so the face shade x AO product collapses
    // to a table lookup instead of three float multiplies per pixel.
    const auto shade_lookup = [this](float f, unsigned char pal) {
        int level =
            static_cast<int>(f * static_cast<float>(kVoxelShadeLevels));
        level = std::clamp(level, 0, kVoxelShadeLevels - 1);
        return shade_table_[static_cast<std::size_t>(level) * 256u +
                            static_cast<std::size_t>(pal)];
    };


    for (const VoxelVolume& v : scene.volumes())
    {
        ++stats.volumes;
        // A model-only volume carries no texture: the hero and comparison
        // renders emit one, and terrain/sprite volumes still carry both.
        const bool has_model = (v.model != nullptr && !v.model->empty());
        if (!has_model && (v.texels == nullptr || v.w <= 0 || v.h <= 0))
            continue;
        if (has_model && camera.collapse() &&
            (v.texels == nullptr || v.w <= 0 || v.h <= 0))
            continue; // Classic draws the sprite imposter or nothing at all

        const int top_slice = collapse ? 0 : std::max(0, v.height);
        const float base_z =
            v.z + ((!collapse && v.material.lift) ? kVoxelProjectileLift : 0.0f);

        // ---- §10: a carved model, rotated by the walker's facing ----
        // Classic ignores models by ruling: the sprite frames ARE the baked
        // view of the model from the game camera, so drawing the model there
        // would be a renderer twin of an imposter that is already exact.
        if (!collapse && has_model)
        {
            const VoxelModel& m = *v.model;
            const float c = m.cell;
            const float hw = m.extent_x() * 0.5f;
            const float hd = m.extent_y() * 0.5f;
            // The model's anchor is its footprint centre expressed in the
            // volume's own texel box, so the volume position stays exactly
            // what the Classic camera wants: the sprite/tile top-left.
            const float ccx = v.x + m.anchor_x;
            const float ccy = v.y + m.anchor_y;
            const float cyaw = std::cos(v.yaw), syaw = std::sin(v.yaw);
            const VoxelMaterial mat = v.material;

            // Model-local (lx, ly) -> world, through the volume's own yaw.
            // Same matrix as the camera's, so the two simply compose.
            const auto wx = [&](float lx, float ly) {
                return ccx + lx * cyaw - ly * syaw;
            };
            const auto wy = [&](float lx, float ly) {
                return ccy + lx * syaw + ly * cyaw;
            };

            if (!m.cube_faces)
            {
                // Terrain models keep the stacked-slice look they were built
                // for: one textured quad per layer, no per-voxel faces.
                for (int k = 0; k < m.z; ++k)
                {
                    const float slice_z = base_z + static_cast<float>(k) * c;
                    const VoxelProjection q00 =
                        camera.project(wx(-hw, -hd), wy(-hw, -hd), slice_z);
                    const VoxelProjection q10 =
                        camera.project(wx(hw, -hd), wy(hw, -hd), slice_z);
                    const VoxelProjection q01 =
                        camera.project(wx(-hw, hd), wy(-hw, hd), slice_z);
                    const std::size_t layer = static_cast<std::size_t>(k) *
                                              static_cast<std::size_t>(m.w) *
                                              static_cast<std::size_t>(m.d);
                    raster_affine_slice(
                        q00, q10, q01, m.w, m.d, cx0, cy0, cx1, cy1, target,
                        depth_.data(), depth_w_, stats,
                        [&](int tx, int ty, std::uint32_t& color,
                            unsigned char& pal) {
                            const std::size_t idx =
                                layer + static_cast<std::size_t>(ty) *
                                            static_cast<std::size_t>(m.w) +
                                static_cast<std::size_t>(tx);
                            if (m.occ[idx] == 0)
                                return false;
                            pal = apply_material(m.index[idx], mat);
                            color = (m.lit[idx] != 0)
                                ? target.lut256[pal]
                                : shaded_lut_[pal];
                            return true;
                        });
                }
                continue;
            }

            // ---- cube faces ----
            // Each surface voxel shows at most three faces: its top, and the
            // two vertical sides whose outward normal has a component toward
            // the viewer. Which two those are follows from the total turn the
            // model has taken on screen — the volume's own yaw composed with
            // the camera's — so it is decided once per volume, not per voxel.
            const float total_yaw =
                v.yaw + camera.yaw_deg * kDeg2Rad;
            const float tsin = std::sin(total_yaw);
            const float tcos = std::cos(total_yaw);
            // World +y runs toward the viewer, so a face is camera-facing
            // when its screen-space normal has a positive +y component.
            const int xdir = (tsin > 0.0f) ? 1 : -1;
            const int ydir = (tcos > 0.0f) ? 1 : -1;
            const float x_face_shade =
                (xdir > 0) ? kVoxelFaceSun : kVoxelFaceShadow;
            const float y_face_shade =
                (ydir > 0) ? kVoxelFaceShadow : kVoxelFaceSun;

            for (int k = 0; k < m.z; ++k)
                for (int j = 0; j < m.d; ++j)
                    for (int i = 0; i < m.w; ++i)
                    {
                        const std::size_t idx = m.at(i, j, k);
                        if (m.occ[idx] == 0)
                            continue;
                        const bool show_top = !m.solid(i, j, k + 1);
                        const bool show_x = !m.solid(i + xdir, j, k);
                        const bool show_y = !m.solid(i, j + ydir, k);
                        if (!show_top && !show_x && !show_y)
                            continue;

                        const unsigned char pal =
                            apply_material(m.index[idx], mat);
                        const float ao = m.shade.empty()
                            ? 1.0f
                            : (kVoxelAoFloor +
                               (1.0f - kVoxelAoFloor) *
                                   static_cast<float>(m.shade[idx]) / 255.0f);

                        const float lx0 = (static_cast<float>(i) * c) - hw;
                        const float ly0 = (static_cast<float>(j) * c) - hd;
                        const float lx1 = lx0 + c;
                        const float ly1 = ly0 + c;
                        const float z0 = base_z + static_cast<float>(k) * c;
                        const float z1 = z0 + c;
                        ++stats.slices;

                        const auto face = [&](float ax0, float ay0, float az0,
                                              float ux, float uy, float uz,
                                              float vx2, float vy2, float vz2,
                                              float shade) {
                            const VoxelProjection q00 =
                                camera.project(wx(ax0, ay0), wy(ax0, ay0), az0);
                            const VoxelProjection q10 = camera.project(
                                wx(ax0 + ux, ay0 + uy), wy(ax0 + ux, ay0 + uy),
                                az0 + uz);
                            const VoxelProjection q01 = camera.project(
                                wx(ax0 + vx2, ay0 + vy2),
                                wy(ax0 + vx2, ay0 + vy2), az0 + vz2);
                            const std::uint32_t colour =
                                shade_lookup(shade * ao, pal);
                            raster_affine_slice(
                                q00, q10, q01, 1, 1, cx0, cy0, cx1, cy1, target,
                                depth_.data(), depth_w_, stats,
                                [&](int, int, std::uint32_t& out_c,
                                    unsigned char& out_p) {
                                    out_c = colour;
                                    out_p = pal;
                                    return true;
                                });
                        };

                        if (show_top)
                            face(lx0, ly0, z1, c, 0.0f, 0.0f, 0.0f, c, 0.0f,
                                 kVoxelFaceTop);
                        if (show_x)
                        {
                            const float xf = (xdir > 0) ? lx1 : lx0;
                            face(xf, ly0, z0, 0.0f, c, 0.0f, 0.0f, 0.0f, c,
                                 x_face_shade);
                        }
                        if (show_y)
                        {
                            const float yf = (ydir > 0) ? ly1 : ly0;
                            face(lx0, yf, z0, c, 0.0f, 0.0f, 0.0f, 0.0f, c,
                                 y_face_shade);
                        }
                    }
            continue;
        }

        for (int s = 0; s <= top_slice; ++s)
        {
            const float slice_z = base_z + static_cast<float>(s);
            const bool is_top = (s == top_slice);

            if (collapse)
            {
                // ---- Classic: exact integer identity ----
                // sx/sy from the camera, then texel (px, py) lands on
                // (sx + px, sy + py). No float inverse map, no rounding drift.
                const VoxelProjection p = camera.project(v.x, v.y, v.z);
                const int sx = static_cast<int>(p.sx);
                const int sy = static_cast<int>(p.sy);
                int px0 = std::max(0, cx0 - sx);
                int px1 = std::min(v.w, cx1 - sx);
                int py0 = std::max(0, cy0 - sy);
                int py1 = std::min(v.h, cy1 - sy);
                if (px0 >= px1 || py0 >= py1)
                    continue;
                ++stats.slices;
                const float depth = static_cast<float>(v.rank);
                for (int py = py0; py < py1; ++py)
                {
                    const int dy = sy + py;
                    const unsigned char* src =
                        v.texels + static_cast<std::size_t>(py) *
                                       static_cast<std::size_t>(v.w);
                    std::uint32_t* row =
                        target.pixels + static_cast<std::size_t>(dy) *
                                            static_cast<std::size_t>(target.pitch_px);
                    float* drow = depth_.data() +
                                  static_cast<std::size_t>(dy) *
                                      static_cast<std::size_t>(depth_w_);
                    for (int px = px0; px < px1; ++px)
                    {
                        ++stats.pixel_samples;
                        unsigned char c = src[px];
                        if (c == 0 && !v.material.opaque)
                            continue;
                        c = apply_material(c, v.material);
                        const int dx = sx + px;
                        if (depth < drow[dx])
                            continue; // ties overwrite: list order wins
                        drow[dx] = depth;
                        row[dx] = target.lut256[c];
                        if (target.index_plane != nullptr)
                            target.index_plane[static_cast<std::size_t>(dy) *
                                                   static_cast<std::size_t>(
                                                       target.pitch_px) +
                                               static_cast<std::size_t>(dx)] = c;
                        ++stats.pixels_written;
                    }
                }
                continue;
            }

            // ---- Free: affine quad, screen bbox, inverse map ----
            const VoxelProjection p00 = camera.project(v.x, v.y, slice_z);
            const VoxelProjection p10 =
                camera.project(v.x + static_cast<float>(v.w), v.y, slice_z);
            const VoxelProjection p01 =
                camera.project(v.x, v.y + static_cast<float>(v.h), slice_z);
            const std::uint32_t* lut =
                is_top ? target.lut256 : shaded_lut_.data();
            const VoxelMaterial mat = v.material;
            const unsigned char* texels = v.texels;
            const int tw = v.w;
            raster_affine_slice(
                p00, p10, p01, v.w, v.h, cx0, cy0, cx1, cy1, target,
                depth_.data(), depth_w_, stats,
                [&](int tx, int ty, std::uint32_t& color, unsigned char& pal) {
                    unsigned char c =
                        texels[static_cast<std::size_t>(ty) *
                                   static_cast<std::size_t>(tw) +
                               static_cast<std::size_t>(tx)];
                    if (c == 0 && !mat.opaque)
                        return false;
                    c = apply_material(c, mat);
                    color = lut[c];
                    pal = c;
                    return true;
                });
        }
    }

    return stats;
}

} // namespace og::render
