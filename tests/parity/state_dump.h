// Parity harness state dumper — schema v1.
//
// Captures a deterministic, canonical JSON snapshot of an og::sim::GameWorld
// suitable for byte-comparison against a master-side companion dump. The
// emitter sorts keys lexicographically, formats floats with "%.6f", and
// normalises negative zero. See .plan/parity-harness-design.md for the
// complete schema specification.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class GameWorld;
namespace og::sim { class SimEventLog; }

namespace og::parity {

struct WalkerEntry
{
    std::uint32_t id           = 0;     // walker::entity_id()
    std::string   family;               // symbolic family name, e.g. "FAMILY_SOLDIER"
    std::uint32_t team         = 0;
    std::int32_t  xpos         = 0;
    std::int32_t  ypos         = 0;
    float         hp           = 0.0f;
    float         max_hp       = 0.0f;
    std::int32_t  weapons_left = 0;     // branch-side analogue of "ammo"
    bool          alive        = true;  // walker::dead() == 0
    std::int32_t  floor        = 0;     // walker::floor() (stacked-floor index)
};

struct EffectEntry
{
    std::uint32_t id       = 0;
    std::string   family;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

// Weapon-order entity (weaplist). The schema-v1 canonical_serialize does
// not yet emit a top-level "weapons" array — Phase 04 wires the producer
// + serialiser together. Until then this vector stays empty for runner-
// produced dumps and parse_state_dump tolerates an absent "weapons" key.
// `WeaponFamilyEmitted` predicates search ONLY this vector — never
// `effects[]` — so FAMILY_DOOR (Order::Weapon, id 18) matches and
// FAMILY_DOOR_OPEN (effect, id 11) does not.
struct WeaponEntry
{
    std::uint32_t id       = 0;
    std::string   family;
    std::uint32_t team     = 0;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

// Per-tick trajectory sample of a single live weaplist projectile. The
// runner samples every live Order::Weapon entity at every tick of the
// scenario, so the resulting array of samples reconstructs each weapon's
// path/speed over its lifetime. `seq` is the intra-family spawn index
// (0,1,2,...), assigned by first-appearance order within the family — it
// is the cross-arm-comparable key (NOT entity_id, which the master side
// does not have). Branch and master deterministically spawn weapons in
// the same order from the same rng_seed, so a given (family, seq) names
// the same projectile on both arms. `tick` is world.tick_count_ at the
// moment of sampling (post world.tick()).
struct WeaponTrackSample
{
    std::uint32_t tick     = 0;
    std::string   family;
    std::int32_t  seq      = 0;
    std::int32_t  xpos     = 0;
    std::int32_t  ypos     = 0;
    std::int32_t  lifetime = 0;
};

struct EventEntry
{
    std::string   kind;       // canonical kind name, e.g. "play_sound"
    std::uint32_t tick     = 0;
    std::uint32_t a        = 0;
    std::uint32_t b        = 0;
    std::string   text;       // optional payload (notifications)
    std::uint32_t sequence = 0; // insertion order within tick
};

struct StateDump
{
    std::string                schema_version = "v1";
    std::uint32_t              tick           = 0;
    std::uint32_t              rng_state      = 0;
    bool                       rng_observable = true;
    std::uint32_t              score_per_team[4] = {0, 0, 0, 0};
    std::vector<WalkerEntry>   walkers;
    std::vector<EffectEntry>   effects;
    std::vector<EventEntry>    events;
    // Phase 02 redo additions to schema v1. The serialiser emits these as
    // top-level keys in sorted order; v1 readers tolerate unknown keys per
    // .plan/parity-harness-design.md (forward-compatible rule).
    std::int32_t               level_done       = 0; // world.level_done
    std::uint32_t              level_tick_count = 0; // world.level_tick_count()
    // Phase 01 (semantic-parity contract) additions. Producer not yet
    // wired — `weapons` stays empty for runner-produced dumps and
    // `inventory_keys` stays std::nullopt. Phase 04 wires both ends.
    // parse_state_dump tolerates an absent "weapons" key and an absent
    // "inventory_keys" key. WeaponFamilyEmitted predicates evaluate over
    // `weapons` ONLY.
    std::vector<WeaponEntry>           weapons;
    // Additive (schema_version stays "v1"). Per-tick trajectory samples of
    // every live weaplist projectile, captured each tick by the runner.
    // Existing goldens predate this key; parse_state_dump tolerates its
    // absence (the vector stays empty) and trajectory predicates report
    // Indeterminate (pass) on legacy dumps. Serialised as the top-level
    // "weapon_tracks" key, sorted lexicographically between "walkers" and
    // "weapons".
    std::vector<WeaponTrackSample>     weapon_tracks;
    std::optional<std::vector<std::uint8_t>> inventory_keys;
};

// Build a StateDump from a live GameWorld. `weapon_tracks` carries the
// per-tick trajectory samples the runner collected over the run (the live
// world only exposes the final tick, so the samples must be threaded in);
// capture_state_dump moves them into the dump and sorts them canonically.
StateDump capture_state_dump(const GameWorld& world,
                             const og::sim::SimEventLog* events = nullptr,
                             std::vector<WeaponTrackSample> weapon_tracks = {});

// Canonical JSON serialisation. Sorted keys, "%.6f" floats, LF line endings,
// single trailing newline. Branch and master companion must produce
// byte-identical output for an equivalent dump.
std::string canonical_serialize(const StateDump& dump);

// (Order, family-id) -> symbolic family name. Schema-v1 (Phase 03) per-Order
// resolution: the dumped `family` string is taken from the table for the
// entity's `Order` (Living / Weapon / Treasure / Generator / FX). Returns
// "FAMILY_UNKNOWN_<order>_<id>" for unmapped (order, id) pairs.
std::string family_symbol_by_order(std::int32_t order, std::int32_t family_id);

// EventKind -> canonical lowercase name (e.g. "play_sound").
std::string event_kind_symbol(std::uint32_t kind_raw);

} // namespace og::parity
