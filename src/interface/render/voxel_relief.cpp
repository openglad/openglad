/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Per-facing sprite reliefs (docs/voxel-render-design.md §12).

#include <openglad/interface/render/voxel_relief.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace og::render {

int voxel_relief_max_depth(Order order) noexcept
{
    switch (order)
    {
    case Order::Living:
        return kVoxelReliefDepthLiving;
    case Order::Generator:
        return kVoxelReliefDepthGenerator;
    case Order::Treasure:
        return kVoxelReliefDepthTreasure;
    case Order::Weapon:
        return kVoxelReliefDepthWeapon;
    case Order::FX:
        return kVoxelReliefDepthFx;
    default:
        return kVoxelReliefDepthTreasure;
    }
}

VoxelRelief voxel_build_relief(const unsigned char* frame, int w, int h,
                               int max_depth, float theta_deg)
{
    VoxelRelief r;
    if (frame == nullptr || w <= 0 || h <= 0)
        return r;
    max_depth = std::max(1, max_depth);
    r.w = w;
    r.h = h;
    r.depth = max_depth;
    r.theta_deg = theta_deg;
    const std::size_t n =
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    r.index.assign(n, 0u);
    r.thick.assign(n, 0u);
    for (std::size_t i = 0; i < n; ++i)
        r.index[i] = frame[i];

    // Euclidean distance to the nearest transparent pixel, counting anything
    // off the frame as transparent. Frames are at most a few hundred pixels,
    // so the exact brute-force answer is cheaper than getting an approximation
    // right.
    for (int v = 0; v < h; ++v)
        for (int u = 0; u < w; ++u)
        {
            const std::size_t s = r.at(u, v);
            if (r.index[s] == 0)
                continue;
            // The frame border is the cheapest escape in most cases.
            float best = static_cast<float>(
                std::min(std::min(u + 1, w - u), std::min(v + 1, h - v)));
            for (int qv = 0; qv < h; ++qv)
                for (int qu = 0; qu < w; ++qu)
                {
                    if (r.index[r.at(qu, qv)] != 0)
                        continue;
                    const float du = static_cast<float>(u - qu);
                    const float dv = static_cast<float>(v - qv);
                    const float d = std::sqrt(du * du + dv * dv);
                    if (d < best)
                        best = d;
                }
            const int t = static_cast<int>(
                std::lround(kVoxelReliefK * best));
            r.thick[s] = static_cast<unsigned char>(
                std::clamp(t, 1, max_depth));
        }

    // Shade: the front face is never touched. Behind it, a layer that has
    // nothing above it in the plane is an upward-facing step and catches more
    // light than a flank.
    r.shade.assign(n * static_cast<std::size_t>(max_depth), 255u);
    for (int t = 1; t < max_depth; ++t)
        for (int v = 0; v < h; ++v)
            for (int u = 0; u < w; ++u)
            {
                const std::size_t s = r.at(u, v);
                if (r.thick[s] <= t)
                    continue;
                const bool open_above =
                    (v == 0) || (r.thick[r.at(u, v - 1)] <= t);
                const float f =
                    open_above ? kVoxelReliefTop : kVoxelReliefSide;
                r.shade[r.at(u, v, t)] =
                    static_cast<unsigned char>(std::lround(f * 255.0f));
            }
    return r;
}

const VoxelRelief* VoxelReliefCache::get(const unsigned char* frame, int w,
                                         int h, int max_depth)
{
    if (frame == nullptr)
        return nullptr;
    const auto it = cache_.find(frame);
    if (it != cache_.end())
        return &it->second;
    VoxelRelief r = voxel_build_relief(frame, w, h, max_depth, theta_deg_);
    if (r.empty())
        return nullptr;
    return &cache_.emplace(frame, std::move(r)).first->second;
}

} // namespace og::render
