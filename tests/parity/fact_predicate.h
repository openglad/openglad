// Parity semantic-equivalence predicate framework — Phase 01.
//
// Replaces strict byte-equality with named predicates evaluated over
// `og::parity::StateDump`. A scenario whose master golden and branch dump
// diverge (RNG, timing, intended branch fix) cannot use byte parity; it
// declares a vector of FactPredicate entries that must be true on both
// the master golden (parsed via parse_state_dump) and the freshly
// captured branch dump.

#pragma once

#include "state_dump.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace og::parity {

enum class FactKind : std::uint8_t
{
    TickReached,                     // arg0 = min_tick
    LevelDoneEquals,                 // arg0 = expected level_done
    ScoreDelta,                      // arg0 = team, arg1 = min, arg2 = max
    WalkerFamilyCount,               // arg0 = family, arg1 = min, arg2 = max
    WalkerOfTeamAlive,               // arg0 = team, arg1 = min, arg2 = max
    WalkerHpRangeAtFinalTick,        // arg0 = family, arg1 = min_hp*100, arg2 = max_hp*100
    WalkerKeysApplied,               // arg0 = min_keys (Indeterminate if !inventory_keys)
    WalkerPositionMoved,             // arg0 = family, arg1 = min_xpos, arg2 = min_ypos
    WalkerDiedByFinal,               // arg0 = family
    WalkerAliveAtFinal,              // arg0 = family, arg1 = min_alive
    TreasureFamilyRemovedFromOblist, // arg0 = family
    StatDeltaOnPickup,               // arg0 = stat_kind, arg1 = min, arg2 = max (Indeterminate w/o baseline)
    EffectFamilyCount,               // arg0 = family, arg1 = min, arg2 = max,
                                     //   arg3 = source-walker-family qualifier (>=0)
                                     //   arg4 = min/max lifetime window marker
                                     //   (lint requires arg3>=0 OR arg4>0)
    EventKindAtLeast,                // arg0 = kind ordinal, arg1 = min
    EventKindExactly,                // arg0 = kind ordinal, arg1 = exact count
    WeaponFamilyEmitted,             // arg0 = family — searches dump.weapons[] ONLY
};

struct FactPredicate
{
    FactKind         kind;
    std::int32_t     arg0  = 0;
    std::int32_t     arg1  = 0;
    std::int32_t     arg2  = 0;
    std::int32_t     arg3  = 0;
    std::int32_t     arg4  = 0;
    std::string_view label = {};
};

struct FactEvalResult
{
    bool        ok           = true;  // predicate held
    bool        indeterminate = false; // dump lacked the structural data
    std::string message;              // human-readable failure detail
};

// One predicate. Returns ok=true if the dump satisfies it. ok=true with
// indeterminate=true means the dump did not carry the structural data
// needed to decide (e.g. WalkerKeysApplied on a v1 dump missing
// inventory_keys); callers treat those as pass.
FactEvalResult evaluate_one(const FactPredicate& p, const StateDump& dump);

// Evaluate every predicate in [begin, count). The result is ok iff every
// determinate evaluation held; the message lists the first failing
// predicate (or empty on full pass).
FactEvalResult evaluate_facts(const FactPredicate* begin,
                              std::size_t          count,
                              const StateDump&     dump);

// Schema-v1 JSON -> StateDump parser. Tolerates unknown top-level keys
// (forward-compatible rule) and missing optional keys (inventory_keys,
// weapons). Returns std::nullopt on hard parse error; the message in the
// optional<FactEvalResult> is unused here — callers ASSERT_TRUE on
// optional::has_value() and inspect fields directly.
std::optional<StateDump> parse_state_dump(std::string_view json);

// Construct a non-default sentinel for a Mutation declaration. Used by
// the lint to assert at least one of file/line/from/to/rationale is set.
inline constexpr bool mutation_is_default(std::string_view file,
                                          int              line,
                                          std::string_view from,
                                          std::string_view to,
                                          std::string_view rationale) noexcept
{
    return file.empty() && line == 0 && from.empty() && to.empty() &&
           rationale.empty();
}

