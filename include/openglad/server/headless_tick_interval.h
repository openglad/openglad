#pragma once
#include <cstdint>

namespace og::server {

std::uint32_t compute_headless_tick_interval_ms(short timer_wait);

} // namespace og::server
