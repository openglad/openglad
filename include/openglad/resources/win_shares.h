/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// Networked win-pot money split (docs/company-basecamp-design.md §4.6-§4.7).
// A won level's fold produces a per-team cash/score delta; this module splits
// that delta among the players who deployed characters on each team, so each
// machine banks only its deploy-ratio share instead of the whole pot (the v7
// duplicated-pot behaviour that is being fixed). All of this is SDL-free and
// deterministic: the capture roster and deltas come from snapshot-synced
// sources, so host and every client compute identical shares.

#include <openglad/gameplay/net_transport.h> // og::sim::kMaxGlobalPlayers

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

class SaveData;
class GameWorld;

namespace og::progression {

// One deployed character (dead or alive) at deploy time. team is the in-level
// combat team — the same index the fold credited the money to.
struct WinShareContributor {
    std::uint8_t owner = 0xff; // global player index; >= kMaxGlobalPlayers ignored
    short team = 0;            // 0..3
};

// The pre-fold deploy roster plus the per-team deltas the fold applied
// (WinFoldContext::applied_*_delta). Latched around the win fold on both the
// host (finalize_level_and_advance_cursor) and each client (screen::endgame).
struct NetWinFoldCapture {
    std::array<std::uint32_t, 4> cash_delta{};
    std::array<std::uint32_t, 4> score_delta{};
    std::vector<WinShareContributor> deployed;
};

// Snapshot the deploy roster from the world's player-owned walkers (one entry
// per walker carrying a myguy, dead or alive). Call BEFORE the fold's
// update_guys, though world.oblist is fold-invariant so the ordering only
// matters for robustness. team = walker team_num() == the money team.
std::vector<WinShareContributor> collect_deployed_contributors(
    const GameWorld& world);

// The full per-team, per-player split. cash[t][p] / score[t][p] is player p's
// floored share of team t's delta with the per-team remainder awarded to the
// largest contributor (tie broken to the lowest player index). Conservation:
// for every team with >= 1 contributor, the shares sum exactly to the delta.
struct NetWinShareTable {
    std::array<std::array<std::uint64_t, og::sim::kMaxGlobalPlayers>, 4> cash{};
    std::array<std::array<std::uint64_t, og::sim::kMaxGlobalPlayers>, 4> score{};
    std::array<int, og::sim::kMaxGlobalPlayers> deployed_count{};
};
NetWinShareTable compute_networked_win_shares(const NetWinFoldCapture& capture);

// Overlay this machine's share onto the wallet totals already present in
// `save` (the disk baseline) — baseline + share, never absolute (§4.6 Edit 2).
// Returns the machine's deployed character count (0-deploy machines get no
// money and no completion credit, §4.7). primary_team, when set, mirrors the
// legacy scalar wallet fields onto that team's merged totals.
int apply_networked_win_shares(SaveData& save, const NetWinFoldCapture& capture,
                               std::span<const std::uint8_t> own_player_indices,
                               std::optional<std::size_t> primary_team);

// This machine's total cash share across every team — the prospective payout
// the results screen shows before the fold runs.
std::uint64_t machine_cash_share(const NetWinFoldCapture& capture,
                                 std::span<const std::uint8_t> own_player_indices);

// SDL-free full networked-win persist shared by the SDL local transport and the
// curses networked client. Loads the company baseline from `slot`, merges this
// machine's owned roster from the world, overlays baseline + share on the
// wallets, marks the finished level completed only when this machine deployed
// >= 1 character (§4.7), advances the private cursor unconditionally, and
// autosaves through the LevelWin choke point. `session` carries the negotiated
// keep_fallen rule, the campaign/cursor to persist, and this peer's completion
// history. Returns false on load or save failure (a load failure clobbers
// nothing). own_player_indices empty ⇒ spectator: cursor-only, no roster/money.
[[nodiscard]] bool persist_networked_win(
    const std::string& slot, const SaveData& session, const GameWorld& world,
    std::span<const std::uint8_t> own_player_indices,
    std::optional<std::size_t> primary_team,
    const NetWinFoldCapture& capture, int completed_level);

} // namespace og::progression