// In-source convenience constructors used by scenario_table.h to declare
// `expected_facts` arrays inline.
namespace pred {

inline constexpr FactPredicate TickReached(std::int32_t tick, std::string_view label = {}) noexcept
{
    return {FactKind::TickReached, tick, 0, 0, 0, 0, label};
}
inline constexpr FactPredicate LevelDoneEquals(std::int32_t expected, std::string_view label = {}) noexcept
{
    return {FactKind::LevelDoneEquals, expected, 0, 0, 0, 0, label};
}
inline constexpr FactPredicate ScoreDelta(std::int32_t team, std::int32_t min, std::int32_t max,
                                          std::string_view label = {}) noexcept
{
    return {FactKind::ScoreDelta, team, min, max, 0, 0, label};
}
inline constexpr FactPredicate WalkerFamilyCount(std::int32_t family, std::int32_t mn, std::int32_t mx,
                                                 std::string_view label = {}) noexcept
{
    return {FactKind::WalkerFamilyCount, family, mn, mx, 0, 0, label};
}
inline constexpr FactPredicate WalkerOfTeamAlive(std::int32_t team, std::int32_t mn, std::int32_t mx,
                                                 std::string_view label = {}) noexcept
{
    return {FactKind::WalkerOfTeamAlive, team, mn, mx, 0, 0, label};
}
inline constexpr FactPredicate WalkerHpRangeAtFinalTick(std::int32_t family, std::int32_t min_hp_cents,
                                                        std::int32_t max_hp_cents,
                                                        std::string_view label = {}) noexcept
{
    return {FactKind::WalkerHpRangeAtFinalTick, family, min_hp_cents, max_hp_cents, 0, 0, label};
}
inline constexpr FactPredicate WalkerKeysApplied(std::int32_t min_keys, std::string_view label = {}) noexcept
{
    return {FactKind::WalkerKeysApplied, min_keys, 0, 0, 0, 0, label};
}
inline constexpr FactPredicate WalkerPositionMoved(std::int32_t family, std::int32_t min_xpos,
                                                   std::int32_t min_ypos,
                                                   std::string_view label = {}) noexcept
{
    return {FactKind::WalkerPositionMoved, family, min_xpos, min_ypos, 0, 0, label};
}
inline constexpr FactPredicate WalkerDiedByFinal(std::int32_t family, std::string_view label = {}) noexcept
{
    return {FactKind::WalkerDiedByFinal, family, 0, 0, 0, 0, label};
}
inline constexpr FactPredicate WalkerAliveAtFinal(std::int32_t family, std::int32_t min_alive,
                                                  std::string_view label = {}) noexcept
{
    return {FactKind::WalkerAliveAtFinal, family, min_alive, 0, 0, 0, label};
}
inline constexpr FactPredicate TreasureFamilyRemovedFromOblist(std::int32_t family,
                                                               std::string_view label = {}) noexcept
{
    return {FactKind::TreasureFamilyRemovedFromOblist, family, 0, 0, 0, 0, label};
}
inline constexpr FactPredicate StatDeltaOnPickup(std::int32_t stat_kind, std::int32_t mn, std::int32_t mx,
                                                 std::string_view label = {}) noexcept
{
    return {FactKind::StatDeltaOnPickup, stat_kind, mn, mx, 0, 0, label};
}
inline constexpr FactPredicate EffectFamilyCount(std::int32_t family, std::int32_t mn, std::int32_t mx,
                                                 std::int32_t source_family_qualifier,
                                                 std::int32_t window_marker = 0,
                                                 std::string_view label     = {}) noexcept
{
    return {FactKind::EffectFamilyCount, family, mn, mx, source_family_qualifier, window_marker, label};
}
inline constexpr FactPredicate EventKindAtLeast(std::int32_t kind_ordinal, std::int32_t mn,
                                                std::string_view label = {}) noexcept
{
    return {FactKind::EventKindAtLeast, kind_ordinal, mn, 0, 0, 0, label};
}
inline constexpr FactPredicate EventKindExactly(std::int32_t kind_ordinal, std::int32_t exact_count,
                                                std::string_view label = {}) noexcept
{
    return {FactKind::EventKindExactly, kind_ordinal, exact_count, 0, 0, 0, label};
}
inline constexpr FactPredicate WeaponFamilyEmitted(std::int32_t family, std::string_view label = {}) noexcept
{
    return {FactKind::WeaponFamilyEmitted, family, 0, 0, 0, 0, label};
}

} // namespace pred

} // namespace og::parity
