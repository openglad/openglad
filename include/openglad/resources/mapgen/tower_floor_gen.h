/* Tower Climb floor generator (og::tower) — tower-triple spec §5.4/§5.6.
 *
 * Builds "The Endless Tower" floors over the og::mapgen builder library:
 * band themes cycling every 30 floors, seven carve templates, a foe ramp
 * that outgrows the player curve, a treasure economy, and the full audit
 * set (footing, stair alignment + clearance, fall lines + depth <= 4,
 * A*-reachability, MAXOBS worst-case, title/briefing budgets). Audit-fail
 * rerolls salt the floor seed +1..+3 deterministically; the 4th attempt
 * carves template T0 (the walled arena, audit-clean by construction) — so
 * GENERATION can never fail a run; only the file WRITE can, and the
 * progression seam holds the cursor in that case.
 *
 * Determinism contract (§5.4, pinned by the generate-twice byte-compare
 * test): (run_seed, N) -> byte-identical .fss + PNG bytes. All randomness
 * is either og::mapgen::position_hash streams or a local splitmix64
 * counter stream seeded from floor_seed; the genre smoother runs on the
 * scratch world's own SimRandom (seeded from the attempt seed). No libc
 * rand(), no ctx().rng, no live-world rng_ — this contract IS the future
 * MP door.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

class GameWorld;

namespace og::tower {

// §5.4 seed policy: floor N of a run regenerates from this and nothing else.
// floor_seed(run_seed, N) = uint32(splitmix64(run_seed ^ (N * golden gamma))).
[[nodiscard]] std::uint32_t floor_seed(std::uint32_t run_seed,
                                       int floor_number);

// §5.6 ramp knobs, exposed pure for tests and calibration sweeps.
[[nodiscard]] int foe_level_for_floor(int floor_number);   // 1+(f-1)/3, cap 50
[[nodiscard]] int foe_count_for_floor(int floor_number);   // min(7+f, 30)
[[nodiscard]] int elite_slots_for_floor(int floor_number); // f/5 (+lap share)
[[nodiscard]] bool is_boss_floor(int floor_number);        // f % 5 == 0
[[nodiscard]] int band_index_for_floor(int floor_number);  // 0..5 in the cycle
[[nodiscard]] int lap_for_floor(int floor_number);         // (f-1)/30

struct TowerFloorReport
{
    int floor_number = 0;
    std::uint32_t run_seed = 0;
    int attempts = 1;           // 1..4 (4 = the T0 fallback build)
    bool used_fallback = false; // 4th attempt: template T0
    bool written = false;       // save_level_to_user_dir succeeded
    // The accepted build's audit failures. Empty for attempts 1-3 (those are
    // only accepted clean); the T0 fallback is accepted unconditionally and
    // reports whatever remains (expected: nothing).
    std::vector<std::string> audit_failures;
};

// Build floor N of the run keyed by `run_seed` into `world` + `description`
// WITHOUT writing files (test/audit entry). `world` must be a fresh scratch
// GameWorld — the builder seeds world.rng_ itself from the attempt seed.
// attempt 0..2 = the salted tries; attempt 3 = the T0 arena fallback.
// Returns that build's audit failure list (empty = clean).
std::vector<std::string> build_tower_floor(GameWorld& world,
                                           std::list<std::string>& description,
                                           std::uint32_t run_seed,
                                           int floor_number,
                                           int attempt);

// The production entry (D7/D8: prefetch at GO, direct user_path writes):
// reroll loop over build_tower_floor, then save_level_to_user_dir under
// scen id kTowerGateLevel + N.
TowerFloorReport generate_tower_floor_to_user_dir(std::uint32_t run_seed,
                                                  int floor_number);

} // namespace og::tower

// --- TowerProgression surface (defined in src/resources/tower_progression.cpp).
// Declared here because WP-5 owns no separate tower_progression.h: the
// dispatch switch in game_mode.cpp names tower_progression(), and the pure
// results formatters below back ending_popup / results_summary_lines and are
// headlessly unit-tested (the format_ctf_caps_segments coverage pattern).
namespace og::mode {

class IProgression;

// The static stateless Tower instance (spec §5.5).
IProgression& tower_progression();

// "Floor {N} conquered — best {best}" (results overview line, wins only).
std::string format_tower_summary(int world_id, short best_floor);

// The run-over popup body: team-wipe/timeout shape carries the shareable
// seed; the withdraw/quit shape does not.
std::string format_tower_loss(int world_id, short best_floor,
                              std::uint32_t run_seed, bool withdrawn);

} // namespace og::mode
