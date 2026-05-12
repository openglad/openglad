#include "parity_runner.h"

#include <openglad/gameplay/game_world.h>

namespace og::parity {

namespace {

void apply_inputs_at_tick(const ScenarioSpec& spec, std::uint32_t tick)
{
    // Phase 04 skeleton: scenario input scripts are declared in the table
    // but not yet routed into og::sim::GameWorld. The branch's input plumbing
    // flows through GameSession/local_transport_shadow rather than a single
    // public field on GameWorld, so wiring is deferred to Phase 06/07 when
    // the master companion is also exercised. We still iterate the script
    // so any future hook attaches in the canonical location.
    (void)spec;
    (void)tick;
}

} // namespace

RunOutcome run_scenario(const ScenarioSpec& spec)
{
    RunOutcome out;

    // Phase 04 skeleton: scaffolds the canonical drive loop. Scenario files
    // referenced by spec.scenario_file are not yet loaded here — that is the
    // Phase 06 task. The runner therefore exercises an empty GameWorld, which
    // is sufficient to validate determinism plumbing (seed → rng_state → dump)
    // without depending on the level loader or PhysFS mount layout.
    GameWorld world(spec.rng_seed);
    world.rng_.state_ = spec.rng_seed;
    out.loaded = false;

    for (std::uint32_t t = 0; t < spec.tick_budget; ++t)
    {
        apply_inputs_at_tick(spec, t);
        world.tick();
        if (world.level_done != 0 && !out.early_stopped)
        {
            out.early_stopped   = true;
            out.early_stop_tick = world.tick_count_;
            break;
        }
    }
    out.ticked = true;
    out.dump   = capture_state_dump(world, nullptr);
    return out;
}

} // namespace og::parity
