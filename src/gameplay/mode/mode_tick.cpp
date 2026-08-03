/* Scripted-mode (TYPE_SCRIPTED) engine.
 *
 * Owns the lazy on_mode_init activation latch, the per-tick win-latch
 * re-assert (GameWorld::tick zeroes game_ended/ending/next_level at entry —
 * the trap that has bitten twice), the engine respawn timers, and the
 * on_mode_tick dispatch. All match POLICY lives in campaign-pack Lua; this
 * file is the fixed frame those hooks run in. Everything here is
 * deterministic and draws no world RNG.
 */

#include <openglad/gameplay/mode/mode_state.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>

namespace og::sim {

const char* team_color_name(int team)
{
    switch (team)
    {
        case 0: return "RED";
        case 1: return "GREEN";
        case 2: return "BLUE";
        default: return "YELLOW";
    }
}

bool mode_scripted_active(const GameWorld& world)
{
    return (world.type & GameWorld::TYPE_SCRIPTED) != 0 && world.mode.active;
}

std::uint8_t authored_team_mask(const GameWorld& world)
{
    // Dead markers included: the level bootstrap kills the markers it
    // consumes for player placement, and the mask must not depend on how
    // many seats deployed (same rule as the respawn anchor scan).
    std::uint8_t mask = 0;
    for (const auto& uptr : world.oblist)
    {
        walker* w = uptr.get();
        if (w == nullptr || w->query_order() != Order::Special ||
            w->family() != FAMILY_RESERVED_TEAM)
        {
            continue;
        }
        const unsigned char team = w->team_num();
        if (team < SCORE_TEAM_COUNT)
        {
            mask = static_cast<std::uint8_t>(
                mask | (1u << static_cast<unsigned>(team)));
        }
    }
    return mask;
}

std::uint8_t effective_team_mask(std::uint8_t authored, int requested) noexcept
{
    const auto all_mask =
        static_cast<std::uint8_t>((1u << SCORE_TEAM_COUNT) - 1u);
    authored = static_cast<std::uint8_t>(authored & all_mask);
    if (requested <= 0)
        return authored;
    int remaining = std::clamp(requested, 2, static_cast<int>(SCORE_TEAM_COUNT));
    std::uint8_t effective = 0;
    for (int team = 0; team < SCORE_TEAM_COUNT && remaining > 0; ++team)
    {
        const auto bit =
            static_cast<std::uint8_t>(1u << static_cast<unsigned>(team));
        if ((authored & bit) == 0)
            continue;
        effective = static_cast<std::uint8_t>(effective | bit);
        --remaining;
    }
    return effective;
}

} // namespace og::sim
