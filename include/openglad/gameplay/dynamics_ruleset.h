#pragma once

#include <cstdint>

namespace og::sim {

// Host-authoritative movement/combat feel. Classic preserves Gladiator's
// stop-to-turn cadence; Modern enables the responsive input rules introduced
// for issue #205. The explicit wire values are part of snapshots and replays.
enum class DynamicsRuleset : std::uint8_t {
    Classic = 0,
    Modern = 1,
};

constexpr std::uint8_t
dynamics_ruleset_value(DynamicsRuleset ruleset) noexcept
{
    return static_cast<std::uint8_t>(ruleset);
}

constexpr bool is_valid_dynamics_ruleset(DynamicsRuleset ruleset) noexcept
{
    return ruleset == DynamicsRuleset::Classic ||
           ruleset == DynamicsRuleset::Modern;
}

constexpr DynamicsRuleset dynamics_ruleset_from_value(
    std::uint8_t value,
    DynamicsRuleset fallback = DynamicsRuleset::Classic) noexcept
{
    const auto ruleset = static_cast<DynamicsRuleset>(value);
    return is_valid_dynamics_ruleset(ruleset) ? ruleset : fallback;
}

} // namespace og::sim
