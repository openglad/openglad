/* Capture-the-flag AI director.
 *
 * Cadence-gated role assignment for AI livings (carrier, interceptor,
 * defender, attacker). The role logic lands with the dedicated AI phase;
 * the entry point is wired into ctf_run_tick now so the cadence and the
 * player-untouched guarantees can be locked in early.
 */

#include <openglad/gameplay/ctf/ctf_state.h>

namespace og::sim {

void ctf_run_ai_director(GameWorld& world)
{
    (void)world;
}

} // namespace og::sim
