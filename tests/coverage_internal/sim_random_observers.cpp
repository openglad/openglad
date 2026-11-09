#include <openglad/gameplay/game_world.h>
#include "../../src/gameplay/sim_random_state.h"

#include <utility>

namespace og::sim {

IRandom** set_sim_random_override(IRandom** rng_ref)
{
    return std::exchange(detail::g_sim_random_override_ref, rng_ref);
}

} // namespace og::sim
