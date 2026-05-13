// Parity scenario runtime helpers — branch side.
//
// Apply post-load spawns, scripted input scripts, and arena-resets to a
// freshly-loaded GameWorld. The master companion has its own mirror at
// `../openglad-master/tools/parity_scenario_runtime.{h,cpp}`; the master
// file reads walker state through public fields, while this file uses
// accessors. Scripted inputs reach the simulation via the real
// `PlayerInput` / `sim_input_handler::sim_process_player_input` pipeline,
// not by reaching into the world from the harness.

#pragma once

#include "scenario_table.h"

#include <openglad/gameplay/sim_input_handler.h>

#include <cstdint>
#include <string_view>
#include <vector>

class GameWorld;
class walker;
namespace og::sim { class SimEventLog; }

namespace og::parity {

// Persistent per-player driver state held across tick invocations: the
// edge-detection cache for `pressed` derivation, the controlled walker,
// and the per-player `SimInputDebounce` consumed by sim_process_player_input.
struct ScenarioInputDriver
{
    walker*          control     = nullptr;
    SimInputDebounce debounce    = {};
    std::uint32_t    held_mask   = 0;  // bits currently held (1 = down)
    std::uint32_t    prev_mask   = 0;  // bits held on the previous tick
    bool             initialised = false;
};

// Parse the integer N from a path like "scen/scenN.fss" or
// "temp/scen/scen99.fss". Returns -1 if the basename does not match.
int scenario_level_id(std::string_view scenario_file);

// Apply every entry in `spec.spawns[]` to `world` via `world.add_ob`,
// placing each walker at (x, y) with the requested team and weapon.
// Returns the pointers in spec-order; the parity dumper uses this set
// as a whitelist so AI- and generator-spawned children (which spawn
// at different cadences on branch vs master) are excluded from the
// dump.
std::vector<const walker*>
apply_post_load_spawns(GameWorld& world, const ScenarioSpec& spec);

// Drive scripted inputs at `tick` through the real
// `sim_process_player_input` pipeline. The driver tracks edge transitions
// (`pressed` vs `held`) across calls, picks up the player walker the
// first time it is invoked, and forwards the resulting `PlayerInput` to
// the simulator. No walker fields are mutated by the harness — every
// state change is the gameplay code reacting to the input.
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
