// Parity harness state dumper — schema v1.
//
// Captures a deterministic, canonical JSON snapshot of an og::sim::GameWorld
// suitable for byte-comparison against a master-side companion dump. The
// emitter sorts keys lexicographically, formats floats with "%.6f", and
// normalises negative zero. See .plan/parity-harness-design.md for the
// complete schema specification.

#pragma once

#include <cstdint>
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
};

struct EffectEntry
{
    std::uint32_t id       = 0;
    std::string   family;
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
};

// Build a StateDump from a live GameWorld.
StateDump capture_state_dump(const GameWorld& world,
                             const og::sim::SimEventLog* events = nullptr);

// Canonical JSON serialisation. Sorted keys, "%.6f" floats, LF line endings,
// single trailing newline. Branch and master companion must produce
// byte-identical output for an equivalent dump.
std::string canonical_serialize(const StateDump& dump);

// Family-id -> symbolic name. Returns "FAMILY_UNKNOWN_<int>" for unmapped ids.
std::string family_symbol(std::int32_t family_id);

// EventKind -> canonical lowercase name (e.g. "play_sound").
std::string event_kind_symbol(std::uint32_t kind_raw);

} // namespace og::parity
