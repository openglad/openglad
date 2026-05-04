#include <openglad/core/frame_rate_config.h>

namespace og::core {

// Keep this in sync with the literal default of SessionState::target_fps_
// in include/openglad/interface/session_state.h. The session header lives
// outside og_core's component include sandbox, so we cannot reference its
// field directly here; we anchor on the literal 72 instead.
static_assert(kDefaultTargetFps == 72,
    "session_state.h target_fps_ literal default must match kDefaultTargetFps");

} // namespace og::core
