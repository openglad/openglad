#include <openglad/core/frame_rate_config.h>

namespace og::core {

// Keep this in sync with the literal default of SessionState::target_fps_
// in include/openglad/interface/session_state.h. The session header lives
// outside og_core's component include sandbox, so we cannot reference its
// field directly here; we anchor on the literal 12 instead. 12 fps matches
// master's effective sim cadence (timer_wait=6 * 13.6 ms ≈ 82 ms per frame).
static_assert(kDefaultTargetFps == 12,
    "session_state.h target_fps_ literal default must match kDefaultTargetFps");

} // namespace og::core
