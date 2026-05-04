#include <openglad/core/frame_rate_config.h>

#include <openglad/core/util.h>
#include <openglad/resources/gparser.h>

#include <algorithm>
#include <string>

namespace og::core {

// Keep in sync with the literal default of SessionState::target_fps_
// (declared in include/openglad/interface/session_state.h). The session
// header intentionally does not include this one to avoid a circular include.
static_assert(kDefaultTargetFps == 72,
    "session_state.h target_fps_ literal default must match kDefaultTargetFps");

int target_fps_from_cfg(cfg_store& cfg)
{
    const std::string raw = cfg.get_setting("graphics", "target_fps");
    const auto parsed = parse_int_strict(raw);
    if (!parsed)
        return kDefaultTargetFps;
    return std::clamp(*parsed, kMinTargetFps, kMaxTargetFps);
}

void apply_target_fps_to_cfg(cfg_store& cfg, int fps)
{
    const int clamped = std::clamp(fps, kMinTargetFps, kMaxTargetFps);
    cfg.apply_setting("graphics", "target_fps", std::to_string(clamped));
}

} // namespace og::core
