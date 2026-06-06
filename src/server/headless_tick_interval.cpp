#include <openglad/server/headless_tick_interval.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/net_constants.h>

#include <algorithm>
#include <cmath>

namespace og::server {

std::uint32_t compute_headless_tick_interval_ms(short timer_wait)
{
    const short tw =
        (timer_wait <= 0) ? og::sim::DEFAULT_TIMER_WAIT : timer_wait;
    const float scaled = og::core::rounded_render_tick_interval_ms(tw, 1.0f);
    return std::max<std::uint32_t>(
        1u, static_cast<std::uint32_t>(std::lround(scaled)));
}

} // namespace og::server
