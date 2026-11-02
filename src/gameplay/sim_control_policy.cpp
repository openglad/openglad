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
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <memory>

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
    // rule: A seat index outside the machine table belongs to no machine (it can claim nothing and
    //   is follow-only under owner-locked control).
    // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
    // tests: SimControlPolicy.derive_encode_and_classify_helpers
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
    // Only a networked game with CROSS CONTROL off locks each character to its owner's machine;
    // local games and cross-control games share every character among all seats.
    return (networked && !cross_control) ? kControlPolicyOwnerLocked
                                         : kControlPolicyLegacy;
}

std::uint8_t encode_player_machine(std::uint8_t machine_id,
                                   bool deployed) noexcept
{
    // rule: A machine entry packs the machine id (low 7 bits) and whether that machine deployed any
    //   character (bit 7); the 'no player' value 0xff never reads as deployed.
    // tests: SimControlPolicy.derive_encode_and_classify_helpers
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

std::array<bool, kPlayerMachineSlots> deployed_players_from_roster(
    std::span<const std::unique_ptr<guy>> roster) noexcept
{
    std::array<bool, kPlayerMachineSlots> deployed = {};
    // rule: A player counts as having deployed this level when any roster character carries that
    //   player's owner tag; untagged (0xff) or out-of-range tags mark nobody.
    // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
    for (const std::unique_ptr<guy>& member : roster)
    {
        // rule: Part of the rule above that begins "A player counts as having deployed this level
        //   when ..."
        // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
        if (member == nullptr)
            continue;
        // guy::kNoOwner (0xff) and any other out-of-range tag never marks a
        // player deployed.
        // rule: Part of the rule above that begins "A player counts as having deployed this level
        //   when ..."
        // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
        if (static_cast<std::size_t>(member->owner_player_index) <
            deployed.size())
        {
            deployed[static_cast<std::size_t>(member->owner_player_index)] =
                true;
        }
    }
    return deployed;
}

std::array<std::uint8_t, kPlayerMachineSlots> build_player_machine_map(
    const std::vector<LobbyPlayerBinding>& bindings,
    const std::array<bool, kPlayerMachineSlots>& player_deployed) noexcept
{
    std::array<std::uint8_t, kPlayerMachineSlots> machines;
    machines.fill(kPlayerMachineNone);
    // rule: Each bound seat's machine id is the lowest player index its peer seats, and a machine
    //   counts as deployed when any of its seats deployed; unbound or out-of-range bindings leave
    //   'no player'.
    // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
    for (const LobbyPlayerBinding& binding : bindings)
    {
        // rule: Part of the rule above that begins "Each bound seat's machine id is the lowest
        //   player ..."
        // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
        if (static_cast<std::size_t>(binding.player_index) >= machines.size())
            continue;
        // The machine id is the peer's lowest bound global player index;
        // bit7 ORs "deployed" across every player the peer machine seats.
        std::uint8_t machine_id = binding.player_index;
        bool machine_deployed = false;
        // rule: Part of the rule above that begins "Each bound seat's machine id is the lowest
        //   player ..."
        // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
        for (const LobbyPlayerBinding& seat : bindings)
        {
            // rule: Part of the rule above that begins "Each bound seat's machine id is the lowest
            //   player ..."
            // tests: SimControlPolicy.install_helpers_build_machine_map_from_bindings_and_roster
            if (seat.peer_id != binding.peer_id ||
                static_cast<std::size_t>(seat.player_index) >= machines.size())
            {
                continue;
            }
            machine_id = std::min(machine_id, seat.player_index);
            machine_deployed =
                machine_deployed ||
                player_deployed[static_cast<std::size_t>(seat.player_index)];
        }
        machines[static_cast<std::size_t>(binding.player_index)] =
            encode_player_machine(machine_id, machine_deployed);
    }
    return machines;
}

void install_control_policy(GameWorld& world,
                            bool networked,
                            bool cross_control,
                            const std::vector<LobbyPlayerBinding>& bindings,
                            std::span<const std::unique_ptr<guy>> roster)
{
    set_control_policy(world,
                       derive_control_policy(networked, cross_control),
                       build_player_machine_map(
                           bindings, deployed_players_from_roster(roster)));
}

bool control_claim_allowed(const GameWorld& world, const walker* w,
                           short player_index) noexcept
{
    // rule: Under owner-locked control a seat may take a hero only if that hero's owner sits on the
    //   same machine; a troop with no owner (or an owner with no machine this level) may be taken
    //   by any seat whose machine deployed at least one character; a seat with no machine, or a
    //   machine that deployed nothing, takes nothing. Under the legacy policy everyone may take
    //   anyone.
    // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
    // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
    if (!owner_locked(world))
        return true; // legacy shared pool: all four §4.4 sites behave like v7
    // rule: Part of the rule above that begins "Under owner-locked control a seat may take a hero
    //   ..."
    // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
    // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
    if (w == nullptr)
        return false;

    const std::uint8_t mine = machine_entry_for_player(world, player_index);
    // rule: Part of the rule above that begins "Under owner-locked control a seat may take a hero
    //   ..."
    // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
    // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
    if (mine == kPlayerMachineNone)
        return false; // unbound seat claims nothing under owner-locked

    // Owned walker with an attributable owner: same-machine seats only.
    // rule: Part of the rule above that begins "Under owner-locked control a seat may take a hero
    //   ..."
    // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
    // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
    if (w->myguy != nullptr)
    {
        const std::uint8_t owner = w->myguy->owner_player_index;
        // rule: Part of the rule above that begins "Under owner-locked control a seat may take a
        //   hero ..."
        // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
        // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
        if (static_cast<std::size_t>(owner) < world.player_machine.size())
        {
            const std::uint8_t owner_entry =
                world.player_machine[static_cast<std::size_t>(owner)];
            // rule: Part of the rule above that begins "Under owner-locked control a seat may take
            //   a hero ..."
            // tests: SimControlPolicy.owner_locked_allow_deny_matrix_with_same_machine_seats
            // tests: SimControlPolicy.owner_locked_unowned_troop_rule_net_f3
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
    // rule: Under owner-locked control a seat whose machine deployed nothing (or has no machine)
    //   only watches; under legacy no seat is follow-only.
    // tests: SimControlPolicy.derive_encode_and_classify_helpers
    if (!owner_locked(world))
        return false;
    const std::uint8_t mine = machine_entry_for_player(world, player_index);
    return !player_machine_deployed(mine);
}

walker* sim_find_next_control_owned(GameWorld& level, short my_team,
                                    short player_index)
{
    // Scripted modes may reseat a player's fighter off its seat team (the
    // FFA band bytes, docs/ffa-design.md §3), so the owner tag is a strictly
    // stronger identity than my_team: scan it first. This also keeps a
    // team-0 prop from stealing a scripted seat whose own fighter is still
    // claimable. Classic worlds never set TYPE_SCRIPTED, so their scans (and
    // the legacy team-0 prop quirk parity depends on) stay byte-identical.
    // player_index < 0 would cast to guy::kNoOwner and adopt untagged heroes.
    // rule: In a scripted match a seat reclaims its OWN tagged fighter first, whatever team byte
    //   the mode reseated it on (FFA bands), so a team-0 prop never steals the seat; classic worlds
    //   keep the team-filtered scans and the team-0 prop quirk.
    // tests: SimControlPolicy.scripted_owner_arm_reacquires_off_team_band_fighter
    // tests: SimControlPolicy.scripted_owner_arm_skips_dead_claimed_dormant_and_untagged
    // tests: SimControlPolicy.scripted_world_prop_not_claimed_when_owner_fighter_exists
    // tests: SimControlPolicy.classic_world_keeps_team_filter_and_team0_prop_quirk
    // tests: SimControlPolicy.scripted_owner_arm_honors_owner_locked_denials
    if ((level.type & GameWorld::TYPE_SCRIPTED) != 0 && player_index >= 0)
    {
        // rule: Part of the rule above that begins "In a scripted match a seat reclaims its OWN
        //   ..."
        // tests: SimControlPolicy.scripted_owner_arm_reacquires_off_team_band_fighter
        // tests: SimControlPolicy.scripted_owner_arm_skips_dead_claimed_dormant_and_untagged
        // tests: SimControlPolicy.scripted_world_prop_not_claimed_when_owner_fighter_exists
        // tests: SimControlPolicy.classic_world_keeps_team_filter_and_team0_prop_quirk
        // tests: SimControlPolicy.scripted_owner_arm_honors_owner_locked_denials
        for (auto& uptr : level.oblist)
        {
            walker* w = uptr.get();
            // rule: Part of the rule above that begins "In a scripted match a seat reclaims its OWN
            //   ..."
            // tests: SimControlPolicy.scripted_owner_arm_reacquires_off_team_band_fighter
            // tests: SimControlPolicy.scripted_owner_arm_skips_dead_claimed_dormant_and_untagged
            // tests: SimControlPolicy.scripted_world_prop_not_claimed_when_owner_fighter_exists
            // tests: SimControlPolicy.classic_world_keeps_team_filter_and_team0_prop_quirk
            // tests: SimControlPolicy.scripted_owner_arm_honors_owner_locked_denials
            if (w && !w->dead() && !w->dormant() && !w->hidden() &&
                w->possess_link() == 0 &&
                w->query_order() == Order::Living &&
                w->user() == -1 &&
                w->myguy &&
                w->myguy->owner_player_index ==
                    static_cast<std::uint8_t>(player_index) &&
                control_claim_allowed(level, w, player_index) &&
                seat_may_claim(level, *w))
            {
                TRACE("sim_control", "found owner-tagged character '%s'",
                      w->stats()->name.c_str());
                return w;
            }
        }
    }

    // Policy off: the decision IS the legacy scan (byte-identical by
    // construction — same function, same TRACE stream).
    // rule: With the legacy policy the reclaim scan is exactly the classic one
    //   (sim_find_next_control).
    // tests: SimControlPolicy.policy_off_matches_legacy_across_the_four_shapes
    // tests: SimControlPolicy.policy_off_matches_legacy_over_random_worlds
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    if (!owner_locked(level))
        return sim_find_next_control(level, my_team);

    TRACE("sim_control", "find_next_control_owned team %d player %d", my_team,
          player_index);

    const auto allowed = [&level, player_index](const walker* w) {
        return control_claim_allowed(level, w, player_index);
    };

    // The two passes mirror sim_find_next_control (sim_input_handler.cpp)
    // exactly, with the ownership conjunction appended to each. The policy-on
    // equivalence suite pins this scan against the legacy one under a fully
    // permissive machine map.

    // First look for a player character, not already controlled
    // rule: When a seat loses its character it takes, in world order, first an unclaimed hero
    //   (roster character) of its team, then any unclaimed living of its team, skipping dead,
    //   dormant, hidden, possession-linked, already-driven and (owner-locked) disallowed walkers,
    //   and (fixes on) walkers charmed onto the team.
    // tests: SimControlPolicy.owner_locked_permissive_map_matches_legacy_over_random_worlds
    // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        // rule: Part of the rule above that begins "When a seat loses its character it takes, in
        //   ..."
        // tests: SimControlPolicy.owner_locked_permissive_map_matches_legacy_over_random_worlds
        // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
        // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
        // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            w->possess_link() == 0 &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->myguy &&
            w->team_num() == my_team && allowed(w) &&
            seat_may_claim(level, *w))
        {
            TRACE("sim_control", "found player character '%s'",
                  w->stats()->name.c_str());
            return w;
        }
    }

    // Second, look for anyone on our team
    // rule: Part of the rule above that begins "When a seat loses its character it takes, in ..."
    // tests: SimControlPolicy.owner_locked_permissive_map_matches_legacy_over_random_worlds
    // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
    for (auto& uptr : level.oblist)
    {
        walker* w = uptr.get();
        // rule: Part of the rule above that begins "When a seat loses its character it takes, in
        //   ..."
        // tests: SimControlPolicy.owner_locked_permissive_map_matches_legacy_over_random_worlds
        // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
        // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
        // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
        if (w && !w->dead() && !w->dormant() && !w->hidden() &&
            w->possess_link() == 0 &&
            w->query_order() == Order::Living &&
            w->user() == -1 &&
            w->team_num() == my_team && allowed(w) &&
            seat_may_claim(level, *w))
        {
            TRACE("sim_control", "found team member '%s'",
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
    // rule: A seat that finds a character claims it; with nothing to claim the legacy game asks for
    //   the end of the game; under owner-locked, a seat denied only by ownership (the legacy scan
    //   would still find someone) spectates instead of ending the game, and only a true wipe ends
    //   it.
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
    // tests: SimControlPolicy.net_r1_allied_claimed_teammate_shape_yields_endgame_verdict
    // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
    if (control_out != nullptr)
        return SimReacquire::Claimed;

    // Policy off: exactly today's "no control found" endgame request. The
    // server's respawn suppression and bound-team wipe suppression stay
    // downstream and unchanged, so the [NET-R1] allied claimed-teammate
    // observables (seat null, ControlChange 0, no world ending) hold.
    // rule: Part of the rule above that begins "A seat that finds a character claims it; with ..."
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
    // tests: SimControlPolicy.net_r1_allied_claimed_teammate_shape_yields_endgame_verdict
    // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
    if (!owner_locked(level))
        return SimReacquire::EndGame;

    // Owner-locked: when the LEGACY scan would still have claimed someone,
    // this failure is a pure ownership denial — the seat spectates (Follow)
    // and must NOT request the endgame a legacy claim would have prevented.
    // When even the legacy scan is empty, behave exactly like today
    // (EndGame + the downstream suppressions), keeping the level's ending
    // reachable ([NET-F3] softlock rule).
    walker* const legacy_candidate = sim_find_next_control(level, my_team);
    // rule: Part of the rule above that begins "A seat that finds a character claims it; with ..."
    // tests: SimControlPolicy.owner_locked_follow_when_only_foreign_heroes_remain
    // tests: SimControlPolicy.owner_locked_reacquire_claims_troop_for_deployed_machines
    // tests: SimControlPolicy.net_r1_allied_claimed_teammate_shape_yields_endgame_verdict
    // tests: SemTraces.an_owner_locked_seat_with_no_claimable_walker_traces_it_and_follows
    if (legacy_candidate != nullptr)
    {
        TRACE("sim_control", "reacquire follow for player %d", player_index);
        return SimReacquire::Follow;
    }
    return SimReacquire::EndGame;
}

bool sim_reacquire_apply(GameWorld& level, short my_team, short player_index,
                         walker*& control_out, SimInputResult& result)
{
    // rule: The reclaim verdict maps onto the input result: Claimed leaves the result for the
    //   caller's claim; Follow returns with a null seat and NO endgame request; EndGame sets
    //   endgame_requested with type 1.
    // tests: SimControlPolicy.reacquire_apply_maps_verdicts_onto_sim_input_result
    switch (sim_reacquire_control(level, my_team, player_index, control_out))
    {
        case SimReacquire::Claimed:
            return false; // the caller runs today's claim tail on control_out
        case SimReacquire::Follow:
            return true; // null seat, `result` untouched: NO endgame request
        case SimReacquire::EndGame:
        default:
            // Exactly today's "no control found" result (§4.4 site 2).
            result.endgame_requested = true;
            result.endgame_type = 1;
            return true;
    }
}

// --- §4.5 follow-camera target selection ---

bool follow_target_visible(const walker* w) noexcept
{
    return w != nullptr && !w->dead() && !w->dormant() && !w->hidden() &&
        w->query_order() == Order::Living;
}

bool follow_target_preferred(const walker* w) noexcept
{
    return follow_target_visible(w) && (w->user() != -1 || w->myguy != nullptr);
}

namespace {

// First eligible walker in oblist order: preferred targets first, then the
// any-living fallback so a scenario with only anonymous troops still has a
// camera subject.
std::uint32_t first_eligible_follow_target_id(GameWorld& world)
{
    std::uint32_t fallback_id = 0;
    // rule: A spectator camera with nothing to watch starts on the first hero or seat-driven walker
    //   in world order; with none, on the first visible living; with nothing living, no target (0).
    // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
    // tests: SemControlPolicy.a_spectator_with_no_hero_to_watch_starts_on_the_first_visible_troop
    for (const auto& uptr : world.oblist)
    {
        const walker* const w = uptr.get();
        // rule: Part of the rule above that begins "A spectator camera with nothing to watch starts
        //   on ..."
        // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
        // tests: SemControlPolicy.a_spectator_with_no_hero_to_watch_starts_on_the_first_visible_troop
        if (follow_target_preferred(w))
            return w->entity_id();
        // rule: Part of the rule above that begins "A spectator camera with nothing to watch starts
        //   on ..."
        // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
        // tests: SemControlPolicy.a_spectator_with_no_hero_to_watch_starts_on_the_first_visible_troop
        if (fallback_id == 0 && follow_target_visible(w))
            fallback_id = w->entity_id();
    }
    return fallback_id;
}

} // namespace

std::uint32_t default_follow_target_id(
    GameWorld& world, std::span<const std::uint32_t> controlled_entity_ids)
{
    // Lowest global player index with a live controlled walker...
    // rule: A seatless view first follows the lowest-indexed player's live controlled walker; with
    //   none alive, the first eligible walker in world order.
    // tests: SemControlPolicy.a_seatless_view_follows_the_lowest_player_with_a_live_walker
    for (const std::uint32_t entity_id : controlled_entity_ids)
    {
        // rule: Part of the rule above that begins "A seatless view first follows the
        //   lowest-indexed player's live ..."
        // tests: SemControlPolicy.a_seatless_view_follows_the_lowest_player_with_a_live_walker
        if (entity_id == 0)
            continue;
        const walker* const w = world.find_by_id(entity_id);
        // rule: Part of the rule above that begins "A seatless view first follows the
        //   lowest-indexed player's live ..."
        // tests: SemControlPolicy.a_seatless_view_follows_the_lowest_player_with_a_live_walker
        if (w != nullptr && !w->dead())
            return entity_id;
    }
    // ...else the first eligible walker in oblist order.
    return first_eligible_follow_target_id(world);
}

std::uint32_t next_follow_target_id(GameWorld& world, walker* current,
                                    bool reverse)
{
    // rule: Switch Character on a spectator cycles to the next hero/seat body in world order
    //   (wrapping, both directions); with no other hero, the next visible living; with nobody else,
    //   it keeps watching the current walker while it is visible, else drops to no target.
    // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
    // tests: GameLoop.display_view_follow_engages_cycles_and_never_stamps_user_tags
    // tests: SemControlPolicy.switch_character_with_nobody_else_keeps_the_visible_target_or_drops_it
    if (current == nullptr)
        return first_eligible_follow_target_id(world);

    walker* const preferred = sim_cycle_next_character(
        world.oblist, current, reverse,
        [](const walker* w) { return follow_target_preferred(w); });
    // rule: Part of the rule above that begins "Switch Character on a spectator cycles to the next
    //   ..."
    // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
    // tests: GameLoop.display_view_follow_engages_cycles_and_never_stamps_user_tags
    // tests: SemControlPolicy.switch_character_with_nobody_else_keeps_the_visible_target_or_drops_it
    if (preferred != nullptr)
        return preferred->entity_id();

    walker* const any_living = sim_cycle_next_character(
        world.oblist, current, reverse,
        [](const walker* w) { return follow_target_visible(w); });
    // rule: Part of the rule above that begins "Switch Character on a spectator cycles to the next
    //   ..."
    // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
    // tests: GameLoop.display_view_follow_engages_cycles_and_never_stamps_user_tags
    // tests: SemControlPolicy.switch_character_with_nobody_else_keeps_the_visible_target_or_drops_it
    if (any_living != nullptr)
        return any_living->entity_id();

    // The cycle never returns `current` itself; keep watching it while it is
    // still watchable rather than dropping to a static camera.
    // rule: Part of the rule above that begins "Switch Character on a spectator cycles to the next
    //   ..."
    // tests: SimControlPolicy.next_follow_target_without_a_current_target_restarts_at_the_first_hero
    // tests: GameLoop.display_view_follow_engages_cycles_and_never_stamps_user_tags
    // tests: SemControlPolicy.switch_character_with_nobody_else_keeps_the_visible_target_or_drops_it
    return follow_target_visible(current) ? current->entity_id() : 0;
}

} // namespace og::sim
