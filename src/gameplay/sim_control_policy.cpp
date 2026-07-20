/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// Networked in-game control rights policy (company-basecamp design §4.4).
// Pure decisions only; the §4.4 selector sites enforce them.

#include <openglad/gameplay/sim_control_policy.h>

#include <openglad/core/test_trace.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/walker.h>

namespace og::sim {

namespace {

bool owner_locked(const GameWorld& world) noexcept
{
    return world.control_policy == kControlPolicyOwnerLocked;
}

// The player's own player_machine entry, or kPlayerMachineNone for an
// out-of-range or unbound seat.
std::uint8_t machine_entry_for_player(const GameWorld& world,
                                      short player_index) noexcept
{
    if (player_index < 0 ||
        static_cast<std::size_t>(player_index) >= world.player_machine.size())
    {
        return kPlayerMachineNone;
    }
    return world.player_machine[static_cast<std::size_t>(player_index)];
}

} // namespace

std::uint8_t derive_control_policy(bool networked, bool cross_control) noexcept
{
    return (networked && !cross_control) ? kControlPolicyOwnerLocked
                                         : kControlPolicyLegacy;
}

std::uint8_t encode_player_machine(std::uint8_t machine_id,
                                   bool deployed) noexcept
{
    return static_cast<std::uint8_t>(
        (machine_id & kPlayerMachineIdMask) |
        (deployed ? kPlayerMachineDeployedBit : 0));
}

std::uint8_t player_machine_id(std::uint8_t entry) noexcept
{
    return static_cast<std::uint8_t>(entry & kPlayerMachineIdMask);
}

bool player_machine_deployed(std::uint8_t entry) noexcept
{
    return entry != kPlayerMachineNone &&
           (entry & kPlayerMachineDeployedBit) != 0;
}

void set_control_policy(
    GameWorld& world, std::uint8_t policy,
    const std::array<std::uint8_t, kPlayerMachineSlots>& machines) noexcept
{
    // The assignment is the compile-time extent check against
    // GameWorld::player_machine (mismatched std::array sizes do not assign).
    world.control_policy = policy;
    world.player_machine = machines;
}

bool control_claim_allowed(const GameWorld& world, const walker* w,
                           short player_index) noexcept
{
    if (!owner_locked(world))
        return true; // legacy shared pool: all four §4.4 sites behave like v7
    if (w == nullptr)
        return false;

    const std::uint8_t mine = machine_entry_for_player(world, player_index);
    if (mine == kPlayerMachineNone)
        return false; // unbound seat claims nothing under owner-locked

    // Owned walker with an attributable owner: same-machine seats only.
    if (w->myguy != nullptr)
    {
        const std::uint8_t owner = w->myguy->owner_player_index;
        if (static_cast<std::size_t>(owner) < world.player_machine.size())
        {
            const std::uint8_t owner_entry =
                world.player_machine[static_cast<std::size_t>(owner)];
            if (owner_entry != kPlayerMachineNone)
                return player_machine_id(owner_entry) ==
                       player_machine_id(mine);
        }
        // Unattributable owner (no owner tag, or the tagged player has no
        // machine entry this level): fall through to the [NET-F3] troop rule
        // so orphaned characters never become permanently unclaimable.
    }

    // Unowned scenario troop [NET-F3]: claimable by any player whose machine
    // deployed >= 1 character this level; 0-deploy machines stay follow-only.
    return player_machine_deployed(mine);
}

bool seat_is_follow_only(const GameWorld& world, short player_index) noexcept
{
    if (!owner_locked(world))
        return false;
    const std::uint8_t mine = machine_entry_for_player(world, player_index);
    return !player_machine_deployed(mine);
}

walker* sim_find_next_control_owned(GameWorld& level, short my_team,
                                    short player_index)
{
    // Policy off: the decision IS the legacy scan (byte-identical by
    // construction — same function, same TRACE stream).
    if (!owner_locked(level))
        return sim_find_next_control(level, my_team);

    TRACE("sim_control", "find_next_control_owned team %d player %d", my_team,
          player_index);

    const auto allowed = [&level, player_index](const walker* w) {
        return control_claim_allowed(level, w, player_index);
    };

    // The three passes mirror sim_find_next_control (sim_input_handler.cpp)
    // exactly — including pass 3's deliberate lack of a dormant() check —
    // with the ownership conjunction appended to each. The policy-on
    // equivalence suite pins this scan against the legacy one under a fully
    // permissive machine map.

    // First look for a player character, not already controlled
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && !w->dormant() &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->myguy &&
            w->team_num() == my_team && allowed(w))
        {
            TRACE("sim_control", "found player character '%s'",
                  w->stats()->name.c_str());
            return w;
        }
    }

    // Second, look for anyone on our team
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() && !w->dormant() &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->team_num() == my_team && allowed(w))
        {
            TRACE("sim_control", "found team member '%s'",
                  w->stats()->name.c_str());
            return w;
        }
    }

    // Now try for ANYONE who's left alive
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        if (w && !w->dead() &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->myguy && allowed(w))
        {
            TRACE("sim_control", "found fallback character '%s'",
                  w->stats()->name.c_str());
            return w;
        }
    }

    TRACE("sim_control", "found no claimable walker");
    return nullptr;
}

SimReacquire sim_reacquire_control(GameWorld& level, short my_team,
                                   short player_index, walker*& control_out)
{
    control_out = sim_find_next_control_owned(level, my_team, player_index);
    if (control_out != nullptr)
        return SimReacquire::Claimed;

    // Policy off: exactly today's "no control found" endgame request. The
    // server's respawn suppression and bound-team wipe suppression stay
    // downstream and unchanged, so the [NET-R1] allied claimed-teammate
    // observables (seat null, ControlChange 0, no world ending) hold.
    if (!owner_locked(level))
        return SimReacquire::EndGame;

    // Owner-locked: when the LEGACY scan would still have claimed someone,
    // this failure is a pure ownership denial — the seat spectates (Follow)
    // and must NOT request the endgame a legacy claim would have prevented.
    // When even the legacy scan is empty, behave exactly like today
    // (EndGame + the downstream suppressions), keeping the level's ending
    // reachable ([NET-F3] softlock rule).
    walker* const legacy_candidate = sim_find_next_control(level, my_team);
    if (legacy_candidate != nullptr)
    {
        TRACE("sim_control", "reacquire follow for player %d", player_index);
        return SimReacquire::Follow;
    }
    return SimReacquire::EndGame;
}

} // namespace og::sim
