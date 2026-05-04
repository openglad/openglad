#pragma once

#include <openglad/core/util.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace og::core {

inline constexpr int kDefaultTargetFps = 72;
inline constexpr int kMinTargetFps = 10;
inline constexpr int kMaxTargetFps = 240;

// Templated on the cfg type so this header does not need to include
// <openglad/resources/gparser.h>, which is outside og_core's component
// include sandbox. Callers (glad.cpp, tests) instantiate with cfg_store,
// which is a complete type at the call site.
template <typename Cfg>
int target_fps_from_cfg(Cfg& cfg)
{
    const std::string raw = cfg.get_setting("graphics", "target_fps");
    const auto parsed = parse_int_strict(raw);
    if (!parsed)
        return kDefaultTargetFps;
    return std::clamp(*parsed, kMinTargetFps, kMaxTargetFps);
}

template <typename Cfg>
void apply_target_fps_to_cfg(Cfg& cfg, int fps)
{
    const int clamped = std::clamp(fps, kMinTargetFps, kMaxTargetFps);
    cfg.apply_setting("graphics", "target_fps", std::to_string(clamped));
}

// Rounds half-up: 72 fps -> 14 ms, 60 fps -> 17 ms. Always >= 1 ms.
inline std::uint32_t target_frame_interval_ms(int fps)
{
    if (fps <= 0)
        return 1u;
    return std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>((1000 + fps / 2) / fps));
}

} // namespace og::core
