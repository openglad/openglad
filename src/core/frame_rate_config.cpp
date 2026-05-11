#include <openglad/core/frame_rate_config.h>

namespace og::core {

// Keep this in sync with the literal default of SessionState::target_fps_
// in include/openglad/interface/session_state.h. The session header lives
// outside og_core's component include sandbox, so we cannot reference its
// field directly here; we anchor on the literal default instead. The
// default is the *render*-frame rate; sim cadence is independent and
// governed by world.timer_wait (master semantics).
static_assert(kDefaultTargetFps == 60,
    "session_state.h target_fps_ literal default must match kDefaultTargetFps (60)");

} // namespace og::core
