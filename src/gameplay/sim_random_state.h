#pragma once

class IRandom;

namespace og::sim::detail {
// The production RNG reads this slot; only test support installs an override.
extern thread_local IRandom** g_sim_random_override_ref;
}
