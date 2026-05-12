// Parity scenario runner.
//
// Owns the deterministic harness driving an og::sim::GameWorld through a
// fixed input script and tick budget, then returns a canonical StateDump.
// Branch-side; the Phase 05 master companion implements the same surface
// against the master worktree.

#pragma once

#include "scenario_table.h"
#include "state_dump.h"

namespace og::parity {

struct RunOutcome
{
    StateDump     dump;
    bool          loaded         = false; // scenario file was found and read
    bool          ticked         = false; // tick loop ran without crashing
    std::uint32_t early_stop_tick = 0;    // first tick at which level_done == 1
    bool          early_stopped  = false;
};

// Construct a fresh world, seed RNG to spec.rng_seed, optionally load
// spec.scenario_file, drive spec.tick_budget invocations of world.tick()
// applying spec.inputs at matching ticks, then dump.
RunOutcome run_scenario(const ScenarioSpec& spec);

} // namespace og::parity
