// Parity scenario runner.
//
// Owns the deterministic harness driving an og::sim::GameWorld through a
// fixed input script and tick budget, then returns a canonical StateDump.
// Branch-side; the Phase 05 master companion implements the same surface
// against the master worktree.

#pragma once

#include "scenario_table.h"
#include "state_dump.h"

#include <cstdint>
#include <set>
#include <string>

namespace og::parity {

// Observation sets collected by walking the live GameWorld at end-of-run.
// These exist because the canonical state dump only emits oblist (mixed
// living + treasure + generator with a living-name-only family symbol)
// and fxlist; weaplist isn't dumped, and order is not serialised.
// Phase 03's coverage gate needs to know which weapon/treasure/generator
// families a scenario actually instantiated, so the runner keeps a
// per-Order family bag here, separate from the schema-v1 dump.
struct CoverageObservation
{
    std::set<std::int32_t> walker_families;     // Order::Living   (oblist)
    std::set<std::int32_t> weapon_families;     // Order::Weapon   (weaplist)
    std::set<std::int32_t> treasure_families;   // Order::Treasure (oblist)
    std::set<std::int32_t> generator_families;  // Order::Generator (oblist)
    std::set<std::int32_t> effect_families;     // Order::FX       (fxlist)
    std::set<std::string>  event_kinds;         // canonical event_kind_symbol
};

struct RunOutcome
{
    StateDump           dump;
    CoverageObservation coverage;
    bool                loaded          = false; // scenario file was found and read
    bool                ticked          = false; // tick loop ran without crashing
    std::uint32_t       early_stop_tick = 0;     // first tick at which level_done == 1
    bool                early_stopped   = false;
};

// Construct a fresh world, seed RNG to spec.rng_seed, optionally load
// spec.scenario_file, drive spec.tick_budget invocations of world.tick()
// applying spec.inputs at matching ticks, then dump.
RunOutcome run_scenario(const ScenarioSpec& spec);

} // namespace og::parity
