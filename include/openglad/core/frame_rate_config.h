#pragma once

#include <openglad/core/util.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace og::core {

// Default render-frame rate. 60 FPS matches the common display refresh
// (~16.7 ms per frame). Sim cadence is governed by world.timer_wait
// (master semantics) — target_fps controls how often the render path
// runs, not the gameplay tick rate.
inline constexpr int kDefaultTargetFps = 60;
inline constexpr int kMinTargetFps = 10;
inline constexpr int kMaxTargetFps = 240;

// Sentinel for "no render frame cap": the loop renders on every pass with no
// pacer and vsync off. Any configured value <= 0 means this. Sim cadence is
// unaffected — it still comes solely from world.timer_wait.
inline constexpr int kUncappedTargetFps = 0;

// Templated on the cfg type so this header does not need to include
// <openglad/resources/gparser.h>, which is outside og_core's component
// include sandbox. Callers (glad.cpp, tests) instantiate with cfg_store,
// which is a complete type at the call site.
template <typename Cfg>
[[nodiscard]] int target_fps_from_cfg(Cfg& cfg)
{
    const std::string raw = cfg.get_setting("graphics", "target_fps");
    const auto parsed = parse_int_strict(raw);
    if (!parsed)
        return kDefaultTargetFps;
    if (*parsed <= 0)
        return kUncappedTargetFps;
    return std::clamp(*parsed, kMinTargetFps, kMaxTargetFps);
}

template <typename Cfg>
void apply_target_fps_to_cfg(Cfg& cfg, int fps)
{
    // The sentinel must survive the boot-time read/write round trip: clamping
    // it here would rewrite a user's uncapped setting as kMinTargetFps.
    if (fps <= 0)
    {
        cfg.apply_setting("graphics", "target_fps",
                          std::to_string(kUncappedTargetFps));
        return;
    }
    const int clamped = std::clamp(fps, kMinTargetFps, kMaxTargetFps);
    cfg.apply_setting("graphics", "target_fps", std::to_string(clamped));
}

// Rounds half-up: 60 fps -> 17 ms, 120 fps -> 8 ms, 30 fps -> 33 ms. Always >= 1 ms.
[[nodiscard]] inline std::uint32_t target_frame_interval_ms(int fps)
{
    if (fps <= 0)
        return 1u;
    return std::max<std::uint32_t>(
        1u,
        static_cast<std::uint32_t>((1000 + fps / 2) / fps));
}

} // namespace og::core
