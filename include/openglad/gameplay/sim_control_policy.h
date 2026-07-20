/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Networked in-game control rights (company-basecamp design §4.4).
//
// Pure, SDL-free decision logic for WHO may claim WHICH walker under the
// protocol-v8 control policy replicated by WorldSnapshot v9:
//
//   GameWorld::control_policy  0 = legacy shared pool (every local/solo
//                              session and cross-control-ON lobbies),
//                              1 = owner-locked (networked && !cross_control,
//                              derived from the game-start config).
//   GameWorld::player_machine  per-global-player map: low 7 bits = machine id
//                              (the owning peer's lowest bound global player
//                              index), bit7 = "this player's machine deployed
//                              >= 1 character this level", 0xff = no player.
//
// Both fields default to legacy values, so every existing flow (and every
// parity golden) is byte-identical: policy-off paths delegate to the
// unchanged legacy selectors.
//
// This module only DECIDES — it never mutates walkers or seats. Enforcement
// lands at the four server-side selector sites of §4.4 in a later WP6 stage.

#include <array>
#include <cstddef>
#include <cstdint>

class GameWorld;
class walker;
struct SimInputResult;

namespace og::sim {

// GameWorld::control_policy values (§4.1).
inline constexpr std::uint8_t kControlPolicyLegacy = 0;
inline constexpr std::uint8_t kControlPolicyOwnerLocked = 1;

// GameWorld::player_machine encoding (§4.1 / snapshot v9).
inline constexpr std::uint8_t kPlayerMachineNone = 0xff;
inline constexpr std::uint8_t kPlayerMachineDeployedBit = 0x80;
inline constexpr std::uint8_t kPlayerMachineIdMask = 0x7f;
// Matches og::sim::kMaxGlobalPlayers and the GameWorld::player_machine
// extent; the set_control_policy assignment enforces the match at compile
// time without pulling net_transport.h into this header.
inline constexpr std::size_t kPlayerMachineSlots = 16;

// Owner-locked derivation (§4.4): only a networked session whose host left
// cross-control OFF locks claims to the owning machine. Callers must read
// cross_control from the game-start CONFIG (the session-only
// SaveData::cross_control carried on the install parameters), never from a
// disk-round-tripped save.
[[nodiscard]] std::uint8_t derive_control_policy(bool networked,
                                                 bool cross_control) noexcept;

// Compose / decompose one player_machine entry.
[[nodiscard]] std::uint8_t encode_player_machine(std::uint8_t machine_id,
                                                 bool deployed) noexcept;
[[nodiscard]] std::uint8_t player_machine_id(std::uint8_t entry) noexcept;
[[nodiscard]] bool player_machine_deployed(std::uint8_t entry) noexcept;

// Stamp the policy + machine map onto the authoritative world; the snapshot
// stream replicates both to every mirror. Called by the networked host
// installs and the dedicated server; local installs never call it (the
// defaults are legacy).
void set_control_policy(
    GameWorld& world, std::uint8_t policy,
    const std::array<std::uint8_t, kPlayerMachineSlots>& machines) noexcept;

// may_control: is `player_index` allowed to claim control of `w`? Pure
// ownership only — call sites keep their own liveness/user()/team filters.
//  - Legacy policy: always yes (all four §4.4 sites behave exactly like v7).
//  - Owner-locked: owned walkers (attributable myguy owner) may be claimed
//    iff the owner's machine == the player's machine (same-machine seats are
//    free among their machine's characters). Unowned scenario troops
//    ([NET-F3]) may be claimed by any player whose machine deployed >= 1
//    character this level; 0-deploy machines stay follow-only. A seat with
//    no player_machine entry claims nothing under owner-locked.
[[nodiscard]] bool control_claim_allowed(const GameWorld& world,
                                         const walker* w,
                                         short player_index) noexcept;

// Static follow-only classification ([NET-F3]): under owner-locked a machine
// that deployed no characters can never claim, so its seats are follow-only
// from bind time. (The dynamic all-owned-dead case is the Follow verdict of
// sim_reacquire_control.)
[[nodiscard]] bool seat_is_follow_only(const GameWorld& world,
                                       short player_index) noexcept;

// The legacy sim_find_next_control 3-pass scan with control_claim_allowed
// conjoined per candidate. Policy-off delegates to sim_find_next_control
// itself, so legacy decisions are byte-identical by construction.
[[nodiscard]] walker* sim_find_next_control_owned(GameWorld& level,
                                                  short my_team,
                                                  short player_index);

// Death auto-switch / entry claim verdict (§4.4 enforcement site 2).
enum class SimReacquire : std::uint8_t {
    Claimed, // control_out set; the caller claims exactly as today
    Follow,  // owner-locked only: every candidate was denied by ownership
             // while the legacy scan still had one — seat goes null with NO
             // endgame request (the follow camera's engagement signal)
    EndGame, // today's endgame result; the server's respawn and bound-team
             // wipe suppressions stay downstream, unchanged ([NET-R1])
};

SimReacquire sim_reacquire_control(GameWorld& level, short my_team,
                                   short player_index, walker*& control_out);

// §4.4 site 2 single-line hook for sim_process_player_input: runs
// sim_reacquire_control and maps the verdict onto the caller's
// SimInputResult. Returns true when the caller must return `result`
// immediately:
//  - Follow: `result` untouched and control_out null — the seat spectates
//    with NO endgame request (the server broadcasts ControlChange entity 0
//    from the seat's null control exactly as today);
//  - EndGame: today's endgame result fields (endgame_requested = true,
//    endgame_type = 1).
// Claimed returns false with control_out set; the caller runs today's claim
// tail (user stamp, ACT_CONTROL, control_hp fields) unchanged.
[[nodiscard]] bool sim_reacquire_apply(GameWorld& level, short my_team,
                                       short player_index,
                                       walker*& control_out,
                                       SimInputResult& result);

} // namespace og::sim
