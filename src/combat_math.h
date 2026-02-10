/*
 * Pure-ish combat math helpers extracted for unit testing.
 *
 * Keep this module free of `myscreen`/rendering/FX spawning. Randomness is
 * injected so tests can be deterministic.
 */
#pragma once

#include "SDL.h"

class IRandom;

using RandomU32 = Uint32(*)(Uint32);

// Original behavior from walker.cpp:
//   d - sqrt(d)/2 + random(floor(sqrt(d)))
float compute_base_damage(float base_damage, RandomU32 rng);

// Original behavior from walker.cpp:
//   reduction = armor/2, clamped to at most (damage - 1) so at least 1 gets through.
float compute_damage_reduction(float incoming_damage, float target_armor);

// Convenience helper.
float compute_post_reduction_damage(float incoming_damage, float target_armor);

// IRandom-based overload: allows injection via GameContext's RNG
float compute_base_damage(float base_damage, IRandom& rng);

