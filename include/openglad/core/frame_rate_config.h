#pragma once

#include <algorithm>
#include <cstdint>

class cfg_store;

namespace og::core {

inline constexpr int kDefaultTargetFps = 72;
inline constexpr int kMinTargetFps = 10;
inline constexpr int kMaxTargetFps = 240;

int target_fps_from_cfg(cfg_store& cfg);
void apply_target_fps_to_cfg(cfg_store& cfg, int fps);

inline std::uint32_t target_frame_interval_ms(int fps)
{
    if (fps <= 0)
        return 1u;
    return std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>((1000 + fps / 2) / fps));
}

} // namespace og::core
