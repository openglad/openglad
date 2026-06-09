// Parity scenario runtime helpers — branch side.
//
// Apply post-load spawns, scripted input scripts, and arena-resets to a
// freshly-loaded GameWorld. The master companion has its own mirror at
// `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`; the master
// file reads walker state through public fields, while this file uses
// accessors. Scripted inputs mirror the e761 recorder shim directly so
// parity captures compare against the same control mutations.

#pragma once

#include "scenario_table.h"

#include <cstdint>
#include <string_view>

class GameWorld;
class walker;
namespace og::sim { class SimEventLog; }

namespace og::parity {

// Persistent per-player driver state held across tick invocations: the
// edge-detection cache for pressed-vs-held input and the controlled walker.
struct ScenarioInputDriver
{
    walker*          control     = nullptr;
    std::uint32_t    held_mask   = 0;  // bits currently held (1 = down)
    std::uint32_t    prev_mask   = 0;  // bits held on the previous tick
    bool             initialised = false;
};

// Parse the integer N from a path like "scen/scenN.fss" or
// "temp/scen/scen99.fss". Returns -1 if the basename does not match.
int scenario_level_id(std::string_view scenario_file);

// Apply every entry in `spec.spawns[]` to `world` via `world.add_ob`,
// placing each walker at (x, y) with the requested team and weapon.
void apply_post_load_spawns(GameWorld& world, const ScenarioSpec& spec);

// Drive scripted inputs at `tick` through the same direct shim used by the
// e761 golden recorder. The driver tracks edge transitions (`pressed` vs
// `held`) across calls and picks up the player walker the first time it is
// invoked.
void apply_inputs_at_tick(GameWorld& world,
                          const ScenarioSpec& spec,
                          std::uint32_t tick,
                          ScenarioInputDriver& driver,
                          og::sim::SimEventLog* sim_events = nullptr);

// Drop every loaded entity from `world`. Used when `spec.fresh_arena` is
// set: a real scen file is still loaded so the grid / pixmaxx / level
// metadata are valid, but the actual walker / weapon / effect populations
// are replaced by `apply_post_load_spawns`.
void clear_world_entities(GameWorld& world);

} // namespace og::parity
