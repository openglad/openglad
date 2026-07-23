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
#include <memory>
#include <span>
#include <vector>

class GameWorld;
class guy;
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

// --- §4.4 install helpers (networked host installs + dedicated server) ---

// Defined in lobby_server.h; only referenced here so the install callers can
// hand their lobby bindings straight through.
struct LobbyPlayerBinding;

// Per-global-player "owns >= 1 character in the deploy-filtered game-start
// roster": true at index p when some non-null roster member carries
// owner_player_index == p. Roster assembly only materializes DEPLOYED slots,
// so an owner tag present in the roster IS a deployed character (the
// [NET-F3] bit7 source). kNoOwner / out-of-range tags are ignored.
[[nodiscard]] std::array<bool, kPlayerMachineSlots>
deployed_players_from_roster(
    std::span<const std::unique_ptr<guy>> roster) noexcept;

// Build the player_machine map (§4.1) from the lobby player bindings:
// machine id = the owning peer's lowest bound global player index; bit7 is
// set for EVERY player of a machine when any of that machine's players is
// deployed. Unbound / out-of-range player indices stay kPlayerMachineNone.
[[nodiscard]] std::array<std::uint8_t, kPlayerMachineSlots>
build_player_machine_map(
    const std::vector<LobbyPlayerBinding>& bindings,
    const std::array<bool, kPlayerMachineSlots>& player_deployed) noexcept;

// One-call §4.4 install for the three production callers (SDL network host
// shadow, curses host session, dedicated server): derive the policy from
// the game-start config (owner-locked iff networked && !cross_control) and
// stamp it together with the machine map built from the bindings + the
// deploy-filtered roster. The map is stamped under BOTH policies (legacy
// ignores it); local installs never call this at all, so local/solo/parity
// worlds keep the all-default legacy scalars.
void install_control_policy(GameWorld& world,
                            bool networked,
                            bool cross_control,
                            const std::vector<LobbyPlayerBinding>& bindings,
                            std::span<const std::unique_ptr<guy>> roster);

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

// The sim_find_next_control same-team scan with control_claim_allowed
// conjoined per candidate. Policy-off delegates to sim_find_next_control
// itself. Neither path may ever claim a walker from another team.
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

// --- §4.5 follow-camera target selection (client-side helpers) ---
//
// Pure selectors over a DISPLAY/mirror world for the follow camera of a
// machine with no controllable walker (0-deploy, all-dead, spectator). They
// are never called by the sim — the SDL local transport shadow and the
// curses networked sessions share them so both clients pick identical
// targets. They never mutate walkers (in particular: no user-tag writes).

// Preferred watch targets: live, non-dormant Order::Living walkers that are
// player-relevant (a user tag set or roster-owned). When no preferred target
// exists any live, non-dormant living qualifies ("fallback any living").
[[nodiscard]] bool follow_target_preferred(const walker* w) noexcept;
[[nodiscard]] bool follow_target_visible(const walker* w) noexcept;

// Default target (§4.5): the walker of the lowest global player index whose
// controlled-entity slot resolves to a live walker, else the first eligible
// walker in oblist order (preferred filter first, then any living). 0 when
// the world holds nothing watchable.
[[nodiscard]] std::uint32_t default_follow_target_id(
    GameWorld& world, std::span<const std::uint32_t> controlled_entity_ids);

// SwitchChar cycle (Shift = reverse): the next eligible target after
// `current` in oblist order, wrapping. A null/stale `current` restarts from
// the first eligible walker. 0 when the world holds nothing watchable.
[[nodiscard]] std::uint32_t next_follow_target_id(GameWorld& world,
                                                  walker* current,
                                                  bool reverse);

} // namespace og::sim
